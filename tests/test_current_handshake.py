import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, call

from backend.current_handshake import discover_current_boot, negotiate_current_session
from backend.current_sessions import initialize, reserve


class CurrentHandshakeTests(unittest.TestCase):
    boot = "0123456789abcdef" * 2
    other_boot = "fedcba9876543210" * 2

    def test_discovery_returns_the_exact_token(self):
        rpc = Mock(return_value=self.boot)
        self.assertEqual(discover_current_boot(rpc), self.boot)
        rpc.assert_called_once_with("hvac_current_boot")

    def test_discovery_rejects_unavailable_or_invalid_tokens(self):
        for reply in ("", "0" * 32, self.boot + "\n", self.boot.upper(), None):
            with self.subTest(reply=reply), self.assertRaises(ValueError):
                discover_current_boot(Mock(return_value=reply))

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.ledger = Path(directory.name) / "sessions.db"
        self.authority = initialize(self.ledger)

    def negotiate(self, exchange, boot=None):
        return negotiate_current_session(
            exchange, self.boot if boot is None else boot, self.ledger, self.authority
        )

    def test_exact_two_step_exchange_after_persistent_reservation(self):
        def exchange(request):
            if request == f"1 O {self.boot} 1":
                self.assertEqual(reserve(self.ledger, self.authority), "2")
                return f"1 A {self.boot} 1"
            self.assertEqual(request, f"1 C {self.boot} 1")
            return f"1 K {self.boot} 1"

        transport = Mock(side_effect=exchange)
        self.assertEqual(self.negotiate(transport), "1")
        self.assertEqual(transport.call_args_list, [
            call(f"1 O {self.boot} 1"), call(f"1 C {self.boot} 1")
        ])

    def test_offer_reply_rejection_prevents_confirmation(self):
        for session, reply in enumerate((
            f"1 A {self.other_boot} 1",
            f"1 A {self.boot} 1",  # Stale reservation.
            f"1 A {self.boot} 3\0",
            f"1 A {self.boot} 4".encode("ascii"),
            "x" * 1024,
            f"1 K {self.boot} 6",
        ), 1):
            with self.subTest(session=session):
                transport = Mock(return_value=reply)
                with self.assertRaises(ValueError):
                    self.negotiate(transport)
                transport.assert_called_once_with(f"1 O {self.boot} {session}")
        self.assertEqual(reserve(self.ledger, self.authority), "7")

    def test_confirmation_failure_consumes_reservation(self):
        timeout = TimeoutError("Confirmation reply lost")
        transport = Mock(side_effect=[f"1 A {self.boot} 1", timeout])
        with self.assertRaises(TimeoutError):
            self.negotiate(transport)
        self.assertEqual(transport.call_count, 2)
        transport = Mock(side_effect=[f"1 A {self.boot} 2", f"1 K {self.other_boot} 2"])
        with self.assertRaises(ValueError):
            self.negotiate(transport)
        self.assertEqual(reserve(self.ledger, self.authority), "3")

    def test_bad_boot_or_ledger_never_sends_offer(self):
        transport = Mock()
        for boot in ("0" * 32, self.boot.upper(), self.boot[:-1], self.boot.encode("ascii")):
            with self.subTest(boot=boot), self.assertRaises(ValueError):
                self.negotiate(transport, boot)
        with self.assertRaises(ValueError):
            negotiate_current_session(transport, self.boot, self.ledger, "0" * 32)
        transport.assert_not_called()
        self.assertEqual(reserve(self.ledger, self.authority), "1")

"""Host negotiation for a single current session over a caller-owned link."""

from .current_sessions import reserve


def _checked_boot_token(boot_token):
    if (
        type(boot_token) is not str
        or len(boot_token) != 32
        or boot_token == "0" * 32
        or any(char not in "0123456789abcdef" for char in boot_token)
    ):
        raise ValueError("Expected a nonzero, lowercase hexadecimal MCU boot token")
    return boot_token


def discover_current_boot(call):
    """Read the boot token over the caller's serialized, bounded RPC connection."""
    return _checked_boot_token(call("hvac_current_boot"))


def negotiate_current_session(exchange, boot_token, ledger, authority):
    """Return the reserved session only after both MCU replies match.

    The caller supplies a discovered boot token and a serialized, deadline-
    bounded exchange callable for that connection. On failure the caller must
    keep current unavailable and retire the connection before negotiating again.
    """
    _checked_boot_token(boot_token)

    session = reserve(ledger, authority)
    binding = f"{boot_token} {session}"
    for command, acknowledgement in (("O", "A"), ("C", "K")):
        expected = f"1 {acknowledgement} {binding}"
        reply = exchange(f"1 {command} {binding}")
        if type(reply) is not str or len(reply) != len(expected) or reply != expected:
            raise ValueError(f"MCU current session reply did not match {acknowledgement}")
    return session

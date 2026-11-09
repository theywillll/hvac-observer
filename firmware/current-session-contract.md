# Current-session acceptance contract

`CurrentSessionStore` is an optional, fixed-storage C++ boundary around
`CurrentSnapshotStore`. It is exercised in the native and UNO Q self-tests;
it is **not connected to Bridge, the environmental sketch, or an ADC worker**.
It implements the state portion of a future handshake, not a complete handshake.

## One serialized owner per MCU boot

All calls require the same external serialization, including clock reads,
configuration, confirmations, worker publication and payload serialization.
The owner is noncopyable. Do not reconstruct it to clear a clock fault or reset
the session counter during a boot. It does not own a mutex, thread, clock or ADC.
Use the checked uptime reader for bookkeeping only; qualified sample timestamps
still need a common, verified time domain. A failed clock read must call
`clockFailed()` rather than passing its cleared zero output as a valid timestamp.

| Event | Result |
| --- | --- |
| Construction | No current publication or payload |
| `offer(id, policy, now)` | Revoke publication and clear pending state first |
| Valid newer offer | Reset all snapshot channels; wait for confirmation |
| `confirm(id, now)` matches pending ID | Enable publication; no readings invented |
| Duplicate confirmation of active ID | Idempotent success; no reset |
| Old/wrong confirmation | Rejected; cannot activate or reset another session |
| `stop()` | Revoke publication; retain high-water ID |
| Clock regression or `clockFailed()` | Sticky failure; no new session can recover |

IDs must be positive, no larger than 2^63-1 (the host ledger limit), and
strictly greater than every consumed ID in this owner. A valid newer ID is
consumed even when its policy is invalid. This prevents reuse after failed
configuration. Zero/out-of-range IDs are rejected without advancing the counter.
Every offer, including repeated/stale/invalid offers, stops publication. This is
deliberately conservative: do not retry offers as though they were idempotent.
Allocate a fresh host reservation to recover; delayed confirmations are harmless.
Counter exhaustion has no wraparound recovery.

Windows whose supplied start/end timestamps predate confirmation are rejected;
duplicate confirmations do not move that boundary. This relies on honest,
qualified sample stamps and a common time domain, not caller completion times.
`publish()` remains subject to all existing timing, sequence, clipping,
calibration and age gates. Only an active session can publish. `write()` takes
a fresh snapshot internally; an active session with no good samples produces
null currents, and expiry still revokes old numbers. Before confirmation or
after stop/fault, it returns false and clears any writable output buffer. The
caller must omit that optional payload on failure, never reuse previous bytes.

## Remaining transport and reboot work

A future transport must send an acceptance reply only after `offer()` succeeds,
and the host must echo the accepted reservation in a confirmation only after
receiving that reply. Do not acknowledge a merely queued configuration.
The transport must validate bounded input and policy, invalidate the host's
live-current projection while negotiating, and stop on disconnect/timeout.
It must flush queued acquisition work when changing sessions and start new
acquisition only after confirmation; this class cannot identify a preconfirmation
sample whose caller fabricated valid stamps. No pending timeout is built in.

### Version 1 command format

`applyCurrentSessionCommand` handles a bounded ASCII request for one serialized
store and a caller-supplied boot token. The four fields are separated by exactly
one space. The token is 32 lowercase hex digits and must be nonzero. The
session is canonical decimal in the host ledger range, 1 through 2^63-1.
The entire request is 38–56 bytes, excluding the C terminator.

| Direction | Message | Meaning |
| --- | --- | --- |
| Host → MCU | `1 O <boot> <session>` | Offer a reservation with the MCU's local policy |
| MCU → host | `1 A <boot> <session>` | Offer accepted and waiting for confirmation |
| Host → MCU | `1 C <boot> <session>` | Confirm the exact offer acknowledged by the MCU |
| MCU → host | `1 K <boot> <session>` | Store confirmed the session |

Supply the exact transport byte count and a reply buffer with at least 57 bytes
for the longest reply and its terminator. Malformed input, a different token
or insufficient reply capacity leaves the store untouched. A well-formed offer
for this boot reaches the store and follows its revocation rules, even when
the store rejects the session or policy. Failure clears a writable reply
buffer. If a reply is lost, a duplicate confirmation can succeed, while
offer recovery requires a fresh reservation.

The token check depends on a fresh token held fixed for the life of the
store. The UNO Q self-test now initializes a boot token using Zephyr's
[`sys_csrand_get()`](https://docs.zephyrproject.org/latest/services/crypto/random/index.html).
The pinned core 1.0.0 UNO Q configuration selects the hardware-device
CSPRNG and STM32 entropy driver, and exports the syscall implementation to
the sketch. The adapter requires both configuration flags. It requests 16
bytes once and encodes them as 32 lowercase hexadecimal digits. An RNG error
or all-zero result leaves the token empty for that owner lifetime. There is
no timer-derived fallback. Use the token for stale-message separation only.

Production discovery and Bridge RPCs remain pending. The production owner
must hold one token and store for the MCU boot, suppress discovery when
initialization fails, and handle MCU reset, host restart, lost replies,
delayed RPCs and disconnects. The compiled self-test reports success or
failure without printing the token. A board run is needed to check actual
entropy availability and token changes across resets.

The host helper `backend.current_handshake.negotiate_current_session` commits
a fresh ledger reservation before sending the offer. It requires the exact
ASCII reply text for that boot token, command phase and reservation before
continuing. It returns the session only after the matching confirmation reply.
The connection owner supplies a discovered token and a serialized exchange
with transport deadlines. A failed exchange consumes its reservation. Keep
live current unavailable and retire the connection on failure, since the MCU
could have applied the command before its reply was lost. Production
acquisition still needs to call this helper and pass its result to
`SnapshotReceiver` as the expected session.

## Verification and next increment

Shared synthetic tests cover inactive startup, zero/out-of-range IDs, offers,
confirmation mismatch/duplication, actual valid synthetic current publication,
replacement clearing, rejection of old-session samples, ordinary age expiry,
stop, invalid-policy consumption, duplicate-offer revocation, clock regression,
explicit failed clock reads and the 2^63-1 boundary. They run through
`tests/current_block_test.cpp` and compile into `current_block_selftest`.
This is software-contract evidence, not a board reset, transport or sampling test.

The command tests use two synthetic boot tokens. They cover a delayed offer
and a delayed confirmation carrying the same session ID as the new pending
offer. They also cover malformed and overflowing IDs, embedded null bytes,
reply capacity, and the largest ledger ID. Host tests use a real temporary
ledger and a simulated connection to exercise reply matching, timeout and
reservation consumption. Next is the production owner and discovery transport.

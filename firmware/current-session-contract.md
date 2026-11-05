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

**There is no boot identity or authentication here.** A newly constructed owner
has no knowledge of IDs from previous MCU boots. Before production use, design
a boot/connection challenge and stale-message rejection with the host ledger
authority, then test MCU reset, host restart, lost replies, delayed RPCs and
disconnects end to end. Session IDs alone do not authenticate a sender or prove
a new boot. No Bridge RPC or host confirmation caller exists yet.

## Verification and next increment

Shared synthetic tests cover inactive startup, zero/out-of-range IDs, offers,
confirmation mismatch/duplication, actual valid synthetic current publication,
replacement clearing, rejection of old-session samples, ordinary age expiry,
stop, invalid-policy consumption, duplicate-offer revocation, clock regression,
explicit failed clock reads and the 2^63-1 boundary. They run through
`tests/current_block_test.cpp` and compile into `current_block_selftest`.
This is software-contract evidence, not a board reset, transport or sampling test.

Next: specify and test the bounded host/MCU message exchange and boot binding
before adding configuration RPCs. No live current acquisition is enabled.

# Host-owned persistent current sessions

The Python service now reserves optional current-session IDs from a dedicated
local SQLite ledger. The UNO receiver checks an independently supplied expected
ID instead of trusting whichever nonnull current session appears first. MCU
current production, session negotiation and extended-clock ownership are still
not connected. This increment performs no board configuration or hardware I/O.

## Ledger lifecycle

`backend/current_sessions.py` separates explicit initialization from allocation:

- `initialize(path)` creates a fresh file exclusively, with schema 1, an authority
  UUID and high-water counter zero. Existing files, including empty or corrupt
  files, are refused. Failure leaves an unusable file for reviewed recovery;
  there is no automatic deletion, reset or replacement.
- `reserve(path, authority)` opens an existing file with SQLite `mode=rw`, checks
  the application/schema identifiers, integrity result, singleton state and
  expected authority, then increments the counter inside `BEGIN IMMEDIATE`.
  It returns the decimal ID only after commit succeeds.
- Missing files, a wrong authority, corruption, lock timeout, invalid state or
  exhaustion return no ID. A committed reservation remains consumed even if the
  response is lost or the session never starts. Gaps are harmless; reuse is not.

Allocation uses DELETE journal mode and verifies `synchronous=EXTRA` on every
connection. SQLite documents that [IMMEDIATE transactions obtain the write
transaction at their start](https://www.sqlite.org/lang_transaction.html), and
[EXTRA adds directory synchronization after rollback-journal deletion](https://www.sqlite.org/pragma.html#pragma_synchronous).
These settings rely on SQLite's [filesystem/locking/flush assumptions](https://www.sqlite.org/atomiccommit.html).
Process-exit tests are not power-cut or storage-controller qualification.

The allocator uses positive SQLite signed integers, **1 through 2^63-1**, a
subset of the existing uint64 wire contract. It never wraps. Exhaustion requires
an explicitly reviewed migration, not a new empty file. The schema's UUID is a
host-side namespace pin, not a secret or a cryptographic device identity.

Use a dedicated, protected **local filesystem** ledger, separate from the
measurement/history database. Network shares, cloned ledgers, backup rollback,
manual counter edits and deletion/reinitialization can defeat monotonicity and
are unsupported. The code cannot detect restoration of an older valid copy with
the same authority UUID. Preserve the database and any recovery journal under
their original names; do not remove journals as cleanup after a failure.

One transaction owns each allocation, but the ledger is **not an exclusive lease
on physical equipment**. Separate processes can reserve distinct IDs concurrently.
Single active board ownership, persistent MCU boot identity, a fresh handshake
and installation/device binding still need implementation/review. IDs are unique
only within the retained ledger's allocation history, not globally across hosts.

## Service and receiver boundary

The service accepts paired optional flags:

```text
--source uno
--current-session-ledger PATH
--current-session-authority AUTHORITY
```

It reserves once per service start, before acquisition, and supplies the ID to
`frames(..., current_session=id)` and `SnapshotReceiver(expected_session=id)`.
Ledger flags are rejected for other sources or if the ledger path resolves to
the measurement database. Startup/configuration failure may consume an ID;
the service must never reclaim that ID to avoid gaps.

Any **nonnull** incoming current session, including a pending/all-null current
payload, must match the host's reservation. Missing/mismatched expected IDs are
rejected before receiver state commits. Legacy environment-only frames and
unconfigured null-session current metadata remain accepted without a reservation.
After a current session is pinned, the existing reset/disappearance, policy,
calibration, ordinal and receipt-deadline checks still apply.

The expected ID must come from the allocator, not from reading the incoming
payload. Synthetic fixtures supply known literal IDs only to exercise this
contract; they are not real ledger reservations or board evidence. In-process
code can misuse the public APIs, so this boundary is not authentication against
local code or an untrusted device forging an expected number.

The current sketch has no session setter/acknowledgement RPC. This increment
does not add one, send a reserved ID to a board, or claim the MCU adopted it.
The receiver expects matching data; a later reviewed handshake must establish
that match and bind it to the correct boot/clock before enabling acquisition.
Without ledger flags, the environment-only UNO path continues to work, but any
configured nonnull current session fails closed rather than being learned.

## Explicit setup tool

For a **fresh installation only**, this command creates a new ledger and prints
its authority UUID as JSON:

```sh
python -m scripts.current_sessions init /absolute/local/path/current-sessions.sqlite3
```

Record the printed authority separately and protect the ledger. A diagnostic
reservation is possible with the command below, but permanently consumes an ID;
do not cache/reuse it or run this merely to make a nightly commit appear active.

```sh
python -m scripts.current_sessions reserve /absolute/local/path/current-sessions.sqlite3 --authority AUTHORITY
```

No production ledger was initialized during this development increment. Tests
create temporary local ledgers only. Runtime `*.sqlite3*` files remain ignored
by Git. There is no ledger reset/import/force option or automatic recovery policy.

## Validation and remaining work

Tests cover reopen persistence, exclusive initialization, missing/empty/foreign/
corrupt files, wrong authorities/schema, invalid state, exhaustion, lock timeout
and six simultaneous allocator processes. Abrupt process exits before commit
and after a committed unused reservation demonstrate rollback/gap behavior on
the test filesystem. They do not simulate a physical power failure.

Receiver tests reject old/missing expected IDs. A fake-Bridge test exercises the
actual adapter with a reserved ID and confirms its only RPC is `hvac_snapshot`.
CLI tests reject foreign sources and measurement-database collisions. The existing
live-expiry, dashboard, C++/Python fixture and pinned UNO Q compile checks remain.
See [session-build-record.json](session-build-record.json) and commit workflows
for evidence. No upload, real RPC capture, sampling or calibrated result occurred.

Next: select/test the MCU's monotonic 64-bit clock owner and explicit boot/session
handshake semantics, then integrate the synchronized passive worker and bounded
router envelope behind unqualified defaults. Actual aperture timing, clock
margins, CT calibration and accuracy still need protected board/reference tests.

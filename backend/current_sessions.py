"""Explicitly initialized host session ledger. No MCU handshake or boot-ID claim."""
from contextlib import closing
from pathlib import Path
import os
import sqlite3
import uuid

APPLICATION_ID = 0x48565345
SCHEMA = 1
MAX_SESSION = (1 << 63) - 1  # Positive SQLite INTEGER subset of the uint64 wire contract.


def _authority(value):
    return type(value) is str and len(value)==32 and all(c in '0123456789abcdef' for c in value)


def _connect(path, timeout):
    path=Path(path).absolute()
    if path.is_symlink():raise ValueError('Session ledger must not be a symlink')
    # mode=rw never creates a missing ledger. No in-memory or URI shortcuts.
    db=sqlite3.connect(path.as_uri()+'?mode=rw',uri=True,timeout=timeout,isolation_level=None)
    try:
        db.execute('PRAGMA synchronous=EXTRA')
        if db.execute('PRAGMA synchronous').fetchone()[0]!=3:
            raise ValueError('Session ledger requires synchronous=EXTRA')
        if db.execute('PRAGMA journal_mode').fetchone()[0]!='delete':
            raise ValueError('Session ledger requires DELETE journal mode')
    except BaseException:
        db.close();raise
    return db


def initialize(path):
    """Fresh installation only. Existing files, even empty/corrupt ones, are refused.

    An interrupted initialization leaves an unusable file for reviewed recovery;
    it is never silently deleted or retried as a fresh counter.
    """
    path=Path(path).absolute()
    fd=os.open(path,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
    os.close(fd)
    authority=uuid.uuid4().hex
    with closing(_connect(path,5)) as db:
        db.execute('BEGIN IMMEDIATE')
        try:
            db.execute(f'PRAGMA application_id={APPLICATION_ID}')
            db.execute(f'PRAGMA user_version={SCHEMA}')
            db.execute('''CREATE TABLE session_authority (
                singleton INTEGER PRIMARY KEY CHECK(singleton=1),
                authority TEXT NOT NULL,
                last_session INTEGER NOT NULL CHECK(typeof(last_session)='integer'
                    AND last_session>=0 AND last_session<=9223372036854775807))''')
            db.execute('INSERT INTO session_authority VALUES(1,?,0)',(authority,))
            db.execute('COMMIT')
        except BaseException:
            if db.in_transaction:db.execute('ROLLBACK')
            raise
    return authority


def reserve(path, authority, *, timeout=5):
    """Return a new canonical decimal ID only AFTER the increment commits.

    One allocation transaction at a time. A consumed but unused ID stays consumed;
    storage errors/exhaustion never fall back to a remembered or zero session.
    Protect the local ledger against deletion, replacement and backup rollback.
    """
    if not _authority(authority):raise ValueError('Expected canonical ledger authority required')
    with closing(_connect(path,timeout)) as db:
        db.execute('BEGIN IMMEDIATE')
        try:
            if (db.execute('PRAGMA application_id').fetchone()[0]!=APPLICATION_ID or
                    db.execute('PRAGMA user_version').fetchone()[0]!=SCHEMA):
                raise ValueError('Unknown session ledger format')
            if db.execute('PRAGMA quick_check').fetchall()!=[('ok',)]:
                raise ValueError('Session ledger failed integrity check')
            rows=db.execute('SELECT singleton,authority,last_session FROM session_authority').fetchall()
            if (len(rows)!=1 or rows[0][0]!=1 or rows[0][1]!=authority or
                    type(rows[0][2]) is not int or not 0<=rows[0][2]<=MAX_SESSION):
                raise ValueError('Invalid session ledger state/authority')
            last=rows[0][2]
            if last==MAX_SESSION:raise ValueError('Session ledger exhausted; no wrap or automatic reset')
            next_session=last+1
            db.execute('UPDATE session_authority SET last_session=? WHERE singleton=1',(next_session,))
            db.execute('COMMIT')
        except BaseException:
            if db.in_transaction:db.execute('ROLLBACK')
            raise
    return str(next_session)


def reserve_for_source(source, ledger=None, authority=None):
    if ledger is None and authority is None:return None
    if source!='uno' or not ledger or not authority:
        raise ValueError('Session ledger and authority must be paired with --source uno')
    return reserve(ledger,authority)

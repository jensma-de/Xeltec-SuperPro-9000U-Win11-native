"""Write Python/build events to the same append-only log as the Windows tools."""
from datetime import datetime, timezone
from pathlib import Path
import msvcrt
import os

ROOT=Path(__file__).resolve().parent.parent
LOG_PATH=ROOT/'Windows11-Native/SP9000U/bin/superpro9000u.log'

def write_log(level,component,message,error=0):
    LOG_PATH.parent.mkdir(parents=True,exist_ok=True)
    timestamp=datetime.now(timezone.utc).isoformat(timespec='milliseconds').replace('+00:00','Z')
    records=''.join(
        f'{timestamp} [{level}] [{component}] pid={os.getpid()} tid=0 error={error} {line}\r\n'
        for line in (str(message).splitlines() or ['']))
    with LOG_PATH.open('a+b',buffering=0) as stream:
        stream.seek(0)
        msvcrt.locking(stream.fileno(),msvcrt.LK_LOCK,1)
        try:
            stream.write(records.encode('utf-8'))
        finally:
            stream.seek(0)
            msvcrt.locking(stream.fileno(),msvcrt.LK_UNLCK,1)

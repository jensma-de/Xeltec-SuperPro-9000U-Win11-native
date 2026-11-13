"""Merge the previous custom logs into the shared programmer log once."""
from pathlib import Path
import hashlib
import re
from logging_support import ROOT, LOG_PATH, write_log

SOURCES=[
    ROOT/'Windows11-Native/SP9000U/bin/xeltek-winusb.log',
    ROOT/'Windows11-Native/usb-install.log',
    ROOT/'Windows11-Native/build-log.txt',
]
TRANSLATIONS={
    'Gefunden:':'Device found:',
    'Treiber:':'Driver:',
    '(keiner)':'(none)',
    'Windows-Treiber:':'Windows driver:',
    'Abschnitt:':'section:',
    'Anbieter:':'provider:',
    'Microsoft WinUSB installiert. Neustart erforderlich: nein':'Microsoft WinUSB installed. Computer restart required: no',
    'Microsoft WinUSB installiert. Neustart erforderlich: ja':'Microsoft WinUSB installed. Computer restart required: yes',
    'WinUSB-Schnittstelle registriert; USB-Geraet neu gestartet.':'WinUSB interface registered; USB device restarted.',
    'WinUSB-Schnittstelle registriert. USB-Kabel jetzt abziehen und erneut einstecken.':'WinUSB interface registered. Disconnect and reconnect the USB cable.',
    'Einrichtung fehlgeschlagen, Windows-Fehler':'Setup failed. Windows error',
}

def main():
    existing=LOG_PATH.read_text(encoding='utf-8') if LOG_PATH.exists() else ''
    for source in SOURCES:
        source=source.resolve()
        if not source.is_relative_to(ROOT.resolve()):raise ValueError('Source is outside the workspace')
        if not source.exists():continue
        data=source.read_bytes();digest=hashlib.sha256(data).hexdigest()
        marker=f'Legacy import completed; sha256={digest}'
        if marker not in existing:
            text=data.decode('utf-8-sig',errors='replace')
            if data.startswith(b'\xff\xfe'):text=data.decode('utf-16')
            write_log('INFO','migration',f'Importing {source.name}; original timestamps are retained only where present.')
            for line in text.splitlines():
                match=re.fullmatch(r'(\d+) (\S+) code=([\da-fA-F]+) len=(\d+) result=(\d+) error=(\d+)',line)
                if match:
                    tick,operation,code,length,result,error=match.groups()
                    write_log('ERROR' if int(error) else 'INFO','legacy-usb-bridge',
                              f'tick_ms={tick} operation={operation} code=0x{code} length={length} result={result}',int(error))
                else:
                    for old,new in TRANSLATIONS.items():line=line.replace(old,new)
                    write_log('ERROR' if re.search(r'error=[1-9]|failed|FAIL',line,re.I) else 'INFO',
                              'legacy-output',line)
            write_log('INFO','migration',marker)
        source.unlink()
        print(f'Merged and removed {source.relative_to(ROOT)}')

if __name__=='__main__':main()

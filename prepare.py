"""Prepare the vendor software for the supplied native tools; no compiler or pip packages required."""
from pathlib import Path
import argparse
import hashlib
import io
import shutil
import sys
import tempfile
import traceback
import zipfile

from extract_xeltek import extract_dat
from native import logging_support as log
from native.pe_patch import patch_application, EXPECTED_DRIVER

ROOT=Path(__file__).resolve().parent
CUSTOM_FILES=['Setup-USB.exe','Start-SUPERPRO.exe','XeltekUsbSetup.exe','SP9000U/bin/xusb32.dll']
ARCHIVES=[('USBBIN.ZIP','bin'),('USBLIB.ZIP','lib'),('USBALGO9.ZIP','algo9')]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',type=Path,help='Original Xeltek nsp9kw.exe or ZIP archive')
    parser.add_argument('--output',type=Path,default=ROOT/'Windows11-Native')
    args=parser.parse_args()
    if sys.version_info<(3,10):raise ValueError('Python 3.10 or newer is required.')
    output=args.output.resolve()
    log.LOG_PATH=output/'SP9000U/bin/superpro9000u.log'
    log.write_log('INFO','prepare','Software preparation started. No driver installation or device operation is performed.')
    archive=args.archive
    if archive is None:
        candidates=[ROOT/'downloads/nsp9kw.exe',ROOT/'downloads/nsp9kw-202005261414.zip']
        present=[p for p in candidates if p.is_file()]
        if len(present)!=1:
            raise ValueError('Place exactly one vendor package in downloads/: nsp9kw.exe OR nsp9kw-202005261414.zip. See README.md.')
        archive=present[0]
    archive=archive.resolve()
    if not archive.is_file():raise FileNotFoundError(f'Vendor package not found: {archive}')
    for relative in CUSTOM_FILES:
        if not (ROOT/'Windows11-Native'/relative).is_file():
            raise FileNotFoundError(f'Missing supplied compatibility tool: Windows11-Native/{relative}')
    log.write_log('INFO','prepare',f'Reading vendor archive: {archive.name}')
    print(f'Reading {archive.name}...',flush=True)
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='superpro-prepare-',dir=output.parent) as temporary:
        staging=Path(temporary)
        with zipfile.ZipFile(archive) as outer:
            members={Path(name).name.upper():name for name in outer.namelist() if not name.endswith('/')}
            for name,folder in ARCHIVES:
                print(f'Extracting {name}...',flush=True)
                if name not in members:raise ValueError(f'Vendor archive is missing {name}')
                with zipfile.ZipFile(io.BytesIO(outer.read(members[name]))) as inner:
                    entries=inner.namelist()
                    if len(entries)!=1:raise ValueError(f'Expected one DAT file in {name}')
                    records=extract_dat(inner.read(entries[0]),staging/folder)
                log.write_log('INFO','prepare',f'Validated and extracted {name}: {len(records)} files.')
        source=staging/'bin/sp9000.exe'
        original=source.read_bytes()
        driver=next((p for p in (staging/'bin').iterdir() if p.name.lower()=='xeusb.sys'),None)
        if driver is None or hashlib.sha256(driver.read_bytes()).hexdigest()!=EXPECTED_DRIVER:
            raise ValueError('Unsupported original XEUSB.SYS version.')
        patched=patch_application(original)
        print('Original application verified. Writing the prepared software...',flush=True)
        for protected in ['xusb32.dll','sp9000-win11.exe','superpro9000u.log']:
            if any(p.name.lower()==protected for p in (staging/'bin').iterdir()):
                raise ValueError(f'Vendor package contains a reserved compatibility filename: {protected}')
        # Only the verified, fully extracted vendor data is copied into the package.
        for _,folder in ARCHIVES:
            shutil.copytree(staging/folder,output/'SP9000U'/folder,dirs_exist_ok=True)
        for relative in CUSTOM_FILES:
            source_tool=ROOT/'Windows11-Native'/relative
            destination=output/relative
            if source_tool.resolve()!=destination.resolve():
                destination.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(source_tool,destination)
        (output/'SP9000U/bin/sp9000-win11.exe').write_bytes(patched)
    log.write_log('INFO','prepare',f'Software prepared successfully: {output}; patched_sha256={hashlib.sha256(patched).hexdigest()}')
    print(f'Software prepared successfully: {output}')
    print('Connect the programmer, run Setup-USB.exe once, then use Start-SUPERPRO.exe.')
    print(f'Log: {log.LOG_PATH}')

if __name__=='__main__':
    try:main()
    except Exception as error:
        log.write_log('ERROR','prepare',f'Preparation failed: {error}\n{traceback.format_exc()}')
        print(f'ERROR: {error}',file=sys.stderr)
        sys.exit(1)

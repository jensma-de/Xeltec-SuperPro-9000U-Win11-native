"""Build a local WinUSB compatibility layer; preserve the supplied executable."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import pefile
import re
import uuid
import traceback
from logging_support import write_log, LOG_PATH

ROOT=Path(__file__).resolve().parent.parent
EXPECTED_DRIVER='c34392c3c466dddb7f0a29b13251939331444f4b95365d000ff96ee921cbdcf7'
HOOKS={'CreateFileA':'XCreateFileA@28','CloseHandle':'XCloseHandle@4',
       'DeviceIoControl':'XDeviceIoControl@32'}

def run(*args, capture_log=True, allowed=(0,)):
    command=' '.join(str(a) for a in args)
    print(command,flush=True)
    write_log('INFO','build',f'Running: {command}')
    result=subprocess.run([str(a) for a in args],stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT,encoding='utf-8',errors='replace')
    if result.stdout:
        print(result.stdout,end='',flush=True)
        if capture_log:write_log('INFO' if result.returncode in allowed else 'ERROR','build',result.stdout)
    write_log('INFO' if result.returncode==0 else 'WARNING' if result.returncode in allowed else 'ERROR',
              'build',f'Command finished; exit_code={result.returncode}')
    if result.returncode not in allowed:raise subprocess.CalledProcessError(result.returncode,args)
    return result


def check_concurrent_logging(out):
    token=uuid.uuid4().hex
    workers=[subprocess.Popen([str(out/f'Logging-SelfTest-{arch}.exe'),f'{token}-{index}'])
             for index,arch in enumerate(['x86','x64']*3)]
    for worker in workers:
        if worker.wait()!=0:raise RuntimeError('Concurrent logging worker failed')
    text=LOG_PATH.read_text(encoding='utf-8')
    for index in range(6):
        sequences=re.findall(rf'token={token}-{index} sequence=(\d+)(?: \|[^\r\n]*)?\r?$',text,re.MULTILINE)
        if sorted(map(int,sequences))!=list(range(100)):
            raise RuntimeError('Concurrent logging lost, duplicated or interleaved records')
    if 'UTF-8 text test: café' not in text:raise RuntimeError('UTF-8 logging failed')
    write_log('INFO','build','PASS: 600 concurrent x86/x64 appends, UTF-8, severity and last-error preservation.')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--toolchain',type=Path,required=True)
    options=parser.parse_args()
    cc32=options.toolchain/'bin/i686-w64-mingw32-clang.exe'
    cc64=options.toolchain/'bin/x86_64-w64-mingw32-clang.exe'
    out=ROOT/'Windows11-Native'
    out.mkdir(exist_ok=True)
    write_log('INFO','build','Build session started.')
    # Clone the complete library, so the offline installation stays usable.
    shutil.copytree(ROOT/'SP9000U',out/'SP9000U',dirs_exist_ok=True)
    source=ROOT/'SP9000U/bin/sp9000.exe'
    original=source.read_bytes()
    original_hash=hashlib.sha256(original).hexdigest()
    manifest=json.loads((ROOT/'Analysis/extracted-manifest.json').read_text())
    archived=next(r['sha256'] for r in manifest['bin'] if r['name']=='sp9000.exe')
    if original_hash!=archived:
        raise ValueError('Application differs from supplied archive')
    if hashlib.sha256((ROOT/'SP9000U/bin/XEUSB.SYS').read_bytes()).hexdigest()!=EXPECTED_DRIVER:
        raise ValueError('Driver differs from analyzed version')
    pe=pefile.PE(data=original)
    kernel=next(lib for lib in pe.DIRECTORY_ENTRY_IMPORT if lib.dll.lower()==b'kernel32.dll')
    names=[i.name.decode('ascii') for i in kernel.imports]
    if not set(HOOKS).issubset(names):raise ValueError('Expected USB imports absent')
    def_file=ROOT/'native/kernel-forwarders.def'
    def_file.write_text('LIBRARY xusb32\nEXPORTS\n'+''.join(
        f'  {name}={HOOKS[name] if name in HOOKS else "KERNEL32."+name}\n'
        for name in names),encoding='ascii')
    target=out/'SP9000U/bin'
    log_source=ROOT/'native/src/log.c'
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-D_WIN32_WINNT=0x0601']
    run(cc32,*flags,'-shared',ROOT/'native/src/bridge.c',ROOT/'native/src/transport.c',log_source,
        def_file,'-lsetupapi','-lwinusb','-o',target/'xusb32.dll')
    run(cc64,*flags,'-municode',ROOT/'native/src/setup.c',ROOT/'native/src/transport.c',log_source,
        '-lsetupapi','-lnewdev','-ladvapi32','-o',out/'XeltekUsbSetup.exe')
    run(cc64,*flags,'-mwindows',ROOT/'native/src/launcher.c',log_source,'-lshell32',
        '-o',out/'Start-SUPERPRO.exe')
    run(cc64,*flags,'-mwindows','-DUSB_SETUP_LAUNCHER',ROOT/'native/src/launcher.c',log_source,'-lshell32',
        '-o',out/'Setup-USB.exe')
    run(cc32,*flags,ROOT/'native/tests/transport_test.c',ROOT/'native/src/transport.c',log_source,
        '-o',out/'Transport-SelfTest-x86.exe')
    run(cc64,*flags,ROOT/'native/tests/transport_test.c',ROOT/'native/src/transport.c',log_source,
        '-o',out/'Transport-SelfTest-x64.exe')
    run(cc32,*flags,ROOT/'native/tests/bridge_test.c',log_source,'-o',out/'Bridge-SelfTest-x86.exe')
    for arch,compiler in [('x86',cc32),('x64',cc64)]:
        run(compiler,*flags,ROOT/'native/tests/log_test.c',log_source,'-o',out/f'Logging-SelfTest-{arch}.exe')
    check_concurrent_logging(out)
    run(out/'Transport-SelfTest-x86.exe',capture_log=False)
    run(out/'Transport-SelfTest-x64.exe',capture_log=False)
    run(out/'Bridge-SelfTest-x86.exe',capture_log=False)
    run(out/'XeltekUsbSetup.exe','--check-driver',capture_log=False)
    selection_test=run(out/'XeltekUsbSetup.exe','--selftest-selection',capture_log=False,allowed=(0,77))
    if selection_test.returncode not in (0,77):
        raise RuntimeError('Device driver selection regression test failed')
    # Change only the KERNEL32 import-library name. All unaffected functions are
    # forwarded to the real KERNEL32. No code or firmware bytes are patched.
    offset=pe.get_offset_from_rva(kernel.struct.Name)
    replacement=b'xusb32.dll\0'
    patched=bytearray(original)
    patched[offset:offset+len(kernel.dll)+1]=replacement.ljust(len(kernel.dll)+1,b'\0')
    patched_pe=pefile.PE(data=bytes(patched))
    patched_pe.OPTIONAL_HEADER.CheckSum=patched_pe.generate_checksum()
    patched_pe.write(str(target/'sp9000-win11.exe'))
    dll=pefile.PE(str(target/'xusb32.dll'))
    exports={e.name.decode():e for e in dll.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
    for name in names:
        if name not in exports:raise ValueError(f'Missing proxy export: {name}')
        if name not in HOOKS and exports[name].forwarder!=('KERNEL32.'+name).encode():
            raise ValueError(f'Unexpected forwarder: {name}')
    summary=f'Validated {len(names)} proxy exports; original SHA256 {original_hash}'
    print(summary)
    write_log('INFO','build',summary)
    report_path=out/'build-report.json'
    if report_path.exists() and not (out/'hardware-verification.json').exists():
        previous=json.loads(report_path.read_text(encoding='utf-8'))
        if previous.get('last_verified_date'):
            (out/'hardware-verification.json').write_text(json.dumps(previous,indent=2),encoding='utf-8')
    report={
        'original_application_sha256':original_hash,
        'analyzed_driver_sha256':EXPECTED_DRIVER,
        'patched_application_sha256':hashlib.sha256((target/'sp9000-win11.exe').read_bytes()).hexdigest(),
        'proxy_exports':len(names),'intercepted_apis':list(HOOKS),
        'ioctls':['0x22204e','0x222051','0x222014','0x22206d','0x222059'],
        'transport_tests_x86':'passed','transport_tests_x64':'passed',
        'bridge_integration_test_x86':'passed',
        'inbox_winusb_driver_selection':'passed',
        'device_driver_list_regression_test':'passed' if selection_test.returncode==0 else 'skipped: administrator required',
        'concurrent_logging_test':'passed: 600 concurrent records across x86/x64 processes',
        'shared_log':'SP9000U/bin/superpro9000u.log',
        'hardware_test':'Not automatically run by this build; see hardware-verification.json',
        'driver_installation_test':'Not run by the build; see hardware-verification.json',
        'toolchain':str(options.toolchain),
    }
    write_log('INFO','build','Build and required tests completed successfully.')
    (out/'build-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')

if __name__=='__main__':
    try:main()
    except Exception as error:
        write_log('ERROR','build',f'Build failed: {error}\n{traceback.format_exc()}')
        raise

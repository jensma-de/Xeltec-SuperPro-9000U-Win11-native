"""Create a clean GitHub upload folder containing source, screenshots and custom binaries."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

ROOT=Path(__file__).resolve().parent.parent
TOP_FILES=['README.md','.gitignore','.gitattributes','Prepare-Software.cmd',
           'prepare.py','extract_xeltek.py','requirements-build.txt']
RUNTIME_FILES=['Setup-USB.exe','Start-SUPERPRO.exe','XeltekUsbSetup.exe',
               'SP9000U/bin/xusb32.dll','README.md']

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'Ready to upload')
    output=parser.parse_args().output.resolve()
    if output==ROOT or not output.is_relative_to(ROOT):
        raise ValueError('Upload folder must be a new directory inside this workspace')
    if output.exists():raise FileExistsError(f'Refusing to overwrite an existing folder: {output}')
    output.mkdir(parents=True)

    def copy(source,relative):
        destination=output/relative
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,destination)

    for name in TOP_FILES:copy(ROOT/name,name)
    for path in (ROOT/'native').rglob('*'):
        if path.is_file() and '__pycache__' not in path.parts and path.suffix in {'.py','.c','.h','.md','.def'}:
            copy(path,path.relative_to(ROOT))
    for name in ['title.png','prog.png']:copy(ROOT/'docs/images'/name,Path('docs/images')/name)
    copy(ROOT/'downloads/README.md','downloads/README.md')
    for name in RUNTIME_FILES:copy(ROOT/'Windows11-Native'/name,Path('Windows11-Native')/name)
    files={path.relative_to(output).as_posix():hashlib.sha256(path.read_bytes()).hexdigest()
           for path in sorted(output.rglob('*')) if path.is_file()}
    manifest={'format':1,'contents':'Source, supplied screenshots and four custom runtime binaries; no vendor package or personal logs.',
              'file_sha256':files}
    (output/'PACKAGE-MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(f'Ready for GitHub: {output}')
    print(f'{len(files)+1} files; {sum(p.stat().st_size for p in output.rglob("*") if p.is_file()):,} bytes')

if __name__=='__main__':main()

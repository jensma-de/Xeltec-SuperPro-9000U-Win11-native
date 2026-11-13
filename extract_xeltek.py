"""Extract the supplied Xeltek setup archives without executing the old installer."""
from pathlib import Path
import hashlib
import json
import struct
import zipfile
import argparse

ROOT = Path(__file__).resolve().parent


def extract_dat(data, target):
    marker, count = struct.unpack_from('<HH', data)
    if marker != 0 or not 0 < count < 50000:
        raise ValueError('Unexpected Xeltek archive header')
    position = 4
    records = []
    pending = []
    while position < len(data):
        end = data.index(b'\0', position)
        name = data[position:end].decode('cp1252')
        size, stamp = struct.unpack_from('<II', data, end + 1)
        position = end + 9
        destination = (target / name).resolve()
        if not destination.is_relative_to(target.resolve()):
            raise ValueError(f'Unsafe archive path: {name}')
        content = data[position:position + size]
        if len(content) != size:
            raise ValueError(f'Truncated file: {name}')
        pending.append((destination, content))
        records.append({'name': name, 'size': size,
                        'sha256': hashlib.sha256(content).hexdigest()})
        position += size
    if position != len(data):
        raise ValueError(f'Archive has {len(data) - position} trailing bytes')
    for destination, content in pending:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(content)
    return records


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',type=Path,help='Original Xeltek nsp9kw.exe or ZIP package')
    args=parser.parse_args()
    if args.archive:
        (ROOT/'Original-Setup').mkdir(exist_ok=True)
        with zipfile.ZipFile(args.archive) as outer:
            members={Path(name).name.upper():name for name in outer.namelist() if not name.endswith('/')}
            for name in ['USBBIN.ZIP','USBLIB.ZIP','USBALGO9.ZIP']:
                if name not in members:raise ValueError(f'Original package is missing {name}')
                (ROOT/'Original-Setup'/name).write_bytes(outer.read(members[name]))
    (ROOT / 'Analysis').mkdir(exist_ok=True)
    manifest = {}
    for archive, folder in [('USBBIN.ZIP', 'bin'), ('USBLIB.ZIP', 'lib'),
                            ('USBALGO9.ZIP', 'algo9')]:
        with zipfile.ZipFile(ROOT / 'Original-Setup' / archive) as source:
            entries = source.namelist()
            if len(entries) != 1:
                raise ValueError('Expected one DAT per ZIP')
            records = extract_dat(source.read(entries[0]), ROOT / 'SP9000U' / folder)
        manifest[folder] = records
        print(folder, len(records), 'files')
        if folder == 'bin':
            for record in records:
                print(record['name'], record['size'])
    (ROOT / 'Analysis' / 'extracted-manifest.json').write_text(
        json.dumps(manifest, indent=2), encoding='utf-8')

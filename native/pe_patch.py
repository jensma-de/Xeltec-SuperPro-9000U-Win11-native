"""Patch the verified x86 vendor application using only Python's standard library."""
import hashlib
import struct

EXPECTED_APPLICATION='4e74dd1e2573f13f12891eeab5a1285f4f5b837af6d522a438423c7093d34997'
EXPECTED_DRIVER='c34392c3c466dddb7f0a29b13251939331444f4b95365d000ff96ee921cbdcf7'

def patch_application(original):
    if hashlib.sha256(original).hexdigest()!=EXPECTED_APPLICATION:
        raise ValueError('Unsupported SP9000.EXE version. Use the Xeltek download linked in README.md.')
    if original[:2]!=b'MZ':raise ValueError('Missing MZ header')
    pe=struct.unpack_from('<I',original,0x3c)[0]
    if original[pe:pe+4]!=b'PE\0\0':raise ValueError('Missing PE header')
    machine,sections=struct.unpack_from('<HH',original,pe+4)
    optional=pe+24
    optional_size=struct.unpack_from('<H',original,pe+20)[0]
    if machine!=0x14c or struct.unpack_from('<H',original,optional)[0]!=0x10b:
        raise ValueError('Expected an x86 PE32 application')
    section_table=optional+optional_size

    def file_offset(rva):
        for index in range(sections):
            entry=section_table+40*index
            size,base,raw_size,raw_offset=struct.unpack_from('<IIII',original,entry+8)
            if base<=rva<base+max(size,raw_size):
                delta=rva-base
                if delta>=raw_size:raise ValueError('RVA points outside file data')
                return raw_offset+delta
        raise ValueError('Cannot resolve PE virtual address')

    import_rva,import_size=struct.unpack_from('<II',original,optional+104)
    imports=file_offset(import_rva)
    patched=bytearray(original)
    found=False
    for index in range(import_size//20):
        descriptor=struct.unpack_from('<IIIII',original,imports+20*index)
        if not any(descriptor):break
        name=file_offset(descriptor[3])
        end=original.index(b'\0',name,name+256)
        if original[name:end].lower()==b'kernel32.dll':
            patched[name:end+1]=b'xusb32.dll\0'.ljust(end-name+1,b'\0')
            found=True;break
    if not found:raise ValueError('KERNEL32 import not found')
    checksum=optional+64
    struct.pack_into('<I',patched,checksum,0)
    total=0
    for offset in range(0,len(patched),2):
        total+=patched[offset] | ((patched[offset+1] if offset+1<len(patched) else 0)<<8)
        total=(total&0xffff)+(total>>16)
    total=(total&0xffff)+(total>>16)
    struct.pack_into('<I',patched,checksum,(total&0xffff)+len(patched))
    return bytes(patched)

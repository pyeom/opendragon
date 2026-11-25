"""Find code/data references to a string (ASCII or UTF-16) in a PE file.

    python xref.py FILE.exe STRING [STRING ...]
"""
import pefile,sys,struct
pe=pefile.PE(sys.argv[1]); base=pe.OPTIONAL_HEADER.ImageBase; img=pe.get_memory_mapped_image()
for s in sys.argv[2:]:
    for enc in ('utf-16le','ascii'):
        needle=s.encode(enc)+(b'\0\0' if enc=='utf-16le' else b'\0')
        i=img.find(needle)
        while i>=0:
            va=base+i; refs=[]
            j=img.find(struct.pack('<I',va))
            while j>=0: refs.append(hex(base+j)); j=img.find(struct.pack('<I',va),j+1)
            print(s,enc,hex(va),'refs at',refs[:10])
            i=img.find(needle,i+1)

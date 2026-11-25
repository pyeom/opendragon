"""Extract PE images (EXE/DLL) embedded in another file.

    python carve.py BINARY OUT_DIR
"""
import pefile,sys,os
data=open(sys.argv[1],'rb').read(); out=sys.argv[2]; i=0
while True:
    i=data.find(b'MZ',i+1)
    if i<0: break
    try:
        pe=pefile.PE(data=data[i:i+0x2000000],fast_load=True)
    except Exception: continue
    end=max(s.PointerToRawData+s.SizeOfRawData for s in pe.sections)
    blob=data[i:i+end]; pe=pefile.PE(data=blob); 
    name=None
    if hasattr(pe,'DIRECTORY_ENTRY_EXPORT') and pe.DIRECTORY_ENTRY_EXPORT.name: name=pe.DIRECTORY_ENTRY_EXPORT.name.decode()
    fn=f"{i:08x}_{name or 'noname'}_{hex(pe.FILE_HEADER.Machine)}.bin"
    open(os.path.join(out,fn),'wb').write(blob); print(fn,len(blob), len(pe.DIRECTORY_ENTRY_EXPORT.symbols) if hasattr(pe,'DIRECTORY_ENTRY_EXPORT') else 0)

"""Disassemble an exported function (or address) of a PE file, annotating imports and strings.

    python dis.py FILE.dll list              # list exports
    python dis.py FILE.dll ExportName [N]    # disassemble N instructions
    python dis.py FILE.dll 0x180005ee0 [N]
"""
import pefile,capstone,sys
pe=pefile.PE(sys.argv[1]); x64=pe.FILE_HEADER.Machine==0x8664
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64 if x64 else capstone.CS_MODE_32)
base=pe.OPTIONAL_HEADER.ImageBase; img=pe.get_memory_mapped_image()
imps={}
for e in pe.DIRECTORY_ENTRY_IMPORT:
    for i in e.imports: imps[i.address]=f"{e.dll.decode()}!{(i.name or b'?').decode()}"
exps={base+s.address:s.name.decode() for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name}
if sys.argv[2]=='list':
    for a,n in sorted(exps.items(),key=lambda x:x[1]): print(hex(a),n)
    sys.exit()
tgt=sys.argv[2]; addr=int(tgt,16) if tgt.startswith('0x') else next(a for a,n in exps.items() if n==tgt)
maxn=int(sys.argv[3]) if len(sys.argv)>3 else 400
def follow(a):
    # resolve jmp thunks
    for _ in range(4):
        ins=next(md.disasm(img[a-base:a-base+16],a),None)
        if ins and ins.mnemonic=='jmp' and ins.op_str.startswith('0x'): a=int(ins.op_str,16)
        else: break
    return a
addr=follow(addr)
def sref(v):
    if base<=v<base+len(img):
        b=img[v-base:v-base+80]
        s=b.split(b'\0')[0]
        if len(s)>=4 and all(32<=c<127 for c in s): return repr(s.decode())
        w=b.decode('utf-16le','ignore').split('\0')[0]
        if len(w)>=4 and w.isprintable(): return 'L'+repr(w)
    return None
n=0
for ins in md.disasm(img[addr-base:addr-base+0x4000],addr):
    c=''
    import re
    for m in re.findall(r'0x[0-9a-f]+',ins.op_str):
        v=int(m,16)
        if v in imps: c+=' ; '+imps[v]
        elif ins.mnemonic=='call' and v in exps: c+=' ; '+exps[v]
        elif ins.mnemonic=='call': 
            f=follow(v); c+=' ; '+(exps.get(f) or imps.get(f) or hex(f))
        s=sref(v)
        if s: c+=' ; '+s
    if 'rip +' in ins.op_str:
        m=re.search(r'rip \+ (0x[0-9a-f]+)',ins.op_str); v=ins.address+ins.size+int(m.group(1),16)
        c+=' ; '+(imps.get(v) or sref(v) or hex(v))
    print(f"{ins.address:x}: {ins.mnemonic} {ins.op_str}{c}")
    n+=1
    if n>=maxn or (ins.mnemonic=='int3' and n>3): break

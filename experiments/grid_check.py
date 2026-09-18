"""Grid-location regression fixtures; run from repository root with one or more executables."""
from pathlib import Path
import random, re, struct, subprocess, sys, tempfile


def run(exe,*args):
    return subprocess.run([str(exe),*map(str,args)],capture_output=True,text=True,timeout=120)


def bmp(path):
    b=bytearray(path.read_bytes());w,h=struct.unpack_from('<ii',b,18)
    off=struct.unpack_from('<I',b,10)[0]
    return b,w,h,off,(w+3)&~3


def old_header(src,dst):
    b,w,h,off,stride=bmp(src)
    rows=[y for y in range(200,h) if 128 in b[off+(h-1-y)*stride:off+(h-1-y)*stride+w]]
    assert rows
    start=h-118-50+(50-(max(rows)-min(rows)+1))//2
    pixels=bytes(b)
    for y in rows:
        source=off+(h-1-y)*stride
        for x in range(w):
            if pixels[source+x]==128: b[source+x]=255
    for y in rows:
        source=off+(h-1-y)*stride;target=off+(h-1-(start+y-min(rows)))*stride
        for x in range(w):
            if pixels[source+x]==128: b[target+x]=0
    for i in range(off,len(b)):
        if b[i]==128: b[i]=0
    dst.write_bytes(b)


def rotate(src,dst):
    b,w,h,off,stride=bmp(src);pixels=bytes(b[off:])
    for y in range(h): b[off+y*stride:off+y*stride+w]=pixels[(h-1-y)*stride:(h-1-y)*stride+w][::-1]
    dst.write_bytes(b)


def fixtures(root,exe):
    data=bytes(range(256))*16;source=root/'input.bin';source.write_bytes(data)
    cases=[]
    for paper in ['A4','Letter','A5','A3','A6','Legal','Tabloid']:
        page=root/(paper+'.bmp');legacy=root/(paper+'-old.bmp')
        p=run(exe,'--encode','-i',source,'-o',page,'--dpi',100,'--header','--paper',paper)
        assert p.returncode==0,p.stderr
        old_header(page,legacy);cases.append((paper,legacy,data))
        turn=root/(paper+'-turned.bmp');rotate(legacy,turn);cases.append((paper+'-180',turn,data))
    big=bytes(range(256))*156;source.write_bytes(big)
    page=root/'dense.bmp';p=run(exe,'--encode','-i',source,'-o',page,'--dpi',100)
    assert p.returncode==0,p.stderr
    base,w,h,off,stride=bmp(page)
    cases.append(('dense',page,big))
    for name,rect in [('center',(w//2-572,h//2-572,w//2+572,h//2+572)),
                      ('vertical',(w//2-572,0,w//2+572,h)),
                      ('horizontal',(0,h//2-572,w,h//2+572)),
                      ('blank',(0,0,w,h))]:
        b=bytearray(base);x0,y0,x1,y1=rect
        for y in range(y0,y1): b[off+y*stride+x0:off+y*stride+x1]=b'\xff'*(x1-x0)
        damaged=root/(name+'.bmp');damaged.write_bytes(b);cases.append((name,damaged,big if name!='blank' else None))
    b,w,h,off,stride=bmp(root/'A4.bmp')
    b[off:]=bytes(255 if v!=128 else 0 for v in b[off:])
    text=root/'text.bmp';text.write_bytes(b);cases.append(('text-only',text,None))
    # Noise must not become a file after the additional window attempts.
    noise=bytearray(base[:off]);size=1024*1024
    struct.pack_into('<ii',noise,18,1024,1024)
    struct.pack_into('<I',noise,2,off+size);struct.pack_into('<I',noise,34,size)
    noise.extend(random.Random(1).randbytes(size))
    path=root/'noise.bmp';path.write_bytes(noise);cases.append(('noise',path,None))
    # A full first page and short last page with the old distant footer.
    multi=bytes(range(256))*220;source.write_bytes(multi)
    p=run(exe,'--encode','-i',source,'-o',root/'multi.bmp','--dpi',100,'--header')
    assert p.returncode==0,p.stderr
    for page in sorted(root.glob('multi_*.bmp')):
        old=root/('old-'+page.name);old_header(page,old)
        turn=root/('turned-'+page.name);rotate(old,turn)
    return cases,multi


if __name__=='__main__':
    check='--check' in sys.argv
    executables=[Path(p).resolve() for p in sys.argv[1:] if p!='--check']
    assert executables,'pass encoder/decoder executable(s)'
    with tempfile.TemporaryDirectory(prefix='grid-check-',dir='.') as folder:
        root=Path(folder);cases,multi=fixtures(root,executables[0])
        for exe in executables:
            for name,page,data in cases:
                out=root/'out.bin';out.unlink(missing_ok=True)
                damaged=name in ['center','vertical','horizontal']
                p=run(exe,'--decode',*(['--force'] if damaged else []),'-i',page,'-o',out)
                summary=re.search(r'Recovered \d+/\d+ blocks',p.stdout)
                correct=out.exists() and data is not None and out.read_bytes()==data
                if check:
                    assert p.stdout.count('Retrying grid search')<=9,p.stdout
                    if data is None:
                        assert p.returncode==1 and not out.exists(),(name,p.stdout,p.stderr)
                    elif damaged:
                        count=int(re.search(r'Recovered (\d+)/',p.stdout)[1])
                        assert p.returncode==2 and count>={'center':302,'vertical':179,'horizontal':206}[name],(name,p.stdout,p.stderr)
                        expected=bytearray(data)
                        for line in Path(str(out)+'.map').read_text().splitlines():
                            if line and line[0].isdigit():
                                start,end=map(int,line.split());expected[start:end]=bytes(end-start)
                        assert out.read_bytes()==expected,name
                    else:
                        assert p.returncode==0 and correct,(name,p.stdout,p.stderr)
                        if name=='dense': assert 'Retrying grid search' not in p.stdout
                print(exe.name,name,p.returncode,'EXACT' if correct else summary[0] if summary else 'NONE',p.stderr.strip().replace('\n',' / '),sep=' | ',flush=True)
            for prefix in ['old','turned']:
                out=root/'out.bin';out.unlink(missing_ok=True)
                pages=sorted(root.glob(prefix+'-multi_*.bmp'))
                p=run(exe,'--decode','-o',out,*pages)
                exact=p.returncode==0 and out.read_bytes()==multi
                if check: assert exact,(prefix,p.stdout,p.stderr)
                print(exe.name,prefix+'-multi','EXACT' if exact else 'FAIL',flush=True)

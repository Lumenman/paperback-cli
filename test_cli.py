"""End-to-end checks: python test_cli.py [path/to/paperback-cli]. Stdlib only."""
from pathlib import Path
import hashlib, os, random, struct, subprocess, sys, tempfile

EXE = str(Path(sys.argv[1] if len(sys.argv)>1 else ('paperback-cli.exe' if os.name=='nt' else './paperback-cli')).resolve())

def run(*args, code=0):
    p=subprocess.run([EXE,*map(str,args)],capture_output=True,text=True,timeout=60)
    assert p.returncode==code,(args,p.returncode,p.stdout,p.stderr)
    return p

def bmp(path):
    b=bytearray(path.read_bytes())
    w,h=struct.unpack_from('<ii',b,18)
    off=struct.unpack_from('<I',b,10)[0]
    return b,w,h,off,(w+3)&~3

def rotated(src,dst):
    b,w,h,off,stride=bmp(src)
    pixels=bytes(b[off:])
    for y in range(h):
        b[off+y*stride:off+y*stride+w]=pixels[(h-1-y)*stride:(h-1-y)*stride+w][::-1]
    dst.write_bytes(b)

with tempfile.TemporaryDirectory(prefix='paperback-test-',dir='.') as folder:
    root=Path(folder)
    source=root/'input.bin'; original=bytes(range(256))*16;source.write_bytes(original)
    page=root/'page.bmp';output=root/'out.bin'
    run('--encode','-i',source,'-o',page,'--dpi',100)
    b,w,h,off,stride=bmp(page)
    assert (w,h)==(2480,3508)
    assert struct.unpack_from('<ii',b,38)==(11811,11811)
    for y in range(h):
        row=b[off+y*stride:off+y*stride+w]
        assert all(v==255 for v in row[:118])
        assert all(v==255 for v in row[w-118:])
        if y<118 or y>=h-118: assert all(v==255 for v in row)
    run('--decode','-i',page,'-o',output);assert output.read_bytes()==original
    turn=root/'turned.bmp';rotated(page,turn)
    run('--decode','-i',turn,'-o',output);assert output.read_bytes()==original
    # Top-down BMP and default 256-entry palette are both valid BMP variants.
    top=bytearray(b);struct.pack_into('<i',top,22,-h);struct.pack_into('<I',top,46,0)
    top[off:]=b''.join(b[off+y*stride:off+(y+1)*stride] for y in reversed(range(h)))
    topfile=root/'top.bmp';topfile.write_bytes(top)
    run('--decode','-i',topfile,'-o',output);assert output.read_bytes()==original
    # 24-bit BMP conversion, including per-row padding.
    rgb=bytearray(b[:54]);rgbstride=(w*3+3)&~3
    struct.pack_into('<I',rgb,2,54+rgbstride*h);struct.pack_into('<I',rgb,10,54)
    struct.pack_into('<H',rgb,28,24);struct.pack_into('<I',rgb,34,rgbstride*h)
    struct.pack_into('<II',rgb,46,0,0)
    table=[bytes([i,i,i]) for i in range(256)]
    for y in range(h):
        rgb.extend(b''.join(table[v] for v in b[off+y*stride:off+y*stride+w]))
        rgb.extend(bytes(rgbstride-w*3))
    rgbfile=root/'rgb.bmp';rgbfile.write_bytes(rgb)
    run('--decode','-i',rgbfile,'-o',output);assert output.read_bytes()==original
    # A stripe faded to the black/white threshold wipes 8 dot rows, 32 bytes, of
    # every block it crosses. That is past plain ECC, which locates 16 bad bytes,
    # and within reach of erasure decoding, which is told where 32 of them are.
    rnd=random.Random(1);faded=bytearray(b)
    for y in range(200,224):
        row=off+(h-1-y)*stride
        faded[row:row+w]=bytes(min(255,max(0,int(rnd.gauss(128,20)))) for _ in range(w))
    fadedfile=root/'faded.bmp';fadedfile.write_bytes(faded)
    run('--decode','-i',fadedfile,'-o',output);assert output.read_bytes()==original
    # Complementary damage: each loses many data blocks, together they restore the file.
    damaged=[]
    for n,(left,right) in enumerate(((w//2+140,w),(0,w//2-140))):
        d=bytearray(b)
        for y in range(h): d[off+y*stride+left:off+y*stride+right]=b'\xff'*(right-left)
        path=root/f'damage{n}.bmp';path.write_bytes(d);damaged.append(path)
        run('--decode','-i',path,'-o',root/f'fail{n}',code=1)
        run('--decode','--force','-i',path,'-o',root/f'partial{n}.rar',code=2)
        part=root/f'partial{n}.rar';assert part.exists()
        data=part.read_bytes();assert len(data)==len(original)
        text=Path(str(part)+'.map').read_text()
        ranges=[tuple(map(int,line.split())) for line in text.splitlines() if line and line[0].isdigit()]
        assert ranges
        expected=bytearray(original)
        for start,end in ranges: expected[start:end]=bytes(end-start)
        assert data==expected  # Every readable byte stays at its original offset.
        assert not Path(str(part)+'.partial').exists()
        assert not Path(str(part)+'.raw.partial').exists()
    rotated(damaged[1],turn)
    run('--decode','-i',damaged[0],'-i',turn,'-o',output);assert output.read_bytes()==original
    run('--decode','-o',output,damaged[0],turn);assert output.read_bytes()==original
    # Invalid input must fail normally, but --force continues to later scans.
    broken=root/'invalid.bmp';broken.write_bytes(b'BM')
    run('--decode','-i',broken,'-i',page,'-o',output,code=1)
    run('--decode','--force','-i',broken,'-i',page,'-o',output);assert output.read_bytes()==original
    truncated=root/'truncated.bmp';truncated.write_bytes(b[:-stride*130])
    run('--decode','-i',truncated,'-o',output,code=1)
    run('--decode','--force','-i',truncated,'-i',page,'-o',output);assert output.read_bytes()==original
    malformed=bytearray(b);struct.pack_into('<I',malformed,46,257);broken.write_bytes(malformed)
    run('--decode','--force','-i',broken,'-o',output,code=1)
    malformed=bytearray(b);struct.pack_into('<I',malformed,10,0xffffffff);broken.write_bytes(malformed)
    run('--decode','--force','-i',broken,'-o',output,code=1)
    digest=hashlib.sha256(original).hexdigest()
    run('--encode','-i',source,'-o',page,'--dpi',100)
    got=run('--decode','-i',page,'-o',output,'--expect',digest.upper())
    assert 'matches --expect' in got.stdout and output.read_bytes()==original
    bad=run('--decode','-i',page,'-o',output,'--expect','f'*64,code=1)
    assert 'HASH MISMATCH' in bad.stdout and digest in bad.stderr
    assert output.read_bytes()==original  # the file is still written
    for opts in [('--expect','abc'),('--expect','g'+'f'*63),('--expect',digest+'0')]:
        run('--decode','-i',page,'-o',output,*opts,code=1)
    run('--encode','-i',source,'-o',page,'--expect',digest,code=1)
    # Full sheets, presets, orientation and custom dimensions.
    for opts,size in [(['--paper','Letter'],(2550,3300)),(['--paper','a3'],(3508,4961)),(['--paper','A6'],(1240,1748)),(['--paper','Legal'],(2550,4200)),(['--paper','Tabloid'],(3300,5100)),(['--paper-size','8.5x11in'],(2550,3300)),(['--paper','A5','--landscape'],(2480,1748)),(['--paper-size','160x200mm'],(1890,2362))]:
        run('--encode','-i',source,'-o',page,'--dpi',100,*opts)
        assert bmp(page)[1:3]==size
        run('--decode','-i',page,'-o',output);assert output.read_bytes()==original
    run('--encode','-i',source,'-o',page,'--dpi',100,'--border','--margin-left','15mm')
    run('--decode','-i',page,'-o',output);assert output.read_bytes()==original
    # Header and footer: text lands in the two reserved bands, the margins stay
    # clear, the data still decodes and the printed digest matches the input.
    head=run('--encode','-i',source,'-o',page,'--dpi',100,'--header')
    assert hashlib.sha256(original).hexdigest() in head.stdout
    b,w,h,off,stride=bmp(page)
    assert (w,h)==(2480,3508)
    def inked(y0,y1): return any(b[off+(h-1-y)*stride+x]<128 for y in range(y0,y1) for x in range(w))
    # Text starts inside the reserved top band and the grid only below it, so a
    # white gap separates the two; a page without a header has grid dots there.
    assert inked(118,168) and not inked(168,190)
    assert inked(h-168,h-118)                    # footer band
    assert not inked(0,118) and not inked(h-118,h)
    run('--decode','-i',page,'-o',output);assert output.read_bytes()==original
    for opts in [('--dpi','abc'),('--dpi','200x'),('--pages','-1'),('--paper','Unknown'),('--margin','nan'),('--paper-size','1x1mm'),('--margin','500mm'),('--image-dpi','100')]:
        run('--encode','-i',source,'-o',page,*opts,code=1)
    # Multiple sheets preserve size, repeat scans do not discard accumulated blocks.
    source.write_bytes(bytes(range(256))*320)
    run('--encode','-i',source,'-o',root/'multi.bmp','--dpi',100)
    pages=sorted(root.glob('multi_*.bmp'));assert len(pages)>1
    assert all(bmp(p)[1:3]==(2480,3508) for p in pages)
    run('--decode','-i',root/'multi.bmp','-p',len(pages),'-o',output)
    assert output.read_bytes()==source.read_bytes()
    run('--decode','-o',output,*reversed(pages),pages[0]);assert output.read_bytes()==source.read_bytes()
    # Regression: 44100 bytes on A4 at 100 DPI fills page one exactly; the
    # 16-byte padding in the header needs a second page for the last block.
    edge=root/'edge.bin';edge.write_bytes((bytes(range(256))*173)[:44100])
    run('--encode','-i',edge,'-o',root/'edge.bmp','--dpi',100)
    edgepages=sorted(root.glob('edge_*.bmp'));assert len(edgepages)==2
    run('--decode','-o',output,*edgepages);assert output.read_bytes()==edge.read_bytes()
    other=root/'other.bin';other.write_bytes(bytes(range(255,-1,-1))*320)
    run('--encode','-i',other,'-o',root/'other.bmp','--dpi',100)
    run('--decode','--force','-o',output,pages[0],root/'other_0001.bmp',code=1)
    # --quality-map draws one character per block. It is the first thing to
    # look at when a scan fails: a band of dots means the raster was lost
    # there, hashes scattered over the sheet mean the dots themselves are
    # unreadable - two different causes needing two different fixes.
    def blockmap(scan,code=0):
        got=run('--decode','--quality-map','-i',scan,'-o',output,code=code)
        rows=[l for l in got.stdout.splitlines()
              if len(l)>10 and set(l)<=set('.#+0123456789')]
        assert rows and len(set(map(len,rows)))==1,got.stdout
        return rows
    clean=blockmap(page)
    assert any('0' in row for row in clean)
    assert output.read_bytes()==original
    # Wiping a band of the scan must show up as whole rows of dots: the raster
    # is gone there, so those blocks are never located in the first place.
    b,w,h,off,stride=bmp(page)
    hole=bytearray(b)
    for y in range(h-420,h-315):       # One block row; the bitmap is bottom-up.
        hole[off+y*stride:off+y*stride+w]=bytes([255])*w
    holed=root/'holed.bmp';holed.write_bytes(hole)
    blank=lambda rows:sum(set(row)=={'.'} for row in rows)
    assert blank(blockmap(holed,code=1))>blank(clean)
print('All CLI checks passed')


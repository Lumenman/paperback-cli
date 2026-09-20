/* Restoration checks: fixed offsets, integrity failures, legacy rejection. */
#define main paperback_main
#include "src/main.c"
#undef main
#include <assert.h>
// Exercise bit recognition before its result reaches the file assembler.
#include "src/Decoder.c"

static void check_recognition_addresses(void) {
  // Real scans produced the three long addresses below out of erasure-assisted
  // results whose CRC happened to match (experiments/NOTES.md 21). No address
  // the assembler cannot use may set orientation or count as a read block.
  const uint32_t addresses[]={0,90,0x1000005A,0x500001C2,0xA0000384,
    SUPERBLOCK,1,0x5000005A,0xB0000000,0xC5F3BA47,0xFF7F032F,
    0xC5F1F1E3,((MAXSIZE+NDATA-1)/NDATA)*NDATA};
  for (unsigned n=0;n<sizeof(addresses)/sizeof(addresses[0]);n++) {
    t_data encoded={0};encoded.addr=addresses[n];
    for (int i=0;i<NDATA;i++) encoded.data[i]=(uchar)(i*37+11);
    encoded.crc=Crc16((uchar *)&encoded,NDATA+4)^0x55AA;
    Encode8((uchar *)&encoded,encoded.ecc,127);
    uchar grid[NDOT][NDOT];
    for (int y=0;y<NDOT;y++) {
      uint32_t row;memcpy(&row,(uchar *)&encoded+y*4,4);
      row^=(y&1)?0xAAAAAAAA:0x55555555;
      for (int x=0;x<NDOT;x++) grid[y][x]=(row>>x)&1?0:255;
    }
    for (int best=0;best<2;best++) for (int erasures=0;erasures<2;erasures++) {
      t_procdata state={0};state.cmax=255;state.orientation=-1;
      state.mode=best?M_BEST:0;
      t_data got={0};
      int answer=Recognizebits(&got,grid,&state,erasures);
      if (n<6) {
        assert(answer==0 && state.orientation==0);
        assert(memcmp(&got,&encoded,sizeof(got))==0);
      } else {
        assert(answer==17 && state.orientation==-1);
      }
    }
  }
}

static void check_retry_without_orientation(void) {
  // End of a pass: the final position has no raster. An earlier failure only
  // deserves a second pass when some accepted block supplied orientation.
  for (int known=0;known<2;known++) for (int failed=0;failed<2;failed++) {
    t_procdata state={0};uchar pixels[256],buffer[256];int x[16],y[16];
    signed char quality=17;
    memset(pixels,255,sizeof(pixels));
    state.data=pixels;state.sizex=state.sizey=16;
    state.buf1=buffer;state.bufx=x;state.bufy=y;
    state.bufdx=state.bufdy=16;state.cmax=255;
    state.nposx=state.nposy=1;state.qmap=&quality;
    state.orientation=known?7:-1;state.nbad=failed;state.step=7;
    Decodenextblock(&state);
    assert(state.step==(known && failed?7:8));
    assert(state.pass==(known && failed?1:0));
  }
}

static void setup(const uchar *bytes,unsigned size) {
  Closefproc(0);
  t_fproc *p=&pb_fproc[0];p->busy=1;p->datasize=p->origsize=size;
  p->nblock=(size+NDATA-1)/NDATA;p->ndata=p->nblock;
  p->data=calloc(p->nblock,NDATA);p->datavalid=malloc(p->nblock);
  assert(p->data && p->datavalid);
  memcpy(p->data,bytes,size);memset(p->datavalid,1,p->nblock);
  p->filecrc=Crc16(p->data,size);
  strcpy(pb_outfile,"core-output.rar");
}
static void setup_digest(const uchar *bytes,unsigned size,int corrupt) {
  char hex[SHA256_HEXLEN+1];
  unsigned stored=(size+SHA256_SIZE+15)&~15u;
  Closefproc(0);
  t_fproc *p=&pb_fproc[0];p->busy=1;p->origsize=size;p->datasize=stored;
  p->nblock=(stored+NDATA-1)/NDATA;p->ndata=p->nblock;
  p->data=calloc(p->nblock,NDATA);p->datavalid=malloc(p->nblock);
  assert(p->data && p->datavalid);
  memcpy(p->data,bytes,size);memset(p->datavalid,1,p->nblock);
  Sha256hex(p->data,size,hex);
  for(int i=0;i<SHA256_SIZE;i++) {
    int hi=hex[2*i],lo=hex[2*i+1];
    hi=hi<='9'?hi-'0':hi-'a'+10;lo=lo<='9'?lo-'0':lo-'a'+10;
    p->data[stored-SHA256_SIZE+i]=(uchar)(hi*16+lo); }
  if(corrupt) p->data[stored-1]^=1;
  p->filecrc=Crc16(p->data,stored);
  strcpy(pb_outfile,"core-output.rar");
}
static void matches(const uchar *bytes,size_t size) {
  FILE *f=fopen(pb_outfile,"rb");assert(f);
  for(size_t i=0;i<size;i++) assert(fgetc(f)==bytes[i]);
  assert(fgetc(f)==EOF);assert(fclose(f)==0);remove(pb_outfile);
}
static void map_contains(const char *text) {
  char buf[2048];FILE *f=fopen("core-output.rar.map","rb");assert(f);
  size_t n=fread(buf,1,sizeof(buf)-1,f);buf[n]=0;fclose(f);assert(strstr(buf,text));
}
int main(void) {
  check_recognition_addresses();
  check_retry_without_orientation();
  assert(strnicmp("aBc","ABC",64)==0);assert(strnicmp("abc","abd",3)<0);
  char a[64],b[64];memset(a,'a',64);memset(b,'a',64);b[63]='b';assert(strnicmp(a,b,64)<0);
  uchar original[512];for(int i=0;i<512;i++) original[i]=(uchar)i;
  setup(original,512);
  assert(Saverestoredfile(0,0)==0);matches(original,512);map_contains("COMPLETE");
  // A checksum failure must not block --force or zero otherwise readable bytes.
  pb_fproc[0].filecrc^=1;
  assert(Saverestoredfile(0,0)==-1);
  assert(Saverestoredfile(0,1)==2);matches(original,512);map_contains("MISMATCH");
  // Missing interior and final blocks become zeros; length and offsets stay exact.
  setup(original,512);pb_fproc[0].datavalid[1]=2;pb_fproc[0].datavalid[5]=0;pb_fproc[0].ndata-=2;
  assert(Saverestoredfile(0,0)==-1);assert(Saverestoredfile(0,1)==2);
  uchar expected[512];memcpy(expected,original,512);memset(expected+90,0,90);memset(expected+450,0,62);
  matches(expected,512);map_contains("90 180");map_contains("450 512");
  // A subsequent complete restore replaces the stale damage report.
  setup(original,512);assert(Saverestoredfile(0,0)==0);matches(original,512);map_contains("Status: COMPLETE");
  // A page that carries its own digest checks the restore without being told
  // anything: the last 32 bytes of the stored data are the SHA-256 of the
  // origsize bytes before them, and nothing in the superblock says so.
  setup_digest(original,512,0);
  assert(Saverestoredfile(0,0)==0);matches(original,512);
  // A digest that does not describe the bytes is a failure, with or without
  // --force. The bytes are still written; it is the verdict that differs.
  setup_digest(original,512,1);
  assert(Saverestoredfile(0,0)==-1);matches(original,512);
  setup_digest(original,512,1);
  assert(Saverestoredfile(0,1)==-1);matches(original,512);
  // Losing only a block that lies past origsize costs the check, not the file:
  // every byte that gets written is still here, so this is not a damaged
  // restore. Before the digest, padding alone could raise the same false alarm.
  setup_digest(original,512,0);
  assert(pb_fproc[0].nblock*NDATA>512+SHA256_SIZE);
  pb_fproc[0].datavalid[pb_fproc[0].nblock-1]=0;pb_fproc[0].ndata--;
  assert(Saverestoredfile(0,0)==0);matches(original,512);map_contains("COMPLETE");
  setup(original,512);
  // A zero CRC is a checksum value, not a legacy 'skip verification' marker.
  assert(pb_fproc[0].filecrc!=0);pb_fproc[0].filecrc=0;assert(Saverestoredfile(0,0)==-1);
  for(int mode=1;mode<=3;mode++) {
    pb_fproc[0].mode=mode;assert(Saverestoredfile(0,1)==-1);
    t_superblock legacy={0};legacy.mode=mode;assert(Startnextpage(&legacy)==-1);
  }
  Closefproc(0);remove("core-output.rar.map");
  // Preserve parity from an earlier scan; repair the tail group as well.
  t_superblock meta={0};meta.datasize=180;meta.origsize=180;meta.pagesize=450;meta.page=1;meta.ngroup=5;
  int slot=Startnextpage(&meta);assert(slot>=0);
  t_block parity={0};parity.recsize=450;memset(parity.data,0xff,90);
  t_block block={0};memset(block.data,0x42,90);
  for(int i=0;i<90;i++) parity.data[i]^=0x42^0x24;
  assert(Addblock(&parity,slot)==0);Finishpage(slot,1,0,0);
  assert(pb_fproc[slot].ndata==0);
  Startnextpage(&meta);assert(Addblock(&block,slot)==0);Finishpage(slot,1,0,0);
  assert(pb_fproc[slot].ndata==2);
  for(int i=90;i<180;i++) assert(pb_fproc[slot].data[i]==0x24);
  Closefproc(slot);
  // A page label turns into a file name only after being read as a name and
  // never as a path: those bytes come off a scanned sheet and can say anything.
  {
    char got[MAXPATH],big[65];
    static const struct {const char *label,*want;} named[]={
      {"plain.bin","plain.bin"},
      {"../../etc/passwd","passwd"},         // a separator starts the name over
      {"C:evil","evil"},
      {"dir\\sub\\file.rar","file.rar"},
      {"bad\tname","bad_name"},              // control characters keep their width
      {"q?*<>|\"x","q______x"},
      {"trail.  ","trail"}};                 // Windows drops these itself
    static const char *refused[]={"","..",".","/","...   "};
    for(size_t i=0;i<sizeof(named)/sizeof(named[0]);i++) {
      assert(Namefrompagelabel(named[i].label,got,sizeof(got))==0);
      assert(strcmp(got,named[i].want)==0);
    }
    for(size_t i=0;i<sizeof(refused)/sizeof(refused[0]);i++)
      assert(Namefrompagelabel(refused[i],got,sizeof(got))<0);
    memset(big,'A',64);big[64]=0;            // a label may fill all 64 bytes
    assert(Namefrompagelabel(big,got,sizeof(got))==0);
    assert(strlen(got)==64);
    assert(Namefrompagelabel("plain.bin",got,4)<0);      // no room for the name
  }
  puts("All core checks passed");return 0;
}

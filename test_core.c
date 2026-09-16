/* Restoration checks: fixed offsets, integrity failures, legacy rejection. */
#define main paperback_main
#include "src/main.c"
#undef main
#include <assert.h>

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
  puts("All core checks passed");return 0;
}

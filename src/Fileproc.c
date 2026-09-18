////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// PaperBack -- high density backups on the plain paper                       //
//                                                                            //
// Copyright (c) 2007 Oleh Yuschuk                                            //
// ollydbg at t-online de (set Subject to 'paperback' or be filtered out!)    //
//                                                                            //
//                                                                            //
// This file is part of PaperBack.                                            //
//                                                                            //
// Paperback is free software; you can redistribute it and/or modify it under //
// the terms of the GNU General Public License as published by the Free       //
// Software Foundation; either version 3 of the License, or (at your option)  //
// any later version.                                                         //
//                                                                            //
// PaperBack is distributed in the hope that it will be useful, but WITHOUT   //
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      //
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for   //
// more details.                                                              //
//                                                                            //
// You should have received a copy of the GNU General Public License along    //
// with this program. If not, see <http://www.gnu.org/licenses/>.             //
//                                                                            //
//                                                                            //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
#include <windows.h>
#elif __linux__
#include <sys/stat.h>
#endif
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <utime.h>

#include "paperbak.h"
#include "Resource.h"




// Clears descriptor of processed file
void Closefproc(int slot) {
  if (slot<0 || slot>=NFILE)
    return;                            // Error in input data
  if (pb_fproc[slot].datavalid!=NULL)
    free(pb_fproc[slot].datavalid);
  if (pb_fproc[slot].data!=NULL)
    free(pb_fproc[slot].data);
  memset(pb_fproc+slot,0,sizeof(t_fproc));
  //Updatefileinfo(slot,pb_fproc+slot); //GUI
};



// Says what the page calls the file it carries. The decoder has always read
// this name and kept it - two scans belong to the same file only if their
// labels match - but never told anyone what it was, so a sheet of unknown
// provenance restored into whatever -o was called and its type was left to be
// guessed at.
//
// Printed with a length and through a filter, never as a string. The format
// lets all 64 bytes be name, so the field need not be terminated; and the
// bytes come off a scanned sheet, where a control character is as easy to
// print as a letter and an escape sequence would be obeyed by the terminal.
// Only C0 and DEL are replaced, so a name in UTF-8 still reads as itself.
void Reportpagelabel(const char *name) {
  int i;
  uchar c,s[65];
  for (i=0; i<64 && name[i]!='\0'; i++) {
    c=(uchar)name[i];
    s[i]=(c<0x20 || c==0x7F)?'?':c; };
  s[i]='\0';
  if (i==0)
    printf("Page label: the page carries no file name\n");
  else
    printf("Page label: %s\n",s);
};

// Starts new decoded page. Returns non-negative index to table of processed
// files on success or -1 on error.
int Startnextpage(t_superblock *superblock) {
  int i,slot,freeslot;
  t_fproc *pf;
  // Check whether file is already in the list of processed files. If not,
  // initialize new descriptor.
  if(superblock->mode!=0) {
    Reporterror("Legacy compressed/encrypted pages are not supported"); return -1;
  }
  if (!superblock->datasize || superblock->datasize>MAXSIZE ||
      !superblock->origsize || superblock->origsize>MAXSIZE ||
      !superblock->pagesize || superblock->pagesize>MAXSIZE || superblock->pagesize%NDATA ||
      superblock->page>(superblock->datasize+superblock->pagesize-1)/superblock->pagesize ||
      !superblock->page || superblock->ngroup>NGROUPMAX ||
      superblock->origsize>superblock->datasize) {
    Reporterror("Invalid page metadata"); return -1;
  }
  freeslot=-1;
  for (slot=0,pf=pb_fproc; slot<NFILE; slot++,pf++) {
    if (pf->busy==0) {                 // Empty descriptor
      if (freeslot<0) freeslot=slot;
      continue; };

    
    if (memcmp(pf->name,superblock->name,64)!=0 || pf->filecrc!=superblock->filecrc)
      continue;                        // Different file name
    if (pf->mode!=superblock->mode)
      continue;                        // Different compression mode
    if (pf->modified.dwLowDateTime!=superblock->modified.dwLowDateTime ||
      pf->modified.dwHighDateTime!=superblock->modified.dwHighDateTime)
      continue;                        // Different timestamp - wrong version?
    if (pf->datasize!=superblock->datasize)
      continue;                        // Different compressed size
    if (pf->origsize!=superblock->origsize)
      continue;                        // Different original size
    // File found. Check for the case of two backup copies printed with
    // different settings.
    if (pf->pagesize!=superblock->pagesize)
      pf->pagesize=0;
    break; };
  if (slot>=NFILE) {
    // No matching descriptor, create new one.
    if (freeslot<0) {
      Reporterror("Maximal number of processed files exceeded");
      return -1; };
    slot=freeslot;
    pf=pb_fproc+slot;
    memset(pf,0,sizeof(t_fproc));
    // Allocate block and recovery tables.
    pf->nblock=(superblock->datasize+NDATA-1)/NDATA;
    pf->datavalid=(uchar *)calloc(pf->nblock, sizeof(uchar));
    pf->data=(uchar *)calloc(pf->nblock*NDATA, sizeof(uchar));
    if (pf->datavalid==NULL || pf->data==NULL) {
      Closefproc(slot);
      Reporterror("Low memory");
      return -1; 
    };
    // Initialize remaining fields.
    memcpy(pf->name,superblock->name,64);
    pf->modified=superblock->modified;
    pf->attributes=superblock->attributes;
    pf->filecrc=superblock->filecrc;
    pf->datasize=superblock->datasize;
    pf->pagesize=superblock->pagesize;
    pf->origsize=superblock->origsize;
    pf->mode=superblock->mode;
    if (pf->pagesize>0)
      pf->npages=(pf->datasize+pf->pagesize-1)/pf->pagesize;
    else
      pf->npages=0;
    pf->ndata=0;
    for (i=0; i<pf->npages && i<8; i++)
      pf->rempages[i]=i+1;
    // Initialize statistics and declare descriptor as busy.
    pf->goodblocks=0;
    pf->badblocks=0;
    pf->restoredbytes=0;
    pf->recoveredblocks=0;
    pf->busy=1;
    Reportpagelabel(pf->name); };
  // Invalidate page limits and report success.
  pf=pb_fproc+slot;
  pf->page=superblock->page;
  if(superblock->ngroup && pf->ngroup!=superblock->ngroup) {
    for(i=0;i<pf->nblock;i++) if(pf->datavalid[i]==2) pf->datavalid[i]=0;
    pf->ngroup=superblock->ngroup;
  }
  pf->minpageaddr=0xFFFFFFFF;
  pf->maxpageaddr=0;
  //Updatefileinfo(slot,pf);
  return slot;
};

// Adds block recognized by decoder to file described by file descriptor with
// specified index. Returns 0 on success and -1 on any error.
int Addblock(t_block *block,int slot) {
  int i,j;
  t_fproc *pf;
  if (slot<0 || slot>=NFILE)
    return -1;                         // Invalid index of file descriptor
  pf=pb_fproc+slot;
  if (pf->busy==0)
    return -1;                         // Index points to unused descriptor
  // Add block to descriptor.
  if (block->recsize==0) {
    // Ordinary data block.
    i=block->addr/NDATA;
    if ((uint32_t)(i*NDATA)!=block->addr)
      return -1;                       // Invalid data alignment
    if (i>=pf->nblock)
      return -1;                       // Data outside the data size
    if (pf->datavalid[i]!=1) {
      memcpy(pf->data+block->addr,block->data,NDATA);
      pf->datavalid[i]=1;              // Valid data
      pf->ndata++; };
    if(block->addr<pf->minpageaddr) pf->minpageaddr=block->addr;
    if(block->addr+NDATA>pf->maxpageaddr) pf->maxpageaddr=block->addr+NDATA; }
  else {
    // Data recovery block. I write it to all free locations within the group.
    if (block->recsize!=(uint32_t)(pf->ngroup*NDATA))
      return -1;                       // Invalid recovery scope
    i=block->addr/block->recsize;
    if (i*block->recsize!=block->addr)
      return -1;                       // Invalid data alignment
    i=block->addr/NDATA;
    for (j=i; j<i+pf->ngroup; j++) {
      if (j>=pf->nblock)
        break;                     // Data outside the data size
      if (pf->datavalid[j]!=0) continue;
      memcpy(pf->data+j*NDATA,block->data,NDATA);
      pf->datavalid[j]=2; };           // Valid recovery data
    if(block->addr<pf->minpageaddr) pf->minpageaddr=block->addr;
    if(block->addr+block->recsize>pf->maxpageaddr) pf->maxpageaddr=block->addr+block->recsize;
  };
  // Report success.
  return 0;
};

// Processes gathered data. Returns -1 on error, 0 if file is complete and
// number of pages to scan if there is still missing data. In the last case,
// fills list of several first remaining pages in file descriptor.
int Finishpage(int slot,int ngood,int nbad,uint32_t nrestored) {
  int i,j,r,rmin,rmax,nrec,irec,firstblock,nrempages;
  uchar *pr,*pd;
  t_fproc *pf;
  if (slot<0 || slot>=NFILE)
    return -1;                         // Invalid index of file descriptor
  pf=pb_fproc+slot;
  if (pf->busy==0)
    return -1;                         // Index points to unused descriptor
  // Update statistics. Note that it grows also when the same page is scanned
  // repeatedly.
  pf->goodblocks+=ngood;
  pf->badblocks+=nbad;
  pf->restoredbytes+=nrestored;

  // Restore bad blocks if corresponding recovery blocks are available (max. 1
  // per group).
  if (pf->ngroup>0 && pf->minpageaddr!=0xFFFFFFFF) {
    rmin=(pf->minpageaddr/(NDATA*pf->ngroup))*pf->ngroup;
    rmax=(pf->maxpageaddr/(NDATA*pf->ngroup))*pf->ngroup;
    // Walk groups of data on current page, one by one.
    for (r=rmin; r<=rmax; r+=pf->ngroup) {
      if (r>=pf->nblock)
        break;                         // Inconsistent data
      // Count blocks with recovery data in the group.
      nrec=0; irec=-1;
      for (i=r; i<r+pf->ngroup && i<pf->nblock; i++) {
        if(pf->datavalid[i]!=1) nrec++;
        if(pf->datavalid[i]==2) irec=i;
      }
      if(nrec==1 && irec>=0) {
        // Exactly one block in group is missing, recovery is possible.
        pr=pf->data+irec*NDATA;
        // Invert recovery data.
        for (j=0; j<NDATA; j++) *pr++^=0xFF;
        // XOR recovery data with good data blocks.
        for (i=r; i<r+pf->ngroup && i<pf->nblock; i++) {
          if (i==irec) continue;
          pr=pf->data+irec*NDATA;
          pd=pf->data+i*NDATA;
          for (j=0; j<NDATA; j++) {
            *pr++^=*pd++;
          };
        };
        pf->datavalid[irec]=1;
        pf->recoveredblocks++;
        pf->ndata++;
      };
    };
  };
  // Check whether there are still bad blocks on the page.
  firstblock=(pf->page-1)*(pf->pagesize/NDATA);
  for (j=firstblock; j<firstblock+pf->pagesize/NDATA && j<pf->nblock; j++) {
    if (pf->datavalid[j]!=1) break; };
  if (j<firstblock+pf->pagesize/NDATA && j<pf->nblock)
    Message("Unrecoverable errors on page, please scan it again\n",0);
  else if (nbad>0)
    Message("Page processed\n, all bad blocks successfully restored",0);
  else
    Message("Page processed\n",0);
  // Calculate list of (partially) incomplete pages.
  nrempages=0;
  if (pf->pagesize>0) {
    for (i=0; i<pf->npages && nrempages<8; i++) {
      firstblock=i*(pf->pagesize/NDATA);
      for (j=firstblock; j<firstblock+pf->pagesize/NDATA && j<pf->nblock; j++) {
        if (pf->datavalid[j]==1)
          continue;
        // Page incomplete.
        pf->rempages[nrempages++]=i+1;
        break;
      };
    };
  };
  if (nrempages<8)
    pf->rempages[nrempages]=0;
  //Updatefileinfo(slot,pf);
  // "File restored." used to be printed here, once per page that completed the
  // file, before anything had been written - and it read as success even when
  // the save was then refused. What it actually meant, that every block is in,
  // the summary says once and at the right time.
  if (pf->ndata==pf->nblock && pb_autosave!=0) {
    Message("File complete",0);
    Saverestoredfile(slot,0); };
  return pf->ndata==pf->nblock ? 0 : (nrempages ? nrempages : 1);
};

// Turns the page label into a name to restore under when the caller named no
// output. The label is 64 bytes off a scanned sheet: it need not be terminated,
// it holds whatever the name of the encoded file held, and a sheet can be made
// to carry anything at all. So it is read as a NAME and never as a path -
// everything up to the last separator is dropped, the characters a name may not
// hold are replaced, and what is left is written in the current directory.
// Returns 0 on success, -1 if nothing usable is left.
int Namefrompagelabel(const char *label,char *out,int size) {
  int i,n;
  unsigned char c;
  char base[65];
  for (i=0,n=0; i<64 && label[i]!=0; i++) {
    c=(unsigned char)label[i];
    if (c=='/' || c=='\\' || c==':')
      n=0;                             // A separator starts the name over, so a
    else if (c<0x20 || c==0x7F ||      // label of ../../etc/passwd leaves passwd
      c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*')
      base[n++]='_';                   // Replaced rather than dropped: the name
    else                               // keeps its length, so two labels cannot
      base[n++]=(char)c; };            // be cleaned into the same file
  while (n>0 && (base[n-1]=='.' || base[n-1]==' '))
    n--;                               // Windows drops these silently
  base[n]=0;
  if (n==0 || strcmp(base,".")==0 || strcmp(base,"..")==0)
    return -1;
#ifdef _WIN32
  // A file called CON or LPT1 cannot be created whatever the sheet says, and
  // opening one talks to a device instead. Refused rather than renamed, so the
  // caller is told to pass -o rather than handed a file under a name they did
  // not ask for.
  {
    static const char *device[]={"CON","PRN","AUX","NUL","COM1","COM2",
      "COM3","COM4","COM5","COM6","COM7","COM8","COM9","LPT1","LPT2",
      "LPT3","LPT4","LPT5","LPT6","LPT7","LPT8","LPT9"};
    char stem[65];
    int j;
    for (j=0; j<n && base[j]!='.'; j++)
      stem[j]=(char)toupper((unsigned char)base[j]);
    stem[j]=0;
    for (i=0; i<(int)(sizeof(device)/sizeof(device[0])); i++)
      if (strcmp(stem,device[i])==0) return -1; }
#endif
  if (n>=size)
    return -1;
  memcpy(out,base,n+1);
  return 0;
};

// Saves accumulated data after all scans. Returns 0 for complete output,
// 2 for partial output, and -1 on error. The caller owns the descriptor.
int Saverestoredfile(int slot,int force) {
  if(slot<0 || slot>=NFILE || !pb_fproc[slot].busy) return -1;
  t_fproc *pf=&pb_fproc[slot];
  if(pf->mode!=0 || !pf->data || !pf->datavalid || !pf->origsize ||
     pf->origsize>pf->datasize || pf->datasize>(size_t)pf->nblock*NDATA) {
    Reporterror("Unsupported or invalid file metadata"); return -1;
  }
  int incomplete=pf->ndata!=pf->nblock;
  int badcrc=!incomplete && Crc16(pf->data,pf->datasize)!=pf->filecrc;
  int partial=incomplete || badcrc;
  if(partial && !force) {
    Reporterror(incomplete?"Incomplete file":"File checksum mismatch"); return -1;
  }
  if(badcrc) fprintf(stderr,"Warning: file checksum mismatch; saving recovered bytes unchanged\n");
  uchar *data=pf->data;
  uint32_t length=pf->origsize;
  const char *path=pb_outfile;
  char mapname[MAXPATH+8],chosen[MAXPATH],note[MAXPATH+64];
  FILE *exists;
  // With no -o, the page says what the file was called. Its label is printed
  // when the page is read either way, so what lands on disk is never a surprise.
  if (path[0]==0) {
    if (Namefrompagelabel(pf->name,chosen,sizeof(chosen))<0) {
      Reporterror("The page carries no name that can be a file; pass -o");
      return -1; };
    path=chosen;
    // An explicit -o overwrites, as it always has: the caller named that path.
    // A name taken off a sheet must not, or a restore run in the wrong directory
    // quietly eats whatever happened to share the name.
    exists=fopen(path,"rb");
    if (exists!=NULL) {
      fclose(exists);
      snprintf(note,sizeof(note),
        "%s is already here; pass -o to restore it somewhere else",path);
      Reporterror(note);
      return -1; }; };
  // This clears pf->data in place, including the parity written into gaps by
  // Addblock, so it is only safe because the CLI saves once after every scan.
  // Turning pb_autosave on would destroy parity later pages still need.
  for(int i=0;i<pf->nblock;i++)
    if(pf->datavalid[i]!=1) memset(data+(size_t)i*NDATA,0,NDATA);
  // Restored backups routinely hold secrets, and the page carries only one
  // attribute bit, so never widen access: create the file as 0600. The umask
  // covers --force output too, which skips the attribute block below.
#ifdef __linux__
  mode_t oldmask=umask(0077);
#endif
  FILE *out=fopen(path,"wb");
#ifdef __linux__
  umask(oldmask);
#endif
  if(!out) {Reporterror("Unable to create output");goto failed;}
  int ok=fwrite(data,1,length,out)==length;
  if(fclose(out)!=0) ok=0;
  if(!ok) {Reporterror("Output write error");goto failed;}
#ifdef __linux__
  chmod(path,0600);                    // an existing output keeps its old mode otherwise
#endif
  {
    // Rewrite the map on every save, so a later complete restore cannot leave stale gaps.
    snprintf(mapname,sizeof(mapname),"%s.map",path);
#ifdef __linux__
    oldmask=umask(0077);
#endif
    FILE *map=fopen(mapname,"w");
#ifdef __linux__
    umask(oldmask);
    if(map) chmod(mapname,0600);       // an existing map keeps its old mode otherwise
#endif
    if(!map) {Reporterror("Unable to create missing-range map");goto failed;}
    fprintf(map,"Format: original file, missing bytes filled with zeros\nStatus: %s\nFile checksum: %s\nOriginal bytes: %u\nRecovered blocks: %d/%d\nMissing ranges: start inclusive, end exclusive; offsets in original file\n",
      partial?"DAMAGED":"COMPLETE",incomplete?"not checked (missing blocks)":badcrc?"MISMATCH (damage locations unknown)":"OK",
      length,pf->ndata,pf->nblock);
    for(int i=0;i<pf->nblock;) {
      if(pf->datavalid[i]==1) {i++;continue;}
      uint32_t start=i*NDATA;
      while(i<pf->nblock && pf->datavalid[i]!=1) i++;
      uint32_t end=i*NDATA;if(end>length) end=length;
      if(start<end) fprintf(map,"%u %u\n",start,end);
    }
    ok=!ferror(map);if(fclose(map)!=0) ok=0;
    if(!ok) {Reporterror("Missing-range map write error");goto failed;}
  }
  if(!partial) {
#ifdef _WIN32
    HANDLE handle=CreateFileA(path,FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    FILETIME modified={pf->modified.dwLowDateTime,pf->modified.dwHighDateTime};
    if(handle!=INVALID_HANDLE_VALUE) {SetFileTime(handle,NULL,NULL,&modified);CloseHandle(handle);}
    SetFileAttributesA(path,pf->attributes?pf->attributes:FILE_ATTRIBUTE_NORMAL);
#elif defined(__linux__)
    struct stat st; struct utimbuf times;
    if(stat(path,&st)==0) {times.actime=st.st_atime;times.modtime=convertToPosixTime(pf->modified);utime(path,&times);}
#endif
  }
  // With --expect the restored bytes are checked against a digest the user
  // wrote down at encode time; the file is still written, so the mismatch is
  // reported rather than hidden.
  int mismatch=0;
  if(pb_expect[0]) {
    char got[SHA256_HEXLEN+1];
    Sha256hex(data,length,got);
    mismatch=strcmp(got,pb_expect)!=0;
    if(mismatch)
      fprintf(stderr,"SHA-256 of the restored file is %s, --expect said %s\n",got,pb_expect);
    else
      printf("SHA-256 %s matches --expect\n",got);
  }
  printf("Saved %s%s\n",path,mismatch?" (HASH MISMATCH)":partial?" (DAMAGED)":"");
  if(mismatch) {Reporterror("Restored file does not match --expect");return -1;}
  return partial?2:0;
failed:
  return -1;
}

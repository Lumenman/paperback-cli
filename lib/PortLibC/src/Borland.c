/*
 * =====================================================================================
 *
 *       Filename:  Borland.c
 *
 *    Description:  Various Borland function replacements
 *
 *        Version:  1.0
 *        Created:  08/24/2017 10:00:40 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  surkeh@protonmail.com
 *
 * =====================================================================================
 */
#include "Borland.h"




/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  fnsplit
 *  Description:  Separates a path into components
 *         NOTE:  Does not handle wildcard * due to shell handling this in Linux
 *       Return:  int, bitflags of what components were found in path, NULL args included
 *     Argument:  const char *, Valid Windows/Posix path
 *     Argument:  char *, Returns drive component, if not NULL and drive was found
 *     Argument:  char *, Returns directory component, if not NULL and dir was found
 *     Argument:  char *, Returns file name component, if not NULL and dir was found
 *     Argument:  char *, Returns extension component, if not NULL and dir was found
 * =====================================================================================
 */
int fnsplit(const char *path, 
                   char *drive, 
                   char *dir, 
                   char *name,
                   char *ext) 
{
  int flags=0;
  if(drive) *drive=0;
  if(dir) *dir=0;
  if(name) *name=0;
  if(ext) *ext=0;
  if(!path || !*path) return 0;
  const char *start=path;
  if(path[1]==':') {
    flags|=DRIVE;
    if(drive) {memcpy(drive,path,2);drive[2]=0;}
    start+=2;
  }
  const char *base=start;
  for(const char *p=start;*p;p++) if(*p=='/' || *p=='\\') base=p+1;
  if(base!=start) {
    size_t n=base-start; if(n>=MAXDIR) n=MAXDIR-1;
    if(dir) {memcpy(dir,start,n);dir[n]=0;} flags|=DIRECTORY;
  }
  const char *dot=strrchr(base,'.');
  if(dot==base) dot=NULL;
  size_t n=dot?(size_t)(dot-base):strlen(base);
  if(n) {if(n>=MAXFILE) n=MAXFILE-1;if(name) {memcpy(name,base,n);name[n]=0;}flags|=FILENAME;}
  if(dot) {if(ext) {strncpy(ext,dot,MAXEXT-1);ext[MAXEXT-1]=0;}flags|=EXTENSION;}
  return flags;
}


/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  fnmerge
 *  Description:  Combines components into a path
 *         NOTE:  Does not handle wildcard * due to shell handling this in Linux
 *     Argument:  char *, Returns assembled path
 *     Argument:  const char *, Drive component
 *     Argument:  const char *, Directory component
 *     Argument:  const char *, File name component
 *     Argument:  const char *, Extension component
 * =====================================================================================
 */
void fnmerge (char *path,
                     const char *drive,
                     const char *dir,
                     const char *name,
                     const char *ext)
{
  if (path == NULL)
    return;

  path[0] = '\0';

  if (drive != NULL)
    strcat (path, drive);
  if (dir != NULL)
    strcat (path, dir);
  if (name != NULL)
    strcat (path, name);
  if (ext != NULL)
    strcat (path, ext);
}




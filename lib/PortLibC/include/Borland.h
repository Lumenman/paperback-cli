/*
 * =====================================================================================
 *
 *       Filename:  Borland.h
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
#ifndef PORTLIBC_BORLAND_H
#define PORTLIBC_BORLAND_H

#include <string.h>

#ifndef NULL
#define NULL 0
#endif




// Values set by Borland compiler
#define MAXDRIVE 3
// For 16-bit cpu
//#define MAXPATH  80
//#define MAXDIR   66 
//#define MAXFILE  9 
//#define MAXEXT   5    
// For 32-bit cpu  (#if defined(__FLAT__))
#define MAXPATH   260
#define MAXDIR    256
#define MAXFILE   256
#define MAXEXT    256
// For Borlands fnsplit function
#define WILDCARDS 0x01
#define EXTENSION 0x02
#define FILENAME  0x04
#define DIRECTORY 0x08
#define DRIVE     0x10




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
                   char *ext);


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
                     const char *ext);


#endif //PORTLIBC_BORLAND_H


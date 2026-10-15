/*
 * =====================================================================================
 *
 *       Filename:  paperbak.c
 *
 *    Description:  
 *
 *        Version:  0.1 
 *        Created:  10/04/2017 02:53:12 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  surkeh@protonmail.com
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include "paperbak.h"

////////////////////////////////////////////////////////////////////////////////
////////////////////////////// SERVICE FUNCTIONS ///////////////////////////////


void Reporterror(const char *input) 
{
  // stdout is buffered and stderr is not, so without this an error surfaces
  // above the lines it belongs under - in a redirected run it lands at the
  // very top, reading as though it happened before the work it interrupted.
  fflush(stdout);
  fprintf(stderr,"%s\n", input);
  pb_errors++;
}



void Message(const char *input, int progress) 
{
  (void)progress;                      // No progress bar on a CLI
  //printf("%s @ %d\%\n", input, progress);
  printf("%s\n", input);
}



// Formerly standard case insentitive cstring compare
int strnicmp (const char *str1, const char *str2, size_t len)
{
  for (size_t i=0;i<len;i++) {
    int a=tolower((unsigned char)str1[i]), b=tolower((unsigned char)str2[i]);
    if (a!=b) return a-b;
    if (!a) return 0;
  }
  return 0;
}



int max (int a, int b) 
{
  return a > b ? a : b;
}

int min (int a, int b) 
{
  return a < b ? a : b;
}


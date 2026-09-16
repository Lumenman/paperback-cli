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
  fprintf(stderr,"%s\n", input);
  pb_errors++;
}



void Message(const char *input, int progress) 
{
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

////////////////////////////////////////////////////////////////////////////////
////////////////////////// WINDOWS SERVICE FUNCTIONS ///////////////////////////

#if defined(_WIN32) || defined(__CYGWIN__)
// Converts file date and time into the text according to system defaults and
// places into the string s of length n. Returns number of characters in s.
int Filetimetotext(FILETIME *fttime,char *s,int n) {
  int l;
  SYSTEMTIME sttime;
  FileTimeToSystemTime(fttime,&sttime);
  l=GetDateFormat(LOCALE_USER_DEFAULT,DATE_SHORTDATE,&sttime,NULL,s,n);
  s[l-1]=' ';                          // Yuck, that's Windows
  l+=GetTimeFormat(LOCALE_USER_DEFAULT,TIME_NOSECONDS,&sttime,NULL,s+l,n-l);
  return l;
};

void print_filetime(FILETIME ftime) {
    char str[30];
    int ok = Filetimetotext(&ftime, str, 30);
    if (ok) {
      printf("%s\n", str);
    }
}

#endif


/*
 * =====================================================================================
 *
 *       Filename:  FileAttributes.h
 *
 *    Description:  
 *
 *        Version:  3.0
 *        Created:  08/23/2017 02:36:05 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  surkeh@protonmail.com
 *
 * =====================================================================================
 */
#include "FileAttributes.h"




/*
 * ===  FUNCTION  ======================================================================
 *        Name:  isPosixTime
 * Description:  Check if a 64-bit value is in a range of possible time_t values
 *     Returns:  int, 0 if success, negative value if failed
 * =====================================================================================
 */
int isPosixTime(time_t t)
{
  time_t now;
  time(&now);
  if(t > UNIX_EPOCH && t <= now)
    return 0;
  else
    return -1;
}

/*
 * ===  FUNCTION  ======================================================================
 *        Name:  convertToPosixTime
 * Description:  Convert FILETIME to time_t
 *     Returns:  time_t, translated FileTimePortable to equivalent time_t
 * =====================================================================================
 */
time_t convertToPosixTime (FileTimePortable windowsTime) 
{
  uint64_t ticks  = windowsTime.dwHighDateTime;
  ticks           =  ticks << 32;
  ticks          |= windowsTime.dwLowDateTime;

  return (time_t)( ticks / WINDOWS_TICK  - SEC_TO_UNIX_EPOCH );
}

/*
 * ===  FUNCTION  ======================================================================
 *        Name:  convertToFileTime
 * Description:  Convert time_t to FILETIME
 *     Returns:  FileTimePortable, struct of same size and structure as FILETIME
 * =====================================================================================
 */
FileTimePortable convertToFileTime (time_t posixTime) 
{
  uint64_t winTime = WINDOWS_TICK * (uint64_t)(posixTime + SEC_TO_UNIX_EPOCH);
  FileTimePortable fileTime;
  uint32_t highBytes = winTime >> 32;
  fileTime.dwHighDateTime = highBytes;
  fileTime.dwLowDateTime = winTime; // just 32 lower bits
  return fileTime;
}

/*
 * ===  FUNCTION  ======================================================================
 *        Name:  convertToWindowsAttributes
 * Description:  Convert mode_t into Windows attributes type
 *     Returns:  uint32_t, Windows attributes bitvector
 * =====================================================================================
 */
uint32_t convertToWindowsAttributes(uint32_t posixMode)
{
  uint32_t readAccess = 
    posixMode & ( POSIX_RUSR | POSIX_RGRP | POSIX_ROTH );
  uint32_t writeAccess =
    posixMode & ( POSIX_WUSR | POSIX_WGRP | POSIX_WOTH );
  uint32_t executeAccess =
    posixMode & ( POSIX_XUSR | POSIX_XGRP | POSIX_XOTH );

  if (readAccess != 0 && writeAccess == 0 && executeAccess == 0)
    return FILE_ATTRIBUTE_READONLY;
  else
    return FILE_ATTRIBUTE_NORMAL;
}

/*
 * ===  FUNCTION  ======================================================================
 *        Name:  convertToPosixAttributes
 * Description:  Converts Windows attributes type into mode_t
 *     Returns:  uint32_t, mode_t bitvector
 * =====================================================================================
 */
uint32_t convertToPosixAttributes(uint32_t windowsAttributes)
{
    if (windowsAttributes & FILE_ATTRIBUTE_READONLY)
      return POSIX_READONLY;
    else
      return POSIX_NORMAL;
}


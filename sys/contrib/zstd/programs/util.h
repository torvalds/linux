/*
 * Copyright (c) 2016-present, Przemyslaw Skibinski, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef UTIL_H_MODULE
#define UTIL_H_MODULE

#if defined (__cplusplus)
extern "C" {
#endif


/*-****************************************
*  Dependencies
******************************************/
#include "platform.h"     /* PLATFORM_POSIX_VERSION, ZSTD_NANOSLEEP_SUPPORT, ZSTD_SETPRIORITY_SUPPORT */
#include <stdlib.h>       /* malloc, realloc, free */
#include <stddef.h>       /* size_t, ptrdiff_t */
#include <stdio.h>        /* fprintf */
#include <sys/types.h>    /* stat, utime */
#include <sys/stat.h>     /* stat, chmod */
#if defined(_MSC_VER)
#  include <sys/utime.h>  /* utime */
#  include <io.h>         /* _chmod */
#else
#  include <unistd.h>     /* chown, stat */
#  include <utime.h>      /* utime */
#endif
#include <time.h>         /* clock_t, clock, CLOCKS_PER_SEC, nanosleep */
#include "mem.h"          /* U32, U64 */


/*-************************************************************
* Avoid fseek()'s 2GiB barrier with MSVC, macOS, *BSD, MinGW
***************************************************************/
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
#   define UTIL_fseek _fseeki64
#elif !defined(__64BIT__) && (PLATFORM_POSIX_VERSION >= 200112L) /* No point defining Large file for 64 bit */
#  define UTIL_fseek fseeko
#elif defined(__MINGW32__) && defined(__MSVCRT__) && !defined(__STRICT_ANSI__) && !defined(__NO_MINGW_LFS)
#   define UTIL_fseek fseeko64
#else
#   define UTIL_fseek fseek
#endif


/*-*************************************************
*  Sleep & priority functions: Windows - Posix - others
***************************************************/
#if defined(_WIN32)
#  include <windows.h>
#  define SET_REALTIME_PRIORITY SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)
#  define UTIL_sleep(s) Sleep(1000*s)
#  define UTIL_sleepMilli(milli) Sleep(milli)

#elif PLATFORM_POSIX_VERSION > 0 /* Unix-like operating system */
#  include <unistd.h>   /* sleep */
#  define UTIL_sleep(s) sleep(s)
#  if ZSTD_NANOSLEEP_SUPPORT   /* necessarily defined in platform.h */
#      define UTIL_sleepMilli(milli) { struct timespec t; t.tv_sec=0; t.tv_nsec=milli*1000000ULL; nanosleep(&t, NULL); }
#  else
#      define UTIL_sleepMilli(milli) /* disabled */
#  endif
#  if ZSTD_SETPRIORITY_SUPPORT
#    include <sys/resource.h> /* setpriority */
#    define SET_REALTIME_PRIORITY setpriority(PRIO_PROCESS, 0, -20)
#  else
#    define SET_REALTIME_PRIORITY /* disabled */
#  endif

#else  /* unknown non-unix operating systen */
#  define UTIL_sleep(s)          /* disabled */
#  define UTIL_sleepMilli(milli) /* disabled */
#  define SET_REALTIME_PRIORITY  /* disabled */
#endif


/*-*************************************
*  Constants
***************************************/
#define LIST_SIZE_INCREASE   (8*1024)


/*-****************************************
*  Compiler specifics
******************************************/
#if defined(__INTEL_COMPILER)
#  pragma warning(disable : 177)    /* disable: message #177: function was declared but never referenced, useful with UTIL_STATIC */
#endif
#if defined(__GNUC__)
#  define UTIL_STATIC static __attribute__((unused))
#elif defined (__cplusplus) || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
#  define UTIL_STATIC static inline
#elif defined(_MSC_VER)
#  define UTIL_STATIC static __inline
#else
#  define UTIL_STATIC static  /* this version may generate warnings for unused static functions; disable the relevant warning */
#endif


/*-****************************************
*  Console log
******************************************/
extern int g_utilDisplayLevel;
#define UTIL_DISPLAY(...)         fprintf(stderr, __VA_ARGS__)
#define UTIL_DISPLAYLEVEL(l, ...) { if (g_utilDisplayLevel>=l) { UTIL_DISPLAY(__VA_ARGS__); } }


/*-****************************************
*  Time functions
******************************************/
#if defined(_WIN32)   /* Windows */

    #define UTIL_TIME_INITIALIZER { { 0, 0 } }
    typedef LARGE_INTEGER UTIL_time_t;

#elif defined(__APPLE__) && defined(__MACH__)

    #include <mach/mach_time.h>
    #define UTIL_TIME_INITIALIZER 0
    typedef U64 UTIL_time_t;

#elif (PLATFORM_POSIX_VERSION >= 200112L) \
   && (defined(__UCLIBC__)                \
      || (defined(__GLIBC__)              \
          && ((__GLIBC__ == 2 && __GLIBC_MINOR__ >= 17) \
             || (__GLIBC__ > 2))))

    #define UTIL_TIME_INITIALIZER { 0, 0 }
    typedef struct timespec UTIL_freq_t;
    typedef struct timespec UTIL_time_t;

    UTIL_time_t UTIL_getSpanTime(UTIL_time_t begin, UTIL_time_t end);

#else   /* relies on standard C (note : clock_t measurements can be wrong when using multi-threading) */

    typedef clock_t UTIL_time_t;
    #define UTIL_TIME_INITIALIZER 0

#endif

UTIL_time_t UTIL_getTime(void);
U64 UTIL_getSpanTimeMicro(UTIL_time_t clockStart, UTIL_time_t clockEnd);
U64 UTIL_getSpanTimeNano(UTIL_time_t clockStart, UTIL_time_t clockEnd);

#define SEC_TO_MICRO 1000000

/* returns time span in microseconds */
U64 UTIL_clockSpanMicro(UTIL_time_t clockStart);

/* returns time span in microseconds */
U64 UTIL_clockSpanNano(UTIL_time_t clockStart);
void UTIL_waitForNextTick(void);

/*-****************************************
*  File functions
******************************************/
#if defined(_MSC_VER)
    #define chmod _chmod
    typedef struct __stat64 stat_t;
#else
    typedef struct stat stat_t;
#endif


int UTIL_fileExist(const char* filename);
int UTIL_isRegularFile(const char* infilename);
int UTIL_setFileStat(const char* filename, stat_t* statbuf);
U32 UTIL_isDirectory(const char* infilename);
int UTIL_getFileStat(const char* infilename, stat_t* statbuf);

U32 UTIL_isLink(const char* infilename);
#define UTIL_FILESIZE_UNKNOWN  ((U64)(-1))
U64 UTIL_getFileSize(const char* infilename);

U64 UTIL_getTotalFileSize(const char* const * const fileNamesTable, unsigned nbFiles);

/*
 * A modified version of realloc().
 * If UTIL_realloc() fails the original block is freed.
*/
UTIL_STATIC void* UTIL_realloc(void *ptr, size_t size)
{
    void *newptr = realloc(ptr, size);
    if (newptr) return newptr;
    free(ptr);
    return NULL;
}

int UTIL_prepareFileList(const char* dirName, char** bufStart, size_t* pos, char** bufEnd, int followLinks);
#ifdef _WIN32
#  define UTIL_HAS_CREATEFILELIST
#elif defined(__linux__) || (PLATFORM_POSIX_VERSION >= 200112L)  /* opendir, readdir require POSIX.1-2001 */
#  define UTIL_HAS_CREATEFILELIST
#  include <dirent.h>       /* opendir, readdir */
#  include <string.h>       /* strerror, memcpy */
#else
#endif /* #ifdef _WIN32 */

/*
 * UTIL_createFileList - takes a list of files and directories (params: inputNames, inputNamesNb), scans directories,
 *                       and returns a new list of files (params: return value, allocatedBuffer, allocatedNamesNb).
 * After finishing usage of the list the structures should be freed with UTIL_freeFileList(params: return value, allocatedBuffer)
 * In case of error UTIL_createFileList returns NULL and UTIL_freeFileList should not be called.
 */
const char**
UTIL_createFileList(const char **inputNames, unsigned inputNamesNb,
                    char** allocatedBuffer, unsigned* allocatedNamesNb,
                    int followLinks);

UTIL_STATIC void UTIL_freeFileList(const char** filenameTable, char* allocatedBuffer)
{
    if (allocatedBuffer) free(allocatedBuffer);
    if (filenameTable) free((void*)filenameTable);
}

int UTIL_countPhysicalCores(void);

#if defined (__cplusplus)
}
#endif

#endif /* UTIL_H_MODULE */

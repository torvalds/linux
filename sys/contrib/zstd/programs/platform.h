/*
 * Copyright (c) 2016-present, Przemyslaw Skibinski, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef PLATFORM_H_MODULE
#define PLATFORM_H_MODULE

#if defined (__cplusplus)
extern "C" {
#endif



/* **************************************
*  Compiler Options
****************************************/
#if defined(_MSC_VER)
#  define _CRT_SECURE_NO_WARNINGS    /* Disable Visual Studio warning messages for fopen, strncpy, strerror */
#  if (_MSC_VER <= 1800)             /* 1800 == Visual Studio 2013 */
#    define _CRT_SECURE_NO_DEPRECATE /* VS2005 - must be declared before <io.h> and <windows.h> */
#    define snprintf sprintf_s       /* snprintf unsupported by Visual <= 2013 */
#  endif
#  pragma warning(disable : 4127)    /* disable: C4127: conditional expression is constant */
#endif


/* **************************************
*  Detect 64-bit OS
*  http://nadeausoftware.com/articles/2012/02/c_c_tip_how_detect_processor_type_using_compiler_predefined_macros
****************************************/
#if defined __ia64 || defined _M_IA64                                                                               /* Intel Itanium */ \
  || defined __powerpc64__ || defined __ppc64__ || defined __PPC64__                                                /* POWER 64-bit */  \
  || (defined __sparc && (defined __sparcv9 || defined __sparc_v9__ || defined __arch64__)) || defined __sparc64__  /* SPARC 64-bit */  \
  || defined __x86_64__s || defined _M_X64                                                                          /* x86 64-bit */    \
  || defined __arm64__ || defined __aarch64__ || defined __ARM64_ARCH_8__                                           /* ARM 64-bit */    \
  || (defined __mips  && (__mips == 64 || __mips == 4 || __mips == 3))                                              /* MIPS 64-bit */   \
  || defined _LP64 || defined __LP64__ /* NetBSD, OpenBSD */ || defined __64BIT__ /* AIX */ || defined _ADDR64 /* Cray */               \
  || (defined __SIZEOF_POINTER__ && __SIZEOF_POINTER__ == 8) /* gcc */
#  if !defined(__64BIT__)
#    define __64BIT__  1
#  endif
#endif


/* *********************************************************
*  Turn on Large Files support (>4GB) for 32-bit Linux/Unix
***********************************************************/
#if !defined(__64BIT__) || defined(__MINGW32__)    /* No point defining Large file for 64 bit but MinGW-w64 requires it */
#  if !defined(_FILE_OFFSET_BITS)
#    define _FILE_OFFSET_BITS 64                   /* turn off_t into a 64-bit type for ftello, fseeko */
#  endif
#  if !defined(_LARGEFILE_SOURCE)                  /* obsolete macro, replaced with _FILE_OFFSET_BITS */
#    define _LARGEFILE_SOURCE 1                    /* Large File Support extension (LFS) - fseeko, ftello */
#  endif
#  if defined(_AIX) || defined(__hpux)
#    define _LARGE_FILES                           /* Large file support on 32-bits AIX and HP-UX */
#  endif
#endif


/* ************************************************************
*  Detect POSIX version
*  PLATFORM_POSIX_VERSION = 0 for non-Unix e.g. Windows
*  PLATFORM_POSIX_VERSION = 1 for Unix-like but non-POSIX
*  PLATFORM_POSIX_VERSION > 1 is equal to found _POSIX_VERSION
*  Value of PLATFORM_POSIX_VERSION can be forced on command line
***************************************************************/
#ifndef PLATFORM_POSIX_VERSION

#  if (defined(__APPLE__) && defined(__MACH__)) || defined(__SVR4) || defined(_AIX) || defined(__hpux) /* POSIX.1-2001 (SUSv3) conformant */ \
     || defined(__DragonFly__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)  /* BSD distros */
     /* exception rule : force posix version to 200112L,
      * note: it's better to use unistd.h's _POSIX_VERSION whenever possible */
#    define PLATFORM_POSIX_VERSION 200112L

/* try to determine posix version through official unistd.h's _POSIX_VERSION (http://pubs.opengroup.org/onlinepubs/7908799/xsh/unistd.h.html).
 * note : there is no simple way to know in advance if <unistd.h> is present or not on target system,
 * Posix specification mandates its presence and its content, but target system must respect this spec.
 * It's necessary to _not_ #include <unistd.h> whenever target OS is not unix-like
 * otherwise it will block preprocessing stage.
 * The following list of build macros tries to "guess" if target OS is likely unix-like, and therefore can #include <unistd.h>
 */
#  elif !defined(_WIN32) \
     && (defined(__unix__) || defined(__unix) \
     || defined(__midipix__) || defined(__VMS) || defined(__HAIKU__))

#    if defined(__linux__) || defined(__linux)
#      ifndef _POSIX_C_SOURCE
#        define _POSIX_C_SOURCE 200112L  /* feature test macro : https://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html */
#      endif
#    endif
#    include <unistd.h>  /* declares _POSIX_VERSION */
#    if defined(_POSIX_VERSION)  /* POSIX compliant */
#      define PLATFORM_POSIX_VERSION _POSIX_VERSION
#    else
#      define PLATFORM_POSIX_VERSION 1
#    endif

#  else  /* non-unix target platform (like Windows) */
#    define PLATFORM_POSIX_VERSION 0
#  endif

#endif   /* PLATFORM_POSIX_VERSION */

/*-*********************************************
*  Detect if isatty() and fileno() are available
************************************************/
#if (defined(__linux__) && (PLATFORM_POSIX_VERSION > 1)) \
 || (PLATFORM_POSIX_VERSION >= 200112L) \
 || defined(__DJGPP__) \
 || defined(__MSYS__)
#  include <unistd.h>   /* isatty */
#  define IS_CONSOLE(stdStream) isatty(fileno(stdStream))
#elif defined(MSDOS) || defined(OS2) || defined(__CYGWIN__)
#  include <io.h>       /* _isatty */
#  define IS_CONSOLE(stdStream) _isatty(_fileno(stdStream))
#elif defined(WIN32) || defined(_WIN32)
#  include <io.h>      /* _isatty */
#  include <windows.h> /* DeviceIoControl, HANDLE, FSCTL_SET_SPARSE */
#  include <stdio.h>   /* FILE */
static __inline int IS_CONSOLE(FILE* stdStream) {
    DWORD dummy;
    return _isatty(_fileno(stdStream)) && GetConsoleMode((HANDLE)_get_osfhandle(_fileno(stdStream)), &dummy);
}
#else
#  define IS_CONSOLE(stdStream) 0
#endif


/******************************
*  OS-specific IO behaviors
******************************/
#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(_WIN32)
#  include <fcntl.h>   /* _O_BINARY */
#  include <io.h>      /* _setmode, _fileno, _get_osfhandle */
#  if !defined(__DJGPP__)
#    include <windows.h> /* DeviceIoControl, HANDLE, FSCTL_SET_SPARSE */
#    include <winioctl.h> /* FSCTL_SET_SPARSE */
#    define SET_BINARY_MODE(file) { int const unused=_setmode(_fileno(file), _O_BINARY); (void)unused; }
#    define SET_SPARSE_FILE_MODE(file) { DWORD dw; DeviceIoControl((HANDLE) _get_osfhandle(_fileno(file)), FSCTL_SET_SPARSE, 0, 0, 0, 0, &dw, 0); }
#  else
#    define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#    define SET_SPARSE_FILE_MODE(file)
#  endif
#else
#  define SET_BINARY_MODE(file)
#  define SET_SPARSE_FILE_MODE(file)
#endif


#ifndef ZSTD_SPARSE_DEFAULT
#  if (defined(__APPLE__) && defined(__MACH__))
#    define ZSTD_SPARSE_DEFAULT 0
#  else
#    define ZSTD_SPARSE_DEFAULT 1
#  endif
#endif


#ifndef ZSTD_START_SYMBOLLIST_FRAME
#  ifdef __linux__
#    define ZSTD_START_SYMBOLLIST_FRAME 2
#  elif defined __APPLE__
#    define ZSTD_START_SYMBOLLIST_FRAME 4
#  else
#    define ZSTD_START_SYMBOLLIST_FRAME 0
#  endif
#endif


#ifndef ZSTD_SETPRIORITY_SUPPORT
   /* mandates presence of <sys/resource.h> and support for setpriority() : http://man7.org/linux/man-pages/man2/setpriority.2.html */
#  define ZSTD_SETPRIORITY_SUPPORT (PLATFORM_POSIX_VERSION >= 200112L)
#endif


#ifndef ZSTD_NANOSLEEP_SUPPORT
   /* mandates support of nanosleep() within <time.h> : http://man7.org/linux/man-pages/man2/nanosleep.2.html */
#  if (defined(__linux__) && (PLATFORM_POSIX_VERSION >= 199309L)) \
   || (PLATFORM_POSIX_VERSION >= 200112L)
#     define ZSTD_NANOSLEEP_SUPPORT 1
#  else
#     define ZSTD_NANOSLEEP_SUPPORT 0
#  endif
#endif


#if defined (__cplusplus)
}
#endif

#endif /* PLATFORM_H_MODULE */

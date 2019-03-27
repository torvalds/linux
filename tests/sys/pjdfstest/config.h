/* $FreeBSD$ */
/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* Define to 1 if NFSv4 ACL support is available */
#define HAS_NFSV4_ACL_SUPPORT 1

/* Define to 1 if you have the `acl_create_entry_np' function. */
#define HAVE_ACL_CREATE_ENTRY_NP 1

/* Define to 1 if you have the `acl_from_text' function. */
#define HAVE_ACL_FROM_TEXT 1

/* Define to 1 if you have the `acl_get_entry' function. */
#define HAVE_ACL_GET_ENTRY 1

/* Define to 1 if you have the `acl_get_file' function. */
#define HAVE_ACL_GET_FILE 1

/* Define to 1 if you have the `acl_set_file' function. */
#define HAVE_ACL_SET_FILE 1

/* Define if bindat exists */
#define HAVE_BINDAT 1

/* Define if chflags exists */
#define HAVE_CHFLAGS 1

/* Define if chflagsat exists */
#define HAVE_CHFLAGSAT 1

/* Define if connectat exists */
#define HAVE_CONNECTAT 1

/* Define if faccessat exists */
#define HAVE_FACCESSAT 1

/* Define if fchflags exists */
#define HAVE_FCHFLAGS 1

/* Define if fchmodat exists */
#define HAVE_FCHMODAT 1

/* Define if fchownat exists */
#define HAVE_FCHOWNAT 1

/* Define if fstatat exists */
#define HAVE_FSTATAT 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define if lchflags exists */
#define HAVE_LCHFLAGS 1

/* Define if lchmod exists */
#define HAVE_LCHMOD 1

/* Define if linkat exists */
#define HAVE_LINKAT 1

/* Define if lpathconf exists */
#define HAVE_LPATHCONF 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define if mkdirat exists */
#define HAVE_MKDIRAT 1

/* Define if mkfifoat exists */
#define HAVE_MKFIFOAT 1

/* Define if mknodat exists */
#define HAVE_MKNODAT 1

/* Define if openat exists */
#define HAVE_OPENAT 1

/* Define if posix_fallocate exists */
#define HAVE_POSIX_FALLOCATE 1

/* Define if readlinkat exists */
#define HAVE_READLINKAT 1

/* Define if renameat exists */
#define HAVE_RENAMEAT 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if `st_atim' is a member of `struct stat'. */
#define HAVE_STRUCT_STAT_ST_ATIM 1

/* Define to 1 if `st_atimespec' is a member of `struct stat'. */
#define HAVE_STRUCT_STAT_ST_ATIMESPEC 1

/* Define to 1 if `st_birthtim' is a member of `struct stat'. */
#define HAVE_STRUCT_STAT_ST_BIRTHTIM 1

/* Define to 1 if `st_birthtime' is a member of `struct stat'. */
#define HAVE_STRUCT_STAT_ST_BIRTHTIME 1

/* Define to 1 if `st_birthtimespec' is a member of `struct stat'. */
#define HAVE_STRUCT_STAT_ST_BIRTHTIMESPEC 1

/* Define to 1 if `st_ctim' is a member of `struct stat'. */
#define HAVE_STRUCT_STAT_ST_CTIM 1

/* Define to 1 if `st_ctimespec' is a member of `struct stat'. */
#define HAVE_STRUCT_STAT_ST_CTIMESPEC 1

/* Define to 1 if `st_mtim' is a member of `struct stat'. */
#define HAVE_STRUCT_STAT_ST_MTIM 1

/* Define to 1 if `st_mtimespec' is a member of `struct stat'. */
#define HAVE_STRUCT_STAT_ST_MTIMESPEC 1

/* Define if symlinkat exists */
#define HAVE_SYMLINKAT 1

/* Define to 1 if sys/acl.h is available */
#define HAVE_SYS_ACL_H 1

/* Define to 1 if you have the <sys/mkdev.h> header file. */
/* #undef HAVE_SYS_MKDEV_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define if utimensat exists */
#define HAVE_UTIMENSAT 1

/* Name of package */
#define PACKAGE "pjdfstest"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME "pjdfstest"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "pjdfstest 0.1"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "pjdfstest"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "0.1"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif


/* Version number of package */
#define VERSION "0.1"

/* Define to 1 if on MINIX. */
/* #undef _MINIX */

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
/* #undef _POSIX_1_SOURCE */

/* Define to 1 if you need to in order for `stat' and other things to work. */
/* #undef _POSIX_SOURCE */

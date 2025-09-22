/* config.h.  Generated automatically by configure.  */
/* config.h.in.  Maintained by hand. */

/* Define if on MINIX.  */
/* #undef _MINIX */

/* Define as the return type of signal handlers (int or void).  */
#define RETSIGTYPE void

/* Characteristics of the compiler. */
/* #undef const */

/* #undef size_t */

/* #undef ssize_t */

#define PROTOTYPES 1

/* #undef __CHAR_UNSIGNED__ */

/* Define if the `S_IS*' macros in <sys/stat.h> do not work properly.  */
/* #undef STAT_MACROS_BROKEN */

#define VOID_SIGHANDLER 1

/* Define if you have the isascii function. */
#define HAVE_ISASCII 1

/* Define if you have the isxdigit function. */
#define HAVE_ISXDIGIT 1

/* Define if you have the lstat function. */
#define HAVE_LSTAT 1

/* Define if you have the mbsrtowcs function. */
/* #undef HAVE_MBSRTOWCS */

/* Define if you have the memmove function. */
#define HAVE_MEMMOVE 1

/* Define if you have the putenv function.  */
#define HAVE_PUTENV 1

/* Define if you have the select function.  */
#define HAVE_SELECT 1

/* Define if you have the setenv function.  */
#define HAVE_SETENV 1

/* Define if you have the setlocale function. */
#define HAVE_SETLOCALE 1

/* Define if you have the strcasecmp function.  */
#define HAVE_STRCASECMP 1

/* Define if you have the strcoll function.  */
#define HAVE_STRCOLL 1

/* #undef STRCOLL_BROKEN */

/* Define if you have the strpbrk function.  */
#define HAVE_STRPBRK 1

/* Define if you have the tcgetattr function.  */
#define HAVE_TCGETATTR 1

/* Define if you have the vsnprintf function.  */
#define HAVE_VSNPRINTF 1

/* Define if you have the wcwidth function.  */
/* #undef HAVE_WCWIDTH */

/* Define if you have the <dirent.h> header file.  */
#define HAVE_DIRENT_H 1

/* Define if you have the <langinfo.h> header file.  */
#define HAVE_LANGINFO_H 1

/* Define if you have the <limits.h> header file.  */
#define HAVE_LIMITS_H 1

/* Define if you have the <locale.h> header file.  */
#define HAVE_LOCALE_H 1

/* Define if you have the <memory.h> header file.  */
#define HAVE_MEMORY_H 1

/* Define if you have the <ndir.h> header file.  */
/* #undef HAVE_NDIR_H */

/* Define if you have the <stdarg.h> header file.  */
#define HAVE_STDARG_H 1

/* Define if you have the <stdlib.h> header file.  */
#define HAVE_STDLIB_H 1

/* Define if you have the <string.h> header file.  */
#define HAVE_STRING_H 1

/* Define if you have the <strings.h> header file.  */
#define HAVE_STRINGS_H 1

/* Define if you have the <sys/dir.h> header file.  */
/* #undef HAVE_SYS_DIR_H */

/* Define if you have the <sys/file.h> header file.  */
#define HAVE_SYS_FILE_H 1

/* Define if you have the <sys/ndir.h> header file.  */
/* #undef HAVE_SYS_NDIR_H */

/* Define if you have the <sys/pte.h> header file.  */
/* #undef HAVE_SYS_PTE_H */

/* Define if you have the <sys/ptem.h> header file.  */
/* #undef HAVE_SYS_PTEM_H */

/* Define if you have the <sys/select.h> header file.  */
#define HAVE_SYS_SELECT_H 1

/* Define if you have the <sys/stream.h> header file.  */
/* #undef HAVE_SYS_STREAM_H */

/* Define if you have the <termcap.h> header file.  */
#define HAVE_TERMCAP_H 1

/* Define if you have the <termio.h> header file.  */
/* #undef HAVE_TERMIO_H */

/* Define if you have the <termios.h> header file.  */
#define HAVE_TERMIOS_H 1

/* Define if you have the <unistd.h> header file.  */
#define HAVE_UNISTD_H 1

/* Define if you have the <varargs.h> header file.  */
#define HAVE_VARARGS_H 1

/* Define if you have the <wchar.h> header file.  */
/* #undef HAVE_WCHAR_H */

/* Define if you have the <varargs.h> header file.  */
/* #undef HAVE_WCTYPE_H */

/* #undef HAVE_MBSTATE_T */

/* Define if you have <langinfo.h> and nl_langinfo(CODESET). */
/* #undef HAVE_LANGINFO_CODESET */

/* Definitions pulled in from aclocal.m4. */
#define VOID_SIGHANDLER 1

/* #undef GWINSZ_IN_SYS_IOCTL */

#define STRUCT_WINSIZE_IN_SYS_IOCTL 1

/* #undef STRUCT_WINSIZE_IN_TERMIOS */

#define TIOCSTAT_IN_SYS_IOCTL 1

#define FIONREAD_IN_SYS_IOCTL 1

/* #undef SPEED_T_IN_SYS_TYPES */

#define HAVE_GETPW_DECLS 1

#define STRUCT_DIRENT_HAS_D_INO 1

#define STRUCT_DIRENT_HAS_D_FILENO 1

/* #undef HAVE_BSD_SIGNALS */

#define HAVE_POSIX_SIGNALS 1

/* #undef HAVE_USG_SIGHOLD */

/* #undef MUST_REINSTALL_SIGHANDLERS */

#define HAVE_POSIX_SIGSETJMP 1

/* modify settings or make new ones based on what autoconf tells us. */

/* Ultrix botches type-ahead when switching from canonical to
   non-canonical mode, at least through version 4.3 */
#if !defined (HAVE_TERMIOS_H) || !defined (HAVE_TCGETATTR) || defined (ultrix)
#  define TERMIOS_MISSING
#endif

#if defined (STRCOLL_BROKEN)
#  define HAVE_STRCOLL 1
#endif

#if defined (__STDC__) && defined (HAVE_STDARG_H)
#  define PREFER_STDARG
#  define USE_VARARGS
#else
#  if defined (HAVE_VARARGS_H)
#    define PREFER_VARARGS
#    define USE_VARARGS
#  endif
#endif

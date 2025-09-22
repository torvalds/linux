/* Function declarations for libiberty.

   Copyright 2001, 2002, 2005 Free Software Foundation, Inc.
   
   Note - certain prototypes declared in this header file are for
   functions whoes implementation copyright does not belong to the
   FSF.  Those prototypes are present in this file for reference
   purposes only and their presence in this file should not construed
   as an indication of ownership by the FSF of the implementation of
   those functions in any way or form whatsoever.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.
   
   Written by Cygnus Support, 1994.

   The libiberty library provides a number of functions which are
   missing on some operating systems.  We do not declare those here,
   to avoid conflicts with the system header files on operating
   systems that do support those functions.  In this file we only
   declare those functions which are specific to libiberty.  */

#ifndef LIBIBERTY_H
#define LIBIBERTY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ansidecl.h"

/* Get a definition for size_t.  */
#include <stddef.h>
/* Get a definition for va_list.  */
#include <stdarg.h>

#include <stdio.h>

/* If the OS supports it, ensure that the supplied stream is setup to
   avoid any multi-threaded locking.  Otherwise leave the FILE pointer
   unchanged.  If the stream is NULL do nothing.  */

extern void unlock_stream (FILE *);

/* If the OS supports it, ensure that the standard I/O streams, stdin,
   stdout and stderr are setup to avoid any multi-threaded locking.
   Otherwise do nothing.  */

extern void unlock_std_streams (void);

/* Open and return a FILE pointer.  If the OS supports it, ensure that
   the stream is setup to avoid any multi-threaded locking.  Otherwise
   return the FILE pointer unchanged.  */

extern FILE *fopen_unlocked (const char *, const char *);
extern FILE *fdopen_unlocked (int, const char *);
extern FILE *freopen_unlocked (const char *, const char *, FILE *);

/* Build an argument vector from a string.  Allocates memory using
   malloc.  Use freeargv to free the vector.  */

extern char **buildargv (const char *) ATTRIBUTE_MALLOC;

/* Free a vector returned by buildargv.  */

extern void freeargv (char **);

/* Duplicate an argument vector. Allocates memory using malloc.  Use
   freeargv to free the vector.  */

extern char **dupargv (char **) ATTRIBUTE_MALLOC;

/* Expand "@file" arguments in argv.  */

extern void expandargv PARAMS ((int *, char ***));

/* Return the last component of a path name.  Note that we can't use a
   prototype here because the parameter is declared inconsistently
   across different systems, sometimes as "char *" and sometimes as
   "const char *" */

/* HAVE_DECL_* is a three-state macro: undefined, 0 or 1.  If it is
   undefined, we haven't run the autoconf check so provide the
   declaration without arguments.  If it is 0, we checked and failed
   to find the declaration so provide a fully prototyped one.  If it
   is 1, we found it so don't provide any declaration at all.  */
#if !HAVE_DECL_BASENAME
#if defined (__GNU_LIBRARY__ ) || defined (__linux__) || defined (__FreeBSD__) || defined (__OpenBSD__) || defined(__NetBSD__) || defined (__CYGWIN__) || defined (__CYGWIN32__) || defined (__MINGW32__) || defined (HAVE_DECL_BASENAME)
extern char *basename (const char *);
#else
/* Do not allow basename to be used if there is no prototype seen.  We
   either need to use the above prototype or have one from
   autoconf which would result in HAVE_DECL_BASENAME being set.  */
#define basename basename_cannot_be_used_without_a_prototype
#endif
#endif

/* A well-defined basename () that is always compiled in.  */

extern const char *lbasename (const char *);

/* A well-defined realpath () that is always compiled in.  */

extern char *lrealpath (const char *);

/* Concatenate an arbitrary number of strings.  You must pass NULL as
   the last argument of this function, to terminate the list of
   strings.  Allocates memory using xmalloc.  */

extern char *concat (const char *, ...) ATTRIBUTE_MALLOC ATTRIBUTE_SENTINEL;

/* Concatenate an arbitrary number of strings.  You must pass NULL as
   the last argument of this function, to terminate the list of
   strings.  Allocates memory using xmalloc.  The first argument is
   not one of the strings to be concatenated, but if not NULL is a
   pointer to be freed after the new string is created, similar to the
   way xrealloc works.  */

extern char *reconcat (char *, const char *, ...) ATTRIBUTE_MALLOC ATTRIBUTE_SENTINEL;

/* Determine the length of concatenating an arbitrary number of
   strings.  You must pass NULL as the last argument of this function,
   to terminate the list of strings.  */

extern unsigned long concat_length (const char *, ...) ATTRIBUTE_SENTINEL;

/* Concatenate an arbitrary number of strings into a SUPPLIED area of
   memory.  You must pass NULL as the last argument of this function,
   to terminate the list of strings.  The supplied memory is assumed
   to be large enough.  */

extern char *concat_copy (char *, const char *, ...) ATTRIBUTE_SENTINEL;

/* Concatenate an arbitrary number of strings into a GLOBAL area of
   memory.  You must pass NULL as the last argument of this function,
   to terminate the list of strings.  The supplied memory is assumed
   to be large enough.  */

extern char *concat_copy2 (const char *, ...) ATTRIBUTE_SENTINEL;

/* This is the global area used by concat_copy2.  */

extern char *libiberty_concat_ptr;

/* Concatenate an arbitrary number of strings.  You must pass NULL as
   the last argument of this function, to terminate the list of
   strings.  Allocates memory using alloca.  The arguments are
   evaluated twice!  */
#define ACONCAT(ACONCAT_PARAMS) \
  (libiberty_concat_ptr = (char *) alloca (concat_length ACONCAT_PARAMS + 1), \
   concat_copy2 ACONCAT_PARAMS)

/* Check whether two file descriptors refer to the same file.  */

extern int fdmatch (int fd1, int fd2);

/* Return the position of the first bit set in the argument.  */
/* Prototypes vary from system to system, so we only provide a
   prototype on systems where we know that we need it.  */
#if defined (HAVE_DECL_FFS) && !HAVE_DECL_FFS
extern int ffs(int);
#endif

/* Get the working directory.  The result is cached, so don't call
   chdir() between calls to getpwd().  */

extern char * getpwd (void);

/* Get the current time.  */
/* Prototypes vary from system to system, so we only provide a
   prototype on systems where we know that we need it.  */
#ifdef __MINGW32__
/* Forward declaration to avoid #include <sys/time.h>.   */
struct timeval;
extern int gettimeofday (struct timeval *, void *); 
#endif

/* Get the amount of time the process has run, in microseconds.  */

extern long get_run_time (void);

/* Generate a relocated path to some installation directory.  Allocates
   return value using malloc.  */

extern char *make_relative_prefix (const char *, const char *,
                                   const char *) ATTRIBUTE_MALLOC;

/* Choose a temporary directory to use for scratch files.  */

extern char *choose_temp_base (void) ATTRIBUTE_MALLOC;

/* Return a temporary file name or NULL if unable to create one.  */

extern char *make_temp_file (const char *) ATTRIBUTE_MALLOC;

/* Remove a link to a file unless it is special. */

extern int unlink_if_ordinary (const char *);

/* Allocate memory filled with spaces.  Allocates using malloc.  */

extern const char *spaces (int count);

/* Return the maximum error number for which strerror will return a
   string.  */

extern int errno_max (void);

/* Return the name of an errno value (e.g., strerrno (EINVAL) returns
   "EINVAL").  */

extern const char *strerrno (int);

/* Given the name of an errno value, return the value.  */

extern int strtoerrno (const char *);

/* ANSI's strerror(), but more robust.  */

extern char *xstrerror (int);

/* Return the maximum signal number for which strsignal will return a
   string.  */

extern int signo_max (void);

/* Return a signal message string for a signal number
   (e.g., strsignal (SIGHUP) returns something like "Hangup").  */
/* This is commented out as it can conflict with one in system headers.
   We still document its existence though.  */

/*extern const char *strsignal (int);*/

/* Return the name of a signal number (e.g., strsigno (SIGHUP) returns
   "SIGHUP").  */

extern const char *strsigno (int);

/* Given the name of a signal, return its number.  */

extern int strtosigno (const char *);

/* Register a function to be run by xexit.  Returns 0 on success.  */

extern int xatexit (void (*fn) (void));

/* Exit, calling all the functions registered with xatexit.  */

extern void xexit (int status) ATTRIBUTE_NORETURN;

/* Set the program name used by xmalloc.  */

extern void xmalloc_set_program_name (const char *);

/* Report an allocation failure.  */
extern void xmalloc_failed (size_t) ATTRIBUTE_NORETURN;

/* Allocate memory without fail.  If malloc fails, this will print a
   message to stderr (using the name set by xmalloc_set_program_name,
   if any) and then call xexit.  */

extern void *xmalloc (size_t) ATTRIBUTE_MALLOC;

/* Reallocate memory without fail.  This works like xmalloc.  Note,
   realloc type functions are not suitable for attribute malloc since
   they may return the same address across multiple calls. */

extern void *xrealloc (void *, size_t);

/* Allocate memory without fail and set it to zero.  This works like
   xmalloc.  */

extern void *xcalloc (size_t, size_t) ATTRIBUTE_MALLOC;

/* Copy a string into a memory buffer without fail.  */

extern char *xstrdup (const char *) ATTRIBUTE_MALLOC;

/* Copy at most N characters from string into a buffer without fail.  */

extern char *xstrndup (const char *, size_t) ATTRIBUTE_MALLOC;

/* Copy an existing memory buffer to a new memory buffer without fail.  */

extern void *xmemdup (const void *, size_t, size_t) ATTRIBUTE_MALLOC;

/* Physical memory routines.  Return values are in BYTES.  */
extern double physmem_total (void);
extern double physmem_available (void);


/* These macros provide a K&R/C89/C++-friendly way of allocating structures
   with nice encapsulation.  The XDELETE*() macros are technically
   superfluous, but provided here for symmetry.  Using them consistently
   makes it easier to update client code to use different allocators such
   as new/delete and new[]/delete[].  */

/* Scalar allocators.  */

#define XNEW(T)			((T *) xmalloc (sizeof (T)))
#define XCNEW(T)		((T *) xcalloc (1, sizeof (T)))
#define XDELETE(P)		free ((void*) (P))

/* Array allocators.  */

#define XNEWVEC(T, N)		((T *) xmalloc (sizeof (T) * (N)))
#define XCNEWVEC(T, N)		((T *) xcalloc ((N), sizeof (T)))
#define XRESIZEVEC(T, P, N)	((T *) xrealloc ((void *) (P), sizeof (T) * (N)))
#define XDELETEVEC(P)		free ((void*) (P))

/* Allocators for variable-sized structures and raw buffers.  */

#define XNEWVAR(T, S)		((T *) xmalloc ((S)))
#define XCNEWVAR(T, S)		((T *) xcalloc (1, (S)))
#define XRESIZEVAR(T, P, S)	((T *) xrealloc ((P), (S)))

/* Type-safe obstack allocator.  */

#define XOBNEW(O, T)		((T *) obstack_alloc ((O), sizeof (T)))
#define XOBFINISH(O, T)         ((T) obstack_finish ((O)))

/* hex character manipulation routines */

#define _hex_array_size 256
#define _hex_bad	99
extern const unsigned char _hex_value[_hex_array_size];
extern void hex_init (void);
#define hex_p(c)	(hex_value (c) != _hex_bad)
/* If you change this, note well: Some code relies on side effects in
   the argument being performed exactly once.  */
#define hex_value(c)	((unsigned int) _hex_value[(unsigned char) (c)])

/* Flags for pex_init.  These are bits to be or'ed together.  */

/* Record subprocess times, if possible.  */
#define PEX_RECORD_TIMES	0x1

/* Use pipes for communication between processes, if possible.  */
#define PEX_USE_PIPES		0x2
   
/* Save files used for communication between processes.  */
#define PEX_SAVE_TEMPS		0x4

/* Prepare to execute one or more programs, with standard output of
   each program fed to standard input of the next.
   FLAGS        As above.
   PNAME        The name of the program to report in error messages. 
   TEMPBASE     A base name to use for temporary files; may be NULL to
                use a random name.
   Returns NULL on error.  */
                
extern struct pex_obj *pex_init (int flags, const char *pname,
		const char *tempbase);

/* Definitions used by the pexecute routine.  */

#define PEXECUTE_FIRST   1
#define PEXECUTE_LAST    2
#define PEXECUTE_ONE     (PEXECUTE_FIRST + PEXECUTE_LAST)
#define PEXECUTE_SEARCH  4
#define PEXECUTE_VERBOSE 8

/* Last program in pipeline.  Standard output of program goes to
   OUTNAME, or, if OUTNAME is NULL, to standard output of caller.  Do
   not set this if you want to call pex_read_output.  After this is
   set, pex_run may no longer be called with the same struct
   pex_obj.  */
#define PEX_LAST	 0x1

/* Search for program in executable search path.  */
#define PEX_SEARCH	 0x2

/* OUTNAME is a suffix.  */
#define PEX_SUFFIX	 0x4

/* Send program's standard error to standard output.  */
#define PEX_STDERR_TO_STDOUT	0x8
   
/* Input file should be opened in binary mode.  This flag is ignored
   on Unix.  */
#define PEX_BINARY_INPUT 0x10

/* Output file should be opened in binary mode.  This flag is ignored
   on Unix.  For proper behaviour PEX_BINARY_INPUT and
   PEX_BINARY_OUTPUT have to match appropriately--i.e., a call using
   PEX_BINARY_OUTPUT should be followed by a call using
   PEX_BINARY_INPUT.  */
#define PEX_BINARY_OUTPUT	0x20

/* Prepare to execute one or more programs, with standard output of
   each program fed to standard input of the next.
   FLAGS        As above.
   PNAME        The name of the program to report in error messages.
   TEMPBASE     A base name to use for temporary files; may be NULL to
                use a random name.
   Returns NULL on error.  */

extern struct pex_obj *pex_init (int flags, const char *pname,
				const char *tempbase);

/* Execute a program.  */

extern int pexecute (const char *, char * const *, const char *,
                     const char *, char **, char **, int);

/* Wait for pexecute to finish.  */

extern int pwait (int, int *, int);

struct pex_time
{
  unsigned long user_seconds;
  unsigned long user_microseconds;
  unsigned long system_seconds;
  unsigned long system_microseconds;
};

#if !HAVE_DECL_ASPRINTF
/* Like sprintf but provides a pointer to malloc'd storage, which must
   be freed by the caller.  */

extern int asprintf (char **, const char *, ...) ATTRIBUTE_PRINTF_2;
#endif

#if !HAVE_DECL_VASPRINTF
/* Like vsprintf but provides a pointer to malloc'd storage, which
   must be freed by the caller.  */

extern int vasprintf (char **, const char *, va_list) ATTRIBUTE_PRINTF(2,0);
#endif

#if defined(HAVE_DECL_SNPRINTF) && !HAVE_DECL_SNPRINTF
/* Like sprintf but prints at most N characters.  */
extern int snprintf (char *, size_t, const char *, ...) ATTRIBUTE_PRINTF_3;
#endif

#if defined(HAVE_DECL_VSNPRINTF) && !HAVE_DECL_VSNPRINTF
/* Like vsprintf but prints at most N characters.  */
extern int vsnprintf (char *, size_t, const char *, va_list) ATTRIBUTE_PRINTF(3,0);
#endif

#if defined(HAVE_DECL_STRVERSCMP) && !HAVE_DECL_STRVERSCMP
/* Compare version strings.  */
extern int strverscmp (const char *, const char *);
#endif

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

/* Drastically simplified alloca configurator.  If we're using GCC,
   we use __builtin_alloca; otherwise we use the C alloca.  The C
   alloca is always available.  You can override GCC by defining
   USE_C_ALLOCA yourself.  The canonical autoconf macro C_ALLOCA is
   also set/unset as it is often used to indicate whether code needs
   to call alloca(0).  */
extern void *C_alloca (size_t) ATTRIBUTE_MALLOC;
#undef alloca
#if GCC_VERSION >= 2000 && !defined USE_C_ALLOCA
# define alloca(x) __builtin_alloca(x)
# undef C_ALLOCA
# define ASTRDUP(X) \
  (__extension__ ({ const char *const libiberty_optr = (X); \
   const unsigned long libiberty_len = strlen (libiberty_optr) + 1; \
   char *const libiberty_nptr = (char *const) alloca (libiberty_len); \
   (char *) memcpy (libiberty_nptr, libiberty_optr, libiberty_len); }))
#else
# define alloca(x) C_alloca(x)
# undef USE_C_ALLOCA
# define USE_C_ALLOCA 1
# undef C_ALLOCA
# define C_ALLOCA 1
extern const char *libiberty_optr;
extern char *libiberty_nptr;
extern unsigned long libiberty_len;
# define ASTRDUP(X) \
  (libiberty_optr = (X), \
   libiberty_len = strlen (libiberty_optr) + 1, \
   libiberty_nptr = (char *) alloca (libiberty_len), \
   (char *) memcpy (libiberty_nptr, libiberty_optr, libiberty_len))
#endif

#ifdef __cplusplus
}
#endif


#endif /* ! defined (LIBIBERTY_H) */

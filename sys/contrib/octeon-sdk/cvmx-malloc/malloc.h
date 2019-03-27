/*
Copyright (c) 2001 Wolfram Gloger
Copyright (c) 2006 Cavium networks

Permission to use, copy, modify, distribute, and sell this software
and its documentation for any purpose is hereby granted without fee,
provided that (i) the above copyright notices and this permission
notice appear in all copies of the software and related documentation,
and (ii) the name of Wolfram Gloger may not be used in any advertising
or publicity relating to the software.

THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.

IN NO EVENT SHALL WOLFRAM GLOGER BE LIABLE FOR ANY SPECIAL,
INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND, OR ANY
DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY
OF LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
*/

#ifndef _MALLOC_H
#define _MALLOC_H 1

#undef _LIBC
#ifdef _LIBC
#include <features.h>
#endif

/*
  $Id: malloc.h 30481 2007-12-05 21:46:59Z rfranz $
  `ptmalloc2', a malloc implementation for multiple threads without
  lock contention, by Wolfram Gloger <wg@malloc.de>.

  VERSION 2.7.0

  This work is mainly derived from malloc-2.7.0 by Doug Lea
  <dl@cs.oswego.edu>, which is available from:

                 ftp://gee.cs.oswego.edu/pub/misc/malloc.c

  This trimmed-down header file only provides function prototypes and
  the exported data structures.  For more detailed function
  descriptions and compile-time options, see the source file
  `malloc.c'.
*/

#if 0
# include <stddef.h>
# define __malloc_ptr_t  void *
# undef  size_t
# define size_t          unsigned long
# undef  ptrdiff_t
# define ptrdiff_t       long
#else
# undef  Void_t
# define Void_t       void
# define __malloc_ptr_t  char *
#endif

#ifdef _LIBC
/* Used by GNU libc internals. */
# define __malloc_size_t size_t
# define __malloc_ptrdiff_t ptrdiff_t
#elif !defined __attribute_malloc__
# define __attribute_malloc__
#endif

#ifdef __GNUC__

/* GCC can always grok prototypes.  For C++ programs we add throw()
   to help it optimize the function calls.  But this works only with
   gcc 2.8.x and egcs.  */
# if defined __cplusplus && (__GNUC__ >= 3 || __GNUC_MINOR__ >= 8)
#  define __THROW	throw ()
# else
#  define __THROW
# endif
# define __MALLOC_P(args)	args __THROW
/* This macro will be used for functions which might take C++ callback
   functions.  */
# define __MALLOC_PMT(args)	args

#else	/* Not GCC.  */

# define __THROW

# if (defined __STDC__ && __STDC__) || defined __cplusplus

#  define __MALLOC_P(args)	args
#  define __MALLOC_PMT(args)	args

# else	/* Not ANSI C or C++.  */

#  define __MALLOC_P(args)	()	/* No prototypes.  */
#  define __MALLOC_PMT(args)	()

# endif	/* ANSI C or C++.  */

#endif	/* GCC.  */

#ifndef NULL
# ifdef __cplusplus
#  define NULL	0
# else
#  define NULL	((__malloc_ptr_t) 0)
# endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Nonzero if the malloc is already initialized.  */
#ifdef _LIBC
/* In the GNU libc we rename the global variable
   `__malloc_initialized' to `__libc_malloc_initialized'.  */
# define __malloc_initialized __libc_malloc_initialized
#endif
extern int cvmx__malloc_initialized;


/* SVID2/XPG mallinfo structure */

struct mallinfo {
  int arena;    /* non-mmapped space allocated from system */
  int ordblks;  /* number of free chunks */
  int smblks;   /* number of fastbin blocks */
  int hblks;    /* number of mmapped regions */
  int hblkhd;   /* space in mmapped regions */
  int usmblks;  /* maximum total allocated space */
  int fsmblks;  /* space available in freed fastbin blocks */
  int uordblks; /* total allocated space */
  int fordblks; /* total free space */
  int keepcost; /* top-most, releasable (via malloc_trim) space */
};

/* Returns a copy of the updated current mallinfo. */
extern struct mallinfo mallinfo __MALLOC_P ((void));

/* SVID2/XPG mallopt options */
#ifndef M_MXFAST
# define M_MXFAST  1	/* maximum request size for "fastbins" */
#endif
#ifndef M_NLBLKS
# define M_NLBLKS  2	/* UNUSED in this malloc */
#endif
#ifndef M_GRAIN
# define M_GRAIN   3	/* UNUSED in this malloc */
#endif
#ifndef M_KEEP
# define M_KEEP    4	/* UNUSED in this malloc */
#endif

/* mallopt options that actually do something */
#define M_TRIM_THRESHOLD    -1
#define M_TOP_PAD           -2
#define M_MMAP_THRESHOLD    -3
#define M_MMAP_MAX          -4
#define M_CHECK_ACTION      -5

/* General SVID/XPG interface to tunable parameters. */
extern int mallopt __MALLOC_P ((int __param, int __val));

/* Release all but __pad bytes of freed top-most memory back to the
   system. Return 1 if successful, else 0. */
extern int malloc_trim __MALLOC_P ((size_t __pad));

/* Report the number of usable allocated bytes associated with allocated
   chunk __ptr. */
extern size_t malloc_usable_size __MALLOC_P ((__malloc_ptr_t __ptr));

/* Prints brief summary statistics on stderr. */
extern void malloc_stats __MALLOC_P ((void));

/* Record the state of all malloc variables in an opaque data structure. */
extern __malloc_ptr_t malloc_get_state __MALLOC_P ((void));

/* Restore the state of all malloc variables from data obtained with
   malloc_get_state(). */
extern int malloc_set_state __MALLOC_P ((__malloc_ptr_t __ptr));

/* Called once when malloc is initialized; redefining this variable in
   the application provides the preferred way to set up the hook
   pointers. */
extern void (*cmvx__malloc_initialize_hook) __MALLOC_PMT ((void));
/* Hooks for debugging and user-defined versions. */
extern void (*cvmx__free_hook) __MALLOC_PMT ((__malloc_ptr_t __ptr,
					__const __malloc_ptr_t));
extern __malloc_ptr_t (*cvmx__malloc_hook) __MALLOC_PMT ((size_t __size,
						    __const __malloc_ptr_t));
extern __malloc_ptr_t (*cvmx__realloc_hook) __MALLOC_PMT ((__malloc_ptr_t __ptr,
						     size_t __size,
						     __const __malloc_ptr_t));
extern __malloc_ptr_t (*cvmx__memalign_hook) __MALLOC_PMT ((size_t __alignment,
						      size_t __size,
						      __const __malloc_ptr_t));
extern void (*__after_morecore_hook) __MALLOC_PMT ((void));

/* Activate a standard set of debugging hooks. */
extern void cvmx__malloc_check_init __MALLOC_P ((void));

/* Internal routines, operating on "arenas".  */
struct malloc_state;
typedef struct malloc_state *mstate;
#ifdef __cplusplus
}; /* end of extern "C" */
#endif


#endif /* malloc.h */

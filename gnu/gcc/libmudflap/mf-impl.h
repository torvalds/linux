/* Implementation header for mudflap runtime library.
   Mudflap: narrow-pointer bounds-checking by tree rewriting.
   Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Frank Ch. Eigler <fche@redhat.com>
   and Graydon Hoare <graydon@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#ifndef __MF_IMPL_H
#define __MF_IMPL_H

#ifdef _MUDFLAP
#error "Do not compile this file with -fmudflap!"
#endif

#if HAVE_PTHREAD_H
#include <pthread.h>
#elif LIBMUDFLAPTH
#error "Cannot build libmudflapth without pthread.h."
#endif

#if HAVE_STDINT_H
#include <stdint.h>
#else
typedef __mf_uintptr_t uintptr_t;
#endif

/* Private definitions related to mf-runtime.h  */

#define __MF_TYPE_MAX_CEM  __MF_TYPE_STACK  /* largest type# for the cemetary */
#define __MF_TYPE_MAX __MF_TYPE_GUESS


#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

/* Address calculation macros.  */

#define MINPTR ((uintptr_t) 0)
#define MAXPTR (~ (uintptr_t) 0)

/* Clamp the addition/subtraction of uintptr_t's to [MINPTR,MAXPTR] */
#define CLAMPSUB(ptr,offset) (((uintptr_t) ptr) >= (offset) ? ((uintptr_t) ptr)-((uintptr_t) offset) : MINPTR)
#define CLAMPADD(ptr,offset) (((uintptr_t) ptr) <= MAXPTR-(offset) ? ((uintptr_t) ptr)+((uintptr_t) offset) : MAXPTR)
#define CLAMPSZ(ptr,size) ((size) ? (((uintptr_t) ptr) <= MAXPTR-(size)+1 ? ((uintptr_t) ptr)+((uintptr_t) size) - 1 : MAXPTR) : ((uintptr_t) ptr))

#define __MF_CACHE_INDEX(ptr) ((((uintptr_t) (ptr)) >> __mf_lc_shift) & __mf_lc_mask)
#define __MF_CACHE_MISS_P(ptr,sz) ({ \
             struct __mf_cache *elem = & __mf_lookup_cache[__MF_CACHE_INDEX((ptr))]; \
             ((elem->low > (uintptr_t) (ptr)) ||                  \
	      (elem->high < (CLAMPADD((uintptr_t) (ptr), (uintptr_t) CLAMPSUB(sz,1) )))); })
/* XXX: the above should use CLAMPSZ () */



/* Private functions. */

extern void __mf_violation (void *ptr, size_t sz,
			    uintptr_t pc, const char *location,
			    int type);
extern size_t __mf_backtrace (char ***, void *, unsigned);
extern int __mf_heuristic_check (uintptr_t, uintptr_t);

/* ------------------------------------------------------------------------ */
/* Type definitions. */
/* ------------------------------------------------------------------------ */

/* The mf_state type codes describe recursion and initialization order.

   reentrant means we are inside a mf-runtime support routine, such as
   __mf_register, and thus there should be no calls to any wrapped functions,
   such as the wrapped malloc.  This indicates a bug if it occurs.
   in_malloc means we are inside a real malloc call inside a wrapped malloc
   call, and thus there should be no calls to any wrapped functions like the
   wrapped mmap.  This happens on some systems due to how the system libraries
   are constructed.  */

enum __mf_state_enum { active, reentrant, in_malloc }; 

/* The __mf_options structure records optional or tunable aspects of the
 mudflap library's behavior. There is a single global instance of this
 structure which is populated from user input (in an environment variable)
 when the library initializes. */

struct __mf_options
{
  /* Emit a trace message for each call. */
  unsigned trace_mf_calls;

  /* Collect and emit statistics. */
  unsigned collect_stats;

  /* Set up a SIGUSR1 -> __mf_report handler. */
  unsigned sigusr1_report;

  /* Execute internal checking code. */
  unsigned internal_checking;

  /* Age object liveness periodically. */
  unsigned tree_aging;

  /* Adapt the lookup cache to working set. */
  unsigned adapt_cache;

  /* Print list of leaked heap objects on shutdown. */
  unsigned print_leaks;

  /* Detect reads of uninitialized objects. */
  unsigned check_initialization;

  /* Print verbose description of violations. */
  unsigned verbose_violations;

  /* Abbreviate duplicate object descriptions.  */
  unsigned abbreviate;

  /* Emit internal tracing message. */
  unsigned verbose_trace;

  /* Wipe stack/heap objects upon unwind.  */
  unsigned wipe_stack;
  unsigned wipe_heap;

  /* Maintain a queue of this many deferred free()s,
     to trap use of freed memory. */
  unsigned free_queue_length;

  /* Maintain a history of this many past unregistered objects. */
  unsigned persistent_count;

  /* Pad allocated extents by this many bytes on either side. */
  unsigned crumple_zone;

  /* Maintain this many stack frames for contexts. */
  unsigned backtrace;

  /* Ignore read operations even if mode_check is in effect.  */
  unsigned ignore_reads;

  /* Collect register/unregister timestamps.  */
  unsigned timestamps;

#ifdef LIBMUDFLAPTH
  /* Thread stack size.  */
  unsigned thread_stack;
#endif

  /* Major operation mode */
#define mode_nop 0      /* Do nothing.  */
#define mode_populate 1 /* Populate tree but do not check for violations.  */
#define mode_check 2    /* Populate and check for violations (normal).  */
#define mode_violate 3  /* Trigger a violation on every call (diagnostic).  */
  unsigned mudflap_mode;

  /* How to handle a violation. */
#define viol_nop 0   /* Return control to application. */
#define viol_segv 1  /* Signal self with segv. */
#define viol_abort 2 /* Call abort (). */
#define viol_gdb 3   /* Fork a debugger on self */
  unsigned violation_mode;

  /* Violation heuristics selection. */
  unsigned heur_stack_bound; /* allow current stack region */
  unsigned heur_proc_map;  /* allow & cache /proc/self/map regions.  */
  unsigned heur_start_end; /* allow _start .. _end */
  unsigned heur_std_data; /* allow & cache stdlib data */
};


#ifdef PIC

/* This is a table of dynamically resolved function pointers. */

struct __mf_dynamic_entry
{
  void *pointer;
  char *name;
  char *version;
};

/* The definition of the array (mf-runtime.c) must match the enums!  */
extern struct __mf_dynamic_entry __mf_dynamic[];
enum __mf_dynamic_index
{
  dyn_calloc, dyn_free, dyn_malloc, dyn_mmap,
  dyn_munmap, dyn_realloc,
  dyn_INITRESOLVE,  /* Marker for last init-time resolution. */
#ifdef LIBMUDFLAPTH
  dyn_pthread_create
#endif
};

#endif /* PIC */

/* ------------------------------------------------------------------------ */
/* Private global variables. */
/* ------------------------------------------------------------------------ */

#ifdef LIBMUDFLAPTH
extern pthread_mutex_t __mf_biglock;
#define LOCKTH() do { extern unsigned long __mf_lock_contention; \
                      int rc = pthread_mutex_trylock (& __mf_biglock); \
                      if (rc) { __mf_lock_contention ++; \
                                rc = pthread_mutex_lock (& __mf_biglock); } \
                      assert (rc==0); } while (0)
#define UNLOCKTH() do { int rc = pthread_mutex_unlock (& __mf_biglock); \
                        assert (rc==0); } while (0)
#else
#define LOCKTH() do {} while (0)
#define UNLOCKTH() do {} while (0)
#endif

#if defined(LIBMUDFLAPTH) && !defined(HAVE_TLS)
extern enum __mf_state_enum __mf_get_state (void);
extern void __mf_set_state (enum __mf_state_enum);
#else
# ifdef LIBMUDFLAPTH
extern __thread enum __mf_state_enum __mf_state_1;
# else
extern enum __mf_state_enum __mf_state_1;
# endif
static inline enum __mf_state_enum __mf_get_state (void)
{
  return __mf_state_1;
}
static inline void __mf_set_state (enum __mf_state_enum s)
{
  __mf_state_1 = s;
}
#endif

extern int __mf_starting_p;
extern struct __mf_options __mf_opts;

/* ------------------------------------------------------------------------ */
/* Utility macros. */
/* ------------------------------------------------------------------------ */

#define UNLIKELY(e) (__builtin_expect (!!(e), 0))
#define LIKELY(e) (__builtin_expect (!!(e), 1))
#define STRINGIFY2(e) #e
#define STRINGIFY(e) STRINGIFY2(e)

#ifdef LIBMUDFLAPTH
#define VERBOSE_TRACE(...) \
  do { if (UNLIKELY (__mf_opts.verbose_trace)) {  \
      fprintf (stderr, "mf(%u): ", (unsigned) pthread_self ()); \
      fprintf (stderr, __VA_ARGS__); \
    } } while (0)
#define TRACE(...) \
  do { if (UNLIKELY (__mf_opts.trace_mf_calls)) { \
      fprintf (stderr, "mf(%u): ", (unsigned) pthread_self ()); \
      fprintf (stderr, __VA_ARGS__); \
    } } while (0)
#else
#define VERBOSE_TRACE(...) \
  do { if (UNLIKELY (__mf_opts.verbose_trace)) {  \
      fprintf (stderr, "mf: "); \
      fprintf (stderr, __VA_ARGS__); \
    } } while (0)
#define TRACE(...) \
  do { if (UNLIKELY (__mf_opts.trace_mf_calls)) { \
      fprintf (stderr, "mf: "); \
      fprintf (stderr, __VA_ARGS__); \
    } } while (0)
#endif


#define __MF_PERSIST_MAX 256
#define __MF_FREEQ_MAX 256

/*
   Wrapping and redirection:

   Mudflap redirects a number of libc functions into itself, for "cheap"
   verification (eg. strcpy, bzero, memcpy) and also to register /
   unregister regions of memory as they are manipulated by the program
   (eg. malloc/free, mmap/munmap).

   There are two methods of wrapping.

   (1) The static method involves a list of -wrap=foo flags being passed to
   the linker, which then links references to "foo" to the symbol
   "__wrap_foo", and links references to "__real_foo" to the symbol "foo".
   When compiled without -DPIC, libmudflap.a contains such __wrap_foo
   functions which delegate to __real_foo functions in libc to get their
   work done.

   (2) The dynamic method involves providing a definition of symbol foo in
   libmudflap.so and linking it earlier in the compiler command line,
   before libc.so. The function "foo" in libmudflap must then call
   dlsym(RTLD_NEXT, "foo") to acquire a pointer to the "real" libc foo, or
   at least the "next" foo in the dynamic link resolution order.

   We switch between these two techniques by the presence of the -DPIC
   #define passed in by libtool when building libmudflap.
*/


#ifdef PIC

extern void __mf_resolve_single_dynamic (struct __mf_dynamic_entry *);

#define _GNU_SOURCE
#include <dlfcn.h>

#define WRAPPER(ret, fname, ...)                      \
ret __wrap_ ## fname (__VA_ARGS__)                    \
    __attribute__ (( alias  (#fname)  ));             \
ret __real_ ## fname (__VA_ARGS__)                    \
    __attribute__ (( alias  (#fname)  ));             \
ret fname (__VA_ARGS__)
#define DECLARE(ty, fname, ...)                       \
 typedef ty (*__mf_fn_ ## fname) (__VA_ARGS__);       \
 extern ty __mf_0fn_ ## fname (__VA_ARGS__);
#define CALL_REAL(fname, ...)                         \
  ({__mf_starting_p \
     ? __mf_0fn_ ## fname (__VA_ARGS__) \
    : (__mf_resolve_single_dynamic (& __mf_dynamic[dyn_ ## fname]), \
       (((__mf_fn_ ## fname)(__mf_dynamic[dyn_ ## fname].pointer)) (__VA_ARGS__)));})
#define CALL_BACKUP(fname, ...)                       \
  __mf_0fn_ ## fname(__VA_ARGS__)

#else /* not PIC --> static library */

#define WRAPPER(ret, fname, ...)            \
ret __wrap_ ## fname (__VA_ARGS__)
#define DECLARE(ty, fname, ...)             \
 extern ty __real_ ## fname (__VA_ARGS__)
#define CALL_REAL(fname, ...)               \
 __real_ ## fname (__VA_ARGS__)
#define CALL_BACKUP(fname, ...)             \
  __real_ ## fname(__VA_ARGS__)

#endif /* PIC */

/* WRAPPER2 is for functions intercepted via macros at compile time. */
#define WRAPPER2(ret, fname, ...)                     \
ret __mfwrap_ ## fname (__VA_ARGS__)


/* Utility macros for mf-hooks*.c */

#define MF_VALIDATE_EXTENT(value,size,acc,context) \
 do { \
  if (UNLIKELY (size > 0 && __MF_CACHE_MISS_P (value, size))) \
    if (acc == __MF_CHECK_WRITE || ! __mf_opts.ignore_reads) \
    __mf_check ((void *) (value), (size), acc, "(" context ")"); \
 } while (0)
#define BEGIN_PROTECT(fname, ...)       \
  if (UNLIKELY (__mf_starting_p)) \
  {                                         \
    return CALL_BACKUP(fname, __VA_ARGS__); \
  }                                         \
  else if (UNLIKELY (__mf_get_state () == reentrant))   \
  {                                         \
    extern unsigned long __mf_reentrancy;   \
    __mf_reentrancy ++; \
    return CALL_REAL(fname, __VA_ARGS__);   \
  }                                         \
  else if (UNLIKELY (__mf_get_state () == in_malloc))   \
  {                                         \
    return CALL_REAL(fname, __VA_ARGS__);   \
  }                                         \
  else                                      \
  {                                         \
    TRACE ("%s\n", __PRETTY_FUNCTION__); \
  }

/* There is an assumption here that these will only be called in routines
   that call BEGIN_PROTECT at the start, and hence the state must always
   be active when BEGIN_MALLOC_PROTECT is called.  */
#define BEGIN_MALLOC_PROTECT() \
  __mf_set_state (in_malloc)

#define END_MALLOC_PROTECT() \
  __mf_set_state (active)

/* Unlocked variants of main entry points from mf-runtime.h.  */
extern void __mfu_check (void *ptr, size_t sz, int type, const char *location);
extern void __mfu_register (void *ptr, size_t sz, int type, const char *name);
extern void __mfu_unregister (void *ptr, size_t sz, int type);
extern void __mfu_report ();
extern int __mfu_set_options (const char *opts);


#endif /* __MF_IMPL_H */

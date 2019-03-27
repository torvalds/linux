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

/* $Id: thread-m.h 30481 2007-12-05 21:46:59Z rfranz $
   One out of _LIBC, USE_PTHREADS, USE_THR or USE_SPROC should be
   defined, otherwise the token NO_THREADS and dummy implementations
   of the macros will be defined.  */

#ifndef _THREAD_M_H
#define _THREAD_M_H

#undef thread_atfork_static


#undef NO_THREADS /* No threads, provide dummy macros */

typedef int thread_id;

/* The mutex functions used to do absolutely nothing, i.e. lock,
   trylock and unlock would always just return 0.  However, even
   without any concurrently active threads, a mutex can be used
   legitimately as an `in use' flag.  To make the code that is
   protected by a mutex async-signal safe, these macros would have to
   be based on atomic test-and-set operations, for example. */
#ifdef __OCTEON__
typedef cvmx_spinlock_t mutex_t;
#define MUTEX_INITIALIZER          CMVX_SPINLOCK_UNLOCKED_VAL
#define mutex_init(m)              cvmx_spinlock_init(m)
#define mutex_lock(m)              cvmx_spinlock_lock(m)
#define mutex_trylock(m)           (cvmx_spinlock_trylock(m))
#define mutex_unlock(m)            cvmx_spinlock_unlock(m)
#else

typedef int mutex_t;

#define MUTEX_INITIALIZER          0
#define mutex_init(m)              (*(m) = 0)
#define mutex_lock(m)              ((*(m) = 1), 0)
#define mutex_trylock(m)           (*(m) ? 1 : ((*(m) = 1), 0))
#define mutex_unlock(m)            (*(m) = 0)
#endif



typedef void *tsd_key_t;
#define tsd_key_create(key, destr) do {} while(0)
#define tsd_setspecific(key, data) ((key) = (data))
#define tsd_getspecific(key, vptr) (vptr = (key))

#define thread_atfork(prepare, parent, child) do {} while(0)


#endif /* !defined(_THREAD_M_H) */

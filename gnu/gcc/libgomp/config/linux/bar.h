/* Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@redhat.com>.

   This file is part of the GNU OpenMP Library (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
   more details.

   You should have received a copy of the GNU Lesser General Public License 
   along with libgomp; see the file COPYING.LIB.  If not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files, some
   of which are compiled with GCC, to produce an executable, this library
   does not by itself cause the resulting executable to be covered by the
   GNU General Public License.  This exception does not however invalidate
   any other reasons why the executable file might be covered by the GNU
   General Public License.  */

/* This is a Linux specific implementation of a barrier synchronization
   mechanism for libgomp.  This type is private to the library.  This 
   implementation uses atomic instructions and the futex syscall.  */

#ifndef GOMP_BARRIER_H
#define GOMP_BARRIER_H 1

#include "mutex.h"

typedef struct
{
  gomp_mutex_t mutex;
  unsigned total;
  unsigned arrived;
  int generation;
} gomp_barrier_t;

static inline void gomp_barrier_init (gomp_barrier_t *bar, unsigned count)
{
  gomp_mutex_init (&bar->mutex);
  bar->total = count;
  bar->arrived = 0;
  bar->generation = 0;
}

static inline void gomp_barrier_reinit (gomp_barrier_t *bar, unsigned count)
{
  gomp_mutex_lock (&bar->mutex);
  bar->total = count;
  gomp_mutex_unlock (&bar->mutex);
}

static inline void gomp_barrier_destroy (gomp_barrier_t *bar)
{
  /* Before destroying, make sure all threads have left the barrier.  */
  gomp_mutex_lock (&bar->mutex);
}

extern void gomp_barrier_wait (gomp_barrier_t *);
extern void gomp_barrier_wait_end (gomp_barrier_t *, bool);

static inline bool gomp_barrier_wait_start (gomp_barrier_t *bar)
{
  gomp_mutex_lock (&bar->mutex);
  return ++bar->arrived == bar->total;
}

#endif /* GOMP_BARRIER_H */

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

/* This is the default PTHREADS implementation of a mutex synchronization
   mechanism for libgomp.  This type is private to the library.  */

#ifndef GOMP_MUTEX_H
#define GOMP_MUTEX_H 1

#include <pthread.h>

typedef pthread_mutex_t gomp_mutex_t;

#define GOMP_MUTEX_INIT_0 0

static inline void gomp_mutex_init (gomp_mutex_t *mutex)
{
  pthread_mutex_init (mutex, NULL);
}

static inline void gomp_mutex_lock (gomp_mutex_t *mutex)
{
  pthread_mutex_lock (mutex);
}

static inline void gomp_mutex_unlock (gomp_mutex_t *mutex)
{
   pthread_mutex_unlock (mutex);
}

static inline void gomp_mutex_destroy (gomp_mutex_t *mutex)
{
  pthread_mutex_destroy (mutex);
}

#endif /* GOMP_MUTEX_H */

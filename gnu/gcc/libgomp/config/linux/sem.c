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

/* This is a Linux specific implementation of a semaphore synchronization
   mechanism for libgomp.  This type is private to the library.  This 
   implementation uses atomic instructions and the futex syscall.  */

#include "libgomp.h"
#include "futex.h"


void
gomp_sem_wait_slow (gomp_sem_t *sem)
{
  while (1)
    {
      int val = __sync_val_compare_and_swap (sem, 0, -1);
      if (val > 0)
	{
	  if (__sync_bool_compare_and_swap (sem, val, val - 1))
	    return;
	}
      futex_wait (sem, -1);
    }
}

void
gomp_sem_post_slow (gomp_sem_t *sem)
{
  int old, tmp = *sem, wake;

  do
    {
      old = tmp;
      wake = old > 0 ? old + 1 : 1;
      tmp = __sync_val_compare_and_swap (sem, old, wake);
    }
  while (old != tmp);

  futex_wake (sem, wake);
}

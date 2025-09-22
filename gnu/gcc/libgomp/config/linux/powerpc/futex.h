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

/* Provide target-specific access to the futex system call.  */

#include <sys/syscall.h>
#define FUTEX_WAIT	0
#define FUTEX_WAKE	1

static inline void
sys_futex0 (int *addr, int op, int val)
{
  register long int r0  __asm__ ("r0");
  register long int r3  __asm__ ("r3");
  register long int r4  __asm__ ("r4");
  register long int r5  __asm__ ("r5");
  register long int r6  __asm__ ("r6");

  r0 = SYS_futex;
  r3 = (long) addr;
  r4 = op;
  r5 = val;
  r6 = 0;

  /* ??? The powerpc64 sysdep.h file clobbers ctr; the powerpc32 sysdep.h
     doesn't.  It doesn't much matter for us.  In the interest of unity,
     go ahead and clobber it always.  */

  __asm volatile ("sc"
		  : "=r"(r0), "=r"(r3), "=r"(r4), "=r"(r5), "=r"(r6)
		  : "r"(r0), "r"(r3), "r"(r4), "r"(r5), "r"(r6)
		  : "r7", "r8", "r9", "r10", "r11", "r12",
		    "cr0", "ctr", "memory");
}

static inline void
futex_wait (int *addr, int val)
{
  sys_futex0 (addr, FUTEX_WAIT, val);
}

static inline void
futex_wake (int *addr, int count)
{
  sys_futex0 (addr, FUTEX_WAKE, count);
}

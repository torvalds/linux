/* Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Jakub Jelinek <jakub@redhat.com>.

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
  register long int g1  __asm__ ("g1");
  register long int o0  __asm__ ("o0");
  register long int o1  __asm__ ("o1");
  register long int o2  __asm__ ("o2");
  register long int o3  __asm__ ("o3");

  g1 = SYS_futex;
  o0 = (long) addr;
  o1 = op;
  o2 = val;
  o3 = 0;

#ifdef __arch64__
# define SYSCALL_STRING "ta\t0x6d"
#else
# define SYSCALL_STRING "ta\t0x10"
#endif

  __asm volatile (SYSCALL_STRING
		  : "=r" (g1), "=r" (o0)
		  : "0" (g1), "1" (o0), "r" (o1), "r" (o2), "r" (o3)
		  : "g2", "g3", "g4", "g5", "g6",
		    "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
		    "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
		    "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
		    "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
#ifdef __arch64__
		    "f32", "f34", "f36", "f38", "f40", "f42", "f44", "f46",
		    "f48", "f50", "f52", "f54", "f56", "f58", "f60", "f62",
#endif
		    "cc", "memory");
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

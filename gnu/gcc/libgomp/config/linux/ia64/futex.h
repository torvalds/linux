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
sys_futex0(int *addr, int op, int val)
{
  register long out0 asm ("out0") = (long) addr;
  register long out1 asm ("out1") = op;
  register long out2 asm ("out2") = val;
  register long out3 asm ("out3") = 0;
  register long r15 asm ("r15") = SYS_futex;

  __asm __volatile ("break 0x100000"
	: "=r"(r15), "=r"(out0), "=r"(out1), "=r"(out2), "=r"(out3)
	: "r"(r15), "r"(out0), "r"(out1), "r"(out2), "r"(out3)
        : "memory", "r8", "r10", "out4", "out5", "out6", "out7",
	  /* Non-stacked integer registers, minus r8, r10, r15.  */
	  "r2", "r3", "r9", "r11", "r12", "r13", "r14", "r16", "r17", "r18",
	  "r19", "r20", "r21", "r22", "r23", "r24", "r25", "r26", "r27",
	  "r28", "r29", "r30", "r31",
	  /* Predicate registers.  */
	  "p6", "p7", "p8", "p9", "p10", "p11", "p12", "p13", "p14", "p15",
	  /* Non-rotating fp registers.  */
	  "f6", "f7", "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
	  /* Branch registers.  */
	  "b6");
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

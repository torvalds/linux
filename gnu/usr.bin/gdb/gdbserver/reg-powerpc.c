/* *INDENT-OFF* */ /* THIS FILE IS GENERATED */

/* A register protocol for GDB, the GNU debugger.
   Copyright 2001, 2002 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* This file was created with the aid of ``regdat.sh'' and ``../../../../contrib/gdb/gdb/regformats/reg-ppc.dat''.  */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "regdef.h"
#include "regcache.h"

struct reg regs_ppc[] = {
  { "r0", 0, 32 },
  { "r1", 32, 32 },
  { "r2", 64, 32 },
  { "r3", 96, 32 },
  { "r4", 128, 32 },
  { "r5", 160, 32 },
  { "r6", 192, 32 },
  { "r7", 224, 32 },
  { "r8", 256, 32 },
  { "r9", 288, 32 },
  { "r10", 320, 32 },
  { "r11", 352, 32 },
  { "r12", 384, 32 },
  { "r13", 416, 32 },
  { "r14", 448, 32 },
  { "r15", 480, 32 },
  { "r16", 512, 32 },
  { "r17", 544, 32 },
  { "r18", 576, 32 },
  { "r19", 608, 32 },
  { "r20", 640, 32 },
  { "r21", 672, 32 },
  { "r22", 704, 32 },
  { "r23", 736, 32 },
  { "r24", 768, 32 },
  { "r25", 800, 32 },
  { "r26", 832, 32 },
  { "r27", 864, 32 },
  { "r28", 896, 32 },
  { "r29", 928, 32 },
  { "r30", 960, 32 },
  { "r31", 992, 32 },
  { "f0", 1024, 64 },
  { "f1", 1088, 64 },
  { "f2", 1152, 64 },
  { "f3", 1216, 64 },
  { "f4", 1280, 64 },
  { "f5", 1344, 64 },
  { "f6", 1408, 64 },
  { "f7", 1472, 64 },
  { "f8", 1536, 64 },
  { "f9", 1600, 64 },
  { "f10", 1664, 64 },
  { "f11", 1728, 64 },
  { "f12", 1792, 64 },
  { "f13", 1856, 64 },
  { "f14", 1920, 64 },
  { "f15", 1984, 64 },
  { "f16", 2048, 64 },
  { "f17", 2112, 64 },
  { "f18", 2176, 64 },
  { "f19", 2240, 64 },
  { "f20", 2304, 64 },
  { "f21", 2368, 64 },
  { "f22", 2432, 64 },
  { "f23", 2496, 64 },
  { "f24", 2560, 64 },
  { "f25", 2624, 64 },
  { "f26", 2688, 64 },
  { "f27", 2752, 64 },
  { "f28", 2816, 64 },
  { "f29", 2880, 64 },
  { "f30", 2944, 64 },
  { "f31", 3008, 64 },
  { "pc", 3072, 32 },
  { "ps", 3104, 32 },
  { "cr", 3136, 32 },
  { "lr", 3168, 32 },
  { "ctr", 3200, 32 },
  { "xer", 3232, 32 },
  { "fpscr", 3264, 32 },
};

const char *expedite_regs_ppc[] = { "r1", "pc", 0 };

void
init_registers ()
{
    set_register_cache (regs_ppc,
			sizeof (regs_ppc) / sizeof (regs_ppc[0]));
    gdbserver_expedite_regs = expedite_regs_ppc;
}

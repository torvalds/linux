/* Definitions for PA_RISC with ELF-32 format
   Copyright (C) 2000, 2002, 2004, 2006 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* Turn off various SOM crap we don't want.  */
#undef TARGET_ELF32
#define TARGET_ELF32 1

/* The libcall __canonicalize_funcptr_for_compare is referenced in
   crtend.o and the reference isn't resolved in objects that don't
   compare function pointers.  Thus, we need to play games to provide
   a reference in crtbegin.o.  The rest of the define is the same
   as that in crtstuff.c  */
#define CTOR_LIST_BEGIN \
  asm (".type __canonicalize_funcptr_for_compare,@function\n"		\
"	.text\n"							\
"	.word __canonicalize_funcptr_for_compare-$PIC_pcrel$0");	\
  STATIC func_ptr __CTOR_LIST__[1]					\
    __attribute__ ((__unused__, section(".ctors"),			\
		    aligned(sizeof(func_ptr))))				\
    = { (func_ptr) (-1) }

/* This is a PIC version of CRT_CALL_STATIC_FUNCTION.  The PIC
   register has to be saved before the call and restored after
   the call.  We assume that register %r4 is available for this
   purpose.  The hack prevents GCC from deleting the restore.  */
#ifdef CRTSTUFFS_O
#define CRT_CALL_STATIC_FUNCTION(SECTION_OP, FUNC)	\
static void __attribute__((__used__))			\
call_ ## FUNC (void)					\
{							\
  asm (SECTION_OP);					\
  asm volatile ("bl " #FUNC ",%%r2\n\t"			\
		"copy %%r19,%%r4\n\t"			\
		"copy %%r4,%%r19\n"			\
		:					\
		:					\
		: "r1", "r2", "r4", "r20", "r21",	\
		  "r22", "r23", "r24", "r25", "r26",	\
		  "r27", "r28", "r29", "r31");		\
  asm (TEXT_SECTION_ASM_OP);				\
}
#endif

#define MD_UNWIND_SUPPORT "config/pa/linux-unwind.h"

/* Target definitions for GNU compiler for VAX using ELF
   Copyright (C) 2002, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Matt Thomas <matt@3am-software.com>

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

#undef TARGET_ELF
#define TARGET_ELF 1

#undef REGISTER_PREFIX
#undef REGISTER_NAMES
#define REGISTER_PREFIX "%"
#define REGISTER_NAMES \
  { "%r0", "%r1",  "%r2",  "%r3", "%r4", "%r5", "%r6", "%r7", \
    "%r8", "%r9", "%r10", "%r11", "%ap", "%fp", "%sp", "%pc", }

#undef SIZE_TYPE
#define SIZE_TYPE "long unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "long int"

/* Profiling routine.  */
#undef VAX_FUNCTION_PROFILER_NAME
#define VAX_FUNCTION_PROFILER_NAME "__mcount"

/*  Let's be re-entrant.  */
#undef PCC_STATIC_STRUCT_RETURN

/* Before the prologue, the top of the frame is below the argument
   count pushed by the CALLS and before the start of the saved registers.  */
#define INCOMING_FRAME_SP_OFFSET 0

/* We use R2-R5 (call-clobbered) registers for exceptions.  */
#define EH_RETURN_DATA_REGNO(N) ((N) < 4 ? (N) + 2 : INVALID_REGNUM)

/* Place the top of the stack for the DWARF2 EH stackadj value.  */
#define EH_RETURN_STACKADJ_RTX						\
  gen_rtx_MEM (SImode,							\
	       plus_constant (gen_rtx_REG (Pmode, FRAME_POINTER_REGNUM),\
			      -4))

/* Simple store the return handler into the call frame.  */
#define EH_RETURN_HANDLER_RTX						\
  gen_rtx_MEM (Pmode,							\
	       plus_constant (gen_rtx_REG (Pmode, FRAME_POINTER_REGNUM),\
			      16))


/* Reserve the top of the stack for exception handler stackadj value.  */
#undef STARTING_FRAME_OFFSET
#define STARTING_FRAME_OFFSET -4

/* The VAX wants no space between the case instruction and the jump table.  */
#undef  ASM_OUTPUT_BEFORE_CASE_LABEL
#define ASM_OUTPUT_BEFORE_CASE_LABEL(FILE, PREFIX, NUM, TABLE)

#undef OVERRIDE_OPTIONS
#define OVERRIDE_OPTIONS				\
  do							\
    {							\
      /* Do generic VAX overrides.  */			\
      override_options ();				\
							\
      /* Turn off function CSE if we're doing PIC.  */	\
      if (flag_pic)					\
	flag_no_function_cse = 1;			\
    }							\
  while (0)

/* VAX ELF is always gas; override the generic VAX ASM_SPEC.  */

#undef ASM_SPEC
#define ASM_SPEC ""


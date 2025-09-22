/* Definitions of target machine for GNU compiler, for a PDP with 2BSD
   Copyright (C) 1995, 1996, 1999, 2000 Free Software Foundation, Inc.
   Contributed by Michael K. Gschwind (mike@vlsivie.tuwien.ac.at).

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

#define TWO_BSD

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in
   functions that have frame pointers.
   No definition is equivalent to always zero.  */

#undef EXIT_IGNORE_STACK
#define EXIT_IGNORE_STACK	1

#undef INITIAL_FRAME_POINTER_OFFSET  
#define INITIAL_FRAME_POINTER_OFFSET(DEPTH_VAR)	\
{								\
  int offset;		      				\
  offset = get_frame_size();					\
  offset = (offset <= 2)? 0: (offset -2);			\
  (DEPTH_VAR) = offset+10;						\
}   

/* Value should be nonzero if functions must have frame pointers.
   Zero means the frame pointer need not be set up (and parms
   may be accessed via the stack pointer) in functions that seem suitable.
   This is computed in `reload', in reload1.c.
  */

#undef FRAME_POINTER_REQUIRED 
#define FRAME_POINTER_REQUIRED 1

/* Offset within stack frame to start allocating local variables at.
   If FRAME_GROWS_DOWNWARD, this is the offset to the END of the
   first local allocated.  Otherwise, it is the offset to the BEGINNING
   of the first local allocated.  */
#undef STARTING_FRAME_OFFSET
#define STARTING_FRAME_OFFSET -8


#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(STREAM, NAME, DECL)	\
do {							\
ASM_OUTPUT_LABEL (STREAM, NAME);			\
fprintf (STREAM, "~~%s:\n", NAME); 			\
} while (0)

#undef TARGET_UNIX_ASM_DEFAULT
#define TARGET_UNIX_ASM_DEFAULT MASK_UNIX_ASM

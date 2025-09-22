/* Definitions for PA_RISC with ELF format on 64-bit Linux
   Copyright (C) 1999, 2000, 2002 Free Software Foundation, Inc.

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

#if 0 /* needs some work :-( */
/* If defined, this macro specifies a table of register pairs used to
   eliminate unneeded registers that point into the stack frame.  */

#define ELIMINABLE_REGS							\
{									\
  {FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM},				\
  {ARG_POINTER_REGNUM,	 STACK_POINTER_REGNUM},				\
  {ARG_POINTER_REGNUM,	 FRAME_POINTER_REGNUM},				\
}

/* A C expression that returns nonzero if the compiler is allowed to try to
   replace register number FROM with register number TO.  The frame pointer
   is automatically handled.  */

#define CAN_ELIMINATE(FROM, TO) 1

/* This macro is similar to `INITIAL_FRAME_POINTER_OFFSET'.  It
   specifies the initial difference between the specified pair of
   registers.  This macro must be defined if `ELIMINABLE_REGS' is
   defined.  */
#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) \
  do								\
    {								\
      int fsize;						\
								\
      fsize = compute_frame_size (get_frame_size (), 0);	\
      if ((TO) == FRAME_POINTER_REGNUM				\
	  && (FROM) == ARG_POINTER_REGNUM)			\
	{							\
	  (OFFSET) = -16;					\
	  break;						\
	}							\
								\
      gcc_assert ((TO) == STACK_POINTER_REGNUM);		\
								\
      switch (FROM)						\
	{							\
	case FRAME_POINTER_REGNUM:				\
	  (OFFSET) = - fsize;					\
	  break;						\
								\
	case ARG_POINTER_REGNUM:				\
	  (OFFSET) = - fsize - 16;				\
	  break;						\
								\
	default:						\
	  gcc_unreachable ();					\
	}							\
    } while (0)
#endif

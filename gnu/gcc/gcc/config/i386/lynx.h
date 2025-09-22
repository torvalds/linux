/* Definitions for LynxOS on i386.
   Copyright (C) 1993, 1995, 1996, 2002, 2004, 2005
   Free Software Foundation, Inc. 

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

#define TARGET_VERSION fputs (" (i386/LynxOS)", stderr);

#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__LITTLE_ENDIAN__");	\
      builtin_define ("__x86__");		\
    }						\
  while (0)

/* The svr4 ABI for the i386 says that records and unions are returned
   in memory.  */

#define DEFAULT_PCC_STRUCT_RETURN 1

/* BSS_SECTION_ASM_OP gets defined i386/unix.h.  */

#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss (FILE, DECL, NAME, SIZE, ALIGN)

/* LynxOS's GDB counts the floating point registers from 16.  */

#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(n)						\
  (TARGET_64BIT ? dbx64_register_map[n]					\
   : (n) == 0 ? 0							\
   : (n) == 1 ? 2							\
   : (n) == 2 ? 1							\
   : (n) == 3 ? 3							\
   : (n) == 4 ? 6							\
   : (n) == 5 ? 7							\
   : (n) == 6 ? 5							\
   : (n) == 7 ? 4							\
   : ((n) >= FIRST_STACK_REG && (n) <= LAST_STACK_REG) ? (n) + 8	\
   : (-1))
  
/* A C statement to output to the stdio stream FILE an assembler
   command to advance the location counter to a multiple of 1<<LOG
   bytes if it is within MAX_SKIP bytes.

   This is used to align code labels according to Intel recommendations.  */

#ifdef HAVE_GAS_MAX_SKIP_P2ALIGN
#define ASM_OUTPUT_MAX_SKIP_ALIGN(FILE,LOG,MAX_SKIP)			\
  do {									\
    if ((LOG) != 0) {							\
      if ((MAX_SKIP) == 0) fprintf ((FILE), "\t.p2align %d\n", (LOG));	\
      else fprintf ((FILE), "\t.p2align %d,,%d\n", (LOG), (MAX_SKIP));	\
    }									\
  } while (0)
#endif

/* Undefine SUBTARGET_EXTRA_SPECS it is empty anyway.  We define it in
   config/lynx.h.  */

#undef SUBTARGET_EXTRA_SPECS

/* Undefine the definition from att.h to enable our default.  */

#undef ASM_OUTPUT_ALIGN

/* Undefine the definition from elfos.h to enable our default.  */

#undef PREFERRED_DEBUGGING_TYPE

/* The file i386.c defines TARGET_HAVE_TLS unconditionally if
   HAVE_AS_TLS is defined.  HAVE_AS_TLS is defined as gas support for
   TLS is detected by configure.  We undefine it here.  */

#undef HAVE_AS_TLS

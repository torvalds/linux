/* Definitions of target machine for GNU compiler, NetBSD/arm ELF version.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Wasabi Systems, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* Run-time Target Specification.  */
#undef TARGET_VERSION
#define TARGET_VERSION fputs (" (NetBSD/arm ELF)", stderr);

/* arm.h defaults to ARM6 CPU.  */

/* This defaults us to little-endian.  */
#ifndef TARGET_ENDIAN_DEFAULT
#define TARGET_ENDIAN_DEFAULT 0
#endif

#undef MULTILIB_DEFAULTS

/* Default it to use ATPCS with soft-VFP.  */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT			\
  (MASK_APCS_FRAME			\
   | TARGET_ENDIAN_DEFAULT)

#undef ARM_DEFAULT_ABI
#define ARM_DEFAULT_ABI ARM_ABI_ATPCS

#define TARGET_OS_CPP_BUILTINS()	\
  do					\
    {					\
      NETBSD_OS_CPP_BUILTINS_ELF();	\
    }					\
  while (0)

#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC NETBSD_CPP_SPEC

#undef SUBTARGET_EXTRA_ASM_SPEC
#define SUBTARGET_EXTRA_ASM_SPEC	\
  "-matpcs %{fpic|fpie:-k} %{fPIC|fPIE:-k}"

/* Default to full VFP if -mhard-float is specified.  */
#undef SUBTARGET_ASM_FLOAT_SPEC
#define SUBTARGET_ASM_FLOAT_SPEC	\
  "%{mhard-float:{!mfpu=*:-mfpu=vfp}}   \
   %{mfloat-abi=hard:{!mfpu=*:-mfpu=vfp}}"

#undef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS				\
  { "subtarget_extra_asm_spec",	SUBTARGET_EXTRA_ASM_SPEC }, \
  { "subtarget_asm_float_spec", SUBTARGET_ASM_FLOAT_SPEC }, \
  { "netbsd_link_spec",		NETBSD_LINK_SPEC_ELF },	\
  { "netbsd_entry_point",	NETBSD_ENTRY_POINT },

#define NETBSD_ENTRY_POINT "__start"

#undef LINK_SPEC
#define LINK_SPEC \
  "-X %{mbig-endian:-EB} %{mlittle-endian:-EL} \
   %(netbsd_link_spec)"

/* Make GCC agree with <machine/ansi.h>.  */

#undef SIZE_TYPE
#define SIZE_TYPE "long unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "long int"

/* We don't have any limit on the length as out debugger is GDB.  */
#undef DBX_CONTIN_LENGTH

/* NetBSD does its profiling differently to the Acorn compiler. We      
   don't need a word following the mcount call; and to skip it
   requires either an assembly stub or use of fomit-frame-pointer when  
   compiling the profiling functions.  Since we break Acorn CC
   compatibility below a little more won't hurt.  */
   
#undef ARM_FUNCTION_PROFILER                                  
#define ARM_FUNCTION_PROFILER(STREAM,LABELNO)		\
{							\
  asm_fprintf (STREAM, "\tmov\t%Rip, %Rlr\n");		\
  asm_fprintf (STREAM, "\tbl\t__mcount%s\n",		\
	       (TARGET_ARM && NEED_PLT_RELOC)		\
	       ? "(PLT)" : "");				\
}

/* VERY BIG NOTE: Change of structure alignment for NetBSD/arm.
   There are consequences you should be aware of...

   Normally GCC/arm uses a structure alignment of 32 for compatibility
   with armcc.  This means that structures are padded to a word
   boundary.  However this causes problems with bugged NetBSD kernel
   code (possibly userland code as well - I have not checked every
   binary).  The nature of this bugged code is to rely on sizeof()
   returning the correct size of various structures rounded to the  
   nearest byte (SCSI and ether code are two examples, the vm system
   is another).  This code breaks when the structure alignment is 32
   as sizeof() will report a word=rounded size.  By changing the        
   structure alignment to 8. GCC will conform to what is expected by
   NetBSD.
   
   This has several side effects that should be considered.
   1. Structures will only be aligned to the size of the largest member.
      i.e. structures containing only bytes will be byte aligned.
           structures containing shorts will be half word aligned.          
           structures containing ints will be word aligned.                 
  
      This means structures should be padded to a word boundary if
      alignment of 32 is required for byte structures etc.
       
   2. A potential performance penalty may exist if strings are no longer
      word aligned.  GCC will not be able to use word load/stores to copy
      short strings.

   This modification is not encouraged but with the present state of the
   NetBSD source tree it is currently the only solution that meets the
   requirements.  */

#undef DEFAULT_STRUCTURE_SIZE_BOUNDARY
#define DEFAULT_STRUCTURE_SIZE_BOUNDARY 8

/* Clear the instruction cache from `BEG' to `END'.  This makes a
   call to the ARM_SYNC_ICACHE architecture specific syscall.  */
#define CLEAR_INSN_CACHE(BEG, END)					\
do									\
  {									\
    extern int sysarch(int number, void *args);				\
    struct								\
      {									\
	unsigned int addr;						\
	int          len;						\
      } s;								\
    s.addr = (unsigned int)(BEG);					\
    s.len = (END) - (BEG);						\
    (void) sysarch (0, &s);						\
  }									\
while (0)

#undef FPUTYPE_DEFAULT
#define FPUTYPE_DEFAULT FPUTYPE_VFP


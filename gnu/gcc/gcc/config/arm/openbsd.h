/* Definitions of target machine for GNU compiler, OpenBSD/arm ELF version.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Contributed by Wasabi Systems, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* Run-time Target Specification.  */
#undef TARGET_VERSION
#define TARGET_VERSION fputs (" (OpenBSD/arm)", stderr);

/* This is used in ASM_FILE_START.  */
#undef ARM_OS_NAME
#define ARM_OS_NAME "OpenBSD"

/* Unsigned chars produces much better code than signed.  */
#define DEFAULT_SIGNED_CHAR  0


/* This defaults us to little-endian.  */
#ifndef TARGET_ENDIAN_DEFAULT
#define TARGET_ENDIAN_DEFAULT 0
#endif

#undef MULTILIB_DEFAULTS

/* armv6k default cpu.  */
#define SUBTARGET_CPU_DEFAULT TARGET_CPU_mpcore

/* We default to a soft-float ABI so that binaries can run on all
   target hardware.  */
#undef TARGET_DEFAULT_FLOAT_ABI
#define TARGET_DEFAULT_FLOAT_ABI ARM_FLOAT_ABI_SOFT

/* We default to the "aapcs-linux" ABI so that enums are int-sized by
   default.  */
#undef ARM_DEFAULT_ABI
#define ARM_DEFAULT_ABI ARM_ABI_AAPCS_LINUX

#define TARGET_OS_CPP_BUILTINS()	\
  do					\
    {					\
      builtin_define ("__GXX_MERGED_TYPEINFO_NAMES=0"); \
      OPENBSD_OS_CPP_BUILTINS_ELF();	\
    }					\
  while (0)

#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC OBSD_CPP_SPEC

/* OBSD_LINK_SPEC appropriate for OpenBSD.  Support for GCC options 
   -static, -assert, and -nostdlib.  */
#undef OBSD_LINK_SPEC
#ifdef OBSD_NO_DYNAMIC_LIBRARIES
#define OBSD_LINK_SPEC \
  "%{!nostdlib:%{!r*:%{!e*:-e __start}}} %{assert*}"
#else
#define OBSD_LINK_SPEC \
  "%{!shared:%{!nostdlib:%{!r*:%{!e*:-e __start}}}} \
   %{shared:-shared} %{R*} \
   %{static:-Bstatic} \
   %{!static:-Bdynamic} \
   %{rdynamic:-export-dynamic} \
   %{assert*} \
   %{!static:%{!dynamic-linker:-dynamic-linker /usr/libexec/ld.so}} \
   %{!nostdlib:-L/usr/lib}"
#endif

#undef SUBTARGET_EXTRA_ASM_SPEC
#define SUBTARGET_EXTRA_ASM_SPEC	\
  "%{mabi=apcs-gnu|mabi=atpcs:-meabi=gnu;:-meabi=4} %{fpic|fPIC|fpie|fPIE:-k}"

/* Default floating point model is soft-VFP.
   FIXME: -mhard-float currently implies FPA.  */
#undef SUBTARGET_ASM_FLOAT_SPEC
#define SUBTARGET_ASM_FLOAT_SPEC	\
  "%{mhard-float:-mfpu=fpa} \
   %{msoft-float:-mfpu=softvfp} \
   %{!mhard-float: \
     %{!msoft-float:-mfpu=softvfp}}"

#undef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS				\
  { "subtarget_extra_asm_spec",	SUBTARGET_EXTRA_ASM_SPEC }, \
  { "subtarget_asm_float_spec", SUBTARGET_ASM_FLOAT_SPEC }, \
  { "openbsd_link_spec",	OBSD_LINK_SPEC },	\
  { "openbsd_entry_point",	OPENBSD_ENTRY_POINT },

#define OPENBSD_ENTRY_POINT "__start"

/* Pass -X to the linker so that it will strip symbols starting with 'L' */
#undef LINK_SPEC
#define LINK_SPEC \
  "-X %{mbig-endian:-EB} %{mlittle-endian:-EL} \
   %(openbsd_link_spec)"

/* Make GCC agree with <machine/_types.h>.  */

#undef SIZE_TYPE
#define SIZE_TYPE "long unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "long int"

#undef INTMAX_TYPE
#define INTMAX_TYPE "long long int"

#undef UINTMAX_TYPE
#define UINTMAX_TYPE "long long unsigned int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

/* We don't have any limit on the length as out debugger is GDB.  */
#undef DBX_CONTIN_LENGTH

/* OpenBSD and NetBSD do their profiling differently to the Acorn compiler. We
   don't need a word following the mcount call; and to skip it
   requires either an assembly stub or use of fomit-frame-pointer when
   compiling the profiling functions.  Since we break Acorn CC
   compatibility below a little more won't hurt.  */
   
#undef ARM_FUNCTION_PROFILER                                  
#define ARM_FUNCTION_PROFILER(STREAM,LABELNO)		\
{							\
  asm_fprintf (STREAM, "\tmov\t%Rip, %Rlr\n");		\
  asm_fprintf (STREAM, "\tbl\t__mcount%s\n",		\
	       NEED_PLT_RELOC ? "(PLT)" : "");		\
}

/* On the ARM `@' introduces a comment, so we must use something else
   for .type directives.  */
#undef TYPE_OPERAND_FMT
#define TYPE_OPERAND_FMT "%%%s"

#undef FPUTYPE_DEFAULT
#define FPUTYPE_DEFAULT FPUTYPE_VFP

/* VERY BIG NOTE: Change of structure alignment for OpenBSD|NetBSD/arm.
   There are consequences you should be aware of...

   Normally GCC/arm uses a structure alignment of 32 for compatibility
   with armcc.  This means that structures are padded to a word
   boundary.  However this causes problems with bugged OpenBSD|NetBSD kernel
   code (possibly userland code as well - I have not checked every
   binary).  The nature of this bugged code is to rely on sizeof()
   returning the correct size of various structures rounded to the
   nearest byte (SCSI and ether code are two examples, the vm system
   is another).  This code breaks when the structure alignment is 32
   as sizeof() will report a word=rounded size.  By changing the
   structure alignment to 8. GCC will conform to what is expected by
   OpenBSD|NetBSD.
   
   This has several side effects that should be considered.
   1. Structures will only be aligned to the size of the largest member.
      i.e. structures containing only bytes will be byte aligned.
	   structures containing shorts will be half word alinged.
	   structures containing ints will be word aligned.

      This means structures should be padded to a word boundary if
      alignment of 32 is required for byte structures etc.

   2. A potential performance penalty may exist if strings are no longer
      word aligned.  GCC will not be able to use word load/stores to copy
      short strings.

   This modification is not encouraged but with the present state of the
   OpenBSD|NetBSD source tree it is currently the only solution that meets the
   requirements.  */

#undef DEFAULT_STRUCTURE_SIZE_BOUNDARY
#define DEFAULT_STRUCTURE_SIZE_BOUNDARY 8

/* Clear the instruction cache from `BEG' to `END'.  This makes a
   call to the ARM_SYNC_ICACHE architecture specific syscall.  */
#define CLEAR_INSN_CACHE(BEG, END)					\
do									\
  {									\
    extern int sysarch(int number, void *args);				\
    struct {								\
	unsigned int addr;						\
	int          len;						\
    } s;								\
    s.addr = (unsigned int)(BEG);					\
    s.len = (END) - (BEG);						\
    (void) sysarch (0, &s);						\
  }									\
while (0)

/* As an elf system, we need crtbegin/crtend stuff.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC "\
	%{!shared: %{pg:gcrt0%O%s} %{!pg:%{p:gcrt0%O%s} \
	%{!p:%{!static:crt0%O%s} %{static:%{nopie:crt0%O%s} \
	%{!nopie:rcrt0%O%s}}}} \
        crtbegin%O%s} %{shared:crtbeginS%O%s}"
#undef ENDFILE_SPEC
#define ENDFILE_SPEC "%{!shared:crtend%O%s} %{shared:crtendS%O%s}"

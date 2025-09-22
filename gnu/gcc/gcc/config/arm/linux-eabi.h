/* Configuration file for ARM GNU/Linux EABI targets.
   Copyright (C) 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC   

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

/* On EABI GNU/Linux, we want both the BPABI builtins and the
   GNU/Linux builtins.  */
#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS() 		\
  do 						\
    {						\
      TARGET_BPABI_CPP_BUILTINS();		\
      LINUX_TARGET_OS_CPP_BUILTINS();		\
    }						\
  while (false)

/* We default to a soft-float ABI so that binaries can run on all
   target hardware.  */
#undef TARGET_DEFAULT_FLOAT_ABI
#define TARGET_DEFAULT_FLOAT_ABI ARM_FLOAT_ABI_SOFT

/* We default to the "aapcs-linux" ABI so that enums are int-sized by
   default.  */
#undef ARM_DEFAULT_ABI
#define ARM_DEFAULT_ABI ARM_ABI_AAPCS_LINUX

/* Default to armv5t so that thumb shared libraries work.
   The ARM10TDMI core is the default for armv5t, so set
   SUBTARGET_CPU_DEFAULT to achieve this.  */
#undef SUBTARGET_CPU_DEFAULT
#define SUBTARGET_CPU_DEFAULT TARGET_CPU_arm10tdmi

#undef SUBTARGET_EXTRA_LINK_SPEC
#define SUBTARGET_EXTRA_LINK_SPEC " -m armelf_linux_eabi"

/* Use ld-linux.so.3 so that it will be possible to run "classic"
   GNU/Linux binaries on an EABI system.  */
#undef GLIBC_DYNAMIC_LINKER
#define GLIBC_DYNAMIC_LINKER "/lib/ld-linux.so.3"

/* At this point, bpabi.h will have clobbered LINK_SPEC.  We want to
   use the GNU/Linux version, not the generic BPABI version.  */
#undef LINK_SPEC
#define LINK_SPEC LINUX_TARGET_LINK_SPEC

/* Use the default LIBGCC_SPEC, not the version in linux-elf.h, as we
   do not use -lfloat.  */
#undef LIBGCC_SPEC

/* Use the AAPCS type for wchar_t, or the previous Linux default for
   non-AAPCS.  */
#undef WCHAR_TYPE
#define WCHAR_TYPE (TARGET_AAPCS_BASED ? "unsigned int" : "long int")

/* Clear the instruction cache from `beg' to `end'.  This makes an
   inline system call to SYS_cacheflush.  It is modified to work with
   both the original and EABI-only syscall interfaces.  */
#undef CLEAR_INSN_CACHE
#define CLEAR_INSN_CACHE(BEG, END)					\
{									\
  register unsigned long _beg __asm ("a1") = (unsigned long) (BEG);	\
  register unsigned long _end __asm ("a2") = (unsigned long) (END);	\
  register unsigned long _flg __asm ("a3") = 0;				\
  register unsigned long _scno __asm ("r7") = 0xf0002;			\
  __asm __volatile ("swi 0x9f0002		@ sys_cacheflush"	\
		    : "=r" (_beg)					\
		    : "0" (_beg), "r" (_end), "r" (_flg), "r" (_scno));	\
}

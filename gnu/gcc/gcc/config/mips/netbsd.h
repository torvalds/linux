/* Definitions of target machine for GNU compiler, for MIPS NetBSD systems.
   Copyright (C) 1993, 1995, 1996, 1997, 1999, 2000, 2001, 2002, 2003, 2004
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


/* Define default target values.  */

#undef MACHINE_TYPE
#if TARGET_ENDIAN_DEFAULT != 0
#define MACHINE_TYPE "NetBSD/mipseb ELF"
#else
#define MACHINE_TYPE "NetBSD/mipsel ELF"
#endif

#define TARGET_OS_CPP_BUILTINS()			\
  do							\
    {							\
      NETBSD_OS_CPP_BUILTINS_ELF();			\
      builtin_define ("__NO_LEADING_UNDERSCORES__");	\
      builtin_define ("__GP_SUPPORT__");		\
      if (TARGET_LONG64)				\
	builtin_define ("__LONG64");			\
							\
      if (TARGET_ABICALLS)				\
	builtin_define ("__ABICALLS__");		\
							\
      if (mips_abi == ABI_EABI)				\
	builtin_define ("__mips_eabi");			\
      else if (mips_abi == ABI_N32)			\
	builtin_define ("__mips_n32");			\
      else if (mips_abi == ABI_64)			\
	builtin_define ("__mips_n64");			\
      else if (mips_abi == ABI_O64)			\
	builtin_define ("__mips_o64");			\
    }							\
  while (0)

/* The generic MIPS TARGET_CPU_CPP_BUILTINS are incorrect for NetBSD.
   Specifically, they define too many namespace-invasive macros.  Override
   them here.  Note this is structured for easy comparison to the version
   in mips.h.

   FIXME: This probably isn't the best solution.  But in the absence
   of something better, it will have to do, for now.  */

#undef TARGET_CPU_CPP_BUILTINS
#define TARGET_CPU_CPP_BUILTINS()				\
  do								\
    {								\
      builtin_assert ("cpu=mips");				\
      builtin_define ("__mips__");				\
      builtin_define ("_mips");					\
								\
      /* No _R3000 or _R4000.  */				\
      if (TARGET_64BIT)						\
	builtin_define ("__mips64");				\
								\
      if (TARGET_FLOAT64)					\
	builtin_define ("__mips_fpr=64");			\
      else							\
	builtin_define ("__mips_fpr=32");			\
								\
      if (TARGET_MIPS16)					\
	builtin_define ("__mips16");				\
								\
      MIPS_CPP_SET_PROCESSOR ("_MIPS_ARCH", mips_arch_info);	\
      MIPS_CPP_SET_PROCESSOR ("_MIPS_TUNE", mips_tune_info);	\
								\
      if (ISA_MIPS1)						\
	builtin_define ("__mips=1");				\
      else if (ISA_MIPS2)					\
	builtin_define ("__mips=2");				\
      else if (ISA_MIPS3)					\
	builtin_define ("__mips=3");				\
      else if (ISA_MIPS4)					\
	builtin_define ("__mips=4");				\
      else if (ISA_MIPS32)					\
	{							\
	  builtin_define ("__mips=32");				\
	  builtin_define ("__mips_isa_rev=1");			\
	}							\
      else if (ISA_MIPS32R2)					\
	{							\
	  builtin_define ("__mips=32");				\
	  builtin_define ("__mips_isa_rev=2");			\
	}							\
      else if (ISA_MIPS64)					\
	{							\
	  builtin_define ("__mips=64");				\
	  builtin_define ("__mips_isa_rev=1");			\
	}							\
								\
      if (TARGET_HARD_FLOAT)					\
	builtin_define ("__mips_hard_float");			\
      else if (TARGET_SOFT_FLOAT)				\
	builtin_define ("__mips_soft_float");			\
								\
      if (TARGET_SINGLE_FLOAT)					\
	builtin_define ("__mips_single_float");			\
								\
      if (TARGET_BIG_ENDIAN)					\
	builtin_define ("__MIPSEB__");				\
      else							\
	builtin_define ("__MIPSEL__");				\
								\
      /* No language dialect defines.  */			\
								\
      /* ABIs handled in TARGET_OS_CPP_BUILTINS.  */		\
    }								\
  while (0)


/* Clean up after the generic MIPS/ELF configuration.  */
#undef MD_EXEC_PREFIX
#undef MD_STARTFILE_PREFIX

/* Extra specs we need.  */
#undef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS						\
  { "netbsd_cpp_spec",		NETBSD_CPP_SPEC },			\
  { "netbsd_link_spec",		NETBSD_LINK_SPEC_ELF },			\
  { "netbsd_entry_point",	NETBSD_ENTRY_POINT },

/* Provide a SUBTARGET_CPP_SPEC appropriate for NetBSD.  */

#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC "%(netbsd_cpp_spec)"

/* Provide a LINK_SPEC appropriate for a NetBSD/mips target.
   This is a copy of LINK_SPEC from <netbsd-elf.h> tweaked for
   the MIPS target.  */

#undef LINK_SPEC
#define LINK_SPEC \
  "%{EL:-m elf32lmip} \
   %{EB:-m elf32bmip} \
   %(endian_spec) \
   %{G*} %{mips1} %{mips2} %{mips3} %{mips4} %{mips32} %{mips32r2} %{mips64} \
   %{bestGnum} %{call_shared} %{no_archive} %{exact_version} \
   %(netbsd_link_spec)"

#define NETBSD_ENTRY_POINT "__start"

#undef SUBTARGET_ASM_SPEC
#define SUBTARGET_ASM_SPEC \
  "%{!mno-abicalls: \
     %{!fno-PIC:%{!fno-pic:-KPIC}}}"


/* -G is incompatible with -KPIC which is the default, so only allow objects
   in the small data section if the user explicitly asks for it.  */

#undef MIPS_DEFAULT_GVALUE
#define MIPS_DEFAULT_GVALUE 0


/* This defines which switch letters take arguments.  -G is a MIPS
   special.  */

#undef SWITCH_TAKES_ARG
#define SWITCH_TAKES_ARG(CHAR)						\
  (DEFAULT_SWITCH_TAKES_ARG (CHAR)					\
   || (CHAR) == 'R'							\
   || (CHAR) == 'G')


#undef ASM_FINAL_SPEC
#undef SET_ASM_OP


/* NetBSD hasn't historically provided _flush_cache(), but rather
   _cacheflush(), which takes the same arguments as the former.  */
#undef CACHE_FLUSH_FUNC
#define CACHE_FLUSH_FUNC "_cacheflush"


/* Make gcc agree with <machine/ansi.h> */

#undef WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

#undef WINT_TYPE
#define WINT_TYPE "int"

/* Definitions of target machine for GNU compiler,
   for 64 bit PowerPC linux.
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

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
   along with GCC; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#ifndef RS6000_BI_ARCH

#undef	DEFAULT_ABI
#define	DEFAULT_ABI ABI_AIX

#undef	TARGET_64BIT
#define	TARGET_64BIT 1

#define	DEFAULT_ARCH64_P 1
#define	RS6000_BI_ARCH_P 0

#else

#define	DEFAULT_ARCH64_P (TARGET_DEFAULT & MASK_64BIT)
#define	RS6000_BI_ARCH_P 1

#endif

#ifdef IN_LIBGCC2
#undef TARGET_64BIT
#ifdef __powerpc64__
#define TARGET_64BIT 1
#else
#define TARGET_64BIT 0
#endif
#endif

#undef	TARGET_AIX
#define	TARGET_AIX TARGET_64BIT

#ifdef HAVE_LD_NO_DOT_SYMS
/* New ABI uses a local sym for the function entry point.  */
extern int dot_symbols;
#undef DOT_SYMBOLS
#define DOT_SYMBOLS dot_symbols
#endif

#undef  PROCESSOR_DEFAULT
#define PROCESSOR_DEFAULT PROCESSOR_POWER4
#undef  PROCESSOR_DEFAULT64
#define PROCESSOR_DEFAULT64 PROCESSOR_POWER4

/* We don't need to generate entries in .fixup, except when
   -mrelocatable or -mrelocatable-lib is given.  */
#undef RELOCATABLE_NEEDS_FIXUP
#define RELOCATABLE_NEEDS_FIXUP \
  (target_flags & target_flags_explicit & MASK_RELOCATABLE)

#undef	RS6000_ABI_NAME
#define	RS6000_ABI_NAME "linux"

#define INVALID_64BIT "-m%s not supported in this configuration"
#define INVALID_32BIT INVALID_64BIT

#undef	SUBSUBTARGET_OVERRIDE_OPTIONS
#define	SUBSUBTARGET_OVERRIDE_OPTIONS				\
  do								\
    {								\
      if (!rs6000_explicit_options.alignment)			\
	rs6000_alignment_flags = MASK_ALIGN_NATURAL;		\
      if (TARGET_64BIT)						\
	{							\
	  if (DEFAULT_ABI != ABI_AIX)				\
	    {							\
	      rs6000_current_abi = ABI_AIX;			\
	      error (INVALID_64BIT, "call");			\
	    }							\
	  dot_symbols = !strcmp (rs6000_abi_name, "aixdesc");	\
	  if (target_flags & MASK_RELOCATABLE)			\
	    {							\
	      target_flags &= ~MASK_RELOCATABLE;		\
	      error (INVALID_64BIT, "relocatable");		\
	    }							\
	  if (target_flags & MASK_EABI)				\
	    {							\
	      target_flags &= ~MASK_EABI;			\
	      error (INVALID_64BIT, "eabi");			\
	    }							\
	  if (target_flags & MASK_PROTOTYPE)			\
	    {							\
	      target_flags &= ~MASK_PROTOTYPE;			\
	      error (INVALID_64BIT, "prototype");		\
	    }							\
	  if ((target_flags & MASK_POWERPC64) == 0)		\
	    {							\
	      target_flags |= MASK_POWERPC64;			\
	      error ("-m64 requires a PowerPC64 cpu");		\
	    }							\
	}							\
      else							\
	{							\
	  if (!RS6000_BI_ARCH_P)				\
	    error (INVALID_32BIT, "32");			\
	  if (TARGET_PROFILE_KERNEL)				\
	    {							\
	      target_flags &= ~MASK_PROFILE_KERNEL;		\
	      error (INVALID_32BIT, "profile-kernel");		\
	    }							\
	}							\
    }								\
  while (0)

#ifdef	RS6000_BI_ARCH

#undef	OVERRIDE_OPTIONS
#define	OVERRIDE_OPTIONS \
  rs6000_override_options (((TARGET_DEFAULT ^ target_flags) & MASK_64BIT) \
			   ? (char *) 0 : TARGET_CPU_DEFAULT)

#endif

#undef	ASM_DEFAULT_SPEC
#undef	ASM_SPEC
#undef	LINK_OS_LINUX_SPEC

#ifndef	RS6000_BI_ARCH
#define	ASM_DEFAULT_SPEC "-mppc64"
#define	ASM_SPEC	 "%(asm_spec64) %(asm_spec_common)"
#define	LINK_OS_LINUX_SPEC "%(link_os_linux_spec64)"
#else
#if DEFAULT_ARCH64_P
#define	ASM_DEFAULT_SPEC "-mppc%{!m32:64}"
#define	ASM_SPEC	 "%{m32:%(asm_spec32)}%{!m32:%(asm_spec64)} %(asm_spec_common)"
#define	LINK_OS_LINUX_SPEC "%{m32:%(link_os_linux_spec32)}%{!m32:%(link_os_linux_spec64)}"
#else
#define	ASM_DEFAULT_SPEC "-mppc%{m64:64}"
#define	ASM_SPEC	 "%{!m64:%(asm_spec32)}%{m64:%(asm_spec64)} %(asm_spec_common)"
#define	LINK_OS_LINUX_SPEC "%{!m64:%(link_os_linux_spec32)}%{m64:%(link_os_linux_spec64)}"
#endif
#endif

#define ASM_SPEC32 "-a32 %{n} %{T} %{Ym,*} %{Yd,*} \
%{mrelocatable} %{mrelocatable-lib} %{fpic:-K PIC} %{fPIC:-K PIC} \
%{memb} %{!memb: %{msdata: -memb} %{msdata=eabi: -memb}} \
%{!mlittle: %{!mlittle-endian: %{!mbig: %{!mbig-endian: \
    %{mcall-freebsd: -mbig} \
    %{mcall-i960-old: -mlittle} \
    %{mcall-linux: -mbig} \
    %{mcall-gnu: -mbig} \
    %{mcall-netbsd: -mbig} \
}}}}"

#define ASM_SPEC64 "-a64"

#define ASM_SPEC_COMMON "%(asm_cpu) \
%{.s: %{mregnames} %{mno-regnames}} %{.S: %{mregnames} %{mno-regnames}} \
%{v:-V} %{Qy:} %{!Qn:-Qy} %{Wa,*:%*} \
%{mlittle} %{mlittle-endian} %{mbig} %{mbig-endian}"

#undef	SUBSUBTARGET_EXTRA_SPECS
#define SUBSUBTARGET_EXTRA_SPECS \
  { "asm_spec_common",		ASM_SPEC_COMMON },			\
  { "asm_spec32",		ASM_SPEC32 },				\
  { "asm_spec64",		ASM_SPEC64 },				\
  { "link_os_linux_spec32",	LINK_OS_LINUX_SPEC32 },			\
  { "link_os_linux_spec64",	LINK_OS_LINUX_SPEC64 },

#undef	MULTILIB_DEFAULTS
#if DEFAULT_ARCH64_P
#define MULTILIB_DEFAULTS { "m64" }
#else
#define MULTILIB_DEFAULTS { "m32" }
#endif

#ifndef RS6000_BI_ARCH

/* 64-bit PowerPC Linux is always big-endian.  */
#undef	TARGET_LITTLE_ENDIAN
#define TARGET_LITTLE_ENDIAN	0

/* 64-bit PowerPC Linux always has a TOC.  */
#undef  TARGET_TOC
#define	TARGET_TOC		1

/* Some things from sysv4.h we don't do when 64 bit.  */
#undef	TARGET_RELOCATABLE
#define	TARGET_RELOCATABLE	0
#undef	TARGET_EABI
#define	TARGET_EABI		0
#undef	TARGET_PROTOTYPE
#define	TARGET_PROTOTYPE	0
#undef RELOCATABLE_NEEDS_FIXUP
#define RELOCATABLE_NEEDS_FIXUP 0

#endif

/* We use glibc _mcount for profiling.  */
#define NO_PROFILE_COUNTERS 1
#define PROFILE_HOOK(LABEL) \
  do { if (TARGET_64BIT) output_profile_hook (LABEL); } while (0)

/* PowerPC64 Linux word-aligns FP doubles when -malign-power is given.  */
#undef  ADJUST_FIELD_ALIGN
#define ADJUST_FIELD_ALIGN(FIELD, COMPUTED) \
  ((TARGET_ALTIVEC && TREE_CODE (TREE_TYPE (FIELD)) == VECTOR_TYPE)	\
   ? 128								\
   : (TARGET_64BIT							\
      && TARGET_ALIGN_NATURAL == 0					\
      && TYPE_MODE (TREE_CODE (TREE_TYPE (FIELD)) == ARRAY_TYPE		\
		    ? get_inner_array_type (FIELD)			\
		    : TREE_TYPE (FIELD)) == DFmode)			\
   ? MIN ((COMPUTED), 32)						\
   : (COMPUTED))

/* PowerPC64 Linux increases natural record alignment to doubleword if
   the first field is an FP double, only if in power alignment mode.  */
#undef  ROUND_TYPE_ALIGN
#define ROUND_TYPE_ALIGN(STRUCT, COMPUTED, SPECIFIED)			\
  ((TARGET_64BIT							\
    && (TREE_CODE (STRUCT) == RECORD_TYPE				\
	|| TREE_CODE (STRUCT) == UNION_TYPE				\
	|| TREE_CODE (STRUCT) == QUAL_UNION_TYPE)			\
    && TARGET_ALIGN_NATURAL == 0)					\
   ? rs6000_special_round_type_align (STRUCT, COMPUTED, SPECIFIED)	\
   : MAX ((COMPUTED), (SPECIFIED)))

/* Use the default for compiling target libs.  */
#ifdef IN_TARGET_LIBS
#undef TARGET_ALIGN_NATURAL
#define TARGET_ALIGN_NATURAL 1
#endif

/* Indicate that jump tables go in the text section.  */
#undef  JUMP_TABLES_IN_TEXT_SECTION
#define JUMP_TABLES_IN_TEXT_SECTION TARGET_64BIT

/* The linux ppc64 ABI isn't explicit on whether aggregates smaller
   than a doubleword should be padded upward or downward.  You could
   reasonably assume that they follow the normal rules for structure
   layout treating the parameter area as any other block of memory,
   then map the reg param area to registers.  i.e. pad upward.
   Setting both of the following defines results in this behavior.
   Setting just the first one will result in aggregates that fit in a
   doubleword being padded downward, and others being padded upward.
   Not a bad idea as this results in struct { int x; } being passed
   the same way as an int.  */
#define AGGREGATE_PADDING_FIXED TARGET_64BIT
#define AGGREGATES_PAD_UPWARD_ALWAYS 0

/* Specify padding for the last element of a block move between
   registers and memory.  FIRST is nonzero if this is the only
   element.  */
#define BLOCK_REG_PADDING(MODE, TYPE, FIRST) \
  (!(FIRST) ? upward : FUNCTION_ARG_PADDING (MODE, TYPE))

/* __throw will restore its own return address to be the same as the
   return address of the function that the throw is being made to.
   This is unfortunate, because we want to check the original
   return address to see if we need to restore the TOC.
   So we have to squirrel it away with this.  */
#define SETUP_FRAME_ADDRESSES() \
  do { if (TARGET_64BIT) rs6000_aix_emit_builtin_unwind_init (); } while (0)

/* Override svr4.h  */
#undef MD_EXEC_PREFIX
#undef MD_STARTFILE_PREFIX

/* Linux doesn't support saving and restoring 64-bit regs in a 32-bit
   process.  */
#define OS_MISSING_POWERPC64 !TARGET_64BIT

/* glibc has float and long double forms of math functions.  */
#undef  TARGET_C99_FUNCTIONS
#define TARGET_C99_FUNCTIONS (OPTION_GLIBC)

#undef  TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()			\
  do							\
    {							\
      if (TARGET_64BIT)					\
	{						\
	  builtin_define ("__PPC__");			\
	  builtin_define ("__PPC64__");			\
	  builtin_define ("__powerpc__");		\
	  builtin_define ("__powerpc64__");		\
	  builtin_assert ("cpu=powerpc64");		\
	  builtin_assert ("machine=powerpc64");		\
	}						\
      else						\
	{						\
	  builtin_define_std ("PPC");			\
	  builtin_define_std ("powerpc");		\
	  builtin_assert ("cpu=powerpc");		\
	  builtin_assert ("machine=powerpc");		\
	  TARGET_OS_SYSV_CPP_BUILTINS ();		\
	}						\
    }							\
  while (0)

#undef  CPP_OS_DEFAULT_SPEC
#define CPP_OS_DEFAULT_SPEC "%(cpp_os_linux)"

/* The GNU C++ standard library currently requires _GNU_SOURCE being
   defined on glibc-based systems. This temporary hack accomplishes this,
   it should go away as soon as libstdc++-v3 has a real fix.  */
#undef  CPLUSPLUS_CPP_SPEC
#define CPLUSPLUS_CPP_SPEC "-D_GNU_SOURCE %(cpp)"

#undef  LINK_SHLIB_SPEC
#define LINK_SHLIB_SPEC "%{shared:-shared} %{!shared: %{static:-static}}"

#undef  LIB_DEFAULT_SPEC
#define LIB_DEFAULT_SPEC "%(lib_linux)"

#undef  STARTFILE_DEFAULT_SPEC
#define STARTFILE_DEFAULT_SPEC "%(startfile_linux)"

#undef	ENDFILE_DEFAULT_SPEC
#define ENDFILE_DEFAULT_SPEC "%(endfile_linux)"

#undef	LINK_START_DEFAULT_SPEC
#define LINK_START_DEFAULT_SPEC "%(link_start_linux)"

#undef	LINK_OS_DEFAULT_SPEC
#define LINK_OS_DEFAULT_SPEC "%(link_os_linux)"

#define GLIBC_DYNAMIC_LINKER32 "/lib/ld.so.1"
#define GLIBC_DYNAMIC_LINKER64 "/lib64/ld64.so.1"
#define UCLIBC_DYNAMIC_LINKER32 "/lib/ld-uClibc.so.0"
#define UCLIBC_DYNAMIC_LINKER64 "/lib/ld64-uClibc.so.0"
#if UCLIBC_DEFAULT
#define CHOOSE_DYNAMIC_LINKER(G, U) "%{mglibc:%{muclibc:%e-mglibc and -muclibc used together}" G ";:" U "}"
#else
#define CHOOSE_DYNAMIC_LINKER(G, U) "%{muclibc:%{mglibc:%e-mglibc and -muclibc used together}" U ";:" G "}"
#endif
#define LINUX_DYNAMIC_LINKER32 \
  CHOOSE_DYNAMIC_LINKER (GLIBC_DYNAMIC_LINKER32, UCLIBC_DYNAMIC_LINKER32)
#define LINUX_DYNAMIC_LINKER64 \
  CHOOSE_DYNAMIC_LINKER (GLIBC_DYNAMIC_LINKER64, UCLIBC_DYNAMIC_LINKER64)


#define LINK_OS_LINUX_SPEC32 "-m elf32ppclinux %{!shared: %{!static: \
  %{rdynamic:-export-dynamic} \
  %{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER32 "}}}"

#define LINK_OS_LINUX_SPEC64 "-m elf64ppc %{!shared: %{!static: \
  %{rdynamic:-export-dynamic} \
  %{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER64 "}}}"

#undef  TOC_SECTION_ASM_OP
#define TOC_SECTION_ASM_OP \
  (TARGET_64BIT						\
   ? "\t.section\t\".toc\",\"aw\""			\
   : "\t.section\t\".got\",\"aw\"")

#undef  MINIMAL_TOC_SECTION_ASM_OP
#define MINIMAL_TOC_SECTION_ASM_OP \
  (TARGET_64BIT						\
   ? "\t.section\t\".toc1\",\"aw\""			\
   : ((TARGET_RELOCATABLE || flag_pic)			\
      ? "\t.section\t\".got2\",\"aw\""			\
      : "\t.section\t\".got1\",\"aw\""))

#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (PowerPC64 GNU/Linux)");

/* Must be at least as big as our pointer type.  */
#undef	SIZE_TYPE
#define	SIZE_TYPE (TARGET_64BIT ? "long unsigned int" : "unsigned int")

#undef	PTRDIFF_TYPE
#define	PTRDIFF_TYPE (TARGET_64BIT ? "long int" : "int")

#undef	WCHAR_TYPE
#define	WCHAR_TYPE (TARGET_64BIT ? "int" : "long int")
#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

/* Override rs6000.h definition.  */
#undef  ASM_APP_ON
#define ASM_APP_ON "#APP\n"

/* Override rs6000.h definition.  */
#undef  ASM_APP_OFF
#define ASM_APP_OFF "#NO_APP\n"

/* PowerPC no-op instruction.  */
#undef  RS6000_CALL_GLUE
#define RS6000_CALL_GLUE (TARGET_64BIT ? "nop" : "cror 31,31,31")

#undef  RS6000_MCOUNT
#define RS6000_MCOUNT "_mcount"

#ifdef __powerpc64__
/* _init and _fini functions are built from bits spread across many
   object files, each potentially with a different TOC pointer.  For
   that reason, place a nop after the call so that the linker can
   restore the TOC pointer if a TOC adjusting call stub is needed.  */
#if DOT_SYMBOLS
#define CRT_CALL_STATIC_FUNCTION(SECTION_OP, FUNC)	\
  asm (SECTION_OP "\n"					\
"	bl ." #FUNC "\n"				\
"	nop\n"						\
"	.previous");
#else
#define CRT_CALL_STATIC_FUNCTION(SECTION_OP, FUNC)	\
  asm (SECTION_OP "\n"					\
"	bl " #FUNC "\n"					\
"	nop\n"						\
"	.previous");
#endif
#endif

/* FP save and restore routines.  */
#undef  SAVE_FP_PREFIX
#define SAVE_FP_PREFIX (TARGET_64BIT ? "._savef" : "_savefpr_")
#undef  SAVE_FP_SUFFIX
#define SAVE_FP_SUFFIX (TARGET_64BIT ? "" : "_l")
#undef  RESTORE_FP_PREFIX
#define RESTORE_FP_PREFIX (TARGET_64BIT ? "._restf" : "_restfpr_")
#undef  RESTORE_FP_SUFFIX
#define RESTORE_FP_SUFFIX (TARGET_64BIT ? "" : "_l")

/* Dwarf2 debugging.  */
#undef  PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

/* This is how to declare the size of a function.  */
#undef	ASM_DECLARE_FUNCTION_SIZE
#define	ASM_DECLARE_FUNCTION_SIZE(FILE, FNAME, DECL)			\
  do									\
    {									\
      if (!flag_inhibit_size_directive)					\
	{								\
	  fputs ("\t.size\t", (FILE));					\
	  if (TARGET_64BIT && DOT_SYMBOLS)				\
	    putc ('.', (FILE));						\
	  assemble_name ((FILE), (FNAME));				\
	  fputs (",.-", (FILE));					\
	  rs6000_output_function_entry (FILE, FNAME);			\
	  putc ('\n', (FILE));						\
	}								\
    }									\
  while (0)

/* Return nonzero if this entry is to be written into the constant
   pool in a special way.  We do so if this is a SYMBOL_REF, LABEL_REF
   or a CONST containing one of them.  If -mfp-in-toc (the default),
   we also do this for floating-point constants.  We actually can only
   do this if the FP formats of the target and host machines are the
   same, but we can't check that since not every file that uses
   GO_IF_LEGITIMATE_ADDRESS_P includes real.h.  We also do this when
   we can write the entry into the TOC and the entry is not larger
   than a TOC entry.  */

#undef  ASM_OUTPUT_SPECIAL_POOL_ENTRY_P
#define ASM_OUTPUT_SPECIAL_POOL_ENTRY_P(X, MODE)			\
  (TARGET_TOC								\
   && (GET_CODE (X) == SYMBOL_REF					\
       || (GET_CODE (X) == CONST && GET_CODE (XEXP (X, 0)) == PLUS	\
	   && GET_CODE (XEXP (XEXP (X, 0), 0)) == SYMBOL_REF)		\
       || GET_CODE (X) == LABEL_REF					\
       || (GET_CODE (X) == CONST_INT 					\
	   && GET_MODE_BITSIZE (MODE) <= GET_MODE_BITSIZE (Pmode))	\
       || (GET_CODE (X) == CONST_DOUBLE					\
	   && ((TARGET_64BIT						\
		&& (TARGET_POWERPC64					\
		    || TARGET_MINIMAL_TOC				\
		    || (SCALAR_FLOAT_MODE_P (GET_MODE (X))		\
			&& ! TARGET_NO_FP_IN_TOC)))			\
	       || (!TARGET_64BIT					\
		   && !TARGET_NO_FP_IN_TOC				\
		   && !TARGET_RELOCATABLE				\
		   && SCALAR_FLOAT_MODE_P (GET_MODE (X))		\
		   && BITS_PER_WORD == HOST_BITS_PER_INT)))))

/* Select a format to encode pointers in exception handling data.  CODE
   is 0 for data, 1 for code labels, 2 for function pointers.  GLOBAL is
   true if the symbol may be affected by dynamic relocations.  */
#undef	ASM_PREFERRED_EH_DATA_FORMAT
#define	ASM_PREFERRED_EH_DATA_FORMAT(CODE, GLOBAL) \
  ((TARGET_64BIT || flag_pic || TARGET_RELOCATABLE)			\
   ? (((GLOBAL) ? DW_EH_PE_indirect : 0) | DW_EH_PE_pcrel		\
      | (TARGET_64BIT ? DW_EH_PE_udata8 : DW_EH_PE_sdata4))		\
   : DW_EH_PE_absptr)

/* For backward compatibility, we must continue to use the AIX
   structure return convention.  */
#undef DRAFT_V4_STRUCT_RET
#define DRAFT_V4_STRUCT_RET (!TARGET_64BIT)

#define TARGET_ASM_FILE_END rs6000_elf_end_indicate_exec_stack

#define TARGET_POSIX_IO

#define LINK_GCC_C_SEQUENCE_SPEC \
  "%{static:--start-group} %G %L %{static:--end-group}%{!static:%G}"

/* Use --as-needed -lgcc_s for eh support.  */
#ifdef HAVE_LD_AS_NEEDED
#define USE_LD_AS_NEEDED 1
#endif

#define MD_UNWIND_SUPPORT "config/rs6000/linux-unwind.h"

#ifdef TARGET_LIBC_PROVIDES_SSP
/* ppc32 glibc provides __stack_chk_guard in -0x7008(2),
   ppc64 glibc provides it at -0x7010(13).  */
#define TARGET_THREAD_SSP_OFFSET	(TARGET_64BIT ? -0x7010 : -0x7008)
#endif

#define POWERPC_LINUX

/* ppc{32,64} linux has 128-bit long double support in glibc 2.4 and later.  */
#ifdef TARGET_DEFAULT_LONG_DOUBLE_128
#define RS6000_DEFAULT_LONG_DOUBLE_SIZE 128
#endif

/* Definitions of target machine for GNU compiler for Renesas / SuperH SH.
   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Steve Chamberlain (sac@cygnus.com).
   Improved by Jim Wilson (wilson@cygnus.com).

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

#ifndef GCC_SH_H
#define GCC_SH_H

#define TARGET_VERSION \
  fputs (" (Hitachi SH)", stderr);

/* Unfortunately, insn-attrtab.c doesn't include insn-codes.h.  We can't
   include it here, because bconfig.h is also included by gencodes.c .  */
/* ??? No longer true.  */
extern int code_for_indirect_jump_scratch;

#define TARGET_CPU_CPP_BUILTINS() \
do { \
  builtin_define ("__sh__"); \
  builtin_assert ("cpu=sh"); \
  builtin_assert ("machine=sh"); \
  switch ((int) sh_cpu) \
    { \
    case PROCESSOR_SH1: \
      builtin_define ("__sh1__"); \
      break; \
    case PROCESSOR_SH2: \
      builtin_define ("__sh2__"); \
      break; \
    case PROCESSOR_SH2E: \
      builtin_define ("__SH2E__"); \
      break; \
    case PROCESSOR_SH2A: \
      builtin_define ("__SH2A__"); \
      builtin_define (TARGET_SH2A_DOUBLE \
		      ? (TARGET_FPU_SINGLE ? "__SH2A_SINGLE__" : "__SH2A_DOUBLE__") \
		      : TARGET_FPU_ANY ? "__SH2A_SINGLE_ONLY__" \
		      : "__SH2A_NOFPU__"); \
      break; \
    case PROCESSOR_SH3: \
      builtin_define ("__sh3__"); \
      builtin_define ("__SH3__"); \
      if (TARGET_HARD_SH4) \
	builtin_define ("__SH4_NOFPU__"); \
      break; \
    case PROCESSOR_SH3E: \
      builtin_define (TARGET_HARD_SH4 ? "__SH4_SINGLE_ONLY__" : "__SH3E__"); \
      break; \
    case PROCESSOR_SH4: \
      builtin_define (TARGET_FPU_SINGLE ? "__SH4_SINGLE__" : "__SH4__"); \
      break; \
    case PROCESSOR_SH4A: \
      builtin_define ("__SH4A__"); \
      builtin_define (TARGET_SH4 \
		      ? (TARGET_FPU_SINGLE ? "__SH4_SINGLE__" : "__SH4__") \
		      : TARGET_FPU_ANY ? "__SH4_SINGLE_ONLY__" \
		      : "__SH4_NOFPU__"); \
      break; \
    case PROCESSOR_SH5: \
      { \
	builtin_define_with_value ("__SH5__", \
				   TARGET_SHMEDIA64 ? "64" : "32", 0); \
	builtin_define_with_value ("__SHMEDIA__", \
				   TARGET_SHMEDIA ? "1" : "0", 0); \
	if (! TARGET_FPU_DOUBLE) \
	  builtin_define ("__SH4_NOFPU__"); \
      } \
    } \
  if (TARGET_FPU_ANY) \
    builtin_define ("__SH_FPU_ANY__"); \
  if (TARGET_FPU_DOUBLE) \
    builtin_define ("__SH_FPU_DOUBLE__"); \
  if (TARGET_HITACHI) \
    builtin_define ("__HITACHI__"); \
  builtin_define (TARGET_LITTLE_ENDIAN \
		  ? "__LITTLE_ENDIAN__" : "__BIG_ENDIAN__"); \
} while (0)

/* We can not debug without a frame pointer.  */
/* #define CAN_DEBUG_WITHOUT_FP */

#define CONDITIONAL_REGISTER_USAGE do					\
{									\
  int regno;								\
  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno ++)		\
    if (! VALID_REGISTER_P (regno))					\
      fixed_regs[regno] = call_used_regs[regno] = 1;			\
  /* R8 and R9 are call-clobbered on SH5, but not on earlier SH ABIs.  */ \
  if (TARGET_SH5)							\
    {									\
      call_used_regs[FIRST_GENERAL_REG + 8]				\
	= call_used_regs[FIRST_GENERAL_REG + 9] = 1;			\
      call_really_used_regs[FIRST_GENERAL_REG + 8]			\
	= call_really_used_regs[FIRST_GENERAL_REG + 9] = 1;		\
    }									\
  if (TARGET_SHMEDIA)							\
    {									\
      regno_reg_class[FIRST_GENERAL_REG] = GENERAL_REGS;		\
      CLEAR_HARD_REG_SET (reg_class_contents[FP0_REGS]);		\
      regno_reg_class[FIRST_FP_REG] = FP_REGS;				\
    }									\
  if (flag_pic)								\
    {									\
      fixed_regs[PIC_OFFSET_TABLE_REGNUM] = 1;				\
      call_used_regs[PIC_OFFSET_TABLE_REGNUM] = 1;			\
    }									\
  /* Renesas saves and restores mac registers on call.  */		\
  if (TARGET_HITACHI && ! TARGET_NOMACSAVE)				\
    {									\
      call_really_used_regs[MACH_REG] = 0;				\
      call_really_used_regs[MACL_REG] = 0;				\
    }									\
  for (regno = FIRST_FP_REG + (TARGET_LITTLE_ENDIAN != 0);		\
       regno <= LAST_FP_REG; regno += 2)				\
    SET_HARD_REG_BIT (reg_class_contents[DF_HI_REGS], regno);		\
  if (TARGET_SHMEDIA)							\
    {									\
      for (regno = FIRST_TARGET_REG; regno <= LAST_TARGET_REG; regno ++)\
	if (! fixed_regs[regno] && call_really_used_regs[regno])	\
	  SET_HARD_REG_BIT (reg_class_contents[SIBCALL_REGS], regno);	\
    }									\
  else									\
    for (regno = FIRST_GENERAL_REG; regno <= LAST_GENERAL_REG; regno++)	\
      if (! fixed_regs[regno] && call_really_used_regs[regno])		\
	SET_HARD_REG_BIT (reg_class_contents[SIBCALL_REGS], regno);	\
} while (0)

/* Nonzero if this is an ELF target - compile time only */
#define TARGET_ELF 0

/* Nonzero if we should generate code using type 2E insns.  */
#define TARGET_SH2E (TARGET_SH2 && TARGET_SH_E)

/* Nonzero if we should generate code using type 2A insns.  */
#define TARGET_SH2A TARGET_HARD_SH2A
/* Nonzero if we should generate code using type 2A SF insns.  */
#define TARGET_SH2A_SINGLE (TARGET_SH2A && TARGET_SH2E)
/* Nonzero if we should generate code using type 2A DF insns.  */
#define TARGET_SH2A_DOUBLE (TARGET_HARD_SH2A_DOUBLE && TARGET_SH2A)

/* Nonzero if we should generate code using type 3E insns.  */
#define TARGET_SH3E (TARGET_SH3 && TARGET_SH_E)

/* Nonzero if the cache line size is 32.  */
#define TARGET_CACHE32 (TARGET_HARD_SH4 || TARGET_SH5)

/* Nonzero if we schedule for a superscalar implementation.  */
#define TARGET_SUPERSCALAR TARGET_HARD_SH4

/* Nonzero if the target has separate instruction and data caches.  */
#define TARGET_HARVARD (TARGET_HARD_SH4 || TARGET_SH5)

/* Nonzero if a double-precision FPU is available.  */
#define TARGET_FPU_DOUBLE \
  ((target_flags & MASK_SH4) != 0 || TARGET_SH2A_DOUBLE)

/* Nonzero if an FPU is available.  */
#define TARGET_FPU_ANY (TARGET_SH2E || TARGET_FPU_DOUBLE)

/* Nonzero if we should generate code using type 4 insns.  */
#undef TARGET_SH4
#define TARGET_SH4 ((target_flags & MASK_SH4) != 0 && TARGET_SH1)

/* Nonzero if we're generating code for the common subset of
   instructions present on both SH4a and SH4al-dsp.  */
#define TARGET_SH4A_ARCH TARGET_SH4A

/* Nonzero if we're generating code for SH4a, unless the use of the
   FPU is disabled (which makes it compatible with SH4al-dsp).  */
#define TARGET_SH4A_FP (TARGET_SH4A_ARCH && TARGET_FPU_ANY)

/* Nonzero if we should generate code using the SHcompact instruction
   set and 32-bit ABI.  */
#define TARGET_SHCOMPACT (TARGET_SH5 && TARGET_SH1)

/* Nonzero if we should generate code using the SHmedia instruction
   set and ABI.  */
#define TARGET_SHMEDIA (TARGET_SH5 && ! TARGET_SH1)

/* Nonzero if we should generate code using the SHmedia ISA and 32-bit
   ABI.  */
#define TARGET_SHMEDIA32 (TARGET_SH5 && ! TARGET_SH1 && TARGET_SH_E)

/* Nonzero if we should generate code using the SHmedia ISA and 64-bit
   ABI.  */
#define TARGET_SHMEDIA64 (TARGET_SH5 && ! TARGET_SH1 && ! TARGET_SH_E)

/* Nonzero if we should generate code using SHmedia FPU instructions.  */
#define TARGET_SHMEDIA_FPU (TARGET_SHMEDIA && TARGET_FPU_DOUBLE)

/* This is not used by the SH2E calling convention  */
#define TARGET_VARARGS_PRETEND_ARGS(FUN_DECL) \
  (TARGET_SH1 && ! TARGET_SH2E && ! TARGET_SH5 \
   && ! (TARGET_HITACHI || sh_attr_renesas_p (FUN_DECL)))

#ifndef TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT SELECT_SH1
#define SUPPORT_SH1 1
#define SUPPORT_SH2E 1
#define SUPPORT_SH4 1
#define SUPPORT_SH4_SINGLE 1
#define SUPPORT_SH2A 1
#define SUPPORT_SH2A_SINGLE 1
#endif

#define TARGET_DIVIDE_INV \
  (sh_div_strategy == SH_DIV_INV || sh_div_strategy == SH_DIV_INV_MINLAT \
   || sh_div_strategy == SH_DIV_INV20U || sh_div_strategy == SH_DIV_INV20L \
   || sh_div_strategy == SH_DIV_INV_CALL \
   || sh_div_strategy == SH_DIV_INV_CALL2 || sh_div_strategy == SH_DIV_INV_FP)
#define TARGET_DIVIDE_FP (sh_div_strategy == SH_DIV_FP)
#define TARGET_DIVIDE_INV_FP (sh_div_strategy == SH_DIV_INV_FP)
#define TARGET_DIVIDE_CALL2 (sh_div_strategy == SH_DIV_CALL2)
#define TARGET_DIVIDE_INV_MINLAT (sh_div_strategy == SH_DIV_INV_MINLAT)
#define TARGET_DIVIDE_INV20U (sh_div_strategy == SH_DIV_INV20U)
#define TARGET_DIVIDE_INV20L (sh_div_strategy == SH_DIV_INV20L)
#define TARGET_DIVIDE_INV_CALL (sh_div_strategy == SH_DIV_INV_CALL)
#define TARGET_DIVIDE_INV_CALL2 (sh_div_strategy == SH_DIV_INV_CALL2)

#define SELECT_SH1               (MASK_SH1)
#define SELECT_SH2               (MASK_SH2 | SELECT_SH1)
#define SELECT_SH2E              (MASK_SH_E | MASK_SH2 | MASK_SH1 \
				  | MASK_FPU_SINGLE)
#define SELECT_SH2A              (MASK_SH_E | MASK_HARD_SH2A \
				  | MASK_HARD_SH2A_DOUBLE \
				  | MASK_SH2 | MASK_SH1)
#define SELECT_SH2A_NOFPU        (MASK_HARD_SH2A | MASK_SH2 | MASK_SH1)
#define SELECT_SH2A_SINGLE_ONLY  (MASK_SH_E | MASK_HARD_SH2A | MASK_SH2 \
				  | MASK_SH1 | MASK_FPU_SINGLE)
#define SELECT_SH2A_SINGLE       (MASK_SH_E | MASK_HARD_SH2A \
				  | MASK_FPU_SINGLE | MASK_HARD_SH2A_DOUBLE \
				  | MASK_SH2 | MASK_SH1)
#define SELECT_SH3               (MASK_SH3 | SELECT_SH2)
#define SELECT_SH3E              (MASK_SH_E | MASK_FPU_SINGLE | SELECT_SH3)
#define SELECT_SH4_NOFPU         (MASK_HARD_SH4 | SELECT_SH3)
#define SELECT_SH4_SINGLE_ONLY   (MASK_HARD_SH4 | SELECT_SH3E)
#define SELECT_SH4               (MASK_SH4 | MASK_SH_E | MASK_HARD_SH4 \
				  | SELECT_SH3)
#define SELECT_SH4_SINGLE        (MASK_FPU_SINGLE | SELECT_SH4)
#define SELECT_SH4A_NOFPU        (MASK_SH4A | SELECT_SH4_NOFPU)
#define SELECT_SH4A_SINGLE_ONLY  (MASK_SH4A | SELECT_SH4_SINGLE_ONLY)
#define SELECT_SH4A              (MASK_SH4A | SELECT_SH4)
#define SELECT_SH4A_SINGLE       (MASK_SH4A | SELECT_SH4_SINGLE)
#define SELECT_SH5_64MEDIA       (MASK_SH5 | MASK_SH4)
#define SELECT_SH5_64MEDIA_NOFPU (MASK_SH5)
#define SELECT_SH5_32MEDIA       (MASK_SH5 | MASK_SH4 | MASK_SH_E)
#define SELECT_SH5_32MEDIA_NOFPU (MASK_SH5 | MASK_SH_E)
#define SELECT_SH5_COMPACT       (MASK_SH5 | MASK_SH4 | SELECT_SH3E)
#define SELECT_SH5_COMPACT_NOFPU (MASK_SH5 | SELECT_SH3)

#if SUPPORT_SH1
#define SUPPORT_SH2 1
#endif
#if SUPPORT_SH2
#define SUPPORT_SH3 1
#endif
#if SUPPORT_SH3
#define SUPPORT_SH4_NOFPU 1
#endif
#if SUPPORT_SH4_NOFPU
#define SUPPORT_SH4A_NOFPU 1
#define SUPPORT_SH4AL 1
#define SUPPORT_SH2A_NOFPU 1
#endif

#if SUPPORT_SH2E
#define SUPPORT_SH3E 1
#endif
#if SUPPORT_SH3E
#define SUPPORT_SH4_SINGLE_ONLY 1
#define SUPPORT_SH4A_SINGLE_ONLY 1
#define SUPPORT_SH2A_SINGLE_ONLY 1
#endif

#if SUPPORT_SH4
#define SUPPORT_SH4A 1
#endif

#if SUPPORT_SH4_SINGLE
#define SUPPORT_SH4A_SINGLE 1
#endif

#if SUPPORT_SH5_COMPAT
#define SUPPORT_SH5_32MEDIA 1
#endif

#if SUPPORT_SH5_COMPACT_NOFPU
#define SUPPORT_SH5_32MEDIA_NOFPU 1
#endif

#define SUPPORT_ANY_SH5_32MEDIA \
  (SUPPORT_SH5_32MEDIA || SUPPORT_SH5_32MEDIA_NOFPU)
#define SUPPORT_ANY_SH5_64MEDIA \
  (SUPPORT_SH5_64MEDIA || SUPPORT_SH5_64MEDIA_NOFPU)
#define SUPPORT_ANY_SH5 \
  (SUPPORT_ANY_SH5_32MEDIA || SUPPORT_ANY_SH5_64MEDIA)

/* Reset all target-selection flags.  */
#define MASK_ARCH (MASK_SH1 | MASK_SH2 | MASK_SH3 | MASK_SH_E | MASK_SH4 \
		   | MASK_HARD_SH2A | MASK_HARD_SH2A_DOUBLE | MASK_SH4A \
		   | MASK_HARD_SH4 | MASK_FPU_SINGLE | MASK_SH5)

/* This defaults us to big-endian.  */
#ifndef TARGET_ENDIAN_DEFAULT
#define TARGET_ENDIAN_DEFAULT 0
#endif

#ifndef TARGET_OPT_DEFAULT
#define TARGET_OPT_DEFAULT  MASK_ADJUST_UNROLL
#endif

#define TARGET_DEFAULT \
  (TARGET_CPU_DEFAULT | TARGET_ENDIAN_DEFAULT | TARGET_OPT_DEFAULT)

#ifndef SH_MULTILIB_CPU_DEFAULT
#define SH_MULTILIB_CPU_DEFAULT "m1"
#endif

#if TARGET_ENDIAN_DEFAULT
#define MULTILIB_DEFAULTS { "ml", SH_MULTILIB_CPU_DEFAULT }
#else
#define MULTILIB_DEFAULTS { "mb", SH_MULTILIB_CPU_DEFAULT }
#endif

#define CPP_SPEC " %(subtarget_cpp_spec) "

#ifndef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC ""
#endif

#ifndef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS
#endif

#define EXTRA_SPECS						\
  { "subtarget_cpp_spec", SUBTARGET_CPP_SPEC },			\
  { "link_emul_prefix", LINK_EMUL_PREFIX },			\
  { "link_default_cpu_emul", LINK_DEFAULT_CPU_EMUL },		\
  { "subtarget_link_emul_suffix", SUBTARGET_LINK_EMUL_SUFFIX },	\
  { "subtarget_link_spec", SUBTARGET_LINK_SPEC },		\
  { "subtarget_asm_endian_spec", SUBTARGET_ASM_ENDIAN_SPEC },	\
  { "subtarget_asm_relax_spec", SUBTARGET_ASM_RELAX_SPEC },	\
  { "subtarget_asm_isa_spec", SUBTARGET_ASM_ISA_SPEC },		\
  { "subtarget_asm_spec", SUBTARGET_ASM_SPEC },			\
  SUBTARGET_EXTRA_SPECS

#if TARGET_CPU_DEFAULT & MASK_HARD_SH4
#define SUBTARGET_ASM_RELAX_SPEC "%{!m1:%{!m2:%{!m3*:%{!m5*:-isa=sh4-up}}}}"
#else
#define SUBTARGET_ASM_RELAX_SPEC "%{m4*:-isa=sh4-up}"
#endif

#define SH_ASM_SPEC \
 "%(subtarget_asm_endian_spec) %{mrelax:-relax %(subtarget_asm_relax_spec)}\
%(subtarget_asm_isa_spec) %(subtarget_asm_spec)\
%{m2a:--isa=sh2a} \
%{m2a-single:--isa=sh2a} \
%{m2a-single-only:--isa=sh2a} \
%{m2a-nofpu:--isa=sh2a-nofpu} \
%{m5-compact*:--isa=SHcompact} \
%{m5-32media*:--isa=SHmedia --abi=32} \
%{m5-64media*:--isa=SHmedia --abi=64} \
%{m4al:-dsp} %{mcut2-workaround:-cut2-workaround}"

#define ASM_SPEC SH_ASM_SPEC

#ifndef SUBTARGET_ASM_ENDIAN_SPEC
#if TARGET_ENDIAN_DEFAULT == MASK_LITTLE_ENDIAN
#define SUBTARGET_ASM_ENDIAN_SPEC "%{mb:-big} %{!mb:-little}"
#else
#define SUBTARGET_ASM_ENDIAN_SPEC "%{ml:-little} %{!ml:-big}"
#endif
#endif

#if STRICT_NOFPU == 1
/* Strict nofpu means that the compiler should tell the assembler
   to reject FPU instructions. E.g. from ASM inserts.  */
#if TARGET_CPU_DEFAULT & MASK_HARD_SH4 && !(TARGET_CPU_DEFAULT & MASK_SH_E)
#define SUBTARGET_ASM_ISA_SPEC "%{!m1:%{!m2:%{!m3*:%{m4-nofpu|!m4*:%{!m5:-isa=sh4-nofpu}}}}}"
#else
/* If there were an -isa option for sh5-nofpu then it would also go here. */
#define SUBTARGET_ASM_ISA_SPEC \
 "%{m4-nofpu:-isa=sh4-nofpu} " ASM_ISA_DEFAULT_SPEC
#endif
#else /* ! STRICT_NOFPU */
#define SUBTARGET_ASM_ISA_SPEC ASM_ISA_DEFAULT_SPEC
#endif

#ifndef SUBTARGET_ASM_SPEC
#define SUBTARGET_ASM_SPEC ""
#endif

#if TARGET_ENDIAN_DEFAULT == MASK_LITTLE_ENDIAN
#define LINK_EMUL_PREFIX "sh%{!mb:l}"
#else
#define LINK_EMUL_PREFIX "sh%{ml:l}"
#endif

#if TARGET_CPU_DEFAULT & MASK_SH5
#if TARGET_CPU_DEFAULT & MASK_SH_E
#define LINK_DEFAULT_CPU_EMUL "32"
#if TARGET_CPU_DEFAULT & MASK_SH1
#define ASM_ISA_SPEC_DEFAULT "--isa=SHcompact"
#else
#define ASM_ISA_SPEC_DEFAULT "--isa=SHmedia --abi=32"
#endif /* MASK_SH1 */
#else /* !MASK_SH_E */
#define LINK_DEFAULT_CPU_EMUL "64"
#define ASM_ISA_SPEC_DEFAULT "--isa=SHmedia --abi=64"
#endif /* MASK_SH_E */
#define ASM_ISA_DEFAULT_SPEC \
" %{!m1:%{!m2*:%{!m3*:%{!m4*:%{!m5*:" ASM_ISA_SPEC_DEFAULT "}}}}}"
#else /* !MASK_SH5 */
#define LINK_DEFAULT_CPU_EMUL ""
#define ASM_ISA_DEFAULT_SPEC ""
#endif /* MASK_SH5 */

#define SUBTARGET_LINK_EMUL_SUFFIX ""
#define SUBTARGET_LINK_SPEC ""

/* svr4.h redefines LINK_SPEC inappropriately, so go via SH_LINK_SPEC,
   so that we can undo the damage without code replication.  */
#define LINK_SPEC SH_LINK_SPEC

#define SH_LINK_SPEC "\
-m %(link_emul_prefix)\
%{m5-compact*|m5-32media*:32}\
%{m5-64media*:64}\
%{!m1:%{!m2:%{!m3*:%{!m4*:%{!m5*:%(link_default_cpu_emul)}}}}}\
%(subtarget_link_emul_suffix) \
%{mrelax:-relax} %(subtarget_link_spec)"

#ifndef SH_DIV_STR_FOR_SIZE
#define SH_DIV_STR_FOR_SIZE "call"
#endif

#define DRIVER_SELF_SPECS "%{m2a:%{ml:%eSH2a does not support little-endian}}"
#define OPTIMIZATION_OPTIONS(LEVEL,SIZE)				\
do {									\
  if (LEVEL)								\
    {									\
      flag_omit_frame_pointer = -1;					\
      if (! SIZE)							\
	sh_div_str = "inv:minlat";					\
    }									\
  if (SIZE)								\
    {									\
      target_flags |= MASK_SMALLCODE;					\
      sh_div_str = SH_DIV_STR_FOR_SIZE ;				\
    }									\
  /* We can't meaningfully test TARGET_SHMEDIA here, because -m options	\
     haven't been parsed yet, hence we';d read only the default.	\
     sh_target_reg_class will return NO_REGS if this is not SHMEDIA, so	\
     it's OK to always set flag_branch_target_load_optimize.  */	\
  if (LEVEL > 1)							\
    {									\
      flag_branch_target_load_optimize = 1;				\
      if (! (SIZE))							\
	target_flags |= MASK_SAVE_ALL_TARGET_REGS;			\
    }									\
  /* Likewise, we can't meaningfully test TARGET_SH2E / TARGET_IEEE	\
     here, so leave it to OVERRIDE_OPTIONS to set			\
    flag_finite_math_only.  We set it to 2 here so we know if the user	\
    explicitly requested this to be on or off.  */			\
  flag_finite_math_only = 2;						\
  /* If flag_schedule_insns is 1, we set it to 2 here so we know if	\
     the user explicitly requested this to be on or off.  */		\
  if (flag_schedule_insns > 0)						\
    flag_schedule_insns = 2;						\
} while (0)

#define ASSEMBLER_DIALECT assembler_dialect

extern int assembler_dialect;

enum sh_divide_strategy_e {
  SH_DIV_CALL,
  SH_DIV_CALL2,
  SH_DIV_FP,
  SH_DIV_INV,
  SH_DIV_INV_MINLAT,
  SH_DIV_INV20U,
  SH_DIV_INV20L,
  SH_DIV_INV_CALL,
  SH_DIV_INV_CALL2,
  SH_DIV_INV_FP
};

extern enum sh_divide_strategy_e sh_div_strategy;

#ifndef SH_DIV_STRATEGY_DEFAULT
#define SH_DIV_STRATEGY_DEFAULT SH_DIV_CALL
#endif

#define OVERRIDE_OPTIONS sh_override_options ()

/* Target machine storage layout.  */

/* Define this if most significant bit is lowest numbered
   in instructions that operate on numbered bit-fields.  */

#define BITS_BIG_ENDIAN  0

/* Define this if most significant byte of a word is the lowest numbered.  */
#define BYTES_BIG_ENDIAN (TARGET_LITTLE_ENDIAN == 0)

/* Define this if most significant word of a multiword number is the lowest
   numbered.  */
#define WORDS_BIG_ENDIAN (TARGET_LITTLE_ENDIAN == 0)

/* Define this to set the endianness to use in libgcc2.c, which can
   not depend on target_flags.  */
#if defined(__LITTLE_ENDIAN__)
#define LIBGCC2_WORDS_BIG_ENDIAN 0
#else
#define LIBGCC2_WORDS_BIG_ENDIAN 1
#endif

#define MAX_BITS_PER_WORD 64

/* Width in bits of an `int'.  We want just 32-bits, even if words are
   longer.  */
#define INT_TYPE_SIZE 32

/* Width in bits of a `long'.  */
#define LONG_TYPE_SIZE (TARGET_SHMEDIA64 ? 64 : 32)

/* Width in bits of a `long long'.  */
#define LONG_LONG_TYPE_SIZE 64

/* Width in bits of a `long double'.  */
#define LONG_DOUBLE_TYPE_SIZE 64

/* Width of a word, in units (bytes).  */
#define UNITS_PER_WORD	(TARGET_SHMEDIA ? 8 : 4)
#define MIN_UNITS_PER_WORD 4

/* Scaling factor for Dwarf data offsets for CFI information.
   The dwarf2out.c default would use -UNITS_PER_WORD, which is -8 for
   SHmedia; however, since we do partial register saves for the registers
   visible to SHcompact, and for target registers for SHMEDIA32, we have
   to allow saves that are only 4-byte aligned.  */
#define DWARF_CIE_DATA_ALIGNMENT -4

/* Width in bits of a pointer.
   See also the macro `Pmode' defined below.  */
#define POINTER_SIZE  (TARGET_SHMEDIA64 ? 64 : 32)

/* Allocation boundary (in *bits*) for storing arguments in argument list.  */
#define PARM_BOUNDARY  	(TARGET_SH5 ? 64 : 32)

/* Boundary (in *bits*) on which stack pointer should be aligned.  */
#define STACK_BOUNDARY  BIGGEST_ALIGNMENT

/* The log (base 2) of the cache line size, in bytes.  Processors prior to
   SH2 have no actual cache, but they fetch code in chunks of 4 bytes.
   The SH2/3 have 16 byte cache lines, and the SH4 has a 32 byte cache line */
#define CACHE_LOG (TARGET_CACHE32 ? 5 : TARGET_SH2 ? 4 : 2)

/* ABI given & required minimum allocation boundary (in *bits*) for the
   code of a function.  */
#define FUNCTION_BOUNDARY (16 << TARGET_SHMEDIA)

/* On SH5, the lowest bit is used to indicate SHmedia functions, so
   the vbit must go into the delta field of
   pointers-to-member-functions.  */
#define TARGET_PTRMEMFUNC_VBIT_LOCATION \
  (TARGET_SH5 ? ptrmemfunc_vbit_in_delta : ptrmemfunc_vbit_in_pfn)

/* Alignment of field after `int : 0' in a structure.  */
#define EMPTY_FIELD_BOUNDARY  32

/* No data type wants to be aligned rounder than this.  */
#define BIGGEST_ALIGNMENT  (TARGET_ALIGN_DOUBLE ? 64 : 32)

/* The best alignment to use in cases where we have a choice.  */
#define FASTEST_ALIGNMENT (TARGET_SH5 ? 64 : 32)

/* Make strings word-aligned so strcpy from constants will be faster.  */
#define CONSTANT_ALIGNMENT(EXP, ALIGN)	\
  ((TREE_CODE (EXP) == STRING_CST	\
    && (ALIGN) < FASTEST_ALIGNMENT)	\
    ? FASTEST_ALIGNMENT : (ALIGN))

/* get_mode_alignment assumes complex values are always held in multiple
   registers, but that is not the case on the SH; CQImode and CHImode are
   held in a single integer register.  SH5 also holds CSImode and SCmode
   values in integer registers.  This is relevant for argument passing on
   SHcompact as we use a stack temp in order to pass CSImode by reference.  */
#define LOCAL_ALIGNMENT(TYPE, ALIGN) \
  ((GET_MODE_CLASS (TYPE_MODE (TYPE)) == MODE_COMPLEX_INT \
    || GET_MODE_CLASS (TYPE_MODE (TYPE)) == MODE_COMPLEX_FLOAT) \
   ? (unsigned) MIN (BIGGEST_ALIGNMENT, GET_MODE_BITSIZE (TYPE_MODE (TYPE))) \
   : (unsigned) ALIGN)

/* Make arrays of chars word-aligned for the same reasons.  */
#define DATA_ALIGNMENT(TYPE, ALIGN)		\
  (TREE_CODE (TYPE) == ARRAY_TYPE		\
   && TYPE_MODE (TREE_TYPE (TYPE)) == QImode	\
   && (ALIGN) < FASTEST_ALIGNMENT ? FASTEST_ALIGNMENT : (ALIGN))

/* Number of bits which any structure or union's size must be a
   multiple of.  Each structure or union's size is rounded up to a
   multiple of this.  */
#define STRUCTURE_SIZE_BOUNDARY (TARGET_PADSTRUCT ? 32 : 8)

/* Set this nonzero if move instructions will actually fail to work
   when given unaligned data.  */
#define STRICT_ALIGNMENT 1

/* If LABEL_AFTER_BARRIER demands an alignment, return its base 2 logarithm.  */
#define LABEL_ALIGN_AFTER_BARRIER(LABEL_AFTER_BARRIER) \
  barrier_align (LABEL_AFTER_BARRIER)

#define LOOP_ALIGN(A_LABEL) \
  ((! optimize || TARGET_HARD_SH4 || TARGET_SMALLCODE) \
   ? 0 : sh_loop_align (A_LABEL))

#define LABEL_ALIGN(A_LABEL) \
(									\
  (PREV_INSN (A_LABEL)							\
   && GET_CODE (PREV_INSN (A_LABEL)) == INSN				\
   && GET_CODE (PATTERN (PREV_INSN (A_LABEL))) == UNSPEC_VOLATILE	\
   && XINT (PATTERN (PREV_INSN (A_LABEL)), 1) == UNSPECV_ALIGN)		\
   /* explicit alignment insn in constant tables.  */			\
  ? INTVAL (XVECEXP (PATTERN (PREV_INSN (A_LABEL)), 0, 0))		\
  : 0)

/* Jump tables must be 32 bit aligned, no matter the size of the element.  */
#define ADDR_VEC_ALIGN(ADDR_VEC) 2

/* The base two logarithm of the known minimum alignment of an insn length.  */
#define INSN_LENGTH_ALIGNMENT(A_INSN)					\
  (GET_CODE (A_INSN) == INSN						\
   ? 1 << TARGET_SHMEDIA						\
   : GET_CODE (A_INSN) == JUMP_INSN || GET_CODE (A_INSN) == CALL_INSN	\
   ? 1 << TARGET_SHMEDIA						\
   : CACHE_LOG)

/* Standard register usage.  */

/* Register allocation for the Renesas calling convention:

        r0		arg return
	r1..r3          scratch
	r4..r7		args in
	r8..r13		call saved
	r14		frame pointer/call saved
	r15		stack pointer
	ap		arg pointer (doesn't really exist, always eliminated)
	pr		subroutine return address
	t               t bit
	mach		multiply/accumulate result, high part
	macl		multiply/accumulate result, low part.
	fpul		fp/int communication register
	rap		return address pointer register
	fr0		fp arg return
	fr1..fr3	scratch floating point registers
	fr4..fr11	fp args in
	fr12..fr15	call saved floating point registers  */

#define MAX_REGISTER_NAME_LENGTH 5
extern char sh_register_names[][MAX_REGISTER_NAME_LENGTH + 1];

#define SH_REGISTER_NAMES_INITIALIZER					\
{				                   			\
  "r0",   "r1",   "r2",   "r3",   "r4",   "r5",   "r6",   "r7", 	\
  "r8",   "r9",   "r10",  "r11",  "r12",  "r13",  "r14",  "r15",	\
  "r16",  "r17",  "r18",  "r19",  "r20",  "r21",  "r22",  "r23",	\
  "r24",  "r25",  "r26",  "r27",  "r28",  "r29",  "r30",  "r31",	\
  "r32",  "r33",  "r34",  "r35",  "r36",  "r37",  "r38",  "r39", 	\
  "r40",  "r41",  "r42",  "r43",  "r44",  "r45",  "r46",  "r47",	\
  "r48",  "r49",  "r50",  "r51",  "r52",  "r53",  "r54",  "r55",	\
  "r56",  "r57",  "r58",  "r59",  "r60",  "r61",  "r62",  "r63",	\
  "fr0",  "fr1",  "fr2",  "fr3",  "fr4",  "fr5",  "fr6",  "fr7", 	\
  "fr8",  "fr9",  "fr10", "fr11", "fr12", "fr13", "fr14", "fr15",	\
  "fr16", "fr17", "fr18", "fr19", "fr20", "fr21", "fr22", "fr23",	\
  "fr24", "fr25", "fr26", "fr27", "fr28", "fr29", "fr30", "fr31",	\
  "fr32", "fr33", "fr34", "fr35", "fr36", "fr37", "fr38", "fr39", 	\
  "fr40", "fr41", "fr42", "fr43", "fr44", "fr45", "fr46", "fr47",	\
  "fr48", "fr49", "fr50", "fr51", "fr52", "fr53", "fr54", "fr55",	\
  "fr56", "fr57", "fr58", "fr59", "fr60", "fr61", "fr62", "fr63",	\
  "tr0",  "tr1",  "tr2",  "tr3",  "tr4",  "tr5",  "tr6",  "tr7", 	\
  "xd0",  "xd2",  "xd4",  "xd6",  "xd8",  "xd10", "xd12", "xd14",	\
  "gbr",  "ap",	  "pr",   "t",    "mach", "macl", "fpul", "fpscr",	\
  "rap",  "sfp"								\
}

#define REGNAMES_ARR_INDEX_1(index) \
  (sh_register_names[index])
#define REGNAMES_ARR_INDEX_2(index) \
  REGNAMES_ARR_INDEX_1 ((index)), REGNAMES_ARR_INDEX_1 ((index)+1)
#define REGNAMES_ARR_INDEX_4(index) \
  REGNAMES_ARR_INDEX_2 ((index)), REGNAMES_ARR_INDEX_2 ((index)+2)
#define REGNAMES_ARR_INDEX_8(index) \
  REGNAMES_ARR_INDEX_4 ((index)), REGNAMES_ARR_INDEX_4 ((index)+4)
#define REGNAMES_ARR_INDEX_16(index) \
  REGNAMES_ARR_INDEX_8 ((index)), REGNAMES_ARR_INDEX_8 ((index)+8)
#define REGNAMES_ARR_INDEX_32(index) \
  REGNAMES_ARR_INDEX_16 ((index)), REGNAMES_ARR_INDEX_16 ((index)+16)
#define REGNAMES_ARR_INDEX_64(index) \
  REGNAMES_ARR_INDEX_32 ((index)), REGNAMES_ARR_INDEX_32 ((index)+32)

#define REGISTER_NAMES \
{ \
  REGNAMES_ARR_INDEX_64 (0), \
  REGNAMES_ARR_INDEX_64 (64), \
  REGNAMES_ARR_INDEX_8 (128), \
  REGNAMES_ARR_INDEX_8 (136), \
  REGNAMES_ARR_INDEX_8 (144), \
  REGNAMES_ARR_INDEX_2 (152) \
}

#define ADDREGNAMES_SIZE 32
#define MAX_ADDITIONAL_REGISTER_NAME_LENGTH 4
extern char sh_additional_register_names[ADDREGNAMES_SIZE] \
  [MAX_ADDITIONAL_REGISTER_NAME_LENGTH + 1];

#define SH_ADDITIONAL_REGISTER_NAMES_INITIALIZER			\
{									\
  "dr0",  "dr2",  "dr4",  "dr6",  "dr8",  "dr10", "dr12", "dr14",	\
  "dr16", "dr18", "dr20", "dr22", "dr24", "dr26", "dr28", "dr30",	\
  "dr32", "dr34", "dr36", "dr38", "dr40", "dr42", "dr44", "dr46",	\
  "dr48", "dr50", "dr52", "dr54", "dr56", "dr58", "dr60", "dr62"	\
}

#define ADDREGNAMES_REGNO(index) \
  ((index < 32) ? (FIRST_FP_REG + (index) * 2) \
   : (-1))

#define ADDREGNAMES_ARR_INDEX_1(index) \
  { (sh_additional_register_names[index]), ADDREGNAMES_REGNO (index) }
#define ADDREGNAMES_ARR_INDEX_2(index) \
  ADDREGNAMES_ARR_INDEX_1 ((index)), ADDREGNAMES_ARR_INDEX_1 ((index)+1)
#define ADDREGNAMES_ARR_INDEX_4(index) \
  ADDREGNAMES_ARR_INDEX_2 ((index)), ADDREGNAMES_ARR_INDEX_2 ((index)+2)
#define ADDREGNAMES_ARR_INDEX_8(index) \
  ADDREGNAMES_ARR_INDEX_4 ((index)), ADDREGNAMES_ARR_INDEX_4 ((index)+4)
#define ADDREGNAMES_ARR_INDEX_16(index) \
  ADDREGNAMES_ARR_INDEX_8 ((index)), ADDREGNAMES_ARR_INDEX_8 ((index)+8)
#define ADDREGNAMES_ARR_INDEX_32(index) \
  ADDREGNAMES_ARR_INDEX_16 ((index)), ADDREGNAMES_ARR_INDEX_16 ((index)+16)

#define ADDITIONAL_REGISTER_NAMES \
{					\
  ADDREGNAMES_ARR_INDEX_32 (0)		\
}

/* Number of actual hardware registers.
   The hardware registers are assigned numbers for the compiler
   from 0 to just below FIRST_PSEUDO_REGISTER.
   All registers that the compiler knows about must be given numbers,
   even those that are not normally considered general registers.  */

/* There are many other relevant definitions in sh.md's md_constants.  */

#define FIRST_GENERAL_REG R0_REG
#define LAST_GENERAL_REG (FIRST_GENERAL_REG + (TARGET_SHMEDIA ? 63 : 15))
#define FIRST_FP_REG DR0_REG
#define LAST_FP_REG  (FIRST_FP_REG + \
		      (TARGET_SHMEDIA_FPU ? 63 : TARGET_SH2E ? 15 : -1))
#define FIRST_XD_REG XD0_REG
#define LAST_XD_REG  (FIRST_XD_REG + ((TARGET_SH4 && TARGET_FMOVD) ? 7 : -1))
#define FIRST_TARGET_REG TR0_REG
#define LAST_TARGET_REG  (FIRST_TARGET_REG + (TARGET_SHMEDIA ? 7 : -1))

#define GENERAL_REGISTER_P(REGNO) \
  IN_RANGE ((REGNO), \
	    (unsigned HOST_WIDE_INT) FIRST_GENERAL_REG, \
	    (unsigned HOST_WIDE_INT) LAST_GENERAL_REG)

#define GENERAL_OR_AP_REGISTER_P(REGNO) \
  (GENERAL_REGISTER_P (REGNO) || ((REGNO) == AP_REG)	\
   || ((REGNO) == FRAME_POINTER_REGNUM))

#define FP_REGISTER_P(REGNO) \
  ((int) (REGNO) >= FIRST_FP_REG && (int) (REGNO) <= LAST_FP_REG)

#define XD_REGISTER_P(REGNO) \
  ((int) (REGNO) >= FIRST_XD_REG && (int) (REGNO) <= LAST_XD_REG)

#define FP_OR_XD_REGISTER_P(REGNO) \
  (FP_REGISTER_P (REGNO) || XD_REGISTER_P (REGNO))

#define FP_ANY_REGISTER_P(REGNO) \
  (FP_REGISTER_P (REGNO) || XD_REGISTER_P (REGNO) || (REGNO) == FPUL_REG)

#define SPECIAL_REGISTER_P(REGNO) \
  ((REGNO) == GBR_REG || (REGNO) == T_REG \
   || (REGNO) == MACH_REG || (REGNO) == MACL_REG)

#define TARGET_REGISTER_P(REGNO) \
  ((int) (REGNO) >= FIRST_TARGET_REG && (int) (REGNO) <= LAST_TARGET_REG)

#define SHMEDIA_REGISTER_P(REGNO) \
  (GENERAL_REGISTER_P (REGNO) || FP_REGISTER_P (REGNO) \
   || TARGET_REGISTER_P (REGNO))

/* This is to be used in CONDITIONAL_REGISTER_USAGE, to mark registers
   that should be fixed.  */
#define VALID_REGISTER_P(REGNO) \
  (SHMEDIA_REGISTER_P (REGNO) || XD_REGISTER_P (REGNO) \
   || (REGNO) == AP_REG || (REGNO) == RAP_REG \
   || (REGNO) == FRAME_POINTER_REGNUM \
   || (TARGET_SH1 && (SPECIAL_REGISTER_P (REGNO) || (REGNO) == PR_REG)) \
   || (TARGET_SH2E && (REGNO) == FPUL_REG))

/* The mode that should be generally used to store a register by
   itself in the stack, or to load it back.  */
#define REGISTER_NATURAL_MODE(REGNO) \
  (FP_REGISTER_P (REGNO) ? SFmode \
   : XD_REGISTER_P (REGNO) ? DFmode \
   : TARGET_SHMEDIA && ! HARD_REGNO_CALL_PART_CLOBBERED ((REGNO), DImode) \
   ? DImode \
   : SImode)

#define FIRST_PSEUDO_REGISTER 154

/* Don't count soft frame pointer.  */
#define DWARF_FRAME_REGISTERS (FIRST_PSEUDO_REGISTER - 1)

/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.

   Mach register is fixed 'cause it's only 10 bits wide for SH1.
   It is 32 bits wide for SH2.  */

#define FIXED_REGISTERS  						\
{				                   			\
/* Regular registers.  */						\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      1,		\
  /* r16 is reserved, r18 is the former pr.  */				\
  1,      0,      0,      0,      0,      0,      0,      0,		\
  /* r24 is reserved for the OS; r25, for the assembler or linker.  */	\
  /* r26 is a global variable data pointer; r27 is for constants.  */	\
  1,      1,      1,      1,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      1,		\
/* FP registers.  */							\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
/* Branch target registers.  */						\
  0,      0,      0,      0,      0,      0,      0,      0,		\
/* XD registers.  */							\
  0,      0,      0,      0,      0,      0,      0,      0,		\
/*"gbr",  "ap",	  "pr",   "t",    "mach", "macl", "fpul", "fpscr", */	\
  1,      1,      1,      1,      1,      1,      0,      1,		\
/*"rap",  "sfp" */							\
  1,	  1,								\
}

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.  */

#define CALL_USED_REGISTERS  						\
{				                   			\
/* Regular registers.  */						\
  1,      1,      1,      1,      1,      1,      1,      1,		\
  /* R8 and R9 are call-clobbered on SH5, but not on earlier SH ABIs.	\
     Only the lower 32bits of R10-R14 are guaranteed to be preserved	\
     across SH5 function calls.  */					\
  0,      0,      0,      0,      0,      0,      0,      1,		\
  1,      1,      1,      1,      1,      1,      1,      1,		\
  1,      1,      1,      1,      0,      0,      0,      0,		\
  0,      0,      0,      0,      1,      1,      1,      1,		\
  1,      1,      1,      1,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      1,      1,      1,      1,		\
/* FP registers.  */							\
  1,      1,      1,      1,      1,      1,      1,      1,		\
  1,      1,      1,      1,      0,      0,      0,      0,		\
  1,      1,      1,      1,      1,      1,      1,      1,		\
  1,      1,      1,      1,      1,      1,      1,      1,		\
  1,      1,      1,      1,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
  0,      0,      0,      0,      0,      0,      0,      0,		\
/* Branch target registers.  */						\
  1,      1,      1,      1,      1,      0,      0,      0,		\
/* XD registers.  */							\
  1,      1,      1,      1,      1,      1,      0,      0,		\
/*"gbr",  "ap",	  "pr",   "t",    "mach", "macl", "fpul", "fpscr", */	\
  1,      1,      1,      1,      1,      1,      1,      1,		\
/*"rap",  "sfp" */							\
  1,	  1,								\
}

/* CONDITIONAL_REGISTER_USAGE might want to make a register call-used, yet
   fixed, like PIC_OFFSET_TABLE_REGNUM.  */
#define CALL_REALLY_USED_REGISTERS CALL_USED_REGISTERS

/* Only the lower 32-bits of R10-R14 are guaranteed to be preserved
   across SHcompact function calls.  We can't tell whether a called
   function is SHmedia or SHcompact, so we assume it may be when
   compiling SHmedia code with the 32-bit ABI, since that's the only
   ABI that can be linked with SHcompact code.  */
#define HARD_REGNO_CALL_PART_CLOBBERED(REGNO,MODE) \
  (TARGET_SHMEDIA32 \
   && GET_MODE_SIZE (MODE) > 4 \
   && (((REGNO) >= FIRST_GENERAL_REG + 10 \
        && (REGNO) <= FIRST_GENERAL_REG + 15) \
       || TARGET_REGISTER_P (REGNO) \
       || (REGNO) == PR_MEDIA_REG))

/* Return number of consecutive hard regs needed starting at reg REGNO
   to hold something of mode MODE.
   This is ordinarily the length in words of a value of mode MODE
   but can be less for certain modes in special long registers.

   On the SH all but the XD regs are UNITS_PER_WORD bits wide.  */

#define HARD_REGNO_NREGS(REGNO, MODE) \
   (XD_REGISTER_P (REGNO) \
    ? ((GET_MODE_SIZE (MODE) + (2*UNITS_PER_WORD - 1)) / (2*UNITS_PER_WORD)) \
    : (TARGET_SHMEDIA && FP_REGISTER_P (REGNO)) \
    ? ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD/2 - 1) / (UNITS_PER_WORD/2)) \
    : ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))

/* Value is 1 if hard register REGNO can hold a value of machine-mode MODE.
   We can allow any mode in any general register.  The special registers
   only allow SImode.  Don't allow any mode in the PR.  */

/* We cannot hold DCmode values in the XD registers because alter_reg
   handles subregs of them incorrectly.  We could work around this by
   spacing the XD registers like the DR registers, but this would require
   additional memory in every compilation to hold larger register vectors.
   We could hold SFmode / SCmode values in XD registers, but that
   would require a tertiary reload when reloading from / to memory,
   and a secondary reload to reload from / to general regs; that
   seems to be a loosing proposition.  */
/* We want to allow TImode FP regs so that when V4SFmode is loaded as TImode,
   it won't be ferried through GP registers first.  */
#define HARD_REGNO_MODE_OK(REGNO, MODE)		\
  (SPECIAL_REGISTER_P (REGNO) ? (MODE) == SImode \
   : (REGNO) == FPUL_REG ? (MODE) == SImode || (MODE) == SFmode	\
   : FP_REGISTER_P (REGNO) && (MODE) == SFmode \
   ? 1 \
   : (MODE) == V2SFmode \
   ? ((FP_REGISTER_P (REGNO) && ((REGNO) - FIRST_FP_REG) % 2 == 0) \
      || GENERAL_REGISTER_P (REGNO)) \
   : (MODE) == V4SFmode \
   ? ((FP_REGISTER_P (REGNO) && ((REGNO) - FIRST_FP_REG) % 4 == 0) \
      || GENERAL_REGISTER_P (REGNO)) \
   : (MODE) == V16SFmode \
   ? (TARGET_SHMEDIA \
      ? (FP_REGISTER_P (REGNO) && ((REGNO) - FIRST_FP_REG) % 16 == 0) \
      : (REGNO) == FIRST_XD_REG) \
   : FP_REGISTER_P (REGNO) \
   ? ((MODE) == SFmode || (MODE) == SImode \
      || ((TARGET_SH2E || TARGET_SHMEDIA) && (MODE) == SCmode) \
      || ((((TARGET_SH4 || TARGET_SH2A_DOUBLE) && (MODE) == DFmode) || (MODE) == DCmode \
	   || (TARGET_SHMEDIA && ((MODE) == DFmode || (MODE) == DImode \
				  || (MODE) == V2SFmode || (MODE) == TImode))) \
	  && (((REGNO) - FIRST_FP_REG) & 1) == 0) \
      || ((TARGET_SH4 || TARGET_SHMEDIA) \
	  && (MODE) == TImode \
	  && (((REGNO) - FIRST_FP_REG) & 3) == 0)) \
   : XD_REGISTER_P (REGNO) \
   ? (MODE) == DFmode \
   : TARGET_REGISTER_P (REGNO) \
   ? ((MODE) == DImode || (MODE) == SImode || (MODE) == PDImode) \
   : (REGNO) == PR_REG ? (MODE) == SImode \
   : (REGNO) == FPSCR_REG ? (MODE) == PSImode \
   : 1)

/* Value is 1 if it is a good idea to tie two pseudo registers
   when one has mode MODE1 and one has mode MODE2.
   If HARD_REGNO_MODE_OK could produce different values for MODE1 and MODE2,
   for any hard reg, then this must be 0 for correct output.
   That's the case for xd registers: we don't hold SFmode values in
   them, so we can't tie an SFmode pseudos with one in another
   floating-point mode.  */

#define MODES_TIEABLE_P(MODE1, MODE2) \
  ((MODE1) == (MODE2) \
   || (TARGET_SHMEDIA \
       && GET_MODE_SIZE (MODE1) == GET_MODE_SIZE (MODE2) \
       && INTEGRAL_MODE_P (MODE1) && INTEGRAL_MODE_P (MODE2)) \
   || (GET_MODE_CLASS (MODE1) == GET_MODE_CLASS (MODE2) \
       && (TARGET_SHMEDIA ? ((GET_MODE_SIZE (MODE1) <= 4) \
			      && (GET_MODE_SIZE (MODE2) <= 4)) \
			  : ((MODE1) != SFmode && (MODE2) != SFmode))))

/* A C expression that is nonzero if hard register NEW_REG can be
   considered for use as a rename register for OLD_REG register */

#define HARD_REGNO_RENAME_OK(OLD_REG, NEW_REG) \
   sh_hard_regno_rename_ok (OLD_REG, NEW_REG)

/* Specify the registers used for certain standard purposes.
   The values of these macros are register numbers.  */

/* Define this if the program counter is overloaded on a register.  */
/* #define PC_REGNUM		15*/

/* Register to use for pushing function arguments.  */
#define STACK_POINTER_REGNUM	SP_REG

/* Base register for access to local variables of the function.  */
#define HARD_FRAME_POINTER_REGNUM	FP_REG

/* Base register for access to local variables of the function.  */
#define FRAME_POINTER_REGNUM	153

/* Fake register that holds the address on the stack of the
   current function's return address.  */
#define RETURN_ADDRESS_POINTER_REGNUM RAP_REG

/* Register to hold the addressing base for position independent
   code access to data items.  */
#define PIC_OFFSET_TABLE_REGNUM	(flag_pic ? PIC_REG : INVALID_REGNUM)

#define GOT_SYMBOL_NAME "*_GLOBAL_OFFSET_TABLE_"

/* Value should be nonzero if functions must have frame pointers.
   Zero means the frame pointer need not be set up (and parms may be accessed
   via the stack pointer) in functions that seem suitable.  */

#define FRAME_POINTER_REQUIRED	0

/* Definitions for register eliminations.

   We have three registers that can be eliminated on the SH.  First, the
   frame pointer register can often be eliminated in favor of the stack
   pointer register.  Secondly, the argument pointer register can always be
   eliminated; it is replaced with either the stack or frame pointer.
   Third, there is the return address pointer, which can also be replaced
   with either the stack or the frame pointer.  */

/* This is an array of structures.  Each structure initializes one pair
   of eliminable registers.  The "from" register number is given first,
   followed by "to".  Eliminations of the same "from" register are listed
   in order of preference.  */

/* If you add any registers here that are not actually hard registers,
   and that have any alternative of elimination that doesn't always
   apply, you need to amend calc_live_regs to exclude it, because
   reload spills all eliminable registers where it sees an
   can_eliminate == 0 entry, thus making them 'live' .
   If you add any hard registers that can be eliminated in different
   ways, you have to patch reload to spill them only when all alternatives
   of elimination fail.  */

#define ELIMINABLE_REGS						\
{{ HARD_FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM},		\
 { FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM},			\
 { FRAME_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM},		\
 { RETURN_ADDRESS_POINTER_REGNUM, STACK_POINTER_REGNUM},	\
 { RETURN_ADDRESS_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM},	\
 { ARG_POINTER_REGNUM, STACK_POINTER_REGNUM},			\
 { ARG_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM},}

/* Given FROM and TO register numbers, say whether this elimination
   is allowed.  */
#define CAN_ELIMINATE(FROM, TO) \
  (!((FROM) == HARD_FRAME_POINTER_REGNUM && FRAME_POINTER_REQUIRED))

/* Define the offset between two registers, one to be eliminated, and the other
   its replacement, at the start of a routine.  */

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) \
  OFFSET = initial_elimination_offset ((FROM), (TO))

/* Base register for access to arguments of the function.  */
#define ARG_POINTER_REGNUM	AP_REG

/* Register in which the static-chain is passed to a function.  */
#define STATIC_CHAIN_REGNUM	(TARGET_SH5 ? 1 : 3)

/* Don't default to pcc-struct-return, because we have already specified
   exactly how to return structures in the TARGET_RETURN_IN_MEMORY
   target hook.  */

#define DEFAULT_PCC_STRUCT_RETURN 0

#define SHMEDIA_REGS_STACK_ADJUST() \
  (TARGET_SHCOMPACT && current_function_has_nonlocal_label \
   ? (8 * (/* r28-r35 */ 8 + /* r44-r59 */ 16 + /* tr5-tr7 */ 3) \
      + (TARGET_FPU_ANY ? 4 * (/* fr36 - fr63 */ 28) : 0)) \
   : 0)


/* Define the classes of registers for register constraints in the
   machine description.  Also define ranges of constants.

   One of the classes must always be named ALL_REGS and include all hard regs.
   If there is more than one class, another class must be named NO_REGS
   and contain no registers.

   The name GENERAL_REGS must be the name of a class (or an alias for
   another name such as ALL_REGS).  This is the class of registers
   that is allowed by "g" or "r" in a register constraint.
   Also, registers outside this class are allocated only when
   instructions express preferences for them.

   The classes must be numbered in nondecreasing order; that is,
   a larger-numbered class must never be contained completely
   in a smaller-numbered class.

   For any two classes, it is very desirable that there be another
   class that represents their union.  */

/* The SH has two sorts of general registers, R0 and the rest.  R0 can
   be used as the destination of some of the arithmetic ops. There are
   also some special purpose registers; the T bit register, the
   Procedure Return Register and the Multiply Accumulate Registers.  */
/* Place GENERAL_REGS after FPUL_REGS so that it will be preferred by
   reg_class_subunion.  We don't want to have an actual union class
   of these, because it would only be used when both classes are calculated
   to give the same cost, but there is only one FPUL register.
   Besides, regclass fails to notice the different REGISTER_MOVE_COSTS
   applying to the actual instruction alternative considered.  E.g., the
   y/r alternative of movsi_ie is considered to have no more cost that
   the r/r alternative, which is patently untrue.  */

enum reg_class
{
  NO_REGS,
  R0_REGS,
  PR_REGS,
  T_REGS,
  MAC_REGS,
  FPUL_REGS,
  SIBCALL_REGS,
  GENERAL_REGS,
  FP0_REGS,
  FP_REGS,
  DF_HI_REGS,
  DF_REGS,
  FPSCR_REGS,
  GENERAL_FP_REGS,
  GENERAL_DF_REGS,
  TARGET_REGS,
  ALL_REGS,
  LIM_REG_CLASSES
};

#define N_REG_CLASSES  (int) LIM_REG_CLASSES

/* Give names of register classes as strings for dump file.  */
#define REG_CLASS_NAMES	\
{			\
  "NO_REGS",		\
  "R0_REGS",		\
  "PR_REGS",		\
  "T_REGS",		\
  "MAC_REGS",		\
  "FPUL_REGS",		\
  "SIBCALL_REGS",	\
  "GENERAL_REGS",	\
  "FP0_REGS",		\
  "FP_REGS",		\
  "DF_HI_REGS",		\
  "DF_REGS",		\
  "FPSCR_REGS",		\
  "GENERAL_FP_REGS",	\
  "GENERAL_DF_REGS",	\
  "TARGET_REGS",	\
  "ALL_REGS",		\
}

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES.  */

#define REG_CLASS_CONTENTS						\
{									\
/* NO_REGS:  */								\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	\
/* R0_REGS:  */								\
  { 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	\
/* PR_REGS:  */								\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00040000 },	\
/* T_REGS:  */								\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00080000 },	\
/* MAC_REGS:  */							\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00300000 },	\
/* FPUL_REGS:  */							\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00400000 },	\
/* SIBCALL_REGS: Initialized in CONDITIONAL_REGISTER_USAGE.  */	\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	\
/* GENERAL_REGS:  */							\
  { 0xffffffff, 0xffffffff, 0x00000000, 0x00000000, 0x03020000 },	\
/* FP0_REGS:  */							\
  { 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000 },	\
/* FP_REGS:  */								\
  { 0x00000000, 0x00000000, 0xffffffff, 0xffffffff, 0x00000000 },	\
/* DF_HI_REGS:  Initialized in CONDITIONAL_REGISTER_USAGE.  */		\
  { 0x00000000, 0x00000000, 0xffffffff, 0xffffffff, 0x0000ff00 },	\
/* DF_REGS:  */								\
  { 0x00000000, 0x00000000, 0xffffffff, 0xffffffff, 0x0000ff00 },	\
/* FPSCR_REGS:  */							\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00800000 },	\
/* GENERAL_FP_REGS:  */							\
  { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0x03020000 },	\
/* GENERAL_DF_REGS:  */							\
  { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0x0302ff00 },	\
/* TARGET_REGS:  */							\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x000000ff },	\
/* ALL_REGS:  */							\
  { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0x03ffffff },	\
}

/* The same information, inverted:
   Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */

extern enum reg_class regno_reg_class[FIRST_PSEUDO_REGISTER];
#define REGNO_REG_CLASS(REGNO) regno_reg_class[(REGNO)]

/* When defined, the compiler allows registers explicitly used in the
   rtl to be used as spill registers but prevents the compiler from
   extending the lifetime of these registers.  */

#define SMALL_REGISTER_CLASSES (! TARGET_SHMEDIA)

/* The order in which register should be allocated.  */
/* Sometimes FP0_REGS becomes the preferred class of a floating point pseudo,
   and GENERAL_FP_REGS the alternate class.  Since FP0 is likely to be
   spilled or used otherwise, we better have the FP_REGS allocated first.  */
#define REG_ALLOC_ORDER \
  {/* Caller-saved FPRs */ \
    65, 66, 67, 68, 69, 70, 71, 64, \
    72, 73, 74, 75, 80, 81, 82, 83, \
    84, 85, 86, 87, 88, 89, 90, 91, \
    92, 93, 94, 95, 96, 97, 98, 99, \
   /* Callee-saved FPRs */ \
    76, 77, 78, 79,100,101,102,103, \
   104,105,106,107,108,109,110,111, \
   112,113,114,115,116,117,118,119, \
   120,121,122,123,124,125,126,127, \
   136,137,138,139,140,141,142,143, \
   /* FPSCR */ 151, \
   /* Caller-saved GPRs (except 8/9 on SH1-4) */ \
     1,  2,  3,  7,  6,  5,  4,  0, \
     8,  9, 17, 19, 20, 21, 22, 23, \
    36, 37, 38, 39, 40, 41, 42, 43, \
    60, 61, 62, \
   /* SH1-4 callee-saved saved GPRs / SH5 partially-saved GPRs */ \
    10, 11, 12, 13, 14, 18, \
    /* SH5 callee-saved GPRs */ \
    28, 29, 30, 31, 32, 33, 34, 35, \
    44, 45, 46, 47, 48, 49, 50, 51, \
    52, 53, 54, 55, 56, 57, 58, 59, \
   /* FPUL */ 150, \
   /* SH5 branch target registers */ \
   128,129,130,131,132,133,134,135, \
   /* Fixed registers */ \
    15, 16, 24, 25, 26, 27, 63,144, \
   145,146,147,148,149,152,153 }

/* The class value for index registers, and the one for base regs.  */
#define INDEX_REG_CLASS \
  (!ALLOW_INDEXED_ADDRESS ? NO_REGS : TARGET_SHMEDIA ? GENERAL_REGS : R0_REGS)
#define BASE_REG_CLASS	 GENERAL_REGS

/* Get reg_class from a letter such as appears in the machine
   description.  */
extern enum reg_class reg_class_from_letter[];

/* We might use 'Rxx' constraints in the future for exotic reg classes.*/
#define REG_CLASS_FROM_CONSTRAINT(C, STR) \
  (ISLOWER (C) ? reg_class_from_letter[(C)-'a'] : NO_REGS )

/* Overview of uppercase letter constraints:
   A: Addresses (constraint len == 3)
    Ac4: sh4 cache operations
    Ac5: sh5 cache operations
   Bxx: miscellaneous constraints
    Bsc: SCRATCH - for the scratch register in movsi_ie in the
	 fldi0 / fldi0 cases
   C: Constants other than only CONST_INT (constraint len == 3)
    Css: signed 16 bit constant, literal or symbolic
    Csu: unsigned 16 bit constant, literal or symbolic
    Csy: label or symbol
    Cpg: non-explicit constants that can be directly loaded into a general
	 purpose register in PIC code.  like 's' except we don't allow
	 PIC_DIRECT_ADDR_P
   IJKLMNOP: CONT_INT constants
    Ixx: signed xx bit
    J16: 0xffffffff00000000 | 0x00000000ffffffff
    Kxx: unsigned xx bit
    M: 1
    N: 0
    P27: 1 | 2 | 8 | 16
   Q: pc relative load operand
   Rxx: reserved for exotic register classes.
   S: extra memory (storage) constraints (constraint len == 3)
    Sua: unaligned memory operations
   W: vector
   Z: zero in any mode

   unused CONST_INT constraint letters: LO
   unused EXTRA_CONSTRAINT letters: D T U Y */

#define CONSTRAINT_LEN(C,STR) \
  (((C) == 'A' || (C) == 'B' || (C) == 'C' \
    || (C) == 'I' || (C) == 'J' || (C) == 'K' || (C) == 'P' \
    || (C) == 'R' || (C) == 'S') \
   ? 3 : DEFAULT_CONSTRAINT_LEN ((C), (STR)))

/* The letters I, J, K, L and M in a register constraint string
   can be used to stand for particular ranges of immediate operands.
   This macro defines what the ranges are.
   C is the letter, and VALUE is a constant value.
   Return 1 if VALUE is in the range specified by C.
	I08: arithmetic operand -127..128, as used in add, sub, etc
	I16: arithmetic operand -32768..32767, as used in SHmedia movi
	K16: arithmetic operand 0..65535, as used in SHmedia shori
	P27: shift operand 1,2,8 or 16
	K08: logical operand 0..255, as used in and, or, etc.
	M: constant 1
	N: constant 0
	I06: arithmetic operand -32..31, as used in SHmedia beqi, bnei and xori
	I10: arithmetic operand -512..511, as used in SHmedia andi, ori
*/

#define CONST_OK_FOR_I06(VALUE) (((HOST_WIDE_INT)(VALUE)) >= -32 \
				 && ((HOST_WIDE_INT)(VALUE)) <= 31)
#define CONST_OK_FOR_I08(VALUE) (((HOST_WIDE_INT)(VALUE))>= -128 \
				 && ((HOST_WIDE_INT)(VALUE)) <= 127)
#define CONST_OK_FOR_I10(VALUE) (((HOST_WIDE_INT)(VALUE)) >= -512 \
				 && ((HOST_WIDE_INT)(VALUE)) <= 511)
#define CONST_OK_FOR_I16(VALUE) (((HOST_WIDE_INT)(VALUE)) >= -32768 \
				 && ((HOST_WIDE_INT)(VALUE)) <= 32767)
#define CONST_OK_FOR_I20(VALUE) (((HOST_WIDE_INT)(VALUE)) >= -524288 \
				 && ((HOST_WIDE_INT)(VALUE)) <= 524287 \
				 && TARGET_SH2A)
#define CONST_OK_FOR_I(VALUE, STR) \
  ((STR)[1] == '0' && (STR)[2] == '6' ? CONST_OK_FOR_I06 (VALUE) \
   : (STR)[1] == '0' && (STR)[2] == '8' ? CONST_OK_FOR_I08 (VALUE) \
   : (STR)[1] == '1' && (STR)[2] == '0' ? CONST_OK_FOR_I10 (VALUE) \
   : (STR)[1] == '1' && (STR)[2] == '6' ? CONST_OK_FOR_I16 (VALUE) \
   : (STR)[1] == '2' && (STR)[2] == '0' ? CONST_OK_FOR_I20 (VALUE) \
   : 0)

#define CONST_OK_FOR_J16(VALUE) \
  ((HOST_BITS_PER_WIDE_INT >= 64 && (VALUE) == (HOST_WIDE_INT) 0xffffffff) \
   || (HOST_BITS_PER_WIDE_INT >= 64 && (VALUE) == (HOST_WIDE_INT) -1 << 32))
#define CONST_OK_FOR_J(VALUE, STR) \
  ((STR)[1] == '1' && (STR)[2] == '6' ? CONST_OK_FOR_J16 (VALUE) \
   : 0)

#define CONST_OK_FOR_K08(VALUE) (((HOST_WIDE_INT)(VALUE))>= 0 \
				 && ((HOST_WIDE_INT)(VALUE)) <= 255)
#define CONST_OK_FOR_K16(VALUE) (((HOST_WIDE_INT)(VALUE))>= 0 \
				 && ((HOST_WIDE_INT)(VALUE)) <= 65535)
#define CONST_OK_FOR_K(VALUE, STR) \
  ((STR)[1] == '0' && (STR)[2] == '8' ? CONST_OK_FOR_K08 (VALUE) \
   : (STR)[1] == '1' && (STR)[2] == '6' ? CONST_OK_FOR_K16 (VALUE)	\
   : 0)
#define CONST_OK_FOR_P27(VALUE) \
  ((VALUE)==1||(VALUE)==2||(VALUE)==8||(VALUE)==16)
#define CONST_OK_FOR_P(VALUE, STR) \
  ((STR)[1] == '2' && (STR)[2] == '7' ? CONST_OK_FOR_P27 (VALUE) \
   : 0)
#define CONST_OK_FOR_M(VALUE) ((VALUE)==1)
#define CONST_OK_FOR_N(VALUE) ((VALUE)==0)
#define CONST_OK_FOR_CONSTRAINT_P(VALUE, C, STR)	\
     ((C) == 'I' ? CONST_OK_FOR_I ((VALUE), (STR))	\
    : (C) == 'J' ? CONST_OK_FOR_J ((VALUE), (STR))	\
    : (C) == 'K' ? CONST_OK_FOR_K ((VALUE), (STR))	\
    : (C) == 'M' ? CONST_OK_FOR_M (VALUE)		\
    : (C) == 'N' ? CONST_OK_FOR_N (VALUE)		\
    : (C) == 'P' ? CONST_OK_FOR_P ((VALUE), (STR))	\
    : 0)

/* Similar, but for floating constants, and defining letters G and H.
   Here VALUE is the CONST_DOUBLE rtx itself.  */

#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C)		\
((C) == 'G' ? (fp_zero_operand (VALUE) && fldi_ok ())	\
 : (C) == 'H' ? (fp_one_operand (VALUE) && fldi_ok ())	\
 : (C) == 'F')

/* Given an rtx X being reloaded into a reg required to be
   in class CLASS, return the class of reg to actually use.
   In general this is just CLASS; but on some machines
   in some cases it is preferable to use a more restrictive class.  */

#define PREFERRED_RELOAD_CLASS(X, CLASS) \
  ((CLASS) == NO_REGS && TARGET_SHMEDIA \
   && (GET_CODE (X) == CONST_DOUBLE \
       || GET_CODE (X) == SYMBOL_REF \
       || PIC_DIRECT_ADDR_P (X)) \
   ? GENERAL_REGS \
   : (CLASS)) \

#if 0
#define SECONDARY_INOUT_RELOAD_CLASS(CLASS,MODE,X,ELSE) \
  ((((REGCLASS_HAS_FP_REG (CLASS) 					\
      && (GET_CODE (X) == REG						\
      && (GENERAL_OR_AP_REGISTER_P (REGNO (X))				\
	  || (FP_REGISTER_P (REGNO (X)) && (MODE) == SImode		\
	      && TARGET_FMOVD))))					\
     || (REGCLASS_HAS_GENERAL_REG (CLASS) 				\
	 && GET_CODE (X) == REG						\
	 && FP_REGISTER_P (REGNO (X))))					\
    && ! TARGET_SHMEDIA							\
    && ((MODE) == SFmode || (MODE) == SImode))				\
   ? FPUL_REGS								\
   : (((CLASS) == FPUL_REGS						\
       || (REGCLASS_HAS_FP_REG (CLASS)					\
	   && ! TARGET_SHMEDIA && MODE == SImode))			\
      && (GET_CODE (X) == MEM						\
	  || (GET_CODE (X) == REG					\
	      && (REGNO (X) >= FIRST_PSEUDO_REGISTER			\
		  || REGNO (X) == T_REG					\
		  || system_reg_operand (X, VOIDmode)))))		\
   ? GENERAL_REGS							\
   : (((CLASS) == TARGET_REGS						\
       || (TARGET_SHMEDIA && (CLASS) == SIBCALL_REGS))			\
      && !EXTRA_CONSTRAINT_Csy (X)					\
      && (GET_CODE (X) != REG || ! GENERAL_REGISTER_P (REGNO (X))))	\
   ? GENERAL_REGS							\
   : (((CLASS) == MAC_REGS || (CLASS) == PR_REGS)			\
      && GET_CODE (X) == REG && ! GENERAL_REGISTER_P (REGNO (X))	\
      && (CLASS) != REGNO_REG_CLASS (REGNO (X)))			\
   ? GENERAL_REGS							\
   : ((CLASS) != GENERAL_REGS && GET_CODE (X) == REG			\
      && TARGET_REGISTER_P (REGNO (X)))					\
   ? GENERAL_REGS : (ELSE))

#define SECONDARY_OUTPUT_RELOAD_CLASS(CLASS,MODE,X) \
 SECONDARY_INOUT_RELOAD_CLASS(CLASS,MODE,X,NO_REGS)

#define SECONDARY_INPUT_RELOAD_CLASS(CLASS,MODE,X)  \
  ((REGCLASS_HAS_FP_REG (CLASS) 					\
    && ! TARGET_SHMEDIA							\
    && immediate_operand ((X), (MODE))					\
    && ! ((fp_zero_operand (X) || fp_one_operand (X))			\
	  && (MODE) == SFmode && fldi_ok ()))				\
   ? R0_REGS								\
   : ((CLASS) == FPUL_REGS						\
      && ((GET_CODE (X) == REG						\
	   && (REGNO (X) == MACL_REG || REGNO (X) == MACH_REG		\
	       || REGNO (X) == T_REG))					\
	  || GET_CODE (X) == PLUS))					\
   ? GENERAL_REGS							\
   : (CLASS) == FPUL_REGS && immediate_operand ((X), (MODE))		\
   ? (GET_CODE (X) == CONST_INT && CONST_OK_FOR_I08 (INTVAL (X))	\
      ? GENERAL_REGS							\
      : R0_REGS)							\
   : ((CLASS) == FPSCR_REGS						\
      && ((GET_CODE (X) == REG && REGNO (X) >= FIRST_PSEUDO_REGISTER)	\
	  || (GET_CODE (X) == MEM && GET_CODE (XEXP ((X), 0)) == PLUS)))\
   ? GENERAL_REGS							\
   : (REGCLASS_HAS_FP_REG (CLASS) 					\
      && TARGET_SHMEDIA							\
      && immediate_operand ((X), (MODE))				\
      && (X) != CONST0_RTX (GET_MODE (X))				\
      && GET_MODE (X) != V4SFmode)					\
   ? GENERAL_REGS							\
   : (((MODE) == QImode || (MODE) == HImode)				\
      && TARGET_SHMEDIA && inqhi_operand ((X), (MODE)))			\
   ? GENERAL_REGS							\
   : (TARGET_SHMEDIA && (CLASS) == GENERAL_REGS				\
      && (GET_CODE (X) == LABEL_REF || PIC_DIRECT_ADDR_P (X)))		\
   ? TARGET_REGS							\
   : SECONDARY_INOUT_RELOAD_CLASS((CLASS),(MODE),(X), NO_REGS))
#else
#define HAVE_SECONDARY_RELOADS
#endif

/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS.

   If TARGET_SHMEDIA, we need two FP registers per word.
   Otherwise we will need at most one register per word.  */
#define CLASS_MAX_NREGS(CLASS, MODE) \
    (TARGET_SHMEDIA \
     && TEST_HARD_REG_BIT (reg_class_contents[CLASS], FIRST_FP_REG) \
     ? (GET_MODE_SIZE (MODE) + UNITS_PER_WORD/2 - 1) / (UNITS_PER_WORD/2) \
     : (GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* If defined, gives a class of registers that cannot be used as the
   operand of a SUBREG that changes the mode of the object illegally.  */
/* ??? We need to renumber the internal numbers for the frnn registers
   when in little endian in order to allow mode size changes.  */

#define CANNOT_CHANGE_MODE_CLASS(FROM, TO, CLASS) 			    \
  sh_cannot_change_mode_class (FROM, TO, CLASS)

/* Stack layout; function entry, exit and calling.  */

/* Define the number of registers that can hold parameters.
   These macros are used only in other macro definitions below.  */

#define NPARM_REGS(MODE) \
  (TARGET_FPU_ANY && (MODE) == SFmode \
   ? (TARGET_SH5 ? 12 : 8) \
   : (TARGET_SH4 || TARGET_SH2A_DOUBLE) && (GET_MODE_CLASS (MODE) == MODE_FLOAT \
		    || GET_MODE_CLASS (MODE) == MODE_COMPLEX_FLOAT) \
   ? (TARGET_SH5 ? 12 : 8) \
   : (TARGET_SH5 ? 8 : 4))

#define FIRST_PARM_REG (FIRST_GENERAL_REG + (TARGET_SH5 ? 2 : 4))
#define FIRST_RET_REG  (FIRST_GENERAL_REG + (TARGET_SH5 ? 2 : 0))

#define FIRST_FP_PARM_REG (FIRST_FP_REG + (TARGET_SH5 ? 0 : 4))
#define FIRST_FP_RET_REG FIRST_FP_REG

/* Define this if pushing a word on the stack
   makes the stack pointer a smaller address.  */
#define STACK_GROWS_DOWNWARD

/*  Define this macro to nonzero if the addresses of local variable slots
    are at negative offsets from the frame pointer.  */
#define FRAME_GROWS_DOWNWARD 1

/* Offset from the frame pointer to the first local variable slot to
   be allocated.  */
#define STARTING_FRAME_OFFSET  0

/* If we generate an insn to push BYTES bytes,
   this says how many the stack pointer really advances by.  */
/* Don't define PUSH_ROUNDING, since the hardware doesn't do this.
   When PUSH_ROUNDING is not defined, PARM_BOUNDARY will cause gcc to
   do correct alignment.  */
#if 0
#define PUSH_ROUNDING(NPUSHED)  (((NPUSHED) + 3) & ~3)
#endif

/* Offset of first parameter from the argument pointer register value.  */
#define FIRST_PARM_OFFSET(FNDECL)  0

/* Value is the number of byte of arguments automatically
   popped when returning from a subroutine call.
   FUNDECL is the declaration node of the function (as a tree),
   FUNTYPE is the data type of the function (as a tree),
   or for a library call it is an identifier node for the subroutine name.
   SIZE is the number of bytes of arguments passed on the stack.

   On the SH, the caller does not pop any of its arguments that were passed
   on the stack.  */
#define RETURN_POPS_ARGS(FUNDECL,FUNTYPE,SIZE)  0

/* Value is the number of bytes of arguments automatically popped when
   calling a subroutine.
   CUM is the accumulated argument list.

   On SHcompact, the call trampoline pops arguments off the stack.  */
#define CALL_POPS_ARGS(CUM) (TARGET_SHCOMPACT ? (CUM).stack_regs * 8 : 0)

/* Some subroutine macros specific to this machine.  */

#define BASE_RETURN_VALUE_REG(MODE) \
  ((TARGET_FPU_ANY && ((MODE) == SFmode))			\
   ? FIRST_FP_RET_REG					\
   : TARGET_FPU_ANY && (MODE) == SCmode		\
   ? FIRST_FP_RET_REG					\
   : (TARGET_FPU_DOUBLE					\
      && ((MODE) == DFmode || (MODE) == SFmode		\
	  || (MODE) == DCmode || (MODE) == SCmode ))	\
   ? FIRST_FP_RET_REG					\
   : FIRST_RET_REG)

#define BASE_ARG_REG(MODE) \
  ((TARGET_SH2E && ((MODE) == SFmode))			\
   ? FIRST_FP_PARM_REG					\
   : (TARGET_SH4 || TARGET_SH2A_DOUBLE) && (GET_MODE_CLASS (MODE) == MODE_FLOAT	\
		    || GET_MODE_CLASS (MODE) == MODE_COMPLEX_FLOAT)\
   ? FIRST_FP_PARM_REG					\
   : FIRST_PARM_REG)

/* Define how to find the value returned by a function.
   VALTYPE is the data type of the value (as a tree).
   If the precise function being called is known, FUNC is its FUNCTION_DECL;
   otherwise, FUNC is 0.
   For the SH, this is like LIBCALL_VALUE, except that we must change the
   mode like PROMOTE_MODE does.
   ??? PROMOTE_MODE is ignored for non-scalar types.  The set of types
   tested here has to be kept in sync with the one in explow.c:promote_mode.  */

#define FUNCTION_VALUE(VALTYPE, FUNC)					\
  gen_rtx_REG (								\
	   ((GET_MODE_CLASS (TYPE_MODE (VALTYPE)) == MODE_INT		\
	     && GET_MODE_SIZE (TYPE_MODE (VALTYPE)) < 4                 \
	     && (TREE_CODE (VALTYPE) == INTEGER_TYPE			\
		 || TREE_CODE (VALTYPE) == ENUMERAL_TYPE		\
		 || TREE_CODE (VALTYPE) == BOOLEAN_TYPE			\
		 || TREE_CODE (VALTYPE) == REAL_TYPE			\
		 || TREE_CODE (VALTYPE) == OFFSET_TYPE))		\
             && sh_promote_prototypes (VALTYPE)				\
	    ? (TARGET_SHMEDIA64 ? DImode : SImode) : TYPE_MODE (VALTYPE)), \
	   BASE_RETURN_VALUE_REG (TYPE_MODE (VALTYPE)))

/* Define how to find the value returned by a library function
   assuming the value has mode MODE.  */
#define LIBCALL_VALUE(MODE) \
  gen_rtx_REG ((MODE), BASE_RETURN_VALUE_REG (MODE));

/* 1 if N is a possible register number for a function value.  */
#define FUNCTION_VALUE_REGNO_P(REGNO) \
  ((REGNO) == FIRST_RET_REG || (TARGET_SH2E && (REGNO) == FIRST_FP_RET_REG) \
   || (TARGET_SHMEDIA_FPU && (REGNO) == FIRST_FP_RET_REG))

/* 1 if N is a possible register number for function argument passing.  */
/* ??? There are some callers that pass REGNO as int, and others that pass
   it as unsigned.  We get warnings unless we do casts everywhere.  */
#define FUNCTION_ARG_REGNO_P(REGNO) \
  (((unsigned) (REGNO) >= (unsigned) FIRST_PARM_REG			\
    && (unsigned) (REGNO) < (unsigned) (FIRST_PARM_REG + NPARM_REGS (SImode)))\
   || (TARGET_FPU_ANY                                                   \
       && (unsigned) (REGNO) >= (unsigned) FIRST_FP_PARM_REG		\
       && (unsigned) (REGNO) < (unsigned) (FIRST_FP_PARM_REG		\
					   + NPARM_REGS (SFmode))))

/* Define a data type for recording info about an argument list
   during the scan of that argument list.  This data type should
   hold all necessary information about the function itself
   and about the args processed so far, enough to enable macros
   such as FUNCTION_ARG to determine where the next arg should go.

   On SH, this is a single integer, which is a number of words
   of arguments scanned so far (including the invisible argument,
   if any, which holds the structure-value-address).
   Thus NARGREGS or more means all following args should go on the stack.  */

enum sh_arg_class { SH_ARG_INT = 0, SH_ARG_FLOAT = 1 };
struct sh_args {
    int arg_count[2];
    int force_mem;
  /* Nonzero if a prototype is available for the function.  */
    int prototype_p;
  /* The number of an odd floating-point register, that should be used
     for the next argument of type float.  */
    int free_single_fp_reg;
  /* Whether we're processing an outgoing function call.  */
    int outgoing;
  /* The number of general-purpose registers that should have been
     used to pass partial arguments, that are passed totally on the
     stack.  On SHcompact, a call trampoline will pop them off the
     stack before calling the actual function, and, if the called
     function is implemented in SHcompact mode, the incoming arguments
     decoder will push such arguments back onto the stack.  For
     incoming arguments, STACK_REGS also takes into account other
     arguments passed by reference, that the decoder will also push
     onto the stack.  */
    int stack_regs;
  /* The number of general-purpose registers that should have been
     used to pass arguments, if the arguments didn't have to be passed
     by reference.  */
    int byref_regs;
  /* Set as by shcompact_byref if the current argument is to be passed
     by reference.  */
    int byref;

  /* call_cookie is a bitmask used by call expanders, as well as
     function prologue and epilogues, to allow SHcompact to comply
     with the SH5 32-bit ABI, that requires 64-bit registers to be
     used even though only the lower 32-bit half is visible in
     SHcompact mode.  The strategy is to call SHmedia trampolines.

     The alternatives for each of the argument-passing registers are
     (a) leave it unchanged; (b) pop it off the stack; (c) load its
     contents from the address in it; (d) add 8 to it, storing the
     result in the next register, then (c); (e) copy it from some
     floating-point register,

     Regarding copies from floating-point registers, r2 may only be
     copied from dr0.  r3 may be copied from dr0 or dr2.  r4 maybe
     copied from dr0, dr2 or dr4.  r5 maybe copied from dr0, dr2,
     dr4 or dr6.  r6 may be copied from dr0, dr2, dr4, dr6 or dr8.
     r7 through to r9 may be copied from dr0, dr2, dr4, dr8, dr8 or
     dr10.

     The bit mask is structured as follows:

     - 1 bit to tell whether to set up a return trampoline.

     - 3 bits to count the number consecutive registers to pop off the
       stack.

     - 4 bits for each of r9, r8, r7 and r6.

     - 3 bits for each of r5, r4, r3 and r2.

     - 3 bits set to 0 (the most significant ones)

        3           2            1           0
       1098 7654 3210 9876 5432 1098 7654 3210
       FLPF LPFL PFLP FFLP FFLP FFLP FFLP SSST
       2223 3344 4555 6666 7777 8888 9999 SSS-

     - If F is set, the register must be copied from an FP register,
       whose number is encoded in the remaining bits.

     - Else, if L is set, the register must be loaded from the address
       contained in it.  If the P bit is *not* set, the address of the
       following dword should be computed first, and stored in the
       following register.

     - Else, if P is set, the register alone should be popped off the
       stack.

     - After all this processing, the number of registers represented
       in SSS will be popped off the stack.  This is an optimization
       for pushing/popping consecutive registers, typically used for
       varargs and large arguments partially passed in registers.

     - If T is set, a return trampoline will be set up for 64-bit
     return values to be split into 2 32-bit registers.  */
#define CALL_COOKIE_RET_TRAMP_SHIFT 0
#define CALL_COOKIE_RET_TRAMP(VAL) ((VAL) << CALL_COOKIE_RET_TRAMP_SHIFT)
#define CALL_COOKIE_STACKSEQ_SHIFT 1
#define CALL_COOKIE_STACKSEQ(VAL) ((VAL) << CALL_COOKIE_STACKSEQ_SHIFT)
#define CALL_COOKIE_STACKSEQ_GET(COOKIE) \
  (((COOKIE) >> CALL_COOKIE_STACKSEQ_SHIFT) & 7)
#define CALL_COOKIE_INT_REG_SHIFT(REG) \
  (4 * (7 - (REG)) + (((REG) <= 2) ? ((REG) - 2) : 1) + 3)
#define CALL_COOKIE_INT_REG(REG, VAL) \
  ((VAL) << CALL_COOKIE_INT_REG_SHIFT (REG))
#define CALL_COOKIE_INT_REG_GET(COOKIE, REG) \
  (((COOKIE) >> CALL_COOKIE_INT_REG_SHIFT (REG)) & ((REG) < 4 ? 7 : 15))
    long call_cookie;

  /* This is set to nonzero when the call in question must use the Renesas ABI,
     even without the -mrenesas option.  */
    int renesas_abi;
};

#define CUMULATIVE_ARGS  struct sh_args

#define GET_SH_ARG_CLASS(MODE) \
  ((TARGET_FPU_ANY && (MODE) == SFmode) \
   ? SH_ARG_FLOAT \
   /* There's no mention of complex float types in the SH5 ABI, so we
      should presumably handle them as aggregate types.  */ \
   : TARGET_SH5 && GET_MODE_CLASS (MODE) == MODE_COMPLEX_FLOAT \
   ? SH_ARG_INT \
   : TARGET_FPU_DOUBLE && (GET_MODE_CLASS (MODE) == MODE_FLOAT \
			   || GET_MODE_CLASS (MODE) == MODE_COMPLEX_FLOAT) \
   ? SH_ARG_FLOAT : SH_ARG_INT)

#define ROUND_ADVANCE(SIZE) \
  (((SIZE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* Round a register number up to a proper boundary for an arg of mode
   MODE.

   The SH doesn't care about double alignment, so we only
   round doubles to even regs when asked to explicitly.  */

#define ROUND_REG(CUM, MODE) \
   (((TARGET_ALIGN_DOUBLE					\
      || ((TARGET_SH4 || TARGET_SH2A_DOUBLE) && ((MODE) == DFmode || (MODE) == DCmode)	\
	  && (CUM).arg_count[(int) SH_ARG_FLOAT] < NPARM_REGS (MODE)))\
     && GET_MODE_UNIT_SIZE ((MODE)) > UNITS_PER_WORD)		\
    ? ((CUM).arg_count[(int) GET_SH_ARG_CLASS (MODE)]		\
       + ((CUM).arg_count[(int) GET_SH_ARG_CLASS (MODE)] & 1))	\
    : (CUM).arg_count[(int) GET_SH_ARG_CLASS (MODE)])

/* Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.

   On SH, the offset always starts at 0: the first parm reg is always
   the same reg for a given argument class.

   For TARGET_HITACHI, the structure value pointer is passed in memory.  */

#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, FNDECL, N_NAMED_ARGS) \
  sh_init_cumulative_args (& (CUM), (FNTYPE), (LIBNAME), (FNDECL), (N_NAMED_ARGS), VOIDmode)

#define INIT_CUMULATIVE_LIBCALL_ARGS(CUM, MODE, LIBNAME) \
  sh_init_cumulative_args (& (CUM), NULL_TREE, (LIBNAME), NULL_TREE, 0, (MODE))

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)	\
	sh_function_arg_advance (&(CUM), (MODE), (TYPE), (NAMED))
#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED)	\
	sh_function_arg (&(CUM), (MODE), (TYPE), (NAMED))

/* Return boolean indicating arg of mode MODE will be passed in a reg.
   This macro is only used in this file.  */

#define PASS_IN_REG_P(CUM, MODE, TYPE) \
  (((TYPE) == 0 \
    || (! TREE_ADDRESSABLE ((tree)(TYPE)) \
	&& (! (TARGET_HITACHI || (CUM).renesas_abi) \
	    || ! (AGGREGATE_TYPE_P (TYPE) \
		  || (!TARGET_FPU_ANY \
		      && (GET_MODE_CLASS (MODE) == MODE_FLOAT \
			  && GET_MODE_SIZE (MODE) > GET_MODE_SIZE (SFmode))))))) \
   && ! (CUM).force_mem \
   && (TARGET_SH2E \
       ? ((MODE) == BLKmode \
	  ? (((CUM).arg_count[(int) SH_ARG_INT] * UNITS_PER_WORD \
	      + int_size_in_bytes (TYPE)) \
	     <= NPARM_REGS (SImode) * UNITS_PER_WORD) \
	  : ((ROUND_REG((CUM), (MODE)) \
	      + HARD_REGNO_NREGS (BASE_ARG_REG (MODE), (MODE))) \
	     <= NPARM_REGS (MODE))) \
       : ROUND_REG ((CUM), (MODE)) < NPARM_REGS (MODE)))

/* By accident we got stuck with passing SCmode on SH4 little endian
   in two registers that are nominally successive - which is different from
   two single SFmode values, where we take endianness translation into
   account.  That does not work at all if an odd number of registers is
   already in use, so that got fixed, but library functions are still more
   likely to use complex numbers without mixing them with SFmode arguments
   (which in C would have to be structures), so for the sake of ABI
   compatibility the way SCmode values are passed when an even number of
   FP registers is in use remains different from a pair of SFmode values for
   now.
   I.e.:
   foo (double); a: fr5,fr4
   foo (float a, float b); a: fr5 b: fr4
   foo (__complex float a); a.real fr4 a.imag: fr5 - for consistency,
                            this should be the other way round...
   foo (float a, __complex float b); a: fr5 b.real: fr4 b.imag: fr7  */
#define FUNCTION_ARG_SCmode_WART 1

/* If an argument of size 5, 6 or 7 bytes is to be passed in a 64-bit
   register in SHcompact mode, it must be padded in the most
   significant end.  This means that passing it by reference wouldn't
   pad properly on a big-endian machine.  In this particular case, we
   pass this argument on the stack, in a way that the call trampoline
   will load its value into the appropriate register.  */
#define SHCOMPACT_FORCE_ON_STACK(MODE,TYPE) \
  ((MODE) == BLKmode \
   && TARGET_SHCOMPACT \
   && ! TARGET_LITTLE_ENDIAN \
   && int_size_in_bytes (TYPE) > 4 \
   && int_size_in_bytes (TYPE) < 8)

/* Minimum alignment for an argument to be passed by callee-copy
   reference.  We need such arguments to be aligned to 8 byte
   boundaries, because they'll be loaded using quad loads.  */
#define SH_MIN_ALIGN_FOR_CALLEE_COPY (8 * BITS_PER_UNIT)

/* The SH5 ABI requires floating-point arguments to be passed to
   functions without a prototype in both an FP register and a regular
   register or the stack.  When passing the argument in both FP and
   general-purpose registers, list the FP register first.  */
#define SH5_PROTOTYPELESS_FLOAT_ARG(CUM,MODE) \
  (gen_rtx_PARALLEL							\
   ((MODE),								\
    gen_rtvec (2,							\
	       gen_rtx_EXPR_LIST					\
	       (VOIDmode,						\
		((CUM).arg_count[(int) SH_ARG_INT] < NPARM_REGS (SImode) \
		 ? gen_rtx_REG ((MODE), FIRST_FP_PARM_REG		\
				+ (CUM).arg_count[(int) SH_ARG_FLOAT])	\
		 : NULL_RTX),						\
		const0_rtx),						\
	       gen_rtx_EXPR_LIST					\
	       (VOIDmode,						\
		((CUM).arg_count[(int) SH_ARG_INT] < NPARM_REGS (SImode) \
		 ? gen_rtx_REG ((MODE), FIRST_PARM_REG			\
				+ (CUM).arg_count[(int) SH_ARG_INT])	\
		 : gen_rtx_REG ((MODE), FIRST_FP_PARM_REG		\
				+ (CUM).arg_count[(int) SH_ARG_FLOAT])), \
		const0_rtx))))

/* The SH5 ABI requires regular registers or stack slots to be
   reserved for floating-point arguments.  Registers are taken care of
   in FUNCTION_ARG_ADVANCE, but stack slots must be reserved here.
   Unfortunately, there's no way to just reserve a stack slot, so
   we'll end up needlessly storing a copy of the argument in the
   stack.  For incoming arguments, however, the PARALLEL will be
   optimized to the register-only form, and the value in the stack
   slot won't be used at all.  */
#define SH5_PROTOTYPED_FLOAT_ARG(CUM,MODE,REG) \
  ((CUM).arg_count[(int) SH_ARG_INT] < NPARM_REGS (SImode)		\
   ? gen_rtx_REG ((MODE), (REG))					\
   : gen_rtx_PARALLEL ((MODE),						\
		       gen_rtvec (2,					\
				  gen_rtx_EXPR_LIST			\
				  (VOIDmode, NULL_RTX,			\
				   const0_rtx),				\
				  gen_rtx_EXPR_LIST			\
				  (VOIDmode, gen_rtx_REG ((MODE),	\
							  (REG)),	\
				   const0_rtx))))

#define SH5_WOULD_BE_PARTIAL_NREGS(CUM, MODE, TYPE, NAMED) \
  (TARGET_SH5							\
   && ((MODE) == BLKmode || (MODE) == TImode || (MODE) == CDImode \
       || (MODE) == DCmode) \
   && ((CUM).arg_count[(int) SH_ARG_INT]			\
       + (((MODE) == BLKmode ? int_size_in_bytes (TYPE)		\
			     : GET_MODE_SIZE (MODE))		\
	  + 7) / 8) > NPARM_REGS (SImode))

/* Perform any needed actions needed for a function that is receiving a
   variable number of arguments.  */

/* Implement `va_start' for varargs and stdarg.  */
#define EXPAND_BUILTIN_VA_START(valist, nextarg) \
  sh_va_start (valist, nextarg)

/* Call the function profiler with a given profile label.
   We use two .aligns, so as to make sure that both the .long is aligned
   on a 4 byte boundary, and that the .long is a fixed distance (2 bytes)
   from the trapa instruction.  */

#define FUNCTION_PROFILER(STREAM,LABELNO)			\
{								\
  if (TARGET_SHMEDIA)						\
    {								\
      fprintf((STREAM), "\tmovi\t33,r0\n");			\
      fprintf((STREAM), "\ttrapa\tr0\n");			\
      asm_fprintf((STREAM), "\t.long\t%LLP%d\n", (LABELNO));	\
    }								\
  else								\
    {								\
      fprintf((STREAM), "\t.align\t2\n");			\
      fprintf((STREAM), "\ttrapa\t#33\n");			\
      fprintf((STREAM), "\t.align\t2\n");			\
      asm_fprintf((STREAM), "\t.long\t%LLP%d\n", (LABELNO));	\
    }								\
}

/* Define this macro if the code for function profiling should come
   before the function prologue.  Normally, the profiling code comes
   after.  */

#define PROFILE_BEFORE_PROLOGUE

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in
   functions that have frame pointers.
   No definition is equivalent to always zero.  */

#define EXIT_IGNORE_STACK 1

/*
   On the SH, the trampoline looks like
   2 0002 D202     	   	mov.l	l2,r2
   1 0000 D301     		mov.l	l1,r3
   3 0004 422B     		jmp	@r2
   4 0006 0009     		nop
   5 0008 00000000 	l1:  	.long   area
   6 000c 00000000 	l2:	.long   function  */

/* Length in units of the trampoline for entering a nested function.  */
#define TRAMPOLINE_SIZE  (TARGET_SHMEDIA64 ? 40 : TARGET_SH5 ? 24 : 16)

/* Alignment required for a trampoline in bits .  */
#define TRAMPOLINE_ALIGNMENT \
  ((CACHE_LOG < 3 || (TARGET_SMALLCODE && ! TARGET_HARVARD)) ? 32 \
   : TARGET_SHMEDIA ? 256 : 64)

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */

#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT) \
  sh_initialize_trampoline ((TRAMP), (FNADDR), (CXT))

/* On SH5, trampolines are SHmedia code, so add 1 to the address.  */

#define TRAMPOLINE_ADJUST_ADDRESS(TRAMP) do				\
{									\
  if (TARGET_SHMEDIA)							\
    (TRAMP) = expand_simple_binop (Pmode, PLUS, (TRAMP), const1_rtx,	\
				   gen_reg_rtx (Pmode), 0,		\
				   OPTAB_LIB_WIDEN);			\
} while (0)

/* A C expression whose value is RTL representing the value of the return
   address for the frame COUNT steps up from the current frame.
   FRAMEADDR is already the frame pointer of the COUNT frame, so we
   can ignore COUNT.  */

#define RETURN_ADDR_RTX(COUNT, FRAME)	\
  (((COUNT) == 0) ? sh_get_pr_initial_val () : (rtx) 0)

/* A C expression whose value is RTL representing the location of the
   incoming return address at the beginning of any function, before the
   prologue.  This RTL is either a REG, indicating that the return
   value is saved in REG, or a MEM representing a location in
   the stack.  */
#define INCOMING_RETURN_ADDR_RTX \
  gen_rtx_REG (Pmode, TARGET_SHMEDIA ? PR_MEDIA_REG : PR_REG)

/* Addressing modes, and classification of registers for them.  */
#define HAVE_POST_INCREMENT  TARGET_SH1
#define HAVE_PRE_DECREMENT   TARGET_SH1

#define USE_LOAD_POST_INCREMENT(mode)    ((mode == SImode || mode == DImode) \
                                           ? 0 : TARGET_SH1)
#define USE_LOAD_PRE_DECREMENT(mode)     0
#define USE_STORE_POST_INCREMENT(mode)   0
#define USE_STORE_PRE_DECREMENT(mode)    ((mode == SImode || mode == DImode) \
                                           ? 0 : TARGET_SH1)

#define MOVE_BY_PIECES_P(SIZE, ALIGN) \
  (move_by_pieces_ninsns (SIZE, ALIGN, MOVE_MAX_PIECES + 1) \
   < (TARGET_SMALLCODE ? 2 : ((ALIGN >= 32) ? 16 : 2)))

#define STORE_BY_PIECES_P(SIZE, ALIGN) \
  (move_by_pieces_ninsns (SIZE, ALIGN, STORE_MAX_PIECES + 1) \
   < (TARGET_SMALLCODE ? 2 : ((ALIGN >= 32) ? 16 : 2)))

/* Macros to check register numbers against specific register classes.  */

/* These assume that REGNO is a hard or pseudo reg number.
   They give nonzero only if REGNO is a hard reg of the suitable class
   or a pseudo reg currently allocated to a suitable hard reg.
   Since they use reg_renumber, they are safe only once reg_renumber
   has been allocated, which happens in local-alloc.c.  */

#define REGNO_OK_FOR_BASE_P(REGNO) \
  (GENERAL_OR_AP_REGISTER_P (REGNO) \
   || GENERAL_OR_AP_REGISTER_P (reg_renumber[(REGNO)]))
#define REGNO_OK_FOR_INDEX_P(REGNO) \
  (TARGET_SHMEDIA \
   ? (GENERAL_REGISTER_P (REGNO) \
      || GENERAL_REGISTER_P ((unsigned) reg_renumber[(REGNO)])) \
   : (REGNO) == R0_REG || (unsigned) reg_renumber[(REGNO)] == R0_REG)

/* Maximum number of registers that can appear in a valid memory
   address.  */

#define MAX_REGS_PER_ADDRESS 2

/* Recognize any constant value that is a valid address.  */

#define CONSTANT_ADDRESS_P(X)	(GET_CODE (X) == LABEL_REF)

/* Nonzero if the constant value X is a legitimate general operand.  */

#define LEGITIMATE_CONSTANT_P(X) \
  (TARGET_SHMEDIA							\
   ? ((GET_MODE (X) != DFmode						\
       && GET_MODE_CLASS (GET_MODE (X)) != MODE_VECTOR_FLOAT)		\
      || (X) == CONST0_RTX (GET_MODE (X))				\
      || ! TARGET_SHMEDIA_FPU						\
      || TARGET_SHMEDIA64)						\
   : (GET_CODE (X) != CONST_DOUBLE					\
      || GET_MODE (X) == DFmode || GET_MODE (X) == SFmode		\
      || (TARGET_SH2E && (fp_zero_operand (X) || fp_one_operand (X)))))

/* The macros REG_OK_FOR..._P assume that the arg is a REG rtx
   and check its validity for a certain class.
   We have two alternate definitions for each of them.
   The usual definition accepts all pseudo regs; the other rejects
   them unless they have been allocated suitable hard regs.
   The symbol REG_OK_STRICT causes the latter definition to be used.  */

#ifndef REG_OK_STRICT

/* Nonzero if X is a hard reg that can be used as a base reg
   or if it is a pseudo reg.  */
#define REG_OK_FOR_BASE_P(X) \
  (GENERAL_OR_AP_REGISTER_P (REGNO (X)) || REGNO (X) >= FIRST_PSEUDO_REGISTER)

/* Nonzero if X is a hard reg that can be used as an index
   or if it is a pseudo reg.  */
#define REG_OK_FOR_INDEX_P(X) \
  ((TARGET_SHMEDIA ? GENERAL_REGISTER_P (REGNO (X)) \
    : REGNO (X) == R0_REG) || REGNO (X) >= FIRST_PSEUDO_REGISTER)

/* Nonzero if X/OFFSET is a hard reg that can be used as an index
   or if X is a pseudo reg.  */
#define SUBREG_OK_FOR_INDEX_P(X, OFFSET) \
  ((TARGET_SHMEDIA ? GENERAL_REGISTER_P (REGNO (X)) \
    : REGNO (X) == R0_REG && OFFSET == 0) || REGNO (X) >= FIRST_PSEUDO_REGISTER)

#else

/* Nonzero if X is a hard reg that can be used as a base reg.  */
#define REG_OK_FOR_BASE_P(X) \
  REGNO_OK_FOR_BASE_P (REGNO (X))

/* Nonzero if X is a hard reg that can be used as an index.  */
#define REG_OK_FOR_INDEX_P(X) \
  REGNO_OK_FOR_INDEX_P (REGNO (X))

/* Nonzero if X/OFFSET is a hard reg that can be used as an index.  */
#define SUBREG_OK_FOR_INDEX_P(X, OFFSET) \
  (REGNO_OK_FOR_INDEX_P (REGNO (X)) && (OFFSET) == 0)

#endif

/* The 'Q' constraint is a pc relative load operand.  */
#define EXTRA_CONSTRAINT_Q(OP)                          		\
  (GET_CODE (OP) == MEM 						\
   && ((GET_CODE (XEXP ((OP), 0)) == LABEL_REF)				\
       || (GET_CODE (XEXP ((OP), 0)) == CONST                		\
	   && GET_CODE (XEXP (XEXP ((OP), 0), 0)) == PLUS		\
	   && GET_CODE (XEXP (XEXP (XEXP ((OP), 0), 0), 0)) == LABEL_REF \
	   && GET_CODE (XEXP (XEXP (XEXP ((OP), 0), 0), 1)) == CONST_INT)))

/* Extra address constraints.  */
#define EXTRA_CONSTRAINT_A(OP, STR) 0

/* Constraint for selecting FLDI0 or FLDI1 instruction. If the clobber
   operand is not SCRATCH (i.e. REG) then R0 is probably being
   used, hence mova is being used, hence do not select this pattern */
#define EXTRA_CONSTRAINT_Bsc(OP)    (GET_CODE(OP) == SCRATCH)
#define EXTRA_CONSTRAINT_B(OP, STR) \
  ((STR)[1] == 's' && (STR)[2] == 'c' ? EXTRA_CONSTRAINT_Bsc (OP) \
   : 0)

/* The `Css' constraint is a signed 16-bit constant, literal or symbolic.  */
#define EXTRA_CONSTRAINT_Css(OP) \
  (GET_CODE (OP) == CONST \
   && GET_CODE (XEXP ((OP), 0)) == SIGN_EXTEND \
   && (GET_MODE (XEXP ((OP), 0)) == DImode \
       || GET_MODE (XEXP ((OP), 0)) == SImode) \
   && GET_CODE (XEXP (XEXP ((OP), 0), 0)) == TRUNCATE \
   && GET_MODE (XEXP (XEXP ((OP), 0), 0)) == HImode \
   && (MOVI_SHORI_BASE_OPERAND_P (XEXP (XEXP (XEXP ((OP), 0), 0), 0)) \
       || (GET_CODE (XEXP (XEXP (XEXP ((OP), 0), 0), 0)) == ASHIFTRT \
	   && (MOVI_SHORI_BASE_OPERAND_P \
	       (XEXP (XEXP (XEXP (XEXP ((OP), 0), 0), 0), 0))) \
	   && GET_CODE (XEXP (XEXP (XEXP (XEXP ((OP), 0), 0), 0), \
			      1)) == CONST_INT)))

/* The `Csu' constraint is an unsigned 16-bit constant, literal or symbolic.  */
#define EXTRA_CONSTRAINT_Csu(OP) \
  (GET_CODE (OP) == CONST \
   && GET_CODE (XEXP ((OP), 0)) == ZERO_EXTEND \
   && (GET_MODE (XEXP ((OP), 0)) == DImode \
       || GET_MODE (XEXP ((OP), 0)) == SImode) \
   && GET_CODE (XEXP (XEXP ((OP), 0), 0)) == TRUNCATE \
   && GET_MODE (XEXP (XEXP ((OP), 0), 0)) == HImode \
   && (MOVI_SHORI_BASE_OPERAND_P (XEXP (XEXP (XEXP ((OP), 0), 0), 0)) \
       || (GET_CODE (XEXP (XEXP (XEXP ((OP), 0), 0), 0)) == ASHIFTRT \
	   && (MOVI_SHORI_BASE_OPERAND_P \
	       (XEXP (XEXP (XEXP (XEXP ((OP), 0), 0), 0), 0))) \
	   && GET_CODE (XEXP (XEXP (XEXP (XEXP ((OP), 0), 0), 0), \
			      1)) == CONST_INT)))

/* Check whether OP is a datalabel unspec.  */
#define DATALABEL_REF_NO_CONST_P(OP) \
  (GET_CODE (OP) == UNSPEC \
   && XINT ((OP), 1) == UNSPEC_DATALABEL \
   && XVECLEN ((OP), 0) == 1 \
   && GET_CODE (XVECEXP ((OP), 0, 0)) == LABEL_REF)

#define GOT_ENTRY_P(OP) \
  (GET_CODE (OP) == CONST && GET_CODE (XEXP ((OP), 0)) == UNSPEC \
   && XINT (XEXP ((OP), 0), 1) == UNSPEC_GOT)

#define GOTPLT_ENTRY_P(OP) \
  (GET_CODE (OP) == CONST && GET_CODE (XEXP ((OP), 0)) == UNSPEC \
   && XINT (XEXP ((OP), 0), 1) == UNSPEC_GOTPLT)

#define UNSPEC_GOTOFF_P(OP) \
  (GET_CODE (OP) == UNSPEC && XINT ((OP), 1) == UNSPEC_GOTOFF)

#define GOTOFF_P(OP) \
  (GET_CODE (OP) == CONST \
   && (UNSPEC_GOTOFF_P (XEXP ((OP), 0)) \
       || (GET_CODE (XEXP ((OP), 0)) == PLUS \
           && UNSPEC_GOTOFF_P (XEXP (XEXP ((OP), 0), 0)) \
	   && GET_CODE (XEXP (XEXP ((OP), 0), 1)) == CONST_INT)))

#define PIC_ADDR_P(OP) \
  (GET_CODE (OP) == CONST && GET_CODE (XEXP ((OP), 0)) == UNSPEC \
   && XINT (XEXP ((OP), 0), 1) == UNSPEC_PIC)

#define PIC_OFFSET_P(OP) \
  (PIC_ADDR_P (OP) \
   && GET_CODE (XVECEXP (XEXP ((OP), 0), 0, 0)) == MINUS \
   && reg_mentioned_p (pc_rtx, XEXP (XVECEXP (XEXP ((OP), 0), 0, 0), 1)))

#define PIC_DIRECT_ADDR_P(OP) \
  (PIC_ADDR_P (OP) && GET_CODE (XVECEXP (XEXP ((OP), 0), 0, 0)) != MINUS)

#define NON_PIC_REFERENCE_P(OP) \
  (GET_CODE (OP) == LABEL_REF || GET_CODE (OP) == SYMBOL_REF \
   || (GET_CODE (OP) == CONST \
       && (GET_CODE (XEXP ((OP), 0)) == LABEL_REF \
	   || GET_CODE (XEXP ((OP), 0)) == SYMBOL_REF \
	   || DATALABEL_REF_NO_CONST_P (XEXP ((OP), 0)))) \
   || (GET_CODE (OP) == CONST && GET_CODE (XEXP ((OP), 0)) == PLUS \
       && (GET_CODE (XEXP (XEXP ((OP), 0), 0)) == SYMBOL_REF \
	   || GET_CODE (XEXP (XEXP ((OP), 0), 0)) == LABEL_REF \
	   || DATALABEL_REF_NO_CONST_P (XEXP (XEXP ((OP), 0), 0))) \
       && GET_CODE (XEXP (XEXP ((OP), 0), 1)) == CONST_INT))

#define PIC_REFERENCE_P(OP) \
  (GOT_ENTRY_P (OP) || GOTPLT_ENTRY_P (OP) \
   || GOTOFF_P (OP) || PIC_ADDR_P (OP))

#define MOVI_SHORI_BASE_OPERAND_P(OP) \
  (flag_pic \
   ? (GOT_ENTRY_P (OP) || GOTPLT_ENTRY_P (OP)  || GOTOFF_P (OP) \
      || PIC_OFFSET_P (OP)) \
   : NON_PIC_REFERENCE_P (OP))

/* The `Csy' constraint is a label or a symbol.  */
#define EXTRA_CONSTRAINT_Csy(OP) \
  (NON_PIC_REFERENCE_P (OP) || PIC_DIRECT_ADDR_P (OP))

/* A zero in any shape or form.  */
#define EXTRA_CONSTRAINT_Z(OP) \
  ((OP) == CONST0_RTX (GET_MODE (OP)))

/* Any vector constant we can handle.  */
#define EXTRA_CONSTRAINT_W(OP) \
  (GET_CODE (OP) == CONST_VECTOR \
   && (sh_rep_vec ((OP), VOIDmode) \
       || (HOST_BITS_PER_WIDE_INT >= 64 \
	   ? sh_const_vec ((OP), VOIDmode) \
	   : sh_1el_vec ((OP), VOIDmode))))

/* A non-explicit constant that can be loaded directly into a general purpose
   register.  This is like 's' except we don't allow PIC_DIRECT_ADDR_P.  */
#define EXTRA_CONSTRAINT_Cpg(OP) \
  (CONSTANT_P (OP) \
   && GET_CODE (OP) != CONST_INT \
   && GET_CODE (OP) != CONST_DOUBLE \
   && (!flag_pic \
       || (LEGITIMATE_PIC_OPERAND_P (OP) \
        && (! PIC_ADDR_P (OP) || PIC_OFFSET_P (OP)) \
        && GET_CODE (OP) != LABEL_REF)))
#define EXTRA_CONSTRAINT_C(OP, STR) \
  ((STR)[1] == 's' && (STR)[2] == 's' ? EXTRA_CONSTRAINT_Css (OP) \
   : (STR)[1] == 's' && (STR)[2] == 'u' ? EXTRA_CONSTRAINT_Csu (OP) \
   : (STR)[1] == 's' && (STR)[2] == 'y' ? EXTRA_CONSTRAINT_Csy (OP) \
   : (STR)[1] == 'p' && (STR)[2] == 'g' ? EXTRA_CONSTRAINT_Cpg (OP) \
   : 0)

#define EXTRA_MEMORY_CONSTRAINT(C,STR) ((C) == 'S')
#define EXTRA_CONSTRAINT_Sr0(OP) \
  (memory_operand((OP), GET_MODE (OP)) \
   && ! refers_to_regno_p (R0_REG, R0_REG + 1, OP, (rtx *)0))
#define EXTRA_CONSTRAINT_Sua(OP) \
  (memory_operand((OP), GET_MODE (OP)) \
   && GET_CODE (XEXP (OP, 0)) != PLUS)
#define EXTRA_CONSTRAINT_S(OP, STR) \
  ((STR)[1] == 'r' && (STR)[2] == '0' ? EXTRA_CONSTRAINT_Sr0 (OP) \
   : (STR)[1] == 'u' && (STR)[2] == 'a' ? EXTRA_CONSTRAINT_Sua (OP) \
   : 0)

#define EXTRA_CONSTRAINT_STR(OP, C, STR)		\
  ((C) == 'Q' ? EXTRA_CONSTRAINT_Q (OP)	\
   : (C) == 'A' ? EXTRA_CONSTRAINT_A ((OP), (STR)) \
   : (C) == 'B' ? EXTRA_CONSTRAINT_B ((OP), (STR)) \
   : (C) == 'C' ? EXTRA_CONSTRAINT_C ((OP), (STR)) \
   : (C) == 'S' ? EXTRA_CONSTRAINT_S ((OP), (STR)) \
   : (C) == 'W' ? EXTRA_CONSTRAINT_W (OP) \
   : (C) == 'Z' ? EXTRA_CONSTRAINT_Z (OP) \
   : 0)

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression
   that is a valid memory address for an instruction.
   The MODE argument is the machine mode for the MEM expression
   that wants to use this address.  */

#define MODE_DISP_OK_4(X,MODE) \
(GET_MODE_SIZE (MODE) == 4 && (unsigned) INTVAL (X) < 64	\
 && ! (INTVAL (X) & 3) && ! (TARGET_SH2E && (MODE) == SFmode))

#define MODE_DISP_OK_8(X,MODE) \
((GET_MODE_SIZE(MODE)==8) && ((unsigned)INTVAL(X)<60)	\
 && ! (INTVAL(X) & 3) && ! (TARGET_SH4 && (MODE) == DFmode))

#undef MODE_DISP_OK_4
#define MODE_DISP_OK_4(X,MODE) \
((GET_MODE_SIZE (MODE) == 4 && (unsigned) INTVAL (X) < 64	\
  && ! (INTVAL (X) & 3) && ! (TARGET_SH2E && (MODE) == SFmode)) \
  || ((GET_MODE_SIZE(MODE)==4) && ((unsigned)INTVAL(X)<16383)	\
  && ! (INTVAL(X) & 3) && TARGET_SH2A))

#undef MODE_DISP_OK_8
#define MODE_DISP_OK_8(X,MODE) \
(((GET_MODE_SIZE(MODE)==8) && ((unsigned)INTVAL(X)<60)	\
  && ! (INTVAL(X) & 3) && ! ((TARGET_SH4 || TARGET_SH2A) && (MODE) == DFmode)) \
 || ((GET_MODE_SIZE(MODE)==8) && ((unsigned)INTVAL(X)<8192)	\
  && ! (INTVAL(X) & (TARGET_SH2A_DOUBLE ? 7 : 3)) && (TARGET_SH2A && (MODE) == DFmode)))

#define BASE_REGISTER_RTX_P(X)				\
  ((GET_CODE (X) == REG && REG_OK_FOR_BASE_P (X))	\
   || (GET_CODE (X) == SUBREG				\
       && TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (GET_MODE ((X))), \
				 GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (X)))) \
       && GET_CODE (SUBREG_REG (X)) == REG		\
       && REG_OK_FOR_BASE_P (SUBREG_REG (X))))

/* Since this must be r0, which is a single register class, we must check
   SUBREGs more carefully, to be sure that we don't accept one that extends
   outside the class.  */
#define INDEX_REGISTER_RTX_P(X)				\
  ((GET_CODE (X) == REG && REG_OK_FOR_INDEX_P (X))	\
   || (GET_CODE (X) == SUBREG				\
       && TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (GET_MODE ((X))), \
				 GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (X)))) \
       && GET_CODE (SUBREG_REG (X)) == REG		\
       && SUBREG_OK_FOR_INDEX_P (SUBREG_REG (X), SUBREG_BYTE (X))))

/* Jump to LABEL if X is a valid address RTX.  This must also take
   REG_OK_STRICT into account when deciding about valid registers, but it uses
   the above macros so we are in luck.

   Allow  REG
	  REG+disp
	  REG+r0
	  REG++
	  --REG  */

/* ??? The SH2e does not have the REG+disp addressing mode when loading values
   into the FRx registers.  We implement this by setting the maximum offset
   to zero when the value is SFmode.  This also restricts loading of SFmode
   values into the integer registers, but that can't be helped.  */

/* The SH allows a displacement in a QI or HI amode, but only when the
   other operand is R0. GCC doesn't handle this very well, so we forgo
   all of that.

   A legitimate index for a QI or HI is 0, SI can be any number 0..63,
   DI can be any number 0..60.  */

#define GO_IF_LEGITIMATE_INDEX(MODE, OP, LABEL)  			\
  do {									\
    if (GET_CODE (OP) == CONST_INT) 					\
      {									\
	if (TARGET_SHMEDIA)						\
	  {								\
	    int MODE_SIZE;						\
	    /* Check if this the address of an unaligned load / store.  */\
	    if ((MODE) == VOIDmode)					\
	     {								\
	      if (CONST_OK_FOR_I06 (INTVAL (OP)))			\
		goto LABEL;						\
	      break;							\
	     }								\
	    MODE_SIZE = GET_MODE_SIZE (MODE);				\
	    if (! (INTVAL (OP) & (MODE_SIZE - 1))			\
		&& INTVAL (OP) >= -512 * MODE_SIZE			\
		&& INTVAL (OP) < 512 * MODE_SIZE)			\
	      goto LABEL;						\
	    else							\
	      break;							\
	  }								\
	if (MODE_DISP_OK_4 ((OP), (MODE)))  goto LABEL;		      	\
	if (MODE_DISP_OK_8 ((OP), (MODE)))  goto LABEL;		      	\
      }									\
  } while(0)

#define ALLOW_INDEXED_ADDRESS \
  ((!TARGET_SHMEDIA32 && !TARGET_SHCOMPACT) || TARGET_ALLOW_INDEXED_ADDRESS)

#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, LABEL)			\
{									\
  if (BASE_REGISTER_RTX_P (X))						\
    goto LABEL;								\
  else if ((GET_CODE (X) == POST_INC || GET_CODE (X) == PRE_DEC)	\
	   && ! TARGET_SHMEDIA						\
	   && BASE_REGISTER_RTX_P (XEXP ((X), 0)))			\
    goto LABEL;								\
  else if (GET_CODE (X) == PLUS						\
	   && ((MODE) != PSImode || reload_completed))			\
    {									\
      rtx xop0 = XEXP ((X), 0);						\
      rtx xop1 = XEXP ((X), 1);						\
      if (GET_MODE_SIZE (MODE) <= 8 && BASE_REGISTER_RTX_P (xop0))	\
	GO_IF_LEGITIMATE_INDEX ((MODE), xop1, LABEL);			\
      if ((ALLOW_INDEXED_ADDRESS || GET_MODE (X) == DImode		\
	   || ((xop0 == stack_pointer_rtx				\
		|| xop0 == hard_frame_pointer_rtx)			\
	       && REG_P (xop1) && REGNO (xop1) == R0_REG)		\
	   || ((xop1 == stack_pointer_rtx				\
		|| xop1 == hard_frame_pointer_rtx)			\
	       && REG_P (xop0) && REGNO (xop0) == R0_REG))		\
	  && ((!TARGET_SHMEDIA && GET_MODE_SIZE (MODE) <= 4)		\
	      || (TARGET_SHMEDIA && GET_MODE_SIZE (MODE) <= 8)		\
	      || ((TARGET_SH4 || TARGET_SH2A_DOUBLE)			\
		  && TARGET_FMOVD && MODE == DFmode)))			\
	{								\
	  if (BASE_REGISTER_RTX_P (xop1) && INDEX_REGISTER_RTX_P (xop0))\
	    goto LABEL;							\
	  if (INDEX_REGISTER_RTX_P (xop1) && BASE_REGISTER_RTX_P (xop0))\
	    goto LABEL;							\
	}								\
    }									\
}

/* Try machine-dependent ways of modifying an illegitimate address
   to be legitimate.  If we find one, return the new, valid address.
   This macro is used in only one place: `memory_address' in explow.c.

   OLDX is the address as it was before break_out_memory_refs was called.
   In some cases it is useful to look at this to decide what needs to be done.

   MODE and WIN are passed so that this macro can use
   GO_IF_LEGITIMATE_ADDRESS.

   It is always safe for this macro to do nothing.  It exists to recognize
   opportunities to optimize the output.

   For the SH, if X is almost suitable for indexing, but the offset is
   out of range, convert it into a normal form so that cse has a chance
   of reducing the number of address registers used.  */

#define LEGITIMIZE_ADDRESS(X,OLDX,MODE,WIN)			\
{								\
  if (flag_pic)							\
    (X) = legitimize_pic_address (OLDX, MODE, NULL_RTX);	\
  if (GET_CODE (X) == PLUS					\
      && (GET_MODE_SIZE (MODE) == 4				\
	  || GET_MODE_SIZE (MODE) == 8)				\
      && GET_CODE (XEXP ((X), 1)) == CONST_INT			\
      && BASE_REGISTER_RTX_P (XEXP ((X), 0))			\
      && ! TARGET_SHMEDIA					\
      && ! ((TARGET_SH4 || TARGET_SH2A_DOUBLE) && (MODE) == DFmode)			\
      && ! (TARGET_SH2E && (MODE) == SFmode))			\
    {								\
      rtx index_rtx = XEXP ((X), 1);				\
      HOST_WIDE_INT offset = INTVAL (index_rtx), offset_base;	\
      rtx sum;							\
								\
      GO_IF_LEGITIMATE_INDEX ((MODE), index_rtx, WIN);		\
      /* On rare occasions, we might get an unaligned pointer	\
	 that is indexed in a way to give an aligned address.	\
	 Therefore, keep the lower two bits in offset_base.  */ \
      /* Instead of offset_base 128..131 use 124..127, so that	\
	 simple add suffices.  */				\
      if (offset > 127)						\
	{							\
	  offset_base = ((offset + 4) & ~60) - 4;		\
	}							\
      else							\
	offset_base = offset & ~60;				\
      /* Sometimes the normal form does not suit DImode.  We	\
	 could avoid that by using smaller ranges, but that	\
	 would give less optimized code when SImode is		\
	 prevalent.  */						\
      if (GET_MODE_SIZE (MODE) + offset - offset_base <= 64)	\
	{							\
	  sum = expand_binop (Pmode, add_optab, XEXP ((X), 0),	\
			      GEN_INT (offset_base), NULL_RTX, 0, \
			      OPTAB_LIB_WIDEN);			\
                                                                \
	  (X) = gen_rtx_PLUS (Pmode, sum, GEN_INT (offset - offset_base)); \
	  goto WIN;						\
	}							\
    }								\
}

/* A C compound statement that attempts to replace X, which is an address
   that needs reloading, with a valid memory address for an operand of
   mode MODE.  WIN is a C statement label elsewhere in the code.

   Like for LEGITIMIZE_ADDRESS, for the SH we try to get a normal form
   of the address.  That will allow inheritance of the address reloads.  */

#define LEGITIMIZE_RELOAD_ADDRESS(X,MODE,OPNUM,TYPE,IND_LEVELS,WIN)	\
{									\
  if (GET_CODE (X) == PLUS						\
      && (GET_MODE_SIZE (MODE) == 4 || GET_MODE_SIZE (MODE) == 8)	\
      && GET_CODE (XEXP (X, 1)) == CONST_INT				\
      && BASE_REGISTER_RTX_P (XEXP (X, 0))				\
      && ! TARGET_SHMEDIA						\
      && ! (TARGET_SH4 && (MODE) == DFmode)				\
      && ! ((MODE) == PSImode && (TYPE) == RELOAD_FOR_INPUT_ADDRESS)	\
      && (ALLOW_INDEXED_ADDRESS						\
	  || XEXP ((X), 0) == stack_pointer_rtx				\
	  || XEXP ((X), 0) == hard_frame_pointer_rtx))			\
    {									\
      rtx index_rtx = XEXP (X, 1);					\
      HOST_WIDE_INT offset = INTVAL (index_rtx), offset_base;		\
      rtx sum;								\
									\
      if (TARGET_SH2A && (MODE) == DFmode && (offset & 0x7))		\
	{								\
	  push_reload (X, NULL_RTX, &X, NULL,				\
		       BASE_REG_CLASS, Pmode, VOIDmode, 0, 0, (OPNUM),	\
		       (TYPE));						\
	  goto WIN;							\
	}								\
      if (TARGET_SH2E && MODE == SFmode)				\
	{								\
	  X = copy_rtx (X);						\
	  push_reload (index_rtx, NULL_RTX, &XEXP (X, 1), NULL,		\
		       R0_REGS, Pmode, VOIDmode, 0, 0, (OPNUM),		\
		       (TYPE));						\
	  goto WIN;							\
	}								\
      /* Instead of offset_base 128..131 use 124..127, so that		\
	 simple add suffices.  */					\
      if (offset > 127)							\
	{								\
	  offset_base = ((offset + 4) & ~60) - 4;			\
	}								\
      else								\
	offset_base = offset & ~60;					\
      /* Sometimes the normal form does not suit DImode.  We		\
	 could avoid that by using smaller ranges, but that		\
	 would give less optimized code when SImode is			\
	 prevalent.  */							\
      if (GET_MODE_SIZE (MODE) + offset - offset_base <= 64)		\
	{								\
	  sum = gen_rtx_PLUS (Pmode, XEXP (X, 0),			\
			 GEN_INT (offset_base));			\
	  X = gen_rtx_PLUS (Pmode, sum, GEN_INT (offset - offset_base));\
	  push_reload (sum, NULL_RTX, &XEXP (X, 0), NULL,		\
		       BASE_REG_CLASS, Pmode, VOIDmode, 0, 0, (OPNUM),	\
		       (TYPE));						\
	  goto WIN;							\
	}								\
    }									\
  /* We must re-recognize what we created before.  */			\
  else if (GET_CODE (X) == PLUS						\
	   && (GET_MODE_SIZE (MODE) == 4 || GET_MODE_SIZE (MODE) == 8)	\
	   && GET_CODE (XEXP (X, 0)) == PLUS				\
	   && GET_CODE (XEXP (XEXP (X, 0), 1)) == CONST_INT		\
	   && BASE_REGISTER_RTX_P (XEXP (XEXP (X, 0), 0))		\
	   && GET_CODE (XEXP (X, 1)) == CONST_INT			\
	   && ! TARGET_SHMEDIA						\
	   && ! (TARGET_SH2E && MODE == SFmode))			\
    {									\
      /* Because this address is so complex, we know it must have	\
	 been created by LEGITIMIZE_RELOAD_ADDRESS before; thus,	\
	 it is already unshared, and needs no further unsharing.  */	\
      push_reload (XEXP ((X), 0), NULL_RTX, &XEXP ((X), 0), NULL,	\
		   BASE_REG_CLASS, Pmode, VOIDmode, 0, 0, (OPNUM), (TYPE));\
      goto WIN;								\
    }									\
}

/* Go to LABEL if ADDR (a legitimate address expression)
   has an effect that depends on the machine mode it is used for.

   ??? Strictly speaking, we should also include all indexed addressing,
   because the index scale factor is the length of the operand.
   However, the impact of GO_IF_MODE_DEPENDENT_ADDRESS would be to
   high if we did that.  So we rely on reload to fix things up.  */

#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR,LABEL)			\
{									\
  if (GET_CODE(ADDR) == PRE_DEC || GET_CODE(ADDR) == POST_INC)		\
    goto LABEL;								\
}

/* Specify the machine mode that this machine uses
   for the index in the tablejump instruction.  */
#define CASE_VECTOR_MODE ((! optimize || TARGET_BIGTABLE) ? SImode : HImode)

#define CASE_VECTOR_SHORTEN_MODE(MIN_OFFSET, MAX_OFFSET, BODY) \
((MIN_OFFSET) >= 0 && (MAX_OFFSET) <= 127 \
 ? (ADDR_DIFF_VEC_FLAGS (BODY).offset_unsigned = 0, QImode) \
 : (MIN_OFFSET) >= 0 && (MAX_OFFSET) <= 255 \
 ? (ADDR_DIFF_VEC_FLAGS (BODY).offset_unsigned = 1, QImode) \
 : (MIN_OFFSET) >= -32768 && (MAX_OFFSET) <= 32767 ? HImode \
 : SImode)

/* Define as C expression which evaluates to nonzero if the tablejump
   instruction expects the table to contain offsets from the address of the
   table.
   Do not define this if the table should contain absolute addresses.  */
#define CASE_VECTOR_PC_RELATIVE 1

/* Define it here, so that it doesn't get bumped to 64-bits on SHmedia.  */
#define FLOAT_TYPE_SIZE 32

/* Since the SH2e has only `float' support, it is desirable to make all
   floating point types equivalent to `float'.  */
#define DOUBLE_TYPE_SIZE ((TARGET_SH2E && ! TARGET_SH4 && ! TARGET_SH2A_DOUBLE) ? 32 : 64)

#if defined(__SH2E__) || defined(__SH3E__) || defined( __SH4_SINGLE_ONLY__)
#define LIBGCC2_DOUBLE_TYPE_SIZE 32
#else
#define LIBGCC2_DOUBLE_TYPE_SIZE 64
#endif

/* 'char' is signed by default.  */
#define DEFAULT_SIGNED_CHAR  1

/* The type of size_t unsigned int.  */
#define SIZE_TYPE (TARGET_SH5 ? "long unsigned int" : "unsigned int")

#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE (TARGET_SH5 ? "long int" : "int")

#define WCHAR_TYPE "short unsigned int"
#define WCHAR_TYPE_SIZE 16

#define SH_ELF_WCHAR_TYPE "long int"

/* Max number of bytes we can move from memory to memory
   in one reasonably fast instruction.  */
#define MOVE_MAX (TARGET_SHMEDIA ? 8 : 4)

/* Maximum value possibly taken by MOVE_MAX.  Must be defined whenever
   MOVE_MAX is not a compile-time constant.  */
#define MAX_MOVE_MAX 8

/* Max number of bytes we want move_by_pieces to be able to copy
   efficiently.  */
#define MOVE_MAX_PIECES (TARGET_SH4 || TARGET_SHMEDIA ? 8 : 4)

/* Define if operations between registers always perform the operation
   on the full register even if a narrower mode is specified.  */
#define WORD_REGISTER_OPERATIONS

/* Define if loading in MODE, an integral mode narrower than BITS_PER_WORD
   will either zero-extend or sign-extend.  The value of this macro should
   be the code that says which one of the two operations is implicitly
   done, UNKNOWN if none.  */
/* For SHmedia, we can truncate to QImode easier using zero extension.  */
/* FP registers can load SImode values, but don't implicitly sign-extend
   them to DImode.  */
#define LOAD_EXTEND_OP(MODE) \
 (((MODE) == QImode  && TARGET_SHMEDIA) ? ZERO_EXTEND \
  : (MODE) != SImode ? SIGN_EXTEND : UNKNOWN)

/* Define if loading short immediate values into registers sign extends.  */
#define SHORT_IMMEDIATES_SIGN_EXTEND

/* Nonzero if access to memory by bytes is no faster than for words.  */
#define SLOW_BYTE_ACCESS 1

/* Immediate shift counts are truncated by the output routines (or was it
   the assembler?).  Shift counts in a register are truncated by SH.  Note
   that the native compiler puts too large (> 32) immediate shift counts
   into a register and shifts by the register, letting the SH decide what
   to do instead of doing that itself.  */
/* ??? The library routines in lib1funcs.asm truncate the shift count.
   However, the SH3 has hardware shifts that do not truncate exactly as gcc
   expects - the sign bit is significant - so it appears that we need to
   leave this zero for correct SH3 code.  */
#define SHIFT_COUNT_TRUNCATED (! TARGET_SH3 && ! TARGET_SH2A)

/* All integers have the same format so truncation is easy.  */
/* But SHmedia must sign-extend DImode when truncating to SImode.  */
#define TRULY_NOOP_TRUNCATION(OUTPREC,INPREC) \
 (!TARGET_SHMEDIA || (INPREC) < 64 || (OUTPREC) >= 64)

/* Define this if addresses of constant functions
   shouldn't be put through pseudo regs where they can be cse'd.
   Desirable on machines where ordinary constants are expensive
   but a CALL with constant address is cheap.  */
/*#define NO_FUNCTION_CSE 1*/

/* The machine modes of pointers and functions.  */
#define Pmode  (TARGET_SHMEDIA64 ? DImode : SImode)
#define FUNCTION_MODE  Pmode

/* The multiply insn on the SH1 and the divide insns on the SH1 and SH2
   are actually function calls with some special constraints on arguments
   and register usage.

   These macros tell reorg that the references to arguments and
   register clobbers for insns of type sfunc do not appear to happen
   until after the millicode call.  This allows reorg to put insns
   which set the argument registers into the delay slot of the millicode
   call -- thus they act more like traditional CALL_INSNs.

   get_attr_is_sfunc will try to recognize the given insn, so make sure to
   filter out things it will not accept -- SEQUENCE, USE and CLOBBER insns
   in particular.  */

#define INSN_SETS_ARE_DELAYED(X) 		\
  ((GET_CODE (X) == INSN			\
    && GET_CODE (PATTERN (X)) != SEQUENCE	\
    && GET_CODE (PATTERN (X)) != USE		\
    && GET_CODE (PATTERN (X)) != CLOBBER	\
    && get_attr_is_sfunc (X)))

#define INSN_REFERENCES_ARE_DELAYED(X) 		\
  ((GET_CODE (X) == INSN			\
    && GET_CODE (PATTERN (X)) != SEQUENCE	\
    && GET_CODE (PATTERN (X)) != USE		\
    && GET_CODE (PATTERN (X)) != CLOBBER	\
    && get_attr_is_sfunc (X)))


/* Position Independent Code.  */

/* We can't directly access anything that contains a symbol,
   nor can we indirect via the constant pool.  */
#define LEGITIMATE_PIC_OPERAND_P(X)				\
	((! nonpic_symbol_mentioned_p (X)			\
	  && (GET_CODE (X) != SYMBOL_REF			\
	      || ! CONSTANT_POOL_ADDRESS_P (X)			\
	      || ! nonpic_symbol_mentioned_p (get_pool_constant (X)))) \
	 || (TARGET_SHMEDIA && GET_CODE (X) == LABEL_REF))

#define SYMBOLIC_CONST_P(X)	\
((GET_CODE (X) == SYMBOL_REF || GET_CODE (X) == LABEL_REF)	\
  && nonpic_symbol_mentioned_p (X))

/* Compute extra cost of moving data between one register class
   and another.  */

/* If SECONDARY*_RELOAD_CLASS says something about the src/dst pair, regclass
   uses this information.  Hence, the general register <-> floating point
   register information here is not used for SFmode.  */

#define REGCLASS_HAS_GENERAL_REG(CLASS) \
  ((CLASS) == GENERAL_REGS || (CLASS) == R0_REGS \
    || (! TARGET_SHMEDIA && (CLASS) == SIBCALL_REGS))

#define REGCLASS_HAS_FP_REG(CLASS) \
  ((CLASS) == FP0_REGS || (CLASS) == FP_REGS \
   || (CLASS) == DF_REGS || (CLASS) == DF_HI_REGS)

#define REGISTER_MOVE_COST(MODE, SRCCLASS, DSTCLASS) \
  sh_register_move_cost ((MODE), (SRCCLASS), (DSTCLASS))

/* ??? Perhaps make MEMORY_MOVE_COST depend on compiler option?  This
   would be so that people with slow memory systems could generate
   different code that does fewer memory accesses.  */

/* A C expression for the cost of a branch instruction.  A value of 1
   is the default; other values are interpreted relative to that.
   The SH1 does not have delay slots, hence we get a pipeline stall
   at every branch.  The SH4 is superscalar, so the single delay slot
   is not sufficient to keep both pipelines filled.  */
#define BRANCH_COST (TARGET_SH5 ? 1 : ! TARGET_SH2 || TARGET_HARD_SH4 ? 2 : 1)

/* Assembler output control.  */

/* A C string constant describing how to begin a comment in the target
   assembler language.  The compiler assumes that the comment will end at
   the end of the line.  */
#define ASM_COMMENT_START "!"

#define ASM_APP_ON  		""
#define ASM_APP_OFF  		""
#define FILE_ASM_OP 		"\t.file\n"
#define SET_ASM_OP		"\t.set\t"

/* How to change between sections.  */

#define TEXT_SECTION_ASM_OP  		(TARGET_SHMEDIA32 ? "\t.section\t.text..SHmedia32,\"ax\"" : "\t.text")
#define DATA_SECTION_ASM_OP  		"\t.data"

#if defined CRT_BEGIN || defined CRT_END
/* Arrange for TEXT_SECTION_ASM_OP to be a compile-time constant.  */
# undef TEXT_SECTION_ASM_OP
# if __SHMEDIA__ == 1 && __SH5__ == 32
#  define TEXT_SECTION_ASM_OP "\t.section\t.text..SHmedia32,\"ax\""
# else
#  define TEXT_SECTION_ASM_OP "\t.text"
# endif
#endif


/* If defined, a C expression whose value is a string containing the
   assembler operation to identify the following data as
   uninitialized global data.  If not defined, and neither
   `ASM_OUTPUT_BSS' nor `ASM_OUTPUT_ALIGNED_BSS' are defined,
   uninitialized global data will be output in the data section if
   `-fno-common' is passed, otherwise `ASM_OUTPUT_COMMON' will be
   used.  */
#ifndef BSS_SECTION_ASM_OP
#define BSS_SECTION_ASM_OP	"\t.section\t.bss"
#endif

/* Like `ASM_OUTPUT_BSS' except takes the required alignment as a
   separate, explicit argument.  If you define this macro, it is used
   in place of `ASM_OUTPUT_BSS', and gives you more flexibility in
   handling the required alignment of the variable.  The alignment is
   specified as the number of bits.

   Try to use function `asm_output_aligned_bss' defined in file
   `varasm.c' when defining this macro.  */
#ifndef ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss (FILE, DECL, NAME, SIZE, ALIGN)
#endif

/* Define this so that jump tables go in same section as the current function,
   which could be text or it could be a user defined section.  */
#define JUMP_TABLES_IN_TEXT_SECTION 1

#undef DO_GLOBAL_CTORS_BODY
#define DO_GLOBAL_CTORS_BODY			\
{						\
  typedef (*pfunc)();				\
  extern pfunc __ctors[];			\
  extern pfunc __ctors_end[];			\
  pfunc *p;					\
  for (p = __ctors_end; p > __ctors; )		\
    {						\
      (*--p)();					\
    }						\
}

#undef DO_GLOBAL_DTORS_BODY
#define DO_GLOBAL_DTORS_BODY			\
{						\
  typedef (*pfunc)();				\
  extern pfunc __dtors[];			\
  extern pfunc __dtors_end[];			\
  pfunc *p;					\
  for (p = __dtors; p < __dtors_end; p++)	\
    {						\
      (*p)();					\
    }						\
}

#define ASM_OUTPUT_REG_PUSH(file, v) \
{							\
  if (TARGET_SHMEDIA)					\
    {							\
      fprintf ((file), "\taddi.l\tr15,-8,r15\n");	\
      fprintf ((file), "\tst.q\tr15,0,r%d\n", (v));	\
    }							\
  else							\
    fprintf ((file), "\tmov.l\tr%d,@-r15\n", (v));	\
}

#define ASM_OUTPUT_REG_POP(file, v) \
{							\
  if (TARGET_SHMEDIA)					\
    {							\
      fprintf ((file), "\tld.q\tr15,0,r%d\n", (v));	\
      fprintf ((file), "\taddi.l\tr15,8,r15\n");	\
    }							\
  else							\
    fprintf ((file), "\tmov.l\t@r15+,r%d\n", (v));	\
}

/* DBX register number for a given compiler register number.  */
/* GDB has FPUL at 23 and FP0 at 25, so we must add one to all FP registers
   to match gdb.  */
/* svr4.h undefines this macro, yet we really want to use the same numbers
   for coff as for elf, so we go via another macro: SH_DBX_REGISTER_NUMBER.  */
/* expand_builtin_init_dwarf_reg_sizes uses this to test if a
   register exists, so we should return -1 for invalid register numbers.  */
#define DBX_REGISTER_NUMBER(REGNO) SH_DBX_REGISTER_NUMBER (REGNO)

/* SHcompact PR_REG used to use the encoding 241, and SHcompact FP registers
   used to use the encodings 245..260, but that doesn't make sense:
   PR_REG and PR_MEDIA_REG are actually the same register, and likewise
   the FP registers stay the same when switching between compact and media
   mode.  Hence, we also need to use the same dwarf frame columns.
   Likewise, we need to support unwind information for SHmedia registers
   even in compact code.  */
#define SH_DBX_REGISTER_NUMBER(REGNO) \
  (IN_RANGE ((REGNO), \
	     (unsigned HOST_WIDE_INT) FIRST_GENERAL_REG, \
	     FIRST_GENERAL_REG + (TARGET_SH5 ? 63U :15U)) \
   ? ((unsigned) (REGNO) - FIRST_GENERAL_REG) \
  : ((int) (REGNO) >= FIRST_FP_REG \
     && ((int) (REGNO) \
	 <= (FIRST_FP_REG + \
	     ((TARGET_SH5 && TARGET_FPU_ANY) ? 63 : TARGET_SH2E ? 15 : -1)))) \
   ? ((unsigned) (REGNO) - FIRST_FP_REG \
      + (TARGET_SH5 ? 77 : 25)) \
   : XD_REGISTER_P (REGNO) \
   ? ((unsigned) (REGNO) - FIRST_XD_REG + (TARGET_SH5 ? 289 : 87)) \
   : TARGET_REGISTER_P (REGNO) \
   ? ((unsigned) (REGNO) - FIRST_TARGET_REG + 68) \
   : (REGNO) == PR_REG \
   ? (TARGET_SH5 ? 18 : 17) \
   : (REGNO) == PR_MEDIA_REG \
   ? (TARGET_SH5 ? 18 : (unsigned) -1) \
   : (REGNO) == T_REG \
   ? (TARGET_SH5 ? 242 : 18) \
   : (REGNO) == GBR_REG \
   ? (TARGET_SH5 ? 238 : 19) \
   : (REGNO) == MACH_REG \
   ? (TARGET_SH5 ? 239 : 20) \
   : (REGNO) == MACL_REG \
   ? (TARGET_SH5 ? 240 : 21) \
   : (REGNO) == FPUL_REG \
   ? (TARGET_SH5 ? 244 : 23) \
   : (unsigned) -1)

/* This is how to output a reference to a symbol_ref.  On SH5,
   references to non-code symbols must be preceded by `datalabel'.  */
#define ASM_OUTPUT_SYMBOL_REF(FILE,SYM)			\
  do 							\
    {							\
      if (TARGET_SH5 && !SYMBOL_REF_FUNCTION_P (SYM))	\
	fputs ("datalabel ", (FILE));			\
      assemble_name ((FILE), XSTR ((SYM), 0));		\
    }							\
  while (0)

/* This is how to output an assembler line
   that says to advance the location counter
   to a multiple of 2**LOG bytes.  */

#define ASM_OUTPUT_ALIGN(FILE,LOG)	\
  if ((LOG) != 0)			\
    fprintf ((FILE), "\t.align %d\n", (LOG))

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.global\t"

/* #define ASM_OUTPUT_CASE_END(STREAM,NUM,TABLE)	    */

/* Output a relative address table.  */

#define ASM_OUTPUT_ADDR_DIFF_ELT(STREAM,BODY,VALUE,REL)  		\
  switch (GET_MODE (BODY))						\
    {									\
    case SImode:							\
      if (TARGET_SH5)							\
	{								\
	  asm_fprintf ((STREAM), "\t.long\t%LL%d-datalabel %LL%d\n",	\
		       (VALUE), (REL));					\
	  break;							\
	}								\
      asm_fprintf ((STREAM), "\t.long\t%LL%d-%LL%d\n", (VALUE),(REL));	\
      break;								\
    case HImode:							\
      if (TARGET_SH5)							\
	{								\
	  asm_fprintf ((STREAM), "\t.word\t%LL%d-datalabel %LL%d\n",	\
		       (VALUE), (REL));					\
	  break;							\
	}								\
      asm_fprintf ((STREAM), "\t.word\t%LL%d-%LL%d\n", (VALUE),(REL));	\
      break;								\
    case QImode:							\
      if (TARGET_SH5)							\
	{								\
	  asm_fprintf ((STREAM), "\t.byte\t%LL%d-datalabel %LL%d\n",	\
		       (VALUE), (REL));					\
	  break;							\
	}								\
      asm_fprintf ((STREAM), "\t.byte\t%LL%d-%LL%d\n", (VALUE),(REL));	\
      break;								\
    default:								\
      break;								\
    }

/* Output an absolute table element.  */

#define ASM_OUTPUT_ADDR_VEC_ELT(STREAM,VALUE)  				\
  if (! optimize || TARGET_BIGTABLE)					\
    asm_fprintf ((STREAM), "\t.long\t%LL%d\n", (VALUE)); 		\
  else									\
    asm_fprintf ((STREAM), "\t.word\t%LL%d\n", (VALUE));


/* A C statement to be executed just prior to the output of
   assembler code for INSN, to modify the extracted operands so
   they will be output differently.

   Here the argument OPVEC is the vector containing the operands
   extracted from INSN, and NOPERANDS is the number of elements of
   the vector which contain meaningful data for this insn.
   The contents of this vector are what will be used to convert the insn
   template into assembler code, so you can change the assembler output
   by changing the contents of the vector.  */

#define FINAL_PRESCAN_INSN(INSN, OPVEC, NOPERANDS) \
  final_prescan_insn ((INSN), (OPVEC), (NOPERANDS))

/* Print operand X (an rtx) in assembler syntax to file FILE.
   CODE is a letter or dot (`z' in `%z0') or 0 if no letter was specified.
   For `%' followed by punctuation, CODE is the punctuation and X is null.  */

#define PRINT_OPERAND(STREAM, X, CODE)  print_operand ((STREAM), (X), (CODE))

/* Print a memory address as an operand to reference that memory location.  */

#define PRINT_OPERAND_ADDRESS(STREAM,X)  print_operand_address ((STREAM), (X))

#define PRINT_OPERAND_PUNCT_VALID_P(CHAR) \
  ((CHAR) == '.' || (CHAR) == '#' || (CHAR) == '@' || (CHAR) == ','	\
   || (CHAR) == '$' || (CHAR) == '\'' || (CHAR) == '>')

/* Recognize machine-specific patterns that may appear within
   constants.  Used for PIC-specific UNSPECs.  */
#define OUTPUT_ADDR_CONST_EXTRA(STREAM, X, FAIL) \
  do									\
    if (GET_CODE (X) == UNSPEC && XVECLEN ((X), 0) == 1)	\
      {									\
	switch (XINT ((X), 1))						\
	  {								\
	  case UNSPEC_DATALABEL:					\
	    fputs ("datalabel ", (STREAM));				\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    break;							\
	  case UNSPEC_PIC:						\
	    /* GLOBAL_OFFSET_TABLE or local symbols, no suffix.  */	\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    break;							\
	  case UNSPEC_GOT:						\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    fputs ("@GOT", (STREAM));					\
	    break;							\
	  case UNSPEC_GOTOFF:						\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    fputs ("@GOTOFF", (STREAM));				\
	    break;							\
	  case UNSPEC_PLT:						\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    fputs ("@PLT", (STREAM));					\
	    break;							\
	  case UNSPEC_GOTPLT:						\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    fputs ("@GOTPLT", (STREAM));				\
	    break;							\
	  case UNSPEC_DTPOFF:						\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    fputs ("@DTPOFF", (STREAM));				\
	    break;							\
	  case UNSPEC_GOTTPOFF:						\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    fputs ("@GOTTPOFF", (STREAM));				\
	    break;							\
	  case UNSPEC_TPOFF:						\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    fputs ("@TPOFF", (STREAM));					\
	    break;							\
	  case UNSPEC_CALLER:						\
	    {								\
	      char name[32];						\
	      /* LPCS stands for Label for PIC Call Site.  */		\
	      ASM_GENERATE_INTERNAL_LABEL				\
		(name, "LPCS", INTVAL (XVECEXP ((X), 0, 0)));		\
	      assemble_name ((STREAM), name);				\
	    }								\
	    break;							\
	  default:							\
	    goto FAIL;							\
	  }								\
	break;								\
      }									\
    else								\
      goto FAIL;							\
  while (0)


extern struct rtx_def *sh_compare_op0;
extern struct rtx_def *sh_compare_op1;

/* Which processor to schedule for.  The elements of the enumeration must
   match exactly the cpu attribute in the sh.md file.  */

enum processor_type {
  PROCESSOR_SH1,
  PROCESSOR_SH2,
  PROCESSOR_SH2E,
  PROCESSOR_SH2A,
  PROCESSOR_SH3,
  PROCESSOR_SH3E,
  PROCESSOR_SH4,
  PROCESSOR_SH4A,
  PROCESSOR_SH5
};

#define sh_cpu_attr ((enum attr_cpu)sh_cpu)
extern enum processor_type sh_cpu;

extern int optimize; /* needed for gen_casesi.  */

enum mdep_reorg_phase_e
{
  SH_BEFORE_MDEP_REORG,
  SH_INSERT_USES_LABELS,
  SH_SHORTEN_BRANCHES0,
  SH_FIXUP_PCLOAD,
  SH_SHORTEN_BRANCHES1,
  SH_AFTER_MDEP_REORG
};

extern enum mdep_reorg_phase_e mdep_reorg_phase;

/* Handle Renesas compiler's pragmas.  */
#define REGISTER_TARGET_PRAGMAS() do {					\
  c_register_pragma (0, "interrupt", sh_pr_interrupt);			\
  c_register_pragma (0, "trapa", sh_pr_trapa);				\
  c_register_pragma (0, "nosave_low_regs", sh_pr_nosave_low_regs);	\
} while (0)

extern tree sh_deferred_function_attributes;
extern tree *sh_deferred_function_attributes_tail;

/* Set when processing a function with interrupt attribute.  */

extern int current_function_interrupt;


/* Instructions with unfilled delay slots take up an
   extra two bytes for the nop in the delay slot.
   sh-dsp parallel processing insns are four bytes long.  */

#define ADJUST_INSN_LENGTH(X, LENGTH)				\
  (LENGTH) += sh_insn_length_adjustment (X);

/* Define this macro if it is advisable to hold scalars in registers
   in a wider mode than that declared by the program.  In such cases,
   the value is constrained to be within the bounds of the declared
   type, but kept valid in the wider mode.  The signedness of the
   extension may differ from that of the type.

   Leaving the unsignedp unchanged gives better code than always setting it
   to 0.  This is despite the fact that we have only signed char and short
   load instructions.  */
#define PROMOTE_MODE(MODE, UNSIGNEDP, TYPE) \
  if (GET_MODE_CLASS (MODE) == MODE_INT			\
      && GET_MODE_SIZE (MODE) < 4/* ! UNITS_PER_WORD */)\
    (UNSIGNEDP) = ((MODE) == SImode ? 0 : (UNSIGNEDP)),	\
    (MODE) = (TARGET_SH1 ? SImode \
	      : TARGET_SHMEDIA32 ? SImode : DImode);

#define MAX_FIXED_MODE_SIZE (TARGET_SH5 ? 128 : 64)

#define SIDI_OFF (TARGET_LITTLE_ENDIAN ? 0 : 4)

/* ??? Define ACCUMULATE_OUTGOING_ARGS?  This is more efficient than pushing
   and popping arguments.  However, we do have push/pop instructions, and
   rather limited offsets (4 bits) in load/store instructions, so it isn't
   clear if this would give better code.  If implemented, should check for
   compatibility problems.  */

#define SH_DYNAMIC_SHIFT_COST \
  (TARGET_HARD_SH4 ? 1 : TARGET_SH3 ? (TARGET_SMALLCODE ? 1 : 2) : 20)


#define NUM_MODES_FOR_MODE_SWITCHING { FP_MODE_NONE }

#define OPTIMIZE_MODE_SWITCHING(ENTITY) (TARGET_SH4 || TARGET_SH2A_DOUBLE)

#define ACTUAL_NORMAL_MODE(ENTITY) \
  (TARGET_FPU_SINGLE ? FP_MODE_SINGLE : FP_MODE_DOUBLE)

#define NORMAL_MODE(ENTITY) \
  (sh_cfun_interrupt_handler_p () \
   ? (TARGET_FMOVD ? FP_MODE_DOUBLE : FP_MODE_NONE) \
   : ACTUAL_NORMAL_MODE (ENTITY))

#define MODE_ENTRY(ENTITY) NORMAL_MODE (ENTITY)

#define MODE_EXIT(ENTITY) \
  (sh_cfun_attr_renesas_p () ? FP_MODE_NONE : NORMAL_MODE (ENTITY))

#define EPILOGUE_USES(REGNO)       ((TARGET_SH2E || TARGET_SH4)		\
				    && (REGNO) == FPSCR_REG)

#define MODE_NEEDED(ENTITY, INSN)					\
  (recog_memoized (INSN) >= 0						\
   ? get_attr_fp_mode (INSN)						\
   : FP_MODE_NONE)

#define MODE_AFTER(MODE, INSN)                  \
     (TARGET_HITACHI				\
      && recog_memoized (INSN) >= 0		\
      && get_attr_fp_set (INSN) != FP_SET_NONE  \
      ? (int) get_attr_fp_set (INSN)            \
      : (MODE))

#define MODE_PRIORITY_TO_MODE(ENTITY, N) \
  ((TARGET_FPU_SINGLE != 0) ^ (N) ? FP_MODE_SINGLE : FP_MODE_DOUBLE)

#define EMIT_MODE_SET(ENTITY, MODE, HARD_REGS_LIVE) \
  fpscr_set_from_mem ((MODE), (HARD_REGS_LIVE))

#define MD_CAN_REDIRECT_BRANCH(INSN, SEQ) \
  sh_can_redirect_branch ((INSN), (SEQ))

#define DWARF_FRAME_RETURN_COLUMN \
  (TARGET_SH5 ? DWARF_FRAME_REGNUM (PR_MEDIA_REG) : DWARF_FRAME_REGNUM (PR_REG))

#define EH_RETURN_DATA_REGNO(N)	\
  ((N) < 4 ? (N) + (TARGET_SH5 ? 2U : 4U) : INVALID_REGNUM)

#define EH_RETURN_STACKADJ_REGNO STATIC_CHAIN_REGNUM
#define EH_RETURN_STACKADJ_RTX	gen_rtx_REG (Pmode, EH_RETURN_STACKADJ_REGNO)

/* We have to distinguish between code and data, so that we apply
   datalabel where and only where appropriate.  Use sdataN for data.  */
#define ASM_PREFERRED_EH_DATA_FORMAT(CODE, GLOBAL) \
 ((flag_pic && (GLOBAL) ? DW_EH_PE_indirect : 0) \
  | (flag_pic ? DW_EH_PE_pcrel : DW_EH_PE_absptr) \
  | ((CODE) ? 0 : (TARGET_SHMEDIA64 ? DW_EH_PE_sdata8 : DW_EH_PE_sdata4)))

/* Handle special EH pointer encodings.  Absolute, pc-relative, and
   indirect are handled automatically.  */
#define ASM_MAYBE_OUTPUT_ENCODED_ADDR_RTX(FILE, ENCODING, SIZE, ADDR, DONE) \
  do { \
    if (((ENCODING) & 0xf) != DW_EH_PE_sdata4 \
	&& ((ENCODING) & 0xf) != DW_EH_PE_sdata8) \
      { \
	gcc_assert (GET_CODE (ADDR) == SYMBOL_REF); \
	SYMBOL_REF_FLAGS (ADDR) |= SYMBOL_FLAG_FUNCTION; \
	if (0) goto DONE; \
      } \
  } while (0)

#if (defined CRT_BEGIN || defined CRT_END) && ! __SHMEDIA__
/* SH constant pool breaks the devices in crtstuff.c to control section
   in where code resides.  We have to write it as asm code.  */
#define CRT_CALL_STATIC_FUNCTION(SECTION_OP, FUNC) \
   asm (SECTION_OP "\n\
	mov.l	1f,r1\n\
	mova	2f,r0\n\
	braf	r1\n\
	lds	r0,pr\n\
0:	.p2align 2\n\
1:	.long	" USER_LABEL_PREFIX #FUNC " - 0b\n\
2:\n" TEXT_SECTION_ASM_OP);
#endif /* (defined CRT_BEGIN || defined CRT_END) && ! __SHMEDIA__ */

#define SIMULTANEOUS_PREFETCHES 2

/* FIXME: middle-end support for highpart optimizations is missing.  */
#define high_life_started reload_in_progress

#endif /* ! GCC_SH_H */

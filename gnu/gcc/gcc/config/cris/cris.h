/* Definitions for GCC.  Part of the machine description for CRIS.
   Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Axis Communications.  Written by Hans-Peter Nilsson.

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

/* After the first "Node:" comment comes all preprocessor directives and
   attached declarations described in the info files, the "Using and
   Porting GCC" manual (uapgcc), in the same order as found in the "Target
   macros" section in the gcc-2.9x CVS edition of 2000-03-17.  FIXME: Not
   really, but needs an update anyway.

   There is no generic copy-of-uapgcc comment, you'll have to see uapgcc
   for that.  If applicable, there is a CRIS-specific comment.  The order
   of macro definitions follow the order in the manual.  Every section in
   the manual (node in the info pages) has an introductory `Node:
   <subchapter>' comment.  If no macros are defined for a section, only
   the section-comment is present.  */

/* Note that other header files (e.g. config/elfos.h, config/linux.h,
   config/cris/linux.h and config/cris/aout.h) are responsible for lots of
   settings not repeated below.  This file contains general CRIS
   definitions and definitions for the cris-*-elf subtarget.  */

/* We don't want to use gcc_assert for everything, as that can be
   compiled out.  */
#define CRIS_ASSERT(x) \
 do { if (!(x)) internal_error ("CRIS-port assertion failed: " #x); } while (0)

/* Replacement for REG_P since it does not match SUBREGs.  Happens for
   testcase Axis-20000320 with gcc-2.9x.  */
#define REG_S_P(x) \
 (REG_P (x) || (GET_CODE (x) == SUBREG && REG_P (XEXP (x, 0))))

/* Last register in main register bank r0..r15.  */
#define CRIS_LAST_GENERAL_REGISTER 15

/* Descriptions of registers used for arguments.  */
#define CRIS_FIRST_ARG_REG 10
#define CRIS_MAX_ARGS_IN_REGS 4

/* See also *_REGNUM constants in cris.md.  */

/* Most of the time, we need the index into the register-names array.
   When passing debug-info, we need the real hardware register number.  */
#define CRIS_CANONICAL_SRP_REGNUM (16 + 11)
#define CRIS_CANONICAL_MOF_REGNUM (16 + 7)
/* We have CCR in all models including v10, but that's 16 bits, so let's
   prefer the DCCR number, which is a DMA pointer in pre-v8, so we'll
   never clash with it for GCC purposes.  */
#define CRIS_CANONICAL_CC0_REGNUM (16 + 13)

/* When generating PIC, these suffixes are added to the names of non-local
   functions when being output.  Contrary to other ports, we have offsets
   relative to the GOT, not the PC.  We might implement PC-relative PLT
   semantics later for the general case; they are used in some cases right
   now, such as MI thunks.  */
#define CRIS_GOTPLT_SUFFIX ":GOTPLT"
#define CRIS_PLT_GOTOFFSET_SUFFIX ":PLTG"
#define CRIS_PLT_PCOFFSET_SUFFIX ":PLT"

#define CRIS_FUNCTION_ARG_SIZE(MODE, TYPE)	\
  ((MODE) != BLKmode ? GET_MODE_SIZE (MODE)	\
   : (unsigned) int_size_in_bytes (TYPE))

/* Which CPU version this is.  The parsed and adjusted cris_cpu_str.  */
extern int cris_cpu_version;

/* Changing the order used to be necessary to put the fourth __make_dp
   argument (a DImode parameter) in registers, to fit with the libfunc
   parameter passing scheme used for intrinsic functions.  FIXME: Check
   performance and maybe remove definition from TARGET_LIBGCC2_CFLAGS now
   that it isn't strictly necessary.  We used to do this through
   TARGET_LIBGCC2_CFLAGS, but that became increasingly difficult as the
   parenthesis (that needed quoting) travels through several layers of
   make and shell invocations.  */
#ifdef IN_LIBGCC2
#define __make_dp(a,b,c,d) __cris_make_dp(d,a,b,c)
#endif


/* Node: Driver */

/* When using make with defaults.mak for Sun this will handily remove
   any "-target sun*" switches.  */
/* We need to override any previous definitions (linux.h) */
#undef WORD_SWITCH_TAKES_ARG
#define WORD_SWITCH_TAKES_ARG(STR)		\
 (DEFAULT_WORD_SWITCH_TAKES_ARG (STR)		\
  || !strcmp (STR, "target"))

/* Also provide canonical vN definitions when user specifies an alias.
   Note that -melf overrides -maout.  */

#define CPP_SPEC \
 "%{mtune=*:-D__tune_%* %{mtune=v*:-D__CRIS_arch_tune=%*}\
   %{mtune=etrax4:-D__tune_v3 -D__CRIS_arch_tune=3}\
   %{mtune=etrax100:-D__tune_v8 -D__CRIS_arch_tune=8}\
   %{mtune=svinto:-D__tune_v8 -D__CRIS_arch_tune=8}\
   %{mtune=etrax100lx:-D__tune_v10 -D__CRIS_arch_tune=10}\
   %{mtune=ng:-D__tune_v10 -D__CRIS_arch_tune=10}}\
  %{mcpu=*:-D__arch_%* %{mcpu=v*:-D__CRIS_arch_version=%*}\
   %{mcpu=etrax4:-D__arch_v3 -D__CRIS_arch_version=3}\
   %{mcpu=etrax100:-D__arch_v8 -D__CRIS_arch_version=8}\
   %{mcpu=svinto:-D__arch_v8 -D__CRIS_arch_version=8}\
   %{mcpu=etrax100lx:-D__arch_v10 -D__CRIS_arch_version=10}\
   %{mcpu=ng:-D__arch_v10 -D__CRIS_arch_version=10}}\
  %{march=*:-D__arch_%* %{march=v*:-D__CRIS_arch_version=%*}\
   %{march=etrax4:-D__arch_v3 -D__CRIS_arch_version=3}\
   %{march=etrax100:-D__arch_v8 -D__CRIS_arch_version=8}\
   %{march=svinto:-D__arch_v8 -D__CRIS_arch_version=8}\
   %{march=etrax100lx:-D__arch_v10 -D__CRIS_arch_version=10}\
   %{march=ng:-D__arch_v10 -D__CRIS_arch_version=10}}\
  %{metrax100:-D__arch__v8 -D__CRIS_arch_version=8}\
  %{metrax4:-D__arch__v3 -D__CRIS_arch_version=3}\
  %(cpp_subtarget)"

/* For the cris-*-elf subtarget.  */
#define CRIS_CPP_SUBTARGET_SPEC \
 "%{mbest-lib-options:\
   %{!moverride-best-lib-options:\
    %{!march=*:%{!metrax*:%{!mcpu=*:-D__tune_v10 -D__CRIS_arch_tune=10}}}}}"

/* Remove those Sun-make "target" switches.  */
/* Override previous definitions (linux.h).  */
#undef CC1_SPEC
#define CC1_SPEC \
 "%{target*:}\
  %{metrax4:-march=v3}\
  %{metrax100:-march=v8}\
  %(cc1_subtarget)"

/* For the cris-*-elf subtarget.  */
#define CRIS_CC1_SUBTARGET_SPEC \
 "-melf\
  %{mbest-lib-options:\
   %{!moverride-best-lib-options:\
    %{!march=*:%{!mcpu=*:-mtune=v10 -D__CRIS_arch_tune=10}}\
    %{!finhibit-size-directive:\
      %{!fno-function-sections: -ffunction-sections}\
      %{!fno-data-sections: -fdata-sections}}}}"

/* This adds to CC1_SPEC.  When bugs are removed from -fvtable-gc
   (-fforce-addr causes invalid .vtable_entry asm in tinfo.cc and
   nothing at all works in GCC 3.0-pre), add this line:
   "%{mbest-lib-options:%{!moverride-best-lib-options:\
   %{!melinux:%{!maout|melf:%{!fno-vtable-gc:-fvtable-gc}}}}}".  */
#define CC1PLUS_SPEC ""

#ifdef HAVE_AS_NO_MUL_BUG_ABORT_OPTION
#define MAYBE_AS_NO_MUL_BUG_ABORT \
 "%{mno-mul-bug-workaround:-no-mul-bug-abort} "
#else
#define MAYBE_AS_NO_MUL_BUG_ABORT
#endif

/* Override previous definitions (linux.h).  */
#undef ASM_SPEC
#define ASM_SPEC \
 MAYBE_AS_NO_MUL_BUG_ABORT \
 "%{v:-v}\
  %(asm_subtarget)"

/* For the cris-*-elf subtarget.  */
#define CRIS_ASM_SUBTARGET_SPEC "--em=criself"

/* FIXME: We should propagate the -melf option to make the criself
   "emulation" unless a linker script is provided (-T*), but I don't know
   how to do that if either of -Ttext, -Tdata or -Tbss is given but no
   linker script, as is usually the case.  Leave it to the user for the
   time being.

   Note that -melf overrides -maout except that a.out-compiled libraries
   are linked in (multilibbing).  The somewhat cryptic -rpath-link pair is
   to avoid *only* picking up the linux multilib subdir from the "-B./"
   option during build, while still giving it preference.  We'd need some
   %s-variant that checked for existence of some specific file.  */
/* Override previous definitions (svr4.h).  */
#undef LINK_SPEC
#define LINK_SPEC \
 "%{v:--verbose}\
  %(link_subtarget)"

/* For the cris-*-elf subtarget.  */
#define CRIS_LINK_SUBTARGET_SPEC \
 "-mcriself\
  %{sim2:%{!T*:-Tdata 0x4000000 -Tbss 0x8000000}}\
  %{!r:%{O2|O3: --gc-sections}}"

/* Which library to get.  The simulator uses a different library for
   the low-level syscalls (implementing the Linux syscall ABI instead
   of direct-iron accesses).  Default everything with the stub "nosys"
   library.  */
/* Override previous definitions (linux.h).  */
#undef LIB_SPEC
#define LIB_SPEC \
 "%{sim*:--start-group -lc -lsyslinux --end-group}\
  %{!sim*:%{g*:-lg}\
    %{!p:%{!pg:-lc}}%{p:-lc_p}%{pg:-lc_p} -lbsp}\
  -lnosys"

/* Linker startfile options; crt0 flavors.
   We need to remove any previous definition (elfos.h).  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
 "%{sim*:crt1.o%s}%{!sim*:crt0.o%s}\
  crti.o%s crtbegin.o%s"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s crtn.o%s"

#define EXTRA_SPECS				\
  {"cpp_subtarget", CRIS_CPP_SUBTARGET_SPEC},	\
  {"cc1_subtarget", CRIS_CC1_SUBTARGET_SPEC},	\
  {"asm_subtarget", CRIS_ASM_SUBTARGET_SPEC},	\
  {"link_subtarget", CRIS_LINK_SUBTARGET_SPEC},	\
  CRIS_SUBTARGET_EXTRA_SPECS

#define CRIS_SUBTARGET_EXTRA_SPECS


/* Node: Run-time Target */

#define TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define_std ("cris");		\
      builtin_define_std ("CRIS");		\
      builtin_define_std ("GNU_CRIS");		\
      builtin_define ("__CRIS_ABI_version=2");	\
      builtin_assert ("cpu=cris");		\
      builtin_assert ("machine=cris");		\
    }						\
  while (0)

/* This needs to be at least 32 bits.  */
extern int target_flags;

/* Previously controlled by target_flags.  */
#define TARGET_ELF 1

/* Previously controlled by target_flags.  Note that this is *not* set
   for -melinux.  */
#define TARGET_LINUX 0

/* Default target_flags if no switches specified.  */
#ifndef TARGET_DEFAULT
# define TARGET_DEFAULT \
 (MASK_SIDE_EFFECT_PREFIXES + MASK_STACK_ALIGN \
  + MASK_CONST_ALIGN + MASK_DATA_ALIGN \
  + MASK_PROLOGUE_EPILOGUE + MASK_MUL_BUG)
#endif

/* For the cris-*-elf subtarget.  */
#define CRIS_SUBTARGET_DEFAULT 0

#define CRIS_CPU_BASE 0
#define CRIS_CPU_ETRAX4 3	/* Just lz added.  */
#define CRIS_CPU_SVINTO 8	/* Added swap, jsrc & Co., 32-bit accesses.  */
#define CRIS_CPU_NG 10		/* Added mul[su].  */

/* Local, providing a default for cris_cpu_version.  */
#define CRIS_DEFAULT_CPU_VERSION CRIS_CPU_BASE

#define TARGET_HAS_MUL_INSNS (cris_cpu_version >= CRIS_CPU_NG)

#define CRIS_SUBTARGET_HANDLE_OPTION(x, y, z)

/* Print subsidiary information on the compiler version in use.
   Do not use VD.D syntax (D=digit), since this will cause confusion
   with the base gcc version among users, when we ask which version of
   gcc-cris they are using.  Please use some flavor of "R<number>" for
   the version (no need for major.minor versions, I believe).  */
#define TARGET_VERSION \
 fprintf (stderr, " [Axis CRIS%s]", CRIS_SUBTARGET_VERSION)

/* For the cris-*-elf subtarget.  */
#define CRIS_SUBTARGET_VERSION " - generic ELF"

#define OVERRIDE_OPTIONS cris_override_options ()

/* The following gives optimal code for gcc-2.7.2, but *may* be subject
   to change.  Omitting flag_force_addr gives .1-.7% faster code for gcc
   *only*, but 1.3% larger code.  On ipps it gives 5.3-10.6% slower
   code(!) and 0.3% larger code.  For products, images gets .1-1.8%
   larger.  Do not set strict aliasing from optimization options.  */
#define OPTIMIZATION_OPTIONS(OPTIMIZE, SIZE)	\
  do						\
    {						\
      if ((OPTIMIZE) >= 2 || (SIZE))		\
	{					\
	  flag_force_addr = 1;			\
	  flag_omit_frame_pointer = 1;		\
	}					\
    }						\
  while (0)


/* Node: Storage Layout */

#define BITS_BIG_ENDIAN 0

#define BYTES_BIG_ENDIAN 0

/* WORDS_BIG_ENDIAN is not defined in the hardware, but for consistency,
   we use little-endianness, and we may also be able to use
   post-increment on DImode indirect.  */
#define WORDS_BIG_ENDIAN 0

#define UNITS_PER_WORD 4

/* A combination of defining PROMOTE_FUNCTION_MODE,
   TARGET_PROMOTE_FUNCTION_ARGS that always returns true
   and *not* defining TARGET_PROMOTE_PROTOTYPES or PROMOTE_MODE gives the
   best code size and speed for gcc, ipps and products in gcc-2.7.2.  */
#define CRIS_PROMOTED_MODE(MODE, UNSIGNEDP, TYPE) \
 (GET_MODE_CLASS (MODE) == MODE_INT && GET_MODE_SIZE (MODE) < 4) \
  ? SImode : MODE

#define PROMOTE_FUNCTION_MODE(MODE, UNSIGNEDP, TYPE)  \
  (MODE) = CRIS_PROMOTED_MODE (MODE, UNSIGNEDP, TYPE)

/* Defining PROMOTE_FUNCTION_RETURN in gcc-2.7.2 uncovers bug 981110 (even
   if defining FUNCTION_VALUE with MODE as PROMOTED_MODE ;-)

   FIXME: Report this when cris.h is part of GCC, so others can easily
   see the problem.  Maybe check other systems that define
   TARGET_PROMOTE_FUNCTION_RETURN that always returns true.  */

/* We will be using prototype promotion, so they will be 32 bit.  */
#define PARM_BOUNDARY 32

/* Stack boundary is guided by -mstack-align, -mno-stack-align,
   -malign.
   Old comment: (2.1: still valid in 2.7.2?)
    Note that to make this macro affect the alignment of stack
   locals, a fix was required, and special precautions when handling
   the stack pointer in various other macros (TARGET_ASM_FUNCTION_PROLOGUE
   et al) were required.  See file "function.c".  If you would just define
   this macro, it would only affect the builtin alloca and variable
   local data (non-ANSI, non-K&R, Gnu C extension).  */
#define STACK_BOUNDARY \
 (TARGET_STACK_ALIGN ? (TARGET_ALIGN_BY_32 ? 32 : 16) : 8)

#define FUNCTION_BOUNDARY 16

/* Do not change BIGGEST_ALIGNMENT (when optimizing), as it will affect
   strange places, at least in 2.1.  */
#define BIGGEST_ALIGNMENT 8

/* If -m16bit,	-m16-bit, -malign or -mdata-align,
   align everything to 16 bit.  */
#define DATA_ALIGNMENT(TYPE, BASIC_ALIGN)			\
 (TARGET_DATA_ALIGN						\
  ? (TARGET_ALIGN_BY_32						\
     ? (BASIC_ALIGN < 32 ? 32 : BASIC_ALIGN)			\
     : (BASIC_ALIGN < 16 ? 16 : BASIC_ALIGN)) : BASIC_ALIGN)

/* Note that CONSTANT_ALIGNMENT has the effect of making gcc believe that
   ALL references to constant stuff (in code segment, like strings) has
   this alignment.  That is a rather rushed assumption.  Luckily we do not
   care about the "alignment" operand to builtin memcpy (only place where
   it counts), so it doesn't affect any bad spots.  */
#define CONSTANT_ALIGNMENT(CONSTANT, BASIC_ALIGN)		\
 (TARGET_CONST_ALIGN						\
  ? (TARGET_ALIGN_BY_32						\
     ? (BASIC_ALIGN < 32 ? 32 : BASIC_ALIGN)			\
     : (BASIC_ALIGN < 16 ? 16 : BASIC_ALIGN)) : BASIC_ALIGN)

/* FIXME: Define LOCAL_ALIGNMENT for word and dword or arrays and
   structures (if -mstack-align=), and check that it is good.  */

#define EMPTY_FIELD_BOUNDARY 8

#define STRUCTURE_SIZE_BOUNDARY 8

#define STRICT_ALIGNMENT 0

/* Remove any previous definition (elfos.h).
   ??? If it wasn't for all the other stuff that affects layout of
   structures and bit-fields, this could presumably cause incompatibility
   with other GNU/Linux ports (i.e. elfos.h users).  */
#undef PCC_BITFIELD_TYPE_MATTERS

/* This is only used for non-scalars.  Strange stuff happens to structs
   (FIXME: What?) if we use anything larger than largest actually used
   datum size, so lets make it 32.  The type "long long" will still work
   as usual.  We can still have DImode insns, but they will only be used
   for scalar data (i.e. long long).  */
#define MAX_FIXED_MODE_SIZE 32


/* Node: Type Layout */

/* Note that DOUBLE_TYPE_SIZE is not defined anymore, since the default
   value gives a 64-bit double, which is what we now use.  */

/* For compatibility and historical reasons, a char should be signed.  */
#define DEFAULT_SIGNED_CHAR 1

/* Note that WCHAR_TYPE_SIZE is used in cexp.y,
   where TARGET_SHORT is not available.  */
#undef WCHAR_TYPE
#define WCHAR_TYPE "long int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32


/* Node: Register Basics */

/*  We count all 16 non-special registers, SRP, a faked argument
    pointer register, MOF and CCR/DCCR.  */
#define FIRST_PSEUDO_REGISTER (16 + 1 + 1 + 1 + 1)

/* For CRIS, these are r15 (pc) and r14 (sp). Register r8 is used as a
   frame-pointer, but is not fixed.  SRP is not included in general
   registers and will not be used automatically.  All other special
   registers are fixed at the moment.  The faked argument pointer register
   is fixed too.  */
#define FIXED_REGISTERS \
 {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0}

/* Register r9 is used for structure-address, r10-r13 for parameters,
   r10- for return values.  */
#define CALL_USED_REGISTERS \
 {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1}

#define CONDITIONAL_REGISTER_USAGE cris_conditional_register_usage ()


/* Node: Allocation Order */

/* We need this on CRIS, because call-used regs should be used first,
   (so we don't need to push).  Else start using registers from r0 and up.
    This preference is mainly because if we put call-used-regs from r0
   and up, then we can't use movem to push the rest, (which have to be
   saved if we use them, and movem has to start with r0).
   Change here if you change which registers to use as call registers.

   The actual need to explicitly prefer call-used registers improved the
   situation a lot for 2.1, but might not actually be needed anymore.
   Still, this order reflects what GCC should find out by itself, so it
   probably does not hurt.

   Order of preference: Call-used-regs first, then r0 and up, last fp &
   sp & pc as fillers.
   Call-used regs in opposite order, so they will cause less conflict if
   a function has few args (<= 3) and it wants a scratch reg.
    Use struct-return address first, since very few functions use
   structure return values so it is likely to be available.  */
#define REG_ALLOC_ORDER \
 {9, 13, 12, 11, 10, 0, 1, 2, 3, 4, 5, 6, 7, 8, 14, 15, 17, 16, 18, 19}


/* Node: Values in Registers */

/* The VOIDmode test is so we can omit mode on anonymous insns.  FIXME:
   Still needed in 2.9x, at least for Axis-20000319.  */
#define HARD_REGNO_NREGS(REGNO, MODE)	\
 (MODE == VOIDmode \
  ? 1 : ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))

/* CRIS permits all registers to hold all modes.  Well, except for the
   condition-code register.  And we can't hold larger-than-register size
   modes in the last special register that can hold a full 32 bits.  */
#define HARD_REGNO_MODE_OK(REGNO, MODE)		\
 (((MODE) == CCmode				\
   || (REGNO) != CRIS_CC0_REGNUM)		\
  && (GET_MODE_SIZE (MODE) <= UNITS_PER_WORD	\
      || (REGNO) != CRIS_MOF_REGNUM))

/* Because CCmode isn't covered by the "narrower mode" statement in
   tm.texi, we can still say all modes are tieable despite not having an
   always 1 HARD_REGNO_MODE_OK.  */
#define MODES_TIEABLE_P(MODE1, MODE2) 1


/* Node: Leaf Functions */
/* (no definitions) */

/* Node: Stack Registers */
/* (no definitions) */


/* Node: Register Classes */

enum reg_class 
  {
    NO_REGS,
    MOF_REGS, CC0_REGS, SPECIAL_REGS, GENERAL_REGS, ALL_REGS,
    LIM_REG_CLASSES
  };

#define N_REG_CLASSES (int) LIM_REG_CLASSES

#define REG_CLASS_NAMES							\
  {"NO_REGS",								\
   "MOF_REGS", "CC0_REGS", "SPECIAL_REGS", "GENERAL_REGS", "ALL_REGS"}

#define CRIS_SPECIAL_REGS_CONTENTS					\
 ((1 << CRIS_SRP_REGNUM) | (1 << CRIS_MOF_REGNUM) | (1 << CRIS_CC0_REGNUM))

/* Count in the faked argument register in GENERAL_REGS.  Keep out SRP.  */
#define REG_CLASS_CONTENTS			\
  {						\
   {0},						\
   {1 << CRIS_MOF_REGNUM},			\
   {1 << CRIS_CC0_REGNUM},			\
   {CRIS_SPECIAL_REGS_CONTENTS},		\
   {0xffff | (1 << CRIS_AP_REGNUM)},		\
   {0xffff | (1 << CRIS_AP_REGNUM)		\
    | CRIS_SPECIAL_REGS_CONTENTS}		\
  }

#define REGNO_REG_CLASS(REGNO)			\
  ((REGNO) == CRIS_MOF_REGNUM ? MOF_REGS :	\
   (REGNO) == CRIS_CC0_REGNUM ? CC0_REGS :	\
   (REGNO) == CRIS_SRP_REGNUM ? SPECIAL_REGS :	\
   GENERAL_REGS)

#define BASE_REG_CLASS GENERAL_REGS

#define INDEX_REG_CLASS GENERAL_REGS

#define REG_CLASS_FROM_LETTER(C)		\
  (						\
   (C) == 'h' ? MOF_REGS :			\
   (C) == 'x' ? SPECIAL_REGS :			\
   (C) == 'c' ? CC0_REGS :			\
   NO_REGS					\
  )

/* Since it uses reg_renumber, it is safe only once reg_renumber
   has been allocated, which happens in local-alloc.c.  */
#define REGNO_OK_FOR_BASE_P(REGNO)					\
 ((REGNO) <= CRIS_LAST_GENERAL_REGISTER					\
  || (REGNO) == ARG_POINTER_REGNUM					\
  || (unsigned) reg_renumber[REGNO] <= CRIS_LAST_GENERAL_REGISTER	\
  || (unsigned) reg_renumber[REGNO] == ARG_POINTER_REGNUM)

/* See REGNO_OK_FOR_BASE_P.  */
#define REGNO_OK_FOR_INDEX_P(REGNO) REGNO_OK_FOR_BASE_P(REGNO)

/* It seems like gcc (2.7.2 and 2.9x of 2000-03-22) may send "NO_REGS" as
   the class for a constant (testcase: __Mul in arit.c).  To avoid forcing
   out a constant into the constant pool, we will trap this case and
   return something a bit more sane.  FIXME: Check if this is a bug.
   Beware that we must not "override" classes that can be specified as
   constraint letters, or else asm operands using them will fail when
   they need to be reloaded.  FIXME: Investigate whether that constitutes
   a bug.  */
#define PREFERRED_RELOAD_CLASS(X, CLASS)	\
 ((CLASS) != MOF_REGS				\
  && (CLASS) != CC0_REGS			\
  && (CLASS) != SPECIAL_REGS			\
  ? GENERAL_REGS : (CLASS))

/* We can't move special registers to and from memory in smaller than
   word_mode.  */
#define SECONDARY_RELOAD_CLASS(CLASS, MODE, X)		\
  (((CLASS) != SPECIAL_REGS && (CLASS) != MOF_REGS)	\
   || GET_MODE_SIZE (MODE) == 4				\
   || GET_CODE (X) != MEM				\
   ? NO_REGS : GENERAL_REGS)

/* For CRIS, this is always the size of MODE in words,
   since all registers are the same size.  To use omitted modes in
   patterns with reload constraints, you must say the widest size
   which is allowed for VOIDmode.
   FIXME:  Does that still apply for gcc-2.9x?  Keep poisoned until such
   patterns are added back.  News: 2001-03-16: Happens as early as the
   underscore-test.  */
#define CLASS_MAX_NREGS(CLASS, MODE)					\
 ((MODE) == VOIDmode							\
  ? 1 /* + cris_fatal ("CLASS_MAX_NREGS with VOIDmode")	*/		\
  : ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))

/* We are now out of letters; we could use ten more.  This forces us to
   use C-code in the 'md' file.  FIXME: Use some EXTRA_CONSTRAINTS.  */
#define CONST_OK_FOR_LETTER_P(VALUE, C)			\
 (							\
  /* MOVEQ, CMPQ, ANDQ, ORQ.  */			\
  (C) == 'I' ? (VALUE) >= -32 && (VALUE) <= 31 :	\
  /* ADDQ, SUBQ.  */					\
  (C) == 'J' ? (VALUE) >= 0 && (VALUE) <= 63 :		\
  /* ASRQ, BTSTQ, LSRQ, LSLQ.  */			\
  (C) == 'K' ? (VALUE) >= 0 && (VALUE) <= 31 :		\
  /* A 16-bit signed number.  */			\
  (C) == 'L' ? (VALUE) >= -32768 && (VALUE) <= 32767 :	\
  /* The constant 0 for CLEAR.  */			\
  (C) == 'M' ? (VALUE) == 0 :				\
  /* A negative ADDQ or SUBQ.  */			\
  (C) == 'N' ? (VALUE) >= -63 && (VALUE) < 0 :		\
  /* Quickened ints, QI and HI.  */			\
  (C) == 'O' ? (VALUE) >= 0 && (VALUE) <= 65535		\
		&& ((VALUE) >= (65535-31)		\
		    || ((VALUE) >= (255-31)		\
			&& (VALUE) <= 255 )) :		\
  /* A 16-bit number signed *or* unsigned.  */		\
  (C) == 'P' ? (VALUE) >= -32768 && (VALUE) <= 65535 :	\
  0)

/* It is really simple to make up a 0.0; it is the same as int-0 in
   IEEE754.  */
#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C)			\
 ((C) == 'G' && ((VALUE) == CONST0_RTX (DFmode)			\
		 || (VALUE) == CONST0_RTX (SFmode)))

/* We need this on cris to distinguish delay-slottable addressing modes.  */
#define EXTRA_CONSTRAINT(X, C)			\
 (						\
  /* Slottable address mode?  */		\
  (C) == 'Q' ? EXTRA_CONSTRAINT_Q (X) :		\
  /* Operand to BDAP or BIAP?  */		\
  (C) == 'R' ? EXTRA_CONSTRAINT_R (X) :		\
  /* A local PIC symbol?  */			\
  (C) == 'S' ? EXTRA_CONSTRAINT_S (X) :		\
  /* A three-address addressing-mode?  */	\
  (C) == 'T' ? EXTRA_CONSTRAINT_T (X) :		\
  0)

#define EXTRA_MEMORY_CONSTRAINT(X, STR) ((X) == 'Q')

#define EXTRA_CONSTRAINT_Q(X)				\
 (							\
  /* Just an indirect register (happens to also be	\
     "all" slottable memory addressing modes not	\
     covered by other constraints, i.e. '>').  */	\
  GET_CODE (X) == MEM && BASE_P (XEXP (X, 0))		\
 )

#define EXTRA_CONSTRAINT_R(X)					\
 (								\
  /* An operand to BDAP or BIAP:				\
     A BIAP; r.S? */						\
  BIAP_INDEX_P (X)						\
  /* A [reg] or (int) [reg], maybe with post-increment.  */	\
  || BDAP_INDEX_P (X)						\
  || CONSTANT_INDEX_P (X)					\
 )

#define EXTRA_CONSTRAINT_T(X)						\
 (									\
  /* Memory three-address operand.  All are indirect-memory:  */	\
  GET_CODE (X) == MEM							\
  && ((GET_CODE (XEXP (X, 0)) == MEM					\
       /* Double indirect: [[reg]] or [[reg+]]?  */			\
       && (BASE_OR_AUTOINCR_P (XEXP (XEXP (X, 0), 0))))			\
      /* Just an explicit indirect reference: [const]?  */		\
      || CONSTANT_P (XEXP (X, 0))					\
      /* Something that is indexed; [...+...]?  */			\
      || (GET_CODE (XEXP (X, 0)) == PLUS				\
	  /* A BDAP constant: [reg+(8|16|32)bit offset]?  */		\
	  && ((BASE_P (XEXP (XEXP (X, 0), 0))				\
	       && CONSTANT_INDEX_P (XEXP (XEXP (X, 0), 1)))		\
	      /* A BDAP register: [reg+[reg(+)].S]?  */			\
	      || (BASE_P (XEXP (XEXP (X, 0), 0))			\
		  && BDAP_INDEX_P(XEXP(XEXP(X, 0), 1)))			\
	      /* Same, but with swapped arguments (no canonical		\
		 ordering between e.g. REG and MEM as of LAST_UPDATED	\
		 "Thu May 12 03:59:11 UTC 2005").  */			\
	      || (BASE_P (XEXP (XEXP (X, 0), 1))			\
		  && BDAP_INDEX_P (XEXP (XEXP (X, 0), 0)))		\
	      /* A BIAP: [reg+reg.S] (MULT comes first).  */		\
	      || (BASE_P (XEXP (XEXP (X, 0), 1))			\
		  && BIAP_INDEX_P (XEXP (XEXP (X, 0), 0))))))		\
 )

/* PIC-constructs for symbols.  */
#define EXTRA_CONSTRAINT_S(X)						\
 (flag_pic && GET_CODE (X) == CONST && cris_valid_pic_const (X))


/* Node: Frame Layout */

#define STACK_GROWS_DOWNWARD
#define FRAME_GROWS_DOWNWARD 1

/* It seems to be indicated in the code (at least 2.1) that this is
   better a constant, and best 0.  */
#define STARTING_FRAME_OFFSET 0

#define FIRST_PARM_OFFSET(FNDECL) 0

#define RETURN_ADDR_RTX(COUNT, FRAMEADDR) \
 cris_return_addr_rtx (COUNT, FRAMEADDR)

#define INCOMING_RETURN_ADDR_RTX gen_rtx_REG (Pmode, CRIS_SRP_REGNUM)

/* FIXME: Any __builtin_eh_return callers must not return anything and
   there must not be collisions with incoming parameters.  Luckily the
   number of __builtin_eh_return callers is limited.  For now return
   parameter registers in reverse order and hope for the best.  */
#define EH_RETURN_DATA_REGNO(N) \
  (IN_RANGE ((N), 0, 3) ? (CRIS_FIRST_ARG_REG + 3 - (N)) : INVALID_REGNUM)

/* Store the stack adjustment in the structure-return-address register.  */
#define CRIS_STACKADJ_REG CRIS_STRUCT_VALUE_REGNUM
#define EH_RETURN_STACKADJ_RTX gen_rtx_REG (SImode, CRIS_STACKADJ_REG)

#define EH_RETURN_HANDLER_RTX \
  cris_return_addr_rtx (0, NULL)

#define INIT_EXPANDERS cris_init_expanders ()

/* FIXME: Move this to right node (it's not documented properly yet).  */
#define DWARF_FRAME_RETURN_COLUMN DWARF_FRAME_REGNUM (CRIS_SRP_REGNUM)

/* FIXME: Move this to right node (it's not documented properly yet).
   FIXME: Check what alignment we can assume regarding
   TARGET_STACK_ALIGN and TARGET_ALIGN_BY_32.  */
#define DWARF_CIE_DATA_ALIGNMENT -1

/* If we would ever need an exact mapping between canonical register
   number and dwarf frame register, we would either need to include all
   registers in the gcc description (with some marked fixed of course), or
   an inverse mapping from dwarf register to gcc register.  There is one
   need in dwarf2out.c:expand_builtin_init_dwarf_reg_sizes.  Right now, I
   don't see that we need exact correspondence between DWARF *frame*
   registers and DBX_REGISTER_NUMBER, so map them onto GCC registers.  */
#define DWARF_FRAME_REGNUM(REG) (REG)

/* Node: Stack Checking */
/* (no definitions) FIXME: Check.  */

/* Node: Frame Registers */

#define STACK_POINTER_REGNUM CRIS_SP_REGNUM

/* Register used for frame pointer.  This is also the last of the saved
   registers, when a frame pointer is not used.  */
#define FRAME_POINTER_REGNUM CRIS_FP_REGNUM

/* Faked register, is always eliminated.  We need it to eliminate
   allocating stack slots for the return address and the frame pointer.  */
#define ARG_POINTER_REGNUM CRIS_AP_REGNUM

#define STATIC_CHAIN_REGNUM CRIS_STATIC_CHAIN_REGNUM


/* Node: Elimination */

/* Really only needed if the stack frame has variable length (alloca
   or variable sized local arguments (GNU C extension).  */
#define FRAME_POINTER_REQUIRED 0

#define ELIMINABLE_REGS				\
 {{ARG_POINTER_REGNUM, STACK_POINTER_REGNUM},	\
  {ARG_POINTER_REGNUM, FRAME_POINTER_REGNUM},	\
  {FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM}}

/* We need not worry about when the frame-pointer is required for other
   reasons.  */
#define CAN_ELIMINATE(FROM, TO) 1

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) \
 (OFFSET) = cris_initial_elimination_offset (FROM, TO)


/* Node: Stack Arguments */

/* Since many parameters take up one register each in any case,
   defining TARGET_PROMOTE_PROTOTYPES that always returns true would
   seem like a good idea, but measurements indicate that a combination
   using PROMOTE_MODE is better.  */

#define ACCUMULATE_OUTGOING_ARGS 1

#define RETURN_POPS_ARGS(FUNDECL, FUNTYPE, STACKSIZE) 0


/* Node: Register Arguments */

/* The void_type_node is sent as a "closing" call.  */
#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED)			\
 ((CUM).regs < CRIS_MAX_ARGS_IN_REGS				\
  ? gen_rtx_REG (MODE, (CRIS_FIRST_ARG_REG) + (CUM).regs)	\
  : NULL_RTX)

/* The differences between this and the previous, is that this one checks
   that an argument is named, since incoming stdarg/varargs arguments are
   pushed onto the stack, and we don't have to check against the "closing"
   void_type_node TYPE parameter.  */
#define FUNCTION_INCOMING_ARG(CUM, MODE, TYPE, NAMED)		\
 ((NAMED) && (CUM).regs < CRIS_MAX_ARGS_IN_REGS			\
  ? gen_rtx_REG (MODE, CRIS_FIRST_ARG_REG + (CUM).regs)		\
  : NULL_RTX)

/* Contrary to what you'd believe, defining FUNCTION_ARG_CALLEE_COPIES
   seems like a (small total) loss, at least for gcc-2.7.2 compiling and
   running gcc-2.1 (small win in size, small loss running -- 100.1%),
   and similarly for size for products (.1 .. .3% bloat, sometimes win).
   Due to the empirical likeliness of making slower code, it is not
   defined.  */

/* This no longer *needs* to be a structure; but keeping it as such should
   not hurt (and hacking the ABI is simpler).  */
#define CUMULATIVE_ARGS struct cum_args
struct cum_args {int regs;};

/* The regs member is an integer, the number of arguments got into
   registers so far.  */
#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, FNDECL, N_NAMED_ARGS) \
 ((CUM).regs = 0)

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)		\
 ((CUM).regs += (3 + CRIS_FUNCTION_ARG_SIZE (MODE, TYPE)) / 4)

#define FUNCTION_ARG_REGNO_P(REGNO)			\
 ((REGNO) >= CRIS_FIRST_ARG_REG				\
  && (REGNO) < CRIS_FIRST_ARG_REG + (CRIS_MAX_ARGS_IN_REGS))


/* Node: Scalar Return */

/* Let's assume all functions return in r[CRIS_FIRST_ARG_REG] for the
   time being.  */
#define FUNCTION_VALUE(VALTYPE, FUNC)  \
 gen_rtx_REG (TYPE_MODE (VALTYPE), CRIS_FIRST_ARG_REG)

#define LIBCALL_VALUE(MODE) gen_rtx_REG (MODE, CRIS_FIRST_ARG_REG)

#define FUNCTION_VALUE_REGNO_P(N) ((N) == CRIS_FIRST_ARG_REG)


/* Node: Aggregate Return */

#if 0
/* FIXME: Let's try this some time, so we return structures in registers.
   We would cast the result of int_size_in_bytes to unsigned, so we will
   get a huge number for "structures" of variable size (-1).  */
#define RETURN_IN_MEMORY(TYPE) \
 ((unsigned) int_size_in_bytes (TYPE) > CRIS_MAX_ARGS_IN_REGS * UNITS_PER_WORD)
#endif

#define CRIS_STRUCT_VALUE_REGNUM ((CRIS_FIRST_ARG_REG) - 1)


/* Node: Caller Saves */
/* (no definitions) */

/* Node: Function entry */

/* See cris.c for TARGET_ASM_FUNCTION_PROLOGUE and
   TARGET_ASM_FUNCTION_EPILOGUE.  */

/* Node: Profiling */

#define FUNCTION_PROFILER(FILE, LABELNO)  \
 error ("no FUNCTION_PROFILER for CRIS")

/* FIXME: Some of the undefined macros might be mandatory.  If so, fix
   documentation.  */


/* Node: Trampolines */

/* This looks too complicated, and it is.  I assigned r7 to be the
   static chain register, but it is call-saved, so we have to save it,
   and come back to restore it after the call, so we have to save srp...
   Anyway, trampolines are rare enough that we can cope with this
   somewhat lack of elegance.
    (Do not be tempted to "straighten up" whitespace in the asms; the
   assembler #NO_APP state mandates strict spacing).  */
#define TRAMPOLINE_TEMPLATE(FILE)		\
  do						\
    {						\
      fprintf (FILE, "\tmove.d $%s,[$pc+20]\n",	\
	       reg_names[STATIC_CHAIN_REGNUM]);	\
      fprintf (FILE, "\tmove $srp,[$pc+22]\n");	\
      fprintf (FILE, "\tmove.d 0,$%s\n",	\
	       reg_names[STATIC_CHAIN_REGNUM]);	\
      fprintf (FILE, "\tjsr 0\n");		\
      fprintf (FILE, "\tmove.d 0,$%s\n",	\
	       reg_names[STATIC_CHAIN_REGNUM]);	\
      fprintf (FILE, "\tjump 0\n");		\
    }						\
  while (0)

#define TRAMPOLINE_SIZE 32

/* CRIS wants instructions on word-boundary.
   Note that due to a bug (reported) in 2.7.2 and earlier, this is
   actually treated as alignment in _bytes_, not _bits_.  (Obviously
   this is not fatal, only a slight waste of stack space).  */
#define TRAMPOLINE_ALIGNMENT 16

#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT)		\
  do								\
    {								\
      emit_move_insn (gen_rtx_MEM (SImode,			\
			       plus_constant (TRAMP, 10)),	\
		      CXT);					\
      emit_move_insn (gen_rtx_MEM (SImode,			\
			       plus_constant (TRAMP, 16)),	\
		      FNADDR);					\
    }								\
  while (0)

/* Note that there is no need to do anything with the cache for sake of
   a trampoline.  */


/* Node: Library Calls */

/* If you change this, you have to check whatever libraries and systems
   that use it.  */
#define TARGET_EDOM 33


/* Node: Addressing Modes */

#define HAVE_POST_INCREMENT 1

#define CONSTANT_ADDRESS_P(X) CONSTANT_P (X)

#define MAX_REGS_PER_ADDRESS 2

/* There are helper macros defined here which are used only in
   GO_IF_LEGITIMATE_ADDRESS.

   Note that you *have to* reject invalid addressing modes for mode
   MODE, even if it is legal for normal addressing modes.  You cannot
   rely on the constraints to do this work.  They can only be used to
   doublecheck your intentions.  One example is that you HAVE TO reject
   (mem:DI (plus:SI (reg:SI x) (reg:SI y))) because for some reason
   this cannot be reloaded.  (Which of course you can argue that gcc
   should have done.)  FIXME:  Strange.  Check.  */

/* No symbol can be used as an index (or more correct, as a base) together
   with a register with PIC; the PIC register must be there.  */
#define CONSTANT_INDEX_P(X) \
 (CONSTANT_P (X) && (!flag_pic || cris_valid_pic_const (X)))

/* True if X is a valid base register.  */
#define BASE_P(X) \
 (REG_P (X) && REG_OK_FOR_BASE_P (X))

/* True if X is a valid base register with or without autoincrement.  */
#define BASE_OR_AUTOINCR_P(X) \
 (BASE_P (X) || (GET_CODE (X) == POST_INC && BASE_P (XEXP (X, 0))))

/* True if X is a valid (register) index for BDAP, i.e. [Rs].S or [Rs+].S.  */
#define BDAP_INDEX_P(X)					\
 ((GET_CODE (X) == MEM && GET_MODE (X) == SImode	\
   && BASE_OR_AUTOINCR_P (XEXP (X, 0)))			\
  || (GET_CODE (X) == SIGN_EXTEND			\
      && GET_CODE (XEXP (X, 0)) == MEM			\
      && (GET_MODE (XEXP (X, 0)) == HImode		\
	  || GET_MODE (XEXP (X, 0)) == QImode)		\
      && BASE_OR_AUTOINCR_P (XEXP (XEXP (X, 0), 0))))

/* True if X is a valid (register) index for BIAP, i.e. Rd.m.  */
#define BIAP_INDEX_P(X)				\
 ((BASE_P (X) && REG_OK_FOR_INDEX_P (X))	\
  || (GET_CODE (X) == MULT			\
      && BASE_P (XEXP (X, 0))			\
      && REG_OK_FOR_INDEX_P (XEXP (X, 0))	\
      && GET_CODE (XEXP (X, 1)) == CONST_INT	\
      && (INTVAL (XEXP (X, 1)) == 2		\
	  || INTVAL (XEXP (X, 1)) == 4)))

/* True if X is an address that doesn't need a prefix i.e. [Rs] or [Rs+].  */
#define SIMPLE_ADDRESS_P(X) \
 (BASE_P (X)						\
  || (GET_CODE (X) == POST_INC				\
      && BASE_P (XEXP (X, 0))))

/* A PIC operand looks like a normal symbol here.  At output we dress it
   in "[rPIC+symbol:GOT]" (global symbol) or "rPIC+symbol:GOTOFF" (local
   symbol) so we exclude all addressing modes where we can't replace a
   plain "symbol" with that.  A global PIC symbol does not fit anywhere
   here (but is thankfully a general_operand in itself).  A local PIC
   symbol is valid for the plain "symbol + offset" case.  */
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)			\
 {								\
   rtx x1, x2;							\
   if (SIMPLE_ADDRESS_P (X))					\
     goto ADDR;							\
   if (CONSTANT_INDEX_P (X))					\
     goto ADDR;							\
   /* Indexed?  */						\
   if (GET_CODE (X) == PLUS)					\
     {								\
       x1 = XEXP (X, 0);					\
       x2 = XEXP (X, 1);					\
       /* BDAP o, Rd.  */					\
       if ((BASE_P (x1) && CONSTANT_INDEX_P (x2))		\
	   || (BASE_P (x2) && CONSTANT_INDEX_P (x1))		\
	    /* BDAP Rs[+], Rd.  */				\
	   || (GET_MODE_SIZE (MODE) <= UNITS_PER_WORD		\
	       && ((BASE_P (x1) && BDAP_INDEX_P (x2))		\
		   || (BASE_P (x2) && BDAP_INDEX_P (x1))	\
		   /* BIAP.m Rs, Rd */				\
		   || (BASE_P (x1) && BIAP_INDEX_P (x2))	\
		   || (BASE_P (x2) && BIAP_INDEX_P (x1)))))	\
	 goto ADDR;						\
     }								\
   else if (GET_CODE (X) == MEM)				\
     {								\
       /* DIP (Rs).  Reject [[reg+]] and [[reg]] for		\
	  DImode (long long).  */				\
       if (GET_MODE_SIZE (MODE) <= UNITS_PER_WORD		\
	   && (BASE_P (XEXP (X, 0))				\
	       || BASE_OR_AUTOINCR_P (XEXP (X, 0))))		\
	 goto ADDR;						\
     }								\
 }

#ifndef REG_OK_STRICT
 /* Nonzero if X is a hard reg that can be used as a base reg
    or if it is a pseudo reg.  */
# define REG_OK_FOR_BASE_P(X)			\
 (REGNO (X) <= CRIS_LAST_GENERAL_REGISTER	\
  || REGNO (X) == ARG_POINTER_REGNUM		\
  || REGNO (X) >= FIRST_PSEUDO_REGISTER)
#else
 /* Nonzero if X is a hard reg that can be used as a base reg.  */
# define REG_OK_FOR_BASE_P(X) REGNO_OK_FOR_BASE_P (REGNO (X))
#endif

#ifndef REG_OK_STRICT
 /* Nonzero if X is a hard reg that can be used as an index
    or if it is a pseudo reg.  */
# define REG_OK_FOR_INDEX_P(X) REG_OK_FOR_BASE_P (X)
#else
 /* Nonzero if X is a hard reg that can be used as an index.  */
# define REG_OK_FOR_INDEX_P(X) REGNO_OK_FOR_INDEX_P (REGNO (X))
#endif

/* For now, don't do anything.  GCC does a good job most often.

    Maybe we could do something about gcc:s misbehavior when it
   recalculates frame offsets for local variables, from fp+offs to
   sp+offs.  The resulting address expression gets screwed up
   sometimes, but I'm not sure that it may be fixed here, since it is
   already split up in several instructions (Is this still true?).
   FIXME: Check and adjust for gcc-2.9x.  */
#define LEGITIMIZE_ADDRESS(X, OLDX, MODE, WIN) {}

/* Fix reloads known to cause suboptimal spilling.  */
#define LEGITIMIZE_RELOAD_ADDRESS(X, MODE, OPNUM, TYPE, INDL, WIN)	\
  do									\
    {									\
      if (cris_reload_address_legitimized (X, MODE, OPNUM, TYPE, INDL))	\
	goto WIN;							\
    }									\
  while (0)

/* In CRIS, only the postincrement address mode depends thus,
   since the increment depends on the size of the operand.  */
#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR, LABEL)	\
  do							\
    {							\
      if (GET_CODE (ADDR) == POST_INC)			\
	goto LABEL;					\
    }							\
  while (0)

#define LEGITIMATE_CONSTANT_P(X) 1


/* Node: Condition Code */

#define NOTICE_UPDATE_CC(EXP, INSN) cris_notice_update_cc (EXP, INSN)

/* FIXME: Maybe define CANONICALIZE_COMPARISON later, when playing with
   optimizations.  It is needed; currently we do this with instruction
   patterns and NOTICE_UPDATE_CC.  */


/* Node: Costs */

/* Can't move to and from a SPECIAL_REGS register, so we have to say
   their move cost within that class is higher.  How about 7?  That's 3
   for a move to a GENERAL_REGS register, 3 for the move from the
   GENERAL_REGS register, and 1 for the increased register pressure.
   Also, it's higher than the memory move cost, which is in order.  
   We also do this for ALL_REGS, since we don't want that class to be
   preferred (even to memory) at all where GENERAL_REGS doesn't fit.
   Whenever it's about to be used, it's for SPECIAL_REGS.  If we don't
   present a higher cost for ALL_REGS than memory, a SPECIAL_REGS may be
   used when a GENERAL_REGS should be used, even if there are call-saved
   GENERAL_REGS left to allocate.  This is because the fall-back when
   the most preferred register class isn't available, isn't the next
   (or next good) wider register class, but the *most widest* register
   class.
   Give the cost 3 between a special register and a general register,
   because we want constraints verified.  */

#define REGISTER_MOVE_COST(MODE, FROM, TO)		\
 ((((FROM) == SPECIAL_REGS || (FROM) == MOF_REGS)	\
   && ((TO) == SPECIAL_REGS || (TO) == MOF_REGS))	\
  || (FROM) == ALL_REGS					\
  || (TO) == ALL_REGS					\
  ? 7 :							\
  ((FROM) == SPECIAL_REGS || (FROM) == MOF_REGS		\
   || (TO) == SPECIAL_REGS || (TO) == MOF_REGS)		\
  ? 3 : 2)

/* This isn't strictly correct for v0..3 in buswidth-8bit mode, but
   should suffice.  */
#define MEMORY_MOVE_COST(M, CLASS, IN) \
 (((M) == QImode) ? 4 : ((M) == HImode) ? 4 : 6)

/* Regardless of the presence of delay slots, the default value of 1 for
   BRANCH_COST is the best in the range (1, 2, 3), tested with gcc-2.7.2
   with testcases ipps and gcc, giving smallest and fastest code.  */

#define SLOW_BYTE_ACCESS 0

/* This is the threshold *below* which inline move sequences of
   word-length sizes will be emitted.  The "9" will translate to
   (9 - 1) * 4 = 32 bytes maximum moved, but using 16 instructions
   (8 instruction sequences) or less.  */
#define MOVE_RATIO 9


/* Node: Sections */

#define TEXT_SECTION_ASM_OP "\t.text"

#define DATA_SECTION_ASM_OP "\t.data"

#define FORCE_EH_FRAME_INFO_IN_DATA_SECTION (! TARGET_ELF)

/* The jump table is immediately connected to the preceding insn.  */
#define JUMP_TABLES_IN_TEXT_SECTION 1


/* Node: PIC */

/* Helper type.  */

enum cris_pic_symbol_type
  {
    cris_no_symbol = 0,
    cris_got_symbol = 1,
    cris_gotrel_symbol = 2,
    cris_got_symbol_needing_fixup = 3,
    cris_invalid_pic_symbol = 4
  };

#define PIC_OFFSET_TABLE_REGNUM (flag_pic ? CRIS_GOT_REGNUM : INVALID_REGNUM)

#define LEGITIMATE_PIC_OPERAND_P(X) cris_legitimate_pic_operand (X)


/* Node: File Framework */

/* We don't want an .ident for gcc.  To avoid that but still support
   #ident, we override ASM_OUTPUT_IDENT and, since the gcc .ident is its
   only use besides ASM_OUTPUT_IDENT, undef IDENT_ASM_OP from elfos.h.  */
#undef IDENT_ASM_OP
#undef ASM_OUTPUT_IDENT
#define ASM_OUTPUT_IDENT(FILE, NAME) \
  fprintf (FILE, "%s\"%s\"\n", "\t.ident\t", NAME);

#define ASM_APP_ON "#APP\n"

#define ASM_APP_OFF "#NO_APP\n"


/* Node: Data Output */

#define OUTPUT_ADDR_CONST_EXTRA(STREAM, X, FAIL) \
  do { if (!cris_output_addr_const_extra (STREAM, X)) goto FAIL; } while (0)

#define IS_ASM_LOGICAL_LINE_SEPARATOR(C) (C) == '@'

/* Node: Uninitialized Data */

/* Remember to round off odd values if we want data alignment,
   since we cannot do that with an .align directive.

   Using .comm causes the space not to be reserved in .bss, but by
   tricks with the symbol type.  Not good if other tools than binutils
   are used on the object files.  Since ".global ... .lcomm ..." works, we
   use that.  Use .._ALIGNED_COMMON, since gcc whines when we only have
   ..._COMMON, and we prefer to whine ourselves; BIGGEST_ALIGNMENT is not
   the one to check.  This done for a.out only.  */
/* FIXME: I suspect a bug in gcc with alignment.  Do not warn until
   investigated; it mucks up the testsuite results.  */
#define CRIS_ASM_OUTPUT_ALIGNED_DECL_COMMON(FILE, DECL, NAME, SIZE, ALIGN, LOCAL) \
  do									\
    {									\
      int align_ = (ALIGN) / BITS_PER_UNIT;				\
      if (TARGET_DATA_ALIGN && TARGET_ALIGN_BY_32 && align_ < 4)	\
	align_ = 4;							\
      else if (TARGET_DATA_ALIGN && align_ < 2)				\
	align_ = 2;							\
      /* FIXME: Do we need this?  */					\
      else if (align_ < 1)						\
	align_ = 1;							\
									\
      if (TARGET_ELF)							\
	{								\
	  if (LOCAL)							\
	    {								\
	      fprintf ((FILE), "%s", LOCAL_ASM_OP);			\
	      assemble_name ((FILE), (NAME));				\
	      fprintf ((FILE), "\n");					\
	    }								\
	  fprintf ((FILE), "%s", COMMON_ASM_OP);			\
	  assemble_name ((FILE), (NAME));				\
	  fprintf ((FILE), ",%u,%u\n", (int)(SIZE), align_);		\
	}								\
      else								\
	{								\
	  /* We can't tell a one-only or weak COMM from a "global	\
	     COMM" so just make all non-locals weak.  */		\
	  if (! (LOCAL))						\
	    ASM_WEAKEN_LABEL (FILE, NAME);				\
	  fputs ("\t.lcomm ", (FILE));					\
	  assemble_name ((FILE), (NAME));				\
	  fprintf ((FILE), ",%u\n",					\
		   ((int)(SIZE) + (align_ - 1)) & ~(align_ - 1));	\
	}								\
    }									\
  while (0)

#define ASM_OUTPUT_ALIGNED_DECL_COMMON(FILE, DECL, NAME, SIZE, ALIGN) \
 CRIS_ASM_OUTPUT_ALIGNED_DECL_COMMON(FILE, DECL, NAME, SIZE, ALIGN, 0)

#undef ASM_OUTPUT_ALIGNED_DECL_LOCAL
#define ASM_OUTPUT_ALIGNED_DECL_LOCAL(FILE, DECL, NAME, SIZE, ALIGN) \
 CRIS_ASM_OUTPUT_ALIGNED_DECL_COMMON(FILE, DECL, NAME, SIZE, ALIGN, 1)

/* Node: Label Output */

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.global "

#define SUPPORTS_WEAK 1

#define ASM_OUTPUT_SYMBOL_REF(STREAM, SYM) \
 cris_asm_output_symbol_ref (STREAM, SYM)

#define ASM_OUTPUT_LABEL_REF(STREAM, BUF) \
 cris_asm_output_label_ref (STREAM, BUF)

/* Remove any previous definition (elfos.h).  */
#undef ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(LABEL, PREFIX, NUM)	\
  sprintf (LABEL, "*%s%s%ld", LOCAL_LABEL_PREFIX, PREFIX, (long) NUM)

/* Node: Initialization */
/* (no definitions) */

/* Node: Macros for Initialization */
/* (no definitions) */

/* Node: Instruction Output */

#define REGISTER_NAMES					\
 {"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8",	\
  "r9", "r10", "r11", "r12", "r13", "sp", "pc", "srp", "mof", "faked_ap", "dccr"}

#define ADDITIONAL_REGISTER_NAMES \
 {{"r14", 14}, {"r15", 15}}

#define PRINT_OPERAND(FILE, X, CODE)		\
 cris_print_operand (FILE, X, CODE)

/* For delay-slot handling.  */
#define PRINT_OPERAND_PUNCT_VALID_P(CODE)	\
 ((CODE) == '#' || (CODE) == '!' || (CODE) == ':')

#define PRINT_OPERAND_ADDRESS(FILE, ADDR)	\
   cris_print_operand_address (FILE, ADDR)

/* Output an empty line to illustrate the presence of the delay slot.  */
#define DBR_OUTPUT_SEQEND(FILE) \
  fprintf (FILE, "\n")

#define LOCAL_LABEL_PREFIX (TARGET_ELF ? "." : "")

/* cppinit.c initializes a const array from this, so it must be constant,
   can't have it different based on options.  Luckily, the prefix is
   always allowed, so let's have it on all GCC-generated code.  Note that
   we have this verbatim everywhere in the back-end, not using %R or %s or
   such.  */
#define REGISTER_PREFIX "$"

/* Remove any previous definition (elfos.h).  */
/* We use -fno-leading-underscore to remove it, when necessary.  */
#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX "_"

#define ASM_OUTPUT_REG_PUSH(FILE, REGNO) \
  fprintf (FILE, "\tpush $%s\n", reg_names[REGNO])

#define ASM_OUTPUT_REG_POP(FILE, REGNO) \
  fprintf (FILE, "\tpop $%s\n", reg_names[REGNO])


/* Node: Dispatch Tables */

#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL)	\
  asm_fprintf (FILE, "\t.word %LL%d-%LL%d\n", VALUE, REL)

#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE)  \
  asm_fprintf (FILE, "\t.dword %LL%d\n", VALUE)

/* Defined to also emit an .align in elfos.h.  We don't want that.  */
#undef ASM_OUTPUT_CASE_LABEL

/* Since the "bound" insn loads the comparison value if the compared<
   value (register) is out of bounds (0..comparison value-1), we need
   to output another case to catch it.
   The way to find it is to look for the label_ref at the else-arm inside
   the expanded casesi core-insn.
   FIXME: Check this construct when changing to new version of gcc.  */
#define ASM_OUTPUT_CASE_END(STREAM, NUM, TABLE)				\
  do									\
    {									\
      asm_fprintf (STREAM, "\t.word %LL%d-%LL%d%s\n",			\
		   CODE_LABEL_NUMBER					\
		    (XEXP (XEXP (XEXP					\
				  (XVECEXP				\
				    (PATTERN				\
				     (prev_nonnote_insn			\
				      (PREV_INSN (TABLE))),		\
				     0, 0), 1), 2), 0)),		\
		   NUM,							\
		   (TARGET_PDEBUG ? "; default" : ""));			\
    }									\
  while (0)


/* Node: Exception Region Output */
/* (no definitions) */
/* FIXME: Fill in with our own optimized layout.  */

/* Node: Alignment Output */

#define ASM_OUTPUT_ALIGN(FILE, LOG)  \
 fprintf (FILE, "\t.align %d\n", (LOG))


/* Node: All Debuggers */

#define DBX_REGISTER_NUMBER(REGNO)				\
 ((REGNO) == CRIS_SRP_REGNUM ? CRIS_CANONICAL_SRP_REGNUM :	\
  (REGNO) == CRIS_MOF_REGNUM ? CRIS_CANONICAL_MOF_REGNUM :	\
  (REGNO) == CRIS_CC0_REGNUM ? CRIS_CANONICAL_CC0_REGNUM :	\
 (REGNO))

/* FIXME: Investigate DEBUGGER_AUTO_OFFSET, DEBUGGER_ARG_OFFSET.  */


/* Node: DBX Options */

/* Is this correct? Check later.  */
#define DBX_NO_XREFS

#define DBX_CONTIN_LENGTH 0

/* FIXME: Is this needed when we have 0 DBX_CONTIN_LENGTH?  */
#define DBX_CONTIN_CHAR '?'


/* Node: DBX Hooks */
/* (no definitions) */

/* Node: File names and DBX */
/* (no definitions) */


/* Node: SDB and DWARF */
/* (no definitions) */

/* Node: Misc */

/* A combination of the bound (umin) insn together with a
   sign-extended add via the table to PC seems optimal.
   If the table overflows, the assembler will take care of it.
   Theoretically, in extreme cases (uncertain if they occur), an error
   will be emitted, so FIXME: Check how large case-tables are emitted,
   possible add an option to emit SImode case-tables.  */
#define CASE_VECTOR_MODE HImode

#define CASE_VECTOR_PC_RELATIVE 1

/* FIXME: Investigate CASE_VECTOR_SHORTEN_MODE to make sure HImode is not
   used when broken-.word could possibly fail (plus testcase).  */

#define FIXUNS_TRUNC_LIKE_FIX_TRUNC

/* This is the number of bytes that can be moved in one
   reasonably fast instruction sequence.  For CRIS, this is two
   instructions: mem => reg, reg => mem.  */
#define MOVE_MAX 4

/* Maybe SHIFT_COUNT_TRUNCATED is safe to define?  FIXME: Check later.  */

#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) 1

#define Pmode SImode

#define FUNCTION_MODE QImode

#define NO_IMPLICIT_EXTERN_C

/*
 * Local variables:
 * eval: (c-set-style "gnu")
 * indent-tabs-mode: t
 * End:
 */

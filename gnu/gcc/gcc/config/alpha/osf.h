/* Definitions of target machine for GNU compiler, for DEC Alpha on OSF/1.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998, 2001, 2002, 2003,
   2004 Free Software Foundation, Inc.
   Contributed by Richard Kenner (kenner@vlsi1.ultra.nyu.edu)

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

/* As of OSF 4.0, as can subtract adjacent labels.  */

#undef TARGET_AS_CAN_SUBTRACT_LABELS
#define TARGET_AS_CAN_SUBTRACT_LABELS 1

/* The GEM libraries for X_float are present, though not used by C.  */

#undef TARGET_HAS_XFLOATING_LIBS
#define TARGET_HAS_XFLOATING_LIBS 1

/* Names to predefine in the preprocessor for this target machine.  */

#define TARGET_OS_CPP_BUILTINS()			\
    do {						\
	builtin_define_std ("unix");			\
	builtin_define_std ("SYSTYPE_BSD");		\
	builtin_define ("_SYSTYPE_BSD");		\
	builtin_define ("__osf__");			\
	builtin_define ("__digital__");			\
	builtin_define ("__arch64__");			\
	builtin_define ("_LONGLONG");			\
	builtin_assert ("system=unix");			\
	builtin_assert ("system=xpg4");			\
	/* Tru64 UNIX V5 has a 16 byte long		\
	   double type and requires __X_FLOAT		\
	   to be defined for <math.h>.  */		\
        if (LONG_DOUBLE_TYPE_SIZE == 128)		\
          builtin_define ("__X_FLOAT");			\
							\
	/* Tru64 UNIX V4/V5 provide several ISO C94	\
	   features protected by the corresponding	\
	   __STDC_VERSION__ macro.  libstdc++ v3	\
	   needs them as well.  */			\
	if (c_dialect_cxx ())				\
	  builtin_define ("__STDC_VERSION__=199409L");	\
    } while (0)

/* Accept DEC C flags for multithreaded programs.  We use _PTHREAD_USE_D4
   instead of PTHREAD_USE_D4 since both have the same effect and the former
   doesn't invade the users' namespace.  */

#undef CPP_SUBTARGET_SPEC
#define CPP_SUBTARGET_SPEC \
"%{pthread|threads:-D_REENTRANT} %{threads:-D_PTHREAD_USE_D4}"

/* Under OSF4, -p and -pg require -lprof1, and -lprof1 requires -lpdf.  */

#define LIB_SPEC \
"%{p|pg:-lprof1%{pthread|threads:_r} -lpdf} %{a:-lprof2} \
 %{threads: -lpthreads} %{pthread|threads: -lpthread -lmach -lexc} -lc"

/* Pass "-G 8" to ld because Alpha's CC does.  Pass -O3 if we are
   optimizing, -O1 if we are not.  Pass -S to silence `weak symbol
   multiply defined' warnings.  Pass -shared, -non_shared or
   -call_shared as appropriate.  Pass -hidden_symbol so that our
   constructor and call-frame data structures are not accidentally
   overridden.  */
#define LINK_SPEC  \
  "-G 8 %{O*:-O3} %{!O*:-O1} -S %{static:-non_shared} \
   %{!static:%{shared:-shared -hidden_symbol _GLOBAL_*} \
   %{!shared:-call_shared}} %{pg} %{taso} %{rpath*}"

#define STARTFILE_SPEC  \
  "%{!shared:%{pg:gcrt0.o%s}%{!pg:%{p:mcrt0.o%s}%{!p:crt0.o%s}}}"

#define ENDFILE_SPEC \
  "%{ffast-math|funsafe-math-optimizations:crtfastmath.o%s}"

#define MD_STARTFILE_PREFIX "/usr/lib/cmplrs/cc/"

/* Tru64 UNIX V5.1 requires a special as flag.  Empty by default.  */

#define ASM_OLDAS_SPEC ""

/* In OSF/1 v3.2c, the assembler by default does not output file names which
   causes mips-tfile to fail.  Passing -g to the assembler fixes this problem.
   ??? Strictly speaking, we need -g only if the user specifies -g.  Passing
   it always means that we get slightly larger than necessary object files
   if the user does not specify -g.  If we don't pass -g, then mips-tfile
   will need to be fixed to work in this case.  Pass -O0 since some
   optimization are broken and don't help us anyway.  Pass -nocpp because
   there's no point in running CPP on our assembler output.  */
#if ((TARGET_DEFAULT | TARGET_CPU_DEFAULT) & MASK_GAS) != 0
#define ASM_SPEC "%{malpha-as:-g %(asm_oldas) -nocpp %{pg} -O0}"
#else
#define ASM_SPEC "%{!mgas:-g %(asm_oldas) -nocpp %{pg} -O0}"
#endif

/* Specify to run a post-processor, mips-tfile after the assembler
   has run to stuff the ecoff debug information into the object file.
   This is needed because the Alpha assembler provides no way
   of specifying such information in the assembly file.  */

#if ((TARGET_DEFAULT | TARGET_CPU_DEFAULT) & MASK_GAS) != 0

#define ASM_FINAL_SPEC "\
%{malpha-as: %{!mno-mips-tfile: \
	\n mips-tfile %{v*: -v} \
		%{K: -I %b.o~} \
		%{!K: %{save-temps: -I %b.o~}} \
		%{c:%W{o*}%{!o*:-o %b.o}}%{!c:-o %U.o} \
		%{.s:%i} %{!.s:%g.s}}}"

#else
#define ASM_FINAL_SPEC "\
%{!mgas: %{!mno-mips-tfile: \
	\n mips-tfile %{v*: -v} \
		%{K: -I %b.o~} \
		%{!K: %{save-temps: -I %b.o~}} \
		%{c:%W{o*}%{!o*:-o %b.o}}%{!c:-o %U.o} \
		%{.s:%i} %{!.s:%g.s}}}"

#endif

#undef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS { "asm_oldas", ASM_OLDAS_SPEC }

/* Indicate that we have a stamp.h to use.  */
#ifndef CROSS_COMPILE
#define HAVE_STAMP_H 1
#endif

/* Attempt to turn on access permissions for the stack.  */

#define ENABLE_EXECUTE_STACK						\
void									\
__enable_execute_stack (void *addr)					\
{									\
  extern int mprotect (const void *, size_t, int);			\
  long size = getpagesize ();						\
  long mask = ~(size-1);						\
  char *page = (char *) (((long) addr) & mask);				\
  char *end  = (char *) ((((long) (addr + TRAMPOLINE_SIZE)) & mask) + size); \
									\
  /* 7 is PROT_READ | PROT_WRITE | PROT_EXEC */				\
  if (mprotect (page, end - page, 7) < 0)				\
    perror ("mprotect of trampoline code");				\
}

/* Digital UNIX V4.0E (1091)/usr/include/sys/types.h 4.3.49.9 1997/08/14 */
#define SIZE_TYPE	"long unsigned int"
#define PTRDIFF_TYPE	"long int"

/* The linker will stick __main into the .init section.  */
#define HAS_INIT_SECTION
#define LD_INIT_SWITCH "-init"
#define LD_FINI_SWITCH "-fini"

/* The linker needs a space after "-o".  This allows -oldstyle_liblookup to
   be passed to ld.  */
#define SWITCHES_NEED_SPACES "o"

/* Select a format to encode pointers in exception handling data.  CODE
   is 0 for data, 1 for code labels, 2 for function pointers.  GLOBAL is
   true if the symbol may be affected by dynamic relocations.
   
   We really ought to be using the SREL32 relocations that ECOFF has,
   but no version of the native assembler supports creating such things,
   and Compaq has no plans to rectify this.  Worse, the dynamic loader
   cannot handle unaligned relocations, so we have to make sure that
   things get padded appropriately.  */
#define ASM_PREFERRED_EH_DATA_FORMAT(CODE,GLOBAL)			     \
  (TARGET_GAS								     \
   ? (((GLOBAL) ? DW_EH_PE_indirect : 0) | DW_EH_PE_pcrel | DW_EH_PE_sdata4) \
   : DW_EH_PE_aligned)

/* This is how we tell the assembler that a symbol is weak.  */

#define ASM_OUTPUT_WEAK_ALIAS(FILE, NAME, VALUE)	\
  do							\
    {							\
      (*targetm.asm_out.globalize_label) (FILE, NAME);  \
      fputs ("\t.weakext\t", FILE);			\
      assemble_name (FILE, NAME);			\
      if (VALUE)					\
        {						\
          fputc (' ', FILE);				\
          assemble_name (FILE, VALUE);			\
        }						\
      fputc ('\n', FILE);				\
    }							\
  while (0)

#define ASM_WEAKEN_LABEL(FILE, NAME) ASM_OUTPUT_WEAK_ALIAS(FILE, NAME, 0)

/* Handle #pragma weak and #pragma pack.  */
#define HANDLE_SYSV_PRAGMA 1

/* Handle #pragma extern_prefix.  Technically only needed for Tru64 5.x,
   but easier to manipulate preprocessor bits from here.  */
#define TARGET_HANDLE_PRAGMA_EXTERN_PREFIX 1

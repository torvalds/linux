/* Definitions of target machine for GNU compiler,
   for IBM RS/6000 POWER running AIX version 4.3.
   Copyright (C) 1998, 1999, 2000, 2001, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by David Edelsohn (edelsohn@gnu.org).

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

/* Sometimes certain combinations of command options do not make sense
   on a particular target machine.  You can define a macro
   `OVERRIDE_OPTIONS' to take account of this.  This macro, if
   defined, is executed once just after all the command options have
   been parsed.

   The macro SUBTARGET_OVERRIDE_OPTIONS is provided for subtargets, to
   get control.  */

#define NON_POWERPC_MASKS (MASK_POWER | MASK_POWER2)
#define SUBTARGET_OVERRIDE_OPTIONS					\
do {									\
  if (TARGET_64BIT && (target_flags & NON_POWERPC_MASKS))		\
    {									\
      target_flags &= ~NON_POWERPC_MASKS;				\
      warning (0, "-maix64 and POWER architecture are incompatible");	\
    }									\
  if (TARGET_64BIT && ! TARGET_POWERPC64)				\
    {									\
      target_flags |= MASK_POWERPC64;					\
      warning (0, "-maix64 requires PowerPC64 architecture remain enabled"); \
    }									\
  if (TARGET_SOFT_FLOAT && TARGET_LONG_DOUBLE_128)			\
    {									\
      rs6000_long_double_type_size = 64;				\
      if (rs6000_explicit_options.long_double)				\
	warning (0, "soft-float and long-double-128 are incompatible");	\
    }									\
  if (TARGET_POWERPC64 && ! TARGET_64BIT)				\
    {									\
      error ("-maix64 required: 64-bit computation with 32-bit addressing not yet supported"); \
    }									\
} while (0);

#undef ASM_SPEC
#define ASM_SPEC "-u %{maix64:-a64 %{!mcpu*:-mppc64}} %(asm_cpu)"

/* Common ASM definitions used by ASM_SPEC amongst the various targets
   for handling -mcpu=xxx switches.  */
#undef ASM_CPU_SPEC
#define ASM_CPU_SPEC \
"%{!mcpu*: %{!maix64: \
  %{mpower: %{!mpower2: -mpwr}} \
  %{mpower2: -mpwr2} \
  %{mpowerpc*: %{!mpowerpc64: -mppc}} \
  %{mpowerpc64: -mppc64} \
  %{!mpower*: %{!mpowerpc*: %(asm_default)}}}} \
%{mcpu=common: -mcom} \
%{mcpu=power: -mpwr} \
%{mcpu=power2: -mpwr2} \
%{mcpu=power3: -m620} \
%{mcpu=power4: -m620} \
%{mcpu=powerpc: -mppc} \
%{mcpu=rios: -mpwr} \
%{mcpu=rios1: -mpwr} \
%{mcpu=rios2: -mpwr2} \
%{mcpu=rsc: -mpwr} \
%{mcpu=rsc1: -mpwr} \
%{mcpu=rs64a: -mppc} \
%{mcpu=601: -m601} \
%{mcpu=602: -mppc} \
%{mcpu=603: -m603} \
%{mcpu=603e: -m603} \
%{mcpu=604: -m604} \
%{mcpu=604e: -m604} \
%{mcpu=620: -m620} \
%{mcpu=630: -m620}"

#undef	ASM_DEFAULT_SPEC
#define ASM_DEFAULT_SPEC "-mcom"

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()     \
  do                                 \
    {                                \
      builtin_define ("_AIX43");     \
      TARGET_OS_AIX_CPP_BUILTINS (); \
    }                                \
  while (0)

#undef CPP_SPEC
#define CPP_SPEC "%{posix: -D_POSIX_SOURCE}\
   %{ansi: -D_ANSI_C_SOURCE}\
   %{maix64: -D__64BIT__}\
   %{mpe: -I/usr/lpp/ppe.poe/include}\
   %{pthread: -D_THREAD_SAFE}"

/* The GNU C++ standard library requires that these macros be 
   defined.  */
#undef CPLUSPLUS_CPP_SPEC			
#define CPLUSPLUS_CPP_SPEC			\
  "-D_ALL_SOURCE				\
   %{maix64: -D__64BIT__}			\
   %{mpe: -I/usr/lpp/ppe.poe/include}		\
   %{pthread: -D_THREAD_SAFE}"

#undef TARGET_DEFAULT
#define TARGET_DEFAULT MASK_NEW_MNEMONICS

#undef PROCESSOR_DEFAULT
#define PROCESSOR_DEFAULT PROCESSOR_PPC604e

/* Define this macro as a C expression for the initializer of an
   array of string to tell the driver program which options are
   defaults for this target and thus do not need to be handled
   specially when using `MULTILIB_OPTIONS'.

   Do not define this macro if `MULTILIB_OPTIONS' is not defined in
   the target makefile fragment or if none of the options listed in
   `MULTILIB_OPTIONS' are set by default.  *Note Target Fragment::.  */

#undef	MULTILIB_DEFAULTS
#define	MULTILIB_DEFAULTS { "mcpu=common" }

#undef LIB_SPEC
#define LIB_SPEC "%{pg:-L/lib/profiled -L/usr/lib/profiled}\
   %{p:-L/lib/profiled -L/usr/lib/profiled}\
   %{!maix64:%{!shared:%{g*:-lg}}}\
   %{mpe:-L/usr/lpp/ppe.poe/lib -lmpi -lvtd}\
   %{pthread:-L/usr/lib/threads -lpthreads -lc_r /usr/lib/libc.a}\
   %{!pthread:-lc}"

#undef LINK_SPEC
#define LINK_SPEC "-bpT:0x10000000 -bpD:0x20000000 %{!r:-btextro} -bnodelcsect\
   %{static:-bnso %(link_syscalls) } %{shared:-bM:SRE %{!e:-bnoentry}}\
   %{!maix64:%{!shared:%{g*: %(link_libg) }}} %{maix64:-b64}\
   %{mpe:-binitfini:poe_remote_main}"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "%{!shared:\
   %{maix64:%{pg:gcrt0_64%O%s}%{!pg:%{p:mcrt0_64%O%s}%{!p:crt0_64%O%s}}}\
   %{!maix64:\
     %{pthread:%{pg:gcrt0_r%O%s}%{!pg:%{p:mcrt0_r%O%s}%{!p:crt0_r%O%s}}}\
     %{!pthread:%{pg:gcrt0%O%s}%{!pg:%{p:mcrt0%O%s}%{!p:crt0%O%s}}}}}"

/* AIX 4.3 typedefs ptrdiff_t as "long" while earlier releases used "int".  */

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "long int"

/* AIX 4 uses PowerPC nop (ori 0,0,0) instruction as call glue for PowerPC
   and "cror 31,31,31" for POWER architecture.  */

#undef RS6000_CALL_GLUE
#define RS6000_CALL_GLUE "{cror 31,31,31|nop}"

/* AIX 4.2 and above provides initialization and finalization function
   support from linker command line.  */
#undef HAS_INIT_SECTION
#define HAS_INIT_SECTION

#undef LD_INIT_SWITCH
#define LD_INIT_SWITCH "-binitfini"

/* The IBM AIX 4.x assembler doesn't support forward references in
   .set directives.  We handle this by deferring the output of .set
   directives to the end of the compilation unit.  */
#define TARGET_DEFERRED_OUTPUT_DEFS(DECL,TARGET) true

/* This target uses the aix64.opt file.  */
#define TARGET_USES_AIX64_OPT 1

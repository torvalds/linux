/* Definitions for ARM running Linux-based GNU systems using ELF
   Copyright (C) 1993, 1994, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004,
   2005, 2006
   Free Software Foundation, Inc.
   Contributed by Philip Blundell <philb@gnu.org>

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
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* elfos.h should have already been included.  Now just override
   any conflicting definitions and add any extras.  */

/* Run-time Target Specification.  */
#undef  TARGET_VERSION
#define TARGET_VERSION  fputs (" (ARM GNU/Linux with ELF)", stderr);

#undef  TARGET_DEFAULT_FLOAT_ABI
#define TARGET_DEFAULT_FLOAT_ABI ARM_FLOAT_ABI_HARD

#undef  TARGET_DEFAULT
#define TARGET_DEFAULT (0)

#define SUBTARGET_CPU_DEFAULT TARGET_CPU_arm6

#define SUBTARGET_EXTRA_LINK_SPEC " -m armelf_linux -p"

#undef  MULTILIB_DEFAULTS
#define MULTILIB_DEFAULTS \
	{ "marm", "mlittle-endian", "mhard-float", "mno-thumb-interwork" }

/* Now we define the strings used to build the spec file.  */
#undef  LIB_SPEC
#define LIB_SPEC \
  "%{pthread:-lpthread} \
   %{shared:-lc} \
   %{!shared:%{profile:-lc_p}%{!profile:-lc}}"

#define LIBGCC_SPEC "%{msoft-float:-lfloat} %{mfloat-abi=soft*:-lfloat} -lgcc"

#define GLIBC_DYNAMIC_LINKER "/lib/ld-linux.so.2"

#define LINUX_TARGET_LINK_SPEC  "%{h*} %{version:-v} \
   %{b} \
   %{static:-Bstatic} \
   %{shared:-shared} \
   %{symbolic:-Bsymbolic} \
   %{rdynamic:-export-dynamic} \
   %{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER "} \
   -X \
   %{mbig-endian:-EB}" \
   SUBTARGET_EXTRA_LINK_SPEC

#undef  LINK_SPEC
#define LINK_SPEC LINUX_TARGET_LINK_SPEC

#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	LINUX_TARGET_OS_CPP_BUILTINS();		\
    }						\
  while (0)

/* This is how we tell the assembler that two symbols have the same value.  */
#define ASM_OUTPUT_DEF(FILE, NAME1, NAME2) \
  do					   \
    {					   \
      assemble_name (FILE, NAME1); 	   \
      fputs (" = ", FILE);		   \
      assemble_name (FILE, NAME2);	   \
      fputc ('\n', FILE);		   \
    }					   \
  while (0)

/* NWFPE always understands FPA instructions.  */
#undef  FPUTYPE_DEFAULT
#define FPUTYPE_DEFAULT FPUTYPE_FPA_EMU3

/* Call the function profiler with a given profile label.  */
#undef  ARM_FUNCTION_PROFILER
#define ARM_FUNCTION_PROFILER(STREAM, LABELNO)  			\
{									\
  fprintf (STREAM, "\tbl\tmcount%s\n",					\
	   (TARGET_ARM && NEED_PLT_RELOC) ? "(PLT)" : "");		\
}

/* The GNU/Linux profiler clobbers the link register.  Make sure the
   prologue knows to save it.  */
#define PROFILE_HOOK(X)						\
  emit_insn (gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (SImode, LR_REGNUM)))

/* The GNU/Linux profiler needs a frame pointer.  */
#define SUBTARGET_FRAME_POINTER_REQUIRED current_function_profile

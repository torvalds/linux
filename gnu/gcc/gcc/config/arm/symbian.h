/* Configuration file for Symbian OS on ARM processors.
   Copyright (C) 2004, 2005
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

/* Do not expand builtin functions (unless explicitly prefixed with
   "__builtin").  Symbian OS code relies on properties of the standard
   library that go beyond those guaranteed by the ANSI/ISO standard.
   For example, "memcpy" works even with overlapping memory, like
   "memmove".  We cannot simply set flag_no_builtin in arm.c because
   (a) flag_no_builtin is not declared in language-independent code,
   and (b) that would prevent users from explicitly overriding the
   default with -fbuiltin, which may sometimes be useful.

   Make all symbols hidden by default.  Symbian OS expects that all
   exported symbols will be explicitly marked with
   "__declspec(dllexport)".  

   Enumeration types use 4 bytes, even if the enumerals are small,
   unless explicitly overridden.

   The wchar_t type is a 2-byte type, unless explicitly
   overridden.  */
#define CC1_SPEC						\
  "%{!fbuiltin:%{!fno-builtin:-fno-builtin}} "			\
  "%{!fvisibility=*:-fvisibility=hidden} "			\
  "%{!fshort-enums:%{!fno-short-enums:-fno-short-enums}} "	\
  "%{!fshort-wchar:%{!fno-short-wchar:-fshort-wchar}} "
#define CC1PLUS_SPEC CC1_SPEC

/* Symbian OS does not use crt*.o, unlike the generic unknown-elf
   configuration.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC ""

#undef ENDFILE_SPEC
#define ENDFILE_SPEC ""

/* Do not link with any libraries by default.  On Symbian OS, the user
   must supply all required libraries on the command line.  */
#undef LIB_SPEC
#define LIB_SPEC ""

/* Support the "dllimport" attribute.  */
#define TARGET_DLLIMPORT_DECL_ATTRIBUTES 1

/* Symbian OS assumes ARM V5 or above.  Since -march=armv5 is
   equivalent to making the ARM 10TDMI core the default, we can set
   SUBTARGET_CPU_DEFAULT and get an equivalent effect.  */
#undef SUBTARGET_CPU_DEFAULT
#define SUBTARGET_CPU_DEFAULT TARGET_CPU_arm10tdmi

/* The assembler should assume VFP FPU format, and armv5t.  */
#undef SUBTARGET_ASM_FLOAT_SPEC
#define SUBTARGET_ASM_FLOAT_SPEC \
  "%{!mfpu=*:-mfpu=vfp} %{!mcpu=*:%{!march=*:-march=armv5t}}"
  
/* SymbianOS provides the BPABI routines in a separate library.
   Therefore, we do not need to define any of them in libgcc.  */
#undef RENAME_LIBRARY
#define RENAME_LIBRARY(GCC_NAME, AEABI_NAME) /* empty */

/* Define the __symbian__ macro.  */
#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      /* Include the default BPABI stuff.  */	\
      TARGET_BPABI_CPP_BUILTINS ();		\
      builtin_define ("__symbian__");		\
    }						\
  while (false)

/* On SymbianOS, these sections are not writable, so we use "a",
   rather than "aw", for the section attributes.  */
#undef ARM_EABI_CTORS_SECTION_OP
#define ARM_EABI_CTORS_SECTION_OP \
  "\t.section\t.init_array,\"a\",%init_array"
#undef ARM_EABI_DTORS_SECTION_OP
#define ARM_EABI_DTORS_SECTION_OP \
  "\t.section\t.fini_array,\"a\",%fini_array"

/* SymbianOS cannot merge entities with vague linkage at runtime.  */
#define TARGET_ARM_DYNAMIC_VAGUE_LINKAGE_P false

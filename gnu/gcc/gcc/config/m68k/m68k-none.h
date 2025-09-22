/* Definitions of target machine for GNU compiler.  "naked" 68020.
   Copyright (C) 1994, 1996, 2003, 2006 Free Software Foundation, Inc.

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

/* Default to m68k (m68020).  */
#ifndef TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT M68K_CPU_m68k
#endif

/* These are values set by the configure script in TARGET_CPU_DEFAULT.
   They are (sequential integer + (desired value for TARGET_DEFAULT) << 4).  */
#define M68K_CPU_m68k	(0 + ((MASK_68020|MASK_68881|MASK_BITFIELD)<<4))
#define M68K_CPU_m68000 (1 + (0 << 4))
#define M68K_CPU_m68010 (1 + (0 << 4)) /* make same as m68000 */
#define M68K_CPU_m68020 (2 + ((MASK_68020|MASK_68881|MASK_BITFIELD) << 4))
#define M68K_CPU_m68030 (3 + ((MASK_68030|MASK_68020|MASK_68881|MASK_BITFIELD) << 4))
#define M68K_CPU_m68040 (4 + ((MASK_68040_ONLY|MASK_68020|MASK_68881|MASK_BITFIELD) << 4))
#define M68K_CPU_m68302 (5 + (0 << 4))
#define M68K_CPU_m68332 (6 + (MASK_68020 << 4))

/* This is tested for below, so if target wants to override this, it
   just set this first in cover file.  */
#ifndef TARGET_DEFAULT
#define TARGET_DEFAULT (TARGET_CPU_DEFAULT >> 4)
#endif

/* Defaults for the various specs below.
   These are collected here so we only test TARGET_CPU_DEFAULT once.  */
/* ??? CC1_CPU_DEFAULT_SPEC was copied over from the earlier version of
   this file.  However, it's not used anywhere here because it doesn't
   seem to be necessary.  */
#if TARGET_CPU_DEFAULT == M68K_CPU_m68k || TARGET_CPU_DEFAULT == M68K_CPU_m68020
#define ASM_CPU_DEFAULT_SPEC "-mc68020"
#define CC1_CPU_DEFAULT_SPEC "-m68020"
#else
#if TARGET_CPU_DEFAULT == M68K_CPU_m68000
#define ASM_CPU_DEFAULT_SPEC "-mc68000"
#define CC1_CPU_DEFAULT_SPEC "-m68000"
#else
#if TARGET_CPU_DEFAULT == M68K_CPU_m68030
#define ASM_CPU_DEFAULT_SPEC "-mc68030"
#define CC1_CPU_DEFAULT_SPEC "-m68030"
#else
#if TARGET_CPU_DEFAULT == M68K_CPU_m68040
#define ASM_CPU_DEFAULT_SPEC "-mc68040"
#define CC1_CPU_DEFAULT_SPEC "-m68040"
#else
#if TARGET_CPU_DEFAULT == M68K_CPU_m68302
#define ASM_CPU_DEFAULT_SPEC "-mc68302"
#define CC1_CPU_DEFAULT_SPEC "-m68302"
#else
#if TARGET_CPU_DEFAULT == M68K_CPU_m68332
#define ASM_CPU_DEFAULT_SPEC "-mc68332"
#define CC1_CPU_DEFAULT_SPEC "-m68332"
#else
Unrecognized value in TARGET_CPU_DEFAULT.
#endif
#endif
#endif
#endif
#endif
#endif

/* Pass flags to gas indicating which type of processor we have.  */

#undef ASM_SPEC
#define ASM_SPEC "\
%{m68851}%{mno-68851}%{m68881}%{mno-68881}%{msoft-float:-mno-68881} %{m68000}%{m68302}%{mc68000}%{m68010}%{m68020}%{mc68020}%{m68030}%{m68040}%{m68020-40:-mc68040} %{m68020-60:-mc68040} %{m68060}%{mcpu32}%{m68332}%{m5200}%{m5206e}%{m528x}%{m5307}%{m5407}%{mcfv4e}%{!mc68000:%{!m68000:%{!m68302:%{!m68010:%{!mc68020:%{!m68020:%{!m68030:%{!m68040:%{!m68020-40:%{!m68020-60:%{!m68060:%{!mcpu32:%{!m68332:%{!m5200:%{!m5206e:%{!m528x:%{!m5307:%{!m5407:%{!mcfv4e:%(asm_cpu_default)}}}}}}}}}}}}}}}}}}} \
%{fPIC:--pcrel} %{fpic:--pcrel} %{msep-data:--pcrel} %{mid-shared-library:--pcrel} \
"

/* cc1/cc1plus always receives all the -m flags. If the specs strings above 
   are consistent with the flags in m68k.opt, there should be no need for
   any further cc1/cc1plus specs.  */

#undef CC1_SPEC
#define CC1_SPEC ""

/* This macro defines names of additional specifications to put in the specs
   that can be used in various specifications like CC1_SPEC.  Its definition
   is an initializer with a subgrouping for each command option.

   Each subgrouping contains a string constant, that defines the
   specification name, and a string constant that used by the GCC driver
   program.

   Do not define this macro if it does not need to do anything.  */

#define EXTRA_SPECS					\
  { "asm_cpu_default",	ASM_CPU_DEFAULT_SPEC },		\
  { "cc1_cpu_default",	CC1_CPU_DEFAULT_SPEC },		\
  SUBTARGET_EXTRA_SPECS

#define CPP_SUBTARGET_SPEC ""
#define SUBTARGET_EXTRA_SPECS

/* Avoid building multilib libraries for the defaults.
   For targets not handled here, just build the full set of multilibs.
   The default is m68k 99.9% of the time anyway.  */

#if TARGET_CPU_DEFAULT == M68K_CPU_m68k || TARGET_CPU_DEFAULT == M68K_CPU_m68020
#if TARGET_DEFAULT & MASK_68881
#define MULTILIB_DEFAULTS { "m68020", "m68881" }
#else
#define MULTILIB_DEFAULTS { "m68020", "msoft-float" }
#endif
#endif

#if TARGET_CPU_DEFAULT == M68K_CPU_m68000 || TARGET_CPU_DEFAULT == M68K_CPU_m68302
#if TARGET_DEFAULT & MASK_68881
#define MULTILIB_DEFAULTS { "m68000", "m68881" }
#else
#define MULTILIB_DEFAULTS { "m68000", "msoft-float" }
#endif
#endif

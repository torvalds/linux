/* Core target definitions for GNU compiler
   for IBM RS/6000 PowerPC targeted to embedded ELF systems.
   Copyright (C) 1995, 1996, 2000, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Cygnus Support.

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

/* Add -meabi to target flags.  */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT (MASK_POWERPC | MASK_NEW_MNEMONICS | MASK_EABI)

/* Invoke an initializer function to set up the GOT.  */
#define NAME__MAIN "__eabi"
#define INVOKE__main

#undef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (PowerPC Embedded)");

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()          \
  do                                      \
    {                                     \
      builtin_define_std ("PPC");         \
      builtin_define ("__embedded__");    \
      builtin_assert ("system=embedded"); \
      builtin_assert ("cpu=powerpc");     \
      builtin_assert ("machine=powerpc"); \
      TARGET_OS_SYSV_CPP_BUILTINS ();     \
    }                                     \
  while (0)

#undef TARGET_SPE_ABI
#undef TARGET_SPE
#undef TARGET_E500
#undef TARGET_ISEL
#undef TARGET_FPRS
#undef TARGET_E500_SINGLE
#undef TARGET_E500_DOUBLE

#define TARGET_SPE_ABI rs6000_spe_abi
#define TARGET_SPE rs6000_spe
#define TARGET_E500 (rs6000_cpu == PROCESSOR_PPC8540)
#define TARGET_ISEL rs6000_isel
#define TARGET_FPRS (rs6000_float_gprs == 0)
#define TARGET_E500_SINGLE (TARGET_HARD_FLOAT && rs6000_float_gprs == 1)
#define TARGET_E500_DOUBLE (TARGET_HARD_FLOAT && rs6000_float_gprs == 2)

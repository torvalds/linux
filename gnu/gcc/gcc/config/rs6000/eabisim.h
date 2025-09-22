/* Support for GCC on simulated PowerPC systems targeted to embedded ELF
   systems.
   Copyright (C) 1995, 1996, 2000, 2003 Free Software Foundation, Inc.
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

#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (PowerPC Simulated)");

#undef  TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()           \
  do                                       \
    {                                      \
      builtin_define_std ("PPC");          \
      builtin_define ("__embedded__");     \
      builtin_define ("__simulator__");    \
      builtin_assert ("system=embedded");  \
      builtin_assert ("system=simulator"); \
      builtin_assert ("cpu=powerpc");      \
      builtin_assert ("machine=powerpc");  \
      TARGET_OS_SYSV_CPP_BUILTINS ();      \
    }                                      \
  while (0)

/* Make the simulator the default */
#undef	LIB_DEFAULT_SPEC
#define LIB_DEFAULT_SPEC "%(lib_sim)"

#undef	STARTFILE_DEFAULT_SPEC
#define STARTFILE_DEFAULT_SPEC "%(startfile_sim)"

#undef	ENDFILE_DEFAULT_SPEC
#define ENDFILE_DEFAULT_SPEC "%(endfile_sim)"

#undef	LINK_START_DEFAULT_SPEC
#define LINK_START_DEFAULT_SPEC "%(link_start_sim)"

#undef	LINK_OS_DEFAULT_SPEC
#define LINK_OS_DEFAULT_SPEC "%(link_os_sim)"

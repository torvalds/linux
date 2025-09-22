/* Configuration common to all targets running RTEMS. 
   Copyright (C) 2000, 2002, 2004 Free Software Foundation, Inc. 

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

/* The system headers under RTEMS are C++-aware.  */
#define NO_IMPLICIT_EXTERN_C

/*
 * Dummy start/end specification to let linker work as
 * needed by autoconf scripts using this compiler.
 */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC "crt0.o%s"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC   ""

/*
 * Some targets do not set up LIB_SPECS, override it, here.
 */
#define STD_LIB_SPEC "%{!shared:%{g*:-lg} %{!p:%{!pg:-lc}}%{p:-lc_p}%{pg:-lc_p}}"

#undef LIB_SPEC
#define LIB_SPEC "%{!qrtems: " STD_LIB_SPEC "} " \
"%{!nostdlib: %{qrtems: --start-group \
 %{!qrtems_debug: -lrtemsbsp -lrtemscpu} \
 %{qrtems_debug: -lrtemsbsp_g -lrtemscpu_g} \
 -lc -lgcc --end-group %{!qnolinkcmds: -T linkcmds%s}}}"

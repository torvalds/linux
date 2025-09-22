/* Definitions of target machine for GCC,
   for SuperH with targeting the VXWorks run time environment. 
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC.
   
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


#define TARGET_OS_CPP_BUILTINS()	\
  do {					\
    builtin_define ("__vxworks");	\
    builtin_define ("CPU=SH7000");		\
  } while (0)

/* VxWorks does all the library stuff itself.  */
#undef  LIB_SPEC
#define LIB_SPEC 	""

/* VxWorks uses object files, not loadable images.  Make the linker just
   combine objects.  */
#undef  LINK_SPEC
#define LINK_SPEC 	"-r"

/* VxWorks provides the functionality of crt0.o and friends itself.  */
#undef  STARTFILE_SPEC
#define STARTFILE_SPEC 	""

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC 	""

#undef  TARGET_VERSION
#define TARGET_VERSION	fputs (" (SH/VxWorks)", stderr);

/* There is no default multilib.  */
#undef MULTILIB_DEFAULTS

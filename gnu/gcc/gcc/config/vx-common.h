/* Target-independent configuration for VxWorks and VxWorks AE.   
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by by CodeSourcery, LLC.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* VxWorks headers are C++-aware.  */
#undef  NO_IMPLICIT_EXTERN_C
#define NO_IMPLICIT_EXTERN_C

/* Most of these will probably be overridden by subsequent headers.  We
   undefine them here just in case, and define VXWORKS_ versions of each,
   to be used in port-specific vxworks.h.  */
#undef LIB_SPEC
#undef LINK_SPEC
#undef LIBGCC_SPEC
#define LIBGCC_SPEC VXWORKS_LIBGCC_SPEC
#undef STARTFILE_SPEC
#undef ENDFILE_SPEC

/* Most of these macros are overridden in "config/vxworks.h" or
   "config/vxworksae.h" and are here merely for documentation
   purposes.  */
#define VXWORKS_ADDITIONAL_CPP_SPEC ""
#define	VXWORKS_LIB_SPEC ""
#define VXWORKS_LINK_SPEC ""
#define VXWORKS_LIBGCC_SPEC ""
#define	VXWORKS_STARTFILE_SPEC ""
#define VXWORKS_ENDFILE_SPEC ""

/* VxWorks cannot have dots in constructor labels, because it uses a
   mutant variation of collect2 that generates C code instead of
   assembly.  Thus each constructor label must be a legitimate C
   symbol.  FIXME: Have VxWorks use real collect2 instead.  */
#undef NO_DOLLAR_IN_LABEL
#define NO_DOT_IN_LABEL

/* VxWorks uses wchar_t == unsigned short (UCS2) on all architectures.  */
#undef WCHAR_TYPE
#define WCHAR_TYPE "short unsigned int"
#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 16

/* Dwarf2 unwind info is not supported.  */
#define DWARF2_UNWIND_INFO 0

/* VxWorks uses DWARF2.  */
#define DWARF2_DEBUGGING_INFO 1
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

/* None of these other formats is supported.  */
#undef DWARF_DEBUGGING_INFO
#undef DBX_DEBUGGING_INFO
#undef SDB_DEBUGGING_INFO
#undef XCOFF_DEBUGGING_INFO
#undef VMS_DEBUGGING_INFO

/* Kernel mode doesn't have ctors/dtors, but RTP mode does.  */
#define TARGET_HAVE_CTORS_DTORS false
#define VXWORKS_OVERRIDE_OPTIONS /* empty */

/* No math library needed.  */
#define MATH_LIBRARY ""

/* No profiling.  */
#define VXWORKS_FUNCTION_PROFILER(FILE, LABELNO) do	\
{							\
  sorry ("profiler support for VxWorks");		\
} while (0)

/* We occasionally need to distinguish between the VxWorks variants.  */
#define VXWORKS_KIND_NORMAL  1
#define VXWORKS_KIND_AE      2

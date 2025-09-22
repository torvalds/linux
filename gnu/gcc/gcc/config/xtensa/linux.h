/* Xtensa Linux configuration.
   Derived from the configuration for GCC for Intel i386 running Linux.
   Copyright (C) 2001, 2002, 2003, 2006 Free Software Foundation, Inc.

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

#define TARGET_OS_CPP_BUILTINS() LINUX_TARGET_OS_CPP_BUILTINS()

#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC "%{posix:-D_POSIX_SOURCE} %{pthread:-D_REENTRANT}"

#undef TARGET_VERSION
#define TARGET_VERSION fputs (" (Xtensa GNU/Linux with ELF)", stderr);

#undef WCHAR_TYPE
#define WCHAR_TYPE "long int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

#undef ASM_SPEC
#define ASM_SPEC \
 "%{v} \
  %{mtext-section-literals:--text-section-literals} \
  %{mno-text-section-literals:--no-text-section-literals} \
  %{mtarget-align:--target-align} \
  %{mno-target-align:--no-target-align} \
  %{mlongcalls:--longcalls} \
  %{mno-longcalls:--no-longcalls}"

#define GLIBC_DYNAMIC_LINKER "/lib/ld.so.1"

#undef LINK_SPEC
#define LINK_SPEC \
 "%{shared:-shared} \
  %{!shared: \
    %{!ibcs: \
      %{!static: \
        %{rdynamic:-export-dynamic} \
        %{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER "}} \
      %{static:-static}}}"

#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX	"."

/* Always enable "-fpic" for Xtensa Linux.  */
#define XTENSA_ALWAYS_PIC 1

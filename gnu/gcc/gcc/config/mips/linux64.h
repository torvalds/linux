/* Definitions for MIPS running Linux-based GNU systems with ELF format
   using n32/64 abi.
   Copyright 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

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

/* Force the default endianness and ABI flags onto the command line
   in order to make the other specs easier to write.  */
#define DRIVER_SELF_SPECS \
"%{!EB:%{!EL:%(endian_spec)}}", \
"%{!mabi=*: -mabi=n32}"

#undef SUBTARGET_ASM_SPEC
#define SUBTARGET_ASM_SPEC "\
%{!fno-PIC:%{!fno-pic:-KPIC}} \
%{fno-PIC:-non_shared} %{fno-pic:-non_shared}"

#undef LIB_SPEC
#define LIB_SPEC "\
%{shared: -lc} \
%{!shared: %{pthread:-lpthread} \
  %{profile:-lc_p} %{!profile: -lc}}"

#define GLIBC_DYNAMIC_LINKER32 "/lib/ld.so.1"
#define GLIBC_DYNAMIC_LINKER64 "/lib64/ld.so.1"
#define GLIBC_DYNAMIC_LINKERN32 "/lib32/ld.so.1"
#define UCLIBC_DYNAMIC_LINKERN32 "/lib32/ld-uClibc.so.0"
#define LINUX_DYNAMIC_LINKERN32 \
  CHOOSE_DYNAMIC_LINKER (GLIBC_DYNAMIC_LINKERN32, UCLIBC_DYNAMIC_LINKERN32)

#undef LINK_SPEC
#define LINK_SPEC "\
%{G*} %{EB} %{EL} %{mips1} %{mips2} %{mips3} %{mips4} \
%{bestGnum} %{shared} %{non_shared} \
%{call_shared} %{no_archive} %{exact_version} \
 %(endian_spec) \
  %{!shared: \
    %{!ibcs: \
      %{!static: \
        %{rdynamic:-export-dynamic} \
        %{!dynamic-linker: \
	  %{mabi=n32: -dynamic-linker " LINUX_DYNAMIC_LINKERN32 "} \
	  %{mabi=64: -dynamic-linker " LINUX_DYNAMIC_LINKER64 "} \
	  %{mabi=32: -dynamic-linker " LINUX_DYNAMIC_LINKER32 "}}} \
      %{static:-static}}} \
%{mabi=n32:-melf32%{EB:b}%{EL:l}tsmipn32} \
%{mabi=64:-melf64%{EB:b}%{EL:l}tsmip} \
%{mabi=32:-melf32%{EB:b}%{EL:l}tsmip}"

#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX (TARGET_OLDABI ? "$" : ".")

/* GNU/Linux doesn't use the same floating-point format that IRIX uses
   for long double.  There's no need to override this here, since
   ieee_quad_format is the default, but let's put this here to make
   sure nobody thinks we just forgot to set it to something else.  */
#define MIPS_TFMODE_FORMAT mips_quad_format

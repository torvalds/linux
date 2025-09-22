/* Definitions for AMD x86-64 running Linux-based GNU systems with ELF format.
   Copyright (C) 2001, 2002, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Jan Hubicka <jh@suse.cz>, based on linux.h.

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

#define TARGET_VERSION fprintf (stderr, " (x86-64 Linux/ELF)");

#define TARGET_OS_CPP_BUILTINS()				\
  do								\
    {								\
	LINUX_TARGET_OS_CPP_BUILTINS();				\
    }								\
  while (0)

#undef CPP_SPEC
#define CPP_SPEC "%{posix:-D_POSIX_SOURCE} %{pthread:-D_REENTRANT}"

/* The svr4 ABI for the i386 says that records and unions are returned
   in memory.  In the 64bit compilation we will turn this flag off in
   override_options, as we never do pcc_struct_return scheme on this target.  */
#undef DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 1

/* We arrange for the whole %fs segment to map the tls area.  */
#undef TARGET_TLS_DIRECT_SEG_REFS_DEFAULT
#define TARGET_TLS_DIRECT_SEG_REFS_DEFAULT MASK_TLS_DIRECT_SEG_REFS

/* Provide a LINK_SPEC.  Here we provide support for the special GCC
   options -static and -shared, which allow us to link things in one
   of these three modes by applying the appropriate combinations of
   options at link-time.

   When the -shared link option is used a final link is not being
   done.  */

#define GLIBC_DYNAMIC_LINKER32 "/lib/ld-linux.so.2"
#define GLIBC_DYNAMIC_LINKER64 "/lib64/ld-linux-x86-64.so.2"

#undef	LINK_SPEC
#define LINK_SPEC "%{!m32:-m elf_x86_64} %{m32:-m elf_i386} \
  %{shared:-shared} \
  %{!shared: \
    %{!static: \
      %{rdynamic:-export-dynamic} \
      %{m32:%{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER32 "}} \
      %{!m32:%{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER64 "}}} \
    %{static:-static}}"

/* Similar to standard Linux, but adding -ffast-math support.  */
#undef  ENDFILE_SPEC
#define ENDFILE_SPEC \
  "%{ffast-math|funsafe-math-optimizations:crtfastmath.o%s} \
   %{shared|pie:crtendS.o%s;:crtend.o%s} crtn.o%s"

#define MULTILIB_DEFAULTS { "m64" }

#undef NEED_INDICATE_EXEC_STACK
#define NEED_INDICATE_EXEC_STACK 1

#define MD_UNWIND_SUPPORT "config/i386/linux-unwind.h"

/* This macro may be overridden in i386/k*bsd-gnu.h.  */
#define REG_NAME(reg) reg

#ifdef TARGET_LIBC_PROVIDES_SSP
/* i386 glibc provides __stack_chk_guard in %gs:0x14,
   x86_64 glibc provides it in %fs:0x28.  */
#define TARGET_THREAD_SSP_OFFSET	(TARGET_64BIT ? 0x28 : 0x14)
#endif

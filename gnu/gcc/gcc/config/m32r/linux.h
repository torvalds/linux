/* Definitions for Renesas M32R running Linux-based GNU systems using ELF.
   Copyright (C) 2003, 2004, 2006 Free Software Foundation, Inc.

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

#define LINUX_DEFAULT_ELF

/* A lie, I guess, but the general idea behind linux/ELF is that we are
   supposed to be outputting something that will assemble under SVr4.
   This gets us pretty close.  */

#define HANDLE_SYSV_PRAGMA

#undef  HANDLE_PRAGMA_PACK

#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (M32R GNU/Linux with ELF)");

#undef  SIZE_TYPE
#define SIZE_TYPE "unsigned int"
 
#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"
  
#undef  WCHAR_TYPE
#define WCHAR_TYPE "long int"
   
#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE BITS_PER_WORD
    
/* Provide a LINK_SPEC appropriate for Linux.  Here we provide support
   for the special GCC options -static and -shared, which allow us to
   link things in one of these three modes by applying the appropriate
   combinations of options at link-time. We like to support here for
   as many of the other GNU linker options as possible. But I don't
   have the time to search for those flags. I am sure how to add
   support for -soname shared_object_name. H.J.

   I took out %{v:%{!V:-V}}. It is too much :-(. They can use
   -Wl,-V.

   When the -shared link option is used a final link is not being
   done.  */

/* If ELF is the default format, we should not use /lib/elf.  */

#define GLIBC_DYNAMIC_LINKER "/lib/ld-linux.so.2"

#undef	LINK_SPEC
#if TARGET_LITTLE_ENDIAN
#define LINK_SPEC "%(link_cpu) -m m32rlelf_linux %{shared:-shared} \
  %{!shared: \
    %{!ibcs: \
      %{!static: \
	%{rdynamic:-export-dynamic} \
	%{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER "}} \
	%{static:-static}}}"
#else
#define LINK_SPEC "%(link_cpu) -m m32relf_linux %{shared:-shared} \
  %{!shared: \
    %{!ibcs: \
      %{!static: \
	%{rdynamic:-export-dynamic} \
	%{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER "}} \
	%{static:-static}}}"
#endif

#undef	LIB_SPEC
#define LIB_SPEC \
  "%{shared: -lc} \
    %{!shared: %{mieee-fp:-lieee} %{pthread:-lpthread} \
    %{profile:-lc_p} %{!profile: -lc}}"

#undef  STARTFILE_SPEC
#if defined HAVE_LD_PIE
#define STARTFILE_SPEC \
  "%{!shared: %{pg|p|profile:gcrt1.o%s;pie:Scrt1.o%s;:crt1.o%s}} \
   crti.o%s %{static:crtbeginT.o%s;shared|pie:crtbeginS.o%s;:crtbegin.o%s}"
#else
#define STARTFILE_SPEC \
  "%{!shared: \
     %{pg:gcrt1.o%s} %{!pg:%{p:gcrt1.o%s} %{!p:crt1.o%s}}}\
   crti.o%s %{!shared:crtbegin.o%s} %{shared:crtbeginS.o%s}"
#endif

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC \
  "%{shared|pie:crtendS.o%s;:crtend.o%s} crtn.o%s"

#undef  SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC "\
   %{posix:-D_POSIX_SOURCE} \
   %{pthread:-D_REENTRANT -D_PTHREADS} \
"
                                                                                
#define TARGET_OS_CPP_BUILTINS() LINUX_TARGET_OS_CPP_BUILTINS()

#define TARGET_ASM_FILE_END file_end_indicate_exec_stack

/* Operating system specific defines to be used when targeting GCC for
   hosting on Windows32, using a Unix style C library and tools.
   Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003
   Free Software Foundation, Inc.

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

#define TARGET_VERSION fprintf (stderr, " (x86 Cygwin)");

#define EXTRA_OS_CPP_BUILTINS()  /* Nothing.  */

#undef CPP_SPEC
#define CPP_SPEC "%(cpp_cpu) %{posix:-D_POSIX_SOURCE} \
  %{mno-win32:%{mno-cygwin: %emno-cygwin and mno-win32 are not compatible}} \
  %{mno-cygwin:-D__MSVCRT__ -D__MINGW32__ %{!ansi:%{mthreads:-D_MT}}}\
  %{!mno-cygwin:-D__CYGWIN32__ -D__CYGWIN__ %{!ansi:-Dunix} -D__unix__ -D__unix }\
  %{mwin32|mno-cygwin:-DWIN32 -D_WIN32 -D__WIN32 -D__WIN32__ %{!ansi:-DWINNT}}\
  %{!nostdinc:%{!mno-win32|mno-cygwin:-idirafter ../include/w32api%s -idirafter ../../include/w32api%s}}\
"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "\
  %{shared|mdll: %{mno-cygwin:dllcrt2%O%s}}\
  %{!shared: %{!mdll: %{!mno-cygwin:crt0%O%s} %{mno-cygwin:crt2%O%s}\
  %{pg:gcrt0%O%s}}}\
"

/* Normally, -lgcc is not needed since everything in it is in the DLL, but we
   want to allow things to be added to it when installing new versions of
   GCC without making a new CYGWIN.DLL, so we leave it.  Profiling is handled
   by calling the init function from main.  */

#undef LIBGCC_SPEC
#define LIBGCC_SPEC \
  "%{mno-cygwin: %{mthreads:-lmingwthrd} -lmingw32} -lgcc	\
   %{mno-cygwin:-lmoldname -lmingwex -lmsvcrt}"

/* We have to dynamic link to get to the system DLLs.  All of libc, libm and
   the Unix stuff is in cygwin.dll.  The import library is called
   'libcygwin.a'.  For Windows applications, include more libraries, but
   always include kernel32.  We'd like to specific subsystem windows to
   ld, but that doesn't work just yet.  */

#undef LIB_SPEC
#define LIB_SPEC "\
  %{pg:-lgmon} \
  %{!mno-cygwin:-lcygwin} \
  %{mno-cygwin:%{mthreads:-lmingwthrd} -lmingw32} \
  %{mwindows:-lgdi32 -lcomdlg32} \
  -luser32 -lkernel32 -ladvapi32 -lshell32"

#define LINK_SPEC "\
  %{mwindows:--subsystem windows} \
  %{mconsole:--subsystem console} \
  %{shared: %{mdll: %eshared and mdll are not compatible}} \
  %{shared: --shared} %{mdll:--dll} \
  %{static:-Bstatic} %{!static:-Bdynamic} \
  %{shared|mdll: -e \
    %{mno-cygwin:_DllMainCRTStartup@12} \
    %{!mno-cygwin:__cygwin_dll_entry@12}}\
  %{!mno-cygwin:--dll-search-prefix=cyg}"

/* Allocate space for all of the machine-spec-specific stuff.
   Allocate enough space for cygwin -> mingw32  munging plus
   possible addition of "/mingw".  */

#ifndef CYGWIN_MINGW_SUBDIR
#define CYGWIN_MINGW_SUBDIR "/mingw"
#endif
#define CYGWIN_MINGW_SUBDIR_LEN (sizeof (CYGWIN_MINGW_SUBDIR) - 1)

#ifdef GPLUSPLUS_INCLUDE_DIR
char cygwin_gplusplus_include_dir[sizeof (GPLUSPLUS_INCLUDE_DIR) + 1
				  + (CYGWIN_MINGW_SUBDIR_LEN)]
  = GPLUSPLUS_INCLUDE_DIR;
#undef GPLUSPLUS_INCLUDE_DIR
#define GPLUSPLUS_INCLUDE_DIR ((const char *) cygwin_gplusplus_include_dir)
#ifndef GEN_CVT_ARRAY
#define GEN_CVT_ARRAY
#endif
#endif

#ifdef GPLUSPLUS_TOOL_INCLUDE_DIR
char cygwin_gplusplus_tool_include_dir[sizeof (GPLUSPLUS_TOOL_INCLUDE_DIR) + 1
				       + CYGWIN_MINGW_SUBDIR_LEN]
  = GPLUSPLUS_TOOL_INCLUDE_DIR;
#undef GPLUSPLUS_TOOL_INCLUDE_DIR
#define GPLUSPLUS_TOOL_INCLUDE_DIR ((const char *) cygwin_gplusplus_tool_include_dir)
#ifndef GEN_CVT_ARRAY
#define GEN_CVT_ARRAY
#endif
#endif

#ifdef GPLUSPLUS_BACKWARD_INCLUDE_DIR
char cygwin_gplusplus_backward_include_dir[sizeof (GPLUSPLUS_BACKWARD_INCLUDE_DIR)  + 1
					   + CYGWIN_MINGW_SUBDIR_LEN]
  = GPLUSPLUS_BACKWARD_INCLUDE_DIR;
#undef GPLUSPLUS_BACKWARD_INCLUDE_DIR
#define GPLUSPLUS_BACKWARD_INCLUDE_DIR ((const char *) cygwin_gplusplus_backward_include_dir)
#ifndef GEN_CVT_ARRAY
#define GEN_CVT_ARRAY
#endif
#endif

#ifdef LOCAL_INCLUDE_DIR
char cygwin_local_include_dir[sizeof (LOCAL_INCLUDE_DIR)  + 1
			      + CYGWIN_MINGW_SUBDIR_LEN]
  = LOCAL_INCLUDE_DIR;
#undef LOCAL_INCLUDE_DIR
#define LOCAL_INCLUDE_DIR ((const char *) cygwin_local_include_dir)
#ifndef GEN_CVT_ARRAY
#define GEN_CVT_ARRAY
#endif
#endif

#ifdef CROSS_INCLUDE_DIR
char cygwin_cross_include_dir[sizeof (CROSS_INCLUDE_DIR) + 1
			      + CYGWIN_MINGW_SUBDIR_LEN]
  = CROSS_INCLUDE_DIR;
#undef CROSS_INCLUDE_DIR
#define CROSS_INCLUDE_DIR ((const char *) cygwin_cross_include_dir)
#ifndef GEN_CVT_ARRAY
#define GEN_CVT_ARRAY
#endif
#endif

#ifdef TOOL_INCLUDE_DIR
char cygwin_tool_include_dir[sizeof (TOOL_INCLUDE_DIR) + 1
			     + CYGWIN_MINGW_SUBDIR_LEN]
  = TOOL_INCLUDE_DIR;
#undef TOOL_INCLUDE_DIR
#define TOOL_INCLUDE_DIR ((const char *) cygwin_tool_include_dir)

#ifndef CROSS_COMPILE
#undef STANDARD_INCLUDE_DIR
#define STANDARD_INCLUDE_DIR "/usr/include"
char cygwin_standard_include_dir[sizeof (STANDARD_INCLUDE_DIR) + 1
				 + CYGWIN_MINGW_SUBDIR_LEN]
  = STANDARD_INCLUDE_DIR;
#undef STANDARD_INCLUDE_DIR
#define STANDARD_INCLUDE_DIR ((const char *) cygwin_standard_include_dir)
#endif

#ifndef GEN_CVT_ARRAY
#define GEN_CVT_ARRAY
#endif
#endif

#ifndef GEN_CVT_ARRAY
extern char *cvt_to_mingw[];
#else
char *cvt_to_mingw[] =
  {
#ifdef GPLUSPLUS_INCLUDE_DIR
    cygwin_gplusplus_include_dir,
#endif

#ifdef GPLUSPLUS_TOOL_INCLUDE_DIR
    cygwin_gplusplus_tool_include_dir,
#endif

#ifdef GPLUSPLUS_BACKWARD_INCLUDE_DIR
    cygwin_gplusplus_backward_include_dir,
#endif

#ifdef LOCAL_INCLUDE_DIR
    cygwin_local_include_dir,
#endif

#ifdef CROSS_INCLUDE_DIR
    cygwin_cross_include_dir,
#endif

#ifdef TOOL_INCLUDE_DIR
    cygwin_tool_include_dir,
#endif

#ifdef STANDARD_INCLUDE_DIR
    cygwin_standard_include_dir,
#endif

    NULL
  };
#undef GEN_CVT_ARRAY
#endif /*GEN_CVT_ARRAY*/

void mingw_scan (int, const char * const *, char **);
#if 1
#define GCC_DRIVER_HOST_INITIALIZATION \
do \
{ \
  mingw_scan(argc, (const char * const *) argv, (char **) &spec_machine); \
  } \
while (0)
#else
#define GCC_DRIVER_HOST_INITIALIZATION \
do \
{ \
  char *cprefix = concat (tooldir_base_prefix, spec_machine, \
			  dir_separator_str, NULL); \
  if (!IS_ABSOLUTE_PATH (cprefix)) \
    cprefix = concat (standard_exec_prefix, spec_machine, dir_separator_str, \
		      spec_version, dir_separator_str, tooldir_prefix, NULL); \
  add_prefix (&exec_prefixes,\
	      concat (cprefix, "../../../../", spec_machine, "/bin/", NULL), \
	      "BINUTILS", PREFIX_PRIORITY_LAST, 0, NULL); \
  add_prefix (&exec_prefixes, cprefix, \
	      "BINUTILS", PREFIX_PRIORITY_LAST, 0, NULL); \
  add_prefix (&startfile_prefixes,\
	      concat (standard_startfile_prefix, "w32api", NULL),\
	      "GCC", PREFIX_PRIORITY_LAST, 0, NULL);\
  mingw_scan(argc, (const char * const *) argv, &spec_machine); \
  } \
while (0)
#endif

/* Binutils does not handle weak symbols from dlls correctly.  For now,
   do not use them unnecessarily in gthr-posix.h.  */
#define GTHREAD_USE_WEAK 0

/* Every program on cygwin links against cygwin1.dll which contains 
   the pthread routines.  There is no need to explicitly link them
   and the -pthread flag is not recognized.  */
#undef GOMP_SELF_SPECS
#define GOMP_SELF_SPECS ""

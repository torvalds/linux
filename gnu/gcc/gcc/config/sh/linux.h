/* Definitions for SH running Linux-based GNU systems using ELF
   Copyright (C) 1999, 2000, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Kazumoto Kojima <kkojima@rr.iij4u.or.jp>

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
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* Run-time Target Specification.  */
#undef TARGET_VERSION
#define TARGET_VERSION  fputs (" (SH GNU/Linux with ELF)", stderr);

/* Enable DWARF 2 exceptions.  */
#undef DWARF2_UNWIND_INFO
#define DWARF2_UNWIND_INFO 1

#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC "\
   %{posix:-D_POSIX_SOURCE} \
   %{pthread:-D_REENTRANT -D_PTHREADS} \
"

#define TARGET_OS_CPP_BUILTINS() \
  do						\
    {						\
      LINUX_TARGET_OS_CPP_BUILTINS();		\
    }						\
  while (0)

#undef TARGET_DEFAULT
#define TARGET_DEFAULT \
  (TARGET_CPU_DEFAULT | MASK_USERMODE | TARGET_ENDIAN_DEFAULT \
   | TARGET_OPT_DEFAULT)

#define TARGET_ASM_FILE_END file_end_indicate_exec_stack

#define GLIBC_DYNAMIC_LINKER "/lib/ld-linux.so.2"

#undef SUBTARGET_LINK_EMUL_SUFFIX
#define SUBTARGET_LINK_EMUL_SUFFIX "_linux"
#undef SUBTARGET_LINK_SPEC
#define SUBTARGET_LINK_SPEC \
  "%{shared:-shared} \
   %{!static: \
     %{rdynamic:-export-dynamic} \
     %{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER "}} \
   %{static:-static}"

/* Output assembler code to STREAM to call the profiler.  */

#undef FUNCTION_PROFILER
#define FUNCTION_PROFILER(STREAM,LABELNO)				\
  do {									\
    if (TARGET_SHMEDIA)							\
      {									\
	fprintf (STREAM, "\tpt\t1f,tr1\n");				\
	fprintf (STREAM, "\taddi.l\tr15,-8,r15\n");			\
	fprintf (STREAM, "\tst.l\tr15,0,r18\n");			\
	if (flag_pic)							\
	  {								\
	    const char *gofs = "(datalabel _GLOBAL_OFFSET_TABLE_-(0f-.))"; \
	    fprintf (STREAM, "\tmovi\t((%s>>16)&0xffff),r21\n", gofs);	\
	    fprintf (STREAM, "\tshori\t(%s & 0xffff),r21\n", gofs);	\
	    fprintf (STREAM, "0:\tptrel/u\tr21,tr0\n");			\
	    fprintf (STREAM, "\tmovi\t((mcount@GOTPLT)&0xffff),r22\n");	\
	    fprintf (STREAM, "\tgettr\ttr0,r21\n");			\
	    fprintf (STREAM, "\tadd.l\tr21,r22,r21\n");			\
	    fprintf (STREAM, "\tld.l\tr21,0,r21\n");			\
	    fprintf (STREAM, "\tptabs\tr21,tr0\n");			\
	  }								\
	else								\
	  fprintf (STREAM, "\tpt\tmcount,tr0\n");			\
	fprintf (STREAM, "\tgettr\ttr1,r18\n");				\
	fprintf (STREAM, "\tblink\ttr0,r63\n");				\
	fprintf (STREAM, "1:\tld.l\tr15,0,r18\n");			\
	fprintf (STREAM, "\taddi.l\tr15,8,r15\n");			\
      }									\
    else								\
      {									\
	if (flag_pic)							\
	  {								\
	    fprintf (STREAM, "\tmov.l\t3f,r1\n");			\
	    fprintf (STREAM, "\tmova\t3f,r0\n");			\
	    fprintf (STREAM, "\tadd\tr1,r0\n");				\
	    fprintf (STREAM, "\tmov.l\t1f,r1\n");			\
	    fprintf (STREAM, "\tmov.l\t@(r0,r1),r1\n");			\
	  }								\
	else								\
	  fprintf (STREAM, "\tmov.l\t1f,r1\n");				\
	fprintf (STREAM, "\tsts.l\tpr,@-r15\n");			\
	fprintf (STREAM, "\tmova\t2f,r0\n");				\
	fprintf (STREAM, "\tjmp\t@r1\n");				\
	fprintf (STREAM, "\tlds\tr0,pr\n");				\
	fprintf (STREAM, "\t.align\t2\n");				\
	if (flag_pic)							\
	  {								\
	    fprintf (STREAM, "1:\t.long\tmcount@GOT\n");		\
	    fprintf (STREAM, "3:\t.long\t_GLOBAL_OFFSET_TABLE_\n");	\
	  }								\
	else								\
	  fprintf (STREAM, "1:\t.long\tmcount\n");			\
	fprintf (STREAM, "2:\tlds.l\t@r15+,pr\n");			\
      }									\
  } while (0)

#define MD_UNWIND_SUPPORT "config/sh/linux-unwind.h"

/* For SH3 and SH4, we use a slot of the unwind frame which correspond
   to a fake register number 16 as a placeholder for the return address
   in MD_FALLBACK_FRAME_STATE_FOR and its content will be read with
   _Unwind_GetGR which uses dwarf_reg_size_table to get the size of
   the register.  So the entry of dwarf_reg_size_table corresponding to
   this slot must be set.  To do this, we redefine DBX_REGISTER_NUMBER
   so as to return itself for 16.  */
#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(REGNO) \
  ((! TARGET_SH5 && (REGNO) == 16) ? 16 : SH_DBX_REGISTER_NUMBER (REGNO))

/* Since libgcc is compiled with -fpic for this target, we can't use
   __sdivsi3_1 as the division strategy for -O0 and -Os.  */
#undef SH_DIV_STRATEGY_DEFAULT
#define SH_DIV_STRATEGY_DEFAULT SH_DIV_CALL2
#undef SH_DIV_STR_FOR_SIZE
#define SH_DIV_STR_FOR_SIZE "call2"

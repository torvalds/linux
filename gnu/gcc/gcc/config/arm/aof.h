/* Definitions of target machine for GNU compiler, for Advanced RISC Machines
   ARM compilation, AOF Assembler.
   Copyright (C) 1995, 1996, 1997, 2000, 2003, 2004
   Free Software Foundation, Inc.
   Contributed by Richard Earnshaw (rearnsha@armltd.co.uk)

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
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */
   


#define AOF_ASSEMBLER

#define LINK_LIBGCC_SPECIAL 1

#define LINK_SPEC "%{aof} %{bin} %{aif} %{ihf} %{shl,*} %{reent*} %{split} \
		   %{ov*} %{reloc*} -nodebug"

#define STARTFILE_SPEC "crtbegin.o%s"

#define ENDFILE_SPEC "crtend.o%s"

#ifndef ASM_SPEC
#define ASM_SPEC "%{g -g} -arch 4 -apcs 3/32bit"
#endif

#ifndef LIB_SPEC
#define LIB_SPEC "%{Eb: armlib_h.32b%s}%{!Eb: armlib_h.32l%s}"
#endif

#define LIBGCC_SPEC "libgcc.a%s"

#define CTOR_LIST_BEGIN				\
  asm (CTORS_SECTION_ASM_OP);			\
  extern func_ptr __CTOR_END__[1];		\
  func_ptr __CTOR_LIST__[1] = {__CTOR_END__};

#define CTOR_LIST_END				\
  asm (CTORS_SECTION_ASM_OP);			\
  func_ptr __CTOR_END__[1] = { (func_ptr) 0 };

#define DO_GLOBAL_CTORS_BODY			\
  do						\
    {						\
      func_ptr *ptr = __CTOR_LIST__ + 1;	\
						\
      while (*ptr)				\
        (*ptr++) ();				\
    }						\
  while (0)

#define DTOR_LIST_BEGIN				\
  asm (DTORS_SECTION_ASM_OP);			\
  extern func_ptr __DTOR_END__[1];		\
  func_ptr __DTOR_LIST__[1] = {__DTOR_END__};

#define DTOR_LIST_END				\
  asm (DTORS_SECTION_ASM_OP);			\
  func_ptr __DTOR_END__[1] = { (func_ptr) 0 };

#define DO_GLOBAL_DTORS_BODY			\
  do						\
    {						\
      func_ptr *ptr = __DTOR_LIST__ + 1;	\
						\
      while (*ptr)				\
        (*ptr++) ();				\
    }						\
  while (0)

/* We really want to put Thumb tables in a read-only data section, but
   switching to another section during function output is not
   possible.  We could however do what the SPARC does and defer the
   whole table generation until the end of the function.  */
#define JUMP_TABLES_IN_TEXT_SECTION 1

#define TARGET_ASM_INIT_SECTIONS aof_asm_init_sections

/* Some systems use __main in a way incompatible with its use in gcc, in these
   cases use the macros NAME__MAIN to give a quoted symbol and SYMBOL__MAIN to
   give the same symbol without quotes for an alternative entry point.  You
   must define both, or neither.  */
#define NAME__MAIN "__gccmain"
#define SYMBOL__MAIN __gccmain

#define ASM_COMMENT_START ";"
#define ASM_APP_ON        ""
#define ASM_APP_OFF       ""

#define ASM_OUTPUT_ASCII(STREAM, PTR, LEN)		\
{							\
  int i;						\
  const char *ptr = (PTR);				\
  fprintf ((STREAM), "\tDCB");				\
  for (i = 0; i < (long)(LEN); i++)			\
    fprintf ((STREAM), " &%02x%s", 			\
	     (unsigned ) *(ptr++),			\
	     (i + 1 < (long)(LEN)			\
	      ? ((i & 3) == 3 ? "\n\tDCB" : ",")	\
	      : "\n"));					\
}

#define IS_ASM_LOGICAL_LINE_SEPARATOR(C) ((C) == '\n')

/* Output of Uninitialized Variables.  */

#define ASM_OUTPUT_COMMON(STREAM, NAME, SIZE, ROUNDED)		\
  (in_section = NULL,						\
   fprintf ((STREAM), "\tAREA "),				\
   assemble_name ((STREAM), (NAME)),				\
   fprintf ((STREAM), ", DATA, COMMON\n\t%% %d\t%s size=%d\n",	\
	    (int)(ROUNDED), ASM_COMMENT_START, (int)(SIZE)))

#define ASM_OUTPUT_LOCAL(STREAM, NAME, SIZE, ROUNDED)	\
   (zero_init_section (),				\
    assemble_name ((STREAM), (NAME)),			\
    fprintf ((STREAM), "\n"),				\
    fprintf ((STREAM), "\t%% %d\t%s size=%d\n",		\
	     (int)(ROUNDED), ASM_COMMENT_START, (int)(SIZE)))

/* Output and Generation of Labels */
extern int arm_main_function;

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\tEXPORT\t"

#define ASM_OUTPUT_LABEL(STREAM,NAME)	\
do {					\
  assemble_name (STREAM,NAME);		\
  fputs ("\n", STREAM);			\
} while (0)

#define ASM_DECLARE_FUNCTION_NAME(STREAM,NAME,DECL) \
{						\
  if (TARGET_POKE_FUNCTION_NAME)		\
    arm_poke_function_name ((STREAM), (NAME));	\
  ASM_OUTPUT_LABEL (STREAM, NAME);		\
  if (! TREE_PUBLIC (DECL))			\
    {						\
      fputs ("\tKEEP ", STREAM);		\
      ASM_OUTPUT_LABEL (STREAM, NAME);		\
    }						\
  aof_delete_import ((NAME));			\
}

#define ASM_DECLARE_OBJECT_NAME(STREAM,NAME,DECL) \
{						\
  ASM_OUTPUT_LABEL (STREAM, NAME);		\
  if (! TREE_PUBLIC (DECL))			\
    {						\
      fputs ("\tKEEP ", STREAM);		\
      ASM_OUTPUT_LABEL (STREAM, NAME);		\
    }						\
  aof_delete_import ((NAME));			\
}

#define ASM_OUTPUT_EXTERNAL(STREAM,DECL,NAME)	\
 aof_add_import ((NAME))

#define ASM_OUTPUT_EXTERNAL_LIBCALL(STREAM,SYMREF)	\
 (fprintf ((STREAM), "\tIMPORT\t"),			\
  assemble_name ((STREAM), XSTR ((SYMREF), 0)),		\
  fputc ('\n', (STREAM)))

#define ASM_OUTPUT_LABELREF(STREAM,NAME)	\
  fprintf ((STREAM), "|%s|", NAME)

#define ASM_GENERATE_INTERNAL_LABEL(STRING,PREFIX,NUM)	\
  sprintf ((STRING), "*|%s..%ld|", (PREFIX), (long)(NUM))

/* How initialization functions are handled.  */

#define CTORS_SECTION_ASM_OP "\tAREA\t|C$$gnu_ctorsvec|, DATA, READONLY"
#define DTORS_SECTION_ASM_OP "\tAREA\t|C$$gnu_dtorsvec|, DATA, READONLY"

/* Output of Assembler Instructions.  */

#define REGISTER_NAMES				\
{						\
  "a1", "a2", "a3", "a4",			\
  "v1", "v2", "v3", "v4",			\
  "v5", "v6", "sl", "fp",			\
  "ip", "sp", "lr", "pc",			\
  "f0", "f1", "f2", "f3",			\
  "f4", "f5", "f6", "f7",			\
  "cc", "sfp", "afp",				\
  "mv0",   "mv1",   "mv2",   "mv3",		\
  "mv4",   "mv5",   "mv6",   "mv7",		\
  "mv8",   "mv9",   "mv10",  "mv11",		\
  "mv12",  "mv13",  "mv14",  "mv15",		\
  "wcgr0", "wcgr1", "wcgr2", "wcgr3",		\
  "wr0",   "wr1",   "wr2",   "wr3",		\
  "wr4",   "wr5",   "wr6",   "wr7",		\
  "wr8",   "wr9",   "wr10",  "wr11",		\
  "wr12",  "wr13",  "wr14",  "wr15",		\
  "s0",  "s1",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",  \
  "s8",  "s9",  "s10", "s11", "s12", "s13", "s14", "s15", \
  "s16", "s17", "s18", "s19", "s20", "s21", "s22", "s23", \
  "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31",  \
  "vfpcc"					\
}

#define ADDITIONAL_REGISTER_NAMES		\
{						\
  {"r0", 0}, {"a1", 0},				\
  {"r1", 1}, {"a2", 1},				\
  {"r2", 2}, {"a3", 2},				\
  {"r3", 3}, {"a4", 3},		      		\
  {"r4", 4}, {"v1", 4},				\
  {"r5", 5}, {"v2", 5},				\
  {"r6", 6}, {"v3", 6},				\
  {"r7", 7}, {"wr", 7},				\
  {"r8", 8}, {"v5", 8},				\
  {"r9", 9}, {"v6", 9},				\
  {"r10", 10}, {"sl", 10}, {"v7", 10},		\
  {"r11", 11}, {"fp", 11},			\
  {"r12", 12}, {"ip", 12}, 			\
  {"r13", 13}, {"sp", 13}, 			\
  {"r14", 14}, {"lr", 14},			\
  {"r15", 15}, {"pc", 15},			\
  {"d0", 63},					\
  {"d1", 65},					\
  {"d2", 67},					\
  {"d3", 69},					\
  {"d4", 71},					\
  {"d5", 73},					\
  {"d6", 75},					\
  {"d7", 77},					\
  {"d8", 79},					\
  {"d9", 81},					\
  {"d10", 83},					\
  {"d11", 85},					\
  {"d12", 87},					\
  {"d13", 89},					\
  {"d14", 91},					\
  {"d15", 93}					\
}

#define REGISTER_PREFIX "__"
#define USER_LABEL_PREFIX ""
#define LOCAL_LABEL_PREFIX ""

/* AOF does not prefix user function names with an underscore.  */
#define ARM_MCOUNT_NAME "_mcount"

/* Output of Dispatch Tables.  */
#define ASM_OUTPUT_ADDR_DIFF_ELT(STREAM, BODY, VALUE, REL)			\
  do										\
    {										\
      if (TARGET_ARM)								\
        fprintf ((STREAM), "\tb\t|L..%d|\n", (VALUE));				\
      else									\
        fprintf ((STREAM), "\tDCD\t|L..%d| - |L..%d|\n", (VALUE), (REL));	\
    }										\
  while (0)

#define ASM_OUTPUT_ADDR_VEC_ELT(STREAM, VALUE)	\
  fprintf ((STREAM), "\tDCD\t|L..%d|\n", (VALUE))

/* A label marking the start of a jump table is a data label.  */
#define ASM_OUTPUT_CASE_LABEL(STREAM, PREFIX, NUM, TABLE)	\
  fprintf ((STREAM), "\tALIGN\n|%s..%d|\n", (PREFIX), (NUM))

/* Assembler Commands for Alignment.  */
#define ASM_OUTPUT_SKIP(STREAM, NBYTES)		\
 fprintf ((STREAM), "\t%%\t%d\n", (int) (NBYTES))

#define ASM_OUTPUT_ALIGN(STREAM, POWER)			\
  do							\
    {							\
      int amount = 1 << (POWER);			\
							\
      if (amount == 2)					\
        fprintf ((STREAM), "\tALIGN 2\n");		\
      else if (amount == 4)				\
        fprintf ((STREAM), "\tALIGN\n");		\
      else						\
        fprintf ((STREAM), "\tALIGN %d\n", amount);	\
    }							\
  while (0)

#undef DBX_DEBUGGING_INFO

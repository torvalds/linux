/* Definitions for Intel 386 running SCO Unix System V 3.2 Version 5.
   Copyright (C) 1992, 1995, 1996, 1997, 1998, 1999, 2000, 2002, 2003, 2004
   Free Software Foundation, Inc.
   Contributed by Kean Johnston (jkj@sco.com)

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

#undef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (i386, SCO OpenServer 5 Syntax)");

#undef ASM_QUAD

#undef GLOBAL_ASM_OP
#define GLOBAL_ASM_OP			"\t.globl\t"

#undef BSS_SECTION_ASM_OP
#define BSS_SECTION_ASM_OP		"\t.section\t.bss, \"aw\", @nobits"
  
/*
 * NOTE: We really do want CTORS_SECTION_ASM_OP and DTORS_SECTION_ASM_OP.
 * Here's the reason why. If we dont define them, and we dont define them
 * to always emit to the same section, the default is to emit to "named"
 * ctors and dtors sections. This would be great if we could use GNU ld,
 * but we can't. The native linker could possibly be trained to coalesce
 * named ctors sections, but that hasn't been done either. So if we don't
 * define these, many C++ ctors and dtors dont get run, because they never
 * wind up in the ctors/dtors arrays.
 */
#define CTORS_SECTION_ASM_OP		"\t.section\t.ctors, \"aw\""
#define DTORS_SECTION_ASM_OP		"\t.section\t.dtors, \"aw\""

#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true
#undef X86_FILE_START_VERSION_DIRECTIVE
#define X86_FILE_START_VERSION_DIRECTIVE true

/* A C statement (sans semicolon) to output to the stdio stream
   FILE the assembler definition of uninitialized global DECL named
   NAME whose size is SIZE bytes and alignment is ALIGN bytes.
   Try to use asm_output_aligned_bss to implement this macro.  */

#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss (FILE, DECL, NAME, SIZE, ALIGN)

#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(n)	svr4_dbx_register_map[n]

#define DWARF2_DEBUGGING_INFO		1
#define DBX_DEBUGGING_INFO		1

#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE	DWARF2_DEBUG

#undef DWARF2_UNWIND_INFO
#define DWARF2_UNWIND_INFO		1

#undef NO_IMPLICIT_EXTERN_C
#define NO_IMPLICIT_EXTERN_C		1

#undef SWITCH_TAKES_ARG
#define SWITCH_TAKES_ARG(CHAR) 						\
  (DEFAULT_SWITCH_TAKES_ARG(CHAR)					\
   || (CHAR) == 'h' 							\
   || (CHAR) == 'R' 							\
   || (CHAR) == 'Y' 							\
   || (CHAR) == 'z')

#undef WORD_SWITCH_TAKES_ARG
#define WORD_SWITCH_TAKES_ARG(STR)					\
 (DEFAULT_WORD_SWITCH_TAKES_ARG (STR)					\
  && strcmp (STR, "Tdata") && strcmp (STR, "Ttext")			\
  && strcmp (STR, "Tbss"))

#undef TARGET_SUBTARGET_DEFAULT
#define TARGET_SUBTARGET_DEFAULT (MASK_80387 | MASK_IEEE_FP | MASK_FLOAT_RETURNS)

/*
 * Define sizes and types
 */
#undef SIZE_TYPE
#undef PTRDIFF_TYPE
#undef WCHAR_TYPE
#undef WCHAR_TYPE_SIZE
#undef WINT_TYPE
#define SIZE_TYPE		"unsigned int"
#define PTRDIFF_TYPE		"int"
#define WCHAR_TYPE		"long int"
#define WCHAR_TYPE_SIZE		BITS_PER_WORD
#define WINT_TYPE		"long int"

/*
 * New for multilib support. Set the default switches for multilib,
 * which is -melf.
 */
#define MULTILIB_DEFAULTS { "melf" }


/* Please note that these specs may look messy but they are required in
   order to emulate the SCO Development system as closely as possible.
   With SCO Open Server 5.0, you now get the linker and assembler free,
   so that is what these specs are targeted for. These utilities are
   very argument sensitive: a space in the wrong place breaks everything.
   So please forgive this mess. It works.

   Parameters which can be passed to gcc, and their SCO equivalents:
   GCC Parameter                SCO Equivalent
   -ansi                        -a ansi
   -posix                       -a posix
   -Xpg4                        -a xpg4
   -Xpg4plus                    -a xpg4plus
   -Xods30                      -a ods30

   As with SCO, the default is XPG4 plus mode. SCO also allows you to
   specify a C dialect with -Xt, -Xa, -Xc, -Xk and -Xm. These are passed
   on to the assembler and linker in the same way that the SCO compiler
   does.

   SCO also allows you to compile, link and generate either ELF or COFF
   binaries. With gcc, we now only support ELF mode.

   GCC also requires that the user has installed OSS646, the Execution
   Environment Update, or is running release 5.0.7 or later. This has
   many fixes to the ELF link editor and assembler, and a considerably
   improved libc and RTLD.

   In terms of tool usage, we want to use the standard link editor always,
   and either the GNU assembler or the native assembler. With OSS646 the
   native assembler has grown up quite a bit. Some of the specs below
   assume that /usr/gnu is the prefix for the GNU tools, because thats
   where the SCO provided ones go. This is especially important for
   include and library search path ordering. We want to look in /usr/gnu
   first because frequently people are linking against -lintl, and they
   MEAN to link with gettext. What they get is the SCO intl library. Its
   a REAL pity that GNU gettext chose that name; perhaps in a future
   version they can be persuaded to change it to -lgnuintl and have a
   link so that -lintl will work for other systems. The same goes for
   header files. We want /usr/gnu/include searched for before the system
   header files. Hence the -isystem /usr/gnu/include in the CPP_SPEC.
   We get /usr/gnu/lib first by virtue of the MD_STARTFILE_PREFIX below.
*/

#define MD_STARTFILE_PREFIX	"/usr/gnu/lib/"
#define MD_STARTFILE_PREFIX_1	"/usr/ccs/lib/"

#if USE_GAS
# define MD_EXEC_PREFIX		"/usr/gnu/bin/"
#else
# define MD_EXEC_PREFIX		"/usr/ccs/bin/elf/"
#endif

/* Always use the system linker, please.  */
#ifndef DEFAULT_LINKER
# define DEFAULT_LINKER		"/usr/ccs/bin/elf/ld"
#endif

/* Set up assembler flags for PIC and ELF compilations */
#undef ASM_SPEC

#if USE_GAS
  /* Leave ASM_SPEC undefined so we pick up the master copy from gcc.c  */
#else
#define ASM_SPEC \
   "%{Ym,*} %{Yd,*} %{Wa,*:%*} \
    -E%{Xa:a}%{!Xa:%{Xc:c}%{!Xc:%{Xk:k}%{!Xk:%{Xt:t}%{!Xt:a}}}},%{ansi:ansi}%{!ansi:%{posix:posix}%{!posix:%{Xpg4:xpg4}%{!Xpg4:%{Xpg4plus:XPG4PLUS}%{!Xpg4plus:%{Xods30:ods30}%{!Xods30:XPG4PLUS}}}}},ELF %{Qn:} %{!Qy:-Qn}"
#endif

/*
 * Use crti.o for shared objects, crt1.o for normal executables. Make sure
 * to recognize both -G and -shared as a valid way of introducing shared
 * library generation. This is important for backwards compatibility.
 */

#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
 "%{pg:%e-pg not supported on this platform} \
  %{p:%{pp:%e-p and -pp specified - pick one}} \
 %{!shared:\
   %{!symbolic: \
    %{!G: \
     %{pp:pcrt1elf.o%s}%{p:mcrt1.o%s}%{!p:%{!pp:crt1.o%s}}}}} \
  crti.o%s \
  %{ansi:values-Xc.o%s} \
  %{!ansi: \
   %{traditional:values-Xt.o%s} \
    %{!traditional: \
     %{Xa:values-Xa.o%s} \
      %{!Xa:%{Xc:values-Xc.o%s} \
       %{!Xc:%{Xk:values-Xk.o%s} \
        %{!Xk:%{Xt:values-Xt.o%s} \
         %{!Xt:values-Xa.o%s}}}}}} \
  crtbegin.o%s"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC \
 "crtend.o%s crtn.o%s"

#define TARGET_OS_CPP_BUILTINS()				\
  do								\
    {								\
	builtin_define ("__unix");				\
	builtin_define ("_SCO_DS");				\
	builtin_define ("_SCO_DS_LL");				\
	builtin_define ("_SCO_ELF");				\
	builtin_define ("_M_I386");				\
	builtin_define ("_M_XENIX");				\
	builtin_define ("_M_UNIX");				\
	builtin_assert ("system=svr3");				\
	if (flag_iso)						\
	  cpp_define (pfile, "_STRICT_ANSI");			\
    }								\
  while (0)

#undef CPP_SPEC
#define CPP_SPEC "\
  -isystem /usr/gnu/include \
  %{!Xods30:-D_STRICT_NAMES} \
  %{!ansi:%{!posix:%{!Xods30:-D_SCO_XPG_VERS=4}}} \
  %{ansi:-isystem include/ansi%s -isystem /usr/include/ansi} \
  %{!ansi: \
   %{posix:-isystem include/posix%s -isystem /usr/include/posix \
           -D_POSIX_C_SOURCE=2 -D_POSIX_SOURCE=1} \
    %{!posix:%{Xpg4:-isystem include/xpg4%s -isystem /usr/include/xpg4 \
                    -D_XOPEN_SOURCE=1} \
     %{!Xpg4:-D_M_I86 -D_M_I86SM -D_M_INTERNAT -D_M_SDATA -D_M_STEXT \
             -D_M_BITFIELDS -D_M_SYS5 -D_M_SYSV -D_M_SYSIII \
             -D_M_WORDSWAP -Dunix -DM_I386 -DM_UNIX -DM_XENIX \
             %{Xods30:-isystem include/ods_30_compat%s \
                      -isystem /usr/include/ods_30_compat \
                      -D_SCO_ODS_30 -DM_I86 -DM_I86SM -DM_SDATA -DM_STEXT \
                      -DM_BITFIELDS -DM_SYS5 -DM_SYSV -DM_INTERNAT -DM_SYSIII \
                      -DM_WORDSWAP}}}} \
  %{scointl:-DM_INTERNAT -D_M_INTERNAT} \
  %{Xa:-D_SCO_C_DIALECT=1} \
  %{!Xa:%{Xc:-D_SCO_C_DIALECT=3} \
   %{!Xc:%{Xk:-D_SCO_C_DIALECT=4} \
    %{!Xk:%{Xt:-D_SCO_C_DIALECT=2} \
     %{!Xt:-D_SCO_C_DIALECT=1}}}}"

#undef LINK_SPEC
#define LINK_SPEC \
 "%{!shared:%{!symbolic:%{!G:-E%{Xa:a}%{!Xa:%{Xc:c}%{!Xc:%{Xk:k}%{!Xk:%{Xt:t}%{!Xt:a}}}},%{ansi:ansi}%{!ansi:%{posix:posix}%{!posix:%{Xpg4:xpg4}%{!Xpg4:%{Xpg4plus:XPG4PLUS}%{!Xpg4plus:%{Xods30:ods30}%{!Xods30:XPG4PLUS}}}}},ELF}}} \
  %{YP,*} %{YL,*} %{YU,*} \
  %{!YP,*:%{p:-YP,/usr/ccs/libp:/lib/libp:/usr/lib/libp:/usr/ccs/lib:/lib:/usr/lib} \
   %{!p:-YP,/usr/ccs/lib:/lib:/usr/lib}} \
  %{h*} %{static:-dn -Bstatic %{G:%e-G and -static are mutually exclusive}} \
  %{shared:%{!G:-G}} %{G:%{!shared:-G}} %{shared:%{G:-G}} \
  %{shared:-dy %{symbolic:-Bsymbolic -G} %{z*}} %{R*} %{Y*} \
  %{Qn:} %{!Qy:-Qn} -z alt_resolve"

/* Library spec. If we are not building a shared library, provide the
   standard libraries, as per the SCO compiler.  */

#undef LIB_SPEC
#define LIB_SPEC \
 "%{shared:%{!G:pic/libgcc.a%s}} \
  %{G:%{!shared:pic/libgcc.a%s}} \
  %{shared:%{G:pic/libgcc.a%s}} \
  %{p:%{!pp:-lelfprof -lelf}} %{pp:%{!p:-lelfprof -lelf}} \
  %{!shared:%{!symbolic:%{!G:-lcrypt -lgen -lc}}}"

#undef LIBGCC_SPEC
#define LIBGCC_SPEC \
 "%{!shared:%{!G:-lgcc}}"

/* Handle special EH pointer encodings.  Absolute, pc-relative, and
   indirect are handled automatically.  */
#define ASM_MAYBE_OUTPUT_ENCODED_ADDR_RTX(FILE, ENCODING, SIZE, ADDR, DONE) \
  do {									\
    if ((SIZE) == 4 && ((ENCODING) & 0x70) == DW_EH_PE_datarel)		\
      {									\
        fputs (ASM_LONG, FILE);						\
        assemble_name (FILE, XSTR (ADDR, 0));				\
	fputs (((ENCODING) & DW_EH_PE_indirect ? "@GOT" : "@GOTOFF"), FILE); \
        goto DONE;							\
      }									\
  } while (0)

/* Used by crtstuff.c to initialize the base of data-relative relocations.
   These are GOT relative on x86, so return the pic register.  */
#ifdef __PIC__
#define CRT_GET_RFIB_DATA(BASE)			\
  {						\
    register void *ebx_ __asm__("ebx");		\
    BASE = ebx_;				\
  }
#else
#define CRT_GET_RFIB_DATA(BASE)						\
  __asm__ ("call\t.LPR%=\n"						\
	   ".LPR%=:\n\t"						\
	   "popl\t%0\n\t"						\
	   /* Due to a GAS bug, this cannot use EAX.  That encodes	\
	      smaller than the traditional EBX, which results in the	\
	      offset being off by one.  */				\
	   "addl\t$_GLOBAL_OFFSET_TABLE_+[.-.LPR%=],%0"			\
	   : "=d"(BASE))
#endif


/* Output VMS debug format symbol table information from GCC.
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Douglas B. Rupp (rupp@gnat.com).
   Updated by Bernard W. Giroud (bgiroud@users.sourceforge.net).

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"

#ifdef VMS_DEBUGGING_INFO
#include "tree.h"
#include "version.h"
#include "flags.h"
#include "rtl.h"
#include "output.h"
#include "vmsdbg.h"
#include "debug.h"
#include "langhooks.h"
#include "function.h"
#include "target.h"

/* Difference in seconds between the VMS Epoch and the Unix Epoch */
static const long long vms_epoch_offset = 3506716800ll;

/* NOTE: In the comments in this file, many references are made to "Debug
   Symbol Table".  This term is abbreviated as `DST' throughout the remainder
   of this file.  */

typedef struct dst_line_info_struct *dst_line_info_ref;

/* Each entry in the line_info_table maintains the file and
   line number associated with the label generated for that
   entry.  The label gives the PC value associated with
   the line number entry.  */
typedef struct dst_line_info_struct
{
  unsigned long dst_file_num;
  unsigned long dst_line_num;
}
dst_line_info_entry;

typedef struct dst_file_info_struct *dst_file_info_ref;

typedef struct dst_file_info_struct
{
  char *file_name;
  unsigned int max_line;
  unsigned int listing_line_start;
  long long cdt;
  long ebk;
  short ffb;
  char rfo;
  char flen;
}
dst_file_info_entry;

/* How to start an assembler comment.  */
#ifndef ASM_COMMENT_START
#define ASM_COMMENT_START ";#"
#endif

/* Maximum size (in bytes) of an artificially generated label.  */
#define MAX_ARTIFICIAL_LABEL_BYTES	30

/* Make sure we know the sizes of the various types debug can describe. These
   are only defaults.  If the sizes are different for your target, you should
   override these values by defining the appropriate symbols in your tm.h
   file.  */
#ifndef PTR_SIZE
#define PTR_SIZE 4 /* Must be 32 bits for VMS debug info */
#endif

/* Pointer to a structure of filenames referenced by this compilation unit.  */
static dst_file_info_ref file_info_table;

/* Total number of entries in the table (i.e. array) pointed to by
   `file_info_table'.  This is the *total* and includes both used and unused
   slots.  */
static unsigned int file_info_table_allocated;

/* Number of entries in the file_info_table which are actually in use.  */
static unsigned int file_info_table_in_use;

/* Size (in elements) of increments by which we may expand the filename
   table.  */
#define FILE_TABLE_INCREMENT 64

/* A structure to hold basic information for the VMS end
   routine.  */

typedef struct vms_func_struct
{
  const char *vms_func_name;
  unsigned funcdef_number;
}
vms_func_node;

typedef struct vms_func_struct *vms_func_ref;

static unsigned int func_table_allocated;
static unsigned int func_table_in_use;
#define FUNC_TABLE_INCREMENT 256

/* A pointer to the base of a table that contains frame description
   information for each routine.  */
static vms_func_ref func_table;

/* Local pointer to the name of the main input file.  Initialized in
   avmdbgout_init.  */
static const char *primary_filename;

static char *module_producer;
static unsigned int module_language;

/* A pointer to the base of a table that contains line information
   for each source code line in .text in the compilation unit.  */
static dst_line_info_ref line_info_table;

/* Number of elements currently allocated for line_info_table.  */
static unsigned int line_info_table_allocated;

/* Number of elements in line_info_table currently in use.  */
static unsigned int line_info_table_in_use;

/* Size (in elements) of increments by which we may expand line_info_table.  */
#define LINE_INFO_TABLE_INCREMENT 1024

/* Forward declarations for functions defined in this file.  */
static char *full_name (const char *);
static unsigned int lookup_filename (const char *);
static void addr_const_to_string (char *, rtx);
static int write_debug_header (DST_HEADER *, const char *, int);
static int write_debug_addr (char *, const char *, int);
static int write_debug_data1 (unsigned int, const char *, int);
static int write_debug_data2 (unsigned int, const char *, int);
static int write_debug_data4 (unsigned long, const char *, int);
static int write_debug_data8 (unsigned long long, const char *, int);
static int write_debug_delta4 (char *, char *, const char *, int);
static int write_debug_string (char *, const char *, int);
static int write_modbeg (int);
static int write_modend (int);
static int write_rtnbeg (int, int);
static int write_rtnend (int, int);
static int write_pclines (int);
static int write_srccorr (int, dst_file_info_entry, int);
static int write_srccorrs (int);

static void vmsdbgout_init (const char *);
static void vmsdbgout_finish (const char *);
static void vmsdbgout_define (unsigned int, const char *);
static void vmsdbgout_undef (unsigned int, const char *);
static void vmsdbgout_start_source_file (unsigned int, const char *);
static void vmsdbgout_end_source_file (unsigned int);
static void vmsdbgout_begin_block (unsigned int, unsigned int);
static void vmsdbgout_end_block (unsigned int, unsigned int);
static bool vmsdbgout_ignore_block (tree);
static void vmsdbgout_source_line (unsigned int, const char *);
static void vmsdbgout_begin_prologue (unsigned int, const char *);
static void vmsdbgout_end_prologue (unsigned int, const char *);
static void vmsdbgout_end_function (unsigned int);
static void vmsdbgout_end_epilogue (unsigned int, const char *);
static void vmsdbgout_begin_function (tree);
static void vmsdbgout_decl (tree);
static void vmsdbgout_global_decl (tree);
static void vmsdbgout_abstract_function (tree);

/* The debug hooks structure.  */

const struct gcc_debug_hooks vmsdbg_debug_hooks
= {vmsdbgout_init,
   vmsdbgout_finish,
   vmsdbgout_define,
   vmsdbgout_undef,
   vmsdbgout_start_source_file,
   vmsdbgout_end_source_file,
   vmsdbgout_begin_block,
   vmsdbgout_end_block,
   vmsdbgout_ignore_block,
   vmsdbgout_source_line,
   vmsdbgout_begin_prologue,
   vmsdbgout_end_prologue,
   vmsdbgout_end_epilogue,
   vmsdbgout_begin_function,
   vmsdbgout_end_function,
   vmsdbgout_decl,
   vmsdbgout_global_decl,
   debug_nothing_tree_int,	  /* type_decl */
   debug_nothing_tree_tree,       /* imported_module_or_decl */
   debug_nothing_tree,		  /* deferred_inline_function */
   vmsdbgout_abstract_function,
   debug_nothing_rtx,		  /* label */
   debug_nothing_int,		  /* handle_pch */
   debug_nothing_rtx,		  /* var_location */
   debug_nothing_void,            /* switch_text_section */
   0                              /* start_end_main_source_file */
};

/* Definitions of defaults for assembler-dependent names of various
   pseudo-ops and section names.
   Theses may be overridden in the tm.h file (if necessary) for a particular
   assembler.  */
#ifdef UNALIGNED_SHORT_ASM_OP
#undef UNALIGNED_SHORT_ASM_OP
#endif
#define UNALIGNED_SHORT_ASM_OP	".word"

#ifdef UNALIGNED_INT_ASM_OP
#undef UNALIGNED_INT_ASM_OP
#endif
#define UNALIGNED_INT_ASM_OP	".long"

#ifdef UNALIGNED_LONG_ASM_OP
#undef UNALIGNED_LONG_ASM_OP
#endif
#define UNALIGNED_LONG_ASM_OP	".long"

#ifdef UNALIGNED_DOUBLE_INT_ASM_OP
#undef UNALIGNED_DOUBLE_INT_ASM_OP
#endif
#define UNALIGNED_DOUBLE_INT_ASM_OP	".quad"

#ifdef ASM_BYTE_OP
#undef ASM_BYTE_OP
#endif
#define ASM_BYTE_OP	".byte"

#define NUMBYTES(I) ((I) < 256 ? 1 : (I) < 65536 ? 2 : 4)

#define NUMBYTES0(I) ((I) < 128 ? 0 : (I) < 65536 ? 2 : 4)

#ifndef UNALIGNED_PTR_ASM_OP
#define UNALIGNED_PTR_ASM_OP \
  (PTR_SIZE == 8 ? UNALIGNED_DOUBLE_INT_ASM_OP : UNALIGNED_INT_ASM_OP)
#endif

#ifndef UNALIGNED_OFFSET_ASM_OP
#define UNALIGNED_OFFSET_ASM_OP(OFFSET) \
  (NUMBYTES(OFFSET) == 4 \
   ? UNALIGNED_LONG_ASM_OP \
   : (NUMBYTES(OFFSET) == 2 ? UNALIGNED_SHORT_ASM_OP : ASM_BYTE_OP))
#endif

/* Definitions of defaults for formats and names of various special
   (artificial) labels which may be generated within this file (when the -g
   options is used and VMS_DEBUGGING_INFO is in effect.  If necessary, these
   may be overridden from within the tm.h file, but typically, overriding these
   defaults is unnecessary.  */

static char text_end_label[MAX_ARTIFICIAL_LABEL_BYTES];

#ifndef TEXT_END_LABEL
#define TEXT_END_LABEL		"Lvetext"
#endif
#ifndef FUNC_BEGIN_LABEL
#define FUNC_BEGIN_LABEL	"LVFB"
#endif
#ifndef FUNC_PROLOG_LABEL
#define FUNC_PROLOG_LABEL	"LVFP"
#endif
#ifndef FUNC_END_LABEL
#define FUNC_END_LABEL		"LVFE"
#endif
#ifndef BLOCK_BEGIN_LABEL
#define BLOCK_BEGIN_LABEL	"LVBB"
#endif
#ifndef BLOCK_END_LABEL
#define BLOCK_END_LABEL		"LVBE"
#endif
#ifndef LINE_CODE_LABEL
#define LINE_CODE_LABEL		"LVM"
#endif

#ifndef ASM_OUTPUT_DEBUG_DELTA2
#define ASM_OUTPUT_DEBUG_DELTA2(FILE,LABEL1,LABEL2)			 \
  do									 \
    {									 \
      fprintf ((FILE), "\t%s\t", UNALIGNED_SHORT_ASM_OP);		 \
      assemble_name (FILE, LABEL1);					 \
      fprintf (FILE, "-");						 \
      assemble_name (FILE, LABEL2);					 \
    }									 \
  while (0)
#endif

#ifndef ASM_OUTPUT_DEBUG_DELTA4
#define ASM_OUTPUT_DEBUG_DELTA4(FILE,LABEL1,LABEL2)			 \
  do									 \
    {									 \
      fprintf ((FILE), "\t%s\t", UNALIGNED_INT_ASM_OP);			 \
      assemble_name (FILE, LABEL1);					 \
      fprintf (FILE, "-");						 \
      assemble_name (FILE, LABEL2);					 \
    }									 \
  while (0)
#endif

#ifndef ASM_OUTPUT_DEBUG_ADDR_DELTA
#define ASM_OUTPUT_DEBUG_ADDR_DELTA(FILE,LABEL1,LABEL2)			 \
  do									 \
    {									 \
      fprintf ((FILE), "\t%s\t", UNALIGNED_PTR_ASM_OP);			 \
      assemble_name (FILE, LABEL1);					 \
      fprintf (FILE, "-");						 \
      assemble_name (FILE, LABEL2);					 \
    }									 \
  while (0)
#endif

#ifndef ASM_OUTPUT_DEBUG_ADDR
#define ASM_OUTPUT_DEBUG_ADDR(FILE,LABEL)				 \
  do									 \
    {									 \
      fprintf ((FILE), "\t%s\t", UNALIGNED_PTR_ASM_OP);			 \
      assemble_name (FILE, LABEL);					 \
    }									 \
  while (0)
#endif

#ifndef ASM_OUTPUT_DEBUG_ADDR_CONST
#define ASM_OUTPUT_DEBUG_ADDR_CONST(FILE,ADDR)				\
  fprintf ((FILE), "\t%s\t%s", UNALIGNED_PTR_ASM_OP, (ADDR))
#endif

#ifndef ASM_OUTPUT_DEBUG_DATA1
#define ASM_OUTPUT_DEBUG_DATA1(FILE,VALUE) \
  fprintf ((FILE), "\t%s\t0x%x", ASM_BYTE_OP, (unsigned char) VALUE)
#endif

#ifndef ASM_OUTPUT_DEBUG_DATA2
#define ASM_OUTPUT_DEBUG_DATA2(FILE,VALUE) \
  fprintf ((FILE), "\t%s\t0x%x", UNALIGNED_SHORT_ASM_OP, \
	   (unsigned short) VALUE)
#endif

#ifndef ASM_OUTPUT_DEBUG_DATA4
#define ASM_OUTPUT_DEBUG_DATA4(FILE,VALUE) \
  fprintf ((FILE), "\t%s\t0x%lx", UNALIGNED_INT_ASM_OP, (unsigned long) VALUE)
#endif

#ifndef ASM_OUTPUT_DEBUG_DATA
#define ASM_OUTPUT_DEBUG_DATA(FILE,VALUE) \
  fprintf ((FILE), "\t%s\t0x%lx", UNALIGNED_OFFSET_ASM_OP(VALUE), VALUE)
#endif

#ifndef ASM_OUTPUT_DEBUG_ADDR_DATA
#define ASM_OUTPUT_DEBUG_ADDR_DATA(FILE,VALUE) \
  fprintf ((FILE), "\t%s\t0x%lx", UNALIGNED_PTR_ASM_OP, \
	   (unsigned long) VALUE)
#endif

#ifndef ASM_OUTPUT_DEBUG_DATA8
#define ASM_OUTPUT_DEBUG_DATA8(FILE,VALUE) \
  fprintf ((FILE), "\t%s\t0x%llx", UNALIGNED_DOUBLE_INT_ASM_OP, \
                                 (unsigned long long) VALUE)
#endif

/* This is similar to the default ASM_OUTPUT_ASCII, except that no trailing
   newline is produced.  When flag_verbose_asm is asserted, we add commentary
   at the end of the line, so we must avoid output of a newline here.  */
#ifndef ASM_OUTPUT_DEBUG_STRING
#define ASM_OUTPUT_DEBUG_STRING(FILE,P)		\
  do						\
    {						\
      register int slen = strlen(P);		\
      register char *p = (P);			\
      register int i;				\
      fprintf (FILE, "\t.ascii \"");		\
      for (i = 0; i < slen; i++)		\
	{					\
	  register int c = p[i];		\
	  if (c == '\"' || c == '\\')		\
	    putc ('\\', FILE);			\
	  if (c >= ' ' && c < 0177)		\
	    putc (c, FILE);			\
	  else					\
	    fprintf (FILE, "\\%o", c);		\
	}					\
      fprintf (FILE, "\"");			\
    }						\
  while (0)
#endif

/* Convert a reference to the assembler name of a C-level name.  This
   macro has the same effect as ASM_OUTPUT_LABELREF, but copies to
   a string rather than writing to a file.  */
#ifndef ASM_NAME_TO_STRING
#define ASM_NAME_TO_STRING(STR, NAME)		\
  do						\
    {						\
      if ((NAME)[0] == '*')			\
	strcpy (STR, NAME+1);			\
      else					\
	strcpy (STR, NAME);			\
    }						\
  while (0)
#endif


/* General utility functions.  */

/* Convert an integer constant expression into assembler syntax.  Addition and
   subtraction are the only arithmetic that may appear in these expressions.
   This is an adaptation of output_addr_const in final.c.  Here, the target
   of the conversion is a string buffer.  We can't use output_addr_const
   directly, because it writes to a file.  */

static void
addr_const_to_string (char *str, rtx x)
{
  char buf1[256];
  char buf2[256];

 restart:
  str[0] = '\0';
  switch (GET_CODE (x))
    {
    case PC:
      gcc_assert (flag_pic);
      strcat (str, ",");
      break;

    case SYMBOL_REF:
      ASM_NAME_TO_STRING (buf1, XSTR (x, 0));
      strcat (str, buf1);
      break;

    case LABEL_REF:
      ASM_GENERATE_INTERNAL_LABEL (buf1, "L", CODE_LABEL_NUMBER (XEXP (x, 0)));
      ASM_NAME_TO_STRING (buf2, buf1);
      strcat (str, buf2);
      break;

    case CODE_LABEL:
      ASM_GENERATE_INTERNAL_LABEL (buf1, "L", CODE_LABEL_NUMBER (x));
      ASM_NAME_TO_STRING (buf2, buf1);
      strcat (str, buf2);
      break;

    case CONST_INT:
      sprintf (buf1, HOST_WIDE_INT_PRINT_DEC, INTVAL (x));
      strcat (str, buf1);
      break;

    case CONST:
      /* This used to output parentheses around the expression, but that does
         not work on the 386 (either ATT or BSD assembler).  */
      addr_const_to_string (buf1, XEXP (x, 0));
      strcat (str, buf1);
      break;

    case CONST_DOUBLE:
      if (GET_MODE (x) == VOIDmode)
	{
	  /* We can use %d if the number is one word and positive.  */
	  if (CONST_DOUBLE_HIGH (x))
	    sprintf (buf1, HOST_WIDE_INT_PRINT_DOUBLE_HEX,
		     CONST_DOUBLE_HIGH (x), CONST_DOUBLE_LOW (x));
	  else if (CONST_DOUBLE_LOW (x) < 0)
	    sprintf (buf1, HOST_WIDE_INT_PRINT_HEX, CONST_DOUBLE_LOW (x));
	  else
	    sprintf (buf1, HOST_WIDE_INT_PRINT_DEC,
		     CONST_DOUBLE_LOW (x));
	  strcat (str, buf1);
	}
      else
	/* We can't handle floating point constants; PRINT_OPERAND must
	   handle them.  */
	output_operand_lossage ("floating constant misused");
      break;

    case PLUS:
      /* Some assemblers need integer constants to appear last (eg masm).  */
      if (GET_CODE (XEXP (x, 0)) == CONST_INT)
	{
	  addr_const_to_string (buf1, XEXP (x, 1));
	  strcat (str, buf1);
	  if (INTVAL (XEXP (x, 0)) >= 0)
	    strcat (str, "+");
	  addr_const_to_string (buf1, XEXP (x, 0));
	  strcat (str, buf1);
	}
      else
	{
	  addr_const_to_string (buf1, XEXP (x, 0));
	  strcat (str, buf1);
	  if (INTVAL (XEXP (x, 1)) >= 0)
	    strcat (str, "+");
	  addr_const_to_string (buf1, XEXP (x, 1));
	  strcat (str, buf1);
	}
      break;

    case MINUS:
      /* Avoid outputting things like x-x or x+5-x, since some assemblers
         can't handle that.  */
      x = simplify_subtraction (x);
      if (GET_CODE (x) != MINUS)
	goto restart;

      addr_const_to_string (buf1, XEXP (x, 0));
      strcat (str, buf1);
      strcat (str, "-");
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) < 0)
	{
	  strcat (str, "(");
	  addr_const_to_string (buf1, XEXP (x, 1));
	  strcat (str, buf1);
	  strcat (str, ")");
	}
      else
	{
	  addr_const_to_string (buf1, XEXP (x, 1));
	  strcat (str, buf1);
	}
      break;

    case ZERO_EXTEND:
    case SIGN_EXTEND:
      addr_const_to_string (buf1, XEXP (x, 0));
      strcat (str, buf1);
      break;

    default:
      output_operand_lossage ("invalid expression as operand");
    }
}

/* Output the debug header HEADER.  Also output COMMENT if flag_verbose_asm is
   set.  Return the header size.  Just return the size if DOSIZEONLY is
   nonzero.  */

static int
write_debug_header (DST_HEADER *header, const char *comment, int dosizeonly)
{
  if (!dosizeonly)
    {
      ASM_OUTPUT_DEBUG_DATA2 (asm_out_file,
			      header->dst__header_length.dst_w_length);

      if (flag_verbose_asm)
	fprintf (asm_out_file, "\t%s record length", ASM_COMMENT_START);
      fputc ('\n', asm_out_file);

      ASM_OUTPUT_DEBUG_DATA2 (asm_out_file,
			      header->dst__header_type.dst_w_type);

      if (flag_verbose_asm)
	fprintf (asm_out_file, "\t%s record type (%s)", ASM_COMMENT_START,
		 comment);

      fputc ('\n', asm_out_file);
    }

  return 4;
}

/* Output the address of SYMBOL.  Also output COMMENT if flag_verbose_asm is
   set.  Return the address size.  Just return the size if DOSIZEONLY is
   nonzero.  */

static int
write_debug_addr (char *symbol, const char *comment, int dosizeonly)
{
  if (!dosizeonly)
    {
      ASM_OUTPUT_DEBUG_ADDR (asm_out_file, symbol);
      if (flag_verbose_asm)
	fprintf (asm_out_file, "\t%s %s", ASM_COMMENT_START, comment);
      fputc ('\n', asm_out_file);
    }

  return PTR_SIZE;
}

/* Output the single byte DATA1.  Also output COMMENT if flag_verbose_asm is
   set.  Return the data size.  Just return the size if DOSIZEONLY is
   nonzero.  */

static int
write_debug_data1 (unsigned int data1, const char *comment, int dosizeonly)
{
  if (!dosizeonly)
    {
      ASM_OUTPUT_DEBUG_DATA1 (asm_out_file, data1);
      if (flag_verbose_asm)
	fprintf (asm_out_file, "\t%s %s", ASM_COMMENT_START, comment);
      fputc ('\n', asm_out_file);
    }

  return 1;
}

/* Output the single word DATA2.  Also output COMMENT if flag_verbose_asm is
   set.  Return the data size.  Just return the size if DOSIZEONLY is
   nonzero.  */

static int
write_debug_data2 (unsigned int data2, const char *comment, int dosizeonly)
{
  if (!dosizeonly)
    {
      ASM_OUTPUT_DEBUG_DATA2 (asm_out_file, data2);
      if (flag_verbose_asm)
	fprintf (asm_out_file, "\t%s %s", ASM_COMMENT_START, comment);
      fputc ('\n', asm_out_file);
    }

  return 2;
}

/* Output double word DATA4.  Also output COMMENT if flag_verbose_asm is set.
   Return the data size.  Just return the size if DOSIZEONLY is nonzero.  */

static int
write_debug_data4 (unsigned long data4, const char *comment, int dosizeonly)
{
  if (!dosizeonly)
    {
      ASM_OUTPUT_DEBUG_DATA4 (asm_out_file, data4);
      if (flag_verbose_asm)
	fprintf (asm_out_file, "\t%s %s", ASM_COMMENT_START, comment);
      fputc ('\n', asm_out_file);
    }

  return 4;
}

/* Output quad word DATA8.  Also output COMMENT if flag_verbose_asm is set.
   Return the data size.  Just return the size if DOSIZEONLY is nonzero.  */

static int
write_debug_data8 (unsigned long long data8, const char *comment,
		   int dosizeonly)
{
  if (!dosizeonly)
    {
      ASM_OUTPUT_DEBUG_DATA8 (asm_out_file, data8);
      if (flag_verbose_asm)
	fprintf (asm_out_file, "\t%s %s", ASM_COMMENT_START, comment);
      fputc ('\n', asm_out_file);
    }

  return 8;
}

/* Output the difference between LABEL1 and LABEL2.  Also output COMMENT if
   flag_verbose_asm is set.  Return the data size.  Just return the size if
   DOSIZEONLY is nonzero.  */

static int
write_debug_delta4 (char *label1, char *label2, const char *comment,
		    int dosizeonly)
{
  if (!dosizeonly)
    {
      ASM_OUTPUT_DEBUG_DELTA4 (asm_out_file, label1, label2);
      if (flag_verbose_asm)
	fprintf (asm_out_file, "\t%s %s", ASM_COMMENT_START, comment);
      fputc ('\n', asm_out_file);
    }

  return 4;
}

/* Output a character string STRING.  Also write COMMENT if flag_verbose_asm is
   set.  Return the string length.  Just return the length if DOSIZEONLY is
   nonzero.  */

static int
write_debug_string (char *string, const char *comment, int dosizeonly)
{
  if (!dosizeonly)
    {
      ASM_OUTPUT_DEBUG_STRING (asm_out_file, string);
      if (flag_verbose_asm)
	fprintf (asm_out_file, "\t%s %s", ASM_COMMENT_START, comment);
      fputc ('\n', asm_out_file);
    }

  return strlen (string);
}

/* Output a module begin header and return the header size.  Just return the
   size if DOSIZEONLY is nonzero.  */

static int
write_modbeg (int dosizeonly)
{
  DST_MODULE_BEGIN modbeg;
  DST_MB_TRLR mb_trlr;
  int i;
  char *module_name, *m;
  int modnamelen;
  int prodnamelen;
  int totsize = 0;

  /* Assumes primary filename has Unix syntax file spec.  */
  module_name = xstrdup (basename ((char *) primary_filename));

  m = strrchr (module_name, '.');
  if (m)
    *m = 0;

  modnamelen = strlen (module_name);
  for (i = 0; i < modnamelen; i++)
    module_name[i] = TOUPPER (module_name[i]);

  prodnamelen = strlen (module_producer);

  modbeg.dst_a_modbeg_header.dst__header_length.dst_w_length
    = DST_K_MODBEG_SIZE + modnamelen + DST_K_MB_TRLR_SIZE + prodnamelen - 1;
  modbeg.dst_a_modbeg_header.dst__header_type.dst_w_type = DST_K_MODBEG;
  modbeg.dst_b_modbeg_flags.dst_v_modbeg_hide = 0;
  modbeg.dst_b_modbeg_flags.dst_v_modbeg_version = 1;
  modbeg.dst_b_modbeg_flags.dst_v_modbeg_unused = 0;
  modbeg.dst_b_modbeg_unused = 0;
  modbeg.dst_l_modbeg_language = module_language;
  modbeg.dst_w_version_major = DST_K_VERSION_MAJOR;
  modbeg.dst_w_version_minor = DST_K_VERSION_MINOR;
  modbeg.dst_b_modbeg_name = strlen (module_name);

  mb_trlr.dst_b_compiler = strlen (module_producer);

  totsize += write_debug_header (&modbeg.dst_a_modbeg_header,
				 "modbeg", dosizeonly);
  totsize += write_debug_data1 (*((char *) &modbeg.dst_b_modbeg_flags),
				"flags", dosizeonly);
  totsize += write_debug_data1 (modbeg.dst_b_modbeg_unused,
				"unused", dosizeonly);
  totsize += write_debug_data4 (modbeg.dst_l_modbeg_language,
				"language", dosizeonly);
  totsize += write_debug_data2 (modbeg.dst_w_version_major,
				"DST major version", dosizeonly);
  totsize += write_debug_data2 (modbeg.dst_w_version_minor,
				"DST minor version", dosizeonly);
  totsize += write_debug_data1 (modbeg.dst_b_modbeg_name,
				"length of module name", dosizeonly);
  totsize += write_debug_string (module_name, "module name", dosizeonly);
  totsize += write_debug_data1 (mb_trlr.dst_b_compiler,
				"length of compiler name", dosizeonly);
  totsize += write_debug_string (module_producer, "compiler name", dosizeonly);

  return totsize;
}

/* Output a module end trailer and return the trailer size.   Just return
   the size if DOSIZEONLY is nonzero.  */

static int
write_modend (int dosizeonly)
{
  DST_MODULE_END modend;
  int totsize = 0;

  modend.dst_a_modend_header.dst__header_length.dst_w_length
   = DST_K_MODEND_SIZE - 1;
  modend.dst_a_modend_header.dst__header_type.dst_w_type = DST_K_MODEND;

  totsize += write_debug_header (&modend.dst_a_modend_header, "modend",
				 dosizeonly);

  return totsize;
}

/* Output a routine begin header routine RTNNUM and return the header size.
   Just return the size if DOSIZEONLY is nonzero.  */

static int
write_rtnbeg (int rtnnum, int dosizeonly)
{
  char *rtnname;
  int rtnnamelen;
  char *rtnentryname;
  int totsize = 0;
  char label[MAX_ARTIFICIAL_LABEL_BYTES];
  DST_ROUTINE_BEGIN rtnbeg;
  DST_PROLOG prolog;
  vms_func_ref fde = &func_table[rtnnum];

  rtnname = (char *)fde->vms_func_name;
  rtnnamelen = strlen (rtnname);
  rtnentryname = concat (rtnname, "..en", NULL);

  if (!strcmp (rtnname, "main"))
    {
      DST_HEADER header;
      const char *go = "TRANSFER$BREAK$GO";

      /* This command isn't documented in DSTRECORDS, so it's made to
	 look like what DEC C does */

      /* header size - 1st byte + flag byte + STO_LW size
	 + string count byte + string length */
      header.dst__header_length.dst_w_length
	= DST_K_DST_HEADER_SIZE - 1 + 1 + 4 + 1 + strlen (go);
      header.dst__header_type.dst_w_type = 0x17;

      totsize += write_debug_header (&header, "transfer", dosizeonly);

      /* I think this is a flag byte, but I don't know what this flag means */
      totsize += write_debug_data1 (0x1, "flags ???", dosizeonly);

      /* Routine Begin PD Address */
      totsize += write_debug_addr (rtnname, "main procedure descriptor",
				   dosizeonly);
      totsize += write_debug_data1 (strlen (go), "length of main_name",
				    dosizeonly);
      totsize += write_debug_string ((char *) go, "main name", dosizeonly);
    }

  /* The header length never includes the length byte.  */
  rtnbeg.dst_a_rtnbeg_header.dst__header_length.dst_w_length
   = DST_K_RTNBEG_SIZE + rtnnamelen - 1;
  rtnbeg.dst_a_rtnbeg_header.dst__header_type.dst_w_type = DST_K_RTNBEG;
  rtnbeg.dst_b_rtnbeg_flags.dst_v_rtnbeg_unused = 0;
  rtnbeg.dst_b_rtnbeg_flags.dst_v_rtnbeg_unalloc = 0;
  rtnbeg.dst_b_rtnbeg_flags.dst_v_rtnbeg_prototype = 0;
  rtnbeg.dst_b_rtnbeg_flags.dst_v_rtnbeg_inlined = 0;
  rtnbeg.dst_b_rtnbeg_flags.dst_v_rtnbeg_no_call = 1;
  rtnbeg.dst_b_rtnbeg_name = rtnnamelen;

  totsize += write_debug_header (&rtnbeg.dst_a_rtnbeg_header, "rtnbeg",
				 dosizeonly);
  totsize += write_debug_data1 (*((char *) &rtnbeg.dst_b_rtnbeg_flags),
				"flags", dosizeonly);

  /* Routine Begin Address */
  totsize += write_debug_addr (rtnentryname, "routine entry name", dosizeonly);

  /* Routine Begin PD Address */
  totsize += write_debug_addr (rtnname, "routine procedure descriptor",
			       dosizeonly);

  /* Routine Begin Name */
  totsize += write_debug_data1 (rtnbeg.dst_b_rtnbeg_name,
				"length of routine name", dosizeonly);

  totsize += write_debug_string (rtnname, "routine name", dosizeonly);

  free (rtnentryname);

  if (debug_info_level > DINFO_LEVEL_TERSE)
    {
      prolog.dst_a_prolog_header.dst__header_length.dst_w_length
	= DST_K_PROLOG_SIZE - 1;
      prolog.dst_a_prolog_header.dst__header_type.dst_w_type = DST_K_PROLOG;

      totsize += write_debug_header (&prolog.dst_a_prolog_header, "prolog",
				     dosizeonly);

      ASM_GENERATE_INTERNAL_LABEL (label, FUNC_PROLOG_LABEL, fde->funcdef_number);
      totsize += write_debug_addr (label, "prolog breakpoint addr",
				   dosizeonly);
    }

  return totsize;
}

/* Output a routine end trailer for routine RTNNUM and return the header size.
   Just return the size if DOSIZEONLY is nonzero.  */

static int
write_rtnend (int rtnnum, int dosizeonly)
{
  DST_ROUTINE_END rtnend;
  char label1[MAX_ARTIFICIAL_LABEL_BYTES];
  char label2[MAX_ARTIFICIAL_LABEL_BYTES];
  int totsize;
  vms_func_ref fde = &func_table[rtnnum];
  int corrected_rtnnum = fde->funcdef_number;

  totsize = 0;

  rtnend.dst_a_rtnend_header.dst__header_length.dst_w_length
   = DST_K_RTNEND_SIZE - 1;
  rtnend.dst_a_rtnend_header.dst__header_type.dst_w_type = DST_K_RTNEND;
  rtnend.dst_b_rtnend_unused = 0;
  rtnend.dst_l_rtnend_size = 0; /* Calculated below.  */

  totsize += write_debug_header (&rtnend.dst_a_rtnend_header, "rtnend",
				 dosizeonly);
  totsize += write_debug_data1 (rtnend.dst_b_rtnend_unused, "unused",
				dosizeonly);

  ASM_GENERATE_INTERNAL_LABEL (label1, FUNC_BEGIN_LABEL, corrected_rtnnum);
  ASM_GENERATE_INTERNAL_LABEL (label2, FUNC_END_LABEL, corrected_rtnnum);
  totsize += write_debug_delta4 (label2, label1, "routine size", dosizeonly);

  return totsize;
}

#define K_DELTA_PC(I) \
 ((I) < 128 ? -(I) : (I) < 65536 ? DST_K_DELTA_PC_W : DST_K_DELTA_PC_L)

#define K_SET_LINUM(I) \
 ((I) < 256 ? DST_K_SET_LINUM_B \
  : (I) < 65536 ? DST_K_SET_LINUM : DST_K_SET_LINUM_L)

#define K_INCR_LINUM(I) \
 ((I) < 256 ? DST_K_INCR_LINUM \
  : (I) < 65536 ? DST_K_INCR_LINUM_W : DST_K_INCR_LINUM_L)

/* Output the PC to line number correlations and return the size.  Just return
   the size if DOSIZEONLY is nonzero */

static int
write_pclines (int dosizeonly)
{
  unsigned i;
  int fn;
  int ln, lastln;
  int linestart = 0;
  int max_line;
  DST_LINE_NUM_HEADER line_num;
  DST_PCLINE_COMMANDS pcline;
  char label[MAX_ARTIFICIAL_LABEL_BYTES];
  char lastlabel[MAX_ARTIFICIAL_LABEL_BYTES];
  int totsize = 0;
  char buff[256];

  max_line = file_info_table[1].max_line;
  file_info_table[1].listing_line_start = linestart;
  linestart = linestart + ((max_line / 100000) + 1) * 100000;

  for (i = 2; i < file_info_table_in_use; i++)
    {
      max_line = file_info_table[i].max_line;
      file_info_table[i].listing_line_start = linestart;
      linestart = linestart + ((max_line / 10000) + 1) * 10000;
    }

  /* Set starting address to beginning of text section.  */
  line_num.dst_a_line_num_header.dst__header_length.dst_w_length = 8;
  line_num.dst_a_line_num_header.dst__header_type.dst_w_type = DST_K_LINE_NUM;
  pcline.dst_b_pcline_command = DST_K_SET_ABS_PC;

  totsize += write_debug_header (&line_num.dst_a_line_num_header,
				 "line_num", dosizeonly);
  totsize += write_debug_data1 (pcline.dst_b_pcline_command,
				"line_num (SET ABS PC)", dosizeonly);

  if (dosizeonly)
    totsize += 4;
  else
    {
      ASM_OUTPUT_DEBUG_ADDR (asm_out_file, TEXT_SECTION_ASM_OP);
      if (flag_verbose_asm)
	fprintf (asm_out_file, "\t%s line_num", ASM_COMMENT_START);
      fputc ('\n', asm_out_file);
    }

  fn = line_info_table[1].dst_file_num;
  ln = (file_info_table[fn].listing_line_start
	+ line_info_table[1].dst_line_num);
  line_num.dst_a_line_num_header.dst__header_length.dst_w_length = 4 + 4;
  pcline.dst_b_pcline_command = DST_K_SET_LINUM_L;

  totsize += write_debug_header (&line_num.dst_a_line_num_header,
				 "line_num", dosizeonly);
  totsize += write_debug_data1 (pcline.dst_b_pcline_command,
				"line_num (SET LINUM LONG)", dosizeonly);

  sprintf (buff, "line_num (%d)", ln ? ln - 1 : 0);
  totsize += write_debug_data4 (ln ? ln - 1 : 0, buff, dosizeonly);

  lastln = ln;
  strcpy (lastlabel, TEXT_SECTION_ASM_OP);
  for (i = 1; i < line_info_table_in_use; i++)
    {
      int extrabytes;

      fn = line_info_table[i].dst_file_num;
      ln = (file_info_table[fn].listing_line_start
	    + line_info_table[i].dst_line_num);

      if (ln - lastln > 1)
	extrabytes = 5; /* NUMBYTES (ln - lastln - 1) + 1; */
      else if (ln <= lastln)
	extrabytes = 5; /* NUMBYTES (ln - 1) + 1; */
      else
	extrabytes = 0;

      line_num.dst_a_line_num_header.dst__header_length.dst_w_length
	= 8 + extrabytes;

      totsize += write_debug_header
	(&line_num.dst_a_line_num_header, "line_num", dosizeonly);

      if (ln - lastln > 1)
	{
	  int lndif = ln - lastln - 1;

	  /* K_INCR_LINUM (lndif); */
	  pcline.dst_b_pcline_command = DST_K_INCR_LINUM_L;

	  totsize += write_debug_data1 (pcline.dst_b_pcline_command,
					"line_num (INCR LINUM LONG)",
					dosizeonly);

	  sprintf (buff, "line_num (%d)", lndif);
	  totsize += write_debug_data4 (lndif, buff, dosizeonly);
	}
      else if (ln <= lastln)
	{
	  /* K_SET_LINUM (ln-1); */
	  pcline.dst_b_pcline_command = DST_K_SET_LINUM_L;

	  totsize += write_debug_data1 (pcline.dst_b_pcline_command,
					"line_num (SET LINUM LONG)",
					dosizeonly);

	  sprintf (buff, "line_num (%d)", ln - 1);
	  totsize += write_debug_data4 (ln - 1, buff, dosizeonly);
	}

      pcline.dst_b_pcline_command = DST_K_DELTA_PC_L;

      totsize += write_debug_data1 (pcline.dst_b_pcline_command,
				    "line_num (DELTA PC LONG)", dosizeonly);

      ASM_GENERATE_INTERNAL_LABEL (label, LINE_CODE_LABEL, i);
      totsize += write_debug_delta4 (label, lastlabel, "increment line_num",
				     dosizeonly);

      lastln = ln;
      strcpy (lastlabel, label);
    }

  return totsize;
}

/* Output a source correlation for file FILEID using information saved in
   FILE_INFO_ENTRY and return the size.  Just return the size if DOSIZEONLY is
   nonzero.  */

static int
write_srccorr (int fileid, dst_file_info_entry file_info_entry,
	       int dosizeonly)
{
  int src_command_size;
  int linesleft = file_info_entry.max_line;
  int linestart = file_info_entry.listing_line_start;
  int flen = file_info_entry.flen;
  int linestodo = 0;
  DST_SOURCE_CORR src_header;
  DST_SRC_COMMAND src_command;
  DST_SRC_COMMAND src_command_sf;
  DST_SRC_COMMAND src_command_sl;
  DST_SRC_COMMAND src_command_sr;
  DST_SRC_COMMAND src_command_dl;
  DST_SRC_CMDTRLR src_cmdtrlr;
  char buff[256];
  int totsize = 0;

  if (fileid == 1)
    {
      src_header.dst_a_source_corr_header.dst__header_length.dst_w_length
	= DST_K_SOURCE_CORR_HEADER_SIZE + 1 - 1;
      src_header.dst_a_source_corr_header.dst__header_type.dst_w_type
	= DST_K_SOURCE;
      src_command.dst_b_src_command = DST_K_SRC_FORMFEED;

      totsize += write_debug_header (&src_header.dst_a_source_corr_header,
				     "source corr", dosizeonly);

      totsize += write_debug_data1 (src_command.dst_b_src_command,
				    "source_corr (SRC FORMFEED)",
				    dosizeonly);
    }

  src_command_size
    = DST_K_SRC_COMMAND_SIZE + flen + DST_K_SRC_CMDTRLR_SIZE;
  src_command.dst_b_src_command = DST_K_SRC_DECLFILE;
  src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_b_src_df_length
    = src_command_size - 2;
  src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_b_src_df_flags = 0;
  src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_w_src_df_fileid
    = fileid;
  src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_q_src_df_rms_cdt
    = file_info_entry.cdt;
  src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_l_src_df_rms_ebk
    = file_info_entry.ebk;
  src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_w_src_df_rms_ffb
    = file_info_entry.ffb;
  src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_b_src_df_rms_rfo
    = file_info_entry.rfo;
  src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_b_src_df_filename
    = file_info_entry.flen;

  src_header.dst_a_source_corr_header.dst__header_length.dst_w_length
    = DST_K_SOURCE_CORR_HEADER_SIZE + src_command_size - 1;
  src_header.dst_a_source_corr_header.dst__header_type.dst_w_type
    = DST_K_SOURCE;

  src_cmdtrlr.dst_b_src_df_libmodname = 0;

  totsize += write_debug_header (&src_header.dst_a_source_corr_header,
				 "source corr", dosizeonly);
  totsize += write_debug_data1 (src_command.dst_b_src_command,
				"source_corr (DECL SRC FILE)", dosizeonly);
  totsize += write_debug_data1
    (src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_b_src_df_length,
     "source_corr (length)", dosizeonly);

  totsize += write_debug_data1
    (src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_b_src_df_flags,
     "source_corr (flags)", dosizeonly);

  totsize += write_debug_data2
    (src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_w_src_df_fileid,
     "source_corr (fileid)", dosizeonly);

  totsize += write_debug_data8
    (src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_q_src_df_rms_cdt,
     "source_corr (creation date)", dosizeonly);

  totsize += write_debug_data4
    (src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_l_src_df_rms_ebk,
     "source_corr (EOF block number)", dosizeonly);

  totsize += write_debug_data2
    (src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_w_src_df_rms_ffb,
     "source_corr (first free byte)", dosizeonly);

  totsize += write_debug_data1
    (src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_b_src_df_rms_rfo,
     "source_corr (record and file organization)", dosizeonly);

  totsize += write_debug_data1
    (src_command.dst_a_src_cmd_fields.dst_a_src_decl_src.dst_b_src_df_filename,
     "source_corr (filename length)", dosizeonly);

  totsize += write_debug_string (file_info_entry.file_name,
				 "source file name", dosizeonly);
  totsize += write_debug_data1 (src_cmdtrlr.dst_b_src_df_libmodname,
				"source_corr (libmodname)", dosizeonly);

  src_command_sf.dst_b_src_command = DST_K_SRC_SETFILE;
  src_command_sf.dst_a_src_cmd_fields.dst_w_src_unsword = fileid;

  src_command_sr.dst_b_src_command = DST_K_SRC_SETREC_W;
  src_command_sr.dst_a_src_cmd_fields.dst_w_src_unsword = 1;

  src_command_sl.dst_b_src_command = DST_K_SRC_SETLNUM_L;
  src_command_sl.dst_a_src_cmd_fields.dst_l_src_unslong = linestart + 1;

  src_command_dl.dst_b_src_command = DST_K_SRC_DEFLINES_W;

  if (linesleft > 65534)
    linesleft = linesleft - 65534, linestodo = 65534;
  else
    linestodo = linesleft, linesleft = 0;

  src_command_dl.dst_a_src_cmd_fields.dst_w_src_unsword = linestodo;

  src_header.dst_a_source_corr_header.dst__header_length.dst_w_length
    = DST_K_SOURCE_CORR_HEADER_SIZE + 3 + 3 + 5 + 3 - 1;
  src_header.dst_a_source_corr_header.dst__header_type.dst_w_type
    = DST_K_SOURCE;

  if (src_command_dl.dst_a_src_cmd_fields.dst_w_src_unsword)
    {
      totsize += write_debug_header (&src_header.dst_a_source_corr_header,
				     "source corr", dosizeonly);

      totsize += write_debug_data1 (src_command_sf.dst_b_src_command,
				    "source_corr (src setfile)", dosizeonly);

      totsize += write_debug_data2
	(src_command_sf.dst_a_src_cmd_fields.dst_w_src_unsword,
	 "source_corr (fileid)", dosizeonly);

      totsize += write_debug_data1 (src_command_sr.dst_b_src_command,
				    "source_corr (setrec)", dosizeonly);

      totsize += write_debug_data2
	(src_command_sr.dst_a_src_cmd_fields.dst_w_src_unsword,
	 "source_corr (recnum)", dosizeonly);

      totsize += write_debug_data1 (src_command_sl.dst_b_src_command,
				    "source_corr (setlnum)", dosizeonly);

      totsize += write_debug_data4
	(src_command_sl.dst_a_src_cmd_fields.dst_l_src_unslong,
	 "source_corr (linenum)", dosizeonly);

      totsize += write_debug_data1 (src_command_dl.dst_b_src_command,
				    "source_corr (deflines)", dosizeonly);

      sprintf (buff, "source_corr (%d)",
	       src_command_dl.dst_a_src_cmd_fields.dst_w_src_unsword);
      totsize += write_debug_data2
	(src_command_dl.dst_a_src_cmd_fields.dst_w_src_unsword,
	 buff, dosizeonly);

      while (linesleft > 0)
	{
	  src_header.dst_a_source_corr_header.dst__header_length.dst_w_length
	    = DST_K_SOURCE_CORR_HEADER_SIZE + 3 - 1;
	  src_header.dst_a_source_corr_header.dst__header_type.dst_w_type
	    = DST_K_SOURCE;
	  src_command_dl.dst_b_src_command = DST_K_SRC_DEFLINES_W;

	  if (linesleft > 65534)
	    linesleft = linesleft - 65534, linestodo = 65534;
	  else
	    linestodo = linesleft, linesleft = 0;

	  src_command_dl.dst_a_src_cmd_fields.dst_w_src_unsword = linestodo;

	  totsize += write_debug_header (&src_header.dst_a_source_corr_header,
					 "source corr", dosizeonly);
	  totsize += write_debug_data1 (src_command_dl.dst_b_src_command,
					"source_corr (deflines)", dosizeonly);
	  sprintf (buff, "source_corr (%d)",
		   src_command_dl.dst_a_src_cmd_fields.dst_w_src_unsword);
	  totsize += write_debug_data2
	    (src_command_dl.dst_a_src_cmd_fields.dst_w_src_unsword,
	     buff, dosizeonly);
	}
    }

  return totsize;
}

/* Output all the source correlation entries and return the size.  Just return
   the size if DOSIZEONLY is nonzero.  */

static int
write_srccorrs (int dosizeonly)
{
  unsigned int i;
  int totsize = 0;

  for (i = 1; i < file_info_table_in_use; i++)
    totsize += write_srccorr (i, file_info_table[i], dosizeonly);

  return totsize;
}

/* Output a marker (i.e. a label) for the beginning of a function, before
   the prologue.  */

static void
vmsdbgout_begin_prologue (unsigned int line, const char *file)
{
  char label[MAX_ARTIFICIAL_LABEL_BYTES];

  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.begin_prologue) (line, file);

  if (debug_info_level > DINFO_LEVEL_NONE)
    {
      ASM_GENERATE_INTERNAL_LABEL (label, FUNC_BEGIN_LABEL,
				   current_function_funcdef_no);
      ASM_OUTPUT_LABEL (asm_out_file, label);
    }
}

/* Output a marker (i.e. a label) for the beginning of a function, after
   the prologue.  */

static void
vmsdbgout_end_prologue (unsigned int line, const char *file)
{
  char label[MAX_ARTIFICIAL_LABEL_BYTES];

  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.end_prologue) (line, file);

  if (debug_info_level > DINFO_LEVEL_TERSE)
    {
      ASM_GENERATE_INTERNAL_LABEL (label, FUNC_PROLOG_LABEL,
				   current_function_funcdef_no);
      ASM_OUTPUT_LABEL (asm_out_file, label);

      /* VMS PCA expects every PC range to correlate to some line and file.  */
      vmsdbgout_source_line (line, file);
    }
}

/* No output for VMS debug, but make obligatory call to Dwarf2 debug */

static void
vmsdbgout_end_function (unsigned int line)
{
  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.end_function) (line);
}

/* Output a marker (i.e. a label) for the absolute end of the generated code
   for a function definition.  This gets called *after* the epilogue code has
   been generated.  */

static void
vmsdbgout_end_epilogue (unsigned int line, const char *file)
{
  char label[MAX_ARTIFICIAL_LABEL_BYTES];

  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.end_epilogue) (line, file);

  if (debug_info_level > DINFO_LEVEL_NONE)
    {
      /* Output a label to mark the endpoint of the code generated for this
         function.  */
      ASM_GENERATE_INTERNAL_LABEL (label, FUNC_END_LABEL,
				   current_function_funcdef_no);
      ASM_OUTPUT_LABEL (asm_out_file, label);

      /* VMS PCA expects every PC range to correlate to some line and file.  */
      vmsdbgout_source_line (line, file);
    }
}

/* Output a marker (i.e. a label) for the beginning of the generated code for
   a lexical block.  */

static void
vmsdbgout_begin_block (register unsigned line, register unsigned blocknum)
{
  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.begin_block) (line, blocknum);

  if (debug_info_level > DINFO_LEVEL_TERSE)
    targetm.asm_out.internal_label (asm_out_file, BLOCK_BEGIN_LABEL, blocknum);
}

/* Output a marker (i.e. a label) for the end of the generated code for a
   lexical block.  */

static void
vmsdbgout_end_block (register unsigned line, register unsigned blocknum)
{
  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.end_block) (line, blocknum);

  if (debug_info_level > DINFO_LEVEL_TERSE)
    targetm.asm_out.internal_label (asm_out_file, BLOCK_END_LABEL, blocknum);
}

/* Not implemented in VMS Debug.  */

static bool
vmsdbgout_ignore_block (tree block)
{
  bool retval = 0;

  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    retval = (*dwarf2_debug_hooks.ignore_block) (block);

  return retval;
}

/* Add an entry for function DECL into the func_table.  */

static void
vmsdbgout_begin_function (tree decl)
{
  const char *name = XSTR (XEXP (DECL_RTL (decl), 0), 0);
  vms_func_ref fde;

  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.begin_function) (decl);

  if (func_table_in_use == func_table_allocated)
    {
      func_table_allocated += FUNC_TABLE_INCREMENT;
      func_table
        = (vms_func_ref) xrealloc (func_table,
				   func_table_allocated * sizeof (vms_func_node));
    }

  /* Add the new entry to the end of the function name table.  */
  fde = &func_table[func_table_in_use++];
  fde->vms_func_name = xstrdup (name);
  fde->funcdef_number = current_function_funcdef_no;

}

static char fullname_buff [4096];

/* Return the full file specification for FILENAME.  The specification must be
   in VMS syntax in order to be processed by VMS Debug.  */

static char *
full_name (const char *filename)
{
#ifdef VMS
  FILE *fp = fopen (filename, "r");

  fgetname (fp, fullname_buff, 1);
  fclose (fp);
#else
  getcwd (fullname_buff, sizeof (fullname_buff));

  strcat (fullname_buff, "/");
  strcat (fullname_buff, filename);

  /* ??? Insert hairy code here to translate Unix style file specification
     to VMS style.  */
#endif

  return fullname_buff;
}

/* Lookup a filename (in the list of filenames that we know about here in
   vmsdbgout.c) and return its "index".  The index of each (known) filename is
   just a unique number which is associated with only that one filename.  We
   need such numbers for the sake of generating labels  and references
   to those files numbers.  If the filename given as an argument is not
   found in our current list, add it to the list and assign it the next
   available unique index number.  In order to speed up searches, we remember
   the index of the filename was looked up last.  This handles the majority of
   all searches.  */

static unsigned int
lookup_filename (const char *file_name)
{
  static unsigned int last_file_lookup_index = 0;
  register char *fn;
  register unsigned i;
  char *fnam;
  long long cdt;
  long ebk;
  short ffb;
  char rfo;
  char flen;
  struct stat statbuf;

  if (stat (file_name, &statbuf) == 0)
    {
      long gmtoff;
#ifdef VMS
      struct tm *ts;

      /* Adjust for GMT.  */
      ts = (struct tm *) localtime (&statbuf.st_ctime);
      gmtoff = ts->tm_gmtoff;

      /* VMS has multiple file format types.  */
      rfo = statbuf.st_fab_rfm;
#else
      /* Is GMT adjustment an issue with a cross-compiler? */
      gmtoff = 0;

      /* Assume stream LF type file.  */
      rfo = 2;
#endif
      cdt = 10000000 * (statbuf.st_ctime + gmtoff + vms_epoch_offset);
      ebk = statbuf.st_size / 512 + 1;
      ffb = statbuf.st_size - ((statbuf.st_size / 512) * 512);
      fnam = full_name (file_name);
      flen = strlen (fnam);
    }
  else
    {
      cdt = 0;
      ebk = 0;
      ffb = 0;
      rfo = 0;
      fnam = (char *) "";
      flen = 0;
    }

  /* Check to see if the file name that was searched on the previous call
     matches this file name. If so, return the index.  */
  if (last_file_lookup_index != 0)
    {
      fn = file_info_table[last_file_lookup_index].file_name;
      if (strcmp (fnam, fn) == 0)
	return last_file_lookup_index;
    }

  /* Didn't match the previous lookup, search the table */
  for (i = 1; i < file_info_table_in_use; ++i)
    {
      fn = file_info_table[i].file_name;
      if (strcmp (fnam, fn) == 0)
	{
	  last_file_lookup_index = i;
	  return i;
	}
    }

  /* Prepare to add a new table entry by making sure there is enough space in
     the table to do so.  If not, expand the current table.  */
  if (file_info_table_in_use == file_info_table_allocated)
    {

      file_info_table_allocated += FILE_TABLE_INCREMENT;
      file_info_table = xrealloc (file_info_table,
				  (file_info_table_allocated
				   * sizeof (dst_file_info_entry)));
    }

  /* Add the new entry to the end of the filename table.  */
  file_info_table[file_info_table_in_use].file_name = xstrdup (fnam);
  file_info_table[file_info_table_in_use].max_line = 0;
  file_info_table[file_info_table_in_use].cdt = cdt;
  file_info_table[file_info_table_in_use].ebk = ebk;
  file_info_table[file_info_table_in_use].ffb = ffb;
  file_info_table[file_info_table_in_use].rfo = rfo;
  file_info_table[file_info_table_in_use].flen = flen;

  last_file_lookup_index = file_info_table_in_use++;
  return last_file_lookup_index;
}

/* Output a label to mark the beginning of a source code line entry
   and record information relating to this source line, in
   'line_info_table' for later output of the .debug_line section.  */

static void
vmsdbgout_source_line (register unsigned line, register const char *filename)
{
  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.source_line) (line, filename);

  if (debug_info_level >= DINFO_LEVEL_TERSE)
    {
      dst_line_info_ref line_info;

      targetm.asm_out.internal_label (asm_out_file, LINE_CODE_LABEL,
				      line_info_table_in_use);

      /* Expand the line info table if necessary.  */
      if (line_info_table_in_use == line_info_table_allocated)
	{
	  line_info_table_allocated += LINE_INFO_TABLE_INCREMENT;
	  line_info_table = xrealloc (line_info_table,
				      (line_info_table_allocated
				       * sizeof (dst_line_info_entry)));
	}

      /* Add the new entry at the end of the line_info_table.  */
      line_info = &line_info_table[line_info_table_in_use++];
      line_info->dst_file_num = lookup_filename (filename);
      line_info->dst_line_num = line;
      if (line > file_info_table[line_info->dst_file_num].max_line)
	file_info_table[line_info->dst_file_num].max_line = line;
    }
}

/* Record the beginning of a new source file, for later output.
   At present, unimplemented.  */

static void
vmsdbgout_start_source_file (unsigned int lineno, const char *filename)
{
  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.start_source_file) (lineno, filename);
}

/* Record the end of a source file, for later output.
   At present, unimplemented.  */

static void
vmsdbgout_end_source_file (unsigned int lineno ATTRIBUTE_UNUSED)
{
  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.end_source_file) (lineno);
}

/* Set up for Debug output at the start of compilation.  */

static void
vmsdbgout_init (const char *main_input_filename)
{
  const char *language_string = lang_hooks.name;

  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.init) (main_input_filename);

  if (debug_info_level == DINFO_LEVEL_NONE)
    return;

  /* Remember the name of the primary input file.  */
  primary_filename = main_input_filename;

  /* Allocate the initial hunk of the file_info_table.  */
  file_info_table
    = xcalloc (FILE_TABLE_INCREMENT, sizeof (dst_file_info_entry));
  file_info_table_allocated = FILE_TABLE_INCREMENT;

  /* Skip the first entry - file numbers begin at 1 */
  file_info_table_in_use = 1;

  func_table = (vms_func_ref) xcalloc (FUNC_TABLE_INCREMENT, sizeof (vms_func_node));
  func_table_allocated = FUNC_TABLE_INCREMENT;
  func_table_in_use = 1;

  /* Allocate the initial hunk of the line_info_table.  */
  line_info_table
    = xcalloc (LINE_INFO_TABLE_INCREMENT, sizeof (dst_line_info_entry));
  line_info_table_allocated = LINE_INFO_TABLE_INCREMENT;
  /* zero-th entry is allocated, but unused */
  line_info_table_in_use = 1;

  lookup_filename (primary_filename);

  if (!strcmp (language_string, "GNU C"))
    module_language = DST_K_C;
  else if (!strcmp (language_string, "GNU C++"))
    module_language = DST_K_CXX;
  else if (!strcmp (language_string, "GNU Ada"))
    module_language = DST_K_ADA;
  else if (!strcmp (language_string, "GNU F77"))
    module_language = DST_K_FORTRAN;
  else
    module_language = DST_K_UNKNOWN;

  module_producer = concat (language_string, " ", version_string, NULL);

  ASM_GENERATE_INTERNAL_LABEL (text_end_label, TEXT_END_LABEL, 0);

}

/* Not implemented in VMS Debug.  */

static void
vmsdbgout_define (unsigned int lineno, const char *buffer)
{
  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.define) (lineno, buffer);
}

/* Not implemented in VMS Debug.  */

static void
vmsdbgout_undef (unsigned int lineno, const char *buffer)
{
  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.undef) (lineno, buffer);
}

/* Not implemented in VMS Debug.  */

static void
vmsdbgout_decl (tree decl)
{
  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.function_decl) (decl);
}

/* Not implemented in VMS Debug.  */

static void
vmsdbgout_global_decl (tree decl)
{
  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.global_decl) (decl);
}

/* Not implemented in VMS Debug.  */

static void
vmsdbgout_abstract_function (tree decl)
{
  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.outlining_inline_function) (decl);
}

/* Output stuff that Debug requires at the end of every file and generate the
   VMS Debug debugging info.  */

static void
vmsdbgout_finish (const char *main_input_filename ATTRIBUTE_UNUSED)
{
  unsigned int i;
  int totsize;

  if (write_symbols == VMS_AND_DWARF2_DEBUG)
    (*dwarf2_debug_hooks.finish) (main_input_filename);

  if (debug_info_level == DINFO_LEVEL_NONE)
    return;

  /* Output a terminator label for the .text section.  */
  switch_to_section (text_section);
  targetm.asm_out.internal_label (asm_out_file, TEXT_END_LABEL, 0);

  /* Output debugging information.
     Warning! Do not change the name of the .vmsdebug section without
     changing it in the assembler also.  */
  switch_to_section (get_named_section (NULL, ".vmsdebug", 0));
  ASM_OUTPUT_ALIGN (asm_out_file, 0);

  totsize = write_modbeg (1);
  for (i = 1; i < func_table_in_use; i++)
    {
      totsize += write_rtnbeg (i, 1);
      totsize += write_rtnend (i, 1);
    }
  totsize += write_pclines (1);

  write_modbeg (0);
  for (i = 1; i < func_table_in_use; i++)
    {
      write_rtnbeg (i, 0);
      write_rtnend (i, 0);
    }
  write_pclines (0);

  if (debug_info_level > DINFO_LEVEL_TERSE)
    {
      totsize = write_srccorrs (1);
      write_srccorrs (0);
    }

  totsize = write_modend (1);
  write_modend (0);
}
#endif /* VMS_DEBUGGING_INFO */

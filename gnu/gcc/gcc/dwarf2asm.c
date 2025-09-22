/* Dwarf2 assembler output helper routines.
   Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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
#include "flags.h"
#include "tree.h"
#include "rtl.h"
#include "output.h"
#include "target.h"
#include "dwarf2asm.h"
#include "dwarf2.h"
#include "splay-tree.h"
#include "ggc.h"
#include "tm_p.h"


/* How to start an assembler comment.  */
#ifndef ASM_COMMENT_START
#define ASM_COMMENT_START ";#"
#endif


/* Output an unaligned integer with the given value and size.  Prefer not
   to print a newline, since the caller may want to add a comment.  */

void
dw2_assemble_integer (int size, rtx x)
{
  const char *op = integer_asm_op (size, FALSE);

  if (op)
    {
      fputs (op, asm_out_file);
      if (GET_CODE (x) == CONST_INT)
	fprintf (asm_out_file, HOST_WIDE_INT_PRINT_HEX, INTVAL (x));
      else
	output_addr_const (asm_out_file, x);
    }
  else
    assemble_integer (x, size, BITS_PER_UNIT, 1);
}


/* Output an immediate constant in a given size.  */

void
dw2_asm_output_data (int size, unsigned HOST_WIDE_INT value,
		     const char *comment, ...)
{
  va_list ap;
  const char *op = integer_asm_op (size, FALSE);

  va_start (ap, comment);

  if (size * 8 < HOST_BITS_PER_WIDE_INT)
    value &= ~(~(unsigned HOST_WIDE_INT) 0 << (size * 8));

  if (op)
    fprintf (asm_out_file, "%s" HOST_WIDE_INT_PRINT_HEX, op, value);
  else
    assemble_integer (GEN_INT (value), size, BITS_PER_UNIT, 1);

  if (flag_debug_asm && comment)
    {
      fprintf (asm_out_file, "\t%s ", ASM_COMMENT_START);
      vfprintf (asm_out_file, comment, ap);
    }
  fputc ('\n', asm_out_file);

  va_end (ap);
}

/* Output the difference between two symbols in a given size.  */
/* ??? There appear to be assemblers that do not like such
   subtraction, but do support ASM_SET_OP.  It's unfortunately
   impossible to do here, since the ASM_SET_OP for the difference
   symbol must appear after both symbols are defined.  */

void
dw2_asm_output_delta (int size, const char *lab1, const char *lab2,
		      const char *comment, ...)
{
  va_list ap;

  va_start (ap, comment);

#ifdef ASM_OUTPUT_DWARF_DELTA
  ASM_OUTPUT_DWARF_DELTA (asm_out_file, size, lab1, lab2);
#else
  dw2_assemble_integer (size,
			gen_rtx_MINUS (Pmode,
				       gen_rtx_SYMBOL_REF (Pmode, lab1),
				       gen_rtx_SYMBOL_REF (Pmode, lab2)));
#endif
  if (flag_debug_asm && comment)
    {
      fprintf (asm_out_file, "\t%s ", ASM_COMMENT_START);
      vfprintf (asm_out_file, comment, ap);
    }
  fputc ('\n', asm_out_file);

  va_end (ap);
}

/* Output a section-relative reference to a LABEL, which was placed in
   BASE.  In general this can only be done for debugging symbols.
   E.g. on most targets with the GNU linker, this is accomplished with
   a direct reference and the knowledge that the debugging section
   will be placed at VMA 0.  Some targets have special relocations for
   this that we must use.  */

void
dw2_asm_output_offset (int size, const char *label,
		       section *base ATTRIBUTE_UNUSED,
		       const char *comment, ...)
{
  va_list ap;

  va_start (ap, comment);

#ifdef ASM_OUTPUT_DWARF_OFFSET
  ASM_OUTPUT_DWARF_OFFSET (asm_out_file, size, label, base);
#else
  dw2_assemble_integer (size, gen_rtx_SYMBOL_REF (Pmode, label));
#endif

  if (flag_debug_asm && comment)
    {
      fprintf (asm_out_file, "\t%s ", ASM_COMMENT_START);
      vfprintf (asm_out_file, comment, ap);
    }
  fputc ('\n', asm_out_file);

  va_end (ap);
}

#if 0

/* Output a self-relative reference to a label, possibly in a
   different section or object file.  */

void
dw2_asm_output_pcrel (int size ATTRIBUTE_UNUSED,
		      const char *label ATTRIBUTE_UNUSED,
		      const char *comment, ...)
{
  va_list ap;

  va_start (ap, comment);

#ifdef ASM_OUTPUT_DWARF_PCREL
  ASM_OUTPUT_DWARF_PCREL (asm_out_file, size, label);
#else
  dw2_assemble_integer (size,
			gen_rtx_MINUS (Pmode,
				       gen_rtx_SYMBOL_REF (Pmode, label),
				       pc_rtx));
#endif

  if (flag_debug_asm && comment)
    {
      fprintf (asm_out_file, "\t%s ", ASM_COMMENT_START);
      vfprintf (asm_out_file, comment, ap);
    }
  fputc ('\n', asm_out_file);

  va_end (ap);
}
#endif /* 0 */

/* Output an absolute reference to a label.  */

void
dw2_asm_output_addr (int size, const char *label,
		     const char *comment, ...)
{
  va_list ap;

  va_start (ap, comment);

  dw2_assemble_integer (size, gen_rtx_SYMBOL_REF (Pmode, label));

  if (flag_debug_asm && comment)
    {
      fprintf (asm_out_file, "\t%s ", ASM_COMMENT_START);
      vfprintf (asm_out_file, comment, ap);
    }
  fputc ('\n', asm_out_file);

  va_end (ap);
}

/* Similar, but use an RTX expression instead of a text label.  */

void
dw2_asm_output_addr_rtx (int size, rtx addr,
			 const char *comment, ...)
{
  va_list ap;

  va_start (ap, comment);

  dw2_assemble_integer (size, addr);

  if (flag_debug_asm && comment)
    {
      fprintf (asm_out_file, "\t%s ", ASM_COMMENT_START);
      vfprintf (asm_out_file, comment, ap);
    }
  fputc ('\n', asm_out_file);

  va_end (ap);
}

/* Output the first ORIG_LEN characters of STR as a string.
   If ORIG_LEN is equal to -1, ignore this parameter and output
   the entire STR instead.
   If COMMENT is not NULL and comments in the debug information
   have been requested by the user, append the given COMMENT
   to the generated output.  */
   
void
dw2_asm_output_nstring (const char *str, size_t orig_len,
			const char *comment, ...)
{
  size_t i, len;
  va_list ap;

  va_start (ap, comment);

  len = orig_len;

  if (len == (size_t) -1)
    len = strlen (str);

  if (flag_debug_asm && comment)
    {
      fputs ("\t.ascii \"", asm_out_file);
      for (i = 0; i < len; i++)
	{
	  int c = str[i];
	  if (c == '\"' || c == '\\')
	    fputc ('\\', asm_out_file);
	  if (ISPRINT(c))
	    fputc (c, asm_out_file);
	  else
	    fprintf (asm_out_file, "\\%o", c);
	}
      fprintf (asm_out_file, "\\0\"\t%s ", ASM_COMMENT_START);
      vfprintf (asm_out_file, comment, ap);
      fputc ('\n', asm_out_file);
    }
  else
    {
      /* If an explicit length was given, we can't assume there
	 is a null termination in the string buffer.  */
      if (orig_len == (size_t) -1)
	len += 1;
      ASM_OUTPUT_ASCII (asm_out_file, str, len);
      if (orig_len != (size_t) -1)
	assemble_integer (const0_rtx, 1, BITS_PER_UNIT, 1);
    }

  va_end (ap);
}


/* Return the size of an unsigned LEB128 quantity.  */

int
size_of_uleb128 (unsigned HOST_WIDE_INT value)
{
  int size = 0;

  do
    {
      value >>= 7;
      size += 1;
    }
  while (value != 0);

  return size;
}

/* Return the size of a signed LEB128 quantity.  */

int
size_of_sleb128 (HOST_WIDE_INT value)
{
  int size = 0, byte;

  do
    {
      byte = (value & 0x7f);
      value >>= 7;
      size += 1;
    }
  while (!((value == 0 && (byte & 0x40) == 0)
	   || (value == -1 && (byte & 0x40) != 0)));

  return size;
}

/* Given an encoding, return the number of bytes the format occupies.
   This is only defined for fixed-size encodings, and so does not
   include leb128.  */

int
size_of_encoded_value (int encoding)
{
  if (encoding == DW_EH_PE_omit)
    return 0;

  switch (encoding & 0x07)
    {
    case DW_EH_PE_absptr:
      return POINTER_SIZE / BITS_PER_UNIT;
    case DW_EH_PE_udata2:
      return 2;
    case DW_EH_PE_udata4:
      return 4;
    case DW_EH_PE_udata8:
      return 8;
    default:
      gcc_unreachable ();
    }
}

/* Yield a name for a given pointer encoding.  */

const char *
eh_data_format_name (int format)
{
#if HAVE_DESIGNATED_INITIALIZERS
#define S(p, v)		[p] = v,
#else
#define S(p, v)		case p: return v;
#endif

#if HAVE_DESIGNATED_INITIALIZERS
  __extension__ static const char * const format_names[256] = {
#else
  switch (format) {
#endif

  S(DW_EH_PE_absptr, "absolute")
  S(DW_EH_PE_omit, "omit")
  S(DW_EH_PE_aligned, "aligned absolute")

  S(DW_EH_PE_uleb128, "uleb128")
  S(DW_EH_PE_udata2, "udata2")
  S(DW_EH_PE_udata4, "udata4")
  S(DW_EH_PE_udata8, "udata8")
  S(DW_EH_PE_sleb128, "sleb128")
  S(DW_EH_PE_sdata2, "sdata2")
  S(DW_EH_PE_sdata4, "sdata4")
  S(DW_EH_PE_sdata8, "sdata8")

  S(DW_EH_PE_absptr | DW_EH_PE_pcrel, "pcrel")
  S(DW_EH_PE_uleb128 | DW_EH_PE_pcrel, "pcrel uleb128")
  S(DW_EH_PE_udata2 | DW_EH_PE_pcrel, "pcrel udata2")
  S(DW_EH_PE_udata4 | DW_EH_PE_pcrel, "pcrel udata4")
  S(DW_EH_PE_udata8 | DW_EH_PE_pcrel, "pcrel udata8")
  S(DW_EH_PE_sleb128 | DW_EH_PE_pcrel, "pcrel sleb128")
  S(DW_EH_PE_sdata2 | DW_EH_PE_pcrel, "pcrel sdata2")
  S(DW_EH_PE_sdata4 | DW_EH_PE_pcrel, "pcrel sdata4")
  S(DW_EH_PE_sdata8 | DW_EH_PE_pcrel, "pcrel sdata8")

  S(DW_EH_PE_absptr | DW_EH_PE_textrel, "textrel")
  S(DW_EH_PE_uleb128 | DW_EH_PE_textrel, "textrel uleb128")
  S(DW_EH_PE_udata2 | DW_EH_PE_textrel, "textrel udata2")
  S(DW_EH_PE_udata4 | DW_EH_PE_textrel, "textrel udata4")
  S(DW_EH_PE_udata8 | DW_EH_PE_textrel, "textrel udata8")
  S(DW_EH_PE_sleb128 | DW_EH_PE_textrel, "textrel sleb128")
  S(DW_EH_PE_sdata2 | DW_EH_PE_textrel, "textrel sdata2")
  S(DW_EH_PE_sdata4 | DW_EH_PE_textrel, "textrel sdata4")
  S(DW_EH_PE_sdata8 | DW_EH_PE_textrel, "textrel sdata8")

  S(DW_EH_PE_absptr | DW_EH_PE_datarel, "datarel")
  S(DW_EH_PE_uleb128 | DW_EH_PE_datarel, "datarel uleb128")
  S(DW_EH_PE_udata2 | DW_EH_PE_datarel, "datarel udata2")
  S(DW_EH_PE_udata4 | DW_EH_PE_datarel, "datarel udata4")
  S(DW_EH_PE_udata8 | DW_EH_PE_datarel, "datarel udata8")
  S(DW_EH_PE_sleb128 | DW_EH_PE_datarel, "datarel sleb128")
  S(DW_EH_PE_sdata2 | DW_EH_PE_datarel, "datarel sdata2")
  S(DW_EH_PE_sdata4 | DW_EH_PE_datarel, "datarel sdata4")
  S(DW_EH_PE_sdata8 | DW_EH_PE_datarel, "datarel sdata8")

  S(DW_EH_PE_absptr | DW_EH_PE_funcrel, "funcrel")
  S(DW_EH_PE_uleb128 | DW_EH_PE_funcrel, "funcrel uleb128")
  S(DW_EH_PE_udata2 | DW_EH_PE_funcrel, "funcrel udata2")
  S(DW_EH_PE_udata4 | DW_EH_PE_funcrel, "funcrel udata4")
  S(DW_EH_PE_udata8 | DW_EH_PE_funcrel, "funcrel udata8")
  S(DW_EH_PE_sleb128 | DW_EH_PE_funcrel, "funcrel sleb128")
  S(DW_EH_PE_sdata2 | DW_EH_PE_funcrel, "funcrel sdata2")
  S(DW_EH_PE_sdata4 | DW_EH_PE_funcrel, "funcrel sdata4")
  S(DW_EH_PE_sdata8 | DW_EH_PE_funcrel, "funcrel sdata8")

  S(DW_EH_PE_indirect | DW_EH_PE_absptr | DW_EH_PE_pcrel,
    "indirect pcrel")
  S(DW_EH_PE_indirect | DW_EH_PE_uleb128 | DW_EH_PE_pcrel,
    "indirect pcrel uleb128")
  S(DW_EH_PE_indirect | DW_EH_PE_udata2 | DW_EH_PE_pcrel,
    "indirect pcrel udata2")
  S(DW_EH_PE_indirect | DW_EH_PE_udata4 | DW_EH_PE_pcrel,
    "indirect pcrel udata4")
  S(DW_EH_PE_indirect | DW_EH_PE_udata8 | DW_EH_PE_pcrel,
    "indirect pcrel udata8")
  S(DW_EH_PE_indirect | DW_EH_PE_sleb128 | DW_EH_PE_pcrel,
    "indirect pcrel sleb128")
  S(DW_EH_PE_indirect | DW_EH_PE_sdata2 | DW_EH_PE_pcrel,
    "indirect pcrel sdata2")
  S(DW_EH_PE_indirect | DW_EH_PE_sdata4 | DW_EH_PE_pcrel,
    "indirect pcrel sdata4")
  S(DW_EH_PE_indirect | DW_EH_PE_sdata8 | DW_EH_PE_pcrel,
    "indirect pcrel sdata8")

  S(DW_EH_PE_indirect | DW_EH_PE_absptr | DW_EH_PE_textrel,
    "indirect textrel")
  S(DW_EH_PE_indirect | DW_EH_PE_uleb128 | DW_EH_PE_textrel,
    "indirect textrel uleb128")
  S(DW_EH_PE_indirect | DW_EH_PE_udata2 | DW_EH_PE_textrel,
    "indirect textrel udata2")
  S(DW_EH_PE_indirect | DW_EH_PE_udata4 | DW_EH_PE_textrel,
    "indirect textrel udata4")
  S(DW_EH_PE_indirect | DW_EH_PE_udata8 | DW_EH_PE_textrel,
    "indirect textrel udata8")
  S(DW_EH_PE_indirect | DW_EH_PE_sleb128 | DW_EH_PE_textrel,
    "indirect textrel sleb128")
  S(DW_EH_PE_indirect | DW_EH_PE_sdata2 | DW_EH_PE_textrel,
    "indirect textrel sdata2")
  S(DW_EH_PE_indirect | DW_EH_PE_sdata4 | DW_EH_PE_textrel,
    "indirect textrel sdata4")
  S(DW_EH_PE_indirect | DW_EH_PE_sdata8 | DW_EH_PE_textrel,
    "indirect textrel sdata8")

  S(DW_EH_PE_indirect | DW_EH_PE_absptr | DW_EH_PE_datarel,
    "indirect datarel")
  S(DW_EH_PE_indirect | DW_EH_PE_uleb128 | DW_EH_PE_datarel,
    "indirect datarel uleb128")
  S(DW_EH_PE_indirect | DW_EH_PE_udata2 | DW_EH_PE_datarel,
    "indirect datarel udata2")
  S(DW_EH_PE_indirect | DW_EH_PE_udata4 | DW_EH_PE_datarel,
    "indirect datarel udata4")
  S(DW_EH_PE_indirect | DW_EH_PE_udata8 | DW_EH_PE_datarel,
    "indirect datarel udata8")
  S(DW_EH_PE_indirect | DW_EH_PE_sleb128 | DW_EH_PE_datarel,
    "indirect datarel sleb128")
  S(DW_EH_PE_indirect | DW_EH_PE_sdata2 | DW_EH_PE_datarel,
    "indirect datarel sdata2")
  S(DW_EH_PE_indirect | DW_EH_PE_sdata4 | DW_EH_PE_datarel,
    "indirect datarel sdata4")
  S(DW_EH_PE_indirect | DW_EH_PE_sdata8 | DW_EH_PE_datarel,
    "indirect datarel sdata8")

  S(DW_EH_PE_indirect | DW_EH_PE_absptr | DW_EH_PE_funcrel,
    "indirect funcrel")
  S(DW_EH_PE_indirect | DW_EH_PE_uleb128 | DW_EH_PE_funcrel,
    "indirect funcrel uleb128")
  S(DW_EH_PE_indirect | DW_EH_PE_udata2 | DW_EH_PE_funcrel,
    "indirect funcrel udata2")
  S(DW_EH_PE_indirect | DW_EH_PE_udata4 | DW_EH_PE_funcrel,
    "indirect funcrel udata4")
  S(DW_EH_PE_indirect | DW_EH_PE_udata8 | DW_EH_PE_funcrel,
    "indirect funcrel udata8")
  S(DW_EH_PE_indirect | DW_EH_PE_sleb128 | DW_EH_PE_funcrel,
    "indirect funcrel sleb128")
  S(DW_EH_PE_indirect | DW_EH_PE_sdata2 | DW_EH_PE_funcrel,
    "indirect funcrel sdata2")
  S(DW_EH_PE_indirect | DW_EH_PE_sdata4 | DW_EH_PE_funcrel,
    "indirect funcrel sdata4")
  S(DW_EH_PE_indirect | DW_EH_PE_sdata8 | DW_EH_PE_funcrel,
    "indirect funcrel sdata8")

#if HAVE_DESIGNATED_INITIALIZERS
  };

  gcc_assert (format >= 0 && format < 0x100 && format_names[format]);
  
  return format_names[format];
#else
  }
  gcc_unreachable ();
#endif
}

/* Output an unsigned LEB128 quantity.  */

void
dw2_asm_output_data_uleb128 (unsigned HOST_WIDE_INT value,
			     const char *comment, ...)
{
  va_list ap;

  va_start (ap, comment);

#ifdef HAVE_AS_LEB128
  fprintf (asm_out_file, "\t.uleb128 " HOST_WIDE_INT_PRINT_HEX , value);

  if (flag_debug_asm && comment)
    {
      fprintf (asm_out_file, "\t%s ", ASM_COMMENT_START);
      vfprintf (asm_out_file, comment, ap);
    }
#else
  {
    unsigned HOST_WIDE_INT work = value;
    const char *byte_op = targetm.asm_out.byte_op;

    if (byte_op)
      fputs (byte_op, asm_out_file);
    do
      {
	int byte = (work & 0x7f);
	work >>= 7;
	if (work != 0)
	  /* More bytes to follow.  */
	  byte |= 0x80;

	if (byte_op)
	  {
	    fprintf (asm_out_file, "0x%x", byte);
	    if (work != 0)
	      fputc (',', asm_out_file);
	  }
	else
	  assemble_integer (GEN_INT (byte), 1, BITS_PER_UNIT, 1);
      }
    while (work != 0);

  if (flag_debug_asm)
    {
      fprintf (asm_out_file, "\t%s uleb128 " HOST_WIDE_INT_PRINT_HEX,
	       ASM_COMMENT_START, value);
      if (comment)
	{
	  fputs ("; ", asm_out_file);
	  vfprintf (asm_out_file, comment, ap);
	}
    }
  }
#endif
  fputc ('\n', asm_out_file);

  va_end (ap);
}

/* Output a signed LEB128 quantity.  */

void
dw2_asm_output_data_sleb128 (HOST_WIDE_INT value,
			     const char *comment, ...)
{
  va_list ap;

  va_start (ap, comment);

#ifdef HAVE_AS_LEB128
  fprintf (asm_out_file, "\t.sleb128 " HOST_WIDE_INT_PRINT_DEC, value);

  if (flag_debug_asm && comment)
    {
      fprintf (asm_out_file, "\t%s ", ASM_COMMENT_START);
      vfprintf (asm_out_file, comment, ap);
    }
#else
  {
    HOST_WIDE_INT work = value;
    int more, byte;
    const char *byte_op = targetm.asm_out.byte_op;

    if (byte_op)
      fputs (byte_op, asm_out_file);
    do
      {
	byte = (work & 0x7f);
	/* arithmetic shift */
	work >>= 7;
	more = !((work == 0 && (byte & 0x40) == 0)
		 || (work == -1 && (byte & 0x40) != 0));
	if (more)
	  byte |= 0x80;

	if (byte_op)
	  {
	    fprintf (asm_out_file, "0x%x", byte);
	    if (more)
	      fputc (',', asm_out_file);
	  }
	else
	  assemble_integer (GEN_INT (byte), 1, BITS_PER_UNIT, 1);
      }
    while (more);

  if (flag_debug_asm)
    {
      fprintf (asm_out_file, "\t%s sleb128 " HOST_WIDE_INT_PRINT_DEC,
	       ASM_COMMENT_START, value);
      if (comment)
	{
	  fputs ("; ", asm_out_file);
	  vfprintf (asm_out_file, comment, ap);
	}
    }
  }
#endif
  fputc ('\n', asm_out_file);

  va_end (ap);
}

void
dw2_asm_output_delta_uleb128 (const char *lab1 ATTRIBUTE_UNUSED,
			      const char *lab2 ATTRIBUTE_UNUSED,
			      const char *comment, ...)
{
  va_list ap;

  va_start (ap, comment);

#ifdef HAVE_AS_LEB128
  fputs ("\t.uleb128 ", asm_out_file);
  assemble_name (asm_out_file, lab1);
  fputc ('-', asm_out_file);
  assemble_name (asm_out_file, lab2);
#else
  gcc_unreachable ();
#endif

  if (flag_debug_asm && comment)
    {
      fprintf (asm_out_file, "\t%s ", ASM_COMMENT_START);
      vfprintf (asm_out_file, comment, ap);
    }
  fputc ('\n', asm_out_file);

  va_end (ap);
}

#if 0

void
dw2_asm_output_delta_sleb128 (const char *lab1 ATTRIBUTE_UNUSED,
			      const char *lab2 ATTRIBUTE_UNUSED,
			      const char *comment, ...)
{
  va_list ap;

  va_start (ap, comment);

#ifdef HAVE_AS_LEB128
  fputs ("\t.sleb128 ", asm_out_file);
  assemble_name (asm_out_file, lab1);
  fputc ('-', asm_out_file);
  assemble_name (asm_out_file, lab2);
#else
  gcc_unreachable ();
#endif

  if (flag_debug_asm && comment)
    {
      fprintf (asm_out_file, "\t%s ", ASM_COMMENT_START);
      vfprintf (asm_out_file, comment, ap);
    }
  fputc ('\n', asm_out_file);

  va_end (ap);
}
#endif /* 0 */

static rtx dw2_force_const_mem (rtx, bool);
static int dw2_output_indirect_constant_1 (splay_tree_node, void *);

static GTY((param1_is (char *), param2_is (tree))) splay_tree indirect_pool;

static GTY(()) int dw2_const_labelno;

#if defined(HAVE_GAS_HIDDEN) && defined(SUPPORTS_ONE_ONLY)
# define USE_LINKONCE_INDIRECT 1
#else
# define USE_LINKONCE_INDIRECT 0
#endif

/* Put X, a SYMBOL_REF, in memory.  Return a SYMBOL_REF to the allocated
   memory.  Differs from force_const_mem in that a single pool is used for
   the entire unit of translation, and the memory is not guaranteed to be
   "near" the function in any interesting sense.  PUBLIC controls whether
   the symbol can be shared across the entire application (or DSO).  */

static rtx
dw2_force_const_mem (rtx x, bool public)
{
  splay_tree_node node;
  const char *str;
  tree decl;

  if (! indirect_pool)
    indirect_pool = splay_tree_new_ggc (splay_tree_compare_pointers);

  gcc_assert (GET_CODE (x) == SYMBOL_REF);

  str = targetm.strip_name_encoding (XSTR (x, 0));
  node = splay_tree_lookup (indirect_pool, (splay_tree_key) str);
  if (node)
    decl = (tree) node->value;
  else
    {
      tree id;

      if (public && USE_LINKONCE_INDIRECT)
	{
	  char *ref_name = alloca (strlen (str) + sizeof "DW.ref.");

	  sprintf (ref_name, "DW.ref.%s", str);
	  id = get_identifier (ref_name);
	  decl = build_decl (VAR_DECL, id, ptr_type_node);
	  DECL_ARTIFICIAL (decl) = 1;
	  DECL_IGNORED_P (decl) = 1;
	  TREE_PUBLIC (decl) = 1;
	  DECL_INITIAL (decl) = decl;
	  make_decl_one_only (decl);
	}
      else
	{
	  char label[32];

	  ASM_GENERATE_INTERNAL_LABEL (label, "LDFCM", dw2_const_labelno);
	  ++dw2_const_labelno;
	  id = get_identifier (label);
	  decl = build_decl (VAR_DECL, id, ptr_type_node);
	  DECL_ARTIFICIAL (decl) = 1;
	  DECL_IGNORED_P (decl) = 1;
	  TREE_STATIC (decl) = 1;
	  DECL_INITIAL (decl) = decl;
	}

      id = maybe_get_identifier (str);
      if (id)
	TREE_SYMBOL_REFERENCED (id) = 1;

      splay_tree_insert (indirect_pool, (splay_tree_key) str,
			 (splay_tree_value) decl);
    }

  return XEXP (DECL_RTL (decl), 0);
}

/* A helper function for dw2_output_indirect_constants called through
   splay_tree_foreach.  Emit one queued constant to memory.  */

static int
dw2_output_indirect_constant_1 (splay_tree_node node,
				void *data ATTRIBUTE_UNUSED)
{
  const char *sym;
  rtx sym_ref;
  tree decl;

  sym = (const char *) node->key;
  decl = (tree) node->value;
  sym_ref = gen_rtx_SYMBOL_REF (Pmode, sym);
  if (TREE_PUBLIC (decl) && USE_LINKONCE_INDIRECT)
    fprintf (asm_out_file, "\t.hidden %sDW.ref.%s\n", user_label_prefix, sym);
  assemble_variable (decl, 1, 1, 1);
  assemble_integer (sym_ref, POINTER_SIZE / BITS_PER_UNIT, POINTER_SIZE, 1);

  return 0;
}

/* Emit the constants queued through dw2_force_const_mem.  */

void
dw2_output_indirect_constants (void)
{
  if (indirect_pool)
    splay_tree_foreach (indirect_pool, dw2_output_indirect_constant_1, NULL);
}

/* Like dw2_asm_output_addr_rtx, but encode the pointer as directed.
   If PUBLIC is set and the encoding is DW_EH_PE_indirect, the indirect
   reference is shared across the entire application (or DSO).  */

void
dw2_asm_output_encoded_addr_rtx (int encoding, rtx addr, bool public,
				 const char *comment, ...)
{
  int size;
  va_list ap;

  va_start (ap, comment);

  size = size_of_encoded_value (encoding);

  if (encoding == DW_EH_PE_aligned)
    {
      assemble_align (POINTER_SIZE);
      assemble_integer (addr, size, POINTER_SIZE, 1);
      return;
    }

  /* NULL is _always_ represented as a plain zero, as is 1 for Ada's
     "all others".  */
  if (addr == const0_rtx || addr == const1_rtx)
    assemble_integer (addr, size, BITS_PER_UNIT, 1);
  else
    {
    restart:
      /* Allow the target first crack at emitting this.  Some of the
	 special relocations require special directives instead of
	 just ".4byte" or whatever.  */
#ifdef ASM_MAYBE_OUTPUT_ENCODED_ADDR_RTX
      ASM_MAYBE_OUTPUT_ENCODED_ADDR_RTX (asm_out_file, encoding, size,
					 addr, done);
#endif

      /* Indirection is used to get dynamic relocations out of a
	 read-only section.  */
      if (encoding & DW_EH_PE_indirect)
	{
	  /* It is very tempting to use force_const_mem so that we share data
	     with the normal constant pool.  However, we've already emitted
	     the constant pool for this function.  Moreover, we'd like to
	     share these constants across the entire unit of translation and
	     even, if possible, across the entire application (or DSO).  */
	  addr = dw2_force_const_mem (addr, public);
	  encoding &= ~DW_EH_PE_indirect;
	  goto restart;
	}

      switch (encoding & 0xF0)
	{
	case DW_EH_PE_absptr:
	  dw2_assemble_integer (size, addr);
	  break;

	case DW_EH_PE_pcrel:
	  gcc_assert (GET_CODE (addr) == SYMBOL_REF);
#ifdef ASM_OUTPUT_DWARF_PCREL
	  ASM_OUTPUT_DWARF_PCREL (asm_out_file, size, XSTR (addr, 0));
#else
	  dw2_assemble_integer (size, gen_rtx_MINUS (Pmode, addr, pc_rtx));
#endif
	  break;

	default:
	  /* Other encodings should have been handled by
	     ASM_MAYBE_OUTPUT_ENCODED_ADDR_RTX.  */
	  gcc_unreachable ();
	}

#ifdef ASM_MAYBE_OUTPUT_ENCODED_ADDR_RTX
    done:;
#endif
    }

  if (flag_debug_asm && comment)
    {
      fprintf (asm_out_file, "\t%s ", ASM_COMMENT_START);
      vfprintf (asm_out_file, comment, ap);
    }
  fputc ('\n', asm_out_file);

  va_end (ap);
}

#include "gt-dwarf2asm.h"

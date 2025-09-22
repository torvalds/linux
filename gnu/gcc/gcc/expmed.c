/* Medium-level subroutines: convert bit-field store and extract
   and shifts, multiplies and divides to rtl instructions.
   Copyright (C) 1987, 1988, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

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
#include "toplev.h"
#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "flags.h"
#include "insn-config.h"
#include "expr.h"
#include "optabs.h"
#include "real.h"
#include "recog.h"
#include "langhooks.h"

static void store_fixed_bit_field (rtx, unsigned HOST_WIDE_INT,
				   unsigned HOST_WIDE_INT,
				   unsigned HOST_WIDE_INT, rtx);
static void store_split_bit_field (rtx, unsigned HOST_WIDE_INT,
				   unsigned HOST_WIDE_INT, rtx);
static rtx extract_fixed_bit_field (enum machine_mode, rtx,
				    unsigned HOST_WIDE_INT,
				    unsigned HOST_WIDE_INT,
				    unsigned HOST_WIDE_INT, rtx, int);
static rtx mask_rtx (enum machine_mode, int, int, int);
static rtx lshift_value (enum machine_mode, rtx, int, int);
static rtx extract_split_bit_field (rtx, unsigned HOST_WIDE_INT,
				    unsigned HOST_WIDE_INT, int);
static void do_cmp_and_jump (rtx, rtx, enum rtx_code, enum machine_mode, rtx);
static rtx expand_smod_pow2 (enum machine_mode, rtx, HOST_WIDE_INT);
static rtx expand_sdiv_pow2 (enum machine_mode, rtx, HOST_WIDE_INT);

/* Test whether a value is zero of a power of two.  */
#define EXACT_POWER_OF_2_OR_ZERO_P(x) (((x) & ((x) - 1)) == 0)

/* Nonzero means divides or modulus operations are relatively cheap for
   powers of two, so don't use branches; emit the operation instead.
   Usually, this will mean that the MD file will emit non-branch
   sequences.  */

static bool sdiv_pow2_cheap[NUM_MACHINE_MODES];
static bool smod_pow2_cheap[NUM_MACHINE_MODES];

#ifndef SLOW_UNALIGNED_ACCESS
#define SLOW_UNALIGNED_ACCESS(MODE, ALIGN) STRICT_ALIGNMENT
#endif

/* For compilers that support multiple targets with different word sizes,
   MAX_BITS_PER_WORD contains the biggest value of BITS_PER_WORD.  An example
   is the H8/300(H) compiler.  */

#ifndef MAX_BITS_PER_WORD
#define MAX_BITS_PER_WORD BITS_PER_WORD
#endif

/* Reduce conditional compilation elsewhere.  */
#ifndef HAVE_insv
#define HAVE_insv	0
#define CODE_FOR_insv	CODE_FOR_nothing
#define gen_insv(a,b,c,d) NULL_RTX
#endif
#ifndef HAVE_extv
#define HAVE_extv	0
#define CODE_FOR_extv	CODE_FOR_nothing
#define gen_extv(a,b,c,d) NULL_RTX
#endif
#ifndef HAVE_extzv
#define HAVE_extzv	0
#define CODE_FOR_extzv	CODE_FOR_nothing
#define gen_extzv(a,b,c,d) NULL_RTX
#endif

/* Cost of various pieces of RTL.  Note that some of these are indexed by
   shift count and some by mode.  */
static int zero_cost;
static int add_cost[NUM_MACHINE_MODES];
static int neg_cost[NUM_MACHINE_MODES];
static int shift_cost[NUM_MACHINE_MODES][MAX_BITS_PER_WORD];
static int shiftadd_cost[NUM_MACHINE_MODES][MAX_BITS_PER_WORD];
static int shiftsub_cost[NUM_MACHINE_MODES][MAX_BITS_PER_WORD];
static int mul_cost[NUM_MACHINE_MODES];
static int sdiv_cost[NUM_MACHINE_MODES];
static int udiv_cost[NUM_MACHINE_MODES];
static int mul_widen_cost[NUM_MACHINE_MODES];
static int mul_highpart_cost[NUM_MACHINE_MODES];

void
init_expmed (void)
{
  struct
  {
    struct rtx_def reg;		rtunion reg_fld[2];
    struct rtx_def plus;	rtunion plus_fld1;
    struct rtx_def neg;
    struct rtx_def mult;	rtunion mult_fld1;
    struct rtx_def sdiv;	rtunion sdiv_fld1;
    struct rtx_def udiv;	rtunion udiv_fld1;
    struct rtx_def zext;
    struct rtx_def sdiv_32;	rtunion sdiv_32_fld1;
    struct rtx_def smod_32;	rtunion smod_32_fld1;
    struct rtx_def wide_mult;	rtunion wide_mult_fld1;
    struct rtx_def wide_lshr;	rtunion wide_lshr_fld1;
    struct rtx_def wide_trunc;
    struct rtx_def shift;	rtunion shift_fld1;
    struct rtx_def shift_mult;	rtunion shift_mult_fld1;
    struct rtx_def shift_add;	rtunion shift_add_fld1;
    struct rtx_def shift_sub;	rtunion shift_sub_fld1;
  } all;

  rtx pow2[MAX_BITS_PER_WORD];
  rtx cint[MAX_BITS_PER_WORD];
  int m, n;
  enum machine_mode mode, wider_mode;

  zero_cost = rtx_cost (const0_rtx, 0);

  for (m = 1; m < MAX_BITS_PER_WORD; m++)
    {
      pow2[m] = GEN_INT ((HOST_WIDE_INT) 1 << m);
      cint[m] = GEN_INT (m);
    }

  memset (&all, 0, sizeof all);

  PUT_CODE (&all.reg, REG);
  /* Avoid using hard regs in ways which may be unsupported.  */
  REGNO (&all.reg) = LAST_VIRTUAL_REGISTER + 1;

  PUT_CODE (&all.plus, PLUS);
  XEXP (&all.plus, 0) = &all.reg;
  XEXP (&all.plus, 1) = &all.reg;

  PUT_CODE (&all.neg, NEG);
  XEXP (&all.neg, 0) = &all.reg;

  PUT_CODE (&all.mult, MULT);
  XEXP (&all.mult, 0) = &all.reg;
  XEXP (&all.mult, 1) = &all.reg;

  PUT_CODE (&all.sdiv, DIV);
  XEXP (&all.sdiv, 0) = &all.reg;
  XEXP (&all.sdiv, 1) = &all.reg;

  PUT_CODE (&all.udiv, UDIV);
  XEXP (&all.udiv, 0) = &all.reg;
  XEXP (&all.udiv, 1) = &all.reg;

  PUT_CODE (&all.sdiv_32, DIV);
  XEXP (&all.sdiv_32, 0) = &all.reg;
  XEXP (&all.sdiv_32, 1) = 32 < MAX_BITS_PER_WORD ? cint[32] : GEN_INT (32);

  PUT_CODE (&all.smod_32, MOD);
  XEXP (&all.smod_32, 0) = &all.reg;
  XEXP (&all.smod_32, 1) = XEXP (&all.sdiv_32, 1);

  PUT_CODE (&all.zext, ZERO_EXTEND);
  XEXP (&all.zext, 0) = &all.reg;

  PUT_CODE (&all.wide_mult, MULT);
  XEXP (&all.wide_mult, 0) = &all.zext;
  XEXP (&all.wide_mult, 1) = &all.zext;

  PUT_CODE (&all.wide_lshr, LSHIFTRT);
  XEXP (&all.wide_lshr, 0) = &all.wide_mult;

  PUT_CODE (&all.wide_trunc, TRUNCATE);
  XEXP (&all.wide_trunc, 0) = &all.wide_lshr;

  PUT_CODE (&all.shift, ASHIFT);
  XEXP (&all.shift, 0) = &all.reg;

  PUT_CODE (&all.shift_mult, MULT);
  XEXP (&all.shift_mult, 0) = &all.reg;

  PUT_CODE (&all.shift_add, PLUS);
  XEXP (&all.shift_add, 0) = &all.shift_mult;
  XEXP (&all.shift_add, 1) = &all.reg;

  PUT_CODE (&all.shift_sub, MINUS);
  XEXP (&all.shift_sub, 0) = &all.shift_mult;
  XEXP (&all.shift_sub, 1) = &all.reg;

  for (mode = GET_CLASS_NARROWEST_MODE (MODE_INT);
       mode != VOIDmode;
       mode = GET_MODE_WIDER_MODE (mode))
    {
      PUT_MODE (&all.reg, mode);
      PUT_MODE (&all.plus, mode);
      PUT_MODE (&all.neg, mode);
      PUT_MODE (&all.mult, mode);
      PUT_MODE (&all.sdiv, mode);
      PUT_MODE (&all.udiv, mode);
      PUT_MODE (&all.sdiv_32, mode);
      PUT_MODE (&all.smod_32, mode);
      PUT_MODE (&all.wide_trunc, mode);
      PUT_MODE (&all.shift, mode);
      PUT_MODE (&all.shift_mult, mode);
      PUT_MODE (&all.shift_add, mode);
      PUT_MODE (&all.shift_sub, mode);

      add_cost[mode] = rtx_cost (&all.plus, SET);
      neg_cost[mode] = rtx_cost (&all.neg, SET);
      mul_cost[mode] = rtx_cost (&all.mult, SET);
      sdiv_cost[mode] = rtx_cost (&all.sdiv, SET);
      udiv_cost[mode] = rtx_cost (&all.udiv, SET);

      sdiv_pow2_cheap[mode] = (rtx_cost (&all.sdiv_32, SET)
			       <= 2 * add_cost[mode]);
      smod_pow2_cheap[mode] = (rtx_cost (&all.smod_32, SET)
			       <= 4 * add_cost[mode]);

      wider_mode = GET_MODE_WIDER_MODE (mode);
      if (wider_mode != VOIDmode)
	{
	  PUT_MODE (&all.zext, wider_mode);
	  PUT_MODE (&all.wide_mult, wider_mode);
	  PUT_MODE (&all.wide_lshr, wider_mode);
	  XEXP (&all.wide_lshr, 1) = GEN_INT (GET_MODE_BITSIZE (mode));

	  mul_widen_cost[wider_mode] = rtx_cost (&all.wide_mult, SET);
	  mul_highpart_cost[mode] = rtx_cost (&all.wide_trunc, SET);
	}

      shift_cost[mode][0] = 0;
      shiftadd_cost[mode][0] = shiftsub_cost[mode][0] = add_cost[mode];

      n = MIN (MAX_BITS_PER_WORD, GET_MODE_BITSIZE (mode));
      for (m = 1; m < n; m++)
	{
	  XEXP (&all.shift, 1) = cint[m];
	  XEXP (&all.shift_mult, 1) = pow2[m];

	  shift_cost[mode][m] = rtx_cost (&all.shift, SET);
	  shiftadd_cost[mode][m] = rtx_cost (&all.shift_add, SET);
	  shiftsub_cost[mode][m] = rtx_cost (&all.shift_sub, SET);
	}
    }
}

/* Return an rtx representing minus the value of X.
   MODE is the intended mode of the result,
   useful if X is a CONST_INT.  */

rtx
negate_rtx (enum machine_mode mode, rtx x)
{
  rtx result = simplify_unary_operation (NEG, mode, x, mode);

  if (result == 0)
    result = expand_unop (mode, neg_optab, x, NULL_RTX, 0);

  return result;
}

/* Report on the availability of insv/extv/extzv and the desired mode
   of each of their operands.  Returns MAX_MACHINE_MODE if HAVE_foo
   is false; else the mode of the specified operand.  If OPNO is -1,
   all the caller cares about is whether the insn is available.  */
enum machine_mode
mode_for_extraction (enum extraction_pattern pattern, int opno)
{
  const struct insn_data *data;

  switch (pattern)
    {
    case EP_insv:
      if (HAVE_insv)
	{
	  data = &insn_data[CODE_FOR_insv];
	  break;
	}
      return MAX_MACHINE_MODE;

    case EP_extv:
      if (HAVE_extv)
	{
	  data = &insn_data[CODE_FOR_extv];
	  break;
	}
      return MAX_MACHINE_MODE;

    case EP_extzv:
      if (HAVE_extzv)
	{
	  data = &insn_data[CODE_FOR_extzv];
	  break;
	}
      return MAX_MACHINE_MODE;

    default:
      gcc_unreachable ();
    }

  if (opno == -1)
    return VOIDmode;

  /* Everyone who uses this function used to follow it with
     if (result == VOIDmode) result = word_mode; */
  if (data->operand[opno].mode == VOIDmode)
    return word_mode;
  return data->operand[opno].mode;
}


/* Generate code to store value from rtx VALUE
   into a bit-field within structure STR_RTX
   containing BITSIZE bits starting at bit BITNUM.
   FIELDMODE is the machine-mode of the FIELD_DECL node for this field.
   ALIGN is the alignment that STR_RTX is known to have.
   TOTAL_SIZE is the size of the structure in bytes, or -1 if varying.  */

/* ??? Note that there are two different ideas here for how
   to determine the size to count bits within, for a register.
   One is BITS_PER_WORD, and the other is the size of operand 3
   of the insv pattern.

   If operand 3 of the insv pattern is VOIDmode, then we will use BITS_PER_WORD
   else, we use the mode of operand 3.  */

rtx
store_bit_field (rtx str_rtx, unsigned HOST_WIDE_INT bitsize,
		 unsigned HOST_WIDE_INT bitnum, enum machine_mode fieldmode,
		 rtx value)
{
  unsigned int unit
    = (MEM_P (str_rtx)) ? BITS_PER_UNIT : BITS_PER_WORD;
  unsigned HOST_WIDE_INT offset, bitpos;
  rtx op0 = str_rtx;
  int byte_offset;
  rtx orig_value;

  enum machine_mode op_mode = mode_for_extraction (EP_insv, 3);

  while (GET_CODE (op0) == SUBREG)
    {
      /* The following line once was done only if WORDS_BIG_ENDIAN,
	 but I think that is a mistake.  WORDS_BIG_ENDIAN is
	 meaningful at a much higher level; when structures are copied
	 between memory and regs, the higher-numbered regs
	 always get higher addresses.  */
      int inner_mode_size = GET_MODE_SIZE (GET_MODE (SUBREG_REG (op0)));
      int outer_mode_size = GET_MODE_SIZE (GET_MODE (op0));
      
      byte_offset = 0;

      /* Paradoxical subregs need special handling on big endian machines.  */
      if (SUBREG_BYTE (op0) == 0 && inner_mode_size < outer_mode_size)
	{
	  int difference = inner_mode_size - outer_mode_size;

	  if (WORDS_BIG_ENDIAN)
	    byte_offset += (difference / UNITS_PER_WORD) * UNITS_PER_WORD;
	  if (BYTES_BIG_ENDIAN)
	    byte_offset += difference % UNITS_PER_WORD;
	}
      else
	byte_offset = SUBREG_BYTE (op0);

      bitnum += byte_offset * BITS_PER_UNIT;
      op0 = SUBREG_REG (op0);
    }

  /* No action is needed if the target is a register and if the field
     lies completely outside that register.  This can occur if the source
     code contains an out-of-bounds access to a small array.  */
  if (REG_P (op0) && bitnum >= GET_MODE_BITSIZE (GET_MODE (op0)))
    return value;

  /* Use vec_set patterns for inserting parts of vectors whenever
     available.  */
  if (VECTOR_MODE_P (GET_MODE (op0))
      && !MEM_P (op0)
      && (vec_set_optab->handlers[GET_MODE (op0)].insn_code
	  != CODE_FOR_nothing)
      && fieldmode == GET_MODE_INNER (GET_MODE (op0))
      && bitsize == GET_MODE_BITSIZE (GET_MODE_INNER (GET_MODE (op0)))
      && !(bitnum % GET_MODE_BITSIZE (GET_MODE_INNER (GET_MODE (op0)))))
    {
      enum machine_mode outermode = GET_MODE (op0);
      enum machine_mode innermode = GET_MODE_INNER (outermode);
      int icode = (int) vec_set_optab->handlers[outermode].insn_code;
      int pos = bitnum / GET_MODE_BITSIZE (innermode);
      rtx rtxpos = GEN_INT (pos);
      rtx src = value;
      rtx dest = op0;
      rtx pat, seq;
      enum machine_mode mode0 = insn_data[icode].operand[0].mode;
      enum machine_mode mode1 = insn_data[icode].operand[1].mode;
      enum machine_mode mode2 = insn_data[icode].operand[2].mode;

      start_sequence ();

      if (! (*insn_data[icode].operand[1].predicate) (src, mode1))
	src = copy_to_mode_reg (mode1, src);

      if (! (*insn_data[icode].operand[2].predicate) (rtxpos, mode2))
	rtxpos = copy_to_mode_reg (mode1, rtxpos);

      /* We could handle this, but we should always be called with a pseudo
	 for our targets and all insns should take them as outputs.  */
      gcc_assert ((*insn_data[icode].operand[0].predicate) (dest, mode0)
		  && (*insn_data[icode].operand[1].predicate) (src, mode1)
		  && (*insn_data[icode].operand[2].predicate) (rtxpos, mode2));
      pat = GEN_FCN (icode) (dest, src, rtxpos);
      seq = get_insns ();
      end_sequence ();
      if (pat)
	{
	  emit_insn (seq);
	  emit_insn (pat);
	  return dest;
	}
    }

  /* If the target is a register, overwriting the entire object, or storing
     a full-word or multi-word field can be done with just a SUBREG.

     If the target is memory, storing any naturally aligned field can be
     done with a simple store.  For targets that support fast unaligned
     memory, any naturally sized, unit aligned field can be done directly.  */

  offset = bitnum / unit;
  bitpos = bitnum % unit;
  byte_offset = (bitnum % BITS_PER_WORD) / BITS_PER_UNIT
                + (offset * UNITS_PER_WORD);

  if (bitpos == 0
      && bitsize == GET_MODE_BITSIZE (fieldmode)
      && (!MEM_P (op0)
	  ? ((GET_MODE_SIZE (fieldmode) >= UNITS_PER_WORD
	     || GET_MODE_SIZE (GET_MODE (op0)) == GET_MODE_SIZE (fieldmode))
	     && byte_offset % GET_MODE_SIZE (fieldmode) == 0)
	  : (! SLOW_UNALIGNED_ACCESS (fieldmode, MEM_ALIGN (op0))
	     || (offset * BITS_PER_UNIT % bitsize == 0
		 && MEM_ALIGN (op0) % GET_MODE_BITSIZE (fieldmode) == 0))))
    {
      if (MEM_P (op0))
	op0 = adjust_address (op0, fieldmode, offset);
      else if (GET_MODE (op0) != fieldmode)
	op0 = simplify_gen_subreg (fieldmode, op0, GET_MODE (op0),
				   byte_offset);
      emit_move_insn (op0, value);
      return value;
    }

  /* Make sure we are playing with integral modes.  Pun with subregs
     if we aren't.  This must come after the entire register case above,
     since that case is valid for any mode.  The following cases are only
     valid for integral modes.  */
  {
    enum machine_mode imode = int_mode_for_mode (GET_MODE (op0));
    if (imode != GET_MODE (op0))
      {
	if (MEM_P (op0))
	  op0 = adjust_address (op0, imode, 0);
	else
	  {
	    gcc_assert (imode != BLKmode);
	    op0 = gen_lowpart (imode, op0);
	  }
      }
  }

  /* We may be accessing data outside the field, which means
     we can alias adjacent data.  */
  if (MEM_P (op0))
    {
      op0 = shallow_copy_rtx (op0);
      set_mem_alias_set (op0, 0);
      set_mem_expr (op0, 0);
    }

  /* If OP0 is a register, BITPOS must count within a word.
     But as we have it, it counts within whatever size OP0 now has.
     On a bigendian machine, these are not the same, so convert.  */
  if (BYTES_BIG_ENDIAN
      && !MEM_P (op0)
      && unit > GET_MODE_BITSIZE (GET_MODE (op0)))
    bitpos += unit - GET_MODE_BITSIZE (GET_MODE (op0));

  /* Storing an lsb-aligned field in a register
     can be done with a movestrict instruction.  */

  if (!MEM_P (op0)
      && (BYTES_BIG_ENDIAN ? bitpos + bitsize == unit : bitpos == 0)
      && bitsize == GET_MODE_BITSIZE (fieldmode)
      && (movstrict_optab->handlers[fieldmode].insn_code
	  != CODE_FOR_nothing))
    {
      int icode = movstrict_optab->handlers[fieldmode].insn_code;

      /* Get appropriate low part of the value being stored.  */
      if (GET_CODE (value) == CONST_INT || REG_P (value))
	value = gen_lowpart (fieldmode, value);
      else if (!(GET_CODE (value) == SYMBOL_REF
		 || GET_CODE (value) == LABEL_REF
		 || GET_CODE (value) == CONST))
	value = convert_to_mode (fieldmode, value, 0);

      if (! (*insn_data[icode].operand[1].predicate) (value, fieldmode))
	value = copy_to_mode_reg (fieldmode, value);

      if (GET_CODE (op0) == SUBREG)
	{
	  /* Else we've got some float mode source being extracted into
	     a different float mode destination -- this combination of
	     subregs results in Severe Tire Damage.  */
	  gcc_assert (GET_MODE (SUBREG_REG (op0)) == fieldmode
		      || GET_MODE_CLASS (fieldmode) == MODE_INT
		      || GET_MODE_CLASS (fieldmode) == MODE_PARTIAL_INT);
	  op0 = SUBREG_REG (op0);
	}

      emit_insn (GEN_FCN (icode)
		 (gen_rtx_SUBREG (fieldmode, op0,
				  (bitnum % BITS_PER_WORD) / BITS_PER_UNIT
				  + (offset * UNITS_PER_WORD)),
				  value));

      return value;
    }

  /* Handle fields bigger than a word.  */

  if (bitsize > BITS_PER_WORD)
    {
      /* Here we transfer the words of the field
	 in the order least significant first.
	 This is because the most significant word is the one which may
	 be less than full.
	 However, only do that if the value is not BLKmode.  */

      unsigned int backwards = WORDS_BIG_ENDIAN && fieldmode != BLKmode;
      unsigned int nwords = (bitsize + (BITS_PER_WORD - 1)) / BITS_PER_WORD;
      unsigned int i;

      /* This is the mode we must force value to, so that there will be enough
	 subwords to extract.  Note that fieldmode will often (always?) be
	 VOIDmode, because that is what store_field uses to indicate that this
	 is a bit field, but passing VOIDmode to operand_subword_force
	 is not allowed.  */
      fieldmode = GET_MODE (value);
      if (fieldmode == VOIDmode)
	fieldmode = smallest_mode_for_size (nwords * BITS_PER_WORD, MODE_INT);

      for (i = 0; i < nwords; i++)
	{
	  /* If I is 0, use the low-order word in both field and target;
	     if I is 1, use the next to lowest word; and so on.  */
	  unsigned int wordnum = (backwards ? nwords - i - 1 : i);
	  unsigned int bit_offset = (backwards
				     ? MAX ((int) bitsize - ((int) i + 1)
					    * BITS_PER_WORD,
					    0)
				     : (int) i * BITS_PER_WORD);

	  store_bit_field (op0, MIN (BITS_PER_WORD,
				     bitsize - i * BITS_PER_WORD),
			   bitnum + bit_offset, word_mode,
			   operand_subword_force (value, wordnum, fieldmode));
	}
      return value;
    }

  /* From here on we can assume that the field to be stored in is
     a full-word (whatever type that is), since it is shorter than a word.  */

  /* OFFSET is the number of words or bytes (UNIT says which)
     from STR_RTX to the first word or byte containing part of the field.  */

  if (!MEM_P (op0))
    {
      if (offset != 0
	  || GET_MODE_SIZE (GET_MODE (op0)) > UNITS_PER_WORD)
	{
	  if (!REG_P (op0))
	    {
	      /* Since this is a destination (lvalue), we can't copy
		 it to a pseudo.  We can remove a SUBREG that does not
		 change the size of the operand.  Such a SUBREG may
		 have been added above.  */
	      gcc_assert (GET_CODE (op0) == SUBREG
			  && (GET_MODE_SIZE (GET_MODE (op0))
			      == GET_MODE_SIZE (GET_MODE (SUBREG_REG (op0)))));
	      op0 = SUBREG_REG (op0);
	    }
	  op0 = gen_rtx_SUBREG (mode_for_size (BITS_PER_WORD, MODE_INT, 0),
		                op0, (offset * UNITS_PER_WORD));
	}
      offset = 0;
    }

  /* If VALUE has a floating-point or complex mode, access it as an
     integer of the corresponding size.  This can occur on a machine
     with 64 bit registers that uses SFmode for float.  It can also
     occur for unaligned float or complex fields.  */
  orig_value = value;
  if (GET_MODE (value) != VOIDmode
      && GET_MODE_CLASS (GET_MODE (value)) != MODE_INT
      && GET_MODE_CLASS (GET_MODE (value)) != MODE_PARTIAL_INT)
    {
      value = gen_reg_rtx (int_mode_for_mode (GET_MODE (value)));
      emit_move_insn (gen_lowpart (GET_MODE (orig_value), value), orig_value);
    }

  /* Now OFFSET is nonzero only if OP0 is memory
     and is therefore always measured in bytes.  */

  if (HAVE_insv
      && GET_MODE (value) != BLKmode
      && bitsize > 0
      && GET_MODE_BITSIZE (op_mode) >= bitsize
      && ! ((REG_P (op0) || GET_CODE (op0) == SUBREG)
	    && (bitsize + bitpos > GET_MODE_BITSIZE (op_mode)))
      && insn_data[CODE_FOR_insv].operand[1].predicate (GEN_INT (bitsize),
							VOIDmode))
    {
      int xbitpos = bitpos;
      rtx value1;
      rtx xop0 = op0;
      rtx last = get_last_insn ();
      rtx pat;
      enum machine_mode maxmode = mode_for_extraction (EP_insv, 3);
      int save_volatile_ok = volatile_ok;

      volatile_ok = 1;

      /* If this machine's insv can only insert into a register, copy OP0
	 into a register and save it back later.  */
      if (MEM_P (op0)
	  && ! ((*insn_data[(int) CODE_FOR_insv].operand[0].predicate)
		(op0, VOIDmode)))
	{
	  rtx tempreg;
	  enum machine_mode bestmode;

	  /* Get the mode to use for inserting into this field.  If OP0 is
	     BLKmode, get the smallest mode consistent with the alignment. If
	     OP0 is a non-BLKmode object that is no wider than MAXMODE, use its
	     mode. Otherwise, use the smallest mode containing the field.  */

	  if (GET_MODE (op0) == BLKmode
	      || GET_MODE_SIZE (GET_MODE (op0)) > GET_MODE_SIZE (maxmode))
	    bestmode
	      = get_best_mode (bitsize, bitnum, MEM_ALIGN (op0), maxmode,
			       MEM_VOLATILE_P (op0));
	  else
	    bestmode = GET_MODE (op0);

	  if (bestmode == VOIDmode
	      || GET_MODE_SIZE (bestmode) < GET_MODE_SIZE (fieldmode)
	      || (SLOW_UNALIGNED_ACCESS (bestmode, MEM_ALIGN (op0))
		  && GET_MODE_BITSIZE (bestmode) > MEM_ALIGN (op0)))
	    goto insv_loses;

	  /* Adjust address to point to the containing unit of that mode.
	     Compute offset as multiple of this unit, counting in bytes.  */
	  unit = GET_MODE_BITSIZE (bestmode);
	  offset = (bitnum / unit) * GET_MODE_SIZE (bestmode);
	  bitpos = bitnum % unit;
	  op0 = adjust_address (op0, bestmode,  offset);

	  /* Fetch that unit, store the bitfield in it, then store
	     the unit.  */
	  tempreg = copy_to_reg (op0);
	  store_bit_field (tempreg, bitsize, bitpos, fieldmode, orig_value);
	  emit_move_insn (op0, tempreg);
	  return value;
	}
      volatile_ok = save_volatile_ok;

      /* Add OFFSET into OP0's address.  */
      if (MEM_P (xop0))
	xop0 = adjust_address (xop0, byte_mode, offset);

      /* If xop0 is a register, we need it in MAXMODE
	 to make it acceptable to the format of insv.  */
      if (GET_CODE (xop0) == SUBREG)
	/* We can't just change the mode, because this might clobber op0,
	   and we will need the original value of op0 if insv fails.  */
	xop0 = gen_rtx_SUBREG (maxmode, SUBREG_REG (xop0), SUBREG_BYTE (xop0));
      if (REG_P (xop0) && GET_MODE (xop0) != maxmode)
	xop0 = gen_rtx_SUBREG (maxmode, xop0, 0);

      /* On big-endian machines, we count bits from the most significant.
	 If the bit field insn does not, we must invert.  */

      if (BITS_BIG_ENDIAN != BYTES_BIG_ENDIAN)
	xbitpos = unit - bitsize - xbitpos;

      /* We have been counting XBITPOS within UNIT.
	 Count instead within the size of the register.  */
      if (BITS_BIG_ENDIAN && !MEM_P (xop0))
	xbitpos += GET_MODE_BITSIZE (maxmode) - unit;

      unit = GET_MODE_BITSIZE (maxmode);

      /* Convert VALUE to maxmode (which insv insn wants) in VALUE1.  */
      value1 = value;
      if (GET_MODE (value) != maxmode)
	{
	  if (GET_MODE_BITSIZE (GET_MODE (value)) >= bitsize)
	    {
	      /* Optimization: Don't bother really extending VALUE
		 if it has all the bits we will actually use.  However,
		 if we must narrow it, be sure we do it correctly.  */

	      if (GET_MODE_SIZE (GET_MODE (value)) < GET_MODE_SIZE (maxmode))
		{
		  rtx tmp;

		  tmp = simplify_subreg (maxmode, value1, GET_MODE (value), 0);
		  if (! tmp)
		    tmp = simplify_gen_subreg (maxmode,
					       force_reg (GET_MODE (value),
							  value1),
					       GET_MODE (value), 0);
		  value1 = tmp;
		}
	      else
		value1 = gen_lowpart (maxmode, value1);
	    }
	  else if (GET_CODE (value) == CONST_INT)
	    value1 = gen_int_mode (INTVAL (value), maxmode);
	  else
	    /* Parse phase is supposed to make VALUE's data type
	       match that of the component reference, which is a type
	       at least as wide as the field; so VALUE should have
	       a mode that corresponds to that type.  */
	    gcc_assert (CONSTANT_P (value));
	}

      /* If this machine's insv insists on a register,
	 get VALUE1 into a register.  */
      if (! ((*insn_data[(int) CODE_FOR_insv].operand[3].predicate)
	     (value1, maxmode)))
	value1 = force_reg (maxmode, value1);

      pat = gen_insv (xop0, GEN_INT (bitsize), GEN_INT (xbitpos), value1);
      if (pat)
	emit_insn (pat);
      else
	{
	  delete_insns_since (last);
	  store_fixed_bit_field (op0, offset, bitsize, bitpos, value);
	}
    }
  else
    insv_loses:
    /* Insv is not available; store using shifts and boolean ops.  */
    store_fixed_bit_field (op0, offset, bitsize, bitpos, value);
  return value;
}

/* Use shifts and boolean operations to store VALUE
   into a bit field of width BITSIZE
   in a memory location specified by OP0 except offset by OFFSET bytes.
     (OFFSET must be 0 if OP0 is a register.)
   The field starts at position BITPOS within the byte.
    (If OP0 is a register, it may be a full word or a narrower mode,
     but BITPOS still counts within a full word,
     which is significant on bigendian machines.)  */

static void
store_fixed_bit_field (rtx op0, unsigned HOST_WIDE_INT offset,
		       unsigned HOST_WIDE_INT bitsize,
		       unsigned HOST_WIDE_INT bitpos, rtx value)
{
  enum machine_mode mode;
  unsigned int total_bits = BITS_PER_WORD;
  rtx temp;
  int all_zero = 0;
  int all_one = 0;

  /* There is a case not handled here:
     a structure with a known alignment of just a halfword
     and a field split across two aligned halfwords within the structure.
     Or likewise a structure with a known alignment of just a byte
     and a field split across two bytes.
     Such cases are not supposed to be able to occur.  */

  if (REG_P (op0) || GET_CODE (op0) == SUBREG)
    {
      gcc_assert (!offset);
      /* Special treatment for a bit field split across two registers.  */
      if (bitsize + bitpos > BITS_PER_WORD)
	{
	  store_split_bit_field (op0, bitsize, bitpos, value);
	  return;
	}
    }
  else
    {
      /* Get the proper mode to use for this field.  We want a mode that
	 includes the entire field.  If such a mode would be larger than
	 a word, we won't be doing the extraction the normal way.
	 We don't want a mode bigger than the destination.  */

      mode = GET_MODE (op0);
      if (GET_MODE_BITSIZE (mode) == 0
	  || GET_MODE_BITSIZE (mode) > GET_MODE_BITSIZE (word_mode))
	mode = word_mode;
      mode = get_best_mode (bitsize, bitpos + offset * BITS_PER_UNIT,
			    MEM_ALIGN (op0), mode, MEM_VOLATILE_P (op0));

      if (mode == VOIDmode)
	{
	  /* The only way this should occur is if the field spans word
	     boundaries.  */
	  store_split_bit_field (op0, bitsize, bitpos + offset * BITS_PER_UNIT,
				 value);
	  return;
	}

      total_bits = GET_MODE_BITSIZE (mode);

      /* Make sure bitpos is valid for the chosen mode.  Adjust BITPOS to
	 be in the range 0 to total_bits-1, and put any excess bytes in
	 OFFSET.  */
      if (bitpos >= total_bits)
	{
	  offset += (bitpos / total_bits) * (total_bits / BITS_PER_UNIT);
	  bitpos -= ((bitpos / total_bits) * (total_bits / BITS_PER_UNIT)
		     * BITS_PER_UNIT);
	}

      /* Get ref to an aligned byte, halfword, or word containing the field.
	 Adjust BITPOS to be position within a word,
	 and OFFSET to be the offset of that word.
	 Then alter OP0 to refer to that word.  */
      bitpos += (offset % (total_bits / BITS_PER_UNIT)) * BITS_PER_UNIT;
      offset -= (offset % (total_bits / BITS_PER_UNIT));
      op0 = adjust_address (op0, mode, offset);
    }

  mode = GET_MODE (op0);

  /* Now MODE is either some integral mode for a MEM as OP0,
     or is a full-word for a REG as OP0.  TOTAL_BITS corresponds.
     The bit field is contained entirely within OP0.
     BITPOS is the starting bit number within OP0.
     (OP0's mode may actually be narrower than MODE.)  */

  if (BYTES_BIG_ENDIAN)
      /* BITPOS is the distance between our msb
	 and that of the containing datum.
	 Convert it to the distance from the lsb.  */
      bitpos = total_bits - bitsize - bitpos;

  /* Now BITPOS is always the distance between our lsb
     and that of OP0.  */

  /* Shift VALUE left by BITPOS bits.  If VALUE is not constant,
     we must first convert its mode to MODE.  */

  if (GET_CODE (value) == CONST_INT)
    {
      HOST_WIDE_INT v = INTVAL (value);

      if (bitsize < HOST_BITS_PER_WIDE_INT)
	v &= ((HOST_WIDE_INT) 1 << bitsize) - 1;

      if (v == 0)
	all_zero = 1;
      else if ((bitsize < HOST_BITS_PER_WIDE_INT
		&& v == ((HOST_WIDE_INT) 1 << bitsize) - 1)
	       || (bitsize == HOST_BITS_PER_WIDE_INT && v == -1))
	all_one = 1;

      value = lshift_value (mode, value, bitpos, bitsize);
    }
  else
    {
      int must_and = (GET_MODE_BITSIZE (GET_MODE (value)) != bitsize
		      && bitpos + bitsize != GET_MODE_BITSIZE (mode));

      if (GET_MODE (value) != mode)
	{
	  if ((REG_P (value) || GET_CODE (value) == SUBREG)
	      && GET_MODE_SIZE (mode) < GET_MODE_SIZE (GET_MODE (value)))
	    value = gen_lowpart (mode, value);
	  else
	    value = convert_to_mode (mode, value, 1);
	}

      if (must_and)
	value = expand_binop (mode, and_optab, value,
			      mask_rtx (mode, 0, bitsize, 0),
			      NULL_RTX, 1, OPTAB_LIB_WIDEN);
      if (bitpos > 0)
	value = expand_shift (LSHIFT_EXPR, mode, value,
			      build_int_cst (NULL_TREE, bitpos), NULL_RTX, 1);
    }

  /* Now clear the chosen bits in OP0,
     except that if VALUE is -1 we need not bother.  */
  /* We keep the intermediates in registers to allow CSE to combine
     consecutive bitfield assignments.  */

  temp = force_reg (mode, op0);

  if (! all_one)
    {
      temp = expand_binop (mode, and_optab, temp,
			   mask_rtx (mode, bitpos, bitsize, 1),
			   NULL_RTX, 1, OPTAB_LIB_WIDEN);
      temp = force_reg (mode, temp);
    }

  /* Now logical-or VALUE into OP0, unless it is zero.  */

  if (! all_zero)
    {
      temp = expand_binop (mode, ior_optab, temp, value,
			   NULL_RTX, 1, OPTAB_LIB_WIDEN);
      temp = force_reg (mode, temp);
    }

  if (op0 != temp)
    emit_move_insn (op0, temp);
}

/* Store a bit field that is split across multiple accessible memory objects.

   OP0 is the REG, SUBREG or MEM rtx for the first of the objects.
   BITSIZE is the field width; BITPOS the position of its first bit
   (within the word).
   VALUE is the value to store.

   This does not yet handle fields wider than BITS_PER_WORD.  */

static void
store_split_bit_field (rtx op0, unsigned HOST_WIDE_INT bitsize,
		       unsigned HOST_WIDE_INT bitpos, rtx value)
{
  unsigned int unit;
  unsigned int bitsdone = 0;

  /* Make sure UNIT isn't larger than BITS_PER_WORD, we can only handle that
     much at a time.  */
  if (REG_P (op0) || GET_CODE (op0) == SUBREG)
    unit = BITS_PER_WORD;
  else
    unit = MIN (MEM_ALIGN (op0), BITS_PER_WORD);

  /* If VALUE is a constant other than a CONST_INT, get it into a register in
     WORD_MODE.  If we can do this using gen_lowpart_common, do so.  Note
     that VALUE might be a floating-point constant.  */
  if (CONSTANT_P (value) && GET_CODE (value) != CONST_INT)
    {
      rtx word = gen_lowpart_common (word_mode, value);

      if (word && (value != word))
	value = word;
      else
	value = gen_lowpart_common (word_mode,
				    force_reg (GET_MODE (value) != VOIDmode
					       ? GET_MODE (value)
					       : word_mode, value));
    }

  while (bitsdone < bitsize)
    {
      unsigned HOST_WIDE_INT thissize;
      rtx part, word;
      unsigned HOST_WIDE_INT thispos;
      unsigned HOST_WIDE_INT offset;

      offset = (bitpos + bitsdone) / unit;
      thispos = (bitpos + bitsdone) % unit;

      /* THISSIZE must not overrun a word boundary.  Otherwise,
	 store_fixed_bit_field will call us again, and we will mutually
	 recurse forever.  */
      thissize = MIN (bitsize - bitsdone, BITS_PER_WORD);
      thissize = MIN (thissize, unit - thispos);

      if (BYTES_BIG_ENDIAN)
	{
	  int total_bits;

	  /* We must do an endian conversion exactly the same way as it is
	     done in extract_bit_field, so that the two calls to
	     extract_fixed_bit_field will have comparable arguments.  */
	  if (!MEM_P (value) || GET_MODE (value) == BLKmode)
	    total_bits = BITS_PER_WORD;
	  else
	    total_bits = GET_MODE_BITSIZE (GET_MODE (value));

	  /* Fetch successively less significant portions.  */
	  if (GET_CODE (value) == CONST_INT)
	    part = GEN_INT (((unsigned HOST_WIDE_INT) (INTVAL (value))
			     >> (bitsize - bitsdone - thissize))
			    & (((HOST_WIDE_INT) 1 << thissize) - 1));
	  else
	    /* The args are chosen so that the last part includes the
	       lsb.  Give extract_bit_field the value it needs (with
	       endianness compensation) to fetch the piece we want.  */
	    part = extract_fixed_bit_field (word_mode, value, 0, thissize,
					    total_bits - bitsize + bitsdone,
					    NULL_RTX, 1);
	}
      else
	{
	  /* Fetch successively more significant portions.  */
	  if (GET_CODE (value) == CONST_INT)
	    part = GEN_INT (((unsigned HOST_WIDE_INT) (INTVAL (value))
			     >> bitsdone)
			    & (((HOST_WIDE_INT) 1 << thissize) - 1));
	  else
	    part = extract_fixed_bit_field (word_mode, value, 0, thissize,
					    bitsdone, NULL_RTX, 1);
	}

      /* If OP0 is a register, then handle OFFSET here.

	 When handling multiword bitfields, extract_bit_field may pass
	 down a word_mode SUBREG of a larger REG for a bitfield that actually
	 crosses a word boundary.  Thus, for a SUBREG, we must find
	 the current word starting from the base register.  */
      if (GET_CODE (op0) == SUBREG)
	{
	  int word_offset = (SUBREG_BYTE (op0) / UNITS_PER_WORD) + offset;
	  word = operand_subword_force (SUBREG_REG (op0), word_offset,
					GET_MODE (SUBREG_REG (op0)));
	  offset = 0;
	}
      else if (REG_P (op0))
	{
	  word = operand_subword_force (op0, offset, GET_MODE (op0));
	  offset = 0;
	}
      else
	word = op0;

      /* OFFSET is in UNITs, and UNIT is in bits.
         store_fixed_bit_field wants offset in bytes.  */
      store_fixed_bit_field (word, offset * unit / BITS_PER_UNIT, thissize,
			     thispos, part);
      bitsdone += thissize;
    }
}

/* Generate code to extract a byte-field from STR_RTX
   containing BITSIZE bits, starting at BITNUM,
   and put it in TARGET if possible (if TARGET is nonzero).
   Regardless of TARGET, we return the rtx for where the value is placed.

   STR_RTX is the structure containing the byte (a REG or MEM).
   UNSIGNEDP is nonzero if this is an unsigned bit field.
   MODE is the natural mode of the field value once extracted.
   TMODE is the mode the caller would like the value to have;
   but the value may be returned with type MODE instead.

   TOTAL_SIZE is the size in bytes of the containing structure,
   or -1 if varying.

   If a TARGET is specified and we can store in it at no extra cost,
   we do so, and return TARGET.
   Otherwise, we return a REG of mode TMODE or MODE, with TMODE preferred
   if they are equally easy.  */

rtx
extract_bit_field (rtx str_rtx, unsigned HOST_WIDE_INT bitsize,
		   unsigned HOST_WIDE_INT bitnum, int unsignedp, rtx target,
		   enum machine_mode mode, enum machine_mode tmode)
{
  unsigned int unit
    = (MEM_P (str_rtx)) ? BITS_PER_UNIT : BITS_PER_WORD;
  unsigned HOST_WIDE_INT offset, bitpos;
  rtx op0 = str_rtx;
  rtx spec_target = target;
  rtx spec_target_subreg = 0;
  enum machine_mode int_mode;
  enum machine_mode extv_mode = mode_for_extraction (EP_extv, 0);
  enum machine_mode extzv_mode = mode_for_extraction (EP_extzv, 0);
  enum machine_mode mode1;
  int byte_offset;

  if (tmode == VOIDmode)
    tmode = mode;

  while (GET_CODE (op0) == SUBREG)
    {
      bitnum += SUBREG_BYTE (op0) * BITS_PER_UNIT;
      op0 = SUBREG_REG (op0);
    }

  /* If we have an out-of-bounds access to a register, just return an
     uninitialized register of the required mode.  This can occur if the
     source code contains an out-of-bounds access to a small array.  */
  if (REG_P (op0) && bitnum >= GET_MODE_BITSIZE (GET_MODE (op0)))
    return gen_reg_rtx (tmode);

  if (REG_P (op0)
      && mode == GET_MODE (op0)
      && bitnum == 0
      && bitsize == GET_MODE_BITSIZE (GET_MODE (op0)))
    {
      /* We're trying to extract a full register from itself.  */
      return op0;
    }

  /* Use vec_extract patterns for extracting parts of vectors whenever
     available.  */
  if (VECTOR_MODE_P (GET_MODE (op0))
      && !MEM_P (op0)
      && (vec_extract_optab->handlers[GET_MODE (op0)].insn_code
	  != CODE_FOR_nothing)
      && ((bitnum + bitsize - 1) / GET_MODE_BITSIZE (GET_MODE_INNER (GET_MODE (op0)))
	  == bitnum / GET_MODE_BITSIZE (GET_MODE_INNER (GET_MODE (op0)))))
    {
      enum machine_mode outermode = GET_MODE (op0);
      enum machine_mode innermode = GET_MODE_INNER (outermode);
      int icode = (int) vec_extract_optab->handlers[outermode].insn_code;
      unsigned HOST_WIDE_INT pos = bitnum / GET_MODE_BITSIZE (innermode);
      rtx rtxpos = GEN_INT (pos);
      rtx src = op0;
      rtx dest = NULL, pat, seq;
      enum machine_mode mode0 = insn_data[icode].operand[0].mode;
      enum machine_mode mode1 = insn_data[icode].operand[1].mode;
      enum machine_mode mode2 = insn_data[icode].operand[2].mode;

      if (innermode == tmode || innermode == mode)
	dest = target;

      if (!dest)
	dest = gen_reg_rtx (innermode);

      start_sequence ();

      if (! (*insn_data[icode].operand[0].predicate) (dest, mode0))
	dest = copy_to_mode_reg (mode0, dest);

      if (! (*insn_data[icode].operand[1].predicate) (src, mode1))
	src = copy_to_mode_reg (mode1, src);

      if (! (*insn_data[icode].operand[2].predicate) (rtxpos, mode2))
	rtxpos = copy_to_mode_reg (mode1, rtxpos);

      /* We could handle this, but we should always be called with a pseudo
	 for our targets and all insns should take them as outputs.  */
      gcc_assert ((*insn_data[icode].operand[0].predicate) (dest, mode0)
		  && (*insn_data[icode].operand[1].predicate) (src, mode1)
		  && (*insn_data[icode].operand[2].predicate) (rtxpos, mode2));

      pat = GEN_FCN (icode) (dest, src, rtxpos);
      seq = get_insns ();
      end_sequence ();
      if (pat)
	{
	  emit_insn (seq);
	  emit_insn (pat);
	  return dest;
	}
    }

  /* Make sure we are playing with integral modes.  Pun with subregs
     if we aren't.  */
  {
    enum machine_mode imode = int_mode_for_mode (GET_MODE (op0));
    if (imode != GET_MODE (op0))
      {
	if (MEM_P (op0))
	  op0 = adjust_address (op0, imode, 0);
	else
	  {
	    gcc_assert (imode != BLKmode);
	    op0 = gen_lowpart (imode, op0);

	    /* If we got a SUBREG, force it into a register since we
	       aren't going to be able to do another SUBREG on it.  */
	    if (GET_CODE (op0) == SUBREG)
	      op0 = force_reg (imode, op0);
	  }
      }
  }

  /* We may be accessing data outside the field, which means
     we can alias adjacent data.  */
  if (MEM_P (op0))
    {
      op0 = shallow_copy_rtx (op0);
      set_mem_alias_set (op0, 0);
      set_mem_expr (op0, 0);
    }

  /* Extraction of a full-word or multi-word value from a structure
     in a register or aligned memory can be done with just a SUBREG.
     A subword value in the least significant part of a register
     can also be extracted with a SUBREG.  For this, we need the
     byte offset of the value in op0.  */

  bitpos = bitnum % unit;
  offset = bitnum / unit;
  byte_offset = bitpos / BITS_PER_UNIT + offset * UNITS_PER_WORD;

  /* If OP0 is a register, BITPOS must count within a word.
     But as we have it, it counts within whatever size OP0 now has.
     On a bigendian machine, these are not the same, so convert.  */
  if (BYTES_BIG_ENDIAN
      && !MEM_P (op0)
      && unit > GET_MODE_BITSIZE (GET_MODE (op0)))
    bitpos += unit - GET_MODE_BITSIZE (GET_MODE (op0));

  /* ??? We currently assume TARGET is at least as big as BITSIZE.
     If that's wrong, the solution is to test for it and set TARGET to 0
     if needed.  */

  /* Only scalar integer modes can be converted via subregs.  There is an
     additional problem for FP modes here in that they can have a precision
     which is different from the size.  mode_for_size uses precision, but
     we want a mode based on the size, so we must avoid calling it for FP
     modes.  */
  mode1  = (SCALAR_INT_MODE_P (tmode)
	    ? mode_for_size (bitsize, GET_MODE_CLASS (tmode), 0)
	    : mode);

  if (((bitsize >= BITS_PER_WORD && bitsize == GET_MODE_BITSIZE (mode)
	&& bitpos % BITS_PER_WORD == 0)
       || (mode1 != BLKmode
	   /* ??? The big endian test here is wrong.  This is correct
	      if the value is in a register, and if mode_for_size is not
	      the same mode as op0.  This causes us to get unnecessarily
	      inefficient code from the Thumb port when -mbig-endian.  */
	   && (BYTES_BIG_ENDIAN
	       ? bitpos + bitsize == BITS_PER_WORD
	       : bitpos == 0)))
      && ((!MEM_P (op0)
	   && TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (mode),
				     GET_MODE_BITSIZE (GET_MODE (op0)))
	   && GET_MODE_SIZE (mode1) != 0
	   && byte_offset % GET_MODE_SIZE (mode1) == 0)
	  || (MEM_P (op0)
	      && (! SLOW_UNALIGNED_ACCESS (mode, MEM_ALIGN (op0))
		  || (offset * BITS_PER_UNIT % bitsize == 0
		      && MEM_ALIGN (op0) % bitsize == 0)))))
    {
      if (mode1 != GET_MODE (op0))
	{
	  if (MEM_P (op0))
	    op0 = adjust_address (op0, mode1, offset);
	  else
	    {
	      rtx sub = simplify_gen_subreg (mode1, op0, GET_MODE (op0),
					     byte_offset);
	      if (sub == NULL)
		goto no_subreg_mode_swap;
	      op0 = sub;
	    }
	}
      if (mode1 != mode)
	return convert_to_mode (tmode, op0, unsignedp);
      return op0;
    }
 no_subreg_mode_swap:

  /* Handle fields bigger than a word.  */

  if (bitsize > BITS_PER_WORD)
    {
      /* Here we transfer the words of the field
	 in the order least significant first.
	 This is because the most significant word is the one which may
	 be less than full.  */

      unsigned int nwords = (bitsize + (BITS_PER_WORD - 1)) / BITS_PER_WORD;
      unsigned int i;

      if (target == 0 || !REG_P (target))
	target = gen_reg_rtx (mode);

      /* Indicate for flow that the entire target reg is being set.  */
      emit_insn (gen_rtx_CLOBBER (VOIDmode, target));

      for (i = 0; i < nwords; i++)
	{
	  /* If I is 0, use the low-order word in both field and target;
	     if I is 1, use the next to lowest word; and so on.  */
	  /* Word number in TARGET to use.  */
	  unsigned int wordnum
	    = (WORDS_BIG_ENDIAN
	       ? GET_MODE_SIZE (GET_MODE (target)) / UNITS_PER_WORD - i - 1
	       : i);
	  /* Offset from start of field in OP0.  */
	  unsigned int bit_offset = (WORDS_BIG_ENDIAN
				     ? MAX (0, ((int) bitsize - ((int) i + 1)
						* (int) BITS_PER_WORD))
				     : (int) i * BITS_PER_WORD);
	  rtx target_part = operand_subword (target, wordnum, 1, VOIDmode);
	  rtx result_part
	    = extract_bit_field (op0, MIN (BITS_PER_WORD,
					   bitsize - i * BITS_PER_WORD),
				 bitnum + bit_offset, 1, target_part, mode,
				 word_mode);

	  gcc_assert (target_part);

	  if (result_part != target_part)
	    emit_move_insn (target_part, result_part);
	}

      if (unsignedp)
	{
	  /* Unless we've filled TARGET, the upper regs in a multi-reg value
	     need to be zero'd out.  */
	  if (GET_MODE_SIZE (GET_MODE (target)) > nwords * UNITS_PER_WORD)
	    {
	      unsigned int i, total_words;

	      total_words = GET_MODE_SIZE (GET_MODE (target)) / UNITS_PER_WORD;
	      for (i = nwords; i < total_words; i++)
		emit_move_insn
		  (operand_subword (target,
				    WORDS_BIG_ENDIAN ? total_words - i - 1 : i,
				    1, VOIDmode),
		   const0_rtx);
	    }
	  return target;
	}

      /* Signed bit field: sign-extend with two arithmetic shifts.  */
      target = expand_shift (LSHIFT_EXPR, mode, target,
			     build_int_cst (NULL_TREE,
					    GET_MODE_BITSIZE (mode) - bitsize),
			     NULL_RTX, 0);
      return expand_shift (RSHIFT_EXPR, mode, target,
			   build_int_cst (NULL_TREE,
					  GET_MODE_BITSIZE (mode) - bitsize),
			   NULL_RTX, 0);
    }

  /* From here on we know the desired field is smaller than a word.  */

  /* Check if there is a correspondingly-sized integer field, so we can
     safely extract it as one size of integer, if necessary; then
     truncate or extend to the size that is wanted; then use SUBREGs or
     convert_to_mode to get one of the modes we really wanted.  */

  int_mode = int_mode_for_mode (tmode);
  if (int_mode == BLKmode)
    int_mode = int_mode_for_mode (mode);
  /* Should probably push op0 out to memory and then do a load.  */
  gcc_assert (int_mode != BLKmode);

  /* OFFSET is the number of words or bytes (UNIT says which)
     from STR_RTX to the first word or byte containing part of the field.  */
  if (!MEM_P (op0))
    {
      if (offset != 0
	  || GET_MODE_SIZE (GET_MODE (op0)) > UNITS_PER_WORD)
	{
	  if (!REG_P (op0))
	    op0 = copy_to_reg (op0);
	  op0 = gen_rtx_SUBREG (mode_for_size (BITS_PER_WORD, MODE_INT, 0),
		                op0, (offset * UNITS_PER_WORD));
	}
      offset = 0;
    }

  /* Now OFFSET is nonzero only for memory operands.  */

  if (unsignedp)
    {
      if (HAVE_extzv
	  && bitsize > 0
	  && GET_MODE_BITSIZE (extzv_mode) >= bitsize
	  && ! ((REG_P (op0) || GET_CODE (op0) == SUBREG)
		&& (bitsize + bitpos > GET_MODE_BITSIZE (extzv_mode))))
	{
	  unsigned HOST_WIDE_INT xbitpos = bitpos, xoffset = offset;
	  rtx bitsize_rtx, bitpos_rtx;
	  rtx last = get_last_insn ();
	  rtx xop0 = op0;
	  rtx xtarget = target;
	  rtx xspec_target = spec_target;
	  rtx xspec_target_subreg = spec_target_subreg;
	  rtx pat;
	  enum machine_mode maxmode = mode_for_extraction (EP_extzv, 0);

	  if (MEM_P (xop0))
	    {
	      int save_volatile_ok = volatile_ok;
	      volatile_ok = 1;

	      /* Is the memory operand acceptable?  */
	      if (! ((*insn_data[(int) CODE_FOR_extzv].operand[1].predicate)
		     (xop0, GET_MODE (xop0))))
		{
		  /* No, load into a reg and extract from there.  */
		  enum machine_mode bestmode;

		  /* Get the mode to use for inserting into this field.  If
		     OP0 is BLKmode, get the smallest mode consistent with the
		     alignment. If OP0 is a non-BLKmode object that is no
		     wider than MAXMODE, use its mode. Otherwise, use the
		     smallest mode containing the field.  */

		  if (GET_MODE (xop0) == BLKmode
		      || (GET_MODE_SIZE (GET_MODE (op0))
			  > GET_MODE_SIZE (maxmode)))
		    bestmode = get_best_mode (bitsize, bitnum,
					      MEM_ALIGN (xop0), maxmode,
					      MEM_VOLATILE_P (xop0));
		  else
		    bestmode = GET_MODE (xop0);

		  if (bestmode == VOIDmode
		      || (SLOW_UNALIGNED_ACCESS (bestmode, MEM_ALIGN (xop0))
			  && GET_MODE_BITSIZE (bestmode) > MEM_ALIGN (xop0)))
		    goto extzv_loses;

		  /* Compute offset as multiple of this unit,
		     counting in bytes.  */
		  unit = GET_MODE_BITSIZE (bestmode);
		  xoffset = (bitnum / unit) * GET_MODE_SIZE (bestmode);
		  xbitpos = bitnum % unit;
		  xop0 = adjust_address (xop0, bestmode, xoffset);

		  /* Make sure register is big enough for the whole field. */
		  if (xoffset * BITS_PER_UNIT + unit 
		      < offset * BITS_PER_UNIT + bitsize)
		    goto extzv_loses;

		  /* Fetch it to a register in that size.  */
		  xop0 = force_reg (bestmode, xop0);

		  /* XBITPOS counts within UNIT, which is what is expected.  */
		}
	      else
		/* Get ref to first byte containing part of the field.  */
		xop0 = adjust_address (xop0, byte_mode, xoffset);

	      volatile_ok = save_volatile_ok;
	    }

	  /* If op0 is a register, we need it in MAXMODE (which is usually
	     SImode). to make it acceptable to the format of extzv.  */
	  if (GET_CODE (xop0) == SUBREG && GET_MODE (xop0) != maxmode)
	    goto extzv_loses;
	  if (REG_P (xop0) && GET_MODE (xop0) != maxmode)
	    xop0 = gen_rtx_SUBREG (maxmode, xop0, 0);

	  /* On big-endian machines, we count bits from the most significant.
	     If the bit field insn does not, we must invert.  */
	  if (BITS_BIG_ENDIAN != BYTES_BIG_ENDIAN)
	    xbitpos = unit - bitsize - xbitpos;

	  /* Now convert from counting within UNIT to counting in MAXMODE.  */
	  if (BITS_BIG_ENDIAN && !MEM_P (xop0))
	    xbitpos += GET_MODE_BITSIZE (maxmode) - unit;

	  unit = GET_MODE_BITSIZE (maxmode);

	  if (xtarget == 0)
	    xtarget = xspec_target = gen_reg_rtx (tmode);

	  if (GET_MODE (xtarget) != maxmode)
	    {
	      if (REG_P (xtarget))
		{
		  int wider = (GET_MODE_SIZE (maxmode)
			       > GET_MODE_SIZE (GET_MODE (xtarget)));
		  xtarget = gen_lowpart (maxmode, xtarget);
		  if (wider)
		    xspec_target_subreg = xtarget;
		}
	      else
		xtarget = gen_reg_rtx (maxmode);
	    }

	  /* If this machine's extzv insists on a register target,
	     make sure we have one.  */
	  if (! ((*insn_data[(int) CODE_FOR_extzv].operand[0].predicate)
		 (xtarget, maxmode)))
	    xtarget = gen_reg_rtx (maxmode);

	  bitsize_rtx = GEN_INT (bitsize);
	  bitpos_rtx = GEN_INT (xbitpos);

	  pat = gen_extzv (xtarget, xop0, bitsize_rtx, bitpos_rtx);
	  if (pat)
	    {
	      emit_insn (pat);
	      target = xtarget;
	      spec_target = xspec_target;
	      spec_target_subreg = xspec_target_subreg;
	    }
	  else
	    {
	      delete_insns_since (last);
	      target = extract_fixed_bit_field (int_mode, op0, offset, bitsize,
						bitpos, target, 1);
	    }
	}
      else
      extzv_loses:
	target = extract_fixed_bit_field (int_mode, op0, offset, bitsize,
					  bitpos, target, 1);
    }
  else
    {
      if (HAVE_extv
	  && bitsize > 0
	  && GET_MODE_BITSIZE (extv_mode) >= bitsize
	  && ! ((REG_P (op0) || GET_CODE (op0) == SUBREG)
		&& (bitsize + bitpos > GET_MODE_BITSIZE (extv_mode))))
	{
	  int xbitpos = bitpos, xoffset = offset;
	  rtx bitsize_rtx, bitpos_rtx;
	  rtx last = get_last_insn ();
	  rtx xop0 = op0, xtarget = target;
	  rtx xspec_target = spec_target;
	  rtx xspec_target_subreg = spec_target_subreg;
	  rtx pat;
	  enum machine_mode maxmode = mode_for_extraction (EP_extv, 0);

	  if (MEM_P (xop0))
	    {
	      /* Is the memory operand acceptable?  */
	      if (! ((*insn_data[(int) CODE_FOR_extv].operand[1].predicate)
		     (xop0, GET_MODE (xop0))))
		{
		  /* No, load into a reg and extract from there.  */
		  enum machine_mode bestmode;

		  /* Get the mode to use for inserting into this field.  If
		     OP0 is BLKmode, get the smallest mode consistent with the
		     alignment. If OP0 is a non-BLKmode object that is no
		     wider than MAXMODE, use its mode. Otherwise, use the
		     smallest mode containing the field.  */

		  if (GET_MODE (xop0) == BLKmode
		      || (GET_MODE_SIZE (GET_MODE (op0))
			  > GET_MODE_SIZE (maxmode)))
		    bestmode = get_best_mode (bitsize, bitnum,
					      MEM_ALIGN (xop0), maxmode,
					      MEM_VOLATILE_P (xop0));
		  else
		    bestmode = GET_MODE (xop0);

		  if (bestmode == VOIDmode
		      || (SLOW_UNALIGNED_ACCESS (bestmode, MEM_ALIGN (xop0))
			  && GET_MODE_BITSIZE (bestmode) > MEM_ALIGN (xop0)))
		    goto extv_loses;

		  /* Compute offset as multiple of this unit,
		     counting in bytes.  */
		  unit = GET_MODE_BITSIZE (bestmode);
		  xoffset = (bitnum / unit) * GET_MODE_SIZE (bestmode);
		  xbitpos = bitnum % unit;
		  xop0 = adjust_address (xop0, bestmode, xoffset);

		  /* Make sure register is big enough for the whole field. */
		  if (xoffset * BITS_PER_UNIT + unit 
		      < offset * BITS_PER_UNIT + bitsize)
		    goto extv_loses;

		  /* Fetch it to a register in that size.  */
		  xop0 = force_reg (bestmode, xop0);

		  /* XBITPOS counts within UNIT, which is what is expected.  */
		}
	      else
		/* Get ref to first byte containing part of the field.  */
		xop0 = adjust_address (xop0, byte_mode, xoffset);
	    }

	  /* If op0 is a register, we need it in MAXMODE (which is usually
	     SImode) to make it acceptable to the format of extv.  */
	  if (GET_CODE (xop0) == SUBREG && GET_MODE (xop0) != maxmode)
	    goto extv_loses;
	  if (REG_P (xop0) && GET_MODE (xop0) != maxmode)
	    xop0 = gen_rtx_SUBREG (maxmode, xop0, 0);

	  /* On big-endian machines, we count bits from the most significant.
	     If the bit field insn does not, we must invert.  */
	  if (BITS_BIG_ENDIAN != BYTES_BIG_ENDIAN)
	    xbitpos = unit - bitsize - xbitpos;

	  /* XBITPOS counts within a size of UNIT.
	     Adjust to count within a size of MAXMODE.  */
	  if (BITS_BIG_ENDIAN && !MEM_P (xop0))
	    xbitpos += (GET_MODE_BITSIZE (maxmode) - unit);

	  unit = GET_MODE_BITSIZE (maxmode);

	  if (xtarget == 0)
	    xtarget = xspec_target = gen_reg_rtx (tmode);

	  if (GET_MODE (xtarget) != maxmode)
	    {
	      if (REG_P (xtarget))
		{
		  int wider = (GET_MODE_SIZE (maxmode)
			       > GET_MODE_SIZE (GET_MODE (xtarget)));
		  xtarget = gen_lowpart (maxmode, xtarget);
		  if (wider)
		    xspec_target_subreg = xtarget;
		}
	      else
		xtarget = gen_reg_rtx (maxmode);
	    }

	  /* If this machine's extv insists on a register target,
	     make sure we have one.  */
	  if (! ((*insn_data[(int) CODE_FOR_extv].operand[0].predicate)
		 (xtarget, maxmode)))
	    xtarget = gen_reg_rtx (maxmode);

	  bitsize_rtx = GEN_INT (bitsize);
	  bitpos_rtx = GEN_INT (xbitpos);

	  pat = gen_extv (xtarget, xop0, bitsize_rtx, bitpos_rtx);
	  if (pat)
	    {
	      emit_insn (pat);
	      target = xtarget;
	      spec_target = xspec_target;
	      spec_target_subreg = xspec_target_subreg;
	    }
	  else
	    {
	      delete_insns_since (last);
	      target = extract_fixed_bit_field (int_mode, op0, offset, bitsize,
						bitpos, target, 0);
	    }
	}
      else
      extv_loses:
	target = extract_fixed_bit_field (int_mode, op0, offset, bitsize,
					  bitpos, target, 0);
    }
  if (target == spec_target)
    return target;
  if (target == spec_target_subreg)
    return spec_target;
  if (GET_MODE (target) != tmode && GET_MODE (target) != mode)
    {
      /* If the target mode is not a scalar integral, first convert to the
	 integer mode of that size and then access it as a floating-point
	 value via a SUBREG.  */
      if (!SCALAR_INT_MODE_P (tmode))
	{
	  enum machine_mode smode
	    = mode_for_size (GET_MODE_BITSIZE (tmode), MODE_INT, 0);
	  target = convert_to_mode (smode, target, unsignedp);
	  target = force_reg (smode, target);
	  return gen_lowpart (tmode, target);
	}

      return convert_to_mode (tmode, target, unsignedp);
    }
  return target;
}

/* Extract a bit field using shifts and boolean operations
   Returns an rtx to represent the value.
   OP0 addresses a register (word) or memory (byte).
   BITPOS says which bit within the word or byte the bit field starts in.
   OFFSET says how many bytes farther the bit field starts;
    it is 0 if OP0 is a register.
   BITSIZE says how many bits long the bit field is.
    (If OP0 is a register, it may be narrower than a full word,
     but BITPOS still counts within a full word,
     which is significant on bigendian machines.)

   UNSIGNEDP is nonzero for an unsigned bit field (don't sign-extend value).
   If TARGET is nonzero, attempts to store the value there
   and return TARGET, but this is not guaranteed.
   If TARGET is not used, create a pseudo-reg of mode TMODE for the value.  */

static rtx
extract_fixed_bit_field (enum machine_mode tmode, rtx op0,
			 unsigned HOST_WIDE_INT offset,
			 unsigned HOST_WIDE_INT bitsize,
			 unsigned HOST_WIDE_INT bitpos, rtx target,
			 int unsignedp)
{
  unsigned int total_bits = BITS_PER_WORD;
  enum machine_mode mode;

  if (GET_CODE (op0) == SUBREG || REG_P (op0))
    {
      /* Special treatment for a bit field split across two registers.  */
      if (bitsize + bitpos > BITS_PER_WORD)
	return extract_split_bit_field (op0, bitsize, bitpos, unsignedp);
    }
  else
    {
      /* Get the proper mode to use for this field.  We want a mode that
	 includes the entire field.  If such a mode would be larger than
	 a word, we won't be doing the extraction the normal way.  */

      mode = get_best_mode (bitsize, bitpos + offset * BITS_PER_UNIT,
			    MEM_ALIGN (op0), word_mode, MEM_VOLATILE_P (op0));

      if (mode == VOIDmode)
	/* The only way this should occur is if the field spans word
	   boundaries.  */
	return extract_split_bit_field (op0, bitsize,
					bitpos + offset * BITS_PER_UNIT,
					unsignedp);

      total_bits = GET_MODE_BITSIZE (mode);

      /* Make sure bitpos is valid for the chosen mode.  Adjust BITPOS to
	 be in the range 0 to total_bits-1, and put any excess bytes in
	 OFFSET.  */
      if (bitpos >= total_bits)
	{
	  offset += (bitpos / total_bits) * (total_bits / BITS_PER_UNIT);
	  bitpos -= ((bitpos / total_bits) * (total_bits / BITS_PER_UNIT)
		     * BITS_PER_UNIT);
	}

      /* Get ref to an aligned byte, halfword, or word containing the field.
	 Adjust BITPOS to be position within a word,
	 and OFFSET to be the offset of that word.
	 Then alter OP0 to refer to that word.  */
      bitpos += (offset % (total_bits / BITS_PER_UNIT)) * BITS_PER_UNIT;
      offset -= (offset % (total_bits / BITS_PER_UNIT));
      op0 = adjust_address (op0, mode, offset);
    }

  mode = GET_MODE (op0);

  if (BYTES_BIG_ENDIAN)
    /* BITPOS is the distance between our msb and that of OP0.
       Convert it to the distance from the lsb.  */
    bitpos = total_bits - bitsize - bitpos;

  /* Now BITPOS is always the distance between the field's lsb and that of OP0.
     We have reduced the big-endian case to the little-endian case.  */

  if (unsignedp)
    {
      if (bitpos)
	{
	  /* If the field does not already start at the lsb,
	     shift it so it does.  */
	  tree amount = build_int_cst (NULL_TREE, bitpos);
	  /* Maybe propagate the target for the shift.  */
	  /* But not if we will return it--could confuse integrate.c.  */
	  rtx subtarget = (target != 0 && REG_P (target) ? target : 0);
	  if (tmode != mode) subtarget = 0;
	  op0 = expand_shift (RSHIFT_EXPR, mode, op0, amount, subtarget, 1);
	}
      /* Convert the value to the desired mode.  */
      if (mode != tmode)
	op0 = convert_to_mode (tmode, op0, 1);

      /* Unless the msb of the field used to be the msb when we shifted,
	 mask out the upper bits.  */

      if (GET_MODE_BITSIZE (mode) != bitpos + bitsize)
	return expand_binop (GET_MODE (op0), and_optab, op0,
			     mask_rtx (GET_MODE (op0), 0, bitsize, 0),
			     target, 1, OPTAB_LIB_WIDEN);
      return op0;
    }

  /* To extract a signed bit-field, first shift its msb to the msb of the word,
     then arithmetic-shift its lsb to the lsb of the word.  */
  op0 = force_reg (mode, op0);
  if (mode != tmode)
    target = 0;

  /* Find the narrowest integer mode that contains the field.  */

  for (mode = GET_CLASS_NARROWEST_MODE (MODE_INT); mode != VOIDmode;
       mode = GET_MODE_WIDER_MODE (mode))
    if (GET_MODE_BITSIZE (mode) >= bitsize + bitpos)
      {
	op0 = convert_to_mode (mode, op0, 0);
	break;
      }

  if (GET_MODE_BITSIZE (mode) != (bitsize + bitpos))
    {
      tree amount
	= build_int_cst (NULL_TREE,
			 GET_MODE_BITSIZE (mode) - (bitsize + bitpos));
      /* Maybe propagate the target for the shift.  */
      rtx subtarget = (target != 0 && REG_P (target) ? target : 0);
      op0 = expand_shift (LSHIFT_EXPR, mode, op0, amount, subtarget, 1);
    }

  return expand_shift (RSHIFT_EXPR, mode, op0,
		       build_int_cst (NULL_TREE,
				      GET_MODE_BITSIZE (mode) - bitsize),
		       target, 0);
}

/* Return a constant integer (CONST_INT or CONST_DOUBLE) mask value
   of mode MODE with BITSIZE ones followed by BITPOS zeros, or the
   complement of that if COMPLEMENT.  The mask is truncated if
   necessary to the width of mode MODE.  The mask is zero-extended if
   BITSIZE+BITPOS is too small for MODE.  */

static rtx
mask_rtx (enum machine_mode mode, int bitpos, int bitsize, int complement)
{
  HOST_WIDE_INT masklow, maskhigh;

  if (bitsize == 0)
    masklow = 0;
  else if (bitpos < HOST_BITS_PER_WIDE_INT)
    masklow = (HOST_WIDE_INT) -1 << bitpos;
  else
    masklow = 0;

  if (bitpos + bitsize < HOST_BITS_PER_WIDE_INT)
    masklow &= ((unsigned HOST_WIDE_INT) -1
		>> (HOST_BITS_PER_WIDE_INT - bitpos - bitsize));

  if (bitpos <= HOST_BITS_PER_WIDE_INT)
    maskhigh = -1;
  else
    maskhigh = (HOST_WIDE_INT) -1 << (bitpos - HOST_BITS_PER_WIDE_INT);

  if (bitsize == 0)
    maskhigh = 0;
  else if (bitpos + bitsize > HOST_BITS_PER_WIDE_INT)
    maskhigh &= ((unsigned HOST_WIDE_INT) -1
		 >> (2 * HOST_BITS_PER_WIDE_INT - bitpos - bitsize));
  else
    maskhigh = 0;

  if (complement)
    {
      maskhigh = ~maskhigh;
      masklow = ~masklow;
    }

  return immed_double_const (masklow, maskhigh, mode);
}

/* Return a constant integer (CONST_INT or CONST_DOUBLE) rtx with the value
   VALUE truncated to BITSIZE bits and then shifted left BITPOS bits.  */

static rtx
lshift_value (enum machine_mode mode, rtx value, int bitpos, int bitsize)
{
  unsigned HOST_WIDE_INT v = INTVAL (value);
  HOST_WIDE_INT low, high;

  if (bitsize < HOST_BITS_PER_WIDE_INT)
    v &= ~((HOST_WIDE_INT) -1 << bitsize);

  if (bitpos < HOST_BITS_PER_WIDE_INT)
    {
      low = v << bitpos;
      high = (bitpos > 0 ? (v >> (HOST_BITS_PER_WIDE_INT - bitpos)) : 0);
    }
  else
    {
      low = 0;
      high = v << (bitpos - HOST_BITS_PER_WIDE_INT);
    }

  return immed_double_const (low, high, mode);
}

/* Extract a bit field from a memory by forcing the alignment of the
   memory.  This efficient only if the field spans at least 4 boundaries.

   OP0 is the MEM.
   BITSIZE is the field width; BITPOS is the position of the first bit.
   UNSIGNEDP is true if the result should be zero-extended.  */

static rtx
extract_force_align_mem_bit_field (rtx op0, unsigned HOST_WIDE_INT bitsize,
				   unsigned HOST_WIDE_INT bitpos,
				   int unsignedp)
{
  enum machine_mode mode, dmode;
  unsigned int m_bitsize, m_size;
  unsigned int sign_shift_up, sign_shift_dn;
  rtx base, a1, a2, v1, v2, comb, shift, result, start;

  /* Choose a mode that will fit BITSIZE.  */
  mode = smallest_mode_for_size (bitsize, MODE_INT);
  m_size = GET_MODE_SIZE (mode);
  m_bitsize = GET_MODE_BITSIZE (mode);

  /* Choose a mode twice as wide.  Fail if no such mode exists.  */
  dmode = mode_for_size (m_bitsize * 2, MODE_INT, false);
  if (dmode == BLKmode)
    return NULL;

  do_pending_stack_adjust ();
  start = get_last_insn ();

  /* At the end, we'll need an additional shift to deal with sign/zero
     extension.  By default this will be a left+right shift of the
     appropriate size.  But we may be able to eliminate one of them.  */
  sign_shift_up = sign_shift_dn = m_bitsize - bitsize;

  if (STRICT_ALIGNMENT)
    {
      base = plus_constant (XEXP (op0, 0), bitpos / BITS_PER_UNIT);
      bitpos %= BITS_PER_UNIT;

      /* We load two values to be concatenate.  There's an edge condition
	 that bears notice -- an aligned value at the end of a page can
	 only load one value lest we segfault.  So the two values we load
	 are at "base & -size" and "(base + size - 1) & -size".  If base
	 is unaligned, the addresses will be aligned and sequential; if
	 base is aligned, the addresses will both be equal to base.  */

      a1 = expand_simple_binop (Pmode, AND, force_operand (base, NULL),
				GEN_INT (-(HOST_WIDE_INT)m_size),
				NULL, true, OPTAB_LIB_WIDEN);
      mark_reg_pointer (a1, m_bitsize);
      v1 = gen_rtx_MEM (mode, a1);
      set_mem_align (v1, m_bitsize);
      v1 = force_reg (mode, validize_mem (v1));

      a2 = plus_constant (base, GET_MODE_SIZE (mode) - 1);
      a2 = expand_simple_binop (Pmode, AND, force_operand (a2, NULL),
				GEN_INT (-(HOST_WIDE_INT)m_size),
				NULL, true, OPTAB_LIB_WIDEN);
      v2 = gen_rtx_MEM (mode, a2);
      set_mem_align (v2, m_bitsize);
      v2 = force_reg (mode, validize_mem (v2));

      /* Combine these two values into a double-word value.  */
      if (m_bitsize == BITS_PER_WORD)
	{
	  comb = gen_reg_rtx (dmode);
	  emit_insn (gen_rtx_CLOBBER (VOIDmode, comb));
	  emit_move_insn (gen_rtx_SUBREG (mode, comb, 0), v1);
	  emit_move_insn (gen_rtx_SUBREG (mode, comb, m_size), v2);
	}
      else
	{
	  if (BYTES_BIG_ENDIAN)
	    comb = v1, v1 = v2, v2 = comb;
	  v1 = convert_modes (dmode, mode, v1, true);
	  if (v1 == NULL)
	    goto fail;
	  v2 = convert_modes (dmode, mode, v2, true);
	  v2 = expand_simple_binop (dmode, ASHIFT, v2, GEN_INT (m_bitsize),
				    NULL, true, OPTAB_LIB_WIDEN);
	  if (v2 == NULL)
	    goto fail;
	  comb = expand_simple_binop (dmode, IOR, v1, v2, NULL,
				      true, OPTAB_LIB_WIDEN);
	  if (comb == NULL)
	    goto fail;
	}

      shift = expand_simple_binop (Pmode, AND, base, GEN_INT (m_size - 1),
				   NULL, true, OPTAB_LIB_WIDEN);
      shift = expand_mult (Pmode, shift, GEN_INT (BITS_PER_UNIT), NULL, 1);

      if (bitpos != 0)
	{
	  if (sign_shift_up <= bitpos)
	    bitpos -= sign_shift_up, sign_shift_up = 0;
	  shift = expand_simple_binop (Pmode, PLUS, shift, GEN_INT (bitpos),
				       NULL, true, OPTAB_LIB_WIDEN);
	}
    }
  else
    {
      unsigned HOST_WIDE_INT offset = bitpos / BITS_PER_UNIT;
      bitpos %= BITS_PER_UNIT;

      /* When strict alignment is not required, we can just load directly
	 from memory without masking.  If the remaining BITPOS offset is
	 small enough, we may be able to do all operations in MODE as 
	 opposed to DMODE.  */
      if (bitpos + bitsize <= m_bitsize)
	dmode = mode;
      comb = adjust_address (op0, dmode, offset);

      if (sign_shift_up <= bitpos)
	bitpos -= sign_shift_up, sign_shift_up = 0;
      shift = GEN_INT (bitpos);
    }

  /* Shift down the double-word such that the requested value is at bit 0.  */
  if (shift != const0_rtx)
    comb = expand_simple_binop (dmode, unsignedp ? LSHIFTRT : ASHIFTRT,
				comb, shift, NULL, unsignedp, OPTAB_LIB_WIDEN);
  if (comb == NULL)
    goto fail;

  /* If the field exactly matches MODE, then all we need to do is return the
     lowpart.  Otherwise, shift to get the sign bits set properly.  */
  result = force_reg (mode, gen_lowpart (mode, comb));

  if (sign_shift_up)
    result = expand_simple_binop (mode, ASHIFT, result,
				  GEN_INT (sign_shift_up),
				  NULL_RTX, 0, OPTAB_LIB_WIDEN);
  if (sign_shift_dn)
    result = expand_simple_binop (mode, unsignedp ? LSHIFTRT : ASHIFTRT,
				  result, GEN_INT (sign_shift_dn),
				  NULL_RTX, 0, OPTAB_LIB_WIDEN);

  return result;

 fail:
  delete_insns_since (start);
  return NULL;
}

/* Extract a bit field that is split across two words
   and return an RTX for the result.

   OP0 is the REG, SUBREG or MEM rtx for the first of the two words.
   BITSIZE is the field width; BITPOS, position of its first bit, in the word.
   UNSIGNEDP is 1 if should zero-extend the contents; else sign-extend.  */

static rtx
extract_split_bit_field (rtx op0, unsigned HOST_WIDE_INT bitsize,
			 unsigned HOST_WIDE_INT bitpos, int unsignedp)
{
  unsigned int unit;
  unsigned int bitsdone = 0;
  rtx result = NULL_RTX;
  int first = 1;

  /* Make sure UNIT isn't larger than BITS_PER_WORD, we can only handle that
     much at a time.  */
  if (REG_P (op0) || GET_CODE (op0) == SUBREG)
    unit = BITS_PER_WORD;
  else
    {
      unit = MIN (MEM_ALIGN (op0), BITS_PER_WORD);
      if (0 && bitsize / unit > 2)
	{
	  rtx tmp = extract_force_align_mem_bit_field (op0, bitsize, bitpos,
						       unsignedp);
	  if (tmp)
	    return tmp;
	}
    }

  while (bitsdone < bitsize)
    {
      unsigned HOST_WIDE_INT thissize;
      rtx part, word;
      unsigned HOST_WIDE_INT thispos;
      unsigned HOST_WIDE_INT offset;

      offset = (bitpos + bitsdone) / unit;
      thispos = (bitpos + bitsdone) % unit;

      /* THISSIZE must not overrun a word boundary.  Otherwise,
	 extract_fixed_bit_field will call us again, and we will mutually
	 recurse forever.  */
      thissize = MIN (bitsize - bitsdone, BITS_PER_WORD);
      thissize = MIN (thissize, unit - thispos);

      /* If OP0 is a register, then handle OFFSET here.

	 When handling multiword bitfields, extract_bit_field may pass
	 down a word_mode SUBREG of a larger REG for a bitfield that actually
	 crosses a word boundary.  Thus, for a SUBREG, we must find
	 the current word starting from the base register.  */
      if (GET_CODE (op0) == SUBREG)
	{
	  int word_offset = (SUBREG_BYTE (op0) / UNITS_PER_WORD) + offset;
	  word = operand_subword_force (SUBREG_REG (op0), word_offset,
					GET_MODE (SUBREG_REG (op0)));
	  offset = 0;
	}
      else if (REG_P (op0))
	{
	  word = operand_subword_force (op0, offset, GET_MODE (op0));
	  offset = 0;
	}
      else
	word = op0;

      /* Extract the parts in bit-counting order,
	 whose meaning is determined by BYTES_PER_UNIT.
	 OFFSET is in UNITs, and UNIT is in bits.
	 extract_fixed_bit_field wants offset in bytes.  */
      part = extract_fixed_bit_field (word_mode, word,
				      offset * unit / BITS_PER_UNIT,
				      thissize, thispos, 0, 1);
      bitsdone += thissize;

      /* Shift this part into place for the result.  */
      if (BYTES_BIG_ENDIAN)
	{
	  if (bitsize != bitsdone)
	    part = expand_shift (LSHIFT_EXPR, word_mode, part,
				 build_int_cst (NULL_TREE, bitsize - bitsdone),
				 0, 1);
	}
      else
	{
	  if (bitsdone != thissize)
	    part = expand_shift (LSHIFT_EXPR, word_mode, part,
				 build_int_cst (NULL_TREE,
						bitsdone - thissize), 0, 1);
	}

      if (first)
	result = part;
      else
	/* Combine the parts with bitwise or.  This works
	   because we extracted each part as an unsigned bit field.  */
	result = expand_binop (word_mode, ior_optab, part, result, NULL_RTX, 1,
			       OPTAB_LIB_WIDEN);

      first = 0;
    }

  /* Unsigned bit field: we are done.  */
  if (unsignedp)
    return result;
  /* Signed bit field: sign-extend with two arithmetic shifts.  */
  result = expand_shift (LSHIFT_EXPR, word_mode, result,
			 build_int_cst (NULL_TREE, BITS_PER_WORD - bitsize),
			 NULL_RTX, 0);
  return expand_shift (RSHIFT_EXPR, word_mode, result,
		       build_int_cst (NULL_TREE, BITS_PER_WORD - bitsize),
		       NULL_RTX, 0);
}

/* Add INC into TARGET.  */

void
expand_inc (rtx target, rtx inc)
{
  rtx value = expand_binop (GET_MODE (target), add_optab,
			    target, inc,
			    target, 0, OPTAB_LIB_WIDEN);
  if (value != target)
    emit_move_insn (target, value);
}

/* Subtract DEC from TARGET.  */

void
expand_dec (rtx target, rtx dec)
{
  rtx value = expand_binop (GET_MODE (target), sub_optab,
			    target, dec,
			    target, 0, OPTAB_LIB_WIDEN);
  if (value != target)
    emit_move_insn (target, value);
}

/* Output a shift instruction for expression code CODE,
   with SHIFTED being the rtx for the value to shift,
   and AMOUNT the tree for the amount to shift by.
   Store the result in the rtx TARGET, if that is convenient.
   If UNSIGNEDP is nonzero, do a logical shift; otherwise, arithmetic.
   Return the rtx for where the value is.  */

rtx
expand_shift (enum tree_code code, enum machine_mode mode, rtx shifted,
	      tree amount, rtx target, int unsignedp)
{
  rtx op1, temp = 0;
  int left = (code == LSHIFT_EXPR || code == LROTATE_EXPR);
  int rotate = (code == LROTATE_EXPR || code == RROTATE_EXPR);
  int try;

  /* Previously detected shift-counts computed by NEGATE_EXPR
     and shifted in the other direction; but that does not work
     on all machines.  */

  op1 = expand_normal (amount);

  if (SHIFT_COUNT_TRUNCATED)
    {
      if (GET_CODE (op1) == CONST_INT
	  && ((unsigned HOST_WIDE_INT) INTVAL (op1) >=
	      (unsigned HOST_WIDE_INT) GET_MODE_BITSIZE (mode)))
	op1 = GEN_INT ((unsigned HOST_WIDE_INT) INTVAL (op1)
		       % GET_MODE_BITSIZE (mode));
      else if (GET_CODE (op1) == SUBREG
	       && subreg_lowpart_p (op1))
	op1 = SUBREG_REG (op1);
    }

  if (op1 == const0_rtx)
    return shifted;

  /* Check whether its cheaper to implement a left shift by a constant
     bit count by a sequence of additions.  */
  if (code == LSHIFT_EXPR
      && GET_CODE (op1) == CONST_INT
      && INTVAL (op1) > 0
      && INTVAL (op1) < GET_MODE_BITSIZE (mode)
      && INTVAL (op1) < MAX_BITS_PER_WORD
      && shift_cost[mode][INTVAL (op1)] > INTVAL (op1) * add_cost[mode]
      && shift_cost[mode][INTVAL (op1)] != MAX_COST)
    {
      int i;
      for (i = 0; i < INTVAL (op1); i++)
	{
	  temp = force_reg (mode, shifted);
	  shifted = expand_binop (mode, add_optab, temp, temp, NULL_RTX,
				  unsignedp, OPTAB_LIB_WIDEN);
	}
      return shifted;
    }

  for (try = 0; temp == 0 && try < 3; try++)
    {
      enum optab_methods methods;

      if (try == 0)
	methods = OPTAB_DIRECT;
      else if (try == 1)
	methods = OPTAB_WIDEN;
      else
	methods = OPTAB_LIB_WIDEN;

      if (rotate)
	{
	  /* Widening does not work for rotation.  */
	  if (methods == OPTAB_WIDEN)
	    continue;
	  else if (methods == OPTAB_LIB_WIDEN)
	    {
	      /* If we have been unable to open-code this by a rotation,
		 do it as the IOR of two shifts.  I.e., to rotate A
		 by N bits, compute (A << N) | ((unsigned) A >> (C - N))
		 where C is the bitsize of A.

		 It is theoretically possible that the target machine might
		 not be able to perform either shift and hence we would
		 be making two libcalls rather than just the one for the
		 shift (similarly if IOR could not be done).  We will allow
		 this extremely unlikely lossage to avoid complicating the
		 code below.  */

	      rtx subtarget = target == shifted ? 0 : target;
	      tree new_amount, other_amount;
	      rtx temp1;
	      tree type = TREE_TYPE (amount);
	      if (GET_MODE (op1) != TYPE_MODE (type)
		  && GET_MODE (op1) != VOIDmode)
		op1 = convert_to_mode (TYPE_MODE (type), op1, 1);
	      new_amount = make_tree (type, op1);
	      other_amount
		= fold_build2 (MINUS_EXPR, type,
			       build_int_cst (type, GET_MODE_BITSIZE (mode)),
			       new_amount);

	      shifted = force_reg (mode, shifted);

	      temp = expand_shift (left ? LSHIFT_EXPR : RSHIFT_EXPR,
				   mode, shifted, new_amount, 0, 1);
	      temp1 = expand_shift (left ? RSHIFT_EXPR : LSHIFT_EXPR,
				    mode, shifted, other_amount, subtarget, 1);
	      return expand_binop (mode, ior_optab, temp, temp1, target,
				   unsignedp, methods);
	    }

	  temp = expand_binop (mode,
			       left ? rotl_optab : rotr_optab,
			       shifted, op1, target, unsignedp, methods);
	}
      else if (unsignedp)
	temp = expand_binop (mode,
			     left ? ashl_optab : lshr_optab,
			     shifted, op1, target, unsignedp, methods);

      /* Do arithmetic shifts.
	 Also, if we are going to widen the operand, we can just as well
	 use an arithmetic right-shift instead of a logical one.  */
      if (temp == 0 && ! rotate
	  && (! unsignedp || (! left && methods == OPTAB_WIDEN)))
	{
	  enum optab_methods methods1 = methods;

	  /* If trying to widen a log shift to an arithmetic shift,
	     don't accept an arithmetic shift of the same size.  */
	  if (unsignedp)
	    methods1 = OPTAB_MUST_WIDEN;

	  /* Arithmetic shift */

	  temp = expand_binop (mode,
			       left ? ashl_optab : ashr_optab,
			       shifted, op1, target, unsignedp, methods1);
	}

      /* We used to try extzv here for logical right shifts, but that was
	 only useful for one machine, the VAX, and caused poor code
	 generation there for lshrdi3, so the code was deleted and a
	 define_expand for lshrsi3 was added to vax.md.  */
    }

  gcc_assert (temp);
  return temp;
}

enum alg_code {
  alg_unknown,
  alg_zero,
  alg_m, alg_shift,
  alg_add_t_m2,
  alg_sub_t_m2,
  alg_add_factor,
  alg_sub_factor,
  alg_add_t2_m,
  alg_sub_t2_m,
  alg_impossible
};

/* This structure holds the "cost" of a multiply sequence.  The
   "cost" field holds the total rtx_cost of every operator in the
   synthetic multiplication sequence, hence cost(a op b) is defined
   as rtx_cost(op) + cost(a) + cost(b), where cost(leaf) is zero.
   The "latency" field holds the minimum possible latency of the
   synthetic multiply, on a hypothetical infinitely parallel CPU.
   This is the critical path, or the maximum height, of the expression
   tree which is the sum of rtx_costs on the most expensive path from
   any leaf to the root.  Hence latency(a op b) is defined as zero for
   leaves and rtx_cost(op) + max(latency(a), latency(b)) otherwise.  */

struct mult_cost {
  short cost;     /* Total rtx_cost of the multiplication sequence.  */
  short latency;  /* The latency of the multiplication sequence.  */
};

/* This macro is used to compare a pointer to a mult_cost against an
   single integer "rtx_cost" value.  This is equivalent to the macro
   CHEAPER_MULT_COST(X,Z) where Z = {Y,Y}.  */
#define MULT_COST_LESS(X,Y) ((X)->cost < (Y)	\
			     || ((X)->cost == (Y) && (X)->latency < (Y)))

/* This macro is used to compare two pointers to mult_costs against
   each other.  The macro returns true if X is cheaper than Y.
   Currently, the cheaper of two mult_costs is the one with the
   lower "cost".  If "cost"s are tied, the lower latency is cheaper.  */
#define CHEAPER_MULT_COST(X,Y)  ((X)->cost < (Y)->cost		\
				 || ((X)->cost == (Y)->cost	\
				     && (X)->latency < (Y)->latency))

/* This structure records a sequence of operations.
   `ops' is the number of operations recorded.
   `cost' is their total cost.
   The operations are stored in `op' and the corresponding
   logarithms of the integer coefficients in `log'.

   These are the operations:
   alg_zero		total := 0;
   alg_m		total := multiplicand;
   alg_shift		total := total * coeff
   alg_add_t_m2		total := total + multiplicand * coeff;
   alg_sub_t_m2		total := total - multiplicand * coeff;
   alg_add_factor	total := total * coeff + total;
   alg_sub_factor	total := total * coeff - total;
   alg_add_t2_m		total := total * coeff + multiplicand;
   alg_sub_t2_m		total := total * coeff - multiplicand;

   The first operand must be either alg_zero or alg_m.  */

struct algorithm
{
  struct mult_cost cost;
  short ops;
  /* The size of the OP and LOG fields are not directly related to the
     word size, but the worst-case algorithms will be if we have few
     consecutive ones or zeros, i.e., a multiplicand like 10101010101...
     In that case we will generate shift-by-2, add, shift-by-2, add,...,
     in total wordsize operations.  */
  enum alg_code op[MAX_BITS_PER_WORD];
  char log[MAX_BITS_PER_WORD];
};

/* The entry for our multiplication cache/hash table.  */
struct alg_hash_entry {
  /* The number we are multiplying by.  */
  unsigned HOST_WIDE_INT t;

  /* The mode in which we are multiplying something by T.  */
  enum machine_mode mode;

  /* The best multiplication algorithm for t.  */
  enum alg_code alg;

  /* The cost of multiplication if ALG_CODE is not alg_impossible.
     Otherwise, the cost within which multiplication by T is
     impossible.  */
  struct mult_cost cost;
};

/* The number of cache/hash entries.  */
#if HOST_BITS_PER_WIDE_INT == 64
#define NUM_ALG_HASH_ENTRIES 1031
#else
#define NUM_ALG_HASH_ENTRIES 307
#endif

/* Each entry of ALG_HASH caches alg_code for some integer.  This is
   actually a hash table.  If we have a collision, that the older
   entry is kicked out.  */
static struct alg_hash_entry alg_hash[NUM_ALG_HASH_ENTRIES];

/* Indicates the type of fixup needed after a constant multiplication.
   BASIC_VARIANT means no fixup is needed, NEGATE_VARIANT means that
   the result should be negated, and ADD_VARIANT means that the
   multiplicand should be added to the result.  */
enum mult_variant {basic_variant, negate_variant, add_variant};

static void synth_mult (struct algorithm *, unsigned HOST_WIDE_INT,
			const struct mult_cost *, enum machine_mode mode);
static bool choose_mult_variant (enum machine_mode, HOST_WIDE_INT,
				 struct algorithm *, enum mult_variant *, int);
static rtx expand_mult_const (enum machine_mode, rtx, HOST_WIDE_INT, rtx,
			      const struct algorithm *, enum mult_variant);
static unsigned HOST_WIDE_INT choose_multiplier (unsigned HOST_WIDE_INT, int,
						 int, rtx *, int *, int *);
static unsigned HOST_WIDE_INT invert_mod2n (unsigned HOST_WIDE_INT, int);
static rtx extract_high_half (enum machine_mode, rtx);
static rtx expand_mult_highpart (enum machine_mode, rtx, rtx, rtx, int, int);
static rtx expand_mult_highpart_optab (enum machine_mode, rtx, rtx, rtx,
				       int, int);
/* Compute and return the best algorithm for multiplying by T.
   The algorithm must cost less than cost_limit
   If retval.cost >= COST_LIMIT, no algorithm was found and all
   other field of the returned struct are undefined.
   MODE is the machine mode of the multiplication.  */

static void
synth_mult (struct algorithm *alg_out, unsigned HOST_WIDE_INT t,
	    const struct mult_cost *cost_limit, enum machine_mode mode)
{
  int m;
  struct algorithm *alg_in, *best_alg;
  struct mult_cost best_cost;
  struct mult_cost new_limit;
  int op_cost, op_latency;
  unsigned HOST_WIDE_INT q;
  int maxm = MIN (BITS_PER_WORD, GET_MODE_BITSIZE (mode));
  int hash_index;
  bool cache_hit = false;
  enum alg_code cache_alg = alg_zero;

  /* Indicate that no algorithm is yet found.  If no algorithm
     is found, this value will be returned and indicate failure.  */
  alg_out->cost.cost = cost_limit->cost + 1;
  alg_out->cost.latency = cost_limit->latency + 1;

  if (cost_limit->cost < 0
      || (cost_limit->cost == 0 && cost_limit->latency <= 0))
    return;

  /* Restrict the bits of "t" to the multiplication's mode.  */
  t &= GET_MODE_MASK (mode);

  /* t == 1 can be done in zero cost.  */
  if (t == 1)
    {
      alg_out->ops = 1;
      alg_out->cost.cost = 0;
      alg_out->cost.latency = 0;
      alg_out->op[0] = alg_m;
      return;
    }

  /* t == 0 sometimes has a cost.  If it does and it exceeds our limit,
     fail now.  */
  if (t == 0)
    {
      if (MULT_COST_LESS (cost_limit, zero_cost))
	return;
      else
	{
	  alg_out->ops = 1;
	  alg_out->cost.cost = zero_cost;
	  alg_out->cost.latency = zero_cost;
	  alg_out->op[0] = alg_zero;
	  return;
	}
    }

  /* We'll be needing a couple extra algorithm structures now.  */

  alg_in = alloca (sizeof (struct algorithm));
  best_alg = alloca (sizeof (struct algorithm));
  best_cost = *cost_limit;

  /* Compute the hash index.  */
  hash_index = (t ^ (unsigned int) mode) % NUM_ALG_HASH_ENTRIES;

  /* See if we already know what to do for T.  */
  if (alg_hash[hash_index].t == t
      && alg_hash[hash_index].mode == mode
      && alg_hash[hash_index].alg != alg_unknown)
    {
      cache_alg = alg_hash[hash_index].alg;

      if (cache_alg == alg_impossible)
	{
	  /* The cache tells us that it's impossible to synthesize
	     multiplication by T within alg_hash[hash_index].cost.  */
	  if (!CHEAPER_MULT_COST (&alg_hash[hash_index].cost, cost_limit))
	    /* COST_LIMIT is at least as restrictive as the one
	       recorded in the hash table, in which case we have no
	       hope of synthesizing a multiplication.  Just
	       return.  */
	    return;

	  /* If we get here, COST_LIMIT is less restrictive than the
	     one recorded in the hash table, so we may be able to
	     synthesize a multiplication.  Proceed as if we didn't
	     have the cache entry.  */
	}
      else
	{
	  if (CHEAPER_MULT_COST (cost_limit, &alg_hash[hash_index].cost))
	    /* The cached algorithm shows that this multiplication
	       requires more cost than COST_LIMIT.  Just return.  This
	       way, we don't clobber this cache entry with
	       alg_impossible but retain useful information.  */
	    return;

	  cache_hit = true;

	  switch (cache_alg)
	    {
	    case alg_shift:
	      goto do_alg_shift;

	    case alg_add_t_m2:
	    case alg_sub_t_m2:
	      goto do_alg_addsub_t_m2;

	    case alg_add_factor:
	    case alg_sub_factor:
	      goto do_alg_addsub_factor;

	    case alg_add_t2_m:
	      goto do_alg_add_t2_m;

	    case alg_sub_t2_m:
	      goto do_alg_sub_t2_m;

	    default:
	      gcc_unreachable ();
	    }
	}
    }

  /* If we have a group of zero bits at the low-order part of T, try
     multiplying by the remaining bits and then doing a shift.  */

  if ((t & 1) == 0)
    {
    do_alg_shift:
      m = floor_log2 (t & -t);	/* m = number of low zero bits */
      if (m < maxm)
	{
	  q = t >> m;
	  /* The function expand_shift will choose between a shift and
	     a sequence of additions, so the observed cost is given as
	     MIN (m * add_cost[mode], shift_cost[mode][m]).  */
	  op_cost = m * add_cost[mode];
	  if (shift_cost[mode][m] < op_cost)
	    op_cost = shift_cost[mode][m];
	  new_limit.cost = best_cost.cost - op_cost;
	  new_limit.latency = best_cost.latency - op_cost;
	  synth_mult (alg_in, q, &new_limit, mode);

	  alg_in->cost.cost += op_cost;
	  alg_in->cost.latency += op_cost;
	  if (CHEAPER_MULT_COST (&alg_in->cost, &best_cost))
	    {
	      struct algorithm *x;
	      best_cost = alg_in->cost;
	      x = alg_in, alg_in = best_alg, best_alg = x;
	      best_alg->log[best_alg->ops] = m;
	      best_alg->op[best_alg->ops] = alg_shift;
	    }
	}
      if (cache_hit)
	goto done;
    }

  /* If we have an odd number, add or subtract one.  */
  if ((t & 1) != 0)
    {
      unsigned HOST_WIDE_INT w;

    do_alg_addsub_t_m2:
      for (w = 1; (w & t) != 0; w <<= 1)
	;
      /* If T was -1, then W will be zero after the loop.  This is another
	 case where T ends with ...111.  Handling this with (T + 1) and
	 subtract 1 produces slightly better code and results in algorithm
	 selection much faster than treating it like the ...0111 case
	 below.  */
      if (w == 0
	  || (w > 2
	      /* Reject the case where t is 3.
		 Thus we prefer addition in that case.  */
	      && t != 3))
	{
	  /* T ends with ...111.  Multiply by (T + 1) and subtract 1.  */

	  op_cost = add_cost[mode];
	  new_limit.cost = best_cost.cost - op_cost;
	  new_limit.latency = best_cost.latency - op_cost;
	  synth_mult (alg_in, t + 1, &new_limit, mode);

	  alg_in->cost.cost += op_cost;
	  alg_in->cost.latency += op_cost;
	  if (CHEAPER_MULT_COST (&alg_in->cost, &best_cost))
	    {
	      struct algorithm *x;
	      best_cost = alg_in->cost;
	      x = alg_in, alg_in = best_alg, best_alg = x;
	      best_alg->log[best_alg->ops] = 0;
	      best_alg->op[best_alg->ops] = alg_sub_t_m2;
	    }
	}
      else
	{
	  /* T ends with ...01 or ...011.  Multiply by (T - 1) and add 1.  */

	  op_cost = add_cost[mode];
	  new_limit.cost = best_cost.cost - op_cost;
	  new_limit.latency = best_cost.latency - op_cost;
	  synth_mult (alg_in, t - 1, &new_limit, mode);

	  alg_in->cost.cost += op_cost;
	  alg_in->cost.latency += op_cost;
	  if (CHEAPER_MULT_COST (&alg_in->cost, &best_cost))
	    {
	      struct algorithm *x;
	      best_cost = alg_in->cost;
	      x = alg_in, alg_in = best_alg, best_alg = x;
	      best_alg->log[best_alg->ops] = 0;
	      best_alg->op[best_alg->ops] = alg_add_t_m2;
	    }
	}
      if (cache_hit)
	goto done;
    }

  /* Look for factors of t of the form
     t = q(2**m +- 1), 2 <= m <= floor(log2(t - 1)).
     If we find such a factor, we can multiply by t using an algorithm that
     multiplies by q, shift the result by m and add/subtract it to itself.

     We search for large factors first and loop down, even if large factors
     are less probable than small; if we find a large factor we will find a
     good sequence quickly, and therefore be able to prune (by decreasing
     COST_LIMIT) the search.  */

 do_alg_addsub_factor:
  for (m = floor_log2 (t - 1); m >= 2; m--)
    {
      unsigned HOST_WIDE_INT d;

      d = ((unsigned HOST_WIDE_INT) 1 << m) + 1;
      if (t % d == 0 && t > d && m < maxm
	  && (!cache_hit || cache_alg == alg_add_factor))
	{
	  /* If the target has a cheap shift-and-add instruction use
	     that in preference to a shift insn followed by an add insn.
	     Assume that the shift-and-add is "atomic" with a latency
	     equal to its cost, otherwise assume that on superscalar
	     hardware the shift may be executed concurrently with the
	     earlier steps in the algorithm.  */
	  op_cost = add_cost[mode] + shift_cost[mode][m];
	  if (shiftadd_cost[mode][m] < op_cost)
	    {
	      op_cost = shiftadd_cost[mode][m];
	      op_latency = op_cost;
	    }
	  else
	    op_latency = add_cost[mode];

	  new_limit.cost = best_cost.cost - op_cost;
	  new_limit.latency = best_cost.latency - op_latency;
	  synth_mult (alg_in, t / d, &new_limit, mode);

	  alg_in->cost.cost += op_cost;
	  alg_in->cost.latency += op_latency;
	  if (alg_in->cost.latency < op_cost)
	    alg_in->cost.latency = op_cost;
	  if (CHEAPER_MULT_COST (&alg_in->cost, &best_cost))
	    {
	      struct algorithm *x;
	      best_cost = alg_in->cost;
	      x = alg_in, alg_in = best_alg, best_alg = x;
	      best_alg->log[best_alg->ops] = m;
	      best_alg->op[best_alg->ops] = alg_add_factor;
	    }
	  /* Other factors will have been taken care of in the recursion.  */
	  break;
	}

      d = ((unsigned HOST_WIDE_INT) 1 << m) - 1;
      if (t % d == 0 && t > d && m < maxm
	  && (!cache_hit || cache_alg == alg_sub_factor))
	{
	  /* If the target has a cheap shift-and-subtract insn use
	     that in preference to a shift insn followed by a sub insn.
	     Assume that the shift-and-sub is "atomic" with a latency
	     equal to it's cost, otherwise assume that on superscalar
	     hardware the shift may be executed concurrently with the
	     earlier steps in the algorithm.  */
	  op_cost = add_cost[mode] + shift_cost[mode][m];
	  if (shiftsub_cost[mode][m] < op_cost)
	    {
	      op_cost = shiftsub_cost[mode][m];
	      op_latency = op_cost;
	    }
	  else
	    op_latency = add_cost[mode];

	  new_limit.cost = best_cost.cost - op_cost;
	  new_limit.latency = best_cost.latency - op_latency;
	  synth_mult (alg_in, t / d, &new_limit, mode);

	  alg_in->cost.cost += op_cost;
	  alg_in->cost.latency += op_latency;
	  if (alg_in->cost.latency < op_cost)
	    alg_in->cost.latency = op_cost;
	  if (CHEAPER_MULT_COST (&alg_in->cost, &best_cost))
	    {
	      struct algorithm *x;
	      best_cost = alg_in->cost;
	      x = alg_in, alg_in = best_alg, best_alg = x;
	      best_alg->log[best_alg->ops] = m;
	      best_alg->op[best_alg->ops] = alg_sub_factor;
	    }
	  break;
	}
    }
  if (cache_hit)
    goto done;

  /* Try shift-and-add (load effective address) instructions,
     i.e. do a*3, a*5, a*9.  */
  if ((t & 1) != 0)
    {
    do_alg_add_t2_m:
      q = t - 1;
      q = q & -q;
      m = exact_log2 (q);
      if (m >= 0 && m < maxm)
	{
	  op_cost = shiftadd_cost[mode][m];
	  new_limit.cost = best_cost.cost - op_cost;
	  new_limit.latency = best_cost.latency - op_cost;
	  synth_mult (alg_in, (t - 1) >> m, &new_limit, mode);

	  alg_in->cost.cost += op_cost;
	  alg_in->cost.latency += op_cost;
	  if (CHEAPER_MULT_COST (&alg_in->cost, &best_cost))
	    {
	      struct algorithm *x;
	      best_cost = alg_in->cost;
	      x = alg_in, alg_in = best_alg, best_alg = x;
	      best_alg->log[best_alg->ops] = m;
	      best_alg->op[best_alg->ops] = alg_add_t2_m;
	    }
	}
      if (cache_hit)
	goto done;

    do_alg_sub_t2_m:
      q = t + 1;
      q = q & -q;
      m = exact_log2 (q);
      if (m >= 0 && m < maxm)
	{
	  op_cost = shiftsub_cost[mode][m];
	  new_limit.cost = best_cost.cost - op_cost;
	  new_limit.latency = best_cost.latency - op_cost;
	  synth_mult (alg_in, (t + 1) >> m, &new_limit, mode);

	  alg_in->cost.cost += op_cost;
	  alg_in->cost.latency += op_cost;
	  if (CHEAPER_MULT_COST (&alg_in->cost, &best_cost))
	    {
	      struct algorithm *x;
	      best_cost = alg_in->cost;
	      x = alg_in, alg_in = best_alg, best_alg = x;
	      best_alg->log[best_alg->ops] = m;
	      best_alg->op[best_alg->ops] = alg_sub_t2_m;
	    }
	}
      if (cache_hit)
	goto done;
    }

 done:
  /* If best_cost has not decreased, we have not found any algorithm.  */
  if (!CHEAPER_MULT_COST (&best_cost, cost_limit))
    {
      /* We failed to find an algorithm.  Record alg_impossible for
	 this case (that is, <T, MODE, COST_LIMIT>) so that next time
	 we are asked to find an algorithm for T within the same or
	 lower COST_LIMIT, we can immediately return to the
	 caller.  */
      alg_hash[hash_index].t = t;
      alg_hash[hash_index].mode = mode;
      alg_hash[hash_index].alg = alg_impossible;
      alg_hash[hash_index].cost = *cost_limit;
      return;
    }

  /* Cache the result.  */
  if (!cache_hit)
    {
      alg_hash[hash_index].t = t;
      alg_hash[hash_index].mode = mode;
      alg_hash[hash_index].alg = best_alg->op[best_alg->ops];
      alg_hash[hash_index].cost.cost = best_cost.cost;
      alg_hash[hash_index].cost.latency = best_cost.latency;
    }

  /* If we are getting a too long sequence for `struct algorithm'
     to record, make this search fail.  */
  if (best_alg->ops == MAX_BITS_PER_WORD)
    return;

  /* Copy the algorithm from temporary space to the space at alg_out.
     We avoid using structure assignment because the majority of
     best_alg is normally undefined, and this is a critical function.  */
  alg_out->ops = best_alg->ops + 1;
  alg_out->cost = best_cost;
  memcpy (alg_out->op, best_alg->op,
	  alg_out->ops * sizeof *alg_out->op);
  memcpy (alg_out->log, best_alg->log,
	  alg_out->ops * sizeof *alg_out->log);
}

/* Find the cheapest way of multiplying a value of mode MODE by VAL.
   Try three variations:

       - a shift/add sequence based on VAL itself
       - a shift/add sequence based on -VAL, followed by a negation
       - a shift/add sequence based on VAL - 1, followed by an addition.

   Return true if the cheapest of these cost less than MULT_COST,
   describing the algorithm in *ALG and final fixup in *VARIANT.  */

static bool
choose_mult_variant (enum machine_mode mode, HOST_WIDE_INT val,
		     struct algorithm *alg, enum mult_variant *variant,
		     int mult_cost)
{
  struct algorithm alg2;
  struct mult_cost limit;
  int op_cost;

  /* Fail quickly for impossible bounds.  */
  if (mult_cost < 0)
    return false;

  /* Ensure that mult_cost provides a reasonable upper bound.
     Any constant multiplication can be performed with less
     than 2 * bits additions.  */
  op_cost = 2 * GET_MODE_BITSIZE (mode) * add_cost[mode];
  if (mult_cost > op_cost)
    mult_cost = op_cost;

  *variant = basic_variant;
  limit.cost = mult_cost;
  limit.latency = mult_cost;
  synth_mult (alg, val, &limit, mode);

  /* This works only if the inverted value actually fits in an
     `unsigned int' */
  if (HOST_BITS_PER_INT >= GET_MODE_BITSIZE (mode))
    {
      op_cost = neg_cost[mode];
      if (MULT_COST_LESS (&alg->cost, mult_cost))
	{
	  limit.cost = alg->cost.cost - op_cost;
	  limit.latency = alg->cost.latency - op_cost;
	}
      else
	{
	  limit.cost = mult_cost - op_cost;
	  limit.latency = mult_cost - op_cost;
	}

      synth_mult (&alg2, -val, &limit, mode);
      alg2.cost.cost += op_cost;
      alg2.cost.latency += op_cost;
      if (CHEAPER_MULT_COST (&alg2.cost, &alg->cost))
	*alg = alg2, *variant = negate_variant;
    }

  /* This proves very useful for division-by-constant.  */
  op_cost = add_cost[mode];
  if (MULT_COST_LESS (&alg->cost, mult_cost))
    {
      limit.cost = alg->cost.cost - op_cost;
      limit.latency = alg->cost.latency - op_cost;
    }
  else
    {
      limit.cost = mult_cost - op_cost;
      limit.latency = mult_cost - op_cost;
    }

  synth_mult (&alg2, val - 1, &limit, mode);
  alg2.cost.cost += op_cost;
  alg2.cost.latency += op_cost;
  if (CHEAPER_MULT_COST (&alg2.cost, &alg->cost))
    *alg = alg2, *variant = add_variant;

  return MULT_COST_LESS (&alg->cost, mult_cost);
}

/* A subroutine of expand_mult, used for constant multiplications.
   Multiply OP0 by VAL in mode MODE, storing the result in TARGET if
   convenient.  Use the shift/add sequence described by ALG and apply
   the final fixup specified by VARIANT.  */

static rtx
expand_mult_const (enum machine_mode mode, rtx op0, HOST_WIDE_INT val,
		   rtx target, const struct algorithm *alg,
		   enum mult_variant variant)
{
  HOST_WIDE_INT val_so_far;
  rtx insn, accum, tem;
  int opno;
  enum machine_mode nmode;

  /* Avoid referencing memory over and over.
     For speed, but also for correctness when mem is volatile.  */
  if (MEM_P (op0))
    op0 = force_reg (mode, op0);

  /* ACCUM starts out either as OP0 or as a zero, depending on
     the first operation.  */

  if (alg->op[0] == alg_zero)
    {
      accum = copy_to_mode_reg (mode, const0_rtx);
      val_so_far = 0;
    }
  else if (alg->op[0] == alg_m)
    {
      accum = copy_to_mode_reg (mode, op0);
      val_so_far = 1;
    }
  else
    gcc_unreachable ();

  for (opno = 1; opno < alg->ops; opno++)
    {
      int log = alg->log[opno];
      rtx shift_subtarget = optimize ? 0 : accum;
      rtx add_target
	= (opno == alg->ops - 1 && target != 0 && variant != add_variant
	   && !optimize)
	  ? target : 0;
      rtx accum_target = optimize ? 0 : accum;

      switch (alg->op[opno])
	{
	case alg_shift:
	  accum = expand_shift (LSHIFT_EXPR, mode, accum,
				build_int_cst (NULL_TREE, log),
				NULL_RTX, 0);
	  val_so_far <<= log;
	  break;

	case alg_add_t_m2:
	  tem = expand_shift (LSHIFT_EXPR, mode, op0,
			      build_int_cst (NULL_TREE, log),
			      NULL_RTX, 0);
	  accum = force_operand (gen_rtx_PLUS (mode, accum, tem),
				 add_target ? add_target : accum_target);
	  val_so_far += (HOST_WIDE_INT) 1 << log;
	  break;

	case alg_sub_t_m2:
	  tem = expand_shift (LSHIFT_EXPR, mode, op0,
			      build_int_cst (NULL_TREE, log),
			      NULL_RTX, 0);
	  accum = force_operand (gen_rtx_MINUS (mode, accum, tem),
				 add_target ? add_target : accum_target);
	  val_so_far -= (HOST_WIDE_INT) 1 << log;
	  break;

	case alg_add_t2_m:
	  accum = expand_shift (LSHIFT_EXPR, mode, accum,
				build_int_cst (NULL_TREE, log),
				shift_subtarget,
				0);
	  accum = force_operand (gen_rtx_PLUS (mode, accum, op0),
				 add_target ? add_target : accum_target);
	  val_so_far = (val_so_far << log) + 1;
	  break;

	case alg_sub_t2_m:
	  accum = expand_shift (LSHIFT_EXPR, mode, accum,
				build_int_cst (NULL_TREE, log),
				shift_subtarget, 0);
	  accum = force_operand (gen_rtx_MINUS (mode, accum, op0),
				 add_target ? add_target : accum_target);
	  val_so_far = (val_so_far << log) - 1;
	  break;

	case alg_add_factor:
	  tem = expand_shift (LSHIFT_EXPR, mode, accum,
			      build_int_cst (NULL_TREE, log),
			      NULL_RTX, 0);
	  accum = force_operand (gen_rtx_PLUS (mode, accum, tem),
				 add_target ? add_target : accum_target);
	  val_so_far += val_so_far << log;
	  break;

	case alg_sub_factor:
	  tem = expand_shift (LSHIFT_EXPR, mode, accum,
			      build_int_cst (NULL_TREE, log),
			      NULL_RTX, 0);
	  accum = force_operand (gen_rtx_MINUS (mode, tem, accum),
				 (add_target
				  ? add_target : (optimize ? 0 : tem)));
	  val_so_far = (val_so_far << log) - val_so_far;
	  break;

	default:
	  gcc_unreachable ();
	}

      /* Write a REG_EQUAL note on the last insn so that we can cse
	 multiplication sequences.  Note that if ACCUM is a SUBREG,
	 we've set the inner register and must properly indicate
	 that.  */

      tem = op0, nmode = mode;
      if (GET_CODE (accum) == SUBREG)
	{
	  nmode = GET_MODE (SUBREG_REG (accum));
	  tem = gen_lowpart (nmode, op0);
	}

      insn = get_last_insn ();
      set_unique_reg_note (insn, REG_EQUAL,
			   gen_rtx_MULT (nmode, tem, GEN_INT (val_so_far)));
    }

  if (variant == negate_variant)
    {
      val_so_far = -val_so_far;
      accum = expand_unop (mode, neg_optab, accum, target, 0);
    }
  else if (variant == add_variant)
    {
      val_so_far = val_so_far + 1;
      accum = force_operand (gen_rtx_PLUS (mode, accum, op0), target);
    }

  /* Compare only the bits of val and val_so_far that are significant
     in the result mode, to avoid sign-/zero-extension confusion.  */
  val &= GET_MODE_MASK (mode);
  val_so_far &= GET_MODE_MASK (mode);
  gcc_assert (val == val_so_far);

  return accum;
}

/* Perform a multiplication and return an rtx for the result.
   MODE is mode of value; OP0 and OP1 are what to multiply (rtx's);
   TARGET is a suggestion for where to store the result (an rtx).

   We check specially for a constant integer as OP1.
   If you want this check for OP0 as well, then before calling
   you should swap the two operands if OP0 would be constant.  */

rtx
expand_mult (enum machine_mode mode, rtx op0, rtx op1, rtx target,
	     int unsignedp)
{
  enum mult_variant variant;
  struct algorithm algorithm;
  int max_cost;

  /* Handling const0_rtx here allows us to use zero as a rogue value for
     coeff below.  */
  if (op1 == const0_rtx)
    return const0_rtx;
  if (op1 == const1_rtx)
    return op0;
  if (op1 == constm1_rtx)
    return expand_unop (mode,
			GET_MODE_CLASS (mode) == MODE_INT
			&& !unsignedp && flag_trapv
			? negv_optab : neg_optab,
			op0, target, 0);

  /* These are the operations that are potentially turned into a sequence
     of shifts and additions.  */
  if (SCALAR_INT_MODE_P (mode)
      && (unsignedp || !flag_trapv))
    {
      HOST_WIDE_INT coeff = 0;
      rtx fake_reg = gen_raw_REG (mode, LAST_VIRTUAL_REGISTER + 1);

      /* synth_mult does an `unsigned int' multiply.  As long as the mode is
	 less than or equal in size to `unsigned int' this doesn't matter.
	 If the mode is larger than `unsigned int', then synth_mult works
	 only if the constant value exactly fits in an `unsigned int' without
	 any truncation.  This means that multiplying by negative values does
	 not work; results are off by 2^32 on a 32 bit machine.  */

      if (GET_CODE (op1) == CONST_INT)
	{
	  /* Attempt to handle multiplication of DImode values by negative
	     coefficients, by performing the multiplication by a positive
	     multiplier and then inverting the result.  */
	  if (INTVAL (op1) < 0
	      && GET_MODE_BITSIZE (mode) > HOST_BITS_PER_WIDE_INT)
	    {
	      /* Its safe to use -INTVAL (op1) even for INT_MIN, as the
		 result is interpreted as an unsigned coefficient.
		 Exclude cost of op0 from max_cost to match the cost
		 calculation of the synth_mult.  */
	      max_cost = rtx_cost (gen_rtx_MULT (mode, fake_reg, op1), SET)
			 - neg_cost[mode];
	      if (max_cost > 0
		  && choose_mult_variant (mode, -INTVAL (op1), &algorithm,
					  &variant, max_cost))
		{
		  rtx temp = expand_mult_const (mode, op0, -INTVAL (op1),
						NULL_RTX, &algorithm,
						variant);
		  return expand_unop (mode, neg_optab, temp, target, 0);
		}
	    }
	  else coeff = INTVAL (op1);
	}
      else if (GET_CODE (op1) == CONST_DOUBLE)
	{
	  /* If we are multiplying in DImode, it may still be a win
	     to try to work with shifts and adds.  */
	  if (CONST_DOUBLE_HIGH (op1) == 0)
	    coeff = CONST_DOUBLE_LOW (op1);
	  else if (CONST_DOUBLE_LOW (op1) == 0
		   && EXACT_POWER_OF_2_OR_ZERO_P (CONST_DOUBLE_HIGH (op1)))
	    {
	      int shift = floor_log2 (CONST_DOUBLE_HIGH (op1))
			  + HOST_BITS_PER_WIDE_INT;
	      return expand_shift (LSHIFT_EXPR, mode, op0,
				   build_int_cst (NULL_TREE, shift),
				   target, unsignedp);
	    }
	}
        
      /* We used to test optimize here, on the grounds that it's better to
	 produce a smaller program when -O is not used.  But this causes
	 such a terrible slowdown sometimes that it seems better to always
	 use synth_mult.  */
      if (coeff != 0)
	{
	  /* Special case powers of two.  */
	  if (EXACT_POWER_OF_2_OR_ZERO_P (coeff))
	    return expand_shift (LSHIFT_EXPR, mode, op0,
				 build_int_cst (NULL_TREE, floor_log2 (coeff)),
				 target, unsignedp);

	  /* Exclude cost of op0 from max_cost to match the cost
	     calculation of the synth_mult.  */
	  max_cost = rtx_cost (gen_rtx_MULT (mode, fake_reg, op1), SET);
	  if (choose_mult_variant (mode, coeff, &algorithm, &variant,
				   max_cost))
	    return expand_mult_const (mode, op0, coeff, target,
				      &algorithm, variant);
	}
    }

  if (GET_CODE (op0) == CONST_DOUBLE)
    {
      rtx temp = op0;
      op0 = op1;
      op1 = temp;
    }

  /* Expand x*2.0 as x+x.  */
  if (GET_CODE (op1) == CONST_DOUBLE
      && SCALAR_FLOAT_MODE_P (mode))
    {
      REAL_VALUE_TYPE d;
      REAL_VALUE_FROM_CONST_DOUBLE (d, op1);

      if (REAL_VALUES_EQUAL (d, dconst2))
	{
	  op0 = force_reg (GET_MODE (op0), op0);
	  return expand_binop (mode, add_optab, op0, op0,
			       target, unsignedp, OPTAB_LIB_WIDEN);
	}
    }

  /* This used to use umul_optab if unsigned, but for non-widening multiply
     there is no difference between signed and unsigned.  */
  op0 = expand_binop (mode,
		      ! unsignedp
		      && flag_trapv && (GET_MODE_CLASS(mode) == MODE_INT)
		      ? smulv_optab : smul_optab,
		      op0, op1, target, unsignedp, OPTAB_LIB_WIDEN);
  gcc_assert (op0);
  return op0;
}

/* Return the smallest n such that 2**n >= X.  */

int
ceil_log2 (unsigned HOST_WIDE_INT x)
{
  return floor_log2 (x - 1) + 1;
}

/* Choose a minimal N + 1 bit approximation to 1/D that can be used to
   replace division by D, and put the least significant N bits of the result
   in *MULTIPLIER_PTR and return the most significant bit.

   The width of operations is N (should be <= HOST_BITS_PER_WIDE_INT), the
   needed precision is in PRECISION (should be <= N).

   PRECISION should be as small as possible so this function can choose
   multiplier more freely.

   The rounded-up logarithm of D is placed in *lgup_ptr.  A shift count that
   is to be used for a final right shift is placed in *POST_SHIFT_PTR.

   Using this function, x/D will be equal to (x * m) >> (*POST_SHIFT_PTR),
   where m is the full HOST_BITS_PER_WIDE_INT + 1 bit multiplier.  */

static
unsigned HOST_WIDE_INT
choose_multiplier (unsigned HOST_WIDE_INT d, int n, int precision,
		   rtx *multiplier_ptr, int *post_shift_ptr, int *lgup_ptr)
{
  HOST_WIDE_INT mhigh_hi, mlow_hi;
  unsigned HOST_WIDE_INT mhigh_lo, mlow_lo;
  int lgup, post_shift;
  int pow, pow2;
  unsigned HOST_WIDE_INT nl, dummy1;
  HOST_WIDE_INT nh, dummy2;

  /* lgup = ceil(log2(divisor)); */
  lgup = ceil_log2 (d);

  gcc_assert (lgup <= n);

  pow = n + lgup;
  pow2 = n + lgup - precision;

  /* We could handle this with some effort, but this case is much
     better handled directly with a scc insn, so rely on caller using
     that.  */
  gcc_assert (pow != 2 * HOST_BITS_PER_WIDE_INT);

  /* mlow = 2^(N + lgup)/d */
 if (pow >= HOST_BITS_PER_WIDE_INT)
    {
      nh = (HOST_WIDE_INT) 1 << (pow - HOST_BITS_PER_WIDE_INT);
      nl = 0;
    }
  else
    {
      nh = 0;
      nl = (unsigned HOST_WIDE_INT) 1 << pow;
    }
  div_and_round_double (TRUNC_DIV_EXPR, 1, nl, nh, d, (HOST_WIDE_INT) 0,
			&mlow_lo, &mlow_hi, &dummy1, &dummy2);

  /* mhigh = (2^(N + lgup) + 2^N + lgup - precision)/d */
  if (pow2 >= HOST_BITS_PER_WIDE_INT)
    nh |= (HOST_WIDE_INT) 1 << (pow2 - HOST_BITS_PER_WIDE_INT);
  else
    nl |= (unsigned HOST_WIDE_INT) 1 << pow2;
  div_and_round_double (TRUNC_DIV_EXPR, 1, nl, nh, d, (HOST_WIDE_INT) 0,
			&mhigh_lo, &mhigh_hi, &dummy1, &dummy2);

  gcc_assert (!mhigh_hi || nh - d < d);
  gcc_assert (mhigh_hi <= 1 && mlow_hi <= 1);
  /* Assert that mlow < mhigh.  */
  gcc_assert (mlow_hi < mhigh_hi
	      || (mlow_hi == mhigh_hi && mlow_lo < mhigh_lo));

  /* If precision == N, then mlow, mhigh exceed 2^N
     (but they do not exceed 2^(N+1)).  */

  /* Reduce to lowest terms.  */
  for (post_shift = lgup; post_shift > 0; post_shift--)
    {
      unsigned HOST_WIDE_INT ml_lo = (mlow_hi << (HOST_BITS_PER_WIDE_INT - 1)) | (mlow_lo >> 1);
      unsigned HOST_WIDE_INT mh_lo = (mhigh_hi << (HOST_BITS_PER_WIDE_INT - 1)) | (mhigh_lo >> 1);
      if (ml_lo >= mh_lo)
	break;

      mlow_hi = 0;
      mlow_lo = ml_lo;
      mhigh_hi = 0;
      mhigh_lo = mh_lo;
    }

  *post_shift_ptr = post_shift;
  *lgup_ptr = lgup;
  if (n < HOST_BITS_PER_WIDE_INT)
    {
      unsigned HOST_WIDE_INT mask = ((unsigned HOST_WIDE_INT) 1 << n) - 1;
      *multiplier_ptr = GEN_INT (mhigh_lo & mask);
      return mhigh_lo >= mask;
    }
  else
    {
      *multiplier_ptr = GEN_INT (mhigh_lo);
      return mhigh_hi;
    }
}

/* Compute the inverse of X mod 2**n, i.e., find Y such that X * Y is
   congruent to 1 (mod 2**N).  */

static unsigned HOST_WIDE_INT
invert_mod2n (unsigned HOST_WIDE_INT x, int n)
{
  /* Solve x*y == 1 (mod 2^n), where x is odd.  Return y.  */

  /* The algorithm notes that the choice y = x satisfies
     x*y == 1 mod 2^3, since x is assumed odd.
     Each iteration doubles the number of bits of significance in y.  */

  unsigned HOST_WIDE_INT mask;
  unsigned HOST_WIDE_INT y = x;
  int nbit = 3;

  mask = (n == HOST_BITS_PER_WIDE_INT
	  ? ~(unsigned HOST_WIDE_INT) 0
	  : ((unsigned HOST_WIDE_INT) 1 << n) - 1);

  while (nbit < n)
    {
      y = y * (2 - x*y) & mask;		/* Modulo 2^N */
      nbit *= 2;
    }
  return y;
}

/* Emit code to adjust ADJ_OPERAND after multiplication of wrong signedness
   flavor of OP0 and OP1.  ADJ_OPERAND is already the high half of the
   product OP0 x OP1.  If UNSIGNEDP is nonzero, adjust the signed product
   to become unsigned, if UNSIGNEDP is zero, adjust the unsigned product to
   become signed.

   The result is put in TARGET if that is convenient.

   MODE is the mode of operation.  */

rtx
expand_mult_highpart_adjust (enum machine_mode mode, rtx adj_operand, rtx op0,
			     rtx op1, rtx target, int unsignedp)
{
  rtx tem;
  enum rtx_code adj_code = unsignedp ? PLUS : MINUS;

  tem = expand_shift (RSHIFT_EXPR, mode, op0,
		      build_int_cst (NULL_TREE, GET_MODE_BITSIZE (mode) - 1),
		      NULL_RTX, 0);
  tem = expand_and (mode, tem, op1, NULL_RTX);
  adj_operand
    = force_operand (gen_rtx_fmt_ee (adj_code, mode, adj_operand, tem),
		     adj_operand);

  tem = expand_shift (RSHIFT_EXPR, mode, op1,
		      build_int_cst (NULL_TREE, GET_MODE_BITSIZE (mode) - 1),
		      NULL_RTX, 0);
  tem = expand_and (mode, tem, op0, NULL_RTX);
  target = force_operand (gen_rtx_fmt_ee (adj_code, mode, adj_operand, tem),
			  target);

  return target;
}

/* Subroutine of expand_mult_highpart.  Return the MODE high part of OP.  */

static rtx
extract_high_half (enum machine_mode mode, rtx op)
{
  enum machine_mode wider_mode;

  if (mode == word_mode)
    return gen_highpart (mode, op);

  gcc_assert (!SCALAR_FLOAT_MODE_P (mode));

  wider_mode = GET_MODE_WIDER_MODE (mode);
  op = expand_shift (RSHIFT_EXPR, wider_mode, op,
		     build_int_cst (NULL_TREE, GET_MODE_BITSIZE (mode)), 0, 1);
  return convert_modes (mode, wider_mode, op, 0);
}

/* Like expand_mult_highpart, but only consider using a multiplication
   optab.  OP1 is an rtx for the constant operand.  */

static rtx
expand_mult_highpart_optab (enum machine_mode mode, rtx op0, rtx op1,
			    rtx target, int unsignedp, int max_cost)
{
  rtx narrow_op1 = gen_int_mode (INTVAL (op1), mode);
  enum machine_mode wider_mode;
  optab moptab;
  rtx tem;
  int size;

  gcc_assert (!SCALAR_FLOAT_MODE_P (mode));

  wider_mode = GET_MODE_WIDER_MODE (mode);
  size = GET_MODE_BITSIZE (mode);

  /* Firstly, try using a multiplication insn that only generates the needed
     high part of the product, and in the sign flavor of unsignedp.  */
  if (mul_highpart_cost[mode] < max_cost)
    {
      moptab = unsignedp ? umul_highpart_optab : smul_highpart_optab;
      tem = expand_binop (mode, moptab, op0, narrow_op1, target,
			  unsignedp, OPTAB_DIRECT);
      if (tem)
	return tem;
    }

  /* Secondly, same as above, but use sign flavor opposite of unsignedp.
     Need to adjust the result after the multiplication.  */
  if (size - 1 < BITS_PER_WORD
      && (mul_highpart_cost[mode] + 2 * shift_cost[mode][size-1]
	  + 4 * add_cost[mode] < max_cost))
    {
      moptab = unsignedp ? smul_highpart_optab : umul_highpart_optab;
      tem = expand_binop (mode, moptab, op0, narrow_op1, target,
			  unsignedp, OPTAB_DIRECT);
      if (tem)
	/* We used the wrong signedness.  Adjust the result.  */
	return expand_mult_highpart_adjust (mode, tem, op0, narrow_op1,
					    tem, unsignedp);
    }

  /* Try widening multiplication.  */
  moptab = unsignedp ? umul_widen_optab : smul_widen_optab;
  if (moptab->handlers[wider_mode].insn_code != CODE_FOR_nothing
      && mul_widen_cost[wider_mode] < max_cost)
    {
      tem = expand_binop (wider_mode, moptab, op0, narrow_op1, 0,
			  unsignedp, OPTAB_WIDEN);
      if (tem)
	return extract_high_half (mode, tem);
    }

  /* Try widening the mode and perform a non-widening multiplication.  */
  if (smul_optab->handlers[wider_mode].insn_code != CODE_FOR_nothing
      && size - 1 < BITS_PER_WORD
      && mul_cost[wider_mode] + shift_cost[mode][size-1] < max_cost)
    {
      rtx insns, wop0, wop1;

      /* We need to widen the operands, for example to ensure the
	 constant multiplier is correctly sign or zero extended.
	 Use a sequence to clean-up any instructions emitted by
	 the conversions if things don't work out.  */
      start_sequence ();
      wop0 = convert_modes (wider_mode, mode, op0, unsignedp);
      wop1 = convert_modes (wider_mode, mode, op1, unsignedp);
      tem = expand_binop (wider_mode, smul_optab, wop0, wop1, 0,
			  unsignedp, OPTAB_WIDEN);
      insns = get_insns ();
      end_sequence ();

      if (tem)
	{
	  emit_insn (insns);
	  return extract_high_half (mode, tem);
	}
    }

  /* Try widening multiplication of opposite signedness, and adjust.  */
  moptab = unsignedp ? smul_widen_optab : umul_widen_optab;
  if (moptab->handlers[wider_mode].insn_code != CODE_FOR_nothing
      && size - 1 < BITS_PER_WORD
      && (mul_widen_cost[wider_mode] + 2 * shift_cost[mode][size-1]
	  + 4 * add_cost[mode] < max_cost))
    {
      tem = expand_binop (wider_mode, moptab, op0, narrow_op1,
			  NULL_RTX, ! unsignedp, OPTAB_WIDEN);
      if (tem != 0)
	{
	  tem = extract_high_half (mode, tem);
	  /* We used the wrong signedness.  Adjust the result.  */
	  return expand_mult_highpart_adjust (mode, tem, op0, narrow_op1,
					      target, unsignedp);
	}
    }

  return 0;
}

/* Emit code to multiply OP0 and OP1 (where OP1 is an integer constant),
   putting the high half of the result in TARGET if that is convenient,
   and return where the result is.  If the operation can not be performed,
   0 is returned.

   MODE is the mode of operation and result.

   UNSIGNEDP nonzero means unsigned multiply.

   MAX_COST is the total allowed cost for the expanded RTL.  */

static rtx
expand_mult_highpart (enum machine_mode mode, rtx op0, rtx op1,
		      rtx target, int unsignedp, int max_cost)
{
  enum machine_mode wider_mode = GET_MODE_WIDER_MODE (mode);
  unsigned HOST_WIDE_INT cnst1;
  int extra_cost;
  bool sign_adjust = false;
  enum mult_variant variant;
  struct algorithm alg;
  rtx tem;

  gcc_assert (!SCALAR_FLOAT_MODE_P (mode));
  /* We can't support modes wider than HOST_BITS_PER_INT.  */
  gcc_assert (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT);

  cnst1 = INTVAL (op1) & GET_MODE_MASK (mode);

  /* We can't optimize modes wider than BITS_PER_WORD. 
     ??? We might be able to perform double-word arithmetic if 
     mode == word_mode, however all the cost calculations in
     synth_mult etc. assume single-word operations.  */
  if (GET_MODE_BITSIZE (wider_mode) > BITS_PER_WORD)
    return expand_mult_highpart_optab (mode, op0, op1, target,
				       unsignedp, max_cost);

  extra_cost = shift_cost[mode][GET_MODE_BITSIZE (mode) - 1];

  /* Check whether we try to multiply by a negative constant.  */
  if (!unsignedp && ((cnst1 >> (GET_MODE_BITSIZE (mode) - 1)) & 1))
    {
      sign_adjust = true;
      extra_cost += add_cost[mode];
    }

  /* See whether shift/add multiplication is cheap enough.  */
  if (choose_mult_variant (wider_mode, cnst1, &alg, &variant,
			   max_cost - extra_cost))
    {
      /* See whether the specialized multiplication optabs are
	 cheaper than the shift/add version.  */
      tem = expand_mult_highpart_optab (mode, op0, op1, target, unsignedp,
					alg.cost.cost + extra_cost);
      if (tem)
	return tem;

      tem = convert_to_mode (wider_mode, op0, unsignedp);
      tem = expand_mult_const (wider_mode, tem, cnst1, 0, &alg, variant);
      tem = extract_high_half (mode, tem);

      /* Adjust result for signedness.  */
      if (sign_adjust)
	tem = force_operand (gen_rtx_MINUS (mode, tem, op0), tem);

      return tem;
    }
  return expand_mult_highpart_optab (mode, op0, op1, target,
				     unsignedp, max_cost);
}


/* Expand signed modulus of OP0 by a power of two D in mode MODE.  */

static rtx
expand_smod_pow2 (enum machine_mode mode, rtx op0, HOST_WIDE_INT d)
{
  unsigned HOST_WIDE_INT masklow, maskhigh;
  rtx result, temp, shift, label;
  int logd;

  logd = floor_log2 (d);
  result = gen_reg_rtx (mode);

  /* Avoid conditional branches when they're expensive.  */
  if (BRANCH_COST >= 2
      && !optimize_size)
    {
      rtx signmask = emit_store_flag (result, LT, op0, const0_rtx,
				      mode, 0, -1);
      if (signmask)
	{
	  signmask = force_reg (mode, signmask);
	  masklow = ((HOST_WIDE_INT) 1 << logd) - 1;
	  shift = GEN_INT (GET_MODE_BITSIZE (mode) - logd);

	  /* Use the rtx_cost of a LSHIFTRT instruction to determine
	     which instruction sequence to use.  If logical right shifts
	     are expensive the use 2 XORs, 2 SUBs and an AND, otherwise
	     use a LSHIFTRT, 1 ADD, 1 SUB and an AND.  */

	  temp = gen_rtx_LSHIFTRT (mode, result, shift);
	  if (lshr_optab->handlers[mode].insn_code == CODE_FOR_nothing
	      || rtx_cost (temp, SET) > COSTS_N_INSNS (2))
	    {
	      temp = expand_binop (mode, xor_optab, op0, signmask,
				   NULL_RTX, 1, OPTAB_LIB_WIDEN);
	      temp = expand_binop (mode, sub_optab, temp, signmask,
				   NULL_RTX, 1, OPTAB_LIB_WIDEN);
	      temp = expand_binop (mode, and_optab, temp, GEN_INT (masklow),
				   NULL_RTX, 1, OPTAB_LIB_WIDEN);
	      temp = expand_binop (mode, xor_optab, temp, signmask,
				   NULL_RTX, 1, OPTAB_LIB_WIDEN);
	      temp = expand_binop (mode, sub_optab, temp, signmask,
				   NULL_RTX, 1, OPTAB_LIB_WIDEN);
	    }
	  else
	    {
	      signmask = expand_binop (mode, lshr_optab, signmask, shift,
				       NULL_RTX, 1, OPTAB_LIB_WIDEN);
	      signmask = force_reg (mode, signmask);

	      temp = expand_binop (mode, add_optab, op0, signmask,
				   NULL_RTX, 1, OPTAB_LIB_WIDEN);
	      temp = expand_binop (mode, and_optab, temp, GEN_INT (masklow),
				   NULL_RTX, 1, OPTAB_LIB_WIDEN);
	      temp = expand_binop (mode, sub_optab, temp, signmask,
				   NULL_RTX, 1, OPTAB_LIB_WIDEN);
	    }
	  return temp;
	}
    }

  /* Mask contains the mode's signbit and the significant bits of the
     modulus.  By including the signbit in the operation, many targets
     can avoid an explicit compare operation in the following comparison
     against zero.  */

  masklow = ((HOST_WIDE_INT) 1 << logd) - 1;
  if (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT)
    {
      masklow |= (HOST_WIDE_INT) -1 << (GET_MODE_BITSIZE (mode) - 1);
      maskhigh = -1;
    }
  else
    maskhigh = (HOST_WIDE_INT) -1
		 << (GET_MODE_BITSIZE (mode) - HOST_BITS_PER_WIDE_INT - 1);

  temp = expand_binop (mode, and_optab, op0,
		       immed_double_const (masklow, maskhigh, mode),
		       result, 1, OPTAB_LIB_WIDEN);
  if (temp != result)
    emit_move_insn (result, temp);

  label = gen_label_rtx ();
  do_cmp_and_jump (result, const0_rtx, GE, mode, label);

  temp = expand_binop (mode, sub_optab, result, const1_rtx, result,
		       0, OPTAB_LIB_WIDEN);
  masklow = (HOST_WIDE_INT) -1 << logd;
  maskhigh = -1;
  temp = expand_binop (mode, ior_optab, temp,
		       immed_double_const (masklow, maskhigh, mode),
		       result, 1, OPTAB_LIB_WIDEN);
  temp = expand_binop (mode, add_optab, temp, const1_rtx, result,
		       0, OPTAB_LIB_WIDEN);
  if (temp != result)
    emit_move_insn (result, temp);
  emit_label (label);
  return result;
}

/* Expand signed division of OP0 by a power of two D in mode MODE.
   This routine is only called for positive values of D.  */

static rtx
expand_sdiv_pow2 (enum machine_mode mode, rtx op0, HOST_WIDE_INT d)
{
  rtx temp, label;
  tree shift;
  int logd;

  logd = floor_log2 (d);
  shift = build_int_cst (NULL_TREE, logd);

  if (d == 2 && BRANCH_COST >= 1)
    {
      temp = gen_reg_rtx (mode);
      temp = emit_store_flag (temp, LT, op0, const0_rtx, mode, 0, 1);
      temp = expand_binop (mode, add_optab, temp, op0, NULL_RTX,
			   0, OPTAB_LIB_WIDEN);
      return expand_shift (RSHIFT_EXPR, mode, temp, shift, NULL_RTX, 0);
    }

#ifdef HAVE_conditional_move
  if (BRANCH_COST >= 2)
    {
      rtx temp2;

      /* ??? emit_conditional_move forces a stack adjustment via
	 compare_from_rtx so, if the sequence is discarded, it will
	 be lost.  Do it now instead.  */
      do_pending_stack_adjust ();

      start_sequence ();
      temp2 = copy_to_mode_reg (mode, op0);
      temp = expand_binop (mode, add_optab, temp2, GEN_INT (d-1),
			   NULL_RTX, 0, OPTAB_LIB_WIDEN);
      temp = force_reg (mode, temp);

      /* Construct "temp2 = (temp2 < 0) ? temp : temp2".  */
      temp2 = emit_conditional_move (temp2, LT, temp2, const0_rtx,
				     mode, temp, temp2, mode, 0);
      if (temp2)
	{
	  rtx seq = get_insns ();
	  end_sequence ();
	  emit_insn (seq);
	  return expand_shift (RSHIFT_EXPR, mode, temp2, shift, NULL_RTX, 0);
	}
      end_sequence ();
    }
#endif

  if (BRANCH_COST >= 2)
    {
      int ushift = GET_MODE_BITSIZE (mode) - logd;

      temp = gen_reg_rtx (mode);
      temp = emit_store_flag (temp, LT, op0, const0_rtx, mode, 0, -1);
      if (shift_cost[mode][ushift] > COSTS_N_INSNS (1))
	temp = expand_binop (mode, and_optab, temp, GEN_INT (d - 1),
			     NULL_RTX, 0, OPTAB_LIB_WIDEN);
      else
	temp = expand_shift (RSHIFT_EXPR, mode, temp,
			     build_int_cst (NULL_TREE, ushift),
			     NULL_RTX, 1);
      temp = expand_binop (mode, add_optab, temp, op0, NULL_RTX,
			   0, OPTAB_LIB_WIDEN);
      return expand_shift (RSHIFT_EXPR, mode, temp, shift, NULL_RTX, 0);
    }

  label = gen_label_rtx ();
  temp = copy_to_mode_reg (mode, op0);
  do_cmp_and_jump (temp, const0_rtx, GE, mode, label);
  expand_inc (temp, GEN_INT (d - 1));
  emit_label (label);
  return expand_shift (RSHIFT_EXPR, mode, temp, shift, NULL_RTX, 0);
}

/* Emit the code to divide OP0 by OP1, putting the result in TARGET
   if that is convenient, and returning where the result is.
   You may request either the quotient or the remainder as the result;
   specify REM_FLAG nonzero to get the remainder.

   CODE is the expression code for which kind of division this is;
   it controls how rounding is done.  MODE is the machine mode to use.
   UNSIGNEDP nonzero means do unsigned division.  */

/* ??? For CEIL_MOD_EXPR, can compute incorrect remainder with ANDI
   and then correct it by or'ing in missing high bits
   if result of ANDI is nonzero.
   For ROUND_MOD_EXPR, can use ANDI and then sign-extend the result.
   This could optimize to a bfexts instruction.
   But C doesn't use these operations, so their optimizations are
   left for later.  */
/* ??? For modulo, we don't actually need the highpart of the first product,
   the low part will do nicely.  And for small divisors, the second multiply
   can also be a low-part only multiply or even be completely left out.
   E.g. to calculate the remainder of a division by 3 with a 32 bit
   multiply, multiply with 0x55555556 and extract the upper two bits;
   the result is exact for inputs up to 0x1fffffff.
   The input range can be reduced by using cross-sum rules.
   For odd divisors >= 3, the following table gives right shift counts
   so that if a number is shifted by an integer multiple of the given
   amount, the remainder stays the same:
   2, 4, 3, 6, 10, 12, 4, 8, 18, 6, 11, 20, 18, 0, 5, 10, 12, 0, 12, 20,
   14, 12, 23, 21, 8, 0, 20, 18, 0, 0, 6, 12, 0, 22, 0, 18, 20, 30, 0, 0,
   0, 8, 0, 11, 12, 10, 36, 0, 30, 0, 0, 12, 0, 0, 0, 0, 44, 12, 24, 0,
   20, 0, 7, 14, 0, 18, 36, 0, 0, 46, 60, 0, 42, 0, 15, 24, 20, 0, 0, 33,
   0, 20, 0, 0, 18, 0, 60, 0, 0, 0, 0, 0, 40, 18, 0, 0, 12

   Cross-sum rules for even numbers can be derived by leaving as many bits
   to the right alone as the divisor has zeros to the right.
   E.g. if x is an unsigned 32 bit number:
   (x mod 12) == (((x & 1023) + ((x >> 8) & ~3)) * 0x15555558 >> 2 * 3) >> 28
   */

rtx
expand_divmod (int rem_flag, enum tree_code code, enum machine_mode mode,
	       rtx op0, rtx op1, rtx target, int unsignedp)
{
  enum machine_mode compute_mode;
  rtx tquotient;
  rtx quotient = 0, remainder = 0;
  rtx last;
  int size;
  rtx insn, set;
  optab optab1, optab2;
  int op1_is_constant, op1_is_pow2 = 0;
  int max_cost, extra_cost;
  static HOST_WIDE_INT last_div_const = 0;
  static HOST_WIDE_INT ext_op1;

  op1_is_constant = GET_CODE (op1) == CONST_INT;
  if (op1_is_constant)
    {
      ext_op1 = INTVAL (op1);
      if (unsignedp)
	ext_op1 &= GET_MODE_MASK (mode);
      op1_is_pow2 = ((EXACT_POWER_OF_2_OR_ZERO_P (ext_op1)
		     || (! unsignedp && EXACT_POWER_OF_2_OR_ZERO_P (-ext_op1))));
    }

  /*
     This is the structure of expand_divmod:

     First comes code to fix up the operands so we can perform the operations
     correctly and efficiently.

     Second comes a switch statement with code specific for each rounding mode.
     For some special operands this code emits all RTL for the desired
     operation, for other cases, it generates only a quotient and stores it in
     QUOTIENT.  The case for trunc division/remainder might leave quotient = 0,
     to indicate that it has not done anything.

     Last comes code that finishes the operation.  If QUOTIENT is set and
     REM_FLAG is set, the remainder is computed as OP0 - QUOTIENT * OP1.  If
     QUOTIENT is not set, it is computed using trunc rounding.

     We try to generate special code for division and remainder when OP1 is a
     constant.  If |OP1| = 2**n we can use shifts and some other fast
     operations.  For other values of OP1, we compute a carefully selected
     fixed-point approximation m = 1/OP1, and generate code that multiplies OP0
     by m.

     In all cases but EXACT_DIV_EXPR, this multiplication requires the upper
     half of the product.  Different strategies for generating the product are
     implemented in expand_mult_highpart.

     If what we actually want is the remainder, we generate that by another
     by-constant multiplication and a subtraction.  */

  /* We shouldn't be called with OP1 == const1_rtx, but some of the
     code below will malfunction if we are, so check here and handle
     the special case if so.  */
  if (op1 == const1_rtx)
    return rem_flag ? const0_rtx : op0;

    /* When dividing by -1, we could get an overflow.
     negv_optab can handle overflows.  */
  if (! unsignedp && op1 == constm1_rtx)
    {
      if (rem_flag)
	return const0_rtx;
      return expand_unop (mode, flag_trapv && GET_MODE_CLASS(mode) == MODE_INT
			  ? negv_optab : neg_optab, op0, target, 0);
    }

  if (target
      /* Don't use the function value register as a target
	 since we have to read it as well as write it,
	 and function-inlining gets confused by this.  */
      && ((REG_P (target) && REG_FUNCTION_VALUE_P (target))
	  /* Don't clobber an operand while doing a multi-step calculation.  */
	  || ((rem_flag || op1_is_constant)
	      && (reg_mentioned_p (target, op0)
		  || (MEM_P (op0) && MEM_P (target))))
	  || reg_mentioned_p (target, op1)
	  || (MEM_P (op1) && MEM_P (target))))
    target = 0;

  /* Get the mode in which to perform this computation.  Normally it will
     be MODE, but sometimes we can't do the desired operation in MODE.
     If so, pick a wider mode in which we can do the operation.  Convert
     to that mode at the start to avoid repeated conversions.

     First see what operations we need.  These depend on the expression
     we are evaluating.  (We assume that divxx3 insns exist under the
     same conditions that modxx3 insns and that these insns don't normally
     fail.  If these assumptions are not correct, we may generate less
     efficient code in some cases.)

     Then see if we find a mode in which we can open-code that operation
     (either a division, modulus, or shift).  Finally, check for the smallest
     mode for which we can do the operation with a library call.  */

  /* We might want to refine this now that we have division-by-constant
     optimization.  Since expand_mult_highpart tries so many variants, it is
     not straightforward to generalize this.  Maybe we should make an array
     of possible modes in init_expmed?  Save this for GCC 2.7.  */

  optab1 = ((op1_is_pow2 && op1 != const0_rtx)
	    ? (unsignedp ? lshr_optab : ashr_optab)
	    : (unsignedp ? udiv_optab : sdiv_optab));
  optab2 = ((op1_is_pow2 && op1 != const0_rtx)
	    ? optab1
	    : (unsignedp ? udivmod_optab : sdivmod_optab));

  for (compute_mode = mode; compute_mode != VOIDmode;
       compute_mode = GET_MODE_WIDER_MODE (compute_mode))
    if (optab1->handlers[compute_mode].insn_code != CODE_FOR_nothing
	|| optab2->handlers[compute_mode].insn_code != CODE_FOR_nothing)
      break;

  if (compute_mode == VOIDmode)
    for (compute_mode = mode; compute_mode != VOIDmode;
	 compute_mode = GET_MODE_WIDER_MODE (compute_mode))
      if (optab1->handlers[compute_mode].libfunc
	  || optab2->handlers[compute_mode].libfunc)
	break;

  /* If we still couldn't find a mode, use MODE, but expand_binop will
     probably die.  */
  if (compute_mode == VOIDmode)
    compute_mode = mode;

  if (target && GET_MODE (target) == compute_mode)
    tquotient = target;
  else
    tquotient = gen_reg_rtx (compute_mode);

  size = GET_MODE_BITSIZE (compute_mode);
#if 0
  /* It should be possible to restrict the precision to GET_MODE_BITSIZE
     (mode), and thereby get better code when OP1 is a constant.  Do that
     later.  It will require going over all usages of SIZE below.  */
  size = GET_MODE_BITSIZE (mode);
#endif

  /* Only deduct something for a REM if the last divide done was
     for a different constant.   Then set the constant of the last
     divide.  */
  max_cost = unsignedp ? udiv_cost[compute_mode] : sdiv_cost[compute_mode];
  if (rem_flag && ! (last_div_const != 0 && op1_is_constant
		     && INTVAL (op1) == last_div_const))
    max_cost -= mul_cost[compute_mode] + add_cost[compute_mode];

  last_div_const = ! rem_flag && op1_is_constant ? INTVAL (op1) : 0;

  /* Now convert to the best mode to use.  */
  if (compute_mode != mode)
    {
      op0 = convert_modes (compute_mode, mode, op0, unsignedp);
      op1 = convert_modes (compute_mode, mode, op1, unsignedp);

      /* convert_modes may have placed op1 into a register, so we
	 must recompute the following.  */
      op1_is_constant = GET_CODE (op1) == CONST_INT;
      op1_is_pow2 = (op1_is_constant
		     && ((EXACT_POWER_OF_2_OR_ZERO_P (INTVAL (op1))
			  || (! unsignedp
			      && EXACT_POWER_OF_2_OR_ZERO_P (-INTVAL (op1)))))) ;
    }

  /* If one of the operands is a volatile MEM, copy it into a register.  */

  if (MEM_P (op0) && MEM_VOLATILE_P (op0))
    op0 = force_reg (compute_mode, op0);
  if (MEM_P (op1) && MEM_VOLATILE_P (op1))
    op1 = force_reg (compute_mode, op1);

  /* If we need the remainder or if OP1 is constant, we need to
     put OP0 in a register in case it has any queued subexpressions.  */
  if (rem_flag || op1_is_constant)
    op0 = force_reg (compute_mode, op0);

  last = get_last_insn ();

  /* Promote floor rounding to trunc rounding for unsigned operations.  */
  if (unsignedp)
    {
      if (code == FLOOR_DIV_EXPR)
	code = TRUNC_DIV_EXPR;
      if (code == FLOOR_MOD_EXPR)
	code = TRUNC_MOD_EXPR;
      if (code == EXACT_DIV_EXPR && op1_is_pow2)
	code = TRUNC_DIV_EXPR;
    }

  if (op1 != const0_rtx)
    switch (code)
      {
      case TRUNC_MOD_EXPR:
      case TRUNC_DIV_EXPR:
	if (op1_is_constant)
	  {
	    if (unsignedp)
	      {
		unsigned HOST_WIDE_INT mh;
		int pre_shift, post_shift;
		int dummy;
		rtx ml;
		unsigned HOST_WIDE_INT d = (INTVAL (op1)
					    & GET_MODE_MASK (compute_mode));

		if (EXACT_POWER_OF_2_OR_ZERO_P (d))
		  {
		    pre_shift = floor_log2 (d);
		    if (rem_flag)
		      {
			remainder
			  = expand_binop (compute_mode, and_optab, op0,
					  GEN_INT (((HOST_WIDE_INT) 1 << pre_shift) - 1),
					  remainder, 1,
					  OPTAB_LIB_WIDEN);
			if (remainder)
			  return gen_lowpart (mode, remainder);
		      }
		    quotient = expand_shift (RSHIFT_EXPR, compute_mode, op0,
					     build_int_cst (NULL_TREE,
							    pre_shift),
					     tquotient, 1);
		  }
		else if (size <= HOST_BITS_PER_WIDE_INT)
		  {
		    if (d >= ((unsigned HOST_WIDE_INT) 1 << (size - 1)))
		      {
			/* Most significant bit of divisor is set; emit an scc
			   insn.  */
			quotient = emit_store_flag (tquotient, GEU, op0, op1,
						    compute_mode, 1, 1);
			if (quotient == 0)
			  goto fail1;
		      }
		    else
		      {
			/* Find a suitable multiplier and right shift count
			   instead of multiplying with D.  */

			mh = choose_multiplier (d, size, size,
						&ml, &post_shift, &dummy);

			/* If the suggested multiplier is more than SIZE bits,
			   we can do better for even divisors, using an
			   initial right shift.  */
			if (mh != 0 && (d & 1) == 0)
			  {
			    pre_shift = floor_log2 (d & -d);
			    mh = choose_multiplier (d >> pre_shift, size,
						    size - pre_shift,
						    &ml, &post_shift, &dummy);
			    gcc_assert (!mh);
			  }
			else
			  pre_shift = 0;

			if (mh != 0)
			  {
			    rtx t1, t2, t3, t4;

			    if (post_shift - 1 >= BITS_PER_WORD)
			      goto fail1;

			    extra_cost
			      = (shift_cost[compute_mode][post_shift - 1]
				 + shift_cost[compute_mode][1]
				 + 2 * add_cost[compute_mode]);
			    t1 = expand_mult_highpart (compute_mode, op0, ml,
						       NULL_RTX, 1,
						       max_cost - extra_cost);
			    if (t1 == 0)
			      goto fail1;
			    t2 = force_operand (gen_rtx_MINUS (compute_mode,
							       op0, t1),
						NULL_RTX);
			    t3 = expand_shift
			      (RSHIFT_EXPR, compute_mode, t2,
			       build_int_cst (NULL_TREE, 1),
			       NULL_RTX,1);
			    t4 = force_operand (gen_rtx_PLUS (compute_mode,
							      t1, t3),
						NULL_RTX);
			    quotient = expand_shift
			      (RSHIFT_EXPR, compute_mode, t4,
			       build_int_cst (NULL_TREE, post_shift - 1),
			       tquotient, 1);
			  }
			else
			  {
			    rtx t1, t2;

			    if (pre_shift >= BITS_PER_WORD
				|| post_shift >= BITS_PER_WORD)
			      goto fail1;

			    t1 = expand_shift
			      (RSHIFT_EXPR, compute_mode, op0,
			       build_int_cst (NULL_TREE, pre_shift),
			       NULL_RTX, 1);
			    extra_cost
			      = (shift_cost[compute_mode][pre_shift]
				 + shift_cost[compute_mode][post_shift]);
			    t2 = expand_mult_highpart (compute_mode, t1, ml,
						       NULL_RTX, 1,
						       max_cost - extra_cost);
			    if (t2 == 0)
			      goto fail1;
			    quotient = expand_shift
			      (RSHIFT_EXPR, compute_mode, t2,
			       build_int_cst (NULL_TREE, post_shift),
			       tquotient, 1);
			  }
		      }
		  }
		else		/* Too wide mode to use tricky code */
		  break;

		insn = get_last_insn ();
		if (insn != last
		    && (set = single_set (insn)) != 0
		    && SET_DEST (set) == quotient)
		  set_unique_reg_note (insn,
				       REG_EQUAL,
				       gen_rtx_UDIV (compute_mode, op0, op1));
	      }
	    else		/* TRUNC_DIV, signed */
	      {
		unsigned HOST_WIDE_INT ml;
		int lgup, post_shift;
		rtx mlr;
		HOST_WIDE_INT d = INTVAL (op1);
		unsigned HOST_WIDE_INT abs_d = d >= 0 ? d : -d;

		/* n rem d = n rem -d */
		if (rem_flag && d < 0)
		  {
		    d = abs_d;
		    op1 = gen_int_mode (abs_d, compute_mode);
		  }

		if (d == 1)
		  quotient = op0;
		else if (d == -1)
		  quotient = expand_unop (compute_mode, neg_optab, op0,
					  tquotient, 0);
		else if (abs_d == (unsigned HOST_WIDE_INT) 1 << (size - 1))
		  {
		    /* This case is not handled correctly below.  */
		    quotient = emit_store_flag (tquotient, EQ, op0, op1,
						compute_mode, 1, 1);
		    if (quotient == 0)
		      goto fail1;
		  }
		else if (EXACT_POWER_OF_2_OR_ZERO_P (d)
			 && (rem_flag ? smod_pow2_cheap[compute_mode]
				      : sdiv_pow2_cheap[compute_mode])
			 /* We assume that cheap metric is true if the
			    optab has an expander for this mode.  */
			 && (((rem_flag ? smod_optab : sdiv_optab)
			      ->handlers[compute_mode].insn_code
			      != CODE_FOR_nothing)
			     || (sdivmod_optab->handlers[compute_mode]
				 .insn_code != CODE_FOR_nothing)))
		  ;
		else if (EXACT_POWER_OF_2_OR_ZERO_P (abs_d))
		  {
		    if (rem_flag)
		      {
			remainder = expand_smod_pow2 (compute_mode, op0, d);
			if (remainder)
			  return gen_lowpart (mode, remainder);
		      }

		    if (sdiv_pow2_cheap[compute_mode]
			&& ((sdiv_optab->handlers[compute_mode].insn_code
			     != CODE_FOR_nothing)
			    || (sdivmod_optab->handlers[compute_mode].insn_code
				!= CODE_FOR_nothing)))
		      quotient = expand_divmod (0, TRUNC_DIV_EXPR,
						compute_mode, op0,
						gen_int_mode (abs_d,
							      compute_mode),
						NULL_RTX, 0);
		    else
		      quotient = expand_sdiv_pow2 (compute_mode, op0, abs_d);

		    /* We have computed OP0 / abs(OP1).  If OP1 is negative,
		       negate the quotient.  */
		    if (d < 0)
		      {
			insn = get_last_insn ();
			if (insn != last
			    && (set = single_set (insn)) != 0
			    && SET_DEST (set) == quotient
			    && abs_d < ((unsigned HOST_WIDE_INT) 1
					<< (HOST_BITS_PER_WIDE_INT - 1)))
			  set_unique_reg_note (insn,
					       REG_EQUAL,
					       gen_rtx_DIV (compute_mode,
							    op0,
							    GEN_INT
							    (trunc_int_for_mode
							     (abs_d,
							      compute_mode))));

			quotient = expand_unop (compute_mode, neg_optab,
						quotient, quotient, 0);
		      }
		  }
		else if (size <= HOST_BITS_PER_WIDE_INT)
		  {
		    choose_multiplier (abs_d, size, size - 1,
				       &mlr, &post_shift, &lgup);
		    ml = (unsigned HOST_WIDE_INT) INTVAL (mlr);
		    if (ml < (unsigned HOST_WIDE_INT) 1 << (size - 1))
		      {
			rtx t1, t2, t3;

			if (post_shift >= BITS_PER_WORD
			    || size - 1 >= BITS_PER_WORD)
			  goto fail1;

			extra_cost = (shift_cost[compute_mode][post_shift]
				      + shift_cost[compute_mode][size - 1]
				      + add_cost[compute_mode]);
			t1 = expand_mult_highpart (compute_mode, op0, mlr,
						   NULL_RTX, 0,
						   max_cost - extra_cost);
			if (t1 == 0)
			  goto fail1;
			t2 = expand_shift
			  (RSHIFT_EXPR, compute_mode, t1,
			   build_int_cst (NULL_TREE, post_shift),
			   NULL_RTX, 0);
			t3 = expand_shift
			  (RSHIFT_EXPR, compute_mode, op0,
			   build_int_cst (NULL_TREE, size - 1),
			   NULL_RTX, 0);
			if (d < 0)
			  quotient
			    = force_operand (gen_rtx_MINUS (compute_mode,
							    t3, t2),
					     tquotient);
			else
			  quotient
			    = force_operand (gen_rtx_MINUS (compute_mode,
							    t2, t3),
					     tquotient);
		      }
		    else
		      {
			rtx t1, t2, t3, t4;

			if (post_shift >= BITS_PER_WORD
			    || size - 1 >= BITS_PER_WORD)
			  goto fail1;

			ml |= (~(unsigned HOST_WIDE_INT) 0) << (size - 1);
			mlr = gen_int_mode (ml, compute_mode);
			extra_cost = (shift_cost[compute_mode][post_shift]
				      + shift_cost[compute_mode][size - 1]
				      + 2 * add_cost[compute_mode]);
			t1 = expand_mult_highpart (compute_mode, op0, mlr,
						   NULL_RTX, 0,
						   max_cost - extra_cost);
			if (t1 == 0)
			  goto fail1;
			t2 = force_operand (gen_rtx_PLUS (compute_mode,
							  t1, op0),
					    NULL_RTX);
			t3 = expand_shift
			  (RSHIFT_EXPR, compute_mode, t2,
			   build_int_cst (NULL_TREE, post_shift),
			   NULL_RTX, 0);
			t4 = expand_shift
			  (RSHIFT_EXPR, compute_mode, op0,
			   build_int_cst (NULL_TREE, size - 1),
			   NULL_RTX, 0);
			if (d < 0)
			  quotient
			    = force_operand (gen_rtx_MINUS (compute_mode,
							    t4, t3),
					     tquotient);
			else
			  quotient
			    = force_operand (gen_rtx_MINUS (compute_mode,
							    t3, t4),
					     tquotient);
		      }
		  }
		else		/* Too wide mode to use tricky code */
		  break;

		insn = get_last_insn ();
		if (insn != last
		    && (set = single_set (insn)) != 0
		    && SET_DEST (set) == quotient)
		  set_unique_reg_note (insn,
				       REG_EQUAL,
				       gen_rtx_DIV (compute_mode, op0, op1));
	      }
	    break;
	  }
      fail1:
	delete_insns_since (last);
	break;

      case FLOOR_DIV_EXPR:
      case FLOOR_MOD_EXPR:
      /* We will come here only for signed operations.  */
	if (op1_is_constant && HOST_BITS_PER_WIDE_INT >= size)
	  {
	    unsigned HOST_WIDE_INT mh;
	    int pre_shift, lgup, post_shift;
	    HOST_WIDE_INT d = INTVAL (op1);
	    rtx ml;

	    if (d > 0)
	      {
		/* We could just as easily deal with negative constants here,
		   but it does not seem worth the trouble for GCC 2.6.  */
		if (EXACT_POWER_OF_2_OR_ZERO_P (d))
		  {
		    pre_shift = floor_log2 (d);
		    if (rem_flag)
		      {
			remainder = expand_binop (compute_mode, and_optab, op0,
						  GEN_INT (((HOST_WIDE_INT) 1 << pre_shift) - 1),
						  remainder, 0, OPTAB_LIB_WIDEN);
			if (remainder)
			  return gen_lowpart (mode, remainder);
		      }
		    quotient = expand_shift
		      (RSHIFT_EXPR, compute_mode, op0,
		       build_int_cst (NULL_TREE, pre_shift),
		       tquotient, 0);
		  }
		else
		  {
		    rtx t1, t2, t3, t4;

		    mh = choose_multiplier (d, size, size - 1,
					    &ml, &post_shift, &lgup);
		    gcc_assert (!mh);

		    if (post_shift < BITS_PER_WORD
			&& size - 1 < BITS_PER_WORD)
		      {
			t1 = expand_shift
			  (RSHIFT_EXPR, compute_mode, op0,
			   build_int_cst (NULL_TREE, size - 1),
			   NULL_RTX, 0);
			t2 = expand_binop (compute_mode, xor_optab, op0, t1,
					   NULL_RTX, 0, OPTAB_WIDEN);
			extra_cost = (shift_cost[compute_mode][post_shift]
				      + shift_cost[compute_mode][size - 1]
				      + 2 * add_cost[compute_mode]);
			t3 = expand_mult_highpart (compute_mode, t2, ml,
						   NULL_RTX, 1,
						   max_cost - extra_cost);
			if (t3 != 0)
			  {
			    t4 = expand_shift
			      (RSHIFT_EXPR, compute_mode, t3,
			       build_int_cst (NULL_TREE, post_shift),
			       NULL_RTX, 1);
			    quotient = expand_binop (compute_mode, xor_optab,
						     t4, t1, tquotient, 0,
						     OPTAB_WIDEN);
			  }
		      }
		  }
	      }
	    else
	      {
		rtx nsign, t1, t2, t3, t4;
		t1 = force_operand (gen_rtx_PLUS (compute_mode,
						  op0, constm1_rtx), NULL_RTX);
		t2 = expand_binop (compute_mode, ior_optab, op0, t1, NULL_RTX,
				   0, OPTAB_WIDEN);
		nsign = expand_shift
		  (RSHIFT_EXPR, compute_mode, t2,
		   build_int_cst (NULL_TREE, size - 1),
		   NULL_RTX, 0);
		t3 = force_operand (gen_rtx_MINUS (compute_mode, t1, nsign),
				    NULL_RTX);
		t4 = expand_divmod (0, TRUNC_DIV_EXPR, compute_mode, t3, op1,
				    NULL_RTX, 0);
		if (t4)
		  {
		    rtx t5;
		    t5 = expand_unop (compute_mode, one_cmpl_optab, nsign,
				      NULL_RTX, 0);
		    quotient = force_operand (gen_rtx_PLUS (compute_mode,
							    t4, t5),
					      tquotient);
		  }
	      }
	  }

	if (quotient != 0)
	  break;
	delete_insns_since (last);

	/* Try using an instruction that produces both the quotient and
	   remainder, using truncation.  We can easily compensate the quotient
	   or remainder to get floor rounding, once we have the remainder.
	   Notice that we compute also the final remainder value here,
	   and return the result right away.  */
	if (target == 0 || GET_MODE (target) != compute_mode)
	  target = gen_reg_rtx (compute_mode);

	if (rem_flag)
	  {
	    remainder
	      = REG_P (target) ? target : gen_reg_rtx (compute_mode);
	    quotient = gen_reg_rtx (compute_mode);
	  }
	else
	  {
	    quotient
	      = REG_P (target) ? target : gen_reg_rtx (compute_mode);
	    remainder = gen_reg_rtx (compute_mode);
	  }

	if (expand_twoval_binop (sdivmod_optab, op0, op1,
				 quotient, remainder, 0))
	  {
	    /* This could be computed with a branch-less sequence.
	       Save that for later.  */
	    rtx tem;
	    rtx label = gen_label_rtx ();
	    do_cmp_and_jump (remainder, const0_rtx, EQ, compute_mode, label);
	    tem = expand_binop (compute_mode, xor_optab, op0, op1,
				NULL_RTX, 0, OPTAB_WIDEN);
	    do_cmp_and_jump (tem, const0_rtx, GE, compute_mode, label);
	    expand_dec (quotient, const1_rtx);
	    expand_inc (remainder, op1);
	    emit_label (label);
	    return gen_lowpart (mode, rem_flag ? remainder : quotient);
	  }

	/* No luck with division elimination or divmod.  Have to do it
	   by conditionally adjusting op0 *and* the result.  */
	{
	  rtx label1, label2, label3, label4, label5;
	  rtx adjusted_op0;
	  rtx tem;

	  quotient = gen_reg_rtx (compute_mode);
	  adjusted_op0 = copy_to_mode_reg (compute_mode, op0);
	  label1 = gen_label_rtx ();
	  label2 = gen_label_rtx ();
	  label3 = gen_label_rtx ();
	  label4 = gen_label_rtx ();
	  label5 = gen_label_rtx ();
	  do_cmp_and_jump (op1, const0_rtx, LT, compute_mode, label2);
	  do_cmp_and_jump (adjusted_op0, const0_rtx, LT, compute_mode, label1);
	  tem = expand_binop (compute_mode, sdiv_optab, adjusted_op0, op1,
			      quotient, 0, OPTAB_LIB_WIDEN);
	  if (tem != quotient)
	    emit_move_insn (quotient, tem);
	  emit_jump_insn (gen_jump (label5));
	  emit_barrier ();
	  emit_label (label1);
	  expand_inc (adjusted_op0, const1_rtx);
	  emit_jump_insn (gen_jump (label4));
	  emit_barrier ();
	  emit_label (label2);
	  do_cmp_and_jump (adjusted_op0, const0_rtx, GT, compute_mode, label3);
	  tem = expand_binop (compute_mode, sdiv_optab, adjusted_op0, op1,
			      quotient, 0, OPTAB_LIB_WIDEN);
	  if (tem != quotient)
	    emit_move_insn (quotient, tem);
	  emit_jump_insn (gen_jump (label5));
	  emit_barrier ();
	  emit_label (label3);
	  expand_dec (adjusted_op0, const1_rtx);
	  emit_label (label4);
	  tem = expand_binop (compute_mode, sdiv_optab, adjusted_op0, op1,
			      quotient, 0, OPTAB_LIB_WIDEN);
	  if (tem != quotient)
	    emit_move_insn (quotient, tem);
	  expand_dec (quotient, const1_rtx);
	  emit_label (label5);
	}
	break;

      case CEIL_DIV_EXPR:
      case CEIL_MOD_EXPR:
	if (unsignedp)
	  {
	    if (op1_is_constant && EXACT_POWER_OF_2_OR_ZERO_P (INTVAL (op1)))
	      {
		rtx t1, t2, t3;
		unsigned HOST_WIDE_INT d = INTVAL (op1);
		t1 = expand_shift (RSHIFT_EXPR, compute_mode, op0,
				   build_int_cst (NULL_TREE, floor_log2 (d)),
				   tquotient, 1);
		t2 = expand_binop (compute_mode, and_optab, op0,
				   GEN_INT (d - 1),
				   NULL_RTX, 1, OPTAB_LIB_WIDEN);
		t3 = gen_reg_rtx (compute_mode);
		t3 = emit_store_flag (t3, NE, t2, const0_rtx,
				      compute_mode, 1, 1);
		if (t3 == 0)
		  {
		    rtx lab;
		    lab = gen_label_rtx ();
		    do_cmp_and_jump (t2, const0_rtx, EQ, compute_mode, lab);
		    expand_inc (t1, const1_rtx);
		    emit_label (lab);
		    quotient = t1;
		  }
		else
		  quotient = force_operand (gen_rtx_PLUS (compute_mode,
							  t1, t3),
					    tquotient);
		break;
	      }

	    /* Try using an instruction that produces both the quotient and
	       remainder, using truncation.  We can easily compensate the
	       quotient or remainder to get ceiling rounding, once we have the
	       remainder.  Notice that we compute also the final remainder
	       value here, and return the result right away.  */
	    if (target == 0 || GET_MODE (target) != compute_mode)
	      target = gen_reg_rtx (compute_mode);

	    if (rem_flag)
	      {
		remainder = (REG_P (target)
			     ? target : gen_reg_rtx (compute_mode));
		quotient = gen_reg_rtx (compute_mode);
	      }
	    else
	      {
		quotient = (REG_P (target)
			    ? target : gen_reg_rtx (compute_mode));
		remainder = gen_reg_rtx (compute_mode);
	      }

	    if (expand_twoval_binop (udivmod_optab, op0, op1, quotient,
				     remainder, 1))
	      {
		/* This could be computed with a branch-less sequence.
		   Save that for later.  */
		rtx label = gen_label_rtx ();
		do_cmp_and_jump (remainder, const0_rtx, EQ,
				 compute_mode, label);
		expand_inc (quotient, const1_rtx);
		expand_dec (remainder, op1);
		emit_label (label);
		return gen_lowpart (mode, rem_flag ? remainder : quotient);
	      }

	    /* No luck with division elimination or divmod.  Have to do it
	       by conditionally adjusting op0 *and* the result.  */
	    {
	      rtx label1, label2;
	      rtx adjusted_op0, tem;

	      quotient = gen_reg_rtx (compute_mode);
	      adjusted_op0 = copy_to_mode_reg (compute_mode, op0);
	      label1 = gen_label_rtx ();
	      label2 = gen_label_rtx ();
	      do_cmp_and_jump (adjusted_op0, const0_rtx, NE,
			       compute_mode, label1);
	      emit_move_insn  (quotient, const0_rtx);
	      emit_jump_insn (gen_jump (label2));
	      emit_barrier ();
	      emit_label (label1);
	      expand_dec (adjusted_op0, const1_rtx);
	      tem = expand_binop (compute_mode, udiv_optab, adjusted_op0, op1,
				  quotient, 1, OPTAB_LIB_WIDEN);
	      if (tem != quotient)
		emit_move_insn (quotient, tem);
	      expand_inc (quotient, const1_rtx);
	      emit_label (label2);
	    }
	  }
	else /* signed */
	  {
	    if (op1_is_constant && EXACT_POWER_OF_2_OR_ZERO_P (INTVAL (op1))
		&& INTVAL (op1) >= 0)
	      {
		/* This is extremely similar to the code for the unsigned case
		   above.  For 2.7 we should merge these variants, but for
		   2.6.1 I don't want to touch the code for unsigned since that
		   get used in C.  The signed case will only be used by other
		   languages (Ada).  */

		rtx t1, t2, t3;
		unsigned HOST_WIDE_INT d = INTVAL (op1);
		t1 = expand_shift (RSHIFT_EXPR, compute_mode, op0,
				   build_int_cst (NULL_TREE, floor_log2 (d)),
				   tquotient, 0);
		t2 = expand_binop (compute_mode, and_optab, op0,
				   GEN_INT (d - 1),
				   NULL_RTX, 1, OPTAB_LIB_WIDEN);
		t3 = gen_reg_rtx (compute_mode);
		t3 = emit_store_flag (t3, NE, t2, const0_rtx,
				      compute_mode, 1, 1);
		if (t3 == 0)
		  {
		    rtx lab;
		    lab = gen_label_rtx ();
		    do_cmp_and_jump (t2, const0_rtx, EQ, compute_mode, lab);
		    expand_inc (t1, const1_rtx);
		    emit_label (lab);
		    quotient = t1;
		  }
		else
		  quotient = force_operand (gen_rtx_PLUS (compute_mode,
							  t1, t3),
					    tquotient);
		break;
	      }

	    /* Try using an instruction that produces both the quotient and
	       remainder, using truncation.  We can easily compensate the
	       quotient or remainder to get ceiling rounding, once we have the
	       remainder.  Notice that we compute also the final remainder
	       value here, and return the result right away.  */
	    if (target == 0 || GET_MODE (target) != compute_mode)
	      target = gen_reg_rtx (compute_mode);
	    if (rem_flag)
	      {
		remainder= (REG_P (target)
			    ? target : gen_reg_rtx (compute_mode));
		quotient = gen_reg_rtx (compute_mode);
	      }
	    else
	      {
		quotient = (REG_P (target)
			    ? target : gen_reg_rtx (compute_mode));
		remainder = gen_reg_rtx (compute_mode);
	      }

	    if (expand_twoval_binop (sdivmod_optab, op0, op1, quotient,
				     remainder, 0))
	      {
		/* This could be computed with a branch-less sequence.
		   Save that for later.  */
		rtx tem;
		rtx label = gen_label_rtx ();
		do_cmp_and_jump (remainder, const0_rtx, EQ,
				 compute_mode, label);
		tem = expand_binop (compute_mode, xor_optab, op0, op1,
				    NULL_RTX, 0, OPTAB_WIDEN);
		do_cmp_and_jump (tem, const0_rtx, LT, compute_mode, label);
		expand_inc (quotient, const1_rtx);
		expand_dec (remainder, op1);
		emit_label (label);
		return gen_lowpart (mode, rem_flag ? remainder : quotient);
	      }

	    /* No luck with division elimination or divmod.  Have to do it
	       by conditionally adjusting op0 *and* the result.  */
	    {
	      rtx label1, label2, label3, label4, label5;
	      rtx adjusted_op0;
	      rtx tem;

	      quotient = gen_reg_rtx (compute_mode);
	      adjusted_op0 = copy_to_mode_reg (compute_mode, op0);
	      label1 = gen_label_rtx ();
	      label2 = gen_label_rtx ();
	      label3 = gen_label_rtx ();
	      label4 = gen_label_rtx ();
	      label5 = gen_label_rtx ();
	      do_cmp_and_jump (op1, const0_rtx, LT, compute_mode, label2);
	      do_cmp_and_jump (adjusted_op0, const0_rtx, GT,
			       compute_mode, label1);
	      tem = expand_binop (compute_mode, sdiv_optab, adjusted_op0, op1,
				  quotient, 0, OPTAB_LIB_WIDEN);
	      if (tem != quotient)
		emit_move_insn (quotient, tem);
	      emit_jump_insn (gen_jump (label5));
	      emit_barrier ();
	      emit_label (label1);
	      expand_dec (adjusted_op0, const1_rtx);
	      emit_jump_insn (gen_jump (label4));
	      emit_barrier ();
	      emit_label (label2);
	      do_cmp_and_jump (adjusted_op0, const0_rtx, LT,
			       compute_mode, label3);
	      tem = expand_binop (compute_mode, sdiv_optab, adjusted_op0, op1,
				  quotient, 0, OPTAB_LIB_WIDEN);
	      if (tem != quotient)
		emit_move_insn (quotient, tem);
	      emit_jump_insn (gen_jump (label5));
	      emit_barrier ();
	      emit_label (label3);
	      expand_inc (adjusted_op0, const1_rtx);
	      emit_label (label4);
	      tem = expand_binop (compute_mode, sdiv_optab, adjusted_op0, op1,
				  quotient, 0, OPTAB_LIB_WIDEN);
	      if (tem != quotient)
		emit_move_insn (quotient, tem);
	      expand_inc (quotient, const1_rtx);
	      emit_label (label5);
	    }
	  }
	break;

      case EXACT_DIV_EXPR:
	if (op1_is_constant && HOST_BITS_PER_WIDE_INT >= size)
	  {
	    HOST_WIDE_INT d = INTVAL (op1);
	    unsigned HOST_WIDE_INT ml;
	    int pre_shift;
	    rtx t1;

	    pre_shift = floor_log2 (d & -d);
	    ml = invert_mod2n (d >> pre_shift, size);
	    t1 = expand_shift (RSHIFT_EXPR, compute_mode, op0,
			       build_int_cst (NULL_TREE, pre_shift),
			       NULL_RTX, unsignedp);
	    quotient = expand_mult (compute_mode, t1,
				    gen_int_mode (ml, compute_mode),
				    NULL_RTX, 1);

	    insn = get_last_insn ();
	    set_unique_reg_note (insn,
				 REG_EQUAL,
				 gen_rtx_fmt_ee (unsignedp ? UDIV : DIV,
						 compute_mode,
						 op0, op1));
	  }
	break;

      case ROUND_DIV_EXPR:
      case ROUND_MOD_EXPR:
	if (unsignedp)
	  {
	    rtx tem;
	    rtx label;
	    label = gen_label_rtx ();
	    quotient = gen_reg_rtx (compute_mode);
	    remainder = gen_reg_rtx (compute_mode);
	    if (expand_twoval_binop (udivmod_optab, op0, op1, quotient, remainder, 1) == 0)
	      {
		rtx tem;
		quotient = expand_binop (compute_mode, udiv_optab, op0, op1,
					 quotient, 1, OPTAB_LIB_WIDEN);
		tem = expand_mult (compute_mode, quotient, op1, NULL_RTX, 1);
		remainder = expand_binop (compute_mode, sub_optab, op0, tem,
					  remainder, 1, OPTAB_LIB_WIDEN);
	      }
	    tem = plus_constant (op1, -1);
	    tem = expand_shift (RSHIFT_EXPR, compute_mode, tem,
				build_int_cst (NULL_TREE, 1),
				NULL_RTX, 1);
	    do_cmp_and_jump (remainder, tem, LEU, compute_mode, label);
	    expand_inc (quotient, const1_rtx);
	    expand_dec (remainder, op1);
	    emit_label (label);
	  }
	else
	  {
	    rtx abs_rem, abs_op1, tem, mask;
	    rtx label;
	    label = gen_label_rtx ();
	    quotient = gen_reg_rtx (compute_mode);
	    remainder = gen_reg_rtx (compute_mode);
	    if (expand_twoval_binop (sdivmod_optab, op0, op1, quotient, remainder, 0) == 0)
	      {
		rtx tem;
		quotient = expand_binop (compute_mode, sdiv_optab, op0, op1,
					 quotient, 0, OPTAB_LIB_WIDEN);
		tem = expand_mult (compute_mode, quotient, op1, NULL_RTX, 0);
		remainder = expand_binop (compute_mode, sub_optab, op0, tem,
					  remainder, 0, OPTAB_LIB_WIDEN);
	      }
	    abs_rem = expand_abs (compute_mode, remainder, NULL_RTX, 1, 0);
	    abs_op1 = expand_abs (compute_mode, op1, NULL_RTX, 1, 0);
	    tem = expand_shift (LSHIFT_EXPR, compute_mode, abs_rem,
				build_int_cst (NULL_TREE, 1),
				NULL_RTX, 1);
	    do_cmp_and_jump (tem, abs_op1, LTU, compute_mode, label);
	    tem = expand_binop (compute_mode, xor_optab, op0, op1,
				NULL_RTX, 0, OPTAB_WIDEN);
	    mask = expand_shift (RSHIFT_EXPR, compute_mode, tem,
				 build_int_cst (NULL_TREE, size - 1),
				 NULL_RTX, 0);
	    tem = expand_binop (compute_mode, xor_optab, mask, const1_rtx,
				NULL_RTX, 0, OPTAB_WIDEN);
	    tem = expand_binop (compute_mode, sub_optab, tem, mask,
				NULL_RTX, 0, OPTAB_WIDEN);
	    expand_inc (quotient, tem);
	    tem = expand_binop (compute_mode, xor_optab, mask, op1,
				NULL_RTX, 0, OPTAB_WIDEN);
	    tem = expand_binop (compute_mode, sub_optab, tem, mask,
				NULL_RTX, 0, OPTAB_WIDEN);
	    expand_dec (remainder, tem);
	    emit_label (label);
	  }
	return gen_lowpart (mode, rem_flag ? remainder : quotient);

      default:
	gcc_unreachable ();
      }

  if (quotient == 0)
    {
      if (target && GET_MODE (target) != compute_mode)
	target = 0;

      if (rem_flag)
	{
	  /* Try to produce the remainder without producing the quotient.
	     If we seem to have a divmod pattern that does not require widening,
	     don't try widening here.  We should really have a WIDEN argument
	     to expand_twoval_binop, since what we'd really like to do here is
	     1) try a mod insn in compute_mode
	     2) try a divmod insn in compute_mode
	     3) try a div insn in compute_mode and multiply-subtract to get
	        remainder
	     4) try the same things with widening allowed.  */
	  remainder
	    = sign_expand_binop (compute_mode, umod_optab, smod_optab,
				 op0, op1, target,
				 unsignedp,
				 ((optab2->handlers[compute_mode].insn_code
				   != CODE_FOR_nothing)
				  ? OPTAB_DIRECT : OPTAB_WIDEN));
	  if (remainder == 0)
	    {
	      /* No luck there.  Can we do remainder and divide at once
		 without a library call?  */
	      remainder = gen_reg_rtx (compute_mode);
	      if (! expand_twoval_binop ((unsignedp
					  ? udivmod_optab
					  : sdivmod_optab),
					 op0, op1,
					 NULL_RTX, remainder, unsignedp))
		remainder = 0;
	    }

	  if (remainder)
	    return gen_lowpart (mode, remainder);
	}

      /* Produce the quotient.  Try a quotient insn, but not a library call.
	 If we have a divmod in this mode, use it in preference to widening
	 the div (for this test we assume it will not fail). Note that optab2
	 is set to the one of the two optabs that the call below will use.  */
      quotient
	= sign_expand_binop (compute_mode, udiv_optab, sdiv_optab,
			     op0, op1, rem_flag ? NULL_RTX : target,
			     unsignedp,
			     ((optab2->handlers[compute_mode].insn_code
			       != CODE_FOR_nothing)
			      ? OPTAB_DIRECT : OPTAB_WIDEN));

      if (quotient == 0)
	{
	  /* No luck there.  Try a quotient-and-remainder insn,
	     keeping the quotient alone.  */
	  quotient = gen_reg_rtx (compute_mode);
	  if (! expand_twoval_binop (unsignedp ? udivmod_optab : sdivmod_optab,
				     op0, op1,
				     quotient, NULL_RTX, unsignedp))
	    {
	      quotient = 0;
	      if (! rem_flag)
		/* Still no luck.  If we are not computing the remainder,
		   use a library call for the quotient.  */
		quotient = sign_expand_binop (compute_mode,
					      udiv_optab, sdiv_optab,
					      op0, op1, target,
					      unsignedp, OPTAB_LIB_WIDEN);
	    }
	}
    }

  if (rem_flag)
    {
      if (target && GET_MODE (target) != compute_mode)
	target = 0;

      if (quotient == 0)
	{
	  /* No divide instruction either.  Use library for remainder.  */
	  remainder = sign_expand_binop (compute_mode, umod_optab, smod_optab,
					 op0, op1, target,
					 unsignedp, OPTAB_LIB_WIDEN);
	  /* No remainder function.  Try a quotient-and-remainder
	     function, keeping the remainder.  */
	  if (!remainder)
	    {
	      remainder = gen_reg_rtx (compute_mode);
	      if (!expand_twoval_binop_libfunc 
		  (unsignedp ? udivmod_optab : sdivmod_optab,
		   op0, op1,
		   NULL_RTX, remainder,
		   unsignedp ? UMOD : MOD))
		remainder = NULL_RTX;
	    }
	}
      else
	{
	  /* We divided.  Now finish doing X - Y * (X / Y).  */
	  remainder = expand_mult (compute_mode, quotient, op1,
				   NULL_RTX, unsignedp);
	  remainder = expand_binop (compute_mode, sub_optab, op0,
				    remainder, target, unsignedp,
				    OPTAB_LIB_WIDEN);
	}
    }

  return gen_lowpart (mode, rem_flag ? remainder : quotient);
}

/* Return a tree node with data type TYPE, describing the value of X.
   Usually this is an VAR_DECL, if there is no obvious better choice.
   X may be an expression, however we only support those expressions
   generated by loop.c.  */

tree
make_tree (tree type, rtx x)
{
  tree t;

  switch (GET_CODE (x))
    {
    case CONST_INT:
      {
	HOST_WIDE_INT hi = 0;

	if (INTVAL (x) < 0
	    && !(TYPE_UNSIGNED (type)
		 && (GET_MODE_BITSIZE (TYPE_MODE (type))
		     < HOST_BITS_PER_WIDE_INT)))
	  hi = -1;
      
	t = build_int_cst_wide (type, INTVAL (x), hi);
	
	return t;
      }
      
    case CONST_DOUBLE:
      if (GET_MODE (x) == VOIDmode)
	t = build_int_cst_wide (type,
				CONST_DOUBLE_LOW (x), CONST_DOUBLE_HIGH (x));
      else
	{
	  REAL_VALUE_TYPE d;

	  REAL_VALUE_FROM_CONST_DOUBLE (d, x);
	  t = build_real (type, d);
	}

      return t;

    case CONST_VECTOR:
      {
	int units = CONST_VECTOR_NUNITS (x);
	tree itype = TREE_TYPE (type);
	tree t = NULL_TREE;
	int i;


	/* Build a tree with vector elements.  */
	for (i = units - 1; i >= 0; --i)
	  {
	    rtx elt = CONST_VECTOR_ELT (x, i);
	    t = tree_cons (NULL_TREE, make_tree (itype, elt), t);
	  }

	return build_vector (type, t);
      }

    case PLUS:
      return fold_build2 (PLUS_EXPR, type, make_tree (type, XEXP (x, 0)),
			  make_tree (type, XEXP (x, 1)));

    case MINUS:
      return fold_build2 (MINUS_EXPR, type, make_tree (type, XEXP (x, 0)),
			  make_tree (type, XEXP (x, 1)));

    case NEG:
      return fold_build1 (NEGATE_EXPR, type, make_tree (type, XEXP (x, 0)));

    case MULT:
      return fold_build2 (MULT_EXPR, type, make_tree (type, XEXP (x, 0)),
			  make_tree (type, XEXP (x, 1)));

    case ASHIFT:
      return fold_build2 (LSHIFT_EXPR, type, make_tree (type, XEXP (x, 0)),
			  make_tree (type, XEXP (x, 1)));

    case LSHIFTRT:
      t = lang_hooks.types.unsigned_type (type);
      return fold_convert (type, build2 (RSHIFT_EXPR, t,
			    		 make_tree (t, XEXP (x, 0)),
				    	 make_tree (type, XEXP (x, 1))));

    case ASHIFTRT:
      t = lang_hooks.types.signed_type (type);
      return fold_convert (type, build2 (RSHIFT_EXPR, t,
					 make_tree (t, XEXP (x, 0)),
				    	 make_tree (type, XEXP (x, 1))));

    case DIV:
      if (TREE_CODE (type) != REAL_TYPE)
	t = lang_hooks.types.signed_type (type);
      else
	t = type;

      return fold_convert (type, build2 (TRUNC_DIV_EXPR, t,
				    	 make_tree (t, XEXP (x, 0)),
				    	 make_tree (t, XEXP (x, 1))));
    case UDIV:
      t = lang_hooks.types.unsigned_type (type);
      return fold_convert (type, build2 (TRUNC_DIV_EXPR, t,
				    	 make_tree (t, XEXP (x, 0)),
				    	 make_tree (t, XEXP (x, 1))));

    case SIGN_EXTEND:
    case ZERO_EXTEND:
      t = lang_hooks.types.type_for_mode (GET_MODE (XEXP (x, 0)),
					  GET_CODE (x) == ZERO_EXTEND);
      return fold_convert (type, make_tree (t, XEXP (x, 0)));

    case CONST:
      return make_tree (type, XEXP (x, 0));

    case SYMBOL_REF:
      t = SYMBOL_REF_DECL (x);
      if (t)
	return fold_convert (type, build_fold_addr_expr (t));
      /* else fall through.  */

    default:
      t = build_decl (VAR_DECL, NULL_TREE, type);

      /* If TYPE is a POINTER_TYPE, X might be Pmode with TYPE_MODE being
	 ptr_mode.  So convert.  */
      if (POINTER_TYPE_P (type))
	x = convert_memory_address (TYPE_MODE (type), x);

      /* Note that we do *not* use SET_DECL_RTL here, because we do not
	 want set_decl_rtl to go adjusting REG_ATTRS for this temporary.  */
      t->decl_with_rtl.rtl = x;

      return t;
    }
}

/* Compute the logical-and of OP0 and OP1, storing it in TARGET
   and returning TARGET.

   If TARGET is 0, a pseudo-register or constant is returned.  */

rtx
expand_and (enum machine_mode mode, rtx op0, rtx op1, rtx target)
{
  rtx tem = 0;

  if (GET_MODE (op0) == VOIDmode && GET_MODE (op1) == VOIDmode)
    tem = simplify_binary_operation (AND, mode, op0, op1);
  if (tem == 0)
    tem = expand_binop (mode, and_optab, op0, op1, target, 0, OPTAB_LIB_WIDEN);

  if (target == 0)
    target = tem;
  else if (tem != target)
    emit_move_insn (target, tem);
  return target;
}

/* Emit a store-flags instruction for comparison CODE on OP0 and OP1
   and storing in TARGET.  Normally return TARGET.
   Return 0 if that cannot be done.

   MODE is the mode to use for OP0 and OP1 should they be CONST_INTs.  If
   it is VOIDmode, they cannot both be CONST_INT.

   UNSIGNEDP is for the case where we have to widen the operands
   to perform the operation.  It says to use zero-extension.

   NORMALIZEP is 1 if we should convert the result to be either zero
   or one.  Normalize is -1 if we should convert the result to be
   either zero or -1.  If NORMALIZEP is zero, the result will be left
   "raw" out of the scc insn.  */

rtx
emit_store_flag (rtx target, enum rtx_code code, rtx op0, rtx op1,
		 enum machine_mode mode, int unsignedp, int normalizep)
{
  rtx subtarget;
  enum insn_code icode;
  enum machine_mode compare_mode;
  enum machine_mode target_mode = GET_MODE (target);
  rtx tem;
  rtx last = get_last_insn ();
  rtx pattern, comparison;

  if (unsignedp)
    code = unsigned_condition (code);

  /* If one operand is constant, make it the second one.  Only do this
     if the other operand is not constant as well.  */

  if (swap_commutative_operands_p (op0, op1))
    {
      tem = op0;
      op0 = op1;
      op1 = tem;
      code = swap_condition (code);
    }

  if (mode == VOIDmode)
    mode = GET_MODE (op0);

  /* For some comparisons with 1 and -1, we can convert this to
     comparisons with zero.  This will often produce more opportunities for
     store-flag insns.  */

  switch (code)
    {
    case LT:
      if (op1 == const1_rtx)
	op1 = const0_rtx, code = LE;
      break;
    case LE:
      if (op1 == constm1_rtx)
	op1 = const0_rtx, code = LT;
      break;
    case GE:
      if (op1 == const1_rtx)
	op1 = const0_rtx, code = GT;
      break;
    case GT:
      if (op1 == constm1_rtx)
	op1 = const0_rtx, code = GE;
      break;
    case GEU:
      if (op1 == const1_rtx)
	op1 = const0_rtx, code = NE;
      break;
    case LTU:
      if (op1 == const1_rtx)
	op1 = const0_rtx, code = EQ;
      break;
    default:
      break;
    }

  /* If we are comparing a double-word integer with zero or -1, we can
     convert the comparison into one involving a single word.  */
  if (GET_MODE_BITSIZE (mode) == BITS_PER_WORD * 2
      && GET_MODE_CLASS (mode) == MODE_INT
      && (!MEM_P (op0) || ! MEM_VOLATILE_P (op0)))
    {
      if ((code == EQ || code == NE)
	  && (op1 == const0_rtx || op1 == constm1_rtx))
	{
	  rtx op00, op01, op0both;

	  /* Do a logical OR or AND of the two words and compare the result.  */
	  op00 = simplify_gen_subreg (word_mode, op0, mode, 0);
	  op01 = simplify_gen_subreg (word_mode, op0, mode, UNITS_PER_WORD);
	  op0both = expand_binop (word_mode,
				  op1 == const0_rtx ? ior_optab : and_optab,
				  op00, op01, NULL_RTX, unsignedp, OPTAB_DIRECT);

	  if (op0both != 0)
	    return emit_store_flag (target, code, op0both, op1, word_mode,
				    unsignedp, normalizep);
	}
      else if ((code == LT || code == GE) && op1 == const0_rtx)
	{
	  rtx op0h;

	  /* If testing the sign bit, can just test on high word.  */
	  op0h = simplify_gen_subreg (word_mode, op0, mode,
				      subreg_highpart_offset (word_mode, mode));
	  return emit_store_flag (target, code, op0h, op1, word_mode,
				  unsignedp, normalizep);
	}
    }

  /* From now on, we won't change CODE, so set ICODE now.  */
  icode = setcc_gen_code[(int) code];

  /* If this is A < 0 or A >= 0, we can do this by taking the ones
     complement of A (for GE) and shifting the sign bit to the low bit.  */
  if (op1 == const0_rtx && (code == LT || code == GE)
      && GET_MODE_CLASS (mode) == MODE_INT
      && (normalizep || STORE_FLAG_VALUE == 1
	  || (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	      && ((STORE_FLAG_VALUE & GET_MODE_MASK (mode))
		  == (unsigned HOST_WIDE_INT) 1 << (GET_MODE_BITSIZE (mode) - 1)))))
    {
      subtarget = target;

      /* If the result is to be wider than OP0, it is best to convert it
	 first.  If it is to be narrower, it is *incorrect* to convert it
	 first.  */
      if (GET_MODE_SIZE (target_mode) > GET_MODE_SIZE (mode))
	{
	  op0 = convert_modes (target_mode, mode, op0, 0);
	  mode = target_mode;
	}

      if (target_mode != mode)
	subtarget = 0;

      if (code == GE)
	op0 = expand_unop (mode, one_cmpl_optab, op0,
			   ((STORE_FLAG_VALUE == 1 || normalizep)
			    ? 0 : subtarget), 0);

      if (STORE_FLAG_VALUE == 1 || normalizep)
	/* If we are supposed to produce a 0/1 value, we want to do
	   a logical shift from the sign bit to the low-order bit; for
	   a -1/0 value, we do an arithmetic shift.  */
	op0 = expand_shift (RSHIFT_EXPR, mode, op0,
			    size_int (GET_MODE_BITSIZE (mode) - 1),
			    subtarget, normalizep != -1);

      if (mode != target_mode)
	op0 = convert_modes (target_mode, mode, op0, 0);

      return op0;
    }

  if (icode != CODE_FOR_nothing)
    {
      insn_operand_predicate_fn pred;

      /* We think we may be able to do this with a scc insn.  Emit the
	 comparison and then the scc insn.  */

      do_pending_stack_adjust ();
      last = get_last_insn ();

      comparison
	= compare_from_rtx (op0, op1, code, unsignedp, mode, NULL_RTX);
      if (CONSTANT_P (comparison))
	{
	  switch (GET_CODE (comparison))
	    {
	    case CONST_INT:
	      if (comparison == const0_rtx)
		return const0_rtx;
	      break;
	      
#ifdef FLOAT_STORE_FLAG_VALUE
	    case CONST_DOUBLE:
	      if (comparison == CONST0_RTX (GET_MODE (comparison)))
		return const0_rtx;
	      break;
#endif
	    default:
	      gcc_unreachable ();
	    }
	  
	  if (normalizep == 1)
	    return const1_rtx;
	  if (normalizep == -1)
	    return constm1_rtx;
	  return const_true_rtx;
	}

      /* The code of COMPARISON may not match CODE if compare_from_rtx
	 decided to swap its operands and reverse the original code.

	 We know that compare_from_rtx returns either a CONST_INT or
	 a new comparison code, so it is safe to just extract the
	 code from COMPARISON.  */
      code = GET_CODE (comparison);

      /* Get a reference to the target in the proper mode for this insn.  */
      compare_mode = insn_data[(int) icode].operand[0].mode;
      subtarget = target;
      pred = insn_data[(int) icode].operand[0].predicate;
      if (optimize || ! (*pred) (subtarget, compare_mode))
	subtarget = gen_reg_rtx (compare_mode);

      pattern = GEN_FCN (icode) (subtarget);
      if (pattern)
	{
	  emit_insn (pattern);

	  /* If we are converting to a wider mode, first convert to
	     TARGET_MODE, then normalize.  This produces better combining
	     opportunities on machines that have a SIGN_EXTRACT when we are
	     testing a single bit.  This mostly benefits the 68k.

	     If STORE_FLAG_VALUE does not have the sign bit set when
	     interpreted in COMPARE_MODE, we can do this conversion as
	     unsigned, which is usually more efficient.  */
	  if (GET_MODE_SIZE (target_mode) > GET_MODE_SIZE (compare_mode))
	    {
	      convert_move (target, subtarget,
			    (GET_MODE_BITSIZE (compare_mode)
			     <= HOST_BITS_PER_WIDE_INT)
			    && 0 == (STORE_FLAG_VALUE
				     & ((HOST_WIDE_INT) 1
					<< (GET_MODE_BITSIZE (compare_mode) -1))));
	      op0 = target;
	      compare_mode = target_mode;
	    }
	  else
	    op0 = subtarget;

	  /* If we want to keep subexpressions around, don't reuse our
	     last target.  */

	  if (optimize)
	    subtarget = 0;

	  /* Now normalize to the proper value in COMPARE_MODE.  Sometimes
	     we don't have to do anything.  */
	  if (normalizep == 0 || normalizep == STORE_FLAG_VALUE)
	    ;
	  /* STORE_FLAG_VALUE might be the most negative number, so write
	     the comparison this way to avoid a compiler-time warning.  */
	  else if (- normalizep == STORE_FLAG_VALUE)
	    op0 = expand_unop (compare_mode, neg_optab, op0, subtarget, 0);

	  /* We don't want to use STORE_FLAG_VALUE < 0 below since this
	     makes it hard to use a value of just the sign bit due to
	     ANSI integer constant typing rules.  */
	  else if (GET_MODE_BITSIZE (compare_mode) <= HOST_BITS_PER_WIDE_INT
		   && (STORE_FLAG_VALUE
		       & ((HOST_WIDE_INT) 1
			  << (GET_MODE_BITSIZE (compare_mode) - 1))))
	    op0 = expand_shift (RSHIFT_EXPR, compare_mode, op0,
				size_int (GET_MODE_BITSIZE (compare_mode) - 1),
				subtarget, normalizep == 1);
	  else
	    {
	      gcc_assert (STORE_FLAG_VALUE & 1);
	      
	      op0 = expand_and (compare_mode, op0, const1_rtx, subtarget);
	      if (normalizep == -1)
		op0 = expand_unop (compare_mode, neg_optab, op0, op0, 0);
	    }

	  /* If we were converting to a smaller mode, do the
	     conversion now.  */
	  if (target_mode != compare_mode)
	    {
	      convert_move (target, op0, 0);
	      return target;
	    }
	  else
	    return op0;
	}
    }

  delete_insns_since (last);

  /* If optimizing, use different pseudo registers for each insn, instead
     of reusing the same pseudo.  This leads to better CSE, but slows
     down the compiler, since there are more pseudos */
  subtarget = (!optimize
	       && (target_mode == mode)) ? target : NULL_RTX;

  /* If we reached here, we can't do this with a scc insn.  However, there
     are some comparisons that can be done directly.  For example, if
     this is an equality comparison of integers, we can try to exclusive-or
     (or subtract) the two operands and use a recursive call to try the
     comparison with zero.  Don't do any of these cases if branches are
     very cheap.  */

  if (BRANCH_COST > 0
      && GET_MODE_CLASS (mode) == MODE_INT && (code == EQ || code == NE)
      && op1 != const0_rtx)
    {
      tem = expand_binop (mode, xor_optab, op0, op1, subtarget, 1,
			  OPTAB_WIDEN);

      if (tem == 0)
	tem = expand_binop (mode, sub_optab, op0, op1, subtarget, 1,
			    OPTAB_WIDEN);
      if (tem != 0)
	tem = emit_store_flag (target, code, tem, const0_rtx,
			       mode, unsignedp, normalizep);
      if (tem == 0)
	delete_insns_since (last);
      return tem;
    }

  /* Some other cases we can do are EQ, NE, LE, and GT comparisons with
     the constant zero.  Reject all other comparisons at this point.  Only
     do LE and GT if branches are expensive since they are expensive on
     2-operand machines.  */

  if (BRANCH_COST == 0
      || GET_MODE_CLASS (mode) != MODE_INT || op1 != const0_rtx
      || (code != EQ && code != NE
	  && (BRANCH_COST <= 1 || (code != LE && code != GT))))
    return 0;

  /* See what we need to return.  We can only return a 1, -1, or the
     sign bit.  */

  if (normalizep == 0)
    {
      if (STORE_FLAG_VALUE == 1 || STORE_FLAG_VALUE == -1)
	normalizep = STORE_FLAG_VALUE;

      else if (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	       && ((STORE_FLAG_VALUE & GET_MODE_MASK (mode))
		   == (unsigned HOST_WIDE_INT) 1 << (GET_MODE_BITSIZE (mode) - 1)))
	;
      else
	return 0;
    }

  /* Try to put the result of the comparison in the sign bit.  Assume we can't
     do the necessary operation below.  */

  tem = 0;

  /* To see if A <= 0, compute (A | (A - 1)).  A <= 0 iff that result has
     the sign bit set.  */

  if (code == LE)
    {
      /* This is destructive, so SUBTARGET can't be OP0.  */
      if (rtx_equal_p (subtarget, op0))
	subtarget = 0;

      tem = expand_binop (mode, sub_optab, op0, const1_rtx, subtarget, 0,
			  OPTAB_WIDEN);
      if (tem)
	tem = expand_binop (mode, ior_optab, op0, tem, subtarget, 0,
			    OPTAB_WIDEN);
    }

  /* To see if A > 0, compute (((signed) A) << BITS) - A, where BITS is the
     number of bits in the mode of OP0, minus one.  */

  if (code == GT)
    {
      if (rtx_equal_p (subtarget, op0))
	subtarget = 0;

      tem = expand_shift (RSHIFT_EXPR, mode, op0,
			  size_int (GET_MODE_BITSIZE (mode) - 1),
			  subtarget, 0);
      tem = expand_binop (mode, sub_optab, tem, op0, subtarget, 0,
			  OPTAB_WIDEN);
    }

  if (code == EQ || code == NE)
    {
      /* For EQ or NE, one way to do the comparison is to apply an operation
	 that converts the operand into a positive number if it is nonzero
	 or zero if it was originally zero.  Then, for EQ, we subtract 1 and
	 for NE we negate.  This puts the result in the sign bit.  Then we
	 normalize with a shift, if needed.

	 Two operations that can do the above actions are ABS and FFS, so try
	 them.  If that doesn't work, and MODE is smaller than a full word,
	 we can use zero-extension to the wider mode (an unsigned conversion)
	 as the operation.  */

      /* Note that ABS doesn't yield a positive number for INT_MIN, but
	 that is compensated by the subsequent overflow when subtracting
	 one / negating.  */

      if (abs_optab->handlers[mode].insn_code != CODE_FOR_nothing)
	tem = expand_unop (mode, abs_optab, op0, subtarget, 1);
      else if (ffs_optab->handlers[mode].insn_code != CODE_FOR_nothing)
	tem = expand_unop (mode, ffs_optab, op0, subtarget, 1);
      else if (GET_MODE_SIZE (mode) < UNITS_PER_WORD)
	{
	  tem = convert_modes (word_mode, mode, op0, 1);
	  mode = word_mode;
	}

      if (tem != 0)
	{
	  if (code == EQ)
	    tem = expand_binop (mode, sub_optab, tem, const1_rtx, subtarget,
				0, OPTAB_WIDEN);
	  else
	    tem = expand_unop (mode, neg_optab, tem, subtarget, 0);
	}

      /* If we couldn't do it that way, for NE we can "or" the two's complement
	 of the value with itself.  For EQ, we take the one's complement of
	 that "or", which is an extra insn, so we only handle EQ if branches
	 are expensive.  */

      if (tem == 0 && (code == NE || BRANCH_COST > 1))
	{
	  if (rtx_equal_p (subtarget, op0))
	    subtarget = 0;

	  tem = expand_unop (mode, neg_optab, op0, subtarget, 0);
	  tem = expand_binop (mode, ior_optab, tem, op0, subtarget, 0,
			      OPTAB_WIDEN);

	  if (tem && code == EQ)
	    tem = expand_unop (mode, one_cmpl_optab, tem, subtarget, 0);
	}
    }

  if (tem && normalizep)
    tem = expand_shift (RSHIFT_EXPR, mode, tem,
			size_int (GET_MODE_BITSIZE (mode) - 1),
			subtarget, normalizep == 1);

  if (tem)
    {
      if (GET_MODE (tem) != target_mode)
	{
	  convert_move (target, tem, 0);
	  tem = target;
	}
      else if (!subtarget)
	{
	  emit_move_insn (target, tem);
	  tem = target;
	}
    }
  else
    delete_insns_since (last);

  return tem;
}

/* Like emit_store_flag, but always succeeds.  */

rtx
emit_store_flag_force (rtx target, enum rtx_code code, rtx op0, rtx op1,
		       enum machine_mode mode, int unsignedp, int normalizep)
{
  rtx tem, label;

  /* First see if emit_store_flag can do the job.  */
  tem = emit_store_flag (target, code, op0, op1, mode, unsignedp, normalizep);
  if (tem != 0)
    return tem;

  if (normalizep == 0)
    normalizep = 1;

  /* If this failed, we have to do this with set/compare/jump/set code.  */

  if (!REG_P (target)
      || reg_mentioned_p (target, op0) || reg_mentioned_p (target, op1))
    target = gen_reg_rtx (GET_MODE (target));

  emit_move_insn (target, const1_rtx);
  label = gen_label_rtx ();
  do_compare_rtx_and_jump (op0, op1, code, unsignedp, mode, NULL_RTX,
			   NULL_RTX, label);

  emit_move_insn (target, const0_rtx);
  emit_label (label);

  return target;
}

/* Perform possibly multi-word comparison and conditional jump to LABEL
   if ARG1 OP ARG2 true where ARG1 and ARG2 are of mode MODE.  This is
   now a thin wrapper around do_compare_rtx_and_jump.  */

static void
do_cmp_and_jump (rtx arg1, rtx arg2, enum rtx_code op, enum machine_mode mode,
		 rtx label)
{
  int unsignedp = (op == LTU || op == LEU || op == GTU || op == GEU);
  do_compare_rtx_and_jump (arg1, arg2, op, unsignedp, mode,
			   NULL_RTX, NULL_RTX, label);
}

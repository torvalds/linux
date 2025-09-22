;; Predicate definitions for Motorola 68000.
;; Copyright (C) 2005 Free Software Foundation, Inc.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;;
;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

;; Special case of a general operand that's used as a source
;; operand. Use this to permit reads from PC-relative memory when
;; -mpcrel is specified.

(define_predicate "general_src_operand"
  (match_code "const_int,const_double,const,symbol_ref,label_ref,subreg,reg,mem")
{
  if (TARGET_PCREL
      && GET_CODE (op) == MEM
      && (GET_CODE (XEXP (op, 0)) == SYMBOL_REF
	  || GET_CODE (XEXP (op, 0)) == LABEL_REF
	  || GET_CODE (XEXP (op, 0)) == CONST))
    return 1;
  return general_operand (op, mode);
})

;; Special case of a nonimmediate operand that's used as a source. Use
;; this to permit reads from PC-relative memory when -mpcrel is
;; specified.

(define_predicate "nonimmediate_src_operand"
  (match_code "subreg,reg,mem")
{
  if (TARGET_PCREL && GET_CODE (op) == MEM
      && (GET_CODE (XEXP (op, 0)) == SYMBOL_REF
	  || GET_CODE (XEXP (op, 0)) == LABEL_REF
	  || GET_CODE (XEXP (op, 0)) == CONST))
    return 1;
  return nonimmediate_operand (op, mode);
})

;; Special case of a memory operand that's used as a source. Use this
;; to permit reads from PC-relative memory when -mpcrel is specified.

(define_predicate "memory_src_operand"
  (match_code "subreg,mem")
{
  if (TARGET_PCREL && GET_CODE (op) == MEM
      && (GET_CODE (XEXP (op, 0)) == SYMBOL_REF
	  || GET_CODE (XEXP (op, 0)) == LABEL_REF
	  || GET_CODE (XEXP (op, 0)) == CONST))
    return 1;
  return memory_operand (op, mode);
})

;; Similar to general_operand, but exclude stack_pointer_rtx.

(define_predicate "not_sp_operand"
  (match_code "subreg,reg,mem")
{
  return op != stack_pointer_rtx && nonimmediate_operand (op, mode);
})

;; Predicate that accepts only a pc-relative address.  This is needed
;; because pc-relative addresses don't satisfy the predicate
;; "general_src_operand".

(define_predicate "pcrel_address"
  (match_code "symbol_ref,label_ref,const"))

;; Accept integer operands in the range 0..0xffffffff.  We have to
;; check the range carefully since this predicate is used in DImode
;; contexts.  Also, we need some extra crud to make it work when
;; hosted on 64-bit machines.

(define_predicate "const_uint32_operand"
  (match_code "const_int,const_double")
{
  /* It doesn't make sense to ask this question with a mode that is
     not larger than 32 bits.  */
  gcc_assert (GET_MODE_BITSIZE (mode) > 32);

#if HOST_BITS_PER_WIDE_INT > 32
  /* All allowed constants will fit a CONST_INT.  */
  return (GET_CODE (op) == CONST_INT
	  && (INTVAL (op) >= 0 && INTVAL (op) <= 0xffffffffL));
#else
  return (GET_CODE (op) == CONST_INT
	  || (GET_CODE (op) == CONST_DOUBLE && CONST_DOUBLE_HIGH (op) == 0));
#endif
})

;; Accept integer operands in the range -0x80000000..0x7fffffff.  We
;; have to check the range carefully since this predicate is used in
;; DImode contexts.

(define_predicate "const_sint32_operand"
  (match_code "const_int")
{
  /* It doesn't make sense to ask this question with a mode that is
     not larger than 32 bits.  */
  gcc_assert (GET_MODE_BITSIZE (mode) > 32);

  /* All allowed constants will fit a CONST_INT.  */
  return (GET_CODE (op) == CONST_INT
	  && (INTVAL (op) >= (-0x7fffffff - 1) && INTVAL (op) <= 0x7fffffff));
})

;; Return true if X is a valid comparison operator for the dbcc
;; instruction.  Note it rejects floating point comparison
;; operators. (In the future we could use Fdbcc).  It also rejects
;; some comparisons when CC_NO_OVERFLOW is set.

(define_predicate "valid_dbcc_comparison_p"
  (and (match_code "eq,ne,gtu,ltu,geu,leu,gt,lt,ge,le")
       (match_test "valid_dbcc_comparison_p_2 (op, mode)")))

;; Check for sign_extend or zero_extend.  Used for bit-count operands.

(define_predicate "extend_operator"
  (match_code "sign_extend,zero_extend"))

;; Returns true if OP is either a symbol reference or a sum of a
;; symbol reference and a constant.

(define_predicate "symbolic_operand"
  (match_code "symbol_ref,label_ref,const")
{
  switch (GET_CODE (op))
    {
    case SYMBOL_REF:
    case LABEL_REF:
      return true;

    case CONST:
      op = XEXP (op, 0);
      return ((GET_CODE (XEXP (op, 0)) == SYMBOL_REF
	       || GET_CODE (XEXP (op, 0)) == LABEL_REF)
	      && GET_CODE (XEXP (op, 1)) == CONST_INT);

#if 0 /* Deleted, with corresponding change in m68k.h,
	 so as to fit the specs.  No CONST_DOUBLE is ever symbolic.  */
    case CONST_DOUBLE:
      return GET_MODE (op) == mode;
#endif

    default:
      return false;
    }
})

;; TODO: Add a comment here.

(define_predicate "post_inc_operand"
  (and (match_code "mem")
       (match_test "GET_CODE (XEXP (op, 0)) == POST_INC")))

;; TODO: Add a comment here.

(define_predicate "pre_dec_operand"
  (and (match_code "mem")
       (match_test "GET_CODE (XEXP (op, 0)) == PRE_DEC")))

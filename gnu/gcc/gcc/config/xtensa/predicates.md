;; Predicate definitions for Xtensa.
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

(define_predicate "add_operand"
  (ior (and (match_code "const_int")
	    (match_test "xtensa_simm8 (INTVAL (op))
			 || xtensa_simm8x256 (INTVAL (op))"))
       (match_operand 0 "register_operand")))

(define_predicate "arith_operand"
  (ior (and (match_code "const_int")
	    (match_test "xtensa_simm8 (INTVAL (op))"))
       (match_operand 0 "register_operand")))

;; Non-immediate operand excluding the constant pool.
(define_predicate "nonimmed_operand"
  (ior (and (match_operand 0 "memory_operand")
	    (match_test "!constantpool_address_p (XEXP (op, 0))"))
       (match_operand 0 "register_operand")))

;; Memory operand excluding the constant pool.
(define_predicate "mem_operand"
  (and (match_operand 0 "memory_operand")
       (match_test "!constantpool_address_p (XEXP (op, 0))")))

(define_predicate "mask_operand"
  (ior (and (match_code "const_int")
	    (match_test "xtensa_mask_immediate (INTVAL (op))"))
       (match_operand 0 "register_operand")))

(define_predicate "extui_fldsz_operand"
  (and (match_code "const_int")
       (match_test "xtensa_mask_immediate ((1 << INTVAL (op)) - 1)")))

(define_predicate "sext_operand"
  (if_then_else (match_test "TARGET_SEXT")
		(match_operand 0 "nonimmed_operand")
		(match_operand 0 "mem_operand")))

(define_predicate "sext_fldsz_operand"
  (and (match_code "const_int")
       (match_test "INTVAL (op) >= 8 && INTVAL (op) <= 23")))

(define_predicate "lsbitnum_operand"
  (and (match_code "const_int")
       (match_test "BITS_BIG_ENDIAN
		    ? (INTVAL (op) == BITS_PER_WORD - 1)
		    : (INTVAL (op) == 0)")))

(define_predicate "branch_operand"
  (ior (and (match_code "const_int")
	    (match_test "xtensa_b4const_or_zero (INTVAL (op))"))
       (match_operand 0 "register_operand")))

(define_predicate "ubranch_operand"
  (ior (and (match_code "const_int")
	    (match_test "xtensa_b4constu (INTVAL (op))"))
       (match_operand 0 "register_operand")))

(define_predicate "call_insn_operand"
  (match_code "const_int,const,symbol_ref,reg")
{
  if ((GET_CODE (op) == REG)
      && (op != arg_pointer_rtx)
      && ((REGNO (op) < FRAME_POINTER_REGNUM)
	  || (REGNO (op) > LAST_VIRTUAL_REGISTER)))
    return true;

  if (CONSTANT_ADDRESS_P (op))
    {
      /* Direct calls only allowed to static functions with PIC.  */
      if (flag_pic)
	{
	  tree callee, callee_sec, caller_sec;

	  if (GET_CODE (op) != SYMBOL_REF
	      || !SYMBOL_REF_LOCAL_P (op) || SYMBOL_REF_EXTERNAL_P (op))
	    return false;

	  /* Don't attempt a direct call if the callee is known to be in
	     a different section, since there's a good chance it will be
	     out of range.  */

	  if (flag_function_sections
	      || DECL_ONE_ONLY (current_function_decl))
	    return false;
	  caller_sec = DECL_SECTION_NAME (current_function_decl);
	  callee = SYMBOL_REF_DECL (op);
	  if (callee)
	    {
	      if (DECL_ONE_ONLY (callee))
		return false;
	      callee_sec = DECL_SECTION_NAME (callee);
	      if (((caller_sec == NULL_TREE) ^ (callee_sec == NULL_TREE))
		  || (caller_sec != NULL_TREE
		      && strcmp (TREE_STRING_POINTER (caller_sec),
				 TREE_STRING_POINTER (callee_sec)) != 0))
		return false;
	    }
	  else if (caller_sec != NULL_TREE)
	    return false;
	}
      return true;
    }

  return false;
})

(define_predicate "move_operand"
  (ior
     (ior (match_operand 0 "register_operand")
	  (match_operand 0 "memory_operand"))
     (ior (and (match_code "const_int")
	       (match_test "GET_MODE_CLASS (mode) == MODE_INT
			    && xtensa_simm12b (INTVAL (op))"))
	  (and (match_code "const_int,const_double,const,symbol_ref,label_ref")
	       (match_test "TARGET_CONST16 && CONSTANT_P (op)
			    && GET_MODE_SIZE (mode) % UNITS_PER_WORD == 0")))))

;; Accept the floating point constant 1 in the appropriate mode.
(define_predicate "const_float_1_operand"
  (match_code "const_double")
{
  REAL_VALUE_TYPE d;
  REAL_VALUE_FROM_CONST_DOUBLE (d, op);
  return REAL_VALUES_EQUAL (d, dconst1);
})

(define_predicate "fpmem_offset_operand"
  (and (match_code "const_int")
       (match_test "xtensa_mem_offset (INTVAL (op), SFmode)")))

(define_predicate "branch_operator"
  (match_code "eq,ne,lt,ge"))

(define_predicate "ubranch_operator"
  (match_code "ltu,geu"))

(define_predicate "boolean_operator"
  (match_code "eq,ne"))

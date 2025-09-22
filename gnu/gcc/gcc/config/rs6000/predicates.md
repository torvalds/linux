;; Predicate definitions for POWER and PowerPC.
;; Copyright (C) 2005, 2006 Free Software Foundation, Inc.
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

;; Return 1 for anything except PARALLEL.
(define_predicate "any_operand"
  (match_code "const_int,const_double,const,symbol_ref,label_ref,subreg,reg,mem"))

;; Return 1 for any PARALLEL.
(define_predicate "any_parallel_operand"
  (match_code "parallel"))

;; Return 1 if op is COUNT register.
(define_predicate "count_register_operand"
  (and (match_code "reg")
       (match_test "REGNO (op) == COUNT_REGISTER_REGNUM
		    || REGNO (op) > LAST_VIRTUAL_REGISTER")))
  
;; Return 1 if op is an Altivec register.
(define_predicate "altivec_register_operand"
   (and (match_operand 0 "register_operand")
	(match_test "GET_CODE (op) != REG
		     || ALTIVEC_REGNO_P (REGNO (op))
		     || REGNO (op) > LAST_VIRTUAL_REGISTER")))

;; Return 1 if op is XER register.
(define_predicate "xer_operand"
  (and (match_code "reg")
       (match_test "XER_REGNO_P (REGNO (op))")))

;; Return 1 if op is a signed 5-bit constant integer.
(define_predicate "s5bit_cint_operand"
  (and (match_code "const_int")
       (match_test "INTVAL (op) >= -16 && INTVAL (op) <= 15")))

;; Return 1 if op is a unsigned 5-bit constant integer.
(define_predicate "u5bit_cint_operand"
  (and (match_code "const_int")
       (match_test "INTVAL (op) >= 0 && INTVAL (op) <= 31")))

;; Return 1 if op is a signed 8-bit constant integer.
;; Integer multiplication complete more quickly
(define_predicate "s8bit_cint_operand"
  (and (match_code "const_int")
       (match_test "INTVAL (op) >= -128 && INTVAL (op) <= 127")))

;; Return 1 if op is a constant integer that can fit in a D field.
(define_predicate "short_cint_operand"
  (and (match_code "const_int")
       (match_test "satisfies_constraint_I (op)")))

;; Return 1 if op is a constant integer that can fit in an unsigned D field.
(define_predicate "u_short_cint_operand"
  (and (match_code "const_int")
       (match_test "satisfies_constraint_K (op)")))

;; Return 1 if op is a constant integer that cannot fit in a signed D field.
(define_predicate "non_short_cint_operand"
  (and (match_code "const_int")
       (match_test "(unsigned HOST_WIDE_INT)
		    (INTVAL (op) + 0x8000) >= 0x10000")))

;; Return 1 if op is a positive constant integer that is an exact power of 2.
(define_predicate "exact_log2_cint_operand"
  (and (match_code "const_int")
       (match_test "INTVAL (op) > 0 && exact_log2 (INTVAL (op)) >= 0")))

;; Return 1 if op is a register that is not special.
(define_predicate "gpc_reg_operand"
   (and (match_operand 0 "register_operand")
	(match_test "(GET_CODE (op) != REG
		      || (REGNO (op) >= ARG_POINTER_REGNUM
			  && !XER_REGNO_P (REGNO (op)))
		      || REGNO (op) < MQ_REGNO)
		     && !((TARGET_E500_DOUBLE || TARGET_SPE)
			  && invalid_e500_subreg (op, mode))")))

;; Return 1 if op is a register that is a condition register field.
(define_predicate "cc_reg_operand"
   (and (match_operand 0 "register_operand")
	(match_test "GET_CODE (op) != REG
		     || REGNO (op) > LAST_VIRTUAL_REGISTER
		     || CR_REGNO_P (REGNO (op))")))

;; Return 1 if op is a register that is a condition register field not cr0.
(define_predicate "cc_reg_not_cr0_operand"
   (and (match_operand 0 "register_operand")
	(match_test "GET_CODE (op) != REG
		     || REGNO (op) > LAST_VIRTUAL_REGISTER
		     || CR_REGNO_NOT_CR0_P (REGNO (op))")))

;; Return 1 if op is a constant integer valid for D field
;; or non-special register register.
(define_predicate "reg_or_short_operand"
  (if_then_else (match_code "const_int")
    (match_operand 0 "short_cint_operand")
    (match_operand 0 "gpc_reg_operand")))

;; Return 1 if op is a constant integer valid whose negation is valid for
;; D field or non-special register register.
;; Do not allow a constant zero because all patterns that call this
;; predicate use "addic r1,r2,-const" to set carry when r2 is greater than
;; or equal to const, which does not work for zero.
(define_predicate "reg_or_neg_short_operand"
  (if_then_else (match_code "const_int")
    (match_test "satisfies_constraint_P (op)
		 && INTVAL (op) != 0")
    (match_operand 0 "gpc_reg_operand")))

;; Return 1 if op is a constant integer valid for DS field
;; or non-special register.
(define_predicate "reg_or_aligned_short_operand"
  (if_then_else (match_code "const_int")
    (and (match_operand 0 "short_cint_operand")
	 (match_test "!(INTVAL (op) & 3)"))
    (match_operand 0 "gpc_reg_operand")))

;; Return 1 if op is a constant integer whose high-order 16 bits are zero
;; or non-special register.
(define_predicate "reg_or_u_short_operand"
  (if_then_else (match_code "const_int")
    (match_operand 0 "u_short_cint_operand")
    (match_operand 0 "gpc_reg_operand")))

;; Return 1 if op is any constant integer 
;; or non-special register.
(define_predicate "reg_or_cint_operand"
  (ior (match_code "const_int")
       (match_operand 0 "gpc_reg_operand")))

;; Return 1 if op is a constant integer valid for addition
;; or non-special register.
(define_predicate "reg_or_add_cint_operand"
  (if_then_else (match_code "const_int")
    (match_test "(HOST_BITS_PER_WIDE_INT == 32
		  && (mode == SImode || INTVAL (op) < 0x7fff8000))
		 || ((unsigned HOST_WIDE_INT) (INTVAL (op) + 0x80008000)
		     < (unsigned HOST_WIDE_INT) 0x100000000ll)")
    (match_operand 0 "gpc_reg_operand")))

;; Return 1 if op is a constant integer valid for subtraction
;; or non-special register.
(define_predicate "reg_or_sub_cint_operand"
  (if_then_else (match_code "const_int")
    (match_test "(HOST_BITS_PER_WIDE_INT == 32
		  && (mode == SImode || - INTVAL (op) < 0x7fff8000))
		 || ((unsigned HOST_WIDE_INT) (- INTVAL (op) 
					       + (mode == SImode
						  ? 0x80000000 : 0x80008000))
		     < (unsigned HOST_WIDE_INT) 0x100000000ll)")
    (match_operand 0 "gpc_reg_operand")))

;; Return 1 if op is any 32-bit unsigned constant integer
;; or non-special register.
(define_predicate "reg_or_logical_cint_operand"
  (if_then_else (match_code "const_int")
    (match_test "(GET_MODE_BITSIZE (mode) > HOST_BITS_PER_WIDE_INT
		  && INTVAL (op) >= 0)
		 || ((INTVAL (op) & GET_MODE_MASK (mode)
		      & (~ (unsigned HOST_WIDE_INT) 0xffffffff)) == 0)")
    (if_then_else (match_code "const_double")
      (match_test "GET_MODE_BITSIZE (mode) > HOST_BITS_PER_WIDE_INT
		   && mode == DImode
		   && CONST_DOUBLE_HIGH (op) == 0")
      (match_operand 0 "gpc_reg_operand"))))

;; Return 1 if operand is a CONST_DOUBLE that can be set in a register
;; with no more than one instruction per word.
(define_predicate "easy_fp_constant"
  (match_code "const_double")
{
  long k[4];
  REAL_VALUE_TYPE rv;

  if (GET_MODE (op) != mode
      || (!SCALAR_FLOAT_MODE_P (mode) && mode != DImode))
    return 0;

  /* Consider all constants with -msoft-float to be easy.  */
  if ((TARGET_SOFT_FLOAT || TARGET_E500_SINGLE)
      && mode != DImode)
    return 1;

  if (DECIMAL_FLOAT_MODE_P (mode))
    return 0;

  /* If we are using V.4 style PIC, consider all constants to be hard.  */
  if (flag_pic && DEFAULT_ABI == ABI_V4)
    return 0;

#ifdef TARGET_RELOCATABLE
  /* Similarly if we are using -mrelocatable, consider all constants
     to be hard.  */
  if (TARGET_RELOCATABLE)
    return 0;
#endif

  switch (mode)
    {
    case TFmode:
      REAL_VALUE_FROM_CONST_DOUBLE (rv, op);
      REAL_VALUE_TO_TARGET_LONG_DOUBLE (rv, k);

      return (num_insns_constant_wide ((HOST_WIDE_INT) k[0]) == 1
	      && num_insns_constant_wide ((HOST_WIDE_INT) k[1]) == 1
	      && num_insns_constant_wide ((HOST_WIDE_INT) k[2]) == 1
	      && num_insns_constant_wide ((HOST_WIDE_INT) k[3]) == 1);

    case DFmode:
      /* Force constants to memory before reload to utilize
	 compress_float_constant.
	 Avoid this when flag_unsafe_math_optimizations is enabled
	 because RDIV division to reciprocal optimization is not able
	 to regenerate the division.  */
      if (TARGET_E500_DOUBLE
          || (!reload_in_progress && !reload_completed
	      && !flag_unsafe_math_optimizations))
        return 0;

      REAL_VALUE_FROM_CONST_DOUBLE (rv, op);
      REAL_VALUE_TO_TARGET_DOUBLE (rv, k);

      return (num_insns_constant_wide ((HOST_WIDE_INT) k[0]) == 1
	      && num_insns_constant_wide ((HOST_WIDE_INT) k[1]) == 1);

    case SFmode:
      /* The constant 0.f is easy.  */
      if (op == CONST0_RTX (SFmode))
	return 1;

      /* Force constants to memory before reload to utilize
	 compress_float_constant.
	 Avoid this when flag_unsafe_math_optimizations is enabled
	 because RDIV division to reciprocal optimization is not able
	 to regenerate the division.  */
      if (!reload_in_progress && !reload_completed
          && !flag_unsafe_math_optimizations)
	return 0;

      REAL_VALUE_FROM_CONST_DOUBLE (rv, op);
      REAL_VALUE_TO_TARGET_SINGLE (rv, k[0]);

      return num_insns_constant_wide (k[0]) == 1;

  case DImode:
    return ((TARGET_POWERPC64
	     && GET_CODE (op) == CONST_DOUBLE && CONST_DOUBLE_LOW (op) == 0)
	    || (num_insns_constant (op, DImode) <= 2));

  case SImode:
    return 1;

  default:
    gcc_unreachable ();
  }
})

;; Return 1 if the operand is a CONST_VECTOR and can be loaded into a
;; vector register without using memory.
(define_predicate "easy_vector_constant"
  (match_code "const_vector")
{
  if (ALTIVEC_VECTOR_MODE (mode))
    {
      if (zero_constant (op, mode))
        return true;
      return easy_altivec_constant (op, mode);
    }

  if (SPE_VECTOR_MODE (mode))
    {
      int cst, cst2;
      if (zero_constant (op, mode))
	return true;
      if (GET_MODE_CLASS (mode) != MODE_VECTOR_INT)
        return false;

      /* Limit SPE vectors to 15 bits signed.  These we can generate with:
	   li r0, CONSTANT1
	   evmergelo r0, r0, r0
	   li r0, CONSTANT2

	 I don't know how efficient it would be to allow bigger constants,
	 considering we'll have an extra 'ori' for every 'li'.  I doubt 5
	 instructions is better than a 64-bit memory load, but I don't
	 have the e500 timing specs.  */
      if (mode == V2SImode)
	{
	  cst  = INTVAL (CONST_VECTOR_ELT (op, 0));
	  cst2 = INTVAL (CONST_VECTOR_ELT (op, 1));
	  return cst  >= -0x7fff && cst <= 0x7fff
	         && cst2 >= -0x7fff && cst2 <= 0x7fff;
	}
    }

  return false;
})

;; Same as easy_vector_constant but only for EASY_VECTOR_15_ADD_SELF.
(define_predicate "easy_vector_constant_add_self"
  (and (match_code "const_vector")
       (and (match_test "TARGET_ALTIVEC")
	    (match_test "easy_altivec_constant (op, mode)")))
{
  rtx last = CONST_VECTOR_ELT (op, GET_MODE_NUNITS (mode) - 1);
  HOST_WIDE_INT val = ((INTVAL (last) & 0xff) ^ 0x80) - 0x80;
  return EASY_VECTOR_15_ADD_SELF (val);
})

;; Return 1 if operand is constant zero (scalars and vectors).
(define_predicate "zero_constant"
  (and (match_code "const_int,const_double,const_vector")
       (match_test "op == CONST0_RTX (mode)")))

;; Return 1 if operand is 0.0.
;; or non-special register register field no cr0
(define_predicate "zero_fp_constant"
  (and (match_code "const_double")
       (match_test "SCALAR_FLOAT_MODE_P (mode)
		    && op == CONST0_RTX (mode)")))

;; Return 1 if the operand is in volatile memory.  Note that during the
;; RTL generation phase, memory_operand does not return TRUE for volatile
;; memory references.  So this function allows us to recognize volatile
;; references where it's safe.
(define_predicate "volatile_mem_operand"
  (and (and (match_code "mem")
	    (match_test "MEM_VOLATILE_P (op)"))
       (if_then_else (match_test "reload_completed")
         (match_operand 0 "memory_operand")
         (if_then_else (match_test "reload_in_progress")
	   (match_test "strict_memory_address_p (mode, XEXP (op, 0))")
	   (match_test "memory_address_p (mode, XEXP (op, 0))")))))

;; Return 1 if the operand is an offsettable memory operand.
(define_predicate "offsettable_mem_operand"
  (and (match_code "mem")
       (match_test "offsettable_address_p (reload_completed
					   || reload_in_progress,
					   mode, XEXP (op, 0))")))

;; Return 1 if the operand is a memory operand with an address divisible by 4
(define_predicate "word_offset_memref_operand"
  (and (match_operand 0 "memory_operand")
       (match_test "GET_CODE (XEXP (op, 0)) != PLUS
		    || ! REG_P (XEXP (XEXP (op, 0), 0)) 
		    || GET_CODE (XEXP (XEXP (op, 0), 1)) != CONST_INT
		    || INTVAL (XEXP (XEXP (op, 0), 1)) % 4 == 0")))

;; Return 1 if the operand is an indexed or indirect memory operand.
(define_predicate "indexed_or_indirect_operand"
  (match_code "mem")
{
  op = XEXP (op, 0);
  if (TARGET_ALTIVEC
      && ALTIVEC_VECTOR_MODE (mode)
      && GET_CODE (op) == AND
      && GET_CODE (XEXP (op, 1)) == CONST_INT
      && INTVAL (XEXP (op, 1)) == -16)
    op = XEXP (op, 0);

  return indexed_or_indirect_address (op, mode);
})

;; Return 1 if the operand is an indexed or indirect address.
(define_special_predicate "indexed_or_indirect_address"
  (and (match_test "REG_P (op)
		    || (GET_CODE (op) == PLUS
			/* Omit testing REG_P (XEXP (op, 0)).  */
			&& REG_P (XEXP (op, 1)))")
       (match_operand 0 "address_operand")))

;; Used for the destination of the fix_truncdfsi2 expander.
;; If stfiwx will be used, the result goes to memory; otherwise,
;; we're going to emit a store and a load of a subreg, so the dest is a
;; register.
(define_predicate "fix_trunc_dest_operand"
  (if_then_else (match_test "! TARGET_E500_DOUBLE && TARGET_PPC_GFXOPT")
   (match_operand 0 "memory_operand")
   (match_operand 0 "gpc_reg_operand")))

;; Return 1 if the operand is either a non-special register or can be used
;; as the operand of a `mode' add insn.
(define_predicate "add_operand"
  (if_then_else (match_code "const_int")
    (match_test "satisfies_constraint_I (op)
		 || satisfies_constraint_L (op)")
    (match_operand 0 "gpc_reg_operand")))

;; Return 1 if OP is a constant but not a valid add_operand.
(define_predicate "non_add_cint_operand"
  (and (match_code "const_int")
       (match_test "!satisfies_constraint_I (op)
		    && !satisfies_constraint_L (op)")))

;; Return 1 if the operand is a constant that can be used as the operand
;; of an OR or XOR.
(define_predicate "logical_const_operand"
  (match_code "const_int,const_double")
{
  HOST_WIDE_INT opl, oph;

  if (GET_CODE (op) == CONST_INT)
    {
      opl = INTVAL (op) & GET_MODE_MASK (mode);

      if (HOST_BITS_PER_WIDE_INT <= 32
	  && GET_MODE_BITSIZE (mode) > HOST_BITS_PER_WIDE_INT && opl < 0)
	return 0;
    }
  else if (GET_CODE (op) == CONST_DOUBLE)
    {
      gcc_assert (GET_MODE_BITSIZE (mode) > HOST_BITS_PER_WIDE_INT);

      opl = CONST_DOUBLE_LOW (op);
      oph = CONST_DOUBLE_HIGH (op);
      if (oph != 0)
	return 0;
    }
  else
    return 0;

  return ((opl & ~ (unsigned HOST_WIDE_INT) 0xffff) == 0
	  || (opl & ~ (unsigned HOST_WIDE_INT) 0xffff0000) == 0);
})

;; Return 1 if the operand is a non-special register or a constant that
;; can be used as the operand of an OR or XOR.
(define_predicate "logical_operand"
  (ior (match_operand 0 "gpc_reg_operand")
       (match_operand 0 "logical_const_operand")))

;; Return 1 if op is a constant that is not a logical operand, but could
;; be split into one.
(define_predicate "non_logical_cint_operand"
  (and (match_code "const_int,const_double")
       (and (not (match_operand 0 "logical_operand"))
	    (match_operand 0 "reg_or_logical_cint_operand"))))

;; Return 1 if op is a constant that can be encoded in a 32-bit mask,
;; suitable for use with rlwinm (no more than two 1->0 or 0->1
;; transitions).  Reject all ones and all zeros, since these should have
;; been optimized away and confuse the making of MB and ME.
(define_predicate "mask_operand"
  (match_code "const_int")
{
  HOST_WIDE_INT c, lsb;

  c = INTVAL (op);

  if (TARGET_POWERPC64)
    {
      /* Fail if the mask is not 32-bit.  */
      if (mode == DImode && (c & ~(unsigned HOST_WIDE_INT) 0xffffffff) != 0)
	return 0;

      /* Fail if the mask wraps around because the upper 32-bits of the
	 mask will all be 1s, contrary to GCC's internal view.  */
      if ((c & 0x80000001) == 0x80000001)
	return 0;
    }

  /* We don't change the number of transitions by inverting,
     so make sure we start with the LS bit zero.  */
  if (c & 1)
    c = ~c;

  /* Reject all zeros or all ones.  */
  if (c == 0)
    return 0;

  /* Find the first transition.  */
  lsb = c & -c;

  /* Invert to look for a second transition.  */
  c = ~c;

  /* Erase first transition.  */
  c &= -lsb;

  /* Find the second transition (if any).  */
  lsb = c & -c;

  /* Match if all the bits above are 1's (or c is zero).  */
  return c == -lsb;
})

;; Return 1 for the PowerPC64 rlwinm corner case.
(define_predicate "mask_operand_wrap"
  (match_code "const_int")
{
  HOST_WIDE_INT c, lsb;

  c = INTVAL (op);

  if ((c & 0x80000001) != 0x80000001)
    return 0;

  c = ~c;
  if (c == 0)
    return 0;

  lsb = c & -c;
  c = ~c;
  c &= -lsb;
  lsb = c & -c;
  return c == -lsb;
})

;; Return 1 if the operand is a constant that is a PowerPC64 mask
;; suitable for use with rldicl or rldicr (no more than one 1->0 or 0->1
;; transition).  Reject all zeros, since zero should have been
;; optimized away and confuses the making of MB and ME.
(define_predicate "mask64_operand"
  (match_code "const_int")
{
  HOST_WIDE_INT c, lsb;

  c = INTVAL (op);

  /* Reject all zeros.  */
  if (c == 0)
    return 0;

  /* We don't change the number of transitions by inverting,
     so make sure we start with the LS bit zero.  */
  if (c & 1)
    c = ~c;

  /* Find the first transition.  */
  lsb = c & -c;

  /* Match if all the bits above are 1's (or c is zero).  */
  return c == -lsb;
})

;; Like mask64_operand, but allow up to three transitions.  This
;; predicate is used by insn patterns that generate two rldicl or
;; rldicr machine insns.
(define_predicate "mask64_2_operand"
  (match_code "const_int")
{
  HOST_WIDE_INT c, lsb;

  c = INTVAL (op);

  /* Disallow all zeros.  */
  if (c == 0)
    return 0;

  /* We don't change the number of transitions by inverting,
     so make sure we start with the LS bit zero.  */
  if (c & 1)
    c = ~c;

  /* Find the first transition.  */
  lsb = c & -c;

  /* Invert to look for a second transition.  */
  c = ~c;

  /* Erase first transition.  */
  c &= -lsb;

  /* Find the second transition.  */
  lsb = c & -c;

  /* Invert to look for a third transition.  */
  c = ~c;

  /* Erase second transition.  */
  c &= -lsb;

  /* Find the third transition (if any).  */
  lsb = c & -c;

  /* Match if all the bits above are 1's (or c is zero).  */
  return c == -lsb;
})

;; Like and_operand, but also match constants that can be implemented
;; with two rldicl or rldicr insns.
(define_predicate "and64_2_operand"
  (ior (match_operand 0 "mask64_2_operand")
       (if_then_else (match_test "fixed_regs[CR0_REGNO]")
	 (match_operand 0 "gpc_reg_operand")
	 (match_operand 0 "logical_operand"))))

;; Return 1 if the operand is either a non-special register or a
;; constant that can be used as the operand of a logical AND.
(define_predicate "and_operand"
  (ior (match_operand 0 "mask_operand")
       (ior (and (match_test "TARGET_POWERPC64 && mode == DImode")
		 (match_operand 0 "mask64_operand"))
            (if_then_else (match_test "fixed_regs[CR0_REGNO]")
	      (match_operand 0 "gpc_reg_operand")
	      (match_operand 0 "logical_operand")))))

;; Return 1 if the operand is either a logical operand or a short cint operand.
(define_predicate "scc_eq_operand"
  (ior (match_operand 0 "logical_operand")
       (match_operand 0 "short_cint_operand")))

;; Return 1 if the operand is a general non-special register or memory operand.
(define_predicate "reg_or_mem_operand"
     (ior (match_operand 0 "memory_operand")
	  (ior (and (match_code "mem")
		    (match_test "macho_lo_sum_memory_operand (op, mode)"))
	       (ior (match_operand 0 "volatile_mem_operand")
		    (match_operand 0 "gpc_reg_operand")))))

;; Return 1 if the operand is either an easy FP constant or memory or reg.
(define_predicate "reg_or_none500mem_operand"
  (if_then_else (match_code "mem")
     (and (match_test "!TARGET_E500_DOUBLE")
	  (ior (match_operand 0 "memory_operand")
	       (ior (match_test "macho_lo_sum_memory_operand (op, mode)")
		    (match_operand 0 "volatile_mem_operand"))))
     (match_operand 0 "gpc_reg_operand")))

;; Return 1 if the operand is CONST_DOUBLE 0, register or memory operand.
(define_predicate "zero_reg_mem_operand"
  (ior (match_operand 0 "zero_fp_constant")
       (match_operand 0 "reg_or_mem_operand")))

;; Return 1 if the operand is a general register or memory operand without
;; pre_inc or pre_dec, which produces invalid form of PowerPC lwa
;; instruction.
(define_predicate "lwa_operand"
  (match_code "reg,subreg,mem")
{
  rtx inner = op;

  if (reload_completed && GET_CODE (inner) == SUBREG)
    inner = SUBREG_REG (inner);

  return gpc_reg_operand (inner, mode)
    || (memory_operand (inner, mode)
	&& GET_CODE (XEXP (inner, 0)) != PRE_INC
	&& GET_CODE (XEXP (inner, 0)) != PRE_DEC
	&& (GET_CODE (XEXP (inner, 0)) != PLUS
	    || GET_CODE (XEXP (XEXP (inner, 0), 1)) != CONST_INT
	    || INTVAL (XEXP (XEXP (inner, 0), 1)) % 4 == 0));
})

;; Return 1 if the operand, used inside a MEM, is a SYMBOL_REF.
(define_predicate "symbol_ref_operand"
  (and (match_code "symbol_ref")
       (match_test "(mode == VOIDmode || GET_MODE (op) == mode)
		    && (DEFAULT_ABI != ABI_AIX || SYMBOL_REF_FUNCTION_P (op))")))

;; Return 1 if op is an operand that can be loaded via the GOT.
;; or non-special register register field no cr0
(define_predicate "got_operand"
  (match_code "symbol_ref,const,label_ref"))

;; Return 1 if op is a simple reference that can be loaded via the GOT,
;; excluding labels involving addition.
(define_predicate "got_no_const_operand"
  (match_code "symbol_ref,label_ref"))

;; Return 1 if op is a SYMBOL_REF for a TLS symbol.
(define_predicate "rs6000_tls_symbol_ref"
  (and (match_code "symbol_ref")
       (match_test "RS6000_SYMBOL_REF_TLS_P (op)")))

;; Return 1 if the operand, used inside a MEM, is a valid first argument
;; to CALL.  This is a SYMBOL_REF, a pseudo-register, LR or CTR.
(define_predicate "call_operand"
  (if_then_else (match_code "reg")
     (match_test "REGNO (op) == LINK_REGISTER_REGNUM
		  || REGNO (op) == COUNT_REGISTER_REGNUM
		  || REGNO (op) >= FIRST_PSEUDO_REGISTER")
     (match_code "symbol_ref")))

;; Return 1 if the operand is a SYMBOL_REF for a function known to be in
;; this file.
(define_predicate "current_file_function_operand"
  (and (match_code "symbol_ref")
       (match_test "(DEFAULT_ABI != ABI_AIX || SYMBOL_REF_FUNCTION_P (op))
		    && ((SYMBOL_REF_LOCAL_P (op)
			 && (DEFAULT_ABI != ABI_AIX
			     || !SYMBOL_REF_EXTERNAL_P (op)))
		        || (op == XEXP (DECL_RTL (current_function_decl),
						  0)))")))

;; Return 1 if this operand is a valid input for a move insn.
(define_predicate "input_operand"
  (match_code "label_ref,symbol_ref,const,high,reg,subreg,mem,
	       const_double,const_vector,const_int,plus")
{
  /* Memory is always valid.  */
  if (memory_operand (op, mode))
    return 1;

  /* For floating-point, easy constants are valid.  */
  if (SCALAR_FLOAT_MODE_P (mode)
      && CONSTANT_P (op)
      && easy_fp_constant (op, mode))
    return 1;

  /* Allow any integer constant.  */
  if (GET_MODE_CLASS (mode) == MODE_INT
      && (GET_CODE (op) == CONST_INT
	  || GET_CODE (op) == CONST_DOUBLE))
    return 1;

  /* Allow easy vector constants.  */
  if (GET_CODE (op) == CONST_VECTOR
      && easy_vector_constant (op, mode))
    return 1;

  /* Do not allow invalid E500 subregs.  */
  if ((TARGET_E500_DOUBLE || TARGET_SPE)
      && GET_CODE (op) == SUBREG
      && invalid_e500_subreg (op, mode))
    return 0;

  /* For floating-point or multi-word mode, the only remaining valid type
     is a register.  */
  if (SCALAR_FLOAT_MODE_P (mode)
      || GET_MODE_SIZE (mode) > UNITS_PER_WORD)
    return register_operand (op, mode);

  /* The only cases left are integral modes one word or smaller (we
     do not get called for MODE_CC values).  These can be in any
     register.  */
  if (register_operand (op, mode))
    return 1;

  /* A SYMBOL_REF referring to the TOC is valid.  */
  if (legitimate_constant_pool_address_p (op))
    return 1;

  /* A constant pool expression (relative to the TOC) is valid */
  if (toc_relative_expr_p (op))
    return 1;

  /* V.4 allows SYMBOL_REFs and CONSTs that are in the small data region
     to be valid.  */
  if (DEFAULT_ABI == ABI_V4
      && (GET_CODE (op) == SYMBOL_REF || GET_CODE (op) == CONST)
      && small_data_operand (op, Pmode))
    return 1;

  return 0;
})

;; Return true if OP is an invalid SUBREG operation on the e500.
(define_predicate "rs6000_nonimmediate_operand"
  (match_code "reg,subreg,mem")
{
  if ((TARGET_E500_DOUBLE || TARGET_SPE)
      && GET_CODE (op) == SUBREG
      && invalid_e500_subreg (op, mode))
    return 0;

  return nonimmediate_operand (op, mode);
})

;; Return true if operand is boolean operator.
(define_predicate "boolean_operator"
  (match_code "and,ior,xor"))

;; Return true if operand is OR-form of boolean operator.
(define_predicate "boolean_or_operator"
  (match_code "ior,xor"))

;; Return true if operand is an equality operator.
(define_special_predicate "equality_operator"
  (match_code "eq,ne"))

;; Return true if operand is MIN or MAX operator.
(define_predicate "min_max_operator"
  (match_code "smin,smax,umin,umax"))

;; Return 1 if OP is a comparison operation that is valid for a branch
;; instruction.  We check the opcode against the mode of the CC value.
;; validate_condition_mode is an assertion.
(define_predicate "branch_comparison_operator"
   (and (match_operand 0 "comparison_operator")
	(and (match_test "GET_MODE_CLASS (GET_MODE (XEXP (op, 0))) == MODE_CC")
	     (match_test "validate_condition_mode (GET_CODE (op),
						   GET_MODE (XEXP (op, 0))),
			  1"))))

;; Return 1 if OP is a comparison operation that is valid for an SCC insn --
;; it must be a positive comparison.
(define_predicate "scc_comparison_operator"
  (and (match_operand 0 "branch_comparison_operator")
       (match_code "eq,lt,gt,ltu,gtu,unordered")))

;; Return 1 if OP is a comparison operation that is valid for a branch
;; insn, which is true if the corresponding bit in the CC register is set.
(define_predicate "branch_positive_comparison_operator"
  (and (match_operand 0 "branch_comparison_operator")
       (match_code "eq,lt,gt,ltu,gtu,unordered")))

;; Return 1 is OP is a comparison operation that is valid for a trap insn.
(define_predicate "trap_comparison_operator"
   (and (match_operand 0 "comparison_operator")
	(match_code "eq,ne,le,lt,ge,gt,leu,ltu,geu,gtu")))

;; Return 1 if OP is a load multiple operation, known to be a PARALLEL.
(define_predicate "load_multiple_operation"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0);
  unsigned int dest_regno;
  rtx src_addr;
  int i;

  /* Perform a quick check so we don't blow up below.  */
  if (count <= 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET
      || GET_CODE (SET_DEST (XVECEXP (op, 0, 0))) != REG
      || GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) != MEM)
    return 0;

  dest_regno = REGNO (SET_DEST (XVECEXP (op, 0, 0)));
  src_addr = XEXP (SET_SRC (XVECEXP (op, 0, 0)), 0);

  for (i = 1; i < count; i++)
    {
      rtx elt = XVECEXP (op, 0, i);

      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_DEST (elt)) != REG
	  || GET_MODE (SET_DEST (elt)) != SImode
	  || REGNO (SET_DEST (elt)) != dest_regno + i
	  || GET_CODE (SET_SRC (elt)) != MEM
	  || GET_MODE (SET_SRC (elt)) != SImode
	  || GET_CODE (XEXP (SET_SRC (elt), 0)) != PLUS
	  || ! rtx_equal_p (XEXP (XEXP (SET_SRC (elt), 0), 0), src_addr)
	  || GET_CODE (XEXP (XEXP (SET_SRC (elt), 0), 1)) != CONST_INT
	  || INTVAL (XEXP (XEXP (SET_SRC (elt), 0), 1)) != i * 4)
	return 0;
    }

  return 1;
})

;; Return 1 if OP is a store multiple operation, known to be a PARALLEL.
;; The second vector element is a CLOBBER.
(define_predicate "store_multiple_operation"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0) - 1;
  unsigned int src_regno;
  rtx dest_addr;
  int i;

  /* Perform a quick check so we don't blow up below.  */
  if (count <= 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET
      || GET_CODE (SET_DEST (XVECEXP (op, 0, 0))) != MEM
      || GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) != REG)
    return 0;

  src_regno = REGNO (SET_SRC (XVECEXP (op, 0, 0)));
  dest_addr = XEXP (SET_DEST (XVECEXP (op, 0, 0)), 0);

  for (i = 1; i < count; i++)
    {
      rtx elt = XVECEXP (op, 0, i + 1);

      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_SRC (elt)) != REG
	  || GET_MODE (SET_SRC (elt)) != SImode
	  || REGNO (SET_SRC (elt)) != src_regno + i
	  || GET_CODE (SET_DEST (elt)) != MEM
	  || GET_MODE (SET_DEST (elt)) != SImode
	  || GET_CODE (XEXP (SET_DEST (elt), 0)) != PLUS
	  || ! rtx_equal_p (XEXP (XEXP (SET_DEST (elt), 0), 0), dest_addr)
	  || GET_CODE (XEXP (XEXP (SET_DEST (elt), 0), 1)) != CONST_INT
	  || INTVAL (XEXP (XEXP (SET_DEST (elt), 0), 1)) != i * 4)
	return 0;
    }

  return 1;
})

;; Return 1 if OP is valid for a save_world call in prologue, known to be
;; a PARLLEL.
(define_predicate "save_world_operation"
  (match_code "parallel")
{
  int index;
  int i;
  rtx elt;
  int count = XVECLEN (op, 0);

  if (count != 55)
    return 0;

  index = 0;
  if (GET_CODE (XVECEXP (op, 0, index++)) != CLOBBER
      || GET_CODE (XVECEXP (op, 0, index++)) != USE)
    return 0;

  for (i=1; i <= 18; i++)
    {
      elt = XVECEXP (op, 0, index++);
      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_DEST (elt)) != MEM
	  || ! memory_operand (SET_DEST (elt), DFmode)
	  || GET_CODE (SET_SRC (elt)) != REG
	  || GET_MODE (SET_SRC (elt)) != DFmode)
	return 0;
    }

  for (i=1; i <= 12; i++)
    {
      elt = XVECEXP (op, 0, index++);
      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_DEST (elt)) != MEM
	  || GET_CODE (SET_SRC (elt)) != REG
	  || GET_MODE (SET_SRC (elt)) != V4SImode)
	return 0;
    }

  for (i=1; i <= 19; i++)
    {
      elt = XVECEXP (op, 0, index++);
      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_DEST (elt)) != MEM
	  || ! memory_operand (SET_DEST (elt), Pmode)
	  || GET_CODE (SET_SRC (elt)) != REG
	  || GET_MODE (SET_SRC (elt)) != Pmode)
	return 0;
    }

  elt = XVECEXP (op, 0, index++);
  if (GET_CODE (elt) != SET
      || GET_CODE (SET_DEST (elt)) != MEM
      || ! memory_operand (SET_DEST (elt), Pmode)
      || GET_CODE (SET_SRC (elt)) != REG
      || REGNO (SET_SRC (elt)) != CR2_REGNO
      || GET_MODE (SET_SRC (elt)) != Pmode)
    return 0;

  if (GET_CODE (XVECEXP (op, 0, index++)) != USE
      || GET_CODE (XVECEXP (op, 0, index++)) != USE
      || GET_CODE (XVECEXP (op, 0, index++)) != CLOBBER)
    return 0;
  return 1;
})

;; Return 1 if OP is valid for a restore_world call in epilogue, known to be
;; a PARLLEL.
(define_predicate "restore_world_operation"
  (match_code "parallel")
{
  int index;
  int i;
  rtx elt;
  int count = XVECLEN (op, 0);

  if (count != 59)
    return 0;

  index = 0;
  if (GET_CODE (XVECEXP (op, 0, index++)) != RETURN
      || GET_CODE (XVECEXP (op, 0, index++)) != USE
      || GET_CODE (XVECEXP (op, 0, index++)) != USE
      || GET_CODE (XVECEXP (op, 0, index++)) != CLOBBER)
    return 0;

  elt = XVECEXP (op, 0, index++);
  if (GET_CODE (elt) != SET
      || GET_CODE (SET_SRC (elt)) != MEM
      || ! memory_operand (SET_SRC (elt), Pmode)
      || GET_CODE (SET_DEST (elt)) != REG
      || REGNO (SET_DEST (elt)) != CR2_REGNO
      || GET_MODE (SET_DEST (elt)) != Pmode)
    return 0;

  for (i=1; i <= 19; i++)
    {
      elt = XVECEXP (op, 0, index++);
      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_SRC (elt)) != MEM
	  || ! memory_operand (SET_SRC (elt), Pmode)
	  || GET_CODE (SET_DEST (elt)) != REG
	  || GET_MODE (SET_DEST (elt)) != Pmode)
	return 0;
    }

  for (i=1; i <= 12; i++)
    {
      elt = XVECEXP (op, 0, index++);
      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_SRC (elt)) != MEM
	  || GET_CODE (SET_DEST (elt)) != REG
	  || GET_MODE (SET_DEST (elt)) != V4SImode)
	return 0;
    }

  for (i=1; i <= 18; i++)
    {
      elt = XVECEXP (op, 0, index++);
      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_SRC (elt)) != MEM
	  || ! memory_operand (SET_SRC (elt), DFmode)
	  || GET_CODE (SET_DEST (elt)) != REG
	  || GET_MODE (SET_DEST (elt)) != DFmode)
	return 0;
    }

  if (GET_CODE (XVECEXP (op, 0, index++)) != CLOBBER
      || GET_CODE (XVECEXP (op, 0, index++)) != CLOBBER
      || GET_CODE (XVECEXP (op, 0, index++)) != CLOBBER
      || GET_CODE (XVECEXP (op, 0, index++)) != CLOBBER
      || GET_CODE (XVECEXP (op, 0, index++)) != USE)
    return 0;
  return 1;
})

;; Return 1 if OP is valid for a vrsave call, known to be a PARALLEL.
(define_predicate "vrsave_operation"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0);
  unsigned int dest_regno, src_regno;
  int i;

  if (count <= 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET
      || GET_CODE (SET_DEST (XVECEXP (op, 0, 0))) != REG
      || GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) != UNSPEC_VOLATILE
      || XINT (SET_SRC (XVECEXP (op, 0, 0)), 1) != UNSPECV_SET_VRSAVE)
    return 0;

  dest_regno = REGNO (SET_DEST (XVECEXP (op, 0, 0)));
  src_regno  = REGNO (XVECEXP (SET_SRC (XVECEXP (op, 0, 0)), 0, 1));

  if (dest_regno != VRSAVE_REGNO || src_regno != VRSAVE_REGNO)
    return 0;

  for (i = 1; i < count; i++)
    {
      rtx elt = XVECEXP (op, 0, i);

      if (GET_CODE (elt) != CLOBBER
	  && GET_CODE (elt) != SET)
	return 0;
    }

  return 1;
})

;; Return 1 if OP is valid for mfcr insn, known to be a PARALLEL.
(define_predicate "mfcr_operation"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0);
  int i;

  /* Perform a quick check so we don't blow up below.  */
  if (count < 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET
      || GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) != UNSPEC
      || XVECLEN (SET_SRC (XVECEXP (op, 0, 0)), 0) != 2)
    return 0;

  for (i = 0; i < count; i++)
    {
      rtx exp = XVECEXP (op, 0, i);
      rtx unspec;
      int maskval;
      rtx src_reg;

      src_reg = XVECEXP (SET_SRC (exp), 0, 0);

      if (GET_CODE (src_reg) != REG
	  || GET_MODE (src_reg) != CCmode
	  || ! CR_REGNO_P (REGNO (src_reg)))
	return 0;

      if (GET_CODE (exp) != SET
	  || GET_CODE (SET_DEST (exp)) != REG
	  || GET_MODE (SET_DEST (exp)) != SImode
	  || ! INT_REGNO_P (REGNO (SET_DEST (exp))))
	return 0;
      unspec = SET_SRC (exp);
      maskval = 1 << (MAX_CR_REGNO - REGNO (src_reg));

      if (GET_CODE (unspec) != UNSPEC
	  || XINT (unspec, 1) != UNSPEC_MOVESI_FROM_CR
	  || XVECLEN (unspec, 0) != 2
	  || XVECEXP (unspec, 0, 0) != src_reg
	  || GET_CODE (XVECEXP (unspec, 0, 1)) != CONST_INT
	  || INTVAL (XVECEXP (unspec, 0, 1)) != maskval)
	return 0;
    }
  return 1;
})

;; Return 1 if OP is valid for mtcrf insn, known to be a PARALLEL.
(define_predicate "mtcrf_operation"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0);
  int i;
  rtx src_reg;

  /* Perform a quick check so we don't blow up below.  */
  if (count < 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET
      || GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) != UNSPEC
      || XVECLEN (SET_SRC (XVECEXP (op, 0, 0)), 0) != 2)
    return 0;
  src_reg = XVECEXP (SET_SRC (XVECEXP (op, 0, 0)), 0, 0);

  if (GET_CODE (src_reg) != REG
      || GET_MODE (src_reg) != SImode
      || ! INT_REGNO_P (REGNO (src_reg)))
    return 0;

  for (i = 0; i < count; i++)
    {
      rtx exp = XVECEXP (op, 0, i);
      rtx unspec;
      int maskval;

      if (GET_CODE (exp) != SET
	  || GET_CODE (SET_DEST (exp)) != REG
	  || GET_MODE (SET_DEST (exp)) != CCmode
	  || ! CR_REGNO_P (REGNO (SET_DEST (exp))))
	return 0;
      unspec = SET_SRC (exp);
      maskval = 1 << (MAX_CR_REGNO - REGNO (SET_DEST (exp)));

      if (GET_CODE (unspec) != UNSPEC
	  || XINT (unspec, 1) != UNSPEC_MOVESI_TO_CR
	  || XVECLEN (unspec, 0) != 2
	  || XVECEXP (unspec, 0, 0) != src_reg
	  || GET_CODE (XVECEXP (unspec, 0, 1)) != CONST_INT
	  || INTVAL (XVECEXP (unspec, 0, 1)) != maskval)
	return 0;
    }
  return 1;
})

;; Return 1 if OP is valid for lmw insn, known to be a PARALLEL.
(define_predicate "lmw_operation"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0);
  unsigned int dest_regno;
  rtx src_addr;
  unsigned int base_regno;
  HOST_WIDE_INT offset;
  int i;

  /* Perform a quick check so we don't blow up below.  */
  if (count <= 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET
      || GET_CODE (SET_DEST (XVECEXP (op, 0, 0))) != REG
      || GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) != MEM)
    return 0;

  dest_regno = REGNO (SET_DEST (XVECEXP (op, 0, 0)));
  src_addr = XEXP (SET_SRC (XVECEXP (op, 0, 0)), 0);

  if (dest_regno > 31
      || count != 32 - (int) dest_regno)
    return 0;

  if (legitimate_indirect_address_p (src_addr, 0))
    {
      offset = 0;
      base_regno = REGNO (src_addr);
      if (base_regno == 0)
	return 0;
    }
  else if (rs6000_legitimate_offset_address_p (SImode, src_addr, 0))
    {
      offset = INTVAL (XEXP (src_addr, 1));
      base_regno = REGNO (XEXP (src_addr, 0));
    }
  else
    return 0;

  for (i = 0; i < count; i++)
    {
      rtx elt = XVECEXP (op, 0, i);
      rtx newaddr;
      rtx addr_reg;
      HOST_WIDE_INT newoffset;

      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_DEST (elt)) != REG
	  || GET_MODE (SET_DEST (elt)) != SImode
	  || REGNO (SET_DEST (elt)) != dest_regno + i
	  || GET_CODE (SET_SRC (elt)) != MEM
	  || GET_MODE (SET_SRC (elt)) != SImode)
	return 0;
      newaddr = XEXP (SET_SRC (elt), 0);
      if (legitimate_indirect_address_p (newaddr, 0))
	{
	  newoffset = 0;
	  addr_reg = newaddr;
	}
      else if (rs6000_legitimate_offset_address_p (SImode, newaddr, 0))
	{
	  addr_reg = XEXP (newaddr, 0);
	  newoffset = INTVAL (XEXP (newaddr, 1));
	}
      else
	return 0;
      if (REGNO (addr_reg) != base_regno
	  || newoffset != offset + 4 * i)
	return 0;
    }

  return 1;
})

;; Return 1 if OP is valid for stmw insn, known to be a PARALLEL.
(define_predicate "stmw_operation"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0);
  unsigned int src_regno;
  rtx dest_addr;
  unsigned int base_regno;
  HOST_WIDE_INT offset;
  int i;

  /* Perform a quick check so we don't blow up below.  */
  if (count <= 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET
      || GET_CODE (SET_DEST (XVECEXP (op, 0, 0))) != MEM
      || GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) != REG)
    return 0;

  src_regno = REGNO (SET_SRC (XVECEXP (op, 0, 0)));
  dest_addr = XEXP (SET_DEST (XVECEXP (op, 0, 0)), 0);

  if (src_regno > 31
      || count != 32 - (int) src_regno)
    return 0;

  if (legitimate_indirect_address_p (dest_addr, 0))
    {
      offset = 0;
      base_regno = REGNO (dest_addr);
      if (base_regno == 0)
	return 0;
    }
  else if (rs6000_legitimate_offset_address_p (SImode, dest_addr, 0))
    {
      offset = INTVAL (XEXP (dest_addr, 1));
      base_regno = REGNO (XEXP (dest_addr, 0));
    }
  else
    return 0;

  for (i = 0; i < count; i++)
    {
      rtx elt = XVECEXP (op, 0, i);
      rtx newaddr;
      rtx addr_reg;
      HOST_WIDE_INT newoffset;

      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_SRC (elt)) != REG
	  || GET_MODE (SET_SRC (elt)) != SImode
	  || REGNO (SET_SRC (elt)) != src_regno + i
	  || GET_CODE (SET_DEST (elt)) != MEM
	  || GET_MODE (SET_DEST (elt)) != SImode)
	return 0;
      newaddr = XEXP (SET_DEST (elt), 0);
      if (legitimate_indirect_address_p (newaddr, 0))
	{
	  newoffset = 0;
	  addr_reg = newaddr;
	}
      else if (rs6000_legitimate_offset_address_p (SImode, newaddr, 0))
	{
	  addr_reg = XEXP (newaddr, 0);
	  newoffset = INTVAL (XEXP (newaddr, 1));
	}
      else
	return 0;
      if (REGNO (addr_reg) != base_regno
	  || newoffset != offset + 4 * i)
	return 0;
    }

  return 1;
})

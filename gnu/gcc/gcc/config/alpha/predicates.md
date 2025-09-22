;; Predicate definitions for DEC Alpha.
;; Copyright (C) 2004, 2005, 2006 Free Software Foundation, Inc.
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

;; Return 1 if OP is the zero constant for MODE.
(define_predicate "const0_operand"
  (and (match_code "const_int,const_double,const_vector")
       (match_test "op == CONST0_RTX (mode)")))

;; Returns true if OP is either the constant zero or a register.
(define_predicate "reg_or_0_operand"
  (ior (match_operand 0 "register_operand")
       (match_operand 0 "const0_operand")))

;; Return 1 if OP is a constant in the range of 0-63 (for a shift) or
;; any register.
(define_predicate "reg_or_6bit_operand"
  (if_then_else (match_code "const_int")
    (match_test "INTVAL (op) >= 0 && INTVAL (op) < 64")
    (match_operand 0 "register_operand")))

;; Return 1 if OP is an 8-bit constant.
(define_predicate "cint8_operand"
  (and (match_code "const_int")
       (match_test "INTVAL (op) >= 0 && INTVAL (op) < 256")))

;; Return 1 if OP is an 8-bit constant or any register.
(define_predicate "reg_or_8bit_operand"
  (if_then_else (match_code "const_int")
    (match_test "INTVAL (op) >= 0 && INTVAL (op) < 256")
    (match_operand 0 "register_operand")))

;; Return 1 if OP is a constant or any register.
(define_predicate "reg_or_cint_operand"
  (ior (match_operand 0 "register_operand")
       (match_operand 0 "const_int_operand")))

;; Return 1 if the operand is a valid second operand to an add insn.
(define_predicate "add_operand"
  (if_then_else (match_code "const_int")
    (match_test "CONST_OK_FOR_LETTER_P (INTVAL (op), 'K')
		 || CONST_OK_FOR_LETTER_P (INTVAL (op), 'L')")
    (match_operand 0 "register_operand")))

;; Return 1 if the operand is a valid second operand to a
;; sign-extending add insn.
(define_predicate "sext_add_operand"
  (if_then_else (match_code "const_int")
    (match_test "CONST_OK_FOR_LETTER_P (INTVAL (op), 'I')
		 || CONST_OK_FOR_LETTER_P (INTVAL (op), 'O')")
    (match_operand 0 "register_operand")))

;; Return 1 if the operand is a non-symbolic constant operand that
;; does not satisfy add_operand.
(define_predicate "non_add_const_operand"
  (and (match_code "const_int,const_double,const_vector")
       (not (match_operand 0 "add_operand"))))

;; Return 1 if the operand is a non-symbolic, nonzero constant operand.
(define_predicate "non_zero_const_operand"
  (and (match_code "const_int,const_double,const_vector")
       (match_test "op != CONST0_RTX (mode)")))

;; Return 1 if OP is the constant 4 or 8.
(define_predicate "const48_operand"
  (and (match_code "const_int")
       (match_test "INTVAL (op) == 4 || INTVAL (op) == 8")))

;; Return 1 if OP is a valid first operand to an AND insn.
(define_predicate "and_operand"
  (if_then_else (match_code "const_int")
    (match_test "(unsigned HOST_WIDE_INT) INTVAL (op) < 0x100
		 || (unsigned HOST_WIDE_INT) ~ INTVAL (op) < 0x100
		 || zap_mask (INTVAL (op))")
    (if_then_else (match_code "const_double")
      (match_test "GET_MODE (op) == VOIDmode
		   && zap_mask (CONST_DOUBLE_LOW (op))
		   && zap_mask (CONST_DOUBLE_HIGH (op))")
      (match_operand 0 "register_operand"))))

;; Return 1 if OP is a valid first operand to an IOR or XOR insn.
(define_predicate "or_operand"
  (if_then_else (match_code "const_int")
    (match_test "(unsigned HOST_WIDE_INT) INTVAL (op) < 0x100
		 || (unsigned HOST_WIDE_INT) ~ INTVAL (op) < 0x100")
    (match_operand 0 "register_operand")))

;; Return 1 if OP is a constant that is the width, in bits, of an integral
;; mode not larger than DImode.
(define_predicate "mode_width_operand"
  (match_code "const_int")
{
  HOST_WIDE_INT i = INTVAL (op);
  return i == 8 || i == 16 || i == 32 || i == 64;
})

;; Return 1 if OP is a constant that is a mask of ones of width of an
;; integral machine mode not larger than DImode.
(define_predicate "mode_mask_operand"
  (match_code "const_int,const_double")
{
  if (GET_CODE (op) == CONST_INT)
    {
      HOST_WIDE_INT value = INTVAL (op);

      if (value == 0xff)
	return 1;
      if (value == 0xffff)
	return 1;
      if (value == 0xffffffff)
	return 1;
      if (value == -1)
	return 1;
    }
  else if (HOST_BITS_PER_WIDE_INT == 32 && GET_CODE (op) == CONST_DOUBLE)
    {
      if (CONST_DOUBLE_LOW (op) == 0xffffffff && CONST_DOUBLE_HIGH (op) == 0)
	return 1;
    }
  return 0;
})

;; Return 1 if OP is a multiple of 8 less than 64.
(define_predicate "mul8_operand"
  (match_code "const_int")
{
  unsigned HOST_WIDE_INT i = INTVAL (op);
  return i < 64 && i % 8 == 0;
})

;; Return 1 if OP is a hard floating-point register.
(define_predicate "hard_fp_register_operand"
  (match_operand 0 "register_operand")
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  return REGNO_REG_CLASS (REGNO (op)) == FLOAT_REGS;
})

;; Return 1 if OP is a hard general register.
(define_predicate "hard_int_register_operand"
  (match_operand 0 "register_operand")
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  return REGNO_REG_CLASS (REGNO (op)) == GENERAL_REGS;
})

;; Return 1 if OP is something that can be reloaded into a register;
;; if it is a MEM, it need not be valid.
(define_predicate "some_operand"
  (ior (match_code "reg,mem,const_int,const_double,const_vector,
		    label_ref,symbol_ref,const,high")
       (and (match_code "subreg")
	    (match_test "some_operand (SUBREG_REG (op), VOIDmode)"))))

;; Likewise, but don't accept constants.
(define_predicate "some_ni_operand"
  (ior (match_code "reg,mem")
       (and (match_code "subreg")
	    (match_test "some_ni_operand (SUBREG_REG (op), VOIDmode)"))))

;; Return 1 if OP is a valid operand for the source of a move insn.
(define_predicate "input_operand"
  (match_code "label_ref,symbol_ref,const,high,reg,subreg,mem,
	       const_double,const_vector,const_int")
{
  switch (GET_CODE (op))
    {
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST:
      if (TARGET_EXPLICIT_RELOCS)
	{
	  /* We don't split symbolic operands into something unintelligable
	     until after reload, but we do not wish non-small, non-global
	     symbolic operands to be reconstructed from their high/lo_sum
	     form.  */
	  return (small_symbolic_operand (op, mode)
		  || global_symbolic_operand (op, mode)
		  || gotdtp_symbolic_operand (op, mode)
		  || gottp_symbolic_operand (op, mode));
	}

      /* This handles both the Windows/NT and OSF cases.  */
      return mode == ptr_mode || mode == DImode;

    case HIGH:
      return (TARGET_EXPLICIT_RELOCS
	      && local_symbolic_operand (XEXP (op, 0), mode));

    case REG:
      return 1;

    case SUBREG:
      if (register_operand (op, mode))
	return 1;
      /* ... fall through ...  */
    case MEM:
      return ((TARGET_BWX || (mode != HImode && mode != QImode))
	      && general_operand (op, mode));

    case CONST_DOUBLE:
      return op == CONST0_RTX (mode);

    case CONST_VECTOR:
      if (reload_in_progress || reload_completed)
	return alpha_legitimate_constant_p (op);
      return op == CONST0_RTX (mode);

    case CONST_INT:
      if (mode == QImode || mode == HImode)
	return true;
      if (reload_in_progress || reload_completed)
	return alpha_legitimate_constant_p (op);
      return add_operand (op, mode);

    default:
      gcc_unreachable ();
    }
  return 0;
})

;; Return 1 if OP is a SYMBOL_REF for a function known to be in this
;; file, and in the same section as the current function.

(define_predicate "samegp_function_operand"
  (match_code "symbol_ref")
{
  /* Easy test for recursion.  */
  if (op == XEXP (DECL_RTL (current_function_decl), 0))
    return true;

  /* Functions that are not local can be overridden, and thus may
     not share the same gp.  */
  if (! SYMBOL_REF_LOCAL_P (op))
    return false;

  /* If -msmall-data is in effect, assume that there is only one GP
     for the module, and so any local symbol has this property.  We
     need explicit relocations to be able to enforce this for symbols
     not defined in this unit of translation, however.  */
  if (TARGET_EXPLICIT_RELOCS && TARGET_SMALL_DATA)
    return true;

  /* Functions that are not external are defined in this UoT,
     and thus must share the same gp.  */
  return ! SYMBOL_REF_EXTERNAL_P (op);
})

;; Return 1 if OP is a SYMBOL_REF for which we can make a call via bsr.
(define_predicate "direct_call_operand"
  (match_operand 0 "samegp_function_operand")
{
  tree op_decl, cfun_sec, op_sec;

  /* If profiling is implemented via linker tricks, we can't jump
     to the nogp alternate entry point.  Note that current_function_profile
     would not be correct, since that doesn't indicate if the target
     function uses profiling.  */
  /* ??? TARGET_PROFILING_NEEDS_GP isn't really the right test,
     but is approximately correct for the OSF ABIs.  Don't know
     what to do for VMS, NT, or UMK.  */
  if (!TARGET_PROFILING_NEEDS_GP && profile_flag)
    return false;

  /* Must be a function.  In some cases folks create thunks in static
     data structures and then make calls to them.  If we allow the
     direct call, we'll get an error from the linker about !samegp reloc
     against a symbol without a .prologue directive.  */
  if (!SYMBOL_REF_FUNCTION_P (op))
    return false;
  
  /* Must be "near" so that the branch is assumed to reach.  With
     -msmall-text, this is assumed true of all local symbols.  Since
     we've already checked samegp, locality is already assured.  */
  if (TARGET_SMALL_TEXT)
    return true;

  /* Otherwise, a decl is "near" if it is defined in the same section.  */
  if (flag_function_sections)
    return false;

  op_decl = SYMBOL_REF_DECL (op);
  if (DECL_ONE_ONLY (current_function_decl)
      || (op_decl && DECL_ONE_ONLY (op_decl)))
    return false;

  cfun_sec = DECL_SECTION_NAME (current_function_decl);
  op_sec = op_decl ? DECL_SECTION_NAME (op_decl) : NULL;
  return ((!cfun_sec && !op_sec)
	  || (cfun_sec && op_sec
	      && strcmp (TREE_STRING_POINTER (cfun_sec),
		         TREE_STRING_POINTER (op_sec)) == 0));
})

;; Return 1 if OP is a valid operand for the MEM of a CALL insn.
;;
;; For TARGET_ABI_OSF, we want to restrict to R27 or a pseudo.
;; For TARGET_ABI_UNICOSMK, we want to restrict to registers.

(define_predicate "call_operand"
  (if_then_else (match_code "reg")
    (match_test "!TARGET_ABI_OSF
		 || REGNO (op) == 27 || REGNO (op) > LAST_VIRTUAL_REGISTER")
    (and (match_test "!TARGET_ABI_UNICOSMK")
	 (match_code "symbol_ref"))))

;; Return true if OP is a LABEL_REF, or SYMBOL_REF or CONST referencing
;; a (non-tls) variable known to be defined in this file.
(define_predicate "local_symbolic_operand"
  (match_code "label_ref,const,symbol_ref")
{
  if (GET_CODE (op) == LABEL_REF)
    return 1;

  if (GET_CODE (op) == CONST
      && GET_CODE (XEXP (op, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (op, 0), 1)) == CONST_INT)
    op = XEXP (XEXP (op, 0), 0);

  if (GET_CODE (op) != SYMBOL_REF)
    return 0;

  return (SYMBOL_REF_LOCAL_P (op)
	  && !SYMBOL_REF_WEAK (op)
	  && !SYMBOL_REF_TLS_MODEL (op));
})

;; Return true if OP is a SYMBOL_REF or CONST referencing a variable
;; known to be defined in this file in the small data area.
(define_predicate "small_symbolic_operand"
  (match_code "const,symbol_ref")
{
  if (! TARGET_SMALL_DATA)
    return 0;

  if (GET_CODE (op) == CONST
      && GET_CODE (XEXP (op, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (op, 0), 1)) == CONST_INT)
    op = XEXP (XEXP (op, 0), 0);

  if (GET_CODE (op) != SYMBOL_REF)
    return 0;

  /* ??? There's no encode_section_info equivalent for the rtl
     constant pool, so SYMBOL_FLAG_SMALL never gets set.  */
  if (CONSTANT_POOL_ADDRESS_P (op))
    return GET_MODE_SIZE (get_pool_mode (op)) <= g_switch_value;

  return (SYMBOL_REF_LOCAL_P (op)
	  && SYMBOL_REF_SMALL_P (op)
	  && !SYMBOL_REF_WEAK (op)
	  && !SYMBOL_REF_TLS_MODEL (op));
})

;; Return true if OP is a SYMBOL_REF or CONST referencing a variable
;; not known (or known not) to be defined in this file.
(define_predicate "global_symbolic_operand"
  (match_code "const,symbol_ref")
{
  if (GET_CODE (op) == CONST
      && GET_CODE (XEXP (op, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (op, 0), 1)) == CONST_INT)
    op = XEXP (XEXP (op, 0), 0);

  if (GET_CODE (op) != SYMBOL_REF)
    return 0;

  return ((!SYMBOL_REF_LOCAL_P (op) || SYMBOL_REF_WEAK (op))
	  && !SYMBOL_REF_TLS_MODEL (op));
})

;; Returns 1 if OP is a symbolic operand, i.e. a symbol_ref or a label_ref,
;; possibly with an offset.
(define_predicate "symbolic_operand"
  (ior (match_code "symbol_ref,label_ref")
       (and (match_code "const")
	    (match_test "GET_CODE (XEXP (op,0)) == PLUS
			 && GET_CODE (XEXP (XEXP (op,0), 0)) == SYMBOL_REF
			 && GET_CODE (XEXP (XEXP (op,0), 1)) == CONST_INT"))))

;; Return true if OP is valid for 16-bit DTP relative relocations.
(define_predicate "dtp16_symbolic_operand"
  (and (match_code "const")
       (match_test "tls_symbolic_operand_1 (op, 16, UNSPEC_DTPREL)")))

;; Return true if OP is valid for 32-bit DTP relative relocations.
(define_predicate "dtp32_symbolic_operand"
  (and (match_code "const")
       (match_test "tls_symbolic_operand_1 (op, 32, UNSPEC_DTPREL)")))

;; Return true if OP is valid for 64-bit DTP relative relocations.
(define_predicate "gotdtp_symbolic_operand"
  (and (match_code "const")
       (match_test "tls_symbolic_operand_1 (op, 64, UNSPEC_DTPREL)")))

;; Return true if OP is valid for 16-bit TP relative relocations.
(define_predicate "tp16_symbolic_operand"
  (and (match_code "const")
       (match_test "tls_symbolic_operand_1 (op, 16, UNSPEC_TPREL)")))

;; Return true if OP is valid for 32-bit TP relative relocations.
(define_predicate "tp32_symbolic_operand"
  (and (match_code "const")
       (match_test "tls_symbolic_operand_1 (op, 32, UNSPEC_TPREL)")))

;; Return true if OP is valid for 64-bit TP relative relocations.
(define_predicate "gottp_symbolic_operand"
  (and (match_code "const")
       (match_test "tls_symbolic_operand_1 (op, 64, UNSPEC_TPREL)")))

;; Return 1 if this memory address is a known aligned register plus
;; a constant.  It must be a valid address.  This means that we can do
;; this as an aligned reference plus some offset.
;;
;; Take into account what reload will do.  Oh god this is awful.
;; The horrible comma-operator construct below is to prevent genrecog
;; from thinking that this predicate accepts REG and SUBREG.  We don't
;; use recog during reload, so pretending these codes are accepted 
;; pessimizes things a tad.

(define_predicate "aligned_memory_operand"
  (ior (match_test "op = resolve_reload_operand (op), 0")
       (match_code "mem"))
{
  rtx base;

  if (MEM_ALIGN (op) >= 32)
    return 1;
  op = XEXP (op, 0);

  /* LEGITIMIZE_RELOAD_ADDRESS creates (plus (plus reg const_hi) const_lo)
     sorts of constructs.  Dig for the real base register.  */
  if (reload_in_progress
      && GET_CODE (op) == PLUS
      && GET_CODE (XEXP (op, 0)) == PLUS)
    base = XEXP (XEXP (op, 0), 0);
  else
    {
      if (! memory_address_p (mode, op))
	return 0;
      base = (GET_CODE (op) == PLUS ? XEXP (op, 0) : op);
    }

  return (GET_CODE (base) == REG && REGNO_POINTER_ALIGN (REGNO (base)) >= 32);
})

;; Similar, but return 1 if OP is a MEM which is not alignable.

(define_predicate "unaligned_memory_operand"
  (ior (match_test "op = resolve_reload_operand (op), 0")
       (match_code "mem"))
{
  rtx base;

  if (MEM_ALIGN (op) >= 32)
    return 0;
  op = XEXP (op, 0);

  /* LEGITIMIZE_RELOAD_ADDRESS creates (plus (plus reg const_hi) const_lo)
     sorts of constructs.  Dig for the real base register.  */
  if (reload_in_progress
      && GET_CODE (op) == PLUS
      && GET_CODE (XEXP (op, 0)) == PLUS)
    base = XEXP (XEXP (op, 0), 0);
  else
    {
      if (! memory_address_p (mode, op))
	return 0;
      base = (GET_CODE (op) == PLUS ? XEXP (op, 0) : op);
    }

  return (GET_CODE (base) == REG && REGNO_POINTER_ALIGN (REGNO (base)) < 32);
})

;; Return 1 if OP is any memory location.  During reload a pseudo matches.
(define_predicate "any_memory_operand"
  (ior (match_code "mem,reg")
       (and (match_code "subreg")
	    (match_test "GET_CODE (SUBREG_REG (op)) == REG"))))

;; Return 1 if OP is either a register or an unaligned memory location.
(define_predicate "reg_or_unaligned_mem_operand"
  (ior (match_operand 0 "register_operand")
       (match_operand 0 "unaligned_memory_operand")))

;; Return 1 is OP is a memory location that is not a reference
;; (using an AND) to an unaligned location.  Take into account
;; what reload will do.
(define_predicate "normal_memory_operand"
  (ior (match_test "op = resolve_reload_operand (op), 0")
       (and (match_code "mem")
	    (match_test "GET_CODE (XEXP (op, 0)) != AND"))))

;; Returns 1 if OP is not an eliminable register.
;;
;; This exists to cure a pathological failure in the s8addq (et al) patterns,
;;
;;	long foo () { long t; bar(); return (long) &t * 26107; }
;;
;; which run afoul of a hack in reload to cure a (presumably) similar
;; problem with lea-type instructions on other targets.  But there is
;; one of us and many of them, so work around the problem by selectively
;; preventing combine from making the optimization.

(define_predicate "reg_not_elim_operand"
  (match_operand 0 "register_operand")
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  return op != frame_pointer_rtx && op != arg_pointer_rtx;
})

;; Accept a register, but not a subreg of any kind.  This allows us to
;; avoid pathological cases in reload wrt data movement common in 
;; int->fp conversion.  */
(define_predicate "reg_no_subreg_operand"
  (and (match_code "reg")
       (match_operand 0 "register_operand")))

;; Return 1 if OP is a valid Alpha comparison operator for "cmp" style
;; instructions.
(define_predicate "alpha_comparison_operator"
  (match_code "eq,le,lt,leu,ltu"))

;; Similarly, but with swapped operands.
(define_predicate "alpha_swapped_comparison_operator"
  (match_code "eq,ge,gt,gtu"))

;; Return 1 if OP is a valid Alpha comparison operator against zero
;; for "bcc" style instructions.
(define_predicate "alpha_zero_comparison_operator"
  (match_code "eq,ne,le,lt,leu,ltu"))

;; Return 1 if OP is a signed comparison operation.
(define_predicate "signed_comparison_operator"
  (match_code "eq,ne,le,lt,ge,gt"))

;; Return 1 if OP is a valid Alpha floating point comparison operator.
(define_predicate "alpha_fp_comparison_operator"
  (match_code "eq,le,lt,unordered"))

;; Return 1 if this is a divide or modulus operator.
(define_predicate "divmod_operator"
  (match_code "div,mod,udiv,umod"))

;; Return 1 if this is a float->int conversion operator.
(define_predicate "fix_operator"
  (match_code "fix,unsigned_fix"))

;; Recognize an addition operation that includes a constant.  Used to
;; convince reload to canonize (plus (plus reg c1) c2) during register
;; elimination.

(define_predicate "addition_operation"
  (and (match_code "plus")
       (match_test "register_operand (XEXP (op, 0), mode)
		    && GET_CODE (XEXP (op, 1)) == CONST_INT
		    && CONST_OK_FOR_LETTER_P (INTVAL (XEXP (op, 1)), 'K')")))

;; For TARGET_EXPLICIT_RELOCS, we don't obfuscate a SYMBOL_REF to a
;; small symbolic operand until after reload.  At which point we need
;; to replace (mem (symbol_ref)) with (mem (lo_sum $29 symbol_ref))
;; so that sched2 has the proper dependency information.  */
(define_predicate "some_small_symbolic_operand"
  (match_code "set,parallel,prefetch,unspec,unspec_volatile")
{
  /* Avoid search unless necessary.  */
  if (!TARGET_EXPLICIT_RELOCS || !reload_completed)
    return false;
  return for_each_rtx (&op, some_small_symbolic_operand_int, NULL);
})

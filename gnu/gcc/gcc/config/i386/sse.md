;; GCC machine description for SSE instructions
;; Copyright (C) 2005, 2006
;; Free Software Foundation, Inc.
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


;; 16 byte integral modes handled by SSE, minus TImode, which gets
;; special-cased for TARGET_64BIT.
(define_mode_macro SSEMODEI [V16QI V8HI V4SI V2DI])

;; All 16-byte vector modes handled by SSE
(define_mode_macro SSEMODE [V16QI V8HI V4SI V2DI V4SF V2DF])

;; Mix-n-match
(define_mode_macro SSEMODE12 [V16QI V8HI])
(define_mode_macro SSEMODE24 [V8HI V4SI])
(define_mode_macro SSEMODE14 [V16QI V4SI])
(define_mode_macro SSEMODE124 [V16QI V8HI V4SI])
(define_mode_macro SSEMODE248 [V8HI V4SI V2DI])

;; Mapping from integer vector mode to mnemonic suffix
(define_mode_attr ssevecsize [(V16QI "b") (V8HI "w") (V4SI "d") (V2DI "q")])

;; Patterns whose name begins with "sse{,2,3}_" are invoked by intrinsics.

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Move patterns
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; All of these patterns are enabled for SSE1 as well as SSE2.
;; This is essential for maintaining stable calling conventions.

(define_expand "mov<mode>"
  [(set (match_operand:SSEMODEI 0 "nonimmediate_operand" "")
	(match_operand:SSEMODEI 1 "nonimmediate_operand" ""))]
  "TARGET_SSE"
{
  ix86_expand_vector_move (<MODE>mode, operands);
  DONE;
})

(define_insn "*mov<mode>_internal"
  [(set (match_operand:SSEMODEI 0 "nonimmediate_operand" "=x,x ,m")
	(match_operand:SSEMODEI 1 "nonimmediate_or_sse_const_operand"  "C ,xm,x"))]
  "TARGET_SSE && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
{
  switch (which_alternative)
    {
    case 0:
      return standard_sse_constant_opcode (insn, operands[1]);
    case 1:
    case 2:
      if (get_attr_mode (insn) == MODE_V4SF)
	return "movaps\t{%1, %0|%0, %1}";
      else
	return "movdqa\t{%1, %0|%0, %1}";
    default:
      gcc_unreachable ();
    }
}
  [(set_attr "type" "sselog1,ssemov,ssemov")
   (set (attr "mode")
	(if_then_else
	  (ior (ior (ne (symbol_ref "optimize_size") (const_int 0))
		    (eq (symbol_ref "TARGET_SSE2") (const_int 0)))
	       (and (eq_attr "alternative" "2")
	  	    (ne (symbol_ref "TARGET_SSE_TYPELESS_STORES")
		        (const_int 0))))
	  (const_string "V4SF")
	  (const_string "TI")))])

(define_expand "movv4sf"
  [(set (match_operand:V4SF 0 "nonimmediate_operand" "")
	(match_operand:V4SF 1 "nonimmediate_operand" ""))]
  "TARGET_SSE"
{
  ix86_expand_vector_move (V4SFmode, operands);
  DONE;
})

(define_insn "*movv4sf_internal"
  [(set (match_operand:V4SF 0 "nonimmediate_operand" "=x,x,m")
	(match_operand:V4SF 1 "nonimmediate_or_sse_const_operand" "C,xm,x"))]
  "TARGET_SSE"
{
  switch (which_alternative)
    {
    case 0:
      return standard_sse_constant_opcode (insn, operands[1]);
    case 1:
    case 2:
      return "movaps\t{%1, %0|%0, %1}";
    default:
      abort();
    }
}
  [(set_attr "type" "sselog1,ssemov,ssemov")
   (set_attr "mode" "V4SF")])

(define_split
  [(set (match_operand:V4SF 0 "register_operand" "")
	(match_operand:V4SF 1 "zero_extended_scalar_load_operand" ""))]
  "TARGET_SSE && reload_completed"
  [(set (match_dup 0)
	(vec_merge:V4SF
	  (vec_duplicate:V4SF (match_dup 1))
	  (match_dup 2)
	  (const_int 1)))]
{
  operands[1] = simplify_gen_subreg (SFmode, operands[1], V4SFmode, 0);
  operands[2] = CONST0_RTX (V4SFmode);
})

(define_expand "movv2df"
  [(set (match_operand:V2DF 0 "nonimmediate_operand" "")
	(match_operand:V2DF 1 "nonimmediate_operand" ""))]
  "TARGET_SSE"
{
  ix86_expand_vector_move (V2DFmode, operands);
  DONE;
})

(define_insn "*movv2df_internal"
  [(set (match_operand:V2DF 0 "nonimmediate_operand" "=x,x,m")
	(match_operand:V2DF 1 "nonimmediate_or_sse_const_operand" "C,xm,x"))]
  "TARGET_SSE && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
{
  switch (which_alternative)
    {
    case 0:
      return standard_sse_constant_opcode (insn, operands[1]);
    case 1:
    case 2:
      if (get_attr_mode (insn) == MODE_V4SF)
	return "movaps\t{%1, %0|%0, %1}";
      else
	return "movapd\t{%1, %0|%0, %1}";
    default:
      gcc_unreachable ();
    }
}
  [(set_attr "type" "sselog1,ssemov,ssemov")
   (set (attr "mode")
	(if_then_else
	  (ior (ior (ne (symbol_ref "optimize_size") (const_int 0))
		    (eq (symbol_ref "TARGET_SSE2") (const_int 0)))
	       (and (eq_attr "alternative" "2")
	  	    (ne (symbol_ref "TARGET_SSE_TYPELESS_STORES")
		        (const_int 0))))
	  (const_string "V4SF")
	  (const_string "V2DF")))])

(define_split
  [(set (match_operand:V2DF 0 "register_operand" "")
	(match_operand:V2DF 1 "zero_extended_scalar_load_operand" ""))]
  "TARGET_SSE2 && reload_completed"
  [(set (match_dup 0) (vec_concat:V2DF (match_dup 1) (match_dup 2)))]
{
  operands[1] = simplify_gen_subreg (DFmode, operands[1], V2DFmode, 0);
  operands[2] = CONST0_RTX (DFmode);
})

(define_expand "push<mode>1"
  [(match_operand:SSEMODE 0 "register_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_push (<MODE>mode, operands[0]);
  DONE;
})

(define_expand "movmisalign<mode>"
  [(set (match_operand:SSEMODE 0 "nonimmediate_operand" "")
	(match_operand:SSEMODE 1 "nonimmediate_operand" ""))]
  "TARGET_SSE"
{
  ix86_expand_vector_move_misalign (<MODE>mode, operands);
  DONE;
})

(define_insn "sse_movups"
  [(set (match_operand:V4SF 0 "nonimmediate_operand" "=x,m")
	(unspec:V4SF [(match_operand:V4SF 1 "nonimmediate_operand" "xm,x")]
		     UNSPEC_MOVU))]
  "TARGET_SSE && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "movups\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_movupd"
  [(set (match_operand:V2DF 0 "nonimmediate_operand" "=x,m")
	(unspec:V2DF [(match_operand:V2DF 1 "nonimmediate_operand" "xm,x")]
		     UNSPEC_MOVU))]
  "TARGET_SSE2 && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "movupd\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_movdqu"
  [(set (match_operand:V16QI 0 "nonimmediate_operand" "=x,m")
	(unspec:V16QI [(match_operand:V16QI 1 "nonimmediate_operand" "xm,x")]
		      UNSPEC_MOVU))]
  "TARGET_SSE2 && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "movdqu\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "TI")])

(define_insn "sse_movntv4sf"
  [(set (match_operand:V4SF 0 "memory_operand" "=m")
	(unspec:V4SF [(match_operand:V4SF 1 "register_operand" "x")]
		     UNSPEC_MOVNT))]
  "TARGET_SSE"
  "movntps\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V4SF")])

(define_insn "sse2_movntv2df"
  [(set (match_operand:V2DF 0 "memory_operand" "=m")
	(unspec:V2DF [(match_operand:V2DF 1 "register_operand" "x")]
		     UNSPEC_MOVNT))]
  "TARGET_SSE2"
  "movntpd\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_movntv2di"
  [(set (match_operand:V2DI 0 "memory_operand" "=m")
	(unspec:V2DI [(match_operand:V2DI 1 "register_operand" "x")]
		     UNSPEC_MOVNT))]
  "TARGET_SSE2"
  "movntdq\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "TI")])

(define_insn "sse2_movntsi"
  [(set (match_operand:SI 0 "memory_operand" "=m")
	(unspec:SI [(match_operand:SI 1 "register_operand" "r")]
		   UNSPEC_MOVNT))]
  "TARGET_SSE2"
  "movnti\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "V2DF")])

(define_insn "sse3_lddqu"
  [(set (match_operand:V16QI 0 "register_operand" "=x")
	(unspec:V16QI [(match_operand:V16QI 1 "memory_operand" "m")]
		      UNSPEC_LDQQU))]
  "TARGET_SSE3"
  "lddqu\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "TI")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel single-precision floating point arithmetic
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_expand "negv4sf2"
  [(set (match_operand:V4SF 0 "register_operand" "")
	(neg:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "")))]
  "TARGET_SSE"
  "ix86_expand_fp_absneg_operator (NEG, V4SFmode, operands); DONE;")

(define_expand "absv4sf2"
  [(set (match_operand:V4SF 0 "register_operand" "")
	(abs:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "")))]
  "TARGET_SSE"
  "ix86_expand_fp_absneg_operator (ABS, V4SFmode, operands); DONE;")

(define_expand "addv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "")
	(plus:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "")
		   (match_operand:V4SF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE"
  "ix86_fixup_binary_operands_no_copy (PLUS, V4SFmode, operands);")

(define_insn "*addv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(plus:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "%0")
		   (match_operand:V4SF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE && ix86_binary_operator_ok (PLUS, V4SFmode, operands)"
  "addps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V4SF")])

(define_insn "sse_vmaddv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	  (plus:V4SF (match_operand:V4SF 1 "register_operand" "0")
		     (match_operand:V4SF 2 "nonimmediate_operand" "xm"))
	  (match_dup 1)
	  (const_int 1)))]
  "TARGET_SSE && ix86_binary_operator_ok (PLUS, V4SFmode, operands)"
  "addss\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "SF")])

(define_expand "subv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "")
	(minus:V4SF (match_operand:V4SF 1 "register_operand" "")
		    (match_operand:V4SF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE"
  "ix86_fixup_binary_operands_no_copy (MINUS, V4SFmode, operands);")

(define_insn "*subv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(minus:V4SF (match_operand:V4SF 1 "register_operand" "0")
		    (match_operand:V4SF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE"
  "subps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V4SF")])

(define_insn "sse_vmsubv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	  (minus:V4SF (match_operand:V4SF 1 "register_operand" "0")
		      (match_operand:V4SF 2 "nonimmediate_operand" "xm"))
	  (match_dup 1)
	  (const_int 1)))]
  "TARGET_SSE"
  "subss\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "SF")])

(define_expand "mulv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "")
	(mult:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "")
		   (match_operand:V4SF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE"
  "ix86_fixup_binary_operands_no_copy (MULT, V4SFmode, operands);")

(define_insn "*mulv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(mult:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "%0")
		   (match_operand:V4SF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE && ix86_binary_operator_ok (MULT, V4SFmode, operands)"
  "mulps\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssemul")
   (set_attr "mode" "V4SF")])

(define_insn "sse_vmmulv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	  (mult:V4SF (match_operand:V4SF 1 "register_operand" "0")
		     (match_operand:V4SF 2 "nonimmediate_operand" "xm"))
	  (match_dup 1)
	  (const_int 1)))]
  "TARGET_SSE && ix86_binary_operator_ok (MULT, V4SFmode, operands)"
  "mulss\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssemul")
   (set_attr "mode" "SF")])

(define_expand "divv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "")
	(div:V4SF (match_operand:V4SF 1 "register_operand" "")
		  (match_operand:V4SF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE"
  "ix86_fixup_binary_operands_no_copy (DIV, V4SFmode, operands);")

(define_insn "*divv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(div:V4SF (match_operand:V4SF 1 "register_operand" "0")
		  (match_operand:V4SF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE"
  "divps\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssediv")
   (set_attr "mode" "V4SF")])

(define_insn "sse_vmdivv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	  (div:V4SF (match_operand:V4SF 1 "register_operand" "0")
		    (match_operand:V4SF 2 "nonimmediate_operand" "xm"))
	  (match_dup 1)
	  (const_int 1)))]
  "TARGET_SSE"
  "divss\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssediv")
   (set_attr "mode" "SF")])

(define_insn "sse_rcpv4sf2"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(unspec:V4SF
	 [(match_operand:V4SF 1 "nonimmediate_operand" "xm")] UNSPEC_RCP))]
  "TARGET_SSE"
  "rcpps\t{%1, %0|%0, %1}"
  [(set_attr "type" "sse")
   (set_attr "mode" "V4SF")])

(define_insn "sse_vmrcpv4sf2"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	  (unspec:V4SF [(match_operand:V4SF 1 "nonimmediate_operand" "xm")]
		       UNSPEC_RCP)
	  (match_operand:V4SF 2 "register_operand" "0")
	  (const_int 1)))]
  "TARGET_SSE"
  "rcpss\t{%1, %0|%0, %1}"
  [(set_attr "type" "sse")
   (set_attr "mode" "SF")])

(define_insn "sse_rsqrtv4sf2"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(unspec:V4SF
	  [(match_operand:V4SF 1 "nonimmediate_operand" "xm")] UNSPEC_RSQRT))]
  "TARGET_SSE"
  "rsqrtps\t{%1, %0|%0, %1}"
  [(set_attr "type" "sse")
   (set_attr "mode" "V4SF")])

(define_insn "sse_vmrsqrtv4sf2"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	  (unspec:V4SF [(match_operand:V4SF 1 "nonimmediate_operand" "xm")]
		       UNSPEC_RSQRT)
	  (match_operand:V4SF 2 "register_operand" "0")
	  (const_int 1)))]
  "TARGET_SSE"
  "rsqrtss\t{%1, %0|%0, %1}"
  [(set_attr "type" "sse")
   (set_attr "mode" "SF")])

(define_insn "sqrtv4sf2"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(sqrt:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "xm")))]
  "TARGET_SSE"
  "sqrtps\t{%1, %0|%0, %1}"
  [(set_attr "type" "sse")
   (set_attr "mode" "V4SF")])

(define_insn "sse_vmsqrtv4sf2"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	  (sqrt:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "xm"))
	  (match_operand:V4SF 2 "register_operand" "0")
	  (const_int 1)))]
  "TARGET_SSE"
  "sqrtss\t{%1, %0|%0, %1}"
  [(set_attr "type" "sse")
   (set_attr "mode" "SF")])

;; ??? For !flag_finite_math_only, the representation with SMIN/SMAX
;; isn't really correct, as those rtl operators aren't defined when 
;; applied to NaNs.  Hopefully the optimizers won't get too smart on us.

(define_expand "smaxv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "")
	(smax:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "")
		   (match_operand:V4SF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE"
{
  if (!flag_finite_math_only)
    operands[1] = force_reg (V4SFmode, operands[1]);
  ix86_fixup_binary_operands_no_copy (SMAX, V4SFmode, operands);
})

(define_insn "*smaxv4sf3_finite"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(smax:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "%0")
		   (match_operand:V4SF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE && flag_finite_math_only
   && ix86_binary_operator_ok (SMAX, V4SFmode, operands)"
  "maxps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sse")
   (set_attr "mode" "V4SF")])

(define_insn "*smaxv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(smax:V4SF (match_operand:V4SF 1 "register_operand" "0")
		   (match_operand:V4SF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE"
  "maxps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sse")
   (set_attr "mode" "V4SF")])

(define_insn "sse_vmsmaxv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	 (smax:V4SF (match_operand:V4SF 1 "register_operand" "0")
		    (match_operand:V4SF 2 "nonimmediate_operand" "xm"))
	 (match_dup 1)
	 (const_int 1)))]
  "TARGET_SSE"
  "maxss\t{%2, %0|%0, %2}"
  [(set_attr "type" "sse")
   (set_attr "mode" "SF")])

(define_expand "sminv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "")
	(smin:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "")
		   (match_operand:V4SF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE"
{
  if (!flag_finite_math_only)
    operands[1] = force_reg (V4SFmode, operands[1]);
  ix86_fixup_binary_operands_no_copy (SMIN, V4SFmode, operands);
})

(define_insn "*sminv4sf3_finite"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(smin:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "%0")
		   (match_operand:V4SF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE && flag_finite_math_only
   && ix86_binary_operator_ok (SMIN, V4SFmode, operands)"
  "minps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sse")
   (set_attr "mode" "V4SF")])

(define_insn "*sminv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(smin:V4SF (match_operand:V4SF 1 "register_operand" "0")
		   (match_operand:V4SF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE"
  "minps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sse")
   (set_attr "mode" "V4SF")])

(define_insn "sse_vmsminv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	 (smin:V4SF (match_operand:V4SF 1 "register_operand" "0")
		    (match_operand:V4SF 2 "nonimmediate_operand" "xm"))
	 (match_dup 1)
	 (const_int 1)))]
  "TARGET_SSE"
  "minss\t{%2, %0|%0, %2}"
  [(set_attr "type" "sse")
   (set_attr "mode" "SF")])

;; These versions of the min/max patterns implement exactly the operations
;;   min = (op1 < op2 ? op1 : op2)
;;   max = (!(op1 < op2) ? op1 : op2)
;; Their operands are not commutative, and thus they may be used in the
;; presence of -0.0 and NaN.

(define_insn "*ieee_sminv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(unspec:V4SF [(match_operand:V4SF 1 "register_operand" "0")
		      (match_operand:V4SF 2 "nonimmediate_operand" "xm")]
		     UNSPEC_IEEE_MIN))]
  "TARGET_SSE"
  "minps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V4SF")])

(define_insn "*ieee_smaxv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(unspec:V4SF [(match_operand:V4SF 1 "register_operand" "0")
		      (match_operand:V4SF 2 "nonimmediate_operand" "xm")]
		     UNSPEC_IEEE_MAX))]
  "TARGET_SSE"
  "maxps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V4SF")])

(define_insn "*ieee_sminv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(unspec:V2DF [(match_operand:V2DF 1 "register_operand" "0")
		      (match_operand:V2DF 2 "nonimmediate_operand" "xm")]
		     UNSPEC_IEEE_MIN))]
  "TARGET_SSE2"
  "minpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V2DF")])

(define_insn "*ieee_smaxv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(unspec:V2DF [(match_operand:V2DF 1 "register_operand" "0")
		      (match_operand:V2DF 2 "nonimmediate_operand" "xm")]
		     UNSPEC_IEEE_MAX))]
  "TARGET_SSE2"
  "maxpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V2DF")])

(define_insn "sse3_addsubv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	  (plus:V4SF
	    (match_operand:V4SF 1 "register_operand" "0")
	    (match_operand:V4SF 2 "nonimmediate_operand" "xm"))
	  (minus:V4SF (match_dup 1) (match_dup 2))
	  (const_int 5)))]
  "TARGET_SSE3"
  "addsubps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V4SF")])

(define_insn "sse3_haddv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_concat:V4SF
	  (vec_concat:V2SF
	    (plus:SF
	      (vec_select:SF 
		(match_operand:V4SF 1 "register_operand" "0")
		(parallel [(const_int 0)]))
	      (vec_select:SF (match_dup 1) (parallel [(const_int 1)])))
	    (plus:SF
	      (vec_select:SF (match_dup 1) (parallel [(const_int 2)]))
	      (vec_select:SF (match_dup 1) (parallel [(const_int 3)]))))
	  (vec_concat:V2SF
	    (plus:SF
	      (vec_select:SF
		(match_operand:V4SF 2 "nonimmediate_operand" "xm")
		(parallel [(const_int 0)]))
	      (vec_select:SF (match_dup 2) (parallel [(const_int 1)])))
	    (plus:SF
	      (vec_select:SF (match_dup 2) (parallel [(const_int 2)]))
	      (vec_select:SF (match_dup 2) (parallel [(const_int 3)]))))))]
  "TARGET_SSE3"
  "haddps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V4SF")])

(define_insn "sse3_hsubv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_concat:V4SF
	  (vec_concat:V2SF
	    (minus:SF
	      (vec_select:SF 
		(match_operand:V4SF 1 "register_operand" "0")
		(parallel [(const_int 0)]))
	      (vec_select:SF (match_dup 1) (parallel [(const_int 1)])))
	    (minus:SF
	      (vec_select:SF (match_dup 1) (parallel [(const_int 2)]))
	      (vec_select:SF (match_dup 1) (parallel [(const_int 3)]))))
	  (vec_concat:V2SF
	    (minus:SF
	      (vec_select:SF
		(match_operand:V4SF 2 "nonimmediate_operand" "xm")
		(parallel [(const_int 0)]))
	      (vec_select:SF (match_dup 2) (parallel [(const_int 1)])))
	    (minus:SF
	      (vec_select:SF (match_dup 2) (parallel [(const_int 2)]))
	      (vec_select:SF (match_dup 2) (parallel [(const_int 3)]))))))]
  "TARGET_SSE3"
  "hsubps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V4SF")])

(define_expand "reduc_splus_v4sf"
  [(match_operand:V4SF 0 "register_operand" "")
   (match_operand:V4SF 1 "register_operand" "")]
  "TARGET_SSE"
{
  if (TARGET_SSE3)
    {
      rtx tmp = gen_reg_rtx (V4SFmode);
      emit_insn (gen_sse3_haddv4sf3 (tmp, operands[1], operands[1]));
      emit_insn (gen_sse3_haddv4sf3 (operands[0], tmp, tmp));
    }
  else
    ix86_expand_reduc_v4sf (gen_addv4sf3, operands[0], operands[1]);
  DONE;
})

(define_expand "reduc_smax_v4sf"
  [(match_operand:V4SF 0 "register_operand" "")
   (match_operand:V4SF 1 "register_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_reduc_v4sf (gen_smaxv4sf3, operands[0], operands[1]);
  DONE;
})

(define_expand "reduc_smin_v4sf"
  [(match_operand:V4SF 0 "register_operand" "")
   (match_operand:V4SF 1 "register_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_reduc_v4sf (gen_sminv4sf3, operands[0], operands[1]);
  DONE;
})

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel single-precision floating point comparisons
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "sse_maskcmpv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(match_operator:V4SF 3 "sse_comparison_operator"
		[(match_operand:V4SF 1 "register_operand" "0")
		 (match_operand:V4SF 2 "nonimmediate_operand" "xm")]))]
  "TARGET_SSE"
  "cmp%D3ps\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssecmp")
   (set_attr "mode" "V4SF")])

(define_insn "sse_vmmaskcmpv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	 (match_operator:V4SF 3 "sse_comparison_operator"
		[(match_operand:V4SF 1 "register_operand" "0")
		 (match_operand:V4SF 2 "register_operand" "x")])
	 (match_dup 1)
	 (const_int 1)))]
  "TARGET_SSE"
  "cmp%D3ss\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssecmp")
   (set_attr "mode" "SF")])

(define_insn "sse_comi"
  [(set (reg:CCFP FLAGS_REG)
	(compare:CCFP
	  (vec_select:SF
	    (match_operand:V4SF 0 "register_operand" "x")
	    (parallel [(const_int 0)]))
	  (vec_select:SF
	    (match_operand:V4SF 1 "nonimmediate_operand" "xm")
	    (parallel [(const_int 0)]))))]
  "TARGET_SSE"
  "comiss\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecomi")
   (set_attr "mode" "SF")])

(define_insn "sse_ucomi"
  [(set (reg:CCFPU FLAGS_REG)
	(compare:CCFPU
	  (vec_select:SF
	    (match_operand:V4SF 0 "register_operand" "x")
	    (parallel [(const_int 0)]))
	  (vec_select:SF
	    (match_operand:V4SF 1 "nonimmediate_operand" "xm")
	    (parallel [(const_int 0)]))))]
  "TARGET_SSE"
  "ucomiss\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecomi")
   (set_attr "mode" "SF")])

(define_expand "vcondv4sf"
  [(set (match_operand:V4SF 0 "register_operand" "")
        (if_then_else:V4SF
          (match_operator 3 ""
            [(match_operand:V4SF 4 "nonimmediate_operand" "")
             (match_operand:V4SF 5 "nonimmediate_operand" "")])
          (match_operand:V4SF 1 "general_operand" "")
          (match_operand:V4SF 2 "general_operand" "")))]
  "TARGET_SSE"
{
  if (ix86_expand_fp_vcond (operands))
    DONE;
  else
    FAIL;
})

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel single-precision floating point logical operations
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_expand "andv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "")
	(and:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "")
		  (match_operand:V4SF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE"
  "ix86_fixup_binary_operands_no_copy (AND, V4SFmode, operands);")

(define_insn "*andv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(and:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "%0")
		  (match_operand:V4SF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE && ix86_binary_operator_ok (AND, V4SFmode, operands)"
  "andps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V4SF")])

(define_insn "sse_nandv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(and:V4SF (not:V4SF (match_operand:V4SF 1 "register_operand" "0"))
		  (match_operand:V4SF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE"
  "andnps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V4SF")])

(define_expand "iorv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "")
	(ior:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "")
		  (match_operand:V4SF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE"
  "ix86_fixup_binary_operands_no_copy (IOR, V4SFmode, operands);")

(define_insn "*iorv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(ior:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "%0")
		  (match_operand:V4SF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE && ix86_binary_operator_ok (IOR, V4SFmode, operands)"
  "orps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V4SF")])

(define_expand "xorv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "")
	(xor:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "")
		  (match_operand:V4SF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE"
  "ix86_fixup_binary_operands_no_copy (XOR, V4SFmode, operands);")

(define_insn "*xorv4sf3"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(xor:V4SF (match_operand:V4SF 1 "nonimmediate_operand" "%0")
		  (match_operand:V4SF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE && ix86_binary_operator_ok (XOR, V4SFmode, operands)"
  "xorps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V4SF")])

;; Also define scalar versions.  These are used for abs, neg, and
;; conditional move.  Using subregs into vector modes causes register
;; allocation lossage.  These patterns do not allow memory operands
;; because the native instructions read the full 128-bits.

(define_insn "*andsf3"
  [(set (match_operand:SF 0 "register_operand" "=x")
	(and:SF (match_operand:SF 1 "register_operand" "0")
		(match_operand:SF 2 "register_operand" "x")))]
  "TARGET_SSE"
  "andps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V4SF")])

(define_insn "*nandsf3"
  [(set (match_operand:SF 0 "register_operand" "=x")
	(and:SF (not:SF (match_operand:SF 1 "register_operand" "0"))
		(match_operand:SF 2 "register_operand" "x")))]
  "TARGET_SSE"
  "andnps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V4SF")])

(define_insn "*iorsf3"
  [(set (match_operand:SF 0 "register_operand" "=x")
	(ior:SF (match_operand:SF 1 "register_operand" "0")
		(match_operand:SF 2 "register_operand" "x")))]
  "TARGET_SSE"
  "orps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V4SF")])

(define_insn "*xorsf3"
  [(set (match_operand:SF 0 "register_operand" "=x")
	(xor:SF (match_operand:SF 1 "register_operand" "0")
		(match_operand:SF 2 "register_operand" "x")))]
  "TARGET_SSE"
  "xorps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V4SF")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel single-precision floating point conversion operations
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "sse_cvtpi2ps"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	  (vec_duplicate:V4SF
	    (float:V2SF (match_operand:V2SI 2 "nonimmediate_operand" "ym")))
	  (match_operand:V4SF 1 "register_operand" "0")
	  (const_int 3)))]
  "TARGET_SSE"
  "cvtpi2ps\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "V4SF")])

(define_insn "sse_cvtps2pi"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(vec_select:V2SI
	  (unspec:V4SI [(match_operand:V4SF 1 "nonimmediate_operand" "xm")]
		       UNSPEC_FIX_NOTRUNC)
	  (parallel [(const_int 0) (const_int 1)])))]
  "TARGET_SSE"
  "cvtps2pi\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "unit" "mmx")
   (set_attr "mode" "DI")])

(define_insn "sse_cvttps2pi"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(vec_select:V2SI
	  (fix:V4SI (match_operand:V4SF 1 "nonimmediate_operand" "xm"))
	  (parallel [(const_int 0) (const_int 1)])))]
  "TARGET_SSE"
  "cvttps2pi\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "unit" "mmx")
   (set_attr "mode" "SF")])

(define_insn "sse_cvtsi2ss"
  [(set (match_operand:V4SF 0 "register_operand" "=x,x")
	(vec_merge:V4SF
	  (vec_duplicate:V4SF
	    (float:SF (match_operand:SI 2 "nonimmediate_operand" "r,m")))
	  (match_operand:V4SF 1 "register_operand" "0,0")
	  (const_int 1)))]
  "TARGET_SSE"
  "cvtsi2ss\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseicvt")
   (set_attr "athlon_decode" "vector,double")
   (set_attr "mode" "SF")])

(define_insn "sse_cvtsi2ssq"
  [(set (match_operand:V4SF 0 "register_operand" "=x,x")
	(vec_merge:V4SF
	  (vec_duplicate:V4SF
	    (float:SF (match_operand:DI 2 "nonimmediate_operand" "r,rm")))
	  (match_operand:V4SF 1 "register_operand" "0,0")
	  (const_int 1)))]
  "TARGET_SSE && TARGET_64BIT"
  "cvtsi2ssq\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseicvt")
   (set_attr "athlon_decode" "vector,double")
   (set_attr "mode" "SF")])

(define_insn "sse_cvtss2si"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(unspec:SI
	  [(vec_select:SF
	     (match_operand:V4SF 1 "nonimmediate_operand" "x,m")
	     (parallel [(const_int 0)]))]
	  UNSPEC_FIX_NOTRUNC))]
  "TARGET_SSE"
  "cvtss2si\t{%1, %0|%0, %1}"
  [(set_attr "type" "sseicvt")
   (set_attr "athlon_decode" "double,vector")
   (set_attr "mode" "SI")])

(define_insn "sse_cvtss2siq"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(unspec:DI
	  [(vec_select:SF
	     (match_operand:V4SF 1 "nonimmediate_operand" "x,m")
	     (parallel [(const_int 0)]))]
	  UNSPEC_FIX_NOTRUNC))]
  "TARGET_SSE && TARGET_64BIT"
  "cvtss2siq\t{%1, %0|%0, %1}"
  [(set_attr "type" "sseicvt")
   (set_attr "athlon_decode" "double,vector")
   (set_attr "mode" "DI")])

(define_insn "sse_cvttss2si"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(fix:SI
	  (vec_select:SF
	    (match_operand:V4SF 1 "nonimmediate_operand" "x,m")
	    (parallel [(const_int 0)]))))]
  "TARGET_SSE"
  "cvttss2si\t{%1, %0|%0, %1}"
  [(set_attr "type" "sseicvt")
   (set_attr "athlon_decode" "double,vector")
   (set_attr "mode" "SI")])

(define_insn "sse_cvttss2siq"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(fix:DI
	  (vec_select:SF
	    (match_operand:V4SF 1 "nonimmediate_operand" "x,m")
	    (parallel [(const_int 0)]))))]
  "TARGET_SSE && TARGET_64BIT"
  "cvttss2siq\t{%1, %0|%0, %1}"
  [(set_attr "type" "sseicvt")
   (set_attr "athlon_decode" "double,vector")
   (set_attr "mode" "DI")])

(define_insn "sse2_cvtdq2ps"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(float:V4SF (match_operand:V4SI 1 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "cvtdq2ps\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_cvtps2dq"
  [(set (match_operand:V4SI 0 "register_operand" "=x")
	(unspec:V4SI [(match_operand:V4SF 1 "nonimmediate_operand" "xm")]
		     UNSPEC_FIX_NOTRUNC))]
  "TARGET_SSE2"
  "cvtps2dq\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "TI")])

(define_insn "sse2_cvttps2dq"
  [(set (match_operand:V4SI 0 "register_operand" "=x")
	(fix:V4SI (match_operand:V4SF 1 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "cvttps2dq\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "TI")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel single-precision floating point element swizzling
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "sse_movhlps"
  [(set (match_operand:V4SF 0 "nonimmediate_operand"     "=x,x,m")
	(vec_select:V4SF
	  (vec_concat:V8SF
	    (match_operand:V4SF 1 "nonimmediate_operand" " 0,0,0")
	    (match_operand:V4SF 2 "nonimmediate_operand" " x,o,x"))
	  (parallel [(const_int 6)
		     (const_int 7)
		     (const_int 2)
		     (const_int 3)])))]
  "TARGET_SSE && !(MEM_P (operands[1]) && MEM_P (operands[2]))"
  "@
   movhlps\t{%2, %0|%0, %2}
   movlps\t{%H2, %0|%0, %H2}
   movhps\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V4SF,V2SF,V2SF")])

(define_insn "sse_movlhps"
  [(set (match_operand:V4SF 0 "nonimmediate_operand"     "=x,x,o")
	(vec_select:V4SF
	  (vec_concat:V8SF
	    (match_operand:V4SF 1 "nonimmediate_operand" " 0,0,0")
	    (match_operand:V4SF 2 "nonimmediate_operand" " x,m,x"))
	  (parallel [(const_int 0)
		     (const_int 1)
		     (const_int 4)
		     (const_int 5)])))]
  "TARGET_SSE && ix86_binary_operator_ok (UNKNOWN, V4SFmode, operands)"
  "@
   movlhps\t{%2, %0|%0, %2}
   movhps\t{%2, %0|%0, %2}
   movlps\t{%2, %H0|%H0, %2}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V4SF,V2SF,V2SF")])

(define_insn "sse_unpckhps"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_select:V4SF
	  (vec_concat:V8SF
	    (match_operand:V4SF 1 "register_operand" "0")
	    (match_operand:V4SF 2 "nonimmediate_operand" "xm"))
	  (parallel [(const_int 2) (const_int 6)
		     (const_int 3) (const_int 7)])))]
  "TARGET_SSE"
  "unpckhps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V4SF")])

(define_insn "sse_unpcklps"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_select:V4SF
	  (vec_concat:V8SF
	    (match_operand:V4SF 1 "register_operand" "0")
	    (match_operand:V4SF 2 "nonimmediate_operand" "xm"))
	  (parallel [(const_int 0) (const_int 4)
		     (const_int 1) (const_int 5)])))]
  "TARGET_SSE"
  "unpcklps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V4SF")])

;; These are modeled with the same vec_concat as the others so that we
;; capture users of shufps that can use the new instructions
(define_insn "sse3_movshdup"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_select:V4SF
	  (vec_concat:V8SF
	    (match_operand:V4SF 1 "nonimmediate_operand" "xm")
	    (match_dup 1))
	  (parallel [(const_int 1)
		     (const_int 1)
		     (const_int 7)
		     (const_int 7)])))]
  "TARGET_SSE3"
  "movshdup\t{%1, %0|%0, %1}"
  [(set_attr "type" "sse")
   (set_attr "mode" "V4SF")])

(define_insn "sse3_movsldup"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_select:V4SF
	  (vec_concat:V8SF
	    (match_operand:V4SF 1 "nonimmediate_operand" "xm")
	    (match_dup 1))
	  (parallel [(const_int 0)
		     (const_int 0)
		     (const_int 6)
		     (const_int 6)])))]
  "TARGET_SSE3"
  "movsldup\t{%1, %0|%0, %1}"
  [(set_attr "type" "sse")
   (set_attr "mode" "V4SF")])

(define_expand "sse_shufps"
  [(match_operand:V4SF 0 "register_operand" "")
   (match_operand:V4SF 1 "register_operand" "")
   (match_operand:V4SF 2 "nonimmediate_operand" "")
   (match_operand:SI 3 "const_int_operand" "")]
  "TARGET_SSE"
{
  int mask = INTVAL (operands[3]);
  emit_insn (gen_sse_shufps_1 (operands[0], operands[1], operands[2],
			       GEN_INT ((mask >> 0) & 3),
			       GEN_INT ((mask >> 2) & 3),
			       GEN_INT (((mask >> 4) & 3) + 4),
			       GEN_INT (((mask >> 6) & 3) + 4)));
  DONE;
})

(define_insn "sse_shufps_1"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_select:V4SF
	  (vec_concat:V8SF
	    (match_operand:V4SF 1 "register_operand" "0")
	    (match_operand:V4SF 2 "nonimmediate_operand" "xm"))
	  (parallel [(match_operand 3 "const_0_to_3_operand" "")
		     (match_operand 4 "const_0_to_3_operand" "")
		     (match_operand 5 "const_4_to_7_operand" "")
		     (match_operand 6 "const_4_to_7_operand" "")])))]
  "TARGET_SSE"
{
  int mask = 0;
  mask |= INTVAL (operands[3]) << 0;
  mask |= INTVAL (operands[4]) << 2;
  mask |= (INTVAL (operands[5]) - 4) << 4;
  mask |= (INTVAL (operands[6]) - 4) << 6;
  operands[3] = GEN_INT (mask);

  return "shufps\t{%3, %2, %0|%0, %2, %3}";
}
  [(set_attr "type" "sselog")
   (set_attr "mode" "V4SF")])

(define_insn "sse_storehps"
  [(set (match_operand:V2SF 0 "nonimmediate_operand" "=m,x,x")
	(vec_select:V2SF
	  (match_operand:V4SF 1 "nonimmediate_operand" "x,x,o")
	  (parallel [(const_int 2) (const_int 3)])))]
  "TARGET_SSE"
  "@
   movhps\t{%1, %0|%0, %1}
   movhlps\t{%1, %0|%0, %1}
   movlps\t{%H1, %0|%0, %H1}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V2SF,V4SF,V2SF")])

(define_insn "sse_loadhps"
  [(set (match_operand:V4SF 0 "nonimmediate_operand" "=x,x,o")
	(vec_concat:V4SF
	  (vec_select:V2SF
	    (match_operand:V4SF 1 "nonimmediate_operand" "0,0,0")
	    (parallel [(const_int 0) (const_int 1)]))
	  (match_operand:V2SF 2 "nonimmediate_operand" "m,x,x")))]
  "TARGET_SSE"
  "@
   movhps\t{%2, %0|%0, %2}
   movlhps\t{%2, %0|%0, %2}
   movlps\t{%2, %H0|%H0, %2}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V2SF,V4SF,V2SF")])

(define_insn "sse_storelps"
  [(set (match_operand:V2SF 0 "nonimmediate_operand" "=m,x,x")
	(vec_select:V2SF
	  (match_operand:V4SF 1 "nonimmediate_operand" "x,x,m")
	  (parallel [(const_int 0) (const_int 1)])))]
  "TARGET_SSE"
  "@
   movlps\t{%1, %0|%0, %1}
   movaps\t{%1, %0|%0, %1}
   movlps\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V2SF,V4SF,V2SF")])

(define_insn "sse_loadlps"
  [(set (match_operand:V4SF 0 "nonimmediate_operand" "=x,x,m")
	(vec_concat:V4SF
	  (match_operand:V2SF 2 "nonimmediate_operand" "0,m,x")
	  (vec_select:V2SF
	    (match_operand:V4SF 1 "nonimmediate_operand" "x,0,0")
	    (parallel [(const_int 2) (const_int 3)]))))]
  "TARGET_SSE"
  "@
   shufps\t{$0xe4, %1, %0|%0, %1, 0xe4}
   movlps\t{%2, %0|%0, %2}
   movlps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog,ssemov,ssemov")
   (set_attr "mode" "V4SF,V2SF,V2SF")])

(define_insn "sse_movss"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_merge:V4SF
	  (match_operand:V4SF 2 "register_operand" "x")
	  (match_operand:V4SF 1 "register_operand" "0")
	  (const_int 1)))]
  "TARGET_SSE"
  "movss\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "SF")])

(define_insn "*vec_dupv4sf"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_duplicate:V4SF
	  (match_operand:SF 1 "register_operand" "0")))]
  "TARGET_SSE"
  "shufps\t{$0, %0, %0|%0, %0, 0}"
  [(set_attr "type" "sselog1")
   (set_attr "mode" "V4SF")])

;; ??? In theory we can match memory for the MMX alternative, but allowing
;; nonimmediate_operand for operand 2 and *not* allowing memory for the SSE
;; alternatives pretty much forces the MMX alternative to be chosen.
(define_insn "*sse_concatv2sf"
  [(set (match_operand:V2SF 0 "register_operand"     "=x,x,*y,*y")
	(vec_concat:V2SF
	  (match_operand:SF 1 "nonimmediate_operand" " 0,m, 0, m")
	  (match_operand:SF 2 "reg_or_0_operand"     " x,C,*y, C")))]
  "TARGET_SSE"
  "@
   unpcklps\t{%2, %0|%0, %2}
   movss\t{%1, %0|%0, %1}
   punpckldq\t{%2, %0|%0, %2}
   movd\t{%1, %0|%0, %1}"
  [(set_attr "type" "sselog,ssemov,mmxcvt,mmxmov")
   (set_attr "mode" "V4SF,SF,DI,DI")])

(define_insn "*sse_concatv4sf"
  [(set (match_operand:V4SF 0 "register_operand"   "=x,x")
	(vec_concat:V4SF
	  (match_operand:V2SF 1 "register_operand" " 0,0")
	  (match_operand:V2SF 2 "nonimmediate_operand" " x,m")))]
  "TARGET_SSE"
  "@
   movlhps\t{%2, %0|%0, %2}
   movhps\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V4SF,V2SF")])

(define_expand "vec_initv4sf"
  [(match_operand:V4SF 0 "register_operand" "")
   (match_operand 1 "" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_init (false, operands[0], operands[1]);
  DONE;
})

(define_insn "*vec_setv4sf_0"
  [(set (match_operand:V4SF 0 "nonimmediate_operand"  "=x,x,Y ,m")
	(vec_merge:V4SF
	  (vec_duplicate:V4SF
	    (match_operand:SF 2 "general_operand"     " x,m,*r,x*rfF"))
	  (match_operand:V4SF 1 "vector_move_operand" " 0,C,C ,0")
	  (const_int 1)))]
  "TARGET_SSE"
  "@
   movss\t{%2, %0|%0, %2}
   movss\t{%2, %0|%0, %2}
   movd\t{%2, %0|%0, %2}
   #"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "SF")])

(define_split
  [(set (match_operand:V4SF 0 "memory_operand" "")
	(vec_merge:V4SF
	  (vec_duplicate:V4SF
	    (match_operand:SF 1 "nonmemory_operand" ""))
	  (match_dup 0)
	  (const_int 1)))]
  "TARGET_SSE && reload_completed"
  [(const_int 0)]
{
  emit_move_insn (adjust_address (operands[0], SFmode, 0), operands[1]);
  DONE;
})

(define_expand "vec_setv4sf"
  [(match_operand:V4SF 0 "register_operand" "")
   (match_operand:SF 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_set (false, operands[0], operands[1],
			  INTVAL (operands[2]));
  DONE;
})

(define_insn_and_split "*vec_extractv4sf_0"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=x,m,fr")
	(vec_select:SF
	  (match_operand:V4SF 1 "nonimmediate_operand" "xm,x,m")
	  (parallel [(const_int 0)])))]
  "TARGET_SSE && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rtx op1 = operands[1];
  if (REG_P (op1))
    op1 = gen_rtx_REG (SFmode, REGNO (op1));
  else
    op1 = gen_lowpart (SFmode, op1);
  emit_move_insn (operands[0], op1);
  DONE;
})

(define_expand "vec_extractv4sf"
  [(match_operand:SF 0 "register_operand" "")
   (match_operand:V4SF 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_extract (false, operands[0], operands[1],
			      INTVAL (operands[2]));
  DONE;
})

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel double-precision floating point arithmetic
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_expand "negv2df2"
  [(set (match_operand:V2DF 0 "register_operand" "")
	(neg:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_expand_fp_absneg_operator (NEG, V2DFmode, operands); DONE;")

(define_expand "absv2df2"
  [(set (match_operand:V2DF 0 "register_operand" "")
	(abs:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_expand_fp_absneg_operator (ABS, V2DFmode, operands); DONE;")

(define_expand "addv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "")
	(plus:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "")
		   (match_operand:V2DF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (PLUS, V2DFmode, operands);")

(define_insn "*addv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(plus:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "%0")
		   (match_operand:V2DF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (PLUS, V2DFmode, operands)"
  "addpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_vmaddv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_merge:V2DF
	  (plus:V2DF (match_operand:V2DF 1 "register_operand" "0")
		     (match_operand:V2DF 2 "nonimmediate_operand" "xm"))
	  (match_dup 1)
	  (const_int 1)))]
  "TARGET_SSE2 && ix86_binary_operator_ok (PLUS, V4SFmode, operands)"
  "addsd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "DF")])

(define_expand "subv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "")
	(minus:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "")
		    (match_operand:V2DF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (MINUS, V2DFmode, operands);")

(define_insn "*subv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(minus:V2DF (match_operand:V2DF 1 "register_operand" "0")
		    (match_operand:V2DF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "subpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_vmsubv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_merge:V2DF
	  (minus:V2DF (match_operand:V2DF 1 "register_operand" "0")
		      (match_operand:V2DF 2 "nonimmediate_operand" "xm"))
	  (match_dup 1)
	  (const_int 1)))]
  "TARGET_SSE2"
  "subsd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "DF")])

(define_expand "mulv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "")
	(mult:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "")
		   (match_operand:V2DF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (MULT, V2DFmode, operands);")

(define_insn "*mulv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(mult:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "%0")
		   (match_operand:V2DF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (MULT, V2DFmode, operands)"
  "mulpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssemul")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_vmmulv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_merge:V2DF
	  (mult:V2DF (match_operand:V2DF 1 "register_operand" "0")
		     (match_operand:V2DF 2 "nonimmediate_operand" "xm"))
	  (match_dup 1)
	  (const_int 1)))]
  "TARGET_SSE2 && ix86_binary_operator_ok (MULT, V2DFmode, operands)"
  "mulsd\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssemul")
   (set_attr "mode" "DF")])

(define_expand "divv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "")
	(div:V2DF (match_operand:V2DF 1 "register_operand" "")
		  (match_operand:V2DF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (DIV, V2DFmode, operands);")

(define_insn "*divv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(div:V2DF (match_operand:V2DF 1 "register_operand" "0")
		  (match_operand:V2DF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "divpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssediv")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_vmdivv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_merge:V2DF
	  (div:V2DF (match_operand:V2DF 1 "register_operand" "0")
		    (match_operand:V2DF 2 "nonimmediate_operand" "xm"))
	  (match_dup 1)
	  (const_int 1)))]
  "TARGET_SSE2"
  "divsd\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssediv")
   (set_attr "mode" "DF")])

(define_insn "sqrtv2df2"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(sqrt:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "sqrtpd\t{%1, %0|%0, %1}"
  [(set_attr "type" "sse")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_vmsqrtv2df2"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_merge:V2DF
	  (sqrt:V2DF (match_operand:V2DF 1 "register_operand" "xm"))
	  (match_operand:V2DF 2 "register_operand" "0")
	  (const_int 1)))]
  "TARGET_SSE2"
  "sqrtsd\t{%1, %0|%0, %1}"
  [(set_attr "type" "sse")
   (set_attr "mode" "DF")])

;; ??? For !flag_finite_math_only, the representation with SMIN/SMAX
;; isn't really correct, as those rtl operators aren't defined when 
;; applied to NaNs.  Hopefully the optimizers won't get too smart on us.

(define_expand "smaxv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "")
	(smax:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "")
		   (match_operand:V2DF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
{
  if (!flag_finite_math_only)
    operands[1] = force_reg (V2DFmode, operands[1]);
  ix86_fixup_binary_operands_no_copy (SMAX, V2DFmode, operands);
})

(define_insn "*smaxv2df3_finite"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(smax:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "%0")
		   (match_operand:V2DF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && flag_finite_math_only
   && ix86_binary_operator_ok (SMAX, V2DFmode, operands)"
  "maxpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V2DF")])

(define_insn "*smaxv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(smax:V2DF (match_operand:V2DF 1 "register_operand" "0")
		   (match_operand:V2DF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "maxpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_vmsmaxv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_merge:V2DF
	  (smax:V2DF (match_operand:V2DF 1 "register_operand" "0")
		     (match_operand:V2DF 2 "nonimmediate_operand" "xm"))
	  (match_dup 1)
	  (const_int 1)))]
  "TARGET_SSE2"
  "maxsd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "DF")])

(define_expand "sminv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "")
	(smin:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "")
		   (match_operand:V2DF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
{
  if (!flag_finite_math_only)
    operands[1] = force_reg (V2DFmode, operands[1]);
  ix86_fixup_binary_operands_no_copy (SMIN, V2DFmode, operands);
})

(define_insn "*sminv2df3_finite"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(smin:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "%0")
		   (match_operand:V2DF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && flag_finite_math_only
   && ix86_binary_operator_ok (SMIN, V2DFmode, operands)"
  "minpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V2DF")])

(define_insn "*sminv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(smin:V2DF (match_operand:V2DF 1 "register_operand" "0")
		   (match_operand:V2DF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "minpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_vmsminv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_merge:V2DF
	  (smin:V2DF (match_operand:V2DF 1 "register_operand" "0")
		     (match_operand:V2DF 2 "nonimmediate_operand" "xm"))
	  (match_dup 1)
	  (const_int 1)))]
  "TARGET_SSE2"
  "minsd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "DF")])

(define_insn "sse3_addsubv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_merge:V2DF
	  (plus:V2DF
	    (match_operand:V2DF 1 "register_operand" "0")
	    (match_operand:V2DF 2 "nonimmediate_operand" "xm"))
	  (minus:V2DF (match_dup 1) (match_dup 2))
	  (const_int 1)))]
  "TARGET_SSE3"
  "addsubpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V2DF")])

(define_insn "sse3_haddv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_concat:V2DF
	  (plus:DF
	    (vec_select:DF
	      (match_operand:V2DF 1 "register_operand" "0")
	      (parallel [(const_int 0)]))
	    (vec_select:DF (match_dup 1) (parallel [(const_int 1)])))
	  (plus:DF
	    (vec_select:DF
	      (match_operand:V2DF 2 "nonimmediate_operand" "xm")
	      (parallel [(const_int 0)]))
	    (vec_select:DF (match_dup 2) (parallel [(const_int 1)])))))]
  "TARGET_SSE3"
  "haddpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V2DF")])

(define_insn "sse3_hsubv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_concat:V2DF
	  (minus:DF
	    (vec_select:DF
	      (match_operand:V2DF 1 "register_operand" "0")
	      (parallel [(const_int 0)]))
	    (vec_select:DF (match_dup 1) (parallel [(const_int 1)])))
	  (minus:DF
	    (vec_select:DF
	      (match_operand:V2DF 2 "nonimmediate_operand" "xm")
	      (parallel [(const_int 0)]))
	    (vec_select:DF (match_dup 2) (parallel [(const_int 1)])))))]
  "TARGET_SSE3"
  "hsubpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseadd")
   (set_attr "mode" "V2DF")])

(define_expand "reduc_splus_v2df"
  [(match_operand:V2DF 0 "register_operand" "")
   (match_operand:V2DF 1 "register_operand" "")]
  "TARGET_SSE3"
{
  emit_insn (gen_sse3_haddv2df3 (operands[0], operands[1], operands[1]));
  DONE;
})

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel double-precision floating point comparisons
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "sse2_maskcmpv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(match_operator:V2DF 3 "sse_comparison_operator"
		[(match_operand:V2DF 1 "register_operand" "0")
		 (match_operand:V2DF 2 "nonimmediate_operand" "xm")]))]
  "TARGET_SSE2"
  "cmp%D3pd\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssecmp")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_vmmaskcmpv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_merge:V2DF
	  (match_operator:V2DF 3 "sse_comparison_operator"
		[(match_operand:V2DF 1 "register_operand" "0")
		 (match_operand:V2DF 2 "nonimmediate_operand" "xm")])
	  (match_dup 1)
	  (const_int 1)))]
  "TARGET_SSE2"
  "cmp%D3sd\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssecmp")
   (set_attr "mode" "DF")])

(define_insn "sse2_comi"
  [(set (reg:CCFP FLAGS_REG)
	(compare:CCFP
	  (vec_select:DF
	    (match_operand:V2DF 0 "register_operand" "x")
	    (parallel [(const_int 0)]))
	  (vec_select:DF
	    (match_operand:V2DF 1 "nonimmediate_operand" "xm")
	    (parallel [(const_int 0)]))))]
  "TARGET_SSE2"
  "comisd\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecomi")
   (set_attr "mode" "DF")])

(define_insn "sse2_ucomi"
  [(set (reg:CCFPU FLAGS_REG)
	(compare:CCFPU
	  (vec_select:DF
	    (match_operand:V2DF 0 "register_operand" "x")
	    (parallel [(const_int 0)]))
	  (vec_select:DF
	    (match_operand:V2DF 1 "nonimmediate_operand" "xm")
	    (parallel [(const_int 0)]))))]
  "TARGET_SSE2"
  "ucomisd\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecomi")
   (set_attr "mode" "DF")])

(define_expand "vcondv2df"
  [(set (match_operand:V2DF 0 "register_operand" "")
        (if_then_else:V2DF
          (match_operator 3 ""
            [(match_operand:V2DF 4 "nonimmediate_operand" "")
             (match_operand:V2DF 5 "nonimmediate_operand" "")])
          (match_operand:V2DF 1 "general_operand" "")
          (match_operand:V2DF 2 "general_operand" "")))]
  "TARGET_SSE2"
{
  if (ix86_expand_fp_vcond (operands))
    DONE;
  else
    FAIL;
})

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel double-precision floating point logical operations
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_expand "andv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "")
	(and:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "")
		  (match_operand:V2DF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (AND, V2DFmode, operands);")

(define_insn "*andv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(and:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "%0")
		  (match_operand:V2DF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (AND, V2DFmode, operands)"
  "andpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_nandv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(and:V2DF (not:V2DF (match_operand:V2DF 1 "register_operand" "0"))
		  (match_operand:V2DF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "andnpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V2DF")])

(define_expand "iorv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "")
	(ior:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "")
		  (match_operand:V2DF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (IOR, V2DFmode, operands);")

(define_insn "*iorv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(ior:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "%0")
		  (match_operand:V2DF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (IOR, V2DFmode, operands)"
  "orpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V2DF")])

(define_expand "xorv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "")
	(xor:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "")
		  (match_operand:V2DF 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (XOR, V2DFmode, operands);")

(define_insn "*xorv2df3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(xor:V2DF (match_operand:V2DF 1 "nonimmediate_operand" "%0")
		  (match_operand:V2DF 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (XOR, V2DFmode, operands)"
  "xorpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V2DF")])

;; Also define scalar versions.  These are used for abs, neg, and
;; conditional move.  Using subregs into vector modes causes register
;; allocation lossage.  These patterns do not allow memory operands
;; because the native instructions read the full 128-bits.

(define_insn "*anddf3"
  [(set (match_operand:DF 0 "register_operand" "=x")
	(and:DF (match_operand:DF 1 "register_operand" "0")
		(match_operand:DF 2 "register_operand" "x")))]
  "TARGET_SSE2"
  "andpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V2DF")])

(define_insn "*nanddf3"
  [(set (match_operand:DF 0 "register_operand" "=x")
	(and:DF (not:DF (match_operand:DF 1 "register_operand" "0"))
		(match_operand:DF 2 "register_operand" "x")))]
  "TARGET_SSE2"
  "andnpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V2DF")])

(define_insn "*iordf3"
  [(set (match_operand:DF 0 "register_operand" "=x")
	(ior:DF (match_operand:DF 1 "register_operand" "0")
		(match_operand:DF 2 "register_operand" "x")))]
  "TARGET_SSE2"
  "orpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V2DF")])

(define_insn "*xordf3"
  [(set (match_operand:DF 0 "register_operand" "=x")
	(xor:DF (match_operand:DF 1 "register_operand" "0")
		(match_operand:DF 2 "register_operand" "x")))]
  "TARGET_SSE2"
  "xorpd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "V2DF")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel double-precision floating point conversion operations
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "sse2_cvtpi2pd"
  [(set (match_operand:V2DF 0 "register_operand" "=x,x")
	(float:V2DF (match_operand:V2SI 1 "nonimmediate_operand" "y,m")))]
  "TARGET_SSE2"
  "cvtpi2pd\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "unit" "mmx,*")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_cvtpd2pi"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(unspec:V2SI [(match_operand:V2DF 1 "nonimmediate_operand" "xm")]
		     UNSPEC_FIX_NOTRUNC))]
  "TARGET_SSE2"
  "cvtpd2pi\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "unit" "mmx")
   (set_attr "mode" "DI")])

(define_insn "sse2_cvttpd2pi"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(fix:V2SI (match_operand:V2DF 1 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "cvttpd2pi\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "unit" "mmx")
   (set_attr "mode" "TI")])

(define_insn "sse2_cvtsi2sd"
  [(set (match_operand:V2DF 0 "register_operand" "=x,x")
	(vec_merge:V2DF
	  (vec_duplicate:V2DF
	    (float:DF (match_operand:SI 2 "nonimmediate_operand" "r,m")))
	  (match_operand:V2DF 1 "register_operand" "0,0")
	  (const_int 1)))]
  "TARGET_SSE2"
  "cvtsi2sd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseicvt")
   (set_attr "mode" "DF")
   (set_attr "athlon_decode" "double,direct")])

(define_insn "sse2_cvtsi2sdq"
  [(set (match_operand:V2DF 0 "register_operand" "=x,x")
	(vec_merge:V2DF
	  (vec_duplicate:V2DF
	    (float:DF (match_operand:DI 2 "nonimmediate_operand" "r,m")))
	  (match_operand:V2DF 1 "register_operand" "0,0")
	  (const_int 1)))]
  "TARGET_SSE2 && TARGET_64BIT"
  "cvtsi2sdq\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseicvt")
   (set_attr "mode" "DF")
   (set_attr "athlon_decode" "double,direct")])

(define_insn "sse2_cvtsd2si"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(unspec:SI
	  [(vec_select:DF
	     (match_operand:V2DF 1 "nonimmediate_operand" "x,m")
	     (parallel [(const_int 0)]))]
	  UNSPEC_FIX_NOTRUNC))]
  "TARGET_SSE2"
  "cvtsd2si\t{%1, %0|%0, %1}"
  [(set_attr "type" "sseicvt")
   (set_attr "athlon_decode" "double,vector")
   (set_attr "mode" "SI")])

(define_insn "sse2_cvtsd2siq"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(unspec:DI
	  [(vec_select:DF
	     (match_operand:V2DF 1 "nonimmediate_operand" "x,m")
	     (parallel [(const_int 0)]))]
	  UNSPEC_FIX_NOTRUNC))]
  "TARGET_SSE2 && TARGET_64BIT"
  "cvtsd2siq\t{%1, %0|%0, %1}"
  [(set_attr "type" "sseicvt")
   (set_attr "athlon_decode" "double,vector")
   (set_attr "mode" "DI")])

(define_insn "sse2_cvttsd2si"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(fix:SI
	  (vec_select:DF
	    (match_operand:V2DF 1 "nonimmediate_operand" "x,m")
	    (parallel [(const_int 0)]))))]
  "TARGET_SSE2"
  "cvttsd2si\t{%1, %0|%0, %1}"
  [(set_attr "type" "sseicvt")
   (set_attr "mode" "SI")
   (set_attr "athlon_decode" "double,vector")])

(define_insn "sse2_cvttsd2siq"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(fix:DI
	  (vec_select:DF
	    (match_operand:V2DF 1 "nonimmediate_operand" "x,m")
	    (parallel [(const_int 0)]))))]
  "TARGET_SSE2 && TARGET_64BIT"
  "cvttsd2siq\t{%1, %0|%0, %1}"
  [(set_attr "type" "sseicvt")
   (set_attr "mode" "DI")
   (set_attr "athlon_decode" "double,vector")])

(define_insn "sse2_cvtdq2pd"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(float:V2DF
	  (vec_select:V2SI
	    (match_operand:V4SI 1 "nonimmediate_operand" "xm")
	    (parallel [(const_int 0) (const_int 1)]))))]
  "TARGET_SSE2"
  "cvtdq2pd\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "V2DF")])

(define_expand "sse2_cvtpd2dq"
  [(set (match_operand:V4SI 0 "register_operand" "")
	(vec_concat:V4SI
	  (unspec:V2SI [(match_operand:V2DF 1 "nonimmediate_operand" "")]
		       UNSPEC_FIX_NOTRUNC)
	  (match_dup 2)))]
  "TARGET_SSE2"
  "operands[2] = CONST0_RTX (V2SImode);")

(define_insn "*sse2_cvtpd2dq"
  [(set (match_operand:V4SI 0 "register_operand" "=x")
	(vec_concat:V4SI
	  (unspec:V2SI [(match_operand:V2DF 1 "nonimmediate_operand" "xm")]
		       UNSPEC_FIX_NOTRUNC)
	  (match_operand:V2SI 2 "const0_operand" "")))]
  "TARGET_SSE2"
  "cvtpd2dq\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "TI")])

(define_expand "sse2_cvttpd2dq"
  [(set (match_operand:V4SI 0 "register_operand" "")
	(vec_concat:V4SI
	  (fix:V2SI (match_operand:V2DF 1 "nonimmediate_operand" ""))
	  (match_dup 2)))]
  "TARGET_SSE2"
  "operands[2] = CONST0_RTX (V2SImode);")

(define_insn "*sse2_cvttpd2dq"
  [(set (match_operand:V4SI 0 "register_operand" "=x")
	(vec_concat:V4SI
	  (fix:V2SI (match_operand:V2DF 1 "nonimmediate_operand" "xm"))
	  (match_operand:V2SI 2 "const0_operand" "")))]
  "TARGET_SSE2"
  "cvttpd2dq\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "TI")])

(define_insn "sse2_cvtsd2ss"
  [(set (match_operand:V4SF 0 "register_operand" "=x,x")
	(vec_merge:V4SF
	  (vec_duplicate:V4SF
	    (float_truncate:V2SF
	      (match_operand:V2DF 2 "nonimmediate_operand" "x,m")))
	  (match_operand:V4SF 1 "register_operand" "0,0")
	  (const_int 1)))]
  "TARGET_SSE2"
  "cvtsd2ss\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssecvt")
   (set_attr "athlon_decode" "vector,double")
   (set_attr "mode" "SF")])

(define_insn "sse2_cvtss2sd"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_merge:V2DF
	  (float_extend:V2DF
	    (vec_select:V2SF
	      (match_operand:V4SF 2 "nonimmediate_operand" "xm")
	      (parallel [(const_int 0) (const_int 1)])))
	  (match_operand:V2DF 1 "register_operand" "0")
	  (const_int 1)))]
  "TARGET_SSE2"
  "cvtss2sd\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "DF")])

(define_expand "sse2_cvtpd2ps"
  [(set (match_operand:V4SF 0 "register_operand" "")
	(vec_concat:V4SF
	  (float_truncate:V2SF
	    (match_operand:V2DF 1 "nonimmediate_operand" "xm"))
	  (match_dup 2)))]
  "TARGET_SSE2"
  "operands[2] = CONST0_RTX (V2SFmode);")

(define_insn "*sse2_cvtpd2ps"
  [(set (match_operand:V4SF 0 "register_operand" "=x")
	(vec_concat:V4SF
	  (float_truncate:V2SF
	    (match_operand:V2DF 1 "nonimmediate_operand" "xm"))
	  (match_operand:V2SF 2 "const0_operand" "")))]
  "TARGET_SSE2"
  "cvtpd2ps\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "V4SF")])

(define_insn "sse2_cvtps2pd"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(float_extend:V2DF
	  (vec_select:V2SF
	    (match_operand:V4SF 1 "nonimmediate_operand" "xm")
	    (parallel [(const_int 0) (const_int 1)]))))]
  "TARGET_SSE2"
  "cvtps2pd\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "V2DF")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel double-precision floating point element swizzling
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "sse2_unpckhpd"
  [(set (match_operand:V2DF 0 "nonimmediate_operand"     "=x,x,m")
	(vec_select:V2DF
	  (vec_concat:V4DF
	    (match_operand:V2DF 1 "nonimmediate_operand" " 0,o,x")
	    (match_operand:V2DF 2 "nonimmediate_operand" " x,0,0"))
	  (parallel [(const_int 1)
		     (const_int 3)])))]
  "TARGET_SSE2 && !(MEM_P (operands[1]) && MEM_P (operands[2]))"
  "@
   unpckhpd\t{%2, %0|%0, %2}
   movlpd\t{%H1, %0|%0, %H1}
   movhpd\t{%1, %0|%0, %1}"
  [(set_attr "type" "sselog,ssemov,ssemov")
   (set_attr "mode" "V2DF,V1DF,V1DF")])

(define_insn "*sse3_movddup"
  [(set (match_operand:V2DF 0 "nonimmediate_operand"     "=x,o")
	(vec_select:V2DF
	  (vec_concat:V4DF
	    (match_operand:V2DF 1 "nonimmediate_operand" "xm,x")
	    (match_dup 1))
	  (parallel [(const_int 0)
		     (const_int 2)])))]
  "TARGET_SSE3 && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "@
   movddup\t{%1, %0|%0, %1}
   #"
  [(set_attr "type" "sselog1,ssemov")
   (set_attr "mode" "V2DF")])

(define_split
  [(set (match_operand:V2DF 0 "memory_operand" "")
	(vec_select:V2DF
	  (vec_concat:V4DF
	    (match_operand:V2DF 1 "register_operand" "")
	    (match_dup 1))
	  (parallel [(const_int 0)
		     (const_int 2)])))]
  "TARGET_SSE3 && reload_completed"
  [(const_int 0)]
{
  rtx low = gen_rtx_REG (DFmode, REGNO (operands[1]));
  emit_move_insn (adjust_address (operands[0], DFmode, 0), low);
  emit_move_insn (adjust_address (operands[0], DFmode, 8), low);
  DONE;
})

(define_insn "sse2_unpcklpd"
  [(set (match_operand:V2DF 0 "nonimmediate_operand"     "=x,x,o")
	(vec_select:V2DF
	  (vec_concat:V4DF
	    (match_operand:V2DF 1 "nonimmediate_operand" " 0,0,0")
	    (match_operand:V2DF 2 "nonimmediate_operand" " x,m,x"))
	  (parallel [(const_int 0)
		     (const_int 2)])))]
  "TARGET_SSE2 && !(MEM_P (operands[1]) && MEM_P (operands[2]))"
  "@
   unpcklpd\t{%2, %0|%0, %2}
   movhpd\t{%2, %0|%0, %2}
   movlpd\t{%2, %H0|%H0, %2}"
  [(set_attr "type" "sselog,ssemov,ssemov")
   (set_attr "mode" "V2DF,V1DF,V1DF")])

(define_expand "sse2_shufpd"
  [(match_operand:V2DF 0 "register_operand" "")
   (match_operand:V2DF 1 "register_operand" "")
   (match_operand:V2DF 2 "nonimmediate_operand" "")
   (match_operand:SI 3 "const_int_operand" "")]
  "TARGET_SSE2"
{
  int mask = INTVAL (operands[3]);
  emit_insn (gen_sse2_shufpd_1 (operands[0], operands[1], operands[2],
				GEN_INT (mask & 1),
				GEN_INT (mask & 2 ? 3 : 2)));
  DONE;
})

(define_insn "sse2_shufpd_1"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_select:V2DF
	  (vec_concat:V4DF
	    (match_operand:V2DF 1 "register_operand" "0")
	    (match_operand:V2DF 2 "nonimmediate_operand" "xm"))
	  (parallel [(match_operand 3 "const_0_to_1_operand" "")
		     (match_operand 4 "const_2_to_3_operand" "")])))]
  "TARGET_SSE2"
{
  int mask;
  mask = INTVAL (operands[3]);
  mask |= (INTVAL (operands[4]) - 2) << 1;
  operands[3] = GEN_INT (mask);

  return "shufpd\t{%3, %2, %0|%0, %2, %3}";
}
  [(set_attr "type" "sselog")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_storehpd"
  [(set (match_operand:DF 0 "nonimmediate_operand"     "=m,x,x*fr")
	(vec_select:DF
	  (match_operand:V2DF 1 "nonimmediate_operand" " x,0,o")
	  (parallel [(const_int 1)])))]
  "TARGET_SSE2 && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "@
   movhpd\t{%1, %0|%0, %1}
   unpckhpd\t%0, %0
   #"
  [(set_attr "type" "ssemov,sselog1,ssemov")
   (set_attr "mode" "V1DF,V2DF,DF")])

(define_split
  [(set (match_operand:DF 0 "register_operand" "")
	(vec_select:DF
	  (match_operand:V2DF 1 "memory_operand" "")
	  (parallel [(const_int 1)])))]
  "TARGET_SSE2 && reload_completed"
  [(set (match_dup 0) (match_dup 1))]
{
  operands[1] = adjust_address (operands[1], DFmode, 8);
})

(define_insn "sse2_storelpd"
  [(set (match_operand:DF 0 "nonimmediate_operand"     "=m,x,x*fr")
	(vec_select:DF
	  (match_operand:V2DF 1 "nonimmediate_operand" " x,x,m")
	  (parallel [(const_int 0)])))]
  "TARGET_SSE2 && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "@
   movlpd\t{%1, %0|%0, %1}
   #
   #"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V1DF,DF,DF")])

(define_split
  [(set (match_operand:DF 0 "register_operand" "")
	(vec_select:DF
	  (match_operand:V2DF 1 "nonimmediate_operand" "")
	  (parallel [(const_int 0)])))]
  "TARGET_SSE2 && reload_completed"
  [(const_int 0)]
{
  rtx op1 = operands[1];
  if (REG_P (op1))
    op1 = gen_rtx_REG (DFmode, REGNO (op1));
  else
    op1 = gen_lowpart (DFmode, op1);
  emit_move_insn (operands[0], op1);
  DONE;
})

(define_insn "sse2_loadhpd"
  [(set (match_operand:V2DF 0 "nonimmediate_operand"     "=x,x,x,o")
	(vec_concat:V2DF
	  (vec_select:DF
	    (match_operand:V2DF 1 "nonimmediate_operand" " 0,0,x,0")
	    (parallel [(const_int 0)]))
	  (match_operand:DF 2 "nonimmediate_operand"     " m,x,0,x*fr")))]
  "TARGET_SSE2 && !(MEM_P (operands[1]) && MEM_P (operands[2]))"
  "@
   movhpd\t{%2, %0|%0, %2}
   unpcklpd\t{%2, %0|%0, %2}
   shufpd\t{$1, %1, %0|%0, %1, 1}
   #"
  [(set_attr "type" "ssemov,sselog,sselog,other")
   (set_attr "mode" "V1DF,V2DF,V2DF,DF")])

(define_split
  [(set (match_operand:V2DF 0 "memory_operand" "")
	(vec_concat:V2DF
	  (vec_select:DF (match_dup 0) (parallel [(const_int 0)]))
	  (match_operand:DF 1 "register_operand" "")))]
  "TARGET_SSE2 && reload_completed"
  [(set (match_dup 0) (match_dup 1))]
{
  operands[0] = adjust_address (operands[0], DFmode, 8);
})

(define_insn "sse2_loadlpd"
  [(set (match_operand:V2DF 0 "nonimmediate_operand"    "=x,x,x,x,x,m")
	(vec_concat:V2DF
	  (match_operand:DF 2 "nonimmediate_operand"    " m,m,x,0,0,x*fr")
	  (vec_select:DF
	    (match_operand:V2DF 1 "vector_move_operand" " C,0,0,x,o,0")
	    (parallel [(const_int 1)]))))]
  "TARGET_SSE2 && !(MEM_P (operands[1]) && MEM_P (operands[2]))"
  "@
   movsd\t{%2, %0|%0, %2}
   movlpd\t{%2, %0|%0, %2}
   movsd\t{%2, %0|%0, %2}
   shufpd\t{$2, %2, %0|%0, %2, 2}
   movhpd\t{%H1, %0|%0, %H1}
   #"
  [(set_attr "type" "ssemov,ssemov,ssemov,sselog,ssemov,other")
   (set_attr "mode" "DF,V1DF,V1DF,V2DF,V1DF,DF")])

(define_split
  [(set (match_operand:V2DF 0 "memory_operand" "")
	(vec_concat:V2DF
	  (match_operand:DF 1 "register_operand" "")
	  (vec_select:DF (match_dup 0) (parallel [(const_int 1)]))))]
  "TARGET_SSE2 && reload_completed"
  [(set (match_dup 0) (match_dup 1))]
{
  operands[0] = adjust_address (operands[0], DFmode, 8);
})

;; Not sure these two are ever used, but it doesn't hurt to have
;; them. -aoliva
(define_insn "*vec_extractv2df_1_sse"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=m,x,x")
	(vec_select:DF
	  (match_operand:V2DF 1 "nonimmediate_operand" "x,x,o")
	  (parallel [(const_int 1)])))]
  "!TARGET_SSE2 && TARGET_SSE
   && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "@
   movhps\t{%1, %0|%0, %1}
   movhlps\t{%1, %0|%0, %1}
   movlps\t{%H1, %0|%0, %H1}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V2SF,V4SF,V2SF")])

(define_insn "*vec_extractv2df_0_sse"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=m,x,x")
	(vec_select:DF
	  (match_operand:V2DF 1 "nonimmediate_operand" "x,x,m")
	  (parallel [(const_int 0)])))]
  "!TARGET_SSE2 && TARGET_SSE
   && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "@
   movlps\t{%1, %0|%0, %1}
   movaps\t{%1, %0|%0, %1}
   movlps\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V2SF,V4SF,V2SF")])

(define_insn "sse2_movsd"
  [(set (match_operand:V2DF 0 "nonimmediate_operand"   "=x,x,m,x,x,o")
	(vec_merge:V2DF
	  (match_operand:V2DF 2 "nonimmediate_operand" " x,m,x,0,0,0")
	  (match_operand:V2DF 1 "nonimmediate_operand" " 0,0,0,x,o,x")
	  (const_int 1)))]
  "TARGET_SSE2"
  "@
   movsd\t{%2, %0|%0, %2}
   movlpd\t{%2, %0|%0, %2}
   movlpd\t{%2, %0|%0, %2}
   shufpd\t{$2, %2, %0|%0, %2, 2}
   movhps\t{%H1, %0|%0, %H1}
   movhps\t{%1, %H0|%H0, %1}"
  [(set_attr "type" "ssemov,ssemov,ssemov,sselog,ssemov,ssemov")
   (set_attr "mode" "DF,V1DF,V1DF,V2DF,V1DF,V1DF")])

(define_insn "*vec_dupv2df_sse3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_duplicate:V2DF
	  (match_operand:DF 1 "nonimmediate_operand" "xm")))]
  "TARGET_SSE3"
  "movddup\t{%1, %0|%0, %1}"
  [(set_attr "type" "sselog1")
   (set_attr "mode" "DF")])

(define_insn "*vec_dupv2df"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_duplicate:V2DF
	  (match_operand:DF 1 "register_operand" "0")))]
  "TARGET_SSE2"
  "unpcklpd\t%0, %0"
  [(set_attr "type" "sselog1")
   (set_attr "mode" "V4SF")])

(define_insn "*vec_concatv2df_sse3"
  [(set (match_operand:V2DF 0 "register_operand" "=x")
	(vec_concat:V2DF
	  (match_operand:DF 1 "nonimmediate_operand" "xm")
	  (match_dup 1)))]
  "TARGET_SSE3"
  "movddup\t{%1, %0|%0, %1}"
  [(set_attr "type" "sselog1")
   (set_attr "mode" "DF")])

(define_insn "*vec_concatv2df"
  [(set (match_operand:V2DF 0 "register_operand"     "=Y,Y,Y,x,x")
	(vec_concat:V2DF
	  (match_operand:DF 1 "nonimmediate_operand" " 0,0,m,0,0")
	  (match_operand:DF 2 "vector_move_operand"  " Y,m,C,x,m")))]
  "TARGET_SSE"
  "@
   unpcklpd\t{%2, %0|%0, %2}
   movhpd\t{%2, %0|%0, %2}
   movsd\t{%1, %0|%0, %1}
   movlhps\t{%2, %0|%0, %2}
   movhps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog,ssemov,ssemov,ssemov,ssemov")
   (set_attr "mode" "V2DF,V1DF,DF,V4SF,V2SF")])

(define_expand "vec_setv2df"
  [(match_operand:V2DF 0 "register_operand" "")
   (match_operand:DF 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_set (false, operands[0], operands[1],
			  INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_extractv2df"
  [(match_operand:DF 0 "register_operand" "")
   (match_operand:V2DF 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_extract (false, operands[0], operands[1],
			      INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_initv2df"
  [(match_operand:V2DF 0 "register_operand" "")
   (match_operand 1 "" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_init (false, operands[0], operands[1]);
  DONE;
})

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel integral arithmetic
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_expand "neg<mode>2"
  [(set (match_operand:SSEMODEI 0 "register_operand" "")
	(minus:SSEMODEI
	  (match_dup 2)
	  (match_operand:SSEMODEI 1 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "operands[2] = force_reg (<MODE>mode, CONST0_RTX (<MODE>mode));")

(define_expand "add<mode>3"
  [(set (match_operand:SSEMODEI 0 "register_operand" "")
	(plus:SSEMODEI (match_operand:SSEMODEI 1 "nonimmediate_operand" "")
		       (match_operand:SSEMODEI 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (PLUS, <MODE>mode, operands);")

(define_insn "*add<mode>3"
  [(set (match_operand:SSEMODEI 0 "register_operand" "=x")
	(plus:SSEMODEI
	  (match_operand:SSEMODEI 1 "nonimmediate_operand" "%0")
	  (match_operand:SSEMODEI 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (PLUS, <MODE>mode, operands)"
  "padd<ssevecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_insn "sse2_ssadd<mode>3"
  [(set (match_operand:SSEMODE12 0 "register_operand" "=x")
	(ss_plus:SSEMODE12
	  (match_operand:SSEMODE12 1 "nonimmediate_operand" "%0")
	  (match_operand:SSEMODE12 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (SS_PLUS, <MODE>mode, operands)"
  "padds<ssevecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_insn "sse2_usadd<mode>3"
  [(set (match_operand:SSEMODE12 0 "register_operand" "=x")
	(us_plus:SSEMODE12
	  (match_operand:SSEMODE12 1 "nonimmediate_operand" "%0")
	  (match_operand:SSEMODE12 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (US_PLUS, <MODE>mode, operands)"
  "paddus<ssevecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_expand "sub<mode>3"
  [(set (match_operand:SSEMODEI 0 "register_operand" "")
	(minus:SSEMODEI (match_operand:SSEMODEI 1 "register_operand" "")
			(match_operand:SSEMODEI 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (MINUS, <MODE>mode, operands);")

(define_insn "*sub<mode>3"
  [(set (match_operand:SSEMODEI 0 "register_operand" "=x")
	(minus:SSEMODEI
	  (match_operand:SSEMODEI 1 "register_operand" "0")
	  (match_operand:SSEMODEI 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "psub<ssevecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_insn "sse2_sssub<mode>3"
  [(set (match_operand:SSEMODE12 0 "register_operand" "=x")
	(ss_minus:SSEMODE12
	  (match_operand:SSEMODE12 1 "register_operand" "0")
	  (match_operand:SSEMODE12 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "psubs<ssevecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_insn "sse2_ussub<mode>3"
  [(set (match_operand:SSEMODE12 0 "register_operand" "=x")
	(us_minus:SSEMODE12
	  (match_operand:SSEMODE12 1 "register_operand" "0")
	  (match_operand:SSEMODE12 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "psubus<ssevecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_expand "mulv16qi3"
  [(set (match_operand:V16QI 0 "register_operand" "")
	(mult:V16QI (match_operand:V16QI 1 "register_operand" "")
		    (match_operand:V16QI 2 "register_operand" "")))]
  "TARGET_SSE2"
{
  rtx t[12], op0;
  int i;

  for (i = 0; i < 12; ++i)
    t[i] = gen_reg_rtx (V16QImode);

  /* Unpack data such that we've got a source byte in each low byte of
     each word.  We don't care what goes into the high byte of each word.
     Rather than trying to get zero in there, most convenient is to let
     it be a copy of the low byte.  */
  emit_insn (gen_sse2_punpckhbw (t[0], operands[1], operands[1]));
  emit_insn (gen_sse2_punpckhbw (t[1], operands[2], operands[2]));
  emit_insn (gen_sse2_punpcklbw (t[2], operands[1], operands[1]));
  emit_insn (gen_sse2_punpcklbw (t[3], operands[2], operands[2]));

  /* Multiply words.  The end-of-line annotations here give a picture of what
     the output of that instruction looks like.  Dot means don't care; the 
     letters are the bytes of the result with A being the most significant.  */
  emit_insn (gen_mulv8hi3 (gen_lowpart (V8HImode, t[4]), /* .A.B.C.D.E.F.G.H */
			   gen_lowpart (V8HImode, t[0]),
			   gen_lowpart (V8HImode, t[1])));
  emit_insn (gen_mulv8hi3 (gen_lowpart (V8HImode, t[5]), /* .I.J.K.L.M.N.O.P */
			   gen_lowpart (V8HImode, t[2]),
			   gen_lowpart (V8HImode, t[3])));

  /* Extract the relevant bytes and merge them back together.  */
  emit_insn (gen_sse2_punpckhbw (t[6], t[5], t[4]));	/* ..AI..BJ..CK..DL */
  emit_insn (gen_sse2_punpcklbw (t[7], t[5], t[4]));	/* ..EM..FN..GO..HP */
  emit_insn (gen_sse2_punpckhbw (t[8], t[7], t[6]));	/* ....AEIM....BFJN */
  emit_insn (gen_sse2_punpcklbw (t[9], t[7], t[6]));	/* ....CGKO....DHLP */
  emit_insn (gen_sse2_punpckhbw (t[10], t[9], t[8]));	/* ........ACEGIKMO */
  emit_insn (gen_sse2_punpcklbw (t[11], t[9], t[8]));	/* ........BDFHJLNP */

  op0 = operands[0];
  emit_insn (gen_sse2_punpcklbw (op0, t[11], t[10]));	/* ABCDEFGHIJKLMNOP */
  DONE;
})

(define_expand "mulv8hi3"
  [(set (match_operand:V8HI 0 "register_operand" "")
	(mult:V8HI (match_operand:V8HI 1 "nonimmediate_operand" "")
		   (match_operand:V8HI 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (MULT, V8HImode, operands);")

(define_insn "*mulv8hi3"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(mult:V8HI (match_operand:V8HI 1 "nonimmediate_operand" "%0")
		   (match_operand:V8HI 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (MULT, V8HImode, operands)"
  "pmullw\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseimul")
   (set_attr "mode" "TI")])

(define_insn "sse2_smulv8hi3_highpart"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(truncate:V8HI
	  (lshiftrt:V8SI
	    (mult:V8SI
	      (sign_extend:V8SI
		(match_operand:V8HI 1 "nonimmediate_operand" "%0"))
	      (sign_extend:V8SI
		(match_operand:V8HI 2 "nonimmediate_operand" "xm")))
	    (const_int 16))))]
  "TARGET_SSE2 && ix86_binary_operator_ok (MULT, V8HImode, operands)"
  "pmulhw\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseimul")
   (set_attr "mode" "TI")])

(define_insn "sse2_umulv8hi3_highpart"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(truncate:V8HI
	  (lshiftrt:V8SI
	    (mult:V8SI
	      (zero_extend:V8SI
		(match_operand:V8HI 1 "nonimmediate_operand" "%0"))
	      (zero_extend:V8SI
		(match_operand:V8HI 2 "nonimmediate_operand" "xm")))
	    (const_int 16))))]
  "TARGET_SSE2 && ix86_binary_operator_ok (MULT, V8HImode, operands)"
  "pmulhuw\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseimul")
   (set_attr "mode" "TI")])

(define_insn "sse2_umulv2siv2di3"
  [(set (match_operand:V2DI 0 "register_operand" "=x")
	(mult:V2DI
	  (zero_extend:V2DI
	    (vec_select:V2SI
	      (match_operand:V4SI 1 "nonimmediate_operand" "%0")
	      (parallel [(const_int 0) (const_int 2)])))
	  (zero_extend:V2DI
	    (vec_select:V2SI
	      (match_operand:V4SI 2 "nonimmediate_operand" "xm")
	      (parallel [(const_int 0) (const_int 2)])))))]
  "TARGET_SSE2 && ix86_binary_operator_ok (MULT, V8HImode, operands)"
  "pmuludq\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseimul")
   (set_attr "mode" "TI")])

(define_insn "sse2_pmaddwd"
  [(set (match_operand:V4SI 0 "register_operand" "=x")
	(plus:V4SI
	  (mult:V4SI
	    (sign_extend:V4SI
	      (vec_select:V4HI
		(match_operand:V8HI 1 "nonimmediate_operand" "%0")
		(parallel [(const_int 0)
			   (const_int 2)
			   (const_int 4)
			   (const_int 6)])))
	    (sign_extend:V4SI
	      (vec_select:V4HI
		(match_operand:V8HI 2 "nonimmediate_operand" "xm")
		(parallel [(const_int 0)
			   (const_int 2)
			   (const_int 4)
			   (const_int 6)]))))
	  (mult:V4SI
	    (sign_extend:V4SI
	      (vec_select:V4HI (match_dup 1)
		(parallel [(const_int 1)
			   (const_int 3)
			   (const_int 5)
			   (const_int 7)])))
	    (sign_extend:V4SI
	      (vec_select:V4HI (match_dup 2)
		(parallel [(const_int 1)
			   (const_int 3)
			   (const_int 5)
			   (const_int 7)]))))))]
  "TARGET_SSE2"
  "pmaddwd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_expand "mulv4si3"
  [(set (match_operand:V4SI 0 "register_operand" "")
	(mult:V4SI (match_operand:V4SI 1 "register_operand" "")
		   (match_operand:V4SI 2 "register_operand" "")))]
  "TARGET_SSE2"
{
  rtx t1, t2, t3, t4, t5, t6, thirtytwo;
  rtx op0, op1, op2;

  op0 = operands[0];
  op1 = operands[1];
  op2 = operands[2];
  t1 = gen_reg_rtx (V4SImode);
  t2 = gen_reg_rtx (V4SImode);
  t3 = gen_reg_rtx (V4SImode);
  t4 = gen_reg_rtx (V4SImode);
  t5 = gen_reg_rtx (V4SImode);
  t6 = gen_reg_rtx (V4SImode);
  thirtytwo = GEN_INT (32);

  /* Multiply elements 2 and 0.  */
  emit_insn (gen_sse2_umulv2siv2di3 (gen_lowpart (V2DImode, t1), op1, op2));

  /* Shift both input vectors down one element, so that elements 3 and 1
     are now in the slots for elements 2 and 0.  For K8, at least, this is
     faster than using a shuffle.  */
  emit_insn (gen_sse2_lshrti3 (gen_lowpart (TImode, t2),
			       gen_lowpart (TImode, op1), thirtytwo));
  emit_insn (gen_sse2_lshrti3 (gen_lowpart (TImode, t3),
			       gen_lowpart (TImode, op2), thirtytwo));

  /* Multiply elements 3 and 1.  */
  emit_insn (gen_sse2_umulv2siv2di3 (gen_lowpart (V2DImode, t4), t2, t3));

  /* Move the results in element 2 down to element 1; we don't care what
     goes in elements 2 and 3.  */
  emit_insn (gen_sse2_pshufd_1 (t5, t1, const0_rtx, const2_rtx,
				const0_rtx, const0_rtx));
  emit_insn (gen_sse2_pshufd_1 (t6, t4, const0_rtx, const2_rtx,
				const0_rtx, const0_rtx));

  /* Merge the parts back together.  */
  emit_insn (gen_sse2_punpckldq (op0, t5, t6));
  DONE;
})

(define_expand "mulv2di3"
  [(set (match_operand:V2DI 0 "register_operand" "")
	(mult:V2DI (match_operand:V2DI 1 "register_operand" "")
		   (match_operand:V2DI 2 "register_operand" "")))]
  "TARGET_SSE2"
{
  rtx t1, t2, t3, t4, t5, t6, thirtytwo;
  rtx op0, op1, op2;

  op0 = operands[0];
  op1 = operands[1];
  op2 = operands[2];
  t1 = gen_reg_rtx (V2DImode);
  t2 = gen_reg_rtx (V2DImode);
  t3 = gen_reg_rtx (V2DImode);
  t4 = gen_reg_rtx (V2DImode);
  t5 = gen_reg_rtx (V2DImode);
  t6 = gen_reg_rtx (V2DImode);
  thirtytwo = GEN_INT (32);

  /* Multiply low parts.  */
  emit_insn (gen_sse2_umulv2siv2di3 (t1, gen_lowpart (V4SImode, op1),
				     gen_lowpart (V4SImode, op2)));

  /* Shift input vectors left 32 bits so we can multiply high parts.  */
  emit_insn (gen_lshrv2di3 (t2, op1, thirtytwo));
  emit_insn (gen_lshrv2di3 (t3, op2, thirtytwo));

  /* Multiply high parts by low parts.  */
  emit_insn (gen_sse2_umulv2siv2di3 (t4, gen_lowpart (V4SImode, op1),
				     gen_lowpart (V4SImode, t3)));
  emit_insn (gen_sse2_umulv2siv2di3 (t5, gen_lowpart (V4SImode, op2),
				     gen_lowpart (V4SImode, t2)));

  /* Shift them back.  */
  emit_insn (gen_ashlv2di3 (t4, t4, thirtytwo));
  emit_insn (gen_ashlv2di3 (t5, t5, thirtytwo));

  /* Add the three parts together.  */
  emit_insn (gen_addv2di3 (t6, t1, t4));
  emit_insn (gen_addv2di3 (op0, t6, t5));
  DONE;
})

(define_expand "sdot_prodv8hi"
  [(match_operand:V4SI 0 "register_operand" "")
   (match_operand:V8HI 1 "nonimmediate_operand" "")
   (match_operand:V8HI 2 "nonimmediate_operand" "")
   (match_operand:V4SI 3 "register_operand" "")]
  "TARGET_SSE2"
{
  rtx t = gen_reg_rtx (V4SImode);
  emit_insn (gen_sse2_pmaddwd (t, operands[1], operands[2]));
  emit_insn (gen_addv4si3 (operands[0], operands[3], t));
  DONE;
})

(define_expand "udot_prodv4si"
  [(match_operand:V2DI 0 "register_operand" "") 
   (match_operand:V4SI 1 "register_operand" "") 
   (match_operand:V4SI 2 "register_operand" "")
   (match_operand:V2DI 3 "register_operand" "")]
  "TARGET_SSE2"
{
  rtx t1, t2, t3, t4;

  t1 = gen_reg_rtx (V2DImode);
  emit_insn (gen_sse2_umulv2siv2di3 (t1, operands[1], operands[2]));
  emit_insn (gen_addv2di3 (t1, t1, operands[3]));

  t2 = gen_reg_rtx (V4SImode);
  t3 = gen_reg_rtx (V4SImode);
  emit_insn (gen_sse2_lshrti3 (gen_lowpart (TImode, t2),
                               gen_lowpart (TImode, operands[1]),
                               GEN_INT (32)));
  emit_insn (gen_sse2_lshrti3 (gen_lowpart (TImode, t3),
                               gen_lowpart (TImode, operands[2]),
                               GEN_INT (32)));

  t4 = gen_reg_rtx (V2DImode);
  emit_insn (gen_sse2_umulv2siv2di3 (t4, t2, t3));

  emit_insn (gen_addv2di3 (operands[0], t1, t4));
  DONE;
})

(define_insn "ashr<mode>3"
  [(set (match_operand:SSEMODE24 0 "register_operand" "=x")
	(ashiftrt:SSEMODE24
	  (match_operand:SSEMODE24 1 "register_operand" "0")
	  (match_operand:TI 2 "nonmemory_operand" "xn")))]
  "TARGET_SSE2"
  "psra<ssevecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseishft")
   (set_attr "mode" "TI")])

(define_insn "lshr<mode>3"
  [(set (match_operand:SSEMODE248 0 "register_operand" "=x")
	(lshiftrt:SSEMODE248
	  (match_operand:SSEMODE248 1 "register_operand" "0")
	  (match_operand:TI 2 "nonmemory_operand" "xn")))]
  "TARGET_SSE2"
  "psrl<ssevecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseishft")
   (set_attr "mode" "TI")])

(define_insn "ashl<mode>3"
  [(set (match_operand:SSEMODE248 0 "register_operand" "=x")
	(ashift:SSEMODE248
	  (match_operand:SSEMODE248 1 "register_operand" "0")
	  (match_operand:TI 2 "nonmemory_operand" "xn")))]
  "TARGET_SSE2"
  "psll<ssevecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseishft")
   (set_attr "mode" "TI")])

(define_insn "sse2_ashlti3"
  [(set (match_operand:TI 0 "register_operand" "=x")
	(ashift:TI (match_operand:TI 1 "register_operand" "0")
		   (match_operand:SI 2 "const_0_to_255_mul_8_operand" "n")))]
  "TARGET_SSE2"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) / 8);
  return "pslldq\t{%2, %0|%0, %2}";
}
  [(set_attr "type" "sseishft")
   (set_attr "mode" "TI")])

(define_expand "vec_shl_<mode>"
  [(set (match_operand:SSEMODEI 0 "register_operand" "")
        (ashift:TI (match_operand:SSEMODEI 1 "register_operand" "")
		   (match_operand:SI 2 "general_operand" "")))]
  "TARGET_SSE2"
{
  if (!const_0_to_255_mul_8_operand (operands[2], SImode))
    FAIL;
  operands[0] = gen_lowpart (TImode, operands[0]);
  operands[1] = gen_lowpart (TImode, operands[1]);
})

(define_insn "sse2_lshrti3"
  [(set (match_operand:TI 0 "register_operand" "=x")
 	(lshiftrt:TI (match_operand:TI 1 "register_operand" "0")
		     (match_operand:SI 2 "const_0_to_255_mul_8_operand" "n")))]
  "TARGET_SSE2"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) / 8);
  return "psrldq\t{%2, %0|%0, %2}";
}
  [(set_attr "type" "sseishft")
   (set_attr "mode" "TI")])

(define_expand "vec_shr_<mode>"
  [(set (match_operand:SSEMODEI 0 "register_operand" "")
        (lshiftrt:TI (match_operand:SSEMODEI 1 "register_operand" "")
		     (match_operand:SI 2 "general_operand" "")))]
  "TARGET_SSE2"
{
  if (!const_0_to_255_mul_8_operand (operands[2], SImode))
    FAIL;
  operands[0] = gen_lowpart (TImode, operands[0]);
  operands[1] = gen_lowpart (TImode, operands[1]);
})

(define_expand "umaxv16qi3"
  [(set (match_operand:V16QI 0 "register_operand" "")
	(umax:V16QI (match_operand:V16QI 1 "nonimmediate_operand" "")
		    (match_operand:V16QI 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (UMAX, V16QImode, operands);")

(define_insn "*umaxv16qi3"
  [(set (match_operand:V16QI 0 "register_operand" "=x")
	(umax:V16QI (match_operand:V16QI 1 "nonimmediate_operand" "%0")
		    (match_operand:V16QI 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (UMAX, V16QImode, operands)"
  "pmaxub\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_expand "smaxv8hi3"
  [(set (match_operand:V8HI 0 "register_operand" "")
	(smax:V8HI (match_operand:V8HI 1 "nonimmediate_operand" "")
		   (match_operand:V8HI 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (SMAX, V8HImode, operands);")

(define_insn "*smaxv8hi3"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(smax:V8HI (match_operand:V8HI 1 "nonimmediate_operand" "%0")
		   (match_operand:V8HI 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (SMAX, V8HImode, operands)"
  "pmaxsw\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_expand "umaxv8hi3"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(us_minus:V8HI (match_operand:V8HI 1 "register_operand" "0")
		       (match_operand:V8HI 2 "nonimmediate_operand" "xm")))
   (set (match_dup 3)
	(plus:V8HI (match_dup 0) (match_dup 2)))]
  "TARGET_SSE2"
{
  operands[3] = operands[0];
  if (rtx_equal_p (operands[0], operands[2]))
    operands[0] = gen_reg_rtx (V8HImode);
})

(define_expand "smax<mode>3"
  [(set (match_operand:SSEMODE14 0 "register_operand" "")
	(smax:SSEMODE14 (match_operand:SSEMODE14 1 "register_operand" "")
			(match_operand:SSEMODE14 2 "register_operand" "")))]
  "TARGET_SSE2"
{
  rtx xops[6];
  bool ok;

  xops[0] = operands[0];
  xops[1] = operands[1];
  xops[2] = operands[2];
  xops[3] = gen_rtx_GT (VOIDmode, operands[1], operands[2]);
  xops[4] = operands[1];
  xops[5] = operands[2];
  ok = ix86_expand_int_vcond (xops);
  gcc_assert (ok);
  DONE;
})

(define_expand "umaxv4si3"
  [(set (match_operand:V4SI 0 "register_operand" "")
	(umax:V4SI (match_operand:V4SI 1 "register_operand" "")
		   (match_operand:V4SI 2 "register_operand" "")))]
  "TARGET_SSE2"
{
  rtx xops[6];
  bool ok;

  xops[0] = operands[0];
  xops[1] = operands[1];
  xops[2] = operands[2];
  xops[3] = gen_rtx_GTU (VOIDmode, operands[1], operands[2]);
  xops[4] = operands[1];
  xops[5] = operands[2];
  ok = ix86_expand_int_vcond (xops);
  gcc_assert (ok);
  DONE;
})

(define_expand "uminv16qi3"
  [(set (match_operand:V16QI 0 "register_operand" "")
	(umin:V16QI (match_operand:V16QI 1 "nonimmediate_operand" "")
		    (match_operand:V16QI 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (UMIN, V16QImode, operands);")

(define_insn "*uminv16qi3"
  [(set (match_operand:V16QI 0 "register_operand" "=x")
	(umin:V16QI (match_operand:V16QI 1 "nonimmediate_operand" "%0")
		    (match_operand:V16QI 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (UMIN, V16QImode, operands)"
  "pminub\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_expand "sminv8hi3"
  [(set (match_operand:V8HI 0 "register_operand" "")
	(smin:V8HI (match_operand:V8HI 1 "nonimmediate_operand" "")
		   (match_operand:V8HI 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (SMIN, V8HImode, operands);")

(define_insn "*sminv8hi3"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(smin:V8HI (match_operand:V8HI 1 "nonimmediate_operand" "%0")
		   (match_operand:V8HI 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (SMIN, V8HImode, operands)"
  "pminsw\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_expand "smin<mode>3"
  [(set (match_operand:SSEMODE14 0 "register_operand" "")
	(smin:SSEMODE14 (match_operand:SSEMODE14 1 "register_operand" "")
			(match_operand:SSEMODE14 2 "register_operand" "")))]
  "TARGET_SSE2"
{
  rtx xops[6];
  bool ok;

  xops[0] = operands[0];
  xops[1] = operands[2];
  xops[2] = operands[1];
  xops[3] = gen_rtx_GT (VOIDmode, operands[1], operands[2]);
  xops[4] = operands[1];
  xops[5] = operands[2];
  ok = ix86_expand_int_vcond (xops);
  gcc_assert (ok);
  DONE;
})

(define_expand "umin<mode>3"
  [(set (match_operand:SSEMODE24 0 "register_operand" "")
	(umin:SSEMODE24 (match_operand:SSEMODE24 1 "register_operand" "")
			(match_operand:SSEMODE24 2 "register_operand" "")))]
  "TARGET_SSE2"
{
  rtx xops[6];
  bool ok;

  xops[0] = operands[0];
  xops[1] = operands[2];
  xops[2] = operands[1];
  xops[3] = gen_rtx_GTU (VOIDmode, operands[1], operands[2]);
  xops[4] = operands[1];
  xops[5] = operands[2];
  ok = ix86_expand_int_vcond (xops);
  gcc_assert (ok);
  DONE;
})

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel integral comparisons
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "sse2_eq<mode>3"
  [(set (match_operand:SSEMODE124 0 "register_operand" "=x")
	(eq:SSEMODE124
	  (match_operand:SSEMODE124 1 "nonimmediate_operand" "%0")
	  (match_operand:SSEMODE124 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (EQ, <MODE>mode, operands)"
  "pcmpeq<ssevecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssecmp")
   (set_attr "mode" "TI")])

(define_insn "sse2_gt<mode>3"
  [(set (match_operand:SSEMODE124 0 "register_operand" "=x")
	(gt:SSEMODE124
	  (match_operand:SSEMODE124 1 "register_operand" "0")
	  (match_operand:SSEMODE124 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "pcmpgt<ssevecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssecmp")
   (set_attr "mode" "TI")])

(define_expand "vcond<mode>"
  [(set (match_operand:SSEMODE124 0 "register_operand" "")
        (if_then_else:SSEMODE124
          (match_operator 3 ""
            [(match_operand:SSEMODE124 4 "nonimmediate_operand" "")
             (match_operand:SSEMODE124 5 "nonimmediate_operand" "")])
          (match_operand:SSEMODE124 1 "general_operand" "")
          (match_operand:SSEMODE124 2 "general_operand" "")))]
  "TARGET_SSE2"
{
  if (ix86_expand_int_vcond (operands))
    DONE;
  else
    FAIL;
})

(define_expand "vcondu<mode>"
  [(set (match_operand:SSEMODE124 0 "register_operand" "")
        (if_then_else:SSEMODE124
          (match_operator 3 ""
            [(match_operand:SSEMODE124 4 "nonimmediate_operand" "")
             (match_operand:SSEMODE124 5 "nonimmediate_operand" "")])
          (match_operand:SSEMODE124 1 "general_operand" "")
          (match_operand:SSEMODE124 2 "general_operand" "")))]
  "TARGET_SSE2"
{
  if (ix86_expand_int_vcond (operands))
    DONE;
  else
    FAIL;
})

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel integral logical operations
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_expand "one_cmpl<mode>2"
  [(set (match_operand:SSEMODEI 0 "register_operand" "")
	(xor:SSEMODEI (match_operand:SSEMODEI 1 "nonimmediate_operand" "")
		      (match_dup 2)))]
  "TARGET_SSE2"
{
  int i, n = GET_MODE_NUNITS (<MODE>mode);
  rtvec v = rtvec_alloc (n);

  for (i = 0; i < n; ++i)
    RTVEC_ELT (v, i) = constm1_rtx;

  operands[2] = force_reg (<MODE>mode, gen_rtx_CONST_VECTOR (<MODE>mode, v));
})

(define_expand "and<mode>3"
  [(set (match_operand:SSEMODEI 0 "register_operand" "")
	(and:SSEMODEI (match_operand:SSEMODEI 1 "nonimmediate_operand" "")
		      (match_operand:SSEMODEI 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (AND, <MODE>mode, operands);")

(define_insn "*and<mode>3"
  [(set (match_operand:SSEMODEI 0 "register_operand" "=x")
	(and:SSEMODEI
	  (match_operand:SSEMODEI 1 "nonimmediate_operand" "%0")
	  (match_operand:SSEMODEI 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (AND, <MODE>mode, operands)"
  "pand\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_insn "sse2_nand<mode>3"
  [(set (match_operand:SSEMODEI 0 "register_operand" "=x")
	(and:SSEMODEI
	  (not:SSEMODEI (match_operand:SSEMODEI 1 "register_operand" "0"))
	  (match_operand:SSEMODEI 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2"
  "pandn\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_expand "ior<mode>3"
  [(set (match_operand:SSEMODEI 0 "register_operand" "")
	(ior:SSEMODEI (match_operand:SSEMODEI 1 "nonimmediate_operand" "")
		      (match_operand:SSEMODEI 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (IOR, <MODE>mode, operands);")

(define_insn "*ior<mode>3"
  [(set (match_operand:SSEMODEI 0 "register_operand" "=x")
	(ior:SSEMODEI
	  (match_operand:SSEMODEI 1 "nonimmediate_operand" "%0")
	  (match_operand:SSEMODEI 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (IOR, <MODE>mode, operands)"
  "por\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_expand "xor<mode>3"
  [(set (match_operand:SSEMODEI 0 "register_operand" "")
	(xor:SSEMODEI (match_operand:SSEMODEI 1 "nonimmediate_operand" "")
		      (match_operand:SSEMODEI 2 "nonimmediate_operand" "")))]
  "TARGET_SSE2"
  "ix86_fixup_binary_operands_no_copy (XOR, <MODE>mode, operands);")

(define_insn "*xor<mode>3"
  [(set (match_operand:SSEMODEI 0 "register_operand" "=x")
	(xor:SSEMODEI
	  (match_operand:SSEMODEI 1 "nonimmediate_operand" "%0")
	  (match_operand:SSEMODEI 2 "nonimmediate_operand" "xm")))]
  "TARGET_SSE2 && ix86_binary_operator_ok (XOR, <MODE>mode, operands)"
  "pxor\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel integral element swizzling
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "sse2_packsswb"
  [(set (match_operand:V16QI 0 "register_operand" "=x")
	(vec_concat:V16QI
	  (ss_truncate:V8QI
	    (match_operand:V8HI 1 "register_operand" "0"))
	  (ss_truncate:V8QI
	    (match_operand:V8HI 2 "nonimmediate_operand" "xm"))))]
  "TARGET_SSE2"
  "packsswb\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_insn "sse2_packssdw"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(vec_concat:V8HI
	  (ss_truncate:V4HI
	    (match_operand:V4SI 1 "register_operand" "0"))
	  (ss_truncate:V4HI
	    (match_operand:V4SI 2 "nonimmediate_operand" "xm"))))]
  "TARGET_SSE2"
  "packssdw\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_insn "sse2_packuswb"
  [(set (match_operand:V16QI 0 "register_operand" "=x")
	(vec_concat:V16QI
	  (us_truncate:V8QI
	    (match_operand:V8HI 1 "register_operand" "0"))
	  (us_truncate:V8QI
	    (match_operand:V8HI 2 "nonimmediate_operand" "xm"))))]
  "TARGET_SSE2"
  "packuswb\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_insn "sse2_punpckhbw"
  [(set (match_operand:V16QI 0 "register_operand" "=x")
	(vec_select:V16QI
	  (vec_concat:V32QI
	    (match_operand:V16QI 1 "register_operand" "0")
	    (match_operand:V16QI 2 "nonimmediate_operand" "xm"))
	  (parallel [(const_int 8)  (const_int 24)
		     (const_int 9)  (const_int 25)
		     (const_int 10) (const_int 26)
		     (const_int 11) (const_int 27)
		     (const_int 12) (const_int 28) 
		     (const_int 13) (const_int 29)
		     (const_int 14) (const_int 30)
		     (const_int 15) (const_int 31)])))]
  "TARGET_SSE2"
  "punpckhbw\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_insn "sse2_punpcklbw"
  [(set (match_operand:V16QI 0 "register_operand" "=x")
	(vec_select:V16QI
	  (vec_concat:V32QI
	    (match_operand:V16QI 1 "register_operand" "0")
	    (match_operand:V16QI 2 "nonimmediate_operand" "xm"))
	  (parallel [(const_int 0) (const_int 16)
		     (const_int 1) (const_int 17)
		     (const_int 2) (const_int 18)
		     (const_int 3) (const_int 19)
		     (const_int 4) (const_int 20)
		     (const_int 5) (const_int 21)
		     (const_int 6) (const_int 22)
		     (const_int 7) (const_int 23)])))]
  "TARGET_SSE2"
  "punpcklbw\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_insn "sse2_punpckhwd"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(vec_select:V8HI
	  (vec_concat:V16HI
	    (match_operand:V8HI 1 "register_operand" "0")
	    (match_operand:V8HI 2 "nonimmediate_operand" "xm"))
	  (parallel [(const_int 4) (const_int 12)
		     (const_int 5) (const_int 13)
		     (const_int 6) (const_int 14)
		     (const_int 7) (const_int 15)])))]
  "TARGET_SSE2"
  "punpckhwd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_insn "sse2_punpcklwd"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(vec_select:V8HI
	  (vec_concat:V16HI
	    (match_operand:V8HI 1 "register_operand" "0")
	    (match_operand:V8HI 2 "nonimmediate_operand" "xm"))
	  (parallel [(const_int 0) (const_int 8)
		     (const_int 1) (const_int 9)
		     (const_int 2) (const_int 10)
		     (const_int 3) (const_int 11)])))]
  "TARGET_SSE2"
  "punpcklwd\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_insn "sse2_punpckhdq"
  [(set (match_operand:V4SI 0 "register_operand" "=x")
	(vec_select:V4SI
	  (vec_concat:V8SI
	    (match_operand:V4SI 1 "register_operand" "0")
	    (match_operand:V4SI 2 "nonimmediate_operand" "xm"))
	  (parallel [(const_int 2) (const_int 6)
		     (const_int 3) (const_int 7)])))]
  "TARGET_SSE2"
  "punpckhdq\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_insn "sse2_punpckldq"
  [(set (match_operand:V4SI 0 "register_operand" "=x")
	(vec_select:V4SI
	  (vec_concat:V8SI
	    (match_operand:V4SI 1 "register_operand" "0")
	    (match_operand:V4SI 2 "nonimmediate_operand" "xm"))
	  (parallel [(const_int 0) (const_int 4)
		     (const_int 1) (const_int 5)])))]
  "TARGET_SSE2"
  "punpckldq\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_insn "sse2_punpckhqdq"
  [(set (match_operand:V2DI 0 "register_operand" "=x")
	(vec_select:V2DI
	  (vec_concat:V4DI
	    (match_operand:V2DI 1 "register_operand" "0")
	    (match_operand:V2DI 2 "nonimmediate_operand" "xm"))
	  (parallel [(const_int 1)
		     (const_int 3)])))]
  "TARGET_SSE2"
  "punpckhqdq\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_insn "sse2_punpcklqdq"
  [(set (match_operand:V2DI 0 "register_operand" "=x")
	(vec_select:V2DI
	  (vec_concat:V4DI
	    (match_operand:V2DI 1 "register_operand" "0")
	    (match_operand:V2DI 2 "nonimmediate_operand" "xm"))
	  (parallel [(const_int 0)
		     (const_int 2)])))]
  "TARGET_SSE2"
  "punpcklqdq\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_expand "sse2_pinsrw"
  [(set (match_operand:V8HI 0 "register_operand" "")
	(vec_merge:V8HI
	  (vec_duplicate:V8HI
	    (match_operand:SI 2 "nonimmediate_operand" ""))
	  (match_operand:V8HI 1 "register_operand" "")
	  (match_operand:SI 3 "const_0_to_7_operand" "")))]
  "TARGET_SSE2"
{
  operands[2] = gen_lowpart (HImode, operands[2]);
  operands[3] = GEN_INT ((1 << INTVAL (operands[3])));
})

(define_insn "*sse2_pinsrw"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(vec_merge:V8HI
	  (vec_duplicate:V8HI
	    (match_operand:HI 2 "nonimmediate_operand" "rm"))
	  (match_operand:V8HI 1 "register_operand" "0")
	  (match_operand:SI 3 "const_pow2_1_to_128_operand" "n")))]
  "TARGET_SSE2"
{
  operands[3] = GEN_INT (exact_log2 (INTVAL (operands[3])));
  return "pinsrw\t{%3, %k2, %0|%0, %k2, %3}";
}
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_insn "sse2_pextrw"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI
	  (vec_select:HI
	    (match_operand:V8HI 1 "register_operand" "x")
	    (parallel [(match_operand:SI 2 "const_0_to_7_operand" "n")]))))]
  "TARGET_SSE2"
  "pextrw\t{%2, %1, %0|%0, %1, %2}"
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_expand "sse2_pshufd"
  [(match_operand:V4SI 0 "register_operand" "")
   (match_operand:V4SI 1 "nonimmediate_operand" "")
   (match_operand:SI 2 "const_int_operand" "")]
  "TARGET_SSE2"
{
  int mask = INTVAL (operands[2]);
  emit_insn (gen_sse2_pshufd_1 (operands[0], operands[1],
				GEN_INT ((mask >> 0) & 3),
				GEN_INT ((mask >> 2) & 3),
				GEN_INT ((mask >> 4) & 3),
				GEN_INT ((mask >> 6) & 3)));
  DONE;
})

(define_insn "sse2_pshufd_1"
  [(set (match_operand:V4SI 0 "register_operand" "=x")
	(vec_select:V4SI
	  (match_operand:V4SI 1 "nonimmediate_operand" "xm")
	  (parallel [(match_operand 2 "const_0_to_3_operand" "")
		     (match_operand 3 "const_0_to_3_operand" "")
		     (match_operand 4 "const_0_to_3_operand" "")
		     (match_operand 5 "const_0_to_3_operand" "")])))]
  "TARGET_SSE2"
{
  int mask = 0;
  mask |= INTVAL (operands[2]) << 0;
  mask |= INTVAL (operands[3]) << 2;
  mask |= INTVAL (operands[4]) << 4;
  mask |= INTVAL (operands[5]) << 6;
  operands[2] = GEN_INT (mask);

  return "pshufd\t{%2, %1, %0|%0, %1, %2}";
}
  [(set_attr "type" "sselog1")
   (set_attr "mode" "TI")])

(define_expand "sse2_pshuflw"
  [(match_operand:V8HI 0 "register_operand" "")
   (match_operand:V8HI 1 "nonimmediate_operand" "")
   (match_operand:SI 2 "const_int_operand" "")]
  "TARGET_SSE2"
{
  int mask = INTVAL (operands[2]);
  emit_insn (gen_sse2_pshuflw_1 (operands[0], operands[1],
				 GEN_INT ((mask >> 0) & 3),
				 GEN_INT ((mask >> 2) & 3),
				 GEN_INT ((mask >> 4) & 3),
				 GEN_INT ((mask >> 6) & 3)));
  DONE;
})

(define_insn "sse2_pshuflw_1"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(vec_select:V8HI
	  (match_operand:V8HI 1 "nonimmediate_operand" "xm")
	  (parallel [(match_operand 2 "const_0_to_3_operand" "")
		     (match_operand 3 "const_0_to_3_operand" "")
		     (match_operand 4 "const_0_to_3_operand" "")
		     (match_operand 5 "const_0_to_3_operand" "")
		     (const_int 4)
		     (const_int 5)
		     (const_int 6)
		     (const_int 7)])))]
  "TARGET_SSE2"
{
  int mask = 0;
  mask |= INTVAL (operands[2]) << 0;
  mask |= INTVAL (operands[3]) << 2;
  mask |= INTVAL (operands[4]) << 4;
  mask |= INTVAL (operands[5]) << 6;
  operands[2] = GEN_INT (mask);

  return "pshuflw\t{%2, %1, %0|%0, %1, %2}";
}
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_expand "sse2_pshufhw"
  [(match_operand:V8HI 0 "register_operand" "")
   (match_operand:V8HI 1 "nonimmediate_operand" "")
   (match_operand:SI 2 "const_int_operand" "")]
  "TARGET_SSE2"
{
  int mask = INTVAL (operands[2]);
  emit_insn (gen_sse2_pshufhw_1 (operands[0], operands[1],
				 GEN_INT (((mask >> 0) & 3) + 4),
				 GEN_INT (((mask >> 2) & 3) + 4),
				 GEN_INT (((mask >> 4) & 3) + 4),
				 GEN_INT (((mask >> 6) & 3) + 4)));
  DONE;
})

(define_insn "sse2_pshufhw_1"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(vec_select:V8HI
	  (match_operand:V8HI 1 "nonimmediate_operand" "xm")
	  (parallel [(const_int 0)
		     (const_int 1)
		     (const_int 2)
		     (const_int 3)
		     (match_operand 2 "const_4_to_7_operand" "")
		     (match_operand 3 "const_4_to_7_operand" "")
		     (match_operand 4 "const_4_to_7_operand" "")
		     (match_operand 5 "const_4_to_7_operand" "")])))]
  "TARGET_SSE2"
{
  int mask = 0;
  mask |= (INTVAL (operands[2]) - 4) << 0;
  mask |= (INTVAL (operands[3]) - 4) << 2;
  mask |= (INTVAL (operands[4]) - 4) << 4;
  mask |= (INTVAL (operands[5]) - 4) << 6;
  operands[2] = GEN_INT (mask);

  return "pshufhw\t{%2, %1, %0|%0, %1, %2}";
}
  [(set_attr "type" "sselog")
   (set_attr "mode" "TI")])

(define_expand "sse2_loadd"
  [(set (match_operand:V4SI 0 "register_operand" "")
	(vec_merge:V4SI
	  (vec_duplicate:V4SI
	    (match_operand:SI 1 "nonimmediate_operand" ""))
	  (match_dup 2)
	  (const_int 1)))]
  "TARGET_SSE"
  "operands[2] = CONST0_RTX (V4SImode);")

(define_insn "sse2_loadld"
  [(set (match_operand:V4SI 0 "register_operand"       "=Y,x,x")
	(vec_merge:V4SI
	  (vec_duplicate:V4SI
	    (match_operand:SI 2 "nonimmediate_operand" "mr,m,x"))
	  (match_operand:V4SI 1 "reg_or_0_operand"     " C,C,0")
	  (const_int 1)))]
  "TARGET_SSE"
  "@
   movd\t{%2, %0|%0, %2}
   movss\t{%2, %0|%0, %2}
   movss\t{%2, %0|%0, %2}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "TI,V4SF,SF")])

;; ??? The hardware supports more, but TARGET_INTER_UNIT_MOVES must
;; be taken into account, and movdi isn't fully populated even without.
(define_insn_and_split "sse2_stored"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=mx")
	(vec_select:SI
	  (match_operand:V4SI 1 "register_operand" "x")
	  (parallel [(const_int 0)])))]
  "TARGET_SSE"
  "#"
  "&& reload_completed"
  [(set (match_dup 0) (match_dup 1))]
{
  operands[1] = gen_rtx_REG (SImode, REGNO (operands[1]));
})

(define_expand "sse_storeq"
  [(set (match_operand:DI 0 "nonimmediate_operand" "")
	(vec_select:DI
	  (match_operand:V2DI 1 "register_operand" "")
	  (parallel [(const_int 0)])))]
  "TARGET_SSE"
  "")

;; ??? The hardware supports more, but TARGET_INTER_UNIT_MOVES must
;; be taken into account, and movdi isn't fully populated even without.
(define_insn "*sse2_storeq"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=mx")
	(vec_select:DI
	  (match_operand:V2DI 1 "register_operand" "x")
	  (parallel [(const_int 0)])))]
  "TARGET_SSE"
  "#")

(define_split
  [(set (match_operand:DI 0 "nonimmediate_operand" "")
	(vec_select:DI
	  (match_operand:V2DI 1 "register_operand" "")
	  (parallel [(const_int 0)])))]
  "TARGET_SSE && reload_completed"
  [(set (match_dup 0) (match_dup 1))]
{
  operands[1] = gen_rtx_REG (DImode, REGNO (operands[1]));
})

(define_insn "*vec_extractv2di_1_sse2"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=m,x,x")
	(vec_select:DI
	  (match_operand:V2DI 1 "nonimmediate_operand" "x,0,o")
	  (parallel [(const_int 1)])))]
  "TARGET_SSE2 && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "@
   movhps\t{%1, %0|%0, %1}
   psrldq\t{$8, %0|%0, 8}
   movq\t{%H1, %0|%0, %H1}"
  [(set_attr "type" "ssemov,sseishft,ssemov")
   (set_attr "memory" "*,none,*")
   (set_attr "mode" "V2SF,TI,TI")])

;; Not sure this is ever used, but it doesn't hurt to have it. -aoliva
(define_insn "*vec_extractv2di_1_sse"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=m,x,x")
	(vec_select:DI
	  (match_operand:V2DI 1 "nonimmediate_operand" "x,x,o")
	  (parallel [(const_int 1)])))]
  "!TARGET_SSE2 && TARGET_SSE
   && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "@
   movhps\t{%1, %0|%0, %1}
   movhlps\t{%1, %0|%0, %1}
   movlps\t{%H1, %0|%0, %H1}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "V2SF,V4SF,V2SF")])

(define_insn "*vec_dupv4si"
  [(set (match_operand:V4SI 0 "register_operand" "=Y,x")
	(vec_duplicate:V4SI
	  (match_operand:SI 1 "register_operand" " Y,0")))]
  "TARGET_SSE"
  "@
   pshufd\t{$0, %1, %0|%0, %1, 0}
   shufps\t{$0, %0, %0|%0, %0, 0}"
  [(set_attr "type" "sselog1")
   (set_attr "mode" "TI,V4SF")])

(define_insn "*vec_dupv2di"
  [(set (match_operand:V2DI 0 "register_operand" "=Y,x")
	(vec_duplicate:V2DI
	  (match_operand:DI 1 "register_operand" " 0,0")))]
  "TARGET_SSE"
  "@
   punpcklqdq\t%0, %0
   movlhps\t%0, %0"
  [(set_attr "type" "sselog1,ssemov")
   (set_attr "mode" "TI,V4SF")])

;; ??? In theory we can match memory for the MMX alternative, but allowing
;; nonimmediate_operand for operand 2 and *not* allowing memory for the SSE
;; alternatives pretty much forces the MMX alternative to be chosen.
(define_insn "*sse2_concatv2si"
  [(set (match_operand:V2SI 0 "register_operand"     "=Y, Y,*y,*y")
	(vec_concat:V2SI
	  (match_operand:SI 1 "nonimmediate_operand" " 0,rm, 0,rm")
	  (match_operand:SI 2 "reg_or_0_operand"     " Y, C,*y, C")))]
  "TARGET_SSE2"
  "@
   punpckldq\t{%2, %0|%0, %2}
   movd\t{%1, %0|%0, %1}
   punpckldq\t{%2, %0|%0, %2}
   movd\t{%1, %0|%0, %1}"
  [(set_attr "type" "sselog,ssemov,mmxcvt,mmxmov")
   (set_attr "mode" "TI,TI,DI,DI")])

(define_insn "*sse1_concatv2si"
  [(set (match_operand:V2SI 0 "register_operand"     "=x,x,*y,*y")
	(vec_concat:V2SI
	  (match_operand:SI 1 "nonimmediate_operand" " 0,m, 0,*rm")
	  (match_operand:SI 2 "reg_or_0_operand"     " x,C,*y,C")))]
  "TARGET_SSE"
  "@
   unpcklps\t{%2, %0|%0, %2}
   movss\t{%1, %0|%0, %1}
   punpckldq\t{%2, %0|%0, %2}
   movd\t{%1, %0|%0, %1}"
  [(set_attr "type" "sselog,ssemov,mmxcvt,mmxmov")
   (set_attr "mode" "V4SF,V4SF,DI,DI")])

(define_insn "*vec_concatv4si_1"
  [(set (match_operand:V4SI 0 "register_operand"       "=Y,x,x")
	(vec_concat:V4SI
	  (match_operand:V2SI 1 "register_operand"     " 0,0,0")
	  (match_operand:V2SI 2 "nonimmediate_operand" " Y,x,m")))]
  "TARGET_SSE"
  "@
   punpcklqdq\t{%2, %0|%0, %2}
   movlhps\t{%2, %0|%0, %2}
   movhps\t{%2, %0|%0, %2}"
  [(set_attr "type" "sselog,ssemov,ssemov")
   (set_attr "mode" "TI,V4SF,V2SF")])

(define_insn "*vec_concatv2di"
  [(set (match_operand:V2DI 0 "register_operand"     "=Y,?Y,Y,x,x,x")
	(vec_concat:V2DI
	  (match_operand:DI 1 "nonimmediate_operand" " m,*y,0,0,0,m")
	  (match_operand:DI 2 "vector_move_operand"  " C, C,Y,x,m,0")))]
  "TARGET_SSE"
  "@
   movq\t{%1, %0|%0, %1}
   movq2dq\t{%1, %0|%0, %1}
   punpcklqdq\t{%2, %0|%0, %2}
   movlhps\t{%2, %0|%0, %2}
   movhps\t{%2, %0|%0, %2}
   movlps\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssemov,ssemov,sselog,ssemov,ssemov,ssemov")
   (set_attr "mode" "TI,TI,TI,V4SF,V2SF,V2SF")])

(define_expand "vec_setv2di"
  [(match_operand:V2DI 0 "register_operand" "")
   (match_operand:DI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_set (false, operands[0], operands[1],
			  INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_extractv2di"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:V2DI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_extract (false, operands[0], operands[1],
			      INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_initv2di"
  [(match_operand:V2DI 0 "register_operand" "")
   (match_operand 1 "" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_init (false, operands[0], operands[1]);
  DONE;
})

(define_expand "vec_setv4si"
  [(match_operand:V4SI 0 "register_operand" "")
   (match_operand:SI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_set (false, operands[0], operands[1],
			  INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_extractv4si"
  [(match_operand:SI 0 "register_operand" "")
   (match_operand:V4SI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_extract (false, operands[0], operands[1],
			      INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_initv4si"
  [(match_operand:V4SI 0 "register_operand" "")
   (match_operand 1 "" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_init (false, operands[0], operands[1]);
  DONE;
})

(define_expand "vec_setv8hi"
  [(match_operand:V8HI 0 "register_operand" "")
   (match_operand:HI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_set (false, operands[0], operands[1],
			  INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_extractv8hi"
  [(match_operand:HI 0 "register_operand" "")
   (match_operand:V8HI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_extract (false, operands[0], operands[1],
			      INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_initv8hi"
  [(match_operand:V8HI 0 "register_operand" "")
   (match_operand 1 "" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_init (false, operands[0], operands[1]);
  DONE;
})

(define_expand "vec_setv16qi"
  [(match_operand:V16QI 0 "register_operand" "")
   (match_operand:QI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_set (false, operands[0], operands[1],
			  INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_extractv16qi"
  [(match_operand:QI 0 "register_operand" "")
   (match_operand:V16QI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_extract (false, operands[0], operands[1],
			      INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_initv16qi"
  [(match_operand:V16QI 0 "register_operand" "")
   (match_operand 1 "" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_init (false, operands[0], operands[1]);
  DONE;
})

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Miscellaneous
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "sse2_uavgv16qi3"
  [(set (match_operand:V16QI 0 "register_operand" "=x")
	(truncate:V16QI
	  (lshiftrt:V16HI
	    (plus:V16HI
	      (plus:V16HI
		(zero_extend:V16HI
		  (match_operand:V16QI 1 "nonimmediate_operand" "%0"))
		(zero_extend:V16HI
		  (match_operand:V16QI 2 "nonimmediate_operand" "xm")))
	      (const_vector:V16QI [(const_int 1) (const_int 1)
				   (const_int 1) (const_int 1)
				   (const_int 1) (const_int 1)
				   (const_int 1) (const_int 1)
				   (const_int 1) (const_int 1)
				   (const_int 1) (const_int 1)
				   (const_int 1) (const_int 1)
				   (const_int 1) (const_int 1)]))
	    (const_int 1))))]
  "TARGET_SSE2 && ix86_binary_operator_ok (PLUS, V16QImode, operands)"
  "pavgb\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_insn "sse2_uavgv8hi3"
  [(set (match_operand:V8HI 0 "register_operand" "=x")
	(truncate:V8HI
	  (lshiftrt:V8SI
	    (plus:V8SI
	      (plus:V8SI
		(zero_extend:V8SI
		  (match_operand:V8HI 1 "nonimmediate_operand" "%0"))
		(zero_extend:V8SI
		  (match_operand:V8HI 2 "nonimmediate_operand" "xm")))
	      (const_vector:V8HI [(const_int 1) (const_int 1)
				  (const_int 1) (const_int 1)
				  (const_int 1) (const_int 1)
				  (const_int 1) (const_int 1)]))
	    (const_int 1))))]
  "TARGET_SSE2 && ix86_binary_operator_ok (PLUS, V8HImode, operands)"
  "pavgw\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

;; The correct representation for this is absolutely enormous, and 
;; surely not generally useful.
(define_insn "sse2_psadbw"
  [(set (match_operand:V2DI 0 "register_operand" "=x")
	(unspec:V2DI [(match_operand:V16QI 1 "register_operand" "0")
		      (match_operand:V16QI 2 "nonimmediate_operand" "xm")]
		     UNSPEC_PSADBW))]
  "TARGET_SSE2"
  "psadbw\t{%2, %0|%0, %2}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "TI")])

(define_insn "sse_movmskps"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(match_operand:V4SF 1 "register_operand" "x")]
		   UNSPEC_MOVMSK))]
  "TARGET_SSE"
  "movmskps\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "V4SF")])

(define_insn "sse2_movmskpd"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(match_operand:V2DF 1 "register_operand" "x")]
		   UNSPEC_MOVMSK))]
  "TARGET_SSE2"
  "movmskpd\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "V2DF")])

(define_insn "sse2_pmovmskb"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(match_operand:V16QI 1 "register_operand" "x")]
		   UNSPEC_MOVMSK))]
  "TARGET_SSE2"
  "pmovmskb\t{%1, %0|%0, %1}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "V2DF")])

(define_expand "sse2_maskmovdqu"
  [(set (match_operand:V16QI 0 "memory_operand" "")
	(unspec:V16QI [(match_operand:V16QI 1 "register_operand" "x")
		       (match_operand:V16QI 2 "register_operand" "x")
		       (match_dup 0)]
		      UNSPEC_MASKMOV))]
  "TARGET_SSE2"
  "")

(define_insn "*sse2_maskmovdqu"
  [(set (mem:V16QI (match_operand:SI 0 "register_operand" "D"))
	(unspec:V16QI [(match_operand:V16QI 1 "register_operand" "x")
		       (match_operand:V16QI 2 "register_operand" "x")
		       (mem:V16QI (match_dup 0))]
		      UNSPEC_MASKMOV))]
  "TARGET_SSE2 && !TARGET_64BIT"
  ;; @@@ check ordering of operands in intel/nonintel syntax
  "maskmovdqu\t{%2, %1|%1, %2}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "TI")])

(define_insn "*sse2_maskmovdqu_rex64"
  [(set (mem:V16QI (match_operand:DI 0 "register_operand" "D"))
	(unspec:V16QI [(match_operand:V16QI 1 "register_operand" "x")
		       (match_operand:V16QI 2 "register_operand" "x")
		       (mem:V16QI (match_dup 0))]
		      UNSPEC_MASKMOV))]
  "TARGET_SSE2 && TARGET_64BIT"
  ;; @@@ check ordering of operands in intel/nonintel syntax
  "maskmovdqu\t{%2, %1|%1, %2}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "TI")])

(define_insn "sse_ldmxcsr"
  [(unspec_volatile [(match_operand:SI 0 "memory_operand" "m")]
		    UNSPECV_LDMXCSR)]
  "TARGET_SSE"
  "ldmxcsr\t%0"
  [(set_attr "type" "sse")
   (set_attr "memory" "load")])

(define_insn "sse_stmxcsr"
  [(set (match_operand:SI 0 "memory_operand" "=m")
	(unspec_volatile:SI [(const_int 0)] UNSPECV_STMXCSR))]
  "TARGET_SSE"
  "stmxcsr\t%0"
  [(set_attr "type" "sse")
   (set_attr "memory" "store")])

(define_expand "sse_sfence"
  [(set (match_dup 0)
	(unspec:BLK [(match_dup 0)] UNSPEC_SFENCE))]
  "TARGET_SSE || TARGET_3DNOW_A"
{
  operands[0] = gen_rtx_MEM (BLKmode, gen_rtx_SCRATCH (Pmode));
  MEM_VOLATILE_P (operands[0]) = 1;
})

(define_insn "*sse_sfence"
  [(set (match_operand:BLK 0 "" "")
	(unspec:BLK [(match_dup 0)] UNSPEC_SFENCE))]
  "TARGET_SSE || TARGET_3DNOW_A"
  "sfence"
  [(set_attr "type" "sse")
   (set_attr "memory" "unknown")])

(define_insn "sse2_clflush"
  [(unspec_volatile [(match_operand 0 "address_operand" "p")]
		    UNSPECV_CLFLUSH)]
  "TARGET_SSE2"
  "clflush\t%a0"
  [(set_attr "type" "sse")
   (set_attr "memory" "unknown")])

(define_expand "sse2_mfence"
  [(set (match_dup 0)
	(unspec:BLK [(match_dup 0)] UNSPEC_MFENCE))]
  "TARGET_SSE2"
{
  operands[0] = gen_rtx_MEM (BLKmode, gen_rtx_SCRATCH (Pmode));
  MEM_VOLATILE_P (operands[0]) = 1;
})

(define_insn "*sse2_mfence"
  [(set (match_operand:BLK 0 "" "")
	(unspec:BLK [(match_dup 0)] UNSPEC_MFENCE))]
  "TARGET_SSE2"
  "mfence"
  [(set_attr "type" "sse")
   (set_attr "memory" "unknown")])

(define_expand "sse2_lfence"
  [(set (match_dup 0)
	(unspec:BLK [(match_dup 0)] UNSPEC_LFENCE))]
  "TARGET_SSE2"
{
  operands[0] = gen_rtx_MEM (BLKmode, gen_rtx_SCRATCH (Pmode));
  MEM_VOLATILE_P (operands[0]) = 1;
})

(define_insn "*sse2_lfence"
  [(set (match_operand:BLK 0 "" "")
	(unspec:BLK [(match_dup 0)] UNSPEC_LFENCE))]
  "TARGET_SSE2"
  "lfence"
  [(set_attr "type" "sse")
   (set_attr "memory" "unknown")])

(define_insn "sse3_mwait"
  [(unspec_volatile [(match_operand:SI 0 "register_operand" "a")
		     (match_operand:SI 1 "register_operand" "c")]
		    UNSPECV_MWAIT)]
  "TARGET_SSE3"
;; 64bit version is "mwait %rax,%rcx". But only lower 32bits are used.
;; Since 32bit register operands are implicitly zero extended to 64bit,
;; we only need to set up 32bit registers.
  "mwait"
  [(set_attr "length" "3")])

(define_insn "sse3_monitor"
  [(unspec_volatile [(match_operand:SI 0 "register_operand" "a")
		     (match_operand:SI 1 "register_operand" "c")
		     (match_operand:SI 2 "register_operand" "d")]
		    UNSPECV_MONITOR)]
  "TARGET_SSE3 && !TARGET_64BIT"
  "monitor\t%0, %1, %2"
  [(set_attr "length" "3")])

(define_insn "sse3_monitor64"
  [(unspec_volatile [(match_operand:DI 0 "register_operand" "a")
		     (match_operand:SI 1 "register_operand" "c")
		     (match_operand:SI 2 "register_operand" "d")]
		    UNSPECV_MONITOR)]
  "TARGET_SSE3 && TARGET_64BIT"
;; 64bit version is "monitor %rax,%rcx,%rdx". But only lower 32bits in
;; RCX and RDX are used.  Since 32bit register operands are implicitly
;; zero extended to 64bit, we only need to set up 32bit registers.
  "monitor"
  [(set_attr "length" "3")])

;; IA-64 machine description for vector operations.
;; Copyright (C) 2004, 2005
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


;; Integer vector operations

(define_mode_macro VECINT [V8QI V4HI V2SI])
(define_mode_macro VECINT12 [V8QI V4HI])
(define_mode_macro VECINT24 [V4HI V2SI])
(define_mode_attr vecsize [(V8QI "1") (V4HI "2") (V2SI "4")])

(define_expand "mov<mode>"
  [(set (match_operand:VECINT 0 "general_operand" "")
        (match_operand:VECINT 1 "general_operand" ""))]
  ""
{
  rtx op1 = ia64_expand_move (operands[0], operands[1]);
  if (!op1)
    DONE;
  operands[1] = op1;
})

(define_insn "*mov<mode>_internal"
  [(set (match_operand:VECINT 0 "destination_operand"
					"=r,r,r,r,m ,*f ,*f,Q ,r ,*f")
	(match_operand:VECINT 1 "move_operand"
					"rU,W,i,m,rU,U*f,Q ,*f,*f,r "))]
  "ia64_move_ok (operands[0], operands[1])"
  "@
   mov %0 = %r1
   addl %0 = %v1, r0
   movl %0 = %v1
   ld8%O1 %0 = %1%P1
   st8%Q0 %0 = %r1%P0
   mov %0 = %F1
   ldf8 %0 = %1%P1
   stf8 %0 = %1%P0
   getf.sig %0 = %1
   setf.sig %0 = %1"
  [(set_attr "itanium_class" "ialu,ialu,long_i,ld,st,fmisc,fld,stf,frfr,tofr")])

(define_insn "one_cmpl<mode>2"
  [(set (match_operand:VECINT 0 "gr_register_operand" "=r")
	(not:VECINT (match_operand:VECINT 1 "gr_register_operand" "r")))]
  ""
  "andcm %0 = -1, %1"
  [(set_attr "itanium_class" "ilog")])

(define_insn "and<mode>3"
  [(set (match_operand:VECINT 0 "grfr_register_operand" "=r,*f")
	(and:VECINT
	  (match_operand:VECINT 1 "grfr_register_operand" "r,*f")
	  (match_operand:VECINT 2 "grfr_reg_or_8bit_operand" "r,*f")))]
  ""
  "@
   and %0 = %2, %1
   fand %0 = %2, %1"
  [(set_attr "itanium_class" "ilog,fmisc")])

(define_insn "*andnot<mode>"
  [(set (match_operand:VECINT 0 "grfr_register_operand" "=r,*f")
	(and:VECINT
	  (not:VECINT (match_operand:VECINT 1 "grfr_register_operand" "r,*f"))
	  (match_operand:VECINT 2 "grfr_reg_or_8bit_operand" "r,*f")))]
  ""
  "@
   andcm %0 = %2, %1
   fandcm %0 = %2, %1"
  [(set_attr "itanium_class" "ilog,fmisc")])

(define_insn "ior<mode>3"
  [(set (match_operand:VECINT 0 "grfr_register_operand" "=r,*f")
	(ior:VECINT
	  (match_operand:VECINT 1 "grfr_register_operand" "r,*f")
	  (match_operand:VECINT 2 "grfr_reg_or_8bit_operand" "r,*f")))]
  ""
  "@
   or %0 = %2, %1
   for %0 = %2, %1"
  [(set_attr "itanium_class" "ilog,fmisc")])

(define_insn "xor<mode>3"
  [(set (match_operand:VECINT 0 "grfr_register_operand" "=r,*f")
	(xor:VECINT
	  (match_operand:VECINT 1 "grfr_register_operand" "r,*f")
	  (match_operand:VECINT 2 "grfr_reg_or_8bit_operand" "r,*f")))]
  ""
  "@
   xor %0 = %2, %1
   fxor %0 = %2, %1"
  [(set_attr "itanium_class" "ilog,fmisc")])

(define_insn "neg<mode>2"
  [(set (match_operand:VECINT 0 "gr_register_operand" "=r")
	(neg:VECINT (match_operand:VECINT 1 "gr_register_operand" "r")))]
  ""
  "psub<vecsize> %0 = r0, %1"
  [(set_attr "itanium_class" "mmalua")])

(define_insn "add<mode>3"
  [(set (match_operand:VECINT 0 "gr_register_operand" "=r")
	(plus:VECINT (match_operand:VECINT 1 "gr_register_operand" "r")
		     (match_operand:VECINT 2 "gr_register_operand" "r")))]
  ""
  "padd<vecsize> %0 = %1, %2"
  [(set_attr "itanium_class" "mmalua")])

(define_insn "*ssadd<mode>3"
  [(set (match_operand:VECINT12 0 "gr_register_operand" "=r")
	(ss_plus:VECINT12
	  (match_operand:VECINT12 1 "gr_register_operand" "r")
	  (match_operand:VECINT12 2 "gr_register_operand" "r")))]
  ""
  "padd<vecsize>.sss %0 = %1, %2"
  [(set_attr "itanium_class" "mmalua")])

(define_insn "*usadd<mode>3"
  [(set (match_operand:VECINT12 0 "gr_register_operand" "=r")
	(us_plus:VECINT12
	  (match_operand:VECINT12 1 "gr_register_operand" "r")
	  (match_operand:VECINT12 2 "gr_register_operand" "r")))]
  ""
  "padd<vecsize>.uuu %0 = %1, %2"
  [(set_attr "itanium_class" "mmalua")])

(define_insn "sub<mode>3"
  [(set (match_operand:VECINT 0 "gr_register_operand" "=r")
	(minus:VECINT (match_operand:VECINT 1 "gr_register_operand" "r")
		      (match_operand:VECINT 2 "gr_register_operand" "r")))]
  ""
  "psub<vecsize> %0 = %1, %2"
  [(set_attr "itanium_class" "mmalua")])

(define_insn "*sssub<mode>3"
  [(set (match_operand:VECINT12 0 "gr_register_operand" "=r")
	(ss_minus:VECINT12
	  (match_operand:VECINT12 1 "gr_register_operand" "r")
	  (match_operand:VECINT12 2 "gr_register_operand" "r")))]
  ""
  "psub<vecsize>.sss %0 = %1, %2"
  [(set_attr "itanium_class" "mmalua")])

(define_insn "*ussub<mode>3"
  [(set (match_operand:VECINT12 0 "gr_register_operand" "=r")
	(us_minus:VECINT12
	  (match_operand:VECINT12 1 "gr_register_operand" "r")
	  (match_operand:VECINT12 2 "gr_register_operand" "r")))]
  ""
  "psub<vecsize>.uuu %0 = %1, %2"
  [(set_attr "itanium_class" "mmalua")])

(define_expand "mulv8qi3"
  [(set (match_operand:V8QI 0 "gr_register_operand" "")
	(mult:V8QI (match_operand:V8QI 1 "gr_register_operand" "r")
		   (match_operand:V8QI 2 "gr_register_operand" "r")))]
  ""
{
  rtx r1, l1, r2, l2, rm, lm;

  r1 = gen_reg_rtx (V4HImode);
  l1 = gen_reg_rtx (V4HImode);
  r2 = gen_reg_rtx (V4HImode);
  l2 = gen_reg_rtx (V4HImode);

  /* Zero-extend the QImode elements into two words of HImode elements
     by interleaving them with zero bytes.  */
  emit_insn (gen_mix1_r (gen_lowpart (V8QImode, r1),
                         operands[1], CONST0_RTX (V8QImode)));
  emit_insn (gen_mix1_r (gen_lowpart (V8QImode, r2),
                         operands[2], CONST0_RTX (V8QImode)));
  emit_insn (gen_mix1_l (gen_lowpart (V8QImode, l1),
                         operands[1], CONST0_RTX (V8QImode)));
  emit_insn (gen_mix1_l (gen_lowpart (V8QImode, l2),
                         operands[2], CONST0_RTX (V8QImode)));

  /* Multiply.  */
  rm = gen_reg_rtx (V4HImode);
  lm = gen_reg_rtx (V4HImode);
  emit_insn (gen_mulv4hi3 (rm, r1, r2));
  emit_insn (gen_mulv4hi3 (lm, l1, l2));

  /* Zap the high order bytes of the HImode elements by overwriting those
     in one part with the low order bytes of the other.  */
  emit_insn (gen_mix1_r (operands[0],
                         gen_lowpart (V8QImode, rm),
                         gen_lowpart (V8QImode, lm)));
  DONE;
})

(define_insn "mulv4hi3"
  [(set (match_operand:V4HI 0 "gr_register_operand" "=r")
	(mult:V4HI (match_operand:V4HI 1 "gr_register_operand" "r")
		   (match_operand:V4HI 2 "gr_register_operand" "r")))]
  ""
  "pmpyshr2 %0 = %1, %2, 0"
  [(set_attr "itanium_class" "mmmul")])

(define_insn "pmpy2_r"
  [(set (match_operand:V2SI 0 "gr_register_operand" "=r")
	(mult:V2SI
	  (vec_select:V2SI
	    (sign_extend:V4SI
	      (match_operand:V4HI 1 "gr_register_operand" "r"))
	    (parallel [(const_int 0) (const_int 2)]))
	  (vec_select:V2SI
	    (sign_extend:V4SI
	      (match_operand:V4HI 2 "gr_register_operand" "r"))
	    (parallel [(const_int 0) (const_int 2)]))))]
  ""
  "pmpy2.r %0 = %1, %2"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "pmpy2_l"
  [(set (match_operand:V2SI 0 "gr_register_operand" "=r")
	(mult:V2SI
	  (vec_select:V2SI
	    (sign_extend:V4SI
	      (match_operand:V4HI 1 "gr_register_operand" "r"))
	    (parallel [(const_int 1) (const_int 3)]))
	  (vec_select:V2SI
	    (sign_extend:V4SI
	      (match_operand:V4HI 2 "gr_register_operand" "r"))
	    (parallel [(const_int 1) (const_int 3)]))))]
  ""
  "pmpy2.l %0 = %1, %2"
  [(set_attr "itanium_class" "mmshf")])

(define_expand "umax<mode>3"
  [(set (match_operand:VECINT 0 "gr_register_operand" "")
	(umax:VECINT (match_operand:VECINT 1 "gr_register_operand" "")
		     (match_operand:VECINT 2 "gr_register_operand" "")))]
  ""
{
  if (ia64_expand_vecint_minmax (UMAX, <MODE>mode, operands))
    DONE;
})

(define_expand "smax<mode>3"
  [(set (match_operand:VECINT 0 "gr_register_operand" "")
	(smax:VECINT (match_operand:VECINT 1 "gr_reg_or_0_operand" "")
		     (match_operand:VECINT 2 "gr_reg_or_0_operand" "")))]
  ""
{
  if (ia64_expand_vecint_minmax (SMAX, <MODE>mode, operands))
    DONE;
})

(define_expand "umin<mode>3"
  [(set (match_operand:VECINT 0 "gr_register_operand" "")
	(umin:VECINT (match_operand:VECINT 1 "gr_register_operand" "")
		     (match_operand:VECINT 2 "gr_register_operand" "")))]
  ""
{
  if (ia64_expand_vecint_minmax (UMIN, <MODE>mode, operands))
    DONE;
})

(define_expand "smin<mode>3"
  [(set (match_operand:VECINT 0 "gr_register_operand" "")
	(smin:VECINT (match_operand:VECINT 1 "gr_reg_or_0_operand" "")
		     (match_operand:VECINT 2 "gr_reg_or_0_operand" "")))]
  ""
{
  if (ia64_expand_vecint_minmax (SMIN, <MODE>mode, operands))
    DONE;
})

(define_insn "*umaxv8qi3"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(umax:V8QI (match_operand:V8QI 1 "gr_register_operand" "r")
		   (match_operand:V8QI 2 "gr_register_operand" "r")))]
  ""
  "pmax1.u %0 = %1, %2"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*smaxv4hi3"
  [(set (match_operand:V4HI 0 "gr_register_operand" "=r")
	(smax:V4HI (match_operand:V4HI 1 "gr_reg_or_0_operand" "rU")
		   (match_operand:V4HI 2 "gr_reg_or_0_operand" "rU")))]
  ""
  "pmax2 %0 = %r1, %r2"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*uminv8qi3"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(umin:V8QI (match_operand:V8QI 1 "gr_register_operand" "r")
		   (match_operand:V8QI 2 "gr_register_operand" "r")))]
  ""
  "pmin1.u %0 = %1, %2"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*sminv4hi3"
  [(set (match_operand:V4HI 0 "gr_register_operand" "=r")
	(smin:V4HI (match_operand:V4HI 1 "gr_reg_or_0_operand" "rU")
		   (match_operand:V4HI 2 "gr_reg_or_0_operand" "rU")))]
  ""
  "pmin2 %0 = %r1, %r2"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "ashl<mode>3"
  [(set (match_operand:VECINT24 0 "gr_register_operand" "=r")
	(ashift:VECINT24
	  (match_operand:VECINT24 1 "gr_register_operand" "r")
	  (match_operand:DI 2 "gr_reg_or_5bit_operand" "rn")))]
  ""
  "pshl<vecsize> %0 = %1, %2"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "ashr<mode>3"
  [(set (match_operand:VECINT24 0 "gr_register_operand" "=r")
	(ashiftrt:VECINT24
	  (match_operand:VECINT24 1 "gr_register_operand" "r")
	  (match_operand:DI 2 "gr_reg_or_5bit_operand" "rn")))]
  ""
  "pshr<vecsize> %0 = %1, %2"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "lshr<mode>3"
  [(set (match_operand:VECINT24 0 "gr_register_operand" "=r")
	(lshiftrt:VECINT24
	  (match_operand:VECINT24 1 "gr_register_operand" "r")
	  (match_operand:DI 2 "gr_reg_or_5bit_operand" "rn")))]
  ""
  "pshr<vecsize>.u %0 = %1, %2"
  [(set_attr "itanium_class" "mmshf")])

(define_expand "vec_shl_<mode>"
  [(set (match_operand:VECINT 0 "gr_register_operand" "")
	(ashift:DI (match_operand:VECINT 1 "gr_register_operand" "")
		   (match_operand:DI 2 "gr_reg_or_6bit_operand" "")))]
  ""
{
  operands[0] = gen_lowpart (DImode, operands[0]);
  operands[1] = gen_lowpart (DImode, operands[1]);
})

(define_expand "vec_shr_<mode>"
  [(set (match_operand:VECINT 0 "gr_register_operand" "")
        (lshiftrt:DI (match_operand:VECINT 1 "gr_register_operand" "")
                     (match_operand:DI 2 "gr_reg_or_6bit_operand" "")))]
  ""
{
  operands[0] = gen_lowpart (DImode, operands[0]);
  operands[1] = gen_lowpart (DImode, operands[1]);
})

(define_expand "widen_usumv8qi3"
  [(match_operand:V4HI 0 "gr_register_operand" "")
   (match_operand:V8QI 1 "gr_register_operand" "")
   (match_operand:V4HI 2 "gr_register_operand" "")]
  ""
{
  ia64_expand_widen_sum (operands, true);
  DONE;
})

(define_expand "widen_usumv4hi3"
  [(match_operand:V2SI 0 "gr_register_operand" "")
   (match_operand:V4HI 1 "gr_register_operand" "")
   (match_operand:V2SI 2 "gr_register_operand" "")]
  ""
{
  ia64_expand_widen_sum (operands, true);
  DONE;
})

(define_expand "widen_ssumv8qi3"
  [(match_operand:V4HI 0 "gr_register_operand" "")
   (match_operand:V8QI 1 "gr_register_operand" "")
   (match_operand:V4HI 2 "gr_register_operand" "")]
  ""
{
  ia64_expand_widen_sum (operands, false);
  DONE;
})

(define_expand "widen_ssumv4hi3"
  [(match_operand:V2SI 0 "gr_register_operand" "")
   (match_operand:V4HI 1 "gr_register_operand" "")
   (match_operand:V2SI 2 "gr_register_operand" "")]
  ""
{
  ia64_expand_widen_sum (operands, false);
  DONE;
})

(define_expand "udot_prodv8qi"
  [(match_operand:V2SI 0 "gr_register_operand" "")
   (match_operand:V8QI 1 "gr_register_operand" "")
   (match_operand:V8QI 2 "gr_register_operand" "")
   (match_operand:V2SI 3 "gr_register_operand" "")]
  ""
{
  ia64_expand_dot_prod_v8qi (operands, true);
  DONE;
})

(define_expand "sdot_prodv8qi"
  [(match_operand:V2SI 0 "gr_register_operand" "")
   (match_operand:V8QI 1 "gr_register_operand" "")
   (match_operand:V8QI 2 "gr_register_operand" "")
   (match_operand:V2SI 3 "gr_register_operand" "")]
  ""
{
  ia64_expand_dot_prod_v8qi (operands, false);
  DONE;
})

(define_expand "sdot_prodv4hi"
  [(match_operand:V2SI 0 "gr_register_operand" "")
   (match_operand:V4HI 1 "gr_register_operand" "")
   (match_operand:V4HI 2 "gr_register_operand" "")
   (match_operand:V2SI 3 "gr_register_operand" "")]
  ""
{
  rtx l, r, t;

  r = gen_reg_rtx (V2SImode);
  l = gen_reg_rtx (V2SImode);
  t = gen_reg_rtx (V2SImode);

  emit_insn (gen_pmpy2_r (r, operands[1], operands[2]));
  emit_insn (gen_pmpy2_l (l, operands[1], operands[2]));
  emit_insn (gen_addv2si3 (t, r, operands[3]));
  emit_insn (gen_addv2si3 (operands[0], t, l));
  DONE;
})

(define_expand "vcond<mode>"
  [(set (match_operand:VECINT 0 "gr_register_operand" "")
	(if_then_else:VECINT
	  (match_operator 3 "" 
	    [(match_operand:VECINT 4 "gr_reg_or_0_operand" "")
	     (match_operand:VECINT 5 "gr_reg_or_0_operand" "")])
	  (match_operand:VECINT 1 "gr_reg_or_0_operand" "")
	  (match_operand:VECINT 2 "gr_reg_or_0_operand" "")))]
  ""
{
  ia64_expand_vecint_cmov (operands);
  DONE;
})

(define_expand "vcondu<mode>"
  [(set (match_operand:VECINT 0 "gr_register_operand" "")
	(if_then_else:VECINT
	  (match_operator 3 "" 
	    [(match_operand:VECINT 4 "gr_reg_or_0_operand" "")
	     (match_operand:VECINT 5 "gr_reg_or_0_operand" "")])
	  (match_operand:VECINT 1 "gr_reg_or_0_operand" "")
	  (match_operand:VECINT 2 "gr_reg_or_0_operand" "")))]
  ""
{
  ia64_expand_vecint_cmov (operands);
  DONE;
})

(define_insn "*cmpeq_<mode>"
  [(set (match_operand:VECINT 0 "gr_register_operand" "=r")
	(eq:VECINT (match_operand:VECINT 1 "gr_reg_or_0_operand" "rU")
		   (match_operand:VECINT 2 "gr_reg_or_0_operand" "rU")))]
  ""
  "pcmp<vecsize>.eq %0 = %r1, %r2"
  [(set_attr "itanium_class" "mmalua")])

(define_insn "*cmpgt_<mode>"
  [(set (match_operand:VECINT 0 "gr_register_operand" "=r")
	(gt:VECINT (match_operand:VECINT 1 "gr_reg_or_0_operand" "rU")
		   (match_operand:VECINT 2 "gr_reg_or_0_operand" "rU")))]
  ""
  "pcmp<vecsize>.gt %0 = %r1, %r2"
  [(set_attr "itanium_class" "mmalua")])

(define_insn "pack2_sss"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(vec_concat:V8QI
	  (ss_truncate:V4QI
	    (match_operand:V4HI 1 "gr_reg_or_0_operand" "rU"))
	  (ss_truncate:V4QI
	    (match_operand:V4HI 2 "gr_reg_or_0_operand" "rU"))))]
  ""
  "pack2.sss %0 = %r1, %r2"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*pack2_uss"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(vec_concat:V8QI
	  (us_truncate:V4QI
	    (match_operand:V4HI 1 "gr_reg_or_0_operand" "rU"))
	  (us_truncate:V4QI
	    (match_operand:V4HI 2 "gr_reg_or_0_operand" "rU"))))]
  ""
  "pack2.uss %0 = %r1, %r2"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "pack4_sss"
  [(set (match_operand:V4HI 0 "gr_register_operand" "=r")
	(vec_concat:V4HI
	  (ss_truncate:V2HI
	    (match_operand:V2SI 1 "gr_reg_or_0_operand" "rU"))
	  (ss_truncate:V2HI
	    (match_operand:V2SI 2 "gr_reg_or_0_operand" "rU"))))]
  ""
  "pack4.sss %0 = %r1, %r2"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "unpack1_l"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(vec_select:V8QI
	  (vec_concat:V16QI
	    (match_operand:V8QI 1 "gr_reg_or_0_operand" "rU")
	    (match_operand:V8QI 2 "gr_reg_or_0_operand" "rU"))
	  (parallel [(const_int 0)
		     (const_int 1)
		     (const_int 2)
		     (const_int 3)
		     (const_int 8)
		     (const_int 9)
		     (const_int 10)
		     (const_int 11)])))]
  ""
  "unpack1.l %0 = %r2, %r1"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "unpack1_h"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(vec_select:V8QI
	  (vec_concat:V16QI
	    (match_operand:V8QI 1 "gr_reg_or_0_operand" "rU")
	    (match_operand:V8QI 2 "gr_reg_or_0_operand" "rU"))
	  (parallel [(const_int 4)
		     (const_int 5)
		     (const_int 6)
		     (const_int 7)
		     (const_int 12)
		     (const_int 13)
		     (const_int 14)
		     (const_int 15)])))]
  ""
  "unpack1.h %0 = %r2, %r1"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "mix1_r"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(vec_select:V8QI
	  (vec_concat:V16QI
	    (match_operand:V8QI 1 "gr_reg_or_0_operand" "rU")
	    (match_operand:V8QI 2 "gr_reg_or_0_operand" "rU"))
	  (parallel [(const_int 0)
		     (const_int 8)
		     (const_int 2)
		     (const_int 10)
		     (const_int 4)
		     (const_int 12)
		     (const_int 6)
		     (const_int 14)])))]
  ""
  "mix1.r %0 = %r2, %r1"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "mix1_l"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(vec_select:V8QI
	  (vec_concat:V16QI
	    (match_operand:V8QI 1 "gr_reg_or_0_operand" "rU")
	    (match_operand:V8QI 2 "gr_reg_or_0_operand" "rU"))
	  (parallel [(const_int 1)
		     (const_int 9)
		     (const_int 3)
		     (const_int 11)
		     (const_int 5)
		     (const_int 13)
		     (const_int 7)
		     (const_int 15)])))]
  ""
  "mix1.l %0 = %r2, %r1"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*mux1_rev"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(vec_select:V8QI
	  (match_operand:V8QI 1 "gr_register_operand" "r")
	  (parallel [(const_int 7)
		     (const_int 6)
		     (const_int 5)
		     (const_int 4)
		     (const_int 3)
		     (const_int 2)
		     (const_int 1)
		     (const_int 0)])))]
  ""
  "mux1 %0 = %1, @rev"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*mux1_mix"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(vec_select:V8QI
	  (match_operand:V8QI 1 "gr_register_operand" "r")
	  (parallel [(const_int 0)
		     (const_int 4)
		     (const_int 2)
		     (const_int 6)
		     (const_int 1)
		     (const_int 5)
		     (const_int 3)
		     (const_int 7)])))]
  ""
  "mux1 %0 = %1, @mix"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*mux1_shuf"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(vec_select:V8QI
	  (match_operand:V8QI 1 "gr_register_operand" "r")
	  (parallel [(const_int 0)
		     (const_int 4)
		     (const_int 1)
		     (const_int 5)
		     (const_int 2)
		     (const_int 6)
		     (const_int 3)
		     (const_int 7)])))]
  ""
  "mux1 %0 = %1, @shuf"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*mux1_alt"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(vec_select:V8QI
	  (match_operand:V8QI 1 "gr_register_operand" "r")
	  (parallel [(const_int 0)
		     (const_int 2)
		     (const_int 4)
		     (const_int 6)
		     (const_int 1)
		     (const_int 3)
		     (const_int 5)
		     (const_int 7)])))]
  ""
  "mux1 %0 = %1, @alt"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*mux1_brcst_v8qi"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(vec_select:V8QI
	  (match_operand:V8QI 1 "gr_register_operand" "r")
	  (parallel [(const_int 0)
		     (const_int 0)
		     (const_int 0)
		     (const_int 0)
		     (const_int 0)
		     (const_int 0)
		     (const_int 0)
		     (const_int 0)])))]
  ""
  "mux1 %0 = %1, @brcst"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*mux1_brcst_qi"
  [(set (match_operand:V8QI 0 "gr_register_operand" "=r")
	(vec_duplicate:V8QI
	  (match_operand:QI 1 "gr_register_operand" "r")))]
  ""
  "mux1 %0 = %1, @brcst"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "unpack2_l"
  [(set (match_operand:V4HI 0 "gr_register_operand" "=r")
	(vec_select:V4HI
	  (vec_concat:V8HI
	    (match_operand:V4HI 1 "gr_reg_or_0_operand" "rU")
	    (match_operand:V4HI 2 "gr_reg_or_0_operand" "rU"))
	  (parallel [(const_int 0)
		     (const_int 4)
		     (const_int 1)
		     (const_int 5)])))]
  ""
  "unpack2.l %0 = %r2, %r1"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "unpack2_h"
  [(set (match_operand:V4HI 0 "gr_register_operand" "=r")
	(vec_select:V4HI
	  (vec_concat:V8HI
	    (match_operand:V4HI 1 "gr_reg_or_0_operand" "rU")
	    (match_operand:V4HI 2 "gr_reg_or_0_operand" "rU"))
	  (parallel [(const_int 2)
		     (const_int 6)
		     (const_int 3)
		     (const_int 7)])))]
  ""
  "unpack2.h %0 = %r2, %r1"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*mix2_r"
  [(set (match_operand:V4HI 0 "gr_register_operand" "=r")
	(vec_select:V4HI
	  (vec_concat:V8HI
	    (match_operand:V4HI 1 "gr_reg_or_0_operand" "rU")
	    (match_operand:V4HI 2 "gr_reg_or_0_operand" "rU"))
	  (parallel [(const_int 0)
		     (const_int 4)
		     (const_int 2)
		     (const_int 6)])))]
  ""
  "mix2.r %0 = %r2, %r1"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*mix2_l"
  [(set (match_operand:V4HI 0 "gr_register_operand" "=r")
	(vec_select:V4HI
	  (vec_concat:V8HI
	    (match_operand:V4HI 1 "gr_reg_or_0_operand" "rU")
	    (match_operand:V4HI 2 "gr_reg_or_0_operand" "rU"))
	  (parallel [(const_int 1)
		     (const_int 5)
		     (const_int 3)
		     (const_int 7)])))]
  ""
  "mix2.l %0 = %r2, %r1"
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*mux2"
  [(set (match_operand:V4HI 0 "gr_register_operand" "=r")
	(vec_select:V4HI
	  (match_operand:V4HI 1 "gr_register_operand" "r")
	  (parallel [(match_operand 2 "const_int_2bit_operand" "")
		     (match_operand 3 "const_int_2bit_operand" "")
		     (match_operand 4 "const_int_2bit_operand" "")
		     (match_operand 5 "const_int_2bit_operand" "")])))]
  ""
{
  int mask;
  mask  = INTVAL (operands[2]);
  mask |= INTVAL (operands[3]) << 2;
  mask |= INTVAL (operands[4]) << 4;
  mask |= INTVAL (operands[5]) << 6;
  operands[2] = GEN_INT (mask);
  return "%,mux2 %0 = %1, %2";
}
  [(set_attr "itanium_class" "mmshf")])

(define_insn "*mux2_brcst_hi"
  [(set (match_operand:V4HI 0 "gr_register_operand" "=r")
	(vec_duplicate:V4HI
	  (match_operand:HI 1 "gr_register_operand" "r")))]
  ""
  "mux2 %0 = %1, 0"
  [(set_attr "itanium_class" "mmshf")])

;; Note that mix4.r performs the exact same operation.
(define_insn "*unpack4_l"
  [(set (match_operand:V2SI 0 "gr_register_operand" "=r")
	(vec_select:V2SI
	  (vec_concat:V4SI
	    (match_operand:V2SI 1 "gr_reg_or_0_operand" "rU")
	    (match_operand:V2SI 2 "gr_reg_or_0_operand" "rU"))
	  (parallel [(const_int 0)
		     (const_int 2)])))]
  ""
  "unpack4.l %0 = %r2, %r1"
  [(set_attr "itanium_class" "mmshf")])

;; Note that mix4.l performs the exact same operation.
(define_insn "*unpack4_h"
  [(set (match_operand:V2SI 0 "gr_register_operand" "=r")
	(vec_select:V2SI
	  (vec_concat:V4SI
	    (match_operand:V2SI 1 "gr_reg_or_0_operand" "rU")
	    (match_operand:V2SI 2 "gr_reg_or_0_operand" "rU"))
	  (parallel [(const_int 1)
		     (const_int 3)])))]
  ""
  "unpack4.h %0 = %r2, %r1"
  [(set_attr "itanium_class" "mmshf")])

(define_expand "vec_initv2si"
  [(match_operand:V2SI 0 "gr_register_operand" "")
   (match_operand 1 "" "")]
  ""
{
  rtx op1 = XVECEXP (operands[1], 0, 0);
  rtx op2 = XVECEXP (operands[1], 0, 1);
  rtx x;

  if (GET_CODE (op1) == CONST_INT && GET_CODE (op2) == CONST_INT)
    {
      rtvec v = rtvec_alloc (2);
      RTVEC_ELT (v, 0) = TARGET_BIG_ENDIAN ? op2 : op1;
      RTVEC_ELT (v, 1) = TARGET_BIG_ENDIAN ? op1 : op2;;
      x = gen_rtx_CONST_VECTOR (V2SImode, v);
      emit_move_insn (operands[0], x);
      DONE;
    }

  if (!gr_reg_or_0_operand (op1, SImode))
    op1 = force_reg (SImode, op1);
  if (!gr_reg_or_0_operand (op2, SImode))
    op2 = force_reg (SImode, op2);

  if (TARGET_BIG_ENDIAN)
    x = gen_rtx_VEC_CONCAT (V2SImode, op2, op1);
  else
    x = gen_rtx_VEC_CONCAT (V2SImode, op1, op2);
  emit_insn (gen_rtx_SET (VOIDmode, operands[0], x));
  DONE;
})

(define_insn "*vecinit_v2si"
  [(set (match_operand:V2SI 0 "gr_register_operand" "=r")
	(vec_concat:V2SI
	  (match_operand:SI 1 "gr_reg_or_0_operand" "rO")
	  (match_operand:SI 2 "gr_reg_or_0_operand" "rO")))]
  ""
  "unpack4.l %0 = %r2, %r1"
  [(set_attr "itanium_class" "mmshf")])

;; Missing operations
;; padd.uus
;; pavg
;; pavgsub
;; pmpyshr, general form
;; psad
;; pshladd
;; pshradd
;; psub.uus

;; Floating point vector operations

(define_expand "movv2sf"
  [(set (match_operand:V2SF 0 "general_operand" "")
        (match_operand:V2SF 1 "general_operand" ""))]
  ""
{
  rtx op1 = ia64_expand_move (operands[0], operands[1]);
  if (!op1)
    DONE;
  operands[1] = op1;
})

(define_insn "*movv2sf_internal"
  [(set (match_operand:V2SF 0 "destination_operand"
					"=f,f,f,Q,*r ,*r,*r,*r,m ,f ,*r")
	(match_operand:V2SF 1 "move_operand"
					"fU,Y,Q,f,U*r,W ,i ,m ,*r,*r,f "))]
  "ia64_move_ok (operands[0], operands[1])"
{
  static const char * const alt[] = {
    "%,mov %0 = %F1",
    "%,fpack %0 = %F2, %F1",
    "%,ldf8 %0 = %1%P1",
    "%,stf8 %0 = %1%P0",
    "%,mov %0 = %r1",
    "%,addl %0 = %v1, r0",
    "%,movl %0 = %v1",
    "%,ld8%O1 %0 = %1%P1",
    "%,st8%Q0 %0 = %r1%P0",
    "%,setf.sig %0 = %1",
    "%,getf.sig %0 = %1"
  };

  if (which_alternative == 1)
    {
      operands[2] = XVECEXP (operands[1], 0, 1);
      operands[1] = XVECEXP (operands[1], 0, 0);
    }

  return alt[which_alternative];
}
  [(set_attr "itanium_class" "fmisc,fmisc,fld,stf,ialu,ialu,long_i,ld,st,tofr,frfr")])

(define_insn "absv2sf2"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(abs:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")))]
  ""
  "fpabs %0 = %1"
  [(set_attr "itanium_class" "fmisc")])

(define_insn "negv2sf2"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(neg:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")))]
  ""
  "fpneg %0 = %1"
  [(set_attr "itanium_class" "fmisc")])

(define_insn "*negabsv2sf2"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(neg:V2SF
	  (abs:V2SF (match_operand:V2SF 1 "fr_register_operand" "f"))))]
  ""
  "fpnegabs %0 = %1"
  [(set_attr "itanium_class" "fmisc")])

;; In order to convince combine to merge plus and mult to a useful fpma,
;; we need a couple of extra patterns.
(define_expand "addv2sf3"
  [(parallel
    [(set (match_operand:V2SF 0 "fr_register_operand" "")
	  (plus:V2SF (match_operand:V2SF 1 "fr_register_operand" "")
		     (match_operand:V2SF 2 "fr_register_operand" "")))
     (use (match_dup 3))])]
  ""
{
  rtvec v = gen_rtvec (2, CONST1_RTX (SFmode), CONST1_RTX (SFmode));
  operands[3] = force_reg (V2SFmode, gen_rtx_CONST_VECTOR (V2SFmode, v));
})

;; The split condition here could be combine_completed, if we had such.
(define_insn_and_split "*addv2sf3_1"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(plus:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")
		   (match_operand:V2SF 2 "fr_register_operand" "f")))
   (use (match_operand:V2SF 3 "fr_register_operand" "f"))]
  ""
  "#"
  "reload_completed"
  [(set (match_dup 0)
	(plus:V2SF
	  (mult:V2SF (match_dup 1) (match_dup 3))
	  (match_dup 2)))]
  "")

(define_insn_and_split "*addv2sf3_2"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(plus:V2SF
	  (mult:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")
		     (match_operand:V2SF 2 "fr_register_operand" "f"))
	  (match_operand:V2SF 3 "fr_register_operand" "f")))
    (use (match_operand:V2SF 4 "" "X"))]
  ""
  "#"
  ""
  [(set (match_dup 0)
	(plus:V2SF
	  (mult:V2SF (match_dup 1) (match_dup 2))
	  (match_dup 3)))]
  "")

;; In order to convince combine to merge minus and mult to a useful fpms,
;; we need a couple of extra patterns.
(define_expand "subv2sf3"
  [(parallel
    [(set (match_operand:V2SF 0 "fr_register_operand" "")
	  (minus:V2SF (match_operand:V2SF 1 "fr_register_operand" "")
		      (match_operand:V2SF 2 "fr_register_operand" "")))
     (use (match_dup 3))])]
  ""
{
  rtvec v = gen_rtvec (2, CONST1_RTX (SFmode), CONST1_RTX (SFmode));
  operands[3] = force_reg (V2SFmode, gen_rtx_CONST_VECTOR (V2SFmode, v));
})

;; The split condition here could be combine_completed, if we had such.
(define_insn_and_split "*subv2sf3_1"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(minus:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")
		    (match_operand:V2SF 2 "fr_register_operand" "f")))
   (use (match_operand:V2SF 3 "fr_register_operand" "f"))]
  ""
  "#"
  "reload_completed"
  [(set (match_dup 0)
	(minus:V2SF
	  (mult:V2SF (match_dup 1) (match_dup 3))
	  (match_dup 2)))]
  "")

(define_insn_and_split "*subv2sf3_2"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(minus:V2SF
	  (mult:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")
		     (match_operand:V2SF 2 "fr_register_operand" "f"))
	  (match_operand:V2SF 3 "fr_register_operand" "f")))
    (use (match_operand:V2SF 4 "" "X"))]
  ""
  "#"
  ""
  [(set (match_dup 0)
	(minus:V2SF
	  (mult:V2SF (match_dup 1) (match_dup 2))
	  (match_dup 3)))]
  "")

(define_insn "mulv2sf3"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(mult:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")
		   (match_operand:V2SF 2 "fr_register_operand" "f")))]
  ""
  "fpmpy %0 = %1, %2"
  [(set_attr "itanium_class" "fmac")])

(define_insn "*fpma"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(plus:V2SF
	  (mult:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")
		     (match_operand:V2SF 2 "fr_register_operand" "f"))
	  (match_operand:V2SF 3 "fr_register_operand" "f")))]
  ""
  "fpma %0 = %1, %2, %3"
  [(set_attr "itanium_class" "fmac")])

(define_insn "*fpms"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(minus:V2SF
	  (mult:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")
		     (match_operand:V2SF 2 "fr_register_operand" "f"))
	  (match_operand:V2SF 3 "fr_register_operand" "f")))]
  ""
  "fpms %0 = %1, %2, %3"
  [(set_attr "itanium_class" "fmac")])

(define_insn "*fpnmpy"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(neg:V2SF
	  (mult:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")
		     (match_operand:V2SF 2 "fr_register_operand" "f"))))]
  ""
  "fpnmpy %0 = %1, %2"
  [(set_attr "itanium_class" "fmac")])

(define_insn "*fpnma"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(plus:V2SF
	  (neg:V2SF
	    (mult:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")
		       (match_operand:V2SF 2 "fr_register_operand" "f")))
	  (match_operand:V2SF 3 "fr_register_operand" "f")))]
  ""
  "fpnma %0 = %1, %2, %3"
  [(set_attr "itanium_class" "fmac")])

(define_insn "smaxv2sf3"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(smax:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")
		   (match_operand:V2SF 2 "fr_register_operand" "f")))]
  ""
  "fpmax %0 = %1, %2"
  [(set_attr "itanium_class" "fmisc")])

(define_insn "sminv2sf3"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(smin:V2SF (match_operand:V2SF 1 "fr_register_operand" "f")
		   (match_operand:V2SF 2 "fr_register_operand" "f")))]
  ""
  "fpmin %0 = %1, %2"
  [(set_attr "itanium_class" "fmisc")])

(define_expand "reduc_splus_v2sf"
  [(match_operand:V2SF 0 "fr_register_operand" "")
   (match_operand:V2SF 1 "fr_register_operand" "")]
  ""
{
  rtx tmp = gen_reg_rtx (V2SFmode);
  emit_insn (gen_fswap (tmp, operands[1], CONST0_RTX (V2SFmode)));
  emit_insn (gen_addv2sf3 (operands[0], operands[1], tmp));
  DONE;
})

(define_expand "reduc_smax_v2sf"
  [(match_operand:V2SF 0 "fr_register_operand" "")
   (match_operand:V2SF 1 "fr_register_operand" "")]
  ""
{
  rtx tmp = gen_reg_rtx (V2SFmode);
  emit_insn (gen_fswap (tmp, operands[1], CONST0_RTX (V2SFmode)));
  emit_insn (gen_smaxv2sf3 (operands[0], operands[1], tmp));
  DONE;
})

(define_expand "reduc_smin_v2sf"
  [(match_operand:V2SF 0 "fr_register_operand" "")
   (match_operand:V2SF 1 "fr_register_operand" "")]
  ""
{
  rtx tmp = gen_reg_rtx (V2SFmode);
  emit_insn (gen_fswap (tmp, operands[1], CONST0_RTX (V2SFmode)));
  emit_insn (gen_sminv2sf3 (operands[0], operands[1], tmp));
  DONE;
})

(define_expand "vcondv2sf"
  [(set (match_operand:V2SF 0 "fr_register_operand" "")
	(if_then_else:V2SF
	  (match_operator 3 "" 
	    [(match_operand:V2SF 4 "fr_reg_or_0_operand" "")
	     (match_operand:V2SF 5 "fr_reg_or_0_operand" "")])
	  (match_operand:V2SF 1 "fr_reg_or_0_operand" "")
	  (match_operand:V2SF 2 "fr_reg_or_0_operand" "")))]
  ""
{
  rtx x, cmp;

  cmp = gen_reg_rtx (V2SFmode);
  PUT_MODE (operands[3], V2SFmode);
  emit_insn (gen_rtx_SET (VOIDmode, cmp, operands[3]));

  x = gen_rtx_IF_THEN_ELSE (V2SFmode, cmp, operands[1], operands[2]);
  emit_insn (gen_rtx_SET (VOIDmode, operands[0], x));
  DONE;
})

(define_insn "*fpcmp"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(match_operator:V2SF 3 "comparison_operator"
	  [(match_operand:V2SF 1 "fr_reg_or_0_operand" "fU")
	   (match_operand:V2SF 2 "fr_reg_or_0_operand" "fU")]))]
  ""
  "fpcmp.%D3 %0 = %F1, %F2"
  [(set_attr "itanium_class" "fmisc")])

(define_insn "*fselect"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(if_then_else:V2SF
	  (match_operand:V2SF 1 "fr_register_operand" "f")
	  (match_operand:V2SF 2 "fr_reg_or_0_operand" "fU")
	  (match_operand:V2SF 3 "fr_reg_or_0_operand" "fU")))]
  ""
  "fselect %0 = %F2, %F3, %1"
  [(set_attr "itanium_class" "fmisc")])

(define_expand "vec_initv2sf"
  [(match_operand:V2SF 0 "fr_register_operand" "")
   (match_operand 1 "" "")]
  ""
{
  rtx op1 = XVECEXP (operands[1], 0, 0);
  rtx op2 = XVECEXP (operands[1], 0, 1);
  rtx x;

  if (GET_CODE (op1) == CONST_DOUBLE && GET_CODE (op2) == CONST_DOUBLE)
    {
      x = gen_rtx_CONST_VECTOR (V2SFmode, XVEC (operands[1], 0));
      emit_move_insn (operands[0], x);
      DONE;
    }

  if (!fr_reg_or_fp01_operand (op1, SFmode))
    op1 = force_reg (SFmode, op1);
  if (!fr_reg_or_fp01_operand (op2, SFmode))
    op2 = force_reg (SFmode, op2);

  if (TARGET_BIG_ENDIAN)
    emit_insn (gen_fpack (operands[0], op2, op1));
  else
    emit_insn (gen_fpack (operands[0], op1, op2));
  DONE;
})

(define_insn "fpack"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(vec_concat:V2SF
	  (match_operand:SF 1 "fr_reg_or_fp01_operand" "fG")
	  (match_operand:SF 2 "fr_reg_or_fp01_operand" "fG")))]
  ""
  "fpack %0 = %F2, %F1"
  [(set_attr "itanium_class" "fmisc")])

(define_insn "fswap"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(vec_select:V2SF
	  (vec_concat:V4SF
	    (match_operand:V2SF 1 "fr_reg_or_0_operand" "fU")
	    (match_operand:V2SF 2 "fr_reg_or_0_operand" "fU"))
	  (parallel [(const_int 1) (const_int 2)])))]
  ""
  "fswap %0 = %F1, %F2"
  [(set_attr "itanium_class" "fmisc")])

(define_insn "*fmix_l"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(vec_select:V2SF
	  (vec_concat:V4SF
	    (match_operand:V2SF 1 "fr_reg_or_0_operand" "fU")
	    (match_operand:V2SF 2 "fr_reg_or_0_operand" "fU"))
	  (parallel [(const_int 1) (const_int 3)])))]
  ""
  "fmix.l %0 = %F2, %F1"
  [(set_attr "itanium_class" "fmisc")])

(define_insn "fmix_r"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(vec_select:V2SF
	  (vec_concat:V4SF
	    (match_operand:V2SF 1 "fr_reg_or_0_operand" "fU")
	    (match_operand:V2SF 2 "fr_reg_or_0_operand" "fU"))
	  (parallel [(const_int 0) (const_int 2)])))]
  ""
  "fmix.r %0 = %F2, %F1"
  [(set_attr "itanium_class" "fmisc")])

(define_insn "fmix_lr"
  [(set (match_operand:V2SF 0 "fr_register_operand" "=f")
	(vec_select:V2SF
	  (vec_concat:V4SF
	    (match_operand:V2SF 1 "fr_reg_or_0_operand" "fU")
	    (match_operand:V2SF 2 "fr_reg_or_0_operand" "fU"))
	  (parallel [(const_int 0) (const_int 3)])))]
  ""
  "fmix.lr %0 = %F2, %F1"
  [(set_attr "itanium_class" "fmisc")])

(define_expand "vec_setv2sf"
  [(match_operand:V2SF 0 "fr_register_operand" "")
   (match_operand:SF 1 "fr_register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  ""
{
  rtx tmp = gen_reg_rtx (V2SFmode);
  emit_insn (gen_fpack (tmp, operands[1], CONST0_RTX (SFmode)));

  switch (INTVAL (operands[2]))
    {
    case 0:
      emit_insn (gen_fmix_lr (operands[0], tmp, operands[0]));
      break;
    case 1:
      emit_insn (gen_fmix_r (operands[0], operands[0], tmp));
      break;
    default:
      gcc_unreachable ();
    }
  DONE;
})

(define_insn_and_split "*vec_extractv2sf_0_le"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=r,f,m")
	(unspec:SF [(match_operand:V2SF 1 "nonimmediate_operand" "rfm,rm,r")
		    (const_int 0)]
		   UNSPEC_VECT_EXTR))]
  "!TARGET_BIG_ENDIAN"
  "#"
  "reload_completed"
  [(set (match_dup 0) (match_dup 1))]
{
  if (REG_P (operands[1]) && FR_REGNO_P (REGNO (operands[1])))
    operands[0] = gen_rtx_REG (V2SFmode, REGNO (operands[0]));
  else if (MEM_P (operands[1]))
    operands[1] = adjust_address (operands[1], SFmode, 0);
  else
    operands[1] = gen_rtx_REG (SFmode, REGNO (operands[1]));
})

(define_insn_and_split "*vec_extractv2sf_0_be"
  [(set (match_operand:SF 0 "register_operand" "=r,f")
	(unspec:SF [(match_operand:V2SF 1 "register_operand" "rf,r")
		    (const_int 0)]
		   UNSPEC_VECT_EXTR))]
  "TARGET_BIG_ENDIAN"
  "#"
  "reload_completed"
  [(set (match_dup 0) (match_dup 1))]
{
  if (REG_P (operands[1]) && FR_REGNO_P (REGNO (operands[1])))
    operands[0] = gen_rtx_REG (V2SFmode, REGNO (operands[0]));
  else
    operands[1] = gen_rtx_REG (SFmode, REGNO (operands[1]));
})

(define_insn_and_split "*vec_extractv2sf_1"
  [(set (match_operand:SF 0 "register_operand" "=r")
	(unspec:SF [(match_operand:V2SF 1 "register_operand" "r")
		    (const_int 1)]
		   UNSPEC_VECT_EXTR))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  operands[0] = gen_rtx_REG (DImode, REGNO (operands[0]));
  operands[1] = gen_rtx_REG (DImode, REGNO (operands[1]));
  if (TARGET_BIG_ENDIAN)
    emit_move_insn (operands[0], operands[1]);
  else
    emit_insn (gen_lshrdi3 (operands[0], operands[1], GEN_INT (32)));
  DONE;
})

(define_expand "vec_extractv2sf"
  [(set (match_operand:SF 0 "register_operand" "")
	(unspec:SF [(match_operand:V2SF 1 "register_operand" "")
		    (match_operand:DI 2 "const_int_operand" "")]
		   UNSPEC_VECT_EXTR))]
  ""
  "")

;; Missing operations
;; fprcpa
;; fpsqrta

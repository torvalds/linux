;; Patterns for the Intel Wireless MMX technology architecture.
;; Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
;; Contributed by Red Hat.

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify it under
;; the terms of the GNU General Public License as published by the Free
;; Software Foundation; either version 2, or (at your option) any later
;; version.

;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

(define_insn "iwmmxt_iordi3"
  [(set (match_operand:DI         0 "register_operand" "=y,?&r,?&r")
        (ior:DI (match_operand:DI 1 "register_operand" "%y,0,r")
		(match_operand:DI 2 "register_operand"  "y,r,r")))]
  "TARGET_REALLY_IWMMXT"
  "@
   wor%?\\t%0, %1, %2
   #
   #"
  [(set_attr "predicable" "yes")
   (set_attr "length" "4,8,8")])

(define_insn "iwmmxt_xordi3"
  [(set (match_operand:DI         0 "register_operand" "=y,?&r,?&r")
        (xor:DI (match_operand:DI 1 "register_operand" "%y,0,r")
		(match_operand:DI 2 "register_operand"  "y,r,r")))]
  "TARGET_REALLY_IWMMXT"
  "@
   wxor%?\\t%0, %1, %2
   #
   #"
  [(set_attr "predicable" "yes")
   (set_attr "length" "4,8,8")])

(define_insn "iwmmxt_anddi3"
  [(set (match_operand:DI         0 "register_operand" "=y,?&r,?&r")
        (and:DI (match_operand:DI 1 "register_operand" "%y,0,r")
		(match_operand:DI 2 "register_operand"  "y,r,r")))]
  "TARGET_REALLY_IWMMXT"
  "@
   wand%?\\t%0, %1, %2
   #
   #"
  [(set_attr "predicable" "yes")
   (set_attr "length" "4,8,8")])

(define_insn "iwmmxt_nanddi3"
  [(set (match_operand:DI                 0 "register_operand" "=y")
        (and:DI (match_operand:DI         1 "register_operand"  "y")
		(not:DI (match_operand:DI 2 "register_operand"  "y"))))]
  "TARGET_REALLY_IWMMXT"
  "wandn%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "*iwmmxt_arm_movdi"
  [(set (match_operand:DI 0 "nonimmediate_di_operand" "=r, r, m,y,y,yr,y,yrUy")
	(match_operand:DI 1 "di_operand"              "rIK,mi,r,y,yr,y,yrUy,y"))]
  "TARGET_REALLY_IWMMXT
   && (   register_operand (operands[0], DImode)
       || register_operand (operands[1], DImode))"
  "*
{
  switch (which_alternative)
    {
    default:
      return output_move_double (operands);
    case 0:
      return \"#\";
    case 3:
      return \"wmov%?\\t%0,%1\";
    case 4:
      return \"tmcrr%?\\t%0,%Q1,%R1\";
    case 5:
      return \"tmrrc%?\\t%Q0,%R0,%1\";
    case 6:
      return \"wldrd%?\\t%0,%1\";
    case 7:
      return \"wstrd%?\\t%1,%0\";
    }
}"
  [(set_attr "length"         "8,8,8,4,4,4,4,4")
   (set_attr "type"           "*,load1,store2,*,*,*,*,*")
   (set_attr "pool_range"     "*,1020,*,*,*,*,*,*")
   (set_attr "neg_pool_range" "*,1012,*,*,*,*,*,*")]
)

(define_insn "*iwmmxt_movsi_insn"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,r,r, m,z,r,?z,Uy,z")
	(match_operand:SI 1 "general_operand"      "rI,K,mi,r,r,z,Uy,z,z"))]
  "TARGET_REALLY_IWMMXT
   && (   register_operand (operands[0], SImode)
       || register_operand (operands[1], SImode))"
  "*
   switch (which_alternative)
   {
   case 0: return \"mov\\t%0, %1\";
   case 1: return \"mvn\\t%0, #%B1\";
   case 2: return \"ldr\\t%0, %1\";
   case 3: return \"str\\t%1, %0\";
   case 4: return \"tmcr\\t%0, %1\";
   case 5: return \"tmrc\\t%0, %1\";
   case 6: return arm_output_load_gr (operands);
   case 7: return \"wstrw\\t%1, %0\";
   default:return \"wstrw\\t%1, [sp, #-4]!\;wldrw\\t%0, [sp], #4\\t@move CG reg\";
  }"
  [(set_attr "type"           "*,*,load1,store1,*,*,load1,store1,*")
   (set_attr "length"         "*,*,*,        *,*,*,  16,     *,8")
   (set_attr "pool_range"     "*,*,4096,     *,*,*,1024,     *,*")
   (set_attr "neg_pool_range" "*,*,4084,     *,*,*,   *,  1012,*")
   ;; Note - the "predicable" attribute is not allowed to have alternatives.
   ;; Since the wSTRw wCx instruction is not predicable, we cannot support
   ;; predicating any of the alternatives in this template.  Instead,
   ;; we do the predication ourselves, in cond_iwmmxt_movsi_insn.
   (set_attr "predicable"     "no")
   ;; Also - we have to pretend that these insns clobber the condition code
   ;; bits as otherwise arm_final_prescan_insn() will try to conditionalize
   ;; them.
   (set_attr "conds" "clob")]
)

;; Because iwmmxt_movsi_insn is not predicable, we provide the
;; cond_exec version explicitly, with appropriate constraints.

(define_insn "*cond_iwmmxt_movsi_insn"
  [(cond_exec
     (match_operator 2 "arm_comparison_operator"
      [(match_operand 3 "cc_register" "")
      (const_int 0)])
     (set (match_operand:SI 0 "nonimmediate_operand" "=r,r,r, m,z,r")
	  (match_operand:SI 1 "general_operand"      "rI,K,mi,r,r,z")))]
  "TARGET_REALLY_IWMMXT
   && (   register_operand (operands[0], SImode)
       || register_operand (operands[1], SImode))"
  "*
   switch (which_alternative)
   {
   case 0: return \"mov%?\\t%0, %1\";
   case 1: return \"mvn%?\\t%0, #%B1\";
   case 2: return \"ldr%?\\t%0, %1\";
   case 3: return \"str%?\\t%1, %0\";
   case 4: return \"tmcr%?\\t%0, %1\";
   default: return \"tmrc%?\\t%0, %1\";
  }"
  [(set_attr "type"           "*,*,load1,store1,*,*")
   (set_attr "pool_range"     "*,*,4096,     *,*,*")
   (set_attr "neg_pool_range" "*,*,4084,     *,*,*")]
)

(define_insn "movv8qi_internal"
  [(set (match_operand:V8QI 0 "nonimmediate_operand" "=y,m,y,?r,?y,?r")
	(match_operand:V8QI 1 "general_operand"       "y,y,mi,y,r,mi"))]
  "TARGET_REALLY_IWMMXT"
  "*
   switch (which_alternative)
   {
   case 0: return \"wmov%?\\t%0, %1\";
   case 1: return \"wstrd%?\\t%1, %0\";
   case 2: return \"wldrd%?\\t%0, %1\";
   case 3: return \"tmrrc%?\\t%Q0, %R0, %1\";
   case 4: return \"tmcrr%?\\t%0, %Q1, %R1\";
   default: return output_move_double (operands);
   }"
  [(set_attr "predicable" "yes")
   (set_attr "length"         "4,     4,   4,4,4,   8")
   (set_attr "type"           "*,store1,load1,*,*,load1")
   (set_attr "pool_range"     "*,     *, 256,*,*, 256")
   (set_attr "neg_pool_range" "*,     *, 244,*,*, 244")])

(define_insn "movv4hi_internal"
  [(set (match_operand:V4HI 0 "nonimmediate_operand" "=y,m,y,?r,?y,?r")
	(match_operand:V4HI 1 "general_operand"       "y,y,mi,y,r,mi"))]
  "TARGET_REALLY_IWMMXT"
  "*
   switch (which_alternative)
   {
   case 0: return \"wmov%?\\t%0, %1\";
   case 1: return \"wstrd%?\\t%1, %0\";
   case 2: return \"wldrd%?\\t%0, %1\";
   case 3: return \"tmrrc%?\\t%Q0, %R0, %1\";
   case 4: return \"tmcrr%?\\t%0, %Q1, %R1\";
   default: return output_move_double (operands);
   }"
  [(set_attr "predicable" "yes")
   (set_attr "length"         "4,     4,   4,4,4,   8")
   (set_attr "type"           "*,store1,load1,*,*,load1")
   (set_attr "pool_range"     "*,     *, 256,*,*, 256")
   (set_attr "neg_pool_range" "*,     *, 244,*,*, 244")])

(define_insn "movv2si_internal"
  [(set (match_operand:V2SI 0 "nonimmediate_operand" "=y,m,y,?r,?y,?r")
	(match_operand:V2SI 1 "general_operand"       "y,y,mi,y,r,mi"))]
  "TARGET_REALLY_IWMMXT"
  "*
   switch (which_alternative)
   {
   case 0: return \"wmov%?\\t%0, %1\";
   case 1: return \"wstrd%?\\t%1, %0\";
   case 2: return \"wldrd%?\\t%0, %1\";
   case 3: return \"tmrrc%?\\t%Q0, %R0, %1\";
   case 4: return \"tmcrr%?\\t%0, %Q1, %R1\";
   default: return output_move_double (operands);
   }"
  [(set_attr "predicable" "yes")
   (set_attr "length"         "4,     4,   4,4,4,  24")
   (set_attr "type"           "*,store1,load1,*,*,load1")
   (set_attr "pool_range"     "*,     *, 256,*,*, 256")
   (set_attr "neg_pool_range" "*,     *, 244,*,*, 244")])

;; This pattern should not be needed.  It is to match a
;; wierd case generated by GCC when no optimizations are
;; enabled.  (Try compiling gcc/testsuite/gcc.c-torture/
;; compile/simd-5.c at -O0).  The mode for operands[1] is
;; deliberately omitted.
(define_insn "movv2si_internal_2"
  [(set (match_operand:V2SI 0 "nonimmediate_operand" "=?r")
	(match_operand      1 "immediate_operand"      "mi"))]
  "TARGET_REALLY_IWMMXT"
  "* return output_move_double (operands);"
  [(set_attr "predicable"     "yes")
   (set_attr "length"         "8")
   (set_attr "type"           "load1")
   (set_attr "pool_range"     "256")
   (set_attr "neg_pool_range" "244")])

;; Vector add/subtract

(define_insn "addv8qi3"
  [(set (match_operand:V8QI            0 "register_operand" "=y")
        (plus:V8QI (match_operand:V8QI 1 "register_operand"  "y")
	           (match_operand:V8QI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "waddb%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "addv4hi3"
  [(set (match_operand:V4HI            0 "register_operand" "=y")
        (plus:V4HI (match_operand:V4HI 1 "register_operand"  "y")
	           (match_operand:V4HI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "waddh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "addv2si3"
  [(set (match_operand:V2SI            0 "register_operand" "=y")
        (plus:V2SI (match_operand:V2SI 1 "register_operand"  "y")
	           (match_operand:V2SI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "waddw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ssaddv8qi3"
  [(set (match_operand:V8QI               0 "register_operand" "=y")
        (ss_plus:V8QI (match_operand:V8QI 1 "register_operand"  "y")
		      (match_operand:V8QI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "waddbss%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ssaddv4hi3"
  [(set (match_operand:V4HI               0 "register_operand" "=y")
        (ss_plus:V4HI (match_operand:V4HI 1 "register_operand"  "y")
		      (match_operand:V4HI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "waddhss%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ssaddv2si3"
  [(set (match_operand:V2SI               0 "register_operand" "=y")
        (ss_plus:V2SI (match_operand:V2SI 1 "register_operand"  "y")
		      (match_operand:V2SI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "waddwss%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "usaddv8qi3"
  [(set (match_operand:V8QI               0 "register_operand" "=y")
        (us_plus:V8QI (match_operand:V8QI 1 "register_operand"  "y")
		      (match_operand:V8QI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "waddbus%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "usaddv4hi3"
  [(set (match_operand:V4HI               0 "register_operand" "=y")
        (us_plus:V4HI (match_operand:V4HI 1 "register_operand"  "y")
		      (match_operand:V4HI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "waddhus%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "usaddv2si3"
  [(set (match_operand:V2SI               0 "register_operand" "=y")
        (us_plus:V2SI (match_operand:V2SI 1 "register_operand"  "y")
		      (match_operand:V2SI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "waddwus%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "subv8qi3"
  [(set (match_operand:V8QI             0 "register_operand" "=y")
        (minus:V8QI (match_operand:V8QI 1 "register_operand"  "y")
		    (match_operand:V8QI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsubb%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "subv4hi3"
  [(set (match_operand:V4HI             0 "register_operand" "=y")
        (minus:V4HI (match_operand:V4HI 1 "register_operand"  "y")
		    (match_operand:V4HI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsubh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "subv2si3"
  [(set (match_operand:V2SI             0 "register_operand" "=y")
        (minus:V2SI (match_operand:V2SI 1 "register_operand"  "y")
		    (match_operand:V2SI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsubw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "sssubv8qi3"
  [(set (match_operand:V8QI                0 "register_operand" "=y")
        (ss_minus:V8QI (match_operand:V8QI 1 "register_operand"  "y")
		       (match_operand:V8QI 2 "register_operand"  "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsubbss%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "sssubv4hi3"
  [(set (match_operand:V4HI                0 "register_operand" "=y")
        (ss_minus:V4HI (match_operand:V4HI 1 "register_operand" "y")
		       (match_operand:V4HI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsubhss%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "sssubv2si3"
  [(set (match_operand:V2SI                0 "register_operand" "=y")
        (ss_minus:V2SI (match_operand:V2SI 1 "register_operand" "y")
		       (match_operand:V2SI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsubwss%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ussubv8qi3"
  [(set (match_operand:V8QI                0 "register_operand" "=y")
        (us_minus:V8QI (match_operand:V8QI 1 "register_operand" "y")
		       (match_operand:V8QI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsubbus%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ussubv4hi3"
  [(set (match_operand:V4HI                0 "register_operand" "=y")
        (us_minus:V4HI (match_operand:V4HI 1 "register_operand" "y")
		       (match_operand:V4HI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsubhus%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ussubv2si3"
  [(set (match_operand:V2SI                0 "register_operand" "=y")
        (us_minus:V2SI (match_operand:V2SI 1 "register_operand" "y")
		       (match_operand:V2SI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsubwus%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "mulv4hi3"
  [(set (match_operand:V4HI            0 "register_operand" "=y")
        (mult:V4HI (match_operand:V4HI 1 "register_operand" "y")
		   (match_operand:V4HI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wmulul%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "smulv4hi3_highpart"
  [(set (match_operand:V4HI                                0 "register_operand" "=y")
	(truncate:V4HI
	 (lshiftrt:V4SI
	  (mult:V4SI (sign_extend:V4SI (match_operand:V4HI 1 "register_operand" "y"))
		     (sign_extend:V4SI (match_operand:V4HI 2 "register_operand" "y")))
	  (const_int 16))))]
  "TARGET_REALLY_IWMMXT"
  "wmulsm%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "umulv4hi3_highpart"
  [(set (match_operand:V4HI                                0 "register_operand" "=y")
	(truncate:V4HI
	 (lshiftrt:V4SI
	  (mult:V4SI (zero_extend:V4SI (match_operand:V4HI 1 "register_operand" "y"))
		     (zero_extend:V4SI (match_operand:V4HI 2 "register_operand" "y")))
	  (const_int 16))))]
  "TARGET_REALLY_IWMMXT"
  "wmulum%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wmacs"
  [(set (match_operand:DI               0 "register_operand" "=y")
	(unspec:DI [(match_operand:DI   1 "register_operand" "0")
		    (match_operand:V4HI 2 "register_operand" "y")
		    (match_operand:V4HI 3 "register_operand" "y")] UNSPEC_WMACS))]
  "TARGET_REALLY_IWMMXT"
  "wmacs%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wmacsz"
  [(set (match_operand:DI               0 "register_operand" "=y")
	(unspec:DI [(match_operand:V4HI 1 "register_operand" "y")
		    (match_operand:V4HI 2 "register_operand" "y")] UNSPEC_WMACSZ))]
  "TARGET_REALLY_IWMMXT"
  "wmacsz%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wmacu"
  [(set (match_operand:DI               0 "register_operand" "=y")
	(unspec:DI [(match_operand:DI   1 "register_operand" "0")
		    (match_operand:V4HI 2 "register_operand" "y")
		    (match_operand:V4HI 3 "register_operand" "y")] UNSPEC_WMACU))]
  "TARGET_REALLY_IWMMXT"
  "wmacu%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wmacuz"
  [(set (match_operand:DI               0 "register_operand" "=y")
	(unspec:DI [(match_operand:V4HI 1 "register_operand" "y")
		    (match_operand:V4HI 2 "register_operand" "y")] UNSPEC_WMACUZ))]
  "TARGET_REALLY_IWMMXT"
  "wmacuz%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

;; Same as xordi3, but don't show input operands so that we don't think
;; they are live.
(define_insn "iwmmxt_clrdi"
  [(set (match_operand:DI 0 "register_operand" "=y")
        (unspec:DI [(const_int 0)] UNSPEC_CLRDI))]
  "TARGET_REALLY_IWMMXT"
  "wxor%?\\t%0, %0, %0"
  [(set_attr "predicable" "yes")])

;; Seems like cse likes to generate these, so we have to support them.

(define_insn "*iwmmxt_clrv8qi"
  [(set (match_operand:V8QI 0 "register_operand" "=y")
        (const_vector:V8QI [(const_int 0) (const_int 0)
			    (const_int 0) (const_int 0)
			    (const_int 0) (const_int 0)
			    (const_int 0) (const_int 0)]))]
  "TARGET_REALLY_IWMMXT"
  "wxor%?\\t%0, %0, %0"
  [(set_attr "predicable" "yes")])

(define_insn "*iwmmxt_clrv4hi"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
        (const_vector:V4HI [(const_int 0) (const_int 0)
			    (const_int 0) (const_int 0)]))]
  "TARGET_REALLY_IWMMXT"
  "wxor%?\\t%0, %0, %0"
  [(set_attr "predicable" "yes")])

(define_insn "*iwmmxt_clrv2si"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
        (const_vector:V2SI [(const_int 0) (const_int 0)]))]
  "TARGET_REALLY_IWMMXT"
  "wxor%?\\t%0, %0, %0"
  [(set_attr "predicable" "yes")])

;; Unsigned averages/sum of absolute differences

(define_insn "iwmmxt_uavgrndv8qi3"
  [(set (match_operand:V8QI              0 "register_operand" "=y")
        (ashiftrt:V8QI
	 (plus:V8QI (plus:V8QI
		     (match_operand:V8QI 1 "register_operand" "y")
		     (match_operand:V8QI 2 "register_operand" "y"))
		    (const_vector:V8QI [(const_int 1)
					(const_int 1)
					(const_int 1)
					(const_int 1)
					(const_int 1)
					(const_int 1)
					(const_int 1)
					(const_int 1)]))
	 (const_int 1)))]
  "TARGET_REALLY_IWMMXT"
  "wavg2br%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_uavgrndv4hi3"
  [(set (match_operand:V4HI              0 "register_operand" "=y")
        (ashiftrt:V4HI
	 (plus:V4HI (plus:V4HI
		     (match_operand:V4HI 1 "register_operand" "y")
		     (match_operand:V4HI 2 "register_operand" "y"))
		    (const_vector:V4HI [(const_int 1)
					(const_int 1)
					(const_int 1)
					(const_int 1)]))
	 (const_int 1)))]
  "TARGET_REALLY_IWMMXT"
  "wavg2hr%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])


(define_insn "iwmmxt_uavgv8qi3"
  [(set (match_operand:V8QI                 0 "register_operand" "=y")
        (ashiftrt:V8QI (plus:V8QI
			(match_operand:V8QI 1 "register_operand" "y")
			(match_operand:V8QI 2 "register_operand" "y"))
		       (const_int 1)))]
  "TARGET_REALLY_IWMMXT"
  "wavg2b%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_uavgv4hi3"
  [(set (match_operand:V4HI                 0 "register_operand" "=y")
        (ashiftrt:V4HI (plus:V4HI
			(match_operand:V4HI 1 "register_operand" "y")
			(match_operand:V4HI 2 "register_operand" "y"))
		       (const_int 1)))]
  "TARGET_REALLY_IWMMXT"
  "wavg2h%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_psadbw"
  [(set (match_operand:V8QI                       0 "register_operand" "=y")
        (abs:V8QI (minus:V8QI (match_operand:V8QI 1 "register_operand" "y")
			      (match_operand:V8QI 2 "register_operand" "y"))))]
  "TARGET_REALLY_IWMMXT"
  "psadbw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])


;; Insert/extract/shuffle

(define_insn "iwmmxt_tinsrb"
  [(set (match_operand:V8QI                             0 "register_operand"    "=y")
        (vec_merge:V8QI (match_operand:V8QI             1 "register_operand"     "0")
			(vec_duplicate:V8QI
			 (truncate:QI (match_operand:SI 2 "nonimmediate_operand" "r")))
			(match_operand:SI               3 "immediate_operand"    "i")))]
  "TARGET_REALLY_IWMMXT"
  "tinsrb%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tinsrh"
  [(set (match_operand:V4HI                             0 "register_operand"    "=y")
        (vec_merge:V4HI (match_operand:V4HI             1 "register_operand"     "0")
			(vec_duplicate:V4HI
			 (truncate:HI (match_operand:SI 2 "nonimmediate_operand" "r")))
			(match_operand:SI               3 "immediate_operand"    "i")))]
  "TARGET_REALLY_IWMMXT"
  "tinsrh%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tinsrw"
  [(set (match_operand:V2SI                 0 "register_operand"    "=y")
        (vec_merge:V2SI (match_operand:V2SI 1 "register_operand"     "0")
			(vec_duplicate:V2SI
			 (match_operand:SI  2 "nonimmediate_operand" "r"))
			(match_operand:SI   3 "immediate_operand"    "i")))]
  "TARGET_REALLY_IWMMXT"
  "tinsrw%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_textrmub"
  [(set (match_operand:SI                                  0 "register_operand" "=r")
        (zero_extend:SI (vec_select:QI (match_operand:V8QI 1 "register_operand" "y")
				       (parallel
					[(match_operand:SI 2 "immediate_operand" "i")]))))]
  "TARGET_REALLY_IWMMXT"
  "textrmub%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_textrmsb"
  [(set (match_operand:SI                                  0 "register_operand" "=r")
        (sign_extend:SI (vec_select:QI (match_operand:V8QI 1 "register_operand" "y")
				       (parallel
					[(match_operand:SI 2 "immediate_operand" "i")]))))]
  "TARGET_REALLY_IWMMXT"
  "textrmsb%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_textrmuh"
  [(set (match_operand:SI                                  0 "register_operand" "=r")
        (zero_extend:SI (vec_select:HI (match_operand:V4HI 1 "register_operand" "y")
				       (parallel
					[(match_operand:SI 2 "immediate_operand" "i")]))))]
  "TARGET_REALLY_IWMMXT"
  "textrmuh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_textrmsh"
  [(set (match_operand:SI                                  0 "register_operand" "=r")
        (sign_extend:SI (vec_select:HI (match_operand:V4HI 1 "register_operand" "y")
				       (parallel
					[(match_operand:SI 2 "immediate_operand" "i")]))))]
  "TARGET_REALLY_IWMMXT"
  "textrmsh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

;; There are signed/unsigned variants of this instruction, but they are
;; pointless.
(define_insn "iwmmxt_textrmw"
  [(set (match_operand:SI                           0 "register_operand" "=r")
        (vec_select:SI (match_operand:V2SI          1 "register_operand" "y")
		       (parallel [(match_operand:SI 2 "immediate_operand" "i")])))]
  "TARGET_REALLY_IWMMXT"
  "textrmsw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wshufh"
  [(set (match_operand:V4HI               0 "register_operand" "=y")
        (unspec:V4HI [(match_operand:V4HI 1 "register_operand" "y")
		      (match_operand:SI   2 "immediate_operand" "i")] UNSPEC_WSHUFH))]
  "TARGET_REALLY_IWMMXT"
  "wshufh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

;; Mask-generating comparisons
;;
;; Note - you cannot use patterns like these here:
;;
;;   (set:<vector> (match:<vector>) (<comparator>:<vector> (match:<vector>) (match:<vector>)))
;;
;; Because GCC will assume that the truth value (1 or 0) is installed
;; into the entire destination vector, (with the '1' going into the least
;; significant element of the vector).  This is not how these instructions
;; behave.
;;
;; Unfortunately the current patterns are illegal.  They are SET insns
;; without a SET in them.  They work in most cases for ordinary code
;; generation, but there are circumstances where they can cause gcc to fail.
;; XXX - FIXME.

(define_insn "eqv8qi3"
  [(unspec_volatile [(match_operand:V8QI 0 "register_operand" "=y")
		     (match_operand:V8QI 1 "register_operand"  "y")
		     (match_operand:V8QI 2 "register_operand"  "y")]
		    VUNSPEC_WCMP_EQ)]
  "TARGET_REALLY_IWMMXT"
  "wcmpeqb%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "eqv4hi3"
  [(unspec_volatile [(match_operand:V4HI 0 "register_operand" "=y")
		     (match_operand:V4HI 1 "register_operand"  "y")
		     (match_operand:V4HI 2 "register_operand"  "y")]
		    VUNSPEC_WCMP_EQ)]
  "TARGET_REALLY_IWMMXT"
  "wcmpeqh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "eqv2si3"
  [(unspec_volatile:V2SI [(match_operand:V2SI 0 "register_operand" "=y")
			  (match_operand:V2SI 1 "register_operand"  "y")
			  (match_operand:V2SI 2 "register_operand"  "y")]
			 VUNSPEC_WCMP_EQ)]
  "TARGET_REALLY_IWMMXT"
  "wcmpeqw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "gtuv8qi3"
  [(unspec_volatile [(match_operand:V8QI 0 "register_operand" "=y")
		     (match_operand:V8QI 1 "register_operand"  "y")
		     (match_operand:V8QI 2 "register_operand"  "y")]
		    VUNSPEC_WCMP_GTU)]
  "TARGET_REALLY_IWMMXT"
  "wcmpgtub%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "gtuv4hi3"
  [(unspec_volatile [(match_operand:V4HI 0 "register_operand" "=y")
		     (match_operand:V4HI 1 "register_operand"  "y")
		     (match_operand:V4HI 2 "register_operand"  "y")]
		    VUNSPEC_WCMP_GTU)]
  "TARGET_REALLY_IWMMXT"
  "wcmpgtuh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "gtuv2si3"
  [(unspec_volatile [(match_operand:V2SI 0 "register_operand" "=y")
		     (match_operand:V2SI 1 "register_operand"  "y")
		     (match_operand:V2SI 2 "register_operand"  "y")]
		    VUNSPEC_WCMP_GTU)]
  "TARGET_REALLY_IWMMXT"
  "wcmpgtuw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "gtv8qi3"
  [(unspec_volatile [(match_operand:V8QI 0 "register_operand" "=y")
		     (match_operand:V8QI 1 "register_operand"  "y")
		     (match_operand:V8QI 2 "register_operand"  "y")]
		    VUNSPEC_WCMP_GT)]
  "TARGET_REALLY_IWMMXT"
  "wcmpgtsb%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "gtv4hi3"
  [(unspec_volatile [(match_operand:V4HI 0 "register_operand" "=y")
		     (match_operand:V4HI 1 "register_operand"  "y")
		     (match_operand:V4HI 2 "register_operand"  "y")]
		    VUNSPEC_WCMP_GT)]
  "TARGET_REALLY_IWMMXT"
  "wcmpgtsh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "gtv2si3"
  [(unspec_volatile [(match_operand:V2SI 0 "register_operand" "=y")
		     (match_operand:V2SI 1 "register_operand"  "y")
		     (match_operand:V2SI 2 "register_operand"  "y")]
		    VUNSPEC_WCMP_GT)]
  "TARGET_REALLY_IWMMXT"
  "wcmpgtsw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

;; Max/min insns

(define_insn "smaxv8qi3"
  [(set (match_operand:V8QI            0 "register_operand" "=y")
        (smax:V8QI (match_operand:V8QI 1 "register_operand" "y")
		   (match_operand:V8QI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wmaxsb%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "umaxv8qi3"
  [(set (match_operand:V8QI            0 "register_operand" "=y")
        (umax:V8QI (match_operand:V8QI 1 "register_operand" "y")
		   (match_operand:V8QI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wmaxub%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "smaxv4hi3"
  [(set (match_operand:V4HI            0 "register_operand" "=y")
        (smax:V4HI (match_operand:V4HI 1 "register_operand" "y")
		   (match_operand:V4HI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wmaxsh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "umaxv4hi3"
  [(set (match_operand:V4HI            0 "register_operand" "=y")
        (umax:V4HI (match_operand:V4HI 1 "register_operand" "y")
		   (match_operand:V4HI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wmaxuh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "smaxv2si3"
  [(set (match_operand:V2SI            0 "register_operand" "=y")
        (smax:V2SI (match_operand:V2SI 1 "register_operand" "y")
		   (match_operand:V2SI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wmaxsw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "umaxv2si3"
  [(set (match_operand:V2SI            0 "register_operand" "=y")
        (umax:V2SI (match_operand:V2SI 1 "register_operand" "y")
		   (match_operand:V2SI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wmaxuw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "sminv8qi3"
  [(set (match_operand:V8QI            0 "register_operand" "=y")
        (smin:V8QI (match_operand:V8QI 1 "register_operand" "y")
		   (match_operand:V8QI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wminsb%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "uminv8qi3"
  [(set (match_operand:V8QI            0 "register_operand" "=y")
        (umin:V8QI (match_operand:V8QI 1 "register_operand" "y")
		   (match_operand:V8QI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wminub%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "sminv4hi3"
  [(set (match_operand:V4HI            0 "register_operand" "=y")
        (smin:V4HI (match_operand:V4HI 1 "register_operand" "y")
		   (match_operand:V4HI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wminsh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "uminv4hi3"
  [(set (match_operand:V4HI            0 "register_operand" "=y")
        (umin:V4HI (match_operand:V4HI 1 "register_operand" "y")
		   (match_operand:V4HI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wminuh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "sminv2si3"
  [(set (match_operand:V2SI            0 "register_operand" "=y")
        (smin:V2SI (match_operand:V2SI 1 "register_operand" "y")
		   (match_operand:V2SI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wminsw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "uminv2si3"
  [(set (match_operand:V2SI            0 "register_operand" "=y")
        (umin:V2SI (match_operand:V2SI 1 "register_operand" "y")
		   (match_operand:V2SI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wminuw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

;; Pack/unpack insns.

(define_insn "iwmmxt_wpackhss"
  [(set (match_operand:V8QI                    0 "register_operand" "=y")
	(vec_concat:V8QI
	 (ss_truncate:V4QI (match_operand:V4HI 1 "register_operand" "y"))
	 (ss_truncate:V4QI (match_operand:V4HI 2 "register_operand" "y"))))]
  "TARGET_REALLY_IWMMXT"
  "wpackhss%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wpackwss"
  [(set (match_operand:V4HI                    0 "register_operand" "=y")
	(vec_concat:V4HI
	 (ss_truncate:V2HI (match_operand:V2SI 1 "register_operand" "y"))
	 (ss_truncate:V2HI (match_operand:V2SI 2 "register_operand" "y"))))]
  "TARGET_REALLY_IWMMXT"
  "wpackwss%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wpackdss"
  [(set (match_operand:V2SI                0 "register_operand" "=y")
	(vec_concat:V2SI
	 (ss_truncate:SI (match_operand:DI 1 "register_operand" "y"))
	 (ss_truncate:SI (match_operand:DI 2 "register_operand" "y"))))]
  "TARGET_REALLY_IWMMXT"
  "wpackdss%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wpackhus"
  [(set (match_operand:V8QI                    0 "register_operand" "=y")
	(vec_concat:V8QI
	 (us_truncate:V4QI (match_operand:V4HI 1 "register_operand" "y"))
	 (us_truncate:V4QI (match_operand:V4HI 2 "register_operand" "y"))))]
  "TARGET_REALLY_IWMMXT"
  "wpackhus%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wpackwus"
  [(set (match_operand:V4HI                    0 "register_operand" "=y")
	(vec_concat:V4HI
	 (us_truncate:V2HI (match_operand:V2SI 1 "register_operand" "y"))
	 (us_truncate:V2HI (match_operand:V2SI 2 "register_operand" "y"))))]
  "TARGET_REALLY_IWMMXT"
  "wpackwus%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wpackdus"
  [(set (match_operand:V2SI                0 "register_operand" "=y")
	(vec_concat:V2SI
	 (us_truncate:SI (match_operand:DI 1 "register_operand" "y"))
	 (us_truncate:SI (match_operand:DI 2 "register_operand" "y"))))]
  "TARGET_REALLY_IWMMXT"
  "wpackdus%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])


(define_insn "iwmmxt_wunpckihb"
  [(set (match_operand:V8QI                   0 "register_operand" "=y")
	(vec_merge:V8QI
	 (vec_select:V8QI (match_operand:V8QI 1 "register_operand" "y")
			  (parallel [(const_int 4)
				     (const_int 0)
				     (const_int 5)
				     (const_int 1)
				     (const_int 6)
				     (const_int 2)
				     (const_int 7)
				     (const_int 3)]))
	 (vec_select:V8QI (match_operand:V8QI 2 "register_operand" "y")
			  (parallel [(const_int 0)
				     (const_int 4)
				     (const_int 1)
				     (const_int 5)
				     (const_int 2)
				     (const_int 6)
				     (const_int 3)
				     (const_int 7)]))
	 (const_int 85)))]
  "TARGET_REALLY_IWMMXT"
  "wunpckihb%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckihh"
  [(set (match_operand:V4HI                   0 "register_operand" "=y")
	(vec_merge:V4HI
	 (vec_select:V4HI (match_operand:V4HI 1 "register_operand" "y")
			  (parallel [(const_int 0)
				     (const_int 2)
				     (const_int 1)
				     (const_int 3)]))
	 (vec_select:V4HI (match_operand:V4HI 2 "register_operand" "y")
			  (parallel [(const_int 2)
				     (const_int 0)
				     (const_int 3)
				     (const_int 1)]))
	 (const_int 5)))]
  "TARGET_REALLY_IWMMXT"
  "wunpckihh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckihw"
  [(set (match_operand:V2SI                   0 "register_operand" "=y")
	(vec_merge:V2SI
	 (vec_select:V2SI (match_operand:V2SI 1 "register_operand" "y")
			  (parallel [(const_int 0)
				     (const_int 1)]))
	 (vec_select:V2SI (match_operand:V2SI 2 "register_operand" "y")
			  (parallel [(const_int 1)
				     (const_int 0)]))
	 (const_int 1)))]
  "TARGET_REALLY_IWMMXT"
  "wunpckihw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckilb"
  [(set (match_operand:V8QI                   0 "register_operand" "=y")
	(vec_merge:V8QI
	 (vec_select:V8QI (match_operand:V8QI 1 "register_operand" "y")
			  (parallel [(const_int 0)
				     (const_int 4)
				     (const_int 1)
				     (const_int 5)
				     (const_int 2)
				     (const_int 6)
				     (const_int 3)
				     (const_int 7)]))
	 (vec_select:V8QI (match_operand:V8QI 2 "register_operand" "y")
			  (parallel [(const_int 4)
				     (const_int 0)
				     (const_int 5)
				     (const_int 1)
				     (const_int 6)
				     (const_int 2)
				     (const_int 7)
				     (const_int 3)]))
	 (const_int 85)))]
  "TARGET_REALLY_IWMMXT"
  "wunpckilb%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckilh"
  [(set (match_operand:V4HI                   0 "register_operand" "=y")
	(vec_merge:V4HI
	 (vec_select:V4HI (match_operand:V4HI 1 "register_operand" "y")
			  (parallel [(const_int 2)
				     (const_int 0)
				     (const_int 3)
				     (const_int 1)]))
	 (vec_select:V4HI (match_operand:V4HI 2 "register_operand" "y")
			  (parallel [(const_int 0)
				     (const_int 2)
				     (const_int 1)
				     (const_int 3)]))
	 (const_int 5)))]
  "TARGET_REALLY_IWMMXT"
  "wunpckilh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckilw"
  [(set (match_operand:V2SI                   0 "register_operand" "=y")
	(vec_merge:V2SI
	 (vec_select:V2SI (match_operand:V2SI 1 "register_operand" "y")
			   (parallel [(const_int 1)
				      (const_int 0)]))
	 (vec_select:V2SI (match_operand:V2SI 2 "register_operand" "y")
			  (parallel [(const_int 0)
				     (const_int 1)]))
	 (const_int 1)))]
  "TARGET_REALLY_IWMMXT"
  "wunpckilw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckehub"
  [(set (match_operand:V4HI                   0 "register_operand" "=y")
	(zero_extend:V4HI
	 (vec_select:V4QI (match_operand:V8QI 1 "register_operand" "y")
			  (parallel [(const_int 4) (const_int 5)
				     (const_int 6) (const_int 7)]))))]
  "TARGET_REALLY_IWMMXT"
  "wunpckehub%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckehuh"
  [(set (match_operand:V2SI                   0 "register_operand" "=y")
	(zero_extend:V2SI
	 (vec_select:V2HI (match_operand:V4HI 1 "register_operand" "y")
			  (parallel [(const_int 2) (const_int 3)]))))]
  "TARGET_REALLY_IWMMXT"
  "wunpckehuh%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckehuw"
  [(set (match_operand:DI                   0 "register_operand" "=y")
	(zero_extend:DI
	 (vec_select:SI (match_operand:V2SI 1 "register_operand" "y")
			(parallel [(const_int 1)]))))]
  "TARGET_REALLY_IWMMXT"
  "wunpckehuw%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckehsb"
  [(set (match_operand:V4HI                   0 "register_operand" "=y")
	(sign_extend:V4HI
	 (vec_select:V4QI (match_operand:V8QI 1 "register_operand" "y")
			  (parallel [(const_int 4) (const_int 5)
				     (const_int 6) (const_int 7)]))))]
  "TARGET_REALLY_IWMMXT"
  "wunpckehsb%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckehsh"
  [(set (match_operand:V2SI                   0 "register_operand" "=y")
	(sign_extend:V2SI
	 (vec_select:V2HI (match_operand:V4HI 1 "register_operand" "y")
			  (parallel [(const_int 2) (const_int 3)]))))]
  "TARGET_REALLY_IWMMXT"
  "wunpckehsh%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckehsw"
  [(set (match_operand:DI                   0 "register_operand" "=y")
	(sign_extend:DI
	 (vec_select:SI (match_operand:V2SI 1 "register_operand" "y")
			(parallel [(const_int 1)]))))]
  "TARGET_REALLY_IWMMXT"
  "wunpckehsw%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckelub"
  [(set (match_operand:V4HI                   0 "register_operand" "=y")
	(zero_extend:V4HI
	 (vec_select:V4QI (match_operand:V8QI 1 "register_operand" "y")
			  (parallel [(const_int 0) (const_int 1)
				     (const_int 2) (const_int 3)]))))]
  "TARGET_REALLY_IWMMXT"
  "wunpckelub%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckeluh"
  [(set (match_operand:V2SI                   0 "register_operand" "=y")
	(zero_extend:V2SI
	 (vec_select:V2HI (match_operand:V4HI 1 "register_operand" "y")
			  (parallel [(const_int 0) (const_int 1)]))))]
  "TARGET_REALLY_IWMMXT"
  "wunpckeluh%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckeluw"
  [(set (match_operand:DI                   0 "register_operand" "=y")
	(zero_extend:DI
	 (vec_select:SI (match_operand:V2SI 1 "register_operand" "y")
			(parallel [(const_int 0)]))))]
  "TARGET_REALLY_IWMMXT"
  "wunpckeluw%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckelsb"
  [(set (match_operand:V4HI                   0 "register_operand" "=y")
	(sign_extend:V4HI
	 (vec_select:V4QI (match_operand:V8QI 1 "register_operand" "y")
			  (parallel [(const_int 0) (const_int 1)
				     (const_int 2) (const_int 3)]))))]
  "TARGET_REALLY_IWMMXT"
  "wunpckelsb%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckelsh"
  [(set (match_operand:V2SI                   0 "register_operand" "=y")
	(sign_extend:V2SI
	 (vec_select:V2HI (match_operand:V4HI 1 "register_operand" "y")
			  (parallel [(const_int 0) (const_int 1)]))))]
  "TARGET_REALLY_IWMMXT"
  "wunpckelsh%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wunpckelsw"
  [(set (match_operand:DI                   0 "register_operand" "=y")
	(sign_extend:DI
	 (vec_select:SI (match_operand:V2SI 1 "register_operand" "y")
			(parallel [(const_int 0)]))))]
  "TARGET_REALLY_IWMMXT"
  "wunpckelsw%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

;; Shifts

(define_insn "rorv4hi3"
  [(set (match_operand:V4HI                0 "register_operand" "=y")
        (rotatert:V4HI (match_operand:V4HI 1 "register_operand" "y")
		       (match_operand:SI   2 "register_operand" "z")))]
  "TARGET_REALLY_IWMMXT"
  "wrorhg%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "rorv2si3"
  [(set (match_operand:V2SI                0 "register_operand" "=y")
        (rotatert:V2SI (match_operand:V2SI 1 "register_operand" "y")
		       (match_operand:SI   2 "register_operand" "z")))]
  "TARGET_REALLY_IWMMXT"
  "wrorwg%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "rordi3"
  [(set (match_operand:DI              0 "register_operand" "=y")
	(rotatert:DI (match_operand:DI 1 "register_operand" "y")
		   (match_operand:SI   2 "register_operand" "z")))]
  "TARGET_REALLY_IWMMXT"
  "wrordg%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ashrv4hi3"
  [(set (match_operand:V4HI                0 "register_operand" "=y")
        (ashiftrt:V4HI (match_operand:V4HI 1 "register_operand" "y")
		       (match_operand:SI   2 "register_operand" "z")))]
  "TARGET_REALLY_IWMMXT"
  "wsrahg%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ashrv2si3"
  [(set (match_operand:V2SI                0 "register_operand" "=y")
        (ashiftrt:V2SI (match_operand:V2SI 1 "register_operand" "y")
		       (match_operand:SI   2 "register_operand" "z")))]
  "TARGET_REALLY_IWMMXT"
  "wsrawg%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ashrdi3_iwmmxt"
  [(set (match_operand:DI              0 "register_operand" "=y")
	(ashiftrt:DI (match_operand:DI 1 "register_operand" "y")
		   (match_operand:SI   2 "register_operand" "z")))]
  "TARGET_REALLY_IWMMXT"
  "wsradg%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "lshrv4hi3"
  [(set (match_operand:V4HI                0 "register_operand" "=y")
        (lshiftrt:V4HI (match_operand:V4HI 1 "register_operand" "y")
		       (match_operand:SI   2 "register_operand" "z")))]
  "TARGET_REALLY_IWMMXT"
  "wsrlhg%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "lshrv2si3"
  [(set (match_operand:V2SI                0 "register_operand" "=y")
        (lshiftrt:V2SI (match_operand:V2SI 1 "register_operand" "y")
		       (match_operand:SI   2 "register_operand" "z")))]
  "TARGET_REALLY_IWMMXT"
  "wsrlwg%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "lshrdi3_iwmmxt"
  [(set (match_operand:DI              0 "register_operand" "=y")
	(lshiftrt:DI (match_operand:DI 1 "register_operand" "y")
		     (match_operand:SI 2 "register_operand" "z")))]
  "TARGET_REALLY_IWMMXT"
  "wsrldg%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ashlv4hi3"
  [(set (match_operand:V4HI              0 "register_operand" "=y")
        (ashift:V4HI (match_operand:V4HI 1 "register_operand" "y")
		     (match_operand:SI   2 "register_operand" "z")))]
  "TARGET_REALLY_IWMMXT"
  "wsllhg%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ashlv2si3"
  [(set (match_operand:V2SI              0 "register_operand" "=y")
        (ashift:V2SI (match_operand:V2SI 1 "register_operand" "y")
		       (match_operand:SI 2 "register_operand" "z")))]
  "TARGET_REALLY_IWMMXT"
  "wsllwg%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ashldi3_iwmmxt"
  [(set (match_operand:DI            0 "register_operand" "=y")
	(ashift:DI (match_operand:DI 1 "register_operand" "y")
		   (match_operand:SI 2 "register_operand" "z")))]
  "TARGET_REALLY_IWMMXT"
  "wslldg%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "rorv4hi3_di"
  [(set (match_operand:V4HI                0 "register_operand" "=y")
        (rotatert:V4HI (match_operand:V4HI 1 "register_operand" "y")
		       (match_operand:DI   2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wrorh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "rorv2si3_di"
  [(set (match_operand:V2SI                0 "register_operand" "=y")
        (rotatert:V2SI (match_operand:V2SI 1 "register_operand" "y")
		       (match_operand:DI   2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wrorw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "rordi3_di"
  [(set (match_operand:DI              0 "register_operand" "=y")
	(rotatert:DI (match_operand:DI 1 "register_operand" "y")
		   (match_operand:DI   2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wrord%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ashrv4hi3_di"
  [(set (match_operand:V4HI                0 "register_operand" "=y")
        (ashiftrt:V4HI (match_operand:V4HI 1 "register_operand" "y")
		       (match_operand:DI   2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsrah%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ashrv2si3_di"
  [(set (match_operand:V2SI                0 "register_operand" "=y")
        (ashiftrt:V2SI (match_operand:V2SI 1 "register_operand" "y")
		       (match_operand:DI   2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsraw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ashrdi3_di"
  [(set (match_operand:DI              0 "register_operand" "=y")
	(ashiftrt:DI (match_operand:DI 1 "register_operand" "y")
		   (match_operand:DI   2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsrad%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "lshrv4hi3_di"
  [(set (match_operand:V4HI                0 "register_operand" "=y")
        (lshiftrt:V4HI (match_operand:V4HI 1 "register_operand" "y")
		       (match_operand:DI   2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsrlh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "lshrv2si3_di"
  [(set (match_operand:V2SI                0 "register_operand" "=y")
        (lshiftrt:V2SI (match_operand:V2SI 1 "register_operand" "y")
		       (match_operand:DI   2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsrlw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "lshrdi3_di"
  [(set (match_operand:DI              0 "register_operand" "=y")
	(lshiftrt:DI (match_operand:DI 1 "register_operand" "y")
		     (match_operand:DI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsrld%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ashlv4hi3_di"
  [(set (match_operand:V4HI              0 "register_operand" "=y")
        (ashift:V4HI (match_operand:V4HI 1 "register_operand" "y")
		     (match_operand:DI   2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsllh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ashlv2si3_di"
  [(set (match_operand:V2SI              0 "register_operand" "=y")
        (ashift:V2SI (match_operand:V2SI 1 "register_operand" "y")
		       (match_operand:DI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wsllw%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "ashldi3_di"
  [(set (match_operand:DI            0 "register_operand" "=y")
	(ashift:DI (match_operand:DI 1 "register_operand" "y")
		   (match_operand:DI 2 "register_operand" "y")))]
  "TARGET_REALLY_IWMMXT"
  "wslld%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wmadds"
  [(set (match_operand:V4HI               0 "register_operand" "=y")
        (unspec:V4HI [(match_operand:V4HI 1 "register_operand" "y")
		      (match_operand:V4HI 2 "register_operand" "y")] UNSPEC_WMADDS))]
  "TARGET_REALLY_IWMMXT"
  "wmadds%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wmaddu"
  [(set (match_operand:V4HI               0 "register_operand" "=y")
        (unspec:V4HI [(match_operand:V4HI 1 "register_operand" "y")
		      (match_operand:V4HI 2 "register_operand" "y")] UNSPEC_WMADDU))]
  "TARGET_REALLY_IWMMXT"
  "wmaddu%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tmia"
  [(set (match_operand:DI                    0 "register_operand" "=y")
	(plus:DI (match_operand:DI           1 "register_operand" "0")
		 (mult:DI (sign_extend:DI
			   (match_operand:SI 2 "register_operand" "r"))
			  (sign_extend:DI
			   (match_operand:SI 3 "register_operand" "r")))))]
  "TARGET_REALLY_IWMMXT"
  "tmia%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tmiaph"
  [(set (match_operand:DI          0 "register_operand" "=y")
	(plus:DI (match_operand:DI 1 "register_operand" "0")
		 (plus:DI
		  (mult:DI (sign_extend:DI
			    (truncate:HI (match_operand:SI 2 "register_operand" "r")))
			   (sign_extend:DI
			    (truncate:HI (match_operand:SI 3 "register_operand" "r"))))
		  (mult:DI (sign_extend:DI
			    (truncate:HI (ashiftrt:SI (match_dup 2) (const_int 16))))
			   (sign_extend:DI
			    (truncate:HI (ashiftrt:SI (match_dup 3) (const_int 16))))))))]
  "TARGET_REALLY_IWMMXT"
  "tmiaph%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tmiabb"
  [(set (match_operand:DI          0 "register_operand" "=y")
	(plus:DI (match_operand:DI 1 "register_operand" "0")
		 (mult:DI (sign_extend:DI
			   (truncate:HI (match_operand:SI 2 "register_operand" "r")))
			  (sign_extend:DI
			   (truncate:HI (match_operand:SI 3 "register_operand" "r"))))))]
  "TARGET_REALLY_IWMMXT"
  "tmiabb%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tmiatb"
  [(set (match_operand:DI          0 "register_operand" "=y")
	(plus:DI (match_operand:DI 1 "register_operand" "0")
		 (mult:DI (sign_extend:DI
			   (truncate:HI (ashiftrt:SI
					 (match_operand:SI 2 "register_operand" "r")
					 (const_int 16))))
			  (sign_extend:DI
			   (truncate:HI (match_operand:SI 3 "register_operand" "r"))))))]
  "TARGET_REALLY_IWMMXT"
  "tmiatb%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tmiabt"
  [(set (match_operand:DI          0 "register_operand" "=y")
	(plus:DI (match_operand:DI 1 "register_operand" "0")
		 (mult:DI (sign_extend:DI
			   (truncate:HI (match_operand:SI 2 "register_operand" "r")))
			  (sign_extend:DI
			   (truncate:HI (ashiftrt:SI
					 (match_operand:SI 3 "register_operand" "r")
					 (const_int 16)))))))]
  "TARGET_REALLY_IWMMXT"
  "tmiabt%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tmiatt"
  [(set (match_operand:DI          0 "register_operand" "=y")
	(plus:DI (match_operand:DI 1 "register_operand" "0")
		 (mult:DI (sign_extend:DI
			   (truncate:HI (ashiftrt:SI
					 (match_operand:SI 2 "register_operand" "r")
					 (const_int 16))))
			  (sign_extend:DI
			   (truncate:HI (ashiftrt:SI
					 (match_operand:SI 3 "register_operand" "r")
					 (const_int 16)))))))]
  "TARGET_REALLY_IWMMXT"
  "tmiatt%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tbcstqi"
  [(set (match_operand:V8QI                   0 "register_operand" "=y")
	(vec_duplicate:V8QI (match_operand:QI 1 "register_operand" "r")))]
  "TARGET_REALLY_IWMMXT"
  "tbcstb%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tbcsthi"
  [(set (match_operand:V4HI                   0 "register_operand" "=y")
	(vec_duplicate:V4HI (match_operand:HI 1 "register_operand" "r")))]
  "TARGET_REALLY_IWMMXT"
  "tbcsth%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tbcstsi"
  [(set (match_operand:V2SI                   0 "register_operand" "=y")
	(vec_duplicate:V2SI (match_operand:SI 1 "register_operand" "r")))]
  "TARGET_REALLY_IWMMXT"
  "tbcstw%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tmovmskb"
  [(set (match_operand:SI               0 "register_operand" "=r")
	(unspec:SI [(match_operand:V8QI 1 "register_operand" "y")] UNSPEC_TMOVMSK))]
  "TARGET_REALLY_IWMMXT"
  "tmovmskb%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tmovmskh"
  [(set (match_operand:SI               0 "register_operand" "=r")
	(unspec:SI [(match_operand:V4HI 1 "register_operand" "y")] UNSPEC_TMOVMSK))]
  "TARGET_REALLY_IWMMXT"
  "tmovmskh%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tmovmskw"
  [(set (match_operand:SI               0 "register_operand" "=r")
	(unspec:SI [(match_operand:V2SI 1 "register_operand" "y")] UNSPEC_TMOVMSK))]
  "TARGET_REALLY_IWMMXT"
  "tmovmskw%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_waccb"
  [(set (match_operand:DI               0 "register_operand" "=y")
	(unspec:DI [(match_operand:V8QI 1 "register_operand" "y")] UNSPEC_WACC))]
  "TARGET_REALLY_IWMMXT"
  "waccb%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wacch"
  [(set (match_operand:DI               0 "register_operand" "=y")
	(unspec:DI [(match_operand:V4HI 1 "register_operand" "y")] UNSPEC_WACC))]
  "TARGET_REALLY_IWMMXT"
  "wacch%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_waccw"
  [(set (match_operand:DI               0 "register_operand" "=y")
	(unspec:DI [(match_operand:V2SI 1 "register_operand" "y")] UNSPEC_WACC))]
  "TARGET_REALLY_IWMMXT"
  "waccw%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_walign"
  [(set (match_operand:V8QI                           0 "register_operand" "=y,y")
	(subreg:V8QI (ashiftrt:TI
		      (subreg:TI (vec_concat:V16QI
				  (match_operand:V8QI 1 "register_operand" "y,y")
				  (match_operand:V8QI 2 "register_operand" "y,y")) 0)
		      (mult:SI
		       (match_operand:SI              3 "nonmemory_operand" "i,z")
		       (const_int 8))) 0))]
  "TARGET_REALLY_IWMMXT"
  "@
   waligni%?\\t%0, %1, %2, %3
   walignr%U3%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tmrc"
  [(set (match_operand:SI                      0 "register_operand" "=r")
	(unspec_volatile:SI [(match_operand:SI 1 "immediate_operand" "i")]
			    VUNSPEC_TMRC))]
  "TARGET_REALLY_IWMMXT"
  "tmrc%?\\t%0, %w1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_tmcr"
  [(unspec_volatile:SI [(match_operand:SI 0 "immediate_operand" "i")
			(match_operand:SI 1 "register_operand"  "r")]
		       VUNSPEC_TMCR)]
  "TARGET_REALLY_IWMMXT"
  "tmcr%?\\t%w0, %1"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wsadb"
  [(set (match_operand:V8QI               0 "register_operand" "=y")
        (unspec:V8QI [(match_operand:V8QI 1 "register_operand" "y")
		      (match_operand:V8QI 2 "register_operand" "y")] UNSPEC_WSAD))]
  "TARGET_REALLY_IWMMXT"
  "wsadb%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wsadh"
  [(set (match_operand:V4HI               0 "register_operand" "=y")
        (unspec:V4HI [(match_operand:V4HI 1 "register_operand" "y")
		      (match_operand:V4HI 2 "register_operand" "y")] UNSPEC_WSAD))]
  "TARGET_REALLY_IWMMXT"
  "wsadh%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wsadbz"
  [(set (match_operand:V8QI               0 "register_operand" "=y")
        (unspec:V8QI [(match_operand:V8QI 1 "register_operand" "y")
		      (match_operand:V8QI 2 "register_operand" "y")] UNSPEC_WSADZ))]
  "TARGET_REALLY_IWMMXT"
  "wsadbz%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])

(define_insn "iwmmxt_wsadhz"
  [(set (match_operand:V4HI               0 "register_operand" "=y")
        (unspec:V4HI [(match_operand:V4HI 1 "register_operand" "y")
		      (match_operand:V4HI 2 "register_operand" "y")] UNSPEC_WSADZ))]
  "TARGET_REALLY_IWMMXT"
  "wsadhz%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")])


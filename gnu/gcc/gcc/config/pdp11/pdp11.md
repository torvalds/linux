;;- Machine description for the pdp11 for GNU C compiler
;; Copyright (C) 1994, 1995, 1997, 1998, 1999, 2000, 2001, 2004, 2005
;; Free Software Foundation, Inc.
;; Contributed by Michael K. Gschwind (mike@vlsivie.tuwien.ac.at).

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.

;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.


;; HI is 16 bit
;; QI is 8 bit 

;;- See file "rtl.def" for documentation on define_insn, match_*, et. al.

;;- cpp macro #define NOTICE_UPDATE_CC in file tm.h handles condition code
;;- updates for most instructions.

;;- Operand classes for the register allocator:

;; Compare instructions.

;; currently we only support df floats, which saves us quite some
;; hassle switching the FP mode! 
;; we assume that CPU is always in long float mode, and 
;; 16 bit integer mode - currently, the prologue for main does this,
;; but maybe we should just set up a NEW crt0 properly, 
;; -- and what about signal handling code?
;; (we don't even let sf floats in the register file, so
;; we only should have to worry about truncating and widening 
;; when going to memory)

;; abort() call by g++ - must define libfunc for cmp_optab
;; and ucmp_optab for mode SImode, because we don't have that!!!
;; - yet since no libfunc is there, we abort ()

;; The only thing that remains to be done then is output 
;; the floats in a way the assembler can handle it (and 
;; if you're really into it, use a PDP11 float emulation
;; library to do floating point constant folding - but 
;; I guess you'll get reasonable results even when not
;; doing this)
;; the last thing to do is fix the UPDATE_CC macro to check
;; for floating point condition codes, and set cc_status
;; properly, also setting the CC_IN_FCCR flag. 

;; define attributes
;; currently type is only fpu or arith or unknown, maybe branch later ?
;; default is arith
(define_attr "type" "unknown,arith,fp" (const_string "arith"))

;; length default is 1 word each
(define_attr "length" "" (const_int 1))

;; a user's asm statement
(define_asm_attributes
  [(set_attr "type" "unknown")
; all bets are off how long it is - make it 256, forces long jumps 
; whenever jumping around it !!!
   (set_attr "length" "256")])

;; define function units

;; arithmetic - values here immediately when next insn issued
;; or does it mean the number of cycles after this insn was issued?
;; how do I say that fpu insns use cpu also? (pre-interaction phase)

;(define_function_unit "cpu" 1 1 (eq_attr "type" "arith") 0 0)
;(define_function_unit "fpu" 1 1 (eq_attr "type" "fp") 0 0)

;; compare
(define_insn "cmpdf"
  [(set (cc0)
	(compare (match_operand:DF 0 "general_operand" "fR,Q,F")
		 (match_operand:DF 1 "register_operand" "a,a,a")))]
  "TARGET_FPU"
  "*
{
  cc_status.flags = CC_IN_FPU;
  return \"{cmpd|cmpf} %0, %1\;cfcc\";
}"
  [(set_attr "length" "2,3,6")])

;; a bit of brain damage, maybe inline later - 
;; problem is - gcc seems to NEED SImode because 
;; of the cmp weirdness - maybe change gcc to handle this?

(define_expand "cmpsi"
  [(set (reg:SI 0)
	(match_operand:SI 0 "general_operand" "g"))
   (set (reg:SI 2)
	(match_operand:SI 1 "general_operand" "g"))
   (parallel [(set (cc0)
		   (compare (reg:SI 0)
			    (reg:SI 2)))
	      (clobber (reg:SI 0))])]
  "0" ;; disable for test
  "")

;; check for next insn for branch code - does this still
;; work in gcc 2.* ?

(define_insn ""
  [(set (cc0)
	(compare (reg:SI 0)
		 (reg:SI 2)))
   (clobber (reg:SI 0))]
  ""
  "*
{
  rtx br_insn = NEXT_INSN (insn);
  RTX_CODE br_code;

  gcc_assert (GET_CODE (br_insn) == JUMP_INSN);
  br_code =  GET_CODE (XEXP (XEXP (PATTERN (br_insn), 1), 0));
  
  switch(br_code)
  {
    case GEU:
    case LTU:
    case GTU:
    case LEU:
      
      return \"jsr pc, ___ucmpsi\;cmp $1,r0\";

    case GE:
    case LT:
    case GT:
    case LE:
    case EQ:
    case NE:

      return \"jsr pc, ___cmpsi\;tst r0\";

    default:

      gcc_unreachable ();
  }
}"
  [(set_attr "length" "4")])


(define_insn "cmphi"
  [(set (cc0)
	(compare (match_operand:HI 0 "general_operand" "rR,rR,Qi,Qi")
		 (match_operand:HI 1 "general_operand" "rR,Qi,rR,Qi")))]
  ""
  "cmp %0,%1"
  [(set_attr "length" "1,2,2,3")])

(define_insn "cmpqi"
  [(set (cc0)
	(compare (match_operand:QI 0 "general_operand" "rR,rR,Qi,Qi")
		 (match_operand:QI 1 "general_operand" "rR,Qi,rR,Qi")))]
  ""
  "cmpb %0,%1"
  [(set_attr "length" "1,2,2,3")])
			   

;; We have to have this because cse can optimize the previous pattern
;; into this one.

(define_insn "tstdf"
  [(set (cc0)
	(match_operand:DF 0 "general_operand" "fR,Q"))]
  "TARGET_FPU"
  "*
{
  cc_status.flags = CC_IN_FPU;
  return \"{tstd|tstf} %0\;cfcc\";
}"
  [(set_attr "length" "2,3")])


(define_expand "tstsi"
  [(set (reg:SI 0)
	(match_operand:SI 0 "general_operand" "g"))
   (parallel [(set (cc0)
		   (reg:SI 0))
	      (clobber (reg:SI 0))])]
  "0" ;; disable for test
  "")

(define_insn ""
  [(set (cc0)
	(reg:SI 0))
   (clobber (reg:SI 0))]
  ""
  "jsr pc, ___tstsi\;tst r0"
  [(set_attr "length" "3")])


(define_insn "tsthi"
  [(set (cc0)
	(match_operand:HI 0 "general_operand" "rR,Q"))]
  ""
  "tst %0"
  [(set_attr "length" "1,2")])

(define_insn "tstqi"
  [(set (cc0)
	(match_operand:QI 0 "general_operand" "rR,Q"))]
  ""
  "tstb %0"
  [(set_attr "length" "1,2")])

;; sob instruction - we need an assembler which can make this instruction
;; valid under _all_ circumstances!

(define_insn ""
  [(set (pc)
	(if_then_else
	 (ne (plus:HI (match_operand:HI 0 "register_operand" "+r")
		      (const_int -1))
	     (const_int 0))
	 (label_ref (match_operand 1 "" ""))
	 (pc)))
   (set (match_dup 0)
	(plus:HI (match_dup 0)
		 (const_int -1)))]
  "TARGET_40_PLUS"
  "*
{
 static int labelcount = 0;
 static char buf[1000];

 if (get_attr_length (insn) == 1)
    return \"sob %0, %l1\";

 /* emulate sob */
 output_asm_insn (\"dec %0\", operands);
 
 sprintf (buf, \"bge LONG_SOB%d\", labelcount);
 output_asm_insn (buf, NULL);

 output_asm_insn (\"jmp %l1\", operands);
 
 sprintf (buf, \"LONG_SOB%d:\", labelcount++);
 output_asm_insn (buf, NULL);

 return \"\";
}"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						       (pc))
						(const_int -256))
					   (ge (minus (match_dup 0)
						       (pc))
						(const_int 0)))
				      (const_int 4)
				      (const_int 1)))])

;; These control RTL generation for conditional jump insns
;; and match them for register allocation.

;; problem with too short jump distance! we need an assembler which can 
;; make this valid for all jump distances!
;; e.g. gas!

;; these must be changed to check for CC_IN_FCCR if float is to be 
;; enabled

(define_insn "beq"
  [(set (pc)
	(if_then_else (eq (cc0)
			  (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "* return output_jump(\"beq\", \"bne\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])


(define_insn "bne"
  [(set (pc)
	(if_then_else (ne (cc0)
			  (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "* return output_jump(\"bne\", \"beq\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn "bgt"
  [(set (pc)
	(if_then_else (gt (cc0)
			  (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "* return output_jump(\"bgt\", \"ble\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn "bgtu"
  [(set (pc)
	(if_then_else (gtu (cc0)
			   (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "* return output_jump(\"bhi\", \"blos\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn "blt"
  [(set (pc)
	(if_then_else (lt (cc0)
			  (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "* return output_jump(\"blt\", \"bge\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])


(define_insn "bltu"
  [(set (pc)
	(if_then_else (ltu (cc0)
			   (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "* return output_jump(\"blo\", \"bhis\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn "bge"
  [(set (pc)
	(if_then_else (ge (cc0)
			  (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "* return output_jump(\"bge\", \"blt\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn "bgeu"
  [(set (pc)
	(if_then_else (geu (cc0)
			   (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "* return output_jump(\"bhis\", \"blo\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn "ble"
  [(set (pc)
	(if_then_else (le (cc0)
			  (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "* return output_jump(\"ble\", \"bgt\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn "bleu"
  [(set (pc)
	(if_then_else (leu (cc0)
			   (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "* return output_jump(\"blos\", \"bhi\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])


;; These match inverted jump insns for register allocation.

(define_insn ""
  [(set (pc)
	(if_then_else (eq (cc0)
			  (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""
  "* return output_jump(\"bne\", \"beq\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn ""
  [(set (pc)
	(if_then_else (ne (cc0)
			  (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""
  "* return output_jump(\"beq\", \"bne\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn ""
  [(set (pc)
	(if_then_else (gt (cc0)
			  (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""
  "* return output_jump(\"ble\", \"bgt\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn ""
  [(set (pc)
	(if_then_else (gtu (cc0)
			   (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""
  "* return output_jump(\"blos\", \"bhi\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn ""
  [(set (pc)
	(if_then_else (lt (cc0)
			  (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""
  "* return output_jump(\"bge\", \"blt\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn ""
  [(set (pc)
	(if_then_else (ltu (cc0)
			   (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""
  "* return output_jump(\"bhis\", \"blo\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn ""
  [(set (pc)
	(if_then_else (ge (cc0)
			  (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""  
  "* return output_jump(\"blt\", \"bge\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn ""
  [(set (pc)
	(if_then_else (geu (cc0)
			   (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""
  "* return output_jump(\"blo\", \"bhis\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn ""
  [(set (pc)
	(if_then_else (le (cc0)
			  (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""
  "* return output_jump(\"bgt\", \"ble\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

(define_insn ""
  [(set (pc)
	(if_then_else (leu (cc0)
			   (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""
  "* return output_jump(\"bhi\", \"blos\", get_attr_length(insn));"
  [(set (attr "length") (if_then_else (ior (le (minus (match_dup 0)
						      (pc))
					       (const_int -128))
					   (ge (minus (match_dup 0)
						      (pc))
					       (const_int 128)))
				      (const_int 3)
				      (const_int 1)))])

;; Move instructions

(define_insn "movdi"
  [(set (match_operand:DI 0 "general_operand" "=g,rm,o")
	(match_operand:DI 1 "general_operand" "m,r,a"))]
  ""
  "* return output_move_quad (operands);"
;; what's the mose expensive code - say twice movsi = 16
  [(set_attr "length" "16,16,16")])

(define_insn "movsi"
  [(set (match_operand:SI 0 "general_operand" "=r,r,r,rm,m")
	(match_operand:SI 1 "general_operand" "rN,IJ,K,m,r"))]
  ""
  "* return output_move_double (operands);"
;; what's the most expensive code ? - I think 8!
;; we could split it up and make several sub-cases...
  [(set_attr "length" "2,3,4,8,8")])

(define_insn "movhi"
  [(set (match_operand:HI 0 "general_operand" "=rR,rR,Q,Q")
	(match_operand:HI 1 "general_operand" "rRN,Qi,rRN,Qi"))]
  ""
  "*
{
  if (operands[1] == const0_rtx)
    return \"clr %0\";

  return \"mov %1, %0\";
}"
  [(set_attr "length" "1,2,2,3")])

(define_insn "movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=g")
	(match_operand:QI 1 "general_operand" "g"))]
  ""
  "*
{
  if (operands[1] == const0_rtx)
    return \"clrb %0\";

  return \"movb %1, %0\";
}"
  [(set_attr "length" "1")])

;; do we have to supply all these moves? e.g. to 
;; NO_LOAD_FPU_REGs ? 
(define_insn "movdf"
  [(set (match_operand:DF 0 "general_operand" "=a,fR,a,Q,m")
        (match_operand:DF 1 "general_operand" "fFR,a,Q,a,m"))]
  ""
  "* if (which_alternative ==0)
       return \"ldd %1, %0\";
     else if (which_alternative == 1)
       return \"std %1, %0\";
     else 
       return output_move_quad (operands); "
;; just a guess..
  [(set_attr "length" "1,1,5,5,16")])

(define_insn "movsf"
  [(set (match_operand:SF 0 "general_operand" "=g,r,g")
        (match_operand:SF 1 "general_operand" "r,rmF,g"))]
  "TARGET_FPU"
  "* return output_move_double (operands);"
  [(set_attr "length" "8,8,8")])

;; maybe fiddle a bit with move_ratio, then 
;; let constraints only accept a register ...

(define_expand "movmemhi"
  [(parallel [(set (match_operand:BLK 0 "general_operand" "=g,g")
		   (match_operand:BLK 1 "general_operand" "g,g"))
	      (use (match_operand:HI 2 "arith_operand" "n,&mr"))
	      (use (match_operand:HI 3 "immediate_operand" "i,i"))
	      (clobber (match_scratch:HI 4 "=&r,X"))
	      (clobber (match_dup 5))
	      (clobber (match_dup 6))
	      (clobber (match_dup 2))])]
  "(TARGET_BCOPY_BUILTIN)"
  "
{
  operands[0]
    = replace_equiv_address (operands[0],
			     copy_to_mode_reg (Pmode, XEXP (operands[0], 0)));
  operands[1]
    = replace_equiv_address (operands[1],
			     copy_to_mode_reg (Pmode, XEXP (operands[1], 0)));

  operands[5] = XEXP (operands[0], 0);
  operands[6] = XEXP (operands[1], 0);
}")


(define_insn "" ; "movmemhi"
  [(set (mem:BLK (match_operand:HI 0 "general_operand" "=r,r"))
	(mem:BLK (match_operand:HI 1 "general_operand" "r,r")))
   (use (match_operand:HI 2 "arith_operand" "n,&r"))
   (use (match_operand:HI 3 "immediate_operand" "i,i"))
   (clobber (match_scratch:HI 4 "=&r,X"))
   (clobber (match_dup 0))
   (clobber (match_dup 1))
   (clobber (match_dup 2))]
  "(TARGET_BCOPY_BUILTIN)"
  "* return output_block_move (operands);"
;;; just a guess
  [(set_attr "length" "40")])
   


;;- truncation instructions

(define_insn  "truncdfsf2"
  [(set (match_operand:SF 0 "general_operand" "=r,R,Q")
	(float_truncate:SF (match_operand:DF 1 "register_operand" "a,a,a")))]
  "TARGET_FPU"
  "* if (which_alternative ==0)
     {
       output_asm_insn(\"{stcdf|movfo} %1, -(sp)\", operands);
       output_asm_insn(\"mov (sp)+, %0\", operands);
       operands[0] = gen_rtx_REG (HImode, REGNO (operands[0])+1);
       output_asm_insn(\"mov (sp)+, %0\", operands);
       return \"\";
     }
     else if (which_alternative == 1)
       return \"{stcdf|movfo} %1, %0\";
     else 
       return \"{stcdf|movfo} %1, %0\";
  "
  [(set_attr "length" "3,1,2")])


(define_expand "truncsihi2"
  [(set (match_operand:HI 0 "general_operand" "=g")
	(subreg:HI 
	  (match_operand:SI 1 "general_operand" "or")
          0))]
  ""
  "")


;;- zero extension instructions

(define_insn "zero_extendqihi2"
  [(set (match_operand:HI 0 "general_operand" "=r")
	(zero_extend:HI (match_operand:QI 1 "general_operand" "0")))]
  ""
  "bic $0177400, %0"
  [(set_attr "length" "2")])
			 
(define_expand "zero_extendhisi2"
  [(set (subreg:HI 
          (match_dup 0)
          2)
        (match_operand:HI 1 "register_operand" "r"))
   (set (subreg:HI 
          (match_operand:SI 0 "register_operand" "=r")
          0)
        (const_int 0))]
  ""
  "/* operands[1] = make_safe_from (operands[1], operands[0]); */")


;;- sign extension instructions

(define_insn "extendsfdf2"
  [(set (match_operand:DF 0 "register_operand" "=a,a,a")
	(float_extend:DF (match_operand:SF 1 "general_operand" "r,R,Q")))]
  "TARGET_FPU"
  "@
   mov %1, -(sp)\;{ldcfd|movof} (sp)+,%0
   {ldcfd|movof} %1, %0
   {ldcfd|movof} %1, %0"
  [(set_attr "length" "2,1,2")])

;; does movb sign extend in register-to-register move?
(define_insn "extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(sign_extend:HI (match_operand:QI 1 "general_operand" "rR,Q")))]
  ""
  "movb %1, %0"
  [(set_attr "length" "1,2")])

(define_insn "extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(sign_extend:SI (match_operand:QI 1 "general_operand" "rR,Q")))]
  "TARGET_40_PLUS"
  "*
{
  rtx latehalf[2];

  /* make register pair available */
  latehalf[0] = operands[0];
  operands[0] = gen_rtx_REG (HImode, REGNO (operands[0])+ 1);

  output_asm_insn(\"movb %1, %0\", operands);
  output_asm_insn(\"sxt %0\", latehalf);
    
  return \"\";
}"
  [(set_attr "length" "2,3")])

;; maybe we have to use define_expand to say that we have the instruction,
;; unconditionally, and then match dependent on CPU type:

(define_expand "extendhisi2"
  [(set (match_operand:SI 0 "general_operand" "=g")
	(sign_extend:SI (match_operand:HI 1 "general_operand" "g")))]
  ""
  "")
  
(define_insn "" ; "extendhisi2"
  [(set (match_operand:SI 0 "general_operand" "=o,<,r")
	(sign_extend:SI (match_operand:HI 1 "general_operand" "g,g,g")))]
  "TARGET_40_PLUS"
  "*
{
  rtx latehalf[2];

  /* we don't want to mess with auto increment */
  
  switch (which_alternative)
  {
    case 0:

      latehalf[0] = operands[0];
      operands[0] = adjust_address(operands[0], HImode, 2);
  
      output_asm_insn(\"mov %1, %0\", operands);
      output_asm_insn(\"sxt %0\", latehalf);

      return \"\";

    case 1:

      /* - auto-decrement - right direction ;-) */
      output_asm_insn(\"mov %1, %0\", operands);
      output_asm_insn(\"sxt %0\", operands);

      return \"\";

    case 2:

      /* make register pair available */
      latehalf[0] = operands[0];
      operands[0] = gen_rtx_REG (HImode, REGNO (operands[0]) + 1);

      output_asm_insn(\"mov %1, %0\", operands);
      output_asm_insn(\"sxt %0\", latehalf);

      return \"\";

    default:

      gcc_unreachable ();
  }
}"
  [(set_attr "length" "5,3,3")])


(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (match_operand:HI 1 "general_operand" "0")))]
  "(! TARGET_40_PLUS)"
  "*
{
  static int count = 0;
  char buf[100];
  rtx lateoperands[2];

  lateoperands[0] = operands[0];
  operands[0] = gen_rtx_REG (HImode, REGNO (operands[0]) + 1);

  output_asm_insn(\"tst %0\", operands);
  sprintf(buf, \"bge extendhisi%d\", count);
  output_asm_insn(buf, NULL);
  output_asm_insn(\"mov -1, %0\", lateoperands);
  sprintf(buf, \"bne extendhisi%d\", count+1);
  output_asm_insn(buf, NULL);
  sprintf(buf, \"\\nextendhisi%d:\", count);
  output_asm_insn(buf, NULL);
  output_asm_insn(\"clr %0\", lateoperands);
  sprintf(buf, \"\\nextendhisi%d:\", count+1);
  output_asm_insn(buf, NULL);

  count += 2;

  return \"\";
}"
  [(set_attr "length" "6")])

;; make float to int and vice versa 
;; using the cc_status.flag field we could probably cut down
;; on seti and setl
;; assume that we are normally in double and integer mode -
;; what do pdp library routines do to fpu mode ?

(define_insn "floatsidf2"
  [(set (match_operand:DF 0 "register_operand" "=a,a,a")
	(float:DF (match_operand:SI 1 "general_operand" "r,R,Q")))]
  "TARGET_FPU"
  "* if (which_alternative ==0)
     {
       rtx latehalf[2];

       latehalf[0] = NULL; 
       latehalf[1] = gen_rtx_REG (HImode, REGNO (operands[1]) + 1);
       output_asm_insn(\"mov %1, -(sp)\", latehalf);
       output_asm_insn(\"mov %1, -(sp)\", operands);
       
       output_asm_insn(\"setl\", operands);
       output_asm_insn(\"{ldcld|movif} (sp)+, %0\", operands);
       output_asm_insn(\"seti\", operands);
       return \"\";
     }
     else if (which_alternative == 1)
       return \"setl\;{ldcld|movif} %1, %0\;seti\";
     else 
       return \"setl\;{ldcld|movif} %1, %0\;seti\";
  "
  [(set_attr "length" "5,3,4")])

(define_insn "floathidf2"
  [(set (match_operand:DF 0 "register_operand" "=a,a")
	(float:DF (match_operand:HI 1 "general_operand" "rR,Qi")))]
  "TARGET_FPU"
  "{ldcid|movif} %1, %0"
  [(set_attr "length" "1,2")])
	
;; cut float to int
(define_insn "fix_truncdfsi2"
  [(set (match_operand:SI 0 "general_operand" "=r,R,Q")
	(fix:SI (fix:DF (match_operand:DF 1 "register_operand" "a,a,a"))))]
  "TARGET_FPU"
  "* if (which_alternative ==0)
     {
       output_asm_insn(\"setl\", operands);
       output_asm_insn(\"{stcdl|movfi} %1, -(sp)\", operands);
       output_asm_insn(\"seti\", operands);
       output_asm_insn(\"mov (sp)+, %0\", operands);
       operands[0] = gen_rtx_REG (HImode, REGNO (operands[0]) + 1);
       output_asm_insn(\"mov (sp)+, %0\", operands);
       return \"\";
     }
     else if (which_alternative == 1)
       return \"setl\;{stcdl|movfi} %1, %0\;seti\";
     else 
       return \"setl\;{stcdl|movfi} %1, %0\;seti\";
  "
  [(set_attr "length" "5,3,4")])

(define_insn "fix_truncdfhi2"
  [(set (match_operand:HI 0 "general_operand" "=rR,Q")
	(fix:HI (fix:DF (match_operand:DF 1 "register_operand" "a,a"))))]
  "TARGET_FPU"
  "{stcdi|movfi} %1, %0"
  [(set_attr "length" "1,2")])


;;- arithmetic instructions
;;- add instructions

(define_insn "adddf3"
  [(set (match_operand:DF 0 "register_operand" "=a,a,a")
	(plus:DF (match_operand:DF 1 "register_operand" "%0,0,0")
		 (match_operand:DF 2 "general_operand" "fR,Q,F")))]
  "TARGET_FPU"
  "{addd|addf} %2, %0"
  [(set_attr "length" "1,2,5")])

(define_insn "addsi3"
  [(set (match_operand:SI 0 "general_operand" "=r,r,o,o,r,r,r,o,o,o")
	(plus:SI (match_operand:SI 1 "general_operand" "%0,0,0,0,0,0,0,0,0,0")
		 (match_operand:SI 2 "general_operand" "r,o,r,o,I,J,K,I,J,K")))]
  ""
  "*
{ /* Here we trust that operands don't overlap 

     or is lateoperands the low word?? - looks like it! */

  rtx lateoperands[3];
  
  lateoperands[0] = operands[0];

  if (REG_P (operands[0]))
    operands[0] = gen_rtx_REG (HImode, REGNO (operands[0]) + 1);
  else
    operands[0] = adjust_address (operands[0], HImode, 2);
  
  if (! CONSTANT_P(operands[2]))
  {
    lateoperands[2] = operands[2];

    if (REG_P (operands[2]))
      operands[2] = gen_rtx_REG (HImode, REGNO (operands[2]) + 1);
    else
      operands[2] = adjust_address (operands[2], HImode, 2);

    output_asm_insn (\"add %2, %0\", operands);
    output_asm_insn (\"adc %0\", lateoperands);
    output_asm_insn (\"add %2, %0\", lateoperands);
    return \"\";
  }

  lateoperands[2] = GEN_INT ((INTVAL (operands[2]) >> 16) & 0xffff);
  operands[2] = GEN_INT (INTVAL (operands[2]) & 0xffff);
  
  if (INTVAL(operands[2]))
  { 
    output_asm_insn (\"add %2, %0\", operands);
    output_asm_insn (\"adc %0\", lateoperands);
  }

  if (INTVAL(lateoperands[2]))
    output_asm_insn (\"add %2, %0\", lateoperands);

  return \"\";
}"
  [(set_attr "length" "3,5,6,8,3,1,5,5,3,8")])

(define_insn "addhi3"
  [(set (match_operand:HI 0 "general_operand" "=rR,rR,Q,Q")
	(plus:HI (match_operand:HI 1 "general_operand" "%0,0,0,0")
		 (match_operand:HI 2 "general_operand" "rRLM,Qi,rRLM,Qi")))]
  ""
  "*
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      if (INTVAL(operands[2]) == 1)
	return \"inc %0\";
      else if (INTVAL(operands[2]) == -1)
        return \"dec %0\";
    }

  return \"add %2, %0\";
}"
  [(set_attr "length" "1,2,2,3")])

(define_insn "addqi3"
  [(set (match_operand:QI 0 "general_operand" "=rR,rR,Q,Q")
	(plus:QI (match_operand:QI 1 "general_operand" "%0,0,0,0")
		 (match_operand:QI 2 "general_operand" "rRLM,Qi,rRLM,Qi")))]
  ""
  "*
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      if (INTVAL(operands[2]) == 1)
	return \"incb %0\";
      else if (INTVAL(operands[2]) == -1)
	return \"decb %0\";
    }

  return \"add %2, %0\";
}"
  [(set_attr "length" "1,2,2,3")])


;;- subtract instructions
;; we don't have to care for constant second 
;; args, since they are canonical plus:xx now!
;; also for minus:DF ??

(define_insn "subdf3"
  [(set (match_operand:DF 0 "register_operand" "=a,a")
	(minus:DF (match_operand:DF 1 "register_operand" "0,0")
		  (match_operand:DF 2 "general_operand" "fR,Q")))]
  "TARGET_FPU"
  "{subd|subf} %2, %0"
  [(set_attr "length" "1,2")])

(define_insn "subsi3"
  [(set (match_operand:SI 0 "general_operand" "=r,r,o,o")
        (minus:SI (match_operand:SI 1 "general_operand" "0,0,0,0")
                  (match_operand:SI 2 "general_operand" "r,o,r,o")))]
  ""
  "*
{ /* Here we trust that operands don't overlap 

     or is lateoperands the low word?? - looks like it! */

  rtx lateoperands[3];
  
  lateoperands[0] = operands[0];

  if (REG_P (operands[0]))
    operands[0] = gen_rtx_REG (HImode, REGNO (operands[0]) + 1);
  else
    operands[0] = adjust_address (operands[0], HImode, 2);
  
  lateoperands[2] = operands[2];

  if (REG_P (operands[2]))
    operands[2] = gen_rtx_REG (HImode, REGNO (operands[2]) + 1);
  else
    operands[2] = adjust_address (operands[2], HImode, 2);

  output_asm_insn (\"sub %2, %0\", operands);
  output_asm_insn (\"sbc %0\", lateoperands);
  output_asm_insn (\"sub %2, %0\", lateoperands);
  return \"\";
}"
;; offsettable memory addresses always are expensive!!!
  [(set_attr "length" "3,5,6,8")])

(define_insn "subhi3"
  [(set (match_operand:HI 0 "general_operand" "=rR,rR,Q,Q")
	(minus:HI (match_operand:HI 1 "general_operand" "0,0,0,0")
		  (match_operand:HI 2 "general_operand" "rR,Qi,rR,Qi")))]
  ""
  "*
{
  gcc_assert (GET_CODE (operands[2]) != CONST_INT);

  return \"sub %2, %0\";
}"
  [(set_attr "length" "1,2,2,3")])

(define_insn "subqi3"
  [(set (match_operand:QI 0 "general_operand" "=rR,rR,Q,Q")
	(minus:QI (match_operand:QI 1 "general_operand" "0,0,0,0")
		  (match_operand:QI 2 "general_operand" "rR,Qi,rR,Qi")))]
  ""
  "*
{
  gcc_assert (GET_CODE (operands[2]) != CONST_INT);

  return \"sub %2, %0\";
}"
  [(set_attr "length" "1,2,2,3")])

;;;;- and instructions
;; Bit-and on the pdp (like on the VAX) is done with a clear-bits insn.

(define_insn "andsi3"
  [(set (match_operand:SI 0 "general_operand" "=r,r,o,o,r,r,r,o,o,o")
        (and:SI (match_operand:SI 1 "general_operand" "%0,0,0,0,0,0,0,0,0,0")
                (not:SI (match_operand:SI 2 "general_operand" "r,o,r,o,I,J,K,I,J,K"))))]
  ""
  "*
{ /* Here we trust that operands don't overlap 

     or is lateoperands the low word?? - looks like it! */

  rtx lateoperands[3];
  
  lateoperands[0] = operands[0];

  if (REG_P (operands[0]))
    operands[0] = gen_rtx_REG (HImode, REGNO (operands[0]) + 1);
  else
    operands[0] = adjust_address (operands[0], HImode, 2);
  
  if (! CONSTANT_P(operands[2]))
  {
    lateoperands[2] = operands[2];

    if (REG_P (operands[2]))
      operands[2] = gen_rtx_REG (HImode, REGNO (operands[2]) + 1);
    else
      operands[2] = adjust_address (operands[2], HImode, 2);

    output_asm_insn (\"bic %2, %0\", operands);
    output_asm_insn (\"bic %2, %0\", lateoperands);
    return \"\";
  }

  lateoperands[2] = GEN_INT ((INTVAL (operands[2]) >> 16) & 0xffff);
  operands[2] = GEN_INT (INTVAL (operands[2]) & 0xffff);
  
  /* these have different lengths, so we should have 
     different constraints! */
  if (INTVAL(operands[2]))
    output_asm_insn (\"bic %2, %0\", operands);

  if (INTVAL(lateoperands[2]))
    output_asm_insn (\"bic %2, %0\", lateoperands);

  return \"\";
}"
  [(set_attr "length" "2,4,4,6,2,2,4,3,3,6")])

(define_insn "andhi3"
  [(set (match_operand:HI 0 "general_operand" "=rR,rR,Q,Q")
	(and:HI (match_operand:HI 1 "general_operand" "0,0,0,0")
		(not:HI (match_operand:HI 2 "general_operand" "rR,Qi,rR,Qi"))))]
  ""
  "bic %2, %0"
  [(set_attr "length" "1,2,2,3")])

(define_insn "andqi3"
  [(set (match_operand:QI 0 "general_operand" "=rR,rR,Q,Q")
	(and:QI (match_operand:QI 1 "general_operand" "0,0,0,0")
		(not:QI (match_operand:QI 2 "general_operand" "rR,Qi,rR,Qi"))))]
  ""
  "bicb %2, %0"
  [(set_attr "length" "1,2,2,3")])

;;- Bit set (inclusive or) instructions
(define_insn "iorsi3"
  [(set (match_operand:SI 0 "general_operand" "=r,r,o,o,r,r,r,o,o,o")
        (ior:SI (match_operand:SI 1 "general_operand" "%0,0,0,0,0,0,0,0,0,0")
                  (match_operand:SI 2 "general_operand" "r,o,r,o,I,J,K,I,J,K")))]
  ""
  "*
{ /* Here we trust that operands don't overlap 

     or is lateoperands the low word?? - looks like it! */

  rtx lateoperands[3];
  
  lateoperands[0] = operands[0];

  if (REG_P (operands[0]))
    operands[0] = gen_rtx_REG (HImode, REGNO (operands[0]) + 1);
  else
    operands[0] = adjust_address (operands[0], HImode, 2);
  
  if (! CONSTANT_P(operands[2]))
    {
      lateoperands[2] = operands[2];

      if (REG_P (operands[2]))
	operands[2] = gen_rtx_REG (HImode, REGNO (operands[2]) + 1);
      else
	operands[2] = adjust_address (operands[2], HImode, 2);

      output_asm_insn (\"bis %2, %0\", operands);
      output_asm_insn (\"bis %2, %0\", lateoperands);
      return \"\";
    }

  lateoperands[2] = GEN_INT ((INTVAL (operands[2]) >> 16) & 0xffff);
  operands[2] = GEN_INT (INTVAL (operands[2]) & 0xffff);
  
  /* these have different lengths, so we should have 
     different constraints! */
  if (INTVAL(operands[2]))
    output_asm_insn (\"bis %2, %0\", operands);

  if (INTVAL(lateoperands[2]))
    output_asm_insn (\"bis %2, %0\", lateoperands);

  return \"\";
}"
  [(set_attr "length" "2,4,4,6,2,2,4,3,3,6")])

(define_insn "iorhi3"
  [(set (match_operand:HI 0 "general_operand" "=rR,rR,Q,Q")
	(ior:HI (match_operand:HI 1 "general_operand" "%0,0,0,0")
		(match_operand:HI 2 "general_operand" "rR,Qi,rR,Qi")))]
  ""
  "bis %2, %0"
  [(set_attr "length" "1,2,2,3")])

(define_insn "iorqi3"
  [(set (match_operand:QI 0 "general_operand" "=rR,rR,Q,Q")
	(ior:QI (match_operand:QI 1 "general_operand" "%0,0,0,0")
		(match_operand:QI 2 "general_operand" "rR,Qi,rR,Qi")))]
  ""
  "bisb %2, %0")

;;- xor instructions
(define_insn "xorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (xor:SI (match_operand:SI 1 "register_operand" "%0")
                (match_operand:SI 2 "arith_operand" "r")))]
  "TARGET_40_PLUS"
  "*
{ /* Here we trust that operands don't overlap */

  rtx lateoperands[3];

  lateoperands[0] = operands[0];
  operands[0] = gen_rtx_REG (HImode, REGNO (operands[0]) + 1);

  if (REG_P(operands[2]))
    {
      lateoperands[2] = operands[2];
      operands[2] = gen_rtx_REG (HImode, REGNO (operands[2]) + 1);

      output_asm_insn (\"xor %2, %0\", operands);
      output_asm_insn (\"xor %2, %0\", lateoperands);

      return \"\";
    }

}"
  [(set_attr "length" "2")])

(define_insn "xorhi3"
  [(set (match_operand:HI 0 "general_operand" "=rR,Q")
	(xor:HI (match_operand:HI 1 "general_operand" "%0,0")
		(match_operand:HI 2 "register_operand" "r,r")))]
  "TARGET_40_PLUS"
  "xor %2, %0"
  [(set_attr "length" "1,2")])

;;- one complement instructions

(define_insn "one_cmplhi2"
  [(set (match_operand:HI 0 "general_operand" "=rR,Q")
        (not:HI (match_operand:HI 1 "general_operand" "0,0")))]
  ""
  "com %0"
  [(set_attr "length" "1,2")])

(define_insn "one_cmplqi2"
  [(set (match_operand:QI 0 "general_operand" "=rR,rR")
        (not:QI (match_operand:QI 1 "general_operand" "0,g")))]
  ""
  "@
  comb %0
  movb %1, %0\; comb %0"
  [(set_attr "length" "1,2")])

;;- arithmetic shift instructions
(define_insn "ashlsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(ashift:SI (match_operand:SI 1 "register_operand" "0,0")
		   (match_operand:HI 2 "general_operand" "rR,Qi")))]
  "TARGET_45"
  "ashc %2,%0"
  [(set_attr "length" "1,2")])

;; Arithmetic right shift on the pdp works by negating the shift count.
(define_expand "ashrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ashift:SI (match_operand:SI 1 "register_operand" "0")
		   (match_operand:HI 2 "general_operand" "g")))]
  ""
  "
{
  operands[2] = negate_rtx (HImode, operands[2]);
}")

;; define asl aslb asr asrb - ashc missing!

;; asl 
(define_insn "" 
  [(set (match_operand:HI 0 "general_operand" "=rR,Q")
	(ashift:HI (match_operand:HI 1 "general_operand" "0,0")
		   (const_int 1)))]
  ""
  "asl %0"
  [(set_attr "length" "1,2")])

;; and another possibility for asr is << -1
;; might cause problems since -1 can also be encoded as 65535!
;; not in gcc2 ??? 

;; asr
(define_insn "" 
  [(set (match_operand:HI 0 "general_operand" "=rR,Q")
	(ashift:HI (match_operand:HI 1 "general_operand" "0,0")
		   (const_int -1)))]
  ""
  "asr %0"
  [(set_attr "length" "1,2")])

;; lsr
(define_insn "" 
  [(set (match_operand:HI 0 "general_operand" "=rR,Q")
	(lshiftrt:HI (match_operand:HI 1 "general_operand" "0,0")
		   (const_int 1)))]
  ""
  "clc\;ror %0"
  [(set_attr "length" "1,2")])

(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lshiftrt:SI (match_operand:SI 1 "general_operand" "0")
                   (const_int 1)))]
  ""
{

  rtx lateoperands[2];

  lateoperands[0] = operands[0];
  operands[0] = gen_rtx_REG (HImode, REGNO (operands[0]) + 1);

  lateoperands[1] = operands[1];
  operands[1] = gen_rtx_REG (HImode, REGNO (operands[1]) + 1);

  output_asm_insn (\"clc\", operands);
  output_asm_insn (\"ror %0\", lateoperands);
  output_asm_insn (\"ror %0\", operands);

  return \"\";
}
  [(set_attr "length" "5")])

;; shift is by arbitrary count is expensive, 
;; shift by one cheap - so let's do that, if
;; space doesn't matter
(define_insn "" 
  [(set (match_operand:HI 0 "general_operand" "=r")
	(ashift:HI (match_operand:HI 1 "general_operand" "0")
		   (match_operand:HI 2 "expand_shift_operand" "O")))]
  "! optimize_size"
  "*
{
  register int i;

  for (i = 1; i <= abs(INTVAL(operands[2])); i++)
    if (INTVAL(operands[2]) < 0)
      output_asm_insn(\"asr %0\", operands);
    else
      output_asm_insn(\"asl %0\", operands);
      
  return \"\";
}"
;; longest is 4
  [(set (attr "length") (const_int 4))])

;; aslb
(define_insn "" 
  [(set (match_operand:QI 0 "general_operand" "=r,o")
	(ashift:QI (match_operand:QI 1 "general_operand" "0,0")
		   (match_operand:HI 2 "const_immediate_operand" "n,n")))]
  ""
  "*
{ /* allowing predec or post_inc is possible, but hairy! */
  int i, cnt;

  cnt = INTVAL(operands[2]) & 0x0007;

  for (i=0 ; i < cnt ; i++)
       output_asm_insn(\"aslb %0\", operands);

  return \"\";
}"
;; set attribute length ( match_dup 2 & 7 ) *(1 or 2) !!!
  [(set_attr_alternative "length" 
                         [(const_int 7)
                          (const_int 14)])])

;;; asr 
;(define_insn "" 
;  [(set (match_operand:HI 0 "general_operand" "=rR,Q")
;	(ashiftrt:HI (match_operand:HI 1 "general_operand" "0,0")
;		     (const_int 1)))]
;  ""
;  "asr %0"
;  [(set_attr "length" "1,2")])

;; asrb
(define_insn "" 
  [(set (match_operand:QI 0 "general_operand" "=r,o")
	(ashiftrt:QI (match_operand:QI 1 "general_operand" "0,0")
		     (match_operand:HI 2 "const_immediate_operand" "n,n")))]
  ""
  "*
{ /* allowing predec or post_inc is possible, but hairy! */
  int i, cnt;

  cnt = INTVAL(operands[2]) & 0x0007;

  for (i=0 ; i < cnt ; i++)
       output_asm_insn(\"asrb %0\", operands);

  return \"\";
}"
  [(set_attr_alternative "length" 
                         [(const_int 7)
                          (const_int 14)])])

;; the following is invalid - too complex!!! - just say 14 !!!
;  [(set (attr "length") (plus (and (match_dup 2)
;                                   (const_int 7))
;                              (and (match_dup 2)
;                                   (const_int 7))))])



;; can we get +-1 in the next pattern? should 
;; have been caught by previous patterns!

(define_insn "ashlhi3"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(ashift:HI (match_operand:HI 1 "register_operand" "0,0")
		   (match_operand:HI 2 "general_operand" "rR,Qi")))]
  ""
  "*
{
  if (GET_CODE(operands[2]) == CONST_INT)
    {
      if (INTVAL(operands[2]) == 1)
	return \"asl %0\";
      else if (INTVAL(operands[2]) == -1)
	return \"asr %0\";
    }

  return \"ash %2,%0\";
}"
  [(set_attr "length" "1,2")])

;; Arithmetic right shift on the pdp works by negating the shift count.
(define_expand "ashrhi3"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(ashift:HI (match_operand:HI 1 "register_operand" "0")
		   (match_operand:HI 2 "general_operand" "g")))]
  ""
  "
{
  operands[2] = negate_rtx (HImode, operands[2]);
}")

;;;;- logical shift instructions
;;(define_insn "lshrsi3"
;;  [(set (match_operand:HI 0 "register_operand" "=r")
;;	(lshiftrt:HI (match_operand:HI 1 "register_operand" "0")
;;		     (match_operand:HI 2 "arith_operand" "rI")))]
;;  ""
;;  "srl %0,%2")

;; absolute 

(define_insn "absdf2"
  [(set (match_operand:DF 0 "general_operand" "=fR,Q")
	(abs:DF (match_operand:DF 1 "general_operand" "0,0")))]
  "TARGET_FPU"
  "{absd|absf} %0"
  [(set_attr "length" "1,2")])

(define_insn "abshi2"
  [(set (match_operand:HI 0 "general_operand" "=r,o")
	(abs:HI (match_operand:HI 1 "general_operand" "0,0")))]
  "TARGET_ABSHI_BUILTIN"
  "*
{
  static int count = 0;
  char buf[200];
	
  output_asm_insn(\"tst %0\", operands);
  sprintf(buf, \"bge abshi%d\", count);
  output_asm_insn(buf, NULL);
  output_asm_insn(\"neg %0\", operands);
  sprintf(buf, \"\\nabshi%d:\", count++);
  output_asm_insn(buf, NULL);

  return \"\";
}"
  [(set_attr "length" "3,5")])


;; define expand abshi - is much better !!! - but
;; will it be optimized into an abshi2 ?
;; it will leave better code, because the tsthi might be 
;; optimized away!!
; -- just a thought - don't have time to check 
;
;(define_expand "abshi2"
;  [(match_operand:HI 0 "general_operand" "")
;   (match_operand:HI 1 "general_operand" "")]
;  ""
;  "
;{
;  rtx label = gen_label_rtx ();
;
;  /* do I need this? */
;  do_pending_stack_adjust ();
;
;  emit_move_insn (operands[0], operands[1]);
;
;  emit_insn (gen_tsthi (operands[0]));
;  emit_insn (gen_bge (label1));
;
;  emit_insn (gen_neghi(operands[0], operands[0])
;  
;  emit_barrier ();
;
;  emit_label (label);
;
;  /* allow REG_NOTES to be set on last insn (labels don't have enough
;     fields, and can't be used for REG_NOTES anyway).  */
;  emit_insn (gen_rtx_USE (VOIDmode, stack_pointer_rtx));
;  DONE;
;}")

;; negate insns

(define_insn "negdf2"
  [(set (match_operand:DF 0 "general_operand" "=fR,Q")
	(neg:DF (match_operand:DF 1 "register_operand" "0,0")))]
  "TARGET_FPU"
  "{negd|negf} %0"
  [(set_attr "length" "1,2")])

(define_insn "negsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (match_operand:SI 1 "general_operand" "0")))]
  ""
{

  rtx lateoperands[2];

  lateoperands[0] = operands[0];
  operands[0] = gen_rtx_REG (HImode, REGNO (operands[0]) + 1);

  lateoperands[1] = operands[1];
  operands[1] = gen_rtx_REG (HImode, REGNO (operands[1]) + 1);

  output_asm_insn (\"com %0\", operands);
  output_asm_insn (\"com %0\", lateoperands);
  output_asm_insn (\"inc %0\", operands);
  output_asm_insn (\"adc %0\", lateoperands);

  return \"\";
}
  [(set_attr "length" "5")])

(define_insn "neghi2"
  [(set (match_operand:HI 0 "general_operand" "=rR,Q")
	(neg:HI (match_operand:HI 1 "general_operand" "0,0")))]
  ""
  "neg %0"
  [(set_attr "length" "1,2")])

(define_insn "negqi2"
  [(set (match_operand:QI 0 "general_operand" "=rR,Q")
	(neg:QI (match_operand:QI 1 "general_operand" "0,0")))]
  ""
  "negb %0"
  [(set_attr "length" "1,2")])


;; Unconditional and other jump instructions
(define_insn "jump"
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  ""
  "jmp %l0"
  [(set_attr "length" "2")])

(define_insn ""
  [(set (pc)
    (label_ref (match_operand 0 "" "")))
   (clobber (const_int 1))]
  ""
  "jmp %l0"
  [(set_attr "length" "2")])

(define_insn "tablejump"
  [(set (pc) (match_operand:HI 0 "general_operand" "rR,Q"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
  "jmp %0"
  [(set_attr "length" "1,2")])

;; indirect jump - let's be conservative!
;; allow only register_operand, even though we could also 
;; allow labels etc.

(define_insn "indirect_jump"
  [(set (pc) (match_operand:HI 0 "register_operand" "r"))]
  ""
  "jmp (%0)")

;;- jump to subroutine

(define_insn "call"
  [(call (match_operand:HI 0 "general_operand" "rR,Q")
	 (match_operand:HI 1 "general_operand" "g,g"))
;;   (use (reg:HI 0)) what was that ???
  ]
  ;;- Don't use operand 1 for most machines.
  ""
  "jsr pc, %0"
  [(set_attr "length" "1,2")])

;;- jump to subroutine
(define_insn "call_value"
  [(set (match_operand 0 "" "")
	(call (match_operand:HI 1 "general_operand" "rR,Q")
	      (match_operand:HI 2 "general_operand" "g,g")))
;;   (use (reg:HI 0)) - what was that ????
  ]
  ;;- Don't use operand 2 for most machines.
  ""
  "jsr pc, %1"
  [(set_attr "length" "1,2")])

;;- nop instruction
(define_insn "nop"
  [(const_int 0)]
  ""
  "nop")


;;- multiply 

(define_insn "muldf3"
  [(set (match_operand:DF 0 "register_operand" "=a,a,a")
	(mult:DF (match_operand:DF 1 "register_operand" "%0,0,0")
		 (match_operand:DF 2 "general_operand" "fR,Q,F")))]
  "TARGET_FPU"
  "{muld|mulf} %2, %0"
  [(set_attr "length" "1,2,5")])

;; 16 bit result multiply:
;; currently we multiply only into odd registers, so we don't use two 
;; registers - but this is a bit inefficient at times. If we define 
;; a register class for each register, then we can specify properly 
;; which register need which scratch register ....

(define_insn "mulhi3"
  [(set (match_operand:HI 0 "register_operand" "=d,d") ; multiply regs
	(mult:HI (match_operand:HI 1 "register_operand" "%0,0")
		 (match_operand:HI 2 "general_operand" "rR,Qi")))]
  "TARGET_45"
  "mul %2, %0"
  [(set_attr "length" "1,2")])

;; 32 bit result
(define_expand "mulhisi3"
  [(set (match_dup 3)
	(match_operand:HI 1 "general_operand" "g,g"))
   (set (match_operand:SI 0 "register_operand" "=r,r") ; even numbered!
	(mult:SI (truncate:HI 
                  (match_dup 0))
		 (match_operand:HI 2 "general_operand" "rR,Qi")))]
  "TARGET_45"
  "operands[3] = gen_lowpart(HImode, operands[1]);")

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r,r") ; even numbered!
	(mult:SI (truncate:HI 
                  (match_operand:SI 1 "register_operand" "%0,0"))
		 (match_operand:HI 2 "general_operand" "rR,Qi")))]
  "TARGET_45"
  "mul %2, %0"
  [(set_attr "length" "1,2")])

;(define_insn "mulhisi3"
;  [(set (match_operand:SI 0 "register_operand" "=r,r") ; even numbered!
;	(mult:SI (truncate:HI 
;                  (match_operand:SI 1 "register_operand" "%0,0"))
;		 (match_operand:HI 2 "general_operand" "rR,Qi")))]
;  "TARGET_45"
;  "mul %2, %0"
;  [(set_attr "length" "1,2")])

;;- divide
(define_insn "divdf3"
  [(set (match_operand:DF 0 "register_operand" "=a,a,a")
	(div:DF (match_operand:DF 1 "register_operand" "0,0,0")
		(match_operand:DF 2 "general_operand" "fR,Q,F")))]
  "TARGET_FPU"
  "{divd|divf} %2, %0"
  [(set_attr "length" "1,2,5")])

	 
(define_expand "divhi3"
  [(set (subreg:HI (match_dup 1) 0)
	(div:HI (match_operand:SI 1 "general_operand" "0")
		(match_operand:HI 2 "general_operand" "g")))
   (set (match_operand:HI 0 "general_operand" "=r")
        (subreg:HI (match_dup 1) 0))]
  "TARGET_45"
  "")

(define_insn ""
  [(set (subreg:HI (match_operand:SI 0 "general_operand" "=r") 0)
	(div:HI (match_operand:SI 1 "general_operand" "0")
		(match_operand:HI 2 "general_operand" "g")))]
  "TARGET_45"
  "div %2,%0"
  [(set_attr "length" "2")])

(define_expand "modhi3"
  [(set (subreg:HI (match_dup 1) 2)
	(mod:HI (match_operand:SI 1 "general_operand" "0")
		(match_operand:HI 2 "general_operand" "g")))
   (set (match_operand:HI 0 "general_operand" "=r")
        (subreg:HI (match_dup 1) 2))]
  "TARGET_45"
  "")

(define_insn ""
  [(set (subreg:HI (match_operand:SI 0 "general_operand" "=r") 2)
	(mod:HI (match_operand:SI 1 "general_operand" "0")
		(match_operand:HI 2 "general_operand" "g")))]
  "TARGET_45"
  "div %2,%0"
  [(set_attr "length" "2")])

;(define_expand "divmodhi4"
;  [(parallel [(set (subreg:HI (match_dup 1) 0)
;	           (div:HI (match_operand:SI 1 "general_operand" "0")
;		           (match_operand:HI 2 "general_operand" "g")))
;              (set (subreg:HI (match_dup 1) 2)
;	           (mod:HI (match_dup 1)
;		           (match_dup 2)))])
;   (set (match_operand:HI 3 "general_operand" "=r")
;        (subreg:HI (match_dup 1) 2))
;   (set (match_operand:HI 0 "general_operand" "=r")
;        (subreg:HI (match_dup 1) 0))]
;  "TARGET_45"
;  "")
;
;(define_insn ""
;  [(set (subreg:HI (match_operand:SI 0 "general_operand" "=r") 0)
;	           (div:HI (match_operand:SI 1 "general_operand" "0")
;		           (match_operand:HI 2 "general_operand" "g")))
;   (set (subreg:HI (match_dup 0) 2)
;	           (mod:HI (match_dup 1)
;		           (match_dup 2)))]
;  "TARGET_45"
;  "div %2, %0")
;
   
;; is rotate doing the right thing to be included here ????

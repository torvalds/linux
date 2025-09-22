;;- Machine description for Blackfin for GNU compiler
;;  Copyright 2005, 2006  Free Software Foundation, Inc.
;;  Contributed by Analog Devices.

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 2, or (at your
;; option) any later version.

;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

; operand punctuation marks:
;
;     X -- integer value printed as log2
;     Y -- integer value printed as log2(~value) - for bitclear
;     h -- print half word register, low part
;     d -- print half word register, high part
;     D -- print operand as dregs pairs
;     w -- print operand as accumulator register word (a0w, a1w)
;     H -- high part of double mode operand
;     T -- byte register representation Oct. 02 2001

; constant operand classes
;
;     J   2**N       5bit imm scaled
;     Ks7 -64 .. 63  signed 7bit imm
;     Ku5 0..31      unsigned 5bit imm
;     Ks4 -8 .. 7    signed 4bit imm
;     Ks3 -4 .. 3    signed 3bit imm
;     Ku3 0 .. 7     unsigned 3bit imm
;     Pn  0, 1, 2    constants 0, 1 or 2, corresponding to n
;
; register operands
;     d  (r0..r7)
;     a  (p0..p5,fp,sp)
;     e  (a0, a1)
;     b  (i0..i3)
;     f  (m0..m3)
;     v  (b0..b3)
;     c  (i0..i3,m0..m3) CIRCREGS
;     C  (CC)            CCREGS
;     t  (lt0,lt1)
;     k  (lc0,lc1)
;     u  (lb0,lb1)
;

;; Define constants for hard registers.

(define_constants
  [(REG_R0 0)
   (REG_R1 1)
   (REG_R2 2)
   (REG_R3 3)
   (REG_R4 4)
   (REG_R5 5)
   (REG_R6 6)
   (REG_R7 7)

   (REG_P0 8)
   (REG_P1 9)
   (REG_P2 10)
   (REG_P3 11)
   (REG_P4 12)
   (REG_P5 13)
   (REG_P6 14)
   (REG_P7 15)

   (REG_SP 14)
   (REG_FP 15)

   (REG_I0 16)
   (REG_I1 17)
   (REG_I2 18)
   (REG_I3 19)

   (REG_B0 20)
   (REG_B1 21)
   (REG_B2 22)
   (REG_B3 23)

   (REG_L0 24)
   (REG_L1 25)
   (REG_L2 26)
   (REG_L3 27)

   (REG_M0 28)
   (REG_M1 29)
   (REG_M2 30)
   (REG_M3 31)

   (REG_A0 32)
   (REG_A1 33)

   (REG_CC 34)
   (REG_RETS 35)
   (REG_RETI 36)
   (REG_RETX 37)
   (REG_RETN 38)
   (REG_RETE 39)

   (REG_ASTAT 40)
   (REG_SEQSTAT 41)
   (REG_USP 42)

   (REG_ARGP 43)

   (REG_LT0 44)
   (REG_LT1 45)
   (REG_LC0 46)
   (REG_LC1 47)
   (REG_LB0 48)
   (REG_LB1 49)])

;; Constants used in UNSPECs and UNSPEC_VOLATILEs.

(define_constants
  [(UNSPEC_CBRANCH_TAKEN 0)
   (UNSPEC_CBRANCH_NOPS 1)
   (UNSPEC_RETURN 2)
   (UNSPEC_MOVE_PIC 3)
   (UNSPEC_LIBRARY_OFFSET 4)
   (UNSPEC_PUSH_MULTIPLE 5)
   ;; Multiply or MAC with extra CONST_INT operand specifying the macflag
   (UNSPEC_MUL_WITH_FLAG 6)
   (UNSPEC_MAC_WITH_FLAG 7)
   (UNSPEC_MOVE_FDPIC 8)
   (UNSPEC_FUNCDESC_GOT17M4 9)
   (UNSPEC_LSETUP_END 10)])

(define_constants
  [(UNSPEC_VOLATILE_EH_RETURN 0)
   (UNSPEC_VOLATILE_CSYNC 1)
   (UNSPEC_VOLATILE_SSYNC 2)
   (UNSPEC_VOLATILE_LOAD_FUNCDESC 3)])

(define_constants
  [(MACFLAG_NONE 0)
   (MACFLAG_T 1)
   (MACFLAG_FU 2)
   (MACFLAG_TFU 3)
   (MACFLAG_IS 4)
   (MACFLAG_IU 5)
   (MACFLAG_W32 6)
   (MACFLAG_M 7)
   (MACFLAG_S2RND 8)
   (MACFLAG_ISS2 9)
   (MACFLAG_IH 10)])

(define_attr "type"
  "move,mvi,mcld,mcst,dsp32,mult,alu0,shft,brcc,br,call,misc,sync,compare,dummy"
  (const_string "misc"))

;; Scheduling definitions

(define_automaton "bfin")

(define_cpu_unit "core" "bfin")

(define_insn_reservation "alu" 1
  (eq_attr "type" "move,mvi,mcst,dsp32,alu0,shft,brcc,br,call,misc,sync,compare")
  "core")

(define_insn_reservation "imul" 3
  (eq_attr "type" "mult")
  "core*3")

(define_insn_reservation "load" 1
  (eq_attr "type" "mcld")
  "core")

;; Make sure genautomata knows about the maximum latency that can be produced
;; by the adjust_cost function.
(define_insn_reservation "dummy" 5
  (eq_attr "type" "mcld")
  "core")

;; Operand and operator predicates

(include "predicates.md")


;;; FRIO branches have been optimized for code density
;;; this comes at a slight cost of complexity when
;;; a compiler needs to generate branches in the general
;;; case.  In order to generate the correct branching
;;; mechanisms the compiler needs keep track of instruction
;;; lengths.  The follow table describes how to count instructions
;;; for the FRIO architecture.
;;;
;;; unconditional br are 12-bit imm pcrelative branches *2
;;; conditional   br are 10-bit imm pcrelative branches *2
;;; brcc 10-bit:
;;;   1024 10-bit imm *2 is 2048 (-1024..1022)
;;; br 12-bit  :
;;;   4096 12-bit imm *2 is 8192 (-4096..4094)
;;; NOTE : For brcc we generate instructions such as
;;;   if cc jmp; jump.[sl] offset
;;;   offset of jump.[sl] is from the jump instruction but
;;;     gcc calculates length from the if cc jmp instruction
;;;     furthermore gcc takes the end address of the branch instruction
;;;     as (pc) for a forward branch
;;;     hence our range is (-4094, 4092) instead of (-4096, 4094) for a br
;;;
;;; The way the (pc) rtx works in these calculations is somewhat odd;
;;; for backward branches it's the address of the current instruction,
;;; for forward branches it's the previously known address of the following
;;; instruction - we have to take this into account by reducing the range
;;; for a forward branch.

;; Lengths for type "mvi" insns are always defined by the instructions
;; themselves.
(define_attr "length" ""
  (cond [(eq_attr "type" "mcld")
         (if_then_else (match_operand 1 "effective_address_32bit_p" "")
                       (const_int 4) (const_int 2))

	 (eq_attr "type" "mcst")
	 (if_then_else (match_operand 0 "effective_address_32bit_p" "")
		       (const_int 4) (const_int 2))

	 (eq_attr "type" "move") (const_int 2)

	 (eq_attr "type" "dsp32") (const_int 4)
	 (eq_attr "type" "call")  (const_int 4)

         (eq_attr "type" "br")
  	 (if_then_else (and
	                  (le (minus (match_dup 0) (pc)) (const_int 4092))
	                  (ge (minus (match_dup 0) (pc)) (const_int -4096)))
        	  (const_int 2)
                  (const_int 4))

         (eq_attr "type" "brcc")
	 (cond [(and
	            (le (minus (match_dup 3) (pc)) (const_int 1020))
	            (ge (minus (match_dup 3) (pc)) (const_int -1024)))
		  (const_int 2)
		(and
	            (le (minus (match_dup 3) (pc)) (const_int 4092))
	            (ge (minus (match_dup 3) (pc)) (const_int -4094)))
		  (const_int 4)]
	       (const_int 6))
        ]

	(const_int 2)))


;; Classify the insns into those that are one instruction and those that
;; are more than one in sequence.
(define_attr "seq_insns" "single,multi"
  (const_string "single"))

;; Conditional moves

(define_expand "movsicc"
  [(set (match_operand:SI 0 "register_operand" "")
        (if_then_else:SI (match_operand 1 "comparison_operator" "")
                         (match_operand:SI 2 "register_operand" "")
                         (match_operand:SI 3 "register_operand" "")))]
  ""
{
  operands[1] = bfin_gen_compare (operands[1], SImode);
})

(define_insn "*movsicc_insn1"
  [(set (match_operand:SI 0 "register_operand" "=da,da,da")
        (if_then_else:SI
	    (eq:BI (match_operand:BI 3 "register_operand" "C,C,C")
		(const_int 0))
	    (match_operand:SI 1 "register_operand" "da,0,da")
	    (match_operand:SI 2 "register_operand" "0,da,da")))]
  ""
  "@
    if !cc %0 =%1; /* movsicc-1a */
    if cc %0 =%2; /* movsicc-1b */
    if !cc %0 =%1; if cc %0=%2; /* movsicc-1 */"
  [(set_attr "length" "2,2,4")
   (set_attr "type" "move")
   (set_attr "seq_insns" "*,*,multi")])

(define_insn "*movsicc_insn2"
  [(set (match_operand:SI 0 "register_operand" "=da,da,da")
        (if_then_else:SI
	    (ne:BI (match_operand:BI 3 "register_operand" "C,C,C")
		(const_int 0))
	    (match_operand:SI 1 "register_operand" "0,da,da")
	    (match_operand:SI 2 "register_operand" "da,0,da")))]
  ""
  "@
   if !cc %0 =%2; /* movsicc-2b */
   if cc %0 =%1; /* movsicc-2a */
   if cc %0 =%1; if !cc %0=%2; /* movsicc-1 */"
  [(set_attr "length" "2,2,4")
   (set_attr "type" "move")
   (set_attr "seq_insns" "*,*,multi")])

;; Insns to load HIGH and LO_SUM

(define_insn "movsi_high"
  [(set (match_operand:SI 0 "register_operand" "=x")
	(high:SI (match_operand:SI 1 "immediate_operand" "i")))]
  "reload_completed"
  "%d0 = %d1;"
  [(set_attr "type" "mvi")
   (set_attr "length" "4")])

(define_insn "movstricthi_high"
  [(set (match_operand:SI 0 "register_operand" "+x")
	(ior:SI (and:SI (match_dup 0) (const_int 65535))
		(match_operand:SI 1 "immediate_operand" "i")))]
  "reload_completed"
  "%d0 = %d1;"
  [(set_attr "type" "mvi")
   (set_attr "length" "4")])

(define_insn "movsi_low"
  [(set (match_operand:SI 0 "register_operand" "=x")
	(lo_sum:SI (match_operand:SI 1 "register_operand" "0")
		   (match_operand:SI 2 "immediate_operand" "i")))]
  "reload_completed"
  "%h0 = %h2;"
  [(set_attr "type" "mvi")
   (set_attr "length" "4")])

(define_insn "movsi_high_pic"
  [(set (match_operand:SI 0 "register_operand" "=x")
	(high:SI (unspec:SI [(match_operand:SI 1 "" "")]
			    UNSPEC_MOVE_PIC)))]
  ""
  "%d0 = %1@GOT_LOW;"
  [(set_attr "type" "mvi")
   (set_attr "length" "4")])

(define_insn "movsi_low_pic"
  [(set (match_operand:SI 0 "register_operand" "=x")
	(lo_sum:SI (match_operand:SI 1 "register_operand" "0")
		   (unspec:SI [(match_operand:SI 2 "" "")]
			      UNSPEC_MOVE_PIC)))]
  ""
  "%h0 = %h2@GOT_HIGH;"
  [(set_attr "type" "mvi")
   (set_attr "length" "4")])

;;; Move instructions

(define_insn_and_split "movdi_insn"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=x,mx,r")
	(match_operand:DI 1 "general_operand" "iFx,r,mx"))]
  "GET_CODE (operands[0]) != MEM || GET_CODE (operands[1]) != MEM"
  "#"
  "reload_completed"
  [(set (match_dup 2) (match_dup 3))
   (set (match_dup 4) (match_dup 5))]
{
  rtx lo_half[2], hi_half[2];
  split_di (operands, 2, lo_half, hi_half);

  if (reg_overlap_mentioned_p (lo_half[0], hi_half[1]))
    {
      operands[2] = hi_half[0];
      operands[3] = hi_half[1];
      operands[4] = lo_half[0];
      operands[5] = lo_half[1];
    }
  else
    {
      operands[2] = lo_half[0];
      operands[3] = lo_half[1];
      operands[4] = hi_half[0];
      operands[5] = hi_half[1];
    }
})

(define_insn "movbi"
  [(set (match_operand:BI 0 "nonimmediate_operand" "=x,x,d,md,C,d,C")
        (match_operand:BI 1 "general_operand" "x,xKs3,md,d,d,C,P0"))]

  ""
  "@
   %0 = %1;
   %0 = %1 (X);
   %0 = B %1 (Z);
   B %0 = %1;
   CC = %1;
   %0 = CC;
   R0 = R0 | R0; CC = AC0;"
  [(set_attr "type" "move,mvi,mcld,mcst,compare,compare,alu0")
   (set_attr "length" "2,2,*,*,2,2,4")
   (set_attr "seq_insns" "*,*,*,*,*,*,multi")])

(define_insn "movpdi"
  [(set (match_operand:PDI 0 "nonimmediate_operand" "=e,<,e")
        (match_operand:PDI 1 "general_operand" " e,e,>"))]
  ""
  "@
   %0 = %1;
   %0 = %x1; %0 = %w1;
   %w0 = %1; %x0 = %1;"
  [(set_attr "type" "move,mcst,mcld")
   (set_attr "seq_insns" "*,multi,multi")])

(define_insn "load_accumulator"
  [(set (match_operand:PDI 0 "register_operand" "=e")
        (sign_extend:PDI (match_operand:SI 1 "register_operand" "d")))]
  ""
  "%0 = %1;"
  [(set_attr "type" "move")])

(define_insn_and_split "load_accumulator_pair"
  [(set (match_operand:V2PDI 0 "register_operand" "=e")
        (sign_extend:V2PDI (vec_concat:V2SI
			    (match_operand:SI 1 "register_operand" "d")
			    (match_operand:SI 2 "register_operand" "d"))))]
  ""
  "#"
  "reload_completed"
  [(set (match_dup 3) (sign_extend:PDI (match_dup 1)))
   (set (match_dup 4) (sign_extend:PDI (match_dup 2)))]
{
  operands[3] = gen_rtx_REG (PDImode, REGNO (operands[0]));
  operands[4] = gen_rtx_REG (PDImode, REGNO (operands[0]) + 1);
})

(define_insn "*pushsi_insn"
  [(set (mem:SI (pre_dec:SI (reg:SI REG_SP)))
        (match_operand:SI 0 "register_operand" "xy"))]
  ""
  "[--SP] = %0;"
  [(set_attr "type" "mcst")
   (set_attr "length" "2")])

(define_insn "*popsi_insn"
  [(set (match_operand:SI 0 "register_operand" "=xy")
        (mem:SI (post_inc:SI (reg:SI REG_SP))))]
  ""
  "%0 = [SP++];"
  [(set_attr "type" "mcld")
   (set_attr "length" "2")])

;; The first alternative is used to make reload choose a limited register
;; class when faced with a movsi_insn that had its input operand replaced
;; with a PLUS.  We generally require fewer secondary reloads this way.

(define_insn "*movsi_insn"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=da,x*y,*k,da,da,x,x,x,da,mr")
	(match_operand:SI 1 "general_operand" "da,x*y,da,*k,xKs7,xKsh,xKuh,ix,mr,da"))]
  "GET_CODE (operands[0]) != MEM || GET_CODE (operands[1]) != MEM"
 "@
   %0 = %1;
   %0 = %1;
   %0 = %1;
   %0 = %1;
   %0 = %1 (X);
   %0 = %1 (X);
   %0 = %1 (Z);
   #
   %0 = %1;
   %0 = %1;"
  [(set_attr "type" "move,move,move,move,mvi,mvi,mvi,*,mcld,mcst")
   (set_attr "length" "2,2,2,2,2,4,4,*,*,*")])

(define_insn_and_split "*movv2hi_insn"
  [(set (match_operand:V2HI 0 "nonimmediate_operand" "=da,da,d,dm")
        (match_operand:V2HI 1 "general_operand" "i,di,md,d"))]

  "GET_CODE (operands[0]) != MEM || GET_CODE (operands[1]) != MEM"
  "@
   #
   %0 = %1;
   %0 = %1;
   %0 = %1;"
  "reload_completed && GET_CODE (operands[1]) == CONST_VECTOR"
  [(set (match_dup 0) (high:SI (match_dup 2)))
   (set (match_dup 0) (lo_sum:SI (match_dup 0) (match_dup 3)))]
{
  HOST_WIDE_INT intval = INTVAL (XVECEXP (operands[1], 0, 1)) << 16;
  intval |= INTVAL (XVECEXP (operands[1], 0, 0)) & 0xFFFF;

  operands[0] = gen_rtx_REG (SImode, REGNO (operands[0]));
  operands[2] = operands[3] = GEN_INT (trunc_int_for_mode (intval, SImode));
}
  [(set_attr "type" "move,move,mcld,mcst")
   (set_attr "length" "2,2,*,*")])

(define_insn "*movhi_insn"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=x,da,x,d,mr")
        (match_operand:HI 1 "general_operand" "x,xKs7,xKsh,mr,d"))]
  "GET_CODE (operands[0]) != MEM || GET_CODE (operands[1]) != MEM"
{
  static const char *templates[] = {
    "%0 = %1;",
    "%0 = %1 (X);",
    "%0 = %1 (X);",
    "%0 = W %1 (X);",
    "W %0 = %1;",
    "%h0 = W %1;",
    "W %0 = %h1;"
  };
  int alt = which_alternative;
  rtx mem = (MEM_P (operands[0]) ? operands[0]
	     : MEM_P (operands[1]) ? operands[1] : NULL_RTX);
  if (mem && bfin_dsp_memref_p (mem))
    alt += 2;
  return templates[alt];
}
  [(set_attr "type" "move,mvi,mvi,mcld,mcst")
   (set_attr "length" "2,2,4,*,*")])

(define_insn "*movqi_insn"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=x,da,x,d,mr")
        (match_operand:QI 1 "general_operand" "x,xKs7,xKsh,mr,d"))]
  "GET_CODE (operands[0]) != MEM || GET_CODE (operands[1]) != MEM"
  "@
   %0 = %1;
   %0 = %1 (X);
   %0 = %1 (X);
   %0 = B %1 (X);
   B %0 = %1;"
  [(set_attr "type" "move,mvi,mvi,mcld,mcst")
   (set_attr "length" "2,2,4,*,*")])

(define_insn "*movsf_insn"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=x,x,da,mr")
        (match_operand:SF 1 "general_operand" "x,Fx,mr,da"))]
  "GET_CODE (operands[0]) != MEM || GET_CODE (operands[1]) != MEM"
  "@
   %0 = %1;
   #
   %0 = %1;
   %0 = %1;"
  [(set_attr "type" "move,*,mcld,mcst")])

(define_insn_and_split "movdf_insn"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=x,mx,r")
	(match_operand:DF 1 "general_operand" "iFx,r,mx"))]
  "GET_CODE (operands[0]) != MEM || GET_CODE (operands[1]) != MEM"
  "#"
  "reload_completed"
  [(set (match_dup 2) (match_dup 3))
   (set (match_dup 4) (match_dup 5))]
{
  rtx lo_half[2], hi_half[2];
  split_di (operands, 2, lo_half, hi_half);

  if (reg_overlap_mentioned_p (lo_half[0], hi_half[1]))
    {
      operands[2] = hi_half[0];
      operands[3] = hi_half[1];
      operands[4] = lo_half[0];
      operands[5] = lo_half[1];
    }
  else
    {
      operands[2] = lo_half[0];
      operands[3] = lo_half[1];
      operands[4] = hi_half[0];
      operands[5] = hi_half[1];
    }
})

;; Storing halfwords.
(define_insn "*movsi_insv"
  [(set (zero_extract:SI (match_operand 0 "register_operand" "+d,x")
			 (const_int 16)
			 (const_int 16))
	(match_operand:SI 1 "nonmemory_operand" "d,n"))]
  ""
  "@
   %d0 = %h1 << 0;
   %d0 = %1;"
  [(set_attr "type" "dsp32,mvi")])

(define_expand "insv"
  [(set (zero_extract:SI (match_operand:SI 0 "register_operand" "")
			 (match_operand:SI 1 "immediate_operand" "")
			 (match_operand:SI 2 "immediate_operand" ""))
        (match_operand:SI 3 "nonmemory_operand" ""))]
  ""
{
  if (INTVAL (operands[1]) != 16 || INTVAL (operands[2]) != 16)
    FAIL;

  /* From mips.md: insert_bit_field doesn't verify that our source
     matches the predicate, so check it again here.  */
  if (! register_operand (operands[0], VOIDmode))
    FAIL;
})

;; This is the main "hook" for PIC code.  When generating
;; PIC, movsi is responsible for determining when the source address
;; needs PIC relocation and appropriately calling legitimize_pic_address
;; to perform the actual relocation.

(define_expand "movsi"
  [(set (match_operand:SI 0 "nonimmediate_operand" "")
	(match_operand:SI 1 "general_operand" ""))]
  ""
  "expand_move (operands, SImode);")

(define_expand "movv2hi"
  [(set (match_operand:V2HI 0 "nonimmediate_operand" "")
	(match_operand:V2HI 1 "general_operand" ""))]
  ""
  "expand_move (operands, V2HImode);")

(define_expand "movdi"
  [(set (match_operand:DI 0 "nonimmediate_operand" "")
	(match_operand:DI 1 "general_operand" ""))]
  ""
  "expand_move (operands, DImode);")

(define_expand "movsf"
 [(set (match_operand:SF 0 "nonimmediate_operand" "")
       (match_operand:SF 1 "general_operand" ""))]
  ""
  "expand_move (operands, SFmode);")

(define_expand "movdf"
 [(set (match_operand:DF 0 "nonimmediate_operand" "")
       (match_operand:DF 1 "general_operand" ""))]
  ""
  "expand_move (operands, DFmode);")

(define_expand "movhi"
  [(set (match_operand:HI 0 "nonimmediate_operand" "")
	(match_operand:HI 1 "general_operand" ""))]
  ""
  "expand_move (operands, HImode);")

(define_expand "movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand" "")
	(match_operand:QI 1 "general_operand" ""))]
  ""
  " expand_move (operands, QImode); ")

;; Some define_splits to break up SI/SFmode loads of immediate constants.

(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(match_operand:SI 1 "symbolic_or_const_operand" ""))]
  "reload_completed
   /* Always split symbolic operands; split integer constants that are
      too large for a single instruction.  */
   && (GET_CODE (operands[1]) != CONST_INT
       || (INTVAL (operands[1]) < -32768
 	   || INTVAL (operands[1]) >= 65536
	   || (INTVAL (operands[1]) >= 32768 && PREG_P (operands[0]))))"
  [(set (match_dup 0) (high:SI (match_dup 1)))
   (set (match_dup 0) (lo_sum:SI (match_dup 0) (match_dup 1)))]
{
  if (GET_CODE (operands[1]) == CONST_INT
      && split_load_immediate (operands))
    DONE;
  /* ??? Do something about TARGET_LOW_64K.  */
})

(define_split
  [(set (match_operand:SF 0 "register_operand" "")
	(match_operand:SF 1 "immediate_operand" ""))]
  "reload_completed"
  [(set (match_dup 2) (high:SI (match_dup 3)))
   (set (match_dup 2) (lo_sum:SI (match_dup 2) (match_dup 3)))]
{
  long values;
  REAL_VALUE_TYPE value;

  gcc_assert (GET_CODE (operands[1]) == CONST_DOUBLE);

  REAL_VALUE_FROM_CONST_DOUBLE (value, operands[1]);
  REAL_VALUE_TO_TARGET_SINGLE (value, values);

  operands[2] = gen_rtx_REG (SImode, true_regnum (operands[0]));
  operands[3] = GEN_INT (trunc_int_for_mode (values, SImode));
  if (values >= -32768 && values < 65536)
    {
      emit_move_insn (operands[2], operands[3]);
      DONE;
    }
  if (split_load_immediate (operands + 2))
    DONE;
})

;; Sadly, this can't be a proper named movstrict pattern, since the compiler
;; expects to be able to use registers for operand 1.
;; Note that the asm instruction is defined by the manual to take an unsigned
;; constant, but it doesn't matter to the assembler, and the compiler only
;; deals with sign-extended constants.  Hence "Ksh".
(define_insn "movstricthi_1"
  [(set (strict_low_part (match_operand:HI 0 "register_operand" "+x"))
	(match_operand:HI 1 "immediate_operand" "Ksh"))]
  ""
  "%h0 = %1;"
  [(set_attr "type" "mvi")
   (set_attr "length" "4")])

;; Sign and zero extensions

(define_insn_and_split "extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=d, d")
	(sign_extend:SI (match_operand:HI 1 "nonimmediate_operand" "d, m")))]
  ""
  "@
   %0 = %h1 (X);
   %0 = W %h1 (X);"
  "reload_completed && bfin_dsp_memref_p (operands[1])"
  [(set (match_dup 2) (match_dup 1))
   (set (match_dup 0) (sign_extend:SI (match_dup 2)))]
{
  operands[2] = gen_lowpart (HImode, operands[0]);
}
  [(set_attr "type" "alu0,mcld")])

(define_insn_and_split "zero_extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=d, d")
	(zero_extend:SI (match_operand:HI 1 "nonimmediate_operand" "d, m")))]
  ""
  "@
   %0 = %h1 (Z);
   %0 = W %h1 (Z);"
  "reload_completed && bfin_dsp_memref_p (operands[1])"
  [(set (match_dup 2) (match_dup 1))
   (set (match_dup 0) (zero_extend:SI (match_dup 2)))]
{
  operands[2] = gen_lowpart (HImode, operands[0]);
}
  [(set_attr "type" "alu0,mcld")])

(define_insn "zero_extendbisi2"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(zero_extend:SI (match_operand:BI 1 "nonimmediate_operand" "C")))]
  ""
  "%0 = %1;"
  [(set_attr "type" "compare")])

(define_insn "extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=d, d")
	(sign_extend:HI (match_operand:QI 1 "nonimmediate_operand" "m, d")))]
  ""
  "@
   %0 = B %1 (X);
   %0 = %T1 (X);"
  [(set_attr "type" "mcld,alu0")])

(define_insn "extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=d, d")
	(sign_extend:SI (match_operand:QI 1 "nonimmediate_operand" "m, d")))]
  ""
  "@
   %0 = B %1 (X);
   %0 = %T1 (X);"
  [(set_attr "type" "mcld,alu0")])


(define_insn "zero_extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=d, d")
	(zero_extend:HI (match_operand:QI 1 "nonimmediate_operand" "m, d")))]
  ""
  "@
   %0 = B %1 (Z);
   %0 = %T1 (Z);"
  [(set_attr "type" "mcld,alu0")])


(define_insn "zero_extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=d, d")
	(zero_extend:SI (match_operand:QI 1 "nonimmediate_operand" "m, d")))]
  ""
  "@
   %0 = B %1 (Z);
   %0 = %T1 (Z);"
  [(set_attr "type" "mcld,alu0")])

;; DImode logical operations

(define_code_macro any_logical [and ior xor])
(define_code_attr optab [(and "and")
			 (ior "ior")
			 (xor "xor")])
(define_code_attr op [(and "&")
		      (ior "|")
		      (xor "^")])
(define_code_attr high_result [(and "0")
			       (ior "%H1")
			       (xor "%H1")])

(define_insn "<optab>di3"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (any_logical:DI (match_operand:DI 1 "register_operand" "0")
			(match_operand:DI 2 "register_operand" "d")))]
  ""
  "%0 = %1 <op> %2;\\n\\t%H0 = %H1 <op> %H2;"
  [(set_attr "length" "4")
   (set_attr "seq_insns" "multi")])

(define_insn "*<optab>di_zesidi_di"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (any_logical:DI (zero_extend:DI
			 (match_operand:SI 2 "register_operand" "d"))
			(match_operand:DI 1 "register_operand" "d")))]
  ""
  "%0 = %1 <op>  %2;\\n\\t%H0 = <high_result>;"
  [(set_attr "length" "4")
   (set_attr "seq_insns" "multi")])

(define_insn "*<optab>di_sesdi_di"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (any_logical:DI (sign_extend:DI
			 (match_operand:SI 2 "register_operand" "d"))
			(match_operand:DI 1 "register_operand" "0")))
   (clobber (match_scratch:SI 3 "=&d"))]
  ""
  "%0 = %1 <op> %2;\\n\\t%3 = %2;\\n\\t%3 >>>= 31;\\n\\t%H0 = %H1 <op> %3;"
  [(set_attr "length" "8")
   (set_attr "seq_insns" "multi")])

(define_insn "negdi2"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (neg:DI (match_operand:DI 1 "register_operand" "d")))
   (clobber (match_scratch:SI 2 "=&d"))
   (clobber (reg:CC REG_CC))]
  ""
  "%2 = 0; %2 = %2 - %1; cc = ac0; cc = !cc; %2 = cc;\\n\\t%0 = -%1; %H0 = -%H1; %H0 = %H0 - %2;"
  [(set_attr "length" "16")
   (set_attr "seq_insns" "multi")])

(define_insn "one_cmpldi2"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (not:DI (match_operand:DI 1 "register_operand" "d")))]
  ""
  "%0 = ~%1;\\n\\t%H0 = ~%H1;"
  [(set_attr "length" "4")
   (set_attr "seq_insns" "multi")])

;; DImode zero and sign extend patterns

(define_insn_and_split "zero_extendsidi2"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (zero_extend:DI (match_operand:SI 1 "register_operand" "d")))]
  ""
  "#"
  "reload_completed"
  [(set (match_dup 3) (const_int 0))]
{
  split_di (operands, 1, operands + 2, operands + 3);
  if (REGNO (operands[0]) != REGNO (operands[1]))
    emit_move_insn (operands[2], operands[1]);
})

(define_insn "zero_extendqidi2"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (zero_extend:DI (match_operand:QI 1 "register_operand" "d")))]
  ""
  "%0 = %T1 (Z);\\n\\t%H0 = 0;"
  [(set_attr "length" "4")
   (set_attr "seq_insns" "multi")])

(define_insn "zero_extendhidi2"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (zero_extend:DI (match_operand:HI 1 "register_operand" "d")))]
  ""
  "%0 = %h1 (Z);\\n\\t%H0 = 0;"
  [(set_attr "length" "4")
   (set_attr "seq_insns" "multi")])

(define_insn_and_split "extendsidi2"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (sign_extend:DI (match_operand:SI 1 "register_operand" "d")))]
  ""
  "#"
  "reload_completed"
  [(set (match_dup 3) (match_dup 1))
   (set (match_dup 3) (ashiftrt:SI (match_dup 3) (const_int 31)))]
{
  split_di (operands, 1, operands + 2, operands + 3);
  if (REGNO (operands[0]) != REGNO (operands[1]))
    emit_move_insn (operands[2], operands[1]);
})

(define_insn_and_split "extendqidi2"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (sign_extend:DI (match_operand:QI 1 "register_operand" "d")))]
  ""
  "#"
  "reload_completed"
  [(set (match_dup 2) (sign_extend:SI (match_dup 1)))
   (set (match_dup 3) (sign_extend:SI (match_dup 1)))
   (set (match_dup 3) (ashiftrt:SI (match_dup 3) (const_int 31)))]
{
  split_di (operands, 1, operands + 2, operands + 3);
})

(define_insn_and_split "extendhidi2"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (sign_extend:DI (match_operand:HI 1 "register_operand" "d")))]
  ""
  "#"
  "reload_completed"
  [(set (match_dup 2) (sign_extend:SI (match_dup 1)))
   (set (match_dup 3) (sign_extend:SI (match_dup 1)))
   (set (match_dup 3) (ashiftrt:SI (match_dup 3) (const_int 31)))]
{
  split_di (operands, 1, operands + 2, operands + 3);
})

;; DImode arithmetic operations

(define_insn "adddi3"
  [(set (match_operand:DI 0 "register_operand" "=&d,&d,&d")
        (plus:DI (match_operand:DI 1 "register_operand" "%0,0,0")
                 (match_operand:DI 2 "nonmemory_operand" "Kn7,Ks7,d")))
   (clobber (match_scratch:SI 3 "=&d,&d,&d"))
   (clobber (reg:CC 34))]
  ""
  "@
   %0 += %2; cc = ac0; %3 = cc; %H0 += -1; %H0 = %H0 + %3;
   %0 += %2; cc = ac0; %3 = cc; %H0 = %H0 + %3;
   %0 = %0 + %2; cc = ac0; %3 = cc; %H0 = %H0 + %H2; %H0 = %H0 + %3;"
  [(set_attr "type" "alu0")
   (set_attr "length" "10,8,10")
   (set_attr "seq_insns" "multi,multi,multi")])

(define_insn "subdi3"
  [(set (match_operand:DI 0 "register_operand" "=&d")
        (minus:DI (match_operand:DI 1 "register_operand" "0")
                  (match_operand:DI 2 "register_operand" "d")))
   (clobber (reg:CC 34))]
  ""
  "%0 = %1-%2;\\n\\tcc = ac0;\\n\\t%H0 = %H1-%H2;\\n\\tif cc jump 1f;\\n\\t%H0 += -1;\\n\\t1:"
  [(set_attr "length" "10")
   (set_attr "seq_insns" "multi")])

(define_insn "*subdi_di_zesidi"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (minus:DI (match_operand:DI 1 "register_operand" "0")
                  (zero_extend:DI
                  (match_operand:SI 2 "register_operand" "d"))))
   (clobber (match_scratch:SI 3 "=&d"))
   (clobber (reg:CC 34))]
  ""
  "%0 = %1 - %2;\\n\\tcc = ac0;\\n\\tcc = ! cc;\\n\\t%3 = cc;\\n\\t%H0 = %H1 - %3;"
  [(set_attr "length" "10")
   (set_attr "seq_insns" "multi")])

(define_insn "*subdi_zesidi_di"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (minus:DI (zero_extend:DI
                  (match_operand:SI 2 "register_operand" "d"))
                  (match_operand:DI 1 "register_operand" "0")))
   (clobber (match_scratch:SI 3 "=&d"))
   (clobber (reg:CC 34))]
  ""
  "%0 = %2 - %1;\\n\\tcc = ac0;\\n\\tcc = ! cc;\\n\\t%3 = cc;\\n\\t%3 = -%3;\\n\\t%H0 = %3 - %H1"
  [(set_attr "length" "12")
   (set_attr "seq_insns" "multi")])

(define_insn "*subdi_di_sesidi"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (minus:DI (match_operand:DI 1 "register_operand" "0")
                  (sign_extend:DI
                  (match_operand:SI 2 "register_operand" "d"))))
   (clobber (match_scratch:SI 3 "=&d"))
   (clobber (reg:CC 34))]
  ""
  "%0 = %1 - %2;\\n\\tcc = ac0;\\n\\t%3 = %2;\\n\\t%3 >>>= 31;\\n\\t%H0 = %H1 - %3;\\n\\tif cc jump 1f;\\n\\t%H0 += -1;\\n\\t1:"
  [(set_attr "length" "14")
   (set_attr "seq_insns" "multi")])

(define_insn "*subdi_sesidi_di"
  [(set (match_operand:DI 0 "register_operand" "=d")
        (minus:DI (sign_extend:DI
                  (match_operand:SI 2 "register_operand" "d"))
                  (match_operand:DI 1 "register_operand" "0")))
   (clobber (match_scratch:SI 3 "=&d"))
   (clobber (reg:CC 34))]
  ""
  "%0 = %2 - %1;\\n\\tcc = ac0;\\n\\t%3 = %2;\\n\\t%3 >>>= 31;\\n\\t%H0 = %3 - %H1;\\n\\tif cc jump 1f;\\n\\t%H0 += -1;\\n\\t1:"
  [(set_attr "length" "14")
   (set_attr "seq_insns" "multi")])

;; Combined shift/add instructions

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=a,d")
	(ashift:SI (plus:SI (match_operand:SI 1 "register_operand" "%0,0")
		            (match_operand:SI 2 "register_operand" "a,d"))
		   (match_operand:SI 3 "pos_scale_operand" "P1P2,P1P2")))]
  ""
  "%0 = (%0 + %2) << %3;" /* "shadd %0,%2,%3;" */
  [(set_attr "type" "alu0")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=a")
	(plus:SI (match_operand:SI 1 "register_operand" "a")
		 (mult:SI (match_operand:SI 2 "register_operand" "a")
			  (match_operand:SI 3 "scale_by_operand" "i"))))]
  ""
  "%0 = %1 + (%2 << %X3);"
  [(set_attr "type" "alu0")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=a")
	(plus:SI (match_operand:SI 1 "register_operand" "a")
		 (ashift:SI (match_operand:SI 2 "register_operand" "a")
			    (match_operand:SI 3 "pos_scale_operand" "i"))))]
  ""
  "%0 = %1 + (%2 << %3);"
  [(set_attr "type" "alu0")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=a")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand" "a")
			  (match_operand:SI 2 "scale_by_operand" "i"))
		 (match_operand:SI 3 "register_operand" "a")))]
  ""
  "%0 = %3 + (%1 << %X2);"
  [(set_attr "type" "alu0")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=a")
	(plus:SI (ashift:SI (match_operand:SI 1 "register_operand" "a")
			    (match_operand:SI 2 "pos_scale_operand" "i"))
		 (match_operand:SI 3 "register_operand" "a")))]
  ""
  "%0 = %3 + (%1 << %2);"
  [(set_attr "type" "alu0")])

(define_insn "mulhisi3"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(mult:SI (sign_extend:SI (match_operand:HI 1 "register_operand" "%d"))
		 (sign_extend:SI (match_operand:HI 2 "register_operand" "d"))))]
  ""
  "%0 = %h1 * %h2 (IS);"
  [(set_attr "type" "dsp32")])

(define_insn "umulhisi3"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(mult:SI (zero_extend:SI (match_operand:HI 1 "register_operand" "%d"))
		 (zero_extend:SI (match_operand:HI 2 "register_operand" "d"))))]
  ""
  "%0 = %h1 * %h2 (FU);"
  [(set_attr "type" "dsp32")])

(define_insn "usmulhisi3"
  [(set (match_operand:SI 0 "register_operand" "=W")
	(mult:SI (zero_extend:SI (match_operand:HI 1 "register_operand" "W"))
		 (sign_extend:SI (match_operand:HI 2 "register_operand" "W"))))]
  ""
  "%0 = %h2 * %h1 (IS,M);"
  [(set_attr "type" "dsp32")])

;; The processor also supports ireg += mreg or ireg -= mreg, but these
;; are unusable if we don't ensure that the corresponding lreg is zero.
;; The same applies to the add/subtract constant versions involving
;; iregs

(define_insn "addsi3"
  [(set (match_operand:SI 0 "register_operand" "=ad,a,d")
	(plus:SI (match_operand:SI 1 "register_operand" "%0, a,d")
		 (match_operand:SI 2 "reg_or_7bit_operand" "Ks7, a,d")))]
  ""
  "@
   %0 += %2;
   %0 = %1 + %2;
   %0 = %1 + %2;"
  [(set_attr "type" "alu0")
   (set_attr "length" "2,2,2")])

(define_insn "ssaddsi3"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(ss_plus:SI (match_operand:SI 1 "register_operand" "d")
		    (match_operand:SI 2 "register_operand" "d")))]
  ""
  "%0 = %1 + %2 (S);"
  [(set_attr "type" "dsp32")])

(define_insn "subsi3"
  [(set (match_operand:SI 0 "register_operand" "=da,d,a")
	(minus:SI (match_operand:SI 1 "register_operand" "0,d,0")
		  (match_operand:SI 2 "reg_or_neg7bit_operand" "KN7,d,a")))]
  ""
{
  static const char *const strings_subsi3[] = {
    "%0 += -%2;",
    "%0 = %1 - %2;",
    "%0 -= %2;",
  };

  if (CONSTANT_P (operands[2]) && INTVAL (operands[2]) < 0) {
     rtx tmp_op = operands[2];
     operands[2] = GEN_INT (-INTVAL (operands[2]));
     output_asm_insn ("%0 += %2;", operands);
     operands[2] = tmp_op;
     return "";
  }

  return strings_subsi3[which_alternative];
}
  [(set_attr "type" "alu0")])

(define_insn "sssubsi3"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(ss_minus:SI (match_operand:SI 1 "register_operand" "d")
		     (match_operand:SI 2 "register_operand" "d")))]
  ""
  "%0 = %1 - %2 (S);"
  [(set_attr "type" "dsp32")])

;; Bit test instructions

(define_insn "*not_bittst"
 [(set (match_operand:BI 0 "register_operand" "=C")
       (eq:BI (zero_extract:SI (match_operand:SI 1 "register_operand" "d")
			       (const_int 1)
			       (match_operand:SI 2 "immediate_operand" "Ku5"))
	      (const_int 0)))]
 ""
 "cc = !BITTST (%1,%2);"
  [(set_attr "type" "alu0")])

(define_insn "*bittst"
 [(set (match_operand:BI 0 "register_operand" "=C")
       (ne:BI (zero_extract:SI (match_operand:SI 1 "register_operand" "d")
			       (const_int 1)
			       (match_operand:SI 2 "immediate_operand" "Ku5"))
		(const_int 0)))]
 ""
 "cc = BITTST (%1,%2);"
  [(set_attr "type" "alu0")])

(define_insn_and_split "*bit_extract"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(zero_extract:SI (match_operand:SI 1 "register_operand" "d")
			 (const_int 1)
			 (match_operand:SI 2 "immediate_operand" "Ku5")))
   (clobber (reg:BI REG_CC))]
  ""
  "#"
  ""
  [(set (reg:BI REG_CC)
	(ne:BI (zero_extract:SI (match_dup 1) (const_int 1) (match_dup 2))
	       (const_int 0)))
   (set (match_dup 0)
	(ne:SI (reg:BI REG_CC) (const_int 0)))])

(define_insn_and_split "*not_bit_extract"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(zero_extract:SI (not:SI (match_operand:SI 1 "register_operand" "d"))
			 (const_int 1)
			 (match_operand:SI 2 "immediate_operand" "Ku5")))
   (clobber (reg:BI REG_CC))]
  ""
  "#"
  ""
  [(set (reg:BI REG_CC)
	(eq:BI (zero_extract:SI (match_dup 1) (const_int 1) (match_dup 2))
	       (const_int 0)))
   (set (match_dup 0)
	(ne:SI (reg:BI REG_CC) (const_int 0)))])

(define_insn "*andsi_insn"
  [(set (match_operand:SI 0 "register_operand" "=d,d,d,d")
	(and:SI (match_operand:SI 1 "register_operand" "%0,d,d,d")
		(match_operand:SI 2 "rhs_andsi3_operand" "L,M1,M2,d")))]
  ""
  "@
   BITCLR (%0,%Y2);
   %0 = %T1 (Z);
   %0 = %h1 (Z);
   %0 = %1 & %2;"
  [(set_attr "type" "alu0")])

(define_expand "andsi3"
  [(set (match_operand:SI 0 "register_operand" "")
	(and:SI (match_operand:SI 1 "register_operand" "")
		(match_operand:SI 2 "general_operand" "")))]
  ""
{
  if (highbits_operand (operands[2], SImode))
    {
      operands[2] = GEN_INT (exact_log2 (-INTVAL (operands[2])));
      emit_insn (gen_ashrsi3 (operands[0], operands[1], operands[2]));
      emit_insn (gen_ashlsi3 (operands[0], operands[0], operands[2]));
      DONE;
    }
  if (! rhs_andsi3_operand (operands[2], SImode))
    operands[2] = force_reg (SImode, operands[2]);
})

(define_insn "iorsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
	(ior:SI (match_operand:SI 1 "register_operand" "%0,d")
		(match_operand:SI 2 "regorlog2_operand" "J,d")))]
  ""
  "@
   BITSET (%0, %X2);
   %0 = %1 | %2;"
  [(set_attr "type" "alu0")])

(define_insn "xorsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
	(xor:SI (match_operand:SI 1 "register_operand" "%0,d")
		  (match_operand:SI 2 "regorlog2_operand" "J,d")))]
  ""
  "@
   BITTGL (%0, %X2);
   %0 = %1 ^ %2;"
  [(set_attr "type" "alu0")])

(define_insn "smaxsi3"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(smax:SI (match_operand:SI 1 "register_operand" "d")
		 (match_operand:SI 2 "register_operand" "d")))]
  ""
  "%0 = max(%1,%2);"
  [(set_attr "type" "dsp32")])

(define_insn "sminsi3"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(smin:SI (match_operand:SI 1 "register_operand" "d")
		 (match_operand:SI 2 "register_operand" "d")))]
  ""
  "%0 = min(%1,%2);"
  [(set_attr "type" "dsp32")])

(define_insn "abssi2"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(abs:SI (match_operand:SI 1 "register_operand" "d")))]
  ""
  "%0 = abs %1;"
  [(set_attr "type" "dsp32")])

(define_insn "negsi2"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(neg:SI (match_operand:SI 1 "register_operand" "d")))]
  ""
  "%0 = -%1;"
  [(set_attr "type" "alu0")])

(define_insn "ssnegsi2"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(ss_neg:SI (match_operand:SI 1 "register_operand" "d")))]
  ""
  "%0 = -%1 (S);"
  [(set_attr "type" "dsp32")])

(define_insn "one_cmplsi2"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(not:SI (match_operand:SI 1 "register_operand" "d")))]
  ""
  "%0 = ~%1;"
  [(set_attr "type" "alu0")])

(define_insn "signbitssi2"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(if_then_else:HI
	 (lt (match_operand:SI 1 "register_operand" "d") (const_int 0))
	 (clz:HI (not:SI (match_dup 1)))
	 (clz:HI (match_dup 1))))]
  ""
  "%h0 = signbits %1;"
  [(set_attr "type" "dsp32")])

(define_insn "smaxhi3"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(smax:HI (match_operand:HI 1 "register_operand" "d")
		 (match_operand:HI 2 "register_operand" "d")))]
  ""
  "%0 = max(%1,%2) (V);"
  [(set_attr "type" "dsp32")])

(define_insn "sminhi3"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(smin:HI (match_operand:HI 1 "register_operand" "d")
		 (match_operand:HI 2 "register_operand" "d")))]
  ""
  "%0 = min(%1,%2) (V);"
  [(set_attr "type" "dsp32")])

(define_insn "abshi2"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(abs:HI (match_operand:HI 1 "register_operand" "d")))]
  ""
  "%0 = abs %1 (V);"
  [(set_attr "type" "dsp32")])

(define_insn "neghi2"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(neg:HI (match_operand:HI 1 "register_operand" "d")))]
  ""
  "%0 = -%1;"
  [(set_attr "type" "dsp32")])

(define_insn "ssneghi2"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(ss_neg:HI (match_operand:HI 1 "register_operand" "d")))]
  ""
  "%0 = -%1 (V);"
  [(set_attr "type" "dsp32")])

(define_insn "signbitshi2"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(if_then_else:HI
	 (lt (match_operand:HI 1 "register_operand" "d") (const_int 0))
	 (clz:HI (not:HI (match_dup 1)))
	 (clz:HI (match_dup 1))))]
  ""
  "%h0 = signbits %h1;"
  [(set_attr "type" "dsp32")])

(define_insn "mulsi3"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(mult:SI (match_operand:SI 1 "register_operand" "%0")
		 (match_operand:SI 2 "register_operand" "d")))]
  ""
  "%0 *= %2;"
  [(set_attr "type" "mult")])

(define_expand "ashlsi3"
  [(set (match_operand:SI 0 "register_operand" "")
        (ashift:SI (match_operand:SI 1 "register_operand" "")
                   (match_operand:SI 2 "nonmemory_operand" "")))]
  ""
{
 if (GET_CODE (operands[2]) == CONST_INT
     && ((unsigned HOST_WIDE_INT) INTVAL (operands[2])) > 31)
   {
     emit_insn (gen_movsi (operands[0], const0_rtx));
     DONE;
   }
})

(define_insn_and_split "*ashlsi3_insn"
  [(set (match_operand:SI 0 "register_operand" "=d,a,a,a")
	(ashift:SI (match_operand:SI 1 "register_operand" "0,a,a,a")
		   (match_operand:SI 2 "nonmemory_operand" "dKu5,P1,P2,?P3P4")))]
  ""
  "@
   %0 <<= %2;
   %0 = %1 + %1;
   %0 = %1 << %2;
   #"
  "PREG_P (operands[0]) && INTVAL (operands[2]) > 2"
  [(set (match_dup 0) (ashift:SI (match_dup 1) (const_int 2)))
   (set (match_dup 0) (ashift:SI (match_dup 0) (match_dup 3)))]
  "operands[3] = GEN_INT (INTVAL (operands[2]) - 2);"
  [(set_attr "type" "shft")])

(define_insn "ashrsi3"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(ashiftrt:SI (match_operand:SI 1 "register_operand" "0")
		     (match_operand:SI 2 "nonmemory_operand" "dKu5")))]
  ""
  "%0 >>>= %2;"
  [(set_attr "type" "shft")])

(define_insn "ror_one"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(ior:SI (lshiftrt:SI (match_operand:SI 1 "register_operand" "d") (const_int 1))
		(ashift:SI (zero_extend:SI (reg:BI REG_CC)) (const_int 31))))
   (set (reg:BI REG_CC)
	(zero_extract:BI (match_dup 1) (const_int 1) (const_int 0)))]
  ""
  "%0 = ROT %1 BY -1;"
  [(set_attr "type" "shft")
   (set_attr "length" "4")])

(define_insn "rol_one"
  [(set (match_operand:SI 0 "register_operand" "+d")
	(ior:SI (ashift:SI (match_operand:SI 1 "register_operand" "d") (const_int 1))
		(zero_extend:SI (reg:BI REG_CC))))
   (set (reg:BI REG_CC)
	(zero_extract:BI (match_dup 1) (const_int 31) (const_int 0)))]
  ""
  "%0 = ROT %1 BY 1;"
  [(set_attr "type" "shft")
   (set_attr "length" "4")])

(define_expand "lshrdi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(lshiftrt:DI (match_operand:DI 1 "register_operand" "")
		     (match_operand:DI 2 "general_operand" "")))]
  ""
{
  rtx lo_half[2], hi_half[2];
      
  if (operands[2] != const1_rtx)
    FAIL;
  if (! rtx_equal_p (operands[0], operands[1]))
    emit_move_insn (operands[0], operands[1]);

  split_di (operands, 2, lo_half, hi_half);

  emit_move_insn (bfin_cc_rtx, const0_rtx);
  emit_insn (gen_ror_one (hi_half[0], hi_half[0]));
  emit_insn (gen_ror_one (lo_half[0], lo_half[0]));
  DONE;
})

(define_expand "ashrdi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(ashiftrt:DI (match_operand:DI 1 "register_operand" "")
		     (match_operand:DI 2 "general_operand" "")))]
  ""
{
  rtx lo_half[2], hi_half[2];
      
  if (operands[2] != const1_rtx)
    FAIL;
  if (! rtx_equal_p (operands[0], operands[1]))
    emit_move_insn (operands[0], operands[1]);

  split_di (operands, 2, lo_half, hi_half);

  emit_insn (gen_compare_lt (gen_rtx_REG (BImode, REG_CC),
			     hi_half[1], const0_rtx));
  emit_insn (gen_ror_one (hi_half[0], hi_half[0]));
  emit_insn (gen_ror_one (lo_half[0], lo_half[0]));
  DONE;
})

(define_expand "ashldi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(ashift:DI (match_operand:DI 1 "register_operand" "")
		   (match_operand:DI 2 "general_operand" "")))]
  ""
{
  rtx lo_half[2], hi_half[2];
      
  if (operands[2] != const1_rtx)
    FAIL;
  if (! rtx_equal_p (operands[0], operands[1]))
    emit_move_insn (operands[0], operands[1]);

  split_di (operands, 2, lo_half, hi_half);

  emit_move_insn (bfin_cc_rtx, const0_rtx);
  emit_insn (gen_rol_one (lo_half[0], lo_half[0]));
  emit_insn (gen_rol_one (hi_half[0], hi_half[0]));
  DONE;
})

(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,a")
	(lshiftrt:SI (match_operand:SI 1 "register_operand" " 0,a")
		     (match_operand:SI 2 "nonmemory_operand" "dKu5,P1P2")))]
  ""
  "@
   %0 >>= %2;
   %0 = %1 >> %2;"
  [(set_attr "type" "shft")])

;; A pattern to reload the equivalent of
;;   (set (Dreg) (plus (FP) (large_constant)))
;; or
;;   (set (dagreg) (plus (FP) (arbitrary_constant))) 
;; using a scratch register
(define_expand "reload_insi"
  [(parallel [(set (match_operand:SI 0 "register_operand" "=w")
                   (match_operand:SI 1 "fp_plus_const_operand" ""))
              (clobber (match_operand:SI 2 "register_operand" "=&a"))])]
  ""
{
  rtx fp_op = XEXP (operands[1], 0);
  rtx const_op = XEXP (operands[1], 1);
  rtx primary = operands[0];
  rtx scratch = operands[2];

  emit_move_insn (scratch, const_op);
  emit_insn (gen_addsi3 (scratch, scratch, fp_op));
  emit_move_insn (primary, scratch);
  DONE;
})

;; Jump instructions

(define_insn "jump"
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  ""
{
  if (get_attr_length (insn) == 2)
    return "jump.s %0;";
  else
    return "jump.l %0;";
}
  [(set_attr "type" "br")])

(define_insn "indirect_jump"
  [(set (pc)
	(match_operand:SI 0 "register_operand" "a"))]
  ""
  "jump (%0);"
  [(set_attr "type" "misc")])

(define_expand "tablejump"
  [(parallel [(set (pc) (match_operand:SI 0 "register_operand" "a"))
              (use (label_ref (match_operand 1 "" "")))])]
  ""
{
  /* In PIC mode, the table entries are stored PC relative.
     Convert the relative address to an absolute address.  */
  if (flag_pic)
    {
      rtx op1 = gen_rtx_LABEL_REF (Pmode, operands[1]);

      operands[0] = expand_simple_binop (Pmode, PLUS, operands[0],
					 op1, NULL_RTX, 0, OPTAB_DIRECT);
    }
})

(define_insn "*tablejump_internal"
  [(set (pc) (match_operand:SI 0 "register_operand" "a"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
  "jump (%0);"
  [(set_attr "type" "misc")])

;;  Hardware loop

; operand 0 is the loop count pseudo register
; operand 1 is the number of loop iterations or 0 if it is unknown
; operand 2 is the maximum number of loop iterations
; operand 3 is the number of levels of enclosed loops
; operand 4 is the label to jump to at the top of the loop
(define_expand "doloop_end"
  [(parallel [(set (pc) (if_then_else
			  (ne (match_operand:SI 0 "" "")
			      (const_int 1))
			  (label_ref (match_operand 4 "" ""))
			  (pc)))
	      (set (match_dup 0)
		   (plus:SI (match_dup 0)
			    (const_int -1)))
	      (unspec [(const_int 0)] UNSPEC_LSETUP_END)
	      (clobber (match_scratch:SI 5 ""))])]
  ""
  {bfin_hardware_loop ();})

(define_insn "loop_end"
  [(set (pc)
	(if_then_else (ne (match_operand:SI 0 "nonimmediate_operand" "+a*d,*b*v*f,m")
			  (const_int 1))
		      (label_ref (match_operand 1 "" ""))
		      (pc)))
   (set (match_dup 0)
	(plus (match_dup 0)
	      (const_int -1)))
   (unspec [(const_int 0)] UNSPEC_LSETUP_END)
   (clobber (match_scratch:SI 2 "=X,&r,&r"))]
  ""
  "@
   /* loop end %0 %l1 */
   #
   #"
  [(set_attr "length" "6,10,14")])

(define_split
  [(set (pc)
	(if_then_else (ne (match_operand:SI 0 "nondp_reg_or_memory_operand" "")
			  (const_int 1))
		      (label_ref (match_operand 1 "" ""))
		      (pc)))
   (set (match_dup 0)
	(plus (match_dup 0)
	      (const_int -1)))
   (unspec [(const_int 0)] UNSPEC_LSETUP_END)
   (clobber (match_scratch:SI 2 "=&r"))]
  "reload_completed"
  [(set (match_dup 2) (match_dup 0))
   (set (match_dup 2) (plus:SI (match_dup 2) (const_int -1)))
   (set (match_dup 0) (match_dup 2))
   (set (reg:BI REG_CC) (eq:BI (match_dup 2) (const_int 0)))
   (set (pc)
	(if_then_else (eq (reg:BI REG_CC)
			  (const_int 0))
		      (label_ref (match_dup 1))
		      (pc)))]
  "")

(define_insn "lsetup_with_autoinit"
  [(set (match_operand:SI 0 "lt_register_operand" "=t")
	(label_ref (match_operand 1 "" "")))
   (set (match_operand:SI 2 "lb_register_operand" "=u")
	(label_ref (match_operand 3 "" "")))
   (set (match_operand:SI 4 "lc_register_operand" "=k")
	(match_operand:SI 5 "register_operand" "a"))]
  ""
  "LSETUP (%1, %3) %4 = %5;"
  [(set_attr "length" "4")])

(define_insn "lsetup_without_autoinit"
  [(set (match_operand:SI 0 "lt_register_operand" "=t")
	(label_ref (match_operand 1 "" "")))
   (set (match_operand:SI 2 "lb_register_operand" "=u")
	(label_ref (match_operand 3 "" "")))
   (use (match_operand:SI 4 "lc_register_operand" "k"))]
  ""
  "LSETUP (%1, %3) %4;"
  [(set_attr "length" "4")])

;;  Call instructions..

;; The explicit MEM inside the UNSPEC prevents the compiler from moving
;; the load before a branch after a NULL test, or before a store that
;; initializes a function descriptor.

(define_insn_and_split "load_funcdescsi"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(unspec_volatile:SI [(mem:SI (match_operand:SI 1 "address_operand" "p"))]
			    UNSPEC_VOLATILE_LOAD_FUNCDESC))]
  ""
  "#"
  "reload_completed"
  [(set (match_dup 0) (mem:SI (match_dup 1)))])

(define_expand "call"
  [(parallel [(call (match_operand:SI 0 "" "")
		    (match_operand 1 "" ""))
	      (use (match_operand 2 "" ""))])]
  ""
{
  bfin_expand_call (NULL_RTX, operands[0], operands[1], operands[2], 0);
  DONE;
})

(define_expand "sibcall"
  [(parallel [(call (match_operand:SI 0 "" "")
		    (match_operand 1 "" ""))
	      (use (match_operand 2 "" ""))
	      (return)])]
  ""
{
  bfin_expand_call (NULL_RTX, operands[0], operands[1], operands[2], 1);
  DONE;
})

(define_expand "call_value"
  [(parallel [(set (match_operand 0 "register_operand" "")
		   (call (match_operand:SI 1 "" "")
			 (match_operand 2 "" "")))
	      (use (match_operand 3 "" ""))])]
  ""
{
  bfin_expand_call (operands[0], operands[1], operands[2], operands[3], 0);
  DONE;
})

(define_expand "sibcall_value"
  [(parallel [(set (match_operand 0 "register_operand" "")
		   (call (match_operand:SI 1 "" "")
			 (match_operand 2 "" "")))
	      (use (match_operand 3 "" ""))
	      (return)])]
  ""
{
  bfin_expand_call (operands[0], operands[1], operands[2], operands[3], 1);
  DONE;
})

(define_insn "*call_symbol_fdpic"
  [(call (mem:SI (match_operand:SI 0 "symbol_ref_operand" "Q"))
	 (match_operand 1 "general_operand" "g"))
   (use (match_operand:SI 2 "register_operand" "Z"))
   (use (match_operand 3 "" ""))]
  "! SIBLING_CALL_P (insn)
   && GET_CODE (operands[0]) == SYMBOL_REF
   && !bfin_longcall_p (operands[0], INTVAL (operands[3]))"
  "call %0;"
  [(set_attr "type" "call")
   (set_attr "length" "4")])

(define_insn "*sibcall_symbol_fdpic"
  [(call (mem:SI (match_operand:SI 0 "symbol_ref_operand" "Q"))
	 (match_operand 1 "general_operand" "g"))
   (use (match_operand:SI 2 "register_operand" "Z"))
   (use (match_operand 3 "" ""))
   (return)]
  "SIBLING_CALL_P (insn)
   && GET_CODE (operands[0]) == SYMBOL_REF
   && !bfin_longcall_p (operands[0], INTVAL (operands[3]))"
  "jump.l %0;"
  [(set_attr "type" "br")
   (set_attr "length" "4")])

(define_insn "*call_value_symbol_fdpic"
  [(set (match_operand 0 "register_operand" "=d")
        (call (mem:SI (match_operand:SI 1 "symbol_ref_operand" "Q"))
	      (match_operand 2 "general_operand" "g")))
   (use (match_operand:SI 3 "register_operand" "Z"))
   (use (match_operand 4 "" ""))]
  "! SIBLING_CALL_P (insn)
   && GET_CODE (operands[1]) == SYMBOL_REF
   && !bfin_longcall_p (operands[1], INTVAL (operands[4]))"
  "call %1;"
  [(set_attr "type" "call")
   (set_attr "length" "4")])

(define_insn "*sibcall_value_symbol_fdpic"
  [(set (match_operand 0 "register_operand" "=d")
         (call (mem:SI (match_operand:SI 1 "symbol_ref_operand" "Q"))
	       (match_operand 2 "general_operand" "g")))
   (use (match_operand:SI 3 "register_operand" "Z"))
   (use (match_operand 4 "" ""))
   (return)]
  "SIBLING_CALL_P (insn)
   && GET_CODE (operands[1]) == SYMBOL_REF
   && !bfin_longcall_p (operands[1], INTVAL (operands[4]))"
  "jump.l %1;"
  [(set_attr "type" "br")
   (set_attr "length" "4")])

(define_insn "*call_insn_fdpic"
  [(call (mem:SI (match_operand:SI 0 "register_no_elim_operand" "Y"))
	 (match_operand 1 "general_operand" "g"))
   (use (match_operand:SI 2 "register_operand" "Z"))
   (use (match_operand 3 "" ""))]
  "! SIBLING_CALL_P (insn)"
  "call (%0);"
  [(set_attr "type" "call")
   (set_attr "length" "2")])

(define_insn "*sibcall_insn_fdpic"
  [(call (mem:SI (match_operand:SI 0 "register_no_elim_operand" "Y"))
	 (match_operand 1 "general_operand" "g"))
   (use (match_operand:SI 2 "register_operand" "Z"))
   (use (match_operand 3 "" ""))
   (return)]
  "SIBLING_CALL_P (insn)"
  "jump (%0);"
  [(set_attr "type" "br")
   (set_attr "length" "2")])

(define_insn "*call_value_insn_fdpic"
  [(set (match_operand 0 "register_operand" "=d")
        (call (mem:SI (match_operand:SI 1 "register_no_elim_operand" "Y"))
	      (match_operand 2 "general_operand" "g")))
   (use (match_operand:SI 3 "register_operand" "Z"))
   (use (match_operand 4 "" ""))]
  "! SIBLING_CALL_P (insn)"
  "call (%1);"
  [(set_attr "type" "call")
   (set_attr "length" "2")])

(define_insn "*sibcall_value_insn_fdpic"
  [(set (match_operand 0 "register_operand" "=d")
         (call (mem:SI (match_operand:SI 1 "register_no_elim_operand" "Y"))
	       (match_operand 2 "general_operand" "g")))
   (use (match_operand:SI 3 "register_operand" "Z"))
   (use (match_operand 4 "" ""))
   (return)]
  "SIBLING_CALL_P (insn)"
  "jump (%1);"
  [(set_attr "type" "br")
   (set_attr "length" "2")])

(define_insn "*call_symbol"
  [(call (mem:SI (match_operand:SI 0 "symbol_ref_operand" "Q"))
	 (match_operand 1 "general_operand" "g"))
   (use (match_operand 2 "" ""))]
  "! SIBLING_CALL_P (insn)
   && !TARGET_ID_SHARED_LIBRARY
   && GET_CODE (operands[0]) == SYMBOL_REF
   && !bfin_longcall_p (operands[0], INTVAL (operands[2]))"
  "call %0;"
  [(set_attr "type" "call")
   (set_attr "length" "4")])

(define_insn "*sibcall_symbol"
  [(call (mem:SI (match_operand:SI 0 "symbol_ref_operand" "Q"))
	 (match_operand 1 "general_operand" "g"))
   (use (match_operand 2 "" ""))
   (return)]
  "SIBLING_CALL_P (insn)
   && !TARGET_ID_SHARED_LIBRARY
   && GET_CODE (operands[0]) == SYMBOL_REF
   && !bfin_longcall_p (operands[0], INTVAL (operands[2]))"
  "jump.l %0;"
  [(set_attr "type" "br")
   (set_attr "length" "4")])

(define_insn "*call_value_symbol"
  [(set (match_operand 0 "register_operand" "=d")
        (call (mem:SI (match_operand:SI 1 "symbol_ref_operand" "Q"))
	      (match_operand 2 "general_operand" "g")))
   (use (match_operand 3 "" ""))]
  "! SIBLING_CALL_P (insn)
   && !TARGET_ID_SHARED_LIBRARY
   && GET_CODE (operands[1]) == SYMBOL_REF
   && !bfin_longcall_p (operands[1], INTVAL (operands[3]))"
  "call %1;"
  [(set_attr "type" "call")
   (set_attr "length" "4")])

(define_insn "*sibcall_value_symbol"
  [(set (match_operand 0 "register_operand" "=d")
         (call (mem:SI (match_operand:SI 1 "symbol_ref_operand" "Q"))
	       (match_operand 2 "general_operand" "g")))
   (use (match_operand 3 "" ""))
   (return)]
  "SIBLING_CALL_P (insn)
   && !TARGET_ID_SHARED_LIBRARY
   && GET_CODE (operands[1]) == SYMBOL_REF
   && !bfin_longcall_p (operands[1], INTVAL (operands[3]))"
  "jump.l %1;"
  [(set_attr "type" "br")
   (set_attr "length" "4")])

(define_insn "*call_insn"
  [(call (mem:SI (match_operand:SI 0 "register_no_elim_operand" "a"))
	 (match_operand 1 "general_operand" "g"))
   (use (match_operand 2 "" ""))]
  "! SIBLING_CALL_P (insn)"
  "call (%0);"
  [(set_attr "type" "call")
   (set_attr "length" "2")])

(define_insn "*sibcall_insn"
  [(call (mem:SI (match_operand:SI 0 "register_no_elim_operand" "z"))
	 (match_operand 1 "general_operand" "g"))
   (use (match_operand 2 "" ""))
   (return)]
  "SIBLING_CALL_P (insn)"
  "jump (%0);"
  [(set_attr "type" "br")
   (set_attr "length" "2")])

(define_insn "*call_value_insn"
  [(set (match_operand 0 "register_operand" "=d")
        (call (mem:SI (match_operand:SI 1 "register_no_elim_operand" "a"))
	      (match_operand 2 "general_operand" "g")))
   (use (match_operand 3 "" ""))]
  "! SIBLING_CALL_P (insn)"
  "call (%1);"
  [(set_attr "type" "call")
   (set_attr "length" "2")])

(define_insn "*sibcall_value_insn"
  [(set (match_operand 0 "register_operand" "=d")
         (call (mem:SI (match_operand:SI 1 "register_no_elim_operand" "z"))
	       (match_operand 2 "general_operand" "g")))
   (use (match_operand 3 "" ""))
   (return)]
  "SIBLING_CALL_P (insn)"
  "jump (%1);"
  [(set_attr "type" "br")
   (set_attr "length" "2")])

;; Block move patterns

;; We cheat.  This copies one more word than operand 2 indicates.

(define_insn "rep_movsi"
  [(set (match_operand:SI 0 "register_operand" "=&a")
        (plus:SI (plus:SI (match_operand:SI 3 "register_operand" "0")
			  (ashift:SI (match_operand:SI 2 "register_operand" "a")
				     (const_int 2)))
		 (const_int 4)))
   (set (match_operand:SI 1 "register_operand" "=&b")
        (plus:SI (plus:SI (match_operand:SI 4 "register_operand" "1")
			  (ashift:SI (match_dup 2) (const_int 2)))
		 (const_int 4)))
   (set (mem:BLK (match_dup 3))
	(mem:BLK (match_dup 4)))
   (use (match_dup 2))
   (clobber (match_scratch:HI 5 "=&d"))
   (clobber (reg:SI REG_LT1))
   (clobber (reg:SI REG_LC1))
   (clobber (reg:SI REG_LB1))]
  ""
  "%5 = [%4++]; lsetup (1f, 1f) LC1 = %2; 1: MNOP || [%3++] = %5 || %5 = [%4++]; [%3++] = %5;"
  [(set_attr "type" "misc")
   (set_attr "length" "16")
   (set_attr "seq_insns" "multi")])

(define_insn "rep_movhi"
  [(set (match_operand:SI 0 "register_operand" "=&a")
        (plus:SI (plus:SI (match_operand:SI 3 "register_operand" "0")
			  (ashift:SI (match_operand:SI 2 "register_operand" "a")
				     (const_int 1)))
		 (const_int 2)))
   (set (match_operand:SI 1 "register_operand" "=&b")
        (plus:SI (plus:SI (match_operand:SI 4 "register_operand" "1")
			  (ashift:SI (match_dup 2) (const_int 1)))
		 (const_int 2)))
   (set (mem:BLK (match_dup 3))
	(mem:BLK (match_dup 4)))
   (use (match_dup 2))
   (clobber (match_scratch:HI 5 "=&d"))
   (clobber (reg:SI REG_LT1))
   (clobber (reg:SI REG_LC1))
   (clobber (reg:SI REG_LB1))]
  ""
  "%h5 = W[%4++]; lsetup (1f, 1f) LC1 = %2; 1: MNOP || W [%3++] = %5 || %h5 = W [%4++]; W [%3++] = %5;"
  [(set_attr "type" "misc")
   (set_attr "length" "16")
   (set_attr "seq_insns" "multi")])

(define_expand "movmemsi"
  [(match_operand:BLK 0 "general_operand" "")
   (match_operand:BLK 1 "general_operand" "")
   (match_operand:SI 2 "const_int_operand" "")
   (match_operand:SI 3 "const_int_operand" "")]
  ""
{
  if (bfin_expand_movmem (operands[0], operands[1], operands[2], operands[3]))
    DONE;
  FAIL;
})

;; Conditional branch patterns
;; The Blackfin has only few condition codes: eq, lt, lte, ltu, leu

;; The only outcome of this pattern is that global variables
;; bfin_compare_op[01] are set for use in bcond patterns.

(define_expand "cmpbi"
 [(set (cc0) (compare (match_operand:BI 0 "register_operand" "")
                      (match_operand:BI 1 "immediate_operand" "")))]
 ""
{
  bfin_compare_op0 = operands[0];
  bfin_compare_op1 = operands[1];
  DONE;
})

(define_expand "cmpsi"
 [(set (cc0) (compare (match_operand:SI 0 "register_operand" "")
                      (match_operand:SI 1 "reg_or_const_int_operand" "")))]
 ""
{
  bfin_compare_op0 = operands[0];
  bfin_compare_op1 = operands[1];
  DONE;
})

(define_insn "compare_eq"
  [(set (match_operand:BI 0 "register_operand" "=C,C")
        (eq:BI (match_operand:SI 1 "register_operand" "d,a")
               (match_operand:SI 2 "reg_or_const_int_operand" "dKs3,aKs3")))]
  ""
  "cc =%1==%2;"
  [(set_attr "type" "compare")])

(define_insn "compare_ne"
  [(set (match_operand:BI 0 "register_operand" "=C,C")
        (ne:BI (match_operand:SI 1 "register_operand" "d,a")
               (match_operand:SI 2 "reg_or_const_int_operand" "dKs3,aKs3")))]
  "0"
  "cc =%1!=%2;"
  [(set_attr "type" "compare")])

(define_insn "compare_lt"
  [(set (match_operand:BI 0 "register_operand" "=C,C")
        (lt:BI (match_operand:SI 1 "register_operand" "d,a")
               (match_operand:SI 2 "reg_or_const_int_operand" "dKs3,aKs3")))]
  ""
  "cc =%1<%2;"
  [(set_attr "type" "compare")])

(define_insn "compare_le"
  [(set (match_operand:BI 0 "register_operand" "=C,C")
        (le:BI (match_operand:SI 1 "register_operand" "d,a")
               (match_operand:SI 2 "reg_or_const_int_operand" "dKs3,aKs3")))]
  ""
  "cc =%1<=%2;"
  [(set_attr "type" "compare")])

(define_insn "compare_leu"
  [(set (match_operand:BI 0 "register_operand" "=C,C")
        (leu:BI (match_operand:SI 1 "register_operand" "d,a")
                (match_operand:SI 2 "reg_or_const_int_operand" "dKu3,aKu3")))]
  ""
  "cc =%1<=%2 (iu);"
  [(set_attr "type" "compare")])

(define_insn "compare_ltu"
  [(set (match_operand:BI 0 "register_operand" "=C,C")
        (ltu:BI (match_operand:SI 1 "register_operand" "d,a")
                (match_operand:SI 2 "reg_or_const_int_operand" "dKu3,aKu3")))]
  ""
  "cc =%1<%2 (iu);"
  [(set_attr "type" "compare")])

(define_expand "beq"
  [(set (match_dup 1) (match_dup 2))
   (set (pc)
	(if_then_else (match_dup 3)
		   (label_ref (match_operand 0 "" ""))
		   (pc)))]
  ""
{
  rtx op0 = bfin_compare_op0, op1 = bfin_compare_op1;
  operands[1] = bfin_cc_rtx;	/* hard register: CC */
  operands[2] = gen_rtx_EQ (BImode, op0, op1);
  /* If we have a BImode input, then we already have a compare result, and
     do not need to emit another comparison.  */
  if (GET_MODE (bfin_compare_op0) == BImode)
    {
      gcc_assert (bfin_compare_op1 == const0_rtx);
      emit_insn (gen_cbranchbi4 (operands[2], op0, op1, operands[0]));
      DONE;
    }

  operands[3] = gen_rtx_NE (BImode, operands[1], const0_rtx);
})

(define_expand "bne"
  [(set (match_dup 1) (match_dup 2))
   (set (pc)
	(if_then_else (match_dup 3)
		      (label_ref (match_operand 0 "" ""))
		    (pc)))]
  ""
{
  rtx op0 = bfin_compare_op0, op1 = bfin_compare_op1;
  /* If we have a BImode input, then we already have a compare result, and
     do not need to emit another comparison.  */
  if (GET_MODE (bfin_compare_op0) == BImode)
    {
      rtx cmp = gen_rtx_NE (BImode, op0, op1);

      gcc_assert (bfin_compare_op1 == const0_rtx);
      emit_insn (gen_cbranchbi4 (cmp, op0, op1, operands[0]));
      DONE;
    }

  operands[1] = bfin_cc_rtx;	/* hard register: CC */
  operands[2] = gen_rtx_EQ (BImode, op0, op1);
  operands[3] = gen_rtx_EQ (BImode, operands[1], const0_rtx);
})

(define_expand "bgt"
  [(set (match_dup 1) (match_dup 2))
   (set (pc)
	(if_then_else (match_dup 3)
		      (label_ref (match_operand 0 "" ""))
		    (pc)))]
  ""
{
  operands[1] = bfin_cc_rtx;
  operands[2] = gen_rtx_LE (BImode, bfin_compare_op0, bfin_compare_op1);
  operands[3] = gen_rtx_EQ (BImode, operands[1], const0_rtx);
})

(define_expand "bgtu"
  [(set (match_dup 1) (match_dup 2))
   (set (pc)
	(if_then_else (match_dup 3)
		      (label_ref (match_operand 0 "" ""))
		    (pc)))]
  ""
{
  operands[1] = bfin_cc_rtx;
  operands[2] = gen_rtx_LEU (BImode, bfin_compare_op0, bfin_compare_op1);
  operands[3] = gen_rtx_EQ (BImode, operands[1], const0_rtx);
})

(define_expand "blt"
  [(set (match_dup 1) (match_dup 2))
   (set (pc)
	(if_then_else (match_dup 3)
		      (label_ref (match_operand 0 "" ""))
		    (pc)))]
  ""
{
  operands[1] = bfin_cc_rtx;
  operands[2] = gen_rtx_LT (BImode, bfin_compare_op0, bfin_compare_op1);
  operands[3] = gen_rtx_NE (BImode, operands[1], const0_rtx);
})

(define_expand "bltu"
  [(set (match_dup 1) (match_dup 2))
   (set (pc)
	(if_then_else (match_dup 3)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  operands[1] = bfin_cc_rtx;
  operands[2] = gen_rtx_LTU (BImode, bfin_compare_op0, bfin_compare_op1);
  operands[3] = gen_rtx_NE (BImode, operands[1], const0_rtx);
})


(define_expand "bge"
  [(set (match_dup 1) (match_dup 2))
   (set (pc)
	(if_then_else (match_dup 3)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  operands[1] = bfin_cc_rtx;
  operands[2] = gen_rtx_LT (BImode, bfin_compare_op0, bfin_compare_op1);
  operands[3] = gen_rtx_EQ (BImode, operands[1], const0_rtx);
})

(define_expand "bgeu"
  [(set (match_dup 1) (match_dup 2))
   (set (pc)
	(if_then_else (match_dup 3)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  operands[1] = bfin_cc_rtx;
  operands[2] = gen_rtx_LTU (BImode, bfin_compare_op0, bfin_compare_op1);
  operands[3] = gen_rtx_EQ (BImode, operands[1], const0_rtx);
})

(define_expand "ble"
  [(set (match_dup 1) (match_dup 2))
   (set (pc)
	(if_then_else (match_dup 3)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  operands[1] = bfin_cc_rtx;
  operands[2] = gen_rtx_LE (BImode, bfin_compare_op0, bfin_compare_op1);
  operands[3] = gen_rtx_NE (BImode, operands[1], const0_rtx);
})

(define_expand "bleu"
  [(set (match_dup 1) (match_dup 2))
   (set (pc)
	(if_then_else (match_dup 3)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))
  ]
  ""
{
  operands[1] = bfin_cc_rtx;
  operands[2] = gen_rtx_LEU (BImode, bfin_compare_op0, bfin_compare_op1);
  operands[3] = gen_rtx_NE (BImode, operands[1], const0_rtx);
})

(define_insn "cbranchbi4"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "bfin_cbranch_operator"
			 [(match_operand:BI 1 "register_operand" "C")
			  (match_operand:BI 2 "immediate_operand" "P0")])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))]
  ""
{
  asm_conditional_branch (insn, operands, 0, 0);
  return "";
}
  [(set_attr "type" "brcc")])

;; Special cbranch patterns to deal with the speculative load problem - see
;; bfin_reorg for details.

(define_insn "cbranch_predicted_taken"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "bfin_cbranch_operator"
			 [(match_operand:BI 1 "register_operand" "C")
			  (match_operand:BI 2 "immediate_operand" "P0")])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))
   (unspec [(const_int 0)] UNSPEC_CBRANCH_TAKEN)]
  ""
{
  asm_conditional_branch (insn, operands, 0, 1);
  return "";
}
  [(set_attr "type" "brcc")])

(define_insn "cbranch_with_nops"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "bfin_cbranch_operator"
			 [(match_operand:BI 1 "register_operand" "C")
			  (match_operand:BI 2 "immediate_operand" "P0")])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))
   (unspec [(match_operand 4 "immediate_operand" "")] UNSPEC_CBRANCH_NOPS)]
  "reload_completed"
{
  asm_conditional_branch (insn, operands, INTVAL (operands[4]), 0);
  return "";
}
  [(set_attr "type" "brcc")
   (set_attr "length" "6")])

;; setcc insns.  */
(define_expand "seq"
  [(set (match_dup 1) (eq:BI (match_dup 2) (match_dup 3)))
   (set (match_operand:SI 0 "register_operand" "")
	(ne:SI (match_dup 1) (const_int 0)))]
  ""
{
  operands[2] = bfin_compare_op0;
  operands[3] = bfin_compare_op1;
  operands[1] = bfin_cc_rtx;
})

(define_expand "slt"
  [(set (match_dup 1) (lt:BI (match_dup 2) (match_dup 3)))
   (set (match_operand:SI 0 "register_operand" "")
	(ne:SI (match_dup 1) (const_int 0)))]
  ""
{
   operands[2] = bfin_compare_op0;
   operands[3] = bfin_compare_op1;
   operands[1] = bfin_cc_rtx;
})

(define_expand "sle"
  [(set (match_dup 1) (le:BI (match_dup 2) (match_dup 3)))
   (set (match_operand:SI 0 "register_operand" "")
	(ne:SI (match_dup 1) (const_int 0)))]
  ""
{
   operands[2] = bfin_compare_op0;
   operands[3] = bfin_compare_op1;
   operands[1] = bfin_cc_rtx;
})

(define_expand "sltu"
  [(set (match_dup 1) (ltu:BI (match_dup 2) (match_dup 3)))
   (set (match_operand:SI 0 "register_operand" "")
	(ne:SI (match_dup 1) (const_int 0)))]
  ""
{
   operands[2] = bfin_compare_op0;
   operands[3] = bfin_compare_op1;
   operands[1] = bfin_cc_rtx;
})

(define_expand "sleu"
  [(set (match_dup 1) (leu:BI (match_dup 2) (match_dup 3)))
   (set (match_operand:SI 0 "register_operand" "")
	(ne:SI (match_dup 1) (const_int 0)))]
  ""
{
   operands[2] = bfin_compare_op0;
   operands[3] = bfin_compare_op1;
   operands[1] = bfin_cc_rtx;
})

(define_insn "nop"
  [(const_int 0)]
  ""
  "nop;")

;;;;;;;;;;;;;;;;;;;;   CC2dreg   ;;;;;;;;;;;;;;;;;;;;;;;;;
(define_insn "movsibi"
  [(set (match_operand:BI 0 "register_operand" "=C")
	(ne:BI (match_operand:SI 1 "register_operand" "d")
	       (const_int 0)))]
  ""
  "CC = %1;"
  [(set_attr "length" "2")])

(define_insn "movbisi"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(ne:SI (match_operand:BI 1 "register_operand" "C")
	       (const_int 0)))]
  ""
  "%0 = CC;"
  [(set_attr "length" "2")])

(define_insn ""
  [(set (match_operand:BI 0 "register_operand" "=C")
	(eq:BI (match_operand:BI 1 "register_operand" " 0")
	       (const_int 0)))]
  ""
  "%0 = ! %0;"    /*  NOT CC;"  */
  [(set_attr "type" "compare")])

;; Vector and DSP insns

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=d")
	(ior:SI (ashift:SI (match_operand:SI 1 "register_operand" "d")
			   (const_int 24))
		(lshiftrt:SI (match_operand:SI 2 "register_operand" "d")
			     (const_int 8))))]
  ""
  "%0 = ALIGN8(%1, %2);"
  [(set_attr "type" "dsp32")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=d")
	(ior:SI (ashift:SI (match_operand:SI 1 "register_operand" "d")
			   (const_int 16))
		(lshiftrt:SI (match_operand:SI 2 "register_operand" "d")
			     (const_int 16))))]
  ""
  "%0 = ALIGN16(%1, %2);"
  [(set_attr "type" "dsp32")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=d")
	(ior:SI (ashift:SI (match_operand:SI 1 "register_operand" "d")
			   (const_int 8))
		(lshiftrt:SI (match_operand:SI 2 "register_operand" "d")
			     (const_int 24))))]
  ""
  "%0 = ALIGN24(%1, %2);"
  [(set_attr "type" "dsp32")])

;; Prologue and epilogue.

(define_expand "prologue"
  [(const_int 1)]
  ""
  "bfin_expand_prologue (); DONE;")

(define_expand "epilogue"
  [(const_int 1)]
  ""
  "bfin_expand_epilogue (1, 0); DONE;")

(define_expand "sibcall_epilogue"
  [(const_int 1)]
  ""
  "bfin_expand_epilogue (0, 0); DONE;")

(define_expand "eh_return"
  [(unspec_volatile [(match_operand:SI 0 "register_operand" "")]
		    UNSPEC_VOLATILE_EH_RETURN)]
  ""
{
  emit_move_insn (EH_RETURN_HANDLER_RTX, operands[0]);
  emit_jump_insn (gen_eh_return_internal ());
  emit_barrier ();
  DONE;
})

(define_insn_and_split "eh_return_internal"
  [(set (pc)
	(unspec_volatile [(reg:SI REG_P2)] UNSPEC_VOLATILE_EH_RETURN))]
  ""
  "#"
  "reload_completed"
  [(const_int 1)]
  "bfin_expand_epilogue (1, 1); DONE;")

(define_insn "link"
  [(set (mem:SI (plus:SI (reg:SI REG_SP) (const_int -4))) (reg:SI REG_RETS))
   (set (mem:SI (plus:SI (reg:SI REG_SP) (const_int -8))) (reg:SI REG_FP))
   (set (reg:SI REG_FP)
	(plus:SI (reg:SI REG_SP) (const_int -8)))
   (set (reg:SI REG_SP)
	(plus:SI (reg:SI REG_SP) (match_operand:SI 0 "immediate_operand" "i")))]
  ""
  "LINK %Z0;"
  [(set_attr "length" "4")])

(define_insn "unlink"
  [(set (reg:SI REG_FP) (mem:SI (reg:SI REG_FP)))
   (set (reg:SI REG_RETS) (mem:SI (plus:SI (reg:SI REG_FP) (const_int 4))))
   (set (reg:SI REG_SP) (plus:SI (reg:SI REG_FP) (const_int 8)))]
  ""
  "UNLINK;"
  [(set_attr "length" "4")])

;; This pattern is slightly clumsy.  The stack adjust must be the final SET in
;; the pattern, otherwise dwarf2out becomes very confused about which reg goes
;; where on the stack, since it goes through all elements of the parallel in
;; sequence.
(define_insn "push_multiple"
  [(match_parallel 0 "push_multiple_operation"
    [(unspec [(match_operand:SI 1 "immediate_operand" "i")] UNSPEC_PUSH_MULTIPLE)])]
  ""
{
  output_push_multiple (insn, operands);
  return "";
})

(define_insn "pop_multiple"
  [(match_parallel 0 "pop_multiple_operation"
    [(set (reg:SI REG_SP)
	  (plus:SI (reg:SI REG_SP) (match_operand:SI 1 "immediate_operand" "i")))])]
  ""
{
  output_pop_multiple (insn, operands);
  return "";
})

(define_insn "return_internal"
  [(return)
   (unspec [(match_operand 0 "immediate_operand" "i")] UNSPEC_RETURN)]
  "reload_completed"
{
  switch (INTVAL (operands[0]))
    {
    case EXCPT_HANDLER:
      return "rtx;";
    case NMI_HANDLER:
      return "rtn;";
    case INTERRUPT_HANDLER:
      return "rti;";
    case SUBROUTINE:
      return "rts;";
    }
  gcc_unreachable ();
})

(define_insn "csync"
  [(unspec_volatile [(const_int 0)] UNSPEC_VOLATILE_CSYNC)]
  ""
  "csync;"
  [(set_attr "type" "sync")])

(define_insn "ssync"
  [(unspec_volatile [(const_int 0)] UNSPEC_VOLATILE_SSYNC)]
  ""
  "ssync;"
  [(set_attr "type" "sync")])

(define_insn "trap"
  [(trap_if (const_int 1) (const_int 3))]
  ""
  "excpt 3;"
  [(set_attr "type" "misc")
   (set_attr "length" "2")])

(define_insn "trapifcc"
  [(trap_if (reg:BI REG_CC) (const_int 3))]
  ""
  "if !cc jump 4 (bp); excpt 3;"
  [(set_attr "type" "misc")
   (set_attr "length" "4")
   (set_attr "seq_insns" "multi")])

;;; Vector instructions

;; First, all sorts of move variants

(define_insn "movhi_low2high"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(vec_concat:V2HI
	 (vec_select:HI (match_operand:V2HI 1 "register_operand" "0")
			(parallel [(const_int 0)]))
	 (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
			(parallel [(const_int 0)]))))]
  ""
  "%d0 = %h2 << 0;"
  [(set_attr "type" "dsp32")])

(define_insn "movhi_high2high"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(vec_concat:V2HI
	 (vec_select:HI (match_operand:V2HI 1 "register_operand" "0")
			(parallel [(const_int 0)]))
	 (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
			(parallel [(const_int 1)]))))]
  ""
  "%d0 = %d2 << 0;"
  [(set_attr "type" "dsp32")])

(define_insn "movhi_low2low"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(vec_concat:V2HI
	 (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
			(parallel [(const_int 0)]))
	 (vec_select:HI (match_operand:V2HI 1 "register_operand" "0")
			(parallel [(const_int 1)]))))]
  ""
  "%h0 = %h2 << 0;"
  [(set_attr "type" "dsp32")])

(define_insn "movhi_high2low"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(vec_concat:V2HI
	 (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
			(parallel [(const_int 1)]))
	 (vec_select:HI (match_operand:V2HI 1 "register_operand" "0")
			(parallel [(const_int 1)]))))]
  ""
  "%h0 = %d2 << 0;"
  [(set_attr "type" "dsp32")])

(define_insn "movhiv2hi_low"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(vec_concat:V2HI
	 (match_operand:HI 2 "register_operand" "d")
	 (vec_select:HI (match_operand:V2HI 1 "register_operand" "0")
			(parallel [(const_int 1)]))))]
  ""
  "%h0 = %h2 << 0;"
  [(set_attr "type" "dsp32")])

(define_insn "movhiv2hi_high"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(vec_concat:V2HI
	 (vec_select:HI (match_operand:V2HI 1 "register_operand" "0")
			(parallel [(const_int 0)]))
	 (match_operand:HI 2 "register_operand" "d")))]
  ""
  "%d0 = %h2 << 0;"
  [(set_attr "type" "dsp32")])

;; No earlyclobber on alternative two since our sequence ought to be safe.
;; The order of operands is intentional to match the VDSP builtin (high word
;; is passed first).
(define_insn_and_split "composev2hi"
  [(set (match_operand:V2HI 0 "register_operand" "=d,d")
	(vec_concat:V2HI (match_operand:HI 2 "register_operand" "0,d")
			 (match_operand:HI 1 "register_operand" "d,d")))]
  ""
  "@
   %d0 = %h2 << 0;
   #"
  "reload_completed"
  [(set (match_dup 0)
	(vec_concat:V2HI
	 (vec_select:HI (match_dup 0) (parallel [(const_int 0)]))
	 (match_dup 2)))
   (set (match_dup 0)
	(vec_concat:V2HI
	 (match_dup 1)
	 (vec_select:HI (match_dup 0) (parallel [(const_int 1)]))))]
  ""
  [(set_attr "type" "dsp32")])

; Like composev2hi, but operating on elements of V2HI vectors.
; Useful on its own, and as a combiner bridge for the multiply and
; mac patterns.
(define_insn "packv2hi"
  [(set (match_operand:V2HI 0 "register_operand" "=d,d,d,d")
	(vec_concat:V2HI (vec_select:HI
			  (match_operand:V2HI 1 "register_operand" "d,d,d,d")
			  (parallel [(match_operand 3 "const01_operand" "P0,P1,P0,P1")]))
			 (vec_select:HI
			  (match_operand:V2HI 2 "register_operand" "d,d,d,d")
			  (parallel [(match_operand 4 "const01_operand" "P0,P0,P1,P1")]))))]
  ""
  "@
   %0 = PACK (%h2,%h1);
   %0 = PACK (%h2,%d1);
   %0 = PACK (%d2,%h1);
   %0 = PACK (%d2,%d1);"
  [(set_attr "type" "dsp32")])

(define_insn "movv2hi_hi"
  [(set (match_operand:HI 0 "register_operand" "=d,d,d")
	(vec_select:HI (match_operand:V2HI 1 "register_operand" "0,d,d")
		       (parallel [(match_operand 2 "const01_operand" "P0,P0,P1")])))]
  ""
  "@
   /* optimized out */
   %h0 = %h1 << 0;
   %h0 = %d1 << 0;"
  [(set_attr "type" "dsp32")])

(define_expand "movv2hi_hi_low"
  [(set (match_operand:HI 0 "register_operand" "")
	(vec_select:HI (match_operand:V2HI 1 "register_operand" "")
		       (parallel [(const_int 0)])))]
  ""
  "")

(define_expand "movv2hi_hi_high"
  [(set (match_operand:HI 0 "register_operand" "")
	(vec_select:HI (match_operand:V2HI 1 "register_operand" "")
		       (parallel [(const_int 1)])))]
  ""
  "")

;; Unusual arithmetic operations on 16 bit registers.

(define_insn "ssaddhi3"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(ss_plus:HI (match_operand:HI 1 "register_operand" "d")
		    (match_operand:HI 2 "register_operand" "d")))]
  ""
  "%h0 = %h1 + %h2 (S);"
  [(set_attr "type" "dsp32")])

(define_insn "sssubhi3"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(ss_minus:HI (match_operand:HI 1 "register_operand" "d")
		     (match_operand:HI 2 "register_operand" "d")))]
  ""
  "%h0 = %h1 - %h2 (S);"
  [(set_attr "type" "dsp32")])

;; V2HI vector insns

(define_insn "addv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(plus:V2HI (match_operand:V2HI 1 "register_operand" "d")
		   (match_operand:V2HI 2 "register_operand" "d")))]
  ""
  "%0 = %1 +|+ %2;"
  [(set_attr "type" "dsp32")])

(define_insn "ssaddv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(ss_plus:V2HI (match_operand:V2HI 1 "register_operand" "d")
		      (match_operand:V2HI 2 "register_operand" "d")))]
  ""
  "%0 = %1 +|+ %2 (S);"
  [(set_attr "type" "dsp32")])

(define_insn "subv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(minus:V2HI (match_operand:V2HI 1 "register_operand" "d")
		   (match_operand:V2HI 2 "register_operand" "d")))]
  ""
  "%0 = %1 -|- %2;"
  [(set_attr "type" "dsp32")])

(define_insn "sssubv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(ss_minus:V2HI (match_operand:V2HI 1 "register_operand" "d")
		       (match_operand:V2HI 2 "register_operand" "d")))]
  ""
  "%0 = %1 -|- %2 (S);"
  [(set_attr "type" "dsp32")])

(define_insn "addsubv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(vec_concat:V2HI
	 (plus:HI (vec_select:HI (match_operand:V2HI 1 "register_operand" "d")
				 (parallel [(const_int 0)]))
		  (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				 (parallel [(const_int 0)])))
	 (minus:HI (vec_select:HI (match_dup 1) (parallel [(const_int 1)]))
		   (vec_select:HI (match_dup 2) (parallel [(const_int 1)])))))]
  ""
  "%0 = %1 +|- %2;"
  [(set_attr "type" "dsp32")])

(define_insn "subaddv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(vec_concat:V2HI
	 (minus:HI (vec_select:HI (match_operand:V2HI 1 "register_operand" "d")
				  (parallel [(const_int 0)]))
		   (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				  (parallel [(const_int 0)])))
	 (plus:HI (vec_select:HI (match_dup 1) (parallel [(const_int 1)]))
		  (vec_select:HI (match_dup 2) (parallel [(const_int 1)])))))]
  ""
  "%0 = %1 -|+ %2;"
  [(set_attr "type" "dsp32")])

(define_insn "ssaddsubv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(vec_concat:V2HI
	 (ss_plus:HI (vec_select:HI (match_operand:V2HI 1 "register_operand" "d")
				    (parallel [(const_int 0)]))
		     (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				    (parallel [(const_int 0)])))
	 (ss_minus:HI (vec_select:HI (match_dup 1) (parallel [(const_int 1)]))
		      (vec_select:HI (match_dup 2) (parallel [(const_int 1)])))))]
  ""
  "%0 = %1 +|- %2 (S);"
  [(set_attr "type" "dsp32")])

(define_insn "sssubaddv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(vec_concat:V2HI
	 (ss_minus:HI (vec_select:HI (match_operand:V2HI 1 "register_operand" "d")
				     (parallel [(const_int 0)]))
		      (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				     (parallel [(const_int 0)])))
	 (ss_plus:HI (vec_select:HI (match_dup 1) (parallel [(const_int 1)]))
		     (vec_select:HI (match_dup 2) (parallel [(const_int 1)])))))]
  ""
  "%0 = %1 -|+ %2 (S);"
  [(set_attr "type" "dsp32")])

(define_insn "sublohiv2hi3"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(minus:HI (vec_select:HI (match_operand:V2HI 1 "register_operand" "d")
				 (parallel [(const_int 1)]))
		  (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				 (parallel [(const_int 0)]))))]
  ""
  "%h0 = %d1 - %h2;"
  [(set_attr "type" "dsp32")])

(define_insn "subhilov2hi3"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(minus:HI (vec_select:HI (match_operand:V2HI 1 "register_operand" "d")
				 (parallel [(const_int 0)]))
		  (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				 (parallel [(const_int 1)]))))]
  ""
  "%h0 = %h1 - %d2;"
  [(set_attr "type" "dsp32")])

(define_insn "sssublohiv2hi3"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(ss_minus:HI (vec_select:HI (match_operand:V2HI 1 "register_operand" "d")
				    (parallel [(const_int 1)]))
		     (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				    (parallel [(const_int 0)]))))]
  ""
  "%h0 = %d1 - %h2 (S);"
  [(set_attr "type" "dsp32")])

(define_insn "sssubhilov2hi3"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(ss_minus:HI (vec_select:HI (match_operand:V2HI 1 "register_operand" "d")
				    (parallel [(const_int 0)]))
		     (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				    (parallel [(const_int 1)]))))]
  ""
  "%h0 = %h1 - %d2 (S);"
  [(set_attr "type" "dsp32")])

(define_insn "addlohiv2hi3"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(plus:HI (vec_select:HI (match_operand:V2HI 1 "register_operand" "d")
				(parallel [(const_int 1)]))
		 (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				(parallel [(const_int 0)]))))]
  ""
  "%h0 = %d1 + %h2;"
  [(set_attr "type" "dsp32")])

(define_insn "addhilov2hi3"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(plus:HI (vec_select:HI (match_operand:V2HI 1 "register_operand" "d")
				(parallel [(const_int 0)]))
		 (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				(parallel [(const_int 1)]))))]
  ""
  "%h0 = %h1 + %d2;"
  [(set_attr "type" "dsp32")])

(define_insn "ssaddlohiv2hi3"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(ss_plus:HI (vec_select:HI (match_operand:V2HI 1 "register_operand" "d")
				   (parallel [(const_int 1)]))
		    (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				   (parallel [(const_int 0)]))))]
  ""
  "%h0 = %d1 + %h2 (S);"
  [(set_attr "type" "dsp32")])

(define_insn "ssaddhilov2hi3"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(ss_plus:HI (vec_select:HI (match_operand:V2HI 1 "register_operand" "d")
				   (parallel [(const_int 0)]))
		    (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				   (parallel [(const_int 1)]))))]
  ""
  "%h0 = %h1 + %d2 (S);"
  [(set_attr "type" "dsp32")])

(define_insn "sminv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(smin:V2HI (match_operand:V2HI 1 "register_operand" "d")
		   (match_operand:V2HI 2 "register_operand" "d")))]
  ""
  "%0 = MIN (%1, %2) (V);"
  [(set_attr "type" "dsp32")])

(define_insn "smaxv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(smax:V2HI (match_operand:V2HI 1 "register_operand" "d")
		   (match_operand:V2HI 2 "register_operand" "d")))]
  ""
  "%0 = MAX (%1, %2) (V);"
  [(set_attr "type" "dsp32")])

;; Multiplications.

;; The Blackfin allows a lot of different options, and we need many patterns to
;; cover most of the hardware's abilities.
;; There are a few simple patterns using MULT rtx codes, but most of them use
;; an unspec with a const_int operand that determines which flag to use in the
;; instruction.
;; There are variants for single and parallel multiplications.
;; There are variants which just use 16 bit lowparts as inputs, and variants
;; which allow the user to choose just which halves to use as input values.
;; There are variants which set D registers, variants which set accumulators,
;; variants which set both, some of them optionally using the accumulators as
;; inputs for multiply-accumulate operations.

(define_insn "flag_mulhi"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(unspec:HI [(match_operand:HI 1 "register_operand" "d")
		    (match_operand:HI 2 "register_operand" "d")
		    (match_operand 3 "const_int_operand" "n")]
		   UNSPEC_MUL_WITH_FLAG))]
  ""
  "%h0 = %h1 * %h2 %M3;"
  [(set_attr "type" "dsp32")])

(define_insn "flag_mulhisi"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(unspec:SI [(match_operand:HI 1 "register_operand" "d")
		    (match_operand:HI 2 "register_operand" "d")
		    (match_operand 3 "const_int_operand" "n")]
		   UNSPEC_MUL_WITH_FLAG))]
  ""
  "%0 = %h1 * %h2 %M3;"
  [(set_attr "type" "dsp32")])

(define_insn "flag_mulhisi_parts"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(unspec:SI [(vec_select:HI
		     (match_operand:V2HI 1 "register_operand" "d")
		     (parallel [(match_operand 3 "const01_operand" "P0P1")]))
		    (vec_select:HI
		     (match_operand:V2HI 2 "register_operand" "d")
		     (parallel [(match_operand 4 "const01_operand" "P0P1")]))
		    (match_operand 5 "const_int_operand" "n")]
		   UNSPEC_MUL_WITH_FLAG))]
  ""
{
  const char *templates[] = {
    "%0 = %h1 * %h2 %M5;",
    "%0 = %d1 * %h2 %M5;",
    "%0 = %h1 * %d2 %M5;",
    "%0 = %d1 * %d2 %M5;" };
  int alt = INTVAL (operands[3]) + (INTVAL (operands[4]) << 1);
  return templates[alt];
}
  [(set_attr "type" "dsp32")])

(define_insn "flag_machi"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(unspec:HI [(match_operand:HI 1 "register_operand" "d")
		    (match_operand:HI 2 "register_operand" "d")
		    (match_operand 3 "register_operand" "A")
		    (match_operand 4 "const01_operand" "P0P1")
		    (match_operand 5 "const_int_operand" "n")]
		   UNSPEC_MAC_WITH_FLAG))
   (set (match_operand:PDI 6 "register_operand" "=A")
	(unspec:PDI [(match_dup 1) (match_dup 2) (match_dup 3)
		     (match_dup 4) (match_dup 5)]
		    UNSPEC_MAC_WITH_FLAG))]
  ""
  "%h0 = (A0 %b4 %h1 * %h2) %M6;"
  [(set_attr "type" "dsp32")])

(define_insn "flag_machi_acconly"
  [(set (match_operand:PDI 0 "register_operand" "=e")
	(unspec:PDI [(match_operand:HI 1 "register_operand" "d")
		     (match_operand:HI 2 "register_operand" "d")
		     (match_operand 3 "register_operand" "A")
		     (match_operand 4 "const01_operand" "P0P1")
		     (match_operand 5 "const_int_operand" "n")]
		    UNSPEC_MAC_WITH_FLAG))]
  ""
  "%0 %b4 %h1 * %h2 %M6;"
  [(set_attr "type" "dsp32")])

(define_insn "flag_macinithi"
  [(set (match_operand:HI 0 "register_operand" "=d")
	(unspec:HI [(match_operand:HI 1 "register_operand" "d")
		    (match_operand:HI 2 "register_operand" "d")
		    (match_operand 3 "const_int_operand" "n")]
		   UNSPEC_MAC_WITH_FLAG))
   (set (match_operand:PDI 4 "register_operand" "=A")
	(unspec:PDI [(match_dup 1) (match_dup 2) (match_dup 3)]
		    UNSPEC_MAC_WITH_FLAG))]
  ""
  "%h0 = (A0 = %h1 * %h2) %M3;"
  [(set_attr "type" "dsp32")])

(define_insn "flag_macinit1hi"
  [(set (match_operand:PDI 0 "register_operand" "=e")
	(unspec:PDI [(match_operand:HI 1 "register_operand" "d")
		     (match_operand:HI 2 "register_operand" "d")
		     (match_operand 3 "const_int_operand" "n")]
		    UNSPEC_MAC_WITH_FLAG))]
  ""
  "%0 = %h1 * %h2 %M3;"
  [(set_attr "type" "dsp32")])

(define_insn "mulv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(mult:V2HI (match_operand:V2HI 1 "register_operand" "d")
		   (match_operand:V2HI 2 "register_operand" "d")))]
  ""
  "%h0 = %h1 * %h2, %d0 = %d1 * %d2 (IS);"
  [(set_attr "type" "dsp32")])

(define_insn "flag_mulv2hi"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(match_operand:V2HI 1 "register_operand" "d")
		      (match_operand:V2HI 2 "register_operand" "d")
		      (match_operand 3 "const_int_operand" "n")]
		     UNSPEC_MUL_WITH_FLAG))]
  ""
  "%h0 = %h1 * %h2, %d0 = %d1 * %d2 %M3;"
  [(set_attr "type" "dsp32")])

(define_insn "flag_mulv2hi_parts"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(vec_concat:V2HI
		       (vec_select:HI
			(match_operand:V2HI 1 "register_operand" "d")
			(parallel [(match_operand 3 "const01_operand" "P0P1")]))
		       (vec_select:HI
			(match_dup 1)
			(parallel [(match_operand 4 "const01_operand" "P0P1")])))
		      (vec_concat:V2HI
		       (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
			(parallel [(match_operand 5 "const01_operand" "P0P1")]))
		       (vec_select:HI (match_dup 2)
			(parallel [(match_operand 6 "const01_operand" "P0P1")])))
		      (match_operand 7 "const_int_operand" "n")]
		     UNSPEC_MUL_WITH_FLAG))]
  ""
{
  const char *templates[] = {
    "%h0 = %h1 * %h2, %d0 = %h1 * %h2 %M7;",
    "%h0 = %d1 * %h2, %d0 = %h1 * %h2 %M7;",
    "%h0 = %h1 * %h2, %d0 = %d1 * %h2 %M7;",
    "%h0 = %d1 * %h2, %d0 = %d1 * %h2 %M7;",
    "%h0 = %h1 * %d2, %d0 = %h1 * %h2 %M7;",
    "%h0 = %d1 * %d2, %d0 = %h1 * %h2 %M7;",
    "%h0 = %h1 * %d2, %d0 = %d1 * %h2 %M7;",
    "%h0 = %d1 * %d2, %d0 = %d1 * %h2 %M7;",
    "%h0 = %h1 * %h2, %d0 = %h1 * %d2 %M7;",
    "%h0 = %d1 * %h2, %d0 = %h1 * %d2 %M7;",
    "%h0 = %h1 * %h2, %d0 = %d1 * %d2 %M7;",
    "%h0 = %d1 * %h2, %d0 = %d1 * %d2 %M7;",
    "%h0 = %h1 * %d2, %d0 = %h1 * %d2 %M7;",
    "%h0 = %d1 * %d2, %d0 = %h1 * %d2 %M7;",
    "%h0 = %h1 * %d2, %d0 = %d1 * %d2 %M7;",
    "%h0 = %d1 * %d2, %d0 = %d1 * %d2 %M7;" };
  int alt = (INTVAL (operands[3]) + (INTVAL (operands[4]) << 1)
	     + (INTVAL (operands[5]) << 2)  + (INTVAL (operands[6]) << 3));
  return templates[alt];
}
  [(set_attr "type" "dsp32")])

;; A slightly complicated pattern.
;; Operand 0 is the halfword output; operand 11 is the accumulator output
;; Halfword inputs are operands 1 and 2; operands 3, 4, 5 and 6 specify which
;; parts of these 2x16 bit registers to use.
;; Operand 7 is the accumulator input.
;; Operands 8/9 specify whether low/high parts are mac (0) or msu (1)
;; Operand 10 is the macflag to be used.
(define_insn "flag_macv2hi_parts"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(vec_concat:V2HI
		       (vec_select:HI
			(match_operand:V2HI 1 "register_operand" "d")
			(parallel [(match_operand 3 "const01_operand" "P0P1")]))
		       (vec_select:HI
			(match_dup 1)
			(parallel [(match_operand 4 "const01_operand" "P0P1")])))
		      (vec_concat:V2HI
		       (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
			(parallel [(match_operand 5 "const01_operand" "P0P1")]))
		       (vec_select:HI (match_dup 2)
			(parallel [(match_operand 6 "const01_operand" "P0P1")])))
		      (match_operand:V2PDI 7 "register_operand" "e")
		      (match_operand 8 "const01_operand" "P0P1")
		      (match_operand 9 "const01_operand" "P0P1")
		      (match_operand 10 "const_int_operand" "n")]
		     UNSPEC_MAC_WITH_FLAG))
   (set (match_operand:V2PDI 11 "register_operand" "=e")
	(unspec:V2PDI [(vec_concat:V2HI
			(vec_select:HI (match_dup 1) (parallel [(match_dup 3)]))
			(vec_select:HI (match_dup 1) (parallel [(match_dup 4)])))
		       (vec_concat:V2HI
			(vec_select:HI (match_dup 2) (parallel [(match_dup 5)]))
			(vec_select:HI (match_dup 2) (parallel [(match_dup 5)])))
		       (match_dup 7) (match_dup 8) (match_dup 9) (match_dup 10)]
		      UNSPEC_MAC_WITH_FLAG))]
  ""
{
  const char *templates[] = {
    "%h0 = (A0 %b8 %h1 * %h2), %d0 = (A1 %b9 %h1 * %h2) %M10;",
    "%h0 = (A0 %b8 %d1 * %h2), %d0 = (A1 %b9 %h1 * %h2) %M10;",
    "%h0 = (A0 %b8 %h1 * %h2), %d0 = (A1 %b9 %d1 * %h2) %M10;",
    "%h0 = (A0 %b8 %d1 * %h2), %d0 = (A1 %b9 %d1 * %h2) %M10;",
    "%h0 = (A0 %b8 %h1 * %d2), %d0 = (A1 %b9 %h1 * %h2) %M10;",
    "%h0 = (A0 %b8 %d1 * %d2), %d0 = (A1 %b9 %h1 * %h2) %M10;",
    "%h0 = (A0 %b8 %h1 * %d2), %d0 = (A1 %b9 %d1 * %h2) %M10;",
    "%h0 = (A0 %b8 %d1 * %d2), %d0 = (A1 %b9 %d1 * %h2) %M10;",
    "%h0 = (A0 %b8 %h1 * %h2), %d0 = (A1 %b9 %h1 * %d2) %M10;",
    "%h0 = (A0 %b8 %d1 * %h2), %d0 = (A1 %b9 %h1 * %d2) %M10;",
    "%h0 = (A0 %b8 %h1 * %h2), %d0 = (A1 %b9 %d1 * %d2) %M10;",
    "%h0 = (A0 %b8 %d1 * %h2), %d0 = (A1 %b9 %d1 * %d2) %M10;",
    "%h0 = (A0 %b8 %h1 * %d2), %d0 = (A1 %b9 %h1 * %d2) %M10;",
    "%h0 = (A0 %b8 %d1 * %d2), %d0 = (A1 %b9 %h1 * %d2) %M10;",
    "%h0 = (A0 %b8 %h1 * %d2), %d0 = (A1 %b9 %d1 * %d2) %M10;",
    "%h0 = (A0 %b8 %d1 * %d2), %d0 = (A1 %b9 %d1 * %d2) %M10;" };
  int alt = (INTVAL (operands[3]) + (INTVAL (operands[4]) << 1)
	     + (INTVAL (operands[5]) << 2)  + (INTVAL (operands[6]) << 3));
  return templates[alt];
}
  [(set_attr "type" "dsp32")])

(define_insn "flag_macv2hi_parts_acconly"
  [(set (match_operand:V2PDI 0 "register_operand" "=e")
	(unspec:V2PDI [(vec_concat:V2HI
			(vec_select:HI
			 (match_operand:V2HI 1 "register_operand" "d")
			 (parallel [(match_operand 3 "const01_operand" "P0P1")]))
			(vec_select:HI
			 (match_dup 1)
			 (parallel [(match_operand 4 "const01_operand" "P0P1")])))
		       (vec_concat:V2HI
			(vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				       (parallel [(match_operand 5 "const01_operand" "P0P1")]))
			(vec_select:HI (match_dup 2)
				       (parallel [(match_operand 6 "const01_operand" "P0P1")])))
		       (match_operand:V2PDI 7 "register_operand" "e")
		       (match_operand 8 "const01_operand" "P0P1")
		       (match_operand 9 "const01_operand" "P0P1")
		       (match_operand 10 "const_int_operand" "n")]
		      UNSPEC_MAC_WITH_FLAG))]
  ""
{
  const char *templates[] = {
    "A0 %b8 %h1 * %h2, A1 %b9 %h1 * %h2 %M10;",
    "A0 %b8 %d1 * %h2, A1 %b9 %h1 * %h2 %M10;",
    "A0 %b8 %h1 * %h2, A1 %b9 %d1 * %h2 %M10;",
    "A0 %b8 %d1 * %h2, A1 %b9 %d1 * %h2 %M10;",
    "A0 %b8 %h1 * %d2, A1 %b9 %h1 * %h2 %M10;",
    "A0 %b8 %d1 * %d2, A1 %b9 %h1 * %h2 %M10;",
    "A0 %b8 %h1 * %d2, A1 %b9 %d1 * %h2 %M10;",
    "A0 %b8 %d1 * %d2, A1 %b9 %d1 * %h2 %M10;",
    "A0 %b8 %h1 * %h2, A1 %b9 %h1 * %d2 %M10;",
    "A0 %b8 %d1 * %h2, A1 %b9 %h1 * %d2 %M10;",
    "A0 %b8 %h1 * %h2, A1 %b9 %d1 * %d2 %M10;",
    "A0 %b8 %d1 * %h2, A1 %b9 %d1 * %d2 %M10;",
    "A0 %b8 %h1 * %d2, A1 %b9 %h1 * %d2 %M10;",
    "A0 %b8 %d1 * %d2, A1 %b9 %h1 * %d2 %M10;",
    "A0 %b8 %h1 * %d2, A1 %b9 %d1 * %d2 %M10;",
    "A0 %b8 %d1 * %d2, A1 %b9 %d1 * %d2 %M10;" };
  int alt = (INTVAL (operands[3]) + (INTVAL (operands[4]) << 1)
	     + (INTVAL (operands[5]) << 2)  + (INTVAL (operands[6]) << 3));
  return templates[alt];
}
  [(set_attr "type" "dsp32")])

;; Same as above, but initializing the accumulators and therefore a couple fewer
;; necessary operands.
(define_insn "flag_macinitv2hi_parts"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(vec_concat:V2HI
		       (vec_select:HI
			(match_operand:V2HI 1 "register_operand" "d")
			(parallel [(match_operand 3 "const01_operand" "P0P1")]))
		       (vec_select:HI
			(match_dup 1)
			(parallel [(match_operand 4 "const01_operand" "P0P1")])))
		      (vec_concat:V2HI
		       (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
			(parallel [(match_operand 5 "const01_operand" "P0P1")]))
		       (vec_select:HI (match_dup 2)
			(parallel [(match_operand 6 "const01_operand" "P0P1")])))
		      (match_operand 7 "const_int_operand" "n")]
		     UNSPEC_MAC_WITH_FLAG))
   (set (match_operand:V2PDI 8 "register_operand" "=e")
	(unspec:V2PDI [(vec_concat:V2HI
			(vec_select:HI (match_dup 1) (parallel [(match_dup 3)]))
			(vec_select:HI (match_dup 1) (parallel [(match_dup 4)])))
		       (vec_concat:V2HI
			(vec_select:HI (match_dup 2) (parallel [(match_dup 5)]))
			(vec_select:HI (match_dup 2) (parallel [(match_dup 5)])))
		       (match_dup 7)]
		      UNSPEC_MAC_WITH_FLAG))]
  ""
{
  const char *templates[] = {
    "%h0 = (A0 = %h1 * %h2), %d0 = (A1 = %h1 * %h2) %M7;",
    "%h0 = (A0 = %d1 * %h2), %d0 = (A1 = %h1 * %h2) %M7;",
    "%h0 = (A0 = %h1 * %h2), %d0 = (A1 = %d1 * %h2) %M7;",
    "%h0 = (A0 = %d1 * %h2), %d0 = (A1 = %d1 * %h2) %M7;",
    "%h0 = (A0 = %h1 * %d2), %d0 = (A1 = %h1 * %h2) %M7;",
    "%h0 = (A0 = %d1 * %d2), %d0 = (A1 = %h1 * %h2) %M7;",
    "%h0 = (A0 = %h1 * %d2), %d0 = (A1 = %d1 * %h2) %M7;",
    "%h0 = (A0 = %d1 * %d2), %d0 = (A1 = %d1 * %h2) %M7;",
    "%h0 = (A0 = %h1 * %h2), %d0 = (A1 = %h1 * %d2) %M7;",
    "%h0 = (A0 = %d1 * %h2), %d0 = (A1 = %h1 * %d2) %M7;",
    "%h0 = (A0 = %h1 * %h2), %d0 = (A1 = %d1 * %d2) %M7;",
    "%h0 = (A0 = %d1 * %h2), %d0 = (A1 = %d1 * %d2) %M7;",
    "%h0 = (A0 = %h1 * %d2), %d0 = (A1 = %h1 * %d2) %M7;",
    "%h0 = (A0 = %d1 * %d2), %d0 = (A1 = %h1 * %d2) %M7;",
    "%h0 = (A0 = %h1 * %d2), %d0 = (A1 = %d1 * %d2) %M7;",
    "%h0 = (A0 = %d1 * %d2), %d0 = (A1 = %d1 * %d2) %M7;" };
  int alt = (INTVAL (operands[3]) + (INTVAL (operands[4]) << 1)
	     + (INTVAL (operands[5]) << 2)  + (INTVAL (operands[6]) << 3));
  return templates[alt];
}
  [(set_attr "type" "dsp32")])

(define_insn "flag_macinit1v2hi_parts"
  [(set (match_operand:V2PDI 0 "register_operand" "=e")
	(unspec:V2PDI [(vec_concat:V2HI
		       (vec_select:HI
			(match_operand:V2HI 1 "register_operand" "d")
			(parallel [(match_operand 3 "const01_operand" "P0P1")]))
		       (vec_select:HI
			(match_dup 1)
			(parallel [(match_operand 4 "const01_operand" "P0P1")])))
		      (vec_concat:V2HI
		       (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
			(parallel [(match_operand 5 "const01_operand" "P0P1")]))
		       (vec_select:HI (match_dup 2)
			(parallel [(match_operand 6 "const01_operand" "P0P1")])))
		      (match_operand 7 "const_int_operand" "n")]
		     UNSPEC_MAC_WITH_FLAG))]
  ""
{
  const char *templates[] = {
    "A0 = %h1 * %h2, A1 = %h1 * %h2 %M7;",
    "A0 = %d1 * %h2, A1 = %h1 * %h2 %M7;",
    "A0 = %h1 * %h2, A1 = %d1 * %h2 %M7;",
    "A0 = %d1 * %h2, A1 = %d1 * %h2 %M7;",
    "A0 = %h1 * %d2, A1 = %h1 * %h2 %M7;",
    "A0 = %d1 * %d2, A1 = %h1 * %h2 %M7;",
    "A0 = %h1 * %d2, A1 = %d1 * %h2 %M7;",
    "A0 = %d1 * %d2, A1 = %d1 * %h2 %M7;",
    "A0 = %h1 * %h2, A1 = %h1 * %d2 %M7;",
    "A0 = %d1 * %h2, A1 = %h1 * %d2 %M7;",
    "A0 = %h1 * %h2, A1 = %d1 * %d2 %M7;",
    "A0 = %d1 * %h2, A1 = %d1 * %d2 %M7;",
    "A0 = %h1 * %d2, A1 = %h1 * %d2 %M7;",
    "A0 = %d1 * %d2, A1 = %h1 * %d2 %M7;",
    "A0 = %h1 * %d2, A1 = %d1 * %d2 %M7;",
    "A0 = %d1 * %d2, A1 = %d1 * %d2 %M7;" };
  int alt = (INTVAL (operands[3]) + (INTVAL (operands[4]) << 1)
	     + (INTVAL (operands[5]) << 2)  + (INTVAL (operands[6]) << 3));
  return templates[alt];
}
  [(set_attr "type" "dsp32")])

(define_insn "mulhisi_ll"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(mult:SI (sign_extend:SI
		  (vec_select:HI (match_operand:V2HI 1 "register_operand" "%d")
				 (parallel [(const_int 0)])))
		 (sign_extend:SI
		  (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				 (parallel [(const_int 0)])))))]
  ""
  "%0 = %h1 * %h2 (IS);"
  [(set_attr "type" "dsp32")])

(define_insn "mulhisi_lh"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(mult:SI (sign_extend:SI
		  (vec_select:HI (match_operand:V2HI 1 "register_operand" "%d")
				 (parallel [(const_int 0)])))
		 (sign_extend:SI
		  (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				 (parallel [(const_int 1)])))))]
  ""
  "%0 = %h1 * %d2 (IS);"
  [(set_attr "type" "dsp32")])

(define_insn "mulhisi_hl"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(mult:SI (sign_extend:SI
		  (vec_select:HI (match_operand:V2HI 1 "register_operand" "%d")
				 (parallel [(const_int 1)])))
		 (sign_extend:SI
		  (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				 (parallel [(const_int 0)])))))]
  ""
  "%0 = %d1 * %h2 (IS);"
  [(set_attr "type" "dsp32")])

(define_insn "mulhisi_hh"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(mult:SI (sign_extend:SI
		  (vec_select:HI (match_operand:V2HI 1 "register_operand" "%d")
				 (parallel [(const_int 1)])))
		 (sign_extend:SI
		  (vec_select:HI (match_operand:V2HI 2 "register_operand" "d")
				 (parallel [(const_int 1)])))))]
  ""
  "%0 = %d1 * %d2 (IS);"
  [(set_attr "type" "dsp32")])

(define_insn "ssnegv2hi2"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(ss_neg:V2HI (match_operand:V2HI 1 "register_operand" "d")))]
  ""
  "%0 = - %1 (V);"
  [(set_attr "type" "dsp32")])

(define_insn "absv2hi2"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(abs:V2HI (match_operand:V2HI 1 "register_operand" "d")))]
  ""
  "%0 = ABS %1 (V);"
  [(set_attr "type" "dsp32")])

;; Shifts.

(define_insn "ssashiftv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d,d,d")
	(if_then_else:V2HI
	 (lt (match_operand:SI 2 "vec_shift_operand" "d,Ku4,Ks4") (const_int 0))
	 (ashiftrt:V2HI (match_operand:V2HI 1 "register_operand" "d,d,d")
			(match_dup 2))
	 (ss_ashift:V2HI (match_dup 1) (match_dup 2))))]
  ""
  "@
   %0 = ASHIFT %1 BY %2 (V, S);
   %0 = %1 >>> %2 (V,S);
   %0 = %1 << %2 (V,S);"
  [(set_attr "type" "dsp32")])

(define_insn "ssashifthi3"
  [(set (match_operand:HI 0 "register_operand" "=d,d,d")
	(if_then_else:HI
	 (lt (match_operand:SI 2 "vec_shift_operand" "d,Ku4,Ks4") (const_int 0))
	 (ashiftrt:HI (match_operand:HI 1 "register_operand" "d,d,d")
		      (match_dup 2))
	 (ss_ashift:HI (match_dup 1) (match_dup 2))))]
  ""
  "@
   %0 = ASHIFT %1 BY %2 (V, S);
   %0 = %1 >>> %2 (V,S);
   %0 = %1 << %2 (V,S);"
  [(set_attr "type" "dsp32")])

(define_insn "lshiftv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=d,d,d")
	(if_then_else:V2HI
	 (lt (match_operand:SI 2 "vec_shift_operand" "d,Ku4,Ks4") (const_int 0))
	 (lshiftrt:V2HI (match_operand:V2HI 1 "register_operand" "d,d,d")
			(match_dup 2))
	 (ashift:V2HI (match_dup 1) (match_dup 2))))]
  ""
  "@
   %0 = LSHIFT %1 BY %2 (V);
   %0 = %1 >> %2 (V);
   %0 = %1 << %2 (V);"
  [(set_attr "type" "dsp32")])

(define_insn "lshifthi3"
  [(set (match_operand:HI 0 "register_operand" "=d,d,d")
	(if_then_else:HI
	 (lt (match_operand:SI 2 "vec_shift_operand" "d,Ku4,Ks4") (const_int 0))
	 (lshiftrt:HI (match_operand:HI 1 "register_operand" "d,d,d")
		      (match_dup 2))
	 (ashift:HI (match_dup 1) (match_dup 2))))]
  ""
  "@
   %0 = LSHIFT %1 BY %2 (V);
   %0 = %1 >> %2 (V);
   %0 = %1 << %2 (V);"
  [(set_attr "type" "dsp32")])


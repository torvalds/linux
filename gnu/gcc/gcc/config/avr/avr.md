;; -*- Mode: Scheme -*-
;;   Machine description for GNU compiler,
;;   for ATMEL AVR micro controllers.
;;   Copyright (C) 1998, 1999, 2000, 2001, 2002, 2004, 2005, 2006, 2007
;;   Free Software Foundation, Inc.
;;   Contributed by Denis Chertykov (denisc@overta.ru)

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

;; Special characters after '%':
;;  A  No effect (add 0).
;;  B  Add 1 to REG number, MEM address or CONST_INT.
;;  C  Add 2.
;;  D  Add 3.
;;  j  Branch condition.
;;  k  Reverse branch condition.
;;  o  Displacement for (mem (plus (reg) (const_int))) operands.
;;  p  POST_INC or PRE_DEC address as a pointer (X, Y, Z)
;;  r  POST_INC or PRE_DEC address as a register (r26, r28, r30)
;;  ~  Output 'r' if not AVR_MEGA.

;; UNSPEC usage:
;;  0  Length of a string, see "strlenhi".
;;  1  Jump by register pair Z or by table addressed by Z, see "casesi".

(define_constants
  [(REG_X	26)
   (REG_Y	28)
   (REG_Z	30)
   (REG_W	24)
   (REG_SP	32)
   (TMP_REGNO	0)	; temporary register r0
   (ZERO_REGNO	1)	; zero register r1
   (UNSPEC_STRLEN	0)
   (UNSPEC_INDEX_JMP	1)])

(include "predicates.md")
(include "constraints.md")
  
;; Condition code settings.
(define_attr "cc" "none,set_czn,set_zn,set_n,compare,clobber"
  (const_string "none"))

(define_attr "type" "branch,branch1,arith,xcall"
  (const_string "arith"))

(define_attr "mcu_have_movw" "yes,no"
  (const (if_then_else (symbol_ref "AVR_HAVE_MOVW")
		       (const_string "yes")
		       (const_string "no"))))

(define_attr "mcu_mega" "yes,no"
  (const (if_then_else (symbol_ref "AVR_MEGA")
		       (const_string "yes")
		       (const_string "no"))))
  

;; The size of instructions in bytes.
;; XXX may depend from "cc"

(define_attr "length" ""
  (cond [(eq_attr "type" "branch")
         (if_then_else (and (ge (minus (pc) (match_dup 0))
                                (const_int -63))
                            (le (minus (pc) (match_dup 0))
                                (const_int 62)))
                       (const_int 1)
                       (if_then_else (and (ge (minus (pc) (match_dup 0))
                                              (const_int -2045))
                                          (le (minus (pc) (match_dup 0))
                                              (const_int 2045)))
                                     (const_int 2)
                                     (const_int 3)))
         (eq_attr "type" "branch1")
         (if_then_else (and (ge (minus (pc) (match_dup 0))
                                (const_int -62))
                            (le (minus (pc) (match_dup 0))
                                (const_int 61)))
                       (const_int 2)
                       (if_then_else (and (ge (minus (pc) (match_dup 0))
                                              (const_int -2044))
                                          (le (minus (pc) (match_dup 0))
                                              (const_int 2043)))
                                     (const_int 3)
                                     (const_int 4)))
	 (eq_attr "type" "xcall")
	 (if_then_else (eq_attr "mcu_mega" "no")
		       (const_int 1)
		       (const_int 2))]
        (const_int 2)))

(define_insn "*pop1"
  [(set (reg:HI 32) (plus:HI (reg:HI 32) (const_int 1)))]
  ""
  "pop __tmp_reg__"
  [(set_attr "length" "1")])

(define_insn "*pop2"
  [(set (reg:HI 32) (plus:HI (reg:HI 32) (const_int 2)))]
  ""
  "pop __tmp_reg__
	pop __tmp_reg__"
  [(set_attr "length" "2")])

(define_insn "*pop3"
  [(set (reg:HI 32) (plus:HI (reg:HI 32) (const_int 3)))]
  ""
  "pop __tmp_reg__
	pop __tmp_reg__
 	pop __tmp_reg__"
  [(set_attr "length" "3")])

(define_insn "*pop4"
  [(set (reg:HI 32) (plus:HI (reg:HI 32) (const_int 4)))]
  ""
  "pop __tmp_reg__
	pop __tmp_reg__
	pop __tmp_reg__
	pop __tmp_reg__"
  [(set_attr "length" "4")])

(define_insn "*pop5"
  [(set (reg:HI 32) (plus:HI (reg:HI 32) (const_int 5)))]
  ""
  "pop __tmp_reg__
	pop __tmp_reg__
	pop __tmp_reg__
	pop __tmp_reg__
	pop __tmp_reg__"
  [(set_attr "length" "5")])

(define_insn "*pushqi"
  [(set (mem:QI (post_dec (reg:HI 32)))
        (match_operand:QI 0 "nonmemory_operand" "r,L"))]
  "(operands[0] == const0_rtx || register_operand (operands[0], QImode))"
  "@
	push %0
	push __zero_reg__"
  [(set_attr "length" "1,1")])


(define_insn "*pushhi"
  [(set (mem:HI (post_dec (reg:HI 32)))
        (match_operand:HI 0 "nonmemory_operand" "r,L"))]
  "(operands[0] == const0_rtx || register_operand (operands[0], HImode))"
  "@
	push %B0\;push %A0
	push __zero_reg__\;push __zero_reg__"
  [(set_attr "length" "2,2")])

(define_insn "*pushsi"
  [(set (mem:SI (post_dec (reg:HI 32)))
        (match_operand:SI 0 "nonmemory_operand" "r,L"))]
  "(operands[0] == const0_rtx || register_operand (operands[0], SImode))"
  "@
	push %D0\;push %C0\;push %B0\;push %A0
	push __zero_reg__\;push __zero_reg__\;push __zero_reg__\;push __zero_reg__"
  [(set_attr "length" "4,4")])

(define_insn "*pushsf"
  [(set (mem:SF (post_dec (reg:HI 32)))
        (match_operand:SF 0 "register_operand" "r"))]
  ""
  "push %D0
	push %C0
	push %B0
	push %A0"
  [(set_attr "length" "4")])

;;========================================================================
;; move byte
;; The last alternative (any immediate constant to any register) is
;; very expensive.  It should be optimized by peephole2 if a scratch
;; register is available, but then that register could just as well be
;; allocated for the variable we are loading.  But, most of NO_LD_REGS
;; are call-saved registers, and most of LD_REGS are call-used registers,
;; so this may still be a win for registers live across function calls.

(define_expand "movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand" "")
	(match_operand:QI 1 "general_operand" ""))]
  ""
  "/* One of the ops has to be in a register.  */
   if (!register_operand(operand0, QImode)
       && ! (register_operand(operand1, QImode) || const0_rtx == operand1))
       operands[1] = copy_to_mode_reg(QImode, operand1);
  ")

(define_insn "*movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=r,d,Qm,r,q,r,*r")
	(match_operand:QI 1 "general_operand"       "r,i,rL,Qm,r,q,i"))]
  "(register_operand (operands[0],QImode)
    || register_operand (operands[1], QImode) || const0_rtx == operands[1])"
  "* return output_movqi (insn, operands, NULL);"
  [(set_attr "length" "1,1,5,5,1,1,4")
   (set_attr "cc" "none,none,clobber,clobber,none,none,clobber")])

;; This is used in peephole2 to optimize loading immediate constants
;; if a scratch register from LD_REGS happens to be available.

(define_insn "*reload_inqi"
  [(set (match_operand:QI 0 "register_operand" "=l")
	(match_operand:QI 1 "immediate_operand" "i"))
   (clobber (match_operand:QI 2 "register_operand" "=&d"))]
  "reload_completed"
  "ldi %2,lo8(%1)
	mov %0,%2"
  [(set_attr "length" "2")
   (set_attr "cc" "none")])

(define_peephole2
  [(match_scratch:QI 2 "d")
   (set (match_operand:QI 0 "l_register_operand" "")
	(match_operand:QI 1 "immediate_operand" ""))]
  "(operands[1] != const0_rtx
    && operands[1] != const1_rtx
    && operands[1] != constm1_rtx)"
  [(parallel [(set (match_dup 0) (match_dup 1))
	      (clobber (match_dup 2))])]
  "if (!avr_peep2_scratch_safe (operands[2]))
     FAIL;")

;;============================================================================
;; move word (16 bit)

(define_expand "movhi"
  [(set (match_operand:HI 0 "nonimmediate_operand" "")
        (match_operand:HI 1 "general_operand"       ""))]
  ""
  "
{
   /* One of the ops has to be in a register.  */
  if (!register_operand(operand0, HImode)
      && !(register_operand(operand1, HImode) || const0_rtx == operands[1]))
    {
      operands[1] = copy_to_mode_reg(HImode, operand1);
    }
}")


(define_peephole2
  [(match_scratch:QI 2 "d")
   (set (match_operand:HI 0 "l_register_operand" "")
        (match_operand:HI 1 "immediate_operand" ""))]
  "(operands[1] != const0_rtx 
    && operands[1] != constm1_rtx)"
  [(parallel [(set (match_dup 0) (match_dup 1))
	      (clobber (match_dup 2))])]
  "if (!avr_peep2_scratch_safe (operands[2]))
     FAIL;")

;; '*' because it is not used in rtl generation, only in above peephole
(define_insn "*reload_inhi"
  [(set (match_operand:HI 0 "register_operand" "=r")
        (match_operand:HI 1 "immediate_operand" "i"))
   (clobber (match_operand:QI 2 "register_operand" "=&d"))]
  "reload_completed"
  "* return output_reload_inhi (insn, operands, NULL);"
  [(set_attr "length" "4")
   (set_attr "cc" "none")])

(define_insn "*movhi"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,r,m,d,*r,q,r")
        (match_operand:HI 1 "general_operand"       "r,m,rL,i,i,r,q"))]
  "(register_operand (operands[0],HImode)
    || register_operand (operands[1],HImode) || const0_rtx == operands[1])"
  "* return output_movhi (insn, operands, NULL);"
  [(set_attr "length" "2,6,7,2,6,5,2")
   (set_attr "cc" "none,clobber,clobber,none,clobber,none,none")])

;;==========================================================================
;; move double word (32 bit)

(define_expand "movsi"
  [(set (match_operand:SI 0 "nonimmediate_operand" "")
        (match_operand:SI 1 "general_operand"  ""))]
  ""
  "
{
  /* One of the ops has to be in a register.  */
  if (!register_operand (operand0, SImode)
      && !(register_operand (operand1, SImode) || const0_rtx == operand1))
    {
      operands[1] = copy_to_mode_reg (SImode, operand1);
    }
}")



(define_peephole2
  [(match_scratch:QI 2 "d")
   (set (match_operand:SI 0 "l_register_operand" "")
        (match_operand:SI 1 "immediate_operand" ""))]
  "(operands[1] != const0_rtx
    && operands[1] != constm1_rtx)"
  [(parallel [(set (match_dup 0) (match_dup 1))
	      (clobber (match_dup 2))])]
  "if (!avr_peep2_scratch_safe (operands[2]))
     FAIL;")

;; '*' because it is not used in rtl generation.
(define_insn "*reload_insi"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (match_operand:SI 1 "immediate_operand" "i"))
   (clobber (match_operand:QI 2 "register_operand" "=&d"))]
  "reload_completed"
  "* return output_reload_insisf (insn, operands, NULL);"
  [(set_attr "length" "8")
   (set_attr "cc" "none")])


(define_insn "*movsi"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,r,r,Qm,!d,r")
        (match_operand:SI 1 "general_operand"       "r,L,Qm,rL,i,i"))]
  "(register_operand (operands[0],SImode)
    || register_operand (operands[1],SImode) || const0_rtx == operands[1])"
  "* return output_movsisf (insn, operands, NULL);"
  [(set_attr "length" "4,4,8,9,4,10")
   (set_attr "cc" "none,set_zn,clobber,clobber,none,clobber")])

;; fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
;; move floating point numbers (32 bit)

(define_expand "movsf"
  [(set (match_operand:SF 0 "nonimmediate_operand" "")
        (match_operand:SF 1 "general_operand"  ""))]
  ""
  "
{
  /* One of the ops has to be in a register.  */
  if (!register_operand (operand1, SFmode)
      && !register_operand (operand0, SFmode))
    {
      operands[1] = copy_to_mode_reg (SFmode, operand1);
    }
}")

(define_insn "*movsf"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=r,r,r,Qm,!d,r")
        (match_operand:SF 1 "general_operand"       "r,G,Qm,r,F,F"))]
  "register_operand (operands[0], SFmode)
   || register_operand (operands[1], SFmode)"
  "* return output_movsisf (insn, operands, NULL);"
  [(set_attr "length" "4,4,8,9,4,10")
   (set_attr "cc" "none,set_zn,clobber,clobber,none,clobber")])

;;=========================================================================
;; move string (like memcpy)
;; implement as RTL loop

(define_expand "movmemhi"
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
          (match_operand:BLK 1 "memory_operand" ""))
          (use (match_operand:HI 2 "const_int_operand" ""))
          (use (match_operand:HI 3 "const_int_operand" ""))])]
  ""
  "{
  int prob;
  HOST_WIDE_INT count;
  enum machine_mode mode;
  rtx label = gen_label_rtx ();
  rtx loop_reg;
  rtx jump;

  /* Copy pointers into new psuedos - they will be changed.  */
  rtx addr0 = copy_to_mode_reg (Pmode, XEXP (operands[0], 0));
  rtx addr1 = copy_to_mode_reg (Pmode, XEXP (operands[1], 0));

  /* Create rtx for tmp register - we use this as scratch.  */
  rtx tmp_reg_rtx  = gen_rtx_REG (QImode, TMP_REGNO);

  if (GET_CODE (operands[2]) != CONST_INT)
    FAIL;

  count = INTVAL (operands[2]);
  if (count <= 0)
    FAIL;

  /* Work out branch probability for latter use.  */
  prob = REG_BR_PROB_BASE - REG_BR_PROB_BASE / count;

  /* See if constant fit 8 bits.  */
  mode = (count < 0x100) ? QImode : HImode;
  /* Create loop counter register.  */
  loop_reg = copy_to_mode_reg (mode, gen_int_mode (count, mode));

  /* Now create RTL code for move loop.  */
  /* Label at top of loop.  */
  emit_label (label);

  /* Move one byte into scratch and inc pointer.  */
  emit_move_insn (tmp_reg_rtx, gen_rtx_MEM (QImode, addr1));
  emit_move_insn (addr1, gen_rtx_PLUS (Pmode, addr1, const1_rtx));

  /* Move to mem and inc pointer.  */
  emit_move_insn (gen_rtx_MEM (QImode, addr0), tmp_reg_rtx);
  emit_move_insn (addr0, gen_rtx_PLUS (Pmode, addr0, const1_rtx));

  /* Decrement count.  */
  emit_move_insn (loop_reg, gen_rtx_PLUS (mode, loop_reg, constm1_rtx));

  /* Compare with zero and jump if not equal. */
  emit_cmp_and_jump_insns (loop_reg, const0_rtx, NE, NULL_RTX, mode, 1,
                           label);
  /* Set jump probability based on loop count.  */
  jump = get_last_insn ();
  REG_NOTES (jump) = gen_rtx_EXPR_LIST (REG_BR_PROB,
                    GEN_INT (prob),
                    REG_NOTES (jump));
  DONE;
}")

;; =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2 =%2
;; memset (%0, %2, %1)

(define_expand "setmemhi"
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
 		   (match_operand 2 "const_int_operand" ""))
	      (use (match_operand:HI 1 "const_int_operand" ""))
	      (use (match_operand:HI 3 "const_int_operand" "n"))
	      (clobber (match_scratch:HI 4 ""))
	      (clobber (match_dup 5))])]
  ""
  "{
  rtx addr0;
  int cnt8;
  enum machine_mode mode;

  /* If value to set is not zero, use the library routine.  */
  if (operands[2] != const0_rtx)
    FAIL;

  if (GET_CODE (operands[1]) != CONST_INT)
    FAIL;

  cnt8 = byte_immediate_operand (operands[1], GET_MODE (operands[1]));
  mode = cnt8 ? QImode : HImode;
  operands[5] = gen_rtx_SCRATCH (mode);
  operands[1] = copy_to_mode_reg (mode,
                                  gen_int_mode (INTVAL (operands[1]), mode));
  addr0 = copy_to_mode_reg (Pmode, XEXP (operands[0], 0));
  operands[0] = gen_rtx_MEM (BLKmode, addr0);
}")

(define_insn "*clrmemqi"
  [(set (mem:BLK (match_operand:HI 0 "register_operand" "e"))
	(const_int 0))
   (use (match_operand:QI 1 "register_operand" "r"))
   (use (match_operand:QI 2 "const_int_operand" "n"))
   (clobber (match_scratch:HI 3 "=0"))
   (clobber (match_scratch:QI 4 "=&1"))]
  ""
  "st %a0+,__zero_reg__
        dec %1
	brne .-6"
  [(set_attr "length" "3")
   (set_attr "cc" "clobber")])

(define_insn "*clrmemhi"
  [(set (mem:BLK (match_operand:HI 0 "register_operand" "e,e"))
	(const_int 0))
   (use (match_operand:HI 1 "register_operand" "!w,d"))
   (use (match_operand:HI 2 "const_int_operand" "n,n"))
   (clobber (match_scratch:HI 3 "=0,0"))
   (clobber (match_scratch:HI 4 "=&1,&1"))]
  ""
  "*{
     if (which_alternative==0)
       return (AS2 (st,%a0+,__zero_reg__) CR_TAB
	       AS2 (sbiw,%A1,1) CR_TAB
	       AS1 (brne,.-6));
     else
       return (AS2 (st,%a0+,__zero_reg__) CR_TAB
	       AS2 (subi,%A1,1) CR_TAB
	       AS2 (sbci,%B1,0) CR_TAB
	       AS1 (brne,.-8));
}"
  [(set_attr "length" "3,4")
   (set_attr "cc" "clobber,clobber")])

(define_expand "strlenhi"
    [(set (match_dup 4)
	  (unspec:HI [(match_operand:BLK 1 "memory_operand" "")
		      (match_operand:QI 2 "const_int_operand" "")
		      (match_operand:HI 3 "immediate_operand" "")]
		     UNSPEC_STRLEN))
     (set (match_dup 4) (plus:HI (match_dup 4)
				 (const_int -1)))
     (set (match_operand:HI 0 "register_operand" "")
	  (minus:HI (match_dup 4)
		    (match_dup 5)))]
   ""
   "{
  rtx addr;
  if (! (GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) == 0))
    FAIL;
  addr = copy_to_mode_reg (Pmode, XEXP (operands[1],0));
  operands[1] = gen_rtx_MEM (BLKmode, addr); 
  operands[5] = addr;
  operands[4] = gen_reg_rtx (HImode);
}")

(define_insn "*strlenhi"
  [(set (match_operand:HI 0 "register_operand" "=e")
	(unspec:HI [(mem:BLK (match_operand:HI 1 "register_operand" "%0"))
		    (const_int 0)
		    (match_operand:HI 2 "immediate_operand" "i")]
		   UNSPEC_STRLEN))]
  ""
  "ld __tmp_reg__,%a0+
	tst __tmp_reg__
	brne .-6"
  [(set_attr "length" "3")
   (set_attr "cc" "clobber")])

;+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
; add bytes

(define_insn "addqi3"
  [(set (match_operand:QI 0 "register_operand" "=r,d,r,r")
        (plus:QI (match_operand:QI 1 "register_operand" "%0,0,0,0")
                 (match_operand:QI 2 "nonmemory_operand" "r,i,P,N")))]
  ""
  "@
	add %0,%2
	subi %0,lo8(-(%2))
	inc %0
	dec %0"
  [(set_attr "length" "1,1,1,1")
   (set_attr "cc" "set_czn,set_czn,set_zn,set_zn")])


(define_expand "addhi3"
  [(set (match_operand:HI 0 "register_operand" "")
	(plus:HI (match_operand:HI 1 "register_operand" "")
		 (match_operand:HI 2 "nonmemory_operand" "")))]
  ""
  "
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      short tmp = INTVAL (operands[2]);
      operands[2] = GEN_INT(tmp);
    }
}")


(define_insn "*addhi3_zero_extend"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(plus:HI (zero_extend:HI
		  (match_operand:QI 1 "register_operand" "r"))
		 (match_operand:HI 2 "register_operand" "0")))]
  ""
  "add %A0,%1
	adc %B0,__zero_reg__"
  [(set_attr "length" "2")
   (set_attr "cc" "set_n")])

(define_insn "*addhi3_zero_extend1"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(plus:HI (match_operand:HI 1 "register_operand" "%0")
		 (zero_extend:HI
		  (match_operand:QI 2 "register_operand" "r"))))]
  ""
  "add %A0,%2
	adc %B0,__zero_reg__"
  [(set_attr "length" "2")
   (set_attr "cc" "set_n")])

(define_insn "*addhi3_zero_extend2"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(plus:HI
	 (zero_extend:HI (match_operand:QI 1 "register_operand" "%0"))
	 (zero_extend:HI (match_operand:QI 2 "register_operand" "r"))))]
  ""
  "add %0,%2
	mov %B0,__zero_reg__
	adc %B0,__zero_reg__"
  [(set_attr "length" "3")
   (set_attr "cc" "set_n")])

(define_insn "*addhi3"
  [(set (match_operand:HI 0 "register_operand" "=r,!w,!w,d,r,r")
 	(plus:HI
 	 (match_operand:HI 1 "register_operand" "%0,0,0,0,0,0")
 	 (match_operand:HI 2 "nonmemory_operand" "r,I,J,i,P,N")))]
  ""
  "@
 	add %A0,%A2\;adc %B0,%B2
 	adiw %A0,%2
 	sbiw %A0,%n2
 	subi %A0,lo8(-(%2))\;sbci %B0,hi8(-(%2))
 	sec\;adc %A0,__zero_reg__\;adc %B0,__zero_reg__
 	sec\;sbc %A0,__zero_reg__\;sbc %B0,__zero_reg__"
  [(set_attr "length" "2,1,1,2,3,3")
   (set_attr "cc" "set_n,set_czn,set_czn,set_czn,set_n,set_n")])

(define_insn "addsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,!w,!w,d,r,r")
	  (plus:SI
	   (match_operand:SI 1 "register_operand" "%0,0,0,0,0,0")
	   (match_operand:SI 2 "nonmemory_operand" "r,I,J,i,P,N")))]
  ""
  "@
	add %A0,%A2\;adc %B0,%B2\;adc %C0,%C2\;adc %D0,%D2
	adiw %0,%2\;adc %C0,__zero_reg__\;adc %D0,__zero_reg__
	sbiw %0,%n2\;sbc %C0,__zero_reg__\;sbc %D0,__zero_reg__
	subi %0,lo8(-(%2))\;sbci %B0,hi8(-(%2))\;sbci %C0,hlo8(-(%2))\;sbci %D0,hhi8(-(%2))
	sec\;adc %A0,__zero_reg__\;adc %B0,__zero_reg__\;adc %C0,__zero_reg__\;adc %D0,__zero_reg__
	sec\;sbc %A0,__zero_reg__\;sbc %B0,__zero_reg__\;sbc %C0,__zero_reg__\;sbc %D0,__zero_reg__"
  [(set_attr "length" "4,3,3,4,5,5")
   (set_attr "cc" "set_n,set_n,set_czn,set_czn,set_n,set_n")])

(define_insn "*addsi3_zero_extend"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (zero_extend:SI
		  (match_operand:QI 1 "register_operand" "r"))
		 (match_operand:SI 2 "register_operand" "0")))]
  ""
  "add %A0,%1
	adc %B0,__zero_reg__
	adc %C0,__zero_reg__
	adc %D0,__zero_reg__"
  [(set_attr "length" "4")
   (set_attr "cc" "set_n")])

;-----------------------------------------------------------------------------
; sub bytes
(define_insn "subqi3"
  [(set (match_operand:QI 0 "register_operand" "=r,d")
        (minus:QI (match_operand:QI 1 "register_operand" "0,0")
                  (match_operand:QI 2 "nonmemory_operand" "r,i")))]
  ""
  "@
	sub %0,%2
	subi %0,lo8(%2)"
  [(set_attr "length" "1,1")
   (set_attr "cc" "set_czn,set_czn")])

(define_insn "subhi3"
  [(set (match_operand:HI 0 "register_operand" "=r,d")
        (minus:HI (match_operand:HI 1 "register_operand" "0,0")
		  (match_operand:HI 2 "nonmemory_operand" "r,i")))]
  ""
  "@
	sub %A0,%A2\;sbc %B0,%B2
	subi %A0,lo8(%2)\;sbci %B0,hi8(%2)"
  [(set_attr "length" "2,2")
   (set_attr "cc" "set_czn,set_czn")])

(define_insn "*subhi3_zero_extend1"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(minus:HI (match_operand:HI 1 "register_operand" "0")
		  (zero_extend:HI
		   (match_operand:QI 2 "register_operand" "r"))))]
  ""
  "sub %A0,%2
	sbc %B0,__zero_reg__"
  [(set_attr "length" "2")
   (set_attr "cc" "set_n")])

(define_insn "subsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,d")
        (minus:SI (match_operand:SI 1 "register_operand" "0,0")
                 (match_operand:SI 2 "nonmemory_operand" "r,i")))]
  ""
  "@
	sub %0,%2\;sbc %B0,%B2\;sbc %C0,%C2\;sbc %D0,%D2
	subi %A0,lo8(%2)\;sbci %B0,hi8(%2)\;sbci %C0,hlo8(%2)\;sbci %D0,hhi8(%2)"
  [(set_attr "length" "4,4")
   (set_attr "cc" "set_czn,set_czn")])

(define_insn "*subsi3_zero_extend"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_operand:SI 1 "register_operand" "0")
		  (zero_extend:SI
		   (match_operand:QI 2 "register_operand" "r"))))]
  ""
  "sub %A0,%2
	sbc %B0,__zero_reg__
	sbc %C0,__zero_reg__
	sbc %D0,__zero_reg__"
  [(set_attr "length" "4")
   (set_attr "cc" "set_n")])

;******************************************************************************
; mul

(define_expand "mulqi3"
  [(set (match_operand:QI 0 "register_operand" "")
	(mult:QI (match_operand:QI 1 "register_operand" "")
		 (match_operand:QI 2 "register_operand" "")))]
  ""
  "{
  if (!AVR_ENHANCED)
    {
      emit_insn (gen_mulqi3_call (operands[0], operands[1], operands[2]));
      DONE;
    }
}")

(define_insn "*mulqi3_enh"
  [(set (match_operand:QI 0 "register_operand" "=r")
	(mult:QI (match_operand:QI 1 "register_operand" "r")
		 (match_operand:QI 2 "register_operand" "r")))]
  "AVR_ENHANCED"
  "mul %1,%2
	mov %0,r0
	clr r1"
  [(set_attr "length" "3")
   (set_attr "cc" "clobber")])

(define_expand "mulqi3_call"
  [(set (reg:QI 24) (match_operand:QI 1 "register_operand" ""))
   (set (reg:QI 22) (match_operand:QI 2 "register_operand" ""))
   (parallel [(set (reg:QI 24) (mult:QI (reg:QI 24) (reg:QI 22)))
	      (clobber (reg:QI 22))])
   (set (match_operand:QI 0 "register_operand" "") (reg:QI 24))]
  ""
  "")

(define_insn "*mulqi3_call"
  [(set (reg:QI 24) (mult:QI (reg:QI 24) (reg:QI 22)))
   (clobber (reg:QI 22))]
  "!AVR_ENHANCED"
  "%~call __mulqi3"
  [(set_attr "type" "xcall")
   (set_attr "cc" "clobber")])

(define_insn "mulqihi3"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(mult:HI (sign_extend:HI (match_operand:QI 1 "register_operand" "d"))
		 (sign_extend:HI (match_operand:QI 2 "register_operand" "d"))))]
  "AVR_ENHANCED"
  "muls %1,%2
	movw %0,r0
	clr r1"
  [(set_attr "length" "3")
   (set_attr "cc" "clobber")])

(define_insn "umulqihi3"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(mult:HI (zero_extend:HI (match_operand:QI 1 "register_operand" "r"))
		 (zero_extend:HI (match_operand:QI 2 "register_operand" "r"))))]
  "AVR_ENHANCED"
  "mul %1,%2
	movw %0,r0
	clr r1"
  [(set_attr "length" "3")
   (set_attr "cc" "clobber")])

(define_expand "mulhi3"
  [(set (match_operand:HI 0 "register_operand" "")
	(mult:HI (match_operand:HI 1 "register_operand" "")
		 (match_operand:HI 2 "register_operand" "")))]
  ""
  "
{
  if (!AVR_ENHANCED)
    {
      emit_insn (gen_mulhi3_call (operands[0], operands[1], operands[2]));
      DONE;
    }
}")

(define_insn "*mulhi3_enh"
  [(set (match_operand:HI 0 "register_operand" "=&r")
	(mult:HI (match_operand:HI 1 "register_operand" "r")
		 (match_operand:HI 2 "register_operand" "r")))]
  "AVR_ENHANCED"
  "mul %A1,%A2
	movw %0,r0
	mul %A1,%B2
	add %B0,r0
	mul %B1,%A2
	add %B0,r0
	clr r1"
  [(set_attr "length" "7")
   (set_attr "cc" "clobber")])

(define_expand "mulhi3_call"
  [(set (reg:HI 24) (match_operand:HI 1 "register_operand" ""))
   (set (reg:HI 22) (match_operand:HI 2 "register_operand" ""))
   (parallel [(set (reg:HI 24) (mult:HI (reg:HI 24) (reg:HI 22)))
	      (clobber (reg:HI 22))
	      (clobber (reg:QI 21))])
   (set (match_operand:HI 0 "register_operand" "") (reg:HI 24))]
  ""
  "")

(define_insn "*mulhi3_call"
  [(set (reg:HI 24) (mult:HI (reg:HI 24) (reg:HI 22)))
   (clobber (reg:HI 22))
   (clobber (reg:QI 21))]
  "!AVR_ENHANCED"
  "%~call __mulhi3"
  [(set_attr "type" "xcall")
   (set_attr "cc" "clobber")])

;; Operand 2 (reg:SI 18) not clobbered on the enhanced core.
;; All call-used registers clobbered otherwise - normal library call.
(define_expand "mulsi3"
  [(set (reg:SI 22) (match_operand:SI 1 "register_operand" ""))
   (set (reg:SI 18) (match_operand:SI 2 "register_operand" ""))
   (parallel [(set (reg:SI 22) (mult:SI (reg:SI 22) (reg:SI 18)))
	      (clobber (reg:HI 26))
	      (clobber (reg:HI 30))])
   (set (match_operand:SI 0 "register_operand" "") (reg:SI 22))]
  "AVR_ENHANCED"
  "")

(define_insn "*mulsi3_call"
  [(set (reg:SI 22) (mult:SI (reg:SI 22) (reg:SI 18)))
   (clobber (reg:HI 26))
   (clobber (reg:HI 30))]
  "AVR_ENHANCED"
  "%~call __mulsi3"
  [(set_attr "type" "xcall")
   (set_attr "cc" "clobber")])

; / % / % / % / % / % / % / % / % / % / % / % / % / % / % / % / % / % / % / %
; divmod

;; Generate libgcc.S calls ourselves, because:
;;  - we know exactly which registers are clobbered (for QI and HI
;;    modes, some of the call-used registers are preserved)
;;  - we get both the quotient and the remainder at no extra cost

(define_expand "divmodqi4"
  [(set (reg:QI 24) (match_operand:QI 1 "register_operand" ""))
   (set (reg:QI 22) (match_operand:QI 2 "register_operand" ""))
   (parallel [(set (reg:QI 24) (div:QI (reg:QI 24) (reg:QI 22)))
	      (set (reg:QI 25) (mod:QI (reg:QI 24) (reg:QI 22)))
	      (clobber (reg:QI 22))
	      (clobber (reg:QI 23))])
   (set (match_operand:QI 0 "register_operand" "") (reg:QI 24))
   (set (match_operand:QI 3 "register_operand" "") (reg:QI 25))]
  ""
  "")

(define_insn "*divmodqi4_call"
  [(set (reg:QI 24) (div:QI (reg:QI 24) (reg:QI 22)))
   (set (reg:QI 25) (mod:QI (reg:QI 24) (reg:QI 22)))
   (clobber (reg:QI 22))
   (clobber (reg:QI 23))]
  ""
  "%~call __divmodqi4"
  [(set_attr "type" "xcall")
   (set_attr "cc" "clobber")])

(define_expand "udivmodqi4"
  [(set (reg:QI 24) (match_operand:QI 1 "register_operand" ""))
   (set (reg:QI 22) (match_operand:QI 2 "register_operand" ""))
   (parallel [(set (reg:QI 24) (udiv:QI (reg:QI 24) (reg:QI 22)))
	      (set (reg:QI 25) (umod:QI (reg:QI 24) (reg:QI 22)))
	      (clobber (reg:QI 23))])
   (set (match_operand:QI 0 "register_operand" "") (reg:QI 24))
   (set (match_operand:QI 3 "register_operand" "") (reg:QI 25))]
  ""
  "")

(define_insn "*udivmodqi4_call"
  [(set (reg:QI 24) (udiv:QI (reg:QI 24) (reg:QI 22)))
   (set (reg:QI 25) (umod:QI (reg:QI 24) (reg:QI 22)))
   (clobber (reg:QI 23))]
  ""
  "%~call __udivmodqi4"
  [(set_attr "type" "xcall")
   (set_attr "cc" "clobber")])

(define_expand "divmodhi4"
  [(set (reg:HI 24) (match_operand:HI 1 "register_operand" ""))
   (set (reg:HI 22) (match_operand:HI 2 "register_operand" ""))
   (parallel [(set (reg:HI 22) (div:HI (reg:HI 24) (reg:HI 22)))
	      (set (reg:HI 24) (mod:HI (reg:HI 24) (reg:HI 22)))
	      (clobber (reg:HI 26))
	      (clobber (reg:QI 21))])
   (set (match_operand:HI 0 "register_operand" "") (reg:HI 22))
   (set (match_operand:HI 3 "register_operand" "") (reg:HI 24))]
  ""
  "")

(define_insn "*divmodhi4_call"
  [(set (reg:HI 22) (div:HI (reg:HI 24) (reg:HI 22)))
   (set (reg:HI 24) (mod:HI (reg:HI 24) (reg:HI 22)))
   (clobber (reg:HI 26))
   (clobber (reg:QI 21))]
  ""
  "%~call __divmodhi4"
  [(set_attr "type" "xcall")
   (set_attr "cc" "clobber")])

(define_expand "udivmodhi4"
  [(set (reg:HI 24) (match_operand:HI 1 "register_operand" ""))
   (set (reg:HI 22) (match_operand:HI 2 "register_operand" ""))
   (parallel [(set (reg:HI 22) (udiv:HI (reg:HI 24) (reg:HI 22)))
	      (set (reg:HI 24) (umod:HI (reg:HI 24) (reg:HI 22)))
	      (clobber (reg:HI 26))
	      (clobber (reg:QI 21))])
   (set (match_operand:HI 0 "register_operand" "") (reg:HI 22))
   (set (match_operand:HI 3 "register_operand" "") (reg:HI 24))]
  ""
  "")

(define_insn "*udivmodhi4_call"
  [(set (reg:HI 22) (udiv:HI (reg:HI 24) (reg:HI 22)))
   (set (reg:HI 24) (umod:HI (reg:HI 24) (reg:HI 22)))
   (clobber (reg:HI 26))
   (clobber (reg:QI 21))]
  ""
  "%~call __udivmodhi4"
  [(set_attr "type" "xcall")
   (set_attr "cc" "clobber")])

(define_expand "divmodsi4"
  [(set (reg:SI 22) (match_operand:SI 1 "register_operand" ""))
   (set (reg:SI 18) (match_operand:SI 2 "register_operand" ""))
   (parallel [(set (reg:SI 18) (div:SI (reg:SI 22) (reg:SI 18)))
	      (set (reg:SI 22) (mod:SI (reg:SI 22) (reg:SI 18)))
	      (clobber (reg:HI 26))
	      (clobber (reg:HI 30))])
   (set (match_operand:SI 0 "register_operand" "") (reg:SI 18))
   (set (match_operand:SI 3 "register_operand" "") (reg:SI 22))]
  ""
  "")

(define_insn "*divmodsi4_call"
  [(set (reg:SI 18) (div:SI (reg:SI 22) (reg:SI 18)))
   (set (reg:SI 22) (mod:SI (reg:SI 22) (reg:SI 18)))
   (clobber (reg:HI 26))
   (clobber (reg:HI 30))]
  ""
  "%~call __divmodsi4"
  [(set_attr "type" "xcall")
   (set_attr "cc" "clobber")])

(define_expand "udivmodsi4"
  [(set (reg:SI 22) (match_operand:SI 1 "register_operand" ""))
   (set (reg:SI 18) (match_operand:SI 2 "register_operand" ""))
   (parallel [(set (reg:SI 18) (udiv:SI (reg:SI 22) (reg:SI 18)))
	      (set (reg:SI 22) (umod:SI (reg:SI 22) (reg:SI 18)))
	      (clobber (reg:HI 26))
	      (clobber (reg:HI 30))])
   (set (match_operand:SI 0 "register_operand" "") (reg:SI 18))
   (set (match_operand:SI 3 "register_operand" "") (reg:SI 22))]
  ""
  "")

(define_insn "*udivmodsi4_call"
  [(set (reg:SI 18) (udiv:SI (reg:SI 22) (reg:SI 18)))
   (set (reg:SI 22) (umod:SI (reg:SI 22) (reg:SI 18)))
   (clobber (reg:HI 26))
   (clobber (reg:HI 30))]
  ""
  "%~call __udivmodsi4"
  [(set_attr "type" "xcall")
   (set_attr "cc" "clobber")])

;&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
; and

(define_insn "andqi3"
  [(set (match_operand:QI 0 "register_operand" "=r,d")
        (and:QI (match_operand:QI 1 "register_operand" "%0,0")
                (match_operand:QI 2 "nonmemory_operand" "r,i")))]
  ""
  "@
	and %0,%2
	andi %0,lo8(%2)"
  [(set_attr "length" "1,1")
   (set_attr "cc" "set_zn,set_zn")])

(define_insn "andhi3"
  [(set (match_operand:HI 0 "register_operand" "=r,d,r")
	  (and:HI (match_operand:HI 1 "register_operand" "%0,0,0")
		  (match_operand:HI 2 "nonmemory_operand" "r,i,M")))
   (clobber (match_scratch:QI 3 "=X,X,&d"))]
  ""
  "*{
  if (which_alternative==0)
    return (AS2 (and,%A0,%A2) CR_TAB
	    AS2 (and,%B0,%B2));
  else if (which_alternative==1)
    {
      if (GET_CODE (operands[2]) == CONST_INT)
        {
	  int mask = INTVAL (operands[2]);
	  if ((mask & 0xff) != 0xff)
	    output_asm_insn (AS2 (andi,%A0,lo8(%2)), operands);
	  if ((mask & 0xff00) != 0xff00)
	    output_asm_insn (AS2 (andi,%B0,hi8(%2)), operands);
	  return \"\";
        }
        return (AS2 (andi,%A0,lo8(%2)) CR_TAB
	        AS2 (andi,%B0,hi8(%2)));
     }
  return (AS2 (ldi,%3,lo8(%2)) CR_TAB
          AS2 (and,%A0,%3)     CR_TAB
          AS1 (clr,%B0));
}"
  [(set_attr "length" "2,2,3")
   (set_attr "cc" "set_n,clobber,set_n")])

(define_insn "andsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,d")
	(and:SI (match_operand:SI 1 "register_operand" "%0,0")
		(match_operand:SI 2 "nonmemory_operand" "r,i")))]
  ""
  "*{
  if (which_alternative==0)
    return (AS2 (and, %0,%2)   CR_TAB
	    AS2 (and, %B0,%B2) CR_TAB
	    AS2 (and, %C0,%C2) CR_TAB
	    AS2 (and, %D0,%D2));
  else if (which_alternative==1)
    {
      if (GET_CODE (operands[2]) == CONST_INT)
        {
	  HOST_WIDE_INT mask = INTVAL (operands[2]);
	  if ((mask & 0xff) != 0xff)
	    output_asm_insn (AS2 (andi,%A0,lo8(%2)), operands);
	  if ((mask & 0xff00) != 0xff00)
	    output_asm_insn (AS2 (andi,%B0,hi8(%2)), operands);
	  if ((mask & 0xff0000L) != 0xff0000L)
	    output_asm_insn (AS2 (andi,%C0,hlo8(%2)), operands);
	  if ((mask & 0xff000000L) != 0xff000000L)
	    output_asm_insn (AS2 (andi,%D0,hhi8(%2)), operands);
	  return \"\";
        }
      return (AS2 (andi, %A0,lo8(%2))  CR_TAB
              AS2 (andi, %B0,hi8(%2)) CR_TAB
	      AS2 (andi, %C0,hlo8(%2)) CR_TAB
	      AS2 (andi, %D0,hhi8(%2)));
    }
  return \"bug\";
}"
  [(set_attr "length" "4,4")
   (set_attr "cc" "set_n,set_n")])

;;|||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
;; ior

(define_insn "iorqi3"
  [(set (match_operand:QI 0 "register_operand" "=r,d")
        (ior:QI (match_operand:QI 1 "register_operand" "%0,0")
                (match_operand:QI 2 "nonmemory_operand" "r,i")))]
  ""
  "@
	or %0,%2
	ori %0,lo8(%2)"
  [(set_attr "length" "1,1")
   (set_attr "cc" "set_zn,set_zn")])

(define_insn "iorhi3"
  [(set (match_operand:HI 0 "register_operand" "=r,d")
	(ior:HI (match_operand:HI 1 "register_operand" "%0,0")
		(match_operand:HI 2 "nonmemory_operand" "r,i")))]
  ""
  "*{
  if (which_alternative==0)
    return (AS2 (or,%A0,%A2) CR_TAB
	    AS2 (or,%B0,%B2));
  if (GET_CODE (operands[2]) == CONST_INT)
     {
	int mask = INTVAL (operands[2]);
	if (mask & 0xff)
	  output_asm_insn (AS2 (ori,%A0,lo8(%2)), operands);
	if (mask & 0xff00)
	  output_asm_insn (AS2 (ori,%B0,hi8(%2)), operands);
	return \"\";
      }
   return (AS2 (ori,%0,lo8(%2)) CR_TAB
	   AS2 (ori,%B0,hi8(%2)));
}"  
  [(set_attr "length" "2,2")
   (set_attr "cc" "set_n,clobber")])

(define_insn "*iorhi3_clobber"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(ior:HI (match_operand:HI 1 "register_operand" "%0,0")
		(match_operand:HI 2 "immediate_operand" "M,i")))
   (clobber (match_scratch:QI 3 "=&d,&d"))]
  ""
  "@
	ldi %3,lo8(%2)\;or %A0,%3
	ldi %3,lo8(%2)\;or %A0,%3\;ldi %3,hi8(%2)\;or %B0,%3"
  [(set_attr "length" "2,4")
   (set_attr "cc" "clobber,set_n")])

(define_insn "iorsi3"
  [(set (match_operand:SI 0 "register_operand"        "=r,d")
	(ior:SI (match_operand:SI 1 "register_operand" "%0,0")
		(match_operand:SI 2 "nonmemory_operand" "r,i")))]
  ""
  "*{
  if (which_alternative==0)
    return (AS2 (or, %0,%2)   CR_TAB
	    AS2 (or, %B0,%B2) CR_TAB
	    AS2 (or, %C0,%C2) CR_TAB
	    AS2 (or, %D0,%D2));
  if (GET_CODE (operands[2]) == CONST_INT)
     {
	HOST_WIDE_INT mask = INTVAL (operands[2]);
	if (mask & 0xff)
	  output_asm_insn (AS2 (ori,%A0,lo8(%2)), operands);
	if (mask & 0xff00)
	  output_asm_insn (AS2 (ori,%B0,hi8(%2)), operands);
	if (mask & 0xff0000L)
	  output_asm_insn (AS2 (ori,%C0,hlo8(%2)), operands);
	if (mask & 0xff000000L)
	  output_asm_insn (AS2 (ori,%D0,hhi8(%2)), operands);
	return \"\";
      }
  return (AS2 (ori, %A0,lo8(%2))  CR_TAB
	  AS2 (ori, %B0,hi8(%2)) CR_TAB
	  AS2 (ori, %C0,hlo8(%2)) CR_TAB
	  AS2 (ori, %D0,hhi8(%2)));
}"
  [(set_attr "length" "4,4")
   (set_attr "cc" "set_n,clobber")])

(define_insn "*iorsi3_clobber"
  [(set (match_operand:SI 0 "register_operand"        "=r,r")
	(ior:SI (match_operand:SI 1 "register_operand" "%0,0")
		(match_operand:SI 2 "immediate_operand" "M,i")))
   (clobber (match_scratch:QI 3 "=&d,&d"))]
  ""
  "@
	ldi %3,lo8(%2)\;or %A0,%3
	ldi %3,lo8(%2)\;or %A0,%3\;ldi %3,hi8(%2)\;or %B0,%3\;ldi %3,hlo8(%2)\;or %C0,%3\;ldi %3,hhi8(%2)\;or %D0,%3"
  [(set_attr "length" "2,8")
   (set_attr "cc" "clobber,set_n")])

;;^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
;; xor

(define_insn "xorqi3"
  [(set (match_operand:QI 0 "register_operand" "=r")
        (xor:QI (match_operand:QI 1 "register_operand" "%0")
                (match_operand:QI 2 "register_operand" "r")))]
  ""
  "eor %0,%2"
  [(set_attr "length" "1")
   (set_attr "cc" "set_zn")])

(define_insn "xorhi3"
  [(set (match_operand:HI 0 "register_operand" "=r")
        (xor:HI (match_operand:HI 1 "register_operand" "%0")
                (match_operand:HI 2 "register_operand" "r")))]
  ""
  "eor %0,%2
	eor %B0,%B2"
  [(set_attr "length" "2")
   (set_attr "cc" "set_n")])

(define_insn "xorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (xor:SI (match_operand:SI 1 "register_operand" "%0")
                (match_operand:SI 2 "register_operand" "r")))]
  ""
  "eor %0,%2
	eor %B0,%B2
	eor %C0,%C2
	eor %D0,%D2"
  [(set_attr "length" "4")
   (set_attr "cc" "set_n")])

;;<< << << << << << << << << << << << << << << << << << << << << << << << << <<
;; arithmetic shift left

(define_insn "ashlqi3"
  [(set (match_operand:QI 0 "register_operand"           "=r,r,r,r,!d,r,r")
	(ashift:QI (match_operand:QI 1 "register_operand" "0,0,0,0,0,0,0")
		   (match_operand:QI 2 "general_operand"  "r,L,P,K,n,n,Qm")))]
  ""
  "* return ashlqi3_out (insn, operands, NULL);"
  [(set_attr "length" "5,0,1,2,4,6,9")
   (set_attr "cc" "clobber,none,set_czn,set_czn,set_czn,set_czn,clobber")])

(define_insn "ashlhi3"
  [(set (match_operand:HI 0 "register_operand"           "=r,r,r,r,r,r,r")
	(ashift:HI (match_operand:HI 1 "register_operand" "0,0,0,r,0,0,0")
		   (match_operand:QI 2 "general_operand"  "r,L,P,O,K,n,Qm")))]
  ""
  "* return ashlhi3_out (insn, operands, NULL);"
  [(set_attr "length" "6,0,2,2,4,10,10")
   (set_attr "cc" "clobber,none,set_n,clobber,set_n,clobber,clobber")])

(define_insn "ashlsi3"
  [(set (match_operand:SI 0 "register_operand"           "=r,r,r,r,r,r,r")
	(ashift:SI (match_operand:SI 1 "register_operand" "0,0,0,r,0,0,0")
		   (match_operand:QI 2 "general_operand"  "r,L,P,O,K,n,Qm")))]
  ""
  "* return ashlsi3_out (insn, operands, NULL);"
  [(set_attr "length" "8,0,4,4,8,10,12")
   (set_attr "cc" "clobber,none,set_n,clobber,set_n,clobber,clobber")])

;; Optimize if a scratch register from LD_REGS happens to be available.

(define_peephole2
  [(match_scratch:QI 3 "d")
   (set (match_operand:HI 0 "register_operand" "")
	(ashift:HI (match_operand:HI 1 "register_operand" "")
		   (match_operand:QI 2 "const_int_operand" "")))]
  ""
  [(parallel [(set (match_dup 0) (ashift:HI (match_dup 1) (match_dup 2)))
	      (clobber (match_dup 3))])]
  "if (!avr_peep2_scratch_safe (operands[3]))
     FAIL;")

(define_insn "*ashlhi3_const"
  [(set (match_operand:HI 0 "register_operand"            "=r,r,r,r,r")
	(ashift:HI (match_operand:HI 1 "register_operand"  "0,0,r,0,0")
		   (match_operand:QI 2 "const_int_operand" "L,P,O,K,n")))
   (clobber (match_scratch:QI 3 "=X,X,X,X,&d"))]
  "reload_completed"
  "* return ashlhi3_out (insn, operands, NULL);"
  [(set_attr "length" "0,2,2,4,10")
   (set_attr "cc" "none,set_n,clobber,set_n,clobber")])

(define_peephole2
  [(match_scratch:QI 3 "d")
   (set (match_operand:SI 0 "register_operand" "")
	(ashift:SI (match_operand:SI 1 "register_operand" "")
		   (match_operand:QI 2 "const_int_operand" "")))]
  ""
  [(parallel [(set (match_dup 0) (ashift:SI (match_dup 1) (match_dup 2)))
	      (clobber (match_dup 3))])]
  "if (!avr_peep2_scratch_safe (operands[3]))
     FAIL;")

(define_insn "*ashlsi3_const"
  [(set (match_operand:SI 0 "register_operand"            "=r,r,r,r")
	(ashift:SI (match_operand:SI 1 "register_operand"  "0,0,r,0")
		   (match_operand:QI 2 "const_int_operand" "L,P,O,n")))
   (clobber (match_scratch:QI 3 "=X,X,X,&d"))]
  "reload_completed"
  "* return ashlsi3_out (insn, operands, NULL);"
  [(set_attr "length" "0,4,4,10")
   (set_attr "cc" "none,set_n,clobber,clobber")])

;; >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >>
;; arithmetic shift right

(define_insn "ashrqi3"
  [(set (match_operand:QI 0 "register_operand" "=r,r,r,r,r,r")
	(ashiftrt:QI (match_operand:QI 1 "register_operand" "0,0,0,0,0,0")
		     (match_operand:QI 2 "general_operand"  "r,L,P,K,n,Qm")))]
  ""
  "* return ashrqi3_out (insn, operands, NULL);"
  [(set_attr "length" "5,0,1,2,5,9")
   (set_attr "cc" "clobber,none,clobber,clobber,clobber,clobber")])

(define_insn "ashrhi3"
  [(set (match_operand:HI 0 "register_operand"             "=r,r,r,r,r,r,r")
	(ashiftrt:HI (match_operand:HI 1 "register_operand" "0,0,0,r,0,0,0")
		     (match_operand:QI 2 "general_operand"  "r,L,P,O,K,n,Qm")))]
  ""
  "* return ashrhi3_out (insn, operands, NULL);"
  [(set_attr "length" "6,0,2,4,4,10,10")
   (set_attr "cc" "clobber,none,clobber,set_n,clobber,clobber,clobber")])

(define_insn "ashrsi3"
  [(set (match_operand:SI 0 "register_operand"             "=r,r,r,r,r,r,r")
	(ashiftrt:SI (match_operand:SI 1 "register_operand" "0,0,0,r,0,0,0")
		     (match_operand:QI 2 "general_operand"  "r,L,P,O,K,n,Qm")))]
  ""
  "* return ashrsi3_out (insn, operands, NULL);"
  [(set_attr "length" "8,0,4,6,8,10,12")
   (set_attr "cc" "clobber,none,clobber,set_n,clobber,clobber,clobber")])

;; Optimize if a scratch register from LD_REGS happens to be available.

(define_peephole2
  [(match_scratch:QI 3 "d")
   (set (match_operand:HI 0 "register_operand" "")
	(ashiftrt:HI (match_operand:HI 1 "register_operand" "")
		     (match_operand:QI 2 "const_int_operand" "")))]
  ""
  [(parallel [(set (match_dup 0) (ashiftrt:HI (match_dup 1) (match_dup 2)))
	      (clobber (match_dup 3))])]
  "if (!avr_peep2_scratch_safe (operands[3]))
     FAIL;")

(define_insn "*ashrhi3_const"
  [(set (match_operand:HI 0 "register_operand"              "=r,r,r,r,r")
	(ashiftrt:HI (match_operand:HI 1 "register_operand"  "0,0,r,0,0")
		     (match_operand:QI 2 "const_int_operand" "L,P,O,K,n")))
   (clobber (match_scratch:QI 3 "=X,X,X,X,&d"))]
  "reload_completed"
  "* return ashrhi3_out (insn, operands, NULL);"
  [(set_attr "length" "0,2,4,4,10")
   (set_attr "cc" "none,clobber,set_n,clobber,clobber")])

(define_peephole2
  [(match_scratch:QI 3 "d")
   (set (match_operand:SI 0 "register_operand" "")
	(ashiftrt:SI (match_operand:SI 1 "register_operand" "")
		     (match_operand:QI 2 "const_int_operand" "")))]
  ""
  [(parallel [(set (match_dup 0) (ashiftrt:SI (match_dup 1) (match_dup 2)))
	      (clobber (match_dup 3))])]
  "if (!avr_peep2_scratch_safe (operands[3]))
     FAIL;")

(define_insn "*ashrsi3_const"
  [(set (match_operand:SI 0 "register_operand"              "=r,r,r,r")
	(ashiftrt:SI (match_operand:SI 1 "register_operand"  "0,0,r,0")
		     (match_operand:QI 2 "const_int_operand" "L,P,O,n")))
   (clobber (match_scratch:QI 3 "=X,X,X,&d"))]
  "reload_completed"
  "* return ashrsi3_out (insn, operands, NULL);"
  [(set_attr "length" "0,4,4,10")
   (set_attr "cc" "none,clobber,set_n,clobber")])

;; >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >> >>
;; logical shift right

(define_insn "lshrqi3"
  [(set (match_operand:QI 0 "register_operand"             "=r,r,r,r,!d,r,r")
	(lshiftrt:QI (match_operand:QI 1 "register_operand" "0,0,0,0,0,0,0")
		     (match_operand:QI 2 "general_operand"  "r,L,P,K,n,n,Qm")))]
  ""
  "* return lshrqi3_out (insn, operands, NULL);"
  [(set_attr "length" "5,0,1,2,4,6,9")
   (set_attr "cc" "clobber,none,set_czn,set_czn,set_czn,set_czn,clobber")])

(define_insn "lshrhi3"
  [(set (match_operand:HI 0 "register_operand"             "=r,r,r,r,r,r,r")
	(lshiftrt:HI (match_operand:HI 1 "register_operand" "0,0,0,r,0,0,0")
		     (match_operand:QI 2 "general_operand"  "r,L,P,O,K,n,Qm")))]
  ""
  "* return lshrhi3_out (insn, operands, NULL);"
  [(set_attr "length" "6,0,2,2,4,10,10")
   (set_attr "cc" "clobber,none,clobber,clobber,clobber,clobber,clobber")])

(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "register_operand"             "=r,r,r,r,r,r,r")
	(lshiftrt:SI (match_operand:SI 1 "register_operand" "0,0,0,r,0,0,0")
		     (match_operand:QI 2 "general_operand"  "r,L,P,O,K,n,Qm")))]
  ""
  "* return lshrsi3_out (insn, operands, NULL);"
  [(set_attr "length" "8,0,4,4,8,10,12")
   (set_attr "cc" "clobber,none,clobber,clobber,clobber,clobber,clobber")])

;; Optimize if a scratch register from LD_REGS happens to be available.

(define_peephole2
  [(match_scratch:QI 3 "d")
   (set (match_operand:HI 0 "register_operand" "")
	(lshiftrt:HI (match_operand:HI 1 "register_operand" "")
		     (match_operand:QI 2 "const_int_operand" "")))]
  ""
  [(parallel [(set (match_dup 0) (lshiftrt:HI (match_dup 1) (match_dup 2)))
	      (clobber (match_dup 3))])]
  "if (!avr_peep2_scratch_safe (operands[3]))
     FAIL;")

(define_insn "*lshrhi3_const"
  [(set (match_operand:HI 0 "register_operand"              "=r,r,r,r,r")
	(lshiftrt:HI (match_operand:HI 1 "register_operand"  "0,0,r,0,0")
		     (match_operand:QI 2 "const_int_operand" "L,P,O,K,n")))
   (clobber (match_scratch:QI 3 "=X,X,X,X,&d"))]
  "reload_completed"
  "* return lshrhi3_out (insn, operands, NULL);"
  [(set_attr "length" "0,2,2,4,10")
   (set_attr "cc" "none,clobber,clobber,clobber,clobber")])

(define_peephole2
  [(match_scratch:QI 3 "d")
   (set (match_operand:SI 0 "register_operand" "")
	(lshiftrt:SI (match_operand:SI 1 "register_operand" "")
		     (match_operand:QI 2 "const_int_operand" "")))]
  ""
  [(parallel [(set (match_dup 0) (lshiftrt:SI (match_dup 1) (match_dup 2)))
	      (clobber (match_dup 3))])]
  "if (!avr_peep2_scratch_safe (operands[3]))
     FAIL;")

(define_insn "*lshrsi3_const"
  [(set (match_operand:SI 0 "register_operand"              "=r,r,r,r")
	(lshiftrt:SI (match_operand:SI 1 "register_operand"  "0,0,r,0")
		     (match_operand:QI 2 "const_int_operand" "L,P,O,n")))
   (clobber (match_scratch:QI 3 "=X,X,X,&d"))]
  "reload_completed"
  "* return lshrsi3_out (insn, operands, NULL);"
  [(set_attr "length" "0,4,4,10")
   (set_attr "cc" "none,clobber,clobber,clobber")])

;; abs(x) abs(x) abs(x) abs(x) abs(x) abs(x) abs(x) abs(x) abs(x) abs(x) abs(x)
;; abs

(define_insn "absqi2"
  [(set (match_operand:QI 0 "register_operand" "=r")
        (abs:QI (match_operand:QI 1 "register_operand" "0")))]
  ""
  "sbrc %0,7
	neg %0"
  [(set_attr "length" "2")
   (set_attr "cc" "clobber")])


(define_insn "abssf2"
  [(set (match_operand:SF 0 "register_operand" "=d,r")
        (abs:SF (match_operand:SF 1 "register_operand" "0,0")))]
  ""
  "@
	andi %D0,0x7f
	clt\;bld %D0,7"
  [(set_attr "length" "1,2")
   (set_attr "cc" "set_n,clobber")])

;; 0 - x  0 - x  0 - x  0 - x  0 - x  0 - x  0 - x  0 - x  0 - x  0 - x  0 - x
;; neg

(define_insn "negqi2"
  [(set (match_operand:QI 0 "register_operand" "=r")
        (neg:QI (match_operand:QI 1 "register_operand" "0")))]
  ""
  "neg %0"
  [(set_attr "length" "1")
   (set_attr "cc" "set_zn")])

(define_insn "neghi2"
  [(set (match_operand:HI 0 "register_operand"       "=!d,r,&r")
	(neg:HI (match_operand:HI 1 "register_operand" "0,0,r")))]
  ""
  "@
	com %B0\;neg %A0\;sbci %B0,lo8(-1)
	com %B0\;neg %A0\;sbc %B0,__zero_reg__\;inc %B0
	clr %A0\;clr %B0\;sub %A0,%A1\;sbc %B0,%B1"
  [(set_attr "length" "3,4,4")
   (set_attr "cc" "set_czn,set_n,set_czn")])

(define_insn "negsi2"
  [(set (match_operand:SI 0 "register_operand"       "=!d,r,&r")
	(neg:SI (match_operand:SI 1 "register_operand" "0,0,r")))]
  ""
  "@
	com %D0\;com %C0\;com %B0\;neg %A0\;sbci %B0,lo8(-1)\;sbci %C0,lo8(-1)\;sbci %D0,lo8(-1)
	com %D0\;com %C0\;com %B0\;com %A0\;adc %A0,__zero_reg__\;adc %B0,__zero_reg__\;adc %C0,__zero_reg__\;adc %D0,__zero_reg__
	clr %A0\;clr %B0\;{clr %C0\;clr %D0|movw %C0,%A0}\;sub %A0,%A1\;sbc %B0,%B1\;sbc %C0,%C1\;sbc %D0,%D1"
  [(set_attr_alternative "length"
			 [(const_int 7)
			  (const_int 8)
			  (if_then_else (eq_attr "mcu_have_movw" "yes")
					(const_int 7)
					(const_int 8))])
   (set_attr "cc" "set_czn,set_n,set_czn")])

(define_insn "negsf2"
  [(set (match_operand:SF 0 "register_operand" "=d,r")
	(neg:SF (match_operand:SF 1 "register_operand" "0,0")))]
  ""
  "@
	subi %D0,0x80
	bst %D0,7\;com %D0\;bld %D0,7\;com %D0"
  [(set_attr "length" "1,4")
   (set_attr "cc" "set_n,set_n")])

;; !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
;; not

(define_insn "one_cmplqi2"
  [(set (match_operand:QI 0 "register_operand" "=r")
        (not:QI (match_operand:QI 1 "register_operand" "0")))]
  ""
  "com %0"
  [(set_attr "length" "1")
   (set_attr "cc" "set_czn")])

(define_insn "one_cmplhi2"
  [(set (match_operand:HI 0 "register_operand" "=r")
        (not:HI (match_operand:HI 1 "register_operand" "0")))]
  ""
  "com %0
	com %B0"
  [(set_attr "length" "2")
   (set_attr "cc" "set_n")])

(define_insn "one_cmplsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (not:SI (match_operand:SI 1 "register_operand" "0")))]
  ""
  "com %0
	com %B0
	com %C0
	com %D0"
  [(set_attr "length" "4")
   (set_attr "cc" "set_n")])

;; xx<---x xx<---x xx<---x xx<---x xx<---x xx<---x xx<---x xx<---x xx<---x
;; sign extend

(define_insn "extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
        (sign_extend:HI (match_operand:QI 1 "register_operand" "0,*r")))]
  ""
  "@
	clr %B0\;sbrc %0,7\;com %B0
	mov %A0,%A1\;clr %B0\;sbrc %A0,7\;com %B0"
  [(set_attr "length" "3,4")
   (set_attr "cc" "set_n,set_n")])

(define_insn "extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (sign_extend:SI (match_operand:QI 1 "register_operand" "0,*r")))]
  ""
  "@
	clr %B0\;sbrc %A0,7\;com %B0\;mov %C0,%B0\;mov %D0,%B0
	mov %A0,%A1\;clr %B0\;sbrc %A0,7\;com %B0\;mov %C0,%B0\;mov %D0,%B0"
  [(set_attr "length" "5,6")
   (set_attr "cc" "set_n,set_n")])

(define_insn "extendhisi2"
  [(set (match_operand:SI 0 "register_operand"               "=r,&r")
        (sign_extend:SI (match_operand:HI 1 "register_operand" "0,*r")))]
  ""
  "@
	clr %C0\;sbrc %B0,7\;com %C0\;mov %D0,%C0
	{mov %A0,%A1\;mov %B0,%B1|movw %A0,%A1}\;clr %C0\;sbrc %B0,7\;com %C0\;mov %D0,%C0"
  [(set_attr_alternative "length"
			 [(const_int 4)
			  (if_then_else (eq_attr "mcu_have_movw" "yes")
					(const_int 5)
					(const_int 6))])
   (set_attr "cc" "set_n,set_n")])

;; xx<---x xx<---x xx<---x xx<---x xx<---x xx<---x xx<---x xx<---x xx<---x
;; zero extend

(define_insn "zero_extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
        (zero_extend:HI (match_operand:QI 1 "register_operand" "0,*r")))]
  ""
  "@
	clr %B0
	mov %A0,%A1\;clr %B0"
  [(set_attr "length" "1,2")
   (set_attr "cc" "set_n,set_n")])

(define_insn "zero_extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (zero_extend:SI (match_operand:QI 1 "register_operand" "0,*r")))]
  ""
  "@
	clr %B0\;clr %C0\;clr %D0
	mov %A0,%A1\;clr %B0\;clr %C0\;clr %D0"
  [(set_attr "length" "3,4")
   (set_attr "cc" "set_n,set_n")])

(define_insn "zero_extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=r,&r")
        (zero_extend:SI (match_operand:HI 1 "register_operand" "0,*r")))]
  ""
  "@
	clr %C0\;clr %D0
	{mov %A0,%A1\;mov %B0,%B1|movw %A0,%A1}\;clr %C0\;clr %D0"
  [(set_attr_alternative "length"
			 [(const_int 2)
			  (if_then_else (eq_attr "mcu_have_movw" "yes")
					(const_int 3)
					(const_int 4))])
   (set_attr "cc" "set_n,set_n")])

;;<=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=>
;; compare

(define_insn "tstqi"
  [(set (cc0)
        (match_operand:QI 0 "register_operand" "r"))]
  ""
  "tst %0"
  [(set_attr "cc" "compare")
   (set_attr "length" "1")])

(define_insn "*negated_tstqi"
  [(set (cc0)
        (neg:QI (match_operand:QI 0 "register_operand" "r")))]
  ""
  "cp __zero_reg__,%0"
  [(set_attr "cc" "compare")
   (set_attr "length" "1")])

(define_insn "tsthi"
  [(set (cc0)
        (match_operand:HI 0 "register_operand" "!w,r"))]
  ""
  "* return out_tsthi (insn,NULL);"
[(set_attr "cc" "compare,compare")
 (set_attr "length" "1,2")])

(define_insn "*negated_tsthi"
  [(set (cc0)
        (neg:HI (match_operand:HI 0 "register_operand" "r")))]
  ""
  "cp __zero_reg__,%A0
	cpc __zero_reg__,%B0"
[(set_attr "cc" "compare")
 (set_attr "length" "2")])

(define_insn "tstsi"
  [(set (cc0)
        (match_operand:SI 0 "register_operand" "r"))]
  ""
  "* return out_tstsi (insn,NULL);"
  [(set_attr "cc" "compare")
   (set_attr "length" "4")])

(define_insn "*negated_tstsi"
  [(set (cc0)
        (neg:SI (match_operand:SI 0 "register_operand" "r")))]
  ""
  "cp __zero_reg__,%A0
	cpc __zero_reg__,%B0
	cpc __zero_reg__,%C0
	cpc __zero_reg__,%D0"
  [(set_attr "cc" "compare")
   (set_attr "length" "4")])


(define_insn "cmpqi"
  [(set (cc0)
        (compare (match_operand:QI 0 "register_operand"  "r,d")
		 (match_operand:QI 1 "nonmemory_operand" "r,i")))]
  ""
  "@
	cp %0,%1
	cpi %0,lo8(%1)"
  [(set_attr "cc" "compare,compare")
   (set_attr "length" "1,1")])

(define_insn "*cmpqi_sign_extend"
  [(set (cc0)
        (compare (sign_extend:HI
		  (match_operand:QI 0 "register_operand"  "d"))
		 (match_operand:HI 1 "const_int_operand" "n")))]
  "INTVAL (operands[1]) >= -128 && INTVAL (operands[1]) <= 127"
  "cpi %0,lo8(%1)"
  [(set_attr "cc" "compare")
   (set_attr "length" "1")])

(define_insn "cmphi"
  [(set (cc0)
	(compare (match_operand:HI 0 "register_operand"  "r,d,d,r,r")
		 (match_operand:HI 1 "nonmemory_operand" "r,M,i,M,i")))
   (clobber (match_scratch:QI 2 "=X,X,&d,&d,&d"))]
  ""
  "*{
  switch (which_alternative)
    {
    case 0:
      return (AS2 (cp,%A0,%A1) CR_TAB
              AS2 (cpc,%B0,%B1));
    case 1:
      if (reg_unused_after (insn, operands[0])
          && INTVAL (operands[1]) >= 0 && INTVAL (operands[1]) <= 63
          && test_hard_reg_class (ADDW_REGS, operands[0]))
        return AS2 (sbiw,%0,%1);
       else
        return (AS2 (cpi,%0,%1) CR_TAB
                AS2 (cpc,%B0,__zero_reg__));
    case 2:
      if (reg_unused_after (insn, operands[0]))
        return (AS2 (subi,%0,lo8(%1))  CR_TAB
                AS2 (sbci,%B0,hi8(%1)));
      else
        return (AS2 (ldi, %2,hi8(%1))  CR_TAB
	        AS2 (cpi, %A0,lo8(%1)) CR_TAB
	        AS2 (cpc, %B0,%2));
   case 3:
      return (AS2 (ldi, %2,lo8(%1))  CR_TAB
	      AS2 (cp, %A0,%2) CR_TAB
	      AS2 (cpc, %B0,__zero_reg__));

   case 4:
      return (AS2 (ldi, %2,lo8(%1))  CR_TAB
              AS2 (cp, %A0,%2)       CR_TAB
              AS2 (ldi, %2,hi8(%1)) CR_TAB
	      AS2 (cpc, %B0,%2));
    }
  return \"bug\";
}" 
  [(set_attr "cc" "compare,compare,compare,compare,compare")
   (set_attr "length" "2,2,3,3,4")])


(define_insn "cmpsi"
  [(set (cc0)
	(compare (match_operand:SI 0 "register_operand"  "r,d,d,r,r")
		 (match_operand:SI 1 "nonmemory_operand" "r,M,i,M,i")))
   (clobber (match_scratch:QI 2 "=X,X,&d,&d,&d"))]
  ""
  "*{
  switch (which_alternative)
    {
    case 0:
      return (AS2 (cp,%A0,%A1) CR_TAB
              AS2 (cpc,%B0,%B1) CR_TAB
	      AS2 (cpc,%C0,%C1) CR_TAB
	      AS2 (cpc,%D0,%D1));
    case 1:
      if (reg_unused_after (insn, operands[0])
          && INTVAL (operands[1]) >= 0 && INTVAL (operands[1]) <= 63
          && test_hard_reg_class (ADDW_REGS, operands[0]))
        return (AS2 (sbiw,%0,%1) CR_TAB
                AS2 (cpc,%C0,__zero_reg__) CR_TAB
                AS2 (cpc,%D0,__zero_reg__));
      else
        return (AS2 (cpi,%A0,lo8(%1))  CR_TAB
                AS2 (cpc,%B0,__zero_reg__) CR_TAB
                AS2 (cpc,%C0,__zero_reg__) CR_TAB
                AS2 (cpc,%D0,__zero_reg__));
    case 2:
      if (reg_unused_after (insn, operands[0]))
        return (AS2 (subi,%A0,lo8(%1))  CR_TAB
                AS2 (sbci,%B0,hi8(%1))  CR_TAB
                AS2 (sbci,%C0,hlo8(%1))  CR_TAB
                AS2 (sbci,%D0,hhi8(%1)));
      else
       return (AS2 (cpi, %A0,lo8(%1))   CR_TAB
	       AS2 (ldi, %2,hi8(%1))  CR_TAB
	       AS2 (cpc, %B0,%2)       CR_TAB
	       AS2 (ldi, %2,hlo8(%1))  CR_TAB
	       AS2 (cpc, %C0,%2)       CR_TAB
	       AS2 (ldi, %2,hhi8(%1)) CR_TAB
	       AS2 (cpc, %D0,%2));
    case 3:
        return (AS2 (ldi,%2,lo8(%1))        CR_TAB
                AS2 (cp,%A0,%2)            CR_TAB
                AS2 (cpc,%B0,__zero_reg__) CR_TAB
                AS2 (cpc,%C0,__zero_reg__) CR_TAB
                AS2 (cpc,%D0,__zero_reg__));
    case 4:
       return (AS2 (ldi, %2,lo8(%1))   CR_TAB
               AS2 (cp, %A0,%2)        CR_TAB
	       AS2 (ldi, %2,hi8(%1))  CR_TAB
	       AS2 (cpc, %B0,%2)       CR_TAB
	       AS2 (ldi, %2,hlo8(%1))  CR_TAB
	       AS2 (cpc, %C0,%2)       CR_TAB
	       AS2 (ldi, %2,hhi8(%1)) CR_TAB
	       AS2 (cpc, %D0,%2));
    }
  return \"bug\";
}"
  [(set_attr "cc" "compare,compare,compare,compare,compare")
   (set_attr "length" "4,4,7,5,8")])

;; ----------------------------------------------------------------------
;; JUMP INSTRUCTIONS
;; ----------------------------------------------------------------------
;; Conditional jump instructions

(define_expand "beq"
  [(set (pc)
        (if_then_else (eq (cc0) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
  "")

(define_expand "bne"
  [(set (pc)
        (if_then_else (ne (cc0) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
  "")

(define_expand "bge"
  [(set (pc)
        (if_then_else (ge (cc0) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
  "")

(define_expand "bgeu"
  [(set (pc)
        (if_then_else (geu (cc0) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
  "")

(define_expand "blt"
  [(set (pc)
        (if_then_else (lt (cc0) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
  "")

(define_expand "bltu"
  [(set (pc)
        (if_then_else (ltu (cc0) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
  "")



/****************************************************************
 AVR not have following conditional jumps: LE,LEU,GT,GTU.
 Convert them all to proper jumps.
*****************************************************************/

(define_expand "ble"
  [(set (pc)
        (if_then_else (le (cc0) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
  "")

(define_expand "bleu"
  [(set (pc)
        (if_then_else (leu (cc0) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
  "")

(define_expand "bgt"
  [(set (pc)
        (if_then_else (gt (cc0) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
  "")

(define_expand "bgtu"
  [(set (pc)
        (if_then_else (gtu (cc0) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
  "")

;; Test a single bit in a QI/HI/SImode register.
(define_insn "*sbrx_branch"
  [(set (pc)
        (if_then_else
	 (match_operator 0 "eqne_operator"
			 [(zero_extract
			   (match_operand:QI 1 "register_operand" "r")
			   (const_int 1)
			   (match_operand 2 "const_int_operand" "n"))
			  (const_int 0)])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))]
  ""
  "* return avr_out_sbxx_branch (insn, operands);"
  [(set (attr "length")
	(if_then_else (and (ge (minus (pc) (match_dup 3)) (const_int -2046))
			   (le (minus (pc) (match_dup 3)) (const_int 2046)))
		      (const_int 2)
		      (if_then_else (eq_attr "mcu_mega" "no")
				    (const_int 2)
				    (const_int 4))))
   (set_attr "cc" "clobber")])

(define_insn "*sbrx_and_branchhi"
  [(set (pc)
        (if_then_else
	 (match_operator 0 "eqne_operator"
			 [(and:HI
			   (match_operand:HI 1 "register_operand" "r")
			   (match_operand:HI 2 "single_one_operand" "n"))
			  (const_int 0)])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))]
  ""
  "* return avr_out_sbxx_branch (insn, operands);"
  [(set (attr "length")
	(if_then_else (and (ge (minus (pc) (match_dup 3)) (const_int -2046))
			   (le (minus (pc) (match_dup 3)) (const_int 2046)))
		      (const_int 2)
		      (if_then_else (eq_attr "mcu_mega" "no")
				    (const_int 2)
				    (const_int 4))))
   (set_attr "cc" "clobber")])

(define_insn "*sbrx_and_branchsi"
  [(set (pc)
        (if_then_else
	 (match_operator 0 "eqne_operator"
			 [(and:SI
			   (match_operand:SI 1 "register_operand" "r")
			   (match_operand:SI 2 "single_one_operand" "n"))
			  (const_int 0)])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))]
  ""
  "* return avr_out_sbxx_branch (insn, operands);"
  [(set (attr "length")
	(if_then_else (and (ge (minus (pc) (match_dup 3)) (const_int -2046))
			   (le (minus (pc) (match_dup 3)) (const_int 2046)))
		      (const_int 2)
		      (if_then_else (eq_attr "mcu_mega" "no")
				    (const_int 2)
				    (const_int 4))))
   (set_attr "cc" "clobber")])

;; Convert sign tests to bit 7/15/31 tests that match the above insns.
(define_peephole2
  [(set (cc0) (match_operand:QI 0 "register_operand" ""))
   (set (pc) (if_then_else (ge (cc0) (const_int 0))
			   (label_ref (match_operand 1 "" ""))
			   (pc)))]
  ""
  [(set (pc) (if_then_else (eq (zero_extract (match_dup 0)
					     (const_int 1)
					     (const_int 7))
			       (const_int 0))
			   (label_ref (match_dup 1))
			   (pc)))]
  "")

(define_peephole2
  [(set (cc0) (match_operand:QI 0 "register_operand" ""))
   (set (pc) (if_then_else (lt (cc0) (const_int 0))
			   (label_ref (match_operand 1 "" ""))
			   (pc)))]
  ""
  [(set (pc) (if_then_else (ne (zero_extract (match_dup 0)
					     (const_int 1)
					     (const_int 7))
			       (const_int 0))
			   (label_ref (match_dup 1))
			   (pc)))]
  "")

(define_peephole2
  [(set (cc0) (match_operand:HI 0 "register_operand" ""))
   (set (pc) (if_then_else (ge (cc0) (const_int 0))
			   (label_ref (match_operand 1 "" ""))
			   (pc)))]
  ""
  [(set (pc) (if_then_else (eq (and:HI (match_dup 0) (const_int -32768))
			       (const_int 0))
			   (label_ref (match_dup 1))
			   (pc)))]
  "")

(define_peephole2
  [(set (cc0) (match_operand:HI 0 "register_operand" ""))
   (set (pc) (if_then_else (lt (cc0) (const_int 0))
			   (label_ref (match_operand 1 "" ""))
			   (pc)))]
  ""
  [(set (pc) (if_then_else (ne (and:HI (match_dup 0) (const_int -32768))
			       (const_int 0))
			   (label_ref (match_dup 1))
			   (pc)))]
  "")

(define_peephole2
  [(set (cc0) (match_operand:SI 0 "register_operand" ""))
   (set (pc) (if_then_else (ge (cc0) (const_int 0))
			   (label_ref (match_operand 1 "" ""))
			   (pc)))]
  ""
  [(set (pc) (if_then_else (eq (and:SI (match_dup 0) (match_dup 2))
			       (const_int 0))
			   (label_ref (match_dup 1))
			   (pc)))]
  "operands[2] = GEN_INT (-2147483647 - 1);")

(define_peephole2
  [(set (cc0) (match_operand:SI 0 "register_operand" ""))
   (set (pc) (if_then_else (lt (cc0) (const_int 0))
			   (label_ref (match_operand 1 "" ""))
			   (pc)))]
  ""
  [(set (pc) (if_then_else (ne (and:SI (match_dup 0) (match_dup 2))
			       (const_int 0))
			   (label_ref (match_dup 1))
			   (pc)))]
  "operands[2] = GEN_INT (-2147483647 - 1);")

;; ************************************************************************
;; Implementation of conditional jumps here.
;;  Compare with 0 (test) jumps
;; ************************************************************************

(define_insn "branch"
  [(set (pc)
        (if_then_else (match_operator 1 "simple_comparison_operator"
                        [(cc0)
                         (const_int 0)])
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
  "*
   return ret_cond_branch (operands[1], avr_jump_mode (operands[0],insn), 0);"
  [(set_attr "type" "branch")
   (set_attr "cc" "clobber")])

(define_insn "difficult_branch"
  [(set (pc)
        (if_then_else (match_operator 1 "difficult_comparison_operator"
                        [(cc0)
                         (const_int 0)])
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
  "*
   return ret_cond_branch (operands[1], avr_jump_mode (operands[0],insn), 0);"
  [(set_attr "type" "branch1")
   (set_attr "cc" "clobber")])

;; revers branch

(define_insn "rvbranch"
  [(set (pc)
        (if_then_else (match_operator 1 "simple_comparison_operator" 
	                [(cc0)
                         (const_int 0)])
                      (pc)
                      (label_ref (match_operand 0 "" ""))))]
  ""
  "*
   return ret_cond_branch (operands[1], avr_jump_mode (operands[0], insn), 1);"
  [(set_attr "type" "branch1")
   (set_attr "cc" "clobber")])

(define_insn "difficult_rvbranch"
  [(set (pc)
        (if_then_else (match_operator 1 "difficult_comparison_operator" 
	                [(cc0)
                         (const_int 0)])
                      (pc)
                      (label_ref (match_operand 0 "" ""))))]
  ""
  "*
   return ret_cond_branch (operands[1], avr_jump_mode (operands[0], insn), 1);"
  [(set_attr "type" "branch")
   (set_attr "cc" "clobber")])

;; **************************************************************************
;; Unconditional and other jump instructions.

(define_insn "jump"
  [(set (pc)
        (label_ref (match_operand 0 "" "")))]
  ""
  "*{
  if (AVR_MEGA && get_attr_length (insn) != 1)
    return AS1 (jmp,%0);
  return AS1 (rjmp,%0);
}"
  [(set (attr "length")
	(if_then_else (and (ge (minus (pc) (match_dup 0)) (const_int -2047))
			   (le (minus (pc) (match_dup 0)) (const_int 2047)))
		      (const_int 1)
		      (const_int 2)))
   (set_attr "cc" "none")])

;; call

(define_expand "call"
  [(call (match_operand:HI 0 "call_insn_operand" "")
         (match_operand:HI 1 "general_operand" ""))]
  ;; Operand 1 not used on the AVR.
  ""
  "")

;; call value

(define_expand "call_value"
  [(set (match_operand 0 "register_operand" "")
        (call (match_operand:HI 1 "call_insn_operand" "")
              (match_operand:HI 2 "general_operand" "")))]
  ;; Operand 2 not used on the AVR.
  ""
  "")

(define_insn "call_insn"
  [(call (mem:HI (match_operand:HI 0 "nonmemory_operand" "!z,*r,s,n"))
         (match_operand:HI 1 "general_operand" "X,X,X,X"))]
;; We don't need in saving Z register because r30,r31 is a call used registers
  ;; Operand 1 not used on the AVR.
  "(register_operand (operands[0], HImode) || CONSTANT_P (operands[0]))"
  "*{
  if (which_alternative==0)
     return \"icall\";
  else if (which_alternative==1)
    {
      if (AVR_HAVE_MOVW)
	return (AS2 (movw, r30, %0) CR_TAB
		\"icall\");
      else
	return (AS2 (mov, r30, %A0) CR_TAB
		AS2 (mov, r31, %B0) CR_TAB
		\"icall\");
    }
  else if (which_alternative==2)
    return AS1(%~call,%c0);
  return (AS2 (ldi,r30,lo8(%0)) CR_TAB
          AS2 (ldi,r31,hi8(%0)) CR_TAB
          \"icall\");
}"
  [(set_attr "cc" "clobber,clobber,clobber,clobber")
   (set_attr_alternative "length"
			 [(const_int 1)
			  (if_then_else (eq_attr "mcu_have_movw" "yes")
					(const_int 2)
					(const_int 3))
			  (if_then_else (eq_attr "mcu_mega" "yes")
					(const_int 2)
					(const_int 1))
			  (const_int 3)])])

(define_insn "call_value_insn"
  [(set (match_operand 0 "register_operand" "=r,r,r,r")
        (call (mem:HI (match_operand:HI 1 "nonmemory_operand" "!z,*r,s,n"))
;; We don't need in saving Z register because r30,r31 is a call used registers
              (match_operand:HI 2 "general_operand" "X,X,X,X")))]
  ;; Operand 2 not used on the AVR.
  "(register_operand (operands[0], VOIDmode) || CONSTANT_P (operands[0]))"
  "*{
  if (which_alternative==0)
     return \"icall\";
  else if (which_alternative==1)
    {
      if (AVR_HAVE_MOVW)
	return (AS2 (movw, r30, %1) CR_TAB
		\"icall\");
      else
	return (AS2 (mov, r30, %A1) CR_TAB
		AS2 (mov, r31, %B1) CR_TAB
		\"icall\");
    }
  else if (which_alternative==2)
    return AS1(%~call,%c1);
  return (AS2 (ldi, r30, lo8(%1)) CR_TAB
          AS2 (ldi, r31, hi8(%1)) CR_TAB
          \"icall\");
}"
  [(set_attr "cc" "clobber,clobber,clobber,clobber")
   (set_attr_alternative "length"
			 [(const_int 1)
			  (if_then_else (eq_attr "mcu_have_movw" "yes")
					(const_int 2)
					(const_int 3))
			  (if_then_else (eq_attr "mcu_mega" "yes")
					(const_int 2)
					(const_int 1))
			  (const_int 3)])])

(define_insn "return"
  [(return)]
  "reload_completed && avr_simple_epilogue ()"
  "ret"
  [(set_attr "cc" "none")
   (set_attr "length" "1")])

(define_insn "nop"
  [(const_int 0)]
  ""
  "nop"
  [(set_attr "cc" "none")
   (set_attr "length" "1")])

; indirect jump
(define_insn "indirect_jump"
  [(set (pc) (match_operand:HI 0 "register_operand" "!z,*r"))]
  ""
  "@
	ijmp
	push %A0\;push %B0\;ret"
  [(set_attr "length" "1,3")
   (set_attr "cc" "none,none")])

;; table jump

;; Table made from "rjmp" instructions for <=8K devices.
(define_insn "*tablejump_rjmp"
  [(set (pc) (unspec:HI [(match_operand:HI 0 "register_operand" "!z,*r")]
			UNSPEC_INDEX_JMP))
   (use (label_ref (match_operand 1 "" "")))
   (clobber (match_dup 0))]
  "!AVR_MEGA"
  "@
	ijmp
	push %A0\;push %B0\;ret"
  [(set_attr "length" "1,3")
   (set_attr "cc" "none,none")])

;; Not a prologue, but similar idea - move the common piece of code to libgcc.
(define_insn "*tablejump_lib"
  [(set (pc) (unspec:HI [(match_operand:HI 0 "register_operand" "z")]
			UNSPEC_INDEX_JMP))
   (use (label_ref (match_operand 1 "" "")))
   (clobber (match_dup 0))]
  "AVR_MEGA && TARGET_CALL_PROLOGUES"
  "jmp __tablejump2__"
  [(set_attr "length" "2")
   (set_attr "cc" "clobber")])

(define_insn "*tablejump_enh"
  [(set (pc) (unspec:HI [(match_operand:HI 0 "register_operand" "z")]
			UNSPEC_INDEX_JMP))
   (use (label_ref (match_operand 1 "" "")))
   (clobber (match_dup 0))]
  "AVR_MEGA && AVR_ENHANCED"
  "lsl r30
	rol r31
	lpm __tmp_reg__,Z+
	lpm r31,Z
	mov r30,__tmp_reg__
	ijmp"
  [(set_attr "length" "6")
   (set_attr "cc" "clobber")])

(define_insn "*tablejump"
  [(set (pc) (unspec:HI [(match_operand:HI 0 "register_operand" "z")]
			UNSPEC_INDEX_JMP))
   (use (label_ref (match_operand 1 "" "")))
   (clobber (match_dup 0))]
  "AVR_MEGA"
  "lsl r30
	rol r31
	lpm
	inc r30
	push r0
	lpm
	push r0
	ret"
  [(set_attr "length" "8")
   (set_attr "cc" "clobber")])

(define_expand "casesi"
  [(set (match_dup 6)
	(minus:HI (subreg:HI (match_operand:SI 0 "register_operand" "") 0)
		  (match_operand:HI 1 "register_operand" "")))
   (parallel [(set (cc0)
		   (compare (match_dup 6)
			    (match_operand:HI 2 "register_operand" "")))
	      (clobber (match_scratch:QI 9 ""))])
   
   (set (pc)
	(if_then_else (gtu (cc0)
			   (const_int 0))
		      (label_ref (match_operand 4 "" ""))
		      (pc)))

   (set (match_dup 6)
	(plus:HI (match_dup 6) (label_ref (match_operand:HI 3 "" ""))))

   (parallel [(set (pc) (unspec:HI [(match_dup 6)] UNSPEC_INDEX_JMP))
	      (use (label_ref (match_dup 3)))
	      (clobber (match_dup 6))])]
  ""
  "
{
  operands[6] = gen_reg_rtx (HImode);
}")


;; ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
;; This instruction sets Z flag

(define_insn "sez"
  [(set (cc0) (const_int 0))]
  ""
  "sez"
  [(set_attr "length" "1")
   (set_attr "cc" "compare")])

;; Clear/set/test a single bit in I/O address space.

(define_insn "*cbi"
  [(set (mem:QI (match_operand 0 "low_io_address_operand" "n"))
	(and:QI (mem:QI (match_dup 0))
		(match_operand:QI 1 "single_zero_operand" "n")))]
  "(optimize > 0)"
{
  operands[2] = GEN_INT (exact_log2 (~INTVAL (operands[1]) & 0xff));
  return AS2 (cbi,%0-0x20,%2);
}
  [(set_attr "length" "1")
   (set_attr "cc" "none")])

(define_insn "*sbi"
  [(set (mem:QI (match_operand 0 "low_io_address_operand" "n"))
	(ior:QI (mem:QI (match_dup 0))
		(match_operand:QI 1 "single_one_operand" "n")))]
  "(optimize > 0)"
{
  operands[2] = GEN_INT (exact_log2 (INTVAL (operands[1]) & 0xff));
  return AS2 (sbi,%0-0x20,%2);
}
  [(set_attr "length" "1")
   (set_attr "cc" "none")])

;; Lower half of the I/O space - use sbic/sbis directly.
(define_insn "*sbix_branch"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "eqne_operator"
			 [(zero_extract
			   (mem:QI (match_operand 1 "low_io_address_operand" "n"))
			   (const_int 1)
			   (match_operand 2 "const_int_operand" "n"))
			  (const_int 0)])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))]
  "(optimize > 0)"
  "* return avr_out_sbxx_branch (insn, operands);"
  [(set (attr "length")
	(if_then_else (and (ge (minus (pc) (match_dup 3)) (const_int -2046))
			   (le (minus (pc) (match_dup 3)) (const_int 2046)))
		      (const_int 2)
		      (if_then_else (eq_attr "mcu_mega" "no")
				    (const_int 2)
				    (const_int 4))))
   (set_attr "cc" "clobber")])

;; Tests of bit 7 are pessimized to sign tests, so we need this too...
(define_insn "*sbix_branch_bit7"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "gelt_operator"
			 [(mem:QI (match_operand 1 "low_io_address_operand" "n"))
			  (const_int 0)])
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  "(optimize > 0)"
{
  operands[3] = operands[2];
  operands[2] = GEN_INT (7);
  return avr_out_sbxx_branch (insn, operands);
}
  [(set (attr "length")
	(if_then_else (and (ge (minus (pc) (match_dup 2)) (const_int -2046))
			   (le (minus (pc) (match_dup 2)) (const_int 2046)))
		      (const_int 2)
		      (if_then_else (eq_attr "mcu_mega" "no")
				    (const_int 2)
				    (const_int 4))))
   (set_attr "cc" "clobber")])

;; Upper half of the I/O space - read port to __tmp_reg__ and use sbrc/sbrs.
(define_insn "*sbix_branch_tmp"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "eqne_operator"
			 [(zero_extract
			   (mem:QI (match_operand 1 "high_io_address_operand" "n"))
			   (const_int 1)
			   (match_operand 2 "const_int_operand" "n"))
			  (const_int 0)])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))]
  "(optimize > 0)"
  "* return avr_out_sbxx_branch (insn, operands);"
  [(set (attr "length")
	(if_then_else (and (ge (minus (pc) (match_dup 3)) (const_int -2046))
			   (le (minus (pc) (match_dup 3)) (const_int 2045)))
		      (const_int 3)
		      (if_then_else (eq_attr "mcu_mega" "no")
				    (const_int 3)
				    (const_int 5))))
   (set_attr "cc" "clobber")])

(define_insn "*sbix_branch_tmp_bit7"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "gelt_operator"
			 [(mem:QI (match_operand 1 "high_io_address_operand" "n"))
			  (const_int 0)])
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  "(optimize > 0)"
{
  operands[3] = operands[2];
  operands[2] = GEN_INT (7);
  return avr_out_sbxx_branch (insn, operands);
}
  [(set (attr "length")
	(if_then_else (and (ge (minus (pc) (match_dup 2)) (const_int -2046))
			   (le (minus (pc) (match_dup 2)) (const_int 2045)))
		      (const_int 3)
		      (if_then_else (eq_attr "mcu_mega" "no")
				    (const_int 3)
				    (const_int 5))))
   (set_attr "cc" "clobber")])

;; ************************* Peepholes ********************************

(define_peephole
  [(set (match_operand:SI 0 "d_register_operand" "")
        (plus:SI (match_dup 0)
                 (const_int -1)))
   (parallel
    [(set (cc0)
          (compare (match_dup 0)
		   (const_int -1)))
     (clobber (match_operand:QI 1 "d_register_operand" ""))])
   (set (pc)
	(if_then_else (ne (cc0) (const_int 0))
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
  "*
{
  CC_STATUS_INIT;
  if (test_hard_reg_class (ADDW_REGS, operands[0]))
    output_asm_insn (AS2 (sbiw,%0,1) CR_TAB
		     AS2 (sbc,%C0,__zero_reg__) CR_TAB
		     AS2 (sbc,%D0,__zero_reg__) \"\\n\", operands);
  else
    output_asm_insn (AS2 (subi,%A0,1) CR_TAB
		     AS2 (sbc,%B0,__zero_reg__) CR_TAB
		     AS2 (sbc,%C0,__zero_reg__) CR_TAB
		     AS2 (sbc,%D0,__zero_reg__) \"\\n\", operands);
  switch (avr_jump_mode (operands[2],insn))
  {
    case 1:
      return AS1 (brcc,%2);
    case 2:
      return (AS1 (brcs,.+2) CR_TAB
              AS1 (rjmp,%2));
  }
  return (AS1 (brcs,.+4) CR_TAB
          AS1 (jmp,%2));
}")

(define_peephole
  [(set (match_operand:HI 0 "d_register_operand" "")
        (plus:HI (match_dup 0)
                 (const_int -1)))
   (parallel
    [(set (cc0)
          (compare (match_dup 0)
		   (const_int 65535)))
     (clobber (match_operand:QI 1 "d_register_operand" ""))])
   (set (pc)
	(if_then_else (ne (cc0) (const_int 0))
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
  "*
{
  CC_STATUS_INIT;
  if (test_hard_reg_class (ADDW_REGS, operands[0]))
    output_asm_insn (AS2 (sbiw,%0,1), operands);
  else
    output_asm_insn (AS2 (subi,%A0,1) CR_TAB
		     AS2 (sbc,%B0,__zero_reg__) \"\\n\", operands);
  switch (avr_jump_mode (operands[2],insn))
  {
    case 1:
      return AS1 (brcc,%2);
    case 2:
      return (AS1 (brcs,.+2) CR_TAB
              AS1 (rjmp,%2));
  }
  return (AS1 (brcs,.+4) CR_TAB
          AS1 (jmp,%2));
}")

(define_peephole
  [(set (match_operand:QI 0 "d_register_operand" "")
        (plus:QI (match_dup 0)
                 (const_int -1)))
   (set (cc0)
	(compare (match_dup 0)
		 (const_int -1)))
   (set (pc)
	(if_then_else (ne (cc0) (const_int 0))
		      (label_ref (match_operand 1 "" ""))
		      (pc)))]
  ""
  "*
{
  CC_STATUS_INIT;
  cc_status.value1 = operands[0];
  cc_status.flags |= CC_OVERFLOW_UNUSABLE;
  output_asm_insn (AS2 (subi,%A0,1), operands);
  switch (avr_jump_mode (operands[1],insn))
  {
    case 1:
      return AS1 (brcc,%1);
    case 2:
      return (AS1 (brcs,.+2) CR_TAB
              AS1 (rjmp,%1));
  }
  return (AS1 (brcs,.+4) CR_TAB
          AS1 (jmp,%1));
}")

(define_peephole
  [(set (cc0) (match_operand:QI 0 "register_operand" ""))
   (set (pc)
	(if_then_else (eq (cc0) (const_int 0))
		      (label_ref (match_operand 1 "" ""))
		      (pc)))]
  "jump_over_one_insn_p (insn, operands[1])"
  "cpse %0,__zero_reg__")

(define_peephole
  [(set (cc0)
        (compare (match_operand:QI 0 "register_operand" "")
		 (match_operand:QI 1 "register_operand" "")))
   (set (pc)
	(if_then_else (eq (cc0) (const_int 0))
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  "jump_over_one_insn_p (insn, operands[2])"
  "cpse %0,%1")

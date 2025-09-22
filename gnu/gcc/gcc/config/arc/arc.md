;; Machine description of the Argonaut ARC cpu for GNU C compiler
;; Copyright (C) 1994, 1997, 1998, 1999, 2000, 2004, 2005
;; Free Software Foundation, Inc.

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

;; See file "rtl.def" for documentation on define_insn, match_*, et. al.

;; ??? This is an old port, and is undoubtedly suffering from bit rot.

;; Insn type.  Used to default other attribute values.

(define_attr "type"
  "move,load,store,cmove,unary,binary,compare,shift,mul,uncond_branch,branch,call,call_no_delay_slot,multi,misc"
  (const_string "binary"))

;; Length (in # of insns, long immediate constants counted too).
;; ??? There's a nasty interaction between the conditional execution fsm
;; and insn lengths: insns with shimm values cannot be conditionally executed.
(define_attr "length" ""
  (cond [(eq_attr "type" "load")
	 (if_then_else (match_operand 1 "long_immediate_loadstore_operand" "")
		       (const_int 2) (const_int 1))

	 (eq_attr "type" "store")
	 (if_then_else (match_operand 0 "long_immediate_loadstore_operand" "")
		       (const_int 2) (const_int 1))

	 (eq_attr "type" "move,unary,compare")
	 (if_then_else (match_operand 1 "long_immediate_operand" "")
		       (const_int 2) (const_int 1))

	 (eq_attr "type" "binary,mul")
	 (if_then_else (match_operand 2 "long_immediate_operand" "")
		       (const_int 2) (const_int 1))

	 (eq_attr "type" "cmove")
	 (if_then_else (match_operand 2 "register_operand" "")
		       (const_int 1) (const_int 2))

	 (eq_attr "type" "multi") (const_int 2)
	]

	(const_int 1)))

;; The length here is the length of a single asm.  Unfortunately it might be
;; 1 or 2 so we must allow for 2.  That's ok though.  How often will users
;; lament asm's not being put in delay slots?
(define_asm_attributes
  [(set_attr "length" "2")
   (set_attr "type" "multi")])

;; Condition codes: this one is used by final_prescan_insn to speed up
;; conditionalizing instructions.  It saves having to scan the rtl to see if
;; it uses or alters the condition codes.

;; USE: This insn uses the condition codes (e.g.: a conditional branch).
;; CANUSE: This insn can use the condition codes (for conditional execution).
;; SET: All condition codes are set by this insn.
;; SET_ZN: the Z and N flags are set by this insn.
;; SET_ZNC: the Z, N, and C flags are set by this insn.
;; CLOB: The condition codes are set to unknown values by this insn.
;; NOCOND: This insn can't use and doesn't affect the condition codes.

(define_attr "cond" "use,canuse,set,set_zn,set_znc,clob,nocond"
  (cond [(and (eq_attr "type" "unary,binary,move")
	      (eq_attr "length" "1"))
	 (const_string "canuse")

	 (eq_attr "type" "compare")
	 (const_string "set")

	 (eq_attr "type" "cmove,branch")
	 (const_string "use")

	 (eq_attr "type" "multi,misc")
	 (const_string "clob")
	 ]

	 (const_string "nocond")))

;; Delay slots.

(define_attr "in_delay_slot" "false,true"
  (cond [(eq_attr "type" "uncond_branch,branch,call,call_no_delay_slot,multi")
	 (const_string "false")
	 ]

	 (if_then_else (eq_attr "length" "1")
		       (const_string "true")
		       (const_string "false"))))

(define_delay (eq_attr "type" "call")
  [(eq_attr "in_delay_slot" "true")
   (eq_attr "in_delay_slot" "true")
   (eq_attr "in_delay_slot" "true")])

(define_delay (eq_attr "type" "branch,uncond_branch")
  [(eq_attr "in_delay_slot" "true")
   (eq_attr "in_delay_slot" "true")
   (eq_attr "in_delay_slot" "true")])
   
;; Scheduling description for the ARC

(define_cpu_unit "branch")

(define_insn_reservation "any_insn" 1 (eq_attr "type" "!load,compare,branch")
			 "nothing")

;; 1) A conditional jump cannot immediately follow the insn setting the flags.
;; This isn't a complete solution as it doesn't come with guarantees.  That
;; is done in the branch patterns and in arc_print_operand.  This exists to
;; avoid inserting a nop when we can.

(define_insn_reservation "compare" 1 (eq_attr "type" "compare")
		         "nothing,branch")

(define_insn_reservation "branch" 1 (eq_attr "type" "branch")
		         "branch")

;; 2) References to loaded registers should wait a cycle.

;; Memory with load-delay of 1 (i.e., 2 cycle load).

(define_insn_reservation "memory" 2 (eq_attr "type" "load")
			 "nothing")

;; Move instructions.

(define_expand "movqi"
  [(set (match_operand:QI 0 "general_operand" "")
	(match_operand:QI 1 "general_operand" ""))]
  ""
  "
{
  /* Everything except mem = const or mem = mem can be done easily.  */

  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (QImode, operands[1]);
}")

(define_insn "*movqi_insn"
  [(set (match_operand:QI 0 "move_dest_operand" "=r,r,r,m")
	(match_operand:QI 1 "move_src_operand" "rI,Ji,m,r"))]
;; ??? Needed?
  "register_operand (operands[0], QImode)
   || register_operand (operands[1], QImode)"
  "@
   mov%? %0,%1
   mov%? %0,%1
   ldb%U1%V1 %0,%1
   stb%U0%V0 %1,%0"
  [(set_attr "type" "move,move,load,store")])

;; ??? This may never match since there's no cmpqi insn.

(define_insn "*movqi_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (sign_extend:SI (match_operand:QI 1 "move_src_operand" "rIJi"))
		       (const_int 0)))
   (set (match_operand:QI 0 "move_dest_operand" "=r")
	(match_dup 1))]
  ""
  "mov%?.f %0,%1"
  [(set_attr "type" "move")
   (set_attr "cond" "set_zn")])

(define_expand "movhi"
  [(set (match_operand:HI 0 "general_operand" "")
	(match_operand:HI 1 "general_operand" ""))]
  ""
  "
{
  /* Everything except mem = const or mem = mem can be done easily.  */

  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (HImode, operands[1]);
}")

(define_insn "*movhi_insn"
  [(set (match_operand:HI 0 "move_dest_operand" "=r,r,r,m")
	(match_operand:HI 1 "move_src_operand" "rI,Ji,m,r"))]
  "register_operand (operands[0], HImode)
   || register_operand (operands[1], HImode)"
  "@
   mov%? %0,%1
   mov%? %0,%1
   ldw%U1%V1 %0,%1
   stw%U0%V0 %1,%0"
  [(set_attr "type" "move,move,load,store")])

;; ??? Will this ever match?

(define_insn "*movhi_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (sign_extend:SI (match_operand:HI 1 "move_src_operand" "rIJi"))
		       (const_int 0)))
   (set (match_operand:HI 0 "move_dest_operand" "=r")
	(match_dup 1))]
;; ??? Needed?
  "register_operand (operands[0], HImode)
   || register_operand (operands[1], HImode)"
  "mov%?.f %0,%1"
  [(set_attr "type" "move")
   (set_attr "cond" "set_zn")])

(define_expand "movsi"
  [(set (match_operand:SI 0 "general_operand" "")
	(match_operand:SI 1 "general_operand" ""))]
  ""
  "
{
  /* Everything except mem = const or mem = mem can be done easily.  */

  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (SImode, operands[1]);
}")

(define_insn "*movsi_insn"
  [(set (match_operand:SI 0 "move_dest_operand" "=r,r,r,m")
	(match_operand:SI 1 "move_src_operand" "rI,GJi,m,r"))]
  "register_operand (operands[0], SImode)
   || register_operand (operands[1], SImode)"
  "@
   mov%? %0,%1
   mov%? %0,%S1
   ld%U1%V1 %0,%1
   st%U0%V0 %1,%0"
  [(set_attr "type" "move,move,load,store")])

(define_insn "*movsi_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (match_operand:SI 1 "move_src_operand" "rIJi")
		       (const_int 0)))
   (set (match_operand:SI 0 "move_dest_operand" "=r")
	(match_dup 1))]
  "register_operand (operands[0], SImode)
   || register_operand (operands[1], SImode)"
  "mov%?.f %0,%S1"
  [(set_attr "type" "move")
   (set_attr "cond" "set_zn")])

(define_expand "movdi"
  [(set (match_operand:DI 0 "general_operand" "")
	(match_operand:DI 1 "general_operand" ""))]
  ""
  "
{
  /* Everything except mem = const or mem = mem can be done easily.  */

  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (DImode, operands[1]);
}")

(define_insn "*movdi_insn"
  [(set (match_operand:DI 0 "move_dest_operand" "=r,r,r,m")
	(match_operand:DI 1 "move_double_src_operand" "r,HK,m,r"))]
  "register_operand (operands[0], DImode)
   || register_operand (operands[1], DImode)"
  "*
{
  switch (which_alternative)
    {
    case 0 :
      /* We normally copy the low-numbered register first.  However, if
	 the first register operand 0 is the same as the second register of
	 operand 1, we must copy in the opposite order.  */
      if (REGNO (operands[0]) == REGNO (operands[1]) + 1)
	return \"mov %R0,%R1\;mov %0,%1\";
      else
	return \"mov %0,%1\;mov %R0,%R1\";
    case 1 :
      return \"mov %0,%L1\;mov %R0,%H1\";
    case 2 :
      /* If the low-address word is used in the address, we must load it
	 last.  Otherwise, load it first.  Note that we cannot have
	 auto-increment in that case since the address register is known to be
	 dead.  */
      if (refers_to_regno_p (REGNO (operands[0]), REGNO (operands[0]) + 1,
			     operands [1], 0))
	  return \"ld%V1 %R0,%R1\;ld%V1 %0,%1\";
      else
	  return \"ld%V1 %0,%1\;ld%V1 %R0,%R1\";
    case 3 :
      return \"st%V0 %1,%0\;st%V0 %R1,%R0\";
    default:
      gcc_unreachable ();
    }
}"
  [(set_attr "type" "move,move,load,store")
   ;; ??? The ld/st values could be 4 if it's [reg,bignum].
   (set_attr "length" "2,4,2,2")])

;(define_expand "movdi"
;  [(set (match_operand:DI 0 "general_operand" "")
;	(match_operand:DI 1 "general_operand" ""))]
;  ""
;  "
;{
;  /* Flow doesn't understand that this is effectively a DFmode move.
;     It doesn't know that all of `operands[0]' is set.  */
;  emit_insn (gen_rtx_CLOBBER (VOIDmode, operands[0]));
;
;  /* Emit insns that movsi_insn can handle.  */
;  emit_insn (gen_movsi (operand_subword (operands[0], 0, 0, DImode),
;			operand_subword (operands[1], 0, 0, DImode)));
;  emit_insn (gen_movsi (operand_subword (operands[0], 1, 0, DImode),
;			operand_subword (operands[1], 1, 0, DImode)));
;  DONE;
;}")

;; Floating point move insns.

(define_expand "movsf"
  [(set (match_operand:SF 0 "general_operand" "")
	(match_operand:SF 1 "general_operand" ""))]
  ""
  "
{
  /* Everything except mem = const or mem = mem can be done easily.  */
  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (SFmode, operands[1]);
}")

(define_insn "*movsf_insn"
  [(set (match_operand:SF 0 "move_dest_operand" "=r,r,r,m")
	(match_operand:SF 1 "move_src_operand" "r,E,m,r"))]
  "register_operand (operands[0], SFmode)
   || register_operand (operands[1], SFmode)"
  "@
   mov%? %0,%1
   mov%? %0,%1 ; %A1
   ld%U1%V1 %0,%1
   st%U0%V0 %1,%0"
  [(set_attr "type" "move,move,load,store")])

(define_expand "movdf"
  [(set (match_operand:DF 0 "general_operand" "")
	(match_operand:DF 1 "general_operand" ""))]
  ""
  "
{
  /* Everything except mem = const or mem = mem can be done easily.  */
  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (DFmode, operands[1]);
}")

(define_insn "*movdf_insn"
  [(set (match_operand:DF 0 "move_dest_operand" "=r,r,r,m")
	(match_operand:DF 1 "move_double_src_operand" "r,E,m,r"))]
  "register_operand (operands[0], DFmode)
   || register_operand (operands[1], DFmode)"
  "*
{
  switch (which_alternative)
    {
    case 0 :
      /* We normally copy the low-numbered register first.  However, if
	 the first register operand 0 is the same as the second register of
	 operand 1, we must copy in the opposite order.  */
      if (REGNO (operands[0]) == REGNO (operands[1]) + 1)
	return \"mov %R0,%R1\;mov %0,%1\";
      else
	return \"mov %0,%1\;mov %R0,%R1\";
    case 1 :
      return \"mov %0,%L1\;mov %R0,%H1 ; %A1\";
    case 2 :
      /* If the low-address word is used in the address, we must load it
	 last.  Otherwise, load it first.  Note that we cannot have
	 auto-increment in that case since the address register is known to be
	 dead.  */
      if (refers_to_regno_p (REGNO (operands[0]), REGNO (operands[0]) + 1,
			     operands [1], 0))
	return \"ld%V1 %R0,%R1\;ld%V1 %0,%1\";
      else
	return \"ld%V1 %0,%1\;ld%V1 %R0,%R1\";
    case 3 :
      return \"st%V0 %1,%0\;st%V0 %R1,%R0\";
    default:
      gcc_unreachable ();
    }
}"
  [(set_attr "type" "move,move,load,store")
   ;; ??? The ld/st values could be 4 if it's [reg,bignum].
   (set_attr "length" "2,4,2,2")])

;(define_expand "movdf"
;  [(set (match_operand:DF 0 "general_operand" "")
;	(match_operand:DF 1 "general_operand" ""))]
;  ""
;  "
;{
;  /* Flow doesn't understand that this is effectively a DFmode move.
;     It doesn't know that all of `operands[0]' is set.  */
;  emit_insn (gen_rtx_CLOBBER (VOIDmode, operands[0]));
;
;  /* Emit insns that movsi_insn can handle.  */
;  emit_insn (gen_movsi (operand_subword (operands[0], 0, 0, DFmode),
;			operand_subword (operands[1], 0, 0, DFmode)));
;  emit_insn (gen_movsi (operand_subword (operands[0], 1, 0, DFmode),
;			operand_subword (operands[1], 1, 0, DFmode)));
;  DONE;
;}")

;; Load/Store with update instructions.
;;
;; Some of these we can get by using pre-decrement or pre-increment, but the
;; hardware can also do cases where the increment is not the size of the
;; object.
;;
;; In all these cases, we use operands 0 and 1 for the register being
;; incremented because those are the operands that local-alloc will
;; tie and these are the pair most likely to be tieable (and the ones
;; that will benefit the most).
;;
;; We use match_operator here because we need to know whether the memory
;; object is volatile or not.

(define_insn "*loadqi_update"
  [(set (match_operand:QI 3 "register_operand" "=r,r")
	(match_operator:QI 4 "load_update_operand"
	 [(match_operand:SI 1 "register_operand" "0,0")
	  (match_operand:SI 2 "nonmemory_operand" "rI,J")]))
   (set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "ldb.a%V4 %3,[%0,%2]"
  [(set_attr "type" "load,load")
   (set_attr "length" "1,2")])

(define_insn "*load_zeroextendqisi_update"
  [(set (match_operand:SI 3 "register_operand" "=r,r")
	(zero_extend:SI (match_operator:QI 4 "load_update_operand"
			 [(match_operand:SI 1 "register_operand" "0,0")
			  (match_operand:SI 2 "nonmemory_operand" "rI,J")])))
   (set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "ldb.a%V4 %3,[%0,%2]"
  [(set_attr "type" "load,load")
   (set_attr "length" "1,2")])

(define_insn "*load_signextendqisi_update"
  [(set (match_operand:SI 3 "register_operand" "=r,r")
	(sign_extend:SI (match_operator:QI 4 "load_update_operand"
			 [(match_operand:SI 1 "register_operand" "0,0")
			  (match_operand:SI 2 "nonmemory_operand" "rI,J")])))
   (set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "ldb.x.a%V4 %3,[%0,%2]"
  [(set_attr "type" "load,load")
   (set_attr "length" "1,2")])

(define_insn "*storeqi_update"
  [(set (match_operator:QI 4 "store_update_operand"
	 [(match_operand:SI 1 "register_operand" "0")
	  (match_operand:SI 2 "short_immediate_operand" "I")])
	(match_operand:QI 3 "register_operand" "r"))
   (set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "stb.a%V4 %3,[%0,%2]"
  [(set_attr "type" "store")
   (set_attr "length" "1")])

(define_insn "*loadhi_update"
  [(set (match_operand:HI 3 "register_operand" "=r,r")
	(match_operator:HI 4 "load_update_operand"
	 [(match_operand:SI 1 "register_operand" "0,0")
	  (match_operand:SI 2 "nonmemory_operand" "rI,J")]))
   (set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "ldw.a%V4 %3,[%0,%2]"
  [(set_attr "type" "load,load")
   (set_attr "length" "1,2")])

(define_insn "*load_zeroextendhisi_update"
  [(set (match_operand:SI 3 "register_operand" "=r,r")
	(zero_extend:SI (match_operator:HI 4 "load_update_operand"
			 [(match_operand:SI 1 "register_operand" "0,0")
			  (match_operand:SI 2 "nonmemory_operand" "rI,J")])))
   (set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "ldw.a%V4 %3,[%0,%2]"
  [(set_attr "type" "load,load")
   (set_attr "length" "1,2")])

(define_insn "*load_signextendhisi_update"
  [(set (match_operand:SI 3 "register_operand" "=r,r")
	(sign_extend:SI (match_operator:HI 4 "load_update_operand"
			 [(match_operand:SI 1 "register_operand" "0,0")
			  (match_operand:SI 2 "nonmemory_operand" "rI,J")])))
   (set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "ldw.x.a%V4 %3,[%0,%2]"
  [(set_attr "type" "load,load")
   (set_attr "length" "1,2")])

(define_insn "*storehi_update"
  [(set (match_operator:HI 4 "store_update_operand"
	 [(match_operand:SI 1 "register_operand" "0")
	  (match_operand:SI 2 "short_immediate_operand" "I")])
	(match_operand:HI 3 "register_operand" "r"))
   (set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "stw.a%V4 %3,[%0,%2]"
  [(set_attr "type" "store")
   (set_attr "length" "1")])

(define_insn "*loadsi_update"
  [(set (match_operand:SI 3 "register_operand" "=r,r")
	(match_operator:SI 4 "load_update_operand"
	 [(match_operand:SI 1 "register_operand" "0,0")
	  (match_operand:SI 2 "nonmemory_operand" "rI,J")]))
   (set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "ld.a%V4 %3,[%0,%2]"
  [(set_attr "type" "load,load")
   (set_attr "length" "1,2")])

(define_insn "*storesi_update"
  [(set (match_operator:SI 4 "store_update_operand"
	 [(match_operand:SI 1 "register_operand" "0")
	  (match_operand:SI 2 "short_immediate_operand" "I")])
	(match_operand:SI 3 "register_operand" "r"))
   (set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "st.a%V4 %3,[%0,%2]"
  [(set_attr "type" "store")
   (set_attr "length" "1")])

(define_insn "*loadsf_update"
  [(set (match_operand:SF 3 "register_operand" "=r,r")
	(match_operator:SF 4 "load_update_operand"
	 [(match_operand:SI 1 "register_operand" "0,0")
	  (match_operand:SI 2 "nonmemory_operand" "rI,J")]))
   (set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "ld.a%V4 %3,[%0,%2]"
  [(set_attr "type" "load,load")
   (set_attr "length" "1,2")])

(define_insn "*storesf_update"
  [(set (match_operator:SF 4 "store_update_operand"
	 [(match_operand:SI 1 "register_operand" "0")
	  (match_operand:SI 2 "short_immediate_operand" "I")])
	(match_operand:SF 3 "register_operand" "r"))
   (set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "st.a%V4 %3,[%0,%2]"
  [(set_attr "type" "store")
   (set_attr "length" "1")])

;; Conditional move instructions.

(define_expand "movsicc"
  [(set (match_operand:SI 0 "register_operand" "")
	(if_then_else:SI (match_operand 1 "comparison_operator" "")
			 (match_operand:SI 2 "nonmemory_operand" "")
			 (match_operand:SI 3 "register_operand" "")))]
  ""
  "
{
  enum rtx_code code = GET_CODE (operands[1]);
  rtx ccreg
    = gen_rtx_REG (SELECT_CC_MODE (code, arc_compare_op0, arc_compare_op1),
		   61);

  operands[1] = gen_rtx_fmt_ee (code, VOIDmode, ccreg, const0_rtx);
}")

;(define_expand "movdicc"
;  [(set (match_operand:DI 0 "register_operand" "")
;	(if_then_else:DI (match_operand 1 "comparison_operator" "")
;			 (match_operand:DI 2 "nonmemory_operand" "")
;			 (match_operand:DI 3 "register_operand" "")))]
;  "0 /* ??? this would work better if we had cmpdi */"
;  "
;{
;  enum rtx_code code = GET_CODE (operands[1]);
;  rtx ccreg
;   = gen_rtx_REG (SELECT_CC_MODE (code, arc_compare_op0, arc_compare_op1),
;		   61);
;
;  operands[1] = gen_rtx_fmt_ee (code, VOIDmode, ccreg, const0_rtx);
;}")

(define_expand "movsfcc"
  [(set (match_operand:SF 0 "register_operand" "")
	(if_then_else:SF (match_operand 1 "comparison_operator" "")
			 (match_operand:SF 2 "nonmemory_operand" "")
			 (match_operand:SF 3 "register_operand" "")))]
  ""
  "
{
  enum rtx_code code = GET_CODE (operands[1]);
  rtx ccreg
    = gen_rtx_REG (SELECT_CC_MODE (code, arc_compare_op0, arc_compare_op1),
		   61);

  operands[1] = gen_rtx_fmt_ee (code, VOIDmode, ccreg, const0_rtx);
}")

;(define_expand "movdfcc"
;  [(set (match_operand:DF 0 "register_operand" "")
;	(if_then_else:DF (match_operand 1 "comparison_operator" "")
;			 (match_operand:DF 2 "nonmemory_operand" "")
;			 (match_operand:DF 3 "register_operand" "")))]
;  "0 /* ??? can generate less efficient code if constants involved */"
;  "
;{
; enum rtx_code code = GET_CODE (operands[1]);
; rtx ccreg
;   = gen_rtx_REG (SELECT_CC_MODE (code, arc_compare_op0, arc_compare_op1),
;		   61);
;
;  operands[1] = gen_rtx_fmt_ee (code, VOIDmode, ccreg, const0_rtx);
;}")

(define_insn "*movsicc_insn"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(if_then_else:SI (match_operand 1 "comparison_operator" "")
			 (match_operand:SI 2 "nonmemory_operand" "rJi")
			 (match_operand:SI 3 "register_operand" "0")))]
  ""
  "mov.%d1 %0,%S2"
  [(set_attr "type" "cmove")])

; ??? This doesn't properly handle constants.
;(define_insn "*movdicc_insn"
;  [(set (match_operand:DI 0 "register_operand" "=r,r")
;	(if_then_else:DI (match_operand 1 "comparison_operator" "")
;			 (match_operand:DI 2 "nonmemory_operand" "r,Ji")
;			 (match_operand:DI 3 "register_operand" "0,0")))]
;  "0"
;  "*
;{
;  switch (which_alternative)
;    {
;    case 0 :
;      /* We normally copy the low-numbered register first.  However, if
;	 the first register operand 0 is the same as the second register of
;	 operand 1, we must copy in the opposite order.  */
;      if (REGNO (operands[0]) == REGNO (operands[2]) + 1)
;	return \"mov.%d1 %R0,%R2\;mov.%d1 %0,%2\";
;      else
;	return \"mov.%d1 %0,%2\;mov.%d1 %R0,%R2\";
;    case 1 :
;      return \"mov.%d1 %0,%2\;mov.%d1 %R0,%R2\";
;    }
;}"
;  [(set_attr "type" "cmove,cmove")
;   (set_attr "length" "2,4")])

(define_insn "*movsfcc_insn"
  [(set (match_operand:SF 0 "register_operand" "=r,r")
	(if_then_else:SF (match_operand 1 "comparison_operator" "")
			 (match_operand:SF 2 "nonmemory_operand" "r,E")
			 (match_operand:SF 3 "register_operand" "0,0")))]
  ""
  "@
   mov.%d1 %0,%2
   mov.%d1 %0,%2 ; %A2"
  [(set_attr "type" "cmove,cmove")])

;(define_insn "*movdfcc_insn"
;  [(set (match_operand:DF 0 "register_operand" "=r,r")
;	(if_then_else:DF (match_operand 1 "comparison_operator" "")
;			 (match_operand:DF 2 "nonmemory_operand" "r,E")
;			 (match_operand:DF 3 "register_operand" "0,0")))]
;  "0"
;  "*
;{
;  switch (which_alternative)
;    {
;    case 0 :
;      /* We normally copy the low-numbered register first.  However, if
;	 the first register operand 0 is the same as the second register of
;	 operand 1, we must copy in the opposite order.  */
;      if (REGNO (operands[0]) == REGNO (operands[2]) + 1)
;	return \"mov.%d1 %R0,%R2\;mov.%d1 %0,%2\";
;      else
;	return \"mov.%d1 %0,%2\;mov.%d1 %R0,%R2\";
;    case 1 :
;      return \"mov.%d1 %0,%L2\;mov.%d1 %R0,%H2 ; %A2\";
;    }
;}"
;  [(set_attr "type" "cmove,cmove")
;   (set_attr "length" "2,4")])

;; Zero extension instructions.
;; ??? We don't support volatile memrefs here, but I'm not sure why.

(define_insn "zero_extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(zero_extend:HI (match_operand:QI 1 "nonvol_nonimm_operand" "r,m")))]
  ""
  "@
   extb%? %0,%1
   ldb%U1 %0,%1"
  [(set_attr "type" "unary,load")])

(define_insn "*zero_extendqihi2_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (zero_extend:SI (match_operand:QI 1 "register_operand" "r"))
		       (const_int 0)))
   (set (match_operand:HI 0 "register_operand" "=r")
	(zero_extend:HI (match_dup 1)))]
  ""
  "extb%?.f %0,%1"
  [(set_attr "type" "unary")
   (set_attr "cond" "set_zn")])

(define_insn "zero_extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(zero_extend:SI (match_operand:QI 1 "nonvol_nonimm_operand" "r,m")))]
  ""
  "@
   extb%? %0,%1
   ldb%U1 %0,%1"
  [(set_attr "type" "unary,load")])

(define_insn "*zero_extendqisi2_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (zero_extend:SI (match_operand:QI 1 "register_operand" "r"))
		       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (match_dup 1)))]
  ""
  "extb%?.f %0,%1"
  [(set_attr "type" "unary")
   (set_attr "cond" "set_zn")])

(define_insn "zero_extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(zero_extend:SI (match_operand:HI 1 "nonvol_nonimm_operand" "r,m")))]
  ""
  "@
   extw%? %0,%1
   ldw%U1 %0,%1"
  [(set_attr "type" "unary,load")])

(define_insn "*zero_extendhisi2_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (zero_extend:SI (match_operand:HI 1 "register_operand" "r"))
		       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (match_dup 1)))]
  ""
  "extw%?.f %0,%1"
  [(set_attr "type" "unary")
   (set_attr "cond" "set_zn")])

;; Sign extension instructions.

(define_insn "extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(sign_extend:HI (match_operand:QI 1 "nonvol_nonimm_operand" "r,m")))]
  ""
  "@
   sexb%? %0,%1
   ldb.x%U1 %0,%1"
  [(set_attr "type" "unary,load")])

(define_insn "*extendqihi2_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (sign_extend:SI (match_operand:QI 1 "register_operand" "r"))
		       (const_int 0)))
   (set (match_operand:HI 0 "register_operand" "=r")
	(sign_extend:HI (match_dup 1)))]
  ""
  "sexb%?.f %0,%1"
  [(set_attr "type" "unary")
   (set_attr "cond" "set_zn")])

(define_insn "extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(sign_extend:SI (match_operand:QI 1 "nonvol_nonimm_operand" "r,m")))]
  ""
  "@
   sexb%? %0,%1
   ldb.x%U1 %0,%1"
  [(set_attr "type" "unary,load")])

(define_insn "*extendqisi2_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (sign_extend:SI (match_operand:QI 1 "register_operand" "r"))
		       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (match_dup 1)))]
  ""
  "sexb%?.f %0,%1"
  [(set_attr "type" "unary")
   (set_attr "cond" "set_zn")])

(define_insn "extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(sign_extend:SI (match_operand:HI 1 "nonvol_nonimm_operand" "r,m")))]
  ""
  "@
   sexw%? %0,%1
   ldw.x%U1 %0,%1"
  [(set_attr "type" "unary,load")])

(define_insn "*extendhisi2_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (sign_extend:SI (match_operand:HI 1 "register_operand" "r"))
		       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (match_dup 1)))]
  ""
  "sexw%?.f %0,%1"
  [(set_attr "type" "unary")
   (set_attr "cond" "set_zn")])

;; Arithmetic instructions.

(define_insn "addsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (match_operand:SI 1 "register_operand" "%r")
		 (match_operand:SI 2 "nonmemory_operand" "rIJ")))]
  ""
  "add%? %0,%1,%2")

(define_insn "*addsi3_set_cc_insn"
  [(set (reg:CC 61) (compare:CC
		     (plus:SI (match_operand:SI 1 "register_operand" "%r")
			      (match_operand:SI 2 "nonmemory_operand" "rIJ"))
		     (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (match_dup 1)
		 (match_dup 2)))]
  ""
  "add%?.f %0,%1,%2"
  [(set_attr "cond" "set")])

(define_insn "adddi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_operand:DI 1 "nonmemory_operand" "%r")
		 (match_operand:DI 2 "nonmemory_operand" "ri")))
   (clobber (reg:CC 61))]
  ""
  "*
{
  rtx op2 = operands[2];

  if (GET_CODE (op2) == CONST_INT)
    {
      int sign = INTVAL (op2);
      if (sign < 0)
	return \"add.f %L0,%L1,%2\;adc %H0,%H1,-1\";
      else
	return \"add.f %L0,%L1,%2\;adc %H0,%H1,0\";
    }
  else
    return \"add.f %L0,%L1,%L2\;adc %H0,%H1,%H2\";
}"
  [(set_attr "length" "2")])

(define_insn "subsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_operand:SI 1 "register_operand" "r")
		  (match_operand:SI 2 "nonmemory_operand" "rIJ")))]
  ""
  "sub%? %0,%1,%2")

(define_insn "*subsi3_set_cc_insn"
  [(set (reg:CC 61) (compare:CC
		     (minus:SI (match_operand:SI 1 "register_operand" "%r")
			       (match_operand:SI 2 "nonmemory_operand" "rIJ"))
		     (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_dup 1)
		  (match_dup 2)))]
  ""
  "sub%?.f %0,%1,%2"
  [(set_attr "cond" "set")])

(define_insn "subdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (match_operand:DI 1 "nonmemory_operand" "r")
		  (match_operand:DI 2 "nonmemory_operand" "ri")))
   (clobber (reg:CC 61))]
  ""
  "*
{
  rtx op2 = operands[2];

  if (GET_CODE (op2) == CONST_INT)
    {
      int sign = INTVAL (op2);
      if (sign < 0)
	return \"sub.f %L0,%L1,%2\;sbc %H0,%H1,-1\";
      else
	return \"sub.f %L0,%L1,%2\;sbc %H0,%H1,0\";
    }
  else
    return \"sub.f %L0,%L1,%L2\;sbc %H0,%H1,%H2\";
}"
  [(set_attr "length" "2")])

;; Boolean instructions.
;;
;; We don't define the DImode versions as expand_binop does a good enough job.

(define_insn "andsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(and:SI (match_operand:SI 1 "register_operand" "%r")
		(match_operand:SI 2 "nonmemory_operand" "rIJ")))]
  ""
  "and%? %0,%1,%2")

(define_insn "*andsi3_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (and:SI (match_operand:SI 1 "register_operand" "%r")
			       (match_operand:SI 2 "nonmemory_operand" "rIJ"))
		       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(and:SI (match_dup 1)
		(match_dup 2)))]
  ""
  "and%?.f %0,%1,%2"
  [(set_attr "cond" "set_zn")])

(define_insn "*bicsi3_insn"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r")
	(and:SI (match_operand:SI 1 "nonmemory_operand" "r,r,I,J")
		(not:SI (match_operand:SI 2 "nonmemory_operand" "rI,J,r,r"))))]
  ""
  "bic%? %0,%1,%2"
  [(set_attr "length" "1,2,1,2")])

(define_insn "*bicsi3_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (and:SI (match_operand:SI 1 "register_operand" "%r")
			       (not:SI (match_operand:SI 2 "nonmemory_operand" "rIJ")))
		       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(and:SI (match_dup 1)
		(not:SI (match_dup 2))))]
  ""
  "bic%?.f %0,%1,%2"
  [(set_attr "cond" "set_zn")])

(define_insn "iorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ior:SI (match_operand:SI 1 "register_operand" "%r")
		(match_operand:SI 2 "nonmemory_operand" "rIJ")))]
  ""
  "or%? %0,%1,%2")

(define_insn "*iorsi3_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (ior:SI (match_operand:SI 1 "register_operand" "%r")
			       (match_operand:SI 2 "nonmemory_operand" "rIJ"))
		       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(ior:SI (match_dup 1)
		(match_dup 2)))]
  ""
  "or%?.f %0,%1,%2"
  [(set_attr "cond" "set_zn")])

(define_insn "xorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(xor:SI (match_operand:SI 1 "register_operand" "%r")
		(match_operand:SI 2 "nonmemory_operand" "rIJ")))]
  ""
  "xor%? %0,%1,%2")

(define_insn "*xorsi3_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (xor:SI (match_operand:SI 1 "register_operand" "%r")
			       (match_operand:SI 2 "nonmemory_operand" "rIJ"))
		       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(xor:SI (match_dup 1)
		(match_dup 2)))]
  ""
  "xor%?.f %0,%1,%2"
  [(set_attr "cond" "set_zn")])

(define_insn "negsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (match_operand:SI 1 "register_operand" "r")))]
  ""
  "sub%? %0,0,%1"
  [(set_attr "type" "unary")])

(define_insn "*negsi2_set_cc_insn"
  [(set (reg:CC 61) (compare:CC
		     (neg:SI (match_operand:SI 1 "register_operand" "r"))
		     (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (match_dup 1)))]
  ""
  "sub%?.f %0,0,%1"
  [(set_attr "type" "unary")
   (set_attr "cond" "set")])

(define_insn "negdi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(neg:DI (match_operand:DI 1 "register_operand" "r")))
   (clobber (reg:SI 61))]
  ""
  "sub.f %L0,0,%L1\;sbc %H0,0,%H1"
  [(set_attr "type" "unary")
   (set_attr "length" "2")])

(define_insn "one_cmplsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(not:SI (match_operand:SI 1 "register_operand" "r")))]
  ""
  "xor%? %0,%1,-1"
  [(set_attr "type" "unary")])

(define_insn "*one_cmplsi2_set_cc_insn"
  [(set (reg:CCZN 61) (compare:CCZN
		       (not:SI (match_operand:SI 1 "register_operand" "r"))
		       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(not:SI (match_dup 1)))]
  ""
  "xor%?.f %0,%1,-1"
  [(set_attr "type" "unary")
   (set_attr "cond" "set_zn")])

;; Shift instructions.

(define_expand "ashlsi3"
  [(set (match_operand:SI 0 "register_operand" "")
	(ashift:SI (match_operand:SI 1 "register_operand" "")
		   (match_operand:SI 2 "nonmemory_operand" "")))]
  ""
  "
{
  if (! TARGET_SHIFTER)
    {
      emit_insn (gen_rtx_PARALLEL
		 (VOIDmode,
		  gen_rtvec (2,
			     gen_rtx_SET (VOIDmode, operands[0],
					  gen_rtx_ASHIFT (SImode, operands[1],
							  operands[2])),
			     gen_rtx_CLOBBER (VOIDmode,
					      gen_rtx_SCRATCH (SImode)))));
      DONE;
    }
}")

(define_expand "ashrsi3"
  [(set (match_operand:SI 0 "register_operand" "")
	(ashiftrt:SI (match_operand:SI 1 "register_operand" "")
		     (match_operand:SI 2 "nonmemory_operand" "")))]
  ""
  "
{
  if (! TARGET_SHIFTER)
    {
      emit_insn (gen_rtx_PARALLEL
		 (VOIDmode,
		  gen_rtvec (2,
			     gen_rtx_SET (VOIDmode, operands[0],
					  gen_rtx_ASHIFTRT (SImode,
							    operands[1],
							    operands[2])),
			     gen_rtx_CLOBBER (VOIDmode,
					      gen_rtx_SCRATCH (SImode)))));
      DONE;
    }
}")

(define_expand "lshrsi3"
  [(set (match_operand:SI 0 "register_operand" "")
	(lshiftrt:SI (match_operand:SI 1 "register_operand" "")
		     (match_operand:SI 2 "nonmemory_operand" "")))]
  ""
  "
{
  if (! TARGET_SHIFTER)
    {
      emit_insn (gen_rtx_PARALLEL
		 (VOIDmode,
		  gen_rtvec (2,
			     gen_rtx_SET (VOIDmode, operands[0],
					  gen_rtx_LSHIFTRT (SImode,
							    operands[1],
							    operands[2])),
			     gen_rtx_CLOBBER (VOIDmode,
					      gen_rtx_SCRATCH (SImode)))));
      DONE;
    }
}")

(define_insn "*ashlsi3_insn"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r")
	(ashift:SI (match_operand:SI 1 "nonmemory_operand" "r,r,I,J")
		   (match_operand:SI 2 "nonmemory_operand" "rI,J,r,r")))]
  "TARGET_SHIFTER"
  "asl%? %0,%1,%2"
  [(set_attr "type" "shift")
   (set_attr "length" "1,2,1,2")])

(define_insn "*ashrsi3_insn"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r")
	(ashiftrt:SI (match_operand:SI 1 "nonmemory_operand" "r,r,I,J")
		     (match_operand:SI 2 "nonmemory_operand" "rI,J,r,r")))]
  "TARGET_SHIFTER"
  "asr%? %0,%1,%2"
  [(set_attr "type" "shift")
   (set_attr "length" "1,2,1,2")])

(define_insn "*lshrsi3_insn"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r")
	(lshiftrt:SI (match_operand:SI 1 "nonmemory_operand" "r,r,I,J")
		     (match_operand:SI 2 "nonmemory_operand" "rI,J,r,r")))]
  "TARGET_SHIFTER"
  "lsr%? %0,%1,%2"
  [(set_attr "type" "shift")
   (set_attr "length" "1,2,1,2")])

(define_insn "*shift_si3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(match_operator:SI 3 "shift_operator"
			   [(match_operand:SI 1 "register_operand" "0")
			    (match_operand:SI 2 "nonmemory_operand" "rIJ")]))
   (clobber (match_scratch:SI 4 "=&r"))]
  "! TARGET_SHIFTER"
  "* return output_shift (operands);"
  [(set_attr "type" "shift")
   (set_attr "length" "8")])

;; Compare instructions.
;; This controls RTL generation and register allocation.

;; We generate RTL for comparisons and branches by having the cmpxx 
;; patterns store away the operands.  Then, the scc and bcc patterns
;; emit RTL for both the compare and the branch.

(define_expand "cmpsi"
  [(set (reg:CC 61)
	(compare:CC (match_operand:SI 0 "register_operand" "")
		    (match_operand:SI 1 "nonmemory_operand" "")))]
  ""
  "
{
  arc_compare_op0 = operands[0];
  arc_compare_op1 = operands[1];
  DONE;
}")

;; ??? We may be able to relax this a bit by adding a new constant 'K' for 0.
;; This assumes sub.f 0,symbol,0 is a valid insn.
;; Note that "sub.f 0,r0,1" is an 8 byte insn.  To avoid unnecessarily
;; creating 8 byte insns we duplicate %1 in the destination reg of the insn
;; if it's a small constant.

(define_insn "*cmpsi_cc_insn"
  [(set (reg:CC 61)
	(compare:CC (match_operand:SI 0 "register_operand" "r,r,r")
		    (match_operand:SI 1 "nonmemory_operand" "r,I,J")))]
  ""
  "@
   sub.f 0,%0,%1
   sub.f %1,%0,%1
   sub.f 0,%0,%1"
  [(set_attr "type" "compare,compare,compare")])

(define_insn "*cmpsi_cczn_insn"
  [(set (reg:CCZN 61)
	(compare:CCZN (match_operand:SI 0 "register_operand" "r,r,r")
		      (match_operand:SI 1 "nonmemory_operand" "r,I,J")))]
  ""
  "@
   sub.f 0,%0,%1
   sub.f %1,%0,%1
   sub.f 0,%0,%1"
  [(set_attr "type" "compare,compare,compare")])

(define_insn "*cmpsi_ccznc_insn"
  [(set (reg:CCZNC 61)
	(compare:CCZNC (match_operand:SI 0 "register_operand" "r,r,r")
		       (match_operand:SI 1 "nonmemory_operand" "r,I,J")))]
  ""
  "@
   sub.f 0,%0,%1
   sub.f %1,%0,%1
   sub.f 0,%0,%1"
  [(set_attr "type" "compare,compare,compare")])

;; Next come the scc insns.

(define_expand "seq"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(eq:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = gen_compare_reg (EQ, arc_compare_op0, arc_compare_op1);
}")

(define_expand "sne"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ne:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = gen_compare_reg (NE, arc_compare_op0, arc_compare_op1);
}")

(define_expand "sgt"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(gt:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = gen_compare_reg (GT, arc_compare_op0, arc_compare_op1);
}")

(define_expand "sle"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(le:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = gen_compare_reg (LE, arc_compare_op0, arc_compare_op1);
}")

(define_expand "sge"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ge:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = gen_compare_reg (GE, arc_compare_op0, arc_compare_op1);
}")

(define_expand "slt"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lt:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = gen_compare_reg (LT, arc_compare_op0, arc_compare_op1);
}")

(define_expand "sgtu"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(gtu:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = gen_compare_reg (GTU, arc_compare_op0, arc_compare_op1);
}")

(define_expand "sleu"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(leu:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = gen_compare_reg (LEU, arc_compare_op0, arc_compare_op1);
}")

(define_expand "sgeu"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(geu:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = gen_compare_reg (GEU, arc_compare_op0, arc_compare_op1);
}")

(define_expand "sltu"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ltu:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = gen_compare_reg (LTU, arc_compare_op0, arc_compare_op1);
}")

(define_insn "*scc_insn"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(match_operator:SI 1 "comparison_operator" [(reg 61) (const_int 0)]))]
  ""
  "mov %0,1\;sub.%D1 %0,%0,%0"
  [(set_attr "type" "unary")
   (set_attr "length" "2")])

;; ??? Look up negscc insn.  See pa.md for example.
(define_insn "*neg_scc_insn"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (match_operator:SI 1 "comparison_operator"
		 [(reg 61) (const_int 0)])))]
  ""
  "mov %0,-1\;sub.%D1 %0,%0,%0"
  [(set_attr "type" "unary")
   (set_attr "length" "2")])

(define_insn "*not_scc_insn"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(not:SI (match_operator:SI 1 "comparison_operator"
		 [(reg 61) (const_int 0)])))]
  ""
  "mov %0,1\;sub.%d1 %0,%0,%0"
  [(set_attr "type" "unary")
   (set_attr "length" "2")])

;; These control RTL generation for conditional jump insns

(define_expand "beq"
  [(set (pc)
	(if_then_else (eq (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1] = gen_compare_reg (EQ, arc_compare_op0, arc_compare_op1);
}")

(define_expand "bne"
  [(set (pc)
	(if_then_else (ne (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1] = gen_compare_reg (NE, arc_compare_op0, arc_compare_op1);
}")

(define_expand "bgt"
  [(set (pc)
	(if_then_else (gt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1] = gen_compare_reg (GT, arc_compare_op0, arc_compare_op1);
}")

(define_expand "ble"
  [(set (pc)
	(if_then_else (le (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1] = gen_compare_reg (LE, arc_compare_op0, arc_compare_op1);
}")

(define_expand "bge"
  [(set (pc)
	(if_then_else (ge (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1] = gen_compare_reg (GE, arc_compare_op0, arc_compare_op1);
}")

(define_expand "blt"
  [(set (pc)
	(if_then_else (lt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1] = gen_compare_reg (LT, arc_compare_op0, arc_compare_op1);
}")

(define_expand "bgtu"
  [(set (pc)
	(if_then_else (gtu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1] = gen_compare_reg (GTU, arc_compare_op0, arc_compare_op1);
}")

(define_expand "bleu"
  [(set (pc)
	(if_then_else (leu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1] = gen_compare_reg (LEU, arc_compare_op0, arc_compare_op1);
}")

(define_expand "bgeu"
  [(set (pc)
	(if_then_else (geu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1] = gen_compare_reg (GEU, arc_compare_op0, arc_compare_op1);
}")

(define_expand "bltu"
  [(set (pc)
	(if_then_else (ltu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1] = gen_compare_reg (LTU, arc_compare_op0, arc_compare_op1);
}")

;; Now match both normal and inverted jump.

(define_insn "*branch_insn"
  [(set (pc)
	(if_then_else (match_operator 1 "proper_comparison_operator"
				      [(reg 61) (const_int 0)])
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "*
{
  if (arc_ccfsm_branch_deleted_p ())
    {
      arc_ccfsm_record_branch_deleted ();
      return \"; branch deleted, next insns conditionalized\";
    }
  else
    return \"%~b%d1%# %l0\";
}"
  [(set_attr "type" "branch")])

(define_insn "*rev_branch_insn"
  [(set (pc)
	(if_then_else (match_operator 1 "proper_comparison_operator"
				      [(reg 61) (const_int 0)])
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  "REVERSIBLE_CC_MODE (GET_MODE (XEXP (operands[1], 0)))"
  "*
{
  if (arc_ccfsm_branch_deleted_p ())
    {
      arc_ccfsm_record_branch_deleted ();
      return \"; branch deleted, next insns conditionalized\";
    }
  else
    return \"%~b%D1%# %l0\";
}"
  [(set_attr "type" "branch")])

;; Unconditional and other jump instructions.

(define_insn "jump"
  [(set (pc) (label_ref (match_operand 0 "" "")))]
  ""
  "b%* %l0"
  [(set_attr "type" "uncond_branch")])

(define_insn "indirect_jump"
  [(set (pc) (match_operand:SI 0 "address_operand" "p"))]
  ""
  "j%* %a0"
  [(set_attr "type" "uncond_branch")])
 
;; Implement a switch statement.
;; This wouldn't be necessary in the non-pic case if we could distinguish
;; label refs of the jump table from other label refs.  The problem is that
;; label refs are output as "%st(.LL42)" but we don't want the %st - we want
;; the real address since it's the address of the table.

(define_expand "casesi"
  [(set (match_dup 5)
	(minus:SI (match_operand:SI 0 "register_operand" "")
		  (match_operand:SI 1 "nonmemory_operand" "")))
   (set (reg:CC 61)
	(compare:CC (match_dup 5)
		    (match_operand:SI 2 "nonmemory_operand" "")))
   (set (pc)
	(if_then_else (gtu (reg:CC 61)
			   (const_int 0))
		      (label_ref (match_operand 4 "" ""))
		      (pc)))
   (parallel
    [(set (pc)
	  (mem:SI (plus:SI (mult:SI (match_dup 5)
				    (const_int 4))
			   (label_ref (match_operand 3 "" "")))))
     (clobber (match_scratch:SI 6 ""))
     (clobber (match_scratch:SI 7 ""))])]
  ""
  "
{
  operands[5] = gen_reg_rtx (SImode);
}")

(define_insn "*casesi_insn"
  [(set (pc)
	(mem:SI (plus:SI (mult:SI (match_operand:SI 0 "register_operand" "r")
				  (const_int 4))
			 (label_ref (match_operand 1 "" "")))))
   (clobber (match_scratch:SI 2 "=r"))
   (clobber (match_scratch:SI 3 "=r"))]
  ""
  "*
{
  output_asm_insn (\"mov %2,%1\", operands);
  if (TARGET_SHIFTER)
    output_asm_insn (\"asl %3,%0,2\", operands);
  else
    output_asm_insn (\"asl %3,%0\;asl %3,%3\", operands);
  output_asm_insn (\"ld %2,[%2,%3]\", operands);
  output_asm_insn (\"j.nd %a2\", operands);
  return \"\";
}"
  [(set_attr "type" "uncond_branch")
   (set_attr "length" "6")])

(define_insn "tablejump"
  [(set (pc) (match_operand:SI 0 "address_operand" "p"))
   (use (label_ref (match_operand 1 "" "")))]
  "0 /* disabled -> using casesi now */"
  "j%* %a0"
  [(set_attr "type" "uncond_branch")])

(define_expand "call"
  ;; operands[1] is stack_size_rtx
  ;; operands[2] is next_arg_register
  [(parallel [(call (match_operand:SI 0 "call_operand" "")
		    (match_operand 1 "" ""))
	     (clobber (reg:SI 31))])]
  ""
  "")

(define_insn "*call_via_reg"
  [(call (mem:SI (match_operand:SI 0 "register_operand" "r"))
	 (match_operand 1 "" ""))
   (clobber (reg:SI 31))]
  ""
  "lr blink,[status]\;j.d %0\;add blink,blink,2"
  [(set_attr "type" "call_no_delay_slot")
   (set_attr "length" "3")])

(define_insn "*call_via_label"
  [(call (mem:SI (match_operand:SI 0 "call_address_operand" ""))
	 (match_operand 1 "" ""))
   (clobber (reg:SI 31))]
  ""
  ; The %~ is necessary in case this insn gets conditionalized and the previous
  ; insn is the cc setter.
  "%~bl%!%* %0"
  [(set_attr "type" "call")
   (set_attr "cond" "canuse")])

(define_expand "call_value"
  ;; operand 2 is stack_size_rtx
  ;; operand 3 is next_arg_register
  [(parallel [(set (match_operand 0 "register_operand" "=r")
		   (call (match_operand:SI 1 "call_operand" "")
			 (match_operand 2 "" "")))
	     (clobber (reg:SI 31))])]
  ""
  "")

(define_insn "*call_value_via_reg"
  [(set (match_operand 0 "register_operand" "=r")
	(call (mem:SI (match_operand:SI 1 "register_operand" "r"))
	      (match_operand 2 "" "")))
   (clobber (reg:SI 31))]
  ""
  "lr blink,[status]\;j.d %1\;add blink,blink,2"
  [(set_attr "type" "call_no_delay_slot")
   (set_attr "length" "3")])

(define_insn "*call_value_via_label"
  [(set (match_operand 0 "register_operand" "=r")
	(call (mem:SI (match_operand:SI 1 "call_address_operand" ""))
	      (match_operand 2 "" "")))
   (clobber (reg:SI 31))]
  ""
  ; The %~ is necessary in case this insn gets conditionalized and the previous
  ; insn is the cc setter.
  "%~bl%!%* %1"
  [(set_attr "type" "call")
   (set_attr "cond" "canuse")])

(define_insn "nop"
  [(const_int 0)]
  ""
  "nop"
  [(set_attr "type" "misc")])

;; Special pattern to flush the icache.
;; ??? Not sure what to do here.  Some ARC's are known to support this.

(define_insn "flush_icache"
  [(unspec_volatile [(match_operand 0 "memory_operand" "m")] 0)]
  ""
  "* return \"\";"
  [(set_attr "type" "misc")])

;; Split up troublesome insns for better scheduling.

;; Peepholes go at the end.

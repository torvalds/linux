;; Machine description for MorphoRISC1
;; Copyright (C) 2005 Free Software Foundation, Inc.
;; Contributed by Red Hat, Inc.

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
;; along with GCC; see the file COPYING.  If not, write to the Free
;; Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
;; 02110-1301, USA.

;; UNSPECs
(define_constants
  [
    (UNSPEC_BLOCKAGE 0)
    (UNSPEC_EI 1)
    (UNSPEC_DI 2)
    (UNSPEC_LOOP 3)
  ])

;; Attributes
(define_attr "type" "branch,call,load,store,io,arith,complex,unknown"
	 (const_string "unknown") )

;; If the attribute takes numeric values, no `enum' type will be defined and
;; the function to obtain the attribute's value will return `int'.

(define_attr "length" "" (const_int 4))


;; DFA scheduler.
(define_automaton "other")
(define_cpu_unit "decode_unit" "other")
(define_cpu_unit "memory_unit" "other")
(define_cpu_unit "branch_unit" "other")

(define_insn_reservation "mem_access" 2
  (ior (eq_attr "type" "load") (eq_attr "type" "store"))
  "decode_unit+memory_unit*2")

(define_insn_reservation "io_access" 2
  (eq_attr "type" "io")
  "decode_unit+memory_unit*2")

(define_insn_reservation "branch_access" 2
  (ior (eq_attr "type" "branch")
       (eq_attr "type" "call"))
  "decode_unit+branch_unit*2")

(define_insn_reservation "arith_access" 1
  (eq_attr "type" "arith")
  "decode_unit")

(define_bypass 2 "arith_access" "branch_access")
(define_bypass 3 "mem_access" "branch_access")
(define_bypass 3 "io_access" "branch_access")


;; Delay Slots

;; The mt does not allow branches in the delay slot.
;; The mt does not allow back to back memory or io instruction.
;; The compiler does not know what the type of instruction is at
;; the destination of the branch.  Thus, only type that will be acceptable
;; (safe) is the arith type.

(define_delay (ior (eq_attr "type" "branch")
		   (eq_attr "type" "call"))
		 [(eq_attr "type" "arith") (nil) (nil)])


(define_insn "decrement_and_branch_until_zero"
   [(set (pc)
	 (if_then_else
	  (ne (match_operand:SI 0 "nonimmediate_operand" "+r,*m")
	      (const_int 0))
	  (label_ref (match_operand 1 "" ""))
	  (pc)))
    (set (match_dup 0)
	 (plus:SI (match_dup 0)
		  (const_int -1)))
    (clobber (match_scratch:SI 2 "=X,&r"))
    (clobber (match_scratch:SI 3 "=X,&r"))]
  "TARGET_MS1_16_003 || TARGET_MS2"
  "@
   dbnz\t%0, %l1%#
   #"
  [(set_attr "length" "4,16")
   (set_attr "type" "branch,unknown")]
)

;; Split the above to handle the case where operand 0 is in memory
;; (a register that couldn't get a hard register).
(define_split
  [(set (pc)
	(if_then_else
	  (ne (match_operand:SI 0 "memory_operand" "")
	      (const_int 0))
	  (label_ref (match_operand 1 "" ""))
	  (pc)))
    (set (match_dup 0)
	 (plus:SI (match_dup 0)
		  (const_int -1)))
    (clobber (match_scratch:SI 2 ""))
    (clobber (match_scratch:SI 3 ""))]
  "TARGET_MS1_16_003 || TARGET_MS2"
  [(set (match_dup 2) (match_dup 0))
   (set (match_dup 3) (plus:SI (match_dup 2) (const_int -1)))
   (set (match_dup 0) (match_dup 3))
   (set (pc)
	(if_then_else
	 (ne (match_dup 2)
	     (const_int 0))
	 (label_ref (match_dup 1))
	 (pc)))]
  "")

;; This peephole is defined in the vain hope that it might actually trigger one
;; day, although I have yet to find a test case that matches it.  The normal
;; problem is that GCC likes to move the loading of the constant value -1 out
;; of the loop, so it is not here to be matched.

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_dup 0) (const_int -1)))
   (set (match_operand:SI 1 "register_operand" "")
     (const_int -1))
   (set (pc) (if_then_else
	        (ne (match_dup 0) (match_dup 1))
		(label_ref (match_operand 2 "" ""))
		(pc)))]
  "TARGET_MS1_16_003 || TARGET_MS2"
  [(parallel [(set (pc)
	           (if_then_else
	              (ne (match_dup 0) (const_int 0))
	              (label_ref (match_dup 2))
	              (pc)))
              (set (match_dup 0)
	           (plus:SI (match_dup 0) (const_int -1)))
	      (clobber (reg:SI 0))
	      (clobber (reg:SI 0))])]
  "")


;; Loop instructions.  ms2 has a low overhead looping instructions.
;; these take a constant or register loop count and a loop length
;; offset.  Unfortunately the loop can only be up to 256 instructions,
;; We deal with longer loops by moving the loop end upwards.  To do
;; otherwise would force us to to be very pessimistic right up until
;; the end.

;; This instruction is a placeholder to make the control flow explicit.
(define_insn "loop_end"
  [(set (pc) (if_then_else
			  (ne (match_operand:SI 0 "register_operand" "")
			      (const_int 1))
			  (label_ref (match_operand 1 "" ""))
			  (pc)))
   (set (match_dup 0) (plus:SI (match_dup 0) (const_int -1)))
   (unspec [(const_int 0)] UNSPEC_LOOP)]
  "TARGET_MS2"
  ";loop end %0,%l1"
  [(set_attr "length" "0")])

;; This is the real looping instruction.  It is placed just before the
;; loop body.  We make it a branch insn, so it stays at the end of the
;; block it is in.
(define_insn "loop_init"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(match_operand:SI 1 "uns_arith_operand" "r,K"))
   (unspec [(label_ref (match_operand 2 "" ""))] UNSPEC_LOOP)]
  "TARGET_MS2"
  "@
   loop  %1,%l2 ;%0%#
   loopi %1,%l2 ;%0%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

; operand 0 is the loop count pseudo register
; operand 1 is the number of loop iterations or 0 if it is unknown
; operand 2 is the maximum number of loop iterations
; operand 3 is the number of levels of enclosed loops
; operand 4 is the label to jump to at the top of the loop
(define_expand "doloop_end"
  [(parallel [(set (pc) (if_then_else
			  (ne (match_operand:SI 0 "nonimmediate_operand" "")
			      (const_int 0))
			  (label_ref (match_operand 4 "" ""))
			  (pc)))
	      (set (match_dup 0)
		   (plus:SI (match_dup 0)
			    (const_int -1)))
	      (clobber (match_scratch:SI 5 ""))
	      (clobber (match_scratch:SI 6 ""))])]
  "TARGET_MS1_16_003 || TARGET_MS2"
  {mt_add_loop ();})

;; Moves

(define_expand "loadqi"
  [
   ;; compute shift
   (set (match_operand:SI 2 "register_operand" "")
	(and:SI (match_dup 1) (const_int 3)))
   (set (match_dup 2)	(xor:SI (match_dup 2) (const_int 3)))
   (set (match_dup 2 )	(ashift:SI (match_dup 2) (const_int 3)))

   ;; get word that contains byte
   (set (match_operand:SI 0 "register_operand" "")
	(mem:SI (and:SI (match_operand:SI 1 "register_operand" "")
			(const_int -3))))

   ;; align byte
   (set (match_dup 0)   (ashiftrt:SI (match_dup 0) (match_dup 2)))
  ]
  ""
  "")


;; storeqi
;; operand 0 byte value to store
;; operand 1 address
;; operand 2 temp, word containing byte
;; operand 3 temp, shift count
;; operand 4 temp, mask, aligned and masked byte
;; operand 5 (unused)
(define_expand "storeqi"
  [
   ;; compute shift
   (set (match_operand:SI 3 "register_operand" "")
	(and:SI (match_operand:SI 1 "register_operand" "") (const_int 3)))
   (set (match_dup 3)	(xor:SI (match_dup 3) (const_int 3)))
   (set (match_dup 3)	(ashift:SI (match_dup 3) (const_int 3)))

   ;; get word that contains byte
   (set (match_operand:SI 2 "register_operand" "")
	(mem:SI (and:SI (match_dup 1) (const_int -3))))

   ;; generate mask
   (set (match_operand:SI 4 "register_operand" "") (const_int 255))
   (set (match_dup 4) (ashift:SI (match_dup 4) (match_dup 3)))
   (set (match_dup 4) (not:SI (match_dup 4)))

   ;; clear appropriate bits
   (set (match_dup 2) (and:SI (match_dup 2) (match_dup 4)))

   ;; align byte
   (set (match_dup 4)
	(and:SI (match_operand:SI 0 "register_operand" "") (const_int 255)))
   (set (match_dup 4) (ashift:SI (match_dup 4) (match_dup 3)))

   ;; combine
   (set (match_dup 2) (ior:SI (match_dup 4) (match_dup 2)))
   ;; store updated word
   (set (mem:SI (and:SI (match_dup 1) (const_int -3))) (match_dup 2))
  ]
  ""
  "")


(define_expand "movqi"
  [(set (match_operand:QI 0 "general_operand" "")
	(match_operand:QI 1 "general_operand" ""))]
  ""
  "
{
  if (!reload_in_progress
      && !reload_completed
      && GET_CODE (operands[0]) == MEM
      && GET_CODE (operands[1]) == MEM)
    operands[1] = copy_to_mode_reg (QImode, operands[1]);
  
  if ( (! TARGET_BYTE_ACCESS) && GET_CODE (operands[0]) == MEM)
    {
	rtx scratch1 = gen_reg_rtx (SImode);
	rtx scratch2 = gen_reg_rtx (SImode);
	rtx scratch3 = gen_reg_rtx (SImode);
	rtx data     = operands[1];
	rtx address  = XEXP (operands[0], 0);
	rtx seq;

	if ( GET_CODE (data) != REG )
	    data = copy_to_mode_reg (QImode, data);

	if ( GET_CODE (address) != REG )
	  address = copy_to_mode_reg (SImode, address);

	start_sequence ();
	emit_insn (gen_storeqi (gen_lowpart (SImode, data), address,
				scratch1, scratch2, scratch3));
	mt_set_memflags (operands[0]);
	seq = get_insns ();
	end_sequence ();
	emit_insn (seq);
	DONE;
    }

  if ( (! TARGET_BYTE_ACCESS) && GET_CODE (operands[1]) == MEM)
    {
	rtx scratch1 = gen_reg_rtx (SImode);
	rtx data = operands[0];
	rtx address = XEXP (operands[1], 0);
	rtx seq;

	if ( GET_CODE (address) != REG )
	  address = copy_to_mode_reg (SImode, address);

	start_sequence ();
	emit_insn (gen_loadqi (gen_lowpart (SImode, data), address, scratch1));
	mt_set_memflags (operands[1]);
	seq = get_insns ();
	end_sequence ();
	emit_insn (seq);
	DONE;
    }

   /* If the load is a pseudo register in a stack slot, some simplification
      can be made because the loads are aligned */
  if ( (! TARGET_BYTE_ACCESS) 
        && (reload_in_progress && GET_CODE (operands[1]) == SUBREG
	  && GET_CODE (SUBREG_REG (operands[1])) == REG
	  && REGNO (SUBREG_REG (operands[1])) >= FIRST_PSEUDO_REGISTER))
    {
	rtx data = operands[0];
	rtx address = XEXP (operands[1], 0);
	rtx seq;

	start_sequence ();
	emit_insn (gen_movsi (gen_lowpart (SImode, data), address));
	mt_set_memflags (operands[1]);
	seq = get_insns ();
	end_sequence ();
	emit_insn (seq);
	DONE;
    }
}")

(define_insn "*movqi_internal"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=r,r,m,r")
	(match_operand:QI 1 "general_operand" "r,m,r,I"))]
  "TARGET_BYTE_ACCESS
    && (!memory_operand (operands[0], QImode)
        || !memory_operand (operands[1], QImode))"
  "@
   or  %0, %1, %1
   ldb %0, %1
   stb %1, %0
   addi %0, r0, %1"
  [(set_attr "length" "4,4,4,4")
   (set_attr "type" "arith,load,store,arith")])

(define_insn "*movqi_internal_nobyte"
  [(set (match_operand:QI 0 "register_operand" "=r,r")
	(match_operand:QI 1 "arith_operand" "r,I"))]
  "!TARGET_BYTE_ACCESS
    && (!memory_operand (operands[0], QImode)
        || !memory_operand (operands[1], QImode))"
  "@
   or   %0, %1, %1
   addi %0, r0, %1"
  [(set_attr "length" "4,4")
   (set_attr "type" "arith,arith")])


;; The MorphoRISC does not have 16-bit loads and stores.
;; These operations must be synthesized.  Note that the code
;; for loadhi and storehi assumes that the least significant bits
;; is ignored.

;; loadhi
;; operand 0 location of result
;; operand 1 memory address
;; operand 2 temp
(define_expand "loadhi"
  [
   ;; compute shift
   (set (match_operand:SI 2 "register_operand" "")
	(and:SI (match_dup 1) (const_int 2)))
   (set (match_dup 2)	(xor:SI (match_dup 2) (const_int 2)))
   (set (match_dup 2 )	(ashift:SI (match_dup 2) (const_int 3)))

   ;; get word that contains the 16-bits
   (set (match_operand:SI 0 "register_operand" "")
	(mem:SI (and:SI (match_operand:SI 1 "register_operand" "")
			(const_int -3))))

   ;; align 16-bit value
   (set (match_dup 0)	(ashiftrt:SI (match_dup 0) (match_dup 2)))
  ]
  ""
  "")

;; storehi
;; operand 0 byte value to store
;; operand 1 address
;; operand 2 temp, word containing byte
;; operand 3 temp, shift count
;; operand 4 temp, mask, aligned and masked byte
;; operand 5 (unused)
(define_expand "storehi"
  [
   ;; compute shift
   (set (match_operand:SI 3 "register_operand" "")
	(and:SI (match_operand:SI 1 "register_operand" "") (const_int 2)))
   (set (match_dup 3)	(xor:SI (match_dup 3) (const_int 2)))
   (set (match_dup 3)	(ashift:SI (match_dup 3) (const_int 3)))

   ;; get word that contains the 16-bits
   (set (match_operand:SI 2 "register_operand" "")
	(mem:SI (and:SI (match_dup 1) (const_int -3))))

   ;; generate mask
   (set (match_operand:SI 4 "register_operand" "") (const_int 65535))
   (set (match_dup 4) (ashift:SI (match_dup 4) (match_dup 3)))
   (set (match_dup 4) (not:SI (match_dup 4)))

   ;; clear appropriate bits
   (set (match_dup 2) (and:SI (match_dup 2) (match_dup 4)))

   ;; align 16-bit value
   (set (match_dup 4)
	(and:SI (match_operand:SI 0 "register_operand" "") (const_int 65535)))
   (set (match_dup 4) (ashift:SI (match_dup 4) (match_dup 3)))

   ;; combine
   (set (match_dup 2) (ior:SI (match_dup 4) (match_dup 2)))
   ;; store updated word
   (set (mem:SI (and:SI (match_dup 1) (const_int -3))) (match_dup 2))
  ]
  ""
  "")


(define_expand "movhi"
  [(set (match_operand:HI 0 "general_operand" "")
	(match_operand:HI 1 "general_operand" ""))]
  ""
  "
{
  if (!reload_in_progress
      && !reload_completed
      && GET_CODE (operands[0]) == MEM
      && GET_CODE (operands[1]) == MEM)
    operands[1] = copy_to_mode_reg (HImode, operands[1]);

  if ( GET_CODE (operands[0]) == MEM)
    {
	rtx scratch1 = gen_reg_rtx (SImode);
	rtx scratch2 = gen_reg_rtx (SImode);
	rtx scratch3 = gen_reg_rtx (SImode);
	rtx data     = operands[1];
	rtx address  = XEXP (operands[0], 0);
	rtx seq;

	if (GET_CODE (data) != REG)
	  data = copy_to_mode_reg (HImode, data);

	if (GET_CODE (address) != REG)
	  address = copy_to_mode_reg (SImode, address);

	start_sequence ();
	emit_insn (gen_storehi (gen_lowpart (SImode, data), address,
			        scratch1, scratch2, scratch3));
	mt_set_memflags (operands[0]);
	seq = get_insns ();
	end_sequence ();
	emit_insn (seq);
	DONE;
    }

  if ( GET_CODE (operands[1]) == MEM)
    {
	rtx scratch1 = gen_reg_rtx (SImode);
	rtx data     = operands[0];
	rtx address  = XEXP (operands[1], 0);
	rtx seq;

	if (GET_CODE (address) != REG)
	    address = copy_to_mode_reg (SImode, address);

	start_sequence ();
	emit_insn (gen_loadhi (gen_lowpart (SImode, data), address,
			       scratch1));
	mt_set_memflags (operands[1]);
	seq = get_insns ();
	end_sequence ();
	emit_insn (seq);
	DONE;
    }

   /* If the load is a pseudo register in a stack slot, some simplification
      can be made because the loads are aligned */
  if ( (reload_in_progress && GET_CODE (operands[1]) == SUBREG
	  && GET_CODE (SUBREG_REG (operands[1])) == REG
	  && REGNO (SUBREG_REG (operands[1])) >= FIRST_PSEUDO_REGISTER))
    {
	rtx data = operands[0];
	rtx address = XEXP (operands[1], 0);
	rtx seq;

	start_sequence ();
	emit_insn (gen_movsi (gen_lowpart (SImode, data), address));
	mt_set_memflags (operands[1]);
	seq = get_insns ();
	end_sequence ();
	emit_insn (seq);
	DONE;
    }
}")

(define_insn "*movhi_internal"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(match_operand:HI 1 "arith_operand" "r,I"))]
  "!memory_operand (operands[0], HImode) || !memory_operand (operands[1], HImode)"
  "@
  or    %0, %1, %1
  addi  %0, r0, %1"
  [(set_attr "length" "4,4")
   (set_attr "type" "arith,arith")])

(define_expand "movsi"
  [(set (match_operand:SI 0 "nonimmediate_operand" "")
	(match_operand:SI 1 "general_operand" ""))]
  ""
  "
{
  if (!reload_in_progress  && !reload_completed
      && !register_operand (operands[0], SImode)
      && !register_operand (operands[1], SImode))
    operands[1] = copy_to_mode_reg (SImode, operands[1]);

  /* Take care of constants that don't fit in single instruction */
  if ( (reload_in_progress || reload_completed)
   && !single_const_operand (operands[1], SImode))
    {
      emit_insn (gen_movsi_high (operands[0], operands[1]));
      emit_insn (gen_movsi_lo_sum (operands[0], operands[0], operands[1]));
      DONE;
    }

}")

(define_insn "movsi_high"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (high:SI (match_operand:SI 1 "general_operand" "i")))]
  ""
  "*
{
  return \"ldui\\t%0, %H1\";
}"
  [(set_attr "length" "4")
   (set_attr "type" "arith")])


(define_insn "movsi_lo_sum"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (lo_sum:SI (match_operand:SI 1 "register_operand" "r")
                   (match_operand:SI 2 "general_operand" "i")))]
  ""
  "*
{
  return \"addui\\t%0, %1, %L2\";
}"
  [(set_attr "length" "4")
   (set_attr "type" "arith")])

/* Take care of constants that don't fit in single instruction */
(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(match_operand:SI 1 "general_operand" ""))]
  "(reload_in_progress || reload_completed)
   && !single_const_operand (operands[1], SImode)"

  [(set (match_dup 0 )
        (high:SI (match_dup 1)))
   (set (match_dup 0 )
        (lo_sum:SI (match_dup 0)
                   (match_dup 1)))]
)


;; The last pattern in movsi (with two instructions)
;; is really handled by the emit_insn's in movsi
;; and the define_split above.  This provides additional
;; instructions to fill delay slots.

;; Note - it is best to only have one movsi pattern and to handle
;; all the various contingencies by the use of alternatives.  This
;; allows reload the greatest amount of flexibility (since reload will
;; only choose amoungst alternatives for a selected insn, it will not
;; replace the insn with another one).
(define_insn "*movsi_internal"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,r,m,r,r,r,r,r")
	(match_operand:SI 1 "general_operand"       "r,m,r,I,P,L,N,i"))]
  "(!memory_operand (operands[0], SImode) || !memory_operand (operands[1], SImode))
   && !((reload_in_progress || reload_completed)
	 && !single_const_operand (operands[1], SImode))"
  "@
  or     %0, %1, %1
  ldw    %0, %1
  stw    %1, %0
  addi   %0, r0, %1
  addui  %0, r0, %1
  ldui   %0, %H1
  nori   %0, r0, %N1
  ldui   %0, %H1\;addui %0, %0, %L1"
  [(set_attr "length" "4,4,4,4,4,4,4,8")
   (set_attr "type" "arith,load,store,arith,arith,arith,arith,complex")]
)

;; Floating Point Moves
;;
;; Note - Patterns for SF mode moves are compulsory, but
;; patterns for DF are optional, as GCC can synthesize them.

(define_expand "movsf"
  [(set (match_operand:SF 0 "general_operand" "")
	(match_operand:SF 1 "general_operand" ""))]
  ""
  "
{
  if (!reload_in_progress
      && !reload_completed
      && GET_CODE (operands[0]) == MEM
      && (GET_CODE (operands[1]) == MEM
         || GET_CODE (operands[1]) == CONST_DOUBLE))
    operands[1] = copy_to_mode_reg (SFmode, operands[1]);

  /* Take care of reg <- SF constant */
  if ( const_double_operand (operands[1], GET_MODE (operands[1]) ) )
    {
      emit_insn (gen_movsf_high (operands[0], operands[1]));
      emit_insn (gen_movsf_lo_sum (operands[0], operands[0], operands[1]));
      DONE;
    }
}")

(define_insn "movsf_lo_sum"
  [(set (match_operand:SF 0 "register_operand" "=r")
        (lo_sum:SF (match_operand:SF 1 "register_operand" "r")
                   (match_operand:SF 2 "const_double_operand" "")))]
  ""
  "*
{
  REAL_VALUE_TYPE r;
  long i;

  REAL_VALUE_FROM_CONST_DOUBLE (r, operands[2]);
  REAL_VALUE_TO_TARGET_SINGLE (r, i);
  operands[2] = GEN_INT (i);
  return \"addui\\t%0, %1, %L2\";
}"
  [(set_attr "length" "4")
   (set_attr "type" "arith")])

(define_insn "movsf_high"
  [(set (match_operand:SF 0 "register_operand" "=r")
        (high:SF (match_operand:SF 1 "const_double_operand" "")))]
  ""
  "*
{
  REAL_VALUE_TYPE r;
  long i;

  REAL_VALUE_FROM_CONST_DOUBLE (r, operands[1]);
  REAL_VALUE_TO_TARGET_SINGLE (r, i);
  operands[1] = GEN_INT (i);
  return \"ldui\\t%0, %H1\";
}"
  [(set_attr "length" "4")
   (set_attr "type" "arith")])


(define_insn "*movsf_internal"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=r,r,m")
	(match_operand:SF 1 "nonimmediate_operand" "r,m,r"))]
  "!memory_operand (operands[0], SFmode) || !memory_operand (operands[1], SFmode)"
  "@
  or     %0, %1, %1
  ldw    %0, %1
  stw    %1, %0"
  [(set_attr "length" "4,4,4")
   (set_attr "type" "arith,load,store")]
)

(define_expand "movdf"
  [(set (match_operand:DF 0 "general_operand" "")
	(match_operand:DF 1 "general_operand" ""))]
  ""
  "
{
  /* One of the ops has to be in a register or 0 */
  if (!register_operand (operand0, DFmode)
      && !reg_or_0_operand (operand1, DFmode))
    operands[1] = copy_to_mode_reg (DFmode, operand1);
}")

(define_insn_and_split "*movdf_internal"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=r,o")
	(match_operand:DF 1 "general_operand" 	   "rim,r"))]
  "! (memory_operand (operands[0], DFmode)
         && memory_operand (operands[1], DFmode))"
  "#"

  "(reload_completed || reload_in_progress)"

  [(set (match_dup 2) (match_dup 3))
   (set (match_dup 4) (match_dup 5))
  ]

  "{
    /* figure out what precisely to put into operands 2, 3, 4, and 5 */
    mt_split_words (SImode, DFmode, operands);
  }"
)


;; Reloads

;; Like `movM', but used when a scratch register is required to move between
;; operand 0 and operand 1.  Operand 2 describes the scratch register.  See the
;; discussion of the `SECONDARY_RELOAD_CLASS' macro.

(define_expand "reload_inqi"
  [(set (match_operand:QI 0 "register_operand" "=r")
        (match_operand:QI 1 "memory_operand" "m"))
   (clobber (match_operand:DI 2 "register_operand" "=&r"))]
  "! TARGET_BYTE_ACCESS"
  "
{
  rtx scratch1 = gen_rtx_REG (SImode, REGNO (operands[2]));
  rtx scratch2 = gen_rtx_REG (SImode, REGNO (operands[2])+1);
  rtx data = operands[0];
  rtx address = XEXP (operands[1], 0);
  rtx swap, seq;

  /* It is possible that the registers we got for scratch1
     might coincide with that of operands[0].  gen_loadqi
     requires operand0 and operand2 to be different registers.
     The following statement ensure that is always the case. */
  if (REGNO(operands[0]) == REGNO(scratch1))
    {
	swap = scratch1;
	scratch1 = scratch2;
	scratch2 = swap;
    }

  /* need to make sure address is already in register */
  if ( GET_CODE (address) != REG )
    address = force_operand (address, scratch2);

  start_sequence ();
  emit_insn (gen_loadqi (gen_lowpart (SImode, data), address, scratch1));
  mt_set_memflags (operands[1]);
  seq = get_insns ();
  end_sequence ();
  emit_insn (seq);
  DONE;
}")

(define_expand "reload_outqi"
  [(set (match_operand:QI 0 "memory_operand" "=m")
        (match_operand:QI 1 "register_operand" "r"))
   (clobber (match_operand:TI 2 "register_operand" "=&r"))]
  "! TARGET_BYTE_ACCESS"
  "
{
  rtx scratch1 = gen_rtx_REG (SImode, REGNO (operands[2]));
  rtx scratch2 = gen_rtx_REG (SImode, REGNO (operands[2])+1);
  rtx scratch3 = gen_rtx_REG (SImode, REGNO (operands[2])+2);
  rtx scratch4 = gen_rtx_REG (SImode, REGNO (operands[2])+3);
  rtx data     = operands[1];
  rtx address  = XEXP (operands[0], 0);
  rtx seq;

  /* need to make sure address is already in register */
  if ( GET_CODE (address) != REG )
    address = force_operand (address, scratch4);

  start_sequence ();
  emit_insn (gen_storeqi (gen_lowpart (SImode, data), address, 
			  scratch1, scratch2, scratch3));
  mt_set_memflags (operands[0]);
  seq = get_insns ();
  end_sequence ();
  emit_insn (seq);
  DONE;
}")

(define_expand "reload_inhi"
  [(set (match_operand:HI 0 "register_operand" "=r")
        (match_operand:HI 1 "memory_operand" "m"))
   (clobber (match_operand:DI 2 "register_operand" "=&r"))]
  ""
  "
{
  rtx scratch1 = gen_rtx_REG (SImode, REGNO (operands[2]));
  rtx scratch2 = gen_rtx_REG (SImode, REGNO (operands[2])+1);
  rtx data     = operands[0];
  rtx address  = XEXP (operands[1], 0);
  rtx swap, seq;

  /* It is possible that the registers we got for scratch1
     might coincide with that of operands[0].  gen_loadqi
     requires operand0 and operand2 to be different registers.
     The following statement ensure that is always the case. */
  if (REGNO(operands[0]) == REGNO(scratch1))
    {
	swap = scratch1;
	scratch1 = scratch2;
	scratch2 = swap;
    }

  /* need to make sure address is already in register */
  if ( GET_CODE (address) != REG )
    address = force_operand (address, scratch2);

  start_sequence ();
  emit_insn (gen_loadhi (gen_lowpart (SImode, data), address,
		         scratch1));
  mt_set_memflags (operands[1]);
  seq = get_insns ();
  end_sequence ();
  emit_insn (seq);
  DONE;
}")

(define_expand "reload_outhi"
  [(set (match_operand:HI 0 "memory_operand" "=m")
        (match_operand:HI 1 "register_operand" "r"))
   (clobber (match_operand:TI 2 "register_operand" "=&r"))]
  ""
  "
{
  rtx scratch1 = gen_rtx_REG (SImode, REGNO (operands[2]));
  rtx scratch2 = gen_rtx_REG (SImode, REGNO (operands[2])+1);
  rtx scratch3 = gen_rtx_REG (SImode, REGNO (operands[2])+2);
  rtx scratch4 = gen_rtx_REG (SImode, REGNO (operands[2])+3);
  rtx data     = operands[1];
  rtx address  = XEXP (operands[0], 0);
  rtx seq;

  /* need to make sure address is already in register */
  if ( GET_CODE (address) != REG )
    address = force_operand (address, scratch4);

  start_sequence ();
  emit_insn (gen_storehi (gen_lowpart (SImode, data), address,
		          scratch1, scratch2, scratch3));
  mt_set_memflags (operands[0]);
  seq = get_insns ();
  end_sequence ();
  emit_insn (seq);
  DONE;
}")


;; 32 bit Integer arithmetic

;; Addition
(define_insn "addsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (match_operand:SI 1 "register_operand" "%r,r")
		 (match_operand:SI 2 "arith_operand" "r,I")))]
  ""
  "@
  add %0, %1, %2
  addi %0, %1, %2"
  [(set_attr "length" "4,4")
   (set_attr "type" "arith,arith")])

;; Subtraction
(define_insn "subsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(minus:SI (match_operand:SI 1 "reg_or_0_operand" "rJ,rJ")
		  (match_operand:SI 2 "arith_operand" "rJ,I")))]
  ""
  "@
  sub %0, %z1, %z2
  subi %0, %z1, %2"
  [(set_attr "length" "4,4")
   (set_attr "type" "arith,arith")])

;;  Negation 
(define_insn "negsi2"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(neg:SI (match_operand:SI 1 "arith_operand" "r,I")))]
  ""
  "@
  sub  %0, r0, %1
  subi  %0, r0, %1"
  [(set_attr "length" "4,4")
   (set_attr "type" "arith,arith")])


;; 32 bit Integer Shifts and Rotates

;; Arithmetic Shift Left
(define_insn "ashlsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(ashift:SI (match_operand:SI 1 "register_operand" "r,r")
		   (match_operand:SI 2 "arith_operand" "r,K")))]
  ""
  "@
  lsl %0, %1, %2
  lsli %0, %1, %2"
  [(set_attr "length" "4,4")
   (set_attr "type" "arith,arith")])

;; Arithmetic Shift Right
(define_insn "ashrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(ashiftrt:SI (match_operand:SI 1 "register_operand" "r,r")
		     (match_operand:SI 2 "uns_arith_operand" "r,K")))]
  ""
  "@
  asr %0, %1, %2
  asri %0, %1, %2"
  [(set_attr "length" "4,4")
   (set_attr "type" "arith,arith")])

;; Logical Shift Right
(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(lshiftrt:SI (match_operand:SI 1 "register_operand" "r,r")
		     (match_operand:SI 2 "uns_arith_operand" "r,K")))]
  ""
  "@
  lsr %0, %1, %2
  lsri %0, %1, %2"
  [(set_attr "length" "4,4")
   (set_attr "type" "arith,arith")])


;; 32 Bit Integer Logical operations

;; Logical AND, 32 bit integers
(define_insn "andsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(and:SI (match_operand:SI 1 "register_operand" "%r,r")
		(match_operand:SI 2 "uns_arith_operand" "r,K")))]
  ""
  "@
  and %0, %1, %2
  andi %0, %1, %2"
  [(set_attr "length" "4,4")
   (set_attr "type" "arith,arith")])

;; Inclusive OR, 32 bit integers
(define_insn "iorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(ior:SI (match_operand:SI 1 "register_operand" "%r,r")
		(match_operand:SI 2 "uns_arith_operand" "r,K")))]
  ""
  "@
  or %0, %1, %2
  ori %0, %1, %2"
  [(set_attr "length" "4,4")
   (set_attr "type" "arith,arith")])

;; Exclusive OR, 32 bit integers
(define_insn "xorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(xor:SI (match_operand:SI 1 "register_operand" "%r,r")
		(match_operand:SI 2 "uns_arith_operand" "r,K")))]
  ""
  "@
  xor %0, %1, %2
  xori %0, %1, %2"
  [(set_attr "length" "4,4")
   (set_attr "type" "arith,arith")])


;; One's complement, 32 bit integers
(define_insn "one_cmplsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(not:SI (match_operand:SI 1 "register_operand" "r")))]
  ""
  "nor %0, %1, %1"
  [(set_attr "length" "4")
   (set_attr "type" "arith")])


;; Multiply

(define_insn "mulhisi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
     (mult:SI (sign_extend:SI (match_operand:HI 1 "register_operand" "%r,r"))
     	      (sign_extend:SI (match_operand:HI 2 "arith_operand" "r,I"))))]
  "TARGET_MS1_16_003 || TARGET_MS2"
  "@
  mul %0, %1, %2
  muli %0, %1, %2"
  [(set_attr "length" "4,4")
   (set_attr "type" "arith,arith")])


;; Comparisons

;; Note, we store the operands in the comparison insns, and use them later
;; when generating the branch or scc operation.

;; First the routines called by the machine independent part of the compiler
(define_expand "cmpsi"
  [(set (cc0)
        (compare (match_operand:SI 0 "register_operand" "")
  		 (match_operand:SI 1 "arith_operand" "")))]
  ""
  "
{
  mt_compare_op0 = operands[0];
  mt_compare_op1 = operands[1];
  DONE;
}")


;; Branches

(define_expand "beq"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (EQ, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "bne"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (NE, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "bge"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (GE, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "bgt"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (GT, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "ble"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (LE, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "blt"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (LT, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "bgeu"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (GEU, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "bgtu"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (GTU, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "bleu"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (LEU, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "bltu"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (LTU, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "bunge"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (GEU, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "bungt"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (GTU, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "bunle"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (LEU, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_expand "bunlt"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  mt_emit_cbranch (LTU, operands[0], mt_compare_op0, mt_compare_op1);
  DONE;
}")

(define_insn "*beq_true"
  [(set (pc)
	(if_then_else (eq (match_operand:SI 0 "reg_or_0_operand" "rJ")
			  (match_operand:SI 1 "reg_or_0_operand" "rJ"))
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
  "breq %z0, %z1, %l2%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

(define_insn "*beq_false"
  [(set (pc)
	(if_then_else (eq (match_operand:SI 0 "reg_or_0_operand" "rJ")
			  (match_operand:SI 1 "reg_or_0_operand" "rJ"))
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
  "brne %z0, %z1, %l2%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])


(define_insn "*bne_true"
  [(set (pc)
	(if_then_else (ne (match_operand:SI 0 "reg_or_0_operand" "rJ")
			  (match_operand:SI 1 "reg_or_0_operand" "rJ"))
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
  "brne %z0, %z1, %l2%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

(define_insn "*bne_false"
  [(set (pc)
	(if_then_else (ne (match_operand:SI 0 "reg_or_0_operand" "rJ")
			  (match_operand:SI 1 "reg_or_0_operand" "rJ"))
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
  "breq %z0, %z1, %l2%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

(define_insn "*blt_true"
  [(set (pc)
	(if_then_else (lt (match_operand:SI 0 "reg_or_0_operand" "rJ")
			  (match_operand:SI 1 "reg_or_0_operand" "rJ"))
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
  "brlt %z0, %z1, %l2%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

(define_insn "*blt_false"
  [(set (pc)
	(if_then_else (lt (match_operand:SI 0 "reg_or_0_operand" "rJ")
			  (match_operand:SI 1 "reg_or_0_operand" "rJ"))
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
  "brle %z1, %z0,%l2%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

(define_insn "*ble_true"
  [(set (pc)
	(if_then_else (le (match_operand:SI 0 "reg_or_0_operand" "rJ")
			  (match_operand:SI 1 "reg_or_0_operand" "rJ"))
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
  "brle %z0, %z1, %l2%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

(define_insn "*ble_false"
  [(set (pc)
	(if_then_else (le (match_operand:SI 0 "reg_or_0_operand" "rJ")
			  (match_operand:SI 1 "reg_or_0_operand" "rJ"))
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
  "brlt %z1, %z0,%l2%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

(define_insn "*bgt_true"
  [(set (pc)
	(if_then_else (gt (match_operand:SI 0 "reg_or_0_operand" "rJ")
			  (match_operand:SI 1 "reg_or_0_operand" "rJ"))
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
  "brlt %z1, %z0, %l2%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

(define_insn "*bgt_false"
  [(set (pc)
	(if_then_else (gt (match_operand:SI 0 "reg_or_0_operand" "rJ")
			  (match_operand:SI 1 "reg_or_0_operand" "rJ"))
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
  "brle %z0, %z1, %l2%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

(define_insn "*bge_true"
  [(set (pc)
	(if_then_else (ge (match_operand:SI 0 "reg_or_0_operand" "rJ")
			  (match_operand:SI 1 "reg_or_0_operand" "rJ"))
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
  "brle %z1, %z0,%l2%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

(define_insn "*bge_false"
  [(set (pc)
	(if_then_else (ge (match_operand:SI 0 "reg_or_0_operand" "rJ")
			  (match_operand:SI 1 "reg_or_0_operand" "rJ"))
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
  "brlt %z0, %z1, %l2%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

;; No unsigned operators on Morpho mt.  All the unsigned operations are
;; converted to the signed operations above.


;; Set flag operations

;; "seq", "sne", "slt", "sle", "sgt", "sge", "sltu", "sleu",
;; "sgtu", and "sgeu" don't exist as regular instruction on the
;; mt, so these are not defined

;; Call and branch instructions

(define_expand "call"
  [(parallel [(call (mem:SI (match_operand:SI 0 "register_operand" ""))
			    (match_operand 1 "" ""))
	      (clobber (reg:SI 14))])]
  ""
  "
{
    operands[0] = force_reg (SImode, XEXP (operands[0], 0));
}")

(define_insn "call_internal"
  [(call (mem:SI (match_operand 0 "register_operand" "r"))
	 (match_operand 1 "" ""))
   ;; possibly add a clobber of the reg that gets the return address
   (clobber (reg:SI 14))]
  ""
  "jal r14, %0%#"
  [(set_attr "length" "4")
   (set_attr "type" "call")])

(define_expand "call_value"
  [(parallel [(set (match_operand 0 "register_operand" "")
		   (call (mem:SI (match_operand:SI 1 "register_operand" ""))
				 (match_operand 2 "general_operand" "")))
	      (clobber (reg:SI 14))])]
  ""
  "
{
    operands[1] = force_reg (SImode, XEXP (operands[1], 0));
}")


(define_insn "call_value_internal"
  [(set (match_operand 0 "register_operand" "=r")
	(call (mem:SI (match_operand 1 "register_operand" "r"))
	      (match_operand 2 "" "")))
	;; possibly add a clobber of the reg that gets the return address
	(clobber (reg:SI 14))]
  ""
  "jal r14, %1%#"
  [(set_attr "length" "4")
   (set_attr "type" "call")])

;; Subroutine return
(define_insn "return_internal"
  [(const_int 2)
   (return)
   (use (reg:SI 14))]
  ""
  "jal r0, r14%#"
  [(set_attr "length" "4")
   (set_attr "type" "call")])

;; Interrupt return
(define_insn "return_interrupt_internal"
  [(const_int 3)
   (return)
   (use (reg:SI 15))]
  ""
  "reti r15%#"
  [(set_attr "length" "4")
   (set_attr "type" "call")])

;; Subroutine return
(define_insn "eh_return_internal"
  [(return)
   (use (reg:SI 7))
   (use (reg:SI 8))
   (use (reg:SI 11))
   (use (reg:SI 10))]
  ""
  "jal r0, r11%#"
  [(set_attr "length" "4")
   (set_attr "type" "call")])


;; Normal unconditional jump
(define_insn "jump"
  [(set (pc) (label_ref (match_operand 0 "" "")))]
  ""
  "jmp %l0%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

;; Indirect jump through a register
(define_insn "indirect_jump"
  [(set (pc) (match_operand 0 "register_operand" "r"))]
  ""
  "jal r0,%0%#"
  [(set_attr "length" "4")
   (set_attr "type" "call")])

(define_insn "tablejump"
  [(set (pc) (match_operand:SI 0 "register_operand" "r"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
  "jal r0, %0%#"
  [(set_attr "length" "4")
   (set_attr "type" "call")])


(define_expand "prologue"
  [(const_int 1)]
  ""
  "
{
  mt_expand_prologue ();
  DONE;
}")

(define_expand "epilogue"
  [(const_int 2)]
  ""
  "
{
  mt_expand_epilogue (NORMAL_EPILOGUE);
  DONE;
}")


(define_expand "eh_return"
  [(use (match_operand:SI 0 "register_operand" "r"))]
  ""
  "
{
  mt_expand_eh_return (operands);
  DONE;
}")


(define_insn_and_split "eh_epilogue"
  [(unspec [(match_operand 0 "register_operand" "r")] 6)]
  ""
  "#"
  "reload_completed"
  [(const_int 1)]
  "mt_emit_eh_epilogue (operands); DONE;"
)

;; No operation, needed in case the user uses -g but not -O.
(define_insn "nop"
  [(const_int 0)]
  ""
  "nop"
  [(set_attr "length" "4")
   (set_attr "type" "arith")])

;; ::::::::::::::::::::
;; ::
;; :: UNSPEC_VOLATILE usage
;; ::
;; ::::::::::::::::::::
;; 
;;	0	blockage
;;	1	Enable interrupts
;;	2	Disable interrupts
;;

;; Pseudo instruction that prevents the scheduler from moving code above this
;; point.
(define_insn "blockage"
  [(unspec_volatile [(const_int 0)] UNSPEC_BLOCKAGE)]
  ""
  ""
  [(set_attr "length" "0")])

;; Trap instruction to allow usage of the __builtin_trap function
(define_insn "trap"
  [(trap_if (const_int 1) (const_int 0))
   (clobber (reg:SI 14))]
  ""
  "si	r14%#"
  [(set_attr "length" "4")
   (set_attr "type" "branch")])

(define_expand "conditional_trap"
  [(trap_if (match_operator 0 "comparison_operator"
			    [(match_dup 2)
			     (match_dup 3)])
	    (match_operand 1 "const_int_operand" ""))]
  ""
  "
{
  operands[2] = mt_compare_op0;
  operands[3] = mt_compare_op1;
}")

;; Templates to control handling of interrupts

;; Enable interrupts template
(define_insn "ei"
  [(unspec_volatile [(const_int 0)] UNSPEC_EI)]
  ""
  "ei"
  [(set_attr "length" "4")])

;; Enable interrupts template
(define_insn "di"
  [(unspec_volatile [(const_int 0)] UNSPEC_DI)]
  ""
  "di"
  [(set_attr "length" "4")])

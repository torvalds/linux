;;- Machine description for ARM for GNU compiler
;;  Copyright 1991, 1993, 1994, 1995, 1996, 1996, 1997, 1998, 1999, 2000,
;;  2001, 2002, 2003, 2004, 2005, 2006  Free Software Foundation, Inc.
;;  Contributed by Pieter `Tiggr' Schoenmakers (rcpieter@win.tue.nl)
;;  and Martin Simmons (@harleqn.co.uk).
;;  More major hacks by Richard Earnshaw (rearnsha@arm.com).

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

;;- See file "rtl.def" for documentation on define_insn, match_*, et. al.


;;---------------------------------------------------------------------------
;; Constants

;; Register numbers
(define_constants
  [(R0_REGNUM        0)		; First CORE register
   (IP_REGNUM	    12)		; Scratch register
   (SP_REGNUM	    13)		; Stack pointer
   (LR_REGNUM       14)		; Return address register
   (PC_REGNUM	    15)		; Program counter
   (CC_REGNUM       24)		; Condition code pseudo register
   (LAST_ARM_REGNUM 15)		;
   (FPA_F0_REGNUM   16)		; FIRST_FPA_REGNUM
   (FPA_F7_REGNUM   23)		; LAST_FPA_REGNUM
  ]
)
;; 3rd operand to select_dominance_cc_mode
(define_constants
  [(DOM_CC_X_AND_Y  0)
   (DOM_CC_NX_OR_Y  1)
   (DOM_CC_X_OR_Y   2)
  ]
)

;; UNSPEC Usage:
;; Note: sin and cos are no-longer used.

(define_constants
  [(UNSPEC_SIN       0)	; `sin' operation (MODE_FLOAT):
			;   operand 0 is the result,
			;   operand 1 the parameter.
   (UNPSEC_COS	     1)	; `cos' operation (MODE_FLOAT):
			;   operand 0 is the result,
			;   operand 1 the parameter.
   (UNSPEC_PUSH_MULT 2)	; `push multiple' operation:
			;   operand 0 is the first register,
			;   subsequent registers are in parallel (use ...)
			;   expressions.
   (UNSPEC_PIC_SYM   3) ; A symbol that has been treated properly for pic
			;   usage, that is, we will add the pic_register
			;   value to it before trying to dereference it.
   (UNSPEC_PIC_BASE  4)	; Adding the PC value to the offset to the
			;   GLOBAL_OFFSET_TABLE.  The operation is fully
			;   described by the RTL but must be wrapped to
			;   prevent combine from trying to rip it apart.
   (UNSPEC_PRLG_STK  5) ; A special barrier that prevents frame accesses 
			;   being scheduled before the stack adjustment insn.
   (UNSPEC_PROLOGUE_USE 6) ; As USE insns are not meaningful after reload,
   			; this unspec is used to prevent the deletion of
   			; instructions setting registers for EH handling
   			; and stack frame generation.  Operand 0 is the
   			; register to "use".
   (UNSPEC_CHECK_ARCH 7); Set CCs to indicate 26-bit or 32-bit mode.
   (UNSPEC_WSHUFH    8) ; Used by the intrinsic form of the iWMMXt WSHUFH instruction.
   (UNSPEC_WACC      9) ; Used by the intrinsic form of the iWMMXt WACC instruction.
   (UNSPEC_TMOVMSK  10) ; Used by the intrinsic form of the iWMMXt TMOVMSK instruction.
   (UNSPEC_WSAD     11) ; Used by the intrinsic form of the iWMMXt WSAD instruction.
   (UNSPEC_WSADZ    12) ; Used by the intrinsic form of the iWMMXt WSADZ instruction.
   (UNSPEC_WMACS    13) ; Used by the intrinsic form of the iWMMXt WMACS instruction.
   (UNSPEC_WMACU    14) ; Used by the intrinsic form of the iWMMXt WMACU instruction.
   (UNSPEC_WMACSZ   15) ; Used by the intrinsic form of the iWMMXt WMACSZ instruction.
   (UNSPEC_WMACUZ   16) ; Used by the intrinsic form of the iWMMXt WMACUZ instruction.
   (UNSPEC_CLRDI    17) ; Used by the intrinsic form of the iWMMXt CLRDI instruction.
   (UNSPEC_WMADDS   18) ; Used by the intrinsic form of the iWMMXt WMADDS instruction.
   (UNSPEC_WMADDU   19) ; Used by the intrinsic form of the iWMMXt WMADDU instruction.
   (UNSPEC_TLS      20) ; A symbol that has been treated properly for TLS usage.
   (UNSPEC_PIC_LABEL 21) ; A label used for PIC access that does not appear in the
                         ; instruction stream.
  ]
)

;; UNSPEC_VOLATILE Usage:

(define_constants
  [(VUNSPEC_BLOCKAGE 0) ; `blockage' insn to prevent scheduling across an
			;   insn in the code.
   (VUNSPEC_EPILOGUE 1) ; `epilogue' insn, used to represent any part of the
			;   instruction epilogue sequence that isn't expanded
			;   into normal RTL.  Used for both normal and sibcall
			;   epilogues.
   (VUNSPEC_ALIGN    2) ; `align' insn.  Used at the head of a minipool table 
			;   for inlined constants.
   (VUNSPEC_POOL_END 3) ; `end-of-table'.  Used to mark the end of a minipool
			;   table.
   (VUNSPEC_POOL_1   4) ; `pool-entry(1)'.  An entry in the constant pool for
			;   an 8-bit object.
   (VUNSPEC_POOL_2   5) ; `pool-entry(2)'.  An entry in the constant pool for
			;   a 16-bit object.
   (VUNSPEC_POOL_4   6) ; `pool-entry(4)'.  An entry in the constant pool for
			;   a 32-bit object.
   (VUNSPEC_POOL_8   7) ; `pool-entry(8)'.  An entry in the constant pool for
			;   a 64-bit object.
   (VUNSPEC_TMRC     8) ; Used by the iWMMXt TMRC instruction.
   (VUNSPEC_TMCR     9) ; Used by the iWMMXt TMCR instruction.
   (VUNSPEC_ALIGN8   10) ; 8-byte alignment version of VUNSPEC_ALIGN
   (VUNSPEC_WCMP_EQ  11) ; Used by the iWMMXt WCMPEQ instructions
   (VUNSPEC_WCMP_GTU 12) ; Used by the iWMMXt WCMPGTU instructions
   (VUNSPEC_WCMP_GT  13) ; Used by the iwMMXT WCMPGT instructions
   (VUNSPEC_EH_RETURN 20); Use to override the return address for exception
			 ; handling.
  ]
)

;;---------------------------------------------------------------------------
;; Attributes

; IS_THUMB is set to 'yes' when we are generating Thumb code, and 'no' when
; generating ARM code.  This is used to control the length of some insn
; patterns that share the same RTL in both ARM and Thumb code.
(define_attr "is_thumb" "no,yes" (const (symbol_ref "thumb_code")))

; IS_STRONGARM is set to 'yes' when compiling for StrongARM, it affects
; scheduling decisions for the load unit and the multiplier.
(define_attr "is_strongarm" "no,yes" (const (symbol_ref "arm_tune_strongarm")))

; IS_XSCALE is set to 'yes' when compiling for XScale.
(define_attr "is_xscale" "no,yes" (const (symbol_ref "arm_tune_xscale")))

;; Operand number of an input operand that is shifted.  Zero if the
;; given instruction does not shift one of its input operands.
(define_attr "shift" "" (const_int 0))

; Floating Point Unit.  If we only have floating point emulation, then there
; is no point in scheduling the floating point insns.  (Well, for best
; performance we should try and group them together).
(define_attr "fpu" "none,fpa,fpe2,fpe3,maverick,vfp"
  (const (symbol_ref "arm_fpu_attr")))

; LENGTH of an instruction (in bytes)
(define_attr "length" "" (const_int 4))

; POOL_RANGE is how far away from a constant pool entry that this insn
; can be placed.  If the distance is zero, then this insn will never
; reference the pool.
; NEG_POOL_RANGE is nonzero for insns that can reference a constant pool entry
; before its address.
(define_attr "pool_range" "" (const_int 0))
(define_attr "neg_pool_range" "" (const_int 0))

; An assembler sequence may clobber the condition codes without us knowing.
; If such an insn references the pool, then we have no way of knowing how,
; so use the most conservative value for pool_range.
(define_asm_attributes
 [(set_attr "conds" "clob")
  (set_attr "length" "4")
  (set_attr "pool_range" "250")])

;; The instruction used to implement a particular pattern.  This
;; information is used by pipeline descriptions to provide accurate
;; scheduling information.

(define_attr "insn"
        "smulxy,smlaxy,smlalxy,smulwy,smlawx,mul,muls,mla,mlas,umull,umulls,umlal,umlals,smull,smulls,smlal,smlals,smlawy,smuad,smuadx,smlad,smladx,smusd,smusdx,smlsd,smlsdx,smmul,smmulr,other"
        (const_string "other"))

; TYPE attribute is used to detect floating point instructions which, if
; running on a co-processor can run in parallel with other, basic instructions
; If write-buffer scheduling is enabled then it can also be used in the
; scheduling of writes.

; Classification of each insn
; alu		any alu  instruction that doesn't hit memory or fp
;		regs or have a shifted source operand
; alu_shift	any data instruction that doesn't hit memory or fp
;		regs, but has a source operand shifted by a constant
; alu_shift_reg	any data instruction that doesn't hit memory or fp
;		regs, but has a source operand shifted by a register value
; mult		a multiply instruction
; block		blockage insn, this blocks all functional units
; float		a floating point arithmetic operation (subject to expansion)
; fdivd		DFmode floating point division
; fdivs		SFmode floating point division
; fmul		Floating point multiply
; ffmul		Fast floating point multiply
; farith	Floating point arithmetic (4 cycle)
; ffarith	Fast floating point arithmetic (2 cycle)
; float_em	a floating point arithmetic operation that is normally emulated
;		even on a machine with an fpa.
; f_load	a floating point load from memory
; f_store	a floating point store to memory
; f_load[sd]	single/double load from memory
; f_store[sd]	single/double store to memory
; f_flag	a transfer of co-processor flags to the CPSR
; f_mem_r	a transfer of a floating point register to a real reg via mem
; r_mem_f	the reverse of f_mem_r
; f_2_r		fast transfer float to arm (no memory needed)
; r_2_f		fast transfer arm to float
; f_cvt		convert floating<->integral
; branch	a branch
; call		a subroutine call
; load_byte	load byte(s) from memory to arm registers
; load1		load 1 word from memory to arm registers
; load2         load 2 words from memory to arm registers
; load3         load 3 words from memory to arm registers
; load4         load 4 words from memory to arm registers
; store		store 1 word to memory from arm registers
; store2	store 2 words
; store3	store 3 words
; store4	store 4 (or more) words
;  Additions for Cirrus Maverick co-processor:
; mav_farith	Floating point arithmetic (4 cycle)
; mav_dmult	Double multiplies (7 cycle)
;
(define_attr "type"
	"alu,alu_shift,alu_shift_reg,mult,block,float,fdivx,fdivd,fdivs,fmul,ffmul,farith,ffarith,f_flag,float_em,f_load,f_store,f_loads,f_loadd,f_stores,f_stored,f_mem_r,r_mem_f,f_2_r,r_2_f,f_cvt,branch,call,load_byte,load1,load2,load3,load4,store1,store2,store3,store4,mav_farith,mav_dmult" 
	(if_then_else 
	 (eq_attr "insn" "smulxy,smlaxy,smlalxy,smulwy,smlawx,mul,muls,mla,mlas,umull,umulls,umlal,umlals,smull,smulls,smlal,smlals")
	 (const_string "mult")
	 (const_string "alu")))

; Load scheduling, set from the arm_ld_sched variable
; initialized by arm_override_options() 
(define_attr "ldsched" "no,yes" (const (symbol_ref "arm_ld_sched")))

; condition codes: this one is used by final_prescan_insn to speed up
; conditionalizing instructions.  It saves having to scan the rtl to see if
; it uses or alters the condition codes.
; 
; USE means that the condition codes are used by the insn in the process of
;   outputting code, this means (at present) that we can't use the insn in
;   inlined branches
;
; SET means that the purpose of the insn is to set the condition codes in a
;   well defined manner.
;
; CLOB means that the condition codes are altered in an undefined manner, if
;   they are altered at all
;
; JUMP_CLOB is used when the condition cannot be represented by a single
;   instruction (UNEQ and LTGT).  These cannot be predicated.
;
; NOCOND means that the condition codes are neither altered nor affect the
;   output of this insn

(define_attr "conds" "use,set,clob,jump_clob,nocond"
	(if_then_else (eq_attr "type" "call")
	 (const_string "clob")
	 (const_string "nocond")))

; Predicable means that the insn can be conditionally executed based on
; an automatically added predicate (additional patterns are generated by 
; gen...).  We default to 'no' because no Thumb patterns match this rule
; and not all ARM patterns do.
(define_attr "predicable" "no,yes" (const_string "no"))

; Only model the write buffer for ARM6 and ARM7.  Earlier processors don't
; have one.  Later ones, such as StrongARM, have write-back caches, so don't
; suffer blockages enough to warrant modelling this (and it can adversely
; affect the schedule).
(define_attr "model_wbuf" "no,yes" (const (symbol_ref "arm_tune_wbuf")))

; WRITE_CONFLICT implies that a read following an unrelated write is likely
; to stall the processor.  Used with model_wbuf above.
(define_attr "write_conflict" "no,yes"
  (if_then_else (eq_attr "type"
		 "block,float_em,f_load,f_store,f_mem_r,r_mem_f,call,load1")
		(const_string "yes")
		(const_string "no")))

; Classify the insns into those that take one cycle and those that take more
; than one on the main cpu execution unit.
(define_attr "core_cycles" "single,multi"
  (if_then_else (eq_attr "type"
		 "alu,alu_shift,float,fdivx,fdivd,fdivs,fmul,ffmul,farith,ffarith")
		(const_string "single")
	        (const_string "multi")))

;; FAR_JUMP is "yes" if a BL instruction is used to generate a branch to a
;; distant label.  Only applicable to Thumb code.
(define_attr "far_jump" "yes,no" (const_string "no"))


;;---------------------------------------------------------------------------
;; Mode macros

; A list of modes that are exactly 64 bits in size.  We use this to expand
; some splits that are the same for all modes when operating on ARM 
; registers.
(define_mode_macro ANY64 [DI DF V8QI V4HI V2SI V2SF])

;;---------------------------------------------------------------------------
;; Predicates

(include "predicates.md")
(include "constraints.md")

;;---------------------------------------------------------------------------
;; Pipeline descriptions

;; Processor type.  This is created automatically from arm-cores.def.
(include "arm-tune.md")

;; True if the generic scheduling description should be used.

(define_attr "generic_sched" "yes,no"
  (const (if_then_else 
          (eq_attr "tune" "arm926ejs,arm1020e,arm1026ejs,arm1136js,arm1136jfs") 
          (const_string "no")
          (const_string "yes"))))

(define_attr "generic_vfp" "yes,no"
  (const (if_then_else
	  (and (eq_attr "fpu" "vfp")
	       (eq_attr "tune" "!arm1020e,arm1022e"))
	  (const_string "yes")
	  (const_string "no"))))

(include "arm-generic.md")
(include "arm926ejs.md")
(include "arm1020e.md")
(include "arm1026ejs.md")
(include "arm1136jfs.md")


;;---------------------------------------------------------------------------
;; Insn patterns
;;
;; Addition insns.

;; Note: For DImode insns, there is normally no reason why operands should
;; not be in the same register, what we don't want is for something being
;; written to partially overlap something that is an input.
;; Cirrus 64bit additions should not be split because we have a native
;; 64bit addition instructions.

(define_expand "adddi3"
 [(parallel
   [(set (match_operand:DI           0 "s_register_operand" "")
	  (plus:DI (match_operand:DI 1 "s_register_operand" "")
	           (match_operand:DI 2 "s_register_operand" "")))
    (clobber (reg:CC CC_REGNUM))])]
  "TARGET_EITHER"
  "
  if (TARGET_HARD_FLOAT && TARGET_MAVERICK)
    {
      if (!cirrus_fp_register (operands[0], DImode))
        operands[0] = force_reg (DImode, operands[0]);
      if (!cirrus_fp_register (operands[1], DImode))
        operands[1] = force_reg (DImode, operands[1]);
      emit_insn (gen_cirrus_adddi3 (operands[0], operands[1], operands[2]));
      DONE;
    }

  if (TARGET_THUMB)
    {
      if (GET_CODE (operands[1]) != REG)
        operands[1] = force_reg (SImode, operands[1]);
      if (GET_CODE (operands[2]) != REG)
        operands[2] = force_reg (SImode, operands[2]);
     }
  "
)

(define_insn "*thumb_adddi3"
  [(set (match_operand:DI          0 "register_operand" "=l")
	(plus:DI (match_operand:DI 1 "register_operand" "%0")
		 (match_operand:DI 2 "register_operand" "l")))
   (clobber (reg:CC CC_REGNUM))
  ]
  "TARGET_THUMB"
  "add\\t%Q0, %Q0, %Q2\;adc\\t%R0, %R0, %R2"
  [(set_attr "length" "4")]
)

(define_insn_and_split "*arm_adddi3"
  [(set (match_operand:DI          0 "s_register_operand" "=&r,&r")
	(plus:DI (match_operand:DI 1 "s_register_operand" "%0, 0")
		 (match_operand:DI 2 "s_register_operand" "r,  0")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM && !(TARGET_HARD_FLOAT && TARGET_MAVERICK)"
  "#"
  "TARGET_ARM && reload_completed"
  [(parallel [(set (reg:CC_C CC_REGNUM)
		   (compare:CC_C (plus:SI (match_dup 1) (match_dup 2))
				 (match_dup 1)))
	      (set (match_dup 0) (plus:SI (match_dup 1) (match_dup 2)))])
   (set (match_dup 3) (plus:SI (ltu:SI (reg:CC_C CC_REGNUM) (const_int 0))
			       (plus:SI (match_dup 4) (match_dup 5))))]
  "
  {
    operands[3] = gen_highpart (SImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[4] = gen_highpart (SImode, operands[1]);
    operands[1] = gen_lowpart (SImode, operands[1]);
    operands[5] = gen_highpart (SImode, operands[2]);
    operands[2] = gen_lowpart (SImode, operands[2]);
  }"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_insn_and_split "*adddi_sesidi_di"
  [(set (match_operand:DI 0 "s_register_operand" "=&r,&r")
	(plus:DI (sign_extend:DI
		  (match_operand:SI 2 "s_register_operand" "r,r"))
		 (match_operand:DI 1 "s_register_operand" "r,0")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM && !(TARGET_HARD_FLOAT && TARGET_MAVERICK)"
  "#"
  "TARGET_ARM && reload_completed"
  [(parallel [(set (reg:CC_C CC_REGNUM)
		   (compare:CC_C (plus:SI (match_dup 1) (match_dup 2))
				 (match_dup 1)))
	      (set (match_dup 0) (plus:SI (match_dup 1) (match_dup 2)))])
   (set (match_dup 3) (plus:SI (ltu:SI (reg:CC_C CC_REGNUM) (const_int 0))
			       (plus:SI (ashiftrt:SI (match_dup 2)
						     (const_int 31))
					(match_dup 4))))]
  "
  {
    operands[3] = gen_highpart (SImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[4] = gen_highpart (SImode, operands[1]);
    operands[1] = gen_lowpart (SImode, operands[1]);
    operands[2] = gen_lowpart (SImode, operands[2]);
  }"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_insn_and_split "*adddi_zesidi_di"
  [(set (match_operand:DI 0 "s_register_operand" "=&r,&r")
	(plus:DI (zero_extend:DI
		  (match_operand:SI 2 "s_register_operand" "r,r"))
		 (match_operand:DI 1 "s_register_operand" "r,0")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM && !(TARGET_HARD_FLOAT && TARGET_MAVERICK)"
  "#"
  "TARGET_ARM && reload_completed"
  [(parallel [(set (reg:CC_C CC_REGNUM)
		   (compare:CC_C (plus:SI (match_dup 1) (match_dup 2))
				 (match_dup 1)))
	      (set (match_dup 0) (plus:SI (match_dup 1) (match_dup 2)))])
   (set (match_dup 3) (plus:SI (ltu:SI (reg:CC_C CC_REGNUM) (const_int 0))
			       (plus:SI (match_dup 4) (const_int 0))))]
  "
  {
    operands[3] = gen_highpart (SImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[4] = gen_highpart (SImode, operands[1]);
    operands[1] = gen_lowpart (SImode, operands[1]);
    operands[2] = gen_lowpart (SImode, operands[2]);
  }"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_expand "addsi3"
  [(set (match_operand:SI          0 "s_register_operand" "")
	(plus:SI (match_operand:SI 1 "s_register_operand" "")
		 (match_operand:SI 2 "reg_or_int_operand" "")))]
  "TARGET_EITHER"
  "
  if (TARGET_ARM && GET_CODE (operands[2]) == CONST_INT)
    {
      arm_split_constant (PLUS, SImode, NULL_RTX,
	                  INTVAL (operands[2]), operands[0], operands[1],
			  optimize && !no_new_pseudos);
      DONE;
    }
  "
)

; If there is a scratch available, this will be faster than synthesizing the
; addition.
(define_peephole2
  [(match_scratch:SI 3 "r")
   (set (match_operand:SI          0 "arm_general_register_operand" "")
	(plus:SI (match_operand:SI 1 "arm_general_register_operand" "")
		 (match_operand:SI 2 "const_int_operand"  "")))]
  "TARGET_ARM &&
   !(const_ok_for_arm (INTVAL (operands[2]))
     || const_ok_for_arm (-INTVAL (operands[2])))
    && const_ok_for_arm (~INTVAL (operands[2]))"
  [(set (match_dup 3) (match_dup 2))
   (set (match_dup 0) (plus:SI (match_dup 1) (match_dup 3)))]
  ""
)

(define_insn_and_split "*arm_addsi3"
  [(set (match_operand:SI          0 "s_register_operand" "=r,r,r")
	(plus:SI (match_operand:SI 1 "s_register_operand" "%r,r,r")
		 (match_operand:SI 2 "reg_or_int_operand" "rI,L,?n")))]
  "TARGET_ARM"
  "@
   add%?\\t%0, %1, %2
   sub%?\\t%0, %1, #%n2
   #"
  "TARGET_ARM &&
   GET_CODE (operands[2]) == CONST_INT
   && !(const_ok_for_arm (INTVAL (operands[2]))
        || const_ok_for_arm (-INTVAL (operands[2])))"
  [(clobber (const_int 0))]
  "
  arm_split_constant (PLUS, SImode, curr_insn,
	              INTVAL (operands[2]), operands[0],
		      operands[1], 0);
  DONE;
  "
  [(set_attr "length" "4,4,16")
   (set_attr "predicable" "yes")]
)

;; Register group 'k' is a single register group containing only the stack
;; register.  Trying to reload it will always fail catastrophically,
;; so never allow those alternatives to match if reloading is needed.

(define_insn "*thumb_addsi3"
  [(set (match_operand:SI          0 "register_operand" "=l,l,l,*r,*h,l,!k")
	(plus:SI (match_operand:SI 1 "register_operand" "%0,0,l,*0,*0,!k,!k")
		 (match_operand:SI 2 "nonmemory_operand" "I,J,lL,*h,*r,!M,!O")))]
  "TARGET_THUMB"
  "*
   static const char * const asms[] = 
   {
     \"add\\t%0, %0, %2\",
     \"sub\\t%0, %0, #%n2\",
     \"add\\t%0, %1, %2\",
     \"add\\t%0, %0, %2\",
     \"add\\t%0, %0, %2\",
     \"add\\t%0, %1, %2\",
     \"add\\t%0, %1, %2\"
   };
   if ((which_alternative == 2 || which_alternative == 6)
       && GET_CODE (operands[2]) == CONST_INT
       && INTVAL (operands[2]) < 0)
     return \"sub\\t%0, %1, #%n2\";
   return asms[which_alternative];
  "
  [(set_attr "length" "2")]
)

;; Reloading and elimination of the frame pointer can
;; sometimes cause this optimization to be missed.
(define_peephole2
  [(set (match_operand:SI 0 "arm_general_register_operand" "")
	(match_operand:SI 1 "const_int_operand" ""))
   (set (match_dup 0)
	(plus:SI (match_dup 0) (reg:SI SP_REGNUM)))]
  "TARGET_THUMB
   && (unsigned HOST_WIDE_INT) (INTVAL (operands[1])) < 1024
   && (INTVAL (operands[1]) & 3) == 0"
  [(set (match_dup 0) (plus:SI (reg:SI SP_REGNUM) (match_dup 1)))]
  ""
)

(define_insn "*addsi3_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV
	 (plus:SI (match_operand:SI 1 "s_register_operand" "r, r")
		  (match_operand:SI 2 "arm_add_operand"    "rI,L"))
	 (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=r,r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  "TARGET_ARM"
  "@
   add%?s\\t%0, %1, %2
   sub%?s\\t%0, %1, #%n2"
  [(set_attr "conds" "set")]
)

(define_insn "*addsi3_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV
	 (plus:SI (match_operand:SI 0 "s_register_operand" "r, r")
		  (match_operand:SI 1 "arm_add_operand"    "rI,L"))
	 (const_int 0)))]
  "TARGET_ARM"
  "@
   cmn%?\\t%0, %1
   cmp%?\\t%0, #%n1"
  [(set_attr "conds" "set")]
)

(define_insn "*compare_negsi_si"
  [(set (reg:CC_Z CC_REGNUM)
	(compare:CC_Z
	 (neg:SI (match_operand:SI 0 "s_register_operand" "r"))
	 (match_operand:SI 1 "s_register_operand" "r")))]
  "TARGET_ARM"
  "cmn%?\\t%1, %0"
  [(set_attr "conds" "set")]
)

;; This is the canonicalization of addsi3_compare0_for_combiner when the
;; addend is a constant.
(define_insn "*cmpsi2_addneg"
  [(set (reg:CC CC_REGNUM)
	(compare:CC
	 (match_operand:SI 1 "s_register_operand" "r,r")
	 (match_operand:SI 2 "arm_addimm_operand" "I,L")))
   (set (match_operand:SI 0 "s_register_operand" "=r,r")
	(plus:SI (match_dup 1)
		 (match_operand:SI 3 "arm_addimm_operand" "L,I")))]
  "TARGET_ARM && INTVAL (operands[2]) == -INTVAL (operands[3])"
  "@
   sub%?s\\t%0, %1, %2
   add%?s\\t%0, %1, #%n2"
  [(set_attr "conds" "set")]
)

;; Convert the sequence
;;  sub  rd, rn, #1
;;  cmn  rd, #1	(equivalent to cmp rd, #-1)
;;  bne  dest
;; into
;;  subs rd, rn, #1
;;  bcs  dest	((unsigned)rn >= 1)
;; similarly for the beq variant using bcc.
;; This is a common looping idiom (while (n--))
(define_peephole2
  [(set (match_operand:SI 0 "arm_general_register_operand" "")
	(plus:SI (match_operand:SI 1 "arm_general_register_operand" "")
		 (const_int -1)))
   (set (match_operand 2 "cc_register" "")
	(compare (match_dup 0) (const_int -1)))
   (set (pc)
	(if_then_else (match_operator 3 "equality_operator"
		       [(match_dup 2) (const_int 0)])
		      (match_operand 4 "" "")
		      (match_operand 5 "" "")))]
  "TARGET_ARM && peep2_reg_dead_p (3, operands[2])"
  [(parallel[
    (set (match_dup 2)
	 (compare:CC
	  (match_dup 1) (const_int 1)))
    (set (match_dup 0) (plus:SI (match_dup 1) (const_int -1)))])
   (set (pc)
	(if_then_else (match_op_dup 3 [(match_dup 2) (const_int 0)])
		      (match_dup 4)
		      (match_dup 5)))]
  "operands[2] = gen_rtx_REG (CCmode, CC_REGNUM);
   operands[3] = gen_rtx_fmt_ee ((GET_CODE (operands[3]) == NE
				  ? GEU : LTU),
				 VOIDmode, 
				 operands[2], const0_rtx);"
)

;; The next four insns work because they compare the result with one of
;; the operands, and we know that the use of the condition code is
;; either GEU or LTU, so we can use the carry flag from the addition
;; instead of doing the compare a second time.
(define_insn "*addsi3_compare_op1"
  [(set (reg:CC_C CC_REGNUM)
	(compare:CC_C
	 (plus:SI (match_operand:SI 1 "s_register_operand" "r,r")
		  (match_operand:SI 2 "arm_add_operand" "rI,L"))
	 (match_dup 1)))
   (set (match_operand:SI 0 "s_register_operand" "=r,r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  "TARGET_ARM"
  "@
   add%?s\\t%0, %1, %2
   sub%?s\\t%0, %1, #%n2"
  [(set_attr "conds" "set")]
)

(define_insn "*addsi3_compare_op2"
  [(set (reg:CC_C CC_REGNUM)
	(compare:CC_C
	 (plus:SI (match_operand:SI 1 "s_register_operand" "r,r")
		  (match_operand:SI 2 "arm_add_operand" "rI,L"))
	 (match_dup 2)))
   (set (match_operand:SI 0 "s_register_operand" "=r,r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  "TARGET_ARM"
  "@
   add%?s\\t%0, %1, %2
   sub%?s\\t%0, %1, #%n2"
  [(set_attr "conds" "set")]
)

(define_insn "*compare_addsi2_op0"
  [(set (reg:CC_C CC_REGNUM)
	(compare:CC_C
	 (plus:SI (match_operand:SI 0 "s_register_operand" "r,r")
		  (match_operand:SI 1 "arm_add_operand" "rI,L"))
	 (match_dup 0)))]
  "TARGET_ARM"
  "@
   cmn%?\\t%0, %1
   cmp%?\\t%0, #%n1"
  [(set_attr "conds" "set")]
)

(define_insn "*compare_addsi2_op1"
  [(set (reg:CC_C CC_REGNUM)
	(compare:CC_C
	 (plus:SI (match_operand:SI 0 "s_register_operand" "r,r")
		  (match_operand:SI 1 "arm_add_operand" "rI,L"))
	 (match_dup 1)))]
  "TARGET_ARM"
  "@
   cmn%?\\t%0, %1
   cmp%?\\t%0, #%n1"
  [(set_attr "conds" "set")]
)

(define_insn "*addsi3_carryin"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(plus:SI (ltu:SI (reg:CC_C CC_REGNUM) (const_int 0))
		 (plus:SI (match_operand:SI 1 "s_register_operand" "r")
			  (match_operand:SI 2 "arm_rhs_operand" "rI"))))]
  "TARGET_ARM"
  "adc%?\\t%0, %1, %2"
  [(set_attr "conds" "use")]
)

(define_insn "*addsi3_carryin_shift"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(plus:SI (ltu:SI (reg:CC_C CC_REGNUM) (const_int 0))
		 (plus:SI
		   (match_operator:SI 2 "shift_operator"
		      [(match_operand:SI 3 "s_register_operand" "r")
		       (match_operand:SI 4 "reg_or_int_operand" "rM")])
		    (match_operand:SI 1 "s_register_operand" "r"))))]
  "TARGET_ARM"
  "adc%?\\t%0, %1, %3%S2"
  [(set_attr "conds" "use")
   (set (attr "type") (if_then_else (match_operand 4 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*addsi3_carryin_alt1"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(plus:SI (plus:SI (match_operand:SI 1 "s_register_operand" "r")
			  (match_operand:SI 2 "arm_rhs_operand" "rI"))
		 (ltu:SI (reg:CC_C CC_REGNUM) (const_int 0))))]
  "TARGET_ARM"
  "adc%?\\t%0, %1, %2"
  [(set_attr "conds" "use")]
)

(define_insn "*addsi3_carryin_alt2"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(plus:SI (plus:SI (ltu:SI (reg:CC_C CC_REGNUM) (const_int 0))
			  (match_operand:SI 1 "s_register_operand" "r"))
		 (match_operand:SI 2 "arm_rhs_operand" "rI")))]
  "TARGET_ARM"
  "adc%?\\t%0, %1, %2"
  [(set_attr "conds" "use")]
)

(define_insn "*addsi3_carryin_alt3"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(plus:SI (plus:SI (ltu:SI (reg:CC_C CC_REGNUM) (const_int 0))
			  (match_operand:SI 2 "arm_rhs_operand" "rI"))
		 (match_operand:SI 1 "s_register_operand" "r")))]
  "TARGET_ARM"
  "adc%?\\t%0, %1, %2"
  [(set_attr "conds" "use")]
)

(define_insn "incscc"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
        (plus:SI (match_operator:SI 2 "arm_comparison_operator"
                    [(match_operand:CC 3 "cc_register" "") (const_int 0)])
                 (match_operand:SI 1 "s_register_operand" "0,?r")))]
  "TARGET_ARM"
  "@
  add%d2\\t%0, %1, #1
  mov%D2\\t%0, %1\;add%d2\\t%0, %1, #1"
  [(set_attr "conds" "use")
   (set_attr "length" "4,8")]
)

; transform ((x << y) - 1) to ~(~(x-1) << y)  Where X is a constant.
(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(plus:SI (ashift:SI (match_operand:SI 1 "const_int_operand" "")
			    (match_operand:SI 2 "s_register_operand" ""))
		 (const_int -1)))
   (clobber (match_operand:SI 3 "s_register_operand" ""))]
  "TARGET_ARM"
  [(set (match_dup 3) (match_dup 1))
   (set (match_dup 0) (not:SI (ashift:SI (match_dup 3) (match_dup 2))))]
  "
  operands[1] = GEN_INT (~(INTVAL (operands[1]) - 1));
")

(define_expand "addsf3"
  [(set (match_operand:SF          0 "s_register_operand" "")
	(plus:SF (match_operand:SF 1 "s_register_operand" "")
		 (match_operand:SF 2 "arm_float_add_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "
  if (TARGET_MAVERICK
      && !cirrus_fp_register (operands[2], SFmode))
    operands[2] = force_reg (SFmode, operands[2]);
")

(define_expand "adddf3"
  [(set (match_operand:DF          0 "s_register_operand" "")
	(plus:DF (match_operand:DF 1 "s_register_operand" "")
		 (match_operand:DF 2 "arm_float_add_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "
  if (TARGET_MAVERICK
      && !cirrus_fp_register (operands[2], DFmode))
    operands[2] = force_reg (DFmode, operands[2]);
")

(define_expand "subdi3"
 [(parallel
   [(set (match_operand:DI            0 "s_register_operand" "")
	  (minus:DI (match_operand:DI 1 "s_register_operand" "")
	            (match_operand:DI 2 "s_register_operand" "")))
    (clobber (reg:CC CC_REGNUM))])]
  "TARGET_EITHER"
  "
  if (TARGET_HARD_FLOAT && TARGET_MAVERICK
      && TARGET_ARM
      && cirrus_fp_register (operands[0], DImode)
      && cirrus_fp_register (operands[1], DImode))
    {
      emit_insn (gen_cirrus_subdi3 (operands[0], operands[1], operands[2]));
      DONE;
    }

  if (TARGET_THUMB)
    {
      if (GET_CODE (operands[1]) != REG)
        operands[1] = force_reg (SImode, operands[1]);
      if (GET_CODE (operands[2]) != REG)
        operands[2] = force_reg (SImode, operands[2]);
     }	
  "
)

(define_insn "*arm_subdi3"
  [(set (match_operand:DI           0 "s_register_operand" "=&r,&r,&r")
	(minus:DI (match_operand:DI 1 "s_register_operand" "0,r,0")
		  (match_operand:DI 2 "s_register_operand" "r,0,0")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "subs\\t%Q0, %Q1, %Q2\;sbc\\t%R0, %R1, %R2"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_insn "*thumb_subdi3"
  [(set (match_operand:DI           0 "register_operand" "=l")
	(minus:DI (match_operand:DI 1 "register_operand"  "0")
		  (match_operand:DI 2 "register_operand"  "l")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_THUMB"
  "sub\\t%Q0, %Q0, %Q2\;sbc\\t%R0, %R0, %R2"
  [(set_attr "length" "4")]
)

(define_insn "*subdi_di_zesidi"
  [(set (match_operand:DI           0 "s_register_operand" "=&r,&r")
	(minus:DI (match_operand:DI 1 "s_register_operand"  "?r,0")
		  (zero_extend:DI
		   (match_operand:SI 2 "s_register_operand"  "r,r"))))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "subs\\t%Q0, %Q1, %2\;sbc\\t%R0, %R1, #0"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_insn "*subdi_di_sesidi"
  [(set (match_operand:DI            0 "s_register_operand" "=&r,&r")
	(minus:DI (match_operand:DI  1 "s_register_operand"  "r,0")
		  (sign_extend:DI
		   (match_operand:SI 2 "s_register_operand"  "r,r"))))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "subs\\t%Q0, %Q1, %2\;sbc\\t%R0, %R1, %2, asr #31"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_insn "*subdi_zesidi_di"
  [(set (match_operand:DI            0 "s_register_operand" "=&r,&r")
	(minus:DI (zero_extend:DI
		   (match_operand:SI 2 "s_register_operand"  "r,r"))
		  (match_operand:DI  1 "s_register_operand" "?r,0")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "rsbs\\t%Q0, %Q1, %2\;rsc\\t%R0, %R1, #0"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_insn "*subdi_sesidi_di"
  [(set (match_operand:DI            0 "s_register_operand" "=&r,&r")
	(minus:DI (sign_extend:DI
		   (match_operand:SI 2 "s_register_operand"   "r,r"))
		  (match_operand:DI  1 "s_register_operand"  "?r,0")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "rsbs\\t%Q0, %Q1, %2\;rsc\\t%R0, %R1, %2, asr #31"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_insn "*subdi_zesidi_zesidi"
  [(set (match_operand:DI            0 "s_register_operand" "=r")
	(minus:DI (zero_extend:DI
		   (match_operand:SI 1 "s_register_operand"  "r"))
		  (zero_extend:DI
		   (match_operand:SI 2 "s_register_operand"  "r"))))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "subs\\t%Q0, %1, %2\;rsc\\t%R0, %1, %1"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_expand "subsi3"
  [(set (match_operand:SI           0 "s_register_operand" "")
	(minus:SI (match_operand:SI 1 "reg_or_int_operand" "")
		  (match_operand:SI 2 "s_register_operand" "")))]
  "TARGET_EITHER"
  "
  if (GET_CODE (operands[1]) == CONST_INT)
    {
      if (TARGET_ARM)
        {
          arm_split_constant (MINUS, SImode, NULL_RTX,
	                      INTVAL (operands[1]), operands[0],
	  		      operands[2], optimize && !no_new_pseudos);
          DONE;
	}
      else /* TARGET_THUMB */
        operands[1] = force_reg (SImode, operands[1]);
    }
  "
)

(define_insn "*thumb_subsi3_insn"
  [(set (match_operand:SI           0 "register_operand" "=l")
	(minus:SI (match_operand:SI 1 "register_operand" "l")
		  (match_operand:SI 2 "register_operand" "l")))]
  "TARGET_THUMB"
  "sub\\t%0, %1, %2"
  [(set_attr "length" "2")]
)

(define_insn_and_split "*arm_subsi3_insn"
  [(set (match_operand:SI           0 "s_register_operand" "=r,r")
	(minus:SI (match_operand:SI 1 "reg_or_int_operand" "rI,?n")
		  (match_operand:SI 2 "s_register_operand" "r,r")))]
  "TARGET_ARM"
  "@
   rsb%?\\t%0, %2, %1
   #"
  "TARGET_ARM
   && GET_CODE (operands[1]) == CONST_INT
   && !const_ok_for_arm (INTVAL (operands[1]))"
  [(clobber (const_int 0))]
  "
  arm_split_constant (MINUS, SImode, curr_insn,
                      INTVAL (operands[1]), operands[0], operands[2], 0);
  DONE;
  "
  [(set_attr "length" "4,16")
   (set_attr "predicable" "yes")]
)

(define_peephole2
  [(match_scratch:SI 3 "r")
   (set (match_operand:SI 0 "arm_general_register_operand" "")
	(minus:SI (match_operand:SI 1 "const_int_operand" "")
		  (match_operand:SI 2 "arm_general_register_operand" "")))]
  "TARGET_ARM
   && !const_ok_for_arm (INTVAL (operands[1]))
   && const_ok_for_arm (~INTVAL (operands[1]))"
  [(set (match_dup 3) (match_dup 1))
   (set (match_dup 0) (minus:SI (match_dup 3) (match_dup 2)))]
  ""
)

(define_insn "*subsi3_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV
	 (minus:SI (match_operand:SI 1 "arm_rhs_operand" "r,I")
		   (match_operand:SI 2 "arm_rhs_operand" "rI,r"))
	 (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=r,r")
	(minus:SI (match_dup 1) (match_dup 2)))]
  "TARGET_ARM"
  "@
   sub%?s\\t%0, %1, %2
   rsb%?s\\t%0, %2, %1"
  [(set_attr "conds" "set")]
)

(define_insn "decscc"
  [(set (match_operand:SI            0 "s_register_operand" "=r,r")
        (minus:SI (match_operand:SI  1 "s_register_operand" "0,?r")
		  (match_operator:SI 2 "arm_comparison_operator"
                   [(match_operand   3 "cc_register" "") (const_int 0)])))]
  "TARGET_ARM"
  "@
   sub%d2\\t%0, %1, #1
   mov%D2\\t%0, %1\;sub%d2\\t%0, %1, #1"
  [(set_attr "conds" "use")
   (set_attr "length" "*,8")]
)

(define_expand "subsf3"
  [(set (match_operand:SF           0 "s_register_operand" "")
	(minus:SF (match_operand:SF 1 "arm_float_rhs_operand" "")
		  (match_operand:SF 2 "arm_float_rhs_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "
  if (TARGET_MAVERICK)
    {
      if (!cirrus_fp_register (operands[1], SFmode))
        operands[1] = force_reg (SFmode, operands[1]);
      if (!cirrus_fp_register (operands[2], SFmode))
        operands[2] = force_reg (SFmode, operands[2]);
    }
")

(define_expand "subdf3"
  [(set (match_operand:DF           0 "s_register_operand" "")
	(minus:DF (match_operand:DF 1 "arm_float_rhs_operand" "")
		  (match_operand:DF 2 "arm_float_rhs_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "
  if (TARGET_MAVERICK)
    {
       if (!cirrus_fp_register (operands[1], DFmode))
         operands[1] = force_reg (DFmode, operands[1]);
       if (!cirrus_fp_register (operands[2], DFmode))
         operands[2] = force_reg (DFmode, operands[2]);
    }
")


;; Multiplication insns

(define_expand "mulsi3"
  [(set (match_operand:SI          0 "s_register_operand" "")
	(mult:SI (match_operand:SI 2 "s_register_operand" "")
		 (match_operand:SI 1 "s_register_operand" "")))]
  "TARGET_EITHER"
  ""
)

;; Use `&' and then `0' to prevent the operands 0 and 1 being the same
(define_insn "*arm_mulsi3"
  [(set (match_operand:SI          0 "s_register_operand" "=&r,&r")
	(mult:SI (match_operand:SI 2 "s_register_operand" "r,r")
		 (match_operand:SI 1 "s_register_operand" "%?r,0")))]
  "TARGET_ARM"
  "mul%?\\t%0, %2, %1"
  [(set_attr "insn" "mul")
   (set_attr "predicable" "yes")]
)

; Unfortunately with the Thumb the '&'/'0' trick can fails when operands 
; 1 and 2; are the same, because reload will make operand 0 match 
; operand 1 without realizing that this conflicts with operand 2.  We fix 
; this by adding another alternative to match this case, and then `reload' 
; it ourselves.  This alternative must come first.
(define_insn "*thumb_mulsi3"
  [(set (match_operand:SI          0 "register_operand" "=&l,&l,&l")
	(mult:SI (match_operand:SI 1 "register_operand" "%l,*h,0")
		 (match_operand:SI 2 "register_operand" "l,l,l")))]
  "TARGET_THUMB"
  "*
  if (which_alternative < 2)
    return \"mov\\t%0, %1\;mul\\t%0, %2\";
  else
    return \"mul\\t%0, %2\";
  "
  [(set_attr "length" "4,4,2")
   (set_attr "insn" "mul")]
)

(define_insn "*mulsi3_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (mult:SI
			  (match_operand:SI 2 "s_register_operand" "r,r")
			  (match_operand:SI 1 "s_register_operand" "%?r,0"))
			 (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=&r,&r")
	(mult:SI (match_dup 2) (match_dup 1)))]
  "TARGET_ARM"
  "mul%?s\\t%0, %2, %1"
  [(set_attr "conds" "set")
   (set_attr "insn" "muls")]
)

(define_insn "*mulsi_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (mult:SI
			  (match_operand:SI 2 "s_register_operand" "r,r")
			  (match_operand:SI 1 "s_register_operand" "%?r,0"))
			 (const_int 0)))
   (clobber (match_scratch:SI 0 "=&r,&r"))]
  "TARGET_ARM"
  "mul%?s\\t%0, %2, %1"
  [(set_attr "conds" "set")
   (set_attr "insn" "muls")]
)

;; Unnamed templates to match MLA instruction.

(define_insn "*mulsi3addsi"
  [(set (match_operand:SI 0 "s_register_operand" "=&r,&r,&r,&r")
	(plus:SI
	  (mult:SI (match_operand:SI 2 "s_register_operand" "r,r,r,r")
		   (match_operand:SI 1 "s_register_operand" "%r,0,r,0"))
	  (match_operand:SI 3 "s_register_operand" "?r,r,0,0")))]
  "TARGET_ARM"
  "mla%?\\t%0, %2, %1, %3"
  [(set_attr "insn" "mla")
   (set_attr "predicable" "yes")]
)

(define_insn "*mulsi3addsi_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV
	 (plus:SI (mult:SI
		   (match_operand:SI 2 "s_register_operand" "r,r,r,r")
		   (match_operand:SI 1 "s_register_operand" "%r,0,r,0"))
		  (match_operand:SI 3 "s_register_operand" "?r,r,0,0"))
	 (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=&r,&r,&r,&r")
	(plus:SI (mult:SI (match_dup 2) (match_dup 1))
		 (match_dup 3)))]
  "TARGET_ARM"
  "mla%?s\\t%0, %2, %1, %3"
  [(set_attr "conds" "set")
   (set_attr "insn" "mlas")]
)

(define_insn "*mulsi3addsi_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV
	 (plus:SI (mult:SI
		   (match_operand:SI 2 "s_register_operand" "r,r,r,r")
		   (match_operand:SI 1 "s_register_operand" "%r,0,r,0"))
		  (match_operand:SI 3 "s_register_operand" "?r,r,0,0"))
	 (const_int 0)))
   (clobber (match_scratch:SI 0 "=&r,&r,&r,&r"))]
  "TARGET_ARM"
  "mla%?s\\t%0, %2, %1, %3"
  [(set_attr "conds" "set")
   (set_attr "insn" "mlas")]
)

;; Unnamed template to match long long multiply-accumulate (smlal)

(define_insn "*mulsidi3adddi"
  [(set (match_operand:DI 0 "s_register_operand" "=&r")
	(plus:DI
	 (mult:DI
	  (sign_extend:DI (match_operand:SI 2 "s_register_operand" "%r"))
	  (sign_extend:DI (match_operand:SI 3 "s_register_operand" "r")))
	 (match_operand:DI 1 "s_register_operand" "0")))]
  "TARGET_ARM && arm_arch3m"
  "smlal%?\\t%Q0, %R0, %3, %2"
  [(set_attr "insn" "smlal")
   (set_attr "predicable" "yes")]
)

(define_insn "mulsidi3"
  [(set (match_operand:DI 0 "s_register_operand" "=&r")
	(mult:DI
	 (sign_extend:DI (match_operand:SI 1 "s_register_operand" "%r"))
	 (sign_extend:DI (match_operand:SI 2 "s_register_operand" "r"))))]
  "TARGET_ARM && arm_arch3m"
  "smull%?\\t%Q0, %R0, %1, %2"
  [(set_attr "insn" "smull")
   (set_attr "predicable" "yes")]
)

(define_insn "umulsidi3"
  [(set (match_operand:DI 0 "s_register_operand" "=&r")
	(mult:DI
	 (zero_extend:DI (match_operand:SI 1 "s_register_operand" "%r"))
	 (zero_extend:DI (match_operand:SI 2 "s_register_operand" "r"))))]
  "TARGET_ARM && arm_arch3m"
  "umull%?\\t%Q0, %R0, %1, %2"
  [(set_attr "insn" "umull")
   (set_attr "predicable" "yes")]
)

;; Unnamed template to match long long unsigned multiply-accumulate (umlal)

(define_insn "*umulsidi3adddi"
  [(set (match_operand:DI 0 "s_register_operand" "=&r")
	(plus:DI
	 (mult:DI
	  (zero_extend:DI (match_operand:SI 2 "s_register_operand" "%r"))
	  (zero_extend:DI (match_operand:SI 3 "s_register_operand" "r")))
	 (match_operand:DI 1 "s_register_operand" "0")))]
  "TARGET_ARM && arm_arch3m"
  "umlal%?\\t%Q0, %R0, %3, %2"
  [(set_attr "insn" "umlal")
   (set_attr "predicable" "yes")]
)

(define_insn "smulsi3_highpart"
  [(set (match_operand:SI 0 "s_register_operand" "=&r,&r")
	(truncate:SI
	 (lshiftrt:DI
	  (mult:DI
	   (sign_extend:DI (match_operand:SI 1 "s_register_operand" "%r,0"))
	   (sign_extend:DI (match_operand:SI 2 "s_register_operand" "r,r")))
	  (const_int 32))))
   (clobber (match_scratch:SI 3 "=&r,&r"))]
  "TARGET_ARM && arm_arch3m"
  "smull%?\\t%3, %0, %2, %1"
  [(set_attr "insn" "smull")
   (set_attr "predicable" "yes")]
)

(define_insn "umulsi3_highpart"
  [(set (match_operand:SI 0 "s_register_operand" "=&r,&r")
	(truncate:SI
	 (lshiftrt:DI
	  (mult:DI
	   (zero_extend:DI (match_operand:SI 1 "s_register_operand" "%r,0"))
	   (zero_extend:DI (match_operand:SI 2 "s_register_operand" "r,r")))
	  (const_int 32))))
   (clobber (match_scratch:SI 3 "=&r,&r"))]
  "TARGET_ARM && arm_arch3m"
  "umull%?\\t%3, %0, %2, %1"
  [(set_attr "insn" "umull")
   (set_attr "predicable" "yes")]
)

(define_insn "mulhisi3"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(mult:SI (sign_extend:SI
		  (match_operand:HI 1 "s_register_operand" "%r"))
		 (sign_extend:SI
		  (match_operand:HI 2 "s_register_operand" "r"))))]
  "TARGET_ARM && arm_arch5e"
  "smulbb%?\\t%0, %1, %2"
  [(set_attr "insn" "smulxy")
   (set_attr "predicable" "yes")]
)

(define_insn "*mulhisi3tb"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(mult:SI (ashiftrt:SI
		  (match_operand:SI 1 "s_register_operand" "r")
		  (const_int 16))
		 (sign_extend:SI
		  (match_operand:HI 2 "s_register_operand" "r"))))]
  "TARGET_ARM && arm_arch5e"
  "smultb%?\\t%0, %1, %2"
  [(set_attr "insn" "smulxy")
   (set_attr "predicable" "yes")]
)

(define_insn "*mulhisi3bt"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(mult:SI (sign_extend:SI
		  (match_operand:HI 1 "s_register_operand" "r"))
		 (ashiftrt:SI
		  (match_operand:SI 2 "s_register_operand" "r")
		  (const_int 16))))]
  "TARGET_ARM && arm_arch5e"
  "smulbt%?\\t%0, %1, %2"
  [(set_attr "insn" "smulxy")
   (set_attr "predicable" "yes")]
)

(define_insn "*mulhisi3tt"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(mult:SI (ashiftrt:SI
		  (match_operand:SI 1 "s_register_operand" "r")
		  (const_int 16))
		 (ashiftrt:SI
		  (match_operand:SI 2 "s_register_operand" "r")
		  (const_int 16))))]
  "TARGET_ARM && arm_arch5e"
  "smultt%?\\t%0, %1, %2"
  [(set_attr "insn" "smulxy")
   (set_attr "predicable" "yes")]
)

(define_insn "*mulhisi3addsi"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(plus:SI (match_operand:SI 1 "s_register_operand" "r")
		 (mult:SI (sign_extend:SI
			   (match_operand:HI 2 "s_register_operand" "%r"))
			  (sign_extend:SI
			   (match_operand:HI 3 "s_register_operand" "r")))))]
  "TARGET_ARM && arm_arch5e"
  "smlabb%?\\t%0, %2, %3, %1"
  [(set_attr "insn" "smlaxy")
   (set_attr "predicable" "yes")]
)

(define_insn "*mulhidi3adddi"
  [(set (match_operand:DI 0 "s_register_operand" "=r")
	(plus:DI
	  (match_operand:DI 1 "s_register_operand" "0")
	  (mult:DI (sign_extend:DI
	 	    (match_operand:HI 2 "s_register_operand" "%r"))
		   (sign_extend:DI
		    (match_operand:HI 3 "s_register_operand" "r")))))]
  "TARGET_ARM && arm_arch5e"
  "smlalbb%?\\t%Q0, %R0, %2, %3"
  [(set_attr "insn" "smlalxy")
   (set_attr "predicable" "yes")])

(define_expand "mulsf3"
  [(set (match_operand:SF          0 "s_register_operand" "")
	(mult:SF (match_operand:SF 1 "s_register_operand" "")
		 (match_operand:SF 2 "arm_float_rhs_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "
  if (TARGET_MAVERICK
      && !cirrus_fp_register (operands[2], SFmode))
    operands[2] = force_reg (SFmode, operands[2]);
")

(define_expand "muldf3"
  [(set (match_operand:DF          0 "s_register_operand" "")
	(mult:DF (match_operand:DF 1 "s_register_operand" "")
		 (match_operand:DF 2 "arm_float_rhs_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "
  if (TARGET_MAVERICK
      && !cirrus_fp_register (operands[2], DFmode))
    operands[2] = force_reg (DFmode, operands[2]);
")

;; Division insns

(define_expand "divsf3"
  [(set (match_operand:SF 0 "s_register_operand" "")
	(div:SF (match_operand:SF 1 "arm_float_rhs_operand" "")
		(match_operand:SF 2 "arm_float_rhs_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "")

(define_expand "divdf3"
  [(set (match_operand:DF 0 "s_register_operand" "")
	(div:DF (match_operand:DF 1 "arm_float_rhs_operand" "")
		(match_operand:DF 2 "arm_float_rhs_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "")

;; Modulo insns

(define_expand "modsf3"
  [(set (match_operand:SF 0 "s_register_operand" "")
	(mod:SF (match_operand:SF 1 "s_register_operand" "")
		(match_operand:SF 2 "arm_float_rhs_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "")

(define_expand "moddf3"
  [(set (match_operand:DF 0 "s_register_operand" "")
	(mod:DF (match_operand:DF 1 "s_register_operand" "")
		(match_operand:DF 2 "arm_float_rhs_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "")

;; Boolean and,ior,xor insns

;; Split up double word logical operations

;; Split up simple DImode logical operations.  Simply perform the logical
;; operation on the upper and lower halves of the registers.
(define_split
  [(set (match_operand:DI 0 "s_register_operand" "")
	(match_operator:DI 6 "logical_binary_operator"
	  [(match_operand:DI 1 "s_register_operand" "")
	   (match_operand:DI 2 "s_register_operand" "")]))]
  "TARGET_ARM && reload_completed && ! IS_IWMMXT_REGNUM (REGNO (operands[0]))"
  [(set (match_dup 0) (match_op_dup:SI 6 [(match_dup 1) (match_dup 2)]))
   (set (match_dup 3) (match_op_dup:SI 6 [(match_dup 4) (match_dup 5)]))]
  "
  {
    operands[3] = gen_highpart (SImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[4] = gen_highpart (SImode, operands[1]);
    operands[1] = gen_lowpart (SImode, operands[1]);
    operands[5] = gen_highpart (SImode, operands[2]);
    operands[2] = gen_lowpart (SImode, operands[2]);
  }"
)

(define_split
  [(set (match_operand:DI 0 "s_register_operand" "")
	(match_operator:DI 6 "logical_binary_operator"
	  [(sign_extend:DI (match_operand:SI 2 "s_register_operand" ""))
	   (match_operand:DI 1 "s_register_operand" "")]))]
  "TARGET_ARM && reload_completed"
  [(set (match_dup 0) (match_op_dup:SI 6 [(match_dup 1) (match_dup 2)]))
   (set (match_dup 3) (match_op_dup:SI 6
			[(ashiftrt:SI (match_dup 2) (const_int 31))
			 (match_dup 4)]))]
  "
  {
    operands[3] = gen_highpart (SImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[4] = gen_highpart (SImode, operands[1]);
    operands[1] = gen_lowpart (SImode, operands[1]);
    operands[5] = gen_highpart (SImode, operands[2]);
    operands[2] = gen_lowpart (SImode, operands[2]);
  }"
)

;; The zero extend of operand 2 means we can just copy the high part of
;; operand1 into operand0.
(define_split
  [(set (match_operand:DI 0 "s_register_operand" "")
	(ior:DI
	  (zero_extend:DI (match_operand:SI 2 "s_register_operand" ""))
	  (match_operand:DI 1 "s_register_operand" "")))]
  "TARGET_ARM && operands[0] != operands[1] && reload_completed"
  [(set (match_dup 0) (ior:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 3) (match_dup 4))]
  "
  {
    operands[4] = gen_highpart (SImode, operands[1]);
    operands[3] = gen_highpart (SImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[1] = gen_lowpart (SImode, operands[1]);
  }"
)

;; The zero extend of operand 2 means we can just copy the high part of
;; operand1 into operand0.
(define_split
  [(set (match_operand:DI 0 "s_register_operand" "")
	(xor:DI
	  (zero_extend:DI (match_operand:SI 2 "s_register_operand" ""))
	  (match_operand:DI 1 "s_register_operand" "")))]
  "TARGET_ARM && operands[0] != operands[1] && reload_completed"
  [(set (match_dup 0) (xor:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 3) (match_dup 4))]
  "
  {
    operands[4] = gen_highpart (SImode, operands[1]);
    operands[3] = gen_highpart (SImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[1] = gen_lowpart (SImode, operands[1]);
  }"
)

(define_insn "anddi3"
  [(set (match_operand:DI         0 "s_register_operand" "=&r,&r")
	(and:DI (match_operand:DI 1 "s_register_operand"  "%0,r")
		(match_operand:DI 2 "s_register_operand"   "r,r")))]
  "TARGET_ARM && ! TARGET_IWMMXT"
  "#"
  [(set_attr "length" "8")]
)

(define_insn_and_split "*anddi_zesidi_di"
  [(set (match_operand:DI 0 "s_register_operand" "=&r,&r")
	(and:DI (zero_extend:DI
		 (match_operand:SI 2 "s_register_operand" "r,r"))
		(match_operand:DI 1 "s_register_operand" "?r,0")))]
  "TARGET_ARM"
  "#"
  "TARGET_ARM && reload_completed"
  ; The zero extend of operand 2 clears the high word of the output
  ; operand.
  [(set (match_dup 0) (and:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 3) (const_int 0))]
  "
  {
    operands[3] = gen_highpart (SImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[1] = gen_lowpart (SImode, operands[1]);
  }"
  [(set_attr "length" "8")]
)

(define_insn "*anddi_sesdi_di"
  [(set (match_operand:DI          0 "s_register_operand" "=&r,&r")
	(and:DI (sign_extend:DI
		 (match_operand:SI 2 "s_register_operand" "r,r"))
		(match_operand:DI  1 "s_register_operand" "?r,0")))]
  "TARGET_ARM"
  "#"
  [(set_attr "length" "8")]
)

(define_expand "andsi3"
  [(set (match_operand:SI         0 "s_register_operand" "")
	(and:SI (match_operand:SI 1 "s_register_operand" "")
		(match_operand:SI 2 "reg_or_int_operand" "")))]
  "TARGET_EITHER"
  "
  if (TARGET_ARM)
    {
      if (GET_CODE (operands[2]) == CONST_INT)
        {
          arm_split_constant (AND, SImode, NULL_RTX,
	                      INTVAL (operands[2]), operands[0],
			      operands[1], optimize && !no_new_pseudos);

          DONE;
        }
    }
  else /* TARGET_THUMB */
    {
      if (GET_CODE (operands[2]) != CONST_INT)
        operands[2] = force_reg (SImode, operands[2]);
      else
        {
          int i;
	  
          if (((unsigned HOST_WIDE_INT) ~INTVAL (operands[2])) < 256)
  	    {
	      operands[2] = force_reg (SImode,
				       GEN_INT (~INTVAL (operands[2])));
	      
	      emit_insn (gen_bicsi3 (operands[0], operands[2], operands[1]));
	      
	      DONE;
	    }

          for (i = 9; i <= 31; i++)
	    {
	      if ((((HOST_WIDE_INT) 1) << i) - 1 == INTVAL (operands[2]))
	        {
	          emit_insn (gen_extzv (operands[0], operands[1], GEN_INT (i),
			 	        const0_rtx));
	          DONE;
	        }
	      else if ((((HOST_WIDE_INT) 1) << i) - 1
		       == ~INTVAL (operands[2]))
	        {
	          rtx shift = GEN_INT (i);
	          rtx reg = gen_reg_rtx (SImode);
		
	          emit_insn (gen_lshrsi3 (reg, operands[1], shift));
	          emit_insn (gen_ashlsi3 (operands[0], reg, shift));
		  
	          DONE;
	        }
	    }

          operands[2] = force_reg (SImode, operands[2]);
        }
    }
  "
)

(define_insn_and_split "*arm_andsi3_insn"
  [(set (match_operand:SI         0 "s_register_operand" "=r,r,r")
	(and:SI (match_operand:SI 1 "s_register_operand" "r,r,r")
		(match_operand:SI 2 "reg_or_int_operand" "rI,K,?n")))]
  "TARGET_ARM"
  "@
   and%?\\t%0, %1, %2
   bic%?\\t%0, %1, #%B2
   #"
  "TARGET_ARM
   && GET_CODE (operands[2]) == CONST_INT
   && !(const_ok_for_arm (INTVAL (operands[2]))
	|| const_ok_for_arm (~INTVAL (operands[2])))"
  [(clobber (const_int 0))]
  "
  arm_split_constant  (AND, SImode, curr_insn, 
	               INTVAL (operands[2]), operands[0], operands[1], 0);
  DONE;
  "
  [(set_attr "length" "4,4,16")
   (set_attr "predicable" "yes")]
)

(define_insn "*thumb_andsi3_insn"
  [(set (match_operand:SI         0 "register_operand" "=l")
	(and:SI (match_operand:SI 1 "register_operand" "%0")
		(match_operand:SI 2 "register_operand" "l")))]
  "TARGET_THUMB"
  "and\\t%0, %0, %2"
  [(set_attr "length" "2")]
)

(define_insn "*andsi3_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV
	 (and:SI (match_operand:SI 1 "s_register_operand" "r,r")
		 (match_operand:SI 2 "arm_not_operand" "rI,K"))
	 (const_int 0)))
   (set (match_operand:SI          0 "s_register_operand" "=r,r")
	(and:SI (match_dup 1) (match_dup 2)))]
  "TARGET_ARM"
  "@
   and%?s\\t%0, %1, %2
   bic%?s\\t%0, %1, #%B2"
  [(set_attr "conds" "set")]
)

(define_insn "*andsi3_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV
	 (and:SI (match_operand:SI 0 "s_register_operand" "r,r")
		 (match_operand:SI 1 "arm_not_operand" "rI,K"))
	 (const_int 0)))
   (clobber (match_scratch:SI 2 "=X,r"))]
  "TARGET_ARM"
  "@
   tst%?\\t%0, %1
   bic%?s\\t%2, %0, #%B1"
  [(set_attr "conds" "set")]
)

(define_insn "*zeroextractsi_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (zero_extract:SI
			  (match_operand:SI 0 "s_register_operand" "r")
		 	  (match_operand 1 "const_int_operand" "n")
			  (match_operand 2 "const_int_operand" "n"))
			 (const_int 0)))]
  "TARGET_ARM
  && (INTVAL (operands[2]) >= 0 && INTVAL (operands[2]) < 32
      && INTVAL (operands[1]) > 0 
      && INTVAL (operands[1]) + (INTVAL (operands[2]) & 1) <= 8
      && INTVAL (operands[1]) + INTVAL (operands[2]) <= 32)"
  "*
  operands[1] = GEN_INT (((1 << INTVAL (operands[1])) - 1)
			 << INTVAL (operands[2]));
  output_asm_insn (\"tst%?\\t%0, %1\", operands);
  return \"\";
  "
  [(set_attr "conds" "set")]
)

(define_insn_and_split "*ne_zeroextractsi"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(ne:SI (zero_extract:SI
		(match_operand:SI 1 "s_register_operand" "r")
		(match_operand:SI 2 "const_int_operand" "n")
		(match_operand:SI 3 "const_int_operand" "n"))
	       (const_int 0)))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM
   && (INTVAL (operands[3]) >= 0 && INTVAL (operands[3]) < 32
       && INTVAL (operands[2]) > 0 
       && INTVAL (operands[2]) + (INTVAL (operands[3]) & 1) <= 8
       && INTVAL (operands[2]) + INTVAL (operands[3]) <= 32)"
  "#"
  "TARGET_ARM
   && (INTVAL (operands[3]) >= 0 && INTVAL (operands[3]) < 32
       && INTVAL (operands[2]) > 0 
       && INTVAL (operands[2]) + (INTVAL (operands[3]) & 1) <= 8
       && INTVAL (operands[2]) + INTVAL (operands[3]) <= 32)"
  [(parallel [(set (reg:CC_NOOV CC_REGNUM)
		   (compare:CC_NOOV (and:SI (match_dup 1) (match_dup 2))
				    (const_int 0)))
	      (set (match_dup 0) (and:SI (match_dup 1) (match_dup 2)))])
   (set (match_dup 0)
	(if_then_else:SI (eq (reg:CC_NOOV CC_REGNUM) (const_int 0))
			 (match_dup 0) (const_int 1)))]
  "
  operands[2] = GEN_INT (((1 << INTVAL (operands[2])) - 1)
			 << INTVAL (operands[3])); 
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_insn_and_split "*ne_zeroextractsi_shifted"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(ne:SI (zero_extract:SI
		(match_operand:SI 1 "s_register_operand" "r")
		(match_operand:SI 2 "const_int_operand" "n")
		(const_int 0))
	       (const_int 0)))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  "TARGET_ARM"
  [(parallel [(set (reg:CC_NOOV CC_REGNUM)
		   (compare:CC_NOOV (ashift:SI (match_dup 1) (match_dup 2))
				    (const_int 0)))
	      (set (match_dup 0) (ashift:SI (match_dup 1) (match_dup 2)))])
   (set (match_dup 0)
	(if_then_else:SI (eq (reg:CC_NOOV CC_REGNUM) (const_int 0))
			 (match_dup 0) (const_int 1)))]
  "
  operands[2] = GEN_INT (32 - INTVAL (operands[2]));
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_insn_and_split "*ite_ne_zeroextractsi"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(if_then_else:SI (ne (zero_extract:SI
			      (match_operand:SI 1 "s_register_operand" "r")
			      (match_operand:SI 2 "const_int_operand" "n")
			      (match_operand:SI 3 "const_int_operand" "n"))
			     (const_int 0))
			 (match_operand:SI 4 "arm_not_operand" "rIK")
			 (const_int 0)))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM
   && (INTVAL (operands[3]) >= 0 && INTVAL (operands[3]) < 32
       && INTVAL (operands[2]) > 0 
       && INTVAL (operands[2]) + (INTVAL (operands[3]) & 1) <= 8
       && INTVAL (operands[2]) + INTVAL (operands[3]) <= 32)
   && !reg_overlap_mentioned_p (operands[0], operands[4])"
  "#"
  "TARGET_ARM
   && (INTVAL (operands[3]) >= 0 && INTVAL (operands[3]) < 32
       && INTVAL (operands[2]) > 0 
       && INTVAL (operands[2]) + (INTVAL (operands[3]) & 1) <= 8
       && INTVAL (operands[2]) + INTVAL (operands[3]) <= 32)
   && !reg_overlap_mentioned_p (operands[0], operands[4])"
  [(parallel [(set (reg:CC_NOOV CC_REGNUM)
		   (compare:CC_NOOV (and:SI (match_dup 1) (match_dup 2))
				    (const_int 0)))
	      (set (match_dup 0) (and:SI (match_dup 1) (match_dup 2)))])
   (set (match_dup 0)
	(if_then_else:SI (eq (reg:CC_NOOV CC_REGNUM) (const_int 0))
			 (match_dup 0) (match_dup 4)))]
  "
  operands[2] = GEN_INT (((1 << INTVAL (operands[2])) - 1)
			 << INTVAL (operands[3])); 
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_insn_and_split "*ite_ne_zeroextractsi_shifted"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(if_then_else:SI (ne (zero_extract:SI
			      (match_operand:SI 1 "s_register_operand" "r")
			      (match_operand:SI 2 "const_int_operand" "n")
			      (const_int 0))
			     (const_int 0))
			 (match_operand:SI 3 "arm_not_operand" "rIK")
			 (const_int 0)))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM && !reg_overlap_mentioned_p (operands[0], operands[3])"
  "#"
  "TARGET_ARM && !reg_overlap_mentioned_p (operands[0], operands[3])"
  [(parallel [(set (reg:CC_NOOV CC_REGNUM)
		   (compare:CC_NOOV (ashift:SI (match_dup 1) (match_dup 2))
				    (const_int 0)))
	      (set (match_dup 0) (ashift:SI (match_dup 1) (match_dup 2)))])
   (set (match_dup 0)
	(if_then_else:SI (eq (reg:CC_NOOV CC_REGNUM) (const_int 0))
			 (match_dup 0) (match_dup 3)))]
  "
  operands[2] = GEN_INT (32 - INTVAL (operands[2]));
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(zero_extract:SI (match_operand:SI 1 "s_register_operand" "")
			 (match_operand:SI 2 "const_int_operand" "")
			 (match_operand:SI 3 "const_int_operand" "")))
   (clobber (match_operand:SI 4 "s_register_operand" ""))]
  "TARGET_THUMB"
  [(set (match_dup 4) (ashift:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (lshiftrt:SI (match_dup 4) (match_dup 3)))]
  "{
     HOST_WIDE_INT temp = INTVAL (operands[2]);

     operands[2] = GEN_INT (32 - temp - INTVAL (operands[3]));
     operands[3] = GEN_INT (32 - temp);
   }"
)

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(match_operator:SI 1 "shiftable_operator"
	 [(zero_extract:SI (match_operand:SI 2 "s_register_operand" "")
			   (match_operand:SI 3 "const_int_operand" "")
			   (match_operand:SI 4 "const_int_operand" ""))
	  (match_operand:SI 5 "s_register_operand" "")]))
   (clobber (match_operand:SI 6 "s_register_operand" ""))]
  "TARGET_ARM"
  [(set (match_dup 6) (ashift:SI (match_dup 2) (match_dup 3)))
   (set (match_dup 0)
	(match_op_dup 1
	 [(lshiftrt:SI (match_dup 6) (match_dup 4))
	  (match_dup 5)]))]
  "{
     HOST_WIDE_INT temp = INTVAL (operands[3]);

     operands[3] = GEN_INT (32 - temp - INTVAL (operands[4]));
     operands[4] = GEN_INT (32 - temp);
   }"
)
  
(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(sign_extract:SI (match_operand:SI 1 "s_register_operand" "")
			 (match_operand:SI 2 "const_int_operand" "")
			 (match_operand:SI 3 "const_int_operand" "")))]
  "TARGET_THUMB"
  [(set (match_dup 0) (ashift:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (ashiftrt:SI (match_dup 0) (match_dup 3)))]
  "{
     HOST_WIDE_INT temp = INTVAL (operands[2]);

     operands[2] = GEN_INT (32 - temp - INTVAL (operands[3]));
     operands[3] = GEN_INT (32 - temp);
   }"
)

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(match_operator:SI 1 "shiftable_operator"
	 [(sign_extract:SI (match_operand:SI 2 "s_register_operand" "")
			   (match_operand:SI 3 "const_int_operand" "")
			   (match_operand:SI 4 "const_int_operand" ""))
	  (match_operand:SI 5 "s_register_operand" "")]))
   (clobber (match_operand:SI 6 "s_register_operand" ""))]
  "TARGET_ARM"
  [(set (match_dup 6) (ashift:SI (match_dup 2) (match_dup 3)))
   (set (match_dup 0)
	(match_op_dup 1
	 [(ashiftrt:SI (match_dup 6) (match_dup 4))
	  (match_dup 5)]))]
  "{
     HOST_WIDE_INT temp = INTVAL (operands[3]);

     operands[3] = GEN_INT (32 - temp - INTVAL (operands[4]));
     operands[4] = GEN_INT (32 - temp);
   }"
)
  
;;; ??? This pattern is bogus.  If operand3 has bits outside the range
;;; represented by the bitfield, then this will produce incorrect results.
;;; Somewhere, the value needs to be truncated.  On targets like the m68k,
;;; which have a real bit-field insert instruction, the truncation happens
;;; in the bit-field insert instruction itself.  Since arm does not have a
;;; bit-field insert instruction, we would have to emit code here to truncate
;;; the value before we insert.  This loses some of the advantage of having
;;; this insv pattern, so this pattern needs to be reevalutated.

(define_expand "insv"
  [(set (zero_extract:SI (match_operand:SI 0 "s_register_operand" "")
                         (match_operand:SI 1 "general_operand" "")
                         (match_operand:SI 2 "general_operand" ""))
        (match_operand:SI 3 "reg_or_int_operand" ""))]
  "TARGET_ARM"
  "
  {
    int start_bit = INTVAL (operands[2]);
    int width = INTVAL (operands[1]);
    HOST_WIDE_INT mask = (((HOST_WIDE_INT)1) << width) - 1;
    rtx target, subtarget;

    target = operands[0];
    /* Avoid using a subreg as a subtarget, and avoid writing a paradoxical 
       subreg as the final target.  */
    if (GET_CODE (target) == SUBREG)
      {
	subtarget = gen_reg_rtx (SImode);
	if (GET_MODE_SIZE (GET_MODE (SUBREG_REG (target)))
	    < GET_MODE_SIZE (SImode))
	  target = SUBREG_REG (target);
      }
    else
      subtarget = target;    

    if (GET_CODE (operands[3]) == CONST_INT)
      {
	/* Since we are inserting a known constant, we may be able to
	   reduce the number of bits that we have to clear so that
	   the mask becomes simple.  */
	/* ??? This code does not check to see if the new mask is actually
	   simpler.  It may not be.  */
	rtx op1 = gen_reg_rtx (SImode);
	/* ??? Truncate operand3 to fit in the bitfield.  See comment before
	   start of this pattern.  */
	HOST_WIDE_INT op3_value = mask & INTVAL (operands[3]);
	HOST_WIDE_INT mask2 = ((mask & ~op3_value) << start_bit);

	emit_insn (gen_andsi3 (op1, operands[0],
			       gen_int_mode (~mask2, SImode)));
	emit_insn (gen_iorsi3 (subtarget, op1,
			       gen_int_mode (op3_value << start_bit, SImode)));
      }
    else if (start_bit == 0
	     && !(const_ok_for_arm (mask)
		  || const_ok_for_arm (~mask)))
      {
	/* A Trick, since we are setting the bottom bits in the word,
	   we can shift operand[3] up, operand[0] down, OR them together
	   and rotate the result back again.  This takes 3 insns, and
	   the third might be mergeable into another op.  */
	/* The shift up copes with the possibility that operand[3] is
           wider than the bitfield.  */
	rtx op0 = gen_reg_rtx (SImode);
	rtx op1 = gen_reg_rtx (SImode);

	emit_insn (gen_ashlsi3 (op0, operands[3], GEN_INT (32 - width)));
	emit_insn (gen_lshrsi3 (op1, operands[0], operands[1]));
	emit_insn (gen_iorsi3  (op1, op1, op0));
	emit_insn (gen_rotlsi3 (subtarget, op1, operands[1]));
      }
    else if ((width + start_bit == 32)
	     && !(const_ok_for_arm (mask)
		  || const_ok_for_arm (~mask)))
      {
	/* Similar trick, but slightly less efficient.  */

	rtx op0 = gen_reg_rtx (SImode);
	rtx op1 = gen_reg_rtx (SImode);

	emit_insn (gen_ashlsi3 (op0, operands[3], GEN_INT (32 - width)));
	emit_insn (gen_ashlsi3 (op1, operands[0], operands[1]));
	emit_insn (gen_lshrsi3 (op1, op1, operands[1]));
	emit_insn (gen_iorsi3 (subtarget, op1, op0));
      }
    else
      {
	rtx op0 = gen_int_mode (mask, SImode);
	rtx op1 = gen_reg_rtx (SImode);
	rtx op2 = gen_reg_rtx (SImode);

	if (!(const_ok_for_arm (mask) || const_ok_for_arm (~mask)))
	  {
	    rtx tmp = gen_reg_rtx (SImode);

	    emit_insn (gen_movsi (tmp, op0));
	    op0 = tmp;
	  }

	/* Mask out any bits in operand[3] that are not needed.  */
	   emit_insn (gen_andsi3 (op1, operands[3], op0));

	if (GET_CODE (op0) == CONST_INT
	    && (const_ok_for_arm (mask << start_bit)
		|| const_ok_for_arm (~(mask << start_bit))))
	  {
	    op0 = gen_int_mode (~(mask << start_bit), SImode);
	    emit_insn (gen_andsi3 (op2, operands[0], op0));
	  }
	else
	  {
	    if (GET_CODE (op0) == CONST_INT)
	      {
		rtx tmp = gen_reg_rtx (SImode);

		emit_insn (gen_movsi (tmp, op0));
		op0 = tmp;
	      }

	    if (start_bit != 0)
	      emit_insn (gen_ashlsi3 (op0, op0, operands[2]));
	    
	    emit_insn (gen_andsi_notsi_si (op2, operands[0], op0));
	  }

	if (start_bit != 0)
          emit_insn (gen_ashlsi3 (op1, op1, operands[2]));

	emit_insn (gen_iorsi3 (subtarget, op1, op2));
      }

    if (subtarget != target)
      {
	/* If TARGET is still a SUBREG, then it must be wider than a word,
	   so we must be careful only to set the subword we were asked to.  */
	if (GET_CODE (target) == SUBREG)
	  emit_move_insn (target, subtarget);
	else
	  emit_move_insn (target, gen_lowpart (GET_MODE (target), subtarget));
      }

    DONE;
  }"
)

; constants for op 2 will never be given to these patterns.
(define_insn_and_split "*anddi_notdi_di"
  [(set (match_operand:DI 0 "s_register_operand" "=&r,&r")
	(and:DI (not:DI (match_operand:DI 1 "s_register_operand" "r,0"))
		(match_operand:DI 2 "s_register_operand" "0,r")))]
  "TARGET_ARM"
  "#"
  "TARGET_ARM && reload_completed && ! IS_IWMMXT_REGNUM (REGNO (operands[0]))"
  [(set (match_dup 0) (and:SI (not:SI (match_dup 1)) (match_dup 2)))
   (set (match_dup 3) (and:SI (not:SI (match_dup 4)) (match_dup 5)))]
  "
  {
    operands[3] = gen_highpart (SImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[4] = gen_highpart (SImode, operands[1]);
    operands[1] = gen_lowpart (SImode, operands[1]);
    operands[5] = gen_highpart (SImode, operands[2]);
    operands[2] = gen_lowpart (SImode, operands[2]);
  }"
  [(set_attr "length" "8")
   (set_attr "predicable" "yes")]
)
  
(define_insn_and_split "*anddi_notzesidi_di"
  [(set (match_operand:DI 0 "s_register_operand" "=&r,&r")
	(and:DI (not:DI (zero_extend:DI
			 (match_operand:SI 2 "s_register_operand" "r,r")))
		(match_operand:DI 1 "s_register_operand" "0,?r")))]
  "TARGET_ARM"
  "@
   bic%?\\t%Q0, %Q1, %2
   #"
  ; (not (zero_extend ...)) allows us to just copy the high word from
  ; operand1 to operand0.
  "TARGET_ARM
   && reload_completed
   && operands[0] != operands[1]"
  [(set (match_dup 0) (and:SI (not:SI (match_dup 2)) (match_dup 1)))
   (set (match_dup 3) (match_dup 4))]
  "
  {
    operands[3] = gen_highpart (SImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[4] = gen_highpart (SImode, operands[1]);
    operands[1] = gen_lowpart (SImode, operands[1]);
  }"
  [(set_attr "length" "4,8")
   (set_attr "predicable" "yes")]
)
  
(define_insn_and_split "*anddi_notsesidi_di"
  [(set (match_operand:DI 0 "s_register_operand" "=&r,&r")
	(and:DI (not:DI (sign_extend:DI
			 (match_operand:SI 2 "s_register_operand" "r,r")))
		(match_operand:DI 1 "s_register_operand" "0,r")))]
  "TARGET_ARM"
  "#"
  "TARGET_ARM && reload_completed"
  [(set (match_dup 0) (and:SI (not:SI (match_dup 2)) (match_dup 1)))
   (set (match_dup 3) (and:SI (not:SI
				(ashiftrt:SI (match_dup 2) (const_int 31)))
			       (match_dup 4)))]
  "
  {
    operands[3] = gen_highpart (SImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[4] = gen_highpart (SImode, operands[1]);
    operands[1] = gen_lowpart (SImode, operands[1]);
  }"
  [(set_attr "length" "8")
   (set_attr "predicable" "yes")]
)
  
(define_insn "andsi_notsi_si"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(and:SI (not:SI (match_operand:SI 2 "s_register_operand" "r"))
		(match_operand:SI 1 "s_register_operand" "r")))]
  "TARGET_ARM"
  "bic%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")]
)

(define_insn "bicsi3"
  [(set (match_operand:SI                 0 "register_operand" "=l")
	(and:SI (not:SI (match_operand:SI 1 "register_operand" "l"))
		(match_operand:SI         2 "register_operand" "0")))]
  "TARGET_THUMB"
  "bic\\t%0, %0, %1"
  [(set_attr "length" "2")]
)

(define_insn "andsi_not_shiftsi_si"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(and:SI (not:SI (match_operator:SI 4 "shift_operator"
			 [(match_operand:SI 2 "s_register_operand" "r")
			  (match_operand:SI 3 "arm_rhs_operand" "rM")]))
		(match_operand:SI 1 "s_register_operand" "r")))]
  "TARGET_ARM"
  "bic%?\\t%0, %1, %2%S4"
  [(set_attr "predicable" "yes")
   (set_attr "shift" "2")
   (set (attr "type") (if_then_else (match_operand 3 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*andsi_notsi_si_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV
	 (and:SI (not:SI (match_operand:SI 2 "s_register_operand" "r"))
		 (match_operand:SI 1 "s_register_operand" "r"))
	 (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=r")
	(and:SI (not:SI (match_dup 2)) (match_dup 1)))]
  "TARGET_ARM"
  "bic%?s\\t%0, %1, %2"
  [(set_attr "conds" "set")]
)

(define_insn "*andsi_notsi_si_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV
	 (and:SI (not:SI (match_operand:SI 2 "s_register_operand" "r"))
		 (match_operand:SI 1 "s_register_operand" "r"))
	 (const_int 0)))
   (clobber (match_scratch:SI 0 "=r"))]
  "TARGET_ARM"
  "bic%?s\\t%0, %1, %2"
  [(set_attr "conds" "set")]
)

(define_insn "iordi3"
  [(set (match_operand:DI         0 "s_register_operand" "=&r,&r")
	(ior:DI (match_operand:DI 1 "s_register_operand"  "%0,r")
		(match_operand:DI 2 "s_register_operand"   "r,r")))]
  "TARGET_ARM && ! TARGET_IWMMXT"
  "#"
  [(set_attr "length" "8")
   (set_attr "predicable" "yes")]
)

(define_insn "*iordi_zesidi_di"
  [(set (match_operand:DI 0 "s_register_operand" "=&r,&r")
	(ior:DI (zero_extend:DI
		 (match_operand:SI 2 "s_register_operand" "r,r"))
		(match_operand:DI 1 "s_register_operand" "0,?r")))]
  "TARGET_ARM"
  "@
   orr%?\\t%Q0, %Q1, %2
   #"
  [(set_attr "length" "4,8")
   (set_attr "predicable" "yes")]
)

(define_insn "*iordi_sesidi_di"
  [(set (match_operand:DI 0 "s_register_operand" "=&r,&r")
	(ior:DI (sign_extend:DI
		 (match_operand:SI 2 "s_register_operand" "r,r"))
		(match_operand:DI 1 "s_register_operand" "?r,0")))]
  "TARGET_ARM"
  "#"
  [(set_attr "length" "8")
   (set_attr "predicable" "yes")]
)

(define_expand "iorsi3"
  [(set (match_operand:SI         0 "s_register_operand" "")
	(ior:SI (match_operand:SI 1 "s_register_operand" "")
		(match_operand:SI 2 "reg_or_int_operand" "")))]
  "TARGET_EITHER"
  "
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      if (TARGET_ARM)
        {
          arm_split_constant (IOR, SImode, NULL_RTX,
	                      INTVAL (operands[2]), operands[0], operands[1],
			      optimize && !no_new_pseudos);
          DONE;
	}
      else /* TARGET_THUMB */
	operands [2] = force_reg (SImode, operands [2]);
    }
  "
)

(define_insn_and_split "*arm_iorsi3"
  [(set (match_operand:SI         0 "s_register_operand" "=r,r")
	(ior:SI (match_operand:SI 1 "s_register_operand" "r,r")
		(match_operand:SI 2 "reg_or_int_operand" "rI,?n")))]
  "TARGET_ARM"
  "@
   orr%?\\t%0, %1, %2
   #"
  "TARGET_ARM
   && GET_CODE (operands[2]) == CONST_INT
   && !const_ok_for_arm (INTVAL (operands[2]))"
  [(clobber (const_int 0))]
  "
  arm_split_constant (IOR, SImode, curr_insn, 
                      INTVAL (operands[2]), operands[0], operands[1], 0);
  DONE;
  "
  [(set_attr "length" "4,16")
   (set_attr "predicable" "yes")]
)

(define_insn "*thumb_iorsi3"
  [(set (match_operand:SI         0 "register_operand" "=l")
	(ior:SI (match_operand:SI 1 "register_operand" "%0")
		(match_operand:SI 2 "register_operand" "l")))]
  "TARGET_THUMB"
  "orr\\t%0, %0, %2"
  [(set_attr "length" "2")]
)

(define_peephole2
  [(match_scratch:SI 3 "r")
   (set (match_operand:SI 0 "arm_general_register_operand" "")
	(ior:SI (match_operand:SI 1 "arm_general_register_operand" "")
		(match_operand:SI 2 "const_int_operand" "")))]
  "TARGET_ARM
   && !const_ok_for_arm (INTVAL (operands[2]))
   && const_ok_for_arm (~INTVAL (operands[2]))"
  [(set (match_dup 3) (match_dup 2))
   (set (match_dup 0) (ior:SI (match_dup 1) (match_dup 3)))]
  ""
)

(define_insn "*iorsi3_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (ior:SI (match_operand:SI 1 "s_register_operand" "%r")
				 (match_operand:SI 2 "arm_rhs_operand" "rI"))
			 (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=r")
	(ior:SI (match_dup 1) (match_dup 2)))]
  "TARGET_ARM"
  "orr%?s\\t%0, %1, %2"
  [(set_attr "conds" "set")]
)

(define_insn "*iorsi3_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (ior:SI (match_operand:SI 1 "s_register_operand" "%r")
				 (match_operand:SI 2 "arm_rhs_operand" "rI"))
			 (const_int 0)))
   (clobber (match_scratch:SI 0 "=r"))]
  "TARGET_ARM"
  "orr%?s\\t%0, %1, %2"
  [(set_attr "conds" "set")]
)

(define_insn "xordi3"
  [(set (match_operand:DI         0 "s_register_operand" "=&r,&r")
	(xor:DI (match_operand:DI 1 "s_register_operand"  "%0,r")
		(match_operand:DI 2 "s_register_operand"   "r,r")))]
  "TARGET_ARM && !TARGET_IWMMXT"
  "#"
  [(set_attr "length" "8")
   (set_attr "predicable" "yes")]
)

(define_insn "*xordi_zesidi_di"
  [(set (match_operand:DI 0 "s_register_operand" "=&r,&r")
	(xor:DI (zero_extend:DI
		 (match_operand:SI 2 "s_register_operand" "r,r"))
		(match_operand:DI 1 "s_register_operand" "0,?r")))]
  "TARGET_ARM"
  "@
   eor%?\\t%Q0, %Q1, %2
   #"
  [(set_attr "length" "4,8")
   (set_attr "predicable" "yes")]
)

(define_insn "*xordi_sesidi_di"
  [(set (match_operand:DI 0 "s_register_operand" "=&r,&r")
	(xor:DI (sign_extend:DI
		 (match_operand:SI 2 "s_register_operand" "r,r"))
		(match_operand:DI 1 "s_register_operand" "?r,0")))]
  "TARGET_ARM"
  "#"
  [(set_attr "length" "8")
   (set_attr "predicable" "yes")]
)

(define_expand "xorsi3"
  [(set (match_operand:SI         0 "s_register_operand" "")
	(xor:SI (match_operand:SI 1 "s_register_operand" "")
		(match_operand:SI 2 "arm_rhs_operand"  "")))]
  "TARGET_EITHER"
  "if (TARGET_THUMB)
     if (GET_CODE (operands[2]) == CONST_INT)
       operands[2] = force_reg (SImode, operands[2]);
  "
)

(define_insn "*arm_xorsi3"
  [(set (match_operand:SI         0 "s_register_operand" "=r")
	(xor:SI (match_operand:SI 1 "s_register_operand" "r")
		(match_operand:SI 2 "arm_rhs_operand" "rI")))]
  "TARGET_ARM"
  "eor%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")]
)

(define_insn "*thumb_xorsi3"
  [(set (match_operand:SI         0 "register_operand" "=l")
	(xor:SI (match_operand:SI 1 "register_operand" "%0")
		(match_operand:SI 2 "register_operand" "l")))]
  "TARGET_THUMB"
  "eor\\t%0, %0, %2"
  [(set_attr "length" "2")]
)

(define_insn "*xorsi3_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (xor:SI (match_operand:SI 1 "s_register_operand" "r")
				 (match_operand:SI 2 "arm_rhs_operand" "rI"))
			 (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=r")
	(xor:SI (match_dup 1) (match_dup 2)))]
  "TARGET_ARM"
  "eor%?s\\t%0, %1, %2"
  [(set_attr "conds" "set")]
)

(define_insn "*xorsi3_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (xor:SI (match_operand:SI 0 "s_register_operand" "r")
				 (match_operand:SI 1 "arm_rhs_operand" "rI"))
			 (const_int 0)))]
  "TARGET_ARM"
  "teq%?\\t%0, %1"
  [(set_attr "conds" "set")]
)

; By splitting (IOR (AND (NOT A) (NOT B)) C) as D = AND (IOR A B) (NOT C), 
; (NOT D) we can sometimes merge the final NOT into one of the following
; insns.

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(ior:SI (and:SI (not:SI (match_operand:SI 1 "s_register_operand" ""))
			(not:SI (match_operand:SI 2 "arm_rhs_operand" "")))
		(match_operand:SI 3 "arm_rhs_operand" "")))
   (clobber (match_operand:SI 4 "s_register_operand" ""))]
  "TARGET_ARM"
  [(set (match_dup 4) (and:SI (ior:SI (match_dup 1) (match_dup 2))
			      (not:SI (match_dup 3))))
   (set (match_dup 0) (not:SI (match_dup 4)))]
  ""
)

(define_insn "*andsi_iorsi3_notsi"
  [(set (match_operand:SI 0 "s_register_operand" "=&r,&r,&r")
	(and:SI (ior:SI (match_operand:SI 1 "s_register_operand" "r,r,0")
			(match_operand:SI 2 "arm_rhs_operand" "rI,0,rI"))
		(not:SI (match_operand:SI 3 "arm_rhs_operand" "rI,rI,rI"))))]
  "TARGET_ARM"
  "orr%?\\t%0, %1, %2\;bic%?\\t%0, %0, %3"
  [(set_attr "length" "8")
   (set_attr "predicable" "yes")]
)

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(match_operator:SI 1 "logical_binary_operator"
	 [(zero_extract:SI (match_operand:SI 2 "s_register_operand" "")
			   (match_operand:SI 3 "const_int_operand" "")
			   (match_operand:SI 4 "const_int_operand" ""))
	  (match_operator:SI 9 "logical_binary_operator"
	   [(lshiftrt:SI (match_operand:SI 5 "s_register_operand" "")
			 (match_operand:SI 6 "const_int_operand" ""))
	    (match_operand:SI 7 "s_register_operand" "")])]))
   (clobber (match_operand:SI 8 "s_register_operand" ""))]
  "TARGET_ARM
   && GET_CODE (operands[1]) == GET_CODE (operands[9])
   && INTVAL (operands[3]) == 32 - INTVAL (operands[6])"
  [(set (match_dup 8)
	(match_op_dup 1
	 [(ashift:SI (match_dup 2) (match_dup 4))
	  (match_dup 5)]))
   (set (match_dup 0)
	(match_op_dup 1
	 [(lshiftrt:SI (match_dup 8) (match_dup 6))
	  (match_dup 7)]))]
  "
  operands[4] = GEN_INT (32 - (INTVAL (operands[3]) + INTVAL (operands[4])));
")

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(match_operator:SI 1 "logical_binary_operator"
	 [(match_operator:SI 9 "logical_binary_operator"
	   [(lshiftrt:SI (match_operand:SI 5 "s_register_operand" "")
			 (match_operand:SI 6 "const_int_operand" ""))
	    (match_operand:SI 7 "s_register_operand" "")])
	  (zero_extract:SI (match_operand:SI 2 "s_register_operand" "")
			   (match_operand:SI 3 "const_int_operand" "")
			   (match_operand:SI 4 "const_int_operand" ""))]))
   (clobber (match_operand:SI 8 "s_register_operand" ""))]
  "TARGET_ARM
   && GET_CODE (operands[1]) == GET_CODE (operands[9])
   && INTVAL (operands[3]) == 32 - INTVAL (operands[6])"
  [(set (match_dup 8)
	(match_op_dup 1
	 [(ashift:SI (match_dup 2) (match_dup 4))
	  (match_dup 5)]))
   (set (match_dup 0)
	(match_op_dup 1
	 [(lshiftrt:SI (match_dup 8) (match_dup 6))
	  (match_dup 7)]))]
  "
  operands[4] = GEN_INT (32 - (INTVAL (operands[3]) + INTVAL (operands[4])));
")

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(match_operator:SI 1 "logical_binary_operator"
	 [(sign_extract:SI (match_operand:SI 2 "s_register_operand" "")
			   (match_operand:SI 3 "const_int_operand" "")
			   (match_operand:SI 4 "const_int_operand" ""))
	  (match_operator:SI 9 "logical_binary_operator"
	   [(ashiftrt:SI (match_operand:SI 5 "s_register_operand" "")
			 (match_operand:SI 6 "const_int_operand" ""))
	    (match_operand:SI 7 "s_register_operand" "")])]))
   (clobber (match_operand:SI 8 "s_register_operand" ""))]
  "TARGET_ARM
   && GET_CODE (operands[1]) == GET_CODE (operands[9])
   && INTVAL (operands[3]) == 32 - INTVAL (operands[6])"
  [(set (match_dup 8)
	(match_op_dup 1
	 [(ashift:SI (match_dup 2) (match_dup 4))
	  (match_dup 5)]))
   (set (match_dup 0)
	(match_op_dup 1
	 [(ashiftrt:SI (match_dup 8) (match_dup 6))
	  (match_dup 7)]))]
  "
  operands[4] = GEN_INT (32 - (INTVAL (operands[3]) + INTVAL (operands[4])));
")

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(match_operator:SI 1 "logical_binary_operator"
	 [(match_operator:SI 9 "logical_binary_operator"
	   [(ashiftrt:SI (match_operand:SI 5 "s_register_operand" "")
			 (match_operand:SI 6 "const_int_operand" ""))
	    (match_operand:SI 7 "s_register_operand" "")])
	  (sign_extract:SI (match_operand:SI 2 "s_register_operand" "")
			   (match_operand:SI 3 "const_int_operand" "")
			   (match_operand:SI 4 "const_int_operand" ""))]))
   (clobber (match_operand:SI 8 "s_register_operand" ""))]
  "TARGET_ARM
   && GET_CODE (operands[1]) == GET_CODE (operands[9])
   && INTVAL (operands[3]) == 32 - INTVAL (operands[6])"
  [(set (match_dup 8)
	(match_op_dup 1
	 [(ashift:SI (match_dup 2) (match_dup 4))
	  (match_dup 5)]))
   (set (match_dup 0)
	(match_op_dup 1
	 [(ashiftrt:SI (match_dup 8) (match_dup 6))
	  (match_dup 7)]))]
  "
  operands[4] = GEN_INT (32 - (INTVAL (operands[3]) + INTVAL (operands[4])));
")


;; Minimum and maximum insns

(define_expand "smaxsi3"
  [(parallel [
    (set (match_operand:SI 0 "s_register_operand" "")
	 (smax:SI (match_operand:SI 1 "s_register_operand" "")
		  (match_operand:SI 2 "arm_rhs_operand" "")))
    (clobber (reg:CC CC_REGNUM))])]
  "TARGET_ARM"
  "
  if (operands[2] == const0_rtx || operands[2] == constm1_rtx)
    {
      /* No need for a clobber of the condition code register here.  */
      emit_insn (gen_rtx_SET (VOIDmode, operands[0],
			      gen_rtx_SMAX (SImode, operands[1],
					    operands[2])));
      DONE;
    }
")

(define_insn "*smax_0"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(smax:SI (match_operand:SI 1 "s_register_operand" "r")
		 (const_int 0)))]
  "TARGET_ARM"
  "bic%?\\t%0, %1, %1, asr #31"
  [(set_attr "predicable" "yes")]
)

(define_insn "*smax_m1"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(smax:SI (match_operand:SI 1 "s_register_operand" "r")
		 (const_int -1)))]
  "TARGET_ARM"
  "orr%?\\t%0, %1, %1, asr #31"
  [(set_attr "predicable" "yes")]
)

(define_insn "*smax_insn"
  [(set (match_operand:SI          0 "s_register_operand" "=r,r")
	(smax:SI (match_operand:SI 1 "s_register_operand"  "%0,?r")
		 (match_operand:SI 2 "arm_rhs_operand"    "rI,rI")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "@
   cmp\\t%1, %2\;movlt\\t%0, %2
   cmp\\t%1, %2\;movge\\t%0, %1\;movlt\\t%0, %2"
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_expand "sminsi3"
  [(parallel [
    (set (match_operand:SI 0 "s_register_operand" "")
	 (smin:SI (match_operand:SI 1 "s_register_operand" "")
		  (match_operand:SI 2 "arm_rhs_operand" "")))
    (clobber (reg:CC CC_REGNUM))])]
  "TARGET_ARM"
  "
  if (operands[2] == const0_rtx)
    {
      /* No need for a clobber of the condition code register here.  */
      emit_insn (gen_rtx_SET (VOIDmode, operands[0],
			      gen_rtx_SMIN (SImode, operands[1],
					    operands[2])));
      DONE;
    }
")

(define_insn "*smin_0"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(smin:SI (match_operand:SI 1 "s_register_operand" "r")
		 (const_int 0)))]
  "TARGET_ARM"
  "and%?\\t%0, %1, %1, asr #31"
  [(set_attr "predicable" "yes")]
)

(define_insn "*smin_insn"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(smin:SI (match_operand:SI 1 "s_register_operand" "%0,?r")
		 (match_operand:SI 2 "arm_rhs_operand" "rI,rI")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "@
   cmp\\t%1, %2\;movge\\t%0, %2
   cmp\\t%1, %2\;movlt\\t%0, %1\;movge\\t%0, %2"
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_insn "umaxsi3"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r")
	(umax:SI (match_operand:SI 1 "s_register_operand" "0,r,?r")
		 (match_operand:SI 2 "arm_rhs_operand" "rI,0,rI")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "@
   cmp\\t%1, %2\;movcc\\t%0, %2
   cmp\\t%1, %2\;movcs\\t%0, %1
   cmp\\t%1, %2\;movcs\\t%0, %1\;movcc\\t%0, %2"
  [(set_attr "conds" "clob")
   (set_attr "length" "8,8,12")]
)

(define_insn "uminsi3"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r")
	(umin:SI (match_operand:SI 1 "s_register_operand" "0,r,?r")
		 (match_operand:SI 2 "arm_rhs_operand" "rI,0,rI")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "@
   cmp\\t%1, %2\;movcs\\t%0, %2
   cmp\\t%1, %2\;movcc\\t%0, %1
   cmp\\t%1, %2\;movcc\\t%0, %1\;movcs\\t%0, %2"
  [(set_attr "conds" "clob")
   (set_attr "length" "8,8,12")]
)

(define_insn "*store_minmaxsi"
  [(set (match_operand:SI 0 "memory_operand" "=m")
	(match_operator:SI 3 "minmax_operator"
	 [(match_operand:SI 1 "s_register_operand" "r")
	  (match_operand:SI 2 "s_register_operand" "r")]))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "*
  operands[3] = gen_rtx_fmt_ee (minmax_code (operands[3]), SImode,
				operands[1], operands[2]);
  output_asm_insn (\"cmp\\t%1, %2\", operands);
  output_asm_insn (\"str%d3\\t%1, %0\", operands);
  output_asm_insn (\"str%D3\\t%2, %0\", operands);
  return \"\";
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "12")
   (set_attr "type" "store1")]
)

; Reject the frame pointer in operand[1], since reloading this after
; it has been eliminated can cause carnage.
(define_insn "*minmax_arithsi"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(match_operator:SI 4 "shiftable_operator"
	 [(match_operator:SI 5 "minmax_operator"
	   [(match_operand:SI 2 "s_register_operand" "r,r")
	    (match_operand:SI 3 "arm_rhs_operand" "rI,rI")])
	  (match_operand:SI 1 "s_register_operand" "0,?r")]))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM && !arm_eliminable_register (operands[1])"
  "*
  {
    enum rtx_code code = GET_CODE (operands[4]);

    operands[5] = gen_rtx_fmt_ee (minmax_code (operands[5]), SImode,
				  operands[2], operands[3]);
    output_asm_insn (\"cmp\\t%2, %3\", operands);
    output_asm_insn (\"%i4%d5\\t%0, %1, %2\", operands);
    if (which_alternative != 0 || operands[3] != const0_rtx
        || (code != PLUS && code != MINUS && code != IOR && code != XOR))
      output_asm_insn (\"%i4%D5\\t%0, %1, %3\", operands);
    return \"\";
  }"
  [(set_attr "conds" "clob")
   (set_attr "length" "12")]
)


;; Shift and rotation insns

(define_expand "ashldi3"
  [(set (match_operand:DI            0 "s_register_operand" "")
        (ashift:DI (match_operand:DI 1 "s_register_operand" "")
                   (match_operand:SI 2 "reg_or_int_operand" "")))]
  "TARGET_ARM"
  "
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      if ((HOST_WIDE_INT) INTVAL (operands[2]) == 1)
        {
          emit_insn (gen_arm_ashldi3_1bit (operands[0], operands[1]));
          DONE;
        }
        /* Ideally we shouldn't fail here if we could know that operands[1] 
           ends up already living in an iwmmxt register. Otherwise it's
           cheaper to have the alternate code being generated than moving
           values to iwmmxt regs and back.  */
        FAIL;
    }
  else if (!TARGET_REALLY_IWMMXT && !(TARGET_HARD_FLOAT && TARGET_MAVERICK))
    FAIL;
  "
)

(define_insn "arm_ashldi3_1bit"
  [(set (match_operand:DI            0 "s_register_operand" "=&r,r")
        (ashift:DI (match_operand:DI 1 "s_register_operand" "?r,0")
                   (const_int 1)))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "movs\\t%Q0, %Q1, asl #1\;adc\\t%R0, %R1, %R1"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_expand "ashlsi3"
  [(set (match_operand:SI            0 "s_register_operand" "")
	(ashift:SI (match_operand:SI 1 "s_register_operand" "")
		   (match_operand:SI 2 "arm_rhs_operand" "")))]
  "TARGET_EITHER"
  "
  if (GET_CODE (operands[2]) == CONST_INT
      && ((unsigned HOST_WIDE_INT) INTVAL (operands[2])) > 31)
    {
      emit_insn (gen_movsi (operands[0], const0_rtx));
      DONE;
    }
  "
)

(define_insn "*thumb_ashlsi3"
  [(set (match_operand:SI            0 "register_operand" "=l,l")
	(ashift:SI (match_operand:SI 1 "register_operand" "l,0")
		   (match_operand:SI 2 "nonmemory_operand" "N,l")))]
  "TARGET_THUMB"
  "lsl\\t%0, %1, %2"
  [(set_attr "length" "2")]
)

(define_expand "ashrdi3"
  [(set (match_operand:DI              0 "s_register_operand" "")
        (ashiftrt:DI (match_operand:DI 1 "s_register_operand" "")
                     (match_operand:SI 2 "reg_or_int_operand" "")))]
  "TARGET_ARM"
  "
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      if ((HOST_WIDE_INT) INTVAL (operands[2]) == 1)
        {
          emit_insn (gen_arm_ashrdi3_1bit (operands[0], operands[1]));
          DONE;
        }
        /* Ideally we shouldn't fail here if we could know that operands[1] 
           ends up already living in an iwmmxt register. Otherwise it's
           cheaper to have the alternate code being generated than moving
           values to iwmmxt regs and back.  */
        FAIL;
    }
  else if (!TARGET_REALLY_IWMMXT)
    FAIL;
  "
)

(define_insn "arm_ashrdi3_1bit"
  [(set (match_operand:DI              0 "s_register_operand" "=&r,r")
        (ashiftrt:DI (match_operand:DI 1 "s_register_operand" "?r,0")
                     (const_int 1)))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "movs\\t%R0, %R1, asr #1\;mov\\t%Q0, %Q1, rrx"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_expand "ashrsi3"
  [(set (match_operand:SI              0 "s_register_operand" "")
	(ashiftrt:SI (match_operand:SI 1 "s_register_operand" "")
		     (match_operand:SI 2 "arm_rhs_operand" "")))]
  "TARGET_EITHER"
  "
  if (GET_CODE (operands[2]) == CONST_INT
      && ((unsigned HOST_WIDE_INT) INTVAL (operands[2])) > 31)
    operands[2] = GEN_INT (31);
  "
)

(define_insn "*thumb_ashrsi3"
  [(set (match_operand:SI              0 "register_operand" "=l,l")
	(ashiftrt:SI (match_operand:SI 1 "register_operand" "l,0")
		     (match_operand:SI 2 "nonmemory_operand" "N,l")))]
  "TARGET_THUMB"
  "asr\\t%0, %1, %2"
  [(set_attr "length" "2")]
)

(define_expand "lshrdi3"
  [(set (match_operand:DI              0 "s_register_operand" "")
        (lshiftrt:DI (match_operand:DI 1 "s_register_operand" "")
                     (match_operand:SI 2 "reg_or_int_operand" "")))]
  "TARGET_ARM"
  "
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      if ((HOST_WIDE_INT) INTVAL (operands[2]) == 1)
        {
          emit_insn (gen_arm_lshrdi3_1bit (operands[0], operands[1]));
          DONE;
        }
        /* Ideally we shouldn't fail here if we could know that operands[1] 
           ends up already living in an iwmmxt register. Otherwise it's
           cheaper to have the alternate code being generated than moving
           values to iwmmxt regs and back.  */
        FAIL;
    }
  else if (!TARGET_REALLY_IWMMXT)
    FAIL;
  "
)

(define_insn "arm_lshrdi3_1bit"
  [(set (match_operand:DI              0 "s_register_operand" "=&r,r")
        (lshiftrt:DI (match_operand:DI 1 "s_register_operand" "?r,0")
                     (const_int 1)))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "movs\\t%R0, %R1, lsr #1\;mov\\t%Q0, %Q1, rrx"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_expand "lshrsi3"
  [(set (match_operand:SI              0 "s_register_operand" "")
	(lshiftrt:SI (match_operand:SI 1 "s_register_operand" "")
		     (match_operand:SI 2 "arm_rhs_operand" "")))]
  "TARGET_EITHER"
  "
  if (GET_CODE (operands[2]) == CONST_INT
      && ((unsigned HOST_WIDE_INT) INTVAL (operands[2])) > 31)
    {
      emit_insn (gen_movsi (operands[0], const0_rtx));
      DONE;
    }
  "
)

(define_insn "*thumb_lshrsi3"
  [(set (match_operand:SI              0 "register_operand" "=l,l")
	(lshiftrt:SI (match_operand:SI 1 "register_operand" "l,0")
		     (match_operand:SI 2 "nonmemory_operand" "N,l")))]
  "TARGET_THUMB"
  "lsr\\t%0, %1, %2"
  [(set_attr "length" "2")]
)

(define_expand "rotlsi3"
  [(set (match_operand:SI              0 "s_register_operand" "")
	(rotatert:SI (match_operand:SI 1 "s_register_operand" "")
		     (match_operand:SI 2 "reg_or_int_operand" "")))]
  "TARGET_ARM"
  "
  if (GET_CODE (operands[2]) == CONST_INT)
    operands[2] = GEN_INT ((32 - INTVAL (operands[2])) % 32);
  else
    {
      rtx reg = gen_reg_rtx (SImode);
      emit_insn (gen_subsi3 (reg, GEN_INT (32), operands[2]));
      operands[2] = reg;
    }
  "
)

(define_expand "rotrsi3"
  [(set (match_operand:SI              0 "s_register_operand" "")
	(rotatert:SI (match_operand:SI 1 "s_register_operand" "")
		     (match_operand:SI 2 "arm_rhs_operand" "")))]
  "TARGET_EITHER"
  "
  if (TARGET_ARM)
    {
      if (GET_CODE (operands[2]) == CONST_INT
          && ((unsigned HOST_WIDE_INT) INTVAL (operands[2])) > 31)
        operands[2] = GEN_INT (INTVAL (operands[2]) % 32);
    }
  else /* TARGET_THUMB */
    {
      if (GET_CODE (operands [2]) == CONST_INT)
        operands [2] = force_reg (SImode, operands[2]);
    }
  "
)

(define_insn "*thumb_rotrsi3"
  [(set (match_operand:SI              0 "register_operand" "=l")
	(rotatert:SI (match_operand:SI 1 "register_operand" "0")
		     (match_operand:SI 2 "register_operand" "l")))]
  "TARGET_THUMB"
  "ror\\t%0, %0, %2"
  [(set_attr "length" "2")]
)

(define_insn "*arm_shiftsi3"
  [(set (match_operand:SI   0 "s_register_operand" "=r")
	(match_operator:SI  3 "shift_operator"
	 [(match_operand:SI 1 "s_register_operand"  "r")
	  (match_operand:SI 2 "reg_or_int_operand" "rM")]))]
  "TARGET_ARM"
  "mov%?\\t%0, %1%S3"
  [(set_attr "predicable" "yes")
   (set_attr "shift" "1")
   (set (attr "type") (if_then_else (match_operand 2 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*shiftsi3_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (match_operator:SI 3 "shift_operator"
			  [(match_operand:SI 1 "s_register_operand" "r")
			   (match_operand:SI 2 "arm_rhs_operand" "rM")])
			 (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=r")
	(match_op_dup 3 [(match_dup 1) (match_dup 2)]))]
  "TARGET_ARM"
  "mov%?s\\t%0, %1%S3"
  [(set_attr "conds" "set")
   (set_attr "shift" "1")
   (set (attr "type") (if_then_else (match_operand 2 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*shiftsi3_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (match_operator:SI 3 "shift_operator"
			  [(match_operand:SI 1 "s_register_operand" "r")
			   (match_operand:SI 2 "arm_rhs_operand" "rM")])
			 (const_int 0)))
   (clobber (match_scratch:SI 0 "=r"))]
  "TARGET_ARM"
  "mov%?s\\t%0, %1%S3"
  [(set_attr "conds" "set")
   (set_attr "shift" "1")]
)

(define_insn "*notsi_shiftsi"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(not:SI (match_operator:SI 3 "shift_operator"
		 [(match_operand:SI 1 "s_register_operand" "r")
		  (match_operand:SI 2 "arm_rhs_operand" "rM")])))]
  "TARGET_ARM"
  "mvn%?\\t%0, %1%S3"
  [(set_attr "predicable" "yes")
   (set_attr "shift" "1")
   (set (attr "type") (if_then_else (match_operand 2 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*notsi_shiftsi_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (not:SI (match_operator:SI 3 "shift_operator"
			  [(match_operand:SI 1 "s_register_operand" "r")
			   (match_operand:SI 2 "arm_rhs_operand" "rM")]))
			 (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=r")
	(not:SI (match_op_dup 3 [(match_dup 1) (match_dup 2)])))]
  "TARGET_ARM"
  "mvn%?s\\t%0, %1%S3"
  [(set_attr "conds" "set")
   (set_attr "shift" "1")
   (set (attr "type") (if_then_else (match_operand 2 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*not_shiftsi_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (not:SI (match_operator:SI 3 "shift_operator"
			  [(match_operand:SI 1 "s_register_operand" "r")
			   (match_operand:SI 2 "arm_rhs_operand" "rM")]))
			 (const_int 0)))
   (clobber (match_scratch:SI 0 "=r"))]
  "TARGET_ARM"
  "mvn%?s\\t%0, %1%S3"
  [(set_attr "conds" "set")
   (set_attr "shift" "1")
   (set (attr "type") (if_then_else (match_operand 2 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

;; We don't really have extzv, but defining this using shifts helps
;; to reduce register pressure later on.

(define_expand "extzv"
  [(set (match_dup 4)
	(ashift:SI (match_operand:SI   1 "register_operand" "")
		   (match_operand:SI   2 "const_int_operand" "")))
   (set (match_operand:SI              0 "register_operand" "")
	(lshiftrt:SI (match_dup 4)
		     (match_operand:SI 3 "const_int_operand" "")))]
  "TARGET_THUMB"
  "
  {
    HOST_WIDE_INT lshift = 32 - INTVAL (operands[2]) - INTVAL (operands[3]);
    HOST_WIDE_INT rshift = 32 - INTVAL (operands[2]);
    
    operands[3] = GEN_INT (rshift);
    
    if (lshift == 0)
      {
        emit_insn (gen_lshrsi3 (operands[0], operands[1], operands[3]));
        DONE;
      }
      
    operands[2] = GEN_INT (lshift);
    operands[4] = gen_reg_rtx (SImode);
  }"
)


;; Unary arithmetic insns

(define_expand "negdi2"
 [(parallel
   [(set (match_operand:DI          0 "s_register_operand" "")
	  (neg:DI (match_operand:DI 1 "s_register_operand" "")))
    (clobber (reg:CC CC_REGNUM))])]
  "TARGET_EITHER"
  "
  if (TARGET_THUMB)
    {
      if (GET_CODE (operands[1]) != REG)
        operands[1] = force_reg (SImode, operands[1]);
     }
  "
)

;; The constraints here are to prevent a *partial* overlap (where %Q0 == %R1).
;; The second alternative is to allow the common case of a *full* overlap.
(define_insn "*arm_negdi2"
  [(set (match_operand:DI         0 "s_register_operand" "=&r,r")
	(neg:DI (match_operand:DI 1 "s_register_operand"  "?r,0")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "rsbs\\t%Q0, %Q1, #0\;rsc\\t%R0, %R1, #0"
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_insn "*thumb_negdi2"
  [(set (match_operand:DI         0 "register_operand" "=&l")
	(neg:DI (match_operand:DI 1 "register_operand"   "l")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_THUMB"
  "mov\\t%R0, #0\;neg\\t%Q0, %Q1\;sbc\\t%R0, %R1"
  [(set_attr "length" "6")]
)

(define_expand "negsi2"
  [(set (match_operand:SI         0 "s_register_operand" "")
	(neg:SI (match_operand:SI 1 "s_register_operand" "")))]
  "TARGET_EITHER"
  ""
)

(define_insn "*arm_negsi2"
  [(set (match_operand:SI         0 "s_register_operand" "=r")
	(neg:SI (match_operand:SI 1 "s_register_operand" "r")))]
  "TARGET_ARM"
  "rsb%?\\t%0, %1, #0"
  [(set_attr "predicable" "yes")]
)

(define_insn "*thumb_negsi2"
  [(set (match_operand:SI         0 "register_operand" "=l")
	(neg:SI (match_operand:SI 1 "register_operand" "l")))]
  "TARGET_THUMB"
  "neg\\t%0, %1"
  [(set_attr "length" "2")]
)

(define_expand "negsf2"
  [(set (match_operand:SF         0 "s_register_operand" "")
	(neg:SF (match_operand:SF 1 "s_register_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  ""
)

(define_expand "negdf2"
  [(set (match_operand:DF         0 "s_register_operand" "")
	(neg:DF (match_operand:DF 1 "s_register_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "")

;; abssi2 doesn't really clobber the condition codes if a different register
;; is being set.  To keep things simple, assume during rtl manipulations that
;; it does, but tell the final scan operator the truth.  Similarly for
;; (neg (abs...))

(define_expand "abssi2"
  [(parallel
    [(set (match_operand:SI         0 "s_register_operand" "")
	  (abs:SI (match_operand:SI 1 "s_register_operand" "")))
     (clobber (reg:CC CC_REGNUM))])]
  "TARGET_ARM"
  "")

(define_insn "*arm_abssi2"
  [(set (match_operand:SI         0 "s_register_operand" "=r,&r")
	(abs:SI (match_operand:SI 1 "s_register_operand" "0,r")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "@
   cmp\\t%0, #0\;rsblt\\t%0, %0, #0
   eor%?\\t%0, %1, %1, asr #31\;sub%?\\t%0, %0, %1, asr #31"
  [(set_attr "conds" "clob,*")
   (set_attr "shift" "1")
   ;; predicable can't be set based on the variant, so left as no
   (set_attr "length" "8")]
)

(define_insn "*neg_abssi2"
  [(set (match_operand:SI 0 "s_register_operand" "=r,&r")
	(neg:SI (abs:SI (match_operand:SI 1 "s_register_operand" "0,r"))))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "@
   cmp\\t%0, #0\;rsbgt\\t%0, %0, #0
   eor%?\\t%0, %1, %1, asr #31\;rsb%?\\t%0, %0, %1, asr #31"
  [(set_attr "conds" "clob,*")
   (set_attr "shift" "1")
   ;; predicable can't be set based on the variant, so left as no
   (set_attr "length" "8")]
)

(define_expand "abssf2"
  [(set (match_operand:SF         0 "s_register_operand" "")
	(abs:SF (match_operand:SF 1 "s_register_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "")

(define_expand "absdf2"
  [(set (match_operand:DF         0 "s_register_operand" "")
	(abs:DF (match_operand:DF 1 "s_register_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "")

(define_expand "sqrtsf2"
  [(set (match_operand:SF 0 "s_register_operand" "")
	(sqrt:SF (match_operand:SF 1 "s_register_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "")

(define_expand "sqrtdf2"
  [(set (match_operand:DF 0 "s_register_operand" "")
	(sqrt:DF (match_operand:DF 1 "s_register_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "")

(define_insn_and_split "one_cmpldi2"
  [(set (match_operand:DI 0 "s_register_operand" "=&r,&r")
	(not:DI (match_operand:DI 1 "s_register_operand" "?r,0")))]
  "TARGET_ARM"
  "#"
  "TARGET_ARM && reload_completed"
  [(set (match_dup 0) (not:SI (match_dup 1)))
   (set (match_dup 2) (not:SI (match_dup 3)))]
  "
  {
    operands[2] = gen_highpart (SImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[3] = gen_highpart (SImode, operands[1]);
    operands[1] = gen_lowpart (SImode, operands[1]);
  }"
  [(set_attr "length" "8")
   (set_attr "predicable" "yes")]
)

(define_expand "one_cmplsi2"
  [(set (match_operand:SI         0 "s_register_operand" "")
	(not:SI (match_operand:SI 1 "s_register_operand" "")))]
  "TARGET_EITHER"
  ""
)

(define_insn "*arm_one_cmplsi2"
  [(set (match_operand:SI         0 "s_register_operand" "=r")
	(not:SI (match_operand:SI 1 "s_register_operand"  "r")))]
  "TARGET_ARM"
  "mvn%?\\t%0, %1"
  [(set_attr "predicable" "yes")]
)

(define_insn "*thumb_one_cmplsi2"
  [(set (match_operand:SI         0 "register_operand" "=l")
	(not:SI (match_operand:SI 1 "register_operand"  "l")))]
  "TARGET_THUMB"
  "mvn\\t%0, %1"
  [(set_attr "length" "2")]
)

(define_insn "*notsi_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (not:SI (match_operand:SI 1 "s_register_operand" "r"))
			 (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=r")
	(not:SI (match_dup 1)))]
  "TARGET_ARM"
  "mvn%?s\\t%0, %1"
  [(set_attr "conds" "set")]
)

(define_insn "*notsi_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (not:SI (match_operand:SI 1 "s_register_operand" "r"))
			 (const_int 0)))
   (clobber (match_scratch:SI 0 "=r"))]
  "TARGET_ARM"
  "mvn%?s\\t%0, %1"
  [(set_attr "conds" "set")]
)

;; Fixed <--> Floating conversion insns

(define_expand "floatsisf2"
  [(set (match_operand:SF           0 "s_register_operand" "")
	(float:SF (match_operand:SI 1 "s_register_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "
  if (TARGET_MAVERICK)
    {
      emit_insn (gen_cirrus_floatsisf2 (operands[0], operands[1]));
      DONE;
    }
")

(define_expand "floatsidf2"
  [(set (match_operand:DF           0 "s_register_operand" "")
	(float:DF (match_operand:SI 1 "s_register_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "
  if (TARGET_MAVERICK)
    {
      emit_insn (gen_cirrus_floatsidf2 (operands[0], operands[1]));
      DONE;
    }
")

(define_expand "fix_truncsfsi2"
  [(set (match_operand:SI         0 "s_register_operand" "")
	(fix:SI (fix:SF (match_operand:SF 1 "s_register_operand"  ""))))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "
  if (TARGET_MAVERICK)
    {
      if (!cirrus_fp_register (operands[0], SImode))
        operands[0] = force_reg (SImode, operands[0]);
      if (!cirrus_fp_register (operands[1], SFmode))
        operands[1] = force_reg (SFmode, operands[0]);
      emit_insn (gen_cirrus_truncsfsi2 (operands[0], operands[1]));
      DONE;
    }
")

(define_expand "fix_truncdfsi2"
  [(set (match_operand:SI         0 "s_register_operand" "")
	(fix:SI (fix:DF (match_operand:DF 1 "s_register_operand"  ""))))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "
  if (TARGET_MAVERICK)
    {
      if (!cirrus_fp_register (operands[1], DFmode))
        operands[1] = force_reg (DFmode, operands[0]);
      emit_insn (gen_cirrus_truncdfsi2 (operands[0], operands[1]));
      DONE;
    }
")

;; Truncation insns

(define_expand "truncdfsf2"
  [(set (match_operand:SF  0 "s_register_operand" "")
	(float_truncate:SF
 	 (match_operand:DF 1 "s_register_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  ""
)

;; Zero and sign extension instructions.

(define_insn "zero_extendsidi2"
  [(set (match_operand:DI 0 "s_register_operand" "=r")
        (zero_extend:DI (match_operand:SI 1 "s_register_operand" "r")))]
  "TARGET_ARM"
  "*
    if (REGNO (operands[1])
        != REGNO (operands[0]) + (WORDS_BIG_ENDIAN ? 1 : 0))
      output_asm_insn (\"mov%?\\t%Q0, %1\", operands);
    return \"mov%?\\t%R0, #0\";
  "
  [(set_attr "length" "8")
   (set_attr "predicable" "yes")]
)

(define_insn "zero_extendqidi2"
  [(set (match_operand:DI                 0 "s_register_operand"  "=r,r")
	(zero_extend:DI (match_operand:QI 1 "nonimmediate_operand" "r,m")))]
  "TARGET_ARM"
  "@
   and%?\\t%Q0, %1, #255\;mov%?\\t%R0, #0
   ldr%?b\\t%Q0, %1\;mov%?\\t%R0, #0"
  [(set_attr "length" "8")
   (set_attr "predicable" "yes")
   (set_attr "type" "*,load_byte")
   (set_attr "pool_range" "*,4092")
   (set_attr "neg_pool_range" "*,4084")]
)

(define_insn "extendsidi2"
  [(set (match_operand:DI 0 "s_register_operand" "=r")
        (sign_extend:DI (match_operand:SI 1 "s_register_operand" "r")))]
  "TARGET_ARM"
  "*
    if (REGNO (operands[1])
        != REGNO (operands[0]) + (WORDS_BIG_ENDIAN ? 1 : 0))
      output_asm_insn (\"mov%?\\t%Q0, %1\", operands);
    return \"mov%?\\t%R0, %Q0, asr #31\";
  "
  [(set_attr "length" "8")
   (set_attr "shift" "1")
   (set_attr "predicable" "yes")]
)

(define_expand "zero_extendhisi2"
  [(set (match_dup 2)
	(ashift:SI (match_operand:HI 1 "nonimmediate_operand" "")
		   (const_int 16)))
   (set (match_operand:SI 0 "s_register_operand" "")
	(lshiftrt:SI (match_dup 2) (const_int 16)))]
  "TARGET_EITHER"
  "
  {
    if ((TARGET_THUMB || arm_arch4) && GET_CODE (operands[1]) == MEM)
      {
	emit_insn (gen_rtx_SET (VOIDmode, operands[0],
				gen_rtx_ZERO_EXTEND (SImode, operands[1])));
	DONE;
      }

    if (TARGET_ARM && GET_CODE (operands[1]) == MEM)
      {
	emit_insn (gen_movhi_bytes (operands[0], operands[1]));
	DONE;
      }

    if (!s_register_operand (operands[1], HImode))
      operands[1] = copy_to_mode_reg (HImode, operands[1]);

    if (arm_arch6)
      {
	emit_insn (gen_rtx_SET (VOIDmode, operands[0],
				gen_rtx_ZERO_EXTEND (SImode, operands[1])));
	DONE;
      }

    operands[1] = gen_lowpart (SImode, operands[1]);
    operands[2] = gen_reg_rtx (SImode);
  }"
)

(define_insn "*thumb_zero_extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=l")
	(zero_extend:SI (match_operand:HI 1 "memory_operand" "m")))]
  "TARGET_THUMB && !arm_arch6"
  "*
  rtx mem = XEXP (operands[1], 0);

  if (GET_CODE (mem) == CONST)
    mem = XEXP (mem, 0);
    
  if (GET_CODE (mem) == LABEL_REF)
    return \"ldr\\t%0, %1\";
    
  if (GET_CODE (mem) == PLUS)
    {
      rtx a = XEXP (mem, 0);
      rtx b = XEXP (mem, 1);

      /* This can happen due to bugs in reload.  */
      if (GET_CODE (a) == REG && REGNO (a) == SP_REGNUM)
        {
          rtx ops[2];
          ops[0] = operands[0];
          ops[1] = a;
      
          output_asm_insn (\"mov	%0, %1\", ops);

          XEXP (mem, 0) = operands[0];
       }

      else if (   GET_CODE (a) == LABEL_REF
	       && GET_CODE (b) == CONST_INT)
        return \"ldr\\t%0, %1\";
    }
    
  return \"ldrh\\t%0, %1\";
  "
  [(set_attr "length" "4")
   (set_attr "type" "load_byte")
   (set_attr "pool_range" "60")]
)

(define_insn "*thumb_zero_extendhisi2_v6"
  [(set (match_operand:SI 0 "register_operand" "=l,l")
	(zero_extend:SI (match_operand:HI 1 "nonimmediate_operand" "l,m")))]
  "TARGET_THUMB && arm_arch6"
  "*
  rtx mem;

  if (which_alternative == 0)
    return \"uxth\\t%0, %1\";

  mem = XEXP (operands[1], 0);

  if (GET_CODE (mem) == CONST)
    mem = XEXP (mem, 0);
    
  if (GET_CODE (mem) == LABEL_REF)
    return \"ldr\\t%0, %1\";
    
  if (GET_CODE (mem) == PLUS)
    {
      rtx a = XEXP (mem, 0);
      rtx b = XEXP (mem, 1);

      /* This can happen due to bugs in reload.  */
      if (GET_CODE (a) == REG && REGNO (a) == SP_REGNUM)
        {
          rtx ops[2];
          ops[0] = operands[0];
          ops[1] = a;
      
          output_asm_insn (\"mov	%0, %1\", ops);

          XEXP (mem, 0) = operands[0];
       }

      else if (   GET_CODE (a) == LABEL_REF
	       && GET_CODE (b) == CONST_INT)
        return \"ldr\\t%0, %1\";
    }
    
  return \"ldrh\\t%0, %1\";
  "
  [(set_attr "length" "2,4")
   (set_attr "type" "alu_shift,load_byte")
   (set_attr "pool_range" "*,60")]
)

(define_insn "*arm_zero_extendhisi2"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(zero_extend:SI (match_operand:HI 1 "memory_operand" "m")))]
  "TARGET_ARM && arm_arch4 && !arm_arch6"
  "ldr%?h\\t%0, %1"
  [(set_attr "type" "load_byte")
   (set_attr "predicable" "yes")
   (set_attr "pool_range" "256")
   (set_attr "neg_pool_range" "244")]
)

(define_insn "*arm_zero_extendhisi2_v6"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(zero_extend:SI (match_operand:HI 1 "nonimmediate_operand" "r,m")))]
  "TARGET_ARM && arm_arch6"
  "@
   uxth%?\\t%0, %1
   ldr%?h\\t%0, %1"
  [(set_attr "type" "alu_shift,load_byte")
   (set_attr "predicable" "yes")
   (set_attr "pool_range" "*,256")
   (set_attr "neg_pool_range" "*,244")]
)

(define_insn "*arm_zero_extendhisi2addsi"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(plus:SI (zero_extend:SI (match_operand:HI 1 "s_register_operand" "r"))
		 (match_operand:SI 2 "s_register_operand" "r")))]
  "TARGET_ARM && arm_arch6"
  "uxtah%?\\t%0, %2, %1"
  [(set_attr "type" "alu_shift")
   (set_attr "predicable" "yes")]
)

(define_expand "zero_extendqisi2"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(zero_extend:SI (match_operand:QI 1 "nonimmediate_operand" "")))]
  "TARGET_EITHER"
  "
  if (!arm_arch6 && GET_CODE (operands[1]) != MEM)
    {
      if (TARGET_ARM)
        {
          emit_insn (gen_andsi3 (operands[0],
				 gen_lowpart (SImode, operands[1]),
			         GEN_INT (255)));
        }
      else /* TARGET_THUMB */
        {
          rtx temp = gen_reg_rtx (SImode);
	  rtx ops[3];
	  
          operands[1] = copy_to_mode_reg (QImode, operands[1]);
          operands[1] = gen_lowpart (SImode, operands[1]);

	  ops[0] = temp;
	  ops[1] = operands[1];
	  ops[2] = GEN_INT (24);

	  emit_insn (gen_rtx_SET (VOIDmode, ops[0],
				  gen_rtx_ASHIFT (SImode, ops[1], ops[2])));
	  
          ops[0] = operands[0];
	  ops[1] = temp;
	  ops[2] = GEN_INT (24);

	  emit_insn (gen_rtx_SET (VOIDmode, ops[0],
				  gen_rtx_LSHIFTRT (SImode, ops[1], ops[2])));
	}
      DONE;
    }
  "
)

(define_insn "*thumb_zero_extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=l")
	(zero_extend:SI (match_operand:QI 1 "memory_operand" "m")))]
  "TARGET_THUMB && !arm_arch6"
  "ldrb\\t%0, %1"
  [(set_attr "length" "2")
   (set_attr "type" "load_byte")
   (set_attr "pool_range" "32")]
)

(define_insn "*thumb_zero_extendqisi2_v6"
  [(set (match_operand:SI 0 "register_operand" "=l,l")
	(zero_extend:SI (match_operand:QI 1 "nonimmediate_operand" "l,m")))]
  "TARGET_THUMB && arm_arch6"
  "@
   uxtb\\t%0, %1
   ldrb\\t%0, %1"
  [(set_attr "length" "2,2")
   (set_attr "type" "alu_shift,load_byte")
   (set_attr "pool_range" "*,32")]
)

(define_insn "*arm_zero_extendqisi2"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(zero_extend:SI (match_operand:QI 1 "memory_operand" "m")))]
  "TARGET_ARM && !arm_arch6"
  "ldr%?b\\t%0, %1\\t%@ zero_extendqisi2"
  [(set_attr "type" "load_byte")
   (set_attr "predicable" "yes")
   (set_attr "pool_range" "4096")
   (set_attr "neg_pool_range" "4084")]
)

(define_insn "*arm_zero_extendqisi2_v6"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(zero_extend:SI (match_operand:QI 1 "nonimmediate_operand" "r,m")))]
  "TARGET_ARM && arm_arch6"
  "@
   uxtb%?\\t%0, %1
   ldr%?b\\t%0, %1\\t%@ zero_extendqisi2"
  [(set_attr "type" "alu_shift,load_byte")
   (set_attr "predicable" "yes")
   (set_attr "pool_range" "*,4096")
   (set_attr "neg_pool_range" "*,4084")]
)

(define_insn "*arm_zero_extendqisi2addsi"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(plus:SI (zero_extend:SI (match_operand:QI 1 "s_register_operand" "r"))
		 (match_operand:SI 2 "s_register_operand" "r")))]
  "TARGET_ARM && arm_arch6"
  "uxtab%?\\t%0, %2, %1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "alu_shift")]
)

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(zero_extend:SI (subreg:QI (match_operand:SI 1 "" "") 0)))
   (clobber (match_operand:SI 2 "s_register_operand" ""))]
  "TARGET_ARM && (GET_CODE (operands[1]) != MEM) && ! BYTES_BIG_ENDIAN"
  [(set (match_dup 2) (match_dup 1))
   (set (match_dup 0) (and:SI (match_dup 2) (const_int 255)))]
  ""
)

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(zero_extend:SI (subreg:QI (match_operand:SI 1 "" "") 3)))
   (clobber (match_operand:SI 2 "s_register_operand" ""))]
  "TARGET_ARM && (GET_CODE (operands[1]) != MEM) && BYTES_BIG_ENDIAN"
  [(set (match_dup 2) (match_dup 1))
   (set (match_dup 0) (and:SI (match_dup 2) (const_int 255)))]
  ""
)

(define_insn "*compareqi_eq0"
  [(set (reg:CC_Z CC_REGNUM)
	(compare:CC_Z (match_operand:QI 0 "s_register_operand" "r")
			 (const_int 0)))]
  "TARGET_ARM"
  "tst\\t%0, #255"
  [(set_attr "conds" "set")]
)

(define_expand "extendhisi2"
  [(set (match_dup 2)
	(ashift:SI (match_operand:HI 1 "nonimmediate_operand" "")
		   (const_int 16)))
   (set (match_operand:SI 0 "s_register_operand" "")
	(ashiftrt:SI (match_dup 2)
		     (const_int 16)))]
  "TARGET_EITHER"
  "
  {
    if (GET_CODE (operands[1]) == MEM)
      {
	if (TARGET_THUMB)
	  {
	    emit_insn (gen_thumb_extendhisi2 (operands[0], operands[1]));
	    DONE;
          }
	else if (arm_arch4)
	  {
	    emit_insn (gen_rtx_SET (VOIDmode, operands[0],
		       gen_rtx_SIGN_EXTEND (SImode, operands[1])));
	    DONE;
	  }
      }

    if (TARGET_ARM && GET_CODE (operands[1]) == MEM)
      {
        emit_insn (gen_extendhisi2_mem (operands[0], operands[1]));
        DONE;
      }

    if (!s_register_operand (operands[1], HImode))
      operands[1] = copy_to_mode_reg (HImode, operands[1]);

    if (arm_arch6)
      {
	if (TARGET_THUMB)
	  emit_insn (gen_thumb_extendhisi2 (operands[0], operands[1]));
	else
	  emit_insn (gen_rtx_SET (VOIDmode, operands[0],
		     gen_rtx_SIGN_EXTEND (SImode, operands[1])));

	DONE;
      }

    operands[1] = gen_lowpart (SImode, operands[1]);
    operands[2] = gen_reg_rtx (SImode);
  }"
)

(define_insn "thumb_extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=l")
	(sign_extend:SI (match_operand:HI 1 "memory_operand" "m")))
   (clobber (match_scratch:SI 2 "=&l"))]
  "TARGET_THUMB && !arm_arch6"
  "*
  {
    rtx ops[4];
    rtx mem = XEXP (operands[1], 0);

    /* This code used to try to use 'V', and fix the address only if it was
       offsettable, but this fails for e.g. REG+48 because 48 is outside the
       range of QImode offsets, and offsettable_address_p does a QImode
       address check.  */
       
    if (GET_CODE (mem) == CONST)
      mem = XEXP (mem, 0);
    
    if (GET_CODE (mem) == LABEL_REF)
      return \"ldr\\t%0, %1\";
    
    if (GET_CODE (mem) == PLUS)
      {
        rtx a = XEXP (mem, 0);
        rtx b = XEXP (mem, 1);

        if (GET_CODE (a) == LABEL_REF
	    && GET_CODE (b) == CONST_INT)
          return \"ldr\\t%0, %1\";

        if (GET_CODE (b) == REG)
          return \"ldrsh\\t%0, %1\";
	  
        ops[1] = a;
        ops[2] = b;
      }
    else
      {
        ops[1] = mem;
        ops[2] = const0_rtx;
      }

    gcc_assert (GET_CODE (ops[1]) == REG);

    ops[0] = operands[0];
    ops[3] = operands[2];
    output_asm_insn (\"mov\\t%3, %2\;ldrsh\\t%0, [%1, %3]\", ops);
    return \"\";
  }"
  [(set_attr "length" "4")
   (set_attr "type" "load_byte")
   (set_attr "pool_range" "1020")]
)

;; We used to have an early-clobber on the scratch register here.
;; However, there's a bug somewhere in reload which means that this
;; can be partially ignored during spill allocation if the memory
;; address also needs reloading; this causes us to die later on when
;; we try to verify the operands.  Fortunately, we don't really need
;; the early-clobber: we can always use operand 0 if operand 2
;; overlaps the address.
(define_insn "*thumb_extendhisi2_insn_v6"
  [(set (match_operand:SI 0 "register_operand" "=l,l")
	(sign_extend:SI (match_operand:HI 1 "nonimmediate_operand" "l,m")))
   (clobber (match_scratch:SI 2 "=X,l"))]
  "TARGET_THUMB && arm_arch6"
  "*
  {
    rtx ops[4];
    rtx mem;

    if (which_alternative == 0)
      return \"sxth\\t%0, %1\";

    mem = XEXP (operands[1], 0);

    /* This code used to try to use 'V', and fix the address only if it was
       offsettable, but this fails for e.g. REG+48 because 48 is outside the
       range of QImode offsets, and offsettable_address_p does a QImode
       address check.  */
       
    if (GET_CODE (mem) == CONST)
      mem = XEXP (mem, 0);
    
    if (GET_CODE (mem) == LABEL_REF)
      return \"ldr\\t%0, %1\";
    
    if (GET_CODE (mem) == PLUS)
      {
        rtx a = XEXP (mem, 0);
        rtx b = XEXP (mem, 1);

        if (GET_CODE (a) == LABEL_REF
	    && GET_CODE (b) == CONST_INT)
          return \"ldr\\t%0, %1\";

        if (GET_CODE (b) == REG)
          return \"ldrsh\\t%0, %1\";
	  
        ops[1] = a;
        ops[2] = b;
      }
    else
      {
        ops[1] = mem;
        ops[2] = const0_rtx;
      }
      
    gcc_assert (GET_CODE (ops[1]) == REG);

    ops[0] = operands[0];
    if (reg_mentioned_p (operands[2], ops[1]))
      ops[3] = ops[0];
    else
      ops[3] = operands[2];
    output_asm_insn (\"mov\\t%3, %2\;ldrsh\\t%0, [%1, %3]\", ops);
    return \"\";
  }"
  [(set_attr "length" "2,4")
   (set_attr "type" "alu_shift,load_byte")
   (set_attr "pool_range" "*,1020")]
)

(define_expand "extendhisi2_mem"
  [(set (match_dup 2) (zero_extend:SI (match_operand:HI 1 "" "")))
   (set (match_dup 3)
	(zero_extend:SI (match_dup 7)))
   (set (match_dup 6) (ashift:SI (match_dup 4) (const_int 24)))
   (set (match_operand:SI 0 "" "")
	(ior:SI (ashiftrt:SI (match_dup 6) (const_int 16)) (match_dup 5)))]
  "TARGET_ARM"
  "
  {
    rtx mem1, mem2;
    rtx addr = copy_to_mode_reg (SImode, XEXP (operands[1], 0));

    mem1 = change_address (operands[1], QImode, addr);
    mem2 = change_address (operands[1], QImode, plus_constant (addr, 1));
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[1] = mem1;
    operands[2] = gen_reg_rtx (SImode);
    operands[3] = gen_reg_rtx (SImode);
    operands[6] = gen_reg_rtx (SImode);
    operands[7] = mem2;

    if (BYTES_BIG_ENDIAN)
      {
	operands[4] = operands[2];
	operands[5] = operands[3];
      }
    else
      {
	operands[4] = operands[3];
	operands[5] = operands[2];
      }
  }"
)

(define_insn "*arm_extendhisi2"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(sign_extend:SI (match_operand:HI 1 "memory_operand" "m")))]
  "TARGET_ARM && arm_arch4 && !arm_arch6"
  "ldr%?sh\\t%0, %1"
  [(set_attr "type" "load_byte")
   (set_attr "predicable" "yes")
   (set_attr "pool_range" "256")
   (set_attr "neg_pool_range" "244")]
)

(define_insn "*arm_extendhisi2_v6"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(sign_extend:SI (match_operand:HI 1 "nonimmediate_operand" "r,m")))]
  "TARGET_ARM && arm_arch6"
  "@
   sxth%?\\t%0, %1
   ldr%?sh\\t%0, %1"
  [(set_attr "type" "alu_shift,load_byte")
   (set_attr "predicable" "yes")
   (set_attr "pool_range" "*,256")
   (set_attr "neg_pool_range" "*,244")]
)

(define_insn "*arm_extendhisi2addsi"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(plus:SI (sign_extend:SI (match_operand:HI 1 "s_register_operand" "r"))
		 (match_operand:SI 2 "s_register_operand" "r")))]
  "TARGET_ARM && arm_arch6"
  "sxtah%?\\t%0, %2, %1"
)

(define_expand "extendqihi2"
  [(set (match_dup 2)
	(ashift:SI (match_operand:QI 1 "general_operand" "")
		   (const_int 24)))
   (set (match_operand:HI 0 "s_register_operand" "")
	(ashiftrt:SI (match_dup 2)
		     (const_int 24)))]
  "TARGET_ARM"
  "
  {
    if (arm_arch4 && GET_CODE (operands[1]) == MEM)
      {
	emit_insn (gen_rtx_SET (VOIDmode,
				operands[0],
				gen_rtx_SIGN_EXTEND (HImode, operands[1])));
	DONE;
      }
    if (!s_register_operand (operands[1], QImode))
      operands[1] = copy_to_mode_reg (QImode, operands[1]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[1] = gen_lowpart (SImode, operands[1]);
    operands[2] = gen_reg_rtx (SImode);
  }"
)

(define_insn "*extendqihi_insn"
  [(set (match_operand:HI 0 "s_register_operand" "=r")
	(sign_extend:HI (match_operand:QI 1 "memory_operand" "Uq")))]
  "TARGET_ARM && arm_arch4"
  "ldr%?sb\\t%0, %1"
  [(set_attr "type" "load_byte")
   (set_attr "predicable" "yes")
   (set_attr "pool_range" "256")
   (set_attr "neg_pool_range" "244")]
)

(define_expand "extendqisi2"
  [(set (match_dup 2)
	(ashift:SI (match_operand:QI 1 "general_operand" "")
		   (const_int 24)))
   (set (match_operand:SI 0 "s_register_operand" "")
	(ashiftrt:SI (match_dup 2)
		     (const_int 24)))]
  "TARGET_EITHER"
  "
  {
    if ((TARGET_THUMB || arm_arch4) && GET_CODE (operands[1]) == MEM)
      {
        emit_insn (gen_rtx_SET (VOIDmode, operands[0],
			        gen_rtx_SIGN_EXTEND (SImode, operands[1])));
        DONE;
      }

    if (!s_register_operand (operands[1], QImode))
      operands[1] = copy_to_mode_reg (QImode, operands[1]);

    if (arm_arch6)
      {
        emit_insn (gen_rtx_SET (VOIDmode, operands[0],
			        gen_rtx_SIGN_EXTEND (SImode, operands[1])));
        DONE;
      }

    operands[1] = gen_lowpart (SImode, operands[1]);
    operands[2] = gen_reg_rtx (SImode);
  }"
)

(define_insn "*arm_extendqisi"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(sign_extend:SI (match_operand:QI 1 "memory_operand" "Uq")))]
  "TARGET_ARM && arm_arch4 && !arm_arch6"
  "ldr%?sb\\t%0, %1"
  [(set_attr "type" "load_byte")
   (set_attr "predicable" "yes")
   (set_attr "pool_range" "256")
   (set_attr "neg_pool_range" "244")]
)

(define_insn "*arm_extendqisi_v6"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(sign_extend:SI (match_operand:QI 1 "nonimmediate_operand" "r,Uq")))]
  "TARGET_ARM && arm_arch6"
  "@
   sxtb%?\\t%0, %1
   ldr%?sb\\t%0, %1"
  [(set_attr "type" "alu_shift,load_byte")
   (set_attr "predicable" "yes")
   (set_attr "pool_range" "*,256")
   (set_attr "neg_pool_range" "*,244")]
)

(define_insn "*arm_extendqisi2addsi"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(plus:SI (sign_extend:SI (match_operand:QI 1 "s_register_operand" "r"))
		 (match_operand:SI 2 "s_register_operand" "r")))]
  "TARGET_ARM && arm_arch6"
  "sxtab%?\\t%0, %2, %1"
  [(set_attr "type" "alu_shift")
   (set_attr "predicable" "yes")]
)

(define_insn "*thumb_extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=l,l")
	(sign_extend:SI (match_operand:QI 1 "memory_operand" "V,m")))]
  "TARGET_THUMB && !arm_arch6"
  "*
  {
    rtx ops[3];
    rtx mem = XEXP (operands[1], 0);
    
    if (GET_CODE (mem) == CONST)
      mem = XEXP (mem, 0);
    
    if (GET_CODE (mem) == LABEL_REF)
      return \"ldr\\t%0, %1\";

    if (GET_CODE (mem) == PLUS
        && GET_CODE (XEXP (mem, 0)) == LABEL_REF)
      return \"ldr\\t%0, %1\";
      
    if (which_alternative == 0)
      return \"ldrsb\\t%0, %1\";
      
    ops[0] = operands[0];
    
    if (GET_CODE (mem) == PLUS)
      {
        rtx a = XEXP (mem, 0);
	rtx b = XEXP (mem, 1);
	
        ops[1] = a;
        ops[2] = b;

        if (GET_CODE (a) == REG)
	  {
	    if (GET_CODE (b) == REG)
              output_asm_insn (\"ldrsb\\t%0, [%1, %2]\", ops);
            else if (REGNO (a) == REGNO (ops[0]))
	      {
                output_asm_insn (\"ldrb\\t%0, [%1, %2]\", ops);
		output_asm_insn (\"lsl\\t%0, %0, #24\", ops);
		output_asm_insn (\"asr\\t%0, %0, #24\", ops);
	      }
	    else
              output_asm_insn (\"mov\\t%0, %2\;ldrsb\\t%0, [%1, %0]\", ops);
	  }
	else
          {
	    gcc_assert (GET_CODE (b) == REG);
            if (REGNO (b) == REGNO (ops[0]))
	      {
                output_asm_insn (\"ldrb\\t%0, [%2, %1]\", ops);
		output_asm_insn (\"lsl\\t%0, %0, #24\", ops);
		output_asm_insn (\"asr\\t%0, %0, #24\", ops);
	      }
	    else
              output_asm_insn (\"mov\\t%0, %2\;ldrsb\\t%0, [%1, %0]\", ops);
          }
      }
    else if (GET_CODE (mem) == REG && REGNO (ops[0]) == REGNO (mem))
      {
        output_asm_insn (\"ldrb\\t%0, [%0, #0]\", ops);
	output_asm_insn (\"lsl\\t%0, %0, #24\", ops);
	output_asm_insn (\"asr\\t%0, %0, #24\", ops);
      }
    else
      {
        ops[1] = mem;
        ops[2] = const0_rtx;
	
        output_asm_insn (\"mov\\t%0, %2\;ldrsb\\t%0, [%1, %0]\", ops);
      }
    return \"\";
  }"
  [(set_attr "length" "2,6")
   (set_attr "type" "load_byte,load_byte")
   (set_attr "pool_range" "32,32")]
)

(define_insn "*thumb_extendqisi2_v6"
  [(set (match_operand:SI 0 "register_operand" "=l,l,l")
	(sign_extend:SI (match_operand:QI 1 "nonimmediate_operand" "l,V,m")))]
  "TARGET_THUMB && arm_arch6"
  "*
  {
    rtx ops[3];
    rtx mem;

    if (which_alternative == 0)
      return \"sxtb\\t%0, %1\";

    mem = XEXP (operands[1], 0);
    
    if (GET_CODE (mem) == CONST)
      mem = XEXP (mem, 0);
    
    if (GET_CODE (mem) == LABEL_REF)
      return \"ldr\\t%0, %1\";

    if (GET_CODE (mem) == PLUS
        && GET_CODE (XEXP (mem, 0)) == LABEL_REF)
      return \"ldr\\t%0, %1\";
      
    if (which_alternative == 0)
      return \"ldrsb\\t%0, %1\";
      
    ops[0] = operands[0];
    
    if (GET_CODE (mem) == PLUS)
      {
        rtx a = XEXP (mem, 0);
	rtx b = XEXP (mem, 1);
	
        ops[1] = a;
        ops[2] = b;

        if (GET_CODE (a) == REG)
	  {
	    if (GET_CODE (b) == REG)
              output_asm_insn (\"ldrsb\\t%0, [%1, %2]\", ops);
            else if (REGNO (a) == REGNO (ops[0]))
	      {
                output_asm_insn (\"ldrb\\t%0, [%1, %2]\", ops);
		output_asm_insn (\"sxtb\\t%0, %0\", ops);
	      }
	    else
              output_asm_insn (\"mov\\t%0, %2\;ldrsb\\t%0, [%1, %0]\", ops);
	  }
	else
          {
	    gcc_assert (GET_CODE (b) == REG);
            if (REGNO (b) == REGNO (ops[0]))
	      {
                output_asm_insn (\"ldrb\\t%0, [%2, %1]\", ops);
		output_asm_insn (\"sxtb\\t%0, %0\", ops);
	      }
	    else
              output_asm_insn (\"mov\\t%0, %2\;ldrsb\\t%0, [%1, %0]\", ops);
          }
      }
    else if (GET_CODE (mem) == REG && REGNO (ops[0]) == REGNO (mem))
      {
        output_asm_insn (\"ldrb\\t%0, [%0, #0]\", ops);
	output_asm_insn (\"sxtb\\t%0, %0\", ops);
      }
    else
      {
        ops[1] = mem;
        ops[2] = const0_rtx;
	
        output_asm_insn (\"mov\\t%0, %2\;ldrsb\\t%0, [%1, %0]\", ops);
      }
    return \"\";
  }"
  [(set_attr "length" "2,2,4")
   (set_attr "type" "alu_shift,load_byte,load_byte")
   (set_attr "pool_range" "*,32,32")]
)

(define_expand "extendsfdf2"
  [(set (match_operand:DF                  0 "s_register_operand" "")
	(float_extend:DF (match_operand:SF 1 "s_register_operand"  "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  ""
)

;; Move insns (including loads and stores)

;; XXX Just some ideas about movti.
;; I don't think these are a good idea on the arm, there just aren't enough
;; registers
;;(define_expand "loadti"
;;  [(set (match_operand:TI 0 "s_register_operand" "")
;;	(mem:TI (match_operand:SI 1 "address_operand" "")))]
;;  "" "")

;;(define_expand "storeti"
;;  [(set (mem:TI (match_operand:TI 0 "address_operand" ""))
;;	(match_operand:TI 1 "s_register_operand" ""))]
;;  "" "")

;;(define_expand "movti"
;;  [(set (match_operand:TI 0 "general_operand" "")
;;	(match_operand:TI 1 "general_operand" ""))]
;;  ""
;;  "
;;{
;;  rtx insn;
;;
;;  if (GET_CODE (operands[0]) == MEM && GET_CODE (operands[1]) == MEM)
;;    operands[1] = copy_to_reg (operands[1]);
;;  if (GET_CODE (operands[0]) == MEM)
;;    insn = gen_storeti (XEXP (operands[0], 0), operands[1]);
;;  else if (GET_CODE (operands[1]) == MEM)
;;    insn = gen_loadti (operands[0], XEXP (operands[1], 0));
;;  else
;;    FAIL;
;;
;;  emit_insn (insn);
;;  DONE;
;;}")

;; Recognize garbage generated above.

;;(define_insn ""
;;  [(set (match_operand:TI 0 "general_operand" "=r,r,r,<,>,m")
;;	(match_operand:TI 1 "general_operand" "<,>,m,r,r,r"))]
;;  ""
;;  "*
;;  {
;;    register mem = (which_alternative < 3);
;;    register const char *template;
;;
;;    operands[mem] = XEXP (operands[mem], 0);
;;    switch (which_alternative)
;;      {
;;      case 0: template = \"ldmdb\\t%1!, %M0\"; break;
;;      case 1: template = \"ldmia\\t%1!, %M0\"; break;
;;      case 2: template = \"ldmia\\t%1, %M0\"; break;
;;      case 3: template = \"stmdb\\t%0!, %M1\"; break;
;;      case 4: template = \"stmia\\t%0!, %M1\"; break;
;;      case 5: template = \"stmia\\t%0, %M1\"; break;
;;      }
;;    output_asm_insn (template, operands);
;;    return \"\";
;;  }")

(define_expand "movdi"
  [(set (match_operand:DI 0 "general_operand" "")
	(match_operand:DI 1 "general_operand" ""))]
  "TARGET_EITHER"
  "
  if (!no_new_pseudos)
    {
      if (GET_CODE (operands[0]) != REG)
	operands[1] = force_reg (DImode, operands[1]);
    }
  "
)

(define_insn "*arm_movdi"
  [(set (match_operand:DI 0 "nonimmediate_di_operand" "=r, r, r, r, m")
	(match_operand:DI 1 "di_operand"              "rDa,Db,Dc,mi,r"))]
  "TARGET_ARM
   && !(TARGET_HARD_FLOAT && (TARGET_MAVERICK || TARGET_VFP))
   && !TARGET_IWMMXT
   && (   register_operand (operands[0], DImode)
       || register_operand (operands[1], DImode))"
  "*
  switch (which_alternative)
    {
    case 0:
    case 1:
    case 2:
      return \"#\";
    default:
      return output_move_double (operands);
    }
  "
  [(set_attr "length" "8,12,16,8,8")
   (set_attr "type" "*,*,*,load2,store2")
   (set_attr "pool_range" "*,*,*,1020,*")
   (set_attr "neg_pool_range" "*,*,*,1008,*")]
)

(define_split
  [(set (match_operand:ANY64 0 "arm_general_register_operand" "")
	(match_operand:ANY64 1 "const_double_operand" ""))]
  "TARGET_ARM
   && reload_completed
   && (arm_const_double_inline_cost (operands[1])
       <= ((optimize_size || arm_ld_sched) ? 3 : 4))"
  [(const_int 0)]
  "
  arm_split_constant (SET, SImode, curr_insn,
		      INTVAL (gen_lowpart (SImode, operands[1])),
		      gen_lowpart (SImode, operands[0]), NULL_RTX, 0);
  arm_split_constant (SET, SImode, curr_insn,
		      INTVAL (gen_highpart_mode (SImode,
						 GET_MODE (operands[0]),
						 operands[1])),
		      gen_highpart (SImode, operands[0]), NULL_RTX, 0);
  DONE;
  "
)

; If optimizing for size, or if we have load delay slots, then 
; we want to split the constant into two separate operations. 
; In both cases this may split a trivial part into a single data op
; leaving a single complex constant to load.  We can also get longer
; offsets in a LDR which means we get better chances of sharing the pool
; entries.  Finally, we can normally do a better job of scheduling
; LDR instructions than we can with LDM.
; This pattern will only match if the one above did not.
(define_split
  [(set (match_operand:ANY64 0 "arm_general_register_operand" "")
	(match_operand:ANY64 1 "const_double_operand" ""))]
  "TARGET_ARM && reload_completed
   && arm_const_double_by_parts (operands[1])"
  [(set (match_dup 0) (match_dup 1))
   (set (match_dup 2) (match_dup 3))]
  "
  operands[2] = gen_highpart (SImode, operands[0]);
  operands[3] = gen_highpart_mode (SImode, GET_MODE (operands[0]),
				   operands[1]);
  operands[0] = gen_lowpart (SImode, operands[0]);
  operands[1] = gen_lowpart (SImode, operands[1]);
  "
)

(define_split
  [(set (match_operand:ANY64 0 "arm_general_register_operand" "")
	(match_operand:ANY64 1 "arm_general_register_operand" ""))]
  "TARGET_EITHER && reload_completed"
  [(set (match_dup 0) (match_dup 1))
   (set (match_dup 2) (match_dup 3))]
  "
  operands[2] = gen_highpart (SImode, operands[0]);
  operands[3] = gen_highpart (SImode, operands[1]);
  operands[0] = gen_lowpart (SImode, operands[0]);
  operands[1] = gen_lowpart (SImode, operands[1]);

  /* Handle a partial overlap.  */
  if (rtx_equal_p (operands[0], operands[3]))
    {
      rtx tmp0 = operands[0];
      rtx tmp1 = operands[1];

      operands[0] = operands[2];
      operands[1] = operands[3];
      operands[2] = tmp0;
      operands[3] = tmp1;
    }
  "
)

;; We can't actually do base+index doubleword loads if the index and
;; destination overlap.  Split here so that we at least have chance to
;; schedule.
(define_split
  [(set (match_operand:DI 0 "s_register_operand" "")
	(mem:DI (plus:SI (match_operand:SI 1 "s_register_operand" "")
			 (match_operand:SI 2 "s_register_operand" ""))))]
  "TARGET_LDRD
  && reg_overlap_mentioned_p (operands[0], operands[1])
  && reg_overlap_mentioned_p (operands[0], operands[2])"
  [(set (match_dup 4)
	(plus:SI (match_dup 1)
		 (match_dup 2)))
   (set (match_dup 0)
	(mem:DI (match_dup 4)))]
  "
  operands[4] = gen_rtx_REG (SImode, REGNO(operands[0]));
  "
)

;;; ??? This should have alternatives for constants.
;;; ??? This was originally identical to the movdf_insn pattern.
;;; ??? The 'i' constraint looks funny, but it should always be replaced by
;;; thumb_reorg with a memory reference.
(define_insn "*thumb_movdi_insn"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=l,l,l,l,>,l, m,*r")
	(match_operand:DI 1 "general_operand"      "l, I,J,>,l,mi,l,*r"))]
  "TARGET_THUMB
   && !(TARGET_HARD_FLOAT && TARGET_MAVERICK)
   && (   register_operand (operands[0], DImode)
       || register_operand (operands[1], DImode))"
  "*
  {
  switch (which_alternative)
    {
    default:
    case 0:
      if (REGNO (operands[1]) == REGNO (operands[0]) + 1)
	return \"add\\t%0,  %1,  #0\;add\\t%H0, %H1, #0\";
      return   \"add\\t%H0, %H1, #0\;add\\t%0,  %1,  #0\";
    case 1:
      return \"mov\\t%Q0, %1\;mov\\t%R0, #0\";
    case 2:
      operands[1] = GEN_INT (- INTVAL (operands[1]));
      return \"mov\\t%Q0, %1\;neg\\t%Q0, %Q0\;asr\\t%R0, %Q0, #31\";
    case 3:
      return \"ldmia\\t%1, {%0, %H0}\";
    case 4:
      return \"stmia\\t%0, {%1, %H1}\";
    case 5:
      return thumb_load_double_from_address (operands);
    case 6:
      operands[2] = gen_rtx_MEM (SImode,
			     plus_constant (XEXP (operands[0], 0), 4));
      output_asm_insn (\"str\\t%1, %0\;str\\t%H1, %2\", operands);
      return \"\";
    case 7:
      if (REGNO (operands[1]) == REGNO (operands[0]) + 1)
	return \"mov\\t%0, %1\;mov\\t%H0, %H1\";
      return \"mov\\t%H0, %H1\;mov\\t%0, %1\";
    }
  }"
  [(set_attr "length" "4,4,6,2,2,6,4,4")
   (set_attr "type" "*,*,*,load2,store2,load2,store2,*")
   (set_attr "pool_range" "*,*,*,*,*,1020,*,*")]
)

(define_expand "movsi"
  [(set (match_operand:SI 0 "general_operand" "")
        (match_operand:SI 1 "general_operand" ""))]
  "TARGET_EITHER"
  "
  if (TARGET_ARM)
    {
      /* Everything except mem = const or mem = mem can be done easily.  */
      if (GET_CODE (operands[0]) == MEM)
        operands[1] = force_reg (SImode, operands[1]);
      if (arm_general_register_operand (operands[0], SImode)
	  && GET_CODE (operands[1]) == CONST_INT
          && !(const_ok_for_arm (INTVAL (operands[1]))
               || const_ok_for_arm (~INTVAL (operands[1]))))
        {
           arm_split_constant (SET, SImode, NULL_RTX,
	                       INTVAL (operands[1]), operands[0], NULL_RTX,
			       optimize && !no_new_pseudos);
          DONE;
        }
    }
  else /* TARGET_THUMB....  */
    {
      if (!no_new_pseudos)
        {
          if (GET_CODE (operands[0]) != REG)
	    operands[1] = force_reg (SImode, operands[1]);
        }
    }

  /* Recognize the case where operand[1] is a reference to thread-local
     data and load its address to a register.  */
  if (arm_tls_referenced_p (operands[1]))
    {
      rtx tmp = operands[1];
      rtx addend = NULL;

      if (GET_CODE (tmp) == CONST && GET_CODE (XEXP (tmp, 0)) == PLUS)
        {
          addend = XEXP (XEXP (tmp, 0), 1);
          tmp = XEXP (XEXP (tmp, 0), 0);
        }

      gcc_assert (GET_CODE (tmp) == SYMBOL_REF);
      gcc_assert (SYMBOL_REF_TLS_MODEL (tmp) != 0);

      tmp = legitimize_tls_address (tmp, no_new_pseudos ? operands[0] : 0);
      if (addend)
        {
          tmp = gen_rtx_PLUS (SImode, tmp, addend);
          tmp = force_operand (tmp, operands[0]);
        }
      operands[1] = tmp;
    }
  else if (flag_pic
	   && (CONSTANT_P (operands[1])
	       || symbol_mentioned_p (operands[1])
	       || label_mentioned_p (operands[1])))
      operands[1] = legitimize_pic_address (operands[1], SImode,
					    (no_new_pseudos ? operands[0] : 0));
  "
)

(define_insn "*arm_movsi_insn"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,r,r, m")
	(match_operand:SI 1 "general_operand"      "rI,K,mi,r"))]
  "TARGET_ARM && ! TARGET_IWMMXT
   && !(TARGET_HARD_FLOAT && TARGET_VFP)
   && (   register_operand (operands[0], SImode)
       || register_operand (operands[1], SImode))"
  "@
   mov%?\\t%0, %1
   mvn%?\\t%0, #%B1
   ldr%?\\t%0, %1
   str%?\\t%1, %0"
  [(set_attr "type" "*,*,load1,store1")
   (set_attr "predicable" "yes")
   (set_attr "pool_range" "*,*,4096,*")
   (set_attr "neg_pool_range" "*,*,4084,*")]
)

(define_split
  [(set (match_operand:SI 0 "arm_general_register_operand" "")
	(match_operand:SI 1 "const_int_operand" ""))]
  "TARGET_ARM
  && (!(const_ok_for_arm (INTVAL (operands[1]))
        || const_ok_for_arm (~INTVAL (operands[1]))))"
  [(clobber (const_int 0))]
  "
  arm_split_constant (SET, SImode, NULL_RTX, 
                      INTVAL (operands[1]), operands[0], NULL_RTX, 0);
  DONE;
  "
)

(define_insn "*thumb_movsi_insn"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=l,l,l,l,l,>,l, m,*lh")
	(match_operand:SI 1 "general_operand"      "l, I,J,K,>,l,mi,l,*lh"))]
  "TARGET_THUMB
   && (   register_operand (operands[0], SImode) 
       || register_operand (operands[1], SImode))"
  "@
   mov	%0, %1
   mov	%0, %1
   #
   #
   ldmia\\t%1, {%0}
   stmia\\t%0, {%1}
   ldr\\t%0, %1
   str\\t%1, %0
   mov\\t%0, %1"
  [(set_attr "length" "2,2,4,4,2,2,2,2,2")
   (set_attr "type" "*,*,*,*,load1,store1,load1,store1,*")
   (set_attr "pool_range" "*,*,*,*,*,*,1020,*,*")]
)

(define_split 
  [(set (match_operand:SI 0 "register_operand" "")
	(match_operand:SI 1 "const_int_operand" ""))]
  "TARGET_THUMB && satisfies_constraint_J (operands[1])"
  [(set (match_dup 0) (match_dup 1))
   (set (match_dup 0) (neg:SI (match_dup 0)))]
  "operands[1] = GEN_INT (- INTVAL (operands[1]));"
)

(define_split 
  [(set (match_operand:SI 0 "register_operand" "")
	(match_operand:SI 1 "const_int_operand" ""))]
  "TARGET_THUMB && satisfies_constraint_K (operands[1])"
  [(set (match_dup 0) (match_dup 1))
   (set (match_dup 0) (ashift:SI (match_dup 0) (match_dup 2)))]
  "
  {
    unsigned HOST_WIDE_INT val = INTVAL (operands[1]);
    unsigned HOST_WIDE_INT mask = 0xff;
    int i;
    
    for (i = 0; i < 25; i++)
      if ((val & (mask << i)) == val)
        break;

    /* Shouldn't happen, but we don't want to split if the shift is zero.  */
    if (i == 0)
      FAIL;

    operands[1] = GEN_INT (val >> i);
    operands[2] = GEN_INT (i);
  }"
)

;; When generating pic, we need to load the symbol offset into a register.
;; So that the optimizer does not confuse this with a normal symbol load
;; we use an unspec.  The offset will be loaded from a constant pool entry,
;; since that is the only type of relocation we can use.

;; The rather odd constraints on the following are to force reload to leave
;; the insn alone, and to force the minipool generation pass to then move
;; the GOT symbol to memory.

(define_insn "pic_load_addr_arm"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(unspec:SI [(match_operand:SI 1 "" "mX")] UNSPEC_PIC_SYM))]
  "TARGET_ARM && flag_pic"
  "ldr%?\\t%0, %1"
  [(set_attr "type" "load1")
   (set (attr "pool_range")     (const_int 4096))
   (set (attr "neg_pool_range") (const_int 4084))]
)

(define_insn "pic_load_addr_thumb"
  [(set (match_operand:SI 0 "s_register_operand" "=l")
	(unspec:SI [(match_operand:SI 1 "" "mX")] UNSPEC_PIC_SYM))]
  "TARGET_THUMB && flag_pic"
  "ldr\\t%0, %1"
  [(set_attr "type" "load1")
   (set (attr "pool_range") (const_int 1024))]
)

;; This variant is used for AOF assembly, since it needs to mention the
;; pic register in the rtl.
(define_expand "pic_load_addr_based"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(unspec:SI [(match_operand 1 "" "") (match_dup 2)] UNSPEC_PIC_SYM))]
  "TARGET_ARM && flag_pic"
  "operands[2] = cfun->machine->pic_reg;"
)

(define_insn "*pic_load_addr_based_insn"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(unspec:SI [(match_operand 1 "" "")
		    (match_operand 2 "s_register_operand" "r")]
		   UNSPEC_PIC_SYM))]
  "TARGET_EITHER && flag_pic && operands[2] == cfun->machine->pic_reg"
  "*
#ifdef AOF_ASSEMBLER
  operands[1] = aof_pic_entry (operands[1]);
#endif
  output_asm_insn (\"ldr%?\\t%0, %a1\", operands);
  return \"\";
  "
  [(set_attr "type" "load1")
   (set (attr "pool_range")
	(if_then_else (eq_attr "is_thumb" "yes")
		      (const_int 1024)
		      (const_int 4096)))
   (set (attr "neg_pool_range")
	(if_then_else (eq_attr "is_thumb" "yes")
		      (const_int 0)
		      (const_int 4084)))]
)

(define_insn "pic_add_dot_plus_four"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(plus:SI (match_operand:SI 1 "register_operand" "0")
			     (const (plus:SI (pc) (const_int 4))))]
		   UNSPEC_PIC_BASE))
   (use (match_operand 2 "" ""))]
  "TARGET_THUMB"
  "*
  (*targetm.asm_out.internal_label) (asm_out_file, \"LPIC\",
				     INTVAL (operands[2]));
  return \"add\\t%0, %|pc\";
  "
  [(set_attr "length" "2")]
)

(define_insn "pic_add_dot_plus_eight"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(plus:SI (match_operand:SI 1 "register_operand" "r")
			     (const (plus:SI (pc) (const_int 8))))]
		   UNSPEC_PIC_BASE))
   (use (match_operand 2 "" ""))]
  "TARGET_ARM"
  "*
    (*targetm.asm_out.internal_label) (asm_out_file, \"LPIC\",
				       INTVAL (operands[2]));
    return \"add%?\\t%0, %|pc, %1\";
  "
  [(set_attr "predicable" "yes")]
)

(define_insn "tls_load_dot_plus_eight"
  [(set (match_operand:SI 0 "register_operand" "+r")
	(mem:SI (unspec:SI [(plus:SI (match_operand:SI 1 "register_operand" "r")
				     (const (plus:SI (pc) (const_int 8))))]
			   UNSPEC_PIC_BASE)))
   (use (match_operand 2 "" ""))]
  "TARGET_ARM"
  "*
    (*targetm.asm_out.internal_label) (asm_out_file, \"LPIC\",
				       INTVAL (operands[2]));
    return \"ldr%?\\t%0, [%|pc, %1]\t\t@ tls_load_dot_plus_eight\";
  "
  [(set_attr "predicable" "yes")]
)

;; PIC references to local variables can generate pic_add_dot_plus_eight
;; followed by a load.  These sequences can be crunched down to
;; tls_load_dot_plus_eight by a peephole.

(define_peephole2
  [(parallel [(set (match_operand:SI 0 "register_operand" "")
		   (unspec:SI [(plus:SI (match_operand:SI 3 "register_operand" "")
			     	 	(const (plus:SI (pc) (const_int 8))))]
			      UNSPEC_PIC_BASE))
   	      (use (label_ref (match_operand 1 "" "")))])
   (set (match_operand:SI 2 "register_operand" "") (mem:SI (match_dup 0)))]
  "TARGET_ARM && peep2_reg_dead_p (2, operands[0])"
  [(parallel [(set (match_dup 2)
		   (mem:SI (unspec:SI [(plus:SI (match_dup 3)
						(const (plus:SI (pc) (const_int 8))))]
				      UNSPEC_PIC_BASE)))
   	      (use (label_ref (match_dup 1)))])]
  ""
)

(define_expand "builtin_setjmp_receiver"
  [(label_ref (match_operand 0 "" ""))]
  "flag_pic"
  "
{
  /* r3 is clobbered by set/longjmp, so we can use it as a scratch
     register.  */
  if (arm_pic_register != INVALID_REGNUM)
    arm_load_pic_register (1UL << 3);
  DONE;
}")

;; If copying one reg to another we can set the condition codes according to
;; its value.  Such a move is common after a return from subroutine and the
;; result is being tested against zero.

(define_insn "*movsi_compare0"
  [(set (reg:CC CC_REGNUM)
	(compare:CC (match_operand:SI 1 "s_register_operand" "0,r")
		    (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=r,r")
	(match_dup 1))]
  "TARGET_ARM"
  "@
   cmp%?\\t%0, #0
   sub%?s\\t%0, %1, #0"
  [(set_attr "conds" "set")]
)

;; Subroutine to store a half word from a register into memory.
;; Operand 0 is the source register (HImode)
;; Operand 1 is the destination address in a register (SImode)

;; In both this routine and the next, we must be careful not to spill
;; a memory address of reg+large_const into a separate PLUS insn, since this
;; can generate unrecognizable rtl.

(define_expand "storehi"
  [;; store the low byte
   (set (match_operand 1 "" "") (match_dup 3))
   ;; extract the high byte
   (set (match_dup 2)
	(ashiftrt:SI (match_operand 0 "" "") (const_int 8)))
   ;; store the high byte
   (set (match_dup 4) (match_dup 5))]
  "TARGET_ARM"
  "
  {
    rtx op1 = operands[1];
    rtx addr = XEXP (op1, 0);
    enum rtx_code code = GET_CODE (addr);

    if ((code == PLUS && GET_CODE (XEXP (addr, 1)) != CONST_INT)
	|| code == MINUS)
      op1 = replace_equiv_address (operands[1], force_reg (SImode, addr));

    operands[4] = adjust_address (op1, QImode, 1);
    operands[1] = adjust_address (operands[1], QImode, 0);
    operands[3] = gen_lowpart (QImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[2] = gen_reg_rtx (SImode);
    operands[5] = gen_lowpart (QImode, operands[2]);
  }"
)

(define_expand "storehi_bigend"
  [(set (match_dup 4) (match_dup 3))
   (set (match_dup 2)
	(ashiftrt:SI (match_operand 0 "" "") (const_int 8)))
   (set (match_operand 1 "" "")	(match_dup 5))]
  "TARGET_ARM"
  "
  {
    rtx op1 = operands[1];
    rtx addr = XEXP (op1, 0);
    enum rtx_code code = GET_CODE (addr);

    if ((code == PLUS && GET_CODE (XEXP (addr, 1)) != CONST_INT)
	|| code == MINUS)
      op1 = replace_equiv_address (op1, force_reg (SImode, addr));

    operands[4] = adjust_address (op1, QImode, 1);
    operands[1] = adjust_address (operands[1], QImode, 0);
    operands[3] = gen_lowpart (QImode, operands[0]);
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[2] = gen_reg_rtx (SImode);
    operands[5] = gen_lowpart (QImode, operands[2]);
  }"
)

;; Subroutine to store a half word integer constant into memory.
(define_expand "storeinthi"
  [(set (match_operand 0 "" "")
	(match_operand 1 "" ""))
   (set (match_dup 3) (match_dup 2))]
  "TARGET_ARM"
  "
  {
    HOST_WIDE_INT value = INTVAL (operands[1]);
    rtx addr = XEXP (operands[0], 0);
    rtx op0 = operands[0];
    enum rtx_code code = GET_CODE (addr);

    if ((code == PLUS && GET_CODE (XEXP (addr, 1)) != CONST_INT)
	|| code == MINUS)
      op0 = replace_equiv_address (op0, force_reg (SImode, addr));

    operands[1] = gen_reg_rtx (SImode);
    if (BYTES_BIG_ENDIAN)
      {
	emit_insn (gen_movsi (operands[1], GEN_INT ((value >> 8) & 255)));
	if ((value & 255) == ((value >> 8) & 255))
	  operands[2] = operands[1];
	else
	  {
	    operands[2] = gen_reg_rtx (SImode);
	    emit_insn (gen_movsi (operands[2], GEN_INT (value & 255)));
	  }
      }
    else
      {
	emit_insn (gen_movsi (operands[1], GEN_INT (value & 255)));
	if ((value & 255) == ((value >> 8) & 255))
	  operands[2] = operands[1];
	else
	  {
	    operands[2] = gen_reg_rtx (SImode);
	    emit_insn (gen_movsi (operands[2], GEN_INT ((value >> 8) & 255)));
	  }
      }

    operands[3] = adjust_address (op0, QImode, 1);
    operands[0] = adjust_address (operands[0], QImode, 0);
    operands[2] = gen_lowpart (QImode, operands[2]);
    operands[1] = gen_lowpart (QImode, operands[1]);
  }"
)

(define_expand "storehi_single_op"
  [(set (match_operand:HI 0 "memory_operand" "")
	(match_operand:HI 1 "general_operand" ""))]
  "TARGET_ARM && arm_arch4"
  "
  if (!s_register_operand (operands[1], HImode))
    operands[1] = copy_to_mode_reg (HImode, operands[1]);
  "
)

(define_expand "movhi"
  [(set (match_operand:HI 0 "general_operand" "")
	(match_operand:HI 1 "general_operand" ""))]
  "TARGET_EITHER"
  "
  if (TARGET_ARM)
    {
      if (!no_new_pseudos)
        {
          if (GET_CODE (operands[0]) == MEM)
	    {
	      if (arm_arch4)
	        {
	          emit_insn (gen_storehi_single_op (operands[0], operands[1]));
	          DONE;
	        }
	      if (GET_CODE (operands[1]) == CONST_INT)
	        emit_insn (gen_storeinthi (operands[0], operands[1]));
	      else
	        {
	          if (GET_CODE (operands[1]) == MEM)
		    operands[1] = force_reg (HImode, operands[1]);
	          if (BYTES_BIG_ENDIAN)
		    emit_insn (gen_storehi_bigend (operands[1], operands[0]));
	          else
		   emit_insn (gen_storehi (operands[1], operands[0]));
	        }
	      DONE;
	    }
          /* Sign extend a constant, and keep it in an SImode reg.  */
          else if (GET_CODE (operands[1]) == CONST_INT)
	    {
	      rtx reg = gen_reg_rtx (SImode);
	      HOST_WIDE_INT val = INTVAL (operands[1]) & 0xffff;

	      /* If the constant is already valid, leave it alone.  */
	      if (!const_ok_for_arm (val))
	        {
	          /* If setting all the top bits will make the constant 
		     loadable in a single instruction, then set them.  
		     Otherwise, sign extend the number.  */

	          if (const_ok_for_arm (~(val | ~0xffff)))
		    val |= ~0xffff;
	          else if (val & 0x8000)
		    val |= ~0xffff;
	        }

	      emit_insn (gen_movsi (reg, GEN_INT (val)));
	      operands[1] = gen_lowpart (HImode, reg);
	    }
	  else if (arm_arch4 && optimize && !no_new_pseudos
		   && GET_CODE (operands[1]) == MEM)
	    {
	      rtx reg = gen_reg_rtx (SImode);

	      emit_insn (gen_zero_extendhisi2 (reg, operands[1]));
	      operands[1] = gen_lowpart (HImode, reg);
	    }
          else if (!arm_arch4)
	    {
	      if (GET_CODE (operands[1]) == MEM)
	        {
		  rtx base;
		  rtx offset = const0_rtx;
		  rtx reg = gen_reg_rtx (SImode);

		  if ((GET_CODE (base = XEXP (operands[1], 0)) == REG
		       || (GET_CODE (base) == PLUS
			   && (GET_CODE (offset = XEXP (base, 1))
			       == CONST_INT)
                           && ((INTVAL(offset) & 1) != 1)
			   && GET_CODE (base = XEXP (base, 0)) == REG))
		      && REGNO_POINTER_ALIGN (REGNO (base)) >= 32)
		    {
		      rtx new;

		      new = widen_memory_access (operands[1], SImode,
						 ((INTVAL (offset) & ~3)
						  - INTVAL (offset)));
		      emit_insn (gen_movsi (reg, new));
		      if (((INTVAL (offset) & 2) != 0)
			  ^ (BYTES_BIG_ENDIAN ? 1 : 0))
			{
			  rtx reg2 = gen_reg_rtx (SImode);

			  emit_insn (gen_lshrsi3 (reg2, reg, GEN_INT (16)));
			  reg = reg2;
			}
		    }
		  else
		    emit_insn (gen_movhi_bytes (reg, operands[1]));

		  operands[1] = gen_lowpart (HImode, reg);
	       }
	   }
        }
      /* Handle loading a large integer during reload.  */
      else if (GET_CODE (operands[1]) == CONST_INT
	       && !const_ok_for_arm (INTVAL (operands[1]))
	       && !const_ok_for_arm (~INTVAL (operands[1])))
        {
          /* Writing a constant to memory needs a scratch, which should
	     be handled with SECONDARY_RELOADs.  */
          gcc_assert (GET_CODE (operands[0]) == REG);

          operands[0] = gen_rtx_SUBREG (SImode, operands[0], 0);
          emit_insn (gen_movsi (operands[0], operands[1]));
          DONE;
       }
    }
  else /* TARGET_THUMB */
    {
      if (!no_new_pseudos)
        {
	  if (GET_CODE (operands[1]) == CONST_INT)
	    {
	      rtx reg = gen_reg_rtx (SImode);

	      emit_insn (gen_movsi (reg, operands[1]));
	      operands[1] = gen_lowpart (HImode, reg);
	    }

          /* ??? We shouldn't really get invalid addresses here, but this can
	     happen if we are passed a SP (never OK for HImode/QImode) or 
	     virtual register (rejected by GO_IF_LEGITIMATE_ADDRESS for 
	     HImode/QImode) relative address.  */
          /* ??? This should perhaps be fixed elsewhere, for instance, in
	     fixup_stack_1, by checking for other kinds of invalid addresses,
	     e.g. a bare reference to a virtual register.  This may confuse the
	     alpha though, which must handle this case differently.  */
          if (GET_CODE (operands[0]) == MEM
	      && !memory_address_p (GET_MODE (operands[0]),
				    XEXP (operands[0], 0)))
	    operands[0]
	      = replace_equiv_address (operands[0],
				       copy_to_reg (XEXP (operands[0], 0)));
   
          if (GET_CODE (operands[1]) == MEM
	      && !memory_address_p (GET_MODE (operands[1]),
				    XEXP (operands[1], 0)))
	    operands[1]
	      = replace_equiv_address (operands[1],
				       copy_to_reg (XEXP (operands[1], 0)));

	  if (GET_CODE (operands[1]) == MEM && optimize > 0)
	    {
	      rtx reg = gen_reg_rtx (SImode);

	      emit_insn (gen_zero_extendhisi2 (reg, operands[1]));
	      operands[1] = gen_lowpart (HImode, reg);
	    }

          if (GET_CODE (operands[0]) == MEM)
	    operands[1] = force_reg (HImode, operands[1]);
        }
      else if (GET_CODE (operands[1]) == CONST_INT
	        && !satisfies_constraint_I (operands[1]))
        {
	  /* Handle loading a large integer during reload.  */

          /* Writing a constant to memory needs a scratch, which should
	     be handled with SECONDARY_RELOADs.  */
          gcc_assert (GET_CODE (operands[0]) == REG);

          operands[0] = gen_rtx_SUBREG (SImode, operands[0], 0);
          emit_insn (gen_movsi (operands[0], operands[1]));
          DONE;
        }
    }
  "
)

(define_insn "*thumb_movhi_insn"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=l,l,m,*r,*h,l")
	(match_operand:HI 1 "general_operand"       "l,m,l,*h,*r,I"))]
  "TARGET_THUMB
   && (   register_operand (operands[0], HImode)
       || register_operand (operands[1], HImode))"
  "*
  switch (which_alternative)
    {
    case 0: return \"add	%0, %1, #0\";
    case 2: return \"strh	%1, %0\";
    case 3: return \"mov	%0, %1\";
    case 4: return \"mov	%0, %1\";
    case 5: return \"mov	%0, %1\";
    default: gcc_unreachable ();
    case 1:
      /* The stack pointer can end up being taken as an index register.
          Catch this case here and deal with it.  */
      if (GET_CODE (XEXP (operands[1], 0)) == PLUS
	  && GET_CODE (XEXP (XEXP (operands[1], 0), 0)) == REG
	  && REGNO    (XEXP (XEXP (operands[1], 0), 0)) == SP_REGNUM)
        {
	  rtx ops[2];
          ops[0] = operands[0];
          ops[1] = XEXP (XEXP (operands[1], 0), 0);
      
          output_asm_insn (\"mov	%0, %1\", ops);

          XEXP (XEXP (operands[1], 0), 0) = operands[0];
    
	}
      return \"ldrh	%0, %1\";
    }"
  [(set_attr "length" "2,4,2,2,2,2")
   (set_attr "type" "*,load1,store1,*,*,*")]
)


(define_expand "movhi_bytes"
  [(set (match_dup 2) (zero_extend:SI (match_operand:HI 1 "" "")))
   (set (match_dup 3)
	(zero_extend:SI (match_dup 6)))
   (set (match_operand:SI 0 "" "")
	 (ior:SI (ashift:SI (match_dup 4) (const_int 8)) (match_dup 5)))]
  "TARGET_ARM"
  "
  {
    rtx mem1, mem2;
    rtx addr = copy_to_mode_reg (SImode, XEXP (operands[1], 0));

    mem1 = change_address (operands[1], QImode, addr);
    mem2 = change_address (operands[1], QImode, plus_constant (addr, 1));
    operands[0] = gen_lowpart (SImode, operands[0]);
    operands[1] = mem1;
    operands[2] = gen_reg_rtx (SImode);
    operands[3] = gen_reg_rtx (SImode);
    operands[6] = mem2;

    if (BYTES_BIG_ENDIAN)
      {
	operands[4] = operands[2];
	operands[5] = operands[3];
      }
    else
      {
	operands[4] = operands[3];
	operands[5] = operands[2];
      }
  }"
)

(define_expand "movhi_bigend"
  [(set (match_dup 2)
	(rotate:SI (subreg:SI (match_operand:HI 1 "memory_operand" "") 0)
		   (const_int 16)))
   (set (match_dup 3)
	(ashiftrt:SI (match_dup 2) (const_int 16)))
   (set (match_operand:HI 0 "s_register_operand" "")
	(match_dup 4))]
  "TARGET_ARM"
  "
  operands[2] = gen_reg_rtx (SImode);
  operands[3] = gen_reg_rtx (SImode);
  operands[4] = gen_lowpart (HImode, operands[3]);
  "
)

;; Pattern to recognize insn generated default case above
(define_insn "*movhi_insn_arch4"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,r,m,r")    
	(match_operand:HI 1 "general_operand"      "rI,K,r,m"))]
  "TARGET_ARM
   && arm_arch4
   && (GET_CODE (operands[1]) != CONST_INT
       || const_ok_for_arm (INTVAL (operands[1]))
       || const_ok_for_arm (~INTVAL (operands[1])))"
  "@
   mov%?\\t%0, %1\\t%@ movhi
   mvn%?\\t%0, #%B1\\t%@ movhi
   str%?h\\t%1, %0\\t%@ movhi
   ldr%?h\\t%0, %1\\t%@ movhi"
  [(set_attr "type" "*,*,store1,load1")
   (set_attr "predicable" "yes")
   (set_attr "pool_range" "*,*,*,256")
   (set_attr "neg_pool_range" "*,*,*,244")]
)

(define_insn "*movhi_bytes"
  [(set (match_operand:HI 0 "s_register_operand" "=r,r")
	(match_operand:HI 1 "arm_rhs_operand"  "rI,K"))]
  "TARGET_ARM"
  "@
   mov%?\\t%0, %1\\t%@ movhi
   mvn%?\\t%0, #%B1\\t%@ movhi"
  [(set_attr "predicable" "yes")]
)

(define_expand "thumb_movhi_clobber"
  [(set (match_operand:HI     0 "memory_operand"   "")
	(match_operand:HI     1 "register_operand" ""))
   (clobber (match_operand:DI 2 "register_operand" ""))]
  "TARGET_THUMB"
  "
  if (strict_memory_address_p (HImode, XEXP (operands[0], 0))
      && REGNO (operands[1]) <= LAST_LO_REGNUM)
    {
      emit_insn (gen_movhi (operands[0], operands[1]));
      DONE;
    }
  /* XXX Fixme, need to handle other cases here as well.  */
  gcc_unreachable ();
  "
)
	
;; We use a DImode scratch because we may occasionally need an additional
;; temporary if the address isn't offsettable -- push_reload doesn't seem
;; to take any notice of the "o" constraints on reload_memory_operand operand.
(define_expand "reload_outhi"
  [(parallel [(match_operand:HI 0 "arm_reload_memory_operand" "=o")
	      (match_operand:HI 1 "s_register_operand"        "r")
	      (match_operand:DI 2 "s_register_operand"        "=&l")])]
  "TARGET_EITHER"
  "if (TARGET_ARM)
     arm_reload_out_hi (operands);
   else
     thumb_reload_out_hi (operands);
  DONE;
  "
)

(define_expand "reload_inhi"
  [(parallel [(match_operand:HI 0 "s_register_operand" "=r")
	      (match_operand:HI 1 "arm_reload_memory_operand" "o")
	      (match_operand:DI 2 "s_register_operand" "=&r")])]
  "TARGET_EITHER"
  "
  if (TARGET_ARM)
    arm_reload_in_hi (operands);
  else
    thumb_reload_out_hi (operands);
  DONE;
")

(define_expand "movqi"
  [(set (match_operand:QI 0 "general_operand" "")
        (match_operand:QI 1 "general_operand" ""))]
  "TARGET_EITHER"
  "
  /* Everything except mem = const or mem = mem can be done easily */

  if (!no_new_pseudos)
    {
      if (GET_CODE (operands[1]) == CONST_INT)
	{
	  rtx reg = gen_reg_rtx (SImode);

	  emit_insn (gen_movsi (reg, operands[1]));
	  operands[1] = gen_lowpart (QImode, reg);
	}

      if (TARGET_THUMB)
	{
          /* ??? We shouldn't really get invalid addresses here, but this can
	     happen if we are passed a SP (never OK for HImode/QImode) or
	     virtual register (rejected by GO_IF_LEGITIMATE_ADDRESS for
	     HImode/QImode) relative address.  */
          /* ??? This should perhaps be fixed elsewhere, for instance, in
	     fixup_stack_1, by checking for other kinds of invalid addresses,
	     e.g. a bare reference to a virtual register.  This may confuse the
	     alpha though, which must handle this case differently.  */
          if (GET_CODE (operands[0]) == MEM
	      && !memory_address_p (GET_MODE (operands[0]),
		  		     XEXP (operands[0], 0)))
	    operands[0]
	      = replace_equiv_address (operands[0],
				       copy_to_reg (XEXP (operands[0], 0)));
          if (GET_CODE (operands[1]) == MEM
	      && !memory_address_p (GET_MODE (operands[1]),
				    XEXP (operands[1], 0)))
	     operands[1]
	       = replace_equiv_address (operands[1],
					copy_to_reg (XEXP (operands[1], 0)));
	}

      if (GET_CODE (operands[1]) == MEM && optimize > 0)
	{
	  rtx reg = gen_reg_rtx (SImode);

	  emit_insn (gen_zero_extendqisi2 (reg, operands[1]));
	  operands[1] = gen_lowpart (QImode, reg);
	}

      if (GET_CODE (operands[0]) == MEM)
	operands[1] = force_reg (QImode, operands[1]);
    }
  else if (TARGET_THUMB
	   && GET_CODE (operands[1]) == CONST_INT
	   && !satisfies_constraint_I (operands[1]))
    {
      /* Handle loading a large integer during reload.  */

      /* Writing a constant to memory needs a scratch, which should
	 be handled with SECONDARY_RELOADs.  */
      gcc_assert (GET_CODE (operands[0]) == REG);

      operands[0] = gen_rtx_SUBREG (SImode, operands[0], 0);
      emit_insn (gen_movsi (operands[0], operands[1]));
      DONE;
    }
  "
)


(define_insn "*arm_movqi_insn"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=r,r,r,m")
	(match_operand:QI 1 "general_operand" "rI,K,m,r"))]
  "TARGET_ARM
   && (   register_operand (operands[0], QImode)
       || register_operand (operands[1], QImode))"
  "@
   mov%?\\t%0, %1
   mvn%?\\t%0, #%B1
   ldr%?b\\t%0, %1
   str%?b\\t%1, %0"
  [(set_attr "type" "*,*,load1,store1")
   (set_attr "predicable" "yes")]
)

(define_insn "*thumb_movqi_insn"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=l,l,m,*r,*h,l")
	(match_operand:QI 1 "general_operand"      "l, m,l,*h,*r,I"))]
  "TARGET_THUMB
   && (   register_operand (operands[0], QImode)
       || register_operand (operands[1], QImode))"
  "@
   add\\t%0, %1, #0
   ldrb\\t%0, %1
   strb\\t%1, %0
   mov\\t%0, %1
   mov\\t%0, %1
   mov\\t%0, %1"
  [(set_attr "length" "2")
   (set_attr "type" "*,load1,store1,*,*,*")
   (set_attr "pool_range" "*,32,*,*,*,*")]
)

(define_expand "movsf"
  [(set (match_operand:SF 0 "general_operand" "")
	(match_operand:SF 1 "general_operand" ""))]
  "TARGET_EITHER"
  "
  if (TARGET_ARM)
    {
      if (GET_CODE (operands[0]) == MEM)
        operands[1] = force_reg (SFmode, operands[1]);
    }
  else /* TARGET_THUMB */
    {
      if (!no_new_pseudos)
        {
           if (GET_CODE (operands[0]) != REG)
	     operands[1] = force_reg (SFmode, operands[1]);
        }
    }
  "
)

;; Transform a floating-point move of a constant into a core register into
;; an SImode operation.
(define_split
  [(set (match_operand:SF 0 "arm_general_register_operand" "")
	(match_operand:SF 1 "immediate_operand" ""))]
  "TARGET_ARM
   && reload_completed
   && GET_CODE (operands[1]) == CONST_DOUBLE"
  [(set (match_dup 2) (match_dup 3))]
  "
  operands[2] = gen_lowpart (SImode, operands[0]);
  operands[3] = gen_lowpart (SImode, operands[1]);
  if (operands[2] == 0 || operands[3] == 0)
    FAIL;
  "
)

(define_insn "*arm_movsf_soft_insn"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=r,r,m")
	(match_operand:SF 1 "general_operand"  "r,mE,r"))]
  "TARGET_ARM
   && TARGET_SOFT_FLOAT
   && (GET_CODE (operands[0]) != MEM
       || register_operand (operands[1], SFmode))"
  "@
   mov%?\\t%0, %1
   ldr%?\\t%0, %1\\t%@ float
   str%?\\t%1, %0\\t%@ float"
  [(set_attr "length" "4,4,4")
   (set_attr "predicable" "yes")
   (set_attr "type" "*,load1,store1")
   (set_attr "pool_range" "*,4096,*")
   (set_attr "neg_pool_range" "*,4084,*")]
)

;;; ??? This should have alternatives for constants.
(define_insn "*thumb_movsf_insn"
  [(set (match_operand:SF     0 "nonimmediate_operand" "=l,l,>,l, m,*r,*h")
	(match_operand:SF     1 "general_operand"      "l, >,l,mF,l,*h,*r"))]
  "TARGET_THUMB
   && (   register_operand (operands[0], SFmode) 
       || register_operand (operands[1], SFmode))"
  "@
   add\\t%0, %1, #0
   ldmia\\t%1, {%0}
   stmia\\t%0, {%1}
   ldr\\t%0, %1
   str\\t%1, %0
   mov\\t%0, %1
   mov\\t%0, %1"
  [(set_attr "length" "2")
   (set_attr "type" "*,load1,store1,load1,store1,*,*")
   (set_attr "pool_range" "*,*,*,1020,*,*,*")]
)

(define_expand "movdf"
  [(set (match_operand:DF 0 "general_operand" "")
	(match_operand:DF 1 "general_operand" ""))]
  "TARGET_EITHER"
  "
  if (TARGET_ARM)
    {
      if (GET_CODE (operands[0]) == MEM)
        operands[1] = force_reg (DFmode, operands[1]);
    }
  else /* TARGET_THUMB */
    {
      if (!no_new_pseudos)
        {
          if (GET_CODE (operands[0]) != REG)
	    operands[1] = force_reg (DFmode, operands[1]);
        }
    }
  "
)

;; Reloading a df mode value stored in integer regs to memory can require a
;; scratch reg.
(define_expand "reload_outdf"
  [(match_operand:DF 0 "arm_reload_memory_operand" "=o")
   (match_operand:DF 1 "s_register_operand" "r")
   (match_operand:SI 2 "s_register_operand" "=&r")]
  "TARGET_ARM"
  "
  {
    enum rtx_code code = GET_CODE (XEXP (operands[0], 0));

    if (code == REG)
      operands[2] = XEXP (operands[0], 0);
    else if (code == POST_INC || code == PRE_DEC)
      {
	operands[0] = gen_rtx_SUBREG (DImode, operands[0], 0);
	operands[1] = gen_rtx_SUBREG (DImode, operands[1], 0);
	emit_insn (gen_movdi (operands[0], operands[1]));
	DONE;
      }
    else if (code == PRE_INC)
      {
	rtx reg = XEXP (XEXP (operands[0], 0), 0);

	emit_insn (gen_addsi3 (reg, reg, GEN_INT (8)));
	operands[2] = reg;
      }
    else if (code == POST_DEC)
      operands[2] = XEXP (XEXP (operands[0], 0), 0);
    else
      emit_insn (gen_addsi3 (operands[2], XEXP (XEXP (operands[0], 0), 0),
			     XEXP (XEXP (operands[0], 0), 1)));

    emit_insn (gen_rtx_SET (VOIDmode,
			    replace_equiv_address (operands[0], operands[2]),
			    operands[1]));

    if (code == POST_DEC)
      emit_insn (gen_addsi3 (operands[2], operands[2], GEN_INT (-8)));

    DONE;
  }"
)

(define_insn "*movdf_soft_insn"
  [(set (match_operand:DF 0 "nonimmediate_soft_df_operand" "=r,r,r,r,m")
	(match_operand:DF 1 "soft_df_operand" "rDa,Db,Dc,mF,r"))]
  "TARGET_ARM && TARGET_SOFT_FLOAT
   && (   register_operand (operands[0], DFmode)
       || register_operand (operands[1], DFmode))"
  "*
  switch (which_alternative)
    {
    case 0:
    case 1:
    case 2:
      return \"#\";
    default:
      return output_move_double (operands);
    }
  "
  [(set_attr "length" "8,12,16,8,8")
   (set_attr "type" "*,*,*,load2,store2")
   (set_attr "pool_range" "1020")
   (set_attr "neg_pool_range" "1008")]
)

;;; ??? This should have alternatives for constants.
;;; ??? This was originally identical to the movdi_insn pattern.
;;; ??? The 'F' constraint looks funny, but it should always be replaced by
;;; thumb_reorg with a memory reference.
(define_insn "*thumb_movdf_insn"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=l,l,>,l, m,*r")
	(match_operand:DF 1 "general_operand"      "l, >,l,mF,l,*r"))]
  "TARGET_THUMB
   && (   register_operand (operands[0], DFmode)
       || register_operand (operands[1], DFmode))"
  "*
  switch (which_alternative)
    {
    default:
    case 0:
      if (REGNO (operands[1]) == REGNO (operands[0]) + 1)
	return \"add\\t%0, %1, #0\;add\\t%H0, %H1, #0\";
      return \"add\\t%H0, %H1, #0\;add\\t%0, %1, #0\";
    case 1:
      return \"ldmia\\t%1, {%0, %H0}\";
    case 2:
      return \"stmia\\t%0, {%1, %H1}\";
    case 3:
      return thumb_load_double_from_address (operands);
    case 4:
      operands[2] = gen_rtx_MEM (SImode,
				 plus_constant (XEXP (operands[0], 0), 4));
      output_asm_insn (\"str\\t%1, %0\;str\\t%H1, %2\", operands);
      return \"\";
    case 5:
      if (REGNO (operands[1]) == REGNO (operands[0]) + 1)
	return \"mov\\t%0, %1\;mov\\t%H0, %H1\";
      return \"mov\\t%H0, %H1\;mov\\t%0, %1\";
    }
  "
  [(set_attr "length" "4,2,2,6,4,4")
   (set_attr "type" "*,load2,store2,load2,store2,*")
   (set_attr "pool_range" "*,*,*,1020,*,*")]
)

(define_expand "movxf"
  [(set (match_operand:XF 0 "general_operand" "")
	(match_operand:XF 1 "general_operand" ""))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "
  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (XFmode, operands[1]);
  "
)

;; Vector Moves
(define_expand "movv2si"
  [(set (match_operand:V2SI 0 "nonimmediate_operand" "")
	(match_operand:V2SI 1 "general_operand" ""))]
  "TARGET_REALLY_IWMMXT"
{
})

(define_expand "movv4hi"
  [(set (match_operand:V4HI 0 "nonimmediate_operand" "")
	(match_operand:V4HI 1 "general_operand" ""))]
  "TARGET_REALLY_IWMMXT"
{
})

(define_expand "movv8qi"
  [(set (match_operand:V8QI 0 "nonimmediate_operand" "")
	(match_operand:V8QI 1 "general_operand" ""))]
  "TARGET_REALLY_IWMMXT"
{
})


;; load- and store-multiple insns
;; The arm can load/store any set of registers, provided that they are in
;; ascending order; but that is beyond GCC so stick with what it knows.

(define_expand "load_multiple"
  [(match_par_dup 3 [(set (match_operand:SI 0 "" "")
                          (match_operand:SI 1 "" ""))
                     (use (match_operand:SI 2 "" ""))])]
  "TARGET_ARM"
{
  HOST_WIDE_INT offset = 0;

  /* Support only fixed point registers.  */
  if (GET_CODE (operands[2]) != CONST_INT
      || INTVAL (operands[2]) > 14
      || INTVAL (operands[2]) < 2
      || GET_CODE (operands[1]) != MEM
      || GET_CODE (operands[0]) != REG
      || REGNO (operands[0]) > (LAST_ARM_REGNUM - 1)
      || REGNO (operands[0]) + INTVAL (operands[2]) > LAST_ARM_REGNUM)
    FAIL;

  operands[3]
    = arm_gen_load_multiple (REGNO (operands[0]), INTVAL (operands[2]),
			     force_reg (SImode, XEXP (operands[1], 0)),
			     TRUE, FALSE, operands[1], &offset);
})

;; Load multiple with write-back

(define_insn "*ldmsi_postinc4"
  [(match_parallel 0 "load_multiple_operation"
    [(set (match_operand:SI 1 "s_register_operand" "=r")
	  (plus:SI (match_operand:SI 2 "s_register_operand" "1")
		   (const_int 16)))
     (set (match_operand:SI 3 "arm_hard_register_operand" "")
	  (mem:SI (match_dup 2)))
     (set (match_operand:SI 4 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 2) (const_int 4))))
     (set (match_operand:SI 5 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 2) (const_int 8))))
     (set (match_operand:SI 6 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 2) (const_int 12))))])]
  "TARGET_ARM && XVECLEN (operands[0], 0) == 5"
  "ldm%?ia\\t%1!, {%3, %4, %5, %6}"
  [(set_attr "type" "load4")
   (set_attr "predicable" "yes")]
)

(define_insn "*ldmsi_postinc4_thumb"
  [(match_parallel 0 "load_multiple_operation"
    [(set (match_operand:SI 1 "s_register_operand" "=l")
	  (plus:SI (match_operand:SI 2 "s_register_operand" "1")
		   (const_int 16)))
     (set (match_operand:SI 3 "arm_hard_register_operand" "")
	  (mem:SI (match_dup 2)))
     (set (match_operand:SI 4 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 2) (const_int 4))))
     (set (match_operand:SI 5 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 2) (const_int 8))))
     (set (match_operand:SI 6 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 2) (const_int 12))))])]
  "TARGET_THUMB && XVECLEN (operands[0], 0) == 5"
  "ldmia\\t%1!, {%3, %4, %5, %6}"
  [(set_attr "type" "load4")]
)

(define_insn "*ldmsi_postinc3"
  [(match_parallel 0 "load_multiple_operation"
    [(set (match_operand:SI 1 "s_register_operand" "=r")
	  (plus:SI (match_operand:SI 2 "s_register_operand" "1")
		   (const_int 12)))
     (set (match_operand:SI 3 "arm_hard_register_operand" "")
	  (mem:SI (match_dup 2)))
     (set (match_operand:SI 4 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 2) (const_int 4))))
     (set (match_operand:SI 5 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 2) (const_int 8))))])]
  "TARGET_ARM && XVECLEN (operands[0], 0) == 4"
  "ldm%?ia\\t%1!, {%3, %4, %5}"
  [(set_attr "type" "load3")
   (set_attr "predicable" "yes")]
)

(define_insn "*ldmsi_postinc2"
  [(match_parallel 0 "load_multiple_operation"
    [(set (match_operand:SI 1 "s_register_operand" "=r")
	  (plus:SI (match_operand:SI 2 "s_register_operand" "1")
		   (const_int 8)))
     (set (match_operand:SI 3 "arm_hard_register_operand" "")
	  (mem:SI (match_dup 2)))
     (set (match_operand:SI 4 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 2) (const_int 4))))])]
  "TARGET_ARM && XVECLEN (operands[0], 0) == 3"
  "ldm%?ia\\t%1!, {%3, %4}"
  [(set_attr "type" "load2")
   (set_attr "predicable" "yes")]
)

;; Ordinary load multiple

(define_insn "*ldmsi4"
  [(match_parallel 0 "load_multiple_operation"
    [(set (match_operand:SI 2 "arm_hard_register_operand" "")
	  (mem:SI (match_operand:SI 1 "s_register_operand" "r")))
     (set (match_operand:SI 3 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 1) (const_int 4))))
     (set (match_operand:SI 4 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 1) (const_int 8))))
     (set (match_operand:SI 5 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 1) (const_int 12))))])]
  "TARGET_ARM && XVECLEN (operands[0], 0) == 4"
  "ldm%?ia\\t%1, {%2, %3, %4, %5}"
  [(set_attr "type" "load4")
   (set_attr "predicable" "yes")]
)

(define_insn "*ldmsi3"
  [(match_parallel 0 "load_multiple_operation"
    [(set (match_operand:SI 2 "arm_hard_register_operand" "")
	  (mem:SI (match_operand:SI 1 "s_register_operand" "r")))
     (set (match_operand:SI 3 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 1) (const_int 4))))
     (set (match_operand:SI 4 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 1) (const_int 8))))])]
  "TARGET_ARM && XVECLEN (operands[0], 0) == 3"
  "ldm%?ia\\t%1, {%2, %3, %4}"
  [(set_attr "type" "load3")
   (set_attr "predicable" "yes")]
)

(define_insn "*ldmsi2"
  [(match_parallel 0 "load_multiple_operation"
    [(set (match_operand:SI 2 "arm_hard_register_operand" "")
	  (mem:SI (match_operand:SI 1 "s_register_operand" "r")))
     (set (match_operand:SI 3 "arm_hard_register_operand" "")
	  (mem:SI (plus:SI (match_dup 1) (const_int 4))))])]
  "TARGET_ARM && XVECLEN (operands[0], 0) == 2"
  "ldm%?ia\\t%1, {%2, %3}"
  [(set_attr "type" "load2")
   (set_attr "predicable" "yes")]
)

(define_expand "store_multiple"
  [(match_par_dup 3 [(set (match_operand:SI 0 "" "")
                          (match_operand:SI 1 "" ""))
                     (use (match_operand:SI 2 "" ""))])]
  "TARGET_ARM"
{
  HOST_WIDE_INT offset = 0;

  /* Support only fixed point registers.  */
  if (GET_CODE (operands[2]) != CONST_INT
      || INTVAL (operands[2]) > 14
      || INTVAL (operands[2]) < 2
      || GET_CODE (operands[1]) != REG
      || GET_CODE (operands[0]) != MEM
      || REGNO (operands[1]) > (LAST_ARM_REGNUM - 1)
      || REGNO (operands[1]) + INTVAL (operands[2]) > LAST_ARM_REGNUM)
    FAIL;

  operands[3]
    = arm_gen_store_multiple (REGNO (operands[1]), INTVAL (operands[2]),
			      force_reg (SImode, XEXP (operands[0], 0)),
			      TRUE, FALSE, operands[0], &offset);
})

;; Store multiple with write-back

(define_insn "*stmsi_postinc4"
  [(match_parallel 0 "store_multiple_operation"
    [(set (match_operand:SI 1 "s_register_operand" "=r")
	  (plus:SI (match_operand:SI 2 "s_register_operand" "1")
		   (const_int 16)))
     (set (mem:SI (match_dup 2))
	  (match_operand:SI 3 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 2) (const_int 4)))
	  (match_operand:SI 4 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 2) (const_int 8)))
	  (match_operand:SI 5 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 2) (const_int 12)))
	  (match_operand:SI 6 "arm_hard_register_operand" ""))])]
  "TARGET_ARM && XVECLEN (operands[0], 0) == 5"
  "stm%?ia\\t%1!, {%3, %4, %5, %6}"
  [(set_attr "predicable" "yes")
   (set_attr "type" "store4")]
)

(define_insn "*stmsi_postinc4_thumb"
  [(match_parallel 0 "store_multiple_operation"
    [(set (match_operand:SI 1 "s_register_operand" "=l")
	  (plus:SI (match_operand:SI 2 "s_register_operand" "1")
		   (const_int 16)))
     (set (mem:SI (match_dup 2))
	  (match_operand:SI 3 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 2) (const_int 4)))
	  (match_operand:SI 4 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 2) (const_int 8)))
	  (match_operand:SI 5 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 2) (const_int 12)))
	  (match_operand:SI 6 "arm_hard_register_operand" ""))])]
  "TARGET_THUMB && XVECLEN (operands[0], 0) == 5"
  "stmia\\t%1!, {%3, %4, %5, %6}"
  [(set_attr "type" "store4")]
)

(define_insn "*stmsi_postinc3"
  [(match_parallel 0 "store_multiple_operation"
    [(set (match_operand:SI 1 "s_register_operand" "=r")
	  (plus:SI (match_operand:SI 2 "s_register_operand" "1")
		   (const_int 12)))
     (set (mem:SI (match_dup 2))
	  (match_operand:SI 3 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 2) (const_int 4)))
	  (match_operand:SI 4 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 2) (const_int 8)))
	  (match_operand:SI 5 "arm_hard_register_operand" ""))])]
  "TARGET_ARM && XVECLEN (operands[0], 0) == 4"
  "stm%?ia\\t%1!, {%3, %4, %5}"
  [(set_attr "predicable" "yes")
   (set_attr "type" "store3")]
)

(define_insn "*stmsi_postinc2"
  [(match_parallel 0 "store_multiple_operation"
    [(set (match_operand:SI 1 "s_register_operand" "=r")
	  (plus:SI (match_operand:SI 2 "s_register_operand" "1")
		   (const_int 8)))
     (set (mem:SI (match_dup 2))
	  (match_operand:SI 3 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 2) (const_int 4)))
	  (match_operand:SI 4 "arm_hard_register_operand" ""))])]
  "TARGET_ARM && XVECLEN (operands[0], 0) == 3"
  "stm%?ia\\t%1!, {%3, %4}"
  [(set_attr "predicable" "yes")
   (set_attr "type" "store2")]
)

;; Ordinary store multiple

(define_insn "*stmsi4"
  [(match_parallel 0 "store_multiple_operation"
    [(set (mem:SI (match_operand:SI 1 "s_register_operand" "r"))
	  (match_operand:SI 2 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 1) (const_int 4)))
	  (match_operand:SI 3 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 1) (const_int 8)))
	  (match_operand:SI 4 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 1) (const_int 12)))
	  (match_operand:SI 5 "arm_hard_register_operand" ""))])]
  "TARGET_ARM && XVECLEN (operands[0], 0) == 4"
  "stm%?ia\\t%1, {%2, %3, %4, %5}"
  [(set_attr "predicable" "yes")
   (set_attr "type" "store4")]
)

(define_insn "*stmsi3"
  [(match_parallel 0 "store_multiple_operation"
    [(set (mem:SI (match_operand:SI 1 "s_register_operand" "r"))
	  (match_operand:SI 2 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 1) (const_int 4)))
	  (match_operand:SI 3 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 1) (const_int 8)))
	  (match_operand:SI 4 "arm_hard_register_operand" ""))])]
  "TARGET_ARM && XVECLEN (operands[0], 0) == 3"
  "stm%?ia\\t%1, {%2, %3, %4}"
  [(set_attr "predicable" "yes")
   (set_attr "type" "store3")]
)

(define_insn "*stmsi2"
  [(match_parallel 0 "store_multiple_operation"
    [(set (mem:SI (match_operand:SI 1 "s_register_operand" "r"))
	  (match_operand:SI 2 "arm_hard_register_operand" ""))
     (set (mem:SI (plus:SI (match_dup 1) (const_int 4)))
	  (match_operand:SI 3 "arm_hard_register_operand" ""))])]
  "TARGET_ARM && XVECLEN (operands[0], 0) == 2"
  "stm%?ia\\t%1, {%2, %3}"
  [(set_attr "predicable" "yes")
   (set_attr "type" "store2")]
)

;; Move a block of memory if it is word aligned and MORE than 2 words long.
;; We could let this apply for blocks of less than this, but it clobbers so
;; many registers that there is then probably a better way.

(define_expand "movmemqi"
  [(match_operand:BLK 0 "general_operand" "")
   (match_operand:BLK 1 "general_operand" "")
   (match_operand:SI 2 "const_int_operand" "")
   (match_operand:SI 3 "const_int_operand" "")]
  "TARGET_EITHER"
  "
  if (TARGET_ARM)
    {
      if (arm_gen_movmemqi (operands))
        DONE;
      FAIL;
    }
  else /* TARGET_THUMB */
    {
      if (   INTVAL (operands[3]) != 4
          || INTVAL (operands[2]) > 48)
        FAIL;

      thumb_expand_movmemqi (operands);
      DONE;
    }
  "
)

;; Thumb block-move insns

(define_insn "movmem12b"
  [(set (mem:SI (match_operand:SI 2 "register_operand" "0"))
	(mem:SI (match_operand:SI 3 "register_operand" "1")))
   (set (mem:SI (plus:SI (match_dup 2) (const_int 4)))
	(mem:SI (plus:SI (match_dup 3) (const_int 4))))
   (set (mem:SI (plus:SI (match_dup 2) (const_int 8)))
	(mem:SI (plus:SI (match_dup 3) (const_int 8))))
   (set (match_operand:SI 0 "register_operand" "=l")
	(plus:SI (match_dup 2) (const_int 12)))
   (set (match_operand:SI 1 "register_operand" "=l")
	(plus:SI (match_dup 3) (const_int 12)))
   (clobber (match_scratch:SI 4 "=&l"))
   (clobber (match_scratch:SI 5 "=&l"))
   (clobber (match_scratch:SI 6 "=&l"))]
  "TARGET_THUMB"
  "* return thumb_output_move_mem_multiple (3, operands);"
  [(set_attr "length" "4")
   ; This isn't entirely accurate...  It loads as well, but in terms of
   ; scheduling the following insn it is better to consider it as a store
   (set_attr "type" "store3")]
)

(define_insn "movmem8b"
  [(set (mem:SI (match_operand:SI 2 "register_operand" "0"))
	(mem:SI (match_operand:SI 3 "register_operand" "1")))
   (set (mem:SI (plus:SI (match_dup 2) (const_int 4)))
	(mem:SI (plus:SI (match_dup 3) (const_int 4))))
   (set (match_operand:SI 0 "register_operand" "=l")
	(plus:SI (match_dup 2) (const_int 8)))
   (set (match_operand:SI 1 "register_operand" "=l")
	(plus:SI (match_dup 3) (const_int 8)))
   (clobber (match_scratch:SI 4 "=&l"))
   (clobber (match_scratch:SI 5 "=&l"))]
  "TARGET_THUMB"
  "* return thumb_output_move_mem_multiple (2, operands);"
  [(set_attr "length" "4")
   ; This isn't entirely accurate...  It loads as well, but in terms of
   ; scheduling the following insn it is better to consider it as a store
   (set_attr "type" "store2")]
)



;; Compare & branch insns
;; The range calculations are based as follows:
;; For forward branches, the address calculation returns the address of
;; the next instruction.  This is 2 beyond the branch instruction.
;; For backward branches, the address calculation returns the address of
;; the first instruction in this pattern (cmp).  This is 2 before the branch
;; instruction for the shortest sequence, and 4 before the branch instruction
;; if we have to jump around an unconditional branch.
;; To the basic branch range the PC offset must be added (this is +4).
;; So for forward branches we have 
;;   (pos_range - pos_base_offs + pc_offs) = (pos_range - 2 + 4).
;; And for backward branches we have 
;;   (neg_range - neg_base_offs + pc_offs) = (neg_range - (-2 or -4) + 4).
;;
;; For a 'b'       pos_range = 2046, neg_range = -2048 giving (-2040->2048).
;; For a 'b<cond>' pos_range = 254,  neg_range = -256  giving (-250 ->256).

(define_expand "cbranchsi4"
  [(set (pc) (if_then_else
	      (match_operator 0 "arm_comparison_operator"
	       [(match_operand:SI 1 "s_register_operand" "")
	        (match_operand:SI 2 "nonmemory_operand" "")])
	      (label_ref (match_operand 3 "" ""))
	      (pc)))]
  "TARGET_THUMB"
  "
  if (thumb_cmpneg_operand (operands[2], SImode))
    {
      emit_jump_insn (gen_cbranchsi4_scratch (NULL, operands[1], operands[2],
					      operands[3], operands[0]));
      DONE;
    }
  if (!thumb_cmp_operand (operands[2], SImode))
    operands[2] = force_reg (SImode, operands[2]);
  ")

(define_insn "*cbranchsi4_insn"
  [(set (pc) (if_then_else
	      (match_operator 0 "arm_comparison_operator"
	       [(match_operand:SI 1 "s_register_operand" "l,*h")
	        (match_operand:SI 2 "thumb_cmp_operand" "lI*h,*r")])
	      (label_ref (match_operand 3 "" ""))
	      (pc)))]
  "TARGET_THUMB"
  "*
  output_asm_insn (\"cmp\\t%1, %2\", operands);

  switch (get_attr_length (insn))
    {
    case 4:  return \"b%d0\\t%l3\";
    case 6:  return \"b%D0\\t.LCB%=\;b\\t%l3\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D0\\t.LCB%=\;bl\\t%l3\\t%@far jump\\n.LCB%=:\";
    }
  "
  [(set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "8")
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length") 
        (if_then_else
	    (and (ge (minus (match_dup 3) (pc)) (const_int -250))
	         (le (minus (match_dup 3) (pc)) (const_int 256)))
	    (const_int 4)
	    (if_then_else
	        (and (ge (minus (match_dup 3) (pc)) (const_int -2040))
		     (le (minus (match_dup 3) (pc)) (const_int 2048)))
		(const_int 6)
		(const_int 8))))]
)

(define_insn "cbranchsi4_scratch"
  [(set (pc) (if_then_else
	      (match_operator 4 "arm_comparison_operator"
	       [(match_operand:SI 1 "s_register_operand" "l,0")
	        (match_operand:SI 2 "thumb_cmpneg_operand" "L,J")])
	      (label_ref (match_operand 3 "" ""))
	      (pc)))
   (clobber (match_scratch:SI 0 "=l,l"))]
  "TARGET_THUMB"
  "*
  output_asm_insn (\"add\\t%0, %1, #%n2\", operands);

  switch (get_attr_length (insn))
    {
    case 4:  return \"b%d4\\t%l3\";
    case 6:  return \"b%D4\\t.LCB%=\;b\\t%l3\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D4\\t.LCB%=\;bl\\t%l3\\t%@far jump\\n.LCB%=:\";
    }
  "
  [(set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "8")
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length") 
        (if_then_else
	    (and (ge (minus (match_dup 3) (pc)) (const_int -250))
	         (le (minus (match_dup 3) (pc)) (const_int 256)))
	    (const_int 4)
	    (if_then_else
	        (and (ge (minus (match_dup 3) (pc)) (const_int -2040))
		     (le (minus (match_dup 3) (pc)) (const_int 2048)))
		(const_int 6)
		(const_int 8))))]
)
(define_insn "*movsi_cbranchsi4"
  [(set (pc)
	(if_then_else
	 (match_operator 3 "arm_comparison_operator"
	  [(match_operand:SI 1 "s_register_operand" "0,l,l,l")
	   (const_int 0)])
	 (label_ref (match_operand 2 "" ""))
	 (pc)))
   (set (match_operand:SI 0 "thumb_cbrch_target_operand" "=l,l,*h,*m")
	(match_dup 1))]
  "TARGET_THUMB"
  "*{
  if (which_alternative == 0)
    output_asm_insn (\"cmp\t%0, #0\", operands);
  else if (which_alternative == 1)
    output_asm_insn (\"sub\t%0, %1, #0\", operands);
  else
    {
      output_asm_insn (\"cmp\t%1, #0\", operands);
      if (which_alternative == 2)
	output_asm_insn (\"mov\t%0, %1\", operands);
      else
	output_asm_insn (\"str\t%1, %0\", operands);
    }
  switch (get_attr_length (insn) - ((which_alternative > 1) ? 2 : 0))
    {
    case 4:  return \"b%d3\\t%l2\";
    case 6:  return \"b%D3\\t.LCB%=\;b\\t%l2\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D3\\t.LCB%=\;bl\\t%l2\\t%@far jump\\n.LCB%=:\";
    }
  }"
  [(set (attr "far_jump")
        (if_then_else
	    (ior (and (gt (symbol_ref ("which_alternative"))
	                  (const_int 1))
		      (eq_attr "length" "8"))
		 (eq_attr "length" "10"))
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length")
     (if_then_else
       (le (symbol_ref ("which_alternative"))
		       (const_int 1))
       (if_then_else
	 (and (ge (minus (match_dup 2) (pc)) (const_int -250))
	      (le (minus (match_dup 2) (pc)) (const_int 256)))
	 (const_int 4)
	 (if_then_else
	   (and (ge (minus (match_dup 2) (pc)) (const_int -2040))
		(le (minus (match_dup 2) (pc)) (const_int 2048)))
	   (const_int 6)
	   (const_int 8)))
       (if_then_else
	 (and (ge (minus (match_dup 2) (pc)) (const_int -248))
	      (le (minus (match_dup 2) (pc)) (const_int 256)))
	 (const_int 6)
	 (if_then_else
	   (and (ge (minus (match_dup 2) (pc)) (const_int -2038))
		(le (minus (match_dup 2) (pc)) (const_int 2048)))
	   (const_int 8)
	   (const_int 10)))))]
)

(define_insn "*negated_cbranchsi4"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "equality_operator"
	  [(match_operand:SI 1 "s_register_operand" "l")
	   (neg:SI (match_operand:SI 2 "s_register_operand" "l"))])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))]
  "TARGET_THUMB"
  "*
  output_asm_insn (\"cmn\\t%1, %2\", operands);
  switch (get_attr_length (insn))
    {
    case 4:  return \"b%d0\\t%l3\";
    case 6:  return \"b%D0\\t.LCB%=\;b\\t%l3\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D0\\t.LCB%=\;bl\\t%l3\\t%@far jump\\n.LCB%=:\";
    }
  "
  [(set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "8")
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length") 
        (if_then_else
	    (and (ge (minus (match_dup 3) (pc)) (const_int -250))
	         (le (minus (match_dup 3) (pc)) (const_int 256)))
	    (const_int 4)
	    (if_then_else
	        (and (ge (minus (match_dup 3) (pc)) (const_int -2040))
		     (le (minus (match_dup 3) (pc)) (const_int 2048)))
		(const_int 6)
		(const_int 8))))]
)

(define_insn "*tbit_cbranch"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "equality_operator"
	  [(zero_extract:SI (match_operand:SI 1 "s_register_operand" "l")
			    (const_int 1)
			    (match_operand:SI 2 "const_int_operand" "i"))
	   (const_int 0)])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))
   (clobber (match_scratch:SI 4 "=l"))]
  "TARGET_THUMB"
  "*
  {
  rtx op[3];
  op[0] = operands[4];
  op[1] = operands[1];
  op[2] = GEN_INT (32 - 1 - INTVAL (operands[2]));

  output_asm_insn (\"lsl\\t%0, %1, %2\", op);
  switch (get_attr_length (insn))
    {
    case 4:  return \"b%d0\\t%l3\";
    case 6:  return \"b%D0\\t.LCB%=\;b\\t%l3\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D0\\t.LCB%=\;bl\\t%l3\\t%@far jump\\n.LCB%=:\";
    }
  }"
  [(set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "8")
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length") 
        (if_then_else
	    (and (ge (minus (match_dup 3) (pc)) (const_int -250))
	         (le (minus (match_dup 3) (pc)) (const_int 256)))
	    (const_int 4)
	    (if_then_else
	        (and (ge (minus (match_dup 3) (pc)) (const_int -2040))
		     (le (minus (match_dup 3) (pc)) (const_int 2048)))
		(const_int 6)
		(const_int 8))))]
)
  
(define_insn "*tlobits_cbranch"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "equality_operator"
	  [(zero_extract:SI (match_operand:SI 1 "s_register_operand" "l")
			    (match_operand:SI 2 "const_int_operand" "i")
			    (const_int 0))
	   (const_int 0)])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))
   (clobber (match_scratch:SI 4 "=l"))]
  "TARGET_THUMB"
  "*
  {
  rtx op[3];
  op[0] = operands[4];
  op[1] = operands[1];
  op[2] = GEN_INT (32 - INTVAL (operands[2]));

  output_asm_insn (\"lsl\\t%0, %1, %2\", op);
  switch (get_attr_length (insn))
    {
    case 4:  return \"b%d0\\t%l3\";
    case 6:  return \"b%D0\\t.LCB%=\;b\\t%l3\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D0\\t.LCB%=\;bl\\t%l3\\t%@far jump\\n.LCB%=:\";
    }
  }"
  [(set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "8")
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length") 
        (if_then_else
	    (and (ge (minus (match_dup 3) (pc)) (const_int -250))
	         (le (minus (match_dup 3) (pc)) (const_int 256)))
	    (const_int 4)
	    (if_then_else
	        (and (ge (minus (match_dup 3) (pc)) (const_int -2040))
		     (le (minus (match_dup 3) (pc)) (const_int 2048)))
		(const_int 6)
		(const_int 8))))]
)
  
(define_insn "*tstsi3_cbranch"
  [(set (pc)
	(if_then_else
	 (match_operator 3 "equality_operator"
	  [(and:SI (match_operand:SI 0 "s_register_operand" "%l")
		   (match_operand:SI 1 "s_register_operand" "l"))
	   (const_int 0)])
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  "TARGET_THUMB"
  "*
  {
  output_asm_insn (\"tst\\t%0, %1\", operands);
  switch (get_attr_length (insn))
    {
    case 4:  return \"b%d3\\t%l2\";
    case 6:  return \"b%D3\\t.LCB%=\;b\\t%l2\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D3\\t.LCB%=\;bl\\t%l2\\t%@far jump\\n.LCB%=:\";
    }
  }"
  [(set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "8")
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length") 
        (if_then_else
	    (and (ge (minus (match_dup 2) (pc)) (const_int -250))
	         (le (minus (match_dup 2) (pc)) (const_int 256)))
	    (const_int 4)
	    (if_then_else
	        (and (ge (minus (match_dup 2) (pc)) (const_int -2040))
		     (le (minus (match_dup 2) (pc)) (const_int 2048)))
		(const_int 6)
		(const_int 8))))]
)
  
(define_insn "*andsi3_cbranch"
  [(set (pc)
	(if_then_else
	 (match_operator 5 "equality_operator"
	  [(and:SI (match_operand:SI 2 "s_register_operand" "%0,1,1,1")
		   (match_operand:SI 3 "s_register_operand" "l,l,l,l"))
	   (const_int 0)])
	 (label_ref (match_operand 4 "" ""))
	 (pc)))
   (set (match_operand:SI 0 "thumb_cbrch_target_operand" "=l,*?h,*?m,*?m")
	(and:SI (match_dup 2) (match_dup 3)))
   (clobber (match_scratch:SI 1 "=X,l,&l,&l"))]
  "TARGET_THUMB"
  "*
  {
  if (which_alternative == 0)
    output_asm_insn (\"and\\t%0, %3\", operands);
  else if (which_alternative == 1)
    {
      output_asm_insn (\"and\\t%1, %3\", operands);
      output_asm_insn (\"mov\\t%0, %1\", operands);
    }
  else
    {
      output_asm_insn (\"and\\t%1, %3\", operands);
      output_asm_insn (\"str\\t%1, %0\", operands);
    }

  switch (get_attr_length (insn) - (which_alternative ? 2 : 0))
    {
    case 4:  return \"b%d5\\t%l4\";
    case 6:  return \"b%D5\\t.LCB%=\;b\\t%l4\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D5\\t.LCB%=\;bl\\t%l4\\t%@far jump\\n.LCB%=:\";
    }
  }"
  [(set (attr "far_jump")
        (if_then_else
	    (ior (and (eq (symbol_ref ("which_alternative"))
	                  (const_int 0))
		      (eq_attr "length" "8"))
		 (eq_attr "length" "10"))
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length")
     (if_then_else
       (eq (symbol_ref ("which_alternative"))
		       (const_int 0))
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -250))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 4)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2040))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 6)
	   (const_int 8)))
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -248))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 6)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2038))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 8)
	   (const_int 10)))))]
)

(define_insn "*orrsi3_cbranch_scratch"
  [(set (pc)
	(if_then_else
	 (match_operator 4 "equality_operator"
	  [(ior:SI (match_operand:SI 1 "s_register_operand" "%0")
		   (match_operand:SI 2 "s_register_operand" "l"))
	   (const_int 0)])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))
   (clobber (match_scratch:SI 0 "=l"))]
  "TARGET_THUMB"
  "*
  {
  output_asm_insn (\"orr\\t%0, %2\", operands);
  switch (get_attr_length (insn))
    {
    case 4:  return \"b%d4\\t%l3\";
    case 6:  return \"b%D4\\t.LCB%=\;b\\t%l3\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D4\\t.LCB%=\;bl\\t%l3\\t%@far jump\\n.LCB%=:\";
    }
  }"
  [(set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "8")
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length") 
        (if_then_else
	    (and (ge (minus (match_dup 3) (pc)) (const_int -250))
	         (le (minus (match_dup 3) (pc)) (const_int 256)))
	    (const_int 4)
	    (if_then_else
	        (and (ge (minus (match_dup 3) (pc)) (const_int -2040))
		     (le (minus (match_dup 3) (pc)) (const_int 2048)))
		(const_int 6)
		(const_int 8))))]
)
  
(define_insn "*orrsi3_cbranch"
  [(set (pc)
	(if_then_else
	 (match_operator 5 "equality_operator"
	  [(ior:SI (match_operand:SI 2 "s_register_operand" "%0,1,1,1")
		   (match_operand:SI 3 "s_register_operand" "l,l,l,l"))
	   (const_int 0)])
	 (label_ref (match_operand 4 "" ""))
	 (pc)))
   (set (match_operand:SI 0 "thumb_cbrch_target_operand" "=l,*?h,*?m,*?m")
	(ior:SI (match_dup 2) (match_dup 3)))
   (clobber (match_scratch:SI 1 "=X,l,&l,&l"))]
  "TARGET_THUMB"
  "*
  {
  if (which_alternative == 0)
    output_asm_insn (\"orr\\t%0, %3\", operands);
  else if (which_alternative == 1)
    {
      output_asm_insn (\"orr\\t%1, %3\", operands);
      output_asm_insn (\"mov\\t%0, %1\", operands);
    }
  else
    {
      output_asm_insn (\"orr\\t%1, %3\", operands);
      output_asm_insn (\"str\\t%1, %0\", operands);
    }

  switch (get_attr_length (insn) - (which_alternative ? 2 : 0))
    {
    case 4:  return \"b%d5\\t%l4\";
    case 6:  return \"b%D5\\t.LCB%=\;b\\t%l4\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D5\\t.LCB%=\;bl\\t%l4\\t%@far jump\\n.LCB%=:\";
    }
  }"
  [(set (attr "far_jump")
        (if_then_else
	    (ior (and (eq (symbol_ref ("which_alternative"))
	                  (const_int 0))
		      (eq_attr "length" "8"))
		 (eq_attr "length" "10"))
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length")
     (if_then_else
       (eq (symbol_ref ("which_alternative"))
		       (const_int 0))
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -250))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 4)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2040))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 6)
	   (const_int 8)))
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -248))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 6)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2038))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 8)
	   (const_int 10)))))]
)

(define_insn "*xorsi3_cbranch_scratch"
  [(set (pc)
	(if_then_else
	 (match_operator 4 "equality_operator"
	  [(xor:SI (match_operand:SI 1 "s_register_operand" "%0")
		   (match_operand:SI 2 "s_register_operand" "l"))
	   (const_int 0)])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))
   (clobber (match_scratch:SI 0 "=l"))]
  "TARGET_THUMB"
  "*
  {
  output_asm_insn (\"eor\\t%0, %2\", operands);
  switch (get_attr_length (insn))
    {
    case 4:  return \"b%d4\\t%l3\";
    case 6:  return \"b%D4\\t.LCB%=\;b\\t%l3\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D4\\t.LCB%=\;bl\\t%l3\\t%@far jump\\n.LCB%=:\";
    }
  }"
  [(set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "8")
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length") 
        (if_then_else
	    (and (ge (minus (match_dup 3) (pc)) (const_int -250))
	         (le (minus (match_dup 3) (pc)) (const_int 256)))
	    (const_int 4)
	    (if_then_else
	        (and (ge (minus (match_dup 3) (pc)) (const_int -2040))
		     (le (minus (match_dup 3) (pc)) (const_int 2048)))
		(const_int 6)
		(const_int 8))))]
)
  
(define_insn "*xorsi3_cbranch"
  [(set (pc)
	(if_then_else
	 (match_operator 5 "equality_operator"
	  [(xor:SI (match_operand:SI 2 "s_register_operand" "%0,1,1,1")
		   (match_operand:SI 3 "s_register_operand" "l,l,l,l"))
	   (const_int 0)])
	 (label_ref (match_operand 4 "" ""))
	 (pc)))
   (set (match_operand:SI 0 "thumb_cbrch_target_operand" "=l,*?h,*?m,*?m")
	(xor:SI (match_dup 2) (match_dup 3)))
   (clobber (match_scratch:SI 1 "=X,l,&l,&l"))]
  "TARGET_THUMB"
  "*
  {
  if (which_alternative == 0)
    output_asm_insn (\"eor\\t%0, %3\", operands);
  else if (which_alternative == 1)
    {
      output_asm_insn (\"eor\\t%1, %3\", operands);
      output_asm_insn (\"mov\\t%0, %1\", operands);
    }
  else
    {
      output_asm_insn (\"eor\\t%1, %3\", operands);
      output_asm_insn (\"str\\t%1, %0\", operands);
    }

  switch (get_attr_length (insn) - (which_alternative ? 2 : 0))
    {
    case 4:  return \"b%d5\\t%l4\";
    case 6:  return \"b%D5\\t.LCB%=\;b\\t%l4\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D5\\t.LCB%=\;bl\\t%l4\\t%@far jump\\n.LCB%=:\";
    }
  }"
  [(set (attr "far_jump")
        (if_then_else
	    (ior (and (eq (symbol_ref ("which_alternative"))
	                  (const_int 0))
		      (eq_attr "length" "8"))
		 (eq_attr "length" "10"))
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length")
     (if_then_else
       (eq (symbol_ref ("which_alternative"))
		       (const_int 0))
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -250))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 4)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2040))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 6)
	   (const_int 8)))
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -248))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 6)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2038))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 8)
	   (const_int 10)))))]
)

(define_insn "*bicsi3_cbranch_scratch"
  [(set (pc)
	(if_then_else
	 (match_operator 4 "equality_operator"
	  [(and:SI (not:SI (match_operand:SI 2 "s_register_operand" "l"))
		   (match_operand:SI 1 "s_register_operand" "0"))
	   (const_int 0)])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))
   (clobber (match_scratch:SI 0 "=l"))]
  "TARGET_THUMB"
  "*
  {
  output_asm_insn (\"bic\\t%0, %2\", operands);
  switch (get_attr_length (insn))
    {
    case 4:  return \"b%d4\\t%l3\";
    case 6:  return \"b%D4\\t.LCB%=\;b\\t%l3\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D4\\t.LCB%=\;bl\\t%l3\\t%@far jump\\n.LCB%=:\";
    }
  }"
  [(set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "8")
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length") 
        (if_then_else
	    (and (ge (minus (match_dup 3) (pc)) (const_int -250))
	         (le (minus (match_dup 3) (pc)) (const_int 256)))
	    (const_int 4)
	    (if_then_else
	        (and (ge (minus (match_dup 3) (pc)) (const_int -2040))
		     (le (minus (match_dup 3) (pc)) (const_int 2048)))
		(const_int 6)
		(const_int 8))))]
)
  
(define_insn "*bicsi3_cbranch"
  [(set (pc)
	(if_then_else
	 (match_operator 5 "equality_operator"
	  [(and:SI (not:SI (match_operand:SI 3 "s_register_operand" "l,l,l,l,l"))
		   (match_operand:SI 2 "s_register_operand" "0,1,1,1,1"))
	   (const_int 0)])
	 (label_ref (match_operand 4 "" ""))
	 (pc)))
   (set (match_operand:SI 0 "thumb_cbrch_target_operand" "=!l,l,*?h,*?m,*?m")
	(and:SI (not:SI (match_dup 3)) (match_dup 2)))
   (clobber (match_scratch:SI 1 "=X,l,l,&l,&l"))]
  "TARGET_THUMB"
  "*
  {
  if (which_alternative == 0)
    output_asm_insn (\"bic\\t%0, %3\", operands);
  else if (which_alternative <= 2)
    {
      output_asm_insn (\"bic\\t%1, %3\", operands);
      /* It's ok if OP0 is a lo-reg, even though the mov will set the
	 conditions again, since we're only testing for equality.  */
      output_asm_insn (\"mov\\t%0, %1\", operands);
    }
  else
    {
      output_asm_insn (\"bic\\t%1, %3\", operands);
      output_asm_insn (\"str\\t%1, %0\", operands);
    }

  switch (get_attr_length (insn) - (which_alternative ? 2 : 0))
    {
    case 4:  return \"b%d5\\t%l4\";
    case 6:  return \"b%D5\\t.LCB%=\;b\\t%l4\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D5\\t.LCB%=\;bl\\t%l4\\t%@far jump\\n.LCB%=:\";
    }
  }"
  [(set (attr "far_jump")
        (if_then_else
	    (ior (and (eq (symbol_ref ("which_alternative"))
	                  (const_int 0))
		      (eq_attr "length" "8"))
		 (eq_attr "length" "10"))
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length")
     (if_then_else
       (eq (symbol_ref ("which_alternative"))
		       (const_int 0))
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -250))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 4)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2040))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 6)
	   (const_int 8)))
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -248))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 6)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2038))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 8)
	   (const_int 10)))))]
)

(define_insn "*cbranchne_decr1"
  [(set (pc)
	(if_then_else (match_operator 3 "equality_operator"
		       [(match_operand:SI 2 "s_register_operand" "l,l,1,l")
		        (const_int 0)])
		      (label_ref (match_operand 4 "" ""))
		      (pc)))
   (set (match_operand:SI 0 "thumb_cbrch_target_operand" "=l,*?h,*?m,*?m")
	(plus:SI (match_dup 2) (const_int -1)))
   (clobber (match_scratch:SI 1 "=X,l,&l,&l"))]
  "TARGET_THUMB"
  "*
   {
     rtx cond[2];
     cond[0] = gen_rtx_fmt_ee ((GET_CODE (operands[3]) == NE
				? GEU : LTU),
			       VOIDmode, operands[2], const1_rtx);
     cond[1] = operands[4];

     if (which_alternative == 0)
       output_asm_insn (\"sub\\t%0, %2, #1\", operands);
     else if (which_alternative == 1)
       {
	 /* We must provide an alternative for a hi reg because reload 
	    cannot handle output reloads on a jump instruction, but we
	    can't subtract into that.  Fortunately a mov from lo to hi
	    does not clobber the condition codes.  */
	 output_asm_insn (\"sub\\t%1, %2, #1\", operands);
	 output_asm_insn (\"mov\\t%0, %1\", operands);
       }
     else
       {
	 /* Similarly, but the target is memory.  */
	 output_asm_insn (\"sub\\t%1, %2, #1\", operands);
	 output_asm_insn (\"str\\t%1, %0\", operands);
       }

     switch (get_attr_length (insn) - (which_alternative ? 2 : 0))
       {
	 case 4:
	   output_asm_insn (\"b%d0\\t%l1\", cond);
	   return \"\";
	 case 6:
	   output_asm_insn (\"b%D0\\t.LCB%=\", cond);
	   return \"b\\t%l4\\t%@long jump\\n.LCB%=:\";
	 default:
	   output_asm_insn (\"b%D0\\t.LCB%=\", cond);
	   return \"bl\\t%l4\\t%@far jump\\n.LCB%=:\";
       }
   }
  "
  [(set (attr "far_jump")
        (if_then_else
	    (ior (and (eq (symbol_ref ("which_alternative"))
	                  (const_int 0))
		      (eq_attr "length" "8"))
		 (eq_attr "length" "10"))
	    (const_string "yes")
            (const_string "no")))
   (set_attr_alternative "length"
      [
       ;; Alternative 0
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -250))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 4)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2040))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 6)
	   (const_int 8)))
       ;; Alternative 1
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -248))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 6)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2038))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 8)
	   (const_int 10)))
       ;; Alternative 2
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -248))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 6)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2038))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 8)
	   (const_int 10)))
       ;; Alternative 3
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -248))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 6)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2038))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 8)
	   (const_int 10)))])]
)

(define_insn "*addsi3_cbranch"
  [(set (pc)
	(if_then_else
	 (match_operator 4 "comparison_operator"
	  [(plus:SI
	    (match_operand:SI 2 "s_register_operand" "%l,0,*0,1,1,1")
	    (match_operand:SI 3 "reg_or_int_operand" "lL,IJ,*r,lIJ,lIJ,lIJ"))
	   (const_int 0)])
	 (label_ref (match_operand 5 "" ""))
	 (pc)))
   (set
    (match_operand:SI 0 "thumb_cbrch_target_operand" "=l,l,*!h,*?h,*?m,*?m")
    (plus:SI (match_dup 2) (match_dup 3)))
   (clobber (match_scratch:SI 1 "=X,X,X,l,&l,&l"))]
  "TARGET_THUMB
   && (GET_CODE (operands[4]) == EQ
       || GET_CODE (operands[4]) == NE
       || GET_CODE (operands[4]) == GE
       || GET_CODE (operands[4]) == LT)"
  "*
   {
     rtx cond[3];

     
     cond[0] = (which_alternative < 3) ? operands[0] : operands[1];
     cond[1] = operands[2];
     cond[2] = operands[3];

     if (GET_CODE (cond[2]) == CONST_INT && INTVAL (cond[2]) < 0)
       output_asm_insn (\"sub\\t%0, %1, #%n2\", cond);
     else
       output_asm_insn (\"add\\t%0, %1, %2\", cond);

     if (which_alternative >= 3
	 && which_alternative < 4)
       output_asm_insn (\"mov\\t%0, %1\", operands);
     else if (which_alternative >= 4)
       output_asm_insn (\"str\\t%1, %0\", operands);

     switch (get_attr_length (insn) - ((which_alternative >= 3) ? 2 : 0))
       {
	 case 4:
	   return \"b%d4\\t%l5\";
	 case 6:
	   return \"b%D4\\t.LCB%=\;b\\t%l5\\t%@long jump\\n.LCB%=:\";
	 default:
	   return \"b%D4\\t.LCB%=\;bl\\t%l5\\t%@far jump\\n.LCB%=:\";
       }
   }
  "
  [(set (attr "far_jump")
        (if_then_else
	    (ior (and (lt (symbol_ref ("which_alternative"))
	                  (const_int 3))
		      (eq_attr "length" "8"))
		 (eq_attr "length" "10"))
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length")
     (if_then_else
       (lt (symbol_ref ("which_alternative"))
		       (const_int 3))
       (if_then_else
	 (and (ge (minus (match_dup 5) (pc)) (const_int -250))
	      (le (minus (match_dup 5) (pc)) (const_int 256)))
	 (const_int 4)
	 (if_then_else
	   (and (ge (minus (match_dup 5) (pc)) (const_int -2040))
		(le (minus (match_dup 5) (pc)) (const_int 2048)))
	   (const_int 6)
	   (const_int 8)))
       (if_then_else
	 (and (ge (minus (match_dup 5) (pc)) (const_int -248))
	      (le (minus (match_dup 5) (pc)) (const_int 256)))
	 (const_int 6)
	 (if_then_else
	   (and (ge (minus (match_dup 5) (pc)) (const_int -2038))
		(le (minus (match_dup 5) (pc)) (const_int 2048)))
	   (const_int 8)
	   (const_int 10)))))]
)

(define_insn "*addsi3_cbranch_scratch"
  [(set (pc)
	(if_then_else
	 (match_operator 3 "comparison_operator"
	  [(plus:SI
	    (match_operand:SI 1 "s_register_operand" "%l,l,l,0")
	    (match_operand:SI 2 "reg_or_int_operand" "J,l,L,IJ"))
	   (const_int 0)])
	 (label_ref (match_operand 4 "" ""))
	 (pc)))
   (clobber (match_scratch:SI 0 "=X,X,l,l"))]
  "TARGET_THUMB
   && (GET_CODE (operands[3]) == EQ
       || GET_CODE (operands[3]) == NE
       || GET_CODE (operands[3]) == GE
       || GET_CODE (operands[3]) == LT)"
  "*
   {
     switch (which_alternative)
       {
       case 0:
	 output_asm_insn (\"cmp\t%1, #%n2\", operands);
	 break;
       case 1:
	 output_asm_insn (\"cmn\t%1, %2\", operands);
	 break;
       case 2:
	 if (INTVAL (operands[2]) < 0)
	   output_asm_insn (\"sub\t%0, %1, %2\", operands);
	 else
	   output_asm_insn (\"add\t%0, %1, %2\", operands);
	 break;
       case 3:
	 if (INTVAL (operands[2]) < 0)
	   output_asm_insn (\"sub\t%0, %0, %2\", operands);
	 else
	   output_asm_insn (\"add\t%0, %0, %2\", operands);
	 break;
       }

     switch (get_attr_length (insn))
       {
	 case 4:
	   return \"b%d3\\t%l4\";
	 case 6:
	   return \"b%D3\\t.LCB%=\;b\\t%l4\\t%@long jump\\n.LCB%=:\";
	 default:
	   return \"b%D3\\t.LCB%=\;bl\\t%l4\\t%@far jump\\n.LCB%=:\";
       }
   }
  "
  [(set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "8")
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length")
       (if_then_else
	 (and (ge (minus (match_dup 4) (pc)) (const_int -250))
	      (le (minus (match_dup 4) (pc)) (const_int 256)))
	 (const_int 4)
	 (if_then_else
	   (and (ge (minus (match_dup 4) (pc)) (const_int -2040))
		(le (minus (match_dup 4) (pc)) (const_int 2048)))
	   (const_int 6)
	   (const_int 8))))]
)

(define_insn "*subsi3_cbranch"
  [(set (pc)
	(if_then_else
	 (match_operator 4 "comparison_operator"
	  [(minus:SI
	    (match_operand:SI 2 "s_register_operand" "l,l,1,l")
	    (match_operand:SI 3 "s_register_operand" "l,l,l,l"))
	   (const_int 0)])
	 (label_ref (match_operand 5 "" ""))
	 (pc)))
   (set (match_operand:SI 0 "thumb_cbrch_target_operand" "=l,*?h,*?m,*?m")
	(minus:SI (match_dup 2) (match_dup 3)))
   (clobber (match_scratch:SI 1 "=X,l,&l,&l"))]
  "TARGET_THUMB
   && (GET_CODE (operands[4]) == EQ
       || GET_CODE (operands[4]) == NE
       || GET_CODE (operands[4]) == GE
       || GET_CODE (operands[4]) == LT)"
  "*
   {
     if (which_alternative == 0)
       output_asm_insn (\"sub\\t%0, %2, %3\", operands);
     else if (which_alternative == 1)
       {
	 /* We must provide an alternative for a hi reg because reload 
	    cannot handle output reloads on a jump instruction, but we
	    can't subtract into that.  Fortunately a mov from lo to hi
	    does not clobber the condition codes.  */
	 output_asm_insn (\"sub\\t%1, %2, %3\", operands);
	 output_asm_insn (\"mov\\t%0, %1\", operands);
       }
     else
       {
	 /* Similarly, but the target is memory.  */
	 output_asm_insn (\"sub\\t%1, %2, %3\", operands);
	 output_asm_insn (\"str\\t%1, %0\", operands);
       }

     switch (get_attr_length (insn) - ((which_alternative != 0) ? 2 : 0))
       {
	 case 4:
	   return \"b%d4\\t%l5\";
	 case 6:
	   return \"b%D4\\t.LCB%=\;b\\t%l5\\t%@long jump\\n.LCB%=:\";
	 default:
	   return \"b%D4\\t.LCB%=\;bl\\t%l5\\t%@far jump\\n.LCB%=:\";
       }
   }
  "
  [(set (attr "far_jump")
        (if_then_else
	    (ior (and (eq (symbol_ref ("which_alternative"))
	                  (const_int 0))
		      (eq_attr "length" "8"))
		 (eq_attr "length" "10"))
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length")
     (if_then_else
       (eq (symbol_ref ("which_alternative"))
		       (const_int 0))
       (if_then_else
	 (and (ge (minus (match_dup 5) (pc)) (const_int -250))
	      (le (minus (match_dup 5) (pc)) (const_int 256)))
	 (const_int 4)
	 (if_then_else
	   (and (ge (minus (match_dup 5) (pc)) (const_int -2040))
		(le (minus (match_dup 5) (pc)) (const_int 2048)))
	   (const_int 6)
	   (const_int 8)))
       (if_then_else
	 (and (ge (minus (match_dup 5) (pc)) (const_int -248))
	      (le (minus (match_dup 5) (pc)) (const_int 256)))
	 (const_int 6)
	 (if_then_else
	   (and (ge (minus (match_dup 5) (pc)) (const_int -2038))
		(le (minus (match_dup 5) (pc)) (const_int 2048)))
	   (const_int 8)
	   (const_int 10)))))]
)

(define_insn "*subsi3_cbranch_scratch"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "arm_comparison_operator"
	  [(minus:SI (match_operand:SI 1 "register_operand" "l")
		     (match_operand:SI 2 "nonmemory_operand" "l"))
	   (const_int 0)])
	 (label_ref (match_operand 3 "" ""))
	 (pc)))]
  "TARGET_THUMB
   && (GET_CODE (operands[0]) == EQ
       || GET_CODE (operands[0]) == NE
       || GET_CODE (operands[0]) == GE
       || GET_CODE (operands[0]) == LT)"
  "*
  output_asm_insn (\"cmp\\t%1, %2\", operands);
  switch (get_attr_length (insn))
    {
    case 4:  return \"b%d0\\t%l3\";
    case 6:  return \"b%D0\\t.LCB%=\;b\\t%l3\\t%@long jump\\n.LCB%=:\";
    default: return \"b%D0\\t.LCB%=\;bl\\t%l3\\t%@far jump\\n.LCB%=:\";
    }
  "
  [(set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "8")
	    (const_string "yes")
            (const_string "no")))
   (set (attr "length") 
        (if_then_else
	    (and (ge (minus (match_dup 3) (pc)) (const_int -250))
	         (le (minus (match_dup 3) (pc)) (const_int 256)))
	    (const_int 4)
	    (if_then_else
	        (and (ge (minus (match_dup 3) (pc)) (const_int -2040))
		     (le (minus (match_dup 3) (pc)) (const_int 2048)))
		(const_int 6)
		(const_int 8))))]
)

;; Comparison and test insns

(define_expand "cmpsi"
  [(match_operand:SI 0 "s_register_operand" "")
   (match_operand:SI 1 "arm_add_operand" "")]
  "TARGET_ARM"
  "{
    arm_compare_op0 = operands[0];
    arm_compare_op1 = operands[1];
    DONE;
  }"
)

(define_expand "cmpsf"
  [(match_operand:SF 0 "s_register_operand" "")
   (match_operand:SF 1 "arm_float_compare_operand" "")]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "
  arm_compare_op0 = operands[0];
  arm_compare_op1 = operands[1];
  DONE;
  "
)

(define_expand "cmpdf"
  [(match_operand:DF 0 "s_register_operand" "")
   (match_operand:DF 1 "arm_float_compare_operand" "")]
  "TARGET_ARM && TARGET_HARD_FLOAT"
  "
  arm_compare_op0 = operands[0];
  arm_compare_op1 = operands[1];
  DONE;
  "
)

(define_insn "*arm_cmpsi_insn"
  [(set (reg:CC CC_REGNUM)
	(compare:CC (match_operand:SI 0 "s_register_operand" "r,r")
		    (match_operand:SI 1 "arm_add_operand"    "rI,L")))]
  "TARGET_ARM"
  "@
   cmp%?\\t%0, %1
   cmn%?\\t%0, #%n1"
  [(set_attr "conds" "set")]
)

(define_insn "*cmpsi_shiftsi"
  [(set (reg:CC CC_REGNUM)
	(compare:CC (match_operand:SI   0 "s_register_operand" "r")
		    (match_operator:SI  3 "shift_operator"
		     [(match_operand:SI 1 "s_register_operand" "r")
		      (match_operand:SI 2 "arm_rhs_operand"    "rM")])))]
  "TARGET_ARM"
  "cmp%?\\t%0, %1%S3"
  [(set_attr "conds" "set")
   (set_attr "shift" "1")
   (set (attr "type") (if_then_else (match_operand 2 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*cmpsi_shiftsi_swp"
  [(set (reg:CC_SWP CC_REGNUM)
	(compare:CC_SWP (match_operator:SI 3 "shift_operator"
			 [(match_operand:SI 1 "s_register_operand" "r")
			  (match_operand:SI 2 "reg_or_int_operand" "rM")])
			(match_operand:SI 0 "s_register_operand" "r")))]
  "TARGET_ARM"
  "cmp%?\\t%0, %1%S3"
  [(set_attr "conds" "set")
   (set_attr "shift" "1")
   (set (attr "type") (if_then_else (match_operand 2 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*cmpsi_negshiftsi_si"
  [(set (reg:CC_Z CC_REGNUM)
	(compare:CC_Z
	 (neg:SI (match_operator:SI 1 "shift_operator"
		    [(match_operand:SI 2 "s_register_operand" "r")
		     (match_operand:SI 3 "reg_or_int_operand" "rM")]))
	 (match_operand:SI 0 "s_register_operand" "r")))]
  "TARGET_ARM"
  "cmn%?\\t%0, %2%S1"
  [(set_attr "conds" "set")
   (set (attr "type") (if_then_else (match_operand 3 "const_int_operand" "")
				    (const_string "alu_shift")
				    (const_string "alu_shift_reg")))]
)

;; Cirrus SF compare instruction
(define_insn "*cirrus_cmpsf"
  [(set (reg:CCFP CC_REGNUM)
	(compare:CCFP (match_operand:SF 0 "cirrus_fp_register" "v")
		      (match_operand:SF 1 "cirrus_fp_register" "v")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_MAVERICK"
  "cfcmps%?\\tr15, %V0, %V1"
  [(set_attr "type"   "mav_farith")
   (set_attr "cirrus" "compare")]
)

;; Cirrus DF compare instruction
(define_insn "*cirrus_cmpdf"
  [(set (reg:CCFP CC_REGNUM)
	(compare:CCFP (match_operand:DF 0 "cirrus_fp_register" "v")
		      (match_operand:DF 1 "cirrus_fp_register" "v")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_MAVERICK"
  "cfcmpd%?\\tr15, %V0, %V1"
  [(set_attr "type"   "mav_farith")
   (set_attr "cirrus" "compare")]
)

;; Cirrus DI compare instruction
(define_expand "cmpdi"
  [(match_operand:DI 0 "cirrus_fp_register" "")
   (match_operand:DI 1 "cirrus_fp_register" "")]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_MAVERICK"
  "{
     arm_compare_op0 = operands[0];
     arm_compare_op1 = operands[1];
     DONE;
   }")

(define_insn "*cirrus_cmpdi"
  [(set (reg:CC CC_REGNUM)
	(compare:CC (match_operand:DI 0 "cirrus_fp_register" "v")
		    (match_operand:DI 1 "cirrus_fp_register" "v")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_MAVERICK"
  "cfcmp64%?\\tr15, %V0, %V1"
  [(set_attr "type"   "mav_farith")
   (set_attr "cirrus" "compare")]
)

; This insn allows redundant compares to be removed by cse, nothing should
; ever appear in the output file since (set (reg x) (reg x)) is a no-op that
; is deleted later on. The match_dup will match the mode here, so that
; mode changes of the condition codes aren't lost by this even though we don't
; specify what they are.

(define_insn "*deleted_compare"
  [(set (match_operand 0 "cc_register" "") (match_dup 0))]
  "TARGET_ARM"
  "\\t%@ deleted compare"
  [(set_attr "conds" "set")
   (set_attr "length" "0")]
)


;; Conditional branch insns

(define_expand "beq"
  [(set (pc)
	(if_then_else (eq (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (EQ, arm_compare_op0, arm_compare_op1);"
)

(define_expand "bne"
  [(set (pc)
	(if_then_else (ne (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (NE, arm_compare_op0, arm_compare_op1);"
)

(define_expand "bgt"
  [(set (pc)
	(if_then_else (gt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (GT, arm_compare_op0, arm_compare_op1);"
)

(define_expand "ble"
  [(set (pc)
	(if_then_else (le (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (LE, arm_compare_op0, arm_compare_op1);"
)

(define_expand "bge"
  [(set (pc)
	(if_then_else (ge (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (GE, arm_compare_op0, arm_compare_op1);"
)

(define_expand "blt"
  [(set (pc)
	(if_then_else (lt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (LT, arm_compare_op0, arm_compare_op1);"
)

(define_expand "bgtu"
  [(set (pc)
	(if_then_else (gtu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (GTU, arm_compare_op0, arm_compare_op1);"
)

(define_expand "bleu"
  [(set (pc)
	(if_then_else (leu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (LEU, arm_compare_op0, arm_compare_op1);"
)

(define_expand "bgeu"
  [(set (pc)
	(if_then_else (geu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (GEU, arm_compare_op0, arm_compare_op1);"
)

(define_expand "bltu"
  [(set (pc)
	(if_then_else (ltu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (LTU, arm_compare_op0, arm_compare_op1);"
)

(define_expand "bunordered"
  [(set (pc)
	(if_then_else (unordered (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (UNORDERED, arm_compare_op0,
				      arm_compare_op1);"
)

(define_expand "bordered"
  [(set (pc)
	(if_then_else (ordered (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (ORDERED, arm_compare_op0,
				      arm_compare_op1);"
)

(define_expand "bungt"
  [(set (pc)
	(if_then_else (ungt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (UNGT, arm_compare_op0, arm_compare_op1);"
)

(define_expand "bunlt"
  [(set (pc)
	(if_then_else (unlt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (UNLT, arm_compare_op0, arm_compare_op1);"
)

(define_expand "bunge"
  [(set (pc)
	(if_then_else (unge (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (UNGE, arm_compare_op0, arm_compare_op1);"
)

(define_expand "bunle"
  [(set (pc)
	(if_then_else (unle (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (UNLE, arm_compare_op0, arm_compare_op1);"
)

;; The following two patterns need two branch instructions, since there is
;; no single instruction that will handle all cases.
(define_expand "buneq"
  [(set (pc)
	(if_then_else (uneq (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (UNEQ, arm_compare_op0, arm_compare_op1);"
)

(define_expand "bltgt"
  [(set (pc)
	(if_then_else (ltgt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (LTGT, arm_compare_op0, arm_compare_op1);"
)

;;
;; Patterns to match conditional branch insns.
;;

; Special pattern to match UNEQ.
(define_insn "*arm_buneq"
  [(set (pc)
	(if_then_else (uneq (match_operand 1 "cc_register" "") (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "*
  gcc_assert (!arm_ccfsm_state);

  return \"bvs\\t%l0\;beq\\t%l0\";
  "
  [(set_attr "conds" "jump_clob")
   (set_attr "length" "8")]
)

; Special pattern to match LTGT.
(define_insn "*arm_bltgt"
  [(set (pc)
	(if_then_else (ltgt (match_operand 1 "cc_register" "") (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "*
  gcc_assert (!arm_ccfsm_state);

  return \"bmi\\t%l0\;bgt\\t%l0\";
  "
  [(set_attr "conds" "jump_clob")
   (set_attr "length" "8")]
)

(define_insn "*arm_cond_branch"
  [(set (pc)
	(if_then_else (match_operator 1 "arm_comparison_operator"
		       [(match_operand 2 "cc_register" "") (const_int 0)])
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  "TARGET_ARM"
  "*
  if (arm_ccfsm_state == 1 || arm_ccfsm_state == 2)
    {
      arm_ccfsm_state += 2;
      return \"\";
    }
  return \"b%d1\\t%l0\";
  "
  [(set_attr "conds" "use")
   (set_attr "type" "branch")]
)

; Special pattern to match reversed UNEQ.
(define_insn "*arm_buneq_reversed"
  [(set (pc)
	(if_then_else (uneq (match_operand 1 "cc_register" "") (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "*
  gcc_assert (!arm_ccfsm_state);

  return \"bmi\\t%l0\;bgt\\t%l0\";
  "
  [(set_attr "conds" "jump_clob")
   (set_attr "length" "8")]
)

; Special pattern to match reversed LTGT.
(define_insn "*arm_bltgt_reversed"
  [(set (pc)
	(if_then_else (ltgt (match_operand 1 "cc_register" "") (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "*
  gcc_assert (!arm_ccfsm_state);

  return \"bvs\\t%l0\;beq\\t%l0\";
  "
  [(set_attr "conds" "jump_clob")
   (set_attr "length" "8")]
)

(define_insn "*arm_cond_branch_reversed"
  [(set (pc)
	(if_then_else (match_operator 1 "arm_comparison_operator"
		       [(match_operand 2 "cc_register" "") (const_int 0)])
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  "TARGET_ARM"
  "*
  if (arm_ccfsm_state == 1 || arm_ccfsm_state == 2)
    {
      arm_ccfsm_state += 2;
      return \"\";
    }
  return \"b%D1\\t%l0\";
  "
  [(set_attr "conds" "use")
   (set_attr "type" "branch")]
)



; scc insns

(define_expand "seq"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(eq:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (EQ, arm_compare_op0, arm_compare_op1);"
)

(define_expand "sne"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(ne:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (NE, arm_compare_op0, arm_compare_op1);"
)

(define_expand "sgt"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(gt:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (GT, arm_compare_op0, arm_compare_op1);"
)

(define_expand "sle"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(le:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (LE, arm_compare_op0, arm_compare_op1);"
)

(define_expand "sge"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(ge:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (GE, arm_compare_op0, arm_compare_op1);"
)

(define_expand "slt"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(lt:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (LT, arm_compare_op0, arm_compare_op1);"
)

(define_expand "sgtu"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(gtu:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (GTU, arm_compare_op0, arm_compare_op1);"
)

(define_expand "sleu"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(leu:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (LEU, arm_compare_op0, arm_compare_op1);"
)

(define_expand "sgeu"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(geu:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (GEU, arm_compare_op0, arm_compare_op1);"
)

(define_expand "sltu"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(ltu:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM"
  "operands[1] = arm_gen_compare_reg (LTU, arm_compare_op0, arm_compare_op1);"
)

(define_expand "sunordered"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(unordered:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (UNORDERED, arm_compare_op0,
				      arm_compare_op1);"
)

(define_expand "sordered"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(ordered:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (ORDERED, arm_compare_op0,
				      arm_compare_op1);"
)

(define_expand "sungt"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(ungt:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (UNGT, arm_compare_op0,
				      arm_compare_op1);"
)

(define_expand "sunge"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(unge:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (UNGE, arm_compare_op0,
				      arm_compare_op1);"
)

(define_expand "sunlt"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(unlt:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (UNLT, arm_compare_op0,
				      arm_compare_op1);"
)

(define_expand "sunle"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(unle:SI (match_dup 1) (const_int 0)))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "operands[1] = arm_gen_compare_reg (UNLE, arm_compare_op0,
				      arm_compare_op1);"
)

;;; DO NOT add patterns for SUNEQ or SLTGT, these can't be represented with
;;; simple ARM instructions. 
;
; (define_expand "suneq"
;   [(set (match_operand:SI 0 "s_register_operand" "")
; 	(uneq:SI (match_dup 1) (const_int 0)))]
;   "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
;   "gcc_unreachable ();"
; )
;
; (define_expand "sltgt"
;   [(set (match_operand:SI 0 "s_register_operand" "")
; 	(ltgt:SI (match_dup 1) (const_int 0)))]
;   "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
;   "gcc_unreachable ();"
; )

(define_insn "*mov_scc"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(match_operator:SI 1 "arm_comparison_operator"
	 [(match_operand 2 "cc_register" "") (const_int 0)]))]
  "TARGET_ARM"
  "mov%D1\\t%0, #0\;mov%d1\\t%0, #1"
  [(set_attr "conds" "use")
   (set_attr "length" "8")]
)

(define_insn "*mov_negscc"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(neg:SI (match_operator:SI 1 "arm_comparison_operator"
		 [(match_operand 2 "cc_register" "") (const_int 0)])))]
  "TARGET_ARM"
  "mov%D1\\t%0, #0\;mvn%d1\\t%0, #0"
  [(set_attr "conds" "use")
   (set_attr "length" "8")]
)

(define_insn "*mov_notscc"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(not:SI (match_operator:SI 1 "arm_comparison_operator"
		 [(match_operand 2 "cc_register" "") (const_int 0)])))]
  "TARGET_ARM"
  "mov%D1\\t%0, #0\;mvn%d1\\t%0, #1"
  [(set_attr "conds" "use")
   (set_attr "length" "8")]
)


;; Conditional move insns

(define_expand "movsicc"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(if_then_else:SI (match_operand 1 "arm_comparison_operator" "")
			 (match_operand:SI 2 "arm_not_operand" "")
			 (match_operand:SI 3 "arm_not_operand" "")))]
  "TARGET_ARM"
  "
  {
    enum rtx_code code = GET_CODE (operands[1]);
    rtx ccreg;

    if (code == UNEQ || code == LTGT)
      FAIL;

    ccreg = arm_gen_compare_reg (code, arm_compare_op0, arm_compare_op1);
    operands[1] = gen_rtx_fmt_ee (code, VOIDmode, ccreg, const0_rtx);
  }"
)

(define_expand "movsfcc"
  [(set (match_operand:SF 0 "s_register_operand" "")
	(if_then_else:SF (match_operand 1 "arm_comparison_operator" "")
			 (match_operand:SF 2 "s_register_operand" "")
			 (match_operand:SF 3 "nonmemory_operand" "")))]
  "TARGET_ARM"
  "
  {
    enum rtx_code code = GET_CODE (operands[1]);
    rtx ccreg;

    if (code == UNEQ || code == LTGT)
      FAIL;

    /* When compiling for SOFT_FLOAT, ensure both arms are in registers. 
       Otherwise, ensure it is a valid FP add operand */
    if ((!(TARGET_HARD_FLOAT && TARGET_FPA))
        || (!arm_float_add_operand (operands[3], SFmode)))
      operands[3] = force_reg (SFmode, operands[3]);

    ccreg = arm_gen_compare_reg (code, arm_compare_op0, arm_compare_op1);
    operands[1] = gen_rtx_fmt_ee (code, VOIDmode, ccreg, const0_rtx);
  }"
)

(define_expand "movdfcc"
  [(set (match_operand:DF 0 "s_register_operand" "")
	(if_then_else:DF (match_operand 1 "arm_comparison_operator" "")
			 (match_operand:DF 2 "s_register_operand" "")
			 (match_operand:DF 3 "arm_float_add_operand" "")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && (TARGET_FPA || TARGET_VFP)"
  "
  {
    enum rtx_code code = GET_CODE (operands[1]);
    rtx ccreg;

    if (code == UNEQ || code == LTGT)
      FAIL;

    ccreg = arm_gen_compare_reg (code, arm_compare_op0, arm_compare_op1);
    operands[1] = gen_rtx_fmt_ee (code, VOIDmode, ccreg, const0_rtx);
  }"
)

(define_insn "*movsicc_insn"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r,r,r,r,r,r")
	(if_then_else:SI
	 (match_operator 3 "arm_comparison_operator"
	  [(match_operand 4 "cc_register" "") (const_int 0)])
	 (match_operand:SI 1 "arm_not_operand" "0,0,rI,K,rI,rI,K,K")
	 (match_operand:SI 2 "arm_not_operand" "rI,K,0,0,rI,K,rI,K")))]
  "TARGET_ARM"
  "@
   mov%D3\\t%0, %2
   mvn%D3\\t%0, #%B2
   mov%d3\\t%0, %1
   mvn%d3\\t%0, #%B1
   mov%d3\\t%0, %1\;mov%D3\\t%0, %2
   mov%d3\\t%0, %1\;mvn%D3\\t%0, #%B2
   mvn%d3\\t%0, #%B1\;mov%D3\\t%0, %2
   mvn%d3\\t%0, #%B1\;mvn%D3\\t%0, #%B2"
  [(set_attr "length" "4,4,4,4,8,8,8,8")
   (set_attr "conds" "use")]
)

(define_insn "*movsfcc_soft_insn"
  [(set (match_operand:SF 0 "s_register_operand" "=r,r")
	(if_then_else:SF (match_operator 3 "arm_comparison_operator"
			  [(match_operand 4 "cc_register" "") (const_int 0)])
			 (match_operand:SF 1 "s_register_operand" "0,r")
			 (match_operand:SF 2 "s_register_operand" "r,0")))]
  "TARGET_ARM && TARGET_SOFT_FLOAT"
  "@
   mov%D3\\t%0, %2
   mov%d3\\t%0, %1"
  [(set_attr "conds" "use")]
)


;; Jump and linkage insns

(define_expand "jump"
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  "TARGET_EITHER"
  ""
)

(define_insn "*arm_jump"
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  "TARGET_ARM"
  "*
  {
    if (arm_ccfsm_state == 1 || arm_ccfsm_state == 2)
      {
        arm_ccfsm_state += 2;
        return \"\";
      }
    return \"b%?\\t%l0\";
  }
  "
  [(set_attr "predicable" "yes")]
)

(define_insn "*thumb_jump"
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  "TARGET_THUMB"
  "*
  if (get_attr_length (insn) == 2)
    return \"b\\t%l0\";
  return \"bl\\t%l0\\t%@ far jump\";
  "
  [(set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "4")
	    (const_string "yes")
	    (const_string "no")))
   (set (attr "length") 
        (if_then_else
	    (and (ge (minus (match_dup 0) (pc)) (const_int -2044))
		 (le (minus (match_dup 0) (pc)) (const_int 2048)))
  	    (const_int 2)
	    (const_int 4)))]
)

(define_expand "call"
  [(parallel [(call (match_operand 0 "memory_operand" "")
	            (match_operand 1 "general_operand" ""))
	      (use (match_operand 2 "" ""))
	      (clobber (reg:SI LR_REGNUM))])]
  "TARGET_EITHER"
  "
  {
    rtx callee;
    
    /* In an untyped call, we can get NULL for operand 2.  */
    if (operands[2] == NULL_RTX)
      operands[2] = const0_rtx;
      
    /* This is to decide if we should generate indirect calls by loading the
       32 bit address of the callee into a register before performing the
       branch and link.  operand[2] encodes the long_call/short_call
       attribute of the function being called.  This attribute is set whenever
       __attribute__((long_call/short_call)) or #pragma long_call/no_long_call
       is used, and the short_call attribute can also be set if function is
       declared as static or if it has already been defined in the current
       compilation unit.  See arm.c and arm.h for info about this.  The third
       parameter to arm_is_longcall_p is used to tell it which pattern
       invoked it.  */
    callee  = XEXP (operands[0], 0);
    
    if ((GET_CODE (callee) == SYMBOL_REF
	 && arm_is_longcall_p (operands[0], INTVAL (operands[2]), 0))
	|| (GET_CODE (callee) != SYMBOL_REF
	    && GET_CODE (callee) != REG))
      XEXP (operands[0], 0) = force_reg (Pmode, callee);
  }"
)

(define_insn "*call_reg_armv5"
  [(call (mem:SI (match_operand:SI 0 "s_register_operand" "r"))
         (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_ARM && arm_arch5"
  "blx%?\\t%0"
  [(set_attr "type" "call")]
)

(define_insn "*call_reg_arm"
  [(call (mem:SI (match_operand:SI 0 "s_register_operand" "r"))
         (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_ARM && !arm_arch5"
  "*
  return output_call (operands);
  "
  ;; length is worst case, normally it is only two
  [(set_attr "length" "12")
   (set_attr "type" "call")]
)

(define_insn "*call_mem"
  [(call (mem:SI (match_operand:SI 0 "call_memory_operand" "m"))
	 (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_ARM"
  "*
  return output_call_mem (operands);
  "
  [(set_attr "length" "12")
   (set_attr "type" "call")]
)

(define_insn "*call_reg_thumb_v5"
  [(call (mem:SI (match_operand:SI 0 "register_operand" "l*r"))
	 (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_THUMB && arm_arch5"
  "blx\\t%0"
  [(set_attr "length" "2")
   (set_attr "type" "call")]
)

(define_insn "*call_reg_thumb"
  [(call (mem:SI (match_operand:SI 0 "register_operand" "l*r"))
	 (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_THUMB && !arm_arch5"
  "*
  {
    if (!TARGET_CALLER_INTERWORKING)
      return thumb_call_via_reg (operands[0]);
    else if (operands[1] == const0_rtx)
      return \"bl\\t%__interwork_call_via_%0\";
    else if (frame_pointer_needed)
      return \"bl\\t%__interwork_r7_call_via_%0\";
    else
      return \"bl\\t%__interwork_r11_call_via_%0\";
  }"
  [(set_attr "type" "call")]
)

(define_expand "call_value"
  [(parallel [(set (match_operand       0 "" "")
	           (call (match_operand 1 "memory_operand" "")
		         (match_operand 2 "general_operand" "")))
	      (use (match_operand 3 "" ""))
	      (clobber (reg:SI LR_REGNUM))])]
  "TARGET_EITHER"
  "
  {
    rtx callee = XEXP (operands[1], 0);
    
    /* In an untyped call, we can get NULL for operand 2.  */
    if (operands[3] == 0)
      operands[3] = const0_rtx;
      
    /* See the comment in define_expand \"call\".  */
    if ((GET_CODE (callee) == SYMBOL_REF
	 && arm_is_longcall_p (operands[1], INTVAL (operands[3]), 0))
	|| (GET_CODE (callee) != SYMBOL_REF
	    && GET_CODE (callee) != REG))
      XEXP (operands[1], 0) = force_reg (Pmode, callee);
  }"
)

(define_insn "*call_value_reg_armv5"
  [(set (match_operand 0 "" "")
        (call (mem:SI (match_operand:SI 1 "s_register_operand" "r"))
	      (match_operand 2 "" "")))
   (use (match_operand 3 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_ARM && arm_arch5"
  "blx%?\\t%1"
  [(set_attr "type" "call")]
)

(define_insn "*call_value_reg_arm"
  [(set (match_operand 0 "" "")
        (call (mem:SI (match_operand:SI 1 "s_register_operand" "r"))
	      (match_operand 2 "" "")))
   (use (match_operand 3 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_ARM && !arm_arch5"
  "*
  return output_call (&operands[1]);
  "
  [(set_attr "length" "12")
   (set_attr "type" "call")]
)

(define_insn "*call_value_mem"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand:SI 1 "call_memory_operand" "m"))
	      (match_operand 2 "" "")))
   (use (match_operand 3 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_ARM && (!CONSTANT_ADDRESS_P (XEXP (operands[1], 0)))"
  "*
  return output_call_mem (&operands[1]);
  "
  [(set_attr "length" "12")
   (set_attr "type" "call")]
)

(define_insn "*call_value_reg_thumb_v5"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand:SI 1 "register_operand" "l*r"))
	      (match_operand 2 "" "")))
   (use (match_operand 3 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_THUMB && arm_arch5"
  "blx\\t%1"
  [(set_attr "length" "2")
   (set_attr "type" "call")]
)

(define_insn "*call_value_reg_thumb"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand:SI 1 "register_operand" "l*r"))
	      (match_operand 2 "" "")))
   (use (match_operand 3 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_THUMB && !arm_arch5"
  "*
  {
    if (!TARGET_CALLER_INTERWORKING)
      return thumb_call_via_reg (operands[1]);
    else if (operands[2] == const0_rtx)
      return \"bl\\t%__interwork_call_via_%1\";
    else if (frame_pointer_needed)
      return \"bl\\t%__interwork_r7_call_via_%1\";
    else
      return \"bl\\t%__interwork_r11_call_via_%1\";
  }"
  [(set_attr "type" "call")]
)

;; Allow calls to SYMBOL_REFs specially as they are not valid general addresses
;; The 'a' causes the operand to be treated as an address, i.e. no '#' output.

(define_insn "*call_symbol"
  [(call (mem:SI (match_operand:SI 0 "" ""))
	 (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_ARM
   && (GET_CODE (operands[0]) == SYMBOL_REF)
   && !arm_is_longcall_p (operands[0], INTVAL (operands[2]), 1)"
  "*
  {
    return NEED_PLT_RELOC ? \"bl%?\\t%a0(PLT)\" : \"bl%?\\t%a0\";
  }"
  [(set_attr "type" "call")]
)

(define_insn "*call_value_symbol"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand:SI 1 "" ""))
	(match_operand:SI 2 "" "")))
   (use (match_operand 3 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_ARM
   && (GET_CODE (operands[1]) == SYMBOL_REF)
   && !arm_is_longcall_p (operands[1], INTVAL (operands[3]), 1)"
  "*
  {
    return NEED_PLT_RELOC ? \"bl%?\\t%a1(PLT)\" : \"bl%?\\t%a1\";
  }"
  [(set_attr "type" "call")]
)

(define_insn "*call_insn"
  [(call (mem:SI (match_operand:SI 0 "" ""))
	 (match_operand:SI 1 "" ""))
   (use (match_operand 2 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_THUMB
   && GET_CODE (operands[0]) == SYMBOL_REF
   && !arm_is_longcall_p (operands[0], INTVAL (operands[2]), 1)"
  "bl\\t%a0"
  [(set_attr "length" "4")
   (set_attr "type" "call")]
)

(define_insn "*call_value_insn"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand 1 "" ""))
	      (match_operand 2 "" "")))
   (use (match_operand 3 "" ""))
   (clobber (reg:SI LR_REGNUM))]
  "TARGET_THUMB
   && GET_CODE (operands[1]) == SYMBOL_REF
   && !arm_is_longcall_p (operands[1], INTVAL (operands[3]), 1)"
  "bl\\t%a1"
  [(set_attr "length" "4")
   (set_attr "type" "call")]
)

;; We may also be able to do sibcalls for Thumb, but it's much harder...
(define_expand "sibcall"
  [(parallel [(call (match_operand 0 "memory_operand" "")
		    (match_operand 1 "general_operand" ""))
	      (return)
	      (use (match_operand 2 "" ""))])]
  "TARGET_ARM"
  "
  {
    if (operands[2] == NULL_RTX)
      operands[2] = const0_rtx;
  }"
)

(define_expand "sibcall_value"
  [(parallel [(set (match_operand 0 "" "")
		   (call (match_operand 1 "memory_operand" "")
			 (match_operand 2 "general_operand" "")))
	      (return)
	      (use (match_operand 3 "" ""))])]
  "TARGET_ARM"
  "
  {
    if (operands[3] == NULL_RTX)
      operands[3] = const0_rtx;
  }"
)

(define_insn "*sibcall_insn"
 [(call (mem:SI (match_operand:SI 0 "" "X"))
	(match_operand 1 "" ""))
  (return)
  (use (match_operand 2 "" ""))]
  "TARGET_ARM && GET_CODE (operands[0]) == SYMBOL_REF"
  "*
  return NEED_PLT_RELOC ? \"b%?\\t%a0(PLT)\" : \"b%?\\t%a0\";
  "
  [(set_attr "type" "call")]
)

(define_insn "*sibcall_value_insn"
 [(set (match_operand 0 "" "")
       (call (mem:SI (match_operand:SI 1 "" "X"))
	     (match_operand 2 "" "")))
  (return)
  (use (match_operand 3 "" ""))]
  "TARGET_ARM && GET_CODE (operands[1]) == SYMBOL_REF"
  "*
  return NEED_PLT_RELOC ? \"b%?\\t%a1(PLT)\" : \"b%?\\t%a1\";
  "
  [(set_attr "type" "call")]
)

;; Often the return insn will be the same as loading from memory, so set attr
(define_insn "return"
  [(return)]
  "TARGET_ARM && USE_RETURN_INSN (FALSE)"
  "*
  {
    if (arm_ccfsm_state == 2)
      {
        arm_ccfsm_state += 2;
        return \"\";
      }
    return output_return_instruction (const_true_rtx, TRUE, FALSE);
  }"
  [(set_attr "type" "load1")
   (set_attr "length" "12")
   (set_attr "predicable" "yes")]
)

(define_insn "*cond_return"
  [(set (pc)
        (if_then_else (match_operator 0 "arm_comparison_operator"
		       [(match_operand 1 "cc_register" "") (const_int 0)])
                      (return)
                      (pc)))]
  "TARGET_ARM && USE_RETURN_INSN (TRUE)"
  "*
  {
    if (arm_ccfsm_state == 2)
      {
        arm_ccfsm_state += 2;
        return \"\";
      }
    return output_return_instruction (operands[0], TRUE, FALSE);
  }"
  [(set_attr "conds" "use")
   (set_attr "length" "12")
   (set_attr "type" "load1")]
)

(define_insn "*cond_return_inverted"
  [(set (pc)
        (if_then_else (match_operator 0 "arm_comparison_operator"
		       [(match_operand 1 "cc_register" "") (const_int 0)])
                      (pc)
		      (return)))]
  "TARGET_ARM && USE_RETURN_INSN (TRUE)"
  "*
  {
    if (arm_ccfsm_state == 2)
      {
        arm_ccfsm_state += 2;
        return \"\";
      }
    return output_return_instruction (operands[0], TRUE, TRUE);
  }"
  [(set_attr "conds" "use")
   (set_attr "length" "12")
   (set_attr "type" "load1")]
)

;; Generate a sequence of instructions to determine if the processor is
;; in 26-bit or 32-bit mode, and return the appropriate return address
;; mask.

(define_expand "return_addr_mask"
  [(set (match_dup 1)
      (compare:CC_NOOV (unspec [(const_int 0)] UNSPEC_CHECK_ARCH)
		       (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "")
      (if_then_else:SI (eq (match_dup 1) (const_int 0))
		       (const_int -1)
		       (const_int 67108860)))] ; 0x03fffffc
  "TARGET_ARM"
  "
  operands[1] = gen_rtx_REG (CC_NOOVmode, CC_REGNUM);
  ")

(define_insn "*check_arch2"
  [(set (match_operand:CC_NOOV 0 "cc_register" "")
      (compare:CC_NOOV (unspec [(const_int 0)] UNSPEC_CHECK_ARCH)
		       (const_int 0)))]
  "TARGET_ARM"
  "teq\\t%|r0, %|r0\;teq\\t%|pc, %|pc"
  [(set_attr "length" "8")
   (set_attr "conds" "set")]
)

;; Call subroutine returning any type.

(define_expand "untyped_call"
  [(parallel [(call (match_operand 0 "" "")
		    (const_int 0))
	      (match_operand 1 "" "")
	      (match_operand 2 "" "")])]
  "TARGET_EITHER"
  "
  {
    int i;
    rtx par = gen_rtx_PARALLEL (VOIDmode,
				rtvec_alloc (XVECLEN (operands[2], 0)));
    rtx addr = gen_reg_rtx (Pmode);
    rtx mem;
    int size = 0;

    emit_move_insn (addr, XEXP (operands[1], 0));
    mem = change_address (operands[1], BLKmode, addr);

    for (i = 0; i < XVECLEN (operands[2], 0); i++)
      {
	rtx src = SET_SRC (XVECEXP (operands[2], 0, i));

	/* Default code only uses r0 as a return value, but we could
	   be using anything up to 4 registers.  */
	if (REGNO (src) == R0_REGNUM)
	  src = gen_rtx_REG (TImode, R0_REGNUM);

        XVECEXP (par, 0, i) = gen_rtx_EXPR_LIST (VOIDmode, src,
						 GEN_INT (size));
        size += GET_MODE_SIZE (GET_MODE (src));
      }

    emit_call_insn (GEN_CALL_VALUE (par, operands[0], const0_rtx, NULL,
				    const0_rtx));

    size = 0;

    for (i = 0; i < XVECLEN (par, 0); i++)
      {
	HOST_WIDE_INT offset = 0;
	rtx reg = XEXP (XVECEXP (par, 0, i), 0);

	if (size != 0)
	  emit_move_insn (addr, plus_constant (addr, size));

	mem = change_address (mem, GET_MODE (reg), NULL);
	if (REGNO (reg) == R0_REGNUM)
	  {
	    /* On thumb we have to use a write-back instruction.  */
	    emit_insn (arm_gen_store_multiple (R0_REGNUM, 4, addr, TRUE,
			TARGET_THUMB ? TRUE : FALSE, mem, &offset));
	    size = TARGET_ARM ? 16 : 0;
	  }
	else
	  {
	    emit_move_insn (mem, reg);
	    size = GET_MODE_SIZE (GET_MODE (reg));
	  }
      }

    /* The optimizer does not know that the call sets the function value
       registers we stored in the result block.  We avoid problems by
       claiming that all hard registers are used and clobbered at this
       point.  */
    emit_insn (gen_blockage ());

    DONE;
  }"
)

(define_expand "untyped_return"
  [(match_operand:BLK 0 "memory_operand" "")
   (match_operand 1 "" "")]
  "TARGET_EITHER"
  "
  {
    int i;
    rtx addr = gen_reg_rtx (Pmode);
    rtx mem;
    int size = 0;

    emit_move_insn (addr, XEXP (operands[0], 0));
    mem = change_address (operands[0], BLKmode, addr);

    for (i = 0; i < XVECLEN (operands[1], 0); i++)
      {
	HOST_WIDE_INT offset = 0;
	rtx reg = SET_DEST (XVECEXP (operands[1], 0, i));

	if (size != 0)
	  emit_move_insn (addr, plus_constant (addr, size));

	mem = change_address (mem, GET_MODE (reg), NULL);
	if (REGNO (reg) == R0_REGNUM)
	  {
	    /* On thumb we have to use a write-back instruction.  */
	    emit_insn (arm_gen_load_multiple (R0_REGNUM, 4, addr, TRUE,
			TARGET_THUMB ? TRUE : FALSE, mem, &offset));
	    size = TARGET_ARM ? 16 : 0;
	  }
	else
	  {
	    emit_move_insn (reg, mem);
	    size = GET_MODE_SIZE (GET_MODE (reg));
	  }
      }

    /* Emit USE insns before the return.  */
    for (i = 0; i < XVECLEN (operands[1], 0); i++)
      emit_insn (gen_rtx_USE (VOIDmode,
			      SET_DEST (XVECEXP (operands[1], 0, i))));

    /* Construct the return.  */
    expand_naked_return ();

    DONE;
  }"
)

;; UNSPEC_VOLATILE is considered to use and clobber all hard registers and
;; all of memory.  This blocks insns from being moved across this point.

(define_insn "blockage"
  [(unspec_volatile [(const_int 0)] VUNSPEC_BLOCKAGE)]
  "TARGET_EITHER"
  ""
  [(set_attr "length" "0")
   (set_attr "type" "block")]
)

(define_expand "casesi"
  [(match_operand:SI 0 "s_register_operand" "")	; index to jump on
   (match_operand:SI 1 "const_int_operand" "")	; lower bound
   (match_operand:SI 2 "const_int_operand" "")	; total range
   (match_operand:SI 3 "" "")			; table label
   (match_operand:SI 4 "" "")]			; Out of range label
  "TARGET_ARM"
  "
  {
    rtx reg;
    if (operands[1] != const0_rtx)
      {
	reg = gen_reg_rtx (SImode);

	emit_insn (gen_addsi3 (reg, operands[0],
			       GEN_INT (-INTVAL (operands[1]))));
	operands[0] = reg;
      }

    if (!const_ok_for_arm (INTVAL (operands[2])))
      operands[2] = force_reg (SImode, operands[2]);

    emit_jump_insn (gen_casesi_internal (operands[0], operands[2], operands[3],
					 operands[4]));
    DONE;
  }"
)

;; The USE in this pattern is needed to tell flow analysis that this is
;; a CASESI insn.  It has no other purpose.
(define_insn "casesi_internal"
  [(parallel [(set (pc)
	       (if_then_else
		(leu (match_operand:SI 0 "s_register_operand" "r")
		     (match_operand:SI 1 "arm_rhs_operand" "rI"))
		(mem:SI (plus:SI (mult:SI (match_dup 0) (const_int 4))
				 (label_ref (match_operand 2 "" ""))))
		(label_ref (match_operand 3 "" ""))))
	      (clobber (reg:CC CC_REGNUM))
	      (use (label_ref (match_dup 2)))])]
  "TARGET_ARM"
  "*
    if (flag_pic)
      return \"cmp\\t%0, %1\;addls\\t%|pc, %|pc, %0, asl #2\;b\\t%l3\";
    return   \"cmp\\t%0, %1\;ldrls\\t%|pc, [%|pc, %0, asl #2]\;b\\t%l3\";
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "12")]
)

(define_expand "indirect_jump"
  [(set (pc)
	(match_operand:SI 0 "s_register_operand" ""))]
  "TARGET_EITHER"
  ""
)

;; NB Never uses BX.
(define_insn "*arm_indirect_jump"
  [(set (pc)
	(match_operand:SI 0 "s_register_operand" "r"))]
  "TARGET_ARM"
  "mov%?\\t%|pc, %0\\t%@ indirect register jump"
  [(set_attr "predicable" "yes")]
)

(define_insn "*load_indirect_jump"
  [(set (pc)
	(match_operand:SI 0 "memory_operand" "m"))]
  "TARGET_ARM"
  "ldr%?\\t%|pc, %0\\t%@ indirect memory jump"
  [(set_attr "type" "load1")
   (set_attr "pool_range" "4096")
   (set_attr "neg_pool_range" "4084")
   (set_attr "predicable" "yes")]
)

;; NB Never uses BX.
(define_insn "*thumb_indirect_jump"
  [(set (pc)
	(match_operand:SI 0 "register_operand" "l*r"))]
  "TARGET_THUMB"
  "mov\\tpc, %0"
  [(set_attr "conds" "clob")
   (set_attr "length" "2")]
)


;; Misc insns

(define_insn "nop"
  [(const_int 0)]
  "TARGET_EITHER"
  "*
  if (TARGET_ARM)
    return \"mov%?\\t%|r0, %|r0\\t%@ nop\";
  return  \"mov\\tr8, r8\";
  "
  [(set (attr "length")
	(if_then_else (eq_attr "is_thumb" "yes")
		      (const_int 2)
		      (const_int 4)))]
)


;; Patterns to allow combination of arithmetic, cond code and shifts

(define_insn "*arith_shiftsi"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
        (match_operator:SI 1 "shiftable_operator"
          [(match_operator:SI 3 "shift_operator"
             [(match_operand:SI 4 "s_register_operand" "r")
              (match_operand:SI 5 "reg_or_int_operand" "rI")])
           (match_operand:SI 2 "s_register_operand" "r")]))]
  "TARGET_ARM"
  "%i1%?\\t%0, %2, %4%S3"
  [(set_attr "predicable" "yes")
   (set_attr "shift" "4")
   (set (attr "type") (if_then_else (match_operand 5 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(match_operator:SI 1 "shiftable_operator"
	 [(match_operator:SI 2 "shiftable_operator"
	   [(match_operator:SI 3 "shift_operator"
	     [(match_operand:SI 4 "s_register_operand" "")
	      (match_operand:SI 5 "reg_or_int_operand" "")])
	    (match_operand:SI 6 "s_register_operand" "")])
	  (match_operand:SI 7 "arm_rhs_operand" "")]))
   (clobber (match_operand:SI 8 "s_register_operand" ""))]
  "TARGET_ARM"
  [(set (match_dup 8)
	(match_op_dup 2 [(match_op_dup 3 [(match_dup 4) (match_dup 5)])
			 (match_dup 6)]))
   (set (match_dup 0)
	(match_op_dup 1 [(match_dup 8) (match_dup 7)]))]
  "")

(define_insn "*arith_shiftsi_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
        (compare:CC_NOOV (match_operator:SI 1 "shiftable_operator"
		          [(match_operator:SI 3 "shift_operator"
		            [(match_operand:SI 4 "s_register_operand" "r")
		             (match_operand:SI 5 "reg_or_int_operand" "rI")])
		           (match_operand:SI 2 "s_register_operand" "r")])
			 (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=r")
	(match_op_dup 1 [(match_op_dup 3 [(match_dup 4) (match_dup 5)])
			 (match_dup 2)]))]
  "TARGET_ARM"
  "%i1%?s\\t%0, %2, %4%S3"
  [(set_attr "conds" "set")
   (set_attr "shift" "4")
   (set (attr "type") (if_then_else (match_operand 5 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*arith_shiftsi_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
        (compare:CC_NOOV (match_operator:SI 1 "shiftable_operator"
		          [(match_operator:SI 3 "shift_operator"
		            [(match_operand:SI 4 "s_register_operand" "r")
		             (match_operand:SI 5 "reg_or_int_operand" "rI")])
		           (match_operand:SI 2 "s_register_operand" "r")])
			 (const_int 0)))
   (clobber (match_scratch:SI 0 "=r"))]
  "TARGET_ARM"
  "%i1%?s\\t%0, %2, %4%S3"
  [(set_attr "conds" "set")
   (set_attr "shift" "4")
   (set (attr "type") (if_then_else (match_operand 5 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*sub_shiftsi"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(minus:SI (match_operand:SI 1 "s_register_operand" "r")
		  (match_operator:SI 2 "shift_operator"
		   [(match_operand:SI 3 "s_register_operand" "r")
		    (match_operand:SI 4 "reg_or_int_operand" "rM")])))]
  "TARGET_ARM"
  "sub%?\\t%0, %1, %3%S2"
  [(set_attr "predicable" "yes")
   (set_attr "shift" "3")
   (set (attr "type") (if_then_else (match_operand 4 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*sub_shiftsi_compare0"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV
	 (minus:SI (match_operand:SI 1 "s_register_operand" "r")
		   (match_operator:SI 2 "shift_operator"
		    [(match_operand:SI 3 "s_register_operand" "r")
		     (match_operand:SI 4 "reg_or_int_operand" "rM")]))
	 (const_int 0)))
   (set (match_operand:SI 0 "s_register_operand" "=r")
	(minus:SI (match_dup 1) (match_op_dup 2 [(match_dup 3)
						 (match_dup 4)])))]
  "TARGET_ARM"
  "sub%?s\\t%0, %1, %3%S2"
  [(set_attr "conds" "set")
   (set_attr "shift" "3")
   (set (attr "type") (if_then_else (match_operand 4 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*sub_shiftsi_compare0_scratch"
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV
	 (minus:SI (match_operand:SI 1 "s_register_operand" "r")
		   (match_operator:SI 2 "shift_operator"
		    [(match_operand:SI 3 "s_register_operand" "r")
		     (match_operand:SI 4 "reg_or_int_operand" "rM")]))
	 (const_int 0)))
   (clobber (match_scratch:SI 0 "=r"))]
  "TARGET_ARM"
  "sub%?s\\t%0, %1, %3%S2"
  [(set_attr "conds" "set")
   (set_attr "shift" "3")
   (set (attr "type") (if_then_else (match_operand 4 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)



(define_insn "*and_scc"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(and:SI (match_operator:SI 1 "arm_comparison_operator"
		 [(match_operand 3 "cc_register" "") (const_int 0)])
		(match_operand:SI 2 "s_register_operand" "r")))]
  "TARGET_ARM"
  "mov%D1\\t%0, #0\;and%d1\\t%0, %2, #1"
  [(set_attr "conds" "use")
   (set_attr "length" "8")]
)

(define_insn "*ior_scc"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(ior:SI (match_operator:SI 2 "arm_comparison_operator"
		 [(match_operand 3 "cc_register" "") (const_int 0)])
		(match_operand:SI 1 "s_register_operand" "0,?r")))]
  "TARGET_ARM"
  "@
   orr%d2\\t%0, %1, #1
   mov%D2\\t%0, %1\;orr%d2\\t%0, %1, #1"
  [(set_attr "conds" "use")
   (set_attr "length" "4,8")]
)

(define_insn "*compare_scc"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(match_operator:SI 1 "arm_comparison_operator"
	 [(match_operand:SI 2 "s_register_operand" "r,r")
	  (match_operand:SI 3 "arm_add_operand" "rI,L")]))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "*
    if (operands[3] == const0_rtx)
      {
	if (GET_CODE (operands[1]) == LT)
	  return \"mov\\t%0, %2, lsr #31\";

	if (GET_CODE (operands[1]) == GE)
	  return \"mvn\\t%0, %2\;mov\\t%0, %0, lsr #31\";

	if (GET_CODE (operands[1]) == EQ)
	  return \"rsbs\\t%0, %2, #1\;movcc\\t%0, #0\";
      }

    if (GET_CODE (operands[1]) == NE)
      {
        if (which_alternative == 1)
	  return \"adds\\t%0, %2, #%n3\;movne\\t%0, #1\";
        return \"subs\\t%0, %2, %3\;movne\\t%0, #1\";
      }
    if (which_alternative == 1)
      output_asm_insn (\"cmn\\t%2, #%n3\", operands);
    else
      output_asm_insn (\"cmp\\t%2, %3\", operands);
    return \"mov%D1\\t%0, #0\;mov%d1\\t%0, #1\";
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "12")]
)

(define_insn "*cond_move"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r")
	(if_then_else:SI (match_operator 3 "equality_operator"
			  [(match_operator 4 "arm_comparison_operator"
			    [(match_operand 5 "cc_register" "") (const_int 0)])
			   (const_int 0)])
			 (match_operand:SI 1 "arm_rhs_operand" "0,rI,?rI")
			 (match_operand:SI 2 "arm_rhs_operand" "rI,0,rI")))]
  "TARGET_ARM"
  "*
    if (GET_CODE (operands[3]) == NE)
      {
        if (which_alternative != 1)
	  output_asm_insn (\"mov%D4\\t%0, %2\", operands);
        if (which_alternative != 0)
	  output_asm_insn (\"mov%d4\\t%0, %1\", operands);
        return \"\";
      }
    if (which_alternative != 0)
      output_asm_insn (\"mov%D4\\t%0, %1\", operands);
    if (which_alternative != 1)
      output_asm_insn (\"mov%d4\\t%0, %2\", operands);
    return \"\";
  "
  [(set_attr "conds" "use")
   (set_attr "length" "4,4,8")]
)

(define_insn "*cond_arith"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
        (match_operator:SI 5 "shiftable_operator" 
	 [(match_operator:SI 4 "arm_comparison_operator"
           [(match_operand:SI 2 "s_register_operand" "r,r")
	    (match_operand:SI 3 "arm_rhs_operand" "rI,rI")])
          (match_operand:SI 1 "s_register_operand" "0,?r")]))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "*
    if (GET_CODE (operands[4]) == LT && operands[3] == const0_rtx)
      return \"%i5\\t%0, %1, %2, lsr #31\";

    output_asm_insn (\"cmp\\t%2, %3\", operands);
    if (GET_CODE (operands[5]) == AND)
      output_asm_insn (\"mov%D4\\t%0, #0\", operands);
    else if (GET_CODE (operands[5]) == MINUS)
      output_asm_insn (\"rsb%D4\\t%0, %1, #0\", operands);
    else if (which_alternative != 0)
      output_asm_insn (\"mov%D4\\t%0, %1\", operands);
    return \"%i5%d4\\t%0, %1, #1\";
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "12")]
)

(define_insn "*cond_sub"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
        (minus:SI (match_operand:SI 1 "s_register_operand" "0,?r")
		  (match_operator:SI 4 "arm_comparison_operator"
                   [(match_operand:SI 2 "s_register_operand" "r,r")
		    (match_operand:SI 3 "arm_rhs_operand" "rI,rI")])))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "*
    output_asm_insn (\"cmp\\t%2, %3\", operands);
    if (which_alternative != 0)
      output_asm_insn (\"mov%D4\\t%0, %1\", operands);
    return \"sub%d4\\t%0, %1, #1\";
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_insn "*cmp_ite0"
  [(set (match_operand 6 "dominant_cc_register" "")
	(compare
	 (if_then_else:SI
	  (match_operator 4 "arm_comparison_operator"
	   [(match_operand:SI 0 "s_register_operand" "r,r,r,r")
	    (match_operand:SI 1 "arm_add_operand" "rI,L,rI,L")])
	  (match_operator:SI 5 "arm_comparison_operator"
	   [(match_operand:SI 2 "s_register_operand" "r,r,r,r")
	    (match_operand:SI 3 "arm_add_operand" "rI,rI,L,L")])
	  (const_int 0))
	 (const_int 0)))]
  "TARGET_ARM"
  "*
  {
    static const char * const opcodes[4][2] =
    {
      {\"cmp\\t%2, %3\;cmp%d5\\t%0, %1\",
       \"cmp\\t%0, %1\;cmp%d4\\t%2, %3\"},
      {\"cmp\\t%2, %3\;cmn%d5\\t%0, #%n1\",
       \"cmn\\t%0, #%n1\;cmp%d4\\t%2, %3\"},
      {\"cmn\\t%2, #%n3\;cmp%d5\\t%0, %1\",
       \"cmp\\t%0, %1\;cmn%d4\\t%2, #%n3\"},
      {\"cmn\\t%2, #%n3\;cmn%d5\\t%0, #%n1\",
       \"cmn\\t%0, #%n1\;cmn%d4\\t%2, #%n3\"}
    };
    int swap =
      comparison_dominates_p (GET_CODE (operands[5]), GET_CODE (operands[4]));

    return opcodes[which_alternative][swap];
  }"
  [(set_attr "conds" "set")
   (set_attr "length" "8")]
)

(define_insn "*cmp_ite1"
  [(set (match_operand 6 "dominant_cc_register" "")
	(compare
	 (if_then_else:SI
	  (match_operator 4 "arm_comparison_operator"
	   [(match_operand:SI 0 "s_register_operand" "r,r,r,r")
	    (match_operand:SI 1 "arm_add_operand" "rI,L,rI,L")])
	  (match_operator:SI 5 "arm_comparison_operator"
	   [(match_operand:SI 2 "s_register_operand" "r,r,r,r")
	    (match_operand:SI 3 "arm_add_operand" "rI,rI,L,L")])
	  (const_int 1))
	 (const_int 0)))]
  "TARGET_ARM"
  "*
  {
    static const char * const opcodes[4][2] =
    {
      {\"cmp\\t%0, %1\;cmp%d4\\t%2, %3\",
       \"cmp\\t%2, %3\;cmp%D5\\t%0, %1\"},
      {\"cmn\\t%0, #%n1\;cmp%d4\\t%2, %3\",
       \"cmp\\t%2, %3\;cmn%D5\\t%0, #%n1\"},
      {\"cmp\\t%0, %1\;cmn%d4\\t%2, #%n3\",
       \"cmn\\t%2, #%n3\;cmp%D5\\t%0, %1\"},
      {\"cmn\\t%0, #%n1\;cmn%d4\\t%2, #%n3\",
       \"cmn\\t%2, #%n3\;cmn%D5\\t%0, #%n1\"}
    };
    int swap =
      comparison_dominates_p (GET_CODE (operands[5]),
			      reverse_condition (GET_CODE (operands[4])));

    return opcodes[which_alternative][swap];
  }"
  [(set_attr "conds" "set")
   (set_attr "length" "8")]
)

(define_insn "*cmp_and"
  [(set (match_operand 6 "dominant_cc_register" "")
	(compare
	 (and:SI
	  (match_operator 4 "arm_comparison_operator"
	   [(match_operand:SI 0 "s_register_operand" "r,r,r,r")
	    (match_operand:SI 1 "arm_add_operand" "rI,L,rI,L")])
	  (match_operator:SI 5 "arm_comparison_operator"
	   [(match_operand:SI 2 "s_register_operand" "r,r,r,r")
	    (match_operand:SI 3 "arm_add_operand" "rI,rI,L,L")]))
	 (const_int 0)))]
  "TARGET_ARM"
  "*
  {
    static const char *const opcodes[4][2] =
    {
      {\"cmp\\t%2, %3\;cmp%d5\\t%0, %1\",
       \"cmp\\t%0, %1\;cmp%d4\\t%2, %3\"},
      {\"cmp\\t%2, %3\;cmn%d5\\t%0, #%n1\",
       \"cmn\\t%0, #%n1\;cmp%d4\\t%2, %3\"},
      {\"cmn\\t%2, #%n3\;cmp%d5\\t%0, %1\",
       \"cmp\\t%0, %1\;cmn%d4\\t%2, #%n3\"},
      {\"cmn\\t%2, #%n3\;cmn%d5\\t%0, #%n1\",
       \"cmn\\t%0, #%n1\;cmn%d4\\t%2, #%n3\"}
    };
    int swap =
      comparison_dominates_p (GET_CODE (operands[5]), GET_CODE (operands[4]));

    return opcodes[which_alternative][swap];
  }"
  [(set_attr "conds" "set")
   (set_attr "predicable" "no")
   (set_attr "length" "8")]
)

(define_insn "*cmp_ior"
  [(set (match_operand 6 "dominant_cc_register" "")
	(compare
	 (ior:SI
	  (match_operator 4 "arm_comparison_operator"
	   [(match_operand:SI 0 "s_register_operand" "r,r,r,r")
	    (match_operand:SI 1 "arm_add_operand" "rI,L,rI,L")])
	  (match_operator:SI 5 "arm_comparison_operator"
	   [(match_operand:SI 2 "s_register_operand" "r,r,r,r")
	    (match_operand:SI 3 "arm_add_operand" "rI,rI,L,L")]))
	 (const_int 0)))]
  "TARGET_ARM"
  "*
{
  static const char *const opcodes[4][2] =
  {
    {\"cmp\\t%0, %1\;cmp%D4\\t%2, %3\",
     \"cmp\\t%2, %3\;cmp%D5\\t%0, %1\"},
    {\"cmn\\t%0, #%n1\;cmp%D4\\t%2, %3\",
     \"cmp\\t%2, %3\;cmn%D5\\t%0, #%n1\"},
    {\"cmp\\t%0, %1\;cmn%D4\\t%2, #%n3\",
     \"cmn\\t%2, #%n3\;cmp%D5\\t%0, %1\"},
    {\"cmn\\t%0, #%n1\;cmn%D4\\t%2, #%n3\",
     \"cmn\\t%2, #%n3\;cmn%D5\\t%0, #%n1\"}
  };
  int swap =
    comparison_dominates_p (GET_CODE (operands[5]), GET_CODE (operands[4]));

  return opcodes[which_alternative][swap];
}
"
  [(set_attr "conds" "set")
   (set_attr "length" "8")]
)

(define_insn_and_split "*ior_scc_scc"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(ior:SI (match_operator:SI 3 "arm_comparison_operator"
		 [(match_operand:SI 1 "s_register_operand" "r")
		  (match_operand:SI 2 "arm_add_operand" "rIL")])
		(match_operator:SI 6 "arm_comparison_operator"
		 [(match_operand:SI 4 "s_register_operand" "r")
		  (match_operand:SI 5 "arm_add_operand" "rIL")])))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM
   && (arm_select_dominance_cc_mode (operands[3], operands[6], DOM_CC_X_OR_Y)
       != CCmode)"
  "#"
  "TARGET_ARM && reload_completed"
  [(set (match_dup 7)
	(compare
	 (ior:SI
	  (match_op_dup 3 [(match_dup 1) (match_dup 2)])
	  (match_op_dup 6 [(match_dup 4) (match_dup 5)]))
	 (const_int 0)))
   (set (match_dup 0) (ne:SI (match_dup 7) (const_int 0)))]
  "operands[7]
     = gen_rtx_REG (arm_select_dominance_cc_mode (operands[3], operands[6],
						  DOM_CC_X_OR_Y),
		    CC_REGNUM);"
  [(set_attr "conds" "clob")
   (set_attr "length" "16")])

; If the above pattern is followed by a CMP insn, then the compare is 
; redundant, since we can rework the conditional instruction that follows.
(define_insn_and_split "*ior_scc_scc_cmp"
  [(set (match_operand 0 "dominant_cc_register" "")
	(compare (ior:SI (match_operator:SI 3 "arm_comparison_operator"
			  [(match_operand:SI 1 "s_register_operand" "r")
			   (match_operand:SI 2 "arm_add_operand" "rIL")])
			 (match_operator:SI 6 "arm_comparison_operator"
			  [(match_operand:SI 4 "s_register_operand" "r")
			   (match_operand:SI 5 "arm_add_operand" "rIL")]))
		 (const_int 0)))
   (set (match_operand:SI 7 "s_register_operand" "=r")
	(ior:SI (match_op_dup 3 [(match_dup 1) (match_dup 2)])
		(match_op_dup 6 [(match_dup 4) (match_dup 5)])))]
  "TARGET_ARM"
  "#"
  "TARGET_ARM && reload_completed"
  [(set (match_dup 0)
	(compare
	 (ior:SI
	  (match_op_dup 3 [(match_dup 1) (match_dup 2)])
	  (match_op_dup 6 [(match_dup 4) (match_dup 5)]))
	 (const_int 0)))
   (set (match_dup 7) (ne:SI (match_dup 0) (const_int 0)))]
  ""
  [(set_attr "conds" "set")
   (set_attr "length" "16")])

(define_insn_and_split "*and_scc_scc"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(and:SI (match_operator:SI 3 "arm_comparison_operator"
		 [(match_operand:SI 1 "s_register_operand" "r")
		  (match_operand:SI 2 "arm_add_operand" "rIL")])
		(match_operator:SI 6 "arm_comparison_operator"
		 [(match_operand:SI 4 "s_register_operand" "r")
		  (match_operand:SI 5 "arm_add_operand" "rIL")])))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM
   && (arm_select_dominance_cc_mode (operands[3], operands[6], DOM_CC_X_AND_Y)
       != CCmode)"
  "#"
  "TARGET_ARM && reload_completed
   && (arm_select_dominance_cc_mode (operands[3], operands[6], DOM_CC_X_AND_Y)
       != CCmode)"
  [(set (match_dup 7)
	(compare
	 (and:SI
	  (match_op_dup 3 [(match_dup 1) (match_dup 2)])
	  (match_op_dup 6 [(match_dup 4) (match_dup 5)]))
	 (const_int 0)))
   (set (match_dup 0) (ne:SI (match_dup 7) (const_int 0)))]
  "operands[7]
     = gen_rtx_REG (arm_select_dominance_cc_mode (operands[3], operands[6],
						  DOM_CC_X_AND_Y),
		    CC_REGNUM);"
  [(set_attr "conds" "clob")
   (set_attr "length" "16")])

; If the above pattern is followed by a CMP insn, then the compare is 
; redundant, since we can rework the conditional instruction that follows.
(define_insn_and_split "*and_scc_scc_cmp"
  [(set (match_operand 0 "dominant_cc_register" "")
	(compare (and:SI (match_operator:SI 3 "arm_comparison_operator"
			  [(match_operand:SI 1 "s_register_operand" "r")
			   (match_operand:SI 2 "arm_add_operand" "rIL")])
			 (match_operator:SI 6 "arm_comparison_operator"
			  [(match_operand:SI 4 "s_register_operand" "r")
			   (match_operand:SI 5 "arm_add_operand" "rIL")]))
		 (const_int 0)))
   (set (match_operand:SI 7 "s_register_operand" "=r")
	(and:SI (match_op_dup 3 [(match_dup 1) (match_dup 2)])
		(match_op_dup 6 [(match_dup 4) (match_dup 5)])))]
  "TARGET_ARM"
  "#"
  "TARGET_ARM && reload_completed"
  [(set (match_dup 0)
	(compare
	 (and:SI
	  (match_op_dup 3 [(match_dup 1) (match_dup 2)])
	  (match_op_dup 6 [(match_dup 4) (match_dup 5)]))
	 (const_int 0)))
   (set (match_dup 7) (ne:SI (match_dup 0) (const_int 0)))]
  ""
  [(set_attr "conds" "set")
   (set_attr "length" "16")])

;; If there is no dominance in the comparison, then we can still save an
;; instruction in the AND case, since we can know that the second compare
;; need only zero the value if false (if true, then the value is already
;; correct).
(define_insn_and_split "*and_scc_scc_nodom"
  [(set (match_operand:SI 0 "s_register_operand" "=&r,&r,&r")
	(and:SI (match_operator:SI 3 "arm_comparison_operator"
		 [(match_operand:SI 1 "s_register_operand" "r,r,0")
		  (match_operand:SI 2 "arm_add_operand" "rIL,0,rIL")])
		(match_operator:SI 6 "arm_comparison_operator"
		 [(match_operand:SI 4 "s_register_operand" "r,r,r")
		  (match_operand:SI 5 "arm_add_operand" "rIL,rIL,rIL")])))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM
   && (arm_select_dominance_cc_mode (operands[3], operands[6], DOM_CC_X_AND_Y)
       == CCmode)"
  "#"
  "TARGET_ARM && reload_completed"
  [(parallel [(set (match_dup 0)
		   (match_op_dup 3 [(match_dup 1) (match_dup 2)]))
	      (clobber (reg:CC CC_REGNUM))])
   (set (match_dup 7) (match_op_dup 8 [(match_dup 4) (match_dup 5)]))
   (set (match_dup 0)
	(if_then_else:SI (match_op_dup 6 [(match_dup 7) (const_int 0)])
			 (match_dup 0)
			 (const_int 0)))]
  "operands[7] = gen_rtx_REG (SELECT_CC_MODE (GET_CODE (operands[6]),
					      operands[4], operands[5]),
			      CC_REGNUM);
   operands[8] = gen_rtx_COMPARE (GET_MODE (operands[7]), operands[4],
				  operands[5]);"
  [(set_attr "conds" "clob")
   (set_attr "length" "20")])

(define_split
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (ior:SI
			  (and:SI (match_operand:SI 0 "s_register_operand" "")
				  (const_int 1))
			  (match_operator:SI 1 "comparison_operator"
			   [(match_operand:SI 2 "s_register_operand" "")
			    (match_operand:SI 3 "arm_add_operand" "")]))
			 (const_int 0)))
   (clobber (match_operand:SI 4 "s_register_operand" ""))]
  "TARGET_ARM"
  [(set (match_dup 4)
	(ior:SI (match_op_dup 1 [(match_dup 2) (match_dup 3)])
		(match_dup 0)))
   (set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (and:SI (match_dup 4) (const_int 1))
			 (const_int 0)))]
  "")

(define_split
  [(set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (ior:SI
			  (match_operator:SI 1 "comparison_operator"
			   [(match_operand:SI 2 "s_register_operand" "")
			    (match_operand:SI 3 "arm_add_operand" "")])
			  (and:SI (match_operand:SI 0 "s_register_operand" "")
				  (const_int 1)))
			 (const_int 0)))
   (clobber (match_operand:SI 4 "s_register_operand" ""))]
  "TARGET_ARM"
  [(set (match_dup 4)
	(ior:SI (match_op_dup 1 [(match_dup 2) (match_dup 3)])
		(match_dup 0)))
   (set (reg:CC_NOOV CC_REGNUM)
	(compare:CC_NOOV (and:SI (match_dup 4) (const_int 1))
			 (const_int 0)))]
  "")

(define_insn "*negscc"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(neg:SI (match_operator 3 "arm_comparison_operator"
		 [(match_operand:SI 1 "s_register_operand" "r")
		  (match_operand:SI 2 "arm_rhs_operand" "rI")])))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "*
  if (GET_CODE (operands[3]) == LT && operands[2] == const0_rtx)
    return \"mov\\t%0, %1, asr #31\";

  if (GET_CODE (operands[3]) == NE)
    return \"subs\\t%0, %1, %2\;mvnne\\t%0, #0\";

  output_asm_insn (\"cmp\\t%1, %2\", operands);
  output_asm_insn (\"mov%D3\\t%0, #0\", operands);
  return \"mvn%d3\\t%0, #0\";
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "12")]
)

(define_insn "movcond"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r")
	(if_then_else:SI
	 (match_operator 5 "arm_comparison_operator"
	  [(match_operand:SI 3 "s_register_operand" "r,r,r")
	   (match_operand:SI 4 "arm_add_operand" "rIL,rIL,rIL")])
	 (match_operand:SI 1 "arm_rhs_operand" "0,rI,?rI")
	 (match_operand:SI 2 "arm_rhs_operand" "rI,0,rI")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "*
  if (GET_CODE (operands[5]) == LT
      && (operands[4] == const0_rtx))
    {
      if (which_alternative != 1 && GET_CODE (operands[1]) == REG)
	{
	  if (operands[2] == const0_rtx)
	    return \"and\\t%0, %1, %3, asr #31\";
	  return \"ands\\t%0, %1, %3, asr #32\;movcc\\t%0, %2\";
	}
      else if (which_alternative != 0 && GET_CODE (operands[2]) == REG)
	{
	  if (operands[1] == const0_rtx)
	    return \"bic\\t%0, %2, %3, asr #31\";
	  return \"bics\\t%0, %2, %3, asr #32\;movcs\\t%0, %1\";
	}
      /* The only case that falls through to here is when both ops 1 & 2
	 are constants.  */
    }

  if (GET_CODE (operands[5]) == GE
      && (operands[4] == const0_rtx))
    {
      if (which_alternative != 1 && GET_CODE (operands[1]) == REG)
	{
	  if (operands[2] == const0_rtx)
	    return \"bic\\t%0, %1, %3, asr #31\";
	  return \"bics\\t%0, %1, %3, asr #32\;movcs\\t%0, %2\";
	}
      else if (which_alternative != 0 && GET_CODE (operands[2]) == REG)
	{
	  if (operands[1] == const0_rtx)
	    return \"and\\t%0, %2, %3, asr #31\";
	  return \"ands\\t%0, %2, %3, asr #32\;movcc\\t%0, %1\";
	}
      /* The only case that falls through to here is when both ops 1 & 2
	 are constants.  */
    }
  if (GET_CODE (operands[4]) == CONST_INT
      && !const_ok_for_arm (INTVAL (operands[4])))
    output_asm_insn (\"cmn\\t%3, #%n4\", operands);
  else
    output_asm_insn (\"cmp\\t%3, %4\", operands);
  if (which_alternative != 0)
    output_asm_insn (\"mov%d5\\t%0, %1\", operands);
  if (which_alternative != 1)
    output_asm_insn (\"mov%D5\\t%0, %2\", operands);
  return \"\";
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "8,8,12")]
)

(define_insn "*ifcompare_plus_move"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI (match_operator 6 "arm_comparison_operator"
			  [(match_operand:SI 4 "s_register_operand" "r,r")
			   (match_operand:SI 5 "arm_add_operand" "rIL,rIL")])
			 (plus:SI
			  (match_operand:SI 2 "s_register_operand" "r,r")
			  (match_operand:SI 3 "arm_add_operand" "rIL,rIL"))
			 (match_operand:SI 1 "arm_rhs_operand" "0,?rI")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_insn "*if_plus_move"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r,r")
	(if_then_else:SI
	 (match_operator 4 "arm_comparison_operator"
	  [(match_operand 5 "cc_register" "") (const_int 0)])
	 (plus:SI
	  (match_operand:SI 2 "s_register_operand" "r,r,r,r")
	  (match_operand:SI 3 "arm_add_operand" "rI,L,rI,L"))
	 (match_operand:SI 1 "arm_rhs_operand" "0,0,?rI,?rI")))]
  "TARGET_ARM"
  "@
   add%d4\\t%0, %2, %3
   sub%d4\\t%0, %2, #%n3
   add%d4\\t%0, %2, %3\;mov%D4\\t%0, %1
   sub%d4\\t%0, %2, #%n3\;mov%D4\\t%0, %1"
  [(set_attr "conds" "use")
   (set_attr "length" "4,4,8,8")
   (set_attr "type" "*,*,*,*")]
)

(define_insn "*ifcompare_move_plus"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI (match_operator 6 "arm_comparison_operator"
			  [(match_operand:SI 4 "s_register_operand" "r,r")
			   (match_operand:SI 5 "arm_add_operand" "rIL,rIL")])
			 (match_operand:SI 1 "arm_rhs_operand" "0,?rI")
			 (plus:SI
			  (match_operand:SI 2 "s_register_operand" "r,r")
			  (match_operand:SI 3 "arm_add_operand" "rIL,rIL"))))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_insn "*if_move_plus"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r,r")
	(if_then_else:SI
	 (match_operator 4 "arm_comparison_operator"
	  [(match_operand 5 "cc_register" "") (const_int 0)])
	 (match_operand:SI 1 "arm_rhs_operand" "0,0,?rI,?rI")
	 (plus:SI
	  (match_operand:SI 2 "s_register_operand" "r,r,r,r")
	  (match_operand:SI 3 "arm_add_operand" "rI,L,rI,L"))))]
  "TARGET_ARM"
  "@
   add%D4\\t%0, %2, %3
   sub%D4\\t%0, %2, #%n3
   add%D4\\t%0, %2, %3\;mov%d4\\t%0, %1
   sub%D4\\t%0, %2, #%n3\;mov%d4\\t%0, %1"
  [(set_attr "conds" "use")
   (set_attr "length" "4,4,8,8")
   (set_attr "type" "*,*,*,*")]
)

(define_insn "*ifcompare_arith_arith"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(if_then_else:SI (match_operator 9 "arm_comparison_operator"
			  [(match_operand:SI 5 "s_register_operand" "r")
			   (match_operand:SI 6 "arm_add_operand" "rIL")])
			 (match_operator:SI 8 "shiftable_operator"
			  [(match_operand:SI 1 "s_register_operand" "r")
			   (match_operand:SI 2 "arm_rhs_operand" "rI")])
			 (match_operator:SI 7 "shiftable_operator"
			  [(match_operand:SI 3 "s_register_operand" "r")
			   (match_operand:SI 4 "arm_rhs_operand" "rI")])))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  [(set_attr "conds" "clob")
   (set_attr "length" "12")]
)

(define_insn "*if_arith_arith"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(if_then_else:SI (match_operator 5 "arm_comparison_operator"
			  [(match_operand 8 "cc_register" "") (const_int 0)])
			 (match_operator:SI 6 "shiftable_operator"
			  [(match_operand:SI 1 "s_register_operand" "r")
			   (match_operand:SI 2 "arm_rhs_operand" "rI")])
			 (match_operator:SI 7 "shiftable_operator"
			  [(match_operand:SI 3 "s_register_operand" "r")
			   (match_operand:SI 4 "arm_rhs_operand" "rI")])))]
  "TARGET_ARM"
  "%I6%d5\\t%0, %1, %2\;%I7%D5\\t%0, %3, %4"
  [(set_attr "conds" "use")
   (set_attr "length" "8")]
)

(define_insn "*ifcompare_arith_move"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI (match_operator 6 "arm_comparison_operator"
			  [(match_operand:SI 2 "s_register_operand" "r,r")
			   (match_operand:SI 3 "arm_add_operand" "rIL,rIL")])
			 (match_operator:SI 7 "shiftable_operator"
			  [(match_operand:SI 4 "s_register_operand" "r,r")
			   (match_operand:SI 5 "arm_rhs_operand" "rI,rI")])
			 (match_operand:SI 1 "arm_rhs_operand" "0,?rI")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "*
  /* If we have an operation where (op x 0) is the identity operation and
     the conditional operator is LT or GE and we are comparing against zero and
     everything is in registers then we can do this in two instructions.  */
  if (operands[3] == const0_rtx
      && GET_CODE (operands[7]) != AND
      && GET_CODE (operands[5]) == REG
      && GET_CODE (operands[1]) == REG 
      && REGNO (operands[1]) == REGNO (operands[4])
      && REGNO (operands[4]) != REGNO (operands[0]))
    {
      if (GET_CODE (operands[6]) == LT)
	return \"and\\t%0, %5, %2, asr #31\;%I7\\t%0, %4, %0\";
      else if (GET_CODE (operands[6]) == GE)
	return \"bic\\t%0, %5, %2, asr #31\;%I7\\t%0, %4, %0\";
    }
  if (GET_CODE (operands[3]) == CONST_INT
      && !const_ok_for_arm (INTVAL (operands[3])))
    output_asm_insn (\"cmn\\t%2, #%n3\", operands);
  else
    output_asm_insn (\"cmp\\t%2, %3\", operands);
  output_asm_insn (\"%I7%d6\\t%0, %4, %5\", operands);
  if (which_alternative != 0)
    return \"mov%D6\\t%0, %1\";
  return \"\";
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_insn "*if_arith_move"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI (match_operator 4 "arm_comparison_operator"
			  [(match_operand 6 "cc_register" "") (const_int 0)])
			 (match_operator:SI 5 "shiftable_operator"
			  [(match_operand:SI 2 "s_register_operand" "r,r")
			   (match_operand:SI 3 "arm_rhs_operand" "rI,rI")])
			 (match_operand:SI 1 "arm_rhs_operand" "0,?rI")))]
  "TARGET_ARM"
  "@
   %I5%d4\\t%0, %2, %3
   %I5%d4\\t%0, %2, %3\;mov%D4\\t%0, %1"
  [(set_attr "conds" "use")
   (set_attr "length" "4,8")
   (set_attr "type" "*,*")]
)

(define_insn "*ifcompare_move_arith"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI (match_operator 6 "arm_comparison_operator"
			  [(match_operand:SI 4 "s_register_operand" "r,r")
			   (match_operand:SI 5 "arm_add_operand" "rIL,rIL")])
			 (match_operand:SI 1 "arm_rhs_operand" "0,?rI")
			 (match_operator:SI 7 "shiftable_operator"
			  [(match_operand:SI 2 "s_register_operand" "r,r")
			   (match_operand:SI 3 "arm_rhs_operand" "rI,rI")])))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "*
  /* If we have an operation where (op x 0) is the identity operation and
     the conditional operator is LT or GE and we are comparing against zero and
     everything is in registers then we can do this in two instructions */
  if (operands[5] == const0_rtx
      && GET_CODE (operands[7]) != AND
      && GET_CODE (operands[3]) == REG
      && GET_CODE (operands[1]) == REG 
      && REGNO (operands[1]) == REGNO (operands[2])
      && REGNO (operands[2]) != REGNO (operands[0]))
    {
      if (GET_CODE (operands[6]) == GE)
	return \"and\\t%0, %3, %4, asr #31\;%I7\\t%0, %2, %0\";
      else if (GET_CODE (operands[6]) == LT)
	return \"bic\\t%0, %3, %4, asr #31\;%I7\\t%0, %2, %0\";
    }

  if (GET_CODE (operands[5]) == CONST_INT
      && !const_ok_for_arm (INTVAL (operands[5])))
    output_asm_insn (\"cmn\\t%4, #%n5\", operands);
  else
    output_asm_insn (\"cmp\\t%4, %5\", operands);

  if (which_alternative != 0)
    output_asm_insn (\"mov%d6\\t%0, %1\", operands);
  return \"%I7%D6\\t%0, %2, %3\";
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_insn "*if_move_arith"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI
	 (match_operator 4 "arm_comparison_operator"
	  [(match_operand 6 "cc_register" "") (const_int 0)])
	 (match_operand:SI 1 "arm_rhs_operand" "0,?rI")
	 (match_operator:SI 5 "shiftable_operator"
	  [(match_operand:SI 2 "s_register_operand" "r,r")
	   (match_operand:SI 3 "arm_rhs_operand" "rI,rI")])))]
  "TARGET_ARM"
  "@
   %I5%D4\\t%0, %2, %3
   %I5%D4\\t%0, %2, %3\;mov%d4\\t%0, %1"
  [(set_attr "conds" "use")
   (set_attr "length" "4,8")
   (set_attr "type" "*,*")]
)

(define_insn "*ifcompare_move_not"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI
	 (match_operator 5 "arm_comparison_operator"
	  [(match_operand:SI 3 "s_register_operand" "r,r")
	   (match_operand:SI 4 "arm_add_operand" "rIL,rIL")])
	 (match_operand:SI 1 "arm_not_operand" "0,?rIK")
	 (not:SI
	  (match_operand:SI 2 "s_register_operand" "r,r"))))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_insn "*if_move_not"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r")
	(if_then_else:SI
	 (match_operator 4 "arm_comparison_operator"
	  [(match_operand 3 "cc_register" "") (const_int 0)])
	 (match_operand:SI 1 "arm_not_operand" "0,?rI,K")
	 (not:SI (match_operand:SI 2 "s_register_operand" "r,r,r"))))]
  "TARGET_ARM"
  "@
   mvn%D4\\t%0, %2
   mov%d4\\t%0, %1\;mvn%D4\\t%0, %2
   mvn%d4\\t%0, #%B1\;mvn%D4\\t%0, %2"
  [(set_attr "conds" "use")
   (set_attr "length" "4,8,8")]
)

(define_insn "*ifcompare_not_move"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI 
	 (match_operator 5 "arm_comparison_operator"
	  [(match_operand:SI 3 "s_register_operand" "r,r")
	   (match_operand:SI 4 "arm_add_operand" "rIL,rIL")])
	 (not:SI
	  (match_operand:SI 2 "s_register_operand" "r,r"))
	 (match_operand:SI 1 "arm_not_operand" "0,?rIK")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_insn "*if_not_move"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r")
	(if_then_else:SI
	 (match_operator 4 "arm_comparison_operator"
	  [(match_operand 3 "cc_register" "") (const_int 0)])
	 (not:SI (match_operand:SI 2 "s_register_operand" "r,r,r"))
	 (match_operand:SI 1 "arm_not_operand" "0,?rI,K")))]
  "TARGET_ARM"
  "@
   mvn%d4\\t%0, %2
   mov%D4\\t%0, %1\;mvn%d4\\t%0, %2
   mvn%D4\\t%0, #%B1\;mvn%d4\\t%0, %2"
  [(set_attr "conds" "use")
   (set_attr "length" "4,8,8")]
)

(define_insn "*ifcompare_shift_move"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI
	 (match_operator 6 "arm_comparison_operator"
	  [(match_operand:SI 4 "s_register_operand" "r,r")
	   (match_operand:SI 5 "arm_add_operand" "rIL,rIL")])
	 (match_operator:SI 7 "shift_operator"
	  [(match_operand:SI 2 "s_register_operand" "r,r")
	   (match_operand:SI 3 "arm_rhs_operand" "rM,rM")])
	 (match_operand:SI 1 "arm_not_operand" "0,?rIK")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_insn "*if_shift_move"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r")
	(if_then_else:SI
	 (match_operator 5 "arm_comparison_operator"
	  [(match_operand 6 "cc_register" "") (const_int 0)])
	 (match_operator:SI 4 "shift_operator"
	  [(match_operand:SI 2 "s_register_operand" "r,r,r")
	   (match_operand:SI 3 "arm_rhs_operand" "rM,rM,rM")])
	 (match_operand:SI 1 "arm_not_operand" "0,?rI,K")))]
  "TARGET_ARM"
  "@
   mov%d5\\t%0, %2%S4
   mov%D5\\t%0, %1\;mov%d5\\t%0, %2%S4
   mvn%D5\\t%0, #%B1\;mov%d5\\t%0, %2%S4"
  [(set_attr "conds" "use")
   (set_attr "shift" "2")
   (set_attr "length" "4,8,8")
   (set (attr "type") (if_then_else (match_operand 3 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*ifcompare_move_shift"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI
	 (match_operator 6 "arm_comparison_operator"
	  [(match_operand:SI 4 "s_register_operand" "r,r")
	   (match_operand:SI 5 "arm_add_operand" "rIL,rIL")])
	 (match_operand:SI 1 "arm_not_operand" "0,?rIK")
	 (match_operator:SI 7 "shift_operator"
	  [(match_operand:SI 2 "s_register_operand" "r,r")
	   (match_operand:SI 3 "arm_rhs_operand" "rM,rM")])))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_insn "*if_move_shift"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r")
	(if_then_else:SI
	 (match_operator 5 "arm_comparison_operator"
	  [(match_operand 6 "cc_register" "") (const_int 0)])
	 (match_operand:SI 1 "arm_not_operand" "0,?rI,K")
	 (match_operator:SI 4 "shift_operator"
	  [(match_operand:SI 2 "s_register_operand" "r,r,r")
	   (match_operand:SI 3 "arm_rhs_operand" "rM,rM,rM")])))]
  "TARGET_ARM"
  "@
   mov%D5\\t%0, %2%S4
   mov%d5\\t%0, %1\;mov%D5\\t%0, %2%S4
   mvn%d5\\t%0, #%B1\;mov%D5\\t%0, %2%S4"
  [(set_attr "conds" "use")
   (set_attr "shift" "2")
   (set_attr "length" "4,8,8")
   (set (attr "type") (if_then_else (match_operand 3 "const_int_operand" "")
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*ifcompare_shift_shift"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(if_then_else:SI
	 (match_operator 7 "arm_comparison_operator"
	  [(match_operand:SI 5 "s_register_operand" "r")
	   (match_operand:SI 6 "arm_add_operand" "rIL")])
	 (match_operator:SI 8 "shift_operator"
	  [(match_operand:SI 1 "s_register_operand" "r")
	   (match_operand:SI 2 "arm_rhs_operand" "rM")])
	 (match_operator:SI 9 "shift_operator"
	  [(match_operand:SI 3 "s_register_operand" "r")
	   (match_operand:SI 4 "arm_rhs_operand" "rM")])))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  [(set_attr "conds" "clob")
   (set_attr "length" "12")]
)

(define_insn "*if_shift_shift"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(if_then_else:SI
	 (match_operator 5 "arm_comparison_operator"
	  [(match_operand 8 "cc_register" "") (const_int 0)])
	 (match_operator:SI 6 "shift_operator"
	  [(match_operand:SI 1 "s_register_operand" "r")
	   (match_operand:SI 2 "arm_rhs_operand" "rM")])
	 (match_operator:SI 7 "shift_operator"
	  [(match_operand:SI 3 "s_register_operand" "r")
	   (match_operand:SI 4 "arm_rhs_operand" "rM")])))]
  "TARGET_ARM"
  "mov%d5\\t%0, %1%S6\;mov%D5\\t%0, %3%S7"
  [(set_attr "conds" "use")
   (set_attr "shift" "1")
   (set_attr "length" "8")
   (set (attr "type") (if_then_else
		        (and (match_operand 2 "const_int_operand" "")
                             (match_operand 4 "const_int_operand" ""))
		      (const_string "alu_shift")
		      (const_string "alu_shift_reg")))]
)

(define_insn "*ifcompare_not_arith"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(if_then_else:SI
	 (match_operator 6 "arm_comparison_operator"
	  [(match_operand:SI 4 "s_register_operand" "r")
	   (match_operand:SI 5 "arm_add_operand" "rIL")])
	 (not:SI (match_operand:SI 1 "s_register_operand" "r"))
	 (match_operator:SI 7 "shiftable_operator"
	  [(match_operand:SI 2 "s_register_operand" "r")
	   (match_operand:SI 3 "arm_rhs_operand" "rI")])))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  [(set_attr "conds" "clob")
   (set_attr "length" "12")]
)

(define_insn "*if_not_arith"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(if_then_else:SI
	 (match_operator 5 "arm_comparison_operator"
	  [(match_operand 4 "cc_register" "") (const_int 0)])
	 (not:SI (match_operand:SI 1 "s_register_operand" "r"))
	 (match_operator:SI 6 "shiftable_operator"
	  [(match_operand:SI 2 "s_register_operand" "r")
	   (match_operand:SI 3 "arm_rhs_operand" "rI")])))]
  "TARGET_ARM"
  "mvn%d5\\t%0, %1\;%I6%D5\\t%0, %2, %3"
  [(set_attr "conds" "use")
   (set_attr "length" "8")]
)

(define_insn "*ifcompare_arith_not"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(if_then_else:SI
	 (match_operator 6 "arm_comparison_operator"
	  [(match_operand:SI 4 "s_register_operand" "r")
	   (match_operand:SI 5 "arm_add_operand" "rIL")])
	 (match_operator:SI 7 "shiftable_operator"
	  [(match_operand:SI 2 "s_register_operand" "r")
	   (match_operand:SI 3 "arm_rhs_operand" "rI")])
	 (not:SI (match_operand:SI 1 "s_register_operand" "r"))))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  [(set_attr "conds" "clob")
   (set_attr "length" "12")]
)

(define_insn "*if_arith_not"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(if_then_else:SI
	 (match_operator 5 "arm_comparison_operator"
	  [(match_operand 4 "cc_register" "") (const_int 0)])
	 (match_operator:SI 6 "shiftable_operator"
	  [(match_operand:SI 2 "s_register_operand" "r")
	   (match_operand:SI 3 "arm_rhs_operand" "rI")])
	 (not:SI (match_operand:SI 1 "s_register_operand" "r"))))]
  "TARGET_ARM"
  "mvn%D5\\t%0, %1\;%I6%d5\\t%0, %2, %3"
  [(set_attr "conds" "use")
   (set_attr "length" "8")]
)

(define_insn "*ifcompare_neg_move"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI
	 (match_operator 5 "arm_comparison_operator"
	  [(match_operand:SI 3 "s_register_operand" "r,r")
	   (match_operand:SI 4 "arm_add_operand" "rIL,rIL")])
	 (neg:SI (match_operand:SI 2 "s_register_operand" "r,r"))
	 (match_operand:SI 1 "arm_not_operand" "0,?rIK")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_insn "*if_neg_move"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r")
	(if_then_else:SI
	 (match_operator 4 "arm_comparison_operator"
	  [(match_operand 3 "cc_register" "") (const_int 0)])
	 (neg:SI (match_operand:SI 2 "s_register_operand" "r,r,r"))
	 (match_operand:SI 1 "arm_not_operand" "0,?rI,K")))]
  "TARGET_ARM"
  "@
   rsb%d4\\t%0, %2, #0
   mov%D4\\t%0, %1\;rsb%d4\\t%0, %2, #0
   mvn%D4\\t%0, #%B1\;rsb%d4\\t%0, %2, #0"
  [(set_attr "conds" "use")
   (set_attr "length" "4,8,8")]
)

(define_insn "*ifcompare_move_neg"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI
	 (match_operator 5 "arm_comparison_operator"
	  [(match_operand:SI 3 "s_register_operand" "r,r")
	   (match_operand:SI 4 "arm_add_operand" "rIL,rIL")])
	 (match_operand:SI 1 "arm_not_operand" "0,?rIK")
	 (neg:SI (match_operand:SI 2 "s_register_operand" "r,r"))))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "#"
  [(set_attr "conds" "clob")
   (set_attr "length" "8,12")]
)

(define_insn "*if_move_neg"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r,r")
	(if_then_else:SI
	 (match_operator 4 "arm_comparison_operator"
	  [(match_operand 3 "cc_register" "") (const_int 0)])
	 (match_operand:SI 1 "arm_not_operand" "0,?rI,K")
	 (neg:SI (match_operand:SI 2 "s_register_operand" "r,r,r"))))]
  "TARGET_ARM"
  "@
   rsb%D4\\t%0, %2, #0
   mov%d4\\t%0, %1\;rsb%D4\\t%0, %2, #0
   mvn%d4\\t%0, #%B1\;rsb%D4\\t%0, %2, #0"
  [(set_attr "conds" "use")
   (set_attr "length" "4,8,8")]
)

(define_insn "*arith_adjacentmem"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(match_operator:SI 1 "shiftable_operator"
	 [(match_operand:SI 2 "memory_operand" "m")
	  (match_operand:SI 3 "memory_operand" "m")]))
   (clobber (match_scratch:SI 4 "=r"))]
  "TARGET_ARM && adjacent_mem_locations (operands[2], operands[3])"
  "*
  {
    rtx ldm[3];
    rtx arith[4];
    rtx base_reg;
    HOST_WIDE_INT val1 = 0, val2 = 0;

    if (REGNO (operands[0]) > REGNO (operands[4]))
      {
	ldm[1] = operands[4];
	ldm[2] = operands[0];
      }
    else
      {
	ldm[1] = operands[0];
	ldm[2] = operands[4];
      }

    base_reg = XEXP (operands[2], 0);

    if (!REG_P (base_reg))
      {
	val1 = INTVAL (XEXP (base_reg, 1));
	base_reg = XEXP (base_reg, 0);
      }

    if (!REG_P (XEXP (operands[3], 0)))
      val2 = INTVAL (XEXP (XEXP (operands[3], 0), 1));

    arith[0] = operands[0];
    arith[3] = operands[1];

    if (val1 < val2)
      {
	arith[1] = ldm[1];
	arith[2] = ldm[2];
      }
    else
      {
	arith[1] = ldm[2];
	arith[2] = ldm[1];
      }

    ldm[0] = base_reg;
    if (val1 !=0 && val2 != 0)
      {
	rtx ops[3];

	if (val1 == 4 || val2 == 4)
	  /* Other val must be 8, since we know they are adjacent and neither
	     is zero.  */
	  output_asm_insn (\"ldm%?ib\\t%0, {%1, %2}\", ldm);
	else if (const_ok_for_arm (val1) || const_ok_for_arm (-val1))
	  {
	    ldm[0] = ops[0] = operands[4];
	    ops[1] = base_reg;
	    ops[2] = GEN_INT (val1);
	    output_add_immediate (ops);
	    if (val1 < val2)
	      output_asm_insn (\"ldm%?ia\\t%0, {%1, %2}\", ldm);
	    else
	      output_asm_insn (\"ldm%?da\\t%0, {%1, %2}\", ldm);
	  }
	else
	  {
	    /* Offset is out of range for a single add, so use two ldr.  */
	    ops[0] = ldm[1];
	    ops[1] = base_reg;
	    ops[2] = GEN_INT (val1);
	    output_asm_insn (\"ldr%?\\t%0, [%1, %2]\", ops);
	    ops[0] = ldm[2];
	    ops[2] = GEN_INT (val2);
	    output_asm_insn (\"ldr%?\\t%0, [%1, %2]\", ops);
	  }
      }
    else if (val1 != 0)
      {
	if (val1 < val2)
	  output_asm_insn (\"ldm%?da\\t%0, {%1, %2}\", ldm);
	else
	  output_asm_insn (\"ldm%?ia\\t%0, {%1, %2}\", ldm);
      }
    else
      {
	if (val1 < val2)
	  output_asm_insn (\"ldm%?ia\\t%0, {%1, %2}\", ldm);
	else
	  output_asm_insn (\"ldm%?da\\t%0, {%1, %2}\", ldm);
      }
    output_asm_insn (\"%I3%?\\t%0, %1, %2\", arith);
    return \"\";
  }"
  [(set_attr "length" "12")
   (set_attr "predicable" "yes")
   (set_attr "type" "load1")]
)

; This pattern is never tried by combine, so do it as a peephole

(define_peephole2
  [(set (match_operand:SI 0 "arm_general_register_operand" "")
	(match_operand:SI 1 "arm_general_register_operand" ""))
   (set (reg:CC CC_REGNUM)
	(compare:CC (match_dup 1) (const_int 0)))]
  "TARGET_ARM"
  [(parallel [(set (reg:CC CC_REGNUM) (compare:CC (match_dup 1) (const_int 0)))
	      (set (match_dup 0) (match_dup 1))])]
  ""
)

; Peepholes to spot possible load- and store-multiples, if the ordering is
; reversed, check that the memory references aren't volatile.

(define_peephole
  [(set (match_operand:SI 0 "s_register_operand" "=r")
        (match_operand:SI 4 "memory_operand" "m"))
   (set (match_operand:SI 1 "s_register_operand" "=r")
        (match_operand:SI 5 "memory_operand" "m"))
   (set (match_operand:SI 2 "s_register_operand" "=r")
        (match_operand:SI 6 "memory_operand" "m"))
   (set (match_operand:SI 3 "s_register_operand" "=r")
        (match_operand:SI 7 "memory_operand" "m"))]
  "TARGET_ARM && load_multiple_sequence (operands, 4, NULL, NULL, NULL)"
  "*
  return emit_ldm_seq (operands, 4);
  "
)

(define_peephole
  [(set (match_operand:SI 0 "s_register_operand" "=r")
        (match_operand:SI 3 "memory_operand" "m"))
   (set (match_operand:SI 1 "s_register_operand" "=r")
        (match_operand:SI 4 "memory_operand" "m"))
   (set (match_operand:SI 2 "s_register_operand" "=r")
        (match_operand:SI 5 "memory_operand" "m"))]
  "TARGET_ARM && load_multiple_sequence (operands, 3, NULL, NULL, NULL)"
  "*
  return emit_ldm_seq (operands, 3);
  "
)

(define_peephole
  [(set (match_operand:SI 0 "s_register_operand" "=r")
        (match_operand:SI 2 "memory_operand" "m"))
   (set (match_operand:SI 1 "s_register_operand" "=r")
        (match_operand:SI 3 "memory_operand" "m"))]
  "TARGET_ARM && load_multiple_sequence (operands, 2, NULL, NULL, NULL)"
  "*
  return emit_ldm_seq (operands, 2);
  "
)

(define_peephole
  [(set (match_operand:SI 4 "memory_operand" "=m")
        (match_operand:SI 0 "s_register_operand" "r"))
   (set (match_operand:SI 5 "memory_operand" "=m")
        (match_operand:SI 1 "s_register_operand" "r"))
   (set (match_operand:SI 6 "memory_operand" "=m")
        (match_operand:SI 2 "s_register_operand" "r"))
   (set (match_operand:SI 7 "memory_operand" "=m")
        (match_operand:SI 3 "s_register_operand" "r"))]
  "TARGET_ARM && store_multiple_sequence (operands, 4, NULL, NULL, NULL)"
  "*
  return emit_stm_seq (operands, 4);
  "
)

(define_peephole
  [(set (match_operand:SI 3 "memory_operand" "=m")
        (match_operand:SI 0 "s_register_operand" "r"))
   (set (match_operand:SI 4 "memory_operand" "=m")
        (match_operand:SI 1 "s_register_operand" "r"))
   (set (match_operand:SI 5 "memory_operand" "=m")
        (match_operand:SI 2 "s_register_operand" "r"))]
  "TARGET_ARM && store_multiple_sequence (operands, 3, NULL, NULL, NULL)"
  "*
  return emit_stm_seq (operands, 3);
  "
)

(define_peephole
  [(set (match_operand:SI 2 "memory_operand" "=m")
        (match_operand:SI 0 "s_register_operand" "r"))
   (set (match_operand:SI 3 "memory_operand" "=m")
        (match_operand:SI 1 "s_register_operand" "r"))]
  "TARGET_ARM && store_multiple_sequence (operands, 2, NULL, NULL, NULL)"
  "*
  return emit_stm_seq (operands, 2);
  "
)

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(and:SI (ge:SI (match_operand:SI 1 "s_register_operand" "")
		       (const_int 0))
		(neg:SI (match_operator:SI 2 "arm_comparison_operator"
			 [(match_operand:SI 3 "s_register_operand" "")
			  (match_operand:SI 4 "arm_rhs_operand" "")]))))
   (clobber (match_operand:SI 5 "s_register_operand" ""))]
  "TARGET_ARM"
  [(set (match_dup 5) (not:SI (ashiftrt:SI (match_dup 1) (const_int 31))))
   (set (match_dup 0) (and:SI (match_op_dup 2 [(match_dup 3) (match_dup 4)])
			      (match_dup 5)))]
  ""
)

;; This split can be used because CC_Z mode implies that the following
;; branch will be an equality, or an unsigned inequality, so the sign
;; extension is not needed.

(define_split
  [(set (reg:CC_Z CC_REGNUM)
	(compare:CC_Z
	 (ashift:SI (subreg:SI (match_operand:QI 0 "memory_operand" "") 0)
		    (const_int 24))
	 (match_operand 1 "const_int_operand" "")))
   (clobber (match_scratch:SI 2 ""))]
  "TARGET_ARM
   && (((unsigned HOST_WIDE_INT) INTVAL (operands[1]))
       == (((unsigned HOST_WIDE_INT) INTVAL (operands[1])) >> 24) << 24)"
  [(set (match_dup 2) (zero_extend:SI (match_dup 0)))
   (set (reg:CC CC_REGNUM) (compare:CC (match_dup 2) (match_dup 1)))]
  "
  operands[1] = GEN_INT (((unsigned long) INTVAL (operands[1])) >> 24);
  "
)

(define_expand "prologue"
  [(clobber (const_int 0))]
  "TARGET_EITHER"
  "if (TARGET_ARM)
     arm_expand_prologue ();
   else
     thumb_expand_prologue ();
  DONE;
  "
)

(define_expand "epilogue"
  [(clobber (const_int 0))]
  "TARGET_EITHER"
  "
  if (current_function_calls_eh_return)
    emit_insn (gen_prologue_use (gen_rtx_REG (Pmode, 2)));
  if (TARGET_THUMB)
    thumb_expand_epilogue ();
  else if (USE_RETURN_INSN (FALSE))
    {
      emit_jump_insn (gen_return ());
      DONE;
    }
  emit_jump_insn (gen_rtx_UNSPEC_VOLATILE (VOIDmode,
	gen_rtvec (1,
		gen_rtx_RETURN (VOIDmode)),
	VUNSPEC_EPILOGUE));
  DONE;
  "
)

;; Note - although unspec_volatile's USE all hard registers,
;; USEs are ignored after relaod has completed.  Thus we need
;; to add an unspec of the link register to ensure that flow
;; does not think that it is unused by the sibcall branch that
;; will replace the standard function epilogue.
(define_insn "sibcall_epilogue"
  [(parallel [(unspec:SI [(reg:SI LR_REGNUM)] UNSPEC_PROLOGUE_USE)
              (unspec_volatile [(return)] VUNSPEC_EPILOGUE)])]
  "TARGET_ARM"
  "*
  if (use_return_insn (FALSE, next_nonnote_insn (insn)))
    return output_return_instruction (const_true_rtx, FALSE, FALSE);
  return arm_output_epilogue (next_nonnote_insn (insn));
  "
;; Length is absolute worst case
  [(set_attr "length" "44")
   (set_attr "type" "block")
   ;; We don't clobber the conditions, but the potential length of this
   ;; operation is sufficient to make conditionalizing the sequence 
   ;; unlikely to be profitable.
   (set_attr "conds" "clob")]
)

(define_insn "*epilogue_insns"
  [(unspec_volatile [(return)] VUNSPEC_EPILOGUE)]
  "TARGET_EITHER"
  "*
  if (TARGET_ARM)
    return arm_output_epilogue (NULL);
  else /* TARGET_THUMB */
    return thumb_unexpanded_epilogue ();
  "
  ; Length is absolute worst case
  [(set_attr "length" "44")
   (set_attr "type" "block")
   ;; We don't clobber the conditions, but the potential length of this
   ;; operation is sufficient to make conditionalizing the sequence 
   ;; unlikely to be profitable.
   (set_attr "conds" "clob")]
)

(define_expand "eh_epilogue"
  [(use (match_operand:SI 0 "register_operand" ""))
   (use (match_operand:SI 1 "register_operand" ""))
   (use (match_operand:SI 2 "register_operand" ""))]
  "TARGET_EITHER"
  "
  {
    cfun->machine->eh_epilogue_sp_ofs = operands[1];
    if (GET_CODE (operands[2]) != REG || REGNO (operands[2]) != 2)
      {
	rtx ra = gen_rtx_REG (Pmode, 2);

	emit_move_insn (ra, operands[2]);
	operands[2] = ra;
      }
    /* This is a hack -- we may have crystalized the function type too
       early.  */
    cfun->machine->func_type = 0;
  }"
)

;; This split is only used during output to reduce the number of patterns
;; that need assembler instructions adding to them.  We allowed the setting
;; of the conditions to be implicit during rtl generation so that
;; the conditional compare patterns would work.  However this conflicts to
;; some extent with the conditional data operations, so we have to split them
;; up again here.

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(if_then_else:SI (match_operator 1 "arm_comparison_operator"
			  [(match_operand 2 "" "") (match_operand 3 "" "")])
			 (match_dup 0)
			 (match_operand 4 "" "")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM && reload_completed"
  [(set (match_dup 5) (match_dup 6))
   (cond_exec (match_dup 7)
	      (set (match_dup 0) (match_dup 4)))]
  "
  {
    enum machine_mode mode = SELECT_CC_MODE (GET_CODE (operands[1]),
					     operands[2], operands[3]);
    enum rtx_code rc = GET_CODE (operands[1]);

    operands[5] = gen_rtx_REG (mode, CC_REGNUM);
    operands[6] = gen_rtx_COMPARE (mode, operands[2], operands[3]);
    if (mode == CCFPmode || mode == CCFPEmode)
      rc = reverse_condition_maybe_unordered (rc);
    else
      rc = reverse_condition (rc);

    operands[7] = gen_rtx_fmt_ee (rc, VOIDmode, operands[5], const0_rtx);
  }"
)

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(if_then_else:SI (match_operator 1 "arm_comparison_operator"
			  [(match_operand 2 "" "") (match_operand 3 "" "")])
			 (match_operand 4 "" "")
			 (match_dup 0)))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM && reload_completed"
  [(set (match_dup 5) (match_dup 6))
   (cond_exec (match_op_dup 1 [(match_dup 5) (const_int 0)])
	      (set (match_dup 0) (match_dup 4)))]
  "
  {
    enum machine_mode mode = SELECT_CC_MODE (GET_CODE (operands[1]),
					     operands[2], operands[3]);

    operands[5] = gen_rtx_REG (mode, CC_REGNUM);
    operands[6] = gen_rtx_COMPARE (mode, operands[2], operands[3]);
  }"
)

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(if_then_else:SI (match_operator 1 "arm_comparison_operator"
			  [(match_operand 2 "" "") (match_operand 3 "" "")])
			 (match_operand 4 "" "")
			 (match_operand 5 "" "")))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM && reload_completed"
  [(set (match_dup 6) (match_dup 7))
   (cond_exec (match_op_dup 1 [(match_dup 6) (const_int 0)])
	      (set (match_dup 0) (match_dup 4)))
   (cond_exec (match_dup 8)
	      (set (match_dup 0) (match_dup 5)))]
  "
  {
    enum machine_mode mode = SELECT_CC_MODE (GET_CODE (operands[1]),
					     operands[2], operands[3]);
    enum rtx_code rc = GET_CODE (operands[1]);

    operands[6] = gen_rtx_REG (mode, CC_REGNUM);
    operands[7] = gen_rtx_COMPARE (mode, operands[2], operands[3]);
    if (mode == CCFPmode || mode == CCFPEmode)
      rc = reverse_condition_maybe_unordered (rc);
    else
      rc = reverse_condition (rc);

    operands[8] = gen_rtx_fmt_ee (rc, VOIDmode, operands[6], const0_rtx);
  }"
)

(define_split
  [(set (match_operand:SI 0 "s_register_operand" "")
	(if_then_else:SI (match_operator 1 "arm_comparison_operator"
			  [(match_operand:SI 2 "s_register_operand" "")
			   (match_operand:SI 3 "arm_add_operand" "")])
			 (match_operand:SI 4 "arm_rhs_operand" "")
			 (not:SI
			  (match_operand:SI 5 "s_register_operand" ""))))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM && reload_completed"
  [(set (match_dup 6) (match_dup 7))
   (cond_exec (match_op_dup 1 [(match_dup 6) (const_int 0)])
	      (set (match_dup 0) (match_dup 4)))
   (cond_exec (match_dup 8)
	      (set (match_dup 0) (not:SI (match_dup 5))))]
  "
  {
    enum machine_mode mode = SELECT_CC_MODE (GET_CODE (operands[1]),
					     operands[2], operands[3]);
    enum rtx_code rc = GET_CODE (operands[1]);

    operands[6] = gen_rtx_REG (mode, CC_REGNUM);
    operands[7] = gen_rtx_COMPARE (mode, operands[2], operands[3]);
    if (mode == CCFPmode || mode == CCFPEmode)
      rc = reverse_condition_maybe_unordered (rc);
    else
      rc = reverse_condition (rc);

    operands[8] = gen_rtx_fmt_ee (rc, VOIDmode, operands[6], const0_rtx);
  }"
)

(define_insn "*cond_move_not"
  [(set (match_operand:SI 0 "s_register_operand" "=r,r")
	(if_then_else:SI (match_operator 4 "arm_comparison_operator"
			  [(match_operand 3 "cc_register" "") (const_int 0)])
			 (match_operand:SI 1 "arm_rhs_operand" "0,?rI")
			 (not:SI
			  (match_operand:SI 2 "s_register_operand" "r,r"))))]
  "TARGET_ARM"
  "@
   mvn%D4\\t%0, %2
   mov%d4\\t%0, %1\;mvn%D4\\t%0, %2"
  [(set_attr "conds" "use")
   (set_attr "length" "4,8")]
)

;; The next two patterns occur when an AND operation is followed by a
;; scc insn sequence 

(define_insn "*sign_extract_onebit"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(sign_extract:SI (match_operand:SI 1 "s_register_operand" "r")
			 (const_int 1)
			 (match_operand:SI 2 "const_int_operand" "n")))
    (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "*
    operands[2] = GEN_INT (1 << INTVAL (operands[2]));
    output_asm_insn (\"ands\\t%0, %1, %2\", operands);
    return \"mvnne\\t%0, #0\";
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "8")]
)

(define_insn "*not_signextract_onebit"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(not:SI
	 (sign_extract:SI (match_operand:SI 1 "s_register_operand" "r")
			  (const_int 1)
			  (match_operand:SI 2 "const_int_operand" "n"))))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_ARM"
  "*
    operands[2] = GEN_INT (1 << INTVAL (operands[2]));
    output_asm_insn (\"tst\\t%1, %2\", operands);
    output_asm_insn (\"mvneq\\t%0, #0\", operands);
    return \"movne\\t%0, #0\";
  "
  [(set_attr "conds" "clob")
   (set_attr "length" "12")]
)

;; Push multiple registers to the stack.  Registers are in parallel (use ...)
;; expressions.  For simplicity, the first register is also in the unspec
;; part.
(define_insn "*push_multi"
  [(match_parallel 2 "multi_register_push"
    [(set (match_operand:BLK 0 "memory_operand" "=m")
	  (unspec:BLK [(match_operand:SI 1 "s_register_operand" "r")]
		      UNSPEC_PUSH_MULT))])]
  "TARGET_ARM"
  "*
  {
    int num_saves = XVECLEN (operands[2], 0);
     
    /* For the StrongARM at least it is faster to
       use STR to store only a single register.  */
    if (num_saves == 1)
      output_asm_insn (\"str\\t%1, [%m0, #-4]!\", operands);
    else
      {
	int i;
	char pattern[100];

	strcpy (pattern, \"stmfd\\t%m0!, {%1\");

	for (i = 1; i < num_saves; i++)
	  {
	    strcat (pattern, \", %|\");
	    strcat (pattern,
		    reg_names[REGNO (XEXP (XVECEXP (operands[2], 0, i), 0))]);
	  }

	strcat (pattern, \"}\");
	output_asm_insn (pattern, operands);
      }

    return \"\";
  }"
  [(set_attr "type" "store4")]
)

(define_insn "stack_tie"
  [(set (mem:BLK (scratch))
	(unspec:BLK [(match_operand:SI 0 "s_register_operand" "r")
		     (match_operand:SI 1 "s_register_operand" "r")]
		    UNSPEC_PRLG_STK))]
  ""
  ""
  [(set_attr "length" "0")]
)

;; Similarly for the floating point registers
(define_insn "*push_fp_multi"
  [(match_parallel 2 "multi_register_push"
    [(set (match_operand:BLK 0 "memory_operand" "=m")
	  (unspec:BLK [(match_operand:XF 1 "f_register_operand" "f")]
		      UNSPEC_PUSH_MULT))])]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "*
  {
    char pattern[100];

    sprintf (pattern, \"sfmfd\\t%%1, %d, [%%m0]!\", XVECLEN (operands[2], 0));
    output_asm_insn (pattern, operands);
    return \"\";
  }"
  [(set_attr "type" "f_store")]
)

;; Special patterns for dealing with the constant pool

(define_insn "align_4"
  [(unspec_volatile [(const_int 0)] VUNSPEC_ALIGN)]
  "TARGET_EITHER"
  "*
  assemble_align (32);
  return \"\";
  "
)

(define_insn "align_8"
  [(unspec_volatile [(const_int 0)] VUNSPEC_ALIGN8)]
  "TARGET_EITHER"
  "*
  assemble_align (64);
  return \"\";
  "
)

(define_insn "consttable_end"
  [(unspec_volatile [(const_int 0)] VUNSPEC_POOL_END)]
  "TARGET_EITHER"
  "*
  making_const_table = FALSE;
  return \"\";
  "
)

(define_insn "consttable_1"
  [(unspec_volatile [(match_operand 0 "" "")] VUNSPEC_POOL_1)]
  "TARGET_THUMB"
  "*
  making_const_table = TRUE;
  assemble_integer (operands[0], 1, BITS_PER_WORD, 1);
  assemble_zeros (3);
  return \"\";
  "
  [(set_attr "length" "4")]
)

(define_insn "consttable_2"
  [(unspec_volatile [(match_operand 0 "" "")] VUNSPEC_POOL_2)]
  "TARGET_THUMB"
  "*
  making_const_table = TRUE;
  assemble_integer (operands[0], 2, BITS_PER_WORD, 1);
  assemble_zeros (2);
  return \"\";
  "
  [(set_attr "length" "4")]
)

(define_insn "consttable_4"
  [(unspec_volatile [(match_operand 0 "" "")] VUNSPEC_POOL_4)]
  "TARGET_EITHER"
  "*
  {
    making_const_table = TRUE;
    switch (GET_MODE_CLASS (GET_MODE (operands[0])))
      {
      case MODE_FLOAT:
      {
        REAL_VALUE_TYPE r;
        REAL_VALUE_FROM_CONST_DOUBLE (r, operands[0]);
        assemble_real (r, GET_MODE (operands[0]), BITS_PER_WORD);
        break;
      }
      default:
        assemble_integer (operands[0], 4, BITS_PER_WORD, 1);
        break;
      }
    return \"\";
  }"
  [(set_attr "length" "4")]
)

(define_insn "consttable_8"
  [(unspec_volatile [(match_operand 0 "" "")] VUNSPEC_POOL_8)]
  "TARGET_EITHER"
  "*
  {
    making_const_table = TRUE;
    switch (GET_MODE_CLASS (GET_MODE (operands[0])))
      {
       case MODE_FLOAT:
        {
          REAL_VALUE_TYPE r;
          REAL_VALUE_FROM_CONST_DOUBLE (r, operands[0]);
          assemble_real (r, GET_MODE (operands[0]), BITS_PER_WORD);
          break;
        }
      default:
        assemble_integer (operands[0], 8, BITS_PER_WORD, 1);
        break;
      }
    return \"\";
  }"
  [(set_attr "length" "8")]
)

;; Miscellaneous Thumb patterns

(define_expand "tablejump"
  [(parallel [(set (pc) (match_operand:SI 0 "register_operand" ""))
	      (use (label_ref (match_operand 1 "" "")))])]
  "TARGET_THUMB"
  "
  if (flag_pic)
    {
      /* Hopefully, CSE will eliminate this copy.  */
      rtx reg1 = copy_addr_to_reg (gen_rtx_LABEL_REF (Pmode, operands[1]));
      rtx reg2 = gen_reg_rtx (SImode);

      emit_insn (gen_addsi3 (reg2, operands[0], reg1));
      operands[0] = reg2;
    }
  "
)

;; NB never uses BX.
(define_insn "*thumb_tablejump"
  [(set (pc) (match_operand:SI 0 "register_operand" "l*r"))
   (use (label_ref (match_operand 1 "" "")))]
  "TARGET_THUMB"
  "mov\\t%|pc, %0"
  [(set_attr "length" "2")]
)

;; V5 Instructions,

(define_insn "clzsi2"
  [(set (match_operand:SI 0 "s_register_operand" "=r")
	(clz:SI (match_operand:SI 1 "s_register_operand" "r")))]
  "TARGET_ARM && arm_arch5"
  "clz%?\\t%0, %1"
  [(set_attr "predicable" "yes")])

(define_expand "ffssi2"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(ffs:SI (match_operand:SI 1 "s_register_operand" "")))]
  "TARGET_ARM && arm_arch5"
  "
  {
    rtx t1, t2, t3;

    t1 = gen_reg_rtx (SImode);
    t2 = gen_reg_rtx (SImode);
    t3 = gen_reg_rtx (SImode);

    emit_insn (gen_negsi2 (t1, operands[1]));
    emit_insn (gen_andsi3 (t2, operands[1], t1));
    emit_insn (gen_clzsi2 (t3, t2));
    emit_insn (gen_subsi3 (operands[0], GEN_INT (32), t3));
    DONE;
  }"
)

(define_expand "ctzsi2"
  [(set (match_operand:SI 0 "s_register_operand" "")
	(ctz:SI (match_operand:SI 1 "s_register_operand" "")))]
  "TARGET_ARM && arm_arch5"
  "
  {
    rtx t1, t2, t3;

    t1 = gen_reg_rtx (SImode);
    t2 = gen_reg_rtx (SImode);
    t3 = gen_reg_rtx (SImode);

    emit_insn (gen_negsi2 (t1, operands[1]));
    emit_insn (gen_andsi3 (t2, operands[1], t1));
    emit_insn (gen_clzsi2 (t3, t2));
    emit_insn (gen_subsi3 (operands[0], GEN_INT (31), t3));
    DONE;
  }"
)

;; V5E instructions.

(define_insn "prefetch"
  [(prefetch (match_operand:SI 0 "address_operand" "p")
	     (match_operand:SI 1 "" "")
	     (match_operand:SI 2 "" ""))]
  "TARGET_ARM && arm_arch5e"
  "pld\\t%a0")

;; General predication pattern

(define_cond_exec
  [(match_operator 0 "arm_comparison_operator"
    [(match_operand 1 "cc_register" "")
     (const_int 0)])]
  "TARGET_ARM"
  ""
)

(define_insn "prologue_use"
  [(unspec:SI [(match_operand:SI 0 "register_operand" "")] UNSPEC_PROLOGUE_USE)]
  ""
  "%@ %0 needed for prologue"
)


;; Patterns for exception handling

(define_expand "eh_return"
  [(use (match_operand 0 "general_operand" ""))]
  "TARGET_EITHER"
  "
  {
    if (TARGET_ARM)
      emit_insn (gen_arm_eh_return (operands[0]));
    else
      emit_insn (gen_thumb_eh_return (operands[0]));
    DONE;
  }"
)
				   
;; We can't expand this before we know where the link register is stored.
(define_insn_and_split "arm_eh_return"
  [(unspec_volatile [(match_operand:SI 0 "s_register_operand" "r")]
		    VUNSPEC_EH_RETURN)
   (clobber (match_scratch:SI 1 "=&r"))]
  "TARGET_ARM"
  "#"
  "&& reload_completed"
  [(const_int 0)]
  "
  {
    arm_set_return_address (operands[0], operands[1]);
    DONE;
  }"
)

(define_insn_and_split "thumb_eh_return"
  [(unspec_volatile [(match_operand:SI 0 "s_register_operand" "l")]
		    VUNSPEC_EH_RETURN)
   (clobber (match_scratch:SI 1 "=&l"))]
  "TARGET_THUMB"
  "#"
  "&& reload_completed"
  [(const_int 0)]
  "
  {
    thumb_set_return_address (operands[0], operands[1]);
    DONE;
  }"
)


;; TLS support

(define_insn "load_tp_hard"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(const_int 0)] UNSPEC_TLS))]
  "TARGET_HARD_TP"
  "mrc%?\\tp15, 0, %0, c13, c0, 3\\t@ load_tp_hard"
  [(set_attr "predicable" "yes")]
)

;; Doesn't clobber R1-R3.  Must use r0 for the first operand.
(define_insn "load_tp_soft"
  [(set (reg:SI 0) (unspec:SI [(const_int 0)] UNSPEC_TLS))
   (clobber (reg:SI LR_REGNUM))
   (clobber (reg:SI IP_REGNUM))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_SOFT_TP"
  "bl\\t__aeabi_read_tp\\t@ load_tp_soft"
  [(set_attr "conds" "clob")]
)

;; Load the FPA co-processor patterns
(include "fpa.md")
;; Load the Maverick co-processor patterns
(include "cirrus.md")
;; Load the Intel Wireless Multimedia Extension patterns
(include "iwmmxt.md")
;; Load the VFP co-processor patterns
(include "vfp.md")


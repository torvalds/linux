;; Machine description for the TMS320C[34]x for GCC
;; Copyright (C) 1994, 1995, 1996, 1997, 1998,
;; 1999, 2000, 2002, 2004, 2005 Free Software Foundation, Inc.

;; Contributed by Michael Hayes (m.hayes@elec.canterbury.ac.nz)
;;            and Herman Ten Brugge (Haj.Ten.Brugge@net.HCC.nl)

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

;
; TODO :
;        Try using PQImode again for addresses since C30 only uses
;        24-bit addresses.   Ideally GCC would emit different insns
;        for QImode and Pmode, whether Pmode was QImode or PQImode.
;        For addresses we wouldn't have to have a clobber of the CC
;        associated with each insn and we could use MPYI in address
;        calculations without having to synthesize a proper 32 bit multiply.

; Additional C30/C40 instructions not coded:
; CALLcond, IACK, IDLE, LDE, LDFI, LDII, LDM, NORM, RETIcond
; ROLC, RORC, SIGI, STFI, STII, SUBC, SWI

; Additional C40 instructions not coded:
; LDEP, LDPE, LWRct, LAJcond, RETIcondD

;
; C4x MODES
;
; QImode                char, short, int, long (32-bits)
; HImode                long long              (64-bits)
; QFmode                float, double          (32-bits)
; HFmode                long double            (40-bits)
; CCmode		
; CC_NOOVmode		

;
; C4x PREDICATES:
;
; comparison_operator   LT, GT, LE, GE, LTU, GTU, LEU, GEU, EQ, NE
; memory_operand        memory                                     [m]
; immediate_operand     immediate constant                         [IKN]
; register_operand      register                                   [rf]
; general_operand       register, memory, constant                 [rfmI]

; addr_reg_operand      AR0-AR7, pseudo reg                        [a]
; sp_reg_operand        SP                                         [b]
; std_reg_operand       AR0-AR7, IR0-IR1, RC, RS, RE, SP, pseudo   [c]
; ext_reg_operand       R0-R11, pseudo reg                         [f]
; ext_low_reg_operand   R0-R7, pseudo reg                          [q]
; index_reg_operand     IR0-IR1, pseudo reg                        [x]
; st_reg_operand        ST                                         [y]
; dp_reg_operand        DP                                         [z]
; stik_const_operand    5-bit const                                [K]
; src_operand           general operand                            [rfHmI]
; par_ind_operand       indirect S mode (ARx + 0, 1, IRx)          [S<>]
; parallel_operand      par_ind_operand or ext_low_reg_operand
; symbolic_address_operand
; call_address_operand

; ADDI src2, src1, dst  three operand op
; ADDI src, dst         two operand op

;  Note that the predicates are only used when selecting a pattern
;  to determine if an operand is valid.

;  The constraints then select which of the possible valid operands
;  is present (and guide register selection). The actual assembly
;  instruction is then selected on the basis of the constraints.

;  The extra constraint (valid_operands) is used to determine if
;  the combination of operands is legitimate for the pattern.

;
; C4x CONSTRAINTS:
;
; a   address reg          AR0-AR7
; b   stack pointer        SP
; c   other int reg        AR0-AR7, IR0-IR1, RC, RS, RE
; d   fp reg               R0-R11 (sets CC when dst) 
; e
; f   fp reg               R0-R11 (sets CC when dst)
; g   general reg, memory, constant
; h   fp reg (HFmode)      R0-R11 (sets CC when dst) 
; i   immediate int constant
; j
; k   block count          BK
; l
; m   memory
; n   immediate int constant with known numeric value
; o   offsettable memory
; p   memory address
; q   low fp reg           R0-R7  (sets CC when dst)
; r   general reg          R0-R11, AR0-AR7, IR0-IR1, RC, RS, RE
; s   immediate int constant (value not explicit)
; t                        R0-R1
; u                        R2-R3
; v   repeat count reg     RC
; w
; x   index reg            IR0-IR1
; y   status (CC) reg      ST
; z   data pointer         DP

; G   fp zero
; H   fp 16-bit constant
; I   signed 16-bit
; J   signed 8-bit    (C4x only)
; K   signed 5-bit    (C4x only)
; L   unsigned 16-bit
; M   unsigned 8-bit  (C4x only)
; N   ones complement of unsigned 16-bit
; O   16 bit high constant
; Q   ARx + 9-bit signed disp
; R   ARx + 5-bit unsigned disp  (C4x only)
; S   ARx + 0, 1, IRx disp
; T   direct memory operand
; V   non offsettable memory
; X   any operand
; <   memory operand with autodecrement addressing
; >   memory operand with autoincrement addressing
; {   memory operand with pre-modify addressing
; }   memory operand with post-modify addressing

;  Note that the 'd', 'f', and 'h' constraints are equivalent.
;  The m constraint is equivalent to 'QT<>{}'

;  Note we cannot use the 'g' constraint with Pmode (i.e, QImode)
;  operations since LEGITIMATE_CONSTANT_P accepts SYMBOL_REF.
;  So instead we use 'rIm' for signed operands or 'rLm' for unsigned operands.

;  Note that the constraints are used to select the operands
;  for a chosen pattern.  The constraint that requires the fewest
;  instructions to load an operand is chosen.

;  Note that the 'r' constraint is mostly only used for src integer register 
;  operands,  while 'c' and 'd' constraints are generally only used for dst
;  integer register operands (the 'r' constraint is the union of the 'c' and
;  'd' constraints).  When a register satisfying the 'd' constraint
;  is used as a dst operand, the CC gets clobbered (except for LDIcond)---but 
;  not for 'c'.

;  The 'f' constraint is only for float register operands---when 
;  a register satisfying the 'f' constraint is used as a dst operand,
;  the CC gets clobbered (except for LDFcond).

;  The ! in front of the 'b' constraint says to GCC to disparage the
;  use of this constraint.  The 'b' constraint applies only to the SP.

;  Note that we deal with the condition code CC like some of the RISC
;  architectures (arm, sh, sparc) where it is stored in a general register,
;  in this case the hard register ST (21).  Unlike these other architectures
;  that do not set the CC with many instructions, the C[34]x architectures
;  sets the CC for many instructions when the destination register is
;  an extended precision register.  While it would have been easier
;  to use the generic cc0 register to store the CC, as with most of
;  the other ported architectures, this constrains the setting and testing
;  of the CC to be consecutive insns.  Thus we would reduce the benefit
;  of scheduling instructions to avoid pipeline conflicts and filling of
;  delayed branch slots.

;  Since the C[34]x has many instructions that set the CC, we pay the
;  price of having to explicitly define which insns clobber the CC
;  (rather than using the macro NOTICE_UPDATE_CC). 

;  Note that many patterns say that the CC is clobbered when in fact
;  that it may not be (depending on the destination register).
;  We have to cover ourselves if an extended precision register
;  is allocated to the destination register.
;  Unfortunately, it is not easy to tell GCC that the clobbering of CC
;  is register dependent.  If we could tolerate the ST register being
;  copied about, then we could store the CC in a pseudo register and
;  use constructs such as (clobber (match_scratch:CC N "&y,X")) to
;  indicate that the 'y' class (ST register) is clobbered for the
;  first combination of operands but not with the second.
;  I tried this approach for a while but reload got unhappy since I
;  didn't allow it to move the CC around.

;  Note that fundamental operations, such as moves, must not clobber the
;  CC.  Thus movqi chooses a move instruction that doesn't clobber the CC.
;  If GCC wants to combine a move with a compare, it is smart enough to
;  chose the move instruction that sets the CC.

;  Unfortunately, the C[34]x instruction set does not have arithmetic or
;  logical operations that never touch the CC.  We thus have to assume
;  that the CC may be clobbered at all times.  If we define patterns
;  such as addqi without the clobber of CC, then GCC will be forced
;  to use registers such as the auxiliary registers which can cause
;  horrible pipeline conflicts.  The tradeoff is that GCC can't now
;  sneak in an add instruction between setting and testing of the CC.

;  Most of the C[34]x instructions require operands of the following formats,
;  where imm represents an immediate constant, dir a direct memory reference,
;  ind an indirect memory reference, and reg a register:

;        src2 (op2)             src1 (op1)      dst (op0)
; imm  dir  ind  reg  |  imm  dir  ind  reg  |  reg      Notes
;---------------------+----------------------+------
; ILH   T   Q<>   r   |   -    -    -    0   |   r       2 operand
;  -    -   S<>   r   |   -    -   S<>   r   |   r       
;  J    -    R    -   |   -    -    R    r   |   r       C4x

;  Arithmetic operations use the I, J constraints for immediate constants,
;  while logical operations use the L, J constraints.  Floating point
;  operations use the H constraint for immediate constants.

;  With most instructions the src2 and src1 operands are commutative
;  (except for SUB, SUBR, ANDN).  The assembler considers
;  ADDI 10, R0, R1 and ADDI R0, 10, R1 to be equivalent.
;  We thus match src2 and src1 with the src_operand predicate and
;  use valid_operands as the extra constraint to reject invalid
;  operand combinations.  For example, ADDI @foo, @bar, R0.

;  Note that we use the ? modifier so that reload doesn't preferentially
;  try the alternative where three registers are acceptable as
;  operands (whenever an operand requires reloading).  Instead it will try
;  the 2 operand form which will produce better code since it won't require
;  a new spill register.

;  Note that the floating point representation of 0.0 on the C4x
;  is 0x80000000 (-2147483648).  This value produces a warning
;  message on 32-bit machines about the decimal constant being so large
;  that it is unsigned.

;  With two operand instructions patterns having two sets,
;  the compare set must come first to keep the combiner happy.
;  While the combiner seems to cope most of the time with the
;  compare set coming second, it's best to have it first.

;
; C4x CONSTANT attributes
;
(define_attr "cpu" "c4x,c3x"
 (const
  (cond [(symbol_ref "TARGET_C3X") (const_string "c3x")]
         (const_string "c4x"))))

;
; C4x INSN ATTRIBUTES:
;
; lda           load address, non-clobber CC
; store         memory store, non-clobber CC
; load_load     parallel memory loads, non-clobber CC
; load_store    parallel memory load and store, non-clobber CC
; store_load    parallel memory store and load, non-clobber CC
; store_store   parallel memory stores, non-clobber CC
; unary         two operand arithmetic, non-clobber CC
; unarycc       two operand arithmetic, clobber CC
; binary        three operand arithmetic, non-clobber CC
; binarycc      three operand arithmetic, clobber CC
; compare       compare, clobber CC
; call          function call
; rets          return from subroutine
; jump          unconditional branch
; jmpc          conditional branch
; db            decrement and branch (unconditional)
; dbc           decrement and branch (conditional)
; ldp           load DP
; push		stack push
; pop		stack pop
; repeat        block repeat
; repeat_top    block repeat top
; laj           link and jump
; multi         multiple instruction
; misc          nop		(default)

;  The only real instructions that affect things are the ones that modify
;  address registers and ones that call or jump.  Note that the number
;  of operands refers to the RTL insn pattern, not the number of explicit
;  operands in the machine instruction.
;
(define_attr "type" "lda,store,unary,unarycc,binary,binarycc,compare,call,rets,jump,jmpc,db,dbc,misc,ldp,repeat,repeat_top,laj,load_load,load_store,store_load,store_store,push,pop,multi"
             (const_string "misc"))


; Some instructions operate on unsigned data constants, some on signed data
; constants, or the ones complement of unsigned constants.
; This differentiates them.  Default to signed.  This attribute
; is used by the macro SMALL_CONST () (defined in c4x.h) to determine
; whether an immediate integer constant will fit within the instruction,
; or will have to be loaded using direct addressing from memory.
; Note that logical operations assume unsigned integers whereas
; arithmetic operations assume signed integers.  Note that the C4x
; small immediate constant (J) used as src2 in three operand instructions
; is always signed.  not_uint16 refers to a number that fits into 16-bits
; when one's complemented.
;
(define_attr "data" "int16,uint16,high_16,not_uint16" (const_string "int16"))

(define_asm_attributes
  [(set_attr "type" "multi")])

;
; C4x DELAY SLOTS
;
; Define delay slot scheduling for branch and call instructions.
; The C[34]x has three delay slots. Note that none of the three instructions
; that follow a delayed branch can be a Bcond, BcondD, BR, BRD, DBcond,
; DBcondD, CALL, CALLcond, TRAPcond, RETIcond, RETScond, RPTB, RPTS, or IDLE.
;
; Annulled branches are a bit difficult because the next instructions
; are preprocessed.
; The table below shows what phase of the c4x is executed.
;        BccA[TF] label
;        op1             fetch, decode and read executed
;        op2             fetch and decode executed
;        op3             fetch executed
; This means that we can allow any instruction in the last delay slot
; and only instructions which modify registers in the first two. 
; lda cannot be executed in the first delay slot 
; and ldpk cannot be executed in the first two delay slots.

(define_attr "onlyreg" "false,true"
       (cond [(eq_attr "type" "unary,unarycc")
                       (if_then_else (and (match_operand 0 "reg_imm_operand" "")
                                          (match_operand 1 "reg_imm_operand" ""))
                                     (const_string "true") (const_string "false"))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (and (match_operand 0 "reg_imm_operand" "")
                                          (and (match_operand 1 "reg_imm_operand" "")
                                               (match_operand 2 "reg_imm_operand" "")))
                                     (const_string "true") (const_string "false"))]
             (const_string "false")))

(define_attr "onlyreg_nomod" "false,true"
       (cond [(eq_attr "type" "unary,unarycc,compare,lda,store")
                       (if_then_else (and (match_operand 0 "not_modify_reg" "")
                                          (match_operand 1 "not_modify_reg" ""))
                                     (const_string "true") (const_string "false"))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (and (match_operand 0 "not_modify_reg" "")
                                          (and (match_operand 1 "not_modify_reg" "")
                                               (match_operand 2 "not_modify_reg" "")))
                                     (const_string "true") (const_string "false"))]
             (const_string "false")))

(define_attr "not_repeat_reg" "false,true"
       (cond [(eq_attr "type" "unary,unarycc,compare,lda,ldp,store")
                       (if_then_else (and (match_operand 0 "not_rc_reg" "")
                                          (match_operand 1 "not_rc_reg" ""))
                                     (const_string "true") (const_string "false"))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (and (match_operand 0 "not_rc_reg" "")
                                          (and (match_operand 1 "not_rc_reg" "")
                                               (match_operand 2 "not_rc_reg" "")))
                                     (const_string "true") (const_string "false"))]
             (const_string "false")))

/* Disable compare because the c4x contains a bug. The cmpi insn sets the CC
   in the read phase of the pipeline instead of the execution phase when
   two registers are compared.  */
(define_attr "in_annul_slot_1" "false,true"
  (if_then_else (and (and (eq_attr "cpu" "c4x")
		          (eq_attr "type" "!jump,call,rets,jmpc,compare,db,dbc,repeat,repeat_top,laj,push,pop,lda,ldp,multi"))
		     (eq_attr "onlyreg" "true"))
		(const_string "true")
		(const_string "false")))

(define_attr "in_annul_slot_2" "false,true"
  (if_then_else (and (and (eq_attr "cpu" "c4x")
		          (eq_attr "type" "!jump,call,rets,jmpc,db,dbc,repeat,repeat_top,laj,push,pop,ldp,multi"))
		     (eq_attr "onlyreg_nomod" "true"))
		(const_string "true")
		(const_string "false")))

/* Disable ldp because the c4x contains a bug. The ldp insn modifies
   the dp register when the insn is anulled or not.
   Also disable autoincrement insns because of a silicon bug.  */
(define_attr "in_annul_slot_3" "false,true"
  (if_then_else (and (and (eq_attr "cpu" "c4x")
		          (eq_attr "type" "!jump,call,rets,jmpc,db,dbc,repeat,repeat_top,laj,push,pop,ldp,multi"))
		     (eq_attr "onlyreg_nomod" "true"))
		(const_string "true")
		(const_string "false")))

(define_attr "in_delay_slot" "false,true"
  (if_then_else (eq_attr "type" "!jump,call,rets,jmpc,db,dbc,repeat,repeat_top,laj,multi")
		(const_string "true")
		(const_string "false")))

(define_attr "in_repeat_slot" "false,true"
  (if_then_else (and (eq_attr "cpu" "c4x")
		     (and (eq_attr "type" "!jump,call,rets,jmpc,db,dbc,repeat,repeat_top,laj,multi")
		          (eq_attr "not_repeat_reg" "true")))
		(const_string "true")
		(const_string "false")))

(define_attr "in_dbc_slot" "false,true"
  (if_then_else (eq_attr "type" "!jump,call,rets,jmpc,unarycc,binarycc,compare,db,dbc,repeat,repeat_top,laj,multi")
		(const_string "true")
		(const_string "false")))

(define_delay (eq_attr "type" "jmpc")
              [(eq_attr "in_delay_slot" "true")
               (eq_attr "in_annul_slot_1" "true")
               (eq_attr "in_annul_slot_1" "true")

               (eq_attr "in_delay_slot" "true")
               (eq_attr "in_annul_slot_2" "true")
               (eq_attr "in_annul_slot_2" "true")

               (eq_attr "in_delay_slot" "true")
               (eq_attr "in_annul_slot_3" "true")
               (eq_attr "in_annul_slot_3" "true") ])


(define_delay (eq_attr "type" "repeat_top")
              [(eq_attr "in_repeat_slot" "true") (nil) (nil)
               (eq_attr "in_repeat_slot" "true") (nil) (nil)
               (eq_attr "in_repeat_slot" "true") (nil) (nil)])

(define_delay (eq_attr "type" "jump,db")
              [(eq_attr "in_delay_slot" "true") (nil) (nil)
               (eq_attr "in_delay_slot" "true") (nil) (nil)
               (eq_attr "in_delay_slot" "true") (nil) (nil)])


; Decrement and branch conditional instructions cannot modify the
; condition codes for the cycles in the delay slots.
;
(define_delay (eq_attr "type" "dbc")
              [(eq_attr "in_dbc_slot" "true") (nil) (nil)
               (eq_attr "in_dbc_slot" "true") (nil) (nil)
               (eq_attr "in_dbc_slot" "true") (nil) (nil)])

; The LAJ instruction has three delay slots but the last slot is
; used for pushing the return address.  Thus we can only use two slots.
;
(define_delay (eq_attr "type" "laj")
              [(eq_attr "in_delay_slot" "true") (nil) (nil)
               (eq_attr "in_delay_slot" "true") (nil) (nil)])

;
; C4x UNSPEC NUMBERS
;
(define_constants
  [
   (UNSPEC_BU			1)
   (UNSPEC_RPTS			2)
   (UNSPEC_LSH			3)
   (UNSPEC_CMPHI		4)
   (UNSPEC_RCPF			5)
   (UNSPEC_RND			6)
   (UNSPEC_RPTB_FILL		7)
   (UNSPEC_LOADHF_INT		8)
   (UNSPEC_STOREHF_INT		9)
   (UNSPEC_RSQRF		10)
   (UNSPEC_LOADQF_INT		11)
   (UNSPEC_STOREQF_INT		12)
   (UNSPEC_LDIV			13)
   (UNSPEC_PUSH_ST		14)
   (UNSPEC_POP_ST		15)
   (UNSPEC_PUSH_DP		16)
   (UNSPEC_POP_DP		17)
   (UNSPEC_POPQI		18)
   (UNSPEC_POPQF		19)
   (UNSPEC_ANDN_ST		20)
   (UNSPEC_RPTB_INIT		22)
   (UNSPEC_TOIEEE		23)
   (UNSPEC_FRIEEE		24)
  ])

;
; C4x PIPELINE MODEL
;
; With the C3x, an external memory write (with no wait states) takes
; two cycles and an external memory read (with no wait states) takes
; one cycle.  However, an external read following an external write
; takes two cycles.  With internal memory, reads and writes take
; half a cycle.
; When a C4x address register is loaded it will not be available for
; an extra machine cycle.  Calculating with a C4x address register
; makes it unavailable for 2 machine cycles.
;
; Just some dummy definitions. The real work is done in c4x_adjust_cost.
; These are needed so the min/max READY_DELAY is known.

(define_insn_reservation "any_insn" 1 (const_int 1) "nothing")
(define_insn_reservation "slowest_insn" 3 (const_int 0) "nothing")

; The attribute setar0 is set to 1 for insns where ar0 is a dst operand.
; Note that the attributes unarycc and binarycc do not apply
; if ar0 is a dst operand (only loading an ext. prec. reg. sets CC)
(define_attr "setar0" ""
       (cond [(eq_attr "type" "unary,binary")
                       (if_then_else (match_operand 0 "ar0_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setlda_ar0" ""
       (cond [(eq_attr "type" "lda")
                       (if_then_else (match_operand 0 "ar0_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

; The attribute usear0 is set to 1 for insns where ar0 is used
; for addressing, as a src operand, or as a dst operand.
(define_attr "usear0" ""
       (cond [(eq_attr "type" "compare,store")
                       (if_then_else (match_operand 0 "ar0_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar0_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar0_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "db,dbc")
                       (if_then_else (match_operand 0 "ar0_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

; The attribute readar0 is set to 1 for insns where ar0 is a src operand.
(define_attr "readar0" ""
       (cond [(eq_attr "type" "compare")
                       (if_then_else (match_operand 0 "ar0_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,store,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar0_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar0_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setar1" ""
       (cond [(eq_attr "type" "unary,binary")
                       (if_then_else (match_operand 0 "ar1_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setlda_ar1" ""
       (cond [(eq_attr "type" "lda")
                       (if_then_else (match_operand 0 "ar1_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "usear1" ""
       (cond [(eq_attr "type" "compare,store")
                       (if_then_else (match_operand 0 "ar1_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar1_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar1_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "db,dbc")
                       (if_then_else (match_operand 0 "ar1_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "readar1" ""
       (cond [(eq_attr "type" "compare")
                       (if_then_else (match_operand 0 "ar1_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,store,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar1_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar1_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setar2" ""
       (cond [(eq_attr "type" "unary,binary")
                       (if_then_else (match_operand 0 "ar2_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setlda_ar2" ""
       (cond [(eq_attr "type" "lda")
                       (if_then_else (match_operand 0 "ar2_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "usear2" ""
       (cond [(eq_attr "type" "compare,store")
                       (if_then_else (match_operand 0 "ar2_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar2_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar2_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "db,dbc")
                       (if_then_else (match_operand 0 "ar2_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "readar2" ""
       (cond [(eq_attr "type" "compare")
                       (if_then_else (match_operand 0 "ar2_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,store,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar2_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar2_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setar3" ""
       (cond [(eq_attr "type" "unary,binary")
                       (if_then_else (match_operand 0 "ar3_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setlda_ar3" ""
       (cond [(eq_attr "type" "lda")
                       (if_then_else (match_operand 0 "ar3_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "usear3" ""
       (cond [(eq_attr "type" "compare,store")
                       (if_then_else (match_operand 0 "ar3_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar3_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar3_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "db,dbc")
                       (if_then_else (match_operand 0 "ar3_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "readar3" ""
       (cond [(eq_attr "type" "compare")
                       (if_then_else (match_operand 0 "ar3_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,store,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar3_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar3_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setar4" ""
       (cond [(eq_attr "type" "unary,binary")
                       (if_then_else (match_operand 0 "ar4_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setlda_ar4" ""
       (cond [(eq_attr "type" "lda")
                       (if_then_else (match_operand 0 "ar4_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "usear4" ""
       (cond [(eq_attr "type" "compare,store")
                       (if_then_else (match_operand 0 "ar4_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar4_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar4_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "db,dbc")
                       (if_then_else (match_operand 0 "ar4_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "readar4" ""
       (cond [(eq_attr "type" "compare")
                       (if_then_else (match_operand 0 "ar4_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,store,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar4_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar4_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setar5" ""
       (cond [(eq_attr "type" "unary,binary")
                       (if_then_else (match_operand 0 "ar5_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setlda_ar5" ""
       (cond [(eq_attr "type" "lda")
                       (if_then_else (match_operand 0 "ar5_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "usear5" ""
       (cond [(eq_attr "type" "compare,store")
                       (if_then_else (match_operand 0 "ar5_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar5_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar5_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "db,dbc")
                       (if_then_else (match_operand 0 "ar5_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "readar5" ""
       (cond [(eq_attr "type" "compare")
                       (if_then_else (match_operand 0 "ar5_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,store,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar5_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar5_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setar6" ""
       (cond [(eq_attr "type" "unary,binary")
                       (if_then_else (match_operand 0 "ar6_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setlda_ar6" ""
       (cond [(eq_attr "type" "lda")
                       (if_then_else (match_operand 0 "ar6_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "usear6" ""
       (cond [(eq_attr "type" "compare,store")
                       (if_then_else (match_operand 0 "ar6_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar6_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar6_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "db,dbc")
                       (if_then_else (match_operand 0 "ar6_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "readar6" ""
       (cond [(eq_attr "type" "compare")
                       (if_then_else (match_operand 0 "ar6_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,store,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar6_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar6_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setar7" ""
       (cond [(eq_attr "type" "unary,binary")
                       (if_then_else (match_operand 0 "ar7_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setlda_ar7" ""
       (cond [(eq_attr "type" "lda")
                       (if_then_else (match_operand 0 "ar7_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "usear7" ""
       (cond [(eq_attr "type" "compare,store")
                       (if_then_else (match_operand 0 "ar7_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar7_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar7_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "db,dbc")
                       (if_then_else (match_operand 0 "ar7_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "readar7" ""
       (cond [(eq_attr "type" "compare")
                       (if_then_else (match_operand 0 "ar7_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,store,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ar7_reg_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ar7_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setir0" ""
       (cond [(eq_attr "type" "unary,binary")
                       (if_then_else (match_operand 0 "ir0_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setlda_ir0" ""
       (cond [(eq_attr "type" "lda")
                       (if_then_else (match_operand 0 "ir0_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "useir0" ""
       (cond [(eq_attr "type" "compare,store")
                       (if_then_else (match_operand 0 "ir0_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ir0_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ir0_mem_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setir1" ""
       (cond [(eq_attr "type" "unary,binary")
                       (if_then_else (match_operand 0 "ir1_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "setlda_ir1" ""
       (cond [(eq_attr "type" "lda")
                       (if_then_else (match_operand 0 "ir1_reg_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "useir1" ""
       (cond [(eq_attr "type" "compare,store")
                       (if_then_else (match_operand 0 "ir1_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "compare,lda,unary,unarycc,binary,binarycc")
                       (if_then_else (match_operand 1 "ir1_mem_operand" "")
                                     (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
                       (if_then_else (match_operand 2 "ir1_mem_operand" "")
                                     (const_int 1) (const_int 0))]
             (const_int 0)))

; With the C3x, things are simpler but slower, i.e. more pipeline conflicts :(
; There are three functional groups:
; (1) AR0-AR7, IR0-IR1, BK
; (2) DP
; (3) SP
;
; When a register in one of these functional groups is loaded,
; the contents of that or any other register in its group
; will not be available to the next instruction for 2 machine cycles.
; Similarly, when a register in one of the functional groups is read
; excepting (IR0-IR1, BK, DP) the contents of that or any other register
; in its group will not be available to the next instruction for
; 1 machine cycle.
;
; Let's ignore functional groups 2 and 3 for now, since they are not
; so important.

(define_attr "setgroup1" ""
       (cond [(eq_attr "type" "lda,unary,binary")
                  (if_then_else (match_operand 0 "group1_reg_operand" "")
                                (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "usegroup1" ""
       (cond [(eq_attr "type" "compare,store,store_store,store_load")
              (if_then_else (match_operand 0 "group1_mem_operand" "")
                            (const_int 1) (const_int 0))
              (eq_attr "type" "compare,lda,unary,unarycc,binary,binarycc,load_load,load_store")
              (if_then_else (match_operand 1 "group1_mem_operand" "")
                            (const_int 1) (const_int 0))
              (eq_attr "type" "store_store,load_store")
              (if_then_else (match_operand 2 "group1_mem_operand" "")
                            (const_int 1) (const_int 0))
              (eq_attr "type" "load_load,store_load")
              (if_then_else (match_operand 3 "group1_mem_operand" "")
                            (const_int 1) (const_int 0))]
             (const_int 0)))

(define_attr "readarx" ""
       (cond [(eq_attr "type" "compare")
              (if_then_else (match_operand 0 "arx_reg_operand" "")
                            (const_int 1) (const_int 0))
              (eq_attr "type" "compare,store,lda,unary,unarycc,binary,binarycc")
              (if_then_else (match_operand 1 "arx_reg_operand" "")
                            (const_int 1) (const_int 0))
              (eq_attr "type" "binary,binarycc")
              (if_then_else (match_operand 2 "arx_reg_operand" "")
                            (const_int 1) (const_int 0))]
             (const_int 0)))

(include "predicates.md")

;
; C4x INSN PATTERNS:
;
; Note that the movMM and addP patterns can be called during reload
; so we need to take special care with theses patterns since
; we cannot blindly clobber CC or generate new pseudo registers.

;
; TWO OPERAND INTEGER INSTRUCTIONS
;

;
; LDP/LDPK
;
(define_insn "set_ldp"
  [(set (match_operand:QI 0 "dp_reg_operand" "=z")
        (high:QI (match_operand:QI 1 "" "")))]
  "! TARGET_SMALL"
  "* return (TARGET_C3X) ? \"ldp\\t%A1\" : \"ldpk\\t%A1\";"
  [(set_attr "type" "ldp")])

(define_insn "set_ldp_prologue"
  [(set (match_operand:QI 0 "dp_reg_operand" "=z")
        (high:QI (match_operand:QI 1 "" "")))]
  "TARGET_SMALL && TARGET_PARANOID"
  "* return (TARGET_C3X) ? \"ldp\\t@data_sec\" : \"ldpk\\t@data_sec\";"
  [(set_attr "type" "ldp")])

(define_insn "set_high"
  [(set (match_operand:QI 0 "std_reg_operand" "=c")
        (high:QI (match_operand:QI 1 "symbolic_address_operand" "")))]
  "! TARGET_C3X && ! TARGET_TI"
  "ldhi\\t^%H1,%0"
  [(set_attr "type" "unary")])

(define_insn "set_lo_sum"
  [(set (match_operand:QI 0 "std_reg_operand" "+c")
        (lo_sum:QI (match_dup 0)
                   (match_operand:QI 1 "symbolic_address_operand" "")))]
  "! TARGET_TI"
  "or\\t#%H1,%0"
  [(set_attr "type" "unary")])

(define_split
  [(set (match_operand:QI 0 "std_reg_operand" "")
        (match_operand:QI 1 "symbolic_address_operand" ""))]
  "reload_completed && ! TARGET_C3X && ! TARGET_TI"
  [(set (match_dup 0) (high:QI (match_dup 1)))
   (set (match_dup 0) (lo_sum:QI (match_dup 0) (match_dup 1)))]
  "")

(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
	(match_operand:QI 1 "const_int_operand" ""))
   (clobber (reg:QI 16))]
  "! TARGET_C3X
   && ! IS_INT16_CONST (INTVAL (operands[1]))
   && ! IS_HIGH_CONST (INTVAL (operands[1]))
   && reload_completed
   && std_reg_operand (operands[0], QImode)"
  [(set (match_dup 0) (match_dup 2))
   (set (match_dup 0) (ior:QI (match_dup 0) (match_dup 3)))]
  "
{
   operands[2] = GEN_INT (INTVAL (operands[1]) & ~0xffff);
   operands[3] = GEN_INT (INTVAL (operands[1]) & 0xffff);
}")

(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
	(match_operand:QI 1 "const_int_operand" ""))]
  "! TARGET_C3X
   && ! IS_INT16_CONST (INTVAL (operands[1]))
   && ! IS_HIGH_CONST (INTVAL (operands[1]))
   && reload_completed
   && std_reg_operand (operands[0], QImode)"
  [(set (match_dup 0) (match_dup 2))
   (set (match_dup 0) (ior:QI (match_dup 0) (match_dup 3)))]
  "
{
   operands[2] = GEN_INT (INTVAL (operands[1]) & ~0xffff);
   operands[3] = GEN_INT (INTVAL (operands[1]) & 0xffff);
}")

(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
	(match_operand:QI 1 "const_int_operand" ""))
   (clobber (reg:QI 16))]
  "TARGET_C3X && ! TARGET_SMALL
   && ! IS_INT16_CONST (INTVAL (operands[1]))
   && reload_completed
   && std_reg_operand (operands[0], QImode)
   && c4x_shiftable_constant (operands[1]) < 0"
  [(set (match_dup 0) (match_dup 2))
   (set (match_dup 0) (ashift:QI (match_dup 0) (match_dup 4)))
   (set (match_dup 0) (ior:QI (match_dup 0) (match_dup 3)))]
  "
{
   /* Generate two's complement value of 16 MSBs.  */
   operands[2] = GEN_INT ((((INTVAL (operands[1]) >> 16) & 0xffff)
			   - 0x8000) ^ ~0x7fff);
   operands[3] = GEN_INT (INTVAL (operands[1]) & 0xffff);
   operands[4] = GEN_INT (16);
}")

(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
	(match_operand:QI 1 "const_int_operand" ""))]
  "TARGET_C3X && ! TARGET_SMALL
   && ! IS_INT16_CONST (INTVAL (operands[1]))
   && reload_completed
   && std_reg_operand (operands[0], QImode)
   && c4x_shiftable_constant (operands[1]) < 0"
  [(set (match_dup 0) (match_dup 2))
   (set (match_dup 0) (ashift:QI (match_dup 0) (match_dup 4)))
   (set (match_dup 0) (ior:QI (match_dup 0) (match_dup 3)))]
  "
{
   /* Generate two's complement value of 16 MSBs.  */
   operands[2] = GEN_INT ((((INTVAL (operands[1]) >> 16) & 0xffff)
			   - 0x8000) ^ ~0x7fff);
   operands[3] = GEN_INT (INTVAL (operands[1]) & 0xffff);
   operands[4] = GEN_INT (16);
}")

(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
	(match_operand:QI 1 "const_int_operand" ""))
   (clobber (reg:QI 16))]
  "TARGET_C3X
   && ! IS_INT16_CONST (INTVAL (operands[1]))
   && reload_completed
   && std_reg_operand (operands[0], QImode)
   && c4x_shiftable_constant (operands[1]) >= 0"
  [(set (match_dup 0) (match_dup 2))
   (set (match_dup 0) (ashift:QI (match_dup 0) (match_dup 3)))]
  "
{
   /* Generate two's complement value of MSBs.  */
   int shift = c4x_shiftable_constant (operands[1]);

   operands[2] = GEN_INT ((((INTVAL (operands[1]) >> shift) & 0xffff)
			   - 0x8000) ^ ~0x7fff);
   operands[3] = GEN_INT (shift);
}")

(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
	(match_operand:QI 1 "const_int_operand" ""))]
  "TARGET_C3X
   && ! IS_INT16_CONST (INTVAL (operands[1]))
   && reload_completed
   && std_reg_operand (operands[0], QImode)
   && c4x_shiftable_constant (operands[1]) >= 0"
  [(set (match_dup 0) (match_dup 2))
   (set (match_dup 0) (ashift:QI (match_dup 0) (match_dup 3)))]
  "
{
   /* Generate two's complement value of MSBs.  */
   int shift = c4x_shiftable_constant (operands[1]);

   operands[2] = GEN_INT ((((INTVAL (operands[1]) >> shift) & 0xffff)
			    - 0x8000) ^ ~0x7fff);
   operands[3] = GEN_INT (shift);
}")

(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
	(match_operand:QI 1 "const_int_operand" ""))
   (clobber (reg:QI 16))]
  "! TARGET_SMALL
   && ! IS_INT16_CONST (INTVAL (operands[1]))
   && ! IS_HIGH_CONST (INTVAL (operands[1]))
   && reload_completed
   && ! std_reg_operand (operands[0], QImode)"
  [(set (match_dup 2) (high:QI (match_dup 3)))
   (set (match_dup 0) (match_dup 4))
   (use (match_dup 1))]
  "
{
   rtx dp_reg = gen_rtx_REG (Pmode, DP_REGNO);
   operands[2] = dp_reg;
   operands[3] = force_const_mem (Pmode, operands[1]);
   operands[4] = change_address (operands[3], QImode,
			         gen_rtx_LO_SUM (Pmode, dp_reg,
                                                 XEXP (operands[3], 0)));
   operands[3] = XEXP (operands[3], 0);
}")

(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
	(match_operand:QI 1 "const_int_operand" ""))]
  "! TARGET_SMALL
   && ! IS_INT16_CONST (INTVAL (operands[1]))
   && ! IS_HIGH_CONST (INTVAL (operands[1]))
   && reload_completed
   && ! std_reg_operand (operands[0], QImode)"
  [(set (match_dup 2) (high:QI (match_dup 3)))
   (set (match_dup 0) (match_dup 4))
   (use (match_dup 1))]
  "
{
   rtx dp_reg = gen_rtx_REG (Pmode, DP_REGNO);
   operands[2] = dp_reg;
   operands[3] = force_const_mem (Pmode, operands[1]);
   operands[4] = change_address (operands[3], QImode,
			         gen_rtx_LO_SUM (Pmode, dp_reg,
                                                 XEXP (operands[3], 0)));
   operands[3] = XEXP (operands[3], 0);
}")

(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
	(match_operand:QI 1 "const_int_operand" ""))
   (clobber (reg:QI 16))]
  "TARGET_SMALL
   && ! IS_INT16_CONST (INTVAL (operands[1]))
   && ! IS_HIGH_CONST (INTVAL (operands[1]))
   && reload_completed
   && ((TARGET_C3X && c4x_shiftable_constant (operands[1]) < 0)
       || ! std_reg_operand (operands[0], QImode))"
  [(set (match_dup 0) (match_dup 2))
   (use (match_dup 1))]
  "
{
   rtx dp_reg = gen_rtx_REG (Pmode, DP_REGNO);
   operands[2] = force_const_mem (Pmode, operands[1]);
   operands[2] = change_address (operands[2], QImode,
			         gen_rtx_LO_SUM (Pmode, dp_reg,
                                                 XEXP (operands[2], 0)));
}")

(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
	(match_operand:QI 1 "const_int_operand" ""))]
  "TARGET_SMALL
   && ! IS_INT16_CONST (INTVAL (operands[1]))
   && ! IS_HIGH_CONST (INTVAL (operands[1]))
   && reload_completed
   && ((TARGET_C3X && c4x_shiftable_constant (operands[1]) < 0)
       || ! std_reg_operand (operands[0], QImode))"
  [(set (match_dup 0) (match_dup 2))
   (use (match_dup 1))]
  "
{
   rtx dp_reg = gen_rtx_REG (Pmode, DP_REGNO);
   operands[2] = force_const_mem (Pmode, operands[1]);
   operands[2] = change_address (operands[2], QImode,
			         gen_rtx_LO_SUM (Pmode, dp_reg,
                                                 XEXP (operands[2], 0)));
}")

(define_split
  [(set (match_operand:HI 0 "reg_operand" "")
	(match_operand:HI 1 "const_int_operand" ""))
   (clobber (reg:QI 16))]
  "reload_completed"
  [(set (match_dup 2) (match_dup 4))
   (set (match_dup 3) (match_dup 5))]
  "
{
   operands[2] = c4x_operand_subword (operands[0], 0, 1, HImode);
   operands[3] = c4x_operand_subword (operands[0], 1, 1, HImode);
   operands[4] = c4x_operand_subword (operands[1], 0, 1, HImode);
   operands[5] = c4x_operand_subword (operands[1], 1, 1, HImode);
}")


; We need to clobber the DP reg to be safe in case we
; need to load this address from memory
(define_insn "load_immed_address"
  [(set (match_operand:QI 0 "reg_operand" "=a?x?c*r")
        (match_operand:QI 1 "symbolic_address_operand" ""))
   (clobber (reg:QI 16))]
  "TARGET_LOAD_ADDRESS"
  "#"
  [(set_attr "type" "multi")])


(define_split
  [(set (match_operand:QI 0 "std_reg_operand" "")
        (match_operand:QI 1 "symbolic_address_operand" ""))
   (clobber (reg:QI 16))]
  "reload_completed && ! TARGET_C3X && ! TARGET_TI"
  [(set (match_dup 0) (high:QI (match_dup 1)))
   (set (match_dup 0) (lo_sum:QI (match_dup 0) (match_dup 1)))]
  "")

; CC has been selected to load a symbolic address.  We force the address
; into memory and then generate LDP and LDIU insns.
; This is also required for the C30 if we pretend that we can 
; easily load symbolic addresses into a register.
(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
        (match_operand:QI 1 "symbolic_address_operand" ""))
   (clobber (reg:QI 16))]
  "reload_completed
   && ! TARGET_SMALL 
   && (TARGET_C3X || TARGET_TI || ! std_reg_operand (operands[0], QImode))"
  [(set (match_dup 2) (high:QI (match_dup 3)))
   (set (match_dup 0) (match_dup 4))
   (use (match_dup 1))]
  "
{
   rtx dp_reg = gen_rtx_REG (Pmode, DP_REGNO);
   operands[2] = dp_reg;
   operands[3] = force_const_mem (Pmode, operands[1]);
   operands[4] = change_address (operands[3], QImode,
			         gen_rtx_LO_SUM (Pmode, dp_reg,
                                                 XEXP (operands[3], 0)));
   operands[3] = XEXP (operands[3], 0);
}")

; This pattern is similar to the above but does not emit a LDP
; for the small memory model.
(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
        (match_operand:QI 1 "symbolic_address_operand" ""))
   (clobber (reg:QI 16))]
  "reload_completed
   && TARGET_SMALL
   && (TARGET_C3X || TARGET_TI || ! std_reg_operand (operands[0], QImode))"
  [(set (match_dup 0) (match_dup 2))
   (use (match_dup 1))]
  "
{  
   rtx dp_reg = gen_rtx_REG (Pmode, DP_REGNO);
   operands[2] = force_const_mem (Pmode, operands[1]);
   operands[2] = change_address (operands[2], QImode,
			         gen_rtx_LO_SUM (Pmode, dp_reg,
                                                 XEXP (operands[2], 0)));
}")

(define_insn "loadhi_big_constant"
  [(set (match_operand:HI 0 "reg_operand" "=c*d")
        (match_operand:HI 1 "const_int_operand" ""))
   (clobber (reg:QI 16))]
  ""
  "#"
  [(set_attr "type" "multi")])

;
; LDIU/LDA/STI/STIK
;
; The following moves will not set the condition codes register.
;

; This must come before the general case
(define_insn "*movqi_stik"
  [(set (match_operand:QI 0 "memory_operand" "=m")
        (match_operand:QI 1 "stik_const_operand" "K"))]
  "! TARGET_C3X"
  "stik\\t%1,%0"
  [(set_attr "type" "store")])

(define_insn "loadqi_big_constant"
  [(set (match_operand:QI 0 "reg_operand" "=c*d")
        (match_operand:QI 1 "const_int_operand" ""))
   (clobber (reg:QI 16))]
  "! IS_INT16_CONST (INTVAL (operands[1]))
   && ! IS_HIGH_CONST (INTVAL (operands[1]))"
  "#"
  [(set_attr "type" "multi")])

; We must provide an alternative to store to memory in case we have to
; spill a register.
(define_insn "movqi_noclobber"
  [(set (match_operand:QI 0 "dst_operand" "=d,*c,m,r")
        (match_operand:QI 1 "src_hi_operand" "rIm,rIm,r,O"))]
  "(REG_P (operands[0]) || REG_P (operands[1])
    || GET_CODE (operands[0]) == SUBREG
    || GET_CODE (operands[1]) == SUBREG)
    && ! symbolic_address_operand (operands[1], QImode)"
  "*
   if (which_alternative == 2)
     return \"sti\\t%1,%0\";

   if (! TARGET_C3X && which_alternative == 3)
     {
       operands[1] = GEN_INT ((INTVAL (operands[1]) >> 16) & 0xffff);
       return \"ldhi\\t%1,%0\";
     }

   /* The lda instruction cannot use the same register as source
      and destination.  */
   if (! TARGET_C3X && which_alternative == 1
       && (   IS_ADDR_REG (operands[0])
           || IS_INDEX_REG (operands[0])
           || IS_SP_REG (operands[0]))
       && (REGNO (operands[0]) != REGNO (operands[1])))
      return \"lda\\t%1,%0\";
   return \"ldiu\\t%1,%0\";
  "
  [(set_attr "type" "unary,lda,store,unary")
   (set_attr "data" "int16,int16,int16,high_16")])

;
; LDI
;

; We shouldn't need these peepholes, but the combiner seems to miss them...
(define_peephole
  [(set (match_operand:QI 0 "ext_reg_operand" "=d")
        (match_operand:QI 1 "src_operand" "rIm"))
   (set (reg:CC 21)
        (compare:CC (match_dup 0) (const_int 0)))]
  ""
  "ldi\\t%1,%0"
  [(set_attr "type" "unarycc")
   (set_attr "data" "int16")])

(define_insn "*movqi_set"
  [(set (reg:CC 21)
        (compare:CC (match_operand:QI 1 "src_operand" "rIm") 
                    (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d")
        (match_dup 1))]
  ""
  "ldi\\t%1,%0"
  [(set_attr "type" "unarycc")
   (set_attr "data" "int16")])

; This pattern probably gets in the way and requires a scratch register
; when a simple compare with zero will suffice.
;(define_insn "*movqi_test"
; [(set (reg:CC 21)
;       (compare:CC (match_operand:QI 1 "src_operand" "rIm") 
;                   (const_int 0)))
;  (clobber (match_scratch:QI 0 "=d"))]
; ""
; "@
;  ldi\\t%1,%0"
;  [(set_attr "type" "unarycc")
;   (set_attr "data" "int16")])

;  If one of the operands is not a register, then we should
;  emit two insns, using a scratch register.  This will produce
;  better code in loops if the source operand is invariant, since
;  the source reload can be optimized out.  During reload we cannot
;  use change_address or force_reg which will allocate new pseudo regs.

;  Unlike most other insns, the move insns can't be split with
;  different predicates, because register spilling and other parts of
;  the compiler, have memoized the insn number already.

(define_expand "movqi"
  [(set (match_operand:QI 0 "general_operand" "")
        (match_operand:QI 1 "general_operand" ""))]
  ""
  "
{
  if (c4x_emit_move_sequence (operands, QImode))
    DONE;
}")


; As far as GCC is concerned, the moves are performed in parallel
; thus it must be convinced that there is no aliasing.
; It also assumes that the input operands are simultaneously loaded
; and then the output operands are simultaneously stored.
; With the C4x, if there are parallel stores to the same address
; both stores are executed.
; If there is a parallel load and store to the same address,
; the load is performed first.
; The problem with this pattern is that reload can spoil
; the show when it eliminates a reference to the frame pointer.
; This can invalidate the memory addressing mode, i.e., when
; the displacement is greater than 1.
(define_insn "movqi_parallel"
  [(set (match_operand:QI 0 "parallel_operand" "=q,S<>!V,q,S<>!V")
        (match_operand:QI 1 "parallel_operand" "S<>!V,q,S<>!V,q"))
   (set (match_operand:QI 2 "parallel_operand" "=q,S<>!V,S<>!V,q")
        (match_operand:QI 3 "parallel_operand" "S<>!V,q,q,S<>!V"))]
  "TARGET_PARALLEL && valid_parallel_load_store (operands, QImode)"
  "@
   ldi1\\t%1,%0\\n||\\tldi2\\t%3,%2
   sti1\\t%1,%0\\n||\\tsti2\\t%3,%2
   ldi\\t%1,%0\\n||\\tsti\\t%3,%2
   ldi\\t%3,%2\\n||\\tsti\\t%1,%0"
  [(set_attr "type" "load_load,store_store,load_store,store_load")])

;
; PUSH/POP
;
(define_insn "pushqi"
  [(set (mem:QI (pre_inc:QI (reg:QI 20)))
        (match_operand:QI 0 "reg_operand" "r"))]
  ""
  "push\\t%0"
  [(set_attr "type" "push")])

(define_insn "push_st"
  [(set (mem:QI (pre_inc:QI (reg:QI 20)))
        (unspec:QI [(reg:QI 21)] UNSPEC_PUSH_ST))
   (use (reg:QI 21))]
  ""
  "push\\tst"
  [(set_attr "type" "push")])

(define_insn "push_dp"
  [(set (mem:QI (pre_inc:QI (reg:QI 20))) 
        (unspec:QI [(reg:QI 16)] UNSPEC_PUSH_DP))
   (use (reg:QI 16))]
  ""
  "push\\tdp"
  [(set_attr "type" "push")])

(define_insn "popqi"
  [(set (match_operand:QI 0 "reg_operand" "=r")
        (mem:QI (post_dec:QI (reg:QI 20))))
   (clobber (reg:CC 21))]
  ""
  "pop\\t%0"
  [(set_attr "type" "pop")])

(define_insn "pop_st"
  [(set (unspec:QI [(reg:QI 21)] UNSPEC_POP_ST) 
        (mem:QI (post_dec:QI (reg:QI 20))))
   (clobber (reg:CC 21))]
  ""
  "pop\\tst"
  [(set_attr "type" "pop")])

(define_insn "pop_dp"
  [(set (unspec:QI [(reg:QI 16)] UNSPEC_POP_DP)
        (mem:QI (post_dec:QI (reg:QI 20))))
   (clobber (reg:CC 16))]
  ""
  "pop\\tdp"
  [(set_attr "type" "pop")])

(define_insn "popqi_unspec"
  [(set (unspec:QI [(match_operand:QI 0 "reg_operand" "=r")] UNSPEC_POPQI)
        (mem:QI (post_dec:QI (reg:QI 20))))
   (clobber (match_dup 0))
   (clobber (reg:CC 21))]
  ""
  "pop\\t%0"
  [(set_attr "type" "pop")])

;
; ABSI
;
(define_expand "absqi2"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (abs:QI (match_operand:QI 1 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
  ""
  "")

(define_insn "*absqi2_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (abs:QI (match_operand:QI 1 "src_operand" "rIm,rIm")))
   (clobber (reg:CC_NOOV 21))]
  ""
  "absi\\t%1,%0"
  [(set_attr "type" "unarycc,unary")
   (set_attr "data" "int16,int16")])

(define_insn "*absqi2_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c")
        (abs:QI (match_operand:QI 1 "src_operand" "rIm")))]
  ""
  "absi\\t%1,%0"
  [(set_attr "type" "unary")
   (set_attr "data" "int16")])

(define_split
  [(set (match_operand:QI 0 "std_reg_operand" "")
        (abs:QI (match_operand:QI 1 "src_operand" "")))
   (clobber (reg:CC_NOOV 21))]
  "reload_completed"
  [(set (match_dup 0)
        (abs:QI (match_dup 1)))]
  "")

(define_insn "*absqi2_test"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (abs:QI (match_operand:QI 1 "src_operand" "rIm"))
                         (const_int 0)))
   (clobber (match_scratch:QI 0 "=d"))]
  ""
  "absi\\t%1,%0"
  [(set_attr "type" "unarycc")
   (set_attr "data" "int16")])

(define_insn "*absqi2_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (abs:QI (match_operand:QI 1 "src_operand" "rIm"))
                         (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d")
        (abs:QI (match_dup 1)))]
  ""
  "absi\\t%1,%0"
  [(set_attr "type" "unarycc")
   (set_attr "data" "int16")])        

;
; NEGI
;
(define_expand "negqi2"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (neg:QI (match_operand:QI 1 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
""
"")

(define_insn "*negqi2_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (neg:QI (match_operand:QI 1 "src_operand" "rIm,rIm")))
   (clobber (reg:CC_NOOV 21))]
  ""
  "negi\\t%1,%0"
  [(set_attr "type" "unarycc,unary")
   (set_attr "data" "int16,int16")])

(define_insn "*negqi2_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c")
        (neg:QI (match_operand:QI 1 "src_operand" "rIm")))]
  ""
  "negi\\t%1,%0"
  [(set_attr "type" "unary")
   (set_attr "data" "int16")])

(define_split
  [(set (match_operand:QI 0 "std_reg_operand" "")
        (neg:QI (match_operand:QI 1 "src_operand" "")))
   (clobber (reg:CC_NOOV 21))]
  "reload_completed"
  [(set (match_dup 0)
        (neg:QI (match_dup 1)))]
  "")

(define_insn "*negqi2_test"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (neg:QI (match_operand:QI 1 "src_operand" "rIm"))
                         (const_int 0)))
   (clobber (match_scratch:QI 0 "=d"))]
  ""
  "negi\\t%1,%0"
  [(set_attr "type" "unarycc")
   (set_attr "data" "int16")])

(define_insn "*negqi2_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (neg:QI (match_operand:QI 1 "src_operand" "rIm"))
                         (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d")
        (neg:QI (match_dup 1)))]
  ""
  "negi\\t%1,%0"
  [(set_attr "type" "unarycc")
   (set_attr "data" "int16")])        

(define_insn "*negbqi2_clobber"
  [(set (match_operand:QI 0 "ext_reg_operand" "=d")
        (neg:QI (match_operand:QI 1 "src_operand" "rIm")))
   (use (reg:CC_NOOV 21))
   (clobber (reg:CC_NOOV 21))]
  ""
  "negb\\t%1,%0"
  [(set_attr "type" "unarycc")
   (set_attr "data" "int16")])        

;
; NOT
;
(define_expand "one_cmplqi2"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (not:QI (match_operand:QI 1 "lsrc_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "")

(define_insn "*one_cmplqi2_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (not:QI (match_operand:QI 1 "lsrc_operand" "rLm,rLm")))
   (clobber (reg:CC 21))]
  ""
  "not\\t%1,%0"
  [(set_attr "type" "unarycc,unary")
   (set_attr "data" "uint16,uint16")])

(define_insn "*one_cmplqi2_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c")
        (not:QI (match_operand:QI 1 "lsrc_operand" "rLm")))]
  ""
  "not\\t%1,%0"
  [(set_attr "type" "unary")
   (set_attr "data" "uint16")])

(define_split
  [(set (match_operand:QI 0 "std_reg_operand" "")
        (not:QI (match_operand:QI 1 "lsrc_operand" "")))
   (clobber (reg:CC 21))]
  "reload_completed"
  [(set (match_dup 0)
        (not:QI (match_dup 1)))]
  "")

(define_insn "*one_cmplqi2_test"
  [(set (reg:CC 21)
        (compare:CC (not:QI (match_operand:QI 1 "lsrc_operand" "rLm"))
                    (const_int 0)))
   (clobber (match_scratch:QI 0 "=d"))]
  ""
  "not\\t%1,%0"
  [(set_attr "type" "unarycc")
   (set_attr "data" "uint16")])

(define_insn "*one_cmplqi2_set"
  [(set (reg:CC 21)
        (compare:CC (not:QI (match_operand:QI 1 "lsrc_operand" "rLm"))
                    (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d")        
        (not:QI (match_dup 1)))]
  ""
  "not\\t%1,%0"
  [(set_attr "type" "unarycc")
   (set_attr "data" "uint16")])        

(define_insn "*one_cmplqi2_const_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (match_operand:QI 1 "not_const_operand" "N,N"))
   (clobber (reg:CC 21))]
  ""
  "@
   not\\t%N1,%0
   not\\t%N1,%0"
   [(set_attr "type" "unarycc,unary")
    (set_attr "data" "not_uint16,not_uint16")])

; movqi can use this for loading an integer that can't normally
; fit into a 16-bit signed integer.  The drawback is that it cannot
; go into R0-R11 since that will clobber the CC and movqi shouldn't
; do that.  This can cause additional reloading but in most cases
; this will cause only an additional register move.  With the large
; memory model we require an extra instruction to load DP anyway,
; if we're loading the constant from memory.  The big advantage of
; allowing constants that satisfy not_const_operand in movqi, is that
; it allows andn to be generated more often.
; However, there is a problem if GCC has decided that it wants
; to use R0-R11, since we won't have a matching pattern...
; In interim, we prevent immed_const allowing `N' constants.
(define_insn "*one_cmplqi2_const_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c")
        (match_operand:QI 1 "not_const_operand" "N"))]
  ""
  "not\\t%N1,%0"
  [(set_attr "type" "unary")
   (set_attr "data" "not_uint16")])

;
; ROL
;
(define_expand "rotlqi3"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (rotate:QI (match_operand:QI 1 "reg_operand" "")
                              (match_operand:QI 2 "const_int_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "if (INTVAL (operands[2]) > 4)
     FAIL; /* Open code as two shifts and an or */
   if (INTVAL (operands[2]) > 1)
     {
        int i;
	rtx tmp;

        /* If we have 4 or fewer shifts, then it is probably faster
           to emit separate ROL instructions.  A C3x requires
           at least 4 instructions (a C4x requires at least 3), to
           perform a rotation by shifts.  */

	tmp = operands[1];
        for (i = 0; i < INTVAL (operands[2]) - 1; i++)
	  {
   	    tmp = gen_reg_rtx (QImode);
            emit_insn (gen_rotl_1_clobber (tmp, operands[1]));
	    operands[1] = tmp;
	  }
        emit_insn (gen_rotl_1_clobber (operands[0], tmp));
        DONE;
     }")

(define_insn "rotl_1_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (rotate:QI (match_operand:QI 1 "reg_operand" "0,0")
                   (const_int 1)))
   (clobber (reg:CC 21))]
  ""
  "rol\\t%0"
  [(set_attr "type" "unarycc,unary")])
; Default to int16 data attr.

;
; ROR
;
(define_expand "rotrqi3"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (rotatert:QI (match_operand:QI 1 "reg_operand" "")
                                (match_operand:QI 2 "const_int_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "if (INTVAL (operands[2]) > 4)
     FAIL; /* Open code as two shifts and an or */
   if (INTVAL (operands[2]) > 1)
     {
        int i;
	rtx tmp;
 
        /* If we have 4 or fewer shifts, then it is probably faster
           to emit separate ROL instructions.  A C3x requires
           at least 4 instructions (a C4x requires at least 3), to
           perform a rotation by shifts.  */
 
	tmp = operands[1];
        for (i = 0; i < INTVAL (operands[2]) - 1; i++)
	  {
   	    tmp = gen_reg_rtx (QImode);
            emit_insn (gen_rotr_1_clobber (tmp, operands[1]));
	    operands[1] = tmp;
	  }
        emit_insn (gen_rotr_1_clobber (operands[0], tmp));
        DONE;
     }")

(define_insn "rotr_1_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (rotatert:QI (match_operand:QI 1 "reg_operand" "0,0")
                     (const_int 1)))
   (clobber (reg:CC 21))]
  ""
  "ror\\t%0"
  [(set_attr "type" "unarycc,unary")])
; Default to int16 data attr.


;
; THREE OPERAND INTEGER INSTRUCTIONS
;

;
; ADDI
;
; This is used by reload when it calls gen_add2_insn for address arithmetic
; so we must emit the pattern that doesn't clobber CC.
;
(define_expand "addqi3"
  [(parallel [(set (match_operand:QI 0 "std_or_reg_operand" "")
                   (plus:QI (match_operand:QI 1 "src_operand" "")
                            (match_operand:QI 2 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
  ""
  "legitimize_operands (PLUS, operands, QImode);
   if (reload_in_progress
       || (! IS_PSEUDO_REG (operands[0]) 
           && ! IS_EXT_REG (operands[0])))
   {
      emit_insn (gen_addqi3_noclobber (operands[0], operands[1], operands[2]));
      DONE;
   }")

; This pattern is required primarily for manipulating the stack pointer
; where GCC doesn't expect CC to be clobbered or for calculating
; addresses during reload.  Since this is a more specific pattern
; it needs to go first (otherwise we get into problems trying to decide
; to add clobbers).
(define_insn "addqi3_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,c,c")
        (plus:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                 (match_operand:QI 2 "src_operand" "rIm,JR,rS<>")))]
  "valid_operands (PLUS, operands, QImode)"
  "@
   addi\\t%2,%0
   addi3\\t%2,%1,%0
   addi3\\t%2,%1,%0"
  [(set_attr "type" "binary,binary,binary")])
; Default to int16 data attr.

(define_insn "*addqi3_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (plus:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>,0,rR,rS<>")
                 (match_operand:QI 2 "src_operand" "rIm,JR,rS<>,rIm,JR,rS<>")))
   (clobber (reg:CC_NOOV 21))]
  "valid_operands (PLUS, operands, QImode)"
  "@
   addi\\t%2,%0
   addi3\\t%2,%1,%0
   addi3\\t%2,%1,%0
   addi\\t%2,%0
   addi3\\t%2,%1,%0
   addi3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")])
; Default to int16 data attr.

(define_split
  [(set (match_operand:QI 0 "std_reg_operand" "")
        (plus:QI (match_operand:QI 1 "src_operand" "")
                 (match_operand:QI 2 "src_operand" "")))
   (clobber (reg:CC_NOOV 21))]
  "reload_completed"
  [(set (match_dup 0)
        (plus:QI (match_dup 1)
                 (match_dup 2)))]
  "")

(define_insn "*addqi3_test"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (plus:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                                  (match_operand:QI 2 "src_operand" "rIm,JR,rS<>"))
                         (const_int 0)))
   (clobber (match_scratch:QI 0 "=d,d,d"))]
  "valid_operands (PLUS, operands, QImode)"
  "@
   addi\\t%2,%0
   addi3\\t%2,%1,%0
   addi3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")])
; Default to int16 data attr.

; gcc does this in combine.c we just reverse it here
(define_insn "*cmp_neg"
  [(set (reg:CC_NOOV 21)
	(compare:CC_NOOV (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
		         (neg: QI (match_operand:QI 2 "src_operand" "g,JR,rS<>"))))
   (clobber (match_scratch:QI 0 "=d,d,d"))]
  "valid_operands (PLUS, operands, QImode)"
  "@
   addi\\t%2,%0
   addi3\\t%2,%1,%0
   addi3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")])
  
(define_peephole
  [(parallel [(set (match_operand:QI 0 "ext_reg_operand" "=d,d,d")
                   (plus:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                            (match_operand:QI 2 "src_operand" "g,JR,rS<>")))
              (clobber (reg:CC_NOOV 21))])
   (set (reg:CC_NOOV 21)
        (compare:CC_NOOV (match_dup 0) (const_int 0)))]
  "valid_operands (PLUS, operands, QImode)"
  "@
   addi\\t%2,%0
   addi3\\t%2,%1,%0
   addi3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")])

(define_insn "*addqi3_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (plus:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                                  (match_operand:QI 2 "src_operand" "rIm,JR,rS<>"))
                         (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d,d,d")
        (plus:QI (match_dup 1) (match_dup 2)))]
  "valid_operands (PLUS, operands, QImode)"
  "@
   addi\\t%2,%0
   addi3\\t%2,%1,%0
   addi3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")])
; Default to int16 data attr.


; This pattern is required during reload when eliminate_regs_in_insn
; effectively converts a move insn into an add insn when the src
; operand is the frame pointer plus a constant.  Without this
; pattern, gen_addqi3 can be called with a register for operand0
; that can clobber CC.
; For example, we may have (set (mem (reg ar0)) (reg 99))
; with (set (reg 99) (plus (reg ar3) (const_int 8)))
; Now since ar3, the frame pointer, is unchanging within the function,
; (plus (reg ar3) (const_int 8)) is considered a constant.
; eliminate_regs_in_insn substitutes this constant to give
; (set (mem (reg ar0)) (plus (reg ar3) (const_int 8))).
; This is an invalid C4x insn but if we don't provide a pattern
; for it, it will be considered to be a move insn for reloading.
(define_insn "*addqi3_noclobber_reload"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,c,c")
        (plus:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                 (match_operand:QI 2 "src_operand" "rIm,JR,rS<>")))]
  "reload_in_progress"
  "@
   addi\\t%2,%0
   addi3\\t%2,%1,%0
   addi3\\t%2,%1,%0"
  [(set_attr "type" "binary,binary,binary")])
; Default to int16 data attr.


(define_insn "*addqi3_carry_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (plus:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>,0,rR,rS<>")
                 (match_operand:QI 2 "src_operand" "rIm,JR,rS<>,rIm,JR,rS<>")))
   (use (reg:CC_NOOV 21))
   (clobber (reg:CC_NOOV 21))]
  "valid_operands (PLUS, operands, QImode)"
  "@
   addc\\t%2,%0
   addc3\\t%2,%1,%0
   addc3\\t%2,%1,%0
   addc\\t%2,%0
   addc3\\t%2,%1,%0
   addc3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")])
; Default to int16 data attr.


;
; SUBI/SUBRI
;
(define_expand "subqi3"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (minus:QI (match_operand:QI 1 "src_operand" "")
                             (match_operand:QI 2 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
  ""
  "legitimize_operands (MINUS, operands, QImode);")

(define_insn "*subqi3_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,d,?d,c,c,c,?c")
        (minus:QI (match_operand:QI 1 "src_operand" "0,rIm,rR,rS<>,0,rIm,rR,rS<>")
                  (match_operand:QI 2 "src_operand" "rIm,0,JR,rS<>,rIm,0,JR,rS<>")))
   (clobber (reg:CC_NOOV 21))]
  "valid_operands (MINUS, operands, QImode)"
  "@
   subi\\t%2,%0
   subri\\t%1,%0
   subi3\\t%2,%1,%0
   subi3\\t%2,%1,%0
   subi\\t%2,%0
   subri\\t%1,%0
   subi3\\t%2,%1,%0
   subi3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc,binary,binary,binary,binary")])
; Default to int16 data attr.

(define_split
  [(set (match_operand:QI 0 "std_reg_operand" "")
        (minus:QI (match_operand:QI 1 "src_operand" "")
                  (match_operand:QI 2 "src_operand" "")))
   (clobber (reg:CC_NOOV 21))]
  "reload_completed"
  [(set (match_dup 0)
        (minus:QI (match_dup 1)
                 (match_dup 2)))]
  "")

(define_insn "*subqi3_test"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (minus:QI (match_operand:QI 1 "src_operand" "0,rIm,rR,rS<>")
                                   (match_operand:QI 2 "src_operand" "rIm,0,JR,rS<>"))
                         (const_int 0)))
   (clobber (match_scratch:QI 0 "=d,d,d,?d"))]
  "valid_operands (MINUS, operands, QImode)"
  "@
   subi\\t%2,%0
   subri\\t%1,%0
   subi3\\t%2,%1,%0
   subi3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc")])
; Default to int16 data attr.

(define_peephole
  [(parallel [(set (match_operand:QI 0 "ext_reg_operand" "=d,d,d,?d")
                   (minus:QI (match_operand:QI 1 "src_operand" "0,rIm,rR,rS<>")
                             (match_operand:QI 2 "src_operand" "rIm,0,JR,rS<>")))
              (clobber (reg:CC_NOOV 21))])
   (set (reg:CC_NOOV 21)
        (compare:CC_NOOV (match_dup 0) (const_int 0)))]
  "valid_operands (MINUS, operands, QImode)"
  "@
   subi\\t%2,%0
   subri\\t%1,%0
   subi3\\t%2,%1,%0
   subi3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc")])
  
(define_insn "*subqi3_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (minus:QI (match_operand:QI 1 "src_operand" "0,rIm,rR,rS<>")
                                   (match_operand:QI 2 "src_operand" "rIm,0,JR,rS<>"))
                         (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d,d,d,?d")
        (minus:QI (match_dup 1)
                  (match_dup 2)))]
  "valid_operands (MINUS, operands, QImode)"
  "@
   subi\\t%2,%0
   subri\\t%1,%0
   subi3\\t%2,%1,%0
   subi3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc")])
; Default to int16 data attr.

(define_insn "*subqi3_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,c,c,?c")
        (minus:QI (match_operand:QI 1 "src_operand" "0,rIm,rR,rS<>")
                  (match_operand:QI 2 "src_operand" "rIm,0,JR,rS<>")))]
  "valid_operands (MINUS, operands, QImode)"
  "@
   subi\\t%2,%0
   subri\\t%1,%0
   subi3\\t%2,%1,%0
   subi3\\t%2,%1,%0"
  [(set_attr "type" "binary,binary,binary,binary")])
; Default to int16 data attr.

(define_insn "*subqi3_carry_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,d,?d,c,c,c,?c")
        (minus:QI (match_operand:QI 1 "src_operand" "0,rIm,rR,rS<>,0,rIm,rR,rS<>")
                  (match_operand:QI 2 "src_operand" "rIm,0,JR,rS<>,rIm,0,JR,rS<>")))
   (use (reg:CC_NOOV 21))
   (clobber (reg:CC_NOOV 21))]
  "valid_operands (MINUS, operands, QImode)"
  "@
   subb\\t%2,%0
   subrb\\t%1,%0
   subb3\\t%2,%1,%0
   subb3\\t%2,%1,%0
   subb\\t%2,%0
   subrb\\t%1,%0
   subb3\\t%2,%1,%0
   subb3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc,binary,binary,binary,binary")])
; Default to int16 data attr.

(define_insn "*subqi3_carry_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (minus:QI (match_operand:QI 1 "src_operand" "0,rIm,rR,rS<>")
                                   (match_operand:QI 2 "src_operand" "rIm,0,JR,rS<>"))
                         (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d,d,d,?d")
        (minus:QI (match_dup 1)
                  (match_dup 2)))
   (use (reg:CC_NOOV 21))]
  "valid_operands (MINUS, operands, QImode)"
  "@
   subb\\t%2,%0
   subrb\\t%1,%0
   subb3\\t%2,%1,%0
   subb3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc")])
; Default to int16 data attr.

;
; MPYI
;
(define_expand "mulqi3"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (mult:QI (match_operand:QI 1 "src_operand" "")
                            (match_operand:QI 2 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
  ""
  "if (TARGET_MPYI || (GET_CODE (operands[2]) == CONST_INT
       && exact_log2 (INTVAL (operands[2])) >= 0))
     legitimize_operands (MULT, operands, QImode);
   else
     {        
       if (GET_CODE (operands[2]) == CONST_INT)
         {
          /* Let GCC try to synthesize the multiplication using shifts
             and adds.  In most cases this will be more profitable than
             using the C3x MPYI.  */
            FAIL;
         }
       if (operands[1] == operands[2])
         {
            /* Do the squaring operation in-line.  */
            emit_insn (gen_sqrqi2_inline (operands[0], operands[1]));
            DONE;
         }
       if (TARGET_INLINE)
         {
            emit_insn (gen_mulqi3_inline (operands[0], operands[1],
                                          operands[2]));
            DONE;
         }
       c4x_emit_libcall3 (smul_optab->handlers[(int) QImode].libfunc,
			  MULT, QImode, operands);
       DONE;
     }
  ")

(define_insn "*mulqi3_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (mult:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>,0,rR,rS<>")
                 (match_operand:QI 2 "src_operand" "rIm,JR,rS<>,rIm,JR,rS<>")))
   (clobber (reg:CC_NOOV 21))]
  "valid_operands (MULT, operands, QImode)"
  "*
  if (which_alternative == 0 || which_alternative == 3)
    {
      if (TARGET_C3X
          && GET_CODE (operands[2]) == CONST_INT
          && exact_log2 (INTVAL (operands[2])) >= 0)
        return \"ash\\t%L2,%0\";
      else
        return \"mpyi\\t%2,%0\";
    }
  else
      return \"mpyi3\\t%2,%1,%0\";"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")])
; Default to int16 data attr.

(define_insn "*mulqi3_test"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (mult:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                                  (match_operand:QI 2 "src_operand" "rIm,JR,rS<>"))
                         (const_int 0)))
   (clobber (match_scratch:QI 0 "=d,d,d"))]
  "valid_operands (MULT, operands, QImode)"
  "*
  if (which_alternative == 0)
    {
      if (TARGET_C3X 
          && GET_CODE (operands[2]) == CONST_INT
          && exact_log2 (INTVAL (operands[2])) >= 0)
        return \"ash\\t%L2,%0\";
      else
        return \"mpyi\\t%2,%0\";
    } 
  else
      return \"mpyi3\\t%2,%1,%0\";"
  [(set_attr "type" "binarycc,binarycc,binarycc")])
; Default to int16 data attr.

(define_insn "*mulqi3_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (mult:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                                  (match_operand:QI 2 "src_operand" "rIm,JR,rS<>"))
                         (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d,d,d")
        (mult:QI (match_dup 1)
                 (match_dup 2)))]
  "valid_operands (MULT, operands, QImode)"
  "*
  if (which_alternative == 0)
    {
      if (TARGET_C3X 
          && GET_CODE (operands[2]) == CONST_INT
          && exact_log2 (INTVAL (operands[2])) >= 0)
        return \"ash\\t%L2,%0\";
      else
        return \"mpyi\\t%2,%0\";
    }
    else
        return \"mpyi3\\t%2,%1,%0\";"
  [(set_attr "type" "binarycc,binarycc,binarycc")])
; Default to int16 data attr.

; The C3x multiply instruction assumes 24-bit signed integer operands
; and the 48-bit result is truncated to 32-bits.
(define_insn "mulqi3_24_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (mult:QI
         (sign_extend:QI
          (and:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>,0,rR,rS<>")
                  (const_int 16777215)))
         (sign_extend:QI
          (and:QI (match_operand:QI 2 "src_operand" "rIm,JR,rS<>,rIm,JR,rS<>")
                  (const_int 16777215)))))
   (clobber (reg:CC_NOOV 21))]
  "TARGET_C3X && valid_operands (MULT, operands, QImode)"
  "@
   mpyi\\t%2,%0
   mpyi3\\t%2,%1,%0
   mpyi3\\t%2,%1,%0
   mpyi\\t%2,%0
   mpyi3\\t%2,%1,%0
   mpyi3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")])
; Default to int16 data attr.


; Fast square function for C3x where TARGET_MPYI not asserted
(define_expand "sqrqi2_inline"
  [(set (match_dup 7) (match_operand:QI 1 "src_operand" ""))
   (parallel [(set (match_dup 3)
                   (lshiftrt:QI (match_dup 7) (const_int 16)))
              (clobber (reg:CC 21))])
   (parallel [(set (match_dup 2)
                   (and:QI (match_dup 7) (const_int 65535)))
              (clobber (reg:CC 21))])
   (parallel [(set (match_dup 4)
                   (mult:QI (sign_extend:QI (and:QI (match_dup 2) 
                                                    (const_int 16777215)))
                            (sign_extend:QI (and:QI (match_dup 2) 
                                                    (const_int 16777215)))))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 5)
                   (mult:QI (sign_extend:QI (and:QI (match_dup 2) 
                                                    (const_int 16777215)))
                            (sign_extend:QI (and:QI (match_dup 3) 
                                                    (const_int 16777215)))))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 6)
                   (ashift:QI (match_dup 5) (const_int 17)))
              (clobber (reg:CC 21))])
   (parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (plus:QI (match_dup 4) (match_dup 6)))
              (clobber (reg:CC_NOOV 21))])]
  ""
  "
  operands[2] = gen_reg_rtx (QImode); /* a = val & 0xffff */
  operands[3] = gen_reg_rtx (QImode); /* b = val >> 16 */
  operands[4] = gen_reg_rtx (QImode); /* a * a */
  operands[5] = gen_reg_rtx (QImode); /* a * b */
  operands[6] = gen_reg_rtx (QImode); /* (a * b) << 17 */
  operands[7] = gen_reg_rtx (QImode); /* val */
  ")

; Inlined integer multiply for C3x
(define_expand "mulqi3_inline"
  [(set (match_dup 12) (const_int -16))
   (set (match_dup 13) (match_operand:QI 1 "src_operand" ""))
   (set (match_dup 14) (match_operand:QI 2 "src_operand" ""))
   (parallel [(set (match_dup 4)
                   (lshiftrt:QI (match_dup 13) (neg:QI (match_dup 12))))
              (clobber (reg:CC 21))])
   (parallel [(set (match_dup 6)
                   (lshiftrt:QI (match_dup 14) (neg:QI (match_dup 12))))
              (clobber (reg:CC 21))])
   (parallel [(set (match_dup 3)
                   (and:QI (match_dup 13)
                           (const_int 65535)))
              (clobber (reg:CC 21))])
   (parallel [(set (match_dup 5)
                   (and:QI (match_dup 14) 
                           (const_int 65535)))
              (clobber (reg:CC 21))])
   (parallel [(set (match_dup 7)
                   (mult:QI (sign_extend:QI (and:QI (match_dup 4) 
                                                    (const_int 16777215)))
                            (sign_extend:QI (and:QI (match_dup 5) 
                                                    (const_int 16777215)))))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 8)
                   (mult:QI (sign_extend:QI (and:QI (match_dup 3) 
                                                    (const_int 16777215)))
                            (sign_extend:QI (and:QI (match_dup 5) 
                                                    (const_int 16777215)))))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 9)
                   (mult:QI (sign_extend:QI (and:QI (match_dup 3) 
                                                    (const_int 16777215)))
                            (sign_extend:QI (and:QI (match_dup 6) 
                                                    (const_int 16777215)))))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 10)
                   (plus:QI (match_dup 7) (match_dup 9)))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 11)
                   (ashift:QI (match_dup 10) (const_int 16)))
              (clobber (reg:CC 21))])
   (parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (plus:QI (match_dup 8) (match_dup 11)))
              (clobber (reg:CC_NOOV 21))])]
  "TARGET_C3X"
  "
  operands[3] = gen_reg_rtx (QImode); /* a = arg1 & 0xffff */
  operands[4] = gen_reg_rtx (QImode); /* b = arg1 >> 16 */
  operands[5] = gen_reg_rtx (QImode); /* a = arg2 & 0xffff */
  operands[6] = gen_reg_rtx (QImode); /* b = arg2 >> 16 */
  operands[7] = gen_reg_rtx (QImode); /* b * c */
  operands[8] = gen_reg_rtx (QImode); /* a * c */
  operands[9] = gen_reg_rtx (QImode); /* a * d */
  operands[10] = gen_reg_rtx (QImode); /* b * c + a * d */
  operands[11] = gen_reg_rtx (QImode); /* (b *c + a * d) << 16 */
  operands[12] = gen_reg_rtx (QImode); /* -16 */
  operands[13] = gen_reg_rtx (QImode); /* arg1 */
  operands[14] = gen_reg_rtx (QImode); /* arg2 */
  ")

;
; MPYSHI (C4x only)
;
(define_expand "smulqi3_highpart"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (truncate:QI
                    (lshiftrt:HI
                     (mult:HI
                      (sign_extend:HI (match_operand:QI 1 "src_operand" ""))
                      (sign_extend:HI (match_operand:QI 2 "src_operand" "")))
                 (const_int 32))))
              (clobber (reg:CC_NOOV 21))])]
 ""
 "legitimize_operands (MULT, operands, QImode);
  if (TARGET_C3X)
    {
       c4x_emit_libcall_mulhi (smulhi3_libfunc, SIGN_EXTEND, QImode, operands);
       DONE;
    }
 ")

(define_insn "*smulqi3_highpart_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (truncate:QI 
         (lshiftrt:HI
          (mult:HI
           (sign_extend:HI (match_operand:QI 1 "src_operand" "%0,rR,rS<>,0,rR,rS<>"))
           (sign_extend:HI (match_operand:QI 2 "src_operand" "rIm,JR,rS<>,rIm,JR,rS<>")))
      (const_int 32))))
   (clobber (reg:CC_NOOV 21))]
  "! TARGET_C3X && valid_operands (MULT, operands, QImode)"
  "@
   mpyshi\\t%2,%0
   mpyshi3\\t%2,%1,%0
   mpyshi3\\t%2,%1,%0
   mpyshi\\t%2,%0
   mpyshi3\\t%2,%1,%0
   mpyshi3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")
   (set_attr "data" "int16,int16,int16,int16,int16,int16")])

(define_insn "*smulqi3_highpart_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,c,?c")
        (truncate:QI 
         (lshiftrt:HI
          (mult:HI
           (sign_extend:HI (match_operand:QI 1 "src_operand" "0,rR,rS<>"))
           (sign_extend:HI (match_operand:QI 2 "src_operand" "rIm,JR,rS<>")))
      (const_int 32))))]
  "! TARGET_C3X && valid_operands (MULT, operands, QImode)"
  "@
   mpyshi\\t%2,%0
   mpyshi3\\t%2,%1,%0
   mpyshi3\\t%2,%1,%0"
  [(set_attr "type" "binary,binary,binary")
   (set_attr "data" "int16,int16,int16")])

;
; MPYUHI (C4x only)
;
(define_expand "umulqi3_highpart"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
               (truncate:QI
                (lshiftrt:HI
                 (mult:HI
                  (zero_extend:HI (match_operand:QI 1
				   "nonimmediate_src_operand" ""))
                  (zero_extend:HI (match_operand:QI 2
				   "nonimmediate_lsrc_operand" "")))
                 (const_int 32))))
              (clobber (reg:CC_NOOV 21))])]
 ""
 "legitimize_operands (MULT, operands, QImode);
  if (TARGET_C3X) 
    {
      c4x_emit_libcall_mulhi (umulhi3_libfunc, ZERO_EXTEND, QImode, operands);
      DONE;
    }
 ")

(define_insn "*umulqi3_highpart_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (truncate:QI
         (lshiftrt:HI
          (mult:HI 
           (zero_extend:HI (match_operand:QI 1
			    "nonimmediate_src_operand" "%0,rR,rS<>,0,rR,rS<>"))
           (zero_extend:HI (match_operand:QI 2
			    "nonimmediate_lsrc_operand" "rm,R,rS<>,rm,R,rS<>")))
          (const_int 32))))
   (clobber (reg:CC_NOOV 21))]
  "! TARGET_C3X && valid_operands (MULT, operands, QImode)"
  "@
   mpyuhi\\t%2,%0
   mpyuhi3\\t%2,%1,%0
   mpyuhi3\\t%2,%1,%0
   mpyuhi\\t%2,%0
   mpyuhi3\\t%2,%1,%0
   mpyuhi3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")
   (set_attr "data" "uint16,uint16,uint16,uint16,uint16,uint16")])

(define_insn "*umulqi3_highpart_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,c,?c")
        (truncate:QI
         (lshiftrt:HI
          (mult:HI 
           (zero_extend:HI (match_operand:QI 1
			    "nonimmediate_src_operand" "0,rR,rS<>"))
           (zero_extend:HI (match_operand:QI 2
			    "nonimmediate_lsrc_operand" "rm,R,rS<>")))
          (const_int 32))))]
  "! TARGET_C3X && valid_operands (MULT, operands, QImode)"
  "@
   mpyuhi\\t%2,%0
   mpyuhi3\\t%2,%1,%0
   mpyuhi3\\t%2,%1,%0"
  [(set_attr "type" "binary,binary,binary")
   (set_attr "data" "uint16,uint16,uint16")])

;
; AND
;
(define_expand "andqi3"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (and:QI (match_operand:QI 1 "src_operand" "")
                           (match_operand:QI 2 "tsrc_operand" "")))
              (clobber (reg:CC 21))])]
 ""
 "legitimize_operands (AND, operands, QImode);")


(define_insn "*andqi3_255_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (and:QI (match_operand:QI 1 "src_operand" "mr,mr")
                (const_int 255)))
   (clobber (reg:CC 21))]
 "! TARGET_C3X"
 "lbu0\\t%1,%0"
  [(set_attr "type" "unarycc,unary")])

(define_insn "*andqi3_255_noclobber"
  [(set (match_operand:QI 0 "reg_operand" "=c")
        (and:QI (match_operand:QI 1 "src_operand" "mr")
                (const_int 255)))]
 "! TARGET_C3X"
 "lbu0\\t%1,%0"
  [(set_attr "type" "unary")])


(define_insn "*andqi3_65535_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (and:QI (match_operand:QI 1 "src_operand" "mr,mr")
                (const_int 65535)))
   (clobber (reg:CC 21))]
 "! TARGET_C3X"
 "lhu0\\t%1,%0"
  [(set_attr "type" "unarycc,unary")])

(define_insn "*andqi3_65535_noclobber"
  [(set (match_operand:QI 0 "reg_operand" "=c")
        (and:QI (match_operand:QI 1 "src_operand" "mr")
                (const_int 65535)))]
 "! TARGET_C3X"
 "lhu0\\t%1,%0"
  [(set_attr "type" "unary")])

(define_insn "*andqi3_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,d,?d,c,c,c,?c")
        (and:QI (match_operand:QI 1 "src_operand" "%0,0,rR,rS<>,0,0,rR,rS<>")
                (match_operand:QI 2 "tsrc_operand" "N,rLm,JR,rS<>,N,rLm,JR,rS<>")))
   (clobber (reg:CC 21))]
  "valid_operands (AND, operands, QImode)"
  "@
   andn\\t%N2,%0
   and\\t%2,%0
   and3\\t%2,%1,%0
   and3\\t%2,%1,%0
   andn\\t%N2,%0
   and\\t%2,%0
   and3\\t%2,%1,%0
   and3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc,binary,binary,binary,binary")
   (set_attr "data" "not_uint16,uint16,int16,uint16,not_uint16,uint16,int16,uint16")])

(define_insn "*andqi3_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,c,c,?c")
        (and:QI (match_operand:QI 1 "src_operand" "%0,0,rR,rS<>")
                (match_operand:QI 2 "tsrc_operand" "N,rLm,JR,rS<>")))]
  "valid_operands (AND, operands, QImode)"
  "@
   andn\\t%N2,%0
   and\\t%2,%0
   and3\\t%2,%1,%0
   and3\\t%2,%1,%0"
  [(set_attr "type" "binary,binary,binary,binary")
   (set_attr "data" "not_uint16,uint16,int16,uint16")])

(define_insn "andn_st"
  [(set (unspec:QI [(reg:QI 21)] 20)
        (and:QI (unspec:QI [(reg:QI 21)] UNSPEC_ANDN_ST)
                (match_operand:QI 0 "" "N")))
   (use (match_dup 0))
   (use (reg:CC 21))
   (clobber (reg:CC 21))]
  ""
  "andn\\t%N0,st"
  [(set_attr "type" "misc")
   (set_attr "data" "not_uint16")])

(define_split
  [(set (match_operand:QI 0 "std_reg_operand" "")
        (and:QI (match_operand:QI 1 "src_operand" "")
                (match_operand:QI 2 "tsrc_operand" "")))
   (clobber (reg:CC 21))]
  "reload_completed"
  [(set (match_dup 0)
        (and:QI (match_dup 1)
                (match_dup 2)))]
  "")

(define_insn "*andqi3_test"
  [(set (reg:CC 21)
        (compare:CC (and:QI (match_operand:QI 1 "src_operand" "%0,r,rR,rS<>")
                            (match_operand:QI 2 "tsrc_operand" "N,rLm,JR,rS<>"))
                    (const_int 0)))
   (clobber (match_scratch:QI 0 "=d,X,X,?X"))]
  "valid_operands (AND, operands, QImode)"
  "@
   andn\\t%N2,%0
   tstb\\t%2,%1
   tstb3\\t%2,%1
   tstb3\\t%2,%1"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc")
   (set_attr "data" "not_uint16,uint16,int16,uint16")])

(define_peephole
  [(parallel [(set (match_operand:QI 0 "ext_reg_operand" "=d,d,d,?d")
                   (and:QI (match_operand:QI 1 "src_operand" "%0,0,rR,rS<>")
                           (match_operand:QI 2 "tsrc_operand" "N,rLm,JR,rS<>")))
              (clobber (reg:CC 21))])
   (set (reg:CC 21)
        (compare:CC (match_dup 0) (const_int 0)))]
  "valid_operands (AND, operands, QImode)"
  "@
   andn\\t%N2,%0
   and\\t%2,%0
   and3\\t%2,%1,%0
   and3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc")
   (set_attr "data" "not_uint16,uint16,int16,uint16")])
  
(define_insn "*andqi3_set"
  [(set (reg:CC 21)
        (compare:CC (and:QI (match_operand:QI 1 "src_operand" "%0,0,rR,rS<>")
                            (match_operand:QI 2 "tsrc_operand" "N,rLm,JR,rS<>"))
                    (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d,d,d,?d")
        (and:QI (match_dup 1)
                (match_dup 2)))]
  "valid_operands (AND, operands, QImode)"
  "@
   andn\\t%N2,%0
   and\\t%2,%0
   and3\\t%2,%1,%0
   and3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc")
   (set_attr "data" "not_uint16,uint16,int16,uint16")])

;
; ANDN
;
; NB, this insn doesn't have commutative operands, but valid_operands
; assumes that the code AND does.  We might have to kludge this if
; we make valid_operands stricter.
(define_insn "*andnqi3_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (and:QI (not:QI (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>,rLm,JR,rS<>"))
                (match_operand:QI 1 "src_operand" "0,rR,rS<>,0,rR,rS<>")))
   (clobber (reg:CC 21))]
  "valid_operands (AND, operands, QImode)"
  "@
   andn\\t%2,%0
   andn3\\t%2,%1,%0
   andn3\\t%2,%1,%0
   andn\\t%2,%0
   andn3\\t%2,%1,%0
   andn3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")
   (set_attr "data" "uint16,int16,uint16,uint16,int16,uint16")])

(define_insn "*andnqi3_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,c,?c")
        (and:QI (not:QI (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>"))
                (match_operand:QI 1 "src_operand" "0,rR,rS<>")))]
  "valid_operands (AND, operands, QImode)"
  "@
   andn\\t%2,%0
   andn3\\t%2,%1,%0
   andn3\\t%2,%1,%0"
  [(set_attr "type" "binary,binary,binary")
   (set_attr "data" "uint16,int16,uint16")])

(define_split
  [(set (match_operand:QI 0 "std_reg_operand" "")
        (and:QI (not:QI (match_operand:QI 2 "lsrc_operand" ""))
                (match_operand:QI 1 "src_operand" "")))
   (clobber (reg:CC 21))]
  "reload_completed"
  [(set (match_dup 0)
        (and:QI (not:QI (match_dup 2))
                (match_dup 1)))]
  "")

(define_insn "*andnqi3_test"
  [(set (reg:CC 21)
        (compare:CC (and:QI (not:QI (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>"))
                            (match_operand:QI 1 "src_operand" "0,rR,rS<>"))
                    (const_int 0)))
   (clobber (match_scratch:QI 0 "=d,d,d"))]
  "valid_operands (AND, operands, QImode)"
  "@
   andn\\t%2,%0
   andn3\\t%2,%1,%0
   andn3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")
   (set_attr "data" "uint16,int16,uint16")])

(define_insn "*andnqi3_set"
  [(set (reg:CC 21)
        (compare:CC (and:QI (not:QI (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>"))
                            (match_operand:QI 1 "src_operand" "0,rR,rS<>"))
                    (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d,d,d")
        (and:QI (not:QI (match_dup 2))
                (match_dup 1)))]
  "valid_operands (AND, operands, QImode)"
  "@
   andn\\t%2,%0
   andn3\\t%2,%1,%0
   andn3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")
   (set_attr "data" "uint16,int16,uint16")])

;
; OR
;
(define_expand "iorqi3"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (ior:QI (match_operand:QI 1 "src_operand" "")
                           (match_operand:QI 2 "lsrc_operand" "")))
              (clobber (reg:CC 21))])]
 ""
 "legitimize_operands (IOR, operands, QImode);")

(define_insn "*iorqi3_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (ior:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>,0,rR,rS<>")
                (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>,rLm,JR,rS<>")))
   (clobber (reg:CC 21))]
  "valid_operands (IOR, operands, QImode)"
  "@
   or\\t%2,%0
   or3\\t%2,%1,%0
   or3\\t%2,%1,%0
   or\\t%2,%0
   or3\\t%2,%1,%0
   or3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")
   (set_attr "data" "uint16,int16,uint16,uint16,int16,uint16")])

(define_split
  [(set (match_operand:QI 0 "std_reg_operand" "")
        (ior:QI (match_operand:QI 1 "src_operand" "")
                (match_operand:QI 2 "lsrc_operand" "")))
   (clobber (reg:CC 21))]
  "reload_completed"
  [(set (match_dup 0)
        (ior:QI (match_dup 1)
                (match_dup 2)))]
  "")

(define_insn "*iorqi3_test"
  [(set (reg:CC 21)
        (compare:CC (ior:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                            (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>"))
                    (const_int 0)))
   (clobber (match_scratch:QI 0 "=d,d,d"))]
  "valid_operands (IOR, operands, QImode)"
  "@
   or\\t%2,%0
   or3\\t%2,%1,%0
   or3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")
   (set_attr "data" "uint16,int16,uint16")])

(define_peephole
  [(parallel [(set (match_operand:QI 0 "ext_reg_operand" "=d,d,d")
                   (ior:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                           (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>")))
              (clobber (reg:CC 21))])
   (set (reg:CC 21)
        (compare:CC (match_dup 0) (const_int 0)))]
  "valid_operands (IOR, operands, QImode)"
  "@
   or\\t%2,%0
   or3\\t%2,%1,%0
   or3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")
   (set_attr "data" "uint16,int16,uint16")])
  
(define_insn "*iorqi3_set"
  [(set (reg:CC 21)
        (compare:CC (ior:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                            (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>"))
                    (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d,d,d")
        (ior:QI (match_dup 1)
                (match_dup 2)))]
  "valid_operands (IOR, operands, QImode)"
  "@
   or\\t%2,%0
   or3\\t%2,%1,%0
   or3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")
   (set_attr "data" "uint16,int16,uint16")])

; This pattern is used for loading symbol references in several parts. 
(define_insn "iorqi3_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,c,c")
        (ior:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>")))]
  "valid_operands (IOR, operands, QImode)"
  "@
   or\\t%2,%0
   or3\\t%2,%1,%0
   or3\\t%2,%1,%0"
  [(set_attr "type" "binary,binary,binary")
   (set_attr "data" "uint16,int16,uint16")])

;
; XOR
;
(define_expand "xorqi3"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (xor:QI (match_operand:QI 1 "src_operand" "")
                           (match_operand:QI 2 "lsrc_operand" "")))
              (clobber (reg:CC 21))])]
 ""
 "legitimize_operands (XOR, operands, QImode);")

(define_insn "*xorqi3_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (xor:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>,0,rR,rS<>")
                (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>,rLm,JR,rS<>")))
   (clobber (reg:CC 21))]
  "valid_operands (XOR, operands, QImode)"
  "@
   xor\\t%2,%0
   xor3\\t%2,%1,%0
   xor3\\t%2,%1,%0
   xor\\t%2,%0
   xor3\\t%2,%1,%0
   xor3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")
   (set_attr "data" "uint16,int16,uint16,uint16,int16,uint16")])

(define_insn "*xorqi3_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,c,?c")
        (xor:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>")))]
  "valid_operands (XOR, operands, QImode)"
  "@
   xor\\t%2,%0
   xor3\\t%2,%1,%0
   xor3\\t%2,%1,%0"
  [(set_attr "type" "binary,binary,binary")
   (set_attr "data" "uint16,int16,uint16")])

(define_split
  [(set (match_operand:QI 0 "std_reg_operand" "")
        (xor:QI (match_operand:QI 1 "src_operand" "")
                (match_operand:QI 2 "lsrc_operand" "")))
   (clobber (reg:CC 21))]
  "reload_completed"
  [(set (match_dup 0)
        (xor:QI (match_dup 1)
                (match_dup 2)))]
  "")

(define_insn "*xorqi3_test"
  [(set (reg:CC 21)
        (compare:CC (xor:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                            (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>"))
                    (const_int 0)))
   (clobber (match_scratch:QI 0 "=d,d,d"))]
  "valid_operands (XOR, operands, QImode)"
  "@
   xor\\t%2,%0
   xor3\\t%2,%1,%0
   xor3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")
   (set_attr "data" "uint16,int16,uint16")])

(define_insn "*xorqi3_set"
  [(set (reg:CC 21)
        (compare:CC (xor:QI (match_operand:QI 1 "src_operand" "%0,rR,rS<>")
                            (match_operand:QI 2 "lsrc_operand" "rLm,JR,rS<>"))
                    (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d,d,d")
        (xor:QI (match_dup 1)
                (match_dup 2)))]
  "valid_operands (XOR, operands, QImode)"
  "@
   xor\\t%2,%0
   xor3\\t%2,%1,%0
   xor3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")
   (set_attr "data" "uint16,int16,uint16")])

;
; LSH/ASH (left)
;
; The C3x and C4x have two shift instructions ASH and LSH
; If the shift count is positive, a left shift is performed
; otherwise a right shift is performed.  The number of bits
; shifted is determined by the seven LSBs of the shift count.
; If the absolute value of the count is 32 or greater, the result
; using the LSH instruction is zero; with the ASH insn the result
; is zero or negative 1.   Note that the ISO C standard allows 
; the result to be machine dependent whenever the shift count
; exceeds the size of the object.
(define_expand "ashlqi3"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (ashift:QI (match_operand:QI 1 "src_operand" "")
                              (match_operand:QI 2 "src_operand" "")))
              (clobber (reg:CC 21))])]
 ""
 "legitimize_operands (ASHIFT, operands, QImode);")

(define_insn "*ashlqi3_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (ashift:QI (match_operand:QI 1 "src_operand" "0,rR,rS<>,0,rR,rS<>")
                   (match_operand:QI 2 "src_operand" "rIm,JR,rS<>,rIm,JR,rS<>")))
   (clobber (reg:CC 21))]
  "valid_operands (ASHIFT, operands, QImode)"
  "@
   ash\\t%2,%0
   ash3\\t%2,%1,%0
   ash3\\t%2,%1,%0
   ash\\t%2,%0
   ash3\\t%2,%1,%0
   ash3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")])
; Default to int16 data attr.

(define_insn "*ashlqi3_set"
  [(set (reg:CC 21)
        (compare:CC
          (ashift:QI (match_operand:QI 1 "src_operand" "0,rR,rS<>")
                     (match_operand:QI 2 "src_operand" "rIm,JR,rS<>"))
          (const_int 0)))
   (set (match_operand:QI 0 "reg_operand" "=d,d,d")
        (ashift:QI (match_dup 1)
                   (match_dup 2)))]
  "valid_operands (ASHIFT, operands, QImode)"
  "@
   ash\\t%2,%0
   ash3\\t%2,%1,%0
   ash3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")])
; Default to int16 data attr.

(define_insn "ashlqi3_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,c,?c")
        (ashift:QI (match_operand:QI 1 "src_operand" "0,rR,rS<>")
                   (match_operand:QI 2 "src_operand" "rIm,JR,rS<>")))]
  "valid_operands (ASHIFT, operands, QImode)"
  "@
   ash\\t%2,%0
   ash3\\t%2,%1,%0
   ash3\\t%2,%1,%0"
  [(set_attr "type" "binary,binary,binary")])
; Default to int16 data attr.

(define_split
  [(set (match_operand:QI 0 "std_reg_operand" "")
        (ashift:QI (match_operand:QI 1 "src_operand" "")
                   (match_operand:QI 2 "src_operand" "")))
   (clobber (reg:CC 21))]
  "reload_completed"
  [(set (match_dup 0)
        (ashift:QI (match_dup 1)
                   (match_dup 2)))]
  "")

; This is only used by lshrhi3_reg where we need a LSH insn that will
; shift both ways.
(define_insn "*lshlqi3_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (ashift:QI (match_operand:QI 1 "src_operand" "0,rR,rS<>,0,rR,rS<>")
                   (unspec:QI [(match_operand:QI 2 "src_operand" "rIm,JR,rS<>,rIm,JR,rS<>")] UNSPEC_LSH)))
   (clobber (reg:CC 21))]
  "valid_operands (ASHIFT, operands, QImode)"
  "@
   lsh\\t%2,%0
   lsh3\\t%2,%1,%0
   lsh3\\t%2,%1,%0
   lsh\\t%2,%0
   lsh3\\t%2,%1,%0
   lsh3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")])
; Default to int16 data attr.

;
; LSH (right)
;
; Logical right shift on the C[34]x works by negating the shift count,
; then emitting a right shift with the shift count negated.  This means
; that all actual shift counts in the RTL will be positive.
;
(define_expand "lshrqi3"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (lshiftrt:QI (match_operand:QI 1 "src_operand" "")
                                (match_operand:QI 2 "src_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "legitimize_operands (LSHIFTRT, operands, QImode);")


(define_insn "*lshrqi3_24_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (lshiftrt:QI (match_operand:QI 1 "src_operand" "mr,mr")
                     (const_int 24)))
   (clobber (reg:CC 21))]
  "! TARGET_C3X"
  "lbu3\\t%1,%0"
  [(set_attr "type" "unarycc")])


(define_insn "*ashrqi3_24_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (ashiftrt:QI (match_operand:QI 1 "src_operand" "mr,mr")
                     (const_int 24)))
   (clobber (reg:CC 21))]
  "! TARGET_C3X"
  "lb3\\t%1,%0"
  [(set_attr "type" "unarycc")])


(define_insn "lshrqi3_16_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (lshiftrt:QI (match_operand:QI 1 "src_operand" "mr,mr")
                     (const_int 16)))
   (clobber (reg:CC 21))]
  "! TARGET_C3X"
  "lhu1\\t%1,%0"
  [(set_attr "type" "unarycc")])


(define_insn "*ashrqi3_16_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (ashiftrt:QI (match_operand:QI 1 "src_operand" "mr,mr")
                     (const_int 16)))
   (clobber (reg:CC 21))]
  "! TARGET_C3X"
  "lh1\\t%1,%0"
  [(set_attr "type" "unarycc")])


; When the shift count is greater than the size of the word
; the result can be implementation specific
(define_insn "*lshrqi3_const_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c,?d,?c")
        (lshiftrt:QI (match_operand:QI 1 "src_operand" "0,0,r,r")
                     (match_operand:QI 2 "const_int_operand" "n,n,J,J")))
   (clobber (reg:CC 21))]
  "valid_operands (LSHIFTRT, operands, QImode)"
  "@
   lsh\\t%n2,%0
   lsh\\t%n2,%0
   lsh3\\t%n2,%1,%0
   lsh3\\t%n2,%1,%0"
  [(set_attr "type" "binarycc,binary,binarycc,binary")])

(define_insn "*lshrqi3_const_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,?c")
        (lshiftrt:QI (match_operand:QI 1 "src_operand" "0,r")
                     (match_operand:QI 2 "const_int_operand" "n,J")))]
  "valid_operands (LSHIFTRT, operands, QImode)"
  "@
   lsh\\t%n2,%0
   lsh3\\t%n2,%1,%0"
  [(set_attr "type" "binary,binary")])

; When the shift count is greater than the size of the word
; the result can be implementation specific
(define_insn "*lshrqi3_const_set"
  [(set (reg:CC 21)
        (compare:CC
          (lshiftrt:QI (match_operand:QI 1 "src_operand" "0,r")
                       (match_operand:QI 2 "const_int_operand" "n,J"))
          (const_int 0)))
   (set (match_operand:QI 0 "reg_operand" "=?d,d")
        (lshiftrt:QI (match_dup 1)
                     (match_dup 2)))]
  "valid_operands (LSHIFTRT, operands, QImode)"
  "@
   lsh\\t%n2,%0
   lsh3\\t%n2,%1,%0"
  [(set_attr "type" "binarycc,binarycc")])

(define_insn "*lshrqi3_nonconst_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (lshiftrt:QI (match_operand:QI 1 "src_operand" "0,rR,rS<>,0,rR,rS<>")
                     (neg:QI (match_operand:QI 2 "src_operand" "rm,R,rS<>,rm,R,rS<>"))))
   (clobber (reg:CC 21))]
  "valid_operands (LSHIFTRT, operands, QImode)"
  "@
   lsh\\t%2,%0
   lsh3\\t%2,%1,%0
   lsh3\\t%2,%1,%0
   lsh\\t%2,%0
   lsh3\\t%2,%1,%0
   lsh3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")])
; Default to int16 data attr.

(define_insn "*lshrqi3_nonconst_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,c,?c")
        (lshiftrt:QI (match_operand:QI 1 "src_operand" "0,rR,rS<>")
                     (neg:QI (match_operand:QI 2 "src_operand" "rm,R,rS<>"))))]
  "valid_operands (LSHIFTRT, operands, QImode)"
  "@
   lsh\\t%2,%0
   lsh3\\t%2,%1,%0
   lsh3\\t%2,%1,%0"
  [(set_attr "type" "binary,binary,binary")])
; Default to int16 data attr.

;
; ASH (right)
;
; Arithmetic right shift on the C[34]x works by negating the shift count,
; then emitting a right shift with the shift count negated.  This means
; that all actual shift counts in the RTL will be positive.

(define_expand "ashrqi3"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (ashiftrt:QI (match_operand:QI 1 "src_operand" "")
                                (match_operand:QI 2 "src_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "legitimize_operands (ASHIFTRT, operands, QImode);")

; When the shift count is greater than the size of the word
; the result can be implementation specific
(define_insn "*ashrqi3_const_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c,?d,?c")
        (ashiftrt:QI (match_operand:QI 1 "src_operand" "0,0,r,r")
                     (match_operand:QI 2 "const_int_operand" "n,n,J,J")))
   (clobber (reg:CC 21))]
  "valid_operands (ASHIFTRT, operands, QImode)"
  "@
   ash\\t%n2,%0
   ash\\t%n2,%0
   ash3\\t%n2,%1,%0
   ash3\\t%n2,%1,%0"
  [(set_attr "type" "binarycc,binary,binarycc,binary")])

(define_insn "*ashrqi3_const_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,?c")
        (ashiftrt:QI (match_operand:QI 1 "src_operand" "0,r")
                     (match_operand:QI 2 "const_int_operand" "n,J")))]
  "valid_operands (ASHIFTRT, operands, QImode)"
  "@
   ash\\t%n2,%0
   ash3\\t%n2,%1,%0"
  [(set_attr "type" "binarycc,binarycc")])

; When the shift count is greater than the size of the word
; the result can be implementation specific
(define_insn "*ashrqi3_const_set"
  [(set (reg:CC 21)
        (compare:CC
          (ashiftrt:QI (match_operand:QI 1 "src_operand" "0,r")
                       (match_operand:QI 2 "const_int_operand" "n,J"))
          (const_int 0)))
   (set (match_operand:QI 0 "reg_operand" "=?d,d")
        (ashiftrt:QI (match_dup 1)
                     (match_dup 2)))]
  "valid_operands (ASHIFTRT, operands, QImode)"
  "@
   ash\\t%n2,%0
   ash3\\t%n2,%1,%0"
  [(set_attr "type" "binarycc,binarycc")])

(define_insn "*ashrqi3_nonconst_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,d,?d,c,c,?c")
        (ashiftrt:QI (match_operand:QI 1 "src_operand" "0,rR,rS<>,0,rR,rS<>")
                     (neg:QI (match_operand:QI 2 "src_operand" "rm,R,rS<>,rm,R,rS<>"))))
   (clobber (reg:CC 21))]
  "valid_operands (ASHIFTRT, operands, QImode)"
  "@
   ash\\t%2,%0
   ash3\\t%2,%1,%0
   ash3\\t%2,%1,%0
   ash\\t%2,%0
   ash3\\t%2,%1,%0
   ash3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binary,binary,binary")])
; Default to int16 data attr.

(define_insn "*ashrqi3_nonconst_noclobber"
  [(set (match_operand:QI 0 "std_reg_operand" "=c,c,?c")
        (ashiftrt:QI (match_operand:QI 1 "src_operand" "0,rR,rS<>")
                     (neg:QI (match_operand:QI 2 "src_operand" "rm,R,rS<>"))))]
  "valid_operands (ASHIFTRT, operands, QImode)"
  "@
   ash\\t%2,%0
   ash3\\t%2,%1,%0
   ash3\\t%2,%1,%0"
  [(set_attr "type" "binary,binary,binary")])
; Default to int16 data attr.

;
; CMPI
;
; Unfortunately the C40 doesn't allow cmpi3 7, *ar0++ so the next best
; thing would be to get the small constant loaded into a register (say r0)
; so that it could be hoisted out of the loop so that we only
; would need to do cmpi3 *ar0++, r0.  Now the loop optimization pass
; comes before the flow pass (which finds autoincrements) so we're stuck.
; Ideally, GCC requires another loop optimization pass (preferably after
; reload) so that it can hoist invariants out of loops.
; The current solution modifies legitimize_operands () so that small
; constants are forced into a pseudo register.
; 
(define_expand "cmpqi"
  [(set (reg:CC 21)
        (compare:CC (match_operand:QI 0 "src_operand" "")
                    (match_operand:QI 1 "src_operand" "")))]
  ""
  "legitimize_operands (COMPARE, operands, QImode);
   c4x_compare_op0 = operands[0];
   c4x_compare_op1 = operands[1];
   DONE;")

(define_insn "*cmpqi_test"
  [(set (reg:CC 21)
        (compare:CC (match_operand:QI 0 "src_operand" "r,rR,rS<>")
                    (match_operand:QI 1 "src_operand" "rIm,JR,rS<>")))]
  "valid_operands (COMPARE, operands, QImode)"
  "@
   cmpi\\t%1,%0
   cmpi3\\t%1,%0
   cmpi3\\t%1,%0"
  [(set_attr "type" "compare,compare,compare")])

(define_insn "*cmpqi_test_noov"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (match_operand:QI 0 "src_operand" "r,rR,rS<>")
                         (match_operand:QI 1 "src_operand" "rIm,JR,rS<>")))]
  "valid_operands (COMPARE, operands, QImode)"
  "@
   cmpi\\t%1,%0
   cmpi3\\t%1,%0
   cmpi3\\t%1,%0"
  [(set_attr "type" "compare,compare,compare")])


;
; BIT-FIELD INSTRUCTIONS
;

;
; LBx/LHw (C4x only)
;
(define_expand "extv"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (sign_extract:QI (match_operand:QI 1 "src_operand" "")
                                    (match_operand:QI 2 "const_int_operand" "")
                                    (match_operand:QI 3 "const_int_operand" "")))
              (clobber (reg:CC 21))])]
 "! TARGET_C3X"
 "if ((INTVAL (operands[2]) != 8 && INTVAL (operands[2]) != 16)
      || (INTVAL (operands[3]) % INTVAL (operands[2]) != 0))
        FAIL;
 ")

(define_insn "*extv_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (sign_extract:QI (match_operand:QI 1 "src_operand" "rLm,rLm")
                         (match_operand:QI 2 "const_int_operand" "n,n")
                         (match_operand:QI 3 "const_int_operand" "n,n")))
   (clobber (reg:CC 21))]
  "! TARGET_C3X
   && (INTVAL (operands[2]) == 8 || INTVAL (operands[2]) == 16)
   && (INTVAL (operands[3]) % INTVAL (operands[2]) == 0)"
  "*
   if (INTVAL (operands[2]) == 8)
     {
       operands[3] = GEN_INT (INTVAL (operands[3]) / 8);
       return \"lb%3\\t%1,%0\";
     }
   operands[3] = GEN_INT (INTVAL (operands[3]) / 16);
   return \"lh%3\\t%1,%0\";
  "
  [(set_attr "type" "binarycc,binary")
   (set_attr "data" "int16,int16")])

(define_insn "*extv_clobber_test"
  [(set (reg:CC 21)
        (compare:CC (sign_extract:QI (match_operand:QI 1 "src_operand" "rLm")
                                     (match_operand:QI 2 "const_int_operand" "n")
                                     (match_operand:QI 3 "const_int_operand" "n"))
   		    (const_int 0)))
   (clobber (match_scratch:QI 0 "=d"))]
  "! TARGET_C3X
   && (INTVAL (operands[2]) == 8 || INTVAL (operands[2]) == 16)
   && (INTVAL (operands[3]) % INTVAL (operands[2]) == 0)"
  "*
   if (INTVAL (operands[2]) == 8)
     {
       operands[3] = GEN_INT (INTVAL (operands[3]) / 8);
       return \"lb%3\\t%1,%0\";
     }
   operands[3] = GEN_INT (INTVAL (operands[3]) / 16);
   return \"lh%3\\t%1,%0\";
  "
  [(set_attr "type" "binarycc")
   (set_attr "data" "int16")])

(define_insn "*extv_clobber_set"
  [(set (reg:CC 21)
        (compare:CC (sign_extract:QI (match_operand:QI 1 "src_operand" "rLm")
                                     (match_operand:QI 2 "const_int_operand" "n")
                                     (match_operand:QI 3 "const_int_operand" "n"))
   		    (const_int 0)))
   (set (match_operand:QI 0 "reg_operand" "=d")
        (sign_extract:QI (match_dup 1)
                         (match_dup 2)
                         (match_dup 3)))]
  "! TARGET_C3X
   && (INTVAL (operands[2]) == 8 || INTVAL (operands[2]) == 16)
   && (INTVAL (operands[3]) % INTVAL (operands[2]) == 0)"
  "*
   if (INTVAL (operands[2]) == 8)
     {
       operands[3] = GEN_INT (INTVAL (operands[3]) / 8);
       return \"lb%3\\t%1,%0\";
     }
   operands[3] = GEN_INT (INTVAL (operands[3]) / 16);
   return \"lh%3\\t%1,%0\";
  "
  [(set_attr "type" "binarycc")
   (set_attr "data" "int16")])

;
; LBUx/LHUw (C4x only)
;
(define_expand "extzv"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (zero_extract:QI (match_operand:QI 1 "src_operand" "")
                                    (match_operand:QI 2 "const_int_operand" "")
                                    (match_operand:QI 3 "const_int_operand" "")))
              (clobber (reg:CC 21))])]
 "! TARGET_C3X"
 "if ((INTVAL (operands[2]) != 8 && INTVAL (operands[2]) != 16)
      || (INTVAL (operands[3]) % INTVAL (operands[2]) != 0))
        FAIL;
 ")

(define_insn "*extzv_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (zero_extract:QI (match_operand:QI 1 "src_operand" "rLm,rLm")
                         (match_operand:QI 2 "const_int_operand" "n,n")
                         (match_operand:QI 3 "const_int_operand" "n,n")))
   (clobber (reg:CC 21))]
  "! TARGET_C3X
   && (INTVAL (operands[2]) == 8 || INTVAL (operands[2]) == 16)
   && (INTVAL (operands[3]) % INTVAL (operands[2]) == 0)"
  "*
   if (INTVAL (operands[2]) == 8)
     {
       operands[3] = GEN_INT (INTVAL (operands[3]) / 8);
       return \"lbu%3\\t%1,%0\";
     }
   operands[3] = GEN_INT (INTVAL (operands[3]) / 16);
   return \"lhu%3\\t%1,%0\";
  "
  [(set_attr "type" "binarycc,binary")
   (set_attr "data" "uint16,uint16")])

(define_insn "*extzv_test"
  [(set (reg:CC 21)
        (compare:CC (zero_extract:QI (match_operand:QI 1 "src_operand" "rLm")
                                     (match_operand:QI 2 "const_int_operand" "n")
                                     (match_operand:QI 3 "const_int_operand" "n"))
   		    (const_int 0)))
   (clobber (match_scratch:QI 0 "=d"))]
  "! TARGET_C3X
   && (INTVAL (operands[2]) == 8 || INTVAL (operands[2]) == 16)
   && (INTVAL (operands[3]) % INTVAL (operands[2]) == 0)"
  "*
   if (INTVAL (operands[2]) == 8)
     {
       operands[3] = GEN_INT (INTVAL (operands[3]) / 8);
       return \"lbu%3\\t%1,%0\";
     }
   operands[3] = GEN_INT (INTVAL (operands[3]) / 16);
   return \"lhu%3\\t%1,%0\";
  "
  [(set_attr "type" "binarycc")
   (set_attr "data" "uint16")])

(define_insn "*extzv_set"
  [(set (reg:CC 21)
        (compare:CC (zero_extract:QI (match_operand:QI 1 "src_operand" "rLm")
                                     (match_operand:QI 2 "const_int_operand" "n")
                                     (match_operand:QI 3 "const_int_operand" "n"))
   		    (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d")
        (zero_extract:QI (match_dup 1)
                         (match_dup 2)
                         (match_dup 3)))]
  "! TARGET_C3X
   && (INTVAL (operands[2]) == 8 || INTVAL (operands[2]) == 16)
   && (INTVAL (operands[3]) % INTVAL (operands[2]) == 0)"
  "*
   if (INTVAL (operands[2]) == 8)
     {
	/* 8 bit extract.  */
       operands[3] = GEN_INT (INTVAL (operands[3]) / 8);
       return \"lbu%3\\t%1,%0\";
     }
   /* 16 bit extract.  */
   operands[3] = GEN_INT (INTVAL (operands[3]) / 16);
   return \"lhu%3\\t%1,%0\";
  "
  [(set_attr "type" "binarycc")
   (set_attr "data" "uint16")])

;
; MBx/MHw (C4x only)
;
(define_expand "insv"
  [(parallel [(set (zero_extract:QI (match_operand:QI 0 "reg_operand" "")
                                    (match_operand:QI 1 "const_int_operand" "")
                                    (match_operand:QI 2 "const_int_operand" ""))
                   (match_operand:QI 3 "src_operand" ""))
              (clobber (reg:CC 21))])]
 "! TARGET_C3X"
 "if (! (((INTVAL (operands[1]) == 8 || INTVAL (operands[1]) == 16)
         && (INTVAL (operands[2]) % INTVAL (operands[1]) == 0))
        || (INTVAL (operands[1]) == 24 && INTVAL (operands[2]) == 8)))
    FAIL;
 ")

(define_insn "*insv_clobber"
  [(set (zero_extract:QI (match_operand:QI 0 "reg_operand" "+d,c")
                         (match_operand:QI 1 "const_int_operand" "n,n")
                         (match_operand:QI 2 "const_int_operand" "n,n"))
        (match_operand:QI 3 "src_operand" "rLm,rLm"))
   (clobber (reg:CC 21))]
  "! TARGET_C3X
   && (((INTVAL (operands[1]) == 8 || INTVAL (operands[1]) == 16)
        && (INTVAL (operands[2]) % INTVAL (operands[1]) == 0))
       || (INTVAL (operands[1]) == 24 && INTVAL (operands[2]) == 8))"
  "*
   if (INTVAL (operands[1]) == 8)
     {
       /* 8 bit insert.  */
       operands[2] = GEN_INT (INTVAL (operands[2]) / 8);
       return \"mb%2\\t%3,%0\";
     }
   else if (INTVAL (operands[1]) == 16)
     {
       /* 16 bit insert.  */
       operands[2] = GEN_INT (INTVAL (operands[2]) / 16);
       return \"mh%2\\t%3,%0\";
     }
   /* 24 bit insert.  */
   return \"lwl1\\t%3,%0\";
  "
  [(set_attr "type" "binarycc,binary")
   (set_attr "data" "uint16,uint16")])

(define_peephole
  [(parallel [(set (zero_extract:QI (match_operand:QI 0 "ext_reg_operand" "+d")
                                    (match_operand:QI 1 "const_int_operand" "n")
                                    (match_operand:QI 2 "const_int_operand" "n"))
                   (match_operand:QI 3 "src_operand" "rLm"))
	      (clobber (reg:CC 21))])
   (set (reg:CC 21)
        (compare:CC (match_dup 0) (const_int 0)))]
  "! TARGET_C3X
   && (INTVAL (operands[1]) == 8 || INTVAL (operands[1]) == 16)
   && (INTVAL (operands[2]) % INTVAL (operands[1]) == 0)"
  "*
   if (INTVAL (operands[1]) == 8)
     {
       operands[2] = GEN_INT (INTVAL (operands[2]) / 8);
       return \"mb%2\\t%3,%0\";
     }
   operands[2] = GEN_INT (INTVAL (operands[2]) / 16);
   return \"mh%2\\t%3,%0\";
  "
  [(set_attr "type" "binarycc")
   (set_attr "data" "uint16")])


; TWO OPERAND FLOAT INSTRUCTIONS
;

;
; LDF/STF
;
;  If one of the operands is not a register, then we should
;  emit two insns, using a scratch register.  This will produce
;  better code in loops if the source operand is invariant, since
;  the source reload can be optimized out.  During reload we cannot
;  use change_address or force_reg.
(define_expand "movqf"
  [(set (match_operand:QF 0 "src_operand" "")
        (match_operand:QF 1 "src_operand" ""))]
 ""
 "
{
  if (c4x_emit_move_sequence (operands, QFmode))
    DONE;
}")

; This can generate invalid stack slot displacements
(define_split
 [(set (match_operand:QI 0 "reg_operand" "")
       (unspec:QI [(match_operand:QF 1 "reg_operand" "")] UNSPEC_STOREQF_INT))]
  "reload_completed"
  [(set (match_dup 3) (match_dup 1))
   (set (match_dup 0) (match_dup 2))]
  "operands[2] = assign_stack_temp (QImode, GET_MODE_SIZE (QImode), 0);
   operands[3] = copy_rtx (operands[2]);
   PUT_MODE (operands[3], QFmode);")


(define_insn "storeqf_int"
 [(set (match_operand:QI 0 "reg_operand" "=r")
       (unspec:QI [(match_operand:QF 1 "reg_operand" "f")] UNSPEC_STOREQF_INT))]
 ""
 "#"
  [(set_attr "type" "multi")])

(define_split
 [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                  (unspec:QI [(match_operand:QF 1 "reg_operand" "")] UNSPEC_STOREQF_INT))
             (clobber (reg:CC 21))])]
  "reload_completed"
  [(set (mem:QF (pre_inc:QI (reg:QI 20)))
        (match_dup 1))
   (parallel [(set (match_dup 0)
                   (mem:QI (post_dec:QI (reg:QI 20))))
              (clobber (reg:CC 21))])]
  "")


; We need accurate death notes for this...
;(define_peephole
;  [(set (match_operand:QF 0 "reg_operand" "=f")
;        (match_operand:QF 1 "memory_operand" "m"))
;   (set (mem:QF (pre_inc:QI (reg:QI 20)))
;        (match_dup 0))
;   (parallel [(set (match_operand:QI 2 "reg_operand" "r")
;                   (mem:QI (post_dec:QI (reg:QI 20))))
;              (clobber (reg:CC 21))])]
;  ""
;  "ldiu\\t%1,%0")

(define_insn "storeqf_int_clobber"
 [(parallel [(set (match_operand:QI 0 "reg_operand" "=r")
                  (unspec:QI [(match_operand:QF 1 "reg_operand" "f")] UNSPEC_STOREQF_INT))
             (clobber (reg:CC 21))])]
 ""
 "#"
  [(set_attr "type" "multi")])


; This can generate invalid stack slot displacements
(define_split
 [(set (match_operand:QF 0 "reg_operand" "")
       (unspec:QF [(match_operand:QI 1 "reg_operand" "")] UNSPEC_LOADQF_INT))]
  "reload_completed"
  [(set (match_dup 2) (match_dup 1))
   (set (match_dup 0) (match_dup 3))]
  "operands[2] = assign_stack_temp (QImode, GET_MODE_SIZE (QImode), 0);
   operands[3] = copy_rtx (operands[2]);
   PUT_MODE (operands[3], QFmode);")


(define_insn "loadqf_int"
 [(set (match_operand:QF 0 "reg_operand" "=f")
       (unspec:QF [(match_operand:QI 1 "reg_operand" "r")] UNSPEC_LOADQF_INT))]
 ""
 "#"
  [(set_attr "type" "multi")])

(define_split
 [(parallel [(set (match_operand:QF 0 "reg_operand" "")
                  (unspec:QF [(match_operand:QI 1 "reg_operand" "")] UNSPEC_LOADQF_INT))
             (clobber (reg:CC 21))])]
  "reload_completed"
  [(set (mem:QI (pre_inc:QI (reg:QI 20)))
        (match_dup 1))
   (parallel [(set (match_dup 0)
                   (mem:QF (post_dec:QI (reg:QI 20))))
              (clobber (reg:CC 21))])]
  "")

(define_insn "loadqf_int_clobber"
 [(parallel [(set (match_operand:QF 0 "reg_operand" "=f")
                  (unspec:QF [(match_operand:QI 1 "reg_operand" "r")] UNSPEC_LOADQF_INT))
             (clobber (reg:CC 21))])]
 ""
 "#"
  [(set_attr "type" "multi")])

; We must provide an alternative to store to memory in case we have to
; spill a register.
(define_insn "movqf_noclobber"
 [(set (match_operand:QF 0 "dst_operand" "=f,m")
       (match_operand:QF 1 "src_operand" "fHm,f"))]
 "REG_P (operands[0]) || REG_P (operands[1])"
 "@
  ldfu\\t%1,%0
  stf\\t%1,%0"
  [(set_attr "type" "unary,store")])

;(define_insn "*movqf_clobber"
;  [(set (match_operand:QF 0 "reg_operand" "=f")
;        (match_operand:QF 1 "src_operand" "fHm"))
;   (clobber (reg:CC 21))]
; "0"
; "ldf\\t%1,%0"
;  [(set_attr "type" "unarycc")])

(define_insn "*movqf_test"
  [(set (reg:CC 21)
        (compare:CC (match_operand:QF 1 "src_operand" "fHm")
                    (const_int 0)))
   (clobber (match_scratch:QF 0 "=f"))]
 ""
 "ldf\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*movqf_set"
  [(set (reg:CC 21)
        (compare:CC (match_operand:QF 1 "src_operand" "fHm")
                    (match_operand:QF 2 "fp_zero_operand" "G")))
    (set (match_operand:QF 0 "reg_operand" "=f")
         (match_dup 1))]
 ""
 "ldf\\t%1,%0"
  [(set_attr "type" "unarycc")])


(define_insn "*movqf_parallel"
 [(set (match_operand:QF 0 "parallel_operand" "=q,S<>!V,q,S<>!V")
       (match_operand:QF 1 "parallel_operand" "S<>!V,q,S<>!V,q"))
  (set (match_operand:QF 2 "parallel_operand" "=q,S<>!V,S<>!V,q")
       (match_operand:QF 3 "parallel_operand" "S<>!V,q,q,S<>!V"))]
 "TARGET_PARALLEL && valid_parallel_load_store (operands, QFmode)"
 "@
  ldf1\\t%1,%0\\n||\\tldf2\\t%3,%2
  stf1\\t%1,%0\\n||\\tstf2\\t%3,%2
  ldf\\t%1,%0\\n||\\tstf\\t%3,%2
  ldf\\t%3,%2\\n||\\tstf\\t%1,%0"
  [(set_attr "type" "load_load,store_store,load_store,store_load")])


;
; PUSH/POP
;
(define_insn "pushqf"
  [(set (mem:QF (pre_inc:QI (reg:QI 20)))
        (match_operand:QF 0 "reg_operand" "f"))]
 ""
 "pushf\\t%0"
 [(set_attr "type" "push")])

(define_insn "popqf"
  [(set (match_operand:QF 0 "reg_operand" "=f")
        (mem:QF (post_dec:QI (reg:QI 20))))
   (clobber (reg:CC 21))]
 ""
 "popf\\t%0"
 [(set_attr "type" "pop")])

(define_insn "popqf_unspec"
  [(set (unspec:QF [(match_operand:QF 0 "reg_operand" "=f")] UNSPEC_POPQF)
        (mem:QF (post_dec:QI (reg:QI 20))))
   (clobber (match_dup 0))
   (clobber (reg:CC 21))]
 ""
 "popf\\t%0"
 [(set_attr "type" "pop")])

;
; ABSF
;
(define_expand "absqf2"
  [(parallel [(set (match_operand:QF 0 "reg_operand" "")
                   (abs:QF (match_operand:QF 1 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
""
"")

(define_insn "*absqf2_clobber"
  [(set (match_operand:QF 0 "reg_operand" "=f")
        (abs:QF (match_operand:QF 1 "src_operand" "fHm")))
   (clobber (reg:CC_NOOV 21))]
  ""
  "absf\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*absqf2_test"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (abs:QF (match_operand:QF 1 "src_operand" "fHm"))
                         (match_operand:QF 2 "fp_zero_operand" "G")))
   (clobber (match_scratch:QF 0 "=f"))]
  ""
  "absf\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*absqf2_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (abs:QF (match_operand:QF 1 "src_operand" "fHm"))
                         (match_operand:QF 2 "fp_zero_operand" "G")))
   (set (match_operand:QF 0 "reg_operand" "=f")
        (abs:QF (match_dup 1)))]

  ""
  "absf\\t%1,%0"
  [(set_attr "type" "unarycc")])

;
; NEGF
;
(define_expand "negqf2"
  [(parallel [(set (match_operand:QF 0 "reg_operand" "")
                   (neg:QF (match_operand:QF 1 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
""
"")

(define_insn "*negqf2_clobber"
  [(set (match_operand:QF 0 "reg_operand" "=f")
        (neg:QF (match_operand:QF 1 "src_operand" "fHm")))
   (clobber (reg:CC_NOOV 21))]
  ""
  "negf\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*negqf2_test"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (neg:QF (match_operand:QF 1 "src_operand" "fHm"))
                         (match_operand:QF 2 "fp_zero_operand" "G")))
   (clobber (match_scratch:QF 0 "=f"))]
  ""
  "negf\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*negqf2_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (neg:QF (match_operand:QF 1 "src_operand" "fHm"))
                         (match_operand:QF 2 "fp_zero_operand" "G")))
   (set (match_operand:QF 0 "reg_operand" "=f")
        (neg:QF (match_dup 1)))]
  ""
  "negf\\t%1,%0"
  [(set_attr "type" "unarycc")])

;
; FLOAT
;
(define_insn "floatqiqf2"
  [(set (match_operand:QF 0 "reg_operand" "=f")
        (float:QF (match_operand:QI 1 "src_operand" "rIm")))
   (clobber (reg:CC 21))]
 ""
 "float\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*floatqiqf2_set"
  [(set (reg:CC 21)
        (compare:CC (float:QF (match_operand:QI 1 "src_operand" "rIm"))
                    (match_operand:QF 2 "fp_zero_operand" "G")))
   (set (match_operand:QF 0 "reg_operand" "=f")
        (float:QF (match_dup 1)))]
 ""
 "float\\t%1,%0"
  [(set_attr "type" "unarycc")])

; Unsigned conversions are a little tricky because we need to
; add the value for the high bit if necessary.
; 
;
(define_expand "floatunsqiqf2"
 [(set (match_dup 2) (match_dup 3))
  (parallel [(set (reg:CC 21)
                  (compare:CC (float:QF (match_operand:QI 1 "src_operand" ""))
                              (match_dup 3)))
             (set (match_dup 4)
                  (float:QF (match_dup 1)))])
  (set (match_dup 2)
       (if_then_else:QF (lt (reg:CC 21) (const_int 0))
                        (match_dup 5)
                        (match_dup 2)))
  (parallel [(set (match_operand:QF 0 "reg_operand" "")
                  (plus:QF (match_dup 2) (match_dup 4)))
             (clobber (reg:CC_NOOV 21))])]
 ""
 "operands[2] = gen_reg_rtx (QFmode);
  operands[3] = CONST0_RTX (QFmode); 
  operands[4] = gen_reg_rtx (QFmode);
  operands[5] = gen_reg_rtx (QFmode);
  emit_move_insn (operands[5], CONST_DOUBLE_ATOF (\"4294967296.0\", QFmode));")

(define_expand "floatunsqihf2"
 [(set (match_dup 2) (match_dup 3))
  (parallel [(set (reg:CC 21)
                  (compare:CC (float:HF (match_operand:QI 1 "src_operand" ""))
                              (match_dup 3)))
             (set (match_dup 4)
                  (float:HF (match_dup 1)))])
  (set (match_dup 2)
       (if_then_else:HF (lt (reg:CC 21) (const_int 0))
                        (match_dup 5)
                        (match_dup 2)))
  (parallel [(set (match_operand:HF 0 "reg_operand" "")
                  (plus:HF (match_dup 2) (match_dup 4)))
             (clobber (reg:CC_NOOV 21))])]
 ""
 "operands[2] = gen_reg_rtx (HFmode);
  operands[3] = CONST0_RTX (HFmode); 
  operands[4] = gen_reg_rtx (HFmode);
  operands[5] = gen_reg_rtx (HFmode);
  emit_move_insn (operands[5], CONST_DOUBLE_ATOF (\"4294967296.0\", HFmode));")

(define_insn "floatqihf2"
  [(set (match_operand:HF 0 "reg_operand" "=h")
        (float:HF (match_operand:QI 1 "src_operand" "rIm")))
   (clobber (reg:CC 21))]
 ""
 "float\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*floatqihf2_set"
  [(set (reg:CC 21)
	(compare:CC (float:HF (match_operand:QI 1 "src_operand" "rIm"))
                    (match_operand:QF 2 "fp_zero_operand" "G")))
   (set (match_operand:HF 0 "reg_operand" "=h")
        (float:HF (match_dup 1)))]
 ""
 "float\\t%1,%0"
  [(set_attr "type" "unarycc")])

;
; FIX
;
(define_insn "fixqfqi_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=d,c")
        (fix:QI (match_operand:QF 1 "src_operand" "fHm,fHm")))
   (clobber (reg:CC 21))]
 ""
 "fix\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*fixqfqi_set"
  [(set (reg:CC 21)
        (compare:CC (fix:QI (match_operand:QF 1 "src_operand" "fHm"))
                    (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d")
        (fix:QI (match_dup 1)))]
 ""
 "fix\\t%1,%0"
  [(set_attr "type" "unarycc")])

;
; The C[34]x fix instruction implements a floor, not a straight trunc,
; so we have to invert the number, fix it, and reinvert it if negative
;
(define_expand "fix_truncqfqi2"
  [(parallel [(set (match_dup 2)
                   (fix:QI (match_operand:QF 1 "src_operand" "")))
              (clobber (reg:CC 21))])
   (parallel [(set (match_dup 3) (neg:QF (match_dup 1)))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (fix:QI (match_dup 3)))
              (clobber (reg:CC 21))])
   (parallel [(set (reg:CC_NOOV 21)
                   (compare:CC_NOOV (neg:QI (match_dup 4)) (const_int 0)))
              (set (match_dup 5) (neg:QI (match_dup 4)))])
   (set (match_dup 2)
        (if_then_else:QI (le (reg:CC 21) (const_int 0))
                         (match_dup 5)
                         (match_dup 2)))
   (set (match_operand:QI 0 "reg_operand" "=r") (match_dup 2))]
 ""
 "if (TARGET_FAST_FIX)
    {
       emit_insn (gen_fixqfqi_clobber (operands[0], operands[1]));
       DONE;
    }
  operands[2] = gen_reg_rtx (QImode);
  operands[3] = gen_reg_rtx (QFmode);
  operands[4] = gen_reg_rtx (QImode);
  operands[5] = gen_reg_rtx (QImode);
 ")

(define_expand "fix_truncqfhi2"
  [(parallel [(set (match_operand:HI 0 "reg_operand" "")
                   (fix:HI (match_operand:QF 1 "src_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "c4x_emit_libcall (fix_truncqfhi2_libfunc, FIX, HImode, QFmode, 2, operands);
   DONE;")

(define_expand "fixuns_truncqfqi2"
 [(parallel [(set (match_dup 2)
		  (fix:QI (match_operand:QF 1 "src_operand" "fHm")))
	     (clobber (reg:CC 21))])
  (parallel [(set (match_dup 3)
	          (minus:QF (match_dup 1) (match_dup 5)))
	     (clobber (reg:CC_NOOV 21))])
  (parallel [(set (reg:CC 21)
		  (compare:CC (fix:QI (match_dup 3))
		              (const_int 0)))
	     (set (match_dup 4)
		  (fix:QI (match_dup 3)))])
  (parallel [(set (match_dup 4) (unspec:QI [(match_dup 2)] UNSPEC_LDIV))
             (use (reg:CC 21))])
  (set (match_operand:QI 0 "reg_operand" "=r") (match_dup 4))]
 ""
 "operands[2] = gen_reg_rtx (QImode);
  operands[3] = gen_reg_rtx (QFmode);
  operands[4] = gen_reg_rtx (QImode);
  operands[5] = gen_reg_rtx (QFmode);
  emit_move_insn (operands[5], CONST_DOUBLE_ATOF (\"4294967296.0\", QFmode));")

(define_expand "fixuns_truncqfhi2"
  [(parallel [(set (match_operand:HI 0 "reg_operand" "")
                   (unsigned_fix:HI (match_operand:QF 1 "src_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "c4x_emit_libcall (fixuns_truncqfhi2_libfunc, UNSIGNED_FIX, 
                     HImode, QFmode, 2, operands);
   DONE;")

;
; RCPF
;
(define_insn "rcpfqf_clobber"
  [(set (match_operand:QF 0 "reg_operand" "=f")
        (unspec:QF [(match_operand:QF 1 "src_operand" "fHm")] UNSPEC_RCPF))
   (clobber (reg:CC_NOOV 21))]
  "! TARGET_C3X"
  "rcpf\\t%1,%0"
  [(set_attr "type" "unarycc")])

;
; RSQRF
;
(define_insn "*rsqrfqf_clobber"
  [(set (match_operand:QF 0 "reg_operand" "=f")
        (unspec:QF [(match_operand:QF 1 "src_operand" "fHm")] UNSPEC_RSQRF))
   (clobber (reg:CC_NOOV 21))]
  "! TARGET_C3X"
  "rsqrf\\t%1,%0"
  [(set_attr "type" "unarycc")])

;
; RNDF
;
(define_insn "*rndqf_clobber"
  [(set (match_operand:QF 0 "reg_operand" "=f")
        (unspec:QF [(match_operand:QF 1 "src_operand" "fHm")] UNSPEC_RND))
   (clobber (reg:CC_NOOV 21))]
  "! TARGET_C3X"
  "rnd\\t%1,%0"
  [(set_attr "type" "unarycc")])


; Inlined float square root for C4x
(define_expand "sqrtqf2_inline"
  [(parallel [(set (match_dup 2)
	           (unspec:QF [(match_operand:QF 1 "src_operand" "")] UNSPEC_RSQRF))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 3) (mult:QF (match_dup 5) (match_dup 1)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:QF (match_dup 2) (match_dup 3)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:QF (match_dup 2) (match_dup 4)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (minus:QF (match_dup 6) (match_dup 4)))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 2) (mult:QF (match_dup 2) (match_dup 4)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:QF (match_dup 2) (match_dup 3)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:QF (match_dup 2) (match_dup 4)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (minus:QF (match_dup 6) (match_dup 4)))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 2) (mult:QF (match_dup 2) (match_dup 4)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:QF (match_dup 2) (match_dup 1)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_operand:QF 0 "reg_operand" "")
	           (unspec:QF [(match_dup 4)] UNSPEC_RND))
	      (clobber (reg:CC_NOOV 21))])]
  "! TARGET_C3X"
  "if (! reload_in_progress
       && ! reg_operand (operands[1], QFmode))
     operands[1] = force_reg (QFmode, operands[1]);
   operands[2] = gen_reg_rtx (QFmode);
   operands[3] = gen_reg_rtx (QFmode);
   operands[4] = gen_reg_rtx (QFmode);
   operands[5] = CONST_DOUBLE_ATOF (\"0.5\", QFmode);
   operands[6] = CONST_DOUBLE_ATOF (\"1.5\", QFmode);")

(define_expand "sqrtqf2"
  [(parallel [(set (match_operand:QF 0 "reg_operand" "")
                   (sqrt:QF (match_operand:QF 1 "src_operand" "")))
              (clobber (reg:CC 21))])]
  "! TARGET_C3X && TARGET_INLINE"
  "emit_insn (gen_sqrtqf2_inline (operands[0], operands[1]));
   DONE;")

;
; TOIEEE / FRIEEE
;
(define_insn "toieee"
  [(set (match_operand:QF 0 "reg_operand" "=f")
        (unspec:QF [(match_operand:QF 1 "src_operand" "fHm")] UNSPEC_TOIEEE))
   (clobber (reg:CC 21))]
 ""
 "toieee\\t%1,%0")

(define_insn "frieee"
  [(set (match_operand:QF 0 "reg_operand" "=f")
        (unspec:QF [(match_operand:QF 1 "memory_operand" "m")] UNSPEC_FRIEEE))
   (clobber (reg:CC 21))]
 ""
 "frieee\\t%1,%0")

;
; THREE OPERAND FLOAT INSTRUCTIONS
;

;
; ADDF
;
(define_expand "addqf3"
  [(parallel [(set (match_operand:QF 0 "reg_operand" "")
                   (plus:QF (match_operand:QF 1 "src_operand" "")
                            (match_operand:QF 2 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
  ""
  "legitimize_operands (PLUS, operands, QFmode);")

(define_insn "*addqf3_clobber"
  [(set (match_operand:QF 0 "reg_operand" "=f,f,?f")
        (plus:QF (match_operand:QF 1 "src_operand" "%0,fR,fS<>")
                 (match_operand:QF 2 "src_operand" "fHm,R,fS<>")))
   (clobber (reg:CC_NOOV 21))]
  "valid_operands (PLUS, operands, QFmode)"
  "@
   addf\\t%2,%0
   addf3\\t%2,%1,%0
   addf3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")])

(define_insn "*addqf3_test"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (plus:QF (match_operand:QF 1 "src_operand" "%0,fR,fS<>")
                                  (match_operand:QF 2 "src_operand" "fHm,R,fS<>"))
                         (match_operand:QF 3 "fp_zero_operand" "G,G,G")))
   (clobber (match_scratch:QF 0 "=f,f,?f"))]
  "valid_operands (PLUS, operands, QFmode)"
  "@
   addf\\t%2,%0
   addf3\\t%2,%1,%0
   addf3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")])

(define_insn "*addqf3_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (plus:QF (match_operand:QF 1 "src_operand" "%0,fR,fS<>")
                                  (match_operand:QF 2 "src_operand" "fHm,R,fS<>"))
                         (match_operand:QF 3 "fp_zero_operand" "G,G,G")))
   (set (match_operand:QF 0 "reg_operand" "=f,f,?f")
        (plus:QF (match_dup 1)
                 (match_dup 2)))]
  "valid_operands (PLUS, operands, QFmode)"
  "@
   addf\\t%2,%0
   addf3\\t%2,%1,%0
   addf3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")])

;
; SUBF/SUBRF
;
(define_expand "subqf3"
  [(parallel [(set (match_operand:QF 0 "reg_operand" "")
                   (minus:QF (match_operand:QF 1 "src_operand" "")
                             (match_operand:QF 2 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
  ""
  "legitimize_operands (MINUS, operands, QFmode);")

(define_insn "*subqf3_clobber"
   [(set (match_operand:QF 0 "reg_operand" "=f,f,f,?f")
         (minus:QF (match_operand:QF 1 "src_operand" "0,fHm,fR,fS<>")
                   (match_operand:QF 2 "src_operand" "fHm,0,R,fS<>")))
   (clobber (reg:CC_NOOV 21))]
  "valid_operands (MINUS, operands, QFmode)"
  "@
   subf\\t%2,%0
   subrf\\t%1,%0
   subf3\\t%2,%1,%0
   subf3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc")])

(define_insn "*subqf3_test"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (minus:QF (match_operand:QF 1 "src_operand" "0,fHm,fR,fS<>")
                                   (match_operand:QF 2 "src_operand" "fHm,0,R,fS<>"))
                         (match_operand:QF 3 "fp_zero_operand" "G,G,G,G")))
   (clobber (match_scratch:QF 0 "=f,f,f,?f"))]
  "valid_operands (MINUS, operands, QFmode)"
  "@
   subf\\t%2,%0
   subrf\\t%1,%0
   subf3\\t%2,%1,%0
   subf3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc")])

(define_insn "*subqf3_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (minus:QF (match_operand:QF 1 "src_operand" "0,fHm,fR,fS<>")
                                   (match_operand:QF 2 "src_operand" "fHm,0,R,fS<>"))
                         (match_operand:QF 3 "fp_zero_operand" "G,G,G,G")))
   (set (match_operand:QF 0 "reg_operand" "=f,f,f,?f")
        (minus:QF (match_dup 1)
                  (match_dup 2)))]
  "valid_operands (MINUS, operands, QFmode)"
  "@
   subf\\t%2,%0
   subrf\\t%1,%0
   subf3\\t%2,%1,%0
   subf3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc")])

;
; MPYF
;
(define_expand "mulqf3"
  [(parallel [(set (match_operand:QF 0 "reg_operand" "")
                   (mult:QF (match_operand:QF 1 "src_operand" "")
                            (match_operand:QF 2 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
  ""
  "legitimize_operands (MULT, operands, QFmode);")

(define_insn "*mulqf3_clobber"
  [(set (match_operand:QF 0 "reg_operand" "=f,f,?f")
        (mult:QF (match_operand:QF 1 "src_operand" "%0,fR,fS<>")
                 (match_operand:QF 2 "src_operand" "fHm,R,fS<>")))
   (clobber (reg:CC_NOOV 21))]
  "valid_operands (MULT, operands, QFmode)"
  "@
   mpyf\\t%2,%0
   mpyf3\\t%2,%1,%0
   mpyf3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")])

(define_insn "*mulqf3_test"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (mult:QF (match_operand:QF 1 "src_operand" "%0,fR,fS<>")
                                  (match_operand:QF 2 "src_operand" "fHm,R,fS<>"))
                         (match_operand:QF 3 "fp_zero_operand" "G,G,G")))
   (clobber (match_scratch:QF 0 "=f,f,?f"))]
  "valid_operands (MULT, operands, QFmode)"
  "@
   mpyf\\t%2,%0
   mpyf3\\t%2,%1,%0
   mpyf3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")])

(define_insn "*mulqf3_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (mult:QF (match_operand:QF 1 "src_operand" "%0,fR,fS<>")
                                  (match_operand:QF 2 "src_operand" "fHm,R,fS<>"))
                         (match_operand:QF 3 "fp_zero_operand" "G,G,G")))
   (set (match_operand:QF 0 "reg_operand" "=f,f,?f")
        (mult:QF (match_dup 1)
                 (match_dup 2)))]
  "valid_operands (MULT, operands, QFmode)"
  "@
   mpyf\\t%2,%0
   mpyf3\\t%2,%1,%0
   mpyf3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")])

;
; CMPF
;
(define_expand "cmpqf"
  [(set (reg:CC 21)
        (compare:CC (match_operand:QF 0 "src_operand" "")
                    (match_operand:QF 1 "src_operand" "")))]
  ""
  "legitimize_operands (COMPARE, operands, QFmode);
   c4x_compare_op0 = operands[0];
   c4x_compare_op1 = operands[1];
   DONE;")

(define_insn "*cmpqf"
  [(set (reg:CC 21)
        (compare:CC (match_operand:QF 0 "src_operand" "f,fR,fS<>")
                    (match_operand:QF 1 "src_operand" "fHm,R,fS<>")))]
  "valid_operands (COMPARE, operands, QFmode)"
  "@
   cmpf\\t%1,%0
   cmpf3\\t%1,%0
   cmpf3\\t%1,%0"
  [(set_attr "type" "compare,compare,compare")])

(define_insn "*cmpqf_noov"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (match_operand:QF 0 "src_operand" "f,fR,fS<>")
                         (match_operand:QF 1 "src_operand" "fHm,R,fS<>")))]
  "valid_operands (COMPARE, operands, QFmode)"
  "@
   cmpf\\t%1,%0
   cmpf3\\t%1,%0
   cmpf3\\t%1,%0"
  [(set_attr "type" "compare,compare,compare")])

; Inlined float divide for C4x
(define_expand "divqf3_inline"
  [(parallel [(set (match_dup 3)
	           (unspec:QF [(match_operand:QF 2 "src_operand" "")] UNSPEC_RCPF))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:QF (match_dup 2) (match_dup 3)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (minus:QF (match_dup 5) (match_dup 4)))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 3) (mult:QF (match_dup 3) (match_dup 4)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:QF (match_dup 2) (match_dup 3)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (minus:QF (match_dup 5) (match_dup 4)))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 3) (mult:QF (match_dup 3) (match_dup 4)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 3)
		   (mult:QF (match_operand:QF 1 "src_operand" "")
	         	    (match_dup 3)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_operand:QF 0 "reg_operand" "")
	           (unspec:QF [(match_dup 3)] UNSPEC_RND))
	      (clobber (reg:CC_NOOV 21))])]
  "! TARGET_C3X"
  "if (! reload_in_progress
      && ! reg_operand (operands[2], QFmode))
     operands[2] = force_reg (QFmode, operands[2]);
   operands[3] = gen_reg_rtx (QFmode);
   operands[4] = gen_reg_rtx (QFmode);
   operands[5] = CONST2_RTX (QFmode);")

(define_expand "divqf3"
  [(parallel [(set (match_operand:QF 0 "reg_operand" "")
                   (div:QF (match_operand:QF 1 "src_operand" "")
                            (match_operand:QF 2 "src_operand" "")))
              (clobber (reg:CC 21))])]
  "! TARGET_C3X && TARGET_INLINE"
  "emit_insn (gen_divqf3_inline (operands[0], operands[1], operands[2]));
   DONE;")

;
; CONDITIONAL MOVES
;

; ???  We should make these pattern fail if the src operand combination
; is not valid.  Although reload will fix things up, it will introduce
; extra load instructions that won't be hoisted out of a loop.

(define_insn "*ldi_conditional"
  [(set (match_operand:QI 0 "reg_operand" "=r,r")
        (if_then_else:QI (match_operator 1 "comparison_operator"
                          [(reg:CC 21) (const_int 0)])
                         (match_operand:QI 2 "src_operand" "rIm,0")
                         (match_operand:QI 3 "src_operand" "0,rIm")))]
 "valid_operands (IF_THEN_ELSE, operands, QImode)"
 "@
  ldi%1\\t%2,%0
  ldi%I1\\t%3,%0"
 [(set_attr "type" "binary")])

(define_insn "*ldi_conditional_noov"
  [(set (match_operand:QI 0 "reg_operand" "=r,r")
        (if_then_else:QI (match_operator 1 "comparison_operator"
                          [(reg:CC_NOOV 21) (const_int 0)])
                         (match_operand:QI 2 "src_operand" "rIm,0")
                         (match_operand:QI 3 "src_operand" "0,rIm")))]
 "GET_CODE (operands[1]) != LE
  && GET_CODE (operands[1]) != GE
  && GET_CODE (operands[1]) != LT
  && GET_CODE (operands[1]) != GT
  && valid_operands (IF_THEN_ELSE, operands, QImode)"
 "@
  ldi%1\\t%2,%0
  ldi%I1\\t%3,%0"
 [(set_attr "type" "binary")])

(define_insn "*ldi_on_overflow"
  [(set (match_operand:QI 0 "reg_operand" "=r")
	(unspec:QI [(match_operand:QI 1 "src_operand" "rIm")] UNSPEC_LDIV))
   (use (reg:CC 21))]
  ""
  "ldiv\\t%1,%0"
  [(set_attr "type" "unary")])

; Move operand 2 to operand 0 if condition (operand 1) is true
; else move operand 3 to operand 0.
; The temporary register is required below because some of the operands
; might be identical (namely 0 and 2). 
;
(define_expand "movqicc"
  [(set (match_operand:QI 0 "reg_operand" "")
        (if_then_else:QI (match_operand 1 "comparison_operator" "")
                         (match_operand:QI 2 "src_operand" "")
                         (match_operand:QI 3 "src_operand" "")))]
 ""
 "{ 
    enum rtx_code code = GET_CODE (operands[1]);
    rtx ccreg = c4x_gen_compare_reg (code, c4x_compare_op0, c4x_compare_op1);
    if (ccreg == NULL_RTX) FAIL;
    emit_insn (gen_rtx_SET (QImode, operands[0],
                            gen_rtx_IF_THEN_ELSE (QImode,
                                 gen_rtx_fmt_ee (code, VOIDmode, ccreg, const0_rtx),
						 operands[2], operands[3])));
    DONE;}")
                      
(define_insn "*ldf_conditional"
  [(set (match_operand:QF 0 "reg_operand" "=f,f")
        (if_then_else:QF (match_operator 1 "comparison_operator"
                          [(reg:CC 21) (const_int 0)])
                         (match_operand:QF 2 "src_operand" "fHm,0")
                         (match_operand:QF 3 "src_operand" "0,fHm")))]
 "valid_operands (IF_THEN_ELSE, operands, QFmode)"
 "@
  ldf%1\\t%2,%0
  ldf%I1\\t%3,%0"
 [(set_attr "type" "binary")])

(define_insn "*ldf_conditional_noov"
  [(set (match_operand:QF 0 "reg_operand" "=f,f")
        (if_then_else:QF (match_operator 1 "comparison_operator"
                          [(reg:CC_NOOV 21) (const_int 0)])
                         (match_operand:QF 2 "src_operand" "fHm,0")
                         (match_operand:QF 3 "src_operand" "0,fHm")))]
 "GET_CODE (operands[1]) != LE
  && GET_CODE (operands[1]) != GE
  && GET_CODE (operands[1]) != LT
  && GET_CODE (operands[1]) != GT
  && valid_operands (IF_THEN_ELSE, operands, QFmode)"
 "@
  ldf%1\\t%2,%0
  ldf%I1\\t%3,%0"
 [(set_attr "type" "binary")])

(define_expand "movqfcc"
  [(set (match_operand:QF 0 "reg_operand" "")
        (if_then_else:QF (match_operand 1 "comparison_operator" "")
                         (match_operand:QF 2 "src_operand" "")
                         (match_operand:QF 3 "src_operand" "")))]
 ""
 "{ 
    enum rtx_code code = GET_CODE (operands[1]);
    rtx ccreg = c4x_gen_compare_reg (code, c4x_compare_op0, c4x_compare_op1);
    if (ccreg == NULL_RTX) FAIL;
    emit_insn (gen_rtx_SET (QFmode, operands[0],
                            gen_rtx_IF_THEN_ELSE (QFmode,
                                 gen_rtx_fmt_ee (code, VOIDmode, ccreg, const0_rtx),
						 operands[2], operands[3])));
    DONE;}")

(define_insn "*ldhf_conditional"
  [(set (match_operand:HF 0 "reg_operand" "=h,h")
        (if_then_else:HF (match_operator 1 "comparison_operator"
                          [(reg:CC 21) (const_int 0)])
                         (match_operand:HF 2 "src_operand" "hH,0")
                         (match_operand:HF 3 "src_operand" "0,hH")))]
 ""
 "@
  ldf%1\\t%2,%0
  ldf%I1\\t%3,%0"
 [(set_attr "type" "binary")])

(define_insn "*ldhf_conditional_noov"
  [(set (match_operand:HF 0 "reg_operand" "=h,h")
        (if_then_else:HF (match_operator 1 "comparison_operator"
                          [(reg:CC_NOOV 21) (const_int 0)])
                         (match_operand:HF 2 "src_operand" "hH,0")
                         (match_operand:HF 3 "src_operand" "0,hH")))]
 "GET_CODE (operands[1]) != LE
  && GET_CODE (operands[1]) != GE
  && GET_CODE (operands[1]) != LT
  && GET_CODE (operands[1]) != GT"
 "@
  ldf%1\\t%2,%0
  ldf%I1\\t%3,%0"
 [(set_attr "type" "binary")])

(define_expand "movhfcc"
  [(set (match_operand:HF 0 "reg_operand" "")
        (if_then_else:HF (match_operand 1 "comparison_operator" "")
                         (match_operand:HF 2 "src_operand" "")
                         (match_operand:HF 3 "src_operand" "")))]
 ""
 "{ 
    enum rtx_code code = GET_CODE (operands[1]);
    rtx ccreg = c4x_gen_compare_reg (code, c4x_compare_op0, c4x_compare_op1);
    if (ccreg == NULL_RTX) FAIL;
    emit_insn (gen_rtx_SET (HFmode, operands[0],
                            gen_rtx_IF_THEN_ELSE (HFmode,
                                 gen_rtx_fmt_ee (code, VOIDmode, ccreg, const0_rtx),
						 operands[2], operands[3])));
    DONE;}")

(define_expand "seq"
 [(set (match_operand:QI 0 "reg_operand" "")
       (const_int 0))
  (set (match_dup 0)
       (if_then_else:QI (eq (match_dup 1) (const_int 0))
		        (const_int 1)
		        (match_dup 0)))]
 ""
 "operands[1] = c4x_gen_compare_reg (EQ, c4x_compare_op0, c4x_compare_op1);")

(define_expand "sne"
 [(set (match_operand:QI 0 "reg_operand" "")
       (const_int 0))
  (set (match_dup 0)
       (if_then_else:QI (ne (match_dup 1) (const_int 0))
		        (const_int 1)
		        (match_dup 0)))]
 ""
 "operands[1] = c4x_gen_compare_reg (NE, c4x_compare_op0, c4x_compare_op1);")

(define_expand "slt"
  [(set (match_operand:QI 0 "reg_operand" "")
        (const_int 0))
   (set (match_dup 0)
        (if_then_else:QI (lt (match_dup 1) (const_int 0))
	 	        (const_int 1)
		         (match_dup 0)))]
  ""
  "operands[1] = c4x_gen_compare_reg (LT, c4x_compare_op0, c4x_compare_op1);
   if (operands[1] == NULL_RTX) FAIL;")

(define_expand "sltu"
  [(set (match_operand:QI 0 "reg_operand" "")
        (const_int 0))
   (set (match_dup 0)
        (if_then_else:QI (ltu (match_dup 1) (const_int 0))
	 	        (const_int 1)
		         (match_dup 0)))]
  ""
  "operands[1] = c4x_gen_compare_reg (LTU, c4x_compare_op0, c4x_compare_op1);")

(define_expand "sgt"
  [(set (match_operand:QI 0 "reg_operand" "")
        (const_int 0))
   (set (match_dup 0)
        (if_then_else:QI (gt (match_dup 1) (const_int 0))
	 	        (const_int 1)
		         (match_dup 0)))]
  "" 
  "operands[1] = c4x_gen_compare_reg (GT, c4x_compare_op0, c4x_compare_op1);
   if (operands[1] == NULL_RTX) FAIL;")

(define_expand "sgtu"
  [(set (match_operand:QI 0 "reg_operand" "")
        (const_int 0))
   (set (match_dup 0)
        (if_then_else:QI (gtu (match_dup 1) (const_int 0))
	 	        (const_int 1)
		         (match_dup 0)))]
  ""
  "operands[1] = c4x_gen_compare_reg (GTU, c4x_compare_op0, c4x_compare_op1);")

(define_expand "sle"
  [(set (match_operand:QI 0 "reg_operand" "")
        (const_int 0))
   (set (match_dup 0)
        (if_then_else:QI (le (match_dup 1) (const_int 0))
	 	         (const_int 1)
		         (match_dup 0)))]
  ""
  "operands[1] = c4x_gen_compare_reg (LE, c4x_compare_op0, c4x_compare_op1);
   if (operands[1] == NULL_RTX) FAIL;")

(define_expand "sleu"
  [(set (match_operand:QI 0 "reg_operand" "")
        (const_int 0))
   (set (match_dup 0)
        (if_then_else:QI (leu (match_dup 1) (const_int 0))
	 	         (const_int 1)
		         (match_dup 0)))]
  ""
  "operands[1] = c4x_gen_compare_reg (LEU, c4x_compare_op0, c4x_compare_op1);")

(define_expand "sge"
  [(set (match_operand:QI 0 "reg_operand" "")
        (const_int 0))
   (set (match_dup 0)
        (if_then_else:QI (ge (match_dup 1) (const_int 0))
	 	         (const_int 1)
		         (match_dup 0)))]
  ""
  "operands[1] = c4x_gen_compare_reg (GE, c4x_compare_op0, c4x_compare_op1);
   if (operands[1] == NULL_RTX) FAIL;")

(define_expand "sgeu"
  [(set (match_operand:QI 0 "reg_operand" "")
        (const_int 0))
   (set (match_dup 0)
        (if_then_else:QI (geu (match_dup 1) (const_int 0))
	 	         (const_int 1)
		         (match_dup 0)))]
  ""
  "operands[1] = c4x_gen_compare_reg (GEU, c4x_compare_op0, c4x_compare_op1);")

(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
        (match_operator:QI 1 "comparison_operator" [(reg:CC 21) (const_int 0)]))]
  "reload_completed"
  [(set (match_dup 0) (const_int 0))
   (set (match_dup 0)
        (if_then_else:QI (match_op_dup 1 [(reg:CC 21) (const_int 0)])
	 	        (const_int 1)
		         (match_dup 0)))]
  "")

(define_split
  [(set (match_operand:QI 0 "reg_operand" "")
        (match_operator:QI 1 "comparison_operator" [(reg:CC_NOOV 21) (const_int 0)]))]
  "reload_completed"
  [(set (match_dup 0) (const_int 0))
   (set (match_dup 0)
        (if_then_else:QI (match_op_dup 1 [(reg:CC_NOOV 21) (const_int 0)])
	 	         (const_int 1)
		         (match_dup 0)))]
  "")

(define_insn "*bu"
  [(set (pc)
        (unspec [(match_operand:QI 0 "reg_operand" "r")] UNSPEC_BU))]
  ""
  "bu%#\\t%0"
  [(set_attr "type" "jump")])

(define_expand "caseqi"
  [(parallel [(set (match_dup 5)
                   (minus:QI (match_operand:QI 0 "reg_operand" "")
                             (match_operand:QI 1 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])
   (set (reg:CC 21)
        (compare:CC (match_dup 5)
                    (match_operand:QI 2 "src_operand" "")))
   (set (pc)
        (if_then_else (gtu (reg:CC 21)
                           (const_int 0))
                      (label_ref (match_operand 4 "" ""))
                      (pc)))
   (parallel [(set (match_dup 6)
                   (plus:QI (match_dup 5)
                            (label_ref:QI (match_operand 3 "" ""))))
              (clobber (reg:CC_NOOV 21))])
   (set (match_dup 7)
        (mem:QI (match_dup 6)))
   (set (pc) (match_dup 7))]
  ""
  "operands[5] = gen_reg_rtx (QImode);
   operands[6] = gen_reg_rtx (QImode);
   operands[7] = gen_reg_rtx (QImode);")
                
;
; PARALLEL FLOAT INSTRUCTIONS
;
; This patterns are under development

;
; ABSF/STF
;

(define_insn "*absqf2_movqf_clobber"
  [(set (match_operand:QF 0 "ext_low_reg_operand" "=q")
        (abs:QF (match_operand:QF 1 "par_ind_operand" "S<>")))
   (set (match_operand:QF 2 "par_ind_operand" "=S<>")
        (match_operand:QF 3 "ext_low_reg_operand" "q"))
   (clobber (reg:CC_NOOV 21))]
  "TARGET_PARALLEL && valid_parallel_operands_4 (operands, QFmode)"
  "absf\\t%1,%0\\n||\\tstf\\t%3,%2"
  [(set_attr "type" "binarycc")])

;
; ADDF/STF
;

(define_insn "*addqf3_movqf_clobber"
  [(set (match_operand:QF 0 "ext_low_reg_operand" "=q,q")
        (plus:QF (match_operand:QF 1 "parallel_operand" "%q,S<>")
                 (match_operand:QF 2 "parallel_operand" "S<>,q")))
   (set (match_operand:QF 3 "par_ind_operand" "=S<>,S<>")
        (match_operand:QF 4 "ext_low_reg_operand" "q,q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_5 (operands, QFmode)"
  "addf3\\t%2,%1,%0\\n||\\tstf\\t%4,%3"
  [(set_attr "type" "binarycc,binarycc")])

;
; FLOAT/STF
;

(define_insn "*floatqiqf2_movqf_clobber"
  [(set (match_operand:QF 0 "ext_low_reg_operand" "=q")
        (float:QF (match_operand:QI 1 "par_ind_operand" "S<>")))
   (set (match_operand:QF 2 "par_ind_operand" "=S<>")
        (match_operand:QF 3 "ext_low_reg_operand" "q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_4 (operands, QFmode)"
  "float\\t%1,%0\\n||\\tstf\\t%3,%2"
  [(set_attr "type" "binarycc")])

;
; MPYF/ADDF
;

(define_insn "*mulqf3_addqf3_clobber"
  [(set (match_operand:QF 0 "r0r1_reg_operand" "=t,t,t,t")
        (mult:QF (match_operand:QF 1 "parallel_operand" "%S<>!V,q,S<>!V,q")
                 (match_operand:QF 2 "parallel_operand" "q,S<>!V,S<>!V,q")))
   (set (match_operand:QF 3 "r2r3_reg_operand" "=u,u,u,u")
        (plus:QF (match_operand:QF 4 "parallel_operand" "%S<>!V,q,q,S<>!V")
                 (match_operand:QF 5 "parallel_operand" "q,S<>!V,q,S<>!V")))
   (clobber (reg:CC_NOOV 21))]
  "TARGET_PARALLEL_MPY && valid_parallel_operands_6 (operands, QFmode)"
  "mpyf3\\t%2,%1,%0\\n||\\taddf3\\t%5,%4,%3"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc")])


;
; MPYF/STF
;

(define_insn "*mulqf3_movqf_clobber"
  [(set (match_operand:QF 0 "ext_low_reg_operand" "=q,q")
        (mult:QF (match_operand:QF 1 "parallel_operand" "%q,S<>")
                 (match_operand:QF 2 "parallel_operand" "S<>,q")))
   (set (match_operand:QF 3 "par_ind_operand" "=S<>,S<>")
        (match_operand:QF 4 "ext_low_reg_operand" "q,q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_5 (operands, QFmode)"
  "mpyf3\\t%2,%1,%0\\n||\\tstf\\t%4,%3"
  [(set_attr "type" "binarycc,binarycc")])

;
; MPYF/SUBF
;

(define_insn "*mulqf3_subqf3_clobber"
  [(set (match_operand:QF 0 "r0r1_reg_operand" "=t,t")
        (mult:QF (match_operand:QF 1 "parallel_operand" "S<>,q")
                 (match_operand:QF 2 "parallel_operand" "q,S<>")))
   (set (match_operand:QF 3 "r2r3_reg_operand" "=u,u")
        (minus:QF (match_operand:QF 4 "parallel_operand" "S<>,q")
                  (match_operand:QF 5 "parallel_operand" "q,S<>")))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL_MPY && valid_parallel_operands_6 (operands, QFmode)"
  "mpyf3\\t%2,%1,%0\\n||\\tsubf3\\t%5,%4,%3"
  [(set_attr "type" "binarycc,binarycc")])

;
; MPYF/LDF 0
;

(define_insn "*mulqf3_clrqf_clobber"
  [(set (match_operand:QF 0 "r0r1_reg_operand" "=t")
        (mult:QF (match_operand:QF 1 "par_ind_operand" "%S<>")
                 (match_operand:QF 2 "par_ind_operand" "S<>")))
   (set (match_operand:QF 3 "r2r3_reg_operand" "=u")
        (match_operand:QF 4 "fp_zero_operand" "G"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL_MPY"
  "mpyf3\\t%2,%1,%0\\n||\\tsubf3\\t%3,%3,%3"
  [(set_attr "type" "binarycc")])

;
; NEGF/STF
;

(define_insn "*negqf2_movqf_clobber"
  [(set (match_operand:QF 0 "ext_low_reg_operand" "=q")
        (neg:QF (match_operand:QF 1 "par_ind_operand" "S<>")))
   (set (match_operand:QF 2 "par_ind_operand" "=S<>")
        (match_operand:QF 3 "ext_low_reg_operand" "q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_4 (operands, QFmode)"
  "negf\\t%1,%0\\n||\\tstf\\t%3,%2"
  [(set_attr "type" "binarycc")])

;
; SUBF/STF
;

(define_insn "*subqf3_movqf_clobber"
  [(set (match_operand:QF 0 "ext_low_reg_operand" "=q")
        (minus:QF (match_operand:QF 1 "ext_low_reg_operand" "q")
                  (match_operand:QF 2 "par_ind_operand" "S<>")))
   (set (match_operand:QF 3 "par_ind_operand" "=S<>")
        (match_operand:QF 4 "ext_low_reg_operand" "q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_5 (operands, QFmode)"
  "subf3\\t%2,%1,%0\\n||\\tstf\\t%4,%3"
  [(set_attr "type" "binarycc")])

;
; TOIEEE/STF
;

(define_insn "*toieee_movqf_clobber"
  [(set (match_operand:QF 0 "ext_low_reg_operand" "=q")
	(unspec:QF [(match_operand:QF 1 "par_ind_operand" "S<>")] UNSPEC_TOIEEE))
   (set (match_operand:QF 2 "par_ind_operand" "=S<>")
        (match_operand:QF 3 "ext_low_reg_operand" "q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_4 (operands, QFmode)"
  "toieee\\t%1,%0\\n||\\tstf\\t%3,%2"
  [(set_attr "type" "binarycc")])

;
; FRIEEE/STF
;

(define_insn "*frieee_movqf_clobber"
  [(set (match_operand:QF 0 "ext_low_reg_operand" "=q")
	(unspec:QF [(match_operand:QF 1 "par_ind_operand" "S<>")] UNSPEC_FRIEEE))
   (set (match_operand:QF 2 "par_ind_operand" "=S<>")
        (match_operand:QF 3 "ext_low_reg_operand" "q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_4 (operands, QFmode)"
  "frieee\\t%1,%0\\n||\\tstf\\t%3,%2"
  [(set_attr "type" "binarycc")])

;
; PARALLEL INTEGER INSTRUCTIONS
;

;
; ABSI/STI
;

(define_insn "*absqi2_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q")
        (abs:QI (match_operand:QI 1 "par_ind_operand" "S<>")))
   (set (match_operand:QI 2 "par_ind_operand" "=S<>")
        (match_operand:QI 3 "ext_low_reg_operand" "q"))
   (clobber (reg:CC_NOOV 21))]
  "TARGET_PARALLEL && valid_parallel_operands_4 (operands, QImode)"
  "absi\\t%1,%0\\n||\\tsti\\t%3,%2"
  [(set_attr "type" "binarycc")])

;
; ADDI/STI
;

(define_insn "*addqi3_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q,q")
        (plus:QI (match_operand:QI 1 "parallel_operand" "%q,S<>")
                 (match_operand:QI 2 "parallel_operand" "S<>,q")))
   (set (match_operand:QI 3 "par_ind_operand" "=S<>,S<>")
        (match_operand:QI 4 "ext_low_reg_operand" "q,q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_5 (operands, QImode)"
  "addi3\\t%2,%1,%0\\n||\\tsti\\t%4,%3"
  [(set_attr "type" "binarycc,binarycc")])

;
; AND/STI
;

(define_insn "*andqi3_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q,q")
        (and:QI (match_operand:QI 1 "parallel_operand" "%q,S<>")
                (match_operand:QI 2 "parallel_operand" "S<>,q")))
   (set (match_operand:QI 3 "par_ind_operand" "=S<>,S<>")
        (match_operand:QI 4 "ext_low_reg_operand" "q,q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_5 (operands, QImode)"
  "and3\\t%2,%1,%0\\n||\\tsti\\t%4,%3"
  [(set_attr "type" "binarycc,binarycc")])

;
; ASH(left)/STI 
;

(define_insn "*ashlqi3_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q")
        (ashift:QI (match_operand:QI 1 "par_ind_operand" "S<>")
                   (match_operand:QI 2 "ext_low_reg_operand" "q")))
   (set (match_operand:QI 3 "par_ind_operand" "=S<>")
        (match_operand:QI 4 "ext_low_reg_operand" "q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_5 (operands, QImode)"
  "ash3\\t%2,%1,%0\\n||\\tsti\\t%4,%3"
  [(set_attr "type" "binarycc")])

;
; ASH(right)/STI 
;

(define_insn "*ashrqi3_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q")
        (ashiftrt:QI (match_operand:QI 1 "par_ind_operand" "S<>")
                     (neg:QI (match_operand:QI 2 "ext_low_reg_operand" "q"))))
   (set (match_operand:QI 3 "par_ind_operand" "=S<>")
        (match_operand:QI 4 "ext_low_reg_operand" "q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_5 (operands, QImode)"
  "ash3\\t%2,%1,%0\\n||\\tsti\\t%4,%3"
  [(set_attr "type" "binarycc")])

;
; FIX/STI
;

(define_insn "*fixqfqi2_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q")
        (fix:QI (match_operand:QF 1 "par_ind_operand" "S<>")))
   (set (match_operand:QI 2 "par_ind_operand" "=S<>")
        (match_operand:QI 3 "ext_low_reg_operand" "q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_4 (operands, QImode)"
  "fix\\t%1,%0\\n||\\tsti\\t%3,%2"
  [(set_attr "type" "binarycc")])

;
; LSH(right)/STI 
;

(define_insn "*lshrqi3_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q")
        (lshiftrt:QI (match_operand:QI 1 "par_ind_operand" "S<>")
                     (neg:QI (match_operand:QI 2 "ext_low_reg_operand" "q"))))
   (set (match_operand:QI 3 "par_ind_operand" "=S<>")
        (match_operand:QI 4 "ext_low_reg_operand" "q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_5 (operands, QImode)"
  "lsh3\\t%2,%1,%0\\n||\\tsti\\t%4,%3"
  [(set_attr "type" "binarycc")])

;
; MPYI/ADDI
;

(define_insn "*mulqi3_addqi3_clobber"
  [(set (match_operand:QI 0 "r0r1_reg_operand" "=t,t,t,t")
        (mult:QI (match_operand:QI 1 "parallel_operand" "%S<>!V,q,S<>!V,q")
                 (match_operand:QI 2 "parallel_operand" "q,S<>!V,S<>!V,q")))
   (set (match_operand:QI 3 "r2r3_reg_operand" "=u,u,u,u")
        (plus:QI (match_operand:QI 4 "parallel_operand" "%S<>!V,q,q,S<>!V")
                 (match_operand:QI 5 "parallel_operand" "q,S<>!V,q,S<>!V")))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL_MPY && TARGET_MPYI 
   && valid_parallel_operands_6 (operands, QImode)"
  "mpyi3\\t%2,%1,%0\\n||\\taddi3\\t%5,%4,%3"
  [(set_attr "type" "binarycc,binarycc,binarycc,binarycc")])


;
; MPYI/STI
;

(define_insn "*mulqi3_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q,q")
        (mult:QI (match_operand:QI 1 "parallel_operand" "%q,S<>")
                 (match_operand:QI 2 "parallel_operand" "S<>,q")))
   (set (match_operand:QI 3 "par_ind_operand" "=S<>,S<>")
        (match_operand:QI 4 "ext_low_reg_operand" "q,q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && TARGET_MPYI
   && valid_parallel_operands_5 (operands, QImode)"
  "mpyi3\\t%2,%1,%0\\n||\\tsti\\t%4,%3"
  [(set_attr "type" "binarycc,binarycc")])

;
; MPYI/SUBI
;

(define_insn "*mulqi3_subqi3_clobber"
  [(set (match_operand:QI 0 "r0r1_reg_operand" "=t,t")
        (mult:QI (match_operand:QI 1 "parallel_operand" "S<>,q")
                 (match_operand:QI 2 "parallel_operand" "q,S<>")))
   (set (match_operand:QI 3 "r2r3_reg_operand" "=u,u")
        (minus:QI (match_operand:QI 4 "parallel_operand" "S<>,q")
                  (match_operand:QI 5 "parallel_operand" "q,S<>")))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL_MPY && TARGET_MPYI
   && valid_parallel_operands_6 (operands, QImode)"
  "mpyi3\\t%2,%1,%0\\n||\\tsubi3\\t%5,%4,%3"
  [(set_attr "type" "binarycc,binarycc")])

;
; MPYI/LDI 0
;

(define_insn "*mulqi3_clrqi_clobber"
  [(set (match_operand:QI 0 "r0r1_reg_operand" "=t")
        (mult:QI (match_operand:QI 1 "par_ind_operand" "%S<>")
                 (match_operand:QI 2 "par_ind_operand" "S<>")))
   (set (match_operand:QI 3 "r2r3_reg_operand" "=u")
	(const_int 0))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL_MPY && TARGET_MPYI"
  "mpyi3\\t%2,%1,%0\\n||\\tsubi3\\t%3,%3,%3"
  [(set_attr "type" "binarycc")])

;
; NEGI/STI
;

(define_insn "*negqi2_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q")
        (neg:QI (match_operand:QI 1 "par_ind_operand" "S<>")))
   (set (match_operand:QI 2 "par_ind_operand" "=S<>")
        (match_operand:QI 3 "ext_low_reg_operand" "q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_4 (operands, QImode)"
  "negi\\t%1,%0\\n||\\tsti\\t%3,%2"
  [(set_attr "type" "binarycc")])

;
; NOT/STI
;

(define_insn "*notqi2_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q")
        (not:QI (match_operand:QI 1 "par_ind_operand" "S<>")))
   (set (match_operand:QI 2 "par_ind_operand" "=S<>")
        (match_operand:QI 3 "ext_low_reg_operand" "q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_4 (operands, QImode)"
  "not\\t%1,%0\\n||\\tsti\\t%3,%2"
  [(set_attr "type" "binarycc")])

;
; OR/STI
;

(define_insn "*iorqi3_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q,q")
        (ior:QI (match_operand:QI 1 "parallel_operand" "%q,S<>")
                (match_operand:QI 2 "parallel_operand" "S<>,q")))
   (set (match_operand:QI 3 "par_ind_operand" "=S<>,S<>")
        (match_operand:QI 4 "ext_low_reg_operand" "q,q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_5 (operands, QImode)"
  "or3\\t%2,%1,%0\\n||\\tsti\\t%4,%3"
  [(set_attr "type" "binarycc,binarycc")])

;
; SUBI/STI
;

(define_insn "*subqi3_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q")
        (minus:QI (match_operand:QI 1 "par_ind_operand" "S<>")
                  (match_operand:QI 2 "ext_low_reg_operand" "q")))
   (set (match_operand:QI 3 "par_ind_operand" "=S<>")
        (match_operand:QI 4 "ext_low_reg_operand" "q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_5 (operands, QImode)"
  "subi3\\t%2,%1,%0\\n||\\tsti\\t%4,%3"
  [(set_attr "type" "binarycc")])

;
; XOR/STI
;

(define_insn "*xorqi3_movqi_clobber"
  [(set (match_operand:QI 0 "ext_low_reg_operand" "=q,q")
        (xor:QI (match_operand:QI 1 "parallel_operand" "%q,S<>")
                (match_operand:QI 2 "parallel_operand" "S<>,q")))
   (set (match_operand:QI 3 "par_ind_operand" "=S<>,S<>")
        (match_operand:QI 4 "ext_low_reg_operand" "q,q"))
   (clobber (reg:CC 21))]
  "TARGET_PARALLEL && valid_parallel_operands_5 (operands, QImode)"
  "xor3\\t%2,%1,%0\\n||\\tsti\\t%4,%3"
  [(set_attr "type" "binarycc,binarycc")])

;
; BRANCH/CALL INSTRUCTIONS
;

;
; Branch instructions
;
(define_insn "*b"
  [(set (pc) (if_then_else (match_operator 0 "comparison_operator"
			   [(reg:CC 21) (const_int 0)])
                           (label_ref (match_operand 1 "" ""))
                           (pc)))]
  ""
  "*
   return c4x_output_cbranch (\"b%0\", insn);"
  [(set_attr "type" "jmpc")])

(define_insn "*b_rev"
  [(set (pc) (if_then_else (match_operator 0 "comparison_operator"
			   [(reg:CC 21) (const_int 0)])
                           (pc)
                           (label_ref (match_operand 1 "" ""))))]
  ""
  "*
   return c4x_output_cbranch (\"b%I0\", insn);"
  [(set_attr "type" "jmpc")])

(define_insn "*b_noov"
  [(set (pc) (if_then_else (match_operator 0 "comparison_operator"
			   [(reg:CC_NOOV 21) (const_int 0)])
                           (label_ref (match_operand 1 "" ""))
                           (pc)))]
 "GET_CODE (operands[0]) != LE
  && GET_CODE (operands[0]) != GE
  && GET_CODE (operands[0]) != LT
  && GET_CODE (operands[0]) != GT"
  "*
   return c4x_output_cbranch (\"b%0\", insn);"
  [(set_attr "type" "jmpc")])

(define_insn "*b_noov_rev"
  [(set (pc) (if_then_else (match_operator 0 "comparison_operator"
			   [(reg:CC_NOOV 21) (const_int 0)])
                           (pc)
                           (label_ref (match_operand 1 "" ""))))]
 "GET_CODE (operands[0]) != LE
  && GET_CODE (operands[0]) != GE
  && GET_CODE (operands[0]) != LT
  && GET_CODE (operands[0]) != GT"
  "*
   return c4x_output_cbranch (\"b%I0\", insn);"
  [(set_attr "type" "jmpc")])

(define_expand "beq"
  [(set (pc) (if_then_else (eq (match_dup 1) (const_int 0))
                           (label_ref (match_operand 0 "" ""))
                           (pc)))]
  ""
  "operands[1] = c4x_gen_compare_reg (EQ, c4x_compare_op0, c4x_compare_op1);")

(define_expand "bne"
  [(set (pc) (if_then_else (ne (match_dup 1) (const_int 0))
                           (label_ref (match_operand 0 "" ""))
                           (pc)))]
  ""
  "operands[1] = c4x_gen_compare_reg (NE, c4x_compare_op0, c4x_compare_op1);")

(define_expand "blt"
  [(set (pc) (if_then_else (lt (match_dup 1) (const_int 0))
                           (label_ref (match_operand 0 "" ""))
                           (pc)))]
  ""
  "operands[1] = c4x_gen_compare_reg (LT, c4x_compare_op0, c4x_compare_op1);
   if (operands[1] == NULL_RTX) FAIL;")

(define_expand "bltu"
  [(set (pc) (if_then_else (ltu (match_dup 1) (const_int 0))
                           (label_ref (match_operand 0 "" ""))
                           (pc)))]
  ""
  "operands[1] = c4x_gen_compare_reg (LTU, c4x_compare_op0, c4x_compare_op1);")

(define_expand "bgt"
  [(set (pc) (if_then_else (gt (match_dup 1) (const_int 0))
                           (label_ref (match_operand 0 "" ""))
                           (pc)))]
  ""
  "operands[1] = c4x_gen_compare_reg (GT, c4x_compare_op0, c4x_compare_op1);
   if (operands[1] == NULL_RTX) FAIL;")

(define_expand "bgtu"
  [(set (pc) (if_then_else (gtu (match_dup 1) (const_int 0))
                           (label_ref (match_operand 0 "" ""))
                           (pc)))]
  ""
  "operands[1] = c4x_gen_compare_reg (GTU, c4x_compare_op0, c4x_compare_op1);")

(define_expand "ble"
  [(set (pc) (if_then_else (le (match_dup 1) (const_int 0))
                           (label_ref (match_operand 0 "" ""))
                           (pc)))]
  ""
  "operands[1] = c4x_gen_compare_reg (LE, c4x_compare_op0, c4x_compare_op1);
   if (operands[1] == NULL_RTX) FAIL;")

(define_expand "bleu"
  [(set (pc) (if_then_else (leu (match_dup 1) (const_int 0))
                           (label_ref (match_operand 0 "" ""))
                           (pc)))]
  ""
  "operands[1] = c4x_gen_compare_reg (LEU, c4x_compare_op0, c4x_compare_op1);")

(define_expand "bge"
  [(set (pc) (if_then_else (ge (match_dup 1) (const_int 0))
                           (label_ref (match_operand 0 "" ""))
                           (pc)))]
  ""
  "operands[1] = c4x_gen_compare_reg (GE, c4x_compare_op0, c4x_compare_op1);
   if (operands[1] == NULL_RTX) FAIL;")

(define_expand "bgeu"
  [(set (pc) (if_then_else (geu (match_dup 1) (const_int 0))
                           (label_ref (match_operand 0 "" ""))
                           (pc)))]
  ""
  "operands[1] = c4x_gen_compare_reg (GEU, c4x_compare_op0, c4x_compare_op1);")

(define_insn "*b_reg"
 [(set (pc) (match_operand:QI 0 "reg_operand" "r"))]
 ""
 "bu%#\\t%0"
  [(set_attr "type" "jump")])

(define_expand "indirect_jump"
 [(set (pc) (match_operand:QI 0 "reg_operand" ""))]
 ""
 "")

(define_insn "tablejump"
  [(set (pc) (match_operand:QI 0 "src_operand" "r"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
  "bu%#\\t%0"
  [(set_attr "type" "jump")])

;
; CALL
;
(define_insn "*call_c3x"
 [(call (mem:QI (match_operand:QI 0 "call_address_operand" "Ur"))
        (match_operand:QI 1 "general_operand" ""))
  (clobber (reg:QI 31))]
  ;; Operand 1 not really used on the C4x.  The C30 doesn't have reg 31.

  "TARGET_C3X"
  "call%U0\\t%C0"
  [(set_attr "type" "call")])

; LAJ requires R11 (31) for the return address
(define_insn "*laj"
 [(call (mem:QI (match_operand:QI 0 "call_address_operand" "Ur"))
        (match_operand:QI 1 "general_operand" ""))
  (clobber (reg:QI 31))]
  ;; Operand 1 not really used on the C4x.

  "! TARGET_C3X"
  "*
   if (final_sequence)
     return c4x_check_laj_p (insn)
	 ? \"nop\\n\\tlaj%U0\\t%C0\" : \"laj%U0\\t%C0\";
   else
     return \"call%U0\\t%C0\";"
  [(set_attr "type" "laj")])

(define_expand "call"
 [(parallel [(call (match_operand:QI 0 "" "")
                   (match_operand:QI 1 "general_operand" ""))
             (clobber (reg:QI 31))])]
 ""
 "
{
  if (GET_CODE (operands[0]) == MEM
      && ! call_address_operand (XEXP (operands[0], 0), Pmode))
    operands[0] = gen_rtx_MEM (GET_MODE (operands[0]),
			       force_reg (Pmode, XEXP (operands[0], 0)));
}")

(define_insn "nodb_call"
 [(call (mem:QI (match_operand:QI 0 "call_address_operand" "Ur"))
	(const_int 0))]
  ""
  "call%U0\\t%C0"
  [(set_attr "type" "call")])

(define_insn "*callv_c3x"
 [(set (match_operand 0 "" "=r")
       (call (mem:QI (match_operand:QI 1 "call_address_operand" "Ur"))
             (match_operand:QI 2 "general_operand" "")))
  (clobber (reg:QI 31))]
  ;; Operand 0 and 2 not really used for the C4x. 
  ;; The C30 doesn't have reg 31.

  "TARGET_C3X"
  "call%U1\\t%C1"
  [(set_attr "type" "call")])

; LAJ requires R11 (31) for the return address
(define_insn "*lajv"
 [(set (match_operand 0 "" "=r")
       (call (mem:QI (match_operand:QI 1 "call_address_operand" "Ur"))
             (match_operand:QI 2 "general_operand" "")))
  (clobber (reg:QI 31))]
  ;; Operand 0 and 2 not really used in the C30 instruction.

  "! TARGET_C3X"
  "*
   if (final_sequence)
     return c4x_check_laj_p (insn)
	 ? \"nop\\n\\tlaj%U1\\t%C1\" : \"laj%U1\\t%C1\";
   else
     return \"call%U1\\t%C1\";"
  [(set_attr "type" "laj")])

(define_expand "call_value"
 [(parallel [(set (match_operand 0 "" "")
                  (call (match_operand:QI 1 "" "")
                        (match_operand:QI 2 "general_operand" "")))
             (clobber (reg:QI 31))])]
 ""
 "
{
  if (GET_CODE (operands[0]) == MEM
      && ! call_address_operand (XEXP (operands[1], 0), Pmode))
    operands[0] = gen_rtx_MEM (GET_MODE (operands[1]),
                               force_reg (Pmode, XEXP (operands[1], 0)));
}")

(define_insn "return"
  [(return)]
  "! c4x_null_epilogue_p ()"
  "rets"
  [(set_attr "type" "rets")])

(define_insn "return_from_epilogue"
  [(return)]
  "reload_completed && ! c4x_interrupt_function_p ()"
  "rets"
  [(set_attr "type" "rets")])

(define_insn "return_from_interrupt_epilogue"
  [(return)]
  "reload_completed && c4x_interrupt_function_p ()"
  "reti"
  [(set_attr "type" "rets")])

(define_insn "*return_cc"
  [(set (pc)
        (if_then_else (match_operator 0 "comparison_operator"
                      [(reg:CC 21) (const_int 0)])
                      (return)
                       (pc)))]
  "! c4x_null_epilogue_p ()"
  "rets%0"
  [(set_attr "type" "rets")])

(define_insn "*return_cc_noov"
  [(set (pc)
        (if_then_else (match_operator 0 "comparison_operator"
                      [(reg:CC_NOOV 21) (const_int 0)])
                      (return)
                       (pc)))]
  "GET_CODE (operands[0]) != LE
   && GET_CODE (operands[0]) != GE
   && GET_CODE (operands[0]) != LT
   && GET_CODE (operands[0]) != GT
   && ! c4x_null_epilogue_p ()"
  "rets%0"
  [(set_attr "type" "rets")])

(define_insn "*return_cc_inverse"
  [(set (pc)
        (if_then_else (match_operator 0 "comparison_operator"
                      [(reg:CC 21) (const_int 0)])
                       (pc)
                      (return)))]
  "! c4x_null_epilogue_p ()"
  "rets%I0"
  [(set_attr "type" "rets")])

(define_insn "*return_cc_noov_inverse"
  [(set (pc)
        (if_then_else (match_operator 0 "comparison_operator"
                      [(reg:CC_NOOV 21) (const_int 0)])
                       (pc)
                      (return)))]
  "GET_CODE (operands[0]) != LE
   && GET_CODE (operands[0]) != GE
   && GET_CODE (operands[0]) != LT
   && GET_CODE (operands[0]) != GT
   && ! c4x_null_epilogue_p ()"
  "rets%I0"
  [(set_attr "type" "rets")])

(define_insn "jump"
  [(set (pc) (label_ref (match_operand 0 "" "")))]
  ""
  "br%#\\t%l0"
  [(set_attr "type" "jump")])

(define_insn "trap"
  [(trap_if (const_int 1) (const_int 31))]
  ""
  "trapu\\t31"
  [(set_attr "type" "call")])

(define_expand "conditional_trap"
 [(trap_if (match_operand 0 "comparison_operator" "")
	   (match_operand 1 "const_int_operand" ""))]
 ""
 "{
    enum rtx_code code = GET_CODE (operands[1]);
    rtx ccreg = c4x_gen_compare_reg (code, c4x_compare_op0, c4x_compare_op1);
    if (ccreg == NULL_RTX) FAIL;
    if (GET_MODE (ccreg) == CCmode)
      emit_insn (gen_cond_trap_cc (operands[0], operands[1]));
    else 
      emit_insn (gen_cond_trap_cc_noov (operands[0], operands[1]));
    DONE;}")

(define_insn "cond_trap_cc"
  [(trap_if (match_operator 0 "comparison_operator"
            [(reg:CC 21) (const_int 0)])
	    (match_operand 1 "const_int_operand" ""))]
  ""
  "trap%0\\t31"
  [(set_attr "type" "call")])

(define_insn "cond_trap_cc_noov"
  [(trap_if (match_operator 0 "comparison_operator"
            [(reg:CC_NOOV 21) (const_int 0)])
	    (match_operand 1 "const_int_operand" ""))]
  "GET_CODE (operands[0]) != LE
   && GET_CODE (operands[0]) != GE
   && GET_CODE (operands[0]) != LT
   && GET_CODE (operands[0]) != GT"
  "trap%0\\t31"
  [(set_attr "type" "call")])

;
; DBcond
;
; Note we have to emit a dbu instruction if there are no delay slots
; to fill.
; Also note that GCC will try to reverse a loop to see if it can
; utilize this instruction.  However, if there are more than one
; memory reference in the loop, it cannot guarantee that reversing
; the loop will work :(  (see check_dbra_loop() in loop.c)
; Note that the C3x only decrements the 24 LSBs of the address register
; and the 8 MSBs are untouched.  The C4x uses all 32-bits.  We thus
; have an option to disable this instruction.
(define_insn "*db"
  [(set (pc)
        (if_then_else (ne (match_operand:QI 0 "addr_reg_operand" "+a,?*d,??*r,!m")
                          (const_int 0))
                      (label_ref (match_operand 1 "" ""))
                      (pc)))
   (set (match_dup 0)
        (plus:QI (match_dup 0)
                 (const_int -1)))
   (use (reg:QI 20))
   (clobber (reg:CC_NOOV 21))]
  "TARGET_DB && TARGET_LOOP_UNSIGNED"
  "*
  if (which_alternative == 0)
    return \"dbu%#\\t%0,%l1\";
  else if (which_alternative == 1)
    return c4x_output_cbranch (\"subi\\t1,%0\\n\\tbge\", insn);
  else if (which_alternative == 2)
    return c4x_output_cbranch (\"subi\\t1,%0\\n\\tcmpi\\t0,%0\\n\\tbge\", insn);
  else
    return c4x_output_cbranch (\"push\\tr0\\n\\tldi\\t%0,r0\\n\\tsubi\\t1,r0\\n\\tsti\\tr0,%0\\n\\tpop\\tr0\\n\\tbhs\", insn);
  "
  [(set_attr "type" "db,jmpc,jmpc,jmpc")])

(define_insn "*db_noclobber"
  [(set (pc)
        (if_then_else (ne (match_operand:QI 0 "addr_reg_operand" "+a")
                          (const_int 0))
                      (label_ref (match_operand 1 "" ""))
                      (pc)))
   (set (match_dup 0)
        (plus:QI (match_dup 0)
                 (const_int -1)))]
  "reload_completed && TARGET_DB && TARGET_LOOP_UNSIGNED"
  "dbu%#\\t%0,%l1"
  [(set_attr "type" "db")])

(define_split
  [(set (pc)
        (if_then_else (ne (match_operand:QI 0 "addr_reg_operand" "")
			  (const_int 0))
		      (label_ref (match_operand 1 "" ""))
		      (pc)))
   (set (match_dup 0)
        (plus:QI (match_dup 0)
                 (const_int -1)))
   (use (reg:QI 20))
   (clobber (reg:CC_NOOV 21))]
  "reload_completed && TARGET_DB && TARGET_LOOP_UNSIGNED"
  [(parallel [(set (pc)
                   (if_then_else (ne (match_dup 0)
			             (const_int 0))
		                 (label_ref (match_dup 1))
		                 (pc)))
              (set (match_dup 0)
                   (plus:QI (match_dup 0)
                            (const_int -1)))])]
  "")
  

; This insn is used for some loop tests, typically loops reversed when
; strength reduction is used.  It is actually created when the instruction
; combination phase combines the special loop test.  Since this insn
; is both a jump insn and has an output, it must deal with its own
; reloads, hence the `m' constraints. 

; The C4x does the decrement and then compares the result against zero.
; It branches if the result was greater than or equal to zero.
; In the RTL the comparison and decrement are assumed to happen
; at the same time so we bias the iteration counter with by -1
; when we make the test.
(define_insn "decrement_and_branch_until_zero"
  [(set (pc)
        (if_then_else (ge (plus:QI (match_operand:QI 0 "addr_reg_operand" "+a,?*d,??*r,!m")
			           (const_int -1))
			  (const_int 0))
                      (label_ref (match_operand 1 "" ""))
                      (pc)))
   (set (match_dup 0)
        (plus:QI (match_dup 0)
                 (const_int -1)))
   (use (reg:QI 20))
   (clobber (reg:CC_NOOV 21))]
  "TARGET_DB && (find_reg_note (insn, REG_NONNEG, 0) || TARGET_LOOP_UNSIGNED)"
  "*
  if (which_alternative == 0)
    return \"dbu%#\\t%0,%l1\";
  else if (which_alternative == 1)
    return c4x_output_cbranch (\"subi\\t1,%0\\n\\tbge\", insn);
  else if (which_alternative == 2)
    return c4x_output_cbranch (\"subi\\t1,%0\\n\\tcmpi\\t0,%0\\n\\tbge\", insn);
  else
    return c4x_output_cbranch (\"push\\tr0\\n\\tldi\\t%0,r0\\n\\tsubi\\t1,r0\\n\\tsti\\tr0,%0\\n\\tpop\\tr0\\n\\tbhs\", insn);
  "
  [(set_attr "type" "db,jmpc,jmpc,jmpc")])

(define_insn "*decrement_and_branch_until_zero_noclobber"
  [(set (pc)
        (if_then_else (ge (plus:QI (match_operand:QI 0 "addr_reg_operand" "+a")
			           (const_int -1))
			  (const_int 0))
                      (label_ref (match_operand 1 "" ""))
                      (pc)))
   (set (match_dup 0)
        (plus:QI (match_dup 0)
                 (const_int -1)))]
  "reload_completed && TARGET_DB && TARGET_LOOP_UNSIGNED"
  "dbu%#\\t%0,%l1"
  [(set_attr "type" "db")])

(define_split
  [(set (pc)
        (if_then_else (ge (plus:QI (match_operand:QI 0 "addr_reg_operand" "")
			           (const_int -1))
			  (const_int 0))
                      (label_ref (match_operand 1 "" ""))
                      (pc)))
   (set (match_dup 0)
        (plus:QI (match_dup 0)
                 (const_int -1)))
   (use (reg:QI 20))
   (clobber (reg:CC_NOOV 21))]
  "reload_completed && TARGET_DB && TARGET_LOOP_UNSIGNED"
  [(parallel [(set (pc)
        	   (if_then_else (ge (plus:QI (match_dup 0)
			                      (const_int -1))
			             (const_int 0))
                                 (label_ref (match_dup 1))
                                 (pc)))
              (set (match_dup 0)
                   (plus:QI (match_dup 0)
                            (const_int -1)))])]
  "")

;
; MISC INSTRUCTIONS
;

;
; NOP
;
(define_insn "nop"
  [(const_int 0)]
  ""
  "nop")
; Default to misc type attr.

(define_insn "return_indirect_internal"
  [(return)
   (use (match_operand:QI 0 "reg_operand" ""))]
  "reload_completed"                           
  "bu%#\\t%0"
  [(set_attr "type" "jump")])

(define_expand "prologue"
  [(const_int 1)]
  ""                           
  "c4x_expand_prologue (); DONE;")

(define_expand "epilogue"
  [(const_int 1)]
  ""
  "c4x_expand_epilogue (); DONE;")

;
; RPTB
;
(define_insn "rptb_top"
  [(use (label_ref (match_operand 0 "" "")))
   (use (label_ref (match_operand 1 "" "")))
   (clobber (reg:QI 25))
   (clobber (reg:QI 26))]
  ""
  "*
   return ! final_sequence && c4x_rptb_rpts_p (insn, operands[0])
	 ? \"rpts\\trc\" : \"rptb%#\\t%l1-1\";
  "
  [(set_attr "type" "repeat_top")])

(define_insn "rpts_top"
  [(unspec [(use (label_ref (match_operand 0 "" "")))
            (use (label_ref (match_operand 1 "" "")))] UNSPEC_RPTS)
   (clobber (reg:QI 25))
   (clobber (reg:QI 26))]
  ""
  "*
   return ! final_sequence && c4x_rptb_rpts_p (insn, operands[0])
	 ? \"rpts\\trc\" : \"rptb%#\\t%l1-1\";
  "
  [(set_attr "type" "repeat")])

; This pattern needs to be emitted at the start of the loop to
; say that RS and RE are loaded.
(define_insn "rptb_init"
  [(unspec [(match_operand:QI 0 "register_operand" "va")] UNSPEC_RPTB_INIT)
   (clobber (reg:QI 25))
   (clobber (reg:QI 26))]
  ""
  ""
  [(set_attr "type" "repeat")])


; operand 0 is the loop count pseudo register
; operand 1 is the number of loop iterations or 0 if it is unknown
; operand 2 is the maximum number of loop iterations
; operand 3 is the number of levels of enclosed loops
(define_expand "doloop_begin"
  [(use (match_operand 0 "register_operand" ""))
   (use (match_operand:QI 1 "const_int_operand" ""))
   (use (match_operand:QI 2 "const_int_operand" ""))
   (use (match_operand:QI 3 "const_int_operand" ""))]
  ""
  "if (INTVAL (operands[3]) > 1 || ! TARGET_RPTB)
     FAIL;
   emit_insn (gen_rptb_init (operands[0]));
   DONE;
  ")


; The RS (25) and RE (26) registers must be unviolate from the top of the loop
; to here.
(define_insn "rptb_end"
  [(set (pc)
        (if_then_else (ge (match_operand:QI 0 "register_operand" "+v,?a,!*d,!*x*k,!m")
                          (const_int 0))
                      (label_ref (match_operand 1 "" ""))
                      (pc)))
   (set (match_dup 0)
        (plus:QI (match_dup 0)
                 (const_int -1)))
   (use (reg:QI 25))
   (use (reg:QI 26))
   (use (reg:QI 20))
   (clobber (reg:CC_NOOV 21))]
  ""
  "*
   if (which_alternative == 0)
     return c4x_rptb_nop_p (insn) ? \"nop\" : \"\";
   else if (which_alternative == 1 && TARGET_DB)
     return \"dbu%#\\t%0,%l1\";
   else if (which_alternative == 2)
     return c4x_output_cbranch (\"subi\\t1,%0\\n\\tbge\", insn);
   else if (which_alternative == 3 || (which_alternative == 1 && ! TARGET_DB))
     return c4x_output_cbranch (\"subi\\t1,%0\\n\\tcmpi\\t0,%0\\n\\tbge\", insn);
   else
     return c4x_output_cbranch (\"push\\tr0\\n\\tldi\\t%0,r0\\n\\tsubi\\t1,r0\\n\\tsti\\tr0,%0\\n\\tpop\\tr0\\n\\tbhs\", insn);
  "
  [(set_attr "type" "repeat,db,jmpc,jmpc,jmpc")])

(define_split
   [(set (pc)
        (if_then_else (ge (match_operand:QI 0 "addr_reg_operand" "")
                          (const_int 0))
                      (label_ref (match_operand 1 "" ""))
                      (pc)))
   (set (match_dup 0)
        (plus:QI (match_dup 0)
                 (const_int -1)))
   (use (match_operand:QI 2 "const_int_operand" ""))
   (use (match_operand:QI 3 "const_int_operand" ""))
   (use (match_operand:QI 4 "const_int_operand" ""))
   (use (reg:QI 25))
   (use (reg:QI 26))
   (use (reg:QI 20))
   (clobber (reg:CC_NOOV 21))]
  "reload_completed"
  [(parallel [(set (pc)
                   (if_then_else (ge (match_dup 0)
			             (const_int 0))
		                 (label_ref (match_dup 1))
		                 (pc)))
              (set (match_dup 0)
                   (plus:QI (match_dup 0)
                            (const_int -1)))])]
  "")

; operand 0 is the loop count pseudo register
; operand 1 is the number of loop iterations or 0 if it is unknown
; operand 2 is the maximum number of loop iterations
; operand 3 is the number of levels of enclosed loops
; operand 4 is the label to jump to at the top of the loop
(define_expand "doloop_end"
  [(use (match_operand 0 "register_operand" ""))
   (use (match_operand:QI 1 "const_int_operand" ""))
   (use (match_operand:QI 2 "const_int_operand" ""))
   (use (match_operand:QI 3 "const_int_operand" ""))
   (use (label_ref (match_operand 4 "" "")))]
  ""
  "if (! TARGET_LOOP_UNSIGNED 
       && (unsigned HOST_WIDE_INT) INTVAL (operands[2]) > ((unsigned) 1 << 31))
     FAIL;
   if (INTVAL (operands[3]) > 1 || ! TARGET_RPTB)
     {
        /* The C30 maximum iteration count for DB is 2^24.  */
	if (! TARGET_DB)
          FAIL;
        emit_jump_insn (gen_decrement_and_branch_until_zero (operands[0],
                                                             operands[4]));
	DONE;
     }
    emit_jump_insn (gen_rptb_end (operands[0], operands[4]));
    DONE;
  ")

(define_expand "decrement_and_branch_on_count"
  [(parallel [(set (pc)
                   (if_then_else (ge (match_operand:QI 0 "register_operand" "")
                                     (const_int 0))
                                 (label_ref (match_operand 1 "" ""))
                                 (pc)))
              (set (match_dup 0)
		   (plus:QI (match_dup 0)
			    (const_int -1)))
              (use (reg:QI 25))
              (use (reg:QI 26))
              (clobber (reg:CC_NOOV 21))])]
  "0"
  "")

(define_expand "movmemqi_small"
  [(parallel [(set (mem:BLK (match_operand:BLK 0 "src_operand" ""))
                   (mem:BLK (match_operand:BLK 1 "src_operand" "")))
              (use (match_operand:QI 2 "immediate_operand" ""))
              (use (match_operand:QI 3 "immediate_operand" ""))
              (clobber (match_operand:QI 4 "ext_low_reg_operand" ""))])]
  ""
  "
 {
    rtx src, dst, tmp;
    rtx src_mem, dst_mem;    
    int len;
    int i;

    dst = operands[0];
    src = operands[1];
    len = INTVAL (operands[2]);
    tmp = operands[4];

    src_mem = gen_rtx_MEM (QImode, src);
    dst_mem = gen_rtx_MEM (QImode, dst);

    if (TARGET_PARALLEL)
      {
        emit_insn (gen_movqi (tmp, src_mem));	
        emit_insn (gen_addqi3_noclobber (src, src, const1_rtx));	
        for (i = 1; i < len; i++)
          {
            emit_insn (gen_movqi_parallel (tmp, src_mem, dst_mem, tmp));
            emit_insn (gen_addqi3_noclobber (src, src, const1_rtx));	
            emit_insn (gen_addqi3_noclobber (dst, dst, const1_rtx));	
          }
        emit_insn (gen_movqi (dst_mem, tmp));	
        emit_insn (gen_addqi3_noclobber (dst, dst, const1_rtx));	
      }
    else
      {
        for (i = 0; i < len; i++)
          {
	    emit_insn (gen_movqi (tmp, src_mem));	
	    emit_insn (gen_movqi (dst_mem, tmp));	
            emit_insn (gen_addqi3_noclobber (src, src, const1_rtx));	
            emit_insn (gen_addqi3_noclobber (dst, dst, const1_rtx));	
          }
      }
    DONE;
  }
  ")


;
; BLOCK MOVE
; We should probably get RC loaded when using RPTB automagically...
; There's probably no need to call _memcpy() if we don't get
; an immediate operand for the size.  We could do a better job here
; than most memcpy() implementations.
; operand 2 is the number of bytes
; operand 3 is the shared alignment
; operand 4 is a scratch register

(define_insn "movmemqi_large"
  [(set (mem:BLK (match_operand:QI 0 "addr_reg_operand" "a"))
        (mem:BLK (match_operand:QI 1 "addr_reg_operand" "a")))
   (use (match_operand:QI 2 "immediate_operand" "i"))
   (use (match_operand:QI 3 "immediate_operand" ""))
   (clobber (match_operand:QI 4 "ext_low_reg_operand" "=&q"))
   (clobber (match_scratch:QI 5 "=0"))
   (clobber (match_scratch:QI 6 "=1"))
   (clobber (reg:QI 25))
   (clobber (reg:QI 26))
   (clobber (reg:QI 27))]
  ""
  "*
 {
   int i;
   int len = INTVAL (operands[2]);

   output_asm_insn (\"ldiu\\t*%1++,%4\", operands);
   if (len < 8)
     {
       for (i = 1; i < len; i++)
	 {
           output_asm_insn (\"sti\\t%4,*%0++\", operands);
           output_asm_insn (\"|| ldi\\t*%1++,%4\", operands);
         } 
     }
   else
     {
       if (TARGET_RPTS_CYCLES (len))
         {
           output_asm_insn (\"rpts\\t%2-2\", operands);  
           output_asm_insn (\"sti\\t%4,*%0++\", operands);
           output_asm_insn (\"|| ldi\\t*%1++,%4\", operands);
         }
       else
         {
           output_asm_insn (\"ldiu\\t%2-2,rc\", operands);
           output_asm_insn (\"rptb\\t$+1\", operands);  
           output_asm_insn (\"sti\\t%4,*%0++\", operands);
           output_asm_insn (\"|| ldi\\t*%1++,%4\", operands);
	 }
     }
   return \"sti\\t%4,*%0++\";
 }"
 [(set_attr "type" "multi")])

; Operand 2 is the count, operand 3 is the alignment.
(define_expand "movmemqi"
  [(parallel [(set (mem:BLK (match_operand:BLK 0 "src_operand" ""))
                   (mem:BLK (match_operand:BLK 1 "src_operand" "")))
              (use (match_operand:QI 2 "immediate_operand" ""))
              (use (match_operand:QI 3 "immediate_operand" ""))])]
  ""
  "
 {
   rtx tmp;
   if (GET_CODE (operands[2]) != CONST_INT 
       || INTVAL (operands[2]) > 32767 
       || INTVAL (operands[2]) <= 0)
     {
        FAIL;  /* Try to call _memcpy */
     }

   operands[0] = copy_to_mode_reg (Pmode, XEXP (operands[0], 0));
   operands[1] = copy_to_mode_reg (Pmode, XEXP (operands[1], 0));
   tmp = gen_reg_rtx (QImode);
   /* Disabled because of reload problems.  */
   if (0 && INTVAL (operands[2]) < 8)
     emit_insn (gen_movmemqi_small (operands[0], operands[1], operands[2],
                                    operands[3], tmp));
   else
     {
      emit_insn (gen_movmemqi_large (operands[0], operands[1], operands[2],
                                     operands[3], tmp));
     }
   DONE;
 }")


(define_insn "*cmpstrnqi"
  [(set (match_operand:QI 0 "ext_reg_operand" "=d")
        (compare:QI (mem:BLK (match_operand:QI 1 "addr_reg_operand" "+a"))
                    (mem:BLK (match_operand:QI 2 "addr_reg_operand" "+a"))))
   (use (match_operand:QI 3 "immediate_operand" "i"))
   (use (match_operand:QI 4 "immediate_operand" ""))
   (clobber (match_operand:QI 5 "std_reg_operand" "=&c"))
   (clobber (reg:QI 21))]
  ""
  "*
 {
    output_asm_insn (\"ldi\\t%3-1,%5\", operands);
    output_asm_insn (\"$1:\tsubi3\\t*%1++,*%2++,%0\", operands);
    output_asm_insn (\"dbeq\\t%5,$1\", operands);
    return \"\";
 }")

(define_expand "cmpstrnqi"
  [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                   (compare:QI (match_operand:BLK 1 "general_operand" "")
                               (match_operand:BLK 2 "general_operand" "")))
              (use (match_operand:QI 3 "immediate_operand" ""))
              (use (match_operand:QI 4 "immediate_operand" ""))
              (clobber (match_dup 5))
              (clobber (reg:QI 21))])]
  ""
  "
{
   if (GET_CODE (operands[3]) != CONST_INT
       || INTVAL (operands[3]) > 32767 
       || INTVAL (operands[3]) <= 0)
     {
        FAIL;
     }
   operands[1] = copy_to_mode_reg (Pmode, XEXP (operands[1], 0));
   operands[2] = copy_to_mode_reg (Pmode, XEXP (operands[2], 0));
   operands[5] = gen_reg_rtx (QImode);
}")

;
; TWO OPERAND LONG DOUBLE INSTRUCTIONS
;

(define_expand "movhf"
  [(set (match_operand:HF 0 "src_operand" "")
        (match_operand:HF 1 "src_operand" ""))]
 ""
 "if (c4x_emit_move_sequence (operands, HFmode))
    DONE;")

(define_insn "*movhf_noclobber_reg"
  [(set (match_operand:HF 0 "reg_operand" "=h")
        (match_operand:HF 1 "src_operand" "Hh"))]
 "GET_CODE (operands[1]) != MEM"
 "ldfu\\t%1,%0"
  [(set_attr "type" "unary")])

(define_insn "*movhf_noclobber"
 [(set (match_operand:HF 0 "dst_operand" "=h,m")
       (match_operand:HF 1 "src_operand" "Hm,h"))]
 "reg_operand (operands[0], HFmode) ^ reg_operand (operands[1], HFmode)"
 "#"
 [(set_attr "type" "multi,multi")])

(define_insn "*movhf_test"
  [(set (reg:CC 21)
        (compare:CC (match_operand:HF 1 "reg_operand" "h")
                    (const_int 0)))
   (clobber (match_scratch:HF 0 "=h"))]
 ""
 "ldf\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*movhf_set"
  [(set (reg:CC 21)
        (compare:CC (match_operand:HF 1 "reg_operand" "h")
                    (match_operand:HF 2 "fp_zero_operand" "G")))
    (set (match_operand:HF 0 "reg_operand" "=h")
         (match_dup 1))]
 ""
 "ldf\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_split
 [(set (match_operand:HF 0 "reg_operand" "")
       (match_operand:HF 1 "memory_operand" ""))]
 "reload_completed"
 [(set (match_dup 0) (float_extend:HF (match_dup 2)))
  (set (match_dup 0) (unspec:HF [(subreg:QI (match_dup 0) 0)
                                            (match_dup 3)] UNSPEC_LOADHF_INT))]
 "operands[2] = c4x_operand_subword (operands[1], 0, 1, HFmode);
  operands[3] = c4x_operand_subword (operands[1], 1, 1, HFmode);
  PUT_MODE (operands[2], QFmode);
  PUT_MODE (operands[3], QImode);")

(define_split
 [(set (match_operand:HF 0 "reg_operand" "")
       (match_operand:HF 1 "const_operand" ""))]
 "reload_completed && 0"
 [(set (match_dup 0) (float_extend:HF (match_dup 2)))
  (set (match_dup 0) (unspec:HF [(subreg:QI (match_dup 0) 0)
                                            (match_dup 3)] UNSPEC_LOADHF_INT))]
 "operands[2] = c4x_operand_subword (operands[1], 0, 1, HFmode);
  operands[3] = c4x_operand_subword (operands[1], 1, 1, HFmode);
  PUT_MODE (operands[2], QFmode);
  PUT_MODE (operands[3], QImode);")

(define_split
 [(set (match_operand:HF 0 "memory_operand" "")
       (match_operand:HF 1 "reg_operand" ""))]
  "reload_completed"
  [(set (match_dup 2) (float_truncate:QF (match_dup 1)))
   (set (match_dup 3) (unspec:QI [(match_dup 1)] UNSPEC_STOREHF_INT))]
 "operands[2] = c4x_operand_subword (operands[0], 0, 1, HFmode);
  operands[3] = c4x_operand_subword (operands[0], 1, 1, HFmode);
  PUT_MODE (operands[2], QFmode);
  PUT_MODE (operands[3], QImode);")

(define_insn "*loadhf_float"
 [(set (match_operand:HF 0 "reg_operand" "=h")
       (float_extend:HF (match_operand:QF 1 "src_operand" "fHm")))]
 ""
 "ldfu\\t%1,%0"
  [(set_attr "type" "unary")])

(define_insn "*loadhf_int"
 [(set (match_operand:HF 0 "reg_operand" "+h")
       (unspec:HF [(subreg:QI (match_dup 0) 0)
                   (match_operand:QI 1 "src_operand" "rIm")] UNSPEC_LOADHF_INT))]
 ""
 "ldiu\\t%1,%0"
  [(set_attr "type" "unary")])

(define_insn "*storehf_float"
  [(set (match_operand:QF 0 "memory_operand" "=m")
        (float_truncate:QF (match_operand:HF 1 "reg_operand" "h")))]
  ""
  "stf\\t%1,%0"
  [(set_attr "type" "store")])

(define_insn "*storehf_int"
 [(set (match_operand:QI 0 "memory_operand" "=m")
       (unspec:QI [(match_operand:HF 1 "reg_operand" "h")] UNSPEC_STOREHF_INT))]
 ""
 "sti\\t%1,%0"
  [(set_attr "type" "store")])

(define_insn "extendqfhf2"
  [(set (match_operand:HF 0 "reg_operand" "=h")
        (float_extend:HF (match_operand:QF 1 "reg_operand" "h")))]
  ""
  "ldfu\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "trunchfqf2"
  [(set (match_operand:QF 0 "reg_operand" "=h")
        (float_truncate:QF (match_operand:HF 1 "reg_operand" "0")))
   (clobber (reg:CC 21))]
  ""
  "andn\\t0ffh,%0"
  [(set_attr "type" "unarycc")])

;
; PUSH/POP
;
(define_insn "pushhf"
  [(set (mem:HF (pre_inc:QI (reg:QI 20)))
        (match_operand:HF 0 "reg_operand" "h"))]
 ""
 "#"
 [(set_attr "type" "multi")])

(define_split
 [(set (mem:HF (pre_inc:QI (reg:QI 20)))
        (match_operand:HF 0 "reg_operand" ""))]
  "reload_completed"
  [(set (mem:QF (pre_inc:QI (reg:QI 20)))
        (float_truncate:QF (match_dup 0)))
   (set (mem:QI (pre_inc:QI (reg:QI 20)))
        (unspec:QI [(match_dup 0)] UNSPEC_STOREHF_INT))]
 "")

(define_insn "pushhf_trunc"
  [(set (mem:QF (pre_inc:QI (reg:QI 20)))
        (float_truncate:QF (match_operand:HF 0 "reg_operand" "h")))]
 ""
 "pushf\\t%0"
 [(set_attr "type" "push")])

(define_insn "pushhf_int"
  [(set (mem:QI (pre_inc:QI (reg:QI 20)))
        (unspec:QI [(match_operand:HF 0 "reg_operand" "h")] UNSPEC_STOREHF_INT))]
 ""
 "push\\t%0"
 [(set_attr "type" "push")])

; we cannot use this because the popf will destroy the low 8 bits
;(define_insn "pophf"
;  [(set (match_operand:HF 0 "reg_operand" "=h")
;        (mem:HF (post_dec:QI (reg:QI 20))))
;   (clobber (reg:CC 21))]
; ""
; "#"
; [(set_attr "type" "multi")])

(define_split
 [(set (match_operand:HF 0 "reg_operand" "")
       (mem:HF (post_dec:QI (reg:QI 20))))
   (clobber (reg:CC 21))]
  "reload_completed"
  [(parallel [(set (match_operand:HF 0 "reg_operand" "=h")
                   (float_extend:HF (mem:QF (post_dec:QI (reg:QI 20)))))
              (clobber (reg:CC 21))])
   (parallel [(set (match_dup 0)
                   (unspec:HF [(subreg:QI (match_dup 0) 0)
                   (mem:QI (post_dec:QI (reg:QI 20)))] UNSPEC_LOADHF_INT))
              (clobber (reg:CC 21))])]
 "")

(define_insn "*pophf_int"
 [(set (match_operand:HF 0 "reg_operand" "+h")
       (unspec:HF [(subreg:QI (match_dup 0) 0)
                   (mem:QI (post_dec:QI (reg:QI 20)))] UNSPEC_LOADHF_INT))
  (clobber (reg:CC 21))]
 ""
 "pop\\t%0"
  [(set_attr "type" "pop")])

(define_insn "*pophf_float"
 [(set (match_operand:HF 0 "reg_operand" "=h")
       (float_extend:HF (mem:QF (post_dec:QI (reg:QI 20)))))
  (clobber (reg:CC 21))]
 ""
 "popf\\t%0"
  [(set_attr "type" "pop")])

;
; FIX
;
(define_expand "fixuns_trunchfqi2"
 [(parallel [(set (match_dup 2)
		  (fix:QI (match_operand:HF 1 "reg_or_const_operand" "hH")))
	     (clobber (reg:CC 21))])
  (parallel [(set (match_dup 3)
	          (minus:HF (match_dup 1) (match_dup 5)))
	     (clobber (reg:CC_NOOV 21))])
  (parallel [(set (reg:CC 21)
		  (compare:CC (fix:QI (match_dup 3))
		              (const_int 0)))
	     (set (match_dup 4)
		  (fix:QI (match_dup 3)))])
  (parallel [(set (match_dup 4) (unspec:QI [(match_dup 2)] UNSPEC_LDIV))
             (use (reg:CC 21))])
  (set (match_operand:QI 0 "reg_operand" "=r") (match_dup 4))]
 ""
 "operands[2] = gen_reg_rtx (QImode);
  operands[3] = gen_reg_rtx (HFmode);
  operands[4] = gen_reg_rtx (QImode);
  operands[5] = gen_reg_rtx (HFmode);
  emit_move_insn (operands[5], CONST_DOUBLE_ATOF (\"4294967296.0\", HFmode));")

(define_expand "fix_trunchfqi2"
  [(parallel [(set (match_dup 2)
                   (fix:QI (match_operand:HF 1 "reg_or_const_operand" "")))
              (clobber (reg:CC 21))])
   (parallel [(set (match_dup 3) (neg:HF (match_dup 1)))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (fix:QI (match_dup 3)))
              (clobber (reg:CC 21))])
   (parallel [(set (reg:CC_NOOV 21)
                   (compare:CC_NOOV (neg:QI (match_dup 4)) (const_int 0)))
              (set (match_dup 5) (neg:QI (match_dup 4)))])
   (set (match_dup 2)
        (if_then_else:QI (le (reg:CC 21) (const_int 0))
                         (match_dup 5)
                         (match_dup 2)))
   (set (match_operand:QI 0 "reg_operand" "=r") (match_dup 2))]
 ""
 "if (TARGET_FAST_FIX)
    {
       emit_insn (gen_fixhfqi_clobber (operands[0], operands[1]));
       DONE;
    }
  operands[2] = gen_reg_rtx (QImode);
  operands[3] = gen_reg_rtx (HFmode);
  operands[4] = gen_reg_rtx (QImode);
  operands[5] = gen_reg_rtx (QImode);
 ")

(define_insn "*fixhfqi_set"
  [(set (reg:CC 21)
        (compare:CC (fix:QI (match_operand:HF 1 "reg_or_const_operand" "hH"))
                    (const_int 0)))
   (set (match_operand:QI 0 "ext_reg_operand" "=d")
        (fix:QI (match_dup 1)))]
 ""
 "fix\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "fixhfqi_clobber"
  [(set (match_operand:QI 0 "reg_operand" "=dc")
        (fix:QI (match_operand:HF 1 "reg_or_const_operand" "hH")))
   (clobber (reg:CC 21))]
 ""
 "fix\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_expand "fix_trunchfhi2"
  [(parallel [(set (match_operand:HI 0 "reg_operand" "")
                   (fix:HI (match_operand:HF 1 "reg_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "c4x_emit_libcall (fix_trunchfhi2_libfunc, FIX, HImode, HFmode, 2, operands);
   DONE;")

(define_expand "fixuns_trunchfhi2"
  [(parallel [(set (match_operand:HI 0 "reg_operand" "")
                   (unsigned_fix:HI (match_operand:HF 1 "reg_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "c4x_emit_libcall (fixuns_trunchfhi2_libfunc, UNSIGNED_FIX, 
                     HImode, HFmode, 2, operands);
   DONE;")

;
; ABSF
;
(define_expand "abshf2"
  [(parallel [(set (match_operand:HF 0 "reg_operand" "")
                   (abs:HF (match_operand:HF 1 "reg_or_const_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
""
"")

(define_insn "*abshf2_clobber"
  [(set (match_operand:HF 0 "reg_operand" "=h")
        (abs:HF (match_operand:HF 1 "reg_or_const_operand" "hH")))
   (clobber (reg:CC_NOOV 21))]
  ""
  "absf\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*abshf2_test"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (abs:HF (match_operand:HF 1 "reg_operand" "h"))
                         (match_operand:HF 2 "fp_zero_operand" "G")))
   (clobber (match_scratch:HF 0 "=h"))]
  ""
  "absf\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*abshf2_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (abs:HF (match_operand:HF 1 "reg_or_const_operand" "hH"))
                         (match_operand:HF 2 "fp_zero_operand" "G")))
   (set (match_operand:HF 0 "reg_operand" "=h")
        (abs:HF (match_dup 1)))]

  ""
  "absf\\t%1,%0"
  [(set_attr "type" "unarycc")])

;
; NEGF
;
(define_expand "neghf2"
  [(parallel [(set (match_operand:HF 0 "reg_operand" "")
                   (neg:HF (match_operand:HF 1 "reg_or_const_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
""
"")

(define_insn "*neghf2_clobber"
  [(set (match_operand:HF 0 "reg_operand" "=h")
        (neg:HF (match_operand:HF 1 "reg_or_const_operand" "hH")))
   (clobber (reg:CC_NOOV 21))]
  ""
  "negf\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*neghf2_test"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (neg:HF (match_operand:HF 1 "reg_or_const_operand" "hH"))
                         (match_operand:HF 2 "fp_zero_operand" "G")))
   (clobber (match_scratch:HF 0 "=h"))]
  ""
  "negf\\t%1,%0"
  [(set_attr "type" "unarycc")])

(define_insn "*neghf2_set"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (neg:HF (match_operand:HF 1 "reg_or_const_operand" "hH"))
                         (match_operand:HF 2 "fp_zero_operand" "G")))
   (set (match_operand:HF 0 "reg_operand" "=h")
        (neg:HF (match_dup 1)))]
  ""
  "negf\\t%1,%0"
  [(set_attr "type" "unarycc")])

;
; RCPF
;
(define_insn "*rcpfhf_clobber"
  [(set (match_operand:HF 0 "reg_operand" "=h")
        (unspec:HF [(match_operand:HF 1 "reg_or_const_operand" "hH")] UNSPEC_RCPF))
   (clobber (reg:CC_NOOV 21))]
  "! TARGET_C3X"
  "rcpf\\t%1,%0"
  [(set_attr "type" "unarycc")])

;
; RSQRF
;
(define_insn "*rsqrfhf_clobber"
  [(set (match_operand:HF 0 "reg_operand" "=h")
        (unspec:HF [(match_operand:HF 1 "reg_or_const_operand" "hH")] UNSPEC_RSQRF))
   (clobber (reg:CC_NOOV 21))]
  "! TARGET_C3X"
  "rsqrf\\t%1,%0"
  [(set_attr "type" "unarycc")])

;
; RNDF
;
(define_insn "*rndhf_clobber"
  [(set (match_operand:HF 0 "reg_operand" "=h")
        (unspec:HF [(match_operand:HF 1 "reg_or_const_operand" "hH")] UNSPEC_RND))
   (clobber (reg:CC_NOOV 21))]
  "! TARGET_C3X"
  "rnd\\t%1,%0"
  [(set_attr "type" "unarycc")])


; Inlined float square root for C4x
(define_expand "sqrthf2_inline"
  [(parallel [(set (match_dup 2)
	           (unspec:HF [(match_operand:HF 1 "reg_operand" "")] UNSPEC_RSQRF))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 3) (mult:HF (match_dup 5) (match_dup 1)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:HF (match_dup 2) (match_dup 3)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:HF (match_dup 2) (match_dup 4)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (minus:HF (match_dup 6) (match_dup 4)))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 2) (mult:HF (match_dup 2) (match_dup 4)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:HF (match_dup 2) (match_dup 3)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:HF (match_dup 2) (match_dup 4)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (minus:HF (match_dup 6) (match_dup 4)))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 2) (mult:HF (match_dup 2) (match_dup 4)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_operand:HF 0 "reg_operand" "")
		   (mult:HF (match_dup 2) (match_dup 1)))
	      (clobber (reg:CC_NOOV 21))])]
  "! TARGET_C3X"
  "
  operands[2] = gen_reg_rtx (HFmode);
  operands[3] = gen_reg_rtx (HFmode);
  operands[4] = gen_reg_rtx (HFmode);
  operands[5] = CONST_DOUBLE_ATOF (\"0.5\", HFmode);
  operands[6] = CONST_DOUBLE_ATOF (\"1.5\", HFmode);
  ")


(define_expand "sqrthf2"
  [(parallel [(set (match_operand:HF 0 "reg_operand" "")
                   (sqrt:HF (match_operand:HF 1 "reg_operand" "")))
              (clobber (reg:CC 21))])]
  "! TARGET_C3X && TARGET_INLINE"
  "emit_insn (gen_sqrthf2_inline (operands[0], operands[1]));
   DONE;")

;
; THREE OPERAND LONG DOUBLE INSTRUCTIONS
;

;
; ADDF
;
(define_insn "addhf3"
  [(set (match_operand:HF 0 "reg_operand" "=h,?h")
        (plus:HF (match_operand:HF 1 "reg_operand" "%0,h")
                 (match_operand:HF 2 "reg_or_const_operand" "H,h")))
   (clobber (reg:CC_NOOV 21))]
  ""
  "@
   addf\\t%2,%0
   addf3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc")])

;
; SUBF
;
(define_insn "subhf3"
  [(set (match_operand:HF 0 "reg_operand" "=h,h,?h")
        (minus:HF (match_operand:HF 1 "reg_or_const_operand" "0,H,h")
                  (match_operand:HF 2 "reg_or_const_operand" "H,0,h")))
   (clobber (reg:CC_NOOV 21))]
  ""
  "@
   subf\\t%2,%0
   subrf\\t%1,%0
   subf3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc,binarycc")])

;
; MULF
;
; The C3x MPYF only uses 24-bit precision while the C4x uses 32-bit precision.
;
(define_expand "mulhf3"
  [(parallel [(set (match_operand:HF 0 "reg_operand" "=h")
                   (mult:HF (match_operand:HF 1 "reg_operand" "h")
                            (match_operand:HF 2 "reg_operand" "h")))
              (clobber (reg:CC_NOOV 21))])]
  "! TARGET_C3X"
  "")

(define_insn "*mulhf3_c40"
  [(set (match_operand:HF 0 "reg_operand" "=h,?h")
        (mult:HF (match_operand:HF 1 "reg_operand" "%0,h")
                 (match_operand:HF 2 "reg_or_const_operand" "hH,h")))
   (clobber (reg:CC_NOOV 21))]
  ""
  "@
   mpyf\\t%2,%0
   mpyf3\\t%2,%1,%0"
  [(set_attr "type" "binarycc,binarycc")])

;
; CMPF
;
(define_expand "cmphf"
  [(set (reg:CC 21)
        (compare:CC (match_operand:HF 0 "reg_operand" "")
                    (match_operand:HF 1 "reg_or_const_operand" "")))]
  ""
  "c4x_compare_op0 = operands[0];
   c4x_compare_op1 = operands[1];
   DONE;")

(define_insn "*cmphf"
  [(set (reg:CC 21)
        (compare:CC (match_operand:HF 0 "reg_operand" "h")
                    (match_operand:HF 1 "reg_or_const_operand" "hH")))]
  ""
  "cmpf\\t%1,%0"
  [(set_attr "type" "compare")])

(define_insn "*cmphf_noov"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (match_operand:HF 0 "reg_operand" "h")
                         (match_operand:HF 1 "reg_or_const_operand" "hH")))]
  ""
  "cmpf\\t%1,%0"
  [(set_attr "type" "compare")])

; Inlined float divide for C4x
(define_expand "divhf3_inline"
  [(parallel [(set (match_dup 3)
	           (unspec:HF [(match_operand:HF 2 "reg_operand" "")] UNSPEC_RCPF))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:HF (match_dup 2) (match_dup 3)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (minus:HF (match_dup 5) (match_dup 4)))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 3) (mult:HF (match_dup 3) (match_dup 4)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (mult:HF (match_dup 2) (match_dup 3)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 4) (minus:HF (match_dup 5) (match_dup 4)))
              (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_dup 3) (mult:HF (match_dup 3) (match_dup 4)))
	      (clobber (reg:CC_NOOV 21))])
   (parallel [(set (match_operand:HF 0 "reg_operand" "")
		   (mult:HF (match_operand:HF 1 "reg_operand" "")
	         	    (match_dup 3)))
	      (clobber (reg:CC_NOOV 21))])]
  "! TARGET_C3X"
  "
  operands[3] = gen_reg_rtx (HFmode);
  operands[4] = gen_reg_rtx (HFmode);
  operands[5] = CONST2_RTX (HFmode);
  ")

(define_expand "divhf3"
  [(parallel [(set (match_operand:HF 0 "reg_operand" "")
                   (div:HF (match_operand:HF 1 "reg_operand" "")
                           (match_operand:HF 2 "reg_operand" "")))
              (clobber (reg:CC 21))])]
  "! TARGET_C3X && TARGET_INLINE"
  "emit_insn (gen_divhf3_inline (operands[0], operands[1], operands[2]));
   DONE;")


;
; TWO OPERAND LONG LONG INSTRUCTIONS
;

(define_insn "*movhi_stik"
  [(set (match_operand:HI 0 "memory_operand" "=m")
        (match_operand:HI 1 "stik_const_operand" "K"))]
  "! TARGET_C3X"
  "#"
  [(set_attr "type" "multi")])

; We could load some constants using define_splits for the C30
; in the large memory model---these would emit shift and or insns.
(define_expand "movhi"
  [(set (match_operand:HI 0 "src_operand" "")
        (match_operand:HI 1 "src_operand" ""))]
 ""
 "if (c4x_emit_move_sequence (operands, HImode))
    DONE;")

; The constraints for movhi must include 'r' if we don't
; restrict HImode regnos to start on an even number, since
; we can get RC, R8 allocated as a pair.  We want more
; votes for FP_REGS so we use dr as the constraints.
(define_insn "*movhi_noclobber"
  [(set (match_operand:HI 0 "dst_operand" "=dr,m")
        (match_operand:HI 1 "src_operand" "drIm,r"))]
  "reg_operand (operands[0], HImode)
   || reg_operand (operands[1], HImode)"
  "#"
  [(set_attr "type" "multi,multi")])

; This will fail miserably if the destination register is used in the 
; source memory address.
; The usual strategy in this case is to swap the order of insns we emit,
; however, this will fail if we have an autoincrement memory address.
; For example:
; ldi *ar0++, ar0
; ldi *ar0++, ar1
;
; We could convert this to
; ldi *ar0(1), ar1
; ldi *ar0, ar0
;
; However, things are likely to be very screwed up if we get this.

(define_split
  [(set (match_operand:HI 0 "dst_operand" "")
	(match_operand:HI 1 "src_operand" ""))]
  "reload_completed
   && (reg_operand (operands[0], HImode)
       || reg_operand (operands[1], HImode)
       || stik_const_operand (operands[1], HImode))"
  [(set (match_dup 2) (match_dup 4))
   (set (match_dup 3) (match_dup 5))]
  "operands[2] = c4x_operand_subword (operands[0], 0, 1, HImode);
   operands[3] = c4x_operand_subword (operands[0], 1, 1, HImode);
   operands[4] = c4x_operand_subword (operands[1], 0, 1, HImode);
   operands[5] = c4x_operand_subword (operands[1], 1, 1, HImode);
   if (reg_overlap_mentioned_p (operands[2], operands[5]))
     {
	/* Swap order of move insns.  */
	rtx tmp;
	tmp = operands[2];
        operands[2] =operands[3];
        operands[3] = tmp;
	tmp = operands[4];
        operands[4] =operands[5];
        operands[5] = tmp;        
     }")


(define_insn "extendqihi2"
  [(set (match_operand:HI 0 "reg_operand" "=dc")
        (sign_extend:HI (match_operand:QI 1 "src_operand" "rIm")))
   (clobber (reg:CC 21))]
  ""
  "#"
  [(set_attr "type" "multi")])

(define_split
  [(set (match_operand:HI 0 "reg_operand" "")
        (sign_extend:HI (match_operand:QI 1 "src_operand" "")))
   (clobber (reg:CC 21))]
  "reload_completed && TARGET_C3X"
  [(set (match_dup 2) (match_dup 1))
   (set (match_dup 3) (match_dup 2))
   (parallel [(set (match_dup 3) (ashiftrt:QI (match_dup 3) (const_int 31)))
              (clobber (reg:CC 21))])]
  "operands[2] = c4x_operand_subword (operands[0], 0, 0, HImode);
   operands[3] = c4x_operand_subword (operands[0], 1, 0, HImode);")

(define_split
  [(set (match_operand:HI 0 "reg_operand" "")
        (sign_extend:HI (match_operand:QI 1 "src_operand" "")))
   (clobber (reg:CC 21))]
  "reload_completed && ! TARGET_C3X"
  [(set (match_dup 2) (match_dup 1))
   (parallel [(set (match_dup 3) (ashiftrt:QI (match_dup 2) (const_int 31)))
              (clobber (reg:CC 21))])]
  "operands[2] = c4x_operand_subword (operands[0], 0, 0, HImode);
   operands[3] = c4x_operand_subword (operands[0], 1, 0, HImode);")

(define_insn "zero_extendqihi2"
  [(set (match_operand:HI 0 "reg_operand" "=?dc")
        (zero_extend:HI (match_operand:QI 1 "nonimmediate_src_operand" "rm")))
   (clobber (reg:CC 21))]
  ""
  "#"
  [(set_attr "type" "multi")])

; If operand0 and operand1 are the same register we don't need
; the first set.
(define_split
  [(set (match_operand:HI 0 "reg_operand" "")
        (zero_extend:HI (match_operand:QI 1 "nonimmediate_src_operand" "")))
   (clobber (reg:CC 21))]
  "reload_completed"
  [(set (match_dup 2) (match_dup 1))
   (set (match_dup 3) (const_int 0))]
  "operands[2] = c4x_operand_subword (operands[0], 0, 0, HImode);
   operands[3] = c4x_operand_subword (operands[0], 1, 0, HImode);")

;
; PUSH/POP
;
(define_insn "*pushhi"
  [(set (mem:HI (pre_inc:QI (reg:QI 20)))
        (match_operand:HI 0 "reg_operand" "r"))]
  ""
  "#"
  [(set_attr "type" "multi")])

(define_split
  [(set (mem:HI (pre_inc:QI (reg:QI 20)))
        (match_operand:HI 0 "reg_operand" ""))]
  "reload_completed"
  [(set (mem:QI (pre_inc:QI (reg:QI 20))) (match_dup 2))
   (set (mem:QI (pre_inc:QI (reg:QI 20))) (match_dup 3))]
  "operands[2] = c4x_operand_subword (operands[0], 0, 0, HImode);
   operands[3] = c4x_operand_subword (operands[0], 1, 0, HImode);")

(define_insn "*pophi"
  [(set (match_operand:HI 0 "reg_operand" "=r")
        (mem:HI (post_dec:QI (reg:QI 20))))
   (clobber (reg:CC 21))]
  ""
  "#"
  [(set_attr "type" "multi")])

(define_split
  [(set (match_operand:HI 0 "reg_operand" "")
       (mem:HI (pre_inc:QI (reg:QI 20))))]
  "reload_completed"
  [(set (match_dup 2) (mem:QI (pre_inc:QI (reg:QI 20))))
   (set (match_dup 3) (mem:QI (pre_inc:QI (reg:QI 20))))]
  "operands[2] = c4x_operand_subword (operands[0], 0, 0, HImode);
   operands[3] = c4x_operand_subword (operands[0], 1, 0, HImode);")

;
; NEG
;
(define_insn "neghi2"
  [(set (match_operand:HI 0 "ext_reg_operand" "=d")
        (neg:HI (match_operand:HI 1 "src_operand" "rm")))
   (clobber (reg:CC_NOOV 21))]
  ""
  "#"
  [(set_attr "type" "multi")])

(define_split
  [(set (match_operand:HI 0 "ext_reg_operand" "")
        (neg:HI (match_operand:HI 1 "src_operand" "")))
   (clobber (reg:CC_NOOV 21))]
  "reload_completed"
   [(parallel [(set (reg:CC_NOOV 21)
                    (compare:CC_NOOV (neg:QI (match_dup 3))
                                     (const_int 0)))
               (set (match_dup 2) (neg:QI (match_dup 3)))])
   (parallel [(set (match_dup 4) (neg:QI (match_dup 5)))
              (use (reg:CC_NOOV 21))
              (clobber (reg:CC_NOOV 21))])]
  "operands[2] = c4x_operand_subword (operands[0], 0, 1, HImode);
   operands[3] = c4x_operand_subword (operands[1], 0, 1, HImode);
   operands[4] = c4x_operand_subword (operands[0], 1, 1, HImode);
   operands[5] = c4x_operand_subword (operands[1], 1, 1, HImode);")

(define_insn "one_cmplhi2"
  [(set (match_operand:HI 0 "reg_operand" "=r")
        (not:HI (match_operand:HI 1 "src_operand" "rm")))
   (clobber (reg:CC 21))]
  ""
  "#"
  [(set_attr "type" "multi")])

(define_split
  [(set (match_operand:HI 0 "reg_operand" "")
        (not:HI (match_operand:HI 1 "src_operand" "")))
   (clobber (reg:CC 21))]
  "reload_completed"
   [(parallel [(set (match_dup 2) (not:QI (match_dup 3)))
               (clobber (reg:CC 21))])
    (parallel [(set (match_dup 4) (not:QI (match_dup 5)))
               (clobber (reg:CC 21))])]
  "operands[2] = c4x_operand_subword (operands[0], 0, 1, HImode);
   operands[3] = c4x_operand_subword (operands[1], 0, 1, HImode);
   operands[4] = c4x_operand_subword (operands[0], 1, 1, HImode);
   operands[5] = c4x_operand_subword (operands[1], 1, 1, HImode);")

(define_expand "floathiqf2"
  [(parallel [(set (match_operand:QF 0 "reg_operand" "")
                   (float:QF (match_operand:HI 1 "src_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "c4x_emit_libcall (floathiqf2_libfunc, FLOAT, QFmode, HImode, 2, operands);
   DONE;")

(define_expand "floatunshiqf2"
  [(parallel [(set (match_operand:QF 0 "reg_operand" "")
                   (unsigned_float:QF (match_operand:HI 1 "src_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "c4x_emit_libcall (floatunshiqf2_libfunc, UNSIGNED_FLOAT,
                     QFmode, HImode, 2, operands);
   DONE;")

(define_expand "floathihf2"
  [(parallel [(set (match_operand:HF 0 "reg_operand" "")
                   (float:HF (match_operand:HI 1 "src_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "c4x_emit_libcall (floathihf2_libfunc, FLOAT, HFmode, HImode, 2, operands);
   DONE;")

(define_expand "floatunshihf2"
  [(parallel [(set (match_operand:HF 0 "reg_operand" "")
                   (unsigned_float:HF (match_operand:HI 1 "src_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "c4x_emit_libcall (floatunshihf2_libfunc, UNSIGNED_FLOAT,
                     HFmode, HImode, 2, operands);
   DONE;")


;
; THREE OPERAND LONG LONG INSTRUCTIONS
;

(define_expand "addhi3"
  [(parallel [(set (match_operand:HI 0 "ext_reg_operand" "")
                   (plus:HI (match_operand:HI 1 "src_operand" "")
                            (match_operand:HI 2 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
  ""
  "legitimize_operands (PLUS, operands, HImode);")

(define_insn "*addhi3_clobber"
  [(set (match_operand:HI 0 "ext_reg_operand" "=d,d,?d")
        (plus:HI (match_operand:HI 1 "src_operand" "%0,rR,rS<>")
                 (match_operand:HI 2 "src_operand" "rm,R,rS<>")))
   (clobber (reg:CC_NOOV 21))]
  "valid_operands (PLUS, operands, HImode)"
  "#"
  [(set_attr "type" "multi,multi,multi")])

(define_split
 [(set (match_operand:HI 0 "ext_reg_operand" "")
       (plus:HI (match_operand:HI 1 "src_operand" "")
                (match_operand:HI 2 "src_operand" "")))
  (clobber (reg:CC_NOOV 21))]
 "reload_completed"
  [(parallel [(set (reg:CC_NOOV 21)
                   (compare:CC_NOOV (plus:QI (match_dup 4) (match_dup 5))
                                    (const_int 0)))
              (set (match_dup 3) (plus:QI (match_dup 4) (match_dup 5)))])
   (parallel [(set (match_dup 6) (plus:QI (match_dup 7) (match_dup 8)))
              (use (reg:CC_NOOV 21))
              (clobber (reg:CC_NOOV 21))])]
  "operands[3] = c4x_operand_subword (operands[0], 0, 1, HImode);
   operands[4] = c4x_operand_subword (operands[1], 0, 1, HImode);
   operands[5] = c4x_operand_subword (operands[2], 0, 1, HImode);
   operands[6] = c4x_operand_subword (operands[0], 1, 1, HImode);
   operands[7] = c4x_operand_subword (operands[1], 1, 1, HImode);
   operands[8] = c4x_operand_subword (operands[2], 1, 1, HImode);")

(define_expand "subhi3"
  [(parallel [(set (match_operand:HI 0 "ext_reg_operand" "")
                   (minus:HI (match_operand:HI 1 "src_operand" "")
                             (match_operand:HI 2 "src_operand" "")))
              (clobber (reg:CC_NOOV 21))])]
  ""
  "legitimize_operands (MINUS, operands, HImode);")


(define_insn "*subhi3_clobber"
  [(set (match_operand:HI 0 "ext_reg_operand" "=d,d,?d")
        (minus:HI (match_operand:HI 1 "src_operand" "0,rR,rS<>")
                  (match_operand:HI 2 "src_operand" "rm,R,rS<>")))
   (clobber (reg:CC_NOOV 21))]
  "valid_operands (MINUS, operands, HImode)"
  "#"
  [(set_attr "type" "multi,multi,multi")])

(define_split
 [(set (match_operand:HI 0 "ext_reg_operand" "")
       (minus:HI (match_operand:HI 1 "src_operand" "")
                 (match_operand:HI 2 "src_operand" "")))
  (clobber (reg:CC_NOOV 21))]
 "reload_completed"
  [(parallel [(set (reg:CC_NOOV 21)
                   (compare:CC_NOOV (minus:QI (match_dup 4) (match_dup 5))
                                    (const_int 0)))
              (set (match_dup 3) (minus:QI (match_dup 4) (match_dup 5)))])
   (parallel [(set (match_dup 6) (minus:QI (match_dup 7) (match_dup 8)))
              (use (reg:CC_NOOV 21))
              (clobber (reg:CC_NOOV 21))])]
  "operands[3] = c4x_operand_subword (operands[0], 0, 1, HImode);
   operands[4] = c4x_operand_subword (operands[1], 0, 1, HImode);
   operands[5] = c4x_operand_subword (operands[2], 0, 1, HImode);
   operands[6] = c4x_operand_subword (operands[0], 1, 1, HImode);
   operands[7] = c4x_operand_subword (operands[1], 1, 1, HImode);
   operands[8] = c4x_operand_subword (operands[2], 1, 1, HImode);")

(define_expand "iorhi3"
  [(parallel [(set (match_operand:HI 0 "reg_operand" "")
                   (ior:HI (match_operand:HI 1 "src_operand" "")
                           (match_operand:HI 2 "src_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "legitimize_operands (IOR, operands, HImode);")

(define_insn "*iorhi3_clobber"
  [(set (match_operand:HI 0 "reg_operand" "=d,d,?d")
        (ior:HI (match_operand:HI 1 "src_operand" "%0,rR,rS<>")
                (match_operand:HI 2 "src_operand" "rm,R,rS<>")))
   (clobber (reg:CC 21))]
  "valid_operands (IOR, operands, HImode)"
  "#"
  [(set_attr "type" "multi,multi,multi")])

(define_split
 [(set (match_operand:HI 0 "reg_operand" "")
       (ior:HI (match_operand:HI 1 "src_operand" "")
               (match_operand:HI 2 "src_operand" "")))
  (clobber (reg:CC 21))]
 "reload_completed"
  [(parallel [(set (match_dup 3) (ior:QI (match_dup 4) (match_dup 5)))
              (clobber (reg:CC 21))])
   (parallel [(set (match_dup 6) (ior:QI (match_dup 7) (match_dup 8)))
              (clobber (reg:CC 21))])]
  "operands[3] = c4x_operand_subword (operands[0], 0, 1, HImode);
   operands[4] = c4x_operand_subword (operands[1], 0, 1, HImode);
   operands[5] = c4x_operand_subword (operands[2], 0, 1, HImode);
   operands[6] = c4x_operand_subword (operands[0], 1, 1, HImode);
   operands[7] = c4x_operand_subword (operands[1], 1, 1, HImode);
   operands[8] = c4x_operand_subword (operands[2], 1, 1, HImode);")

(define_expand "andhi3"
  [(parallel [(set (match_operand:HI 0 "reg_operand" "")
                   (and:HI (match_operand:HI 1 "src_operand" "")
                           (match_operand:HI 2 "src_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "legitimize_operands (AND, operands, HImode);")

(define_insn "*andhi3_clobber"
  [(set (match_operand:HI 0 "reg_operand" "=d,d,?d")
        (and:HI (match_operand:HI 1 "src_operand" "%0,rR,rS<>")
                (match_operand:HI 2 "src_operand" "rm,R,rS<>")))
   (clobber (reg:CC 21))]
  "valid_operands (AND, operands, HImode)"
  "#"
  [(set_attr "type" "multi,multi,multi")])

(define_split
 [(set (match_operand:HI 0 "reg_operand" "")
       (and:HI (match_operand:HI 1 "src_operand" "")
                (match_operand:HI 2 "src_operand" "")))
  (clobber (reg:CC 21))]
 "reload_completed"
  [(parallel [(set (match_dup 3) (and:QI (match_dup 4) (match_dup 5)))
              (clobber (reg:CC 21))])
   (parallel [(set (match_dup 6) (and:QI (match_dup 7) (match_dup 8)))
              (clobber (reg:CC 21))])]
  "operands[3] = c4x_operand_subword (operands[0], 0, 1, HImode);
   operands[4] = c4x_operand_subword (operands[1], 0, 1, HImode);
   operands[5] = c4x_operand_subword (operands[2], 0, 1, HImode);
   operands[6] = c4x_operand_subword (operands[0], 1, 1, HImode);
   operands[7] = c4x_operand_subword (operands[1], 1, 1, HImode);
   operands[8] = c4x_operand_subword (operands[2], 1, 1, HImode);")

(define_expand "xorhi3"
  [(parallel [(set (match_operand:HI 0 "reg_operand" "")
                   (xor:HI (match_operand:HI 1 "src_operand" "")
                           (match_operand:HI 2 "src_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "legitimize_operands (XOR, operands, HImode);")


(define_insn "*xorhi3_clobber"
  [(set (match_operand:HI 0 "reg_operand" "=d,d,?d")
        (xor:HI (match_operand:HI 1 "src_operand" "%0,rR,rS<>")
                (match_operand:HI 2 "src_operand" "rm,R,rS<>")))
   (clobber (reg:CC 21))]
  "valid_operands (XOR, operands, HImode)"
  "#"
  [(set_attr "type" "multi,multi,multi")])

(define_split
 [(set (match_operand:HI 0 "reg_operand" "")
       (xor:HI (match_operand:HI 1 "src_operand" "")
               (match_operand:HI 2 "src_operand" "")))
  (clobber (reg:CC 21))]
 "reload_completed"
  [(parallel [(set (match_dup 3) (xor:QI (match_dup 4) (match_dup 5)))
              (clobber (reg:CC 21))])
   (parallel [(set (match_dup 6) (xor:QI (match_dup 7) (match_dup 8)))
              (clobber (reg:CC 21))])]
  "operands[3] = c4x_operand_subword (operands[0], 0, 1, HImode);
   operands[4] = c4x_operand_subword (operands[1], 0, 1, HImode);
   operands[5] = c4x_operand_subword (operands[2], 0, 1, HImode);
   operands[6] = c4x_operand_subword (operands[0], 1, 1, HImode);
   operands[7] = c4x_operand_subword (operands[1], 1, 1, HImode);
   operands[8] = c4x_operand_subword (operands[2], 1, 1, HImode);")

(define_expand "ashlhi3"
 [(parallel [(set (match_operand:HI 0 "reg_operand" "")
             (ashift:HI (match_operand:HI 1 "src_operand" "")
                        (match_operand:QI 2 "src_operand" "")))
             (clobber (reg:CC 21))])]
 ""
 "if (GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) >= 32)
    {
       rtx op0hi = operand_subword (operands[0], 1, 0, HImode);
       rtx op0lo = operand_subword (operands[0], 0, 0, HImode);
       rtx op1lo = operand_subword (operands[1], 0, 0, HImode);
       rtx count = GEN_INT ((INTVAL (operands[2]) - 32));

       if (INTVAL (count))
         emit_insn (gen_ashlqi3 (op0hi, op1lo, count));
       else
         emit_insn (gen_movqi (op0hi, op1lo));
       emit_insn (gen_movqi (op0lo, const0_rtx));
       DONE;
    }
  if (! REG_P (operands[1]))
    operands[1] = force_reg (HImode, operands[1]);
  emit_insn (gen_ashlhi3_reg (operands[0], operands[1], operands[2]));
  DONE;
 ")

; %0.lo = %1.lo << %2
; %0.hi = (%1.hi << %2 ) | (%1.lo >> (32 - %2))
; This algorithm should work for shift counts greater than 32
(define_expand "ashlhi3_reg" 
 [(use (match_operand:HI 1 "reg_operand" ""))
  (use (match_operand:HI 0 "reg_operand" ""))
  /* If the shift count is greater than 32 this will give zero.  */
  (parallel [(set (match_dup 7)
                  (ashift:QI (match_dup 3)
                             (match_operand:QI 2 "reg_operand" "")))
             (clobber (reg:CC 21))])
  /* If the shift count is greater than 32 this will give zero.  */
  (parallel [(set (match_dup 8)
                  (ashift:QI (match_dup 4) (match_dup 2)))
             (clobber (reg:CC 21))])
  (parallel [(set (match_dup 10)
                  (plus:QI (match_dup 2) (const_int -32)))
             (clobber (reg:CC_NOOV 21))])
  /* If the shift count is greater than 32 this will do a left shift.  */
  (parallel [(set (match_dup 9)
                  (lshiftrt:QI (match_dup 3) (neg:QI (match_dup 10))))
             (clobber (reg:CC 21))])
  (set (match_dup 5) (match_dup 7))
  (parallel [(set (match_dup 6)
                  (ior:QI (match_dup 8) (match_dup 9)))
             (clobber (reg:CC 21))])]
 ""
 " 
  operands[3] = operand_subword (operands[1], 0, 1, HImode); /* lo */
  operands[4] = operand_subword (operands[1], 1, 1, HImode); /* hi */
  operands[5] = operand_subword (operands[0], 0, 1, HImode); /* lo */
  operands[6] = operand_subword (operands[0], 1, 1, HImode); /* hi */
  operands[7] = gen_reg_rtx (QImode); /* lo << count */
  operands[8] = gen_reg_rtx (QImode); /* hi << count */
  operands[9] = gen_reg_rtx (QImode); /* lo >> (32 - count) */
  operands[10] = gen_reg_rtx (QImode); /* 32 - count */
 ")

; This should do all the dirty work with define_split
(define_expand "lshrhi3"
 [(parallel [(set (match_operand:HI 0 "reg_operand" "")
             (lshiftrt:HI (match_operand:HI 1 "src_operand" "")
                          (match_operand:QI 2 "src_operand" "")))
             (clobber (reg:CC 21))])]
 ""
 "if (GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) >= 32)
    {
       rtx op0hi = operand_subword (operands[0], 1, 0, HImode);
       rtx op0lo = operand_subword (operands[0], 0, 0, HImode);
       rtx op1hi = operand_subword (operands[1], 1, 0, HImode);
       rtx count = GEN_INT ((INTVAL (operands[2]) - 32));

       if (INTVAL (count))
         emit_insn (gen_lshrqi3 (op0lo, op1hi, count));
       else
         emit_insn (gen_movqi (op0lo, op1hi));
       emit_insn (gen_movqi (op0hi, const0_rtx));
       DONE;
    }
  if (! REG_P (operands[1]))
    operands[1] = force_reg (HImode, operands[1]);
  emit_insn (gen_lshrhi3_reg (operands[0], operands[1], operands[2]));
  DONE;")

; %0.hi = %1.hi >> %2
; %0.lo = (%1.lo >> %2 ) | (%1.hi << (32 - %2))
; This algorithm should work for shift counts greater than 32
(define_expand "lshrhi3_reg" 
 [(use (match_operand:HI 1 "reg_operand" ""))
  (use (match_operand:HI 0 "reg_operand" ""))
  (parallel [(set (match_dup 11)
                  (neg:QI (match_operand:QI 2 "reg_operand" "")))
             (clobber (reg:CC_NOOV 21))])
  /* If the shift count is greater than 32 this will give zero.  */
  (parallel [(set (match_dup 7)
                  (lshiftrt:QI (match_dup 3)
                               (neg:QI (match_dup 11))))
             (clobber (reg:CC 21))])
  /* If the shift count is greater than 32 this will give zero.  */
  (parallel [(set (match_dup 8)
                  (lshiftrt:QI (match_dup 4) 
                               (neg:QI (match_dup 11))))
             (clobber (reg:CC 21))])
  (parallel [(set (match_dup 10)
                  (plus:QI (match_dup 11) (const_int 32)))
             (clobber (reg:CC_NOOV 21))])
  /* If the shift count is greater than 32 this will do an arithmetic
     right shift.  However, we need a logical right shift.  */
  (parallel [(set (match_dup 9)
                  (ashift:QI (match_dup 4) (unspec:QI [(match_dup 10)] UNSPEC_LSH)))
             (clobber (reg:CC 21))])
  (set (match_dup 6) (match_dup 8))
  (parallel [(set (match_dup 5)
                  (ior:QI (match_dup 7) (match_dup 9)))
             (clobber (reg:CC 21))])]
 ""
 " 
  operands[3] = operand_subword (operands[1], 0, 1, HImode); /* lo */
  operands[4] = operand_subword (operands[1], 1, 1, HImode); /* hi */
  operands[5] = operand_subword (operands[0], 0, 1, HImode); /* lo */
  operands[6] = operand_subword (operands[0], 1, 1, HImode); /* hi */
  operands[7] = gen_reg_rtx (QImode); /* lo >> count */
  operands[8] = gen_reg_rtx (QImode); /* hi >> count */
  operands[9] = gen_reg_rtx (QImode); /* hi << (32 - count) */
  operands[10] = gen_reg_rtx (QImode); /* 32 - count */
  operands[11] = gen_reg_rtx (QImode); /* -count */
 ")

; This should do all the dirty work with define_split
(define_expand "ashrhi3"
  [(parallel [(set (match_operand:HI 0 "reg_operand" "")
              (ashiftrt:HI (match_operand:HI 1 "src_operand" "")
                           (match_operand:QI 2 "src_operand" "")))
              (clobber (reg:CC 21))])]
 ""
 "if (GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) >= 32)
    {
       rtx op0hi = operand_subword (operands[0], 1, 0, HImode);
       rtx op0lo = operand_subword (operands[0], 0, 0, HImode);
       rtx op1hi = operand_subword (operands[1], 1, 0, HImode);
       rtx count = GEN_INT ((INTVAL (operands[2]) - 32));

       if (INTVAL (count))
         emit_insn (gen_ashrqi3 (op0lo, op1hi, count));
       else
         emit_insn (gen_movqi (op0lo, op1hi));
       emit_insn (gen_ashrqi3 (op0hi, op1hi, GEN_INT (31)));
       DONE;
    }
  if (! REG_P (operands[1]))
    operands[1] = force_reg (HImode, operands[1]);
  emit_insn (gen_ashrhi3_reg (operands[0], operands[1], operands[2]));
  DONE;")

; %0.hi = %1.hi >> %2
; %0.lo = (%1.lo >> %2 ) | (%1.hi << (32 - %2))
; This algorithm should work for shift counts greater than 32
(define_expand "ashrhi3_reg" 
 [(use (match_operand:HI 1 "reg_operand" ""))
  (use (match_operand:HI 0 "reg_operand" ""))
  (parallel [(set (match_dup 11)
                  (neg:QI (match_operand:QI 2 "reg_operand" "")))
             (clobber (reg:CC_NOOV 21))])
  /* If the shift count is greater than 32 this will give zero.  */
  (parallel [(set (match_dup 7)
                  (lshiftrt:QI (match_dup 3)
                               (neg:QI (match_dup 11))))
             (clobber (reg:CC 21))])
  /* If the shift count is greater than 32 this will give zero.  */
  (parallel [(set (match_dup 8)
                  (ashiftrt:QI (match_dup 4) 
                               (neg:QI (match_dup 11))))
             (clobber (reg:CC 21))])
  (parallel [(set (match_dup 10)
                  (plus:QI (match_dup 11) (const_int 32)))
             (clobber (reg:CC_NOOV 21))])
  /* If the shift count is greater than 32 this will do an arithmetic
     right shift.  */
  (parallel [(set (match_dup 9)
                  (ashift:QI (match_dup 4) (match_dup 10)))
             (clobber (reg:CC 21))])
  (set (match_dup 6) (match_dup 8))
  (parallel [(set (match_dup 5)
                  (ior:QI (match_dup 7) (match_dup 9)))
             (clobber (reg:CC 21))])]
 ""
 " 
  operands[3] = operand_subword (operands[1], 0, 1, HImode); /* lo */
  operands[4] = operand_subword (operands[1], 1, 1, HImode); /* hi */
  operands[5] = operand_subword (operands[0], 0, 1, HImode); /* lo */
  operands[6] = operand_subword (operands[0], 1, 1, HImode); /* hi */
  operands[7] = gen_reg_rtx (QImode); /* lo >> count */
  operands[8] = gen_reg_rtx (QImode); /* hi >> count */
  operands[9] = gen_reg_rtx (QImode); /* hi << (32 - count) */
  operands[10] = gen_reg_rtx (QImode); /* 32 - count */
  operands[11] = gen_reg_rtx (QImode); /* -count */
 ")

(define_expand "cmphi"
  [(set (reg:CC 21)
        (compare:CC (match_operand:HI 0 "src_operand" "")
                    (match_operand:HI 1 "src_operand" "")))]
  ""
  "legitimize_operands (COMPARE, operands, HImode);
   c4x_compare_op0 = operands[0];
   c4x_compare_op1 = operands[1];
   DONE;")

(define_insn "*cmphi_cc"
  [(set (reg:CC 21)
        (compare:CC (match_operand:HI 0 "src_operand" "rR,rS<>")
                    (match_operand:HI 1 "src_operand" "R,rS<>")))]
  "valid_operands (COMPARE, operands, HImode)"
  "#"
  [(set_attr "type" "multi")])

(define_insn "*cmphi_cc_noov"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (match_operand:HI 0 "src_operand" "rR,rS<>")
                         (match_operand:HI 1 "src_operand" "R,rS<>")))]
  "valid_operands (COMPARE, operands, HImode)"
  "#"
  [(set_attr "type" "multi")])

; This works only before reload because we need 2 extra registers.
; Use unspec to avoid recursive split.
(define_split
  [(set (reg:CC 21)
        (compare:CC (match_operand:HI 0 "src_operand" "")
                    (match_operand:HI 1 "src_operand" "")))]
  "! reload_completed"
  [(parallel [(set (reg:CC 21)
                   (unspec:CC [(compare:CC (match_dup 0)
                                           (match_dup 1))] UNSPEC_CMPHI))
              (clobber (match_scratch:QI 2 ""))
	      (clobber (match_scratch:QI 3 ""))])]
  "")

(define_split
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (match_operand:HI 0 "src_operand" "")
                         (match_operand:HI 1 "src_operand" "")))]
  "! reload_completed"
  [(parallel [(set (reg:CC_NOOV 21)
                   (unspec:CC_NOOV [(compare:CC_NOOV (match_dup 0)
                                                     (match_dup 1))] UNSPEC_CMPHI))
              (clobber (match_scratch:QI 2 ""))
	      (clobber (match_scratch:QI 3 ""))])]
  "")

; This is normally not used. The define splits above are used first.
(define_split
  [(set (reg:CC 21)
        (compare:CC (match_operand:HI 0 "src_operand" "")
                    (match_operand:HI 1 "src_operand" "")))]
  "reload_completed"
  [(parallel [(set (reg:CC 21)
                   (compare:CC (match_dup 0) (match_dup 1)))
              (use (reg:QI 20))])]
  "")

(define_split
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (match_operand:HI 0 "src_operand" "")
                         (match_operand:HI 1 "src_operand" "")))]
  "reload_completed"
  [(parallel [(set (reg:CC_NOOV 21)
                   (compare:CC_NOOV (match_dup 0) (match_dup 1)))
              (use (reg:QI 20))])]
  "")

(define_insn "*cmphi"
  [(set (reg:CC 21)
        (compare:CC (match_operand:HI 0 "src_operand" "rR,rS<>")
                    (match_operand:HI 1 "src_operand" "R,rS<>")))
   (use (reg:QI 20))]
  "valid_operands (COMPARE, operands, HImode)"
  "*
   {
     int use_ir1 = (reg_operand (operands[0], HImode)
	            && REG_P (operands[0])
		    && REGNO (operands[0]) == IR1_REGNO)
		    || (reg_operand (operands[1], HImode)
		        && REG_P (operands[1])
		        && REGNO (operands[1]) == IR1_REGNO);

     if (use_ir1)
       output_asm_insn (\"push\\tir1\", operands);
     else
       output_asm_insn (\"push\\tbk\", operands);
     output_asm_insn (\"push\\tr0\", operands);
     output_asm_insn (\"subi3\\t%1,%0,r0\", operands);
     if (use_ir1)
       {
         output_asm_insn (\"ldiu\\tst,ir1\", operands);
         output_asm_insn (\"or\\t07bh,ir1\", operands);
       }
     else
       {
         output_asm_insn (\"ldiu\\tst,bk\", operands);
         output_asm_insn (\"or\\t07bh,bk\", operands);
       }
     output_asm_insn (\"subb3\\t%O1,%O0,r0\", operands);
     if (use_ir1)
       output_asm_insn (\"and3\\tir1,st,ir1\", operands);
     else
       output_asm_insn (\"and3\\tbk,st,bk\", operands);
     output_asm_insn (\"pop\\tr0\", operands);
     if (use_ir1)
       {
         output_asm_insn (\"ldiu\\tir1,st\", operands);
         output_asm_insn (\"pop\\tir1\", operands);
       }
     else
       {
         output_asm_insn (\"ldiu\\tbk,st\", operands);
         output_asm_insn (\"pop\\tbk\", operands);
       }
     return \"\";
   }"
  [(set_attr "type" "multi")])
 
(define_insn "*cmphi_noov"
  [(set (reg:CC_NOOV 21)
        (compare:CC_NOOV (match_operand:HI 0 "src_operand" "rR,rS<>")
                         (match_operand:HI 1 "src_operand" "R,rS<>")))
   (use (reg:QI 20))]
  "valid_operands (COMPARE, operands, HImode)"
  "*
   {
     int use_ir1 = (reg_operand (operands[0], HImode)
	            && REG_P (operands[0])
		    && REGNO (operands[0]) == IR1_REGNO)
		    || (reg_operand (operands[1], HImode)
		        && REG_P (operands[1])
		        && REGNO (operands[1]) == IR1_REGNO);

     if (use_ir1)
       output_asm_insn (\"push\\tir1\", operands);
     else
       output_asm_insn (\"push\\tbk\", operands);
     output_asm_insn (\"push\\tr0\", operands);
     output_asm_insn (\"subi3\\t%1,%0,r0\", operands);
     if (use_ir1)
       {
         output_asm_insn (\"ldiu\\tst,ir1\", operands);
         output_asm_insn (\"or\\t07bh,ir1\", operands);
       }
     else
       {
         output_asm_insn (\"ldiu\\tst,bk\", operands);
         output_asm_insn (\"or\\t07bh,bk\", operands);
       }
     output_asm_insn (\"subb3\\t%O1,%O0,r0\", operands);
     if (use_ir1)
       output_asm_insn (\"and3\\tir1,st,ir1\", operands);
     else
       output_asm_insn (\"and3\\tbk,st,bk\", operands);
     output_asm_insn (\"pop\\tr0\", operands);
     if (use_ir1)
       {
         output_asm_insn (\"ldiu\\tir1,st\", operands);
         output_asm_insn (\"pop\\tir1\", operands);
       }
     else
       {
         output_asm_insn (\"ldiu\\tbk,st\", operands);
         output_asm_insn (\"pop\\tbk\", operands);
       }
     return \"\";
   }"
  [(set_attr "type" "multi")])

 
(define_insn "cmphi_cc"
  [(set (reg:CC 21)
        (unspec:CC [(compare:CC (match_operand:HI 0 "src_operand" "rR,rS<>")
                                (match_operand:HI 1 "src_operand" "R,rS<>"))] UNSPEC_CMPHI))
   (clobber (match_scratch:QI 2 "=&d,&d"))
   (clobber (match_scratch:QI 3 "=&c,&c"))]
  "valid_operands (COMPARE, operands, HImode)"
  "*
   output_asm_insn (\"subi3\\t%1,%0,%2\", operands);
   output_asm_insn (\"ldiu\\tst,%3\", operands);
   output_asm_insn (\"or\\t07bh,%3\", operands);
   output_asm_insn (\"subb3\\t%O1,%O0,%2\", operands);
   output_asm_insn (\"and\\t%3,st\", operands);
   return \"\";"
  [(set_attr "type" "multi")])

(define_insn "cmphi_cc_noov"
  [(set (reg:CC_NOOV 21)
        (unspec:CC_NOOV [(compare:CC_NOOV (match_operand:HI 0 "src_operand" "rR,rS<>")
                                     (match_operand:HI 1 "src_operand" "R,rS<>"))] UNSPEC_CMPHI))
   (clobber (match_scratch:QI 2 "=&d,&d"))
   (clobber (match_scratch:QI 3 "=&c,&c"))]
  "valid_operands (COMPARE, operands, HImode)"
  "*
   output_asm_insn (\"subi3\\t%1,%0,%2\", operands);
   output_asm_insn (\"ldiu\\tst,%3\", operands);
   output_asm_insn (\"or\\t07bh,%3\", operands);
   output_asm_insn (\"subb3\\t%O1,%O0,%2\", operands);
   output_asm_insn (\"and\\t%3,st\", operands);
   return \"\";"
  [(set_attr "type" "multi")])

(define_expand "mulhi3"
  [(parallel [(set (match_operand:HI 0 "reg_operand" "")
                   (mult:HI (match_operand:HI 1 "src_operand" "")
                            (match_operand:HI 2 "src_operand" "")))
              (clobber (reg:CC 21))])]
  ""
  "c4x_emit_libcall3 (smul_optab->handlers[(int) HImode].libfunc,
		      MULT, HImode, operands);
   DONE;")


;
; PEEPHOLES
;

; dbCC peepholes
;
; Turns
;   loop:
;           [ ... ]
;           bCC label           ; abnormal loop termination
;           dbu aN, loop        ; normal loop termination
;
; Into
;   loop:
;           [ ... ]
;           dbCC aN, loop
;           bCC label
;
; Which moves the bCC condition outside the inner loop for free.
;
(define_peephole
  [(set (pc) (if_then_else (match_operator 3 "comparison_operator"
                           [(reg:CC 21) (const_int 0)])
                           (label_ref (match_operand 2 "" ""))
                           (pc)))
   (parallel
    [(set (pc)
          (if_then_else
            (ge (plus:QI (match_operand:QI 0 "addr_reg_operand" "+a")
                         (const_int -1))
                (const_int 0))
            (label_ref (match_operand 1 "" ""))
            (pc)))
     (set (match_dup 0)
          (plus:QI (match_dup 0)
                   (const_int -1)))
     (use (reg:QI 20))
     (clobber (reg:CC_NOOV 21))])]
  "! c4x_label_conflict (insn, operands[2], operands[1])"
  "db%I3\\t%0,%l1\\n\\tb%3\\t%l2"
  [(set_attr "type" "multi")])

(define_peephole
  [(set (pc) (if_then_else (match_operator 3 "comparison_operator"
                           [(reg:CC 21) (const_int 0)])
                           (label_ref (match_operand 2 "" ""))
                           (pc)))
   (parallel
    [(set (pc)
          (if_then_else
            (ne (match_operand:QI 0 "addr_reg_operand" "+a")
                (const_int 0))
            (label_ref (match_operand 1 "" ""))
            (pc)))
     (set (match_dup 0)
          (plus:QI (match_dup 0)
                   (const_int -1)))])]
  "! c4x_label_conflict (insn, operands[2], operands[1])"
  "db%I3\\t%0,%l1\\n\\tb%3\\t%l2"
  [(set_attr "type" "multi")])

;
; Peepholes to convert 'call label; rets' into jump label
;

(define_peephole
  [(parallel [(call (mem:QI (match_operand:QI 0 "call_address_operand" ""))
                    (match_operand:QI 1 "general_operand" ""))
              (clobber (reg:QI 31))])
   (return)]
  "! c4x_null_epilogue_p ()"
  "*
   if (REG_P (operands[0]))
     return \"bu%#\\t%C0\";
   else
     return \"br%#\\t%C0\";"
  [(set_attr "type" "jump")])

(define_peephole
  [(parallel [(set (match_operand 0 "" "")
                   (call (mem:QI (match_operand:QI 1 "call_address_operand" ""))
                         (match_operand:QI 2 "general_operand" "")))
              (clobber (reg:QI 31))])
   (return)]
  "! c4x_null_epilogue_p ()"
  "*
   if (REG_P (operands[1]))
     return \"bu%#\\t%C1\";
   else
     return \"br%#\\t%C1\";"
  [(set_attr "type" "jump")])


; This peephole should be unnecessary with my patches to flow.c
; for better autoincrement detection
(define_peephole
 [(set (match_operand:QF 0 "ext_low_reg_operand" "")
       (mem:QF (match_operand:QI 1 "addr_reg_operand" "")))
  (set (match_operand:QF 2 "ext_low_reg_operand" "")
       (mem:QF (plus:QI (match_dup 1) (const_int 1))))
  (parallel [(set (match_dup 1) (plus:QI (match_dup 1) (const_int 2)))
             (clobber (reg:CC_NOOV 21))])]
 ""
 "ldf\\t*%1++,%0\\n\\tldf\\t*%1++,%2")


; This peephole should be unnecessary with my patches to flow.c
; for better autoincrement detection
(define_peephole
 [(set (mem:QF (match_operand:QI 0 "addr_reg_operand" ""))
       (match_operand:QF 1 "ext_low_reg_operand" ""))
  (set (mem:QF (plus:QI (match_dup 0) (const_int 1)))
       (match_operand:QF 2 "ext_low_reg_operand" ""))
  (parallel [(set (match_dup 0) (plus:QI (match_dup 0) (const_int 2)))
             (clobber (reg:CC_NOOV 21))])]
 ""
 "stf\\t%1,*%0++\\n\\tstf\\t%2,*%0++")


; The following two peepholes remove an unnecessary load
; often found at the end of a function.  These peepholes
; could be generalized to other binary operators.  They shouldn't
; be required if we run a post reload mop-up pass.
(define_peephole
 [(parallel [(set (match_operand:QF 0 "ext_reg_operand" "")
                  (plus:QF (match_operand:QF 1 "ext_reg_operand" "")
                           (match_operand:QF 2 "ext_reg_operand" "")))
             (clobber (reg:CC_NOOV 21))])
  (set (match_operand:QF 3 "ext_reg_operand" "")
       (match_dup 0))]
 "dead_or_set_p (insn, operands[0])"
 "addf3\\t%2,%1,%3")

(define_peephole
 [(parallel [(set (match_operand:QI 0 "reg_operand" "")
                  (plus:QI (match_operand:QI 1 "reg_operand" "")
                           (match_operand:QI 2 "reg_operand" "")))
             (clobber (reg:CC_NOOV 21))])
  (set (match_operand:QI 3 "reg_operand" "")
       (match_dup 0))]
 "dead_or_set_p (insn, operands[0])"
 "addi3\\t%2,%1,%3")

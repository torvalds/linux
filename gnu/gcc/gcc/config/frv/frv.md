;; Frv Machine Description
;; Copyright (C) 1999, 2000, 2001, 2003, 2004, 2005 Free Software Foundation,
;; Inc.
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
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

;;- See file "rtl.def" for documentation on define_insn, match_*, et. al.


;; ::::::::::::::::::::
;; ::
;; :: Unspec's used
;; ::
;; ::::::::::::::::::::

;; GOT constants must go 12/HI/LO for the splitter to work

(define_constants
  [(UNSPEC_BLOCKAGE		0)
   (UNSPEC_CC_TO_GPR		1)
   (UNSPEC_GPR_TO_CC		2)
   (UNSPEC_PIC_PROLOGUE		3)
   (UNSPEC_CR_LOGIC		4)
   (UNSPEC_STACK_ADJUST		5)
   (UNSPEC_EH_RETURN_EPILOGUE	6)
   (UNSPEC_GOT			7)
   (UNSPEC_LDD			8)
   (UNSPEC_OPTIONAL_MEMBAR	9)

   (UNSPEC_GETTLSOFF			200)
   (UNSPEC_TLS_LOAD_GOTTLSOFF12		201)
   (UNSPEC_TLS_INDIRECT_CALL		202)
   (UNSPEC_TLS_TLSDESC_LDD		203)
   (UNSPEC_TLS_TLSDESC_LDD_AUX		204)
   (UNSPEC_TLS_TLSOFF_LD		205)
   (UNSPEC_TLS_LDDI			206)
   (UNSPEC_TLSOFF_HILO			207)

   (R_FRV_GOT12			11)
   (R_FRV_GOTHI			12)
   (R_FRV_GOTLO			13)
   (R_FRV_FUNCDESC		14)
   (R_FRV_FUNCDESC_GOT12	15)
   (R_FRV_FUNCDESC_GOTHI	16)
   (R_FRV_FUNCDESC_GOTLO	17)
   (R_FRV_FUNCDESC_VALUE	18)
   (R_FRV_FUNCDESC_GOTOFF12	19)
   (R_FRV_FUNCDESC_GOTOFFHI	20)
   (R_FRV_FUNCDESC_GOTOFFLO	21)
   (R_FRV_GOTOFF12		22)
   (R_FRV_GOTOFFHI		23)
   (R_FRV_GOTOFFLO		24)
   (R_FRV_GPREL12		25)
   (R_FRV_GPRELHI		26)
   (R_FRV_GPRELLO		27)
   (R_FRV_GOTTLSOFF_HI		28)
   (R_FRV_GOTTLSOFF_LO		29)
   (R_FRV_TLSMOFFHI		30)
   (R_FRV_TLSMOFFLO           	31)
   (R_FRV_TLSMOFF12           	32)
   (R_FRV_TLSDESCHI           	33)
   (R_FRV_TLSDESCLO           	34)
   (R_FRV_GOTTLSDESCHI		35)
   (R_FRV_GOTTLSDESCLO		36)

   (GR8_REG			8)
   (GR9_REG			9)
   (GR14_REG			14)
   ;; LR_REG conflicts with definition in frv.h
   (LRREG                       169)
   (FDPIC_REG			15)
   ])

(define_mode_macro IMODE [QI HI SI DI])
(define_mode_attr IMODEsuffix [(QI "b") (HI "h") (SI "") (DI "d")])
(define_mode_attr BREADsuffix [(QI "ub") (HI "uh") (SI "") (DI "d")])

;; ::::::::::::::::::::
;; ::
;; :: Constraints
;; ::
;; ::::::::::::::::::::

;; Standard Constraints
;;
;; `m' A memory operand is allowed, with any kind of address that the
;;     machine supports in general.
;;
;; `o' A memory operand is allowed, but only if the address is
;;     "offsettable".  This means that adding a small integer (actually, the
;;     width in bytes of the operand, as determined by its machine mode) may be
;;     added to the address and the result is also a valid memory address.
;;
;; `V' A memory operand that is not offsettable.  In other words,
;;     anything that would fit the `m' constraint but not the `o' constraint.
;;
;; `<' A memory operand with autodecrement addressing (either
;;     predecrement or postdecrement) is allowed.
;;
;; `>' A memory operand with autoincrement addressing (either
;;     preincrement or postincrement) is allowed.
;;
;; `r' A register operand is allowed provided that it is in a general
;;     register.
;;
;; `d', `a', `f', ...
;;     Other letters can be defined in machine-dependent fashion to stand for
;;     particular classes of registers.  `d', `a' and `f' are defined on the
;;     68000/68020 to stand for data, address and floating point registers.
;;
;; `i' An immediate integer operand (one with constant value) is allowed.
;;     This includes symbolic constants whose values will be known only at
;;     assembly time.
;;
;; `n' An immediate integer operand with a known numeric value is allowed.
;;     Many systems cannot support assembly-time constants for operands less
;;     than a word wide.  Constraints for these operands should use `n' rather
;;     than `i'.
;;
;; 'I' First machine-dependent integer constant (6 bit signed ints).
;; 'J' Second machine-dependent integer constant (10 bit signed ints).
;; 'K' Third machine-dependent integer constant (-2048).
;; 'L' Fourth machine-dependent integer constant (16 bit signed ints).
;; 'M' Fifth machine-dependent integer constant (16 bit unsigned ints).
;; 'N' Sixth machine-dependent integer constant (-2047..-1).
;; 'O' Seventh machine-dependent integer constant (zero).
;; 'P' Eighth machine-dependent integer constant (1..2047).
;;
;;     Other letters in the range `I' through `P' may be defined in a
;;     machine-dependent fashion to permit immediate integer operands with
;;     explicit integer values in specified ranges.  For example, on the 68000,
;;     `I' is defined to stand for the range of values 1 to 8.  This is the
;;     range permitted as a shift count in the shift instructions.
;;
;; `E' An immediate floating operand (expression code `const_double') is
;;     allowed, but only if the target floating point format is the same as
;;     that of the host machine (on which the compiler is running).
;;
;; `F' An immediate floating operand (expression code `const_double') is
;;     allowed.
;;
;; 'G' First machine-dependent const_double.
;; 'H' Second machine-dependent const_double.
;;
;; `s' An immediate integer operand whose value is not an explicit
;;     integer is allowed.
;;
;;     This might appear strange; if an insn allows a constant operand with a
;;     value not known at compile time, it certainly must allow any known
;;     value.  So why use `s' instead of `i'?  Sometimes it allows better code
;;     to be generated.
;;
;;     For example, on the 68000 in a fullword instruction it is possible to
;;     use an immediate operand; but if the immediate value is between -128 and
;;     127, better code results from loading the value into a register and
;;     using the register.  This is because the load into the register can be
;;     done with a `moveq' instruction.  We arrange for this to happen by
;;     defining the letter `K' to mean "any integer outside the range -128 to
;;     127", and then specifying `Ks' in the operand constraints.
;;
;; `g' Any register, memory or immediate integer operand is allowed,
;;     except for registers that are not general registers.
;;
;; `X' Any operand whatsoever is allowed, even if it does not satisfy
;;     `general_operand'.  This is normally used in the constraint of a
;;     `match_scratch' when certain alternatives will not actually require a
;;     scratch register.
;;
;; `0' Match operand 0.
;; `1' Match operand 1.
;; `2' Match operand 2.
;; `3' Match operand 3.
;; `4' Match operand 4.
;; `5' Match operand 5.
;; `6' Match operand 6.
;; `7' Match operand 7.
;; `8' Match operand 8.
;; `9' Match operand 9.
;;
;;     An operand that matches the specified operand number is allowed.  If a
;;     digit is used together with letters within the same alternative, the
;;     digit should come last.
;;
;;     This is called a "matching constraint" and what it really means is that
;;     the assembler has only a single operand that fills two roles considered
;;     separate in the RTL insn.  For example, an add insn has two input
;;     operands and one output operand in the RTL, but on most CISC machines an
;;     add instruction really has only two operands, one of them an
;;     input-output operand:
;;
;;          addl #35,r12
;;
;;     Matching constraints are used in these circumstances.  More precisely,
;;     the two operands that match must include one input-only operand and one
;;     output-only operand.  Moreover, the digit must be a smaller number than
;;     the number of the operand that uses it in the constraint.
;;
;;     For operands to match in a particular case usually means that they are
;;     identical-looking RTL expressions.  But in a few special cases specific
;;     kinds of dissimilarity are allowed.  For example, `*x' as an input
;;     operand will match `*x++' as an output operand.  For proper results in
;;     such cases, the output template should always use the output-operand's
;;     number when printing the operand.
;;
;; `p' An operand that is a valid memory address is allowed.  This is for
;;     "load address" and "push address" instructions.
;;
;;     `p' in the constraint must be accompanied by `address_operand' as the
;;     predicate in the `match_operand'.  This predicate interprets the mode
;;     specified in the `match_operand' as the mode of the memory reference for
;;     which the address would be valid.
;;
;; `Q` First non constant, non register machine-dependent insns
;; `R` Second non constant, non register machine-dependent insns
;; `S` Third non constant, non register machine-dependent insns
;; `T` Fourth non constant, non register machine-dependent insns
;; `U` Fifth non constant, non register machine-dependent insns
;;
;;     Letters in the range `Q' through `U' may be defined in a
;;     machine-dependent fashion to stand for arbitrary operand types.  The
;;     machine description macro `EXTRA_CONSTRAINT' is passed the operand as
;;     its first argument and the constraint letter as its second operand.
;;
;;     A typical use for this would be to distinguish certain types of memory
;;     references that affect other insn operands.
;;
;;     Do not define these constraint letters to accept register references
;;     (`reg'); the reload pass does not expect this and would not handle it
;;     properly.

;; Multiple Alternative Constraints
;; `?' Disparage slightly the alternative that the `?' appears in, as a
;;     choice when no alternative applies exactly.  The compiler regards this
;;     alternative as one unit more costly for each `?' that appears in it.
;;
;; `!' Disparage severely the alternative that the `!' appears in.  This
;;     alternative can still be used if it fits without reloading, but if
;;     reloading is needed, some other alternative will be used.

;; Constraint modifiers
;; `=' Means that this operand is write-only for this instruction: the
;;     previous value is discarded and replaced by output data.
;;
;; `+' Means that this operand is both read and written by the
;;     instruction.
;;
;;     When the compiler fixes up the operands to satisfy the constraints, it
;;     needs to know which operands are inputs to the instruction and which are
;;     outputs from it.  `=' identifies an output; `+' identifies an operand
;;     that is both input and output; all other operands are assumed to be
;;     input only.
;;
;; `&' Means (in a particular alternative) that this operand is written
;;     before the instruction is finished using the input operands.  Therefore,
;;     this operand may not lie in a register that is used as an input operand
;;     or as part of any memory address.
;;
;;     `&' applies only to the alternative in which it is written.  In
;;     constraints with multiple alternatives, sometimes one alternative
;;     requires `&' while others do not.
;;
;;     `&' does not obviate the need to write `='.
;;
;; `%' Declares the instruction to be commutative for this operand and the
;;     following operand.  This means that the compiler may interchange the two
;;     operands if that is the cheapest way to make all operands fit the
;;     constraints.  This is often used in patterns for addition instructions
;;     that really have only two operands: the result must go in one of the
;;     arguments.
;;
;; `#' Says that all following characters, up to the next comma, are to be
;;     ignored as a constraint.  They are significant only for choosing
;;     register preferences.
;;
;; `*' Says that the following character should be ignored when choosing
;;     register preferences.  `*' has no effect on the meaning of the
;;     constraint as a constraint, and no effect on reloading.


;; ::::::::::::::::::::
;; ::
;; :: Attributes
;; ::
;; ::::::::::::::::::::

;; The `define_attr' expression is used to define each attribute required by
;; the target machine.  It looks like:
;;
;; (define_attr NAME LIST-OF-VALUES DEFAULT)

;; NAME is a string specifying the name of the attribute being defined.

;; LIST-OF-VALUES is either a string that specifies a comma-separated list of
;; values that can be assigned to the attribute, or a null string to indicate
;; that the attribute takes numeric values.

;; DEFAULT is an attribute expression that gives the value of this attribute
;; for insns that match patterns whose definition does not include an explicit
;; value for this attribute.

;; For each defined attribute, a number of definitions are written to the
;; `insn-attr.h' file.  For cases where an explicit set of values is specified
;; for an attribute, the following are defined:

;; * A `#define' is written for the symbol `HAVE_ATTR_NAME'.
;;
;; * An enumeral class is defined for `attr_NAME' with elements of the
;;   form `UPPER-NAME_UPPER-VALUE' where the attribute name and value are first
;;   converted to upper case.
;;
;; * A function `get_attr_NAME' is defined that is passed an insn and
;;   returns the attribute value for that insn.

;; For example, if the following is present in the `md' file:
;;
;; (define_attr "type" "branch,fp,load,store,arith" ...)
;;
;; the following lines will be written to the file `insn-attr.h'.
;;
;; #define HAVE_ATTR_type
;; enum attr_type {TYPE_BRANCH, TYPE_FP, TYPE_LOAD, TYPE_STORE, TYPE_ARITH};
;; extern enum attr_type get_attr_type ();

;; If the attribute takes numeric values, no `enum' type will be defined and
;; the function to obtain the attribute's value will return `int'.

(define_attr "length" "" (const_int 4))

;; Processor type -- this attribute must exactly match the processor_type
;; enumeration in frv-protos.h.

(define_attr "cpu" "generic,fr550,fr500,fr450,fr405,fr400,fr300,simple,tomcat"
  (const (symbol_ref "frv_cpu_type")))

;; Attribute is "yes" for branches and jumps that span too great a distance
;; to be implemented in the most natural way.  Such instructions will use
;; a call instruction in some way.

(define_attr "far_jump" "yes,no" (const_string "no"))

;; Instruction type
;; "unknown" must come last.
(define_attr "type"
  "int,sethi,setlo,mul,div,gload,gstore,fload,fstore,movfg,movgf,macc,scan,cut,branch,jump,jumpl,call,spr,trap,fnop,fsconv,fsadd,fscmp,fsmul,fsmadd,fsdiv,sqrt_single,fdconv,fdadd,fdcmp,fdmul,fdmadd,fddiv,sqrt_double,mnop,mlogic,maveh,msath,maddh,mqaddh,mpackh,munpackh,mdpackh,mbhconv,mrot,mshift,mexpdhw,mexpdhd,mwcut,mmulh,mmulxh,mmach,mmrdh,mqmulh,mqmulxh,mqmach,mcpx,mqcpx,mcut,mclracc,mclracca,mdunpackh,mbhconve,mrdacc,mwtacc,maddacc,mdaddacc,mabsh,mdrot,mcpl,mdcut,mqsath,mqlimh,mqshift,mset,ccr,multi,load_or_call,unknown"
  (const_string "unknown"))

(define_attr "acc_group" "none,even,odd"
  (symbol_ref "frv_acc_group (insn)"))

;; Scheduling and Packing Overview
;; -------------------------------
;;
;; FR-V instructions are divided into five groups: integer, floating-point,
;; media, branch and control.  Each group is associated with a separate set
;; of processing units, the number and behavior of which depend on the target
;; target processor.  Integer units have names like I0 and I1, floating-point
;; units have names like F0 and F1, and so on.
;;
;; Each member of the FR-V family has its own restrictions on which
;; instructions can issue to which units.  For example, some processors
;; allow loads to issue to I0 or I1 while others only allow them to issue
;; to I0.  As well as these processor-specific restrictions, there is a
;; general rule that an instruction can only issue to unit X + 1 if an
;; instruction in the same packet issued to unit X.
;;
;; Sometimes the only way to honor these restrictions is by adding nops
;; to a packet.  For example, on the fr550, media instructions that access
;; ACC4-7 can only issue to M1 or M3.  It is therefore only possible to
;; execute these instructions by packing them with something that issues
;; to M0.  When no useful M0 instruction exists, an "mnop" can be used
;; instead.
;;
;; Having decided which instructions should issue to which units, the packet
;; should be ordered according to the following template:
;;
;;     I0 F0/M0 I1 F1/M1 .... B0 B1 ...
;;
;; Note that VLIW packets execute strictly in parallel.  Every instruction
;; in the packet will stall until all input operands are ready.  These
;; operands are then read simultaneously before any registers are modified.
;; This means that it's OK to have write-after-read hazards between
;; instructions in the same packet, even if the write is listed earlier
;; than the read.
;;
;; Three gcc passes are involved in generating VLIW packets:
;;
;;    (1) The scheduler.  This pass uses the standard scheduling code and
;;	  behaves in much the same way as it would for a superscalar RISC
;;	  architecture.
;;
;;    (2) frv_reorg.  This pass inserts nops into packets in order to meet
;;	  the processor's issue requirements.  It also has code to optimize
;;	  the type of padding used to align labels.
;;
;;    (3) frv_pack_insns.  The final packing phase, which puts the
;;	  instructions into assembly language order according to the
;;	  "I0 F0/M0 ..." template above.
;;
;; In the ideal case, these three passes will agree on which instructions
;; should be packed together, but this won't always happen.  In particular:
;;
;;    (a) (2) might not pack predicated instructions in the same way as (1).
;;	  The scheduler tries to schedule predicated instructions for the
;;	  worst case, assuming the predicate is true.  However, if we have
;;	  something like a predicated load, it isn't always possible to
;;	  fill the load delay with useful instructions.  (2) should then
;;	  pack the user of the loaded value as aggressively as possible,
;;	  in order to optimize the case when the predicate is false.
;;	  See frv_pack_insn_p for more details.
;;
;;    (b) The final shorten_branches pass runs between (2) and (3).
;;	  Since (2) inserts nops, it is possible that some branches
;;	  that were thought to be in range during (2) turned out to
;;	  out-of-range in (3).
;;
;; All three passes use DFAs to model issue restrictions.  The main
;; question that the DFAs are supposed to answer is simply: can these
;; instructions be packed together?  The DFAs are not responsible for
;; assigning instructions to execution units; that's the job of
;; frv_sort_insn_group, see below for details.
;;
;; To get the best results, the DFAs should try to allow packets to
;; be built in every possible order.  This gives the scheduler more
;; flexibility, removing the need for things like multipass lookahead.
;; It also means we can take more advantage of inter-packet dependencies.
;;
;; For example, suppose we're compiling for the fr400 and we have:
;;
;;	addi	gr4,#1,gr5
;;	ldi	@(gr6,gr0),gr4
;;
;; We can pack these instructions together by assigning the load to I0 and
;; the addition to I1.  However, because of the anti dependence between the
;; two instructions, the scheduler must schedule the addition first.
;; We should generally get better schedules if the DFA allows both
;; (ldi, addi) and (addi, ldi), leaving the final packing pass to
;; reorder the packet where appropriate.
;;
;; Almost all integer instructions can issue to any unit in the range I0
;; to Ix, where the value of "x" depends on the type of instruction and
;; on the target processor.  The rules for other instruction groups are
;; usually similar.
;;
;; When the restrictions are as regular as this, we can get the desired
;; behavior by claiming the DFA unit associated with the highest unused
;; execution unit.  For example, if an instruction can issue to I0 or I1,
;; the DFA first tries to take the DFA unit associated with I1, and will
;; only take I0's unit if I1 isn't free.  (Note that, as mentioned above,
;; the DFA does not assign instructions to units.  An instruction that
;; claims DFA unit I1 will not necessarily issue to I1 in the final packet.)
;;
;; There are some cases, such as the fr550 media restriction mentioned
;; above, where the rule is not as simple as "any unit between 0 and X".
;; Even so, allocating higher units first brings us close to the ideal.
;;
;; Having divided instructions into packets, passes (2) and (3) must
;; assign instructions to specific execution units.  They do this using
;; the following algorithm:
;;
;;    1. Partition the instructions into groups (integer, float/media, etc.)
;;
;;    2. For each group of instructions:
;;
;;	 (a) Issue each instruction in the reset DFA state and use the
;;	     DFA cpu_unit_query interface to find out which unit it picks
;;	     first.
;;
;;	 (b) Sort the instructions into ascending order of picked units.
;;	     Instructions that pick I1 first come after those that pick
;;	     I0 first, and so on.  Let S be the sorted sequence and S[i]
;;	     be the ith element of it (counting from zero).
;;
;;	 (c) If this is the control or branch group, goto (i)
;;
;;	 (d) Find the largest L such that S[0]...S[L-1] can be issued
;;	     consecutively from the reset state and such that the DFA
;;	     claims unit X when S[X] is added.  Let D be the DFA state
;;	     after instructions S[0]...S[L-1] have been issued.
;;
;;	 (e) If L is the length of S, goto (i)
;;
;;	 (f) Let U be the number of units belonging to this group and #S be
;;	     the length of S.  Create a new sequence S' by concatenating
;;	     S[L]...S[#S-1] and (U - #S) nops.
;;
;;	 (g) For each permutation S'' of S', try issuing S'' from last to
;;	     first, starting with state D.  See if the DFA claims unit
;;	     X + L when each S''[X] is added.  If so, set S to the
;;	     concatenation of S[0]...S[L-1] and S'', then goto (i).
;;
;;	 (h) If (g) found no permutation, abort.
;;
;;	 (i) S is now the sorted sequence for this group, meaning that S[X]
;;	     issues to unit X.  Trim any unwanted nops from the end of S.
;;
;; The sequence calculated by (b) is trivially correct for control
;; instructions since they can't be packed.  It is also correct for branch
;; instructions due to their simple issue requirements.  For integer and
;; floating-point/media instructions, the sequence calculated by (b) is
;; often the correct answer; the rest of the algorithm is optimized for
;; the case in which it is correct.
;;
;; If there were no irregularities in the issue restrictions then step
;; (d) would not be needed.  It is mainly there to cope with the fr550
;; integer restrictions, where a store can issue to I1, but only if a store
;; also issues to I0.  (Note that if a packet has two stores, they will be
;; at the beginning of the sequence calculated by (b).)  It also copes
;; with fr400 M-2 instructions, which must issue to M0, and which cannot
;; be issued together with an mnop in M1.
;;
;; Step (g) is the main one for integer and float/media instructions.
;; The first permutation it tries is S' itself (because, as noted above,
;; the sequence calculated by (b) is often correct).  If S' doesn't work,
;; the implementation tries varying the beginning of the sequence first.
;; Thus the nops towards the end of the sequence will only move to lower
;; positions if absolutely necessary.
;;
;; The algorithm is theoretically exponential in the number of instructions
;; in a group, although it's only O(n log(n)) if the sequence calculated by
;; (b) is acceptable.  In practice, the algorithm completes quickly even
;; in the rare cases where (g) needs to try other permutations.
(define_automaton "integer, float_media, branch, control, idiv, div")

;; The main issue units.  Note that not all units are available on
;; all processors.
(define_query_cpu_unit "i0,i1,i2,i3" "integer")
(define_query_cpu_unit "f0,f1,f2,f3" "float_media")
(define_query_cpu_unit "b0,b1" "branch")
(define_query_cpu_unit "c" "control")

;; Division units.
(define_cpu_unit "idiv1,idiv2" "idiv")
(define_cpu_unit "div1,div2,root" "div")

;; Control instructions cannot be packed with others.
(define_reservation "control" "i0+i1+i2+i3+f0+f1+f2+f3+b0+b1")

;; Generic reservation for control insns
(define_insn_reservation "control" 1
  (eq_attr "type" "trap,spr,unknown,multi")
  "c + control")

;; Reservation for relaxable calls to gettlsoff.
(define_insn_reservation "load_or_call" 3
  (eq_attr "type" "load_or_call")
  "c + control")

;; ::::::::::::::::::::
;; ::
;; :: Generic/FR500 scheduler description
;; ::
;; ::::::::::::::::::::

;; Integer insns
;; Synthetic units used to describe issue restrictions.
(define_automaton "fr500_integer")
(define_cpu_unit "fr500_load0,fr500_load1,fr500_store0" "fr500_integer")
(exclusion_set "fr500_load0,fr500_load1" "fr500_store0")

(define_bypass 0 "fr500_i1_sethi" "fr500_i1_setlo")
(define_insn_reservation "fr500_i1_sethi" 1
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "sethi"))
  "i1|i0")

(define_insn_reservation "fr500_i1_setlo" 1
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "setlo"))
  "i1|i0")

(define_insn_reservation "fr500_i1_int" 1
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "int"))
  "i1|i0")

(define_insn_reservation "fr500_i1_mul" 3
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "mul"))
  "i1|i0")

(define_insn_reservation "fr500_i1_div" 19
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "div"))
  "(i1|i0),(idiv1*18|idiv2*18)")

(define_insn_reservation "fr500_i2" 4
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "gload,fload"))
  "(i1|i0) + (fr500_load0|fr500_load1)")

(define_insn_reservation "fr500_i3" 0
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "gstore,fstore"))
  "i0 + fr500_store0")

(define_insn_reservation "fr500_i4" 3
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "movgf,movfg"))
  "i0")

(define_insn_reservation "fr500_i5" 0
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "jumpl"))
  "i0")

;;
;; Branch-instructions
;;
(define_insn_reservation "fr500_branch" 0
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "jump,branch,ccr"))
  "b1|b0")

(define_insn_reservation "fr500_call" 0
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "call"))
  "b0")

;; Floating point insns.  The default latencies are for non-media
;; instructions; media instructions incur an extra cycle.

(define_bypass 4 "fr500_farith" "fr500_m1,fr500_m2,fr500_m3,
			         fr500_m4,fr500_m5,fr500_m6")
(define_insn_reservation "fr500_farith" 3
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "fnop,fsconv,fsadd,fsmul,fsmadd,fdconv,fdadd,fdmul,fdmadd"))
  "(f1|f0)")

(define_insn_reservation "fr500_fcmp" 4
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "fscmp,fdcmp"))
  "(f1|f0)")

(define_bypass 11 "fr500_fdiv" "fr500_m1,fr500_m2,fr500_m3,
			        fr500_m4,fr500_m5,fr500_m6")
(define_insn_reservation "fr500_fdiv" 10
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "fsdiv,fddiv"))
  "(f1|f0),(div1*9 | div2*9)")

(define_bypass 16 "fr500_froot" "fr500_m1,fr500_m2,fr500_m3,
				 fr500_m4,fr500_m5,fr500_m6")
(define_insn_reservation "fr500_froot" 15
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "sqrt_single,sqrt_double"))
  "(f1|f0) + root*15")

;; Media insns.  Conflict table is as follows:
;;
;;           M1  M2  M3  M4  M5  M6
;;        M1  -   -   -   -   -   -
;;        M2  -   -   -   -   X   X
;;        M3  -   -   -   -   X   X
;;        M4  -   -   -   -   -   X
;;        M5  -   X   X   -   X   X
;;        M6  -   X   X   X   X   X
;;
;; where X indicates an invalid combination.
;;
;; Target registers are as follows:
;;
;;	  M1 : FPRs
;;	  M2 : FPRs
;;	  M3 : ACCs
;;	  M4 : ACCs
;;	  M5 : FPRs
;;	  M6 : ACCs
;;
;; The default FPR latencies are for integer instructions.
;; Floating-point instructions need one cycle more and media
;; instructions need one cycle less.
(define_automaton "fr500_media")
(define_cpu_unit "fr500_m2_0,fr500_m2_1" "fr500_media")
(define_cpu_unit "fr500_m3_0,fr500_m3_1" "fr500_media")
(define_cpu_unit "fr500_m4_0,fr500_m4_1" "fr500_media")
(define_cpu_unit "fr500_m5" "fr500_media")
(define_cpu_unit "fr500_m6" "fr500_media")

(exclusion_set "fr500_m5,fr500_m6" "fr500_m2_0,fr500_m2_1,
				    fr500_m3_0,fr500_m3_1")
(exclusion_set "fr500_m6" "fr500_m4_0,fr500_m4_1,fr500_m5")

(define_bypass 2 "fr500_m1" "fr500_m1,fr500_m2,fr500_m3,
			     fr500_m4,fr500_m5,fr500_m6")
(define_bypass 4 "fr500_m1" "fr500_farith,fr500_fcmp,fr500_fdiv,fr500_froot")
(define_insn_reservation "fr500_m1" 3
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "mnop,mlogic,maveh,msath,maddh,mqaddh"))
  "(f1|f0)")

(define_bypass 2 "fr500_m2" "fr500_m1,fr500_m2,fr500_m3,
			     fr500_m4,fr500_m5,fr500_m6")
(define_bypass 4 "fr500_m2" "fr500_farith,fr500_fcmp,fr500_fdiv,fr500_froot")
(define_insn_reservation "fr500_m2" 3
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "mrdacc,mpackh,munpackh,mbhconv,mrot,mshift,mexpdhw,mexpdhd,mwcut,mcut,mdunpackh,mbhconve"))
  "(f1|f0) + (fr500_m2_0|fr500_m2_1)")

(define_bypass 1 "fr500_m3" "fr500_m4")
(define_insn_reservation "fr500_m3" 2
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "mclracc,mwtacc"))
  "(f1|f0) + (fr500_m3_0|fr500_m3_1)")

(define_bypass 1 "fr500_m4" "fr500_m4")
(define_insn_reservation "fr500_m4" 2
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "mmulh,mmulxh,mmach,mmrdh,mqmulh,mqmulxh,mqmach,mcpx,mqcpx"))
  "(f1|f0) + (fr500_m4_0|fr500_m4_1)")

(define_bypass 2 "fr500_m5" "fr500_m1,fr500_m2,fr500_m3,
			     fr500_m4,fr500_m5,fr500_m6")
(define_bypass 4 "fr500_m5" "fr500_farith,fr500_fcmp,fr500_fdiv,fr500_froot")
(define_insn_reservation "fr500_m5" 3
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "mdpackh"))
  "(f1|f0) + fr500_m5")

(define_bypass 1 "fr500_m6" "fr500_m4")
(define_insn_reservation "fr500_m6" 2
  (and (eq_attr "cpu" "generic,fr500,tomcat")
       (eq_attr "type" "mclracca"))
  "(f1|f0) + fr500_m6")

;; ::::::::::::::::::::
;; ::
;; :: FR400 scheduler description
;; ::
;; ::::::::::::::::::::

;; Category 2 media instructions use both media units, but can be packed
;; with non-media instructions.  Use fr400_m1unit to claim the M1 unit
;; without claiming a slot.

;; Name		Class	Units	Latency
;; ====	        =====	=====	=======
;; int		I1	I0/I1	1
;; sethi	I1	I0/I1	0       -- does not interfere with setlo
;; setlo	I1	I0/I1	1
;; mul		I1	I0	3  (*)
;; div		I1	I0	20 (*)
;; gload	I2	I0	4  (*)
;; fload	I2	I0	4       -- only 3 if read by a media insn
;; gstore	I3	I0	0       -- provides no result
;; fstore	I3	I0	0       -- provides no result
;; movfg	I4	I0	3  (*)
;; movgf	I4	I0	3  (*)
;; jumpl	I5	I0	0       -- provides no result
;;
;; (*) The results of these instructions can be read one cycle earlier
;; than indicated.  The penalty given is for instructions with write-after-
;; write dependencies.

;; The FR400 can only do loads and stores in I0, so we there's no danger
;; of memory unit collision in the same packet.  There's only one divide
;; unit too.

(define_automaton "fr400_integer")
(define_cpu_unit "fr400_mul" "fr400_integer")

(define_insn_reservation "fr400_i1_int" 1
  (and (eq_attr "cpu" "fr400,fr405,fr450")
       (eq_attr "type" "int"))
  "i1|i0")

(define_bypass 0 "fr400_i1_sethi" "fr400_i1_setlo")
(define_insn_reservation "fr400_i1_sethi" 1
  (and (eq_attr "cpu" "fr400,fr405,fr450")
       (eq_attr "type" "sethi"))
  "i1|i0")

(define_insn_reservation "fr400_i1_setlo" 1
  (and (eq_attr "cpu" "fr400,fr405,fr450")
       (eq_attr "type" "setlo"))
  "i1|i0")

;; 3 is the worst case (write-after-write hazard).
(define_insn_reservation "fr400_i1_mul" 3
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "mul"))
  "i0 + fr400_mul")

(define_insn_reservation "fr450_i1_mul" 2
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "mul"))
  "i0 + fr400_mul")

(define_bypass 1 "fr400_i1_macc" "fr400_i1_macc")
(define_insn_reservation "fr400_i1_macc" 2
  (and (eq_attr "cpu" "fr405,fr450")
       (eq_attr "type" "macc"))
  "(i0|i1) + fr400_mul")

(define_insn_reservation "fr400_i1_scan" 1
  (and (eq_attr "cpu" "fr400,fr405,fr450")
       (eq_attr "type" "scan"))
  "i0")

(define_insn_reservation "fr400_i1_cut" 2
  (and (eq_attr "cpu" "fr405,fr450")
       (eq_attr "type" "cut"))
  "i0 + fr400_mul")

;; 20 is for a write-after-write hazard.
(define_insn_reservation "fr400_i1_div" 20
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "div"))
  "i0 + idiv1*19")

(define_insn_reservation "fr450_i1_div" 19
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "div"))
  "i0 + idiv1*19")

;; 4 is for a write-after-write hazard.
(define_insn_reservation "fr400_i2" 4
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "gload,fload"))
  "i0")

(define_insn_reservation "fr450_i2_gload" 3
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "gload"))
  "i0")

;; 4 is for a write-after-write hazard.
(define_insn_reservation "fr450_i2_fload" 4
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "fload"))
  "i0")

(define_insn_reservation "fr400_i3" 0
  (and (eq_attr "cpu" "fr400,fr405,fr450")
       (eq_attr "type" "gstore,fstore"))
  "i0")

;; 3 is for a write-after-write hazard.
(define_insn_reservation "fr400_i4" 3
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "movfg,movgf"))
  "i0")

(define_insn_reservation "fr450_i4_movfg" 2
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "movfg"))
  "i0")

;; 3 is for a write-after-write hazard.
(define_insn_reservation "fr450_i4_movgf" 3
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "movgf"))
  "i0")

(define_insn_reservation "fr400_i5" 0
  (and (eq_attr "cpu" "fr400,fr405,fr450")
       (eq_attr "type" "jumpl"))
  "i0")

;; The bypass between FPR loads and media instructions, described above.

(define_bypass 3
  "fr400_i2"
  "fr400_m1_1,fr400_m1_2,\
   fr400_m2_1,fr400_m2_2,\
   fr400_m3_1,fr400_m3_2,\
   fr400_m4_1,fr400_m4_2,\
   fr400_m5")

;; The branch instructions all use the B unit and produce no result.

(define_insn_reservation "fr400_b" 0
  (and (eq_attr "cpu" "fr400,fr405,fr450")
       (eq_attr "type" "jump,branch,ccr,call"))
  "b0")

;; FP->FP moves are marked as "fsconv" instructions in the define_insns
;; below, but are implemented on the FR400 using "mlogic" instructions.
;; It's easier to class "fsconv" as a "m1:1" instruction than provide
;; separate define_insns for the FR400.

;; M1 instructions store their results in FPRs.  Any instruction can read
;; the result in the following cycle, so no penalty occurs.

(define_automaton "fr400_media")
(define_cpu_unit "fr400_m1a,fr400_m1b,fr400_m2a" "fr400_media")
(exclusion_set "fr400_m1a,fr400_m1b" "fr400_m2a")

(define_reservation "fr400_m1" "(f1|f0) + (fr400_m1a|fr400_m1b)")
(define_reservation "fr400_m2" "f0 + fr400_m2a")

(define_insn_reservation "fr400_m1_1" 1
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "fsconv,mnop,mlogic,maveh,msath,maddh,mabsh,mset"))
  "fr400_m1")

(define_insn_reservation "fr400_m1_2" 1
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "mqaddh,mqsath,mqlimh,mqshift"))
  "fr400_m2")

;; M2 instructions store their results in accumulators, which are read
;; by M2 or M4 media commands.  M2 instructions can read the results in
;; the following cycle, but M4 instructions must wait a cycle more.

(define_bypass 1
  "fr400_m2_1,fr400_m2_2"
  "fr400_m2_1,fr400_m2_2")

(define_insn_reservation "fr400_m2_1" 2
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "mmulh,mmulxh,mmach,mmrdh,mcpx,maddacc"))
  "fr400_m1")

(define_insn_reservation "fr400_m2_2" 2
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "mqmulh,mqmulxh,mqmach,mqcpx,mdaddacc"))
  "fr400_m2")

;; For our purposes, there seems to be little real difference between
;; M1 and M3 instructions.  Keep them separate anyway in case the distinction
;; is needed later.

(define_insn_reservation "fr400_m3_1" 1
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "mpackh,mrot,mshift,mexpdhw"))
  "fr400_m1")

(define_insn_reservation "fr400_m3_2" 1
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "munpackh,mdpackh,mbhconv,mexpdhd,mwcut,mdrot,mcpl"))
  "fr400_m2")

;; M4 instructions write to accumulators or FPRs.  MOVFG and STF
;; instructions can read an FPR result in the following cycle, but
;; M-unit instructions must wait a cycle more for either kind of result.

(define_bypass 1 "fr400_m4_1,fr400_m4_2" "fr400_i3,fr400_i4")

(define_insn_reservation "fr400_m4_1" 2
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "mrdacc,mcut,mclracc"))
  "fr400_m1")

(define_insn_reservation "fr400_m4_2" 2
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "mclracca,mdcut"))
  "fr400_m2")

;; M5 instructions always incur a 1-cycle penalty.

(define_insn_reservation "fr400_m5" 2
  (and (eq_attr "cpu" "fr400,fr405")
       (eq_attr "type" "mwtacc"))
  "fr400_m2")

;; ::::::::::::::::::::
;; ::
;; :: FR450 media scheduler description
;; ::
;; ::::::::::::::::::::

;; The FR451 media restrictions are similar to the FR400's, but not as
;; strict and not as regular.  There are 6 categories with the following
;; restrictions:
;;
;;		          M1
;;	      M-1  M-2  M-3  M-4  M-5  M-6
;;	M-1:         x         x         x
;;	M-2:    x    x    x    x    x    x
;;  M0	M-3:         x         x         x
;;	M-4:    x    x    x    x
;;	M-5:         x         x         x
;;	M-6:    x    x    x    x    x    x
;;
;; where "x" indicates a conflict.
;;
;; There is no difference between M-1 and M-3 as far as issue
;; restrictions are concerned, so they are combined as "m13".

;; Units for odd-numbered categories.  There can be two of these
;; in a packet.
(define_cpu_unit "fr450_m13a,fr450_m13b" "float_media")
(define_cpu_unit "fr450_m5a,fr450_m5b" "float_media")

;; Units for even-numbered categories.  There can only be one per packet.
(define_cpu_unit "fr450_m2a,fr450_m4a,fr450_m6a" "float_media")

;; Enforce the restriction matrix above.
(exclusion_set "fr450_m2a,fr450_m4a,fr450_m6a" "fr450_m13a,fr450_m13b")
(exclusion_set "fr450_m2a,fr450_m6a" "fr450_m5a,fr450_m5b")
(exclusion_set "fr450_m4a,fr450_m6a" "fr450_m2a")

(define_reservation "fr450_m13" "(f1|f0) + (fr450_m13a|fr450_m13b)")
(define_reservation "fr450_m2" "f0 + fr450_m2a")
(define_reservation "fr450_m4" "f0 + fr450_m4a")
(define_reservation "fr450_m5" "(f1|f0) + (fr450_m5a|fr450_m5b)")
(define_reservation "fr450_m6" "(f0|f1) + fr450_m6a")

;; MD-1, MD-3 and MD-8 instructions, which are the same as far
;; as scheduling is concerned.  The inputs and outputs are FPRs.
;; Instructions that have 32-bit inputs and outputs belong to M-1 while
;; the rest belong to M-2.
;;
;; ??? Arithmetic shifts (MD-6) have an extra cycle latency, but we don't
;; make the distinction between them and logical shifts.
(define_insn_reservation "fr450_md138_1" 1
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "fsconv,mnop,mlogic,maveh,msath,maddh,mabsh,mset,
			mrot,mshift,mexpdhw,mpackh"))
  "fr450_m13")

(define_insn_reservation "fr450_md138_2" 1
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "mqaddh,mqsath,mqlimh,
			mdrot,mwcut,mqshift,mexpdhd,
			munpackh,mdpackh,mbhconv,mcpl"))
  "fr450_m2")

;; MD-2 instructions.  These take FPR or ACC inputs and produce an ACC output.
;; Instructions that write to double ACCs belong to M-3 while those that write
;; to quad ACCs belong to M-4.
(define_insn_reservation "fr450_md2_3" 2
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "mmulh,mmach,mcpx,mmulxh,mmrdh,maddacc"))
  "fr450_m13")

(define_insn_reservation "fr450_md2_4" 2
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "mqmulh,mqmach,mqcpx,mqmulxh,mdaddacc"))
  "fr450_m4")

;; Another MD-2 instruction can use the result on the following cycle.
(define_bypass 1 "fr450_md2_3,fr450_md2_4" "fr450_md2_3,fr450_md2_4")

;; MD-4 instructions that write to ACCs.
(define_insn_reservation "fr450_md4_3" 2
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "mclracc"))
  "fr450_m13")

(define_insn_reservation "fr450_md4_4" 3
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "mclracca"))
  "fr450_m4")

;; MD-4 instructions that write to FPRs.
(define_insn_reservation "fr450_md4_1" 2
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "mcut"))
  "fr450_m13")

(define_insn_reservation "fr450_md4_5" 2
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "mrdacc"))
  "fr450_m5")

(define_insn_reservation "fr450_md4_6" 2
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "mdcut"))
  "fr450_m6")

;; Integer instructions can read the FPR result of an MD-4 instruction on
;; the following cycle.
(define_bypass 1 "fr450_md4_1,fr450_md4_5,fr450_md4_6"
		 "fr400_i3,fr450_i4_movfg")

;; MD-5 instructions, which belong to M-3.  They take FPR inputs and
;; write to ACCs.
(define_insn_reservation "fr450_md5_3" 2
  (and (eq_attr "cpu" "fr450")
       (eq_attr "type" "mwtacc"))
  "fr450_m13")

;; ::::::::::::::::::::
;; ::
;; :: FR550 scheduler description
;; ::
;; ::::::::::::::::::::

;; Prevent loads and stores from being issued in the same packet.
;; These units must go into the generic "integer" reservation because
;; of the constraints on fr550_store0 and fr550_store1.
(define_cpu_unit "fr550_load0,fr550_load1" "integer")
(define_cpu_unit "fr550_store0,fr550_store1" "integer")
(exclusion_set "fr550_load0,fr550_load1" "fr550_store0,fr550_store1")

;; A store can only issue to I1 if one has also been issued to I0.
(presence_set "fr550_store1" "fr550_store0")

(define_bypass 0 "fr550_sethi" "fr550_setlo")
(define_insn_reservation "fr550_sethi" 1
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "sethi"))
  "i3|i2|i1|i0")

(define_insn_reservation "fr550_setlo" 1
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "setlo"))
  "i3|i2|i1|i0")

(define_insn_reservation "fr550_int" 1
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "int"))
  "i3|i2|i1|i0")

(define_insn_reservation "fr550_mul" 2
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "mul"))
  "i1|i0")

(define_insn_reservation "fr550_div" 19
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "div"))
  "(i1|i0),(idiv1*18 | idiv2*18)")

(define_insn_reservation "fr550_load" 3
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "gload,fload"))
  "(i1|i0)+(fr550_load0|fr550_load1)")

;; We can only issue a store to I1 if one was also issued to I0.
;; This means that, as far as frv_reorder_packet is concerned,
;; the instruction has the same priority as an I0-only instruction.
(define_insn_reservation "fr550_store" 1
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "gstore,fstore"))
  "(i0+fr550_store0)|(i1+fr550_store1)")

(define_insn_reservation "fr550_transfer" 2
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "movgf,movfg"))
  "i0")

(define_insn_reservation "fr550_jumpl" 0
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "jumpl"))
  "i0")

(define_cpu_unit "fr550_ccr0,fr550_ccr1" "float_media")

(define_insn_reservation "fr550_branch" 0
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "jump,branch"))
  "b1|b0")

(define_insn_reservation "fr550_ccr" 0
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "ccr"))
  "(b1|b0) + (fr550_ccr1|fr550_ccr0)")

(define_insn_reservation "fr550_call" 0
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "call"))
  "b0")

(define_automaton "fr550_float_media")
(define_cpu_unit "fr550_add0,fr550_add1" "fr550_float_media")

;; There are three possible combinations of floating-point/media instructions:
;;
;;    - one media and one float
;;    - up to four float, no media
;;    - up to four media, no float
(define_cpu_unit "fr550_f0,fr550_f1,fr550_f2,fr550_f3" "fr550_float_media")
(define_cpu_unit "fr550_m0,fr550_m1,fr550_m2,fr550_m3" "fr550_float_media")
(exclusion_set "fr550_f1,fr550_f2,fr550_f3" "fr550_m1,fr550_m2,fr550_m3")

(define_reservation "fr550_float" "fr550_f0|fr550_f1|fr550_f2|fr550_f3")
(define_reservation "fr550_media" "fr550_m0|fr550_m1|fr550_m2|fr550_m3")

(define_insn_reservation "fr550_f1" 0
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "fnop"))
  "(f3|f2|f1|f0) + fr550_float")

(define_insn_reservation "fr550_f2" 3
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "fsconv,fsadd,fscmp"))
  "(f3|f2|f1|f0) + (fr550_add0|fr550_add1) + fr550_float")

(define_insn_reservation "fr550_f3_mul" 3
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "fsmul"))
  "(f1|f0) + fr550_float")

(define_insn_reservation "fr550_f3_div" 10
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "fsdiv"))
  "(f1|f0) + fr550_float")

(define_insn_reservation "fr550_f3_sqrt" 15
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "sqrt_single"))
  "(f1|f0) + fr550_float")

;; Synthetic units for enforcing media issue restrictions.  Certain types
;; of insn in M2 conflict with certain types in M0:
;;
;;			     M2
;;               MNOP   MALU   MSFT   MMAC   MSET
;;         MNOP     -      -      x      -      -
;;         MALU     -      x      x      -      -
;;   M0    MSFT     -      -      x      -      x
;;         MMAC     -      -      x      x      -
;;         MSET     -      -      x      -      -
;;
;; where "x" indicates a conflict.  The same restrictions apply to
;; M3 and M1.
;;
;; In addition -- and this is the awkward bit! -- instructions that
;; access ACC0-3 can only issue to M0 or M2.  Those that access ACC4-7
;; can only issue to M1 or M3.  We refer to such instructions as "even"
;; and "odd" respectively.
(define_cpu_unit "fr550_malu0,fr550_malu1" "float_media")
(define_cpu_unit "fr550_malu2,fr550_malu3" "float_media")
(define_cpu_unit "fr550_msft0,fr550_msft1" "float_media")
(define_cpu_unit "fr550_mmac0,fr550_mmac1" "float_media")
(define_cpu_unit "fr550_mmac2,fr550_mmac3" "float_media")
(define_cpu_unit "fr550_mset0,fr550_mset1" "float_media")
(define_cpu_unit "fr550_mset2,fr550_mset3" "float_media")

(exclusion_set "fr550_malu0" "fr550_malu2")
(exclusion_set "fr550_malu1" "fr550_malu3")

(exclusion_set "fr550_msft0" "fr550_mset2")
(exclusion_set "fr550_msft1" "fr550_mset3")

(exclusion_set "fr550_mmac0" "fr550_mmac2")
(exclusion_set "fr550_mmac1" "fr550_mmac3")

;; If an MSFT or MMAC instruction issues to a unit other than M0, we may
;; need to insert some nops.  In the worst case, the packet will end up
;; having 4 integer instructions and 4 media instructions, leaving no
;; room for any branch instructions that the DFA might have accepted.
;;
;; This doesn't matter for JUMP_INSNs and CALL_INSNs because they are
;; always the last instructions to be passed to the DFA, and could be
;; pushed out to a separate packet once the nops have been added.
;; However, it does cause problems for ccr instructions since they
;; can occur anywhere in the unordered packet.
(exclusion_set "fr550_msft1,fr550_mmac1,fr550_mmac2,fr550_mmac3"
	       "fr550_ccr0,fr550_ccr1")

(define_reservation "fr550_malu"
  "(f3 + fr550_malu3) | (f2 + fr550_malu2)
   | (f1 + fr550_malu1) | (f0 + fr550_malu0)")

(define_reservation "fr550_msft_even"
  "f0 + fr550_msft0")

(define_reservation "fr550_msft_odd"
  "f1 + fr550_msft1")

(define_reservation "fr550_msft_either"
  "(f1 + fr550_msft1) | (f0 + fr550_msft0)")

(define_reservation "fr550_mmac_even"
  "(f2 + fr550_mmac2) | (f0 + fr550_mmac0)")

(define_reservation "fr550_mmac_odd"
  "(f3 + fr550_mmac3) | (f1 + fr550_mmac1)")

(define_reservation "fr550_mset"
  "(f3 + fr550_mset3) | (f2 + fr550_mset2)
    | (f1 + fr550_mset1) | (f0 + fr550_mset0)")

(define_insn_reservation "fr550_mnop" 0
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "mnop"))
  "fr550_media + (f3|f2|f1|f0)")

(define_insn_reservation "fr550_malu" 2
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "mlogic,maveh,msath,mabsh,maddh,mqaddh,mqsath"))
  "fr550_media + fr550_malu")

;; These insns only operate on FPRs and so don't need to be classified
;; as even/odd.
(define_insn_reservation "fr550_msft_1_either" 2
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "mrot,mwcut,mshift,mexpdhw,mexpdhd,mpackh,
			munpackh,mdpackh,mbhconv,mdrot,mcpl"))
  "fr550_media + fr550_msft_either")

;; These insns read from ACC0-3.
(define_insn_reservation "fr550_msft_1_even" 2
  (and (eq_attr "cpu" "fr550")
       (and (eq_attr "type" "mcut,mrdacc,mdcut")
	    (eq_attr "acc_group" "even")))
  "fr550_media + fr550_msft_even")

;; These insns read from ACC4-7.
(define_insn_reservation "fr550_msft_1_odd" 2
  (and (eq_attr "cpu" "fr550")
       (and (eq_attr "type" "mcut,mrdacc,mdcut")
	    (eq_attr "acc_group" "odd")))
  "fr550_media + fr550_msft_odd")

;; MCLRACC with A=1 can issue to either M0 or M1.
(define_insn_reservation "fr550_msft_2_either" 2
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "mclracca"))
  "fr550_media + fr550_msft_either")

;; These insns write to ACC0-3.
(define_insn_reservation "fr550_msft_2_even" 2
  (and (eq_attr "cpu" "fr550")
       (and (eq_attr "type" "mclracc,mwtacc")
	    (eq_attr "acc_group" "even")))
  "fr550_media + fr550_msft_even")

;; These insns write to ACC4-7.
(define_insn_reservation "fr550_msft_2_odd" 2
  (and (eq_attr "cpu" "fr550")
       (and (eq_attr "type" "mclracc,mwtacc")
	    (eq_attr "acc_group" "odd")))
  "fr550_media + fr550_msft_odd")

;; These insns read from and write to ACC0-3.
(define_insn_reservation "fr550_mmac_even" 2
  (and (eq_attr "cpu" "fr550")
       (and (eq_attr "type" "mmulh,mmulxh,mmach,mmrdh,mqmulh,mqmulxh,mqmach,
			     maddacc,mdaddacc,mcpx,mqcpx")
	    (eq_attr "acc_group" "even")))
  "fr550_media + fr550_mmac_even")

;; These insns read from and write to ACC4-7.
(define_insn_reservation "fr550_mmac_odd" 2
  (and (eq_attr "cpu" "fr550")
       (and (eq_attr "type" "mmulh,mmulxh,mmach,mmrdh,mqmulh,mqmulxh,mqmach,
			     maddacc,mdaddacc,mcpx,mqcpx")
	    (eq_attr "acc_group" "odd")))
  "fr550_media + fr550_mmac_odd")

(define_insn_reservation "fr550_mset" 1
  (and (eq_attr "cpu" "fr550")
       (eq_attr "type" "mset"))
  "fr550_media + fr550_mset")

;; ::::::::::::::::::::
;; ::
;; :: Simple/FR300 scheduler description
;; ::
;; ::::::::::::::::::::

;; Fr300 or simple processor.  To describe it as 1 insn issue
;; processor, we use control unit.

(define_insn_reservation "fr300_lat1" 1
  (and (eq_attr "cpu" "fr300,simple")
       (eq_attr "type" "!gload,fload,movfg,movgf"))
  "c + control")

(define_insn_reservation "fr300_lat2" 2
  (and (eq_attr "cpu" "fr300,simple")
       (eq_attr "type" "gload,fload,movfg,movgf"))
  "c + control")


;; ::::::::::::::::::::
;; ::
;; :: Delay Slots
;; ::
;; ::::::::::::::::::::

;; The insn attribute mechanism can be used to specify the requirements for
;; delay slots, if any, on a target machine.  An instruction is said to require
;; a "delay slot" if some instructions that are physically after the
;; instruction are executed as if they were located before it.  Classic
;; examples are branch and call instructions, which often execute the following
;; instruction before the branch or call is performed.

;; On some machines, conditional branch instructions can optionally "annul"
;; instructions in the delay slot.  This means that the instruction will not be
;; executed for certain branch outcomes.  Both instructions that annul if the
;; branch is true and instructions that annul if the branch is false are
;; supported.

;; Delay slot scheduling differs from instruction scheduling in that
;; determining whether an instruction needs a delay slot is dependent only
;; on the type of instruction being generated, not on data flow between the
;; instructions.  See the next section for a discussion of data-dependent
;; instruction scheduling.

;; The requirement of an insn needing one or more delay slots is indicated via
;; the `define_delay' expression.  It has the following form:
;;
;; (define_delay TEST
;;   [DELAY-1 ANNUL-TRUE-1 ANNUL-FALSE-1
;;    DELAY-2 ANNUL-TRUE-2 ANNUL-FALSE-2
;;    ...])

;; TEST is an attribute test that indicates whether this `define_delay' applies
;; to a particular insn.  If so, the number of required delay slots is
;; determined by the length of the vector specified as the second argument.  An
;; insn placed in delay slot N must satisfy attribute test DELAY-N.
;; ANNUL-TRUE-N is an attribute test that specifies which insns may be annulled
;; if the branch is true.  Similarly, ANNUL-FALSE-N specifies which insns in
;; the delay slot may be annulled if the branch is false.  If annulling is not
;; supported for that delay slot, `(nil)' should be coded.

;; For example, in the common case where branch and call insns require a single
;; delay slot, which may contain any insn other than a branch or call, the
;; following would be placed in the `md' file:

;; (define_delay (eq_attr "type" "branch,call")
;;		 [(eq_attr "type" "!branch,call") (nil) (nil)])

;; Multiple `define_delay' expressions may be specified.  In this case, each
;; such expression specifies different delay slot requirements and there must
;; be no insn for which tests in two `define_delay' expressions are both true.

;; For example, if we have a machine that requires one delay slot for branches
;; but two for calls, no delay slot can contain a branch or call insn, and any
;; valid insn in the delay slot for the branch can be annulled if the branch is
;; true, we might represent this as follows:

;; (define_delay (eq_attr "type" "branch")
;;   [(eq_attr "type" "!branch,call")
;;    (eq_attr "type" "!branch,call")
;;    (nil)])
;;
;; (define_delay (eq_attr "type" "call")
;;   [(eq_attr "type" "!branch,call") (nil) (nil)
;;    (eq_attr "type" "!branch,call") (nil) (nil)])

;; Note - it is the backend's responsibility to fill any unfilled delay slots
;; at assembler generation time.  This is usually done by adding a special print
;; operand to the delayed instruction, and then in the PRINT_OPERAND function
;; calling dbr_sequence_length() to determine how many delay slots were filled.
;; For example:
;;
;; --------------<machine>.md-----------------
;; (define_insn "call"
;;  [(call (match_operand 0 "memory_operand" "m")
;;         (match_operand 1 "" ""))]
;;   ""
;;   "call_delayed %0,%1,%2%#"
;;  [(set_attr "length" "4")
;;   (set_attr "type" "call")])
;;
;; -------------<machine>.h-------------------
;; #define PRINT_OPERAND_PUNCT_VALID_P(CODE) (CODE == '#')
;;
;;  ------------<machine>.c------------------
;; void
;; machine_print_operand (file, x, code)
;;     FILE * file;
;;     rtx    x;
;;     int    code;
;; {
;;   switch (code)
;;   {
;;   case '#':
;;     if (dbr_sequence_length () == 0)
;;       fputs ("\n\tnop", file);
;;     return;

;; ::::::::::::::::::::
;; ::
;; :: Notes on Patterns
;; ::
;; ::::::::::::::::::::

;; If you need to construct a sequence of assembler instructions in order
;; to implement a pattern be sure to escape any backslashes and double quotes
;; that you use, e.g.:
;;
;; (define_insn "an example"
;;   [(some rtl)]
;;   ""
;;   "*
;;    { static char buffer [100];
;;      sprintf (buffer, \"insn \\t %d\", REGNO (operands[1]));
;;      return buffer;
;;    }"
;; )
;;
;; Also if there is more than one instruction, they can be separated by \\;
;; which is a space saving synonym for \\n\\t:
;;
;; (define_insn "another example"
;;   [(some rtl)]
;;   ""
;;   "*
;;    { static char buffer [100];
;;      sprintf (buffer, \"insn1 \\t %d\\;insn2 \\t %%1\",
;;        REGNO (operands[1]));
;;      return buffer;
;;    }"
;; )
;;

(include "predicates.md")

;; ::::::::::::::::::::
;; ::
;; :: Moves
;; ::
;; ::::::::::::::::::::

;; Wrap moves in define_expand to prevent memory->memory moves from being
;; generated at the RTL level, which generates better code for most machines
;; which can't do mem->mem moves.

;; If operand 0 is a `subreg' with mode M of a register whose own mode is wider
;; than M, the effect of this instruction is to store the specified value in
;; the part of the register that corresponds to mode M.  The effect on the rest
;; of the register is undefined.

;; This class of patterns is special in several ways.  First of all, each of
;; these names *must* be defined, because there is no other way to copy a datum
;; from one place to another.

;; Second, these patterns are not used solely in the RTL generation pass.  Even
;; the reload pass can generate move insns to copy values from stack slots into
;; temporary registers.  When it does so, one of the operands is a hard
;; register and the other is an operand that can need to be reloaded into a
;; register.

;; Therefore, when given such a pair of operands, the pattern must
;; generate RTL which needs no reloading and needs no temporary
;; registers--no registers other than the operands.  For example, if
;; you support the pattern with a `define_expand', then in such a
;; case the `define_expand' mustn't call `force_reg' or any other such
;; function which might generate new pseudo registers.

;; This requirement exists even for subword modes on a RISC machine
;; where fetching those modes from memory normally requires several
;; insns and some temporary registers.  Look in `spur.md' to see how
;; the requirement can be satisfied.

;; During reload a memory reference with an invalid address may be passed as an
;; operand.  Such an address will be replaced with a valid address later in the
;; reload pass.  In this case, nothing may be done with the address except to
;; use it as it stands.  If it is copied, it will not be replaced with a valid
;; address.  No attempt should be made to make such an address into a valid
;; address and no routine (such as `change_address') that will do so may be
;; called.  Note that `general_operand' will fail when applied to such an
;; address.
;;
;; The global variable `reload_in_progress' (which must be explicitly declared
;; if required) can be used to determine whether such special handling is
;; required.
;;
;; The variety of operands that have reloads depends on the rest of
;; the machine description, but typically on a RISC machine these can
;; only be pseudo registers that did not get hard registers, while on
;; other machines explicit memory references will get optional
;; reloads.
;;
;; If a scratch register is required to move an object to or from memory, it
;; can be allocated using `gen_reg_rtx' prior to reload.  But this is
;; impossible during and after reload.  If there are cases needing scratch
;; registers after reload, you must define `SECONDARY_INPUT_RELOAD_CLASS' and
;; perhaps also `SECONDARY_OUTPUT_RELOAD_CLASS' to detect them, and provide
;; patterns `reload_inM' or `reload_outM' to handle them.

;; The constraints on a `moveM' must permit moving any hard register to any
;; other hard register provided that `HARD_REGNO_MODE_OK' permits mode M in
;; both registers and `REGISTER_MOVE_COST' applied to their classes returns a
;; value of 2.

;; It is obligatory to support floating point `moveM' instructions
;; into and out of any registers that can hold fixed point values,
;; because unions and structures (which have modes `SImode' or
;; `DImode') can be in those registers and they may have floating
;; point members.

;; There may also be a need to support fixed point `moveM' instructions in and
;; out of floating point registers.  Unfortunately, I have forgotten why this
;; was so, and I don't know whether it is still true.  If `HARD_REGNO_MODE_OK'
;; rejects fixed point values in floating point registers, then the constraints
;; of the fixed point `moveM' instructions must be designed to avoid ever
;; trying to reload into a floating point register.

(define_expand "movqi"
  [(set (match_operand:QI 0 "general_operand" "")
	(match_operand:QI 1 "general_operand" ""))]
  ""
  "{ frv_emit_move (QImode, operands[0], operands[1]); DONE; }")

(define_insn "*movqi_load"
  [(set (match_operand:QI 0 "register_operand" "=d,f")
	(match_operand:QI 1 "frv_load_operand" "m,m"))]
  ""
  "* return output_move_single (operands, insn);"
  [(set_attr "length" "4")
   (set_attr "type" "gload,fload")])

(define_insn "*movqi_internal"
  [(set (match_operand:QI 0 "move_destination_operand" "=d,d,m,m,?f,?f,?d,?m,f,d,f")
	(match_operand:QI 1 "move_source_operand"       "L,d,d,O, d, f, f, f,GO,!m,!m"))]
  "register_operand(operands[0], QImode) || reg_or_0_operand (operands[1], QImode)"
  "* return output_move_single (operands, insn);"
  [(set_attr "length" "4")
   (set_attr "type" "int,int,gstore,gstore,movgf,fsconv,movfg,fstore,movgf,gload,fload")])

(define_expand "movhi"
  [(set (match_operand:HI 0 "general_operand" "")
	(match_operand:HI 1 "general_operand" ""))]
  ""
  "{ frv_emit_move (HImode, operands[0], operands[1]); DONE; }")

(define_insn "*movhi_load"
  [(set (match_operand:HI 0 "register_operand" "=d,f")
	(match_operand:HI 1 "frv_load_operand" "m,m"))]
  ""
  "* return output_move_single (operands, insn);"
  [(set_attr "length" "4")
   (set_attr "type" "gload,fload")])

(define_insn "*movhi_internal"
  [(set (match_operand:HI 0 "move_destination_operand" "=d,d,d,m,m,?f,?f,?d,?m,f,d,f")
	(match_operand:HI 1 "move_source_operand"       "L,n,d,d,O, d, f, f, f,GO,!m,!m"))]
  "register_operand(operands[0], HImode) || reg_or_0_operand (operands[1], HImode)"
  "* return output_move_single (operands, insn);"
  [(set_attr "length" "4,8,4,4,4,4,4,4,4,4,4,4")
   (set_attr "type" "int,multi,int,gstore,gstore,movgf,fsconv,movfg,fstore,movgf,gload,fload")])

;; Split 2 word load of constants into sethi/setlo instructions
(define_split
  [(set (match_operand:HI 0 "integer_register_operand" "")
	(match_operand:HI 1 "int_2word_operand" ""))]
  "reload_completed"
  [(set (match_dup 0)
	(high:HI (match_dup 1)))
   (set (match_dup 0)
	(lo_sum:HI (match_dup 0)
		(match_dup 1)))]
  "")

(define_insn "movhi_high"
  [(set (match_operand:HI 0 "integer_register_operand" "=d")
	(high:HI (match_operand:HI 1 "int_2word_operand" "i")))]
  ""
  "sethi #hi(%1), %0"
  [(set_attr "type" "sethi")
   (set_attr "length" "4")])

(define_insn "movhi_lo_sum"
  [(set (match_operand:HI 0 "integer_register_operand" "+d")
	(lo_sum:HI (match_dup 0)
		   (match_operand:HI 1 "int_2word_operand" "i")))]
  ""
  "setlo #lo(%1), %0"
  [(set_attr "type" "setlo")
   (set_attr "length" "4")])

(define_expand "movsi"
  [(set (match_operand:SI 0 "move_destination_operand" "")
	(match_operand:SI 1 "move_source_operand" ""))]
  ""
  "{ frv_emit_move (SImode, operands[0], operands[1]); DONE; }")

;; Note - it is best to only have one movsi pattern and to handle
;; all the various contingencies by the use of alternatives.  This
;; allows reload the greatest amount of flexibility (since reload will
;; only choose amongst alternatives for a selected insn, it will not
;; replace the insn with another one).

;; Unfortunately, we do have to separate out load-type moves from the rest,
;; and only allow memory source operands in the former.  If we do memory and
;; constant loads in a single pattern, reload will be tempted to force
;; constants into memory when the destination is a floating-point register.
;; That may make a function use a PIC pointer when it didn't before, and we
;; cannot change PIC usage (and hence stack layout) so late in the game.
;; The resulting sequences for loading constants into FPRs are preferable
;; even when we're not generating PIC code.

;; However, if we don't accept input from memory at all in the generic
;; movsi pattern, reloads for asm instructions that reference pseudos
;; that end up assigned to memory will fail to match, because we
;; recognize them right after they're emitted, and we don't
;; re-recognize them again after the substitution for memory.  So keep
;; a memory constraint available, just make sure reload won't be
;; tempted to use it.
;;
		   
		   
(define_insn "*movsi_load"
  [(set (match_operand:SI 0 "register_operand" "=d,f")
	(match_operand:SI 1 "frv_load_operand" "m,m"))]
  ""
  "* return output_move_single (operands, insn);"
  [(set_attr "length" "4")
   (set_attr "type" "gload,fload")])

(define_insn "*movsi_got"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(match_operand:SI 1 "got12_operand" ""))]
  ""
  "addi gr0, %1, %0"
  [(set_attr "type" "int")
   (set_attr "length" "4")])

(define_insn "*movsi_high_got"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(high:SI (match_operand:SI 1 "const_unspec_operand" "")))]
  ""
  "sethi %1, %0"
  [(set_attr "type" "sethi")
   (set_attr "length" "4")])

(define_insn "*movsi_lo_sum_got"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(lo_sum:SI (match_operand:SI 1 "integer_register_operand" "0")
		   (match_operand:SI 2 "const_unspec_operand" "")))]
  ""
  "setlo %2, %0"
  [(set_attr "type" "setlo")
   (set_attr "length" "4")])

(define_insn "*movsi_internal"
  [(set (match_operand:SI 0 "move_destination_operand" "=d,d,d,m,m,z,d,d,f,f,m,?f,?z,d,f")
	(match_operand:SI 1 "move_source_operand"      "L,n,d,d,O,d,z,f,d,f,f,GO,GO,!m,!m"))]
  "register_operand (operands[0], SImode) || reg_or_0_operand (operands[1], SImode)"
  "* return output_move_single (operands, insn);"
  [(set_attr "length" "4,8,4,4,4,4,4,4,4,4,4,4,4,4,4")
   (set_attr "type" "int,multi,int,gstore,gstore,spr,spr,movfg,movgf,fsconv,fstore,movgf,spr,gload,fload")])

;; Split 2 word load of constants into sethi/setlo instructions
(define_insn_and_split "*movsi_2word"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(match_operand:SI 1 "int_2word_operand" "i"))]
  ""
  "#"
  "reload_completed"
  [(set (match_dup 0)
	(high:SI (match_dup 1)))
   (set (match_dup 0)
	(lo_sum:SI (match_dup 0)
		(match_dup 1)))]
  ""
  [(set_attr "length" "8")
   (set_attr "type" "multi")])

(define_insn "movsi_high"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(high:SI (match_operand:SI 1 "int_2word_operand" "i")))]
  ""
  "sethi #hi(%1), %0"
  [(set_attr "type" "sethi")
   (set_attr "length" "4")])

(define_insn "movsi_lo_sum"
  [(set (match_operand:SI 0 "integer_register_operand" "+d")
	(lo_sum:SI (match_dup 0)
		   (match_operand:SI 1 "int_2word_operand" "i")))]
  ""
  "setlo #lo(%1), %0"
  [(set_attr "type" "setlo")
   (set_attr "length" "4")])

(define_expand "movdi"
  [(set (match_operand:DI 0 "nonimmediate_operand" "")
	(match_operand:DI 1 "general_operand" ""))]
  ""
  "{ frv_emit_move (DImode, operands[0], operands[1]); DONE; }")

(define_insn "*movdi_double"
  [(set (match_operand:DI 0 "move_destination_operand" "=e,?h,??d,??f,R,?R,??m,??m,e,?h,??d,??f,?e,??d,?h,??f,R,m,e,??d,e,??d,?h,??f")
	(match_operand:DI 1 "move_source_operand"      " e,h,d,f,e,h,d,f,R,R,m,m,h,f,e,d,GO,GO,GO,GO,nF,nF,GO,GO"))]
  "TARGET_DOUBLE
   && (register_operand (operands[0], DImode)
       || reg_or_0_operand (operands[1], DImode))"
  "* return output_move_double (operands, insn);"
  [(set_attr "length" "8,4,8,8,4,4,8,8,4,4,8,8,4,8,4,8,4,8,8,8,16,16,8,8")
   (set_attr "type" "multi,fdconv,multi,multi,gstore,fstore,gstore,fstore,gload,fload,gload,fload,movfg,movfg,movgf,movgf,gstore,gstore,multi,multi,multi,multi,movgf,movgf")])

(define_insn "*movdi_nodouble"
  [(set (match_operand:DI 0 "move_destination_operand" "=e,?h,??d,??f,R,?R,??m,??m,e,?h,??d,??f,?e,??d,?h,??f,R,m,e,??d,e,??d,?h,??f")
	(match_operand:DI 1 "move_source_operand"      " e,h,d,f,e,h,d,f,R,R,m,m,h,f,e,d,GO,GO,GO,GO,nF,nF,GO,GO"))]
  "!TARGET_DOUBLE
   && (register_operand (operands[0], DImode)
       || reg_or_0_operand (operands[1], DImode))"
  "* return output_move_double (operands, insn);"
  [(set_attr "length" "8,8,8,8,4,4,8,8,4,4,8,8,8,8,8,8,4,8,8,8,16,16,8,8")
   (set_attr "type" "multi,multi,multi,multi,gstore,fstore,gstore,fstore,gload,fload,gload,fload,movfg,movfg,movgf,movgf,gstore,gstore,multi,multi,multi,multi,movgf,movgf")])

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(match_operand:DI 1 "dbl_memory_two_insn_operand" ""))]
  "reload_completed"
  [(const_int 0)]
  "frv_split_double_load (operands[0], operands[1]);")

(define_split
  [(set (match_operand:DI 0 "odd_reg_operand" "")
	(match_operand:DI 1 "memory_operand" ""))]
  "reload_completed"
  [(const_int 0)]
  "frv_split_double_load (operands[0], operands[1]);")

(define_split
  [(set (match_operand:DI 0 "dbl_memory_two_insn_operand" "")
	(match_operand:DI 1 "reg_or_0_operand" ""))]
  "reload_completed"
  [(const_int 0)]
  "frv_split_double_store (operands[0], operands[1]);")

(define_split
  [(set (match_operand:DI 0 "memory_operand" "")
	(match_operand:DI 1 "odd_reg_operand" ""))]
  "reload_completed"
  [(const_int 0)]
  "frv_split_double_store (operands[0], operands[1]);")

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(match_operand:DI 1 "register_operand" ""))]
  "reload_completed
   && (odd_reg_operand (operands[0], DImode)
       || odd_reg_operand (operands[1], DImode)
       || (integer_register_operand (operands[0], DImode)
	   && integer_register_operand (operands[1], DImode))
       || (!TARGET_DOUBLE
	   && fpr_operand (operands[0], DImode)
	   && fpr_operand (operands[1], DImode)))"
  [(set (match_dup 2) (match_dup 4))
   (set (match_dup 3) (match_dup 5))]
  "
{
  rtx op0      = operands[0];
  rtx op0_low  = gen_lowpart (SImode, op0);
  rtx op0_high = gen_highpart (SImode, op0);
  rtx op1      = operands[1];
  rtx op1_low  = gen_lowpart (SImode, op1);
  rtx op1_high = gen_highpart (SImode, op1);

  /* We normally copy the low-numbered register first.  However, if the first
     register operand 0 is the same as the second register of operand 1, we
     must copy in the opposite order.  */

  if (REGNO (op0_high) == REGNO (op1_low))
    {
      operands[2] = op0_low;
      operands[3] = op0_high;
      operands[4] = op1_low;
      operands[5] = op1_high;
    }
  else
    {
      operands[2] = op0_high;
      operands[3] = op0_low;
      operands[4] = op1_high;
      operands[5] = op1_low;
    }
}")

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(match_operand:DI 1 "const_int_operand" ""))]
  "reload_completed"
  [(set (match_dup 2) (match_dup 4))
   (set (match_dup 3) (match_dup 5))]
  "
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];

  operands[2] = gen_highpart (SImode, op0);
  operands[3] = gen_lowpart (SImode, op0);
  if (HOST_BITS_PER_WIDE_INT <= 32)
    {
      operands[4] = GEN_INT ((INTVAL (op1) < 0) ? -1 : 0);
      operands[5] = op1;
    }
  else
    {
      operands[4] = GEN_INT ((((unsigned HOST_WIDE_INT)INTVAL (op1) >> 16)
			      >> 16) ^ ((unsigned HOST_WIDE_INT)1 << 31)
			     - ((unsigned HOST_WIDE_INT)1 << 31));
      operands[5] = GEN_INT (trunc_int_for_mode (INTVAL (op1), SImode));
    }
}")

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(match_operand:DI 1 "const_double_operand" ""))]
  "reload_completed"
  [(set (match_dup 2) (match_dup 4))
   (set (match_dup 3) (match_dup 5))]
  "
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];

  operands[2] = gen_highpart (SImode, op0);
  operands[3] = gen_lowpart (SImode, op0);
  operands[4] = GEN_INT (CONST_DOUBLE_HIGH (op1));
  operands[5] = GEN_INT (CONST_DOUBLE_LOW (op1));
}")

;; Floating Point Moves
;;
;; Note - Patterns for SF mode moves are compulsory, but
;; patterns for DF are optional, as GCC can synthesize them.

(define_expand "movsf"
  [(set (match_operand:SF 0 "general_operand" "")
	(match_operand:SF 1 "general_operand" ""))]
  ""
  "{ frv_emit_move (SFmode, operands[0], operands[1]); DONE; }")

(define_split
  [(set (match_operand:SF 0 "integer_register_operand" "")
	(match_operand:SF 1 "int_2word_operand" ""))]
  "reload_completed"
  [(set (match_dup 0)
	(high:SF (match_dup 1)))
   (set (match_dup 0)
	(lo_sum:SF (match_dup 0)
		(match_dup 1)))]
  "")

(define_insn "*movsf_load_has_fprs"
  [(set (match_operand:SF 0 "register_operand" "=f,d")
	(match_operand:SF 1 "frv_load_operand" "m,m"))]
  "TARGET_HAS_FPRS"
  "* return output_move_single (operands, insn);"
  [(set_attr "length" "4")
   (set_attr "type" "fload,gload")])

(define_insn "*movsf_internal_has_fprs"
  [(set (match_operand:SF 0 "move_destination_operand" "=f,f,m,m,?f,?d,?d,m,?d")
	(match_operand:SF 1 "move_source_operand" "f,OG,f,OG,d,f,d,d,F"))]
  "TARGET_HAS_FPRS
   && (register_operand (operands[0], SFmode) || reg_or_0_operand (operands[1], SFmode))"
  "* return output_move_single (operands, insn);"
  [(set_attr "length" "4,4,4,4,4,4,4,4,8")
   (set_attr "type" "fsconv,movgf,fstore,gstore,movgf,movfg,int,gstore,multi")])

;; If we don't support the double instructions, prefer gprs over fprs, since it
;; will all be emulated
(define_insn "*movsf_internal_no_fprs"
  [(set (match_operand:SF 0 "move_destination_operand" "=d,d,m,d,d")
	(match_operand:SF 1 "move_source_operand"      " d,OG,dOG,m,F"))]
  "!TARGET_HAS_FPRS
   && (register_operand (operands[0], SFmode) || reg_or_0_operand (operands[1], SFmode))"
  "* return output_move_single (operands, insn);"
  [(set_attr "length" "4,4,4,4,8")
   (set_attr "type" "int,int,gstore,gload,multi")])

(define_insn "movsf_high"
  [(set (match_operand:SF 0 "integer_register_operand" "=d")
	(high:SF (match_operand:SF 1 "int_2word_operand" "i")))]
  ""
  "sethi #hi(%1), %0"
  [(set_attr "type" "sethi")
   (set_attr "length" "4")])

(define_insn "movsf_lo_sum"
  [(set (match_operand:SF 0 "integer_register_operand" "+d")
	(lo_sum:SF (match_dup 0)
		   (match_operand:SF 1 "int_2word_operand" "i")))]
  ""
  "setlo #lo(%1), %0"
  [(set_attr "type" "setlo")
   (set_attr "length" "4")])

(define_expand "movdf"
  [(set (match_operand:DF 0 "nonimmediate_operand" "")
	(match_operand:DF 1 "general_operand" ""))]
  ""
  "{ frv_emit_move (DFmode, operands[0], operands[1]); DONE; }")

(define_insn "*movdf_double"
  [(set (match_operand:DF 0 "move_destination_operand" "=h,?e,??f,??d,R,?R,??m,??m,h,?e,??f,??d,?h,??f,?e,??d,R,m,h,??f,e,??d,e,??d")
	(match_operand:DF 1 "move_source_operand"      " h,e,f,d,h,e,f,d,R,R,m,m,e,d,h,f,GO,GO,GO,GO,GO,GO,F,F"))]
  "TARGET_DOUBLE
   && (register_operand (operands[0], DFmode)
       || reg_or_0_operand (operands[1], DFmode))"
  "* return output_move_double (operands, insn);"
  [(set_attr "length" "4,8,8,8,4,4,8,8,4,4,8,8,4,8,4,8,4,8,8,8,8,8,16,16")
   (set_attr "type" "fdconv,multi,multi,multi,fstore,gstore,fstore,gstore,fload,gload,fload,gload,movgf,movgf,movfg,movfg,gstore,gstore,movgf,movgf,multi,multi,multi,multi")])

;; If we don't support the double instructions, prefer gprs over fprs, since it
;; will all be emulated
(define_insn "*movdf_nodouble"
  [(set (match_operand:DF 0 "move_destination_operand" "=e,?h,??d,??f,R,?R,??m,??m,e,?h,??d,??f,?e,??d,?h,??f,R,m,e,??d,e,??d,?h,??f")
	(match_operand:DF 1 "move_source_operand"      " e,h,d,f,e,h,d,f,R,R,m,m,h,f,e,d,GO,GO,GO,GO,nF,nF,GO,GO"))]
  "!TARGET_DOUBLE
   && (register_operand (operands[0], DFmode)
       || reg_or_0_operand (operands[1], DFmode))"
  "* return output_move_double (operands, insn);"
  [(set_attr "length" "8,8,8,8,4,4,8,8,4,4,8,8,8,8,8,8,4,8,8,8,16,16,8,8")
   (set_attr "type" "multi,multi,multi,multi,gstore,fstore,gstore,fstore,gload,fload,gload,fload,movfg,movfg,movgf,movgf,gstore,gstore,multi,multi,multi,multi,movgf,movgf")])

(define_split
  [(set (match_operand:DF 0 "register_operand" "")
	(match_operand:DF 1 "dbl_memory_two_insn_operand" ""))]
  "reload_completed"
  [(const_int 0)]
  "frv_split_double_load (operands[0], operands[1]);")

(define_split
  [(set (match_operand:DF 0 "odd_reg_operand" "")
	(match_operand:DF 1 "memory_operand" ""))]
  "reload_completed"
  [(const_int 0)]
  "frv_split_double_load (operands[0], operands[1]);")

(define_split
  [(set (match_operand:DF 0 "dbl_memory_two_insn_operand" "")
	(match_operand:DF 1 "reg_or_0_operand" ""))]
  "reload_completed"
  [(const_int 0)]
  "frv_split_double_store (operands[0], operands[1]);")

(define_split
  [(set (match_operand:DF 0 "memory_operand" "")
	(match_operand:DF 1 "odd_reg_operand" ""))]
  "reload_completed"
  [(const_int 0)]
  "frv_split_double_store (operands[0], operands[1]);")

(define_split
  [(set (match_operand:DF 0 "register_operand" "")
	(match_operand:DF 1 "register_operand" ""))]
  "reload_completed
   && (odd_reg_operand (operands[0], DFmode)
       || odd_reg_operand (operands[1], DFmode)
       || (integer_register_operand (operands[0], DFmode)
	   && integer_register_operand (operands[1], DFmode))
       || (!TARGET_DOUBLE
	   && fpr_operand (operands[0], DFmode)
	   && fpr_operand (operands[1], DFmode)))"
  [(set (match_dup 2) (match_dup 4))
   (set (match_dup 3) (match_dup 5))]
  "
{
  rtx op0      = operands[0];
  rtx op0_low  = gen_lowpart (SImode, op0);
  rtx op0_high = gen_highpart (SImode, op0);
  rtx op1      = operands[1];
  rtx op1_low  = gen_lowpart (SImode, op1);
  rtx op1_high = gen_highpart (SImode, op1);

  /* We normally copy the low-numbered register first.  However, if the first
     register operand 0 is the same as the second register of operand 1, we
     must copy in the opposite order.  */

  if (REGNO (op0_high) == REGNO (op1_low))
    {
      operands[2] = op0_low;
      operands[3] = op0_high;
      operands[4] = op1_low;
      operands[5] = op1_high;
    }
  else
    {
      operands[2] = op0_high;
      operands[3] = op0_low;
      operands[4] = op1_high;
      operands[5] = op1_low;
    }
}")

(define_split
  [(set (match_operand:DF 0 "register_operand" "")
	(match_operand:DF 1 "const_int_operand" ""))]
  "reload_completed"
  [(set (match_dup 2) (match_dup 4))
   (set (match_dup 3) (match_dup 5))]
  "
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];

  operands[2] = gen_highpart (SImode, op0);
  operands[3] = gen_lowpart (SImode, op0);
  if (HOST_BITS_PER_WIDE_INT <= 32)
    {
      operands[4] = GEN_INT ((INTVAL (op1) < 0) ? -1 : 0);
      operands[5] = op1;
    }
  else
    {
      operands[4] = GEN_INT ((((unsigned HOST_WIDE_INT)INTVAL (op1) >> 16)
			      >> 16) ^ ((unsigned HOST_WIDE_INT)1 << 31)
			     - ((unsigned HOST_WIDE_INT)1 << 31));
      operands[5] = GEN_INT (trunc_int_for_mode (INTVAL (op1), SImode));
    }
}")

(define_split
  [(set (match_operand:DF 0 "register_operand" "")
	(match_operand:DF 1 "const_double_operand" ""))]
  "reload_completed"
  [(set (match_dup 2) (match_dup 4))
   (set (match_dup 3) (match_dup 5))]
  "
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  REAL_VALUE_TYPE rv;
  long l[2];

  REAL_VALUE_FROM_CONST_DOUBLE (rv, op1);
  REAL_VALUE_TO_TARGET_DOUBLE (rv, l);

  operands[2] = gen_highpart (SImode, op0);
  operands[3] = gen_lowpart (SImode, op0);
  operands[4] = GEN_INT (l[0]);
  operands[5] = GEN_INT (l[1]);
}")

;; String/block move insn.
;; Argument 0 is the destination
;; Argument 1 is the source
;; Argument 2 is the length
;; Argument 3 is the alignment

(define_expand "movmemsi"
  [(parallel [(set (match_operand:BLK 0 "" "")
		   (match_operand:BLK 1 "" ""))
	      (use (match_operand:SI 2 "" ""))
	      (use (match_operand:SI 3 "" ""))])]
  ""
  "
{
  if (frv_expand_block_move (operands))
    DONE;
  else
    FAIL;
}")

;; String/block set insn.
;; Argument 0 is the destination
;; Argument 1 is the length
;; Argument 2 is the byte value -- ignore any value but zero
;; Argument 3 is the alignment

(define_expand "setmemsi"
  [(parallel [(set (match_operand:BLK 0 "" "")
		   (match_operand 2 "" ""))
	      (use (match_operand:SI 1 "" ""))
	      (use (match_operand:SI 3 "" ""))])]
  ""
  "
{
  /* If value to set is not zero, use the library routine.  */
  if (operands[2] != const0_rtx)
    FAIL;

  if (frv_expand_block_clear (operands))
    DONE;
  else
    FAIL;
}")


;; The "membar" part of a __builtin_read* or __builtin_write* function.
;; Operand 0 is a volatile reference to the memory that the function reads
;; or writes.  Operand 1 is the address being accessed, or zero if the
;; address isn't a known constant.  Operand 2 describes the __builtin
;; function (either FRV_IO_READ or FRV_IO_WRITE).
(define_insn "optional_membar_<mode>"
  [(set (match_operand:IMODE 0 "memory_operand" "=m")
	(unspec:IMODE [(match_operand 1 "const_int_operand" "")
		       (match_operand 2 "const_int_operand" "")]
		      UNSPEC_OPTIONAL_MEMBAR))]
  ""
  "membar"
  [(set_attr "length" "4")])

;; ::::::::::::::::::::
;; ::
;; :: Reload CC registers
;; ::
;; ::::::::::::::::::::

;; Use as a define_expand so that cse/gcse/combine can't accidentally
;; create movcc insns.

(define_expand "movcc"
  [(parallel [(set (match_operand:CC 0 "move_destination_operand" "")
		   (match_operand:CC 1 "move_source_operand" ""))
	      (clobber (match_dup 2))])]
  ""
  "
{
 if (! reload_in_progress && ! reload_completed)
    FAIL;

 operands[2] = gen_rtx_REG (CC_CCRmode, ICR_TEMP);
}")

(define_insn "*internal_movcc"
  [(set (match_operand:CC 0 "move_destination_operand" "=t,d,d,m,d")
	(match_operand:CC 1 "move_source_operand" "d,d,m,d,t"))
   (clobber (match_scratch:CC_CCR 2 "=X,X,X,X,&v"))]
  "reload_in_progress || reload_completed"
  "@
   cmpi %1, #0, %0
   mov %1, %0
   ld%I1%U1 %M1, %0
   st%I0%U0 %1, %M0
   #"
  [(set_attr "length" "4,4,4,4,20")
   (set_attr "type" "int,int,gload,gstore,multi")])

;; To move an ICC value to a GPR for a signed comparison, we create a value
;; that when compared to 0, sets the N and Z flags appropriately (we don't care
;; about the V and C flags, since these comparisons are signed).

(define_split
  [(set (match_operand:CC 0 "integer_register_operand" "")
	(match_operand:CC 1 "icc_operand" ""))
   (clobber (match_operand:CC_CCR 2 "icr_operand" ""))]
  "reload_in_progress || reload_completed"
  [(match_dup 3)]
  "
{
  rtx dest = simplify_gen_subreg (SImode, operands[0], CCmode, 0);
  rtx icc  = operands[1];
  rtx icr  = operands[2];

  start_sequence ();

  emit_insn (gen_rtx_SET (VOIDmode, icr,
			  gen_rtx_LT (CC_CCRmode, icc, const0_rtx)));

  emit_insn (gen_movsi (dest, const1_rtx));

  emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				gen_rtx_NE (CC_CCRmode, icr, const0_rtx),
				gen_rtx_SET (VOIDmode, dest,
					     gen_rtx_NEG (SImode, dest))));

  emit_insn (gen_rtx_SET (VOIDmode, icr,
			  gen_rtx_EQ (CC_CCRmode, icc, const0_rtx)));

  emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				gen_rtx_NE (CC_CCRmode, icr, const0_rtx),
				gen_rtx_SET (VOIDmode, dest, const0_rtx)));

  operands[3] = get_insns ();
  end_sequence ();
}")

(define_expand "reload_incc"
  [(parallel [(set (match_operand:CC 2 "integer_register_operand" "=&d")
		   (match_operand:CC 1 "memory_operand" "m"))
	      (clobber (match_scratch:CC_CCR 3 ""))])
   (parallel [(set (match_operand:CC 0 "icc_operand" "=t")
		   (match_dup 2))
	      (clobber (match_scratch:CC_CCR 4 ""))])]
  ""
  "")

(define_expand "reload_outcc"
  [(parallel [(set (match_operand:CC 2 "integer_register_operand" "=&d")
		   (match_operand:CC 1 "icc_operand" "t"))
	      (clobber (match_dup 3))])
   (parallel [(set (match_operand:CC 0 "memory_operand" "=m")
		   (match_dup 2))
	      (clobber (match_scratch:CC_CCR 4 ""))])]
  ""
  "operands[3] = gen_rtx_REG (CC_CCRmode, ICR_TEMP);")

;; Reload CC_UNSmode for unsigned integer comparisons
;; Use define_expand so that cse/gcse/combine can't create movcc_uns insns

(define_expand "movcc_uns"
  [(parallel [(set (match_operand:CC_UNS 0 "move_destination_operand" "")
		   (match_operand:CC_UNS 1 "move_source_operand" ""))
	      (clobber (match_dup 2))])]
  ""
  "
{
 if (! reload_in_progress && ! reload_completed)
    FAIL;
 operands[2] = gen_rtx_REG (CC_CCRmode, ICR_TEMP);
}")

(define_insn "*internal_movcc_uns"
  [(set (match_operand:CC_UNS 0 "move_destination_operand" "=t,d,d,m,d")
	(match_operand:CC_UNS 1 "move_source_operand" "d,d,m,d,t"))
   (clobber (match_scratch:CC_CCR 2 "=X,X,X,X,&v"))]
  "reload_in_progress || reload_completed"
  "@
   cmpi %1, #1, %0
   mov %1, %0
   ld%I1%U1 %M1, %0
   st%I0%U0 %1, %M0
   #"
  [(set_attr "length" "4,4,4,4,20")
   (set_attr "type" "int,int,gload,gstore,multi")])

;; To move an ICC value to a GPR for an unsigned comparison, we create a value
;; that when compared to 1, sets the Z, V, and C flags appropriately (we don't
;; care about the N flag, since these comparisons are unsigned).

(define_split
  [(set (match_operand:CC_UNS 0 "integer_register_operand" "")
	(match_operand:CC_UNS 1 "icc_operand" ""))
   (clobber (match_operand:CC_CCR 2 "icr_operand" ""))]
  "reload_in_progress || reload_completed"
  [(match_dup 3)]
  "
{
  rtx dest = simplify_gen_subreg (SImode, operands[0], CC_UNSmode, 0);
  rtx icc  = operands[1];
  rtx icr  = operands[2];

  start_sequence ();

  emit_insn (gen_rtx_SET (VOIDmode, icr,
			  gen_rtx_GTU (CC_CCRmode, icc, const0_rtx)));

  emit_insn (gen_movsi (dest, const1_rtx));

  emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				gen_rtx_NE (CC_CCRmode, icr, const0_rtx),
				gen_addsi3 (dest, dest, dest)));

  emit_insn (gen_rtx_SET (VOIDmode, icr,
			  gen_rtx_LTU (CC_CCRmode, icc, const0_rtx)));

  emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				gen_rtx_NE (CC_CCRmode, icr, const0_rtx),
				gen_rtx_SET (VOIDmode, dest, const0_rtx)));

  operands[3] = get_insns ();
  end_sequence ();
}")

(define_expand "reload_incc_uns"
  [(parallel [(set (match_operand:CC_UNS 2 "integer_register_operand" "=&d")
		   (match_operand:CC_UNS 1 "memory_operand" "m"))
	      (clobber (match_scratch:CC_CCR 3 ""))])
   (parallel [(set (match_operand:CC_UNS 0 "icc_operand" "=t")
		   (match_dup 2))
	      (clobber (match_scratch:CC_CCR 4 ""))])]
  ""
  "")

(define_expand "reload_outcc_uns"
  [(parallel [(set (match_operand:CC_UNS 2 "integer_register_operand" "=&d")
		   (match_operand:CC_UNS 1 "icc_operand" "t"))
	      (clobber (match_dup 3))])
   (parallel [(set (match_operand:CC_UNS 0 "memory_operand" "=m")
		   (match_dup 2))
	      (clobber (match_scratch:CC_CCR 4 ""))])]
  ""
  "operands[3] = gen_rtx_REG (CC_CCRmode, ICR_TEMP);")

;; Reload CC_NZmode.  This is mostly the same as the CCmode and CC_UNSmode
;; handling, but it uses different sequences for moving between GPRs and ICCs.

(define_expand "movcc_nz"
  [(parallel [(set (match_operand:CC_NZ 0 "move_destination_operand" "")
		   (match_operand:CC_NZ 1 "move_source_operand" ""))
	      (clobber (match_dup 2))])]
  ""
  "
{
  if (!reload_in_progress && !reload_completed)
    FAIL;
  operands[2] = gen_rtx_REG (CC_CCRmode, ICR_TEMP);
}")

(define_insn "*internal_movcc_nz"
  [(set (match_operand:CC_NZ 0 "move_destination_operand" "=t,d,d,m,d")
	(match_operand:CC_NZ 1 "move_source_operand" "d,d,m,d,t"))
   (clobber (match_scratch:CC_CCR 2 "=X,X,X,X,&v"))]
  "reload_in_progress || reload_completed"
  "@
   cmpi %1, #0, %0
   mov %1, %0
   ld%I1%U1 %M1, %0
   st%I0%U0 %1, %M0
   #"
  [(set_attr "length" "4,4,4,4,20")
   (set_attr "type" "int,int,gload,gstore,multi")])

;; Set the destination to a value that, when compared with zero, will
;; restore the value of the Z and N flags.  The values of the other
;; flags don't matter.  The sequence is:
;;
;;     setlos op0,#-1
;;     ckp op1,op2
;;     csub gr0,op0,op0,op2
;;     ckeq op1,op2
;;     cmov gr0,op0,op2
(define_split
  [(set (match_operand:CC_NZ 0 "integer_register_operand" "")
	(match_operand:CC_NZ 1 "icc_operand" ""))
   (clobber (match_operand:CC_CCR 2 "icr_operand" ""))]
  "reload_in_progress || reload_completed"
  [(set (match_dup 3)
	(const_int -1))
   (set (match_dup 2)
	(ge:CC_CCR (match_dup 1)
		   (const_int 0)))
   (cond_exec (ne:CC_CCR (match_dup 2)
			 (const_int 0))
	      (set (match_dup 3)
		   (neg:SI (match_dup 3))))
   (set (match_dup 2)
	(eq:CC_CCR (match_dup 1)
		   (const_int 0)))
   (cond_exec (ne:CC_CCR (match_dup 2)
			 (const_int 0))
	      (set (match_dup 3) (const_int 0)))]
  "operands[3] = simplify_gen_subreg (SImode, operands[0], CC_NZmode, 0);")

(define_expand "reload_incc_nz"
  [(parallel [(set (match_operand:CC_NZ 2 "integer_register_operand" "=&d")
		   (match_operand:CC_NZ 1 "memory_operand" "m"))
	      (clobber (match_scratch:CC_CCR 3 ""))])
   (parallel [(set (match_operand:CC_NZ 0 "icc_operand" "=t")
		   (match_dup 2))
	      (clobber (match_scratch:CC_CCR 4 ""))])]
  ""
  "")

(define_expand "reload_outcc_nz"
  [(parallel [(set (match_operand:CC_NZ 2 "integer_register_operand" "=&d")
		   (match_operand:CC_NZ 1 "icc_operand" "t"))
	      (clobber (match_dup 3))])
   (parallel [(set (match_operand:CC_NZ 0 "memory_operand" "=m")
		   (match_dup 2))
	      (clobber (match_scratch:CC_CCR 4 ""))])]
  ""
  "operands[3] = gen_rtx_REG (CC_CCRmode, ICR_TEMP);")

;; Reload CC_FPmode for floating point comparisons
;; We use a define_expand here so that cse/gcse/combine can't accidentally
;; create movcc insns.  If this was a named define_insn, we would not be able
;; to make it conditional on reload.

(define_expand "movcc_fp"
  [(set (match_operand:CC_FP 0 "movcc_fp_destination_operand" "")
	(match_operand:CC_FP 1 "move_source_operand" ""))]
  "TARGET_HAS_FPRS"
  "
{
 if (! reload_in_progress && ! reload_completed)
    FAIL;
}")

(define_insn "*movcc_fp_internal"
  [(set (match_operand:CC_FP 0 "movcc_fp_destination_operand" "=d,d,d,m")
	(match_operand:CC_FP 1 "move_source_operand" "u,d,m,d"))]
  "TARGET_HAS_FPRS && (reload_in_progress || reload_completed)"
  "@
   #
   mov %1, %0
   ld%I1%U1 %M1, %0
   st%I0%U0 %1, %M0"
  [(set_attr "length" "12,4,4,4")
   (set_attr "type" "multi,int,gload,gstore")])


(define_expand "reload_incc_fp"
  [(match_operand:CC_FP 0 "fcc_operand" "=u")
   (match_operand:CC_FP 1 "gpr_or_memory_operand_with_scratch" "m")
   (match_operand:TI 2 "integer_register_operand" "=&d")]
  "TARGET_HAS_FPRS"
  "
{
  rtx cc_op2 = simplify_gen_subreg (CC_FPmode, operands[2], TImode, 0);
  rtx int_op2 = simplify_gen_subreg (SImode, operands[2], TImode, 0);
  rtx temp1 = simplify_gen_subreg (SImode, operands[2], TImode, 4);
  rtx temp2 = simplify_gen_subreg (SImode, operands[2], TImode, 8);
  int shift = CC_SHIFT_RIGHT (REGNO (operands[0]));
  HOST_WIDE_INT mask;

  if (!gpr_or_memory_operand (operands[1], CC_FPmode))
    {
      rtx addr;
      rtx temp3 = simplify_gen_subreg (SImode, operands[2], TImode, 12);

      gcc_assert (GET_CODE (operands[1]) == MEM);

      addr = XEXP (operands[1], 0);

      gcc_assert (GET_CODE (addr) == PLUS);

      emit_move_insn (temp3, XEXP (addr, 1));

      operands[1] = replace_equiv_address (operands[1],
					   gen_rtx_PLUS (GET_MODE (addr),
							 XEXP (addr, 0),
							 temp3));
    }

  emit_insn (gen_movcc_fp (cc_op2, operands[1]));
  if (shift)
    emit_insn (gen_ashlsi3 (int_op2, int_op2, GEN_INT (shift)));

  mask = ~ ((HOST_WIDE_INT)CC_MASK << shift);
  emit_insn (gen_movsi (temp1, GEN_INT (mask)));
  emit_insn (gen_update_fcc (operands[0], int_op2, temp1, temp2));
  DONE;
}")

(define_expand "reload_outcc_fp"
  [(set (match_operand:CC_FP 2 "integer_register_operand" "=&d")
	(match_operand:CC_FP 1 "fcc_operand" "u"))
   (set (match_operand:CC_FP 0 "memory_operand" "=m")
	(match_dup 2))]
  "TARGET_HAS_FPRS"
 "")

;; Convert a FCC value to gpr
(define_insn "read_fcc"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(unspec:SI [(match_operand:CC_FP 1 "fcc_operand" "u")]
		   UNSPEC_CC_TO_GPR))]
  "TARGET_HAS_FPRS"
  "movsg ccr, %0"
  [(set_attr "type" "spr")
   (set_attr "length" "4")])

(define_split
  [(set (match_operand:CC_FP 0 "integer_register_operand" "")
	(match_operand:CC_FP 1 "fcc_operand" ""))]
  "reload_completed && TARGET_HAS_FPRS"
  [(match_dup 2)]
  "
{
  rtx int_op0 = simplify_gen_subreg (SImode, operands[0], CC_FPmode, 0);
  int shift = CC_SHIFT_RIGHT (REGNO (operands[1]));

  start_sequence ();

  emit_insn (gen_read_fcc (int_op0, operands[1]));
  if (shift)
    emit_insn (gen_lshrsi3 (int_op0, int_op0, GEN_INT (shift)));

  emit_insn (gen_andsi3 (int_op0, int_op0, GEN_INT (CC_MASK)));

  operands[2] = get_insns ();
  end_sequence ();
}")

;; Move a gpr value to FCC.
;; Operand0 = FCC
;; Operand1 = reloaded value shifted appropriately
;; Operand2 = mask to eliminate current register
;; Operand3 = temporary to load/store ccr
(define_insn "update_fcc"
  [(set (match_operand:CC_FP 0 "fcc_operand" "=u")
	(unspec:CC_FP [(match_operand:SI 1 "integer_register_operand" "d")
		       (match_operand:SI 2 "integer_register_operand" "d")]
		      UNSPEC_GPR_TO_CC))
   (clobber (match_operand:SI 3 "integer_register_operand" "=&d"))]
  "TARGET_HAS_FPRS"
  "movsg ccr, %3\;and %2, %3, %3\;or %1, %3, %3\;movgs %3, ccr"
  [(set_attr "type" "multi")
   (set_attr "length" "16")])

;; Reload CC_CCRmode for conditional execution registers
(define_insn "movcc_ccr"
  [(set (match_operand:CC_CCR 0 "move_destination_operand" "=d,d,d,m,v,?w,C,d")
	(match_operand:CC_CCR 1 "move_source_operand" "C,d,m,d,n,n,C,L"))]
  ""
  "@
   #
   mov %1, %0
   ld%I1%U1 %M1, %0
   st%I0%U0 %1, %M0
   #
   #
   orcr %1, %1, %0
   setlos #%1, %0"
  [(set_attr "length" "8,4,4,4,8,12,4,4")
   (set_attr "type" "multi,int,gload,gstore,multi,multi,ccr,int")])

(define_expand "reload_incc_ccr"
  [(match_operand:CC_CCR 0 "cr_operand" "=C")
   (match_operand:CC_CCR 1 "memory_operand" "m")
   (match_operand:CC_CCR 2 "integer_register_operand" "=&d")]
  ""
  "
{
  rtx icc = gen_rtx_REG (CCmode, ICC_TEMP);
  rtx int_op2 = simplify_gen_subreg (SImode, operands[2], CC_CCRmode, 0);
  rtx icr = (ICR_P (REGNO (operands[0]))
	     ? operands[0] : gen_rtx_REG (CC_CCRmode, ICR_TEMP));

  emit_insn (gen_movcc_ccr (operands[2], operands[1]));
  emit_insn (gen_cmpsi_cc (icc, int_op2, const0_rtx));
  emit_insn (gen_movcc_ccr (icr, gen_rtx_NE (CC_CCRmode, icc, const0_rtx)));

  if (! ICR_P (REGNO (operands[0])))
    emit_insn (gen_movcc_ccr (operands[0], icr));

  DONE;
}")

(define_expand "reload_outcc_ccr"
  [(set (match_operand:CC_CCR 2 "integer_register_operand" "=&d")
	(match_operand:CC_CCR 1 "cr_operand" "C"))
   (set (match_operand:CC_CCR 0 "memory_operand" "=m")
	(match_dup 2))]
  ""
  "")

(define_split
  [(set (match_operand:CC_CCR 0 "integer_register_operand" "")
	(match_operand:CC_CCR 1 "cr_operand" ""))]
  "reload_completed"
  [(match_dup 2)]
  "
{
  rtx int_op0 = simplify_gen_subreg (SImode, operands[0], CC_CCRmode, 0);

  start_sequence ();
  emit_move_insn (operands[0], const1_rtx);
  emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				gen_rtx_EQ (CC_CCRmode,
					    operands[1],
					    const0_rtx),
				gen_rtx_SET (VOIDmode, int_op0,
					     const0_rtx)));

  operands[2] = get_insns ();
  end_sequence ();
}")

(define_split
  [(set (match_operand:CC_CCR 0 "cr_operand" "")
	(match_operand:CC_CCR 1 "const_int_operand" ""))]
  "reload_completed"
  [(match_dup 2)]
  "
{
  rtx icc = gen_rtx_REG (CCmode, ICC_TEMP);
  rtx r0  = gen_rtx_REG (SImode, GPR_FIRST);
  rtx icr = (ICR_P (REGNO (operands[0]))
	     ? operands[0] : gen_rtx_REG (CC_CCRmode, ICR_TEMP));

  start_sequence ();

 emit_insn (gen_cmpsi_cc (icc, r0, const0_rtx));

  emit_insn (gen_movcc_ccr (icr,
			    gen_rtx_fmt_ee (((INTVAL (operands[1]) == 0)
					     ? EQ : NE), CC_CCRmode,
					    r0, const0_rtx)));

  if (! ICR_P (REGNO (operands[0])))
    emit_insn (gen_movcc_ccr (operands[0], icr));

  operands[2] = get_insns ();
  end_sequence ();
}")


;; ::::::::::::::::::::
;; ::
;; :: Conversions
;; ::
;; ::::::::::::::::::::

;; Signed conversions from a smaller integer to a larger integer
;;
;; These operations are optional.  If they are not
;; present GCC will synthesize them for itself
;; Even though frv does not provide these instructions, we define them
;; to allow load + sign extend to be collapsed together
(define_insn "extendqihi2"
  [(set (match_operand:HI 0 "integer_register_operand" "=d,d")
	(sign_extend:HI (match_operand:QI 1 "gpr_or_memory_operand" "d,m")))]
  ""
  "@
   #
   ldsb%I1%U1 %M1,%0"
  [(set_attr "length" "8,4")
   (set_attr "type" "multi,gload")])

(define_split
  [(set (match_operand:HI 0 "integer_register_operand" "")
	(sign_extend:HI (match_operand:QI 1 "integer_register_operand" "")))]
  "reload_completed"
  [(match_dup 2)
   (match_dup 3)]
  "
{
  rtx op0   = gen_lowpart (SImode, operands[0]);
  rtx op1   = gen_lowpart (SImode, operands[1]);
  rtx shift = GEN_INT (24);

  operands[2] = gen_ashlsi3 (op0, op1, shift);
  operands[3] = gen_ashrsi3 (op0, op0, shift);
}")

(define_insn "extendqisi2"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d")
	(sign_extend:SI (match_operand:QI 1 "gpr_or_memory_operand" "d,m")))]
  ""
  "@
   #
   ldsb%I1%U1 %M1,%0"
  [(set_attr "length" "8,4")
   (set_attr "type" "multi,gload")])

(define_split
  [(set (match_operand:SI 0 "integer_register_operand" "")
	(sign_extend:SI (match_operand:QI 1 "integer_register_operand" "")))]
  "reload_completed"
  [(match_dup 2)
   (match_dup 3)]
  "
{
  rtx op0   = gen_lowpart (SImode, operands[0]);
  rtx op1   = gen_lowpart (SImode, operands[1]);
  rtx shift = GEN_INT (24);

  operands[2] = gen_ashlsi3 (op0, op1, shift);
  operands[3] = gen_ashrsi3 (op0, op0, shift);
}")

;;(define_insn "extendqidi2"
;;  [(set (match_operand:DI 0 "register_operand" "=r")
;;	(sign_extend:DI (match_operand:QI 1 "general_operand" "g")))]
;;  ""
;;  "extendqihi2 %0,%1"
;;  [(set_attr "length" "4")])

(define_insn "extendhisi2"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d")
	(sign_extend:SI (match_operand:HI 1 "gpr_or_memory_operand" "d,m")))]
  ""
  "@
   #
   ldsh%I1%U1 %M1,%0"
  [(set_attr "length" "8,4")
   (set_attr "type" "multi,gload")])

(define_split
  [(set (match_operand:SI 0 "integer_register_operand" "")
	(sign_extend:SI (match_operand:HI 1 "integer_register_operand" "")))]
  "reload_completed"
  [(match_dup 2)
   (match_dup 3)]
  "
{
  rtx op0   = gen_lowpart (SImode, operands[0]);
  rtx op1   = gen_lowpart (SImode, operands[1]);
  rtx shift = GEN_INT (16);

  operands[2] = gen_ashlsi3 (op0, op1, shift);
  operands[3] = gen_ashrsi3 (op0, op0, shift);
}")

;;(define_insn "extendhidi2"
;;  [(set (match_operand:DI 0 "register_operand" "=r")
;;	(sign_extend:DI (match_operand:HI 1 "general_operand" "g")))]
;;  ""
;;  "extendhihi2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;(define_insn "extendsidi2"
;;  [(set (match_operand:DI 0 "register_operand" "=r")
;;	(sign_extend:DI (match_operand:SI 1 "general_operand" "g")))]
;;  ""
;;  "extendsidi2 %0,%1"
;;  [(set_attr "length" "4")])

;; Unsigned conversions from a smaller integer to a larger integer
(define_insn "zero_extendqihi2"
  [(set (match_operand:HI 0 "integer_register_operand" "=d,d,d")
	(zero_extend:HI
	  (match_operand:QI 1 "gpr_or_memory_operand" "d,L,m")))]
  ""
  "@
   andi %1,#0xff,%0
   setlos %1,%0
   ldub%I1%U1 %M1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "int,int,gload")])

(define_insn "zero_extendqisi2"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d,d")
	(zero_extend:SI
	  (match_operand:QI 1 "gpr_or_memory_operand" "d,L,m")))]
  ""
  "@
   andi %1,#0xff,%0
   setlos %1,%0
   ldub%I1%U1 %M1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "int,int,gload")])

;;(define_insn "zero_extendqidi2"
;;  [(set (match_operand:DI 0 "register_operand" "=r")
;;	(zero_extend:DI (match_operand:QI 1 "general_operand" "g")))]
;;  ""
;;  "zero_extendqihi2 %0,%1"
;;  [(set_attr "length" "4")])

;; Do not set the type for the sethi to "sethi", since the scheduler will think
;; the sethi takes 0 cycles as part of allowing sethi/setlo to be in the same
;; VLIW instruction.
(define_insn "zero_extendhisi2"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d")
	(zero_extend:SI (match_operand:HI 1 "gpr_or_memory_operand" "0,m")))]
  ""
  "@
    sethi #hi(#0),%0
    lduh%I1%U1 %M1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "int,gload")])

;;(define_insn "zero_extendhidi2"
;;  [(set (match_operand:DI 0 "register_operand" "=r")
;;	(zero_extend:DI (match_operand:HI 1 "general_operand" "g")))]
;;  ""
;;  "zero_extendhihi2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;(define_insn "zero_extendsidi2"
;;  [(set (match_operand:DI 0 "register_operand" "=r")
;;	(zero_extend:DI (match_operand:SI 1 "general_operand" "g")))]
;;  ""
;;  "zero_extendsidi2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;;; Convert between floating point types of different sizes.
;;
;;(define_insn "extendsfdf2"
;;  [(set (match_operand:DF 0 "register_operand" "=r")
;;	(float_extend:DF (match_operand:SF 1 "register_operand" "r")))]
;;  ""
;;  "extendsfdf2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;(define_insn "truncdfsf2"
;;  [(set (match_operand:SF 0 "register_operand" "=r")
;;	(float_truncate:SF (match_operand:DF 1 "register_operand" "r")))]
;;  ""
;;  "truncdfsf2 %0,%1"
;;  [(set_attr "length" "4")])

;;;; Convert between signed integer types and floating point.
(define_insn "floatsisf2"
  [(set (match_operand:SF 0 "fpr_operand" "=f")
	(float:SF (match_operand:SI 1 "fpr_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "fitos %1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fsconv")])

(define_insn "floatsidf2"
  [(set (match_operand:DF 0 "fpr_operand" "=h")
	(float:DF (match_operand:SI 1 "fpr_operand" "f")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "fitod %1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fdconv")])

;;(define_insn "floatdisf2"
;;  [(set (match_operand:SF 0 "register_operand" "=r")
;;	(float:SF (match_operand:DI 1 "register_operand" "r")))]
;;  ""
;;  "floatdisf2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;(define_insn "floatdidf2"
;;  [(set (match_operand:DF 0 "register_operand" "=r")
;;	(float:DF (match_operand:DI 1 "register_operand" "r")))]
;;  ""
;;  "floatdidf2 %0,%1"
;;  [(set_attr "length" "4")])

(define_insn "fix_truncsfsi2"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
	(fix:SI (match_operand:SF 1 "fpr_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "fstoi %1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fsconv")])

(define_insn "fix_truncdfsi2"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
	(fix:SI (match_operand:DF 1 "fpr_operand" "h")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "fdtoi %1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fdconv")])

;;(define_insn "fix_truncsfdi2"
;;  [(set (match_operand:DI 0 "register_operand" "=r")
;;	(fix:DI (match_operand:SF 1 "register_operand" "r")))]
;;  ""
;;  "fix_truncsfdi2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;(define_insn "fix_truncdfdi2"
;;  [(set (match_operand:DI 0 "register_operand" "=r")
;;	(fix:DI (match_operand:DF 1 "register_operand" "r")))]
;;  ""
;;  "fix_truncdfdi2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;;; Convert between unsigned integer types and floating point.
;;
;;(define_insn "floatunssisf2"
;;  [(set (match_operand:SF 0 "register_operand" "=r")
;;	(unsigned_float:SF (match_operand:SI 1 "register_operand" "r")))]
;;  ""
;;  "floatunssisf2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;(define_insn "floatunssidf2"
;;  [(set (match_operand:DF 0 "register_operand" "=r")
;;	(unsigned_float:DF (match_operand:SI 1 "register_operand" "r")))]
;;  ""
;;  "floatunssidf2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;(define_insn "floatunsdisf2"
;;  [(set (match_operand:SF 0 "register_operand" "=r")
;;	(unsigned_float:SF (match_operand:DI 1 "register_operand" "r")))]
;;  ""
;;  "floatunsdisf2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;(define_insn "floatunsdidf2"
;;  [(set (match_operand:DF 0 "register_operand" "=r")
;;	(unsigned_float:DF (match_operand:DI 1 "register_operand" "r")))]
;;  ""
;;  "floatunsdidf2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;(define_insn "fixuns_truncsfsi2"
;;  [(set (match_operand:SI 0 "register_operand" "=r")
;;	(unsigned_fix:SI (match_operand:SF 1 "register_operand" "r")))]
;;  ""
;;  "fixuns_truncsfsi2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;(define_insn "fixuns_truncdfsi2"
;;  [(set (match_operand:SI 0 "register_operand" "=r")
;;	(unsigned_fix:SI (match_operand:DF 1 "register_operand" "r")))]
;;  ""
;;  "fixuns_truncdfsi2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;(define_insn "fixuns_truncsfdi2"
;;  [(set (match_operand:DI 0 "register_operand" "=r")
;;	(unsigned_fix:DI (match_operand:SF 1 "register_operand" "r")))]
;;  ""
;;  "fixuns_truncsfdi2 %0,%1"
;;  [(set_attr "length" "4")])
;;
;;(define_insn "fixuns_truncdfdi2"
;;  [(set (match_operand:DI 0 "register_operand" "=r")
;;	(unsigned_fix:DI (match_operand:DF 1 "register_operand" "r")))]
;;  ""
;;  "fixuns_truncdfdi2 %0,%1"
;;  [(set_attr "length" "4")])


;; ::::::::::::::::::::
;; ::
;; :: 32 bit Integer arithmetic
;; ::
;; ::::::::::::::::::::

;; Addition
(define_insn "addsi3"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(plus:SI (match_operand:SI 1 "integer_register_operand" "%d")
		 (match_operand:SI 2 "gpr_or_int12_operand" "dNOPQ")))]
  ""
  "add%I2 %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

;; Subtraction.  No need to worry about constants, since the compiler
;; canonicalizes them into addsi3's.  We prevent SUBREG's here to work around a
;; combine bug, that combines the 32x32->upper 32 bit multiply that uses a
;; SUBREG with a minus that shows up in modulus by constants.
(define_insn "subsi3"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(minus:SI (match_operand:SI 1 "gpr_no_subreg_operand" "d")
		  (match_operand:SI 2 "gpr_no_subreg_operand" "d")))]
  ""
  "sub %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

;; Signed multiplication producing 64 bit results from 32 bit inputs
;; Note, frv doesn't have a 32x32->32 bit multiply, but the compiler
;; will do the 32x32->64 bit multiply and use the bottom word.
(define_expand "mulsidi3"
  [(set (match_operand:DI 0 "integer_register_operand" "")
	(mult:DI (sign_extend:DI (match_operand:SI 1 "integer_register_operand" ""))
		 (sign_extend:DI (match_operand:SI 2 "gpr_or_int12_operand" ""))))]
  ""
  "
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      emit_insn (gen_mulsidi3_const (operands[0], operands[1], operands[2]));
      DONE;
    }
}")

(define_insn "*mulsidi3_reg"
  [(set (match_operand:DI 0 "even_gpr_operand" "=e")
	(mult:DI (sign_extend:DI (match_operand:SI 1 "integer_register_operand" "%d"))
		 (sign_extend:DI (match_operand:SI 2 "integer_register_operand" "d"))))]
  ""
  "smul %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "mul")])

(define_insn "mulsidi3_const"
  [(set (match_operand:DI 0 "even_gpr_operand" "=e")
	(mult:DI (sign_extend:DI (match_operand:SI 1 "integer_register_operand" "d"))
		 (match_operand:SI 2 "int12_operand" "NOP")))]
  ""
  "smuli %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "mul")])

;; Unsigned multiplication producing 64 bit results from 32 bit inputs
(define_expand "umulsidi3"
  [(set (match_operand:DI 0 "even_gpr_operand" "")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "integer_register_operand" ""))
		 (zero_extend:DI (match_operand:SI 2 "gpr_or_int12_operand" ""))))]
  ""
  "
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      emit_insn (gen_umulsidi3_const (operands[0], operands[1], operands[2]));
      DONE;
    }
}")

(define_insn "*mulsidi3_reg"
  [(set (match_operand:DI 0 "even_gpr_operand" "=e")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "integer_register_operand" "%d"))
		 (zero_extend:DI (match_operand:SI 2 "integer_register_operand" "d"))))]
  ""
  "umul %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "mul")])

(define_insn "umulsidi3_const"
  [(set (match_operand:DI 0 "even_gpr_operand" "=e")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "integer_register_operand" "d"))
		 (match_operand:SI 2 "int12_operand" "NOP")))]
  ""
  "umuli %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "mul")])

;; Signed Division
(define_insn "divsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
	(div:SI (match_operand:SI 1 "register_operand" "d,d")
		(match_operand:SI 2 "gpr_or_int12_operand" "d,NOP")))]
  ""
  "sdiv%I2 %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "div")])

;; Unsigned Division
(define_insn "udivsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
	(udiv:SI (match_operand:SI 1 "register_operand" "d,d")
		 (match_operand:SI 2 "gpr_or_int12_operand" "d,NOP")))]
  ""
  "udiv%I2 %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "div")])

;; Negation
(define_insn "negsi2"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(neg:SI (match_operand:SI 1 "integer_register_operand" "d")))]
  ""
  "sub %.,%1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

;; Find first one bit
;; (define_insn "ffssi2"
;;   [(set (match_operand:SI 0 "register_operand" "=r")
;; 	(ffs:SI (match_operand:SI 1 "register_operand" "r")))]
;;   ""
;;   "ffssi2 %0,%1"
;;   [(set_attr "length" "4")])


;; ::::::::::::::::::::
;; ::
;; :: 64 bit Integer arithmetic
;; ::
;; ::::::::::::::::::::

;; Addition
(define_insn_and_split "adddi3"
  [(set (match_operand:DI 0 "integer_register_operand" "=&e,e")
	(plus:DI (match_operand:DI 1 "integer_register_operand" "%e,0")
		 (match_operand:DI 2 "gpr_or_int10_operand" "eJ,eJ")))
   (clobber (match_scratch:CC 3 "=t,t"))]
  ""
  "#"
  "reload_completed"
  [(match_dup 4)
   (match_dup 5)]
  "
{
  rtx parts[3][2];
  int op, part;

  for (op = 0; op < 3; op++)
    for (part = 0; part < 2; part++)
      parts[op][part] = simplify_gen_subreg (SImode, operands[op],
					     DImode, part * UNITS_PER_WORD);

  operands[4] = gen_adddi3_lower (parts[0][1], parts[1][1], parts[2][1],
				  operands[3]);
  operands[5] = gen_adddi3_upper (parts[0][0], parts[1][0], parts[2][0],
				  copy_rtx (operands[3]));
}"
  [(set_attr "length" "8")
   (set_attr "type" "multi")])

;; Subtraction  No need to worry about constants, since the compiler
;; canonicalizes them into adddi3's.
(define_insn_and_split "subdi3"
  [(set (match_operand:DI 0 "integer_register_operand" "=&e,e,e")
	(minus:DI (match_operand:DI 1 "integer_register_operand" "e,0,e")
		  (match_operand:DI 2 "integer_register_operand" "e,e,0")))
   (clobber (match_scratch:CC 3 "=t,t,t"))]
  ""
  "#"
  "reload_completed"
  [(match_dup 4)
   (match_dup 5)]
  "
{
  rtx op0_high = gen_highpart (SImode, operands[0]);
  rtx op1_high = gen_highpart (SImode, operands[1]);
  rtx op2_high = gen_highpart (SImode, operands[2]);
  rtx op0_low  = gen_lowpart (SImode, operands[0]);
  rtx op1_low  = gen_lowpart (SImode, operands[1]);
  rtx op2_low  = gen_lowpart (SImode, operands[2]);
  rtx op3 = operands[3];

  operands[4] = gen_subdi3_lower (op0_low, op1_low, op2_low, op3);
  operands[5] = gen_subdi3_upper (op0_high, op1_high, op2_high, op3);
}"
  [(set_attr "length" "8")
   (set_attr "type" "multi")])

;; Patterns for addsi3/subdi3 after splitting
(define_insn "adddi3_lower"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(plus:SI (match_operand:SI 1 "integer_register_operand" "d")
		 (match_operand:SI 2 "gpr_or_int10_operand" "dJ")))
   (set (match_operand:CC 3 "icc_operand" "=t")
	(compare:CC (plus:SI (match_dup 1)
			     (match_dup 2))
		    (const_int 0)))]
  ""
  "add%I2cc %1,%2,%0,%3"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "adddi3_upper"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(plus:SI (match_operand:SI 1 "integer_register_operand" "d")
		 (plus:SI (match_operand:SI 2 "gpr_or_int10_operand" "dJ")
			  (match_operand:CC 3 "icc_operand" "t"))))]
  ""
  "addx%I2 %1,%2,%0,%3"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "subdi3_lower"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(minus:SI (match_operand:SI 1 "integer_register_operand" "d")
		  (match_operand:SI 2 "integer_register_operand" "d")))
   (set (match_operand:CC 3 "icc_operand" "=t")
	(compare:CC (plus:SI (match_dup 1)
			     (match_dup 2))
		    (const_int 0)))]
  ""
  "subcc %1,%2,%0,%3"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "subdi3_upper"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(minus:SI (match_operand:SI 1 "integer_register_operand" "d")
		  (minus:SI (match_operand:SI 2 "integer_register_operand" "d")
			    (match_operand:CC 3 "icc_operand" "t"))))]
  ""
  "subx %1,%2,%0,%3"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn_and_split "negdi2"
  [(set (match_operand:DI 0 "integer_register_operand" "=&e,e")
	(neg:DI (match_operand:DI 1 "integer_register_operand" "e,0")))
   (clobber (match_scratch:CC 2 "=t,t"))]
  ""
  "#"
  "reload_completed"
  [(match_dup 3)
   (match_dup 4)]
  "
{
  rtx op0_high = gen_highpart (SImode, operands[0]);
  rtx op1_high = gen_rtx_REG (SImode, GPR_FIRST);
  rtx op2_high = gen_highpart (SImode, operands[1]);
  rtx op0_low  = gen_lowpart (SImode, operands[0]);
  rtx op1_low  = op1_high;
  rtx op2_low  = gen_lowpart (SImode, operands[1]);
  rtx op3 = operands[2];

  operands[3] = gen_subdi3_lower (op0_low, op1_low, op2_low, op3);
  operands[4] = gen_subdi3_upper (op0_high, op1_high, op2_high, op3);
}"
  [(set_attr "length" "8")
   (set_attr "type" "multi")])

;; Multiplication (same size)
;; (define_insn "muldi3"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(mult:DI (match_operand:DI 1 "register_operand" "%r")
;; 		 (match_operand:DI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "muldi3 %0,%1,%2"
;;   [(set_attr "length" "4")])

;; Signed Division
;; (define_insn "divdi3"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(div:DI (match_operand:DI 1 "register_operand" "r")
;; 		(match_operand:DI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "divdi3 %0,%1,%2"
;;   [(set_attr "length" "4")])

;; Undsgned Division
;; (define_insn "udivdi3"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(udiv:DI (match_operand:DI 1 "register_operand" "r")
;; 		 (match_operand:DI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "udivdi3 %0,%1,%2"
;;   [(set_attr "length" "4")])

;; Negation
;; (define_insn "negdi2"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(neg:DI (match_operand:DI 1 "register_operand" "r")))]
;;   ""
;;   "negdi2 %0,%1"
;;   [(set_attr "length" "4")])

;; Find first one bit
;; (define_insn "ffsdi2"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(ffs:DI (match_operand:DI 1 "register_operand" "r")))]
;;   ""
;;   "ffsdi2 %0,%1"
;;   [(set_attr "length" "4")])


;; ::::::::::::::::::::
;; ::
;; :: 32 bit floating point arithmetic
;; ::
;; ::::::::::::::::::::

;; Addition
(define_insn "addsf3"
  [(set (match_operand:SF 0 "fpr_operand" "=f")
	(plus:SF (match_operand:SF 1 "fpr_operand" "%f")
		 (match_operand:SF 2 "fpr_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "fadds %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fsadd")])

;; Subtraction
(define_insn "subsf3"
  [(set (match_operand:SF 0 "fpr_operand" "=f")
	(minus:SF (match_operand:SF 1 "fpr_operand" "f")
		  (match_operand:SF 2 "fpr_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "fsubs %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fsadd")])

;; Multiplication
(define_insn "mulsf3"
  [(set (match_operand:SF 0 "fpr_operand" "=f")
	(mult:SF (match_operand:SF 1 "fpr_operand" "%f")
		 (match_operand:SF 2 "fpr_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "fmuls %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fsmul")])

;; Multiplication with addition/subtraction
(define_insn "*muladdsf4"
  [(set (match_operand:SF 0 "fpr_operand" "=f")
	(plus:SF (mult:SF (match_operand:SF 1 "fpr_operand" "%f")
			  (match_operand:SF 2 "fpr_operand" "f"))
		 (match_operand:SF 3 "fpr_operand" "0")))]
  "TARGET_HARD_FLOAT && TARGET_MULADD"
  "fmadds %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fsmadd")])

(define_insn "*mulsubsf4"
  [(set (match_operand:SF 0 "fpr_operand" "=f")
	(minus:SF (mult:SF (match_operand:SF 1 "fpr_operand" "%f")
			   (match_operand:SF 2 "fpr_operand" "f"))
		  (match_operand:SF 3 "fpr_operand" "0")))]
  "TARGET_HARD_FLOAT && TARGET_MULADD"
  "fmsubs %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fsmadd")])

;; Division
(define_insn "divsf3"
  [(set (match_operand:SF 0 "fpr_operand" "=f")
	(div:SF (match_operand:SF 1 "fpr_operand" "f")
		(match_operand:SF 2 "fpr_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "fdivs %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fsdiv")])

;; Negation
(define_insn "negsf2"
  [(set (match_operand:SF 0 "fpr_operand" "=f")
	(neg:SF (match_operand:SF 1 "fpr_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "fnegs %1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fsconv")])

;; Absolute value
(define_insn "abssf2"
  [(set (match_operand:SF 0 "fpr_operand" "=f")
	(abs:SF (match_operand:SF 1 "fpr_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "fabss %1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fsconv")])

;; Square root
(define_insn "sqrtsf2"
  [(set (match_operand:SF 0 "fpr_operand" "=f")
	(sqrt:SF (match_operand:SF 1 "fpr_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "fsqrts %1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "sqrt_single")])


;; ::::::::::::::::::::
;; ::
;; :: 64 bit floating point arithmetic
;; ::
;; ::::::::::::::::::::

;; Addition
(define_insn "adddf3"
  [(set (match_operand:DF 0 "even_fpr_operand" "=h")
	(plus:DF (match_operand:DF 1 "fpr_operand" "%h")
		 (match_operand:DF 2 "fpr_operand" "h")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "faddd %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fdadd")])

;; Subtraction
(define_insn "subdf3"
  [(set (match_operand:DF 0 "even_fpr_operand" "=h")
	(minus:DF (match_operand:DF 1 "fpr_operand" "h")
		  (match_operand:DF 2 "fpr_operand" "h")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "fsubd %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fdadd")])

;; Multiplication
(define_insn "muldf3"
  [(set (match_operand:DF 0 "even_fpr_operand" "=h")
	(mult:DF (match_operand:DF 1 "fpr_operand" "%h")
		 (match_operand:DF 2 "fpr_operand" "h")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "fmuld %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fdmul")])

;; Multiplication with addition/subtraction
(define_insn "*muladddf4"
  [(set (match_operand:DF 0 "fpr_operand" "=f")
	(plus:DF (mult:DF (match_operand:DF 1 "fpr_operand" "%f")
			  (match_operand:DF 2 "fpr_operand" "f"))
		 (match_operand:DF 3 "fpr_operand" "0")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE && TARGET_MULADD"
  "fmaddd %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fdmadd")])

(define_insn "*mulsubdf4"
  [(set (match_operand:DF 0 "fpr_operand" "=f")
	(minus:DF (mult:DF (match_operand:DF 1 "fpr_operand" "%f")
			   (match_operand:DF 2 "fpr_operand" "f"))
		  (match_operand:DF 3 "fpr_operand" "0")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE && TARGET_MULADD"
  "fmsubd %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fdmadd")])

;; Division
(define_insn "divdf3"
  [(set (match_operand:DF 0 "even_fpr_operand" "=h")
	(div:DF (match_operand:DF 1 "fpr_operand" "h")
		(match_operand:DF 2 "fpr_operand" "h")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "fdivd %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fddiv")])

;; Negation
(define_insn "negdf2"
  [(set (match_operand:DF 0 "even_fpr_operand" "=h")
	(neg:DF (match_operand:DF 1 "fpr_operand" "h")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "fnegd %1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fdconv")])

;; Absolute value
(define_insn "absdf2"
  [(set (match_operand:DF 0 "even_fpr_operand" "=h")
	(abs:DF (match_operand:DF 1 "fpr_operand" "h")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "fabsd %1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fdconv")])

;; Square root
(define_insn "sqrtdf2"
  [(set (match_operand:DF 0 "even_fpr_operand" "=h")
	(sqrt:DF (match_operand:DF 1 "fpr_operand" "h")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "fsqrtd %1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "sqrt_double")])


;; ::::::::::::::::::::
;; ::
;; :: 32 bit Integer Shifts and Rotates
;; ::
;; ::::::::::::::::::::

;; Arithmetic Shift Left
(define_insn "ashlsi3"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d")
	(ashift:SI (match_operand:SI 1 "integer_register_operand" "d,d")
		   (match_operand:SI 2 "gpr_or_int12_operand" "d,NOP")))]
  ""
  "sll%I2 %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

;; Arithmetic Shift Right
(define_insn "ashrsi3"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d")
	(ashiftrt:SI (match_operand:SI 1 "integer_register_operand" "d,d")
		     (match_operand:SI 2 "gpr_or_int12_operand" "d,NOP")))]
  ""
  "sra%I2 %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

;; Logical Shift Right
(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d")
	(lshiftrt:SI (match_operand:SI 1 "integer_register_operand" "d,d")
		     (match_operand:SI 2 "gpr_or_int12_operand" "d,NOP")))]
  ""
  "srl%I2 %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

;; Rotate Left
;; (define_insn "rotlsi3"
;;   [(set (match_operand:SI 0 "register_operand" "=r")
;; 	(rotate:SI (match_operand:SI 1 "register_operand" "r")
;; 		   (match_operand:SI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "rotlsi3 %0,%1,%2"
;;   [(set_attr "length" "4")])

;; Rotate Right
;; (define_insn "rotrsi3"
;;   [(set (match_operand:SI 0 "register_operand" "=r")
;; 	(rotatert:SI (match_operand:SI 1 "register_operand" "r")
;; 		     (match_operand:SI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "rotrsi3 %0,%1,%2"
;;   [(set_attr "length" "4")])


;; ::::::::::::::::::::
;; ::
;; :: 64 bit Integer Shifts and Rotates
;; ::
;; ::::::::::::::::::::

;; Arithmetic Shift Left
;; (define_insn "ashldi3"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(ashift:DI (match_operand:DI 1 "register_operand" "r")
;; 		   (match_operand:SI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "ashldi3 %0,%1,%2"
;;   [(set_attr "length" "4")])

;; Arithmetic Shift Right
;; (define_insn "ashrdi3"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(ashiftrt:DI (match_operand:DI 1 "register_operand" "r")
;; 		     (match_operand:SI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "ashrdi3 %0,%1,%2"
;;   [(set_attr "length" "4")])

;; Logical Shift Right
;; (define_insn "lshrdi3"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(lshiftrt:DI (match_operand:DI 1 "register_operand" "r")
;; 		     (match_operand:SI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "lshrdi3 %0,%1,%2"
;;   [(set_attr "length" "4")])

;; Rotate Left
;; (define_insn "rotldi3"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(rotate:DI (match_operand:DI 1 "register_operand" "r")
;; 		   (match_operand:SI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "rotldi3 %0,%1,%2"
;;   [(set_attr "length" "4")])

;; Rotate Right
;; (define_insn "rotrdi3"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(rotatert:DI (match_operand:DI 1 "register_operand" "r")
;; 		     (match_operand:SI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "rotrdi3 %0,%1,%2"
;;   [(set_attr "length" "4")])


;; ::::::::::::::::::::
;; ::
;; :: 32 Bit Integer Logical operations
;; ::
;; ::::::::::::::::::::

;; Logical AND, 32 bit integers
(define_insn "andsi3_media"
  [(set (match_operand:SI 0 "gpr_or_fpr_operand" "=d,f")
	(and:SI (match_operand:SI 1 "gpr_or_fpr_operand" "%d,f")
		(match_operand:SI 2 "gpr_fpr_or_int12_operand" "dNOP,f")))]
  "TARGET_MEDIA"
  "@
   and%I2 %1, %2, %0
   mand %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "int,mlogic")])

(define_insn "andsi3_nomedia"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(and:SI (match_operand:SI 1 "integer_register_operand" "%d")
		(match_operand:SI 2 "gpr_or_int12_operand" "dNOP")))]
  "!TARGET_MEDIA"
  "and%I2 %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_expand "andsi3"
  [(set (match_operand:SI 0 "gpr_or_fpr_operand" "")
	(and:SI (match_operand:SI 1 "gpr_or_fpr_operand" "")
		(match_operand:SI 2 "gpr_fpr_or_int12_operand" "")))]
  ""
  "")

;; Inclusive OR, 32 bit integers
(define_insn "iorsi3_media"
  [(set (match_operand:SI 0 "gpr_or_fpr_operand" "=d,f")
	(ior:SI (match_operand:SI 1 "gpr_or_fpr_operand" "%d,f")
		(match_operand:SI 2 "gpr_fpr_or_int12_operand" "dNOP,f")))]
  "TARGET_MEDIA"
  "@
   or%I2 %1, %2, %0
   mor %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "int,mlogic")])

(define_insn "iorsi3_nomedia"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(ior:SI (match_operand:SI 1 "integer_register_operand" "%d")
		(match_operand:SI 2 "gpr_or_int12_operand" "dNOP")))]
  "!TARGET_MEDIA"
  "or%I2 %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_expand "iorsi3"
  [(set (match_operand:SI 0 "gpr_or_fpr_operand" "")
	(ior:SI (match_operand:SI 1 "gpr_or_fpr_operand" "")
		(match_operand:SI 2 "gpr_fpr_or_int12_operand" "")))]
  ""
  "")

;; Exclusive OR, 32 bit integers
(define_insn "xorsi3_media"
  [(set (match_operand:SI 0 "gpr_or_fpr_operand" "=d,f")
	(xor:SI (match_operand:SI 1 "gpr_or_fpr_operand" "%d,f")
		(match_operand:SI 2 "gpr_fpr_or_int12_operand" "dNOP,f")))]
  "TARGET_MEDIA"
  "@
   xor%I2 %1, %2, %0
   mxor %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "int,mlogic")])

(define_insn "xorsi3_nomedia"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(xor:SI (match_operand:SI 1 "integer_register_operand" "%d")
		(match_operand:SI 2 "gpr_or_int12_operand" "dNOP")))]
  "!TARGET_MEDIA"
  "xor%I2 %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_expand "xorsi3"
  [(set (match_operand:SI 0 "gpr_or_fpr_operand" "")
	(xor:SI (match_operand:SI 1 "gpr_or_fpr_operand" "")
		(match_operand:SI 2 "gpr_fpr_or_int12_operand" "")))]
  ""
  "")

;; One's complement, 32 bit integers
(define_insn "one_cmplsi2_media"
  [(set (match_operand:SI 0 "gpr_or_fpr_operand" "=d,f")
	(not:SI (match_operand:SI 1 "gpr_or_fpr_operand" "d,f")))]
  "TARGET_MEDIA"
  "@
   not %1, %0
   mnot %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "int,mlogic")])

(define_insn "one_cmplsi2_nomedia"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(not:SI (match_operand:SI 1 "integer_register_operand" "d")))]
  "!TARGET_MEDIA"
  "not %1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_expand "one_cmplsi2"
  [(set (match_operand:SI 0 "gpr_or_fpr_operand" "")
	(not:SI (match_operand:SI 1 "gpr_or_fpr_operand" "")))]
  ""
  "")


;; ::::::::::::::::::::
;; ::
;; :: 64 Bit Integer Logical operations
;; ::
;; ::::::::::::::::::::

;; Logical AND, 64 bit integers
;; (define_insn "anddi3"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(and:DI (match_operand:DI 1 "register_operand" "%r")
;; 		(match_operand:DI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "anddi3 %0,%1,%2"
;;   [(set_attr "length" "4")])

;; Inclusive OR, 64 bit integers
;; (define_insn "iordi3"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(ior:DI (match_operand:DI 1 "register_operand" "%r")
;; 		(match_operand:DI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "iordi3 %0,%1,%2"
;;   [(set_attr "length" "4")])

;; Exclusive OR, 64 bit integers
;; (define_insn "xordi3"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(xor:DI (match_operand:DI 1 "register_operand" "%r")
;; 		(match_operand:DI 2 "nonmemory_operand" "ri")))]
;;   ""
;;   "xordi3 %0,%1,%2"
;;   [(set_attr "length" "4")])

;; One's complement, 64 bit integers
;; (define_insn "one_cmpldi2"
;;   [(set (match_operand:DI 0 "register_operand" "=r")
;; 	(not:DI (match_operand:DI 1 "register_operand" "r")))]
;;   ""
;;   "notdi3 %0,%1"
;;   [(set_attr "length" "4")])


;; ::::::::::::::::::::
;; ::
;; :: Combination of integer operation with comparison
;; ::
;; ::::::::::::::::::::

(define_insn "*combo_intop_compare1"
  [(set (match_operand:CC_NZ 0 "icc_operand" "=t")
	(compare:CC_NZ
	 (match_operator:SI 1 "intop_compare_operator"
		       [(match_operand:SI 2 "integer_register_operand" "d")
			(match_operand:SI 3 "gpr_or_int10_operand" "dJ")])
	 (const_int 0)))]
  ""
  "%O1%I3cc %2, %3, %., %0"
  [(set_attr "type" "int")
   (set_attr "length" "4")])

(define_insn "*combo_intop_compare2"
  [(set (match_operand:CC_NZ 0 "icc_operand" "=t")
	(compare:CC_NZ
	 (match_operator:SI 1 "intop_compare_operator"
			[(match_operand:SI 2 "integer_register_operand" "d")
			 (match_operand:SI 3 "gpr_or_int10_operand" "dJ")])
	 (const_int 0)))
   (set (match_operand:SI 4 "integer_register_operand" "=d")
	(match_operator:SI 5 "intop_compare_operator"
			   [(match_dup 2)
			    (match_dup 3)]))]
  "GET_CODE (operands[1]) == GET_CODE (operands[5])"
  "%O1%I3cc %2, %3, %4, %0"
  [(set_attr "type" "int")
   (set_attr "length" "4")])

;; ::::::::::::::::::::
;; ::
;; :: Comparisons
;; ::
;; ::::::::::::::::::::

;; Note, we store the operands in the comparison insns, and use them later
;; when generating the branch or scc operation.

;; First the routines called by the machine independent part of the compiler
(define_expand "cmpsi"
  [(set (cc0)
        (compare (match_operand:SI 0 "integer_register_operand" "")
  		 (match_operand:SI 1 "gpr_or_int10_operand" "")))]
  ""
  "
{
  frv_compare_op0 = operands[0];
  frv_compare_op1 = operands[1];
  DONE;
}")

;(define_expand "cmpdi"
;  [(set (cc0)
;        (compare (match_operand:DI 0 "register_operand" "")
;  		 (match_operand:DI 1 "nonmemory_operand" "")))]
;  ""
;  "
;{
;  frv_compare_op0 = operands[0];
;  frv_compare_op1 = operands[1];
;  DONE;
;}")

(define_expand "cmpsf"
 [(set (cc0)
       (compare (match_operand:SF 0 "fpr_operand" "")
 		 (match_operand:SF 1 "fpr_operand" "")))]
 "TARGET_HARD_FLOAT"
 "
{
  frv_compare_op0 = operands[0];
  frv_compare_op1 = operands[1];
  DONE;
}")

(define_expand "cmpdf"
  [(set (cc0)
        (compare (match_operand:DF 0 "fpr_operand" "")
  		 (match_operand:DF 1 "fpr_operand" "")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "
{
  frv_compare_op0 = operands[0];
  frv_compare_op1 = operands[1];
  DONE;
}")

;; Now, the actual comparisons, generated by the branch and/or scc operations

(define_insn "cmpsi_cc"
  [(set (match_operand:CC 0 "icc_operand" "=t,t")
	(compare:CC (match_operand:SI 1 "integer_register_operand" "d,d")
		    (match_operand:SI 2 "gpr_or_int10_operand" "d,J")))]
  ""
  "cmp%I2 %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "*cmpsi_cc_uns"
  [(set (match_operand:CC_UNS 0 "icc_operand" "=t,t")
	(compare:CC_UNS (match_operand:SI 1 "integer_register_operand" "d,d")
			(match_operand:SI 2 "gpr_or_int10_operand" "d,J")))]
  ""
  "cmp%I2 %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

;; The only requirement for a CC_NZmode GPR or memory value is that
;; comparing it against zero must set the Z and N flags appropriately.
;; The source operand is therefore a valid CC_NZmode value.
(define_insn "*cmpsi_cc_nz"
  [(set (match_operand:CC_NZ 0 "nonimmediate_operand" "=t,d,m")
	(compare:CC_NZ (match_operand:SI 1 "integer_register_operand" "d,d,d")
		       (const_int 0)))]
  ""
  "@
   cmpi %1, #0, %0
   mov %1, %0
   st%I0%U0 %1, %M0"
  [(set_attr "length" "4,4,4")
   (set_attr "type" "int,int,gstore")])

(define_insn "*cmpsf_cc_fp"
  [(set (match_operand:CC_FP 0 "fcc_operand" "=u")
	(compare:CC_FP (match_operand:SF 1 "fpr_operand" "f")
		       (match_operand:SF 2 "fpr_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "fcmps %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fscmp")])

(define_insn "*cmpdf_cc_fp"
  [(set (match_operand:CC_FP 0 "fcc_operand" "=u")
	(compare:CC_FP (match_operand:DF 1 "even_fpr_operand" "h")
		       (match_operand:DF 2 "even_fpr_operand" "h")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "fcmpd %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "fdcmp")])


;; ::::::::::::::::::::
;; ::
;; :: Branches
;; ::
;; ::::::::::::::::::::

;; Define_expands called by the machine independent part of the compiler
;; to allocate a new comparison register.  Each of these named patterns
;; must be present, and they cannot be amalgamated into one pattern.
;;
;; If a fixed condition code register is being used, (as opposed to, say,
;; using cc0), then the expands should look like this:
;;
;; (define_expand "<name_of_test>"
;;   [(set (reg:CC <number_of_CC_register>)
;; 	(compare:CC (match_dup 1)
;; 		    (match_dup 2)))
;;    (set (pc)
;; 	(if_then_else (eq:CC (reg:CC <number_of_CC_register>)
;; 			     (const_int 0))
;; 		      (label_ref (match_operand 0 "" ""))
;; 		      (pc)))]
;;   ""
;;   "{
;;     operands[1] = frv_compare_op0;
;;     operands[2] = frv_compare_op1;
;;   }"
;; )

(define_expand "beq"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  if (! frv_emit_cond_branch (EQ, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "bne"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  if (! frv_emit_cond_branch (NE, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "blt"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  if (! frv_emit_cond_branch (LT, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "ble"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  if (! frv_emit_cond_branch (LE, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "bgt"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  if (! frv_emit_cond_branch (GT, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "bge"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  if (! frv_emit_cond_branch (GE, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "bltu"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  if (! frv_emit_cond_branch (LTU, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "bleu"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  if (! frv_emit_cond_branch (LEU, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "bgtu"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  if (! frv_emit_cond_branch (GTU, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "bgeu"
  [(use (match_operand 0 "" ""))]
  ""
  "
{
  if (! frv_emit_cond_branch (GEU, operands[0]))
    FAIL;

  DONE;
}")

;; Actual branches.  We must allow for the (label_ref) and the (pc) to be
;; swapped.  If they are swapped, it reverses the sense of the branch.
;;
;; Note - unlike the define expands above, these patterns can be amalgamated
;; into one pattern for branch-if-true and one for branch-if-false.  This does
;; require an operand operator to select the correct branch mnemonic.
;;
;; If a fixed condition code register is being used, (as opposed to, say,
;; using cc0), then the expands could look like this:
;;
;; (define_insn "*branch_true"
;;   [(set (pc)
;; 	(if_then_else (match_operator:CC 0 "comparison_operator"
;; 					 [(reg:CC <number_of_CC_register>)
;; 					  (const_int 0)])
;; 		      (label_ref (match_operand 1 "" ""))
;; 		      (pc)))]
;;   ""
;;   "b%B0 %1"
;;   [(set_attr "length" "4")]
;; )
;;
;; In the above example the %B is a directive to frv_print_operand()
;; to decode and print the correct branch mnemonic.

(define_insn "*branch_int_true"
  [(set (pc)
	(if_then_else (match_operator 0 "integer_relational_operator"
				      [(match_operand 1 "icc_operand" "t")
				       (const_int 0)])
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
  "*
{
  if (get_attr_length (insn) == 4)
    return \"b%c0 %1,%#,%l2\";
  else
    return \"b%C0 %1,%#,1f\;call %l2\\n1:\";
}"
  [(set (attr "length")
	(if_then_else
	    (and (ge (minus (match_dup 2) (pc)) (const_int -32768))
		 (le (minus (match_dup 2) (pc)) (const_int 32764)))
	    (const_int 4)
	    (const_int 8)))
   (set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "4")
	    (const_string "no")
	    (const_string "yes")))
   (set (attr "type")
	(if_then_else
	    (eq_attr "length" "4")
	    (const_string "branch")
	    (const_string "multi")))])

(define_insn "*branch_int_false"
  [(set (pc)
	(if_then_else (match_operator 0 "integer_relational_operator"
				      [(match_operand 1 "icc_operand" "t")
				       (const_int 0)])
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
  "*
{
  if (get_attr_length (insn) == 4)
    return \"b%C0 %1,%#,%l2\";
  else
    return \"b%c0 %1,%#,1f\;call %l2\\n1:\";
}"
  [(set (attr "length")
	(if_then_else
	    (and (ge (minus (match_dup 2) (pc)) (const_int -32768))
		 (le (minus (match_dup 2) (pc)) (const_int 32764)))
	    (const_int 4)
	    (const_int 8)))
   (set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "4")
	    (const_string "no")
	    (const_string "yes")))
   (set (attr "type")
	(if_then_else
	    (eq_attr "length" "4")
	    (const_string "branch")
	    (const_string "multi")))])

(define_insn "*branch_fp_true"
  [(set (pc)
	(if_then_else (match_operator:CC_FP 0 "float_relational_operator"
					    [(match_operand 1 "fcc_operand" "u")
					     (const_int 0)])
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
  "*
{
  if (get_attr_length (insn) == 4)
    return \"fb%f0 %1,%#,%l2\";
  else
    return \"fb%F0 %1,%#,1f\;call %l2\\n1:\";
}"
  [(set (attr "length")
	(if_then_else
	    (and (ge (minus (match_dup 2) (pc)) (const_int -32768))
		 (le (minus (match_dup 2) (pc)) (const_int 32764)))
	    (const_int 4)
	    (const_int 8)))
   (set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "4")
	    (const_string "no")
	    (const_string "yes")))
   (set (attr "type")
	(if_then_else
	    (eq_attr "length" "4")
	    (const_string "branch")
	    (const_string "multi")))])

(define_insn "*branch_fp_false"
  [(set (pc)
	(if_then_else (match_operator:CC_FP 0 "float_relational_operator"
					    [(match_operand 1 "fcc_operand" "u")
					     (const_int 0)])
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
  "*
{
  if (get_attr_length (insn) == 4)
    return \"fb%F0 %1,%#,%l2\";
  else
    return \"fb%f0 %1,%#,1f\;call %l2\\n1:\";
}"
  [(set (attr "length")
	(if_then_else
	    (and (ge (minus (match_dup 2) (pc)) (const_int -32768))
		 (le (minus (match_dup 2) (pc)) (const_int 32764)))
	    (const_int 4)
	    (const_int 8)))
   (set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "4")
	    (const_string "no")
	    (const_string "yes")))
   (set (attr "type")
	(if_then_else
	    (eq_attr "length" "4")
	    (const_string "branch")
	    (const_string "multi")))])


;; ::::::::::::::::::::
;; ::
;; :: Set flag operations
;; ::
;; ::::::::::::::::::::

;; Define_expands called by the machine independent part of the compiler
;; to allocate a new comparison register

(define_expand "seq"
  [(match_operand:SI 0 "integer_register_operand" "")]
  "TARGET_SCC"
  "
{
  if (! frv_emit_scc (EQ, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "sne"
  [(match_operand:SI 0 "integer_register_operand" "")]
  "TARGET_SCC"
  "
{
  if (! frv_emit_scc (NE, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "slt"
  [(match_operand:SI 0 "integer_register_operand" "")]
  "TARGET_SCC"
  "
{
  if (! frv_emit_scc (LT, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "sle"
  [(match_operand:SI 0 "integer_register_operand" "")]
  "TARGET_SCC"
  "
{
  if (! frv_emit_scc (LE, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "sgt"
  [(match_operand:SI 0 "integer_register_operand" "")]
  "TARGET_SCC"
  "
{
  if (! frv_emit_scc (GT, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "sge"
  [(match_operand:SI 0 "integer_register_operand" "")]
  "TARGET_SCC"
  "
{
  if (! frv_emit_scc (GE, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "sltu"
  [(match_operand:SI 0 "integer_register_operand" "")]
  "TARGET_SCC"
  "
{
  if (! frv_emit_scc (LTU, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "sleu"
  [(match_operand:SI 0 "integer_register_operand" "")]
  "TARGET_SCC"
  "
{
  if (! frv_emit_scc (LEU, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "sgtu"
  [(match_operand:SI 0 "integer_register_operand" "")]
  "TARGET_SCC"
  "
{
  if (! frv_emit_scc (GTU, operands[0]))
    FAIL;

  DONE;
}")

(define_expand "sgeu"
  [(match_operand:SI 0 "integer_register_operand" "")]
  "TARGET_SCC"
  "
{
  if (! frv_emit_scc (GEU, operands[0]))
    FAIL;

  DONE;
}")

(define_insn "*scc_int"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(match_operator:SI 1 "integer_relational_operator"
			   [(match_operand 2 "icc_operand" "t")
			    (const_int 0)]))
   (clobber (match_operand:CC_CCR 3 "icr_operand" "=v"))]
  ""
  "#"
  [(set_attr "length" "12")
   (set_attr "type" "multi")])

(define_insn "*scc_float"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(match_operator:SI 1 "float_relational_operator"
			   [(match_operand:CC_FP 2 "fcc_operand" "u")
			    (const_int 0)]))
   (clobber (match_operand:CC_CCR 3 "fcr_operand" "=w"))]
  ""
  "#"
  [(set_attr "length" "12")
   (set_attr "type" "multi")])

;; XXX -- add reload_completed to the splits, because register allocation
;; currently isn't ready to see cond_exec packets.
(define_split
  [(set (match_operand:SI 0 "integer_register_operand" "")
	(match_operator:SI 1 "relational_operator"
			   [(match_operand 2 "cc_operand" "")
			    (const_int 0)]))
   (clobber (match_operand 3 "cr_operand" ""))]
  "reload_completed"
  [(match_dup 4)]
  "operands[4] = frv_split_scc (operands[0], operands[1], operands[2],
				operands[3], (HOST_WIDE_INT) 1);")

(define_insn "*scc_neg1_int"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(neg:SI (match_operator:SI 1 "integer_relational_operator"
				   [(match_operand 2 "icc_operand" "t")
				    (const_int 0)])))
   (clobber (match_operand:CC_CCR 3 "icr_operand" "=v"))]
  ""
  "#"
  [(set_attr "length" "12")
   (set_attr "type" "multi")])

(define_insn "*scc_neg1_float"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(neg:SI (match_operator:SI 1 "float_relational_operator"
				   [(match_operand:CC_FP 2 "fcc_operand" "u")
				    (const_int 0)])))
   (clobber (match_operand:CC_CCR 3 "fcr_operand" "=w"))]
  ""
  "#"
  [(set_attr "length" "12")
   (set_attr "type" "multi")])

(define_split
  [(set (match_operand:SI 0 "integer_register_operand" "")
	(neg:SI (match_operator:SI 1 "relational_operator"
				   [(match_operand 2 "cc_operand" "")
				    (const_int 0)])))
   (clobber (match_operand 3 "cr_operand" ""))]
  "reload_completed"
  [(match_dup 4)]
  "operands[4] = frv_split_scc (operands[0], operands[1], operands[2],
				operands[3], (HOST_WIDE_INT) -1);")


;; ::::::::::::::::::::
;; ::
;; :: Conditionally executed instructions
;; ::
;; ::::::::::::::::::::

;; Convert ICC/FCC comparison into CCR bits so we can do conditional execution
(define_insn "*ck_signed"
  [(set (match_operand:CC_CCR 0 "icr_operand" "=v")
	(match_operator:CC_CCR 1 "integer_relational_operator"
			       [(match_operand 2 "icc_operand" "t")
				(const_int 0)]))]
  ""
  "ck%c1 %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "ccr")])

(define_insn "*fck_float"
  [(set (match_operand:CC_CCR 0 "fcr_operand" "=w")
	(match_operator:CC_CCR 1 "float_relational_operator"
			       [(match_operand:CC_FP 2 "fcc_operand" "u")
				(const_int 0)]))]
  "TARGET_HAS_FPRS"
  "fck%c1 %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "ccr")])

;; Conditionally convert ICC/FCC comparison into CCR bits to provide && and ||
;; tests in conditional execution
(define_insn "cond_exec_ck"
  [(set (match_operand:CC_CCR 0 "cr_operand" "=v,w")
	(if_then_else:CC_CCR (match_operator 1 "ccr_eqne_operator"
					     [(match_operand 2 "cr_operand" "C,C")
					      (const_int 0)])
			     (match_operator 3 "relational_operator"
					     [(match_operand 4 "cc_operand" "t,u")
					      (const_int 0)])
			     (const_int 0)))]
  ""
  "@
   cck%c3 %4, %0, %2, %e1
   cfck%f3 %4, %0, %2, %e1"
  [(set_attr "length" "4")
   (set_attr "type" "ccr")])

;; Conditionally set a register to either 0 or another register
(define_insn "*cond_exec_movqi"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C,C,C,C,C,C")
		     (const_int 0)])
    (set (match_operand:QI 2 "condexec_dest_operand" "=d,d,U,?f,?f,?d")
	 (match_operand:QI 3 "condexec_source_operand" "dO,U,dO,f,d,f")))]
  "register_operand(operands[2], QImode) || reg_or_0_operand (operands[3], QImode)"
  "* return output_condmove_single (operands, insn);"
  [(set_attr "length" "4")
   (set_attr "type" "int,gload,gstore,fsconv,movgf,movfg")])

(define_insn "*cond_exec_movhi"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C,C,C,C,C,C")
		     (const_int 0)])
    (set (match_operand:HI 2 "condexec_dest_operand" "=d,d,U,?f,?f,?d")
	 (match_operand:HI 3 "condexec_source_operand" "dO,U,dO,f,d,f")))]
  "register_operand(operands[2], HImode) || reg_or_0_operand (operands[3], HImode)"
  "* return output_condmove_single (operands, insn);"
  [(set_attr "length" "4")
   (set_attr "type" "int,gload,gstore,fsconv,movgf,movfg")])

(define_insn "*cond_exec_movsi"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C,C,C,C,C,C,C,C")
		     (const_int 0)])
    (set (match_operand:SI 2 "condexec_dest_operand" "=d,d,U,?f,?f,?d,?f,?m")
	 (match_operand:SI 3 "condexec_source_operand" "dO,U,dO,f,d,f,m,f")))]
  "register_operand(operands[2], SImode) || reg_or_0_operand (operands[3], SImode)"
  "* return output_condmove_single (operands, insn);"
  [(set_attr "length" "4")
   (set_attr "type" "int,gload,gstore,fsconv,movgf,movfg,fload,fstore")])


(define_insn "*cond_exec_movsf_has_fprs"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C,C,C,C,C,C,C,C,C,C")
		     (const_int 0)])
    (set (match_operand:SF 2 "condexec_dest_operand" "=f,?d,?d,?f,f,f,?d,U,?U,U")
	 (match_operand:SF 3 "condexec_source_operand" "f,d,f,d,G,U,U,f,d,G")))]
  "TARGET_HAS_FPRS"
  "* return output_condmove_single (operands, insn);"
  [(set_attr "length" "4")
   (set_attr "type" "fsconv,int,movgf,movfg,movgf,fload,gload,fstore,gstore,gstore")])

(define_insn "*cond_exec_movsf_no_fprs"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C,C,C")
		     (const_int 0)])
    (set (match_operand:SF 2 "condexec_dest_operand" "=d,d,U")
	 (match_operand:SF 3 "condexec_source_operand" "d,U,dG")))]
  "! TARGET_HAS_FPRS"
  "* return output_condmove_single (operands, insn);"
  [(set_attr "length" "4")
   (set_attr "type" "int,gload,gstore")])

(define_insn "*cond_exec_si_binary1"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SI 2 "integer_register_operand" "=d")
	 (match_operator:SI 3 "condexec_si_binary_operator"
			    [(match_operand:SI 4 "integer_register_operand" "d")
			     (match_operand:SI 5 "integer_register_operand" "d")])))]
  ""
  "*
{
  switch (GET_CODE (operands[3]))
    {
      case PLUS:     return \"cadd %4, %z5, %2, %1, %e0\";
      case MINUS:    return \"csub %4, %z5, %2, %1, %e0\";
      case AND:      return \"cand %4, %z5, %2, %1, %e0\";
      case IOR:      return \"cor %4, %z5, %2, %1, %e0\";
      case XOR:      return \"cxor %4, %z5, %2, %1, %e0\";
      case ASHIFT:   return \"csll %4, %z5, %2, %1, %e0\";
      case ASHIFTRT: return \"csra %4, %z5, %2, %1, %e0\";
      case LSHIFTRT: return \"csrl %4, %z5, %2, %1, %e0\";
      default:       gcc_unreachable ();
    }
}"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "*cond_exec_si_binary2"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SI 2 "fpr_operand" "=f")
	 (match_operator:SI 3 "condexec_si_media_operator"
			    [(match_operand:SI 4 "fpr_operand" "f")
			     (match_operand:SI 5 "fpr_operand" "f")])))]
  "TARGET_MEDIA"
  "*
{
  switch (GET_CODE (operands[3]))
    {
      case AND: return \"cmand %4, %5, %2, %1, %e0\";
      case IOR: return \"cmor %4, %5, %2, %1, %e0\";
      case XOR: return \"cmxor %4, %5, %2, %1, %e0\";
      default:  gcc_unreachable ();
    }
}"
  [(set_attr "length" "4")
   (set_attr "type" "mlogic")])

;; Note, flow does not (currently) know how to handle an operation that uses
;; only part of the hard registers allocated for a multiregister value, such as
;; DImode in this case if the user is only interested in the lower 32-bits.  So
;; we emit a USE of the entire register after the csmul instruction so it won't
;; get confused.  See frv_ifcvt_modify_insn for more details.

(define_insn "*cond_exec_si_smul"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:DI 2 "even_gpr_operand" "=e")
	 (mult:DI (sign_extend:DI (match_operand:SI 3 "integer_register_operand" "%d"))
		  (sign_extend:DI (match_operand:SI 4 "integer_register_operand" "d")))))]
  ""
  "csmul %3, %4, %2, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "mul")])

(define_insn "*cond_exec_si_divide"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SI 2 "integer_register_operand" "=d")
	 (match_operator:SI 3 "condexec_si_divide_operator"
			    [(match_operand:SI 4 "integer_register_operand" "d")
			     (match_operand:SI 5 "integer_register_operand" "d")])))]
  ""
  "*
{
  switch (GET_CODE (operands[3]))
    {
      case DIV:  return \"csdiv %4, %z5, %2, %1, %e0\";
      case UDIV: return \"cudiv %4, %z5, %2, %1, %e0\";
      default:   gcc_unreachable ();
    }
}"
  [(set_attr "length" "4")
   (set_attr "type" "div")])

(define_insn "*cond_exec_si_unary1"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SI 2 "integer_register_operand" "=d")
	 (match_operator:SI 3 "condexec_si_unary_operator"
			    [(match_operand:SI 4 "integer_register_operand" "d")])))]
  ""
  "*
{
  switch (GET_CODE (operands[3]))
    {
      case NOT: return \"cnot %4, %2, %1, %e0\";
      case NEG: return \"csub %., %4, %2, %1, %e0\";
      default:  gcc_unreachable ();
    }
}"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "*cond_exec_si_unary2"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SI 2 "fpr_operand" "=f")
	 (not:SI (match_operand:SI 3 "fpr_operand" "f"))))]
  "TARGET_MEDIA"
  "cmnot %3, %2, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "mlogic")])

(define_insn "*cond_exec_cmpsi_cc"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:CC 2 "icc_operand" "=t")
	 (compare:CC (match_operand:SI 3 "integer_register_operand" "d")
		     (match_operand:SI 4 "reg_or_0_operand" "dO"))))]
  "reload_completed
   && REGNO (operands[1]) == REGNO (operands[2]) - ICC_FIRST + ICR_FIRST"
  "ccmp %3, %z4, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "*cond_exec_cmpsi_cc_uns"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:CC_UNS 2 "icc_operand" "=t")
	 (compare:CC_UNS (match_operand:SI 3 "integer_register_operand" "d")
			 (match_operand:SI 4 "reg_or_0_operand" "dO"))))]
  "reload_completed
   && REGNO (operands[1]) == REGNO (operands[2]) - ICC_FIRST + ICR_FIRST"
  "ccmp %3, %z4, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "*cond_exec_cmpsi_cc_nz"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:CC_NZ 2 "icc_operand" "=t")
	 (compare:CC_NZ (match_operand:SI 3 "integer_register_operand" "d")
			(const_int 0))))]
  "reload_completed
   && REGNO (operands[1]) == REGNO (operands[2]) - ICC_FIRST + ICR_FIRST"
  "ccmp %3, %., %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "*cond_exec_sf_conv"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SF 2 "fpr_operand" "=f")
	 (match_operator:SF 3 "condexec_sf_conv_operator"
			    [(match_operand:SF 4 "fpr_operand" "f")])))]
  "TARGET_HARD_FLOAT"
  "*
{
  switch (GET_CODE (operands[3]))
    {
      case ABS: return \"cfabss %4, %2, %1, %e0\";
      case NEG: return \"cfnegs %4, %2, %1, %e0\";
      default:  gcc_unreachable ();
    }
}"
  [(set_attr "length" "4")
   (set_attr "type" "fsconv")])

(define_insn "*cond_exec_sf_add"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SF 2 "fpr_operand" "=f")
	 (match_operator:SF 3 "condexec_sf_add_operator"
			    [(match_operand:SF 4 "fpr_operand" "f")
			     (match_operand:SF 5 "fpr_operand" "f")])))]
  "TARGET_HARD_FLOAT"
  "*
{
  switch (GET_CODE (operands[3]))
    {
      case PLUS:  return \"cfadds %4, %5, %2, %1, %e0\";
      case MINUS: return \"cfsubs %4, %5, %2, %1, %e0\";
      default:    gcc_unreachable ();
    }
}"
  [(set_attr "length" "4")
   (set_attr "type" "fsadd")])

(define_insn "*cond_exec_sf_mul"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SF 2 "fpr_operand" "=f")
	 (mult:SF (match_operand:SF 3 "fpr_operand" "f")
		  (match_operand:SF 4 "fpr_operand" "f"))))]
  "TARGET_HARD_FLOAT"
  "cfmuls %3, %4, %2, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "fsmul")])

(define_insn "*cond_exec_sf_div"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SF 2 "fpr_operand" "=f")
	 (div:SF (match_operand:SF 3 "fpr_operand" "f")
		 (match_operand:SF 4 "fpr_operand" "f"))))]
  "TARGET_HARD_FLOAT"
  "cfdivs %3, %4, %2, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "fsdiv")])

(define_insn "*cond_exec_sf_sqrt"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SF 2 "fpr_operand" "=f")
	 (sqrt:SF (match_operand:SF 3 "fpr_operand" "f"))))]
  "TARGET_HARD_FLOAT"
  "cfsqrts %3, %2, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "fsdiv")])

(define_insn "*cond_exec_cmpsi_cc_fp"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:CC_FP 2 "fcc_operand" "=u")
	 (compare:CC_FP (match_operand:SF 3 "fpr_operand" "f")
			(match_operand:SF 4 "fpr_operand" "f"))))]
  "reload_completed && TARGET_HARD_FLOAT
   && REGNO (operands[1]) == REGNO (operands[2]) - FCC_FIRST + FCR_FIRST"
  "cfcmps %3, %4, %2, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "fsconv")])


;; ::::::::::::::::::::
;; ::
;; :: Logical operations on CR registers
;; ::
;; ::::::::::::::::::::

;; We use UNSPEC to encode andcr/iorcr/etc. rather than the normal RTL
;; operations, since the RTL operations only have an idea of TRUE and FALSE,
;; while the CRs have TRUE, FALSE, and UNDEFINED.

(define_expand "andcr"
  [(set (match_operand:CC_CCR 0 "cr_operand" "")
	(unspec:CC_CCR [(match_operand:CC_CCR 1 "cr_operand" "")
			(match_operand:CC_CCR 2 "cr_operand" "")
			(const_int 0)] UNSPEC_CR_LOGIC))]
  ""
  "")

(define_expand "orcr"
  [(set (match_operand:CC_CCR 0 "cr_operand" "")
	(unspec:CC_CCR [(match_operand:CC_CCR 1 "cr_operand" "")
			(match_operand:CC_CCR 2 "cr_operand" "")
			(const_int 1)] UNSPEC_CR_LOGIC))]
  ""
  "")

(define_expand "xorcr"
  [(set (match_operand:CC_CCR 0 "cr_operand" "")
	(unspec:CC_CCR [(match_operand:CC_CCR 1 "cr_operand" "")
			(match_operand:CC_CCR 2 "cr_operand" "")
			(const_int 2)] UNSPEC_CR_LOGIC))]
  ""
  "")

(define_expand "nandcr"
  [(set (match_operand:CC_CCR 0 "cr_operand" "")
	(unspec:CC_CCR [(match_operand:CC_CCR 1 "cr_operand" "")
			(match_operand:CC_CCR 2 "cr_operand" "")
			(const_int 3)] UNSPEC_CR_LOGIC))]
  ""
  "")

(define_expand "norcr"
  [(set (match_operand:CC_CCR 0 "cr_operand" "")
	(unspec:CC_CCR [(match_operand:CC_CCR 1 "cr_operand" "")
			(match_operand:CC_CCR 2 "cr_operand" "")
			(const_int 4)] UNSPEC_CR_LOGIC))]
  ""
  "")

(define_expand "andncr"
  [(set (match_operand:CC_CCR 0 "cr_operand" "")
	(unspec:CC_CCR [(match_operand:CC_CCR 1 "cr_operand" "")
			(match_operand:CC_CCR 2 "cr_operand" "")
			(const_int 5)] UNSPEC_CR_LOGIC))]
  ""
  "")

(define_expand "orncr"
  [(set (match_operand:CC_CCR 0 "cr_operand" "")
	(unspec:CC_CCR [(match_operand:CC_CCR 1 "cr_operand" "")
			(match_operand:CC_CCR 2 "cr_operand" "")
			(const_int 6)] UNSPEC_CR_LOGIC))]
  ""
  "")

(define_expand "nandncr"
  [(set (match_operand:CC_CCR 0 "cr_operand" "")
	(unspec:CC_CCR [(match_operand:CC_CCR 1 "cr_operand" "")
			(match_operand:CC_CCR 2 "cr_operand" "")
			(const_int 7)] UNSPEC_CR_LOGIC))]
  ""
  "")

(define_expand "norncr"
  [(set (match_operand:CC_CCR 0 "cr_operand" "")
	(unspec:CC_CCR [(match_operand:CC_CCR 1 "cr_operand" "")
			(match_operand:CC_CCR 2 "cr_operand" "")
			(const_int 8)] UNSPEC_CR_LOGIC))]
  ""
  "")

(define_expand "notcr"
  [(set (match_operand:CC_CCR 0 "cr_operand" "")
	(unspec:CC_CCR [(match_operand:CC_CCR 1 "cr_operand" "")
			(match_dup 1)
			(const_int 9)] UNSPEC_CR_LOGIC))]
  ""
  "")

(define_insn "*logical_cr"
  [(set (match_operand:CC_CCR 0 "cr_operand" "=C")
	(unspec:CC_CCR [(match_operand:CC_CCR 1 "cr_operand" "C")
			(match_operand:CC_CCR 2 "cr_operand" "C")
			(match_operand:SI 3 "const_int_operand" "n")]
		       UNSPEC_CR_LOGIC))]
  ""
  "*
{
  switch (INTVAL (operands[3]))
  {
  default: break;
  case 0: return \"andcr %1, %2, %0\";
  case 1: return \"orcr %1, %2, %0\";
  case 2: return \"xorcr %1, %2, %0\";
  case 3: return \"nandcr %1, %2, %0\";
  case 4: return \"norcr %1, %2, %0\";
  case 5: return \"andncr %1, %2, %0\";
  case 6: return \"orncr %1, %2, %0\";
  case 7: return \"nandncr %1, %2, %0\";
  case 8: return \"norncr %1, %2, %0\";
  case 9: return \"notcr %1, %0\";
  }

  fatal_insn (\"logical_cr\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "ccr")])


;; ::::::::::::::::::::
;; ::
;; :: Conditional move instructions
;; ::
;; ::::::::::::::::::::


;; - conditional moves based on floating-point comparisons require
;;   TARGET_HARD_FLOAT, because an FPU is required to do the comparison.

;; - conditional moves between FPRs based on integer comparisons
;;   require TARGET_HAS_FPRS.

(define_expand "movqicc"
  [(set (match_operand:QI 0 "integer_register_operand" "")
	(if_then_else:QI (match_operand 1 "" "")
			 (match_operand:QI 2 "gpr_or_int_operand" "")
			 (match_operand:QI 3 "gpr_or_int_operand" "")))]
  "TARGET_COND_MOVE"
  "
{
  if (!frv_emit_cond_move (operands[0], operands[1], operands[2], operands[3]))
    FAIL;

  DONE;
}")

(define_insn "*movqicc_internal1_int"
  [(set (match_operand:QI 0 "integer_register_operand" "=d,d,d")
	(if_then_else:QI (match_operator 1 "integer_relational_operator"
			     [(match_operand 2 "icc_operand" "t,t,t")
			      (const_int 0)])
			 (match_operand:QI 3 "reg_or_0_operand" "0,dO,dO")
			 (match_operand:QI 4 "reg_or_0_operand" "dO,0,dO")))
   (clobber (match_operand:CC_CCR 5 "icr_operand" "=v,v,v"))]
  ""
  "#"
  [(set_attr "length" "8,8,12")
   (set_attr "type" "multi")])

(define_insn "*movqicc_internal1_float"
  [(set (match_operand:QI 0 "integer_register_operand" "=d,d,d")
	(if_then_else:QI (match_operator:CC_FP 1 "float_relational_operator"
			     [(match_operand:CC_FP 2 "fcc_operand" "u,u,u")
			      (const_int 0)])
			 (match_operand:QI 3 "reg_or_0_operand" "0,dO,dO")
			 (match_operand:QI 4 "reg_or_0_operand" "dO,0,dO")))
   (clobber (match_operand:CC_CCR 5 "fcr_operand" "=w,w,w"))]
  "TARGET_HARD_FLOAT"
  "#"
  [(set_attr "length" "8,8,12")
   (set_attr "type" "multi")])

(define_insn "*movqicc_internal2_int"
  [(set (match_operand:QI 0 "integer_register_operand" "=d,d,d,d,d")
	(if_then_else:QI (match_operator 1 "integer_relational_operator"
			     [(match_operand 2 "icc_operand" "t,t,t,t,t")
			      (const_int 0)])
			 (match_operand:QI 3 "const_int_operand" "O,O,L,n,n")
			 (match_operand:QI 4 "const_int_operand" "L,n,O,O,n")))
   (clobber (match_operand:CC_CCR 5 "icr_operand" "=v,v,v,v,v"))]
  "(INTVAL (operands[3]) == 0
    || INTVAL (operands[4]) == 0
    || (IN_RANGE_P (INTVAL (operands[3]), -2048, 2047)
        && IN_RANGE_P (INTVAL (operands[4]) - INTVAL (operands[3]), -2048, 2047)))"
  "#"
  [(set_attr "length" "8,12,8,12,12")
   (set_attr "type" "multi")])

(define_insn "*movqicc_internal2_float"
  [(set (match_operand:QI 0 "integer_register_operand" "=d,d,d,d,d")
	(if_then_else:QI (match_operator:CC_FP 1 "float_relational_operator"
			     [(match_operand:CC_FP 2 "fcc_operand" "u,u,u,u,u")
			      (const_int 0)])
			 (match_operand:QI 3 "const_int_operand" "O,O,L,n,n")
			 (match_operand:QI 4 "const_int_operand" "L,n,O,O,n")))
   (clobber (match_operand:CC_CCR 5 "fcr_operand" "=w,w,w,w,w"))]
  "TARGET_HARD_FLOAT
   && (INTVAL (operands[3]) == 0
       || INTVAL (operands[4]) == 0
       || (IN_RANGE_P (INTVAL (operands[3]), -2048, 2047)
	   && IN_RANGE_P (INTVAL (operands[4]) - INTVAL (operands[3]), -2048, 2047)))"
  "#"
  [(set_attr "length" "8,12,8,12,12")
   (set_attr "type" "multi")])

(define_split
  [(set (match_operand:QI 0 "integer_register_operand" "")
	(if_then_else:QI (match_operator 1 "relational_operator"
			     [(match_operand 2 "cc_operand" "")
			      (const_int 0)])
			 (match_operand:QI 3 "gpr_or_int_operand" "")
			 (match_operand:QI 4 "gpr_or_int_operand" "")))
   (clobber (match_operand:CC_CCR 5 "cr_operand" ""))]
  "reload_completed"
  [(match_dup 6)]
  "operands[6] = frv_split_cond_move (operands);")

(define_expand "movhicc"
  [(set (match_operand:HI 0 "integer_register_operand" "")
	(if_then_else:HI (match_operand 1 "" "")
			 (match_operand:HI 2 "gpr_or_int_operand" "")
			 (match_operand:HI 3 "gpr_or_int_operand" "")))]
  "TARGET_COND_MOVE"
  "
{
  if (!frv_emit_cond_move (operands[0], operands[1], operands[2], operands[3]))
    FAIL;

  DONE;
}")

(define_insn "*movhicc_internal1_int"
  [(set (match_operand:HI 0 "integer_register_operand" "=d,d,d")
	(if_then_else:HI (match_operator 1 "integer_relational_operator"
			     [(match_operand 2 "icc_operand" "t,t,t")
			      (const_int 0)])
			 (match_operand:HI 3 "reg_or_0_operand" "0,dO,dO")
			 (match_operand:HI 4 "reg_or_0_operand" "dO,0,dO")))
   (clobber (match_operand:CC_CCR 5 "icr_operand" "=v,v,v"))]
  ""
  "#"
  [(set_attr "length" "8,8,12")
   (set_attr "type" "multi")])

(define_insn "*movhicc_internal1_float"
  [(set (match_operand:HI 0 "integer_register_operand" "=d,d,d")
	(if_then_else:HI (match_operator:CC_FP 1 "float_relational_operator"
			     [(match_operand:CC_FP 2 "fcc_operand" "u,u,u")
			      (const_int 0)])
			 (match_operand:HI 3 "reg_or_0_operand" "0,dO,dO")
			 (match_operand:HI 4 "reg_or_0_operand" "dO,0,dO")))
   (clobber (match_operand:CC_CCR 5 "fcr_operand" "=w,w,w"))]
  "TARGET_HARD_FLOAT"
  "#"
  [(set_attr "length" "8,8,12")
   (set_attr "type" "multi")])

(define_insn "*movhicc_internal2_int"
  [(set (match_operand:HI 0 "integer_register_operand" "=d,d,d,d,d")
	(if_then_else:HI (match_operator 1 "integer_relational_operator"
			     [(match_operand 2 "icc_operand" "t,t,t,t,t")
			      (const_int 0)])
			 (match_operand:HI 3 "const_int_operand" "O,O,L,n,n")
			 (match_operand:HI 4 "const_int_operand" "L,n,O,O,n")))
   (clobber (match_operand:CC_CCR 5 "icr_operand" "=v,v,v,v,v"))]
  "(INTVAL (operands[3]) == 0
    || INTVAL (operands[4]) == 0
    || (IN_RANGE_P (INTVAL (operands[3]), -2048, 2047)
        && IN_RANGE_P (INTVAL (operands[4]) - INTVAL (operands[3]), -2048, 2047)))"
  "#"
  [(set_attr "length" "8,12,8,12,12")
   (set_attr "type" "multi")])

(define_insn "*movhicc_internal2_float"
  [(set (match_operand:HI 0 "integer_register_operand" "=d,d,d,d,d")
	(if_then_else:HI (match_operator:CC_FP 1 "float_relational_operator"
			     [(match_operand:CC_FP 2 "fcc_operand" "u,u,u,u,u")
			      (const_int 0)])
			 (match_operand:HI 3 "const_int_operand" "O,O,L,n,n")
			 (match_operand:HI 4 "const_int_operand" "L,n,O,O,n")))
   (clobber (match_operand:CC_CCR 5 "fcr_operand" "=w,w,w,w,w"))]
  "TARGET_HARD_FLOAT
   && (INTVAL (operands[3]) == 0
       || INTVAL (operands[4]) == 0
       || (IN_RANGE_P (INTVAL (operands[3]), -2048, 2047)
	   && IN_RANGE_P (INTVAL (operands[4]) - INTVAL (operands[3]), -2048, 2047)))"
  "#"
  [(set_attr "length" "8,12,8,12,12")
   (set_attr "type" "multi")])

(define_split
  [(set (match_operand:HI 0 "integer_register_operand" "")
	(if_then_else:HI (match_operator 1 "relational_operator"
			     [(match_operand 2 "cc_operand" "")
			      (const_int 0)])
			 (match_operand:HI 3 "gpr_or_int_operand" "")
			 (match_operand:HI 4 "gpr_or_int_operand" "")))
   (clobber (match_operand:CC_CCR 5 "cr_operand" ""))]
  "reload_completed"
  [(match_dup 6)]
  "operands[6] = frv_split_cond_move (operands);")

(define_expand "movsicc"
  [(set (match_operand:SI 0 "integer_register_operand" "")
	(if_then_else:SI (match_operand 1 "" "")
			 (match_operand:SI 2 "gpr_or_int_operand" "")
			 (match_operand:SI 3 "gpr_or_int_operand" "")))]
  "TARGET_COND_MOVE"
  "
{
  if (!frv_emit_cond_move (operands[0], operands[1], operands[2], operands[3]))
    FAIL;

  DONE;
}")

(define_insn "*movsicc_internal1_int"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d,d")
	(if_then_else:SI (match_operator 1 "integer_relational_operator"
			     [(match_operand 2 "icc_operand" "t,t,t")
			      (const_int 0)])
			 (match_operand:SI 3 "reg_or_0_operand" "0,dO,dO")
			 (match_operand:SI 4 "reg_or_0_operand" "dO,0,dO")))
   (clobber (match_operand:CC_CCR 5 "icr_operand" "=v,v,v"))]
  ""
  "#"
  [(set_attr "length" "8,8,12")
   (set_attr "type" "multi")])

(define_insn "*movsicc_internal1_float"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d,d")
	(if_then_else:SI (match_operator:CC_FP 1 "float_relational_operator"
			     [(match_operand:CC_FP 2 "fcc_operand" "u,u,u")
			      (const_int 0)])
			 (match_operand:SI 3 "reg_or_0_operand" "0,dO,dO")
			 (match_operand:SI 4 "reg_or_0_operand" "dO,0,dO")))
   (clobber (match_operand:CC_CCR 5 "fcr_operand" "=w,w,w"))]
  "TARGET_HARD_FLOAT"
  "#"
  [(set_attr "length" "8,8,12")
   (set_attr "type" "multi")])

(define_insn "*movsicc_internal2_int"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d,d,d,d")
	(if_then_else:SI (match_operator 1 "integer_relational_operator"
			     [(match_operand 2 "icc_operand" "t,t,t,t,t")
			      (const_int 0)])
			 (match_operand:SI 3 "const_int_operand" "O,O,L,n,n")
			 (match_operand:SI 4 "const_int_operand" "L,n,O,O,n")))
   (clobber (match_operand:CC_CCR 5 "icr_operand" "=v,v,v,v,v"))]
  "(INTVAL (operands[3]) == 0
    || INTVAL (operands[4]) == 0
    || (IN_RANGE_P (INTVAL (operands[3]), -2048, 2047)
        && IN_RANGE_P (INTVAL (operands[4]) - INTVAL (operands[3]), -2048, 2047)))"
  "#"
  [(set_attr "length" "8,12,8,12,12")
   (set_attr "type" "multi")])

(define_insn "*movsicc_internal2_float"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d,d,d,d")
	(if_then_else:SI (match_operator:CC_FP 1 "float_relational_operator"
			     [(match_operand:CC_FP 2 "fcc_operand" "u,u,u,u,u")
			      (const_int 0)])
			 (match_operand:SI 3 "const_int_operand" "O,O,L,n,n")
			 (match_operand:SI 4 "const_int_operand" "L,n,O,O,n")))
   (clobber (match_operand:CC_CCR 5 "fcr_operand" "=w,w,w,w,w"))]
  "TARGET_HARD_FLOAT
   && (INTVAL (operands[3]) == 0
       || INTVAL (operands[4]) == 0
       || (IN_RANGE_P (INTVAL (operands[3]), -2048, 2047)
	   && IN_RANGE_P (INTVAL (operands[4]) - INTVAL (operands[3]), -2048, 2047)))"
  "#"
  [(set_attr "length" "8,12,8,12,12")
   (set_attr "type" "multi")])

(define_split
  [(set (match_operand:SI 0 "integer_register_operand" "")
	(if_then_else:SI (match_operator 1 "relational_operator"
			     [(match_operand 2 "cc_operand" "")
			      (const_int 0)])
			 (match_operand:SI 3 "gpr_or_int_operand" "")
			 (match_operand:SI 4 "gpr_or_int_operand" "")))
   (clobber (match_operand:CC_CCR 5 "cr_operand" ""))]
  "reload_completed"
  [(match_dup 6)]
  "operands[6] = frv_split_cond_move (operands);")

(define_expand "movsfcc"
  [(set (match_operand:SF 0 "register_operand" "")
	(if_then_else:SF (match_operand 1 "" "")
			 (match_operand:SF 2 "register_operand" "")
			 (match_operand:SF 3 "register_operand" "")))]
  "TARGET_COND_MOVE"
  "
{
  if (!frv_emit_cond_move (operands[0], operands[1], operands[2], operands[3]))
    FAIL;

  DONE;
}")

(define_insn "*movsfcc_has_fprs_int"
  [(set (match_operand:SF 0 "register_operand" "=f,f,f,?f,?f,?d")
	(if_then_else:SF (match_operator 1 "integer_relational_operator"
			     [(match_operand 2 "icc_operand" "t,t,t,t,t,t")
			      (const_int 0)])
			 (match_operand:SF 3 "register_operand" "0,f,f,f,d,fd")
			 (match_operand:SF 4 "register_operand" "f,0,f,d,fd,fd")))
   (clobber (match_operand:CC_CCR 5 "icr_operand" "=v,v,v,v,v,v"))]
  "TARGET_HAS_FPRS"
  "#"
  [(set_attr "length" "8,8,12,12,12,12")
   (set_attr "type" "multi")])

(define_insn "*movsfcc_hardfloat_float"
  [(set (match_operand:SF 0 "register_operand" "=f,f,f,?f,?f,?d")
	(if_then_else:SF (match_operator:CC_FP 1 "float_relational_operator"
			     [(match_operand:CC_FP 2 "fcc_operand" "u,u,u,u,u,u")
			      (const_int 0)])
			 (match_operand:SF 3 "register_operand" "0,f,f,f,d,fd")
			 (match_operand:SF 4 "register_operand" "f,0,f,d,fd,fd")))
   (clobber (match_operand:CC_CCR 5 "fcr_operand" "=w,w,w,w,w,w"))]
  "TARGET_HARD_FLOAT"
  "#"
  [(set_attr "length" "8,8,12,12,12,12")
   (set_attr "type" "multi")])

(define_insn "*movsfcc_no_fprs_int"
  [(set (match_operand:SF 0 "integer_register_operand" "=d,d,d")
	(if_then_else:SF (match_operator 1 "integer_relational_operator"
			     [(match_operand 2 "icc_operand" "t,t,t")
			      (const_int 0)])
			 (match_operand:SF 3 "integer_register_operand" "0,d,d")
			 (match_operand:SF 4 "integer_register_operand" "d,0,d")))
   (clobber (match_operand:CC_CCR 5 "icr_operand" "=v,v,v"))]
  "! TARGET_HAS_FPRS"
  "#"
  [(set_attr "length" "8,8,12")
   (set_attr "type" "multi")])

(define_split
  [(set (match_operand:SF 0 "register_operand" "")
	(if_then_else:SF (match_operator 1 "relational_operator"
			     [(match_operand 2 "cc_operand" "")
			      (const_int 0)])
			 (match_operand:SF 3 "register_operand" "")
			 (match_operand:SF 4 "register_operand" "")))
   (clobber (match_operand:CC_CCR 5 "cr_operand" ""))]
  "reload_completed"
  [(match_dup 6)]
  "operands[6] = frv_split_cond_move (operands);")


;; ::::::::::::::::::::
;; ::
;; :: Minimum, maximum, and integer absolute value
;; ::
;; ::::::::::::::::::::

;; These 'instructions' are provided to give the compiler a slightly better
;; nudge at register allocation, then it would if it constructed the
;; instructions from basic building blocks (since it indicates it prefers one
;; of the operands to be the same as the destination.  It also helps the
;; earlier passes of the compiler, by not breaking things into small basic
;; blocks.

(define_expand "abssi2"
  [(parallel [(set (match_operand:SI 0 "integer_register_operand" "")
		   (abs:SI (match_operand:SI 1 "integer_register_operand" "")))
	      (clobber (match_dup 2))
	      (clobber (match_dup 3))])]
  "TARGET_COND_MOVE"
  "
{
  operands[2] = gen_reg_rtx (CCmode);
  operands[3] = gen_reg_rtx (CC_CCRmode);
}")

(define_insn_and_split "*abssi2_internal"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d")
	(abs:SI (match_operand:SI 1 "integer_register_operand" "0,d")))
   (clobber (match_operand:CC 2 "icc_operand" "=t,t"))
   (clobber (match_operand:CC_CCR 3 "icr_operand" "=v,v"))]
  "TARGET_COND_MOVE"
  "#"
  "reload_completed"
  [(match_dup 4)]
  "operands[4] = frv_split_abs (operands);"
  [(set_attr "length" "12,16")
   (set_attr "type" "multi")])

(define_expand "sminsi3"
  [(parallel [(set (match_operand:SI 0 "integer_register_operand" "")
		   (smin:SI (match_operand:SI 1 "integer_register_operand" "")
			    (match_operand:SI 2 "gpr_or_int10_operand" "")))
	      (clobber (match_dup 3))
	      (clobber (match_dup 4))])]
  "TARGET_COND_MOVE"
  "
{
  operands[3] = gen_reg_rtx (CCmode);
  operands[4] = gen_reg_rtx (CC_CCRmode);
}")

(define_expand "smaxsi3"
  [(parallel [(set (match_operand:SI 0 "integer_register_operand" "")
		   (smax:SI (match_operand:SI 1 "integer_register_operand" "")
			    (match_operand:SI 2 "gpr_or_int10_operand" "")))
	      (clobber (match_dup 3))
	      (clobber (match_dup 4))])]
  "TARGET_COND_MOVE"
  "
{
  operands[3] = gen_reg_rtx (CCmode);
  operands[4] = gen_reg_rtx (CC_CCRmode);
}")

(define_insn_and_split "*minmax_si_signed"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d,&d")
	(match_operator:SI 1 "minmax_operator"
			   [(match_operand:SI 2 "integer_register_operand" "%0,dO,d")
			    (match_operand:SI 3 "gpr_or_int10_operand" "dO,0,dJ")]))
   (clobber (match_operand:CC 4 "icc_operand" "=t,t,t"))
   (clobber (match_operand:CC_CCR 5 "icr_operand" "=v,v,v"))]
  "TARGET_COND_MOVE"
  "#"
  "reload_completed"
  [(match_dup 6)]
  "operands[6] = frv_split_minmax (operands);"
  [(set_attr "length" "12,12,16")
   (set_attr "type" "multi")])

(define_expand "uminsi3"
  [(parallel [(set (match_operand:SI 0 "integer_register_operand" "")
		   (umin:SI (match_operand:SI 1 "integer_register_operand" "")
			    (match_operand:SI 2 "gpr_or_int10_operand" "")))
	      (clobber (match_dup 3))
	      (clobber (match_dup 4))])]
  "TARGET_COND_MOVE"
  "
{
  operands[3] = gen_reg_rtx (CC_UNSmode);
  operands[4] = gen_reg_rtx (CC_CCRmode);
}")

(define_expand "umaxsi3"
  [(parallel [(set (match_operand:SI 0 "integer_register_operand" "")
		   (umax:SI (match_operand:SI 1 "integer_register_operand" "")
			    (match_operand:SI 2 "gpr_or_int10_operand" "")))
	      (clobber (match_dup 3))
	      (clobber (match_dup 4))])]
  "TARGET_COND_MOVE"
  "
{
  operands[3] = gen_reg_rtx (CC_UNSmode);
  operands[4] = gen_reg_rtx (CC_CCRmode);
}")

(define_insn_and_split "*minmax_si_unsigned"
  [(set (match_operand:SI 0 "integer_register_operand" "=d,d,&d")
	(match_operator:SI 1 "minmax_operator"
			   [(match_operand:SI 2 "integer_register_operand" "%0,dO,d")
			    (match_operand:SI 3 "gpr_or_int10_operand" "dO,0,dJ")]))
   (clobber (match_operand:CC_UNS 4 "icc_operand" "=t,t,t"))
   (clobber (match_operand:CC_CCR 5 "icr_operand" "=v,v,v"))]
  "TARGET_COND_MOVE"
  "#"
  "reload_completed"
  [(match_dup 6)]
  "operands[6] = frv_split_minmax (operands);"
  [(set_attr "length" "12,12,16")
   (set_attr "type" "multi")])

(define_expand "sminsf3"
  [(parallel [(set (match_operand:SF 0 "fpr_operand" "")
		   (smin:SF (match_operand:SF 1 "fpr_operand" "")
			    (match_operand:SF 2 "fpr_operand" "")))
	      (clobber (match_dup 3))
	      (clobber (match_dup 4))])]
  "TARGET_COND_MOVE && TARGET_HARD_FLOAT"
  "
{
  operands[3] = gen_reg_rtx (CC_FPmode);
  operands[4] = gen_reg_rtx (CC_CCRmode);
}")

(define_expand "smaxsf3"
  [(parallel [(set (match_operand:SF 0 "fpr_operand" "")
		   (smax:SF (match_operand:SF 1 "fpr_operand" "")
			    (match_operand:SF 2 "fpr_operand" "")))
	      (clobber (match_dup 3))
	      (clobber (match_dup 4))])]
  "TARGET_COND_MOVE && TARGET_HARD_FLOAT"
  "
{
  operands[3] = gen_reg_rtx (CC_FPmode);
  operands[4] = gen_reg_rtx (CC_CCRmode);
}")

(define_insn_and_split "*minmax_sf"
  [(set (match_operand:SF 0 "fpr_operand" "=f,f,f")
	(match_operator:SF 1 "minmax_operator"
			   [(match_operand:SF 2 "fpr_operand" "%0,f,f")
			    (match_operand:SF 3 "fpr_operand" "f,0,f")]))
   (clobber (match_operand:CC_FP 4 "fcc_operand" "=u,u,u"))
   (clobber (match_operand:CC_CCR 5 "fcr_operand" "=w,w,w"))]
  "TARGET_COND_MOVE && TARGET_HARD_FLOAT"
  "#"
  "reload_completed"
  [(match_dup 6)]
  "operands[6] = frv_split_minmax (operands);"
  [(set_attr "length" "12,12,16")
   (set_attr "type" "multi")])

(define_expand "smindf3"
  [(parallel [(set (match_operand:DF 0 "fpr_operand" "")
		   (smin:DF (match_operand:DF 1 "fpr_operand" "")
			    (match_operand:DF 2 "fpr_operand" "")))
	      (clobber (match_dup 3))
	      (clobber (match_dup 4))])]
  "TARGET_COND_MOVE && TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "
{
  operands[3] = gen_reg_rtx (CC_FPmode);
  operands[4] = gen_reg_rtx (CC_CCRmode);
}")

(define_expand "smaxdf3"
  [(parallel [(set (match_operand:DF 0 "fpr_operand" "")
		   (smax:DF (match_operand:DF 1 "fpr_operand" "")
			    (match_operand:DF 2 "fpr_operand" "")))
	      (clobber (match_dup 3))
	      (clobber (match_dup 4))])]
  "TARGET_COND_MOVE && TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "
{
  operands[3] = gen_reg_rtx (CC_FPmode);
  operands[4] = gen_reg_rtx (CC_CCRmode);
}")

(define_insn_and_split "*minmax_df"
  [(set (match_operand:DF 0 "fpr_operand" "=f,f,f")
	(match_operator:DF 1 "minmax_operator"
			   [(match_operand:DF 2 "fpr_operand" "%0,f,f")
			    (match_operand:DF 3 "fpr_operand" "f,0,f")]))
   (clobber (match_operand:CC_FP 4 "fcc_operand" "=u,u,u"))
   (clobber (match_operand:CC_CCR 5 "fcr_operand" "=w,w,w"))]
  "TARGET_COND_MOVE && TARGET_HARD_FLOAT && TARGET_DOUBLE"
  "#"
  "reload_completed"
  [(match_dup 6)]
  "operands[6] = frv_split_minmax (operands);"
  [(set_attr "length" "12,12,16")
   (set_attr "type" "multi")])


;; ::::::::::::::::::::
;; ::
;; :: Call and branch instructions
;; ::
;; ::::::::::::::::::::

;; Subroutine call instruction returning no value.  Operand 0 is the function
;; to call; operand 1 is the number of bytes of arguments pushed (in mode
;; `SImode', except it is normally a `const_int'); operand 2 is the number of
;; registers used as operands.

;; On most machines, operand 2 is not actually stored into the RTL pattern.  It
;; is supplied for the sake of some RISC machines which need to put this
;; information into the assembler code; they can put it in the RTL instead of
;; operand 1.

(define_expand "call"
  [(use (match_operand:QI 0 "" ""))
   (use (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (use (match_operand 3 "" ""))]
  ""
  "
{
  rtx lr = gen_rtx_REG (Pmode, LR_REGNO);
  rtx addr;

  gcc_assert (GET_CODE (operands[0]) == MEM);

  addr = XEXP (operands[0], 0);
  if (! call_operand (addr, Pmode))
    addr = force_reg (Pmode, addr);

  if (! operands[2])
    operands[2] = const0_rtx;

  if (TARGET_FDPIC)
    frv_expand_fdpic_call (operands, false, false);
  else
    emit_call_insn (gen_call_internal (addr, operands[1], operands[2], lr));

  DONE;
}")

(define_insn "call_internal"
  [(call (mem:QI (match_operand:SI 0 "call_operand" "S,dNOP"))
	 (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (clobber (match_operand:SI 3 "lr_operand" "=l,l"))]
  "! TARGET_FDPIC"
  "@
   call %0
   call%i0l %M0"
  [(set_attr "length" "4")
   (set_attr "type" "call,jumpl")])

;; The odd use of GR0 within the UNSPEC below prevents cseing or
;; hoisting function descriptor loads out of loops.  This is almost
;; never desirable, since if we preserve the function descriptor in a
;; pair of registers, it takes two insns to move it to gr14/gr15, and
;; if it's in the stack, we just waste space with the store, since
;; we'll have to load back from memory anyway.  And, in the worst
;; case, we may end up reusing a function descriptor still pointing at
;; a PLT entry, instead of to the resolved function, which means going
;; through the resolver for every call that uses the outdated value.
;; Bad!

;; The explicit MEM inside the SPEC prevents the compiler from moving
;; the load before a branch after a NULL test, or before a store that
;; initializes a function descriptor.

(define_insn "movdi_ldd"
  [(set (match_operand:DI 0 "fdpic_fptr_operand" "=e")
	(unspec:DI [(mem:DI (match_operand:SI 1 "ldd_address_operand" "p"))
		    (reg:SI 0)] UNSPEC_LDD))]
  ""
  "ldd%I1 %M1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "gload")])

(define_insn "call_fdpicdi"
  [(call (mem:QI (match_operand:DI 0 "fdpic_fptr_operand" "W"))
	 (match_operand 1 "" ""))
   (clobber (match_operand:SI 2 "lr_operand" "=l"))]
  "TARGET_FDPIC"
  "call%i0l %M0"
  [(set_attr "length" "4")
   (set_attr "type" "jumpl")])

(define_insn "call_fdpicsi"
  [(call (mem:QI (match_operand:SI 0 "call_operand" "S,dNOP"))
	 (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (use (match_operand:SI 3 "fdpic_operand" "Z,Z"))
   (clobber (match_operand:SI 4 "lr_operand" "=l,l"))]
  "TARGET_FDPIC"
  "@
   call %0
   call%i0l %M0"
  [(set_attr "length" "4")
   (set_attr "type" "call,jumpl")])

(define_expand "sibcall"
  [(use (match_operand:QI 0 "" ""))
   (use (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (use (match_operand 3 "" ""))]
  ""
  "
{
  rtx addr;

  gcc_assert (GET_CODE (operands[0]) == MEM);

  addr = XEXP (operands[0], 0);
  if (! sibcall_operand (addr, Pmode))
    addr = force_reg (Pmode, addr);

  if (! operands[2])
    operands[2] = const0_rtx;

  if (TARGET_FDPIC)
    frv_expand_fdpic_call (operands, false, true);
  else
    emit_call_insn (gen_sibcall_internal (addr, operands[1], operands[2]));

  DONE;
}")
  
;; It might seem that these sibcall patterns are missing references to
;; LR, but they're not necessary because sibcall_epilogue will make
;; sure LR is restored, and having LR here will set
;; regs_ever_used[REG_LR], forcing it to be saved on the stack, and
;; then restored in sibcalls and regular return code paths, even if
;; the function becomes a leaf function after tail-call elimination.

;; We must not use a call-saved register here.  `W' limits ourselves
;; to gr14 or gr15, but since we're almost running out of constraint
;; letters, and most other call-clobbered registers are often used for
;; argument-passing, this will do.
(define_insn "sibcall_internal"
  [(call (mem:QI (match_operand:SI 0 "sibcall_operand" "WNOP"))
	 (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (return)]
  "! TARGET_FDPIC"
  "jmp%i0l %M0"
  [(set_attr "length" "4")
   (set_attr "type" "jumpl")])

(define_insn "sibcall_fdpicdi"
  [(call (mem:QI (match_operand:DI 0 "fdpic_fptr_operand" "W"))
	 (match_operand 1 "" ""))
   (return)]
  "TARGET_FDPIC"
  "jmp%i0l %M0"
  [(set_attr "length" "4")
   (set_attr "type" "jumpl")])


;; Subroutine call instruction returning a value.  Operand 0 is the hard
;; register in which the value is returned.  There are three more operands, the
;; same as the three operands of the `call' instruction (but with numbers
;; increased by one).

;; Subroutines that return `BLKmode' objects use the `call' insn.

(define_expand "call_value"
  [(use (match_operand 0 "" ""))
   (use (match_operand:QI 1 "" ""))
   (use (match_operand 2 "" ""))
   (use (match_operand 3 "" ""))
   (use (match_operand 4 "" ""))]
  ""
  "
{
  rtx lr = gen_rtx_REG (Pmode, LR_REGNO);
  rtx addr;

  gcc_assert (GET_CODE (operands[1]) == MEM);

  addr = XEXP (operands[1], 0);
  if (! call_operand (addr, Pmode))
    addr = force_reg (Pmode, addr);

  if (! operands[3])
    operands[3] = const0_rtx;

  if (TARGET_FDPIC)
    frv_expand_fdpic_call (operands, true, false);
  else
    emit_call_insn (gen_call_value_internal (operands[0], addr, operands[2],
					     operands[3], lr));

  DONE;
}")

(define_insn "call_value_internal"
  [(set (match_operand 0 "register_operand" "=d,d")
	(call (mem:QI (match_operand:SI 1 "call_operand" "S,dNOP"))
		      (match_operand 2 "" "")))
   (use (match_operand 3 "" ""))
   (clobber (match_operand:SI 4 "lr_operand" "=l,l"))]
  "! TARGET_FDPIC"
  "@
   call %1
   call%i1l %M1"
  [(set_attr "length" "4")
   (set_attr "type" "call,jumpl")])

(define_insn "call_value_fdpicdi"
  [(set (match_operand 0 "register_operand" "=d")
	(call (mem:QI (match_operand:DI 1 "fdpic_fptr_operand" "W"))
	      (match_operand 2 "" "")))
   (clobber (match_operand:SI 3 "lr_operand" "=l"))]
  "TARGET_FDPIC"
  "call%i1l %M1"
  [(set_attr "length" "4")
   (set_attr "type" "jumpl")])

(define_insn "call_value_fdpicsi"
  [(set (match_operand 0 "register_operand" "=d,d")
	(call (mem:QI (match_operand:SI 1 "call_operand" "S,dNOP"))
		      (match_operand 2 "" "")))
   (use (match_operand 3 "" ""))
   (use (match_operand:SI 4 "fdpic_operand" "Z,Z"))
   (clobber (match_operand:SI 5 "lr_operand" "=l,l"))]
  "TARGET_FDPIC"
  "@
   call %1
   call%i1l %M1"
  [(set_attr "length" "4")
   (set_attr "type" "call,jumpl")])

(define_expand "sibcall_value"
  [(use (match_operand 0 "" ""))
   (use (match_operand:QI 1 "" ""))
   (use (match_operand 2 "" ""))
   (use (match_operand 3 "" ""))
   (use (match_operand 4 "" ""))]
  ""
  "
{
  rtx addr;

  gcc_assert (GET_CODE (operands[1]) == MEM);

  addr = XEXP (operands[1], 0);
  if (! sibcall_operand (addr, Pmode))
    addr = force_reg (Pmode, addr);

  if (! operands[3])
    operands[3] = const0_rtx;

  if (TARGET_FDPIC)
    frv_expand_fdpic_call (operands, true, true);
  else
    emit_call_insn (gen_sibcall_value_internal (operands[0], addr, operands[2],
						operands[3]));
  DONE;
}")

(define_insn "sibcall_value_internal"
  [(set (match_operand 0 "register_operand" "=d")
	(call (mem:QI (match_operand:SI 1 "sibcall_operand" "WNOP"))
		      (match_operand 2 "" "")))
   (use (match_operand 3 "" ""))
   (return)]
  "! TARGET_FDPIC"
  "jmp%i1l %M1"
  [(set_attr "length" "4")
   (set_attr "type" "jumpl")])

(define_insn "sibcall_value_fdpicdi"
  [(set (match_operand 0 "register_operand" "=d")
	(call (mem:QI (match_operand:DI 1 "fdpic_fptr_operand" "W"))
	      (match_operand 2 "" "")))
   (return)]
  "TARGET_FDPIC"
  "jmp%i1l %M1"
  [(set_attr "length" "4")
   (set_attr "type" "jumpl")])

;; return instruction generated instead of jmp to epilog
(define_expand "return"
  [(parallel [(return)
	      (use (match_dup 0))
	      (use (const_int 1))])]
  "direct_return_p ()"
  "
{
  operands[0] = gen_rtx_REG (Pmode, LR_REGNO);
}")

;; return instruction generated by the epilogue
(define_expand "epilogue_return"
  [(parallel [(return)
	      (use (match_operand:SI 0 "register_operand" ""))
	      (use (const_int 0))])]
  ""
  "")

(define_insn "*return_internal"
  [(return)
   (use (match_operand:SI 0 "register_operand" "l,d"))
   (use (match_operand:SI 1 "immediate_operand" "n,n"))]
  ""
  "@
    ret
    jmpl @(%0,%.)"
  [(set_attr "length" "4")
   (set_attr "type" "jump,jumpl")])

(define_insn "*return_true"
  [(set (pc)
	(if_then_else (match_operator 0 "integer_relational_operator"
				      [(match_operand 1 "icc_operand" "t")
				       (const_int 0)])
		      (return)
		      (pc)))]
  "direct_return_p ()"
  "b%c0lr %1,%#"
  [(set_attr "length" "4")
   (set_attr "type" "jump")])

(define_insn "*return_false"
  [(set (pc)
	(if_then_else (match_operator 0 "integer_relational_operator"
				      [(match_operand 1 "icc_operand" "t")
				       (const_int 0)])
		      (pc)
		      (return)))]
  "direct_return_p ()"
  "b%C0lr %1,%#"
  [(set_attr "length" "4")
   (set_attr "type" "jump")])

;; A version of addsi3 for deallocating stack space at the end of the
;; epilogue.  The addition is done in parallel with an (unspec_volatile),
;; which represents the clobbering of the deallocated space.
(define_insn "stack_adjust"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "d")
		 (match_operand:SI 2 "general_operand" "dNOP")))
   (unspec_volatile [(const_int 0)] UNSPEC_STACK_ADJUST)]
  ""
  "add%I2 %1,%2,%0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

;; Normal unconditional jump

;; Use the "call" instruction for long branches, but prefer to use "bra" for
;; short ones since it does not force us to save the link register.

;; This define_insn uses the branch-shortening code to decide which
;; instruction it emits.  Since the main branch-shortening interface is
;; through get_attr_length(), the two alternatives must be given different
;; lengths.  Here we pretend that the far jump is 8 rather than 4 bytes
;; long, though both alternatives are really the same size.
(define_insn "jump"
  [(set (pc) (label_ref (match_operand 0 "" "")))]
  ""
  "*
{
  if (get_attr_length (insn) == 4)
    return \"bra %l0\";
  else
    return \"call %l0\";
}"
  [(set (attr "length")
        (if_then_else
	    (and (ge (minus (match_dup 0) (pc)) (const_int -32768))
		 (le (minus (match_dup 0) (pc)) (const_int 32764)))
	    (const_int 4)
	    (const_int 8)))
   (set (attr "far_jump")
        (if_then_else
	    (eq_attr "length" "4")
	    (const_string "no")
	    (const_string "yes")))
   (set (attr "type")
	(if_then_else
	    (eq_attr "length" "4")
	    (const_string "jump")
	    (const_string "call")))])

;; Indirect jump through a register
(define_insn "indirect_jump"
  [(set (pc) (match_operand:SI 0 "register_operand" "d,l"))]
  ""
  "@
   jmpl @(%0,%.)
   bralr"
  [(set_attr "length" "4")
   (set_attr "type" "jumpl,branch")])

;; Instruction to jump to a variable address.  This is a low-level capability
;; which can be used to implement a dispatch table when there is no `casesi'
;; pattern.  Either the 'casesi' pattern or the 'tablejump' pattern, or both,
;; MUST be present in this file.

;; This pattern requires two operands: the address or offset, and a label which
;; should immediately precede the jump table.  If the macro
;; `CASE_VECTOR_PC_RELATIVE' is defined then the first operand is an offset
;; which counts from the address of the table; otherwise, it is an absolute
;; address to jump to.  In either case, the first operand has mode `Pmode'.

;; The `tablejump' insn is always the last insn before the jump table it uses.
;; Its assembler code normally has no need to use the second operand, but you
;; should incorporate it in the RTL pattern so that the jump optimizer will not
;; delete the table as unreachable code.

(define_expand "tablejump"
  [(parallel [(set (pc) (match_operand:SI 0 "address_operand" "p"))
	      (use (label_ref (match_operand 1 "" "")))])]
  "!flag_pic"
  "")

(define_insn "tablejump_insn"
  [(set (pc) (match_operand:SI 0 "address_operand" "p"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
  "jmp%I0l %M0"
  [(set_attr "length" "4")
   (set_attr "type" "jumpl")])

;; Implement switch statements when generating PIC code.  Switches are
;; implemented by `tablejump' when not using -fpic.

;; Emit code here to do the range checking and make the index zero based.
;; operand 0 is the index
;; operand 1 is the lower bound
;; operand 2 is the range of indices (highest - lowest + 1)
;; operand 3 is the label that precedes the table itself
;; operand 4 is the fall through label

(define_expand "casesi"
  [(use (match_operand:SI 0 "integer_register_operand" ""))
   (use (match_operand:SI 1 "const_int_operand" ""))
   (use (match_operand:SI 2 "const_int_operand" ""))
   (use (match_operand 3 "" ""))
   (use (match_operand 4 "" ""))]
  "flag_pic"
  "
{
  rtx indx;
  rtx scale;
  rtx low = operands[1];
  rtx range = operands[2];
  rtx table = operands[3];
  rtx treg;
  rtx fail = operands[4];
  rtx mem;
  rtx reg2;
  rtx reg3;

  gcc_assert (GET_CODE (operands[1]) == CONST_INT);

  gcc_assert (GET_CODE (operands[2]) == CONST_INT);

  /* If we can't generate an immediate instruction, promote to register.  */
  if (! IN_RANGE_P (INTVAL (range), -2048, 2047))
    range = force_reg (SImode, range);

  /* If low bound is 0, we don't have to subtract it.  */
  if (INTVAL (operands[1]) == 0)
    indx = operands[0];
  else
    {
      indx = gen_reg_rtx (SImode);
      if (IN_RANGE_P (INTVAL (low), -2047, 2048))
	emit_insn (gen_addsi3 (indx, operands[0], GEN_INT (- INTVAL (low))));
      else
	emit_insn (gen_subsi3 (indx, operands[0], force_reg (SImode, low)));
    }

  /* Do an unsigned comparison (in the proper mode) between the index
     expression and the value which represents the length of the range.
     Since we just finished subtracting the lower bound of the range
     from the index expression, this comparison allows us to simultaneously
     check that the original index expression value is both greater than
     or equal to the minimum value of the range and less than or equal to
     the maximum value of the range.  */

  emit_cmp_and_jump_insns (indx, range, GTU, NULL_RTX, SImode, 1, fail);

  /* Move the table address to a register.  */
  treg = gen_reg_rtx (Pmode);
  emit_insn (gen_movsi (treg, gen_rtx_LABEL_REF (VOIDmode, table)));

  /* Scale index-low by wordsize.  */
  scale = gen_reg_rtx (SImode);
  emit_insn (gen_ashlsi3 (scale, indx, const2_rtx));

  /* Load the address, add the start of the table back in,
     and jump to it.  */
  mem = gen_rtx_MEM (SImode, gen_rtx_PLUS (Pmode, scale, treg));
  reg2 = gen_reg_rtx (SImode);
  reg3 = gen_reg_rtx (SImode);
  emit_insn (gen_movsi (reg2, mem));
  emit_insn (gen_addsi3 (reg3, reg2, treg));
  emit_jump_insn (gen_tablejump_insn (reg3, table));
  DONE;
}")


;; ::::::::::::::::::::
;; ::
;; :: Prologue and Epilogue instructions
;; ::
;; ::::::::::::::::::::

;; Called after register allocation to add any instructions needed for the
;; prologue.  Using a prologue insn is favored compared to putting all of the
;; instructions in the FUNCTION_PROLOGUE macro, since it allows the scheduler
;; to intermix instructions with the saves of the caller saved registers.  In
;; some cases, it might be necessary to emit a barrier instruction as the last
;; insn to prevent such scheduling.
(define_expand "prologue"
  [(const_int 1)]
  ""
  "
{
  frv_expand_prologue ();
  DONE;
}")

;; Called after register allocation to add any instructions needed for the
;; epilogue.  Using an epilogue insn is favored compared to putting all of the
;; instructions in the FUNCTION_EPILOGUE macro, since it allows the scheduler
;; to intermix instructions with the restores of the caller saved registers.
;; In some cases, it might be necessary to emit a barrier instruction as the
;; first insn to prevent such scheduling.
(define_expand "epilogue"
  [(const_int 2)]
  ""
  "
{
  frv_expand_epilogue (true);
  DONE;
}")

;; This pattern, if defined, emits RTL for exit from a function without the final
;; branch back to the calling function.  This pattern will be emitted before any
;; sibling call (aka tail call) sites.
;;
;; The sibcall_epilogue pattern must not clobber any arguments used for
;; parameter passing or any stack slots for arguments passed to the current
;; function.
(define_expand "sibcall_epilogue"
  [(const_int 3)]
  ""
  "
{
  frv_expand_epilogue (false);
  DONE;
}")

;; Set up the pic register to hold the address of the pic table
(define_insn "pic_prologue"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
        (unspec_volatile:SI [(const_int 0)] UNSPEC_PIC_PROLOGUE))
   (clobber (match_operand:SI 1 "lr_operand" "=l"))
   (clobber (match_operand:SI 2 "integer_register_operand" "=d"))]
  ""
  "*
{
  static int frv_pic_labelno = 0;

  operands[3] = GEN_INT (frv_pic_labelno++);
  return \"call %P3\\n%P3:\;movsg %1, %0\;sethi #gprelhi(%P3), %2\;setlo #gprello(%P3), %2\;sub %0,%2,%0\";
}"
  [(set_attr "length" "16")
   (set_attr "type" "multi")])

;; ::::::::::::::::::::
;; ::
;; :: Miscellaneous instructions
;; ::
;; ::::::::::::::::::::

;; No operation, needed in case the user uses -g but not -O.
(define_insn "nop"
  [(const_int 0)]
  ""
  "nop"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "fnop"
  [(const_int 1)]
  ""
  "fnop"
  [(set_attr "length" "4")
   (set_attr "type" "fnop")])

(define_insn "mnop"
  [(const_int 2)]
  ""
  "mnop"
  [(set_attr "length" "4")
   (set_attr "type" "mnop")])

;; Pseudo instruction that prevents the scheduler from moving code above this
;; point.  Note, type unknown is used to make sure the VLIW instructions are
;; not continued past this point.
(define_insn "blockage"
  [(unspec_volatile [(const_int 0)] UNSPEC_BLOCKAGE)]
  ""
  "# blockage"
  [(set_attr "length" "0")
   (set_attr "type" "unknown")])

;; ::::::::::::::::::::
;; ::
;; :: Media instructions
;; ::
;; ::::::::::::::::::::

;; Unimplemented instructions:
;;   - MCMPSH, MCMPUH

(define_constants
  [(UNSPEC_MLOGIC		100)
   (UNSPEC_MNOT			101)
   (UNSPEC_MAVEH		102)
   (UNSPEC_MSATH		103)
   (UNSPEC_MADDH		104)
   (UNSPEC_MQADDH		105)
   (UNSPEC_MPACKH		106)
   (UNSPEC_MUNPACKH		107)
   (UNSPEC_MDPACKH		108)
   (UNSPEC_MBTOH		109)
   (UNSPEC_MHTOB		110)
   (UNSPEC_MROT			111)
   (UNSPEC_MSHIFT		112)
   (UNSPEC_MEXPDHW		113)
   (UNSPEC_MEXPDHD		114)
   (UNSPEC_MWCUT		115)
   (UNSPEC_MMULH		116)
   (UNSPEC_MMULXH		117)
   (UNSPEC_MMACH		118)
   (UNSPEC_MMRDH		119)
   (UNSPEC_MQMULH		120)
   (UNSPEC_MQMULXH		121)
   (UNSPEC_MQMACH		122)
   (UNSPEC_MCPX			123)
   (UNSPEC_MQCPX		124)
   (UNSPEC_MCUT			125)
   (UNSPEC_MRDACC		126)
   (UNSPEC_MRDACCG		127)
   (UNSPEC_MWTACC		128)
   (UNSPEC_MWTACCG		129)
   (UNSPEC_MTRAP		130)
   (UNSPEC_MCLRACC		131)
   (UNSPEC_MCLRACCA		132)
   (UNSPEC_MCOP1		133)
   (UNSPEC_MCOP2		134)
   (UNSPEC_MDUNPACKH		135)
   (UNSPEC_MDUNPACKH_INTERNAL	136)
   (UNSPEC_MBTOHE		137)
   (UNSPEC_MBTOHE_INTERNAL	138)
   (UNSPEC_MBTOHE		137)
   (UNSPEC_MBTOHE_INTERNAL	138)
   (UNSPEC_MQMACH2		139)
   (UNSPEC_MADDACC		140)
   (UNSPEC_MDADDACC		141)
   (UNSPEC_MABSHS		142)
   (UNSPEC_MDROTLI		143)
   (UNSPEC_MCPLHI		144)
   (UNSPEC_MCPLI		145)
   (UNSPEC_MDCUTSSI		146)
   (UNSPEC_MQSATHS		147)
   (UNSPEC_MHSETLOS		148)
   (UNSPEC_MHSETLOH		149)
   (UNSPEC_MHSETHIS		150)
   (UNSPEC_MHSETHIH		151)
   (UNSPEC_MHDSETS		152)
   (UNSPEC_MHDSETH		153)
   (UNSPEC_MQLCLRHS		154)
   (UNSPEC_MQLMTHS		155)
   (UNSPEC_MQSLLHI		156)
   (UNSPEC_MQSRAHI		157)
   (UNSPEC_MASACCS		158)
   (UNSPEC_MDASACCS		159)
])

;; Logic operations: type "mlogic"

(define_expand "mand"
  [(set (match_operand:SI 0 "fpr_operand" "")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "")
		    (match_operand:SI 2 "fpr_operand" "")
		    (match_dup 3)]
		   UNSPEC_MLOGIC))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MAND);")

(define_expand "mor"
  [(set (match_operand:SI 0 "fpr_operand" "")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "")
		    (match_operand:SI 2 "fpr_operand" "")
		    (match_dup 3)]
		   UNSPEC_MLOGIC))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MOR);")

(define_expand "mxor"
  [(set (match_operand:SI 0 "fpr_operand" "")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "")
		    (match_operand:SI 2 "fpr_operand" "")
		    (match_dup 3)]
		   UNSPEC_MLOGIC))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MXOR);")

(define_insn "*mlogic"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
		    (match_operand:SI 2 "fpr_operand" "f")
		    (match_operand:SI 3 "const_int_operand" "n")]
		   UNSPEC_MLOGIC))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[3]))
  {
  default:		 break;
  case FRV_BUILTIN_MAND: return \"mand %1, %2, %0\";
  case FRV_BUILTIN_MOR:  return \"mor %1, %2, %0\";
  case FRV_BUILTIN_MXOR: return \"mxor %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mlogic\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mlogic")])

(define_insn "*cond_exec_mlogic"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SI 2 "fpr_operand" "=f")
         (unspec:SI [(match_operand:SI 3 "fpr_operand" "f")
		     (match_operand:SI 4 "fpr_operand" "f")
		     (match_operand:SI 5 "const_int_operand" "n")]
		    UNSPEC_MLOGIC)))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[5]))
  {
  default:		    break;
  case FRV_BUILTIN_MAND: return \"cmand %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MOR:  return \"cmor %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MXOR: return \"cmxor %3, %4, %2, %1, %e0\";
  }

  fatal_insn (\"Bad media insn, cond_exec_mlogic\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mlogic")])

;; Logical not: type "mlogic"

(define_insn "mnot"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")] UNSPEC_MNOT))]
  "TARGET_MEDIA"
  "mnot %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mlogic")])

(define_insn "*cond_exec_mnot"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SI 2 "fpr_operand" "=f")
         (unspec:SI [(match_operand:SI 3 "fpr_operand" "f")] UNSPEC_MNOT)))]
  "TARGET_MEDIA"
  "cmnot %3, %2, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "mlogic")])

;; Dual average (halfword): type "maveh"

(define_insn "maveh"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")]
		   UNSPEC_MAVEH))]
  "TARGET_MEDIA"
  "maveh %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "maveh")])

;; Dual saturation (halfword): type "msath"

(define_expand "msaths"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")
		    (match_dup 3)]
                   UNSPEC_MSATH))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MSATHS);")

(define_expand "msathu"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")
		    (match_dup 3)]
                   UNSPEC_MSATH))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MSATHU);")

(define_insn "*msath"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")
		    (match_operand:SI 3 "const_int_operand" "n")]
		   UNSPEC_MSATH))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[3]))
  {
  default:		    break;
  case FRV_BUILTIN_MSATHS:  return \"msaths %1, %2, %0\";
  case FRV_BUILTIN_MSATHU:  return \"msathu %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, msath\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "msath")])

;; Dual addition/subtraction with saturation (halfword): type "maddh"

(define_expand "maddhss"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")
		    (match_dup 3)]
		   UNSPEC_MADDH))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MADDHSS);")

(define_expand "maddhus"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")
		    (match_dup 3)]
                   UNSPEC_MADDH))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MADDHUS);")

(define_expand "msubhss"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")
		    (match_dup 3)]
                   UNSPEC_MADDH))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MSUBHSS);")

(define_expand "msubhus"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")
		    (match_dup 3)]
                   UNSPEC_MADDH))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MSUBHUS);")

(define_insn "*maddh"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")
		    (match_operand:SI 3 "const_int_operand" "n")]
		   UNSPEC_MADDH))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[3]))
  {
  default:		    break;
  case FRV_BUILTIN_MADDHSS: return \"maddhss %1, %2, %0\";
  case FRV_BUILTIN_MADDHUS: return \"maddhus %1, %2, %0\";
  case FRV_BUILTIN_MSUBHSS: return \"msubhss %1, %2, %0\";
  case FRV_BUILTIN_MSUBHUS: return \"msubhus %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, maddh\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "maddh")])

(define_insn "*cond_exec_maddh"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SI 2 "fpr_operand" "=f")
	 (unspec:SI [(match_operand:SI 3 "fpr_operand" "f")
		     (match_operand:SI 4 "fpr_operand" "f")
		     (match_operand:SI 5 "const_int_operand" "n")]
		    UNSPEC_MADDH)))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[5]))
  {
  default:		    break;
  case FRV_BUILTIN_MADDHSS: return \"cmaddhss %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MADDHUS: return \"cmaddhus %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MSUBHSS: return \"cmsubhss %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MSUBHUS: return \"cmsubhus %3, %4, %2, %1, %e0\";
  }

  fatal_insn (\"Bad media insn, cond_exec_maddh\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "maddh")])

;; Quad addition/subtraction with saturation (halfword): type "mqaddh"

(define_expand "mqaddhss"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:DI 1 "even_fpr_operand" "h")
                    (match_operand:DI 2 "even_fpr_operand" "h")
		    (match_dup 3)]
		   UNSPEC_MQADDH))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MQADDHSS);")

(define_expand "mqaddhus"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:DI 1 "even_fpr_operand" "h")
                    (match_operand:DI 2 "even_fpr_operand" "h")
		    (match_dup 3)]
		   UNSPEC_MQADDH))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MQADDHUS);")

(define_expand "mqsubhss"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:DI 1 "even_fpr_operand" "h")
                    (match_operand:DI 2 "even_fpr_operand" "h")
		    (match_dup 3)]
		   UNSPEC_MQADDH))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MQSUBHSS);")

(define_expand "mqsubhus"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:DI 1 "even_fpr_operand" "h")
                    (match_operand:DI 2 "even_fpr_operand" "h")
		    (match_dup 3)]
		   UNSPEC_MQADDH))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MQSUBHUS);")

(define_insn "*mqaddh"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:DI 1 "even_fpr_operand" "h")
                    (match_operand:DI 2 "even_fpr_operand" "h")
		    (match_operand:SI 3 "const_int_operand" "n")]
		   UNSPEC_MQADDH))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[3]))
  {
  default:		     break;
  case FRV_BUILTIN_MQADDHSS: return \"mqaddhss %1, %2, %0\";
  case FRV_BUILTIN_MQADDHUS: return \"mqaddhus %1, %2, %0\";
  case FRV_BUILTIN_MQSUBHSS: return \"mqsubhss %1, %2, %0\";
  case FRV_BUILTIN_MQSUBHUS: return \"mqsubhus %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mqaddh\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mqaddh")])

(define_insn "*cond_exec_mqaddh"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:DI 2 "even_fpr_operand" "=h")
         (unspec:DI [(match_operand:DI 3 "even_fpr_operand" "h")
                     (match_operand:DI 4 "even_fpr_operand" "h")
		     (match_operand:SI 5 "const_int_operand" "n")]
		    UNSPEC_MQADDH)))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[5]))
  {
  default:		     break;
  case FRV_BUILTIN_MQADDHSS: return \"cmqaddhss %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MQADDHUS: return \"cmqaddhus %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MQSUBHSS: return \"cmqsubhss %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MQSUBHUS: return \"cmqsubhus %3, %4, %2, %1, %e0\";
  }

  fatal_insn (\"Bad media insn, cond_exec_mqaddh\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mqaddh")])

;; Pack halfword: type "mpackh"

(define_insn "mpackh"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:HI 1 "fpr_operand" "f")
                    (match_operand:HI 2 "fpr_operand" "f")]
		   UNSPEC_MPACKH))]
  "TARGET_MEDIA"
  "mpackh %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mpackh")])

;; Unpack halfword: type "mpackh"

(define_insn "munpackh"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:SI 1 "fpr_operand" "f")]
		   UNSPEC_MUNPACKH))]
  "TARGET_MEDIA"
  "munpackh %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "munpackh")])

;; Dual pack halfword: type "mdpackh"

(define_insn "mdpackh"
    [(set (match_operand:DI 0 "even_fpr_operand" "=h")
	  (unspec:DI [(match_operand:DI 1 "even_fpr_operand" "h")
		      (match_operand:DI 2 "even_fpr_operand" "h")]
		     UNSPEC_MDPACKH))]
  "TARGET_MEDIA"
  "mdpackh %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mdpackh")])

;; Byte-halfword conversion: type "mbhconv"

(define_insn "mbtoh"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:SI 1 "fpr_operand" "f")]
		   UNSPEC_MBTOH))]
  "TARGET_MEDIA"
  "mbtoh %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mbhconv")])

(define_insn "*cond_exec_mbtoh"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:DI 2 "even_fpr_operand" "=h")
	 (unspec:DI [(match_operand:SI 3 "fpr_operand" "f")]
		    UNSPEC_MBTOH)))]
  "TARGET_MEDIA"
  "cmbtoh %3, %2, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "mbhconv")])

(define_insn "mhtob"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:DI 1 "even_fpr_operand" "h")]
		   UNSPEC_MHTOB))]
  "TARGET_MEDIA"
  "mhtob %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mbhconv")])

(define_insn "*cond_exec_mhtob"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SI 2 "fpr_operand" "=f")
	 (unspec:SI [(match_operand:DI 3 "even_fpr_operand" "h")]
		    UNSPEC_MHTOB)))]
  "TARGET_MEDIA"
  "cmhtob %3, %2, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "mbhconv")])

;; Rotate: type "mrot"

(define_expand "mrotli"
  [(set (match_operand:SI 0 "fpr_operand" "")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "")
                    (match_operand:SI 2 "uint5_operand" "")
		    (match_dup 3)]
		   UNSPEC_MROT))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MROTLI);")

(define_expand "mrotri"
  [(set (match_operand:SI 0 "fpr_operand" "")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "")
                    (match_operand:SI 2 "uint5_operand" "")
		    (match_dup 3)]
		   UNSPEC_MROT))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MROTRI);")

(define_insn "*mrot"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "uint5_operand" "I")
		    (match_operand:SI 3 "const_int_operand" "n")]
		   UNSPEC_MROT))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[3]))
  {
  default:		   break;
  case FRV_BUILTIN_MROTLI: return \"mrotli %1, %2, %0\";
  case FRV_BUILTIN_MROTRI: return \"mrotri %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mrot\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mrot")])

;; Dual shift halfword: type "msh"

(define_expand "msllhi"
  [(set (match_operand:SI 0 "fpr_operand" "")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "")
                    (match_operand:SI 2 "uint4_operand" "")
		    (match_dup 3)]
		   UNSPEC_MSHIFT))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MSLLHI);")

(define_expand "msrlhi"
  [(set (match_operand:SI 0 "fpr_operand" "")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "")
                    (match_operand:SI 2 "uint4_operand" "")
		    (match_dup 3)]
		   UNSPEC_MSHIFT))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MSRLHI);")

(define_expand "msrahi"
  [(set (match_operand:SI 0 "fpr_operand" "")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "")
                    (match_operand:SI 2 "uint4_operand" "")
		    (match_dup 3)]
		   UNSPEC_MSHIFT))]
  "TARGET_MEDIA"
  "operands[3] = GEN_INT (FRV_BUILTIN_MSRAHI);")

(define_insn "*mshift"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "uint4_operand" "I")
		    (match_operand:SI 3 "const_int_operand" "n")]
		   UNSPEC_MSHIFT))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[3]))
  {
  default:		   break;
  case FRV_BUILTIN_MSLLHI: return \"msllhi %1, %2, %0\";
  case FRV_BUILTIN_MSRLHI: return \"msrlhi %1, %2, %0\";
  case FRV_BUILTIN_MSRAHI: return \"msrahi %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mshift\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mshift")])

;; Expand halfword to word: type "mexpdhw"

(define_insn "mexpdhw"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "uint1_operand" "I")]
		   UNSPEC_MEXPDHW))]
  "TARGET_MEDIA"
  "mexpdhw %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mexpdhw")])

(define_insn "*cond_exec_mexpdhw"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:SI 2 "fpr_operand" "=f")
	 (unspec:SI [(match_operand:SI 3 "fpr_operand" "f")
		     (match_operand:SI 4 "uint1_operand" "I")]
		    UNSPEC_MEXPDHW)))]
  "TARGET_MEDIA"
  "cmexpdhw %3, %4, %2, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "mexpdhw")])

;; Expand halfword to double: type "mexpdhd"

(define_insn "mexpdhd"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "uint1_operand" "I")]
		   UNSPEC_MEXPDHD))]
  "TARGET_MEDIA"
  "mexpdhd %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mexpdhd")])

(define_insn "*cond_exec_mexpdhd"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (set (match_operand:DI 2 "even_fpr_operand" "=h")
	 (unspec:DI [(match_operand:SI 3 "fpr_operand" "f")
		     (match_operand:SI 4 "uint1_operand" "I")]
		    UNSPEC_MEXPDHD)))]
  "TARGET_MEDIA"
  "cmexpdhd %3, %4, %2, %1, %e0"
  [(set_attr "length" "4")
   (set_attr "type" "mexpdhd")])

;; FR cut: type "mwcut"

(define_insn "mwcut"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:DI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_or_int6_operand" "fI")]
		   UNSPEC_MWCUT))]
  "TARGET_MEDIA"
  "mwcut%i2 %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mwcut")])

;; Dual multiplication (halfword): type "mmulh"

(define_expand "mmulhs"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "=b")
		   (unspec:DI [(match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_dup 4)]
			      UNSPEC_MMULH))
	      (set (match_operand:HI 3 "accg_operand" "=B")
		   (unspec:HI [(const_int 0)] UNSPEC_MMULH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MMULHS);")

(define_expand "mmulhu"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "=b")
		   (unspec:DI [(match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_dup 4)]
			      UNSPEC_MMULH))
	      (set (match_operand:HI 3 "accg_operand" "=B")
		   (unspec:HI [(const_int 0)] UNSPEC_MMULH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MMULHU);")

(define_insn "*mmulh"
  [(set (match_operand:DI 0 "even_acc_operand" "=b")
        (unspec:DI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")
		    (match_operand:SI 3 "const_int_operand" "n")]
		   UNSPEC_MMULH))
   (set (match_operand:HI 4 "accg_operand" "=B")
	(unspec:HI [(const_int 0)] UNSPEC_MMULH))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[3]))
  {
  default:		    break;
  case FRV_BUILTIN_MMULHS:  return \"mmulhs %1, %2, %0\";
  case FRV_BUILTIN_MMULHU:  return \"mmulhu %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mmulh\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mmulh")])

(define_insn "*cond_exec_mmulh"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (parallel [(set (match_operand:DI 2 "even_acc_operand" "=b")
		    (unspec:DI [(match_operand:SI 3 "fpr_operand" "f")
				(match_operand:SI 4 "fpr_operand" "f")
				(match_operand:SI 5 "const_int_operand" "n")]
			       UNSPEC_MMULH))
	       (set (match_operand:HI 6 "accg_operand" "=B")
		    (unspec:HI [(const_int 0)] UNSPEC_MMULH))]))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[5]))
  {
  default:		    break;
  case FRV_BUILTIN_MMULHS:  return \"cmmulhs %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MMULHU:  return \"cmmulhu %3, %4, %2, %1, %e0\";
  }

  fatal_insn (\"Bad media insn, cond_exec_mmulh\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mmulh")])

;; Dual cross multiplication (halfword): type "mmulxh"

(define_expand "mmulxhs"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "=b")
		   (unspec:DI [(match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_dup 4)]
			      UNSPEC_MMULXH))
	      (set (match_operand:HI 3 "accg_operand" "=B")
		   (unspec:HI [(const_int 0)] UNSPEC_MMULXH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MMULXHS);")

(define_expand "mmulxhu"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "=b")
		   (unspec:DI [(match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_dup 4)]
			      UNSPEC_MMULXH))
	      (set (match_operand:HI 3 "accg_operand" "=B")
		   (unspec:HI [(const_int 0)] UNSPEC_MMULXH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MMULXHU);")

(define_insn "*mmulxh"
  [(set (match_operand:DI 0 "even_acc_operand" "=b")
        (unspec:DI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")
		    (match_operand:SI 3 "const_int_operand" "n")]
		   UNSPEC_MMULXH))
   (set (match_operand:HI 4 "accg_operand" "=B")
	(unspec:HI [(const_int 0)] UNSPEC_MMULXH))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[3]))
  {
  default:		    break;
  case FRV_BUILTIN_MMULXHS: return \"mmulxhs %1, %2, %0\";
  case FRV_BUILTIN_MMULXHU: return \"mmulxhu %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mmulxh\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mmulxh")])

;; Dual product-sum (halfword): type "mmach"

(define_expand "mmachs"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "+b")
		   (unspec:DI [(match_dup 0)
			       (match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_operand:HI 3 "accg_operand" "+B")
			       (match_dup 4)]
			      UNSPEC_MMACH))
	      (set (match_dup 3)
		   (unspec:HI [(const_int 0)] UNSPEC_MMACH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MMACHS);")

(define_expand "mmachu"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "+b")
		   (unspec:DI [(match_dup 0)
			       (match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_operand:HI 3 "accg_operand" "+B")
			       (match_dup 4)]
			      UNSPEC_MMACH))
	      (set (match_dup 3)
		   (unspec:HI [(const_int 0)] UNSPEC_MMACH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MMACHU);")

(define_insn "*mmach"
  [(set (match_operand:DI 0 "even_acc_operand" "+b")
        (unspec:DI [(match_dup 0)
		    (match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")
		    (match_operand:HI 3 "accg_operand" "+B")
		    (match_operand:SI 4 "const_int_operand" "n")]
		   UNSPEC_MMACH))
   (set (match_dup 3) (unspec:HI [(const_int 0)] UNSPEC_MMACH))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[4]))
  {
  default:		   break;
  case FRV_BUILTIN_MMACHS: return \"mmachs %1, %2, %0\";
  case FRV_BUILTIN_MMACHU: return \"mmachu %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mmach\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mmach")])

(define_insn "*cond_exec_mmach"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (parallel [(set (match_operand:DI 2 "even_acc_operand" "+b")
		    (unspec:DI [(match_dup 2)
				(match_operand:SI 3 "fpr_operand" "f")
				(match_operand:SI 4 "fpr_operand" "f")
				(match_operand:HI 5 "accg_operand" "+B")
				(match_operand:SI 6 "const_int_operand" "n")]
			       UNSPEC_MMACH))
	       (set (match_dup 5)
		    (unspec:HI [(const_int 0)] UNSPEC_MMACH))]))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[6]))
  {
  default:		   break;
  case FRV_BUILTIN_MMACHS: return \"cmmachs %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MMACHU: return \"cmmachu %3, %4, %2, %1, %e0\";
  }

  fatal_insn (\"Bad media insn, cond_exec_mmach\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mmach")])

;; Dual product-difference: type "mmrdh"

(define_expand "mmrdhs"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "+b")
		   (unspec:DI [(match_dup 0)
			       (match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_operand:HI 3 "accg_operand" "+B")
			       (match_dup 4)]
			      UNSPEC_MMRDH))
	      (set (match_dup 3)
		   (unspec:HI [(const_int 0)] UNSPEC_MMRDH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MMRDHS);")

(define_expand "mmrdhu"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "+b")
		   (unspec:DI [(match_dup 0)
			       (match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_operand:HI 3 "accg_operand" "+B")
			       (match_dup 4)]
			      UNSPEC_MMRDH))
	      (set (match_dup 3)
		   (unspec:HI [(const_int 0)] UNSPEC_MMRDH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MMRDHU);")

(define_insn "*mmrdh"
  [(set (match_operand:DI 0 "even_acc_operand" "+b")
        (unspec:DI [(match_dup 0)
		    (match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")
		    (match_operand:HI 3 "accg_operand" "+B")
		    (match_operand:SI 4 "const_int_operand" "n")]
		   UNSPEC_MMRDH))
   (set (match_dup 3)
	(unspec:HI [(const_int 0)] UNSPEC_MMRDH))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[4]))
  {
  default:		   break;
  case FRV_BUILTIN_MMRDHS: return \"mmrdhs %1, %2, %0\";
  case FRV_BUILTIN_MMRDHU: return \"mmrdhu %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mrdh\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mmrdh")])

;; Quad multiply (halfword): type "mqmulh"

(define_expand "mqmulhs"
  [(parallel [(set (match_operand:V4SI 0 "quad_acc_operand" "=A")
		   (unspec:V4SI [(match_operand:DI 1 "even_fpr_operand" "h")
				 (match_operand:DI 2 "even_fpr_operand" "h")
				 (match_dup 4)]
				UNSPEC_MQMULH))
	      (set (match_operand:V4QI 3 "accg_operand" "=B")
		   (unspec:V4QI [(const_int 0)] UNSPEC_MQMULH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQMULHS);")

(define_expand "mqmulhu"
  [(parallel [(set (match_operand:V4SI 0 "quad_acc_operand" "=A")
		   (unspec:V4SI [(match_operand:DI 1 "even_fpr_operand" "h")
				 (match_operand:DI 2 "even_fpr_operand" "h")
				 (match_dup 4)]
				UNSPEC_MQMULH))
	      (set (match_operand:V4QI 3 "accg_operand" "=B")
		   (unspec:V4QI [(const_int 0)] UNSPEC_MQMULH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQMULHU);")

(define_insn "*mqmulh"
  [(set (match_operand:V4SI 0 "quad_acc_operand" "=A")
        (unspec:V4SI [(match_operand:DI 1 "even_fpr_operand" "h")
		      (match_operand:DI 2 "even_fpr_operand" "h")
		      (match_operand:SI 3 "const_int_operand" "n")]
		     UNSPEC_MQMULH))
   (set (match_operand:V4QI 4 "accg_operand" "=B")
	(unspec:V4QI [(const_int 0)] UNSPEC_MQMULH))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[3]))
  {
  default:		     break;
  case FRV_BUILTIN_MQMULHS:  return \"mqmulhs %1, %2, %0\";
  case FRV_BUILTIN_MQMULHU:  return \"mqmulhu %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mqmulh\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mqmulh")])

(define_insn "*cond_exec_mqmulh"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (parallel [(set (match_operand:V4SI 2 "quad_acc_operand" "=A")
		    (unspec:V4SI [(match_operand:DI 3 "even_fpr_operand" "h")
				  (match_operand:DI 4 "even_fpr_operand" "h")
				  (match_operand:SI 5 "const_int_operand" "n")]
				 UNSPEC_MQMULH))
	       (set (match_operand:V4QI 6 "accg_operand" "=B")
		    (unspec:V4QI [(const_int 0)] UNSPEC_MQMULH))]))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[5]))
  {
  default:		     break;
  case FRV_BUILTIN_MQMULHS:  return \"cmqmulhs %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MQMULHU:  return \"cmqmulhu %3, %4, %2, %1, %e0\";
  }

  fatal_insn (\"Bad media insn, cond_exec_mqmulh\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mqmulh")])

;; Quad cross multiply (halfword): type "mqmulxh"

(define_expand "mqmulxhs"
  [(parallel [(set (match_operand:V4SI 0 "quad_acc_operand" "=A")
		   (unspec:V4SI [(match_operand:DI 1 "even_fpr_operand" "h")
				 (match_operand:DI 2 "even_fpr_operand" "h")
				 (match_dup 4)]
				UNSPEC_MQMULXH))
	      (set (match_operand:V4QI 3 "accg_operand" "=B")
		   (unspec:V4QI [(const_int 0)] UNSPEC_MQMULXH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQMULXHS);")

(define_expand "mqmulxhu"
  [(parallel [(set (match_operand:V4SI 0 "quad_acc_operand" "=A")
		   (unspec:V4SI [(match_operand:DI 1 "even_fpr_operand" "h")
				 (match_operand:DI 2 "even_fpr_operand" "h")
				 (match_dup 4)]
				UNSPEC_MQMULXH))
	      (set (match_operand:V4QI 3 "accg_operand" "=B")
		   (unspec:V4QI [(const_int 0)] UNSPEC_MQMULXH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQMULXHU);")

(define_insn "*mqmulxh"
  [(set (match_operand:V4SI 0 "quad_acc_operand" "=A")
        (unspec:V4SI [(match_operand:DI 1 "even_fpr_operand" "h")
		      (match_operand:DI 2 "even_fpr_operand" "h")
		      (match_operand:SI 3 "const_int_operand" "n")]
		     UNSPEC_MQMULXH))
   (set (match_operand:V4QI 4 "accg_operand" "=B")
	(unspec:V4QI [(const_int 0)] UNSPEC_MQMULXH))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[3]))
  {
  default:		     break;
  case FRV_BUILTIN_MQMULXHS: return \"mqmulxhs %1, %2, %0\";
  case FRV_BUILTIN_MQMULXHU: return \"mqmulxhu %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mqmulxh\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mqmulxh")])

;; Quad product-sum (halfword): type "mqmach"

(define_expand "mqmachs"
  [(parallel [(set (match_operand:V4SI 0 "even_acc_operand" "+A")
		   (unspec:V4SI [(match_dup 0)
				 (match_operand:DI 1 "even_fpr_operand" "h")
				 (match_operand:DI 2 "even_fpr_operand" "h")
				 (match_operand:V4QI 3 "accg_operand" "+B")
				 (match_dup 4)]
				UNSPEC_MQMACH))
	      (set (match_dup 3)
		   (unspec:V4QI [(const_int 0)] UNSPEC_MQMACH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQMACHS);")

(define_expand "mqmachu"
  [(parallel [(set (match_operand:V4SI 0 "even_acc_operand" "+A")
		   (unspec:V4SI [(match_dup 0)
				 (match_operand:DI 1 "even_fpr_operand" "h")
				 (match_operand:DI 2 "even_fpr_operand" "h")
				 (match_operand:V4QI 3 "accg_operand" "+B")
				 (match_dup 4)]
				UNSPEC_MQMACH))
	      (set (match_dup 3)
		   (unspec:V4QI [(const_int 0)] UNSPEC_MQMACH))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQMACHU);")

(define_insn "*mqmach"
  [(set (match_operand:V4SI 0 "even_acc_operand" "+A")
        (unspec:V4SI [(match_dup 0)
		      (match_operand:DI 1 "even_fpr_operand" "h")
		      (match_operand:DI 2 "even_fpr_operand" "h")
		      (match_operand:V4QI 3 "accg_operand" "+B")
		      (match_operand:SI 4 "const_int_operand" "n")]
		     UNSPEC_MQMACH))
   (set (match_dup 3)
	(unspec:V4QI [(const_int 0)] UNSPEC_MQMACH))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[4]))
  {
  default:		    break;
  case FRV_BUILTIN_MQMACHS: return \"mqmachs %1, %2, %0\";
  case FRV_BUILTIN_MQMACHU: return \"mqmachu %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mqmach\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mqmach")])

(define_insn "*cond_exec_mqmach"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (parallel [(set (match_operand:V4SI 2 "even_acc_operand" "+A")
		    (unspec:V4SI [(match_dup 2)
				  (match_operand:DI 3 "even_fpr_operand" "h")
				  (match_operand:DI 4 "even_fpr_operand" "h")
				  (match_operand:V4QI 5 "accg_operand" "+B")
				  (match_operand:SI 6 "const_int_operand" "n")]
				 UNSPEC_MQMACH))
	       (set (match_dup 5)
		    (unspec:V4QI [(const_int 0)] UNSPEC_MQMACH))]))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[6]))
  {
  default:		    break;
  case FRV_BUILTIN_MQMACHS: return \"cmqmachs %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MQMACHU: return \"cmqmachu %3, %4, %2, %1, %e0\";
  }

  fatal_insn (\"Bad media insn, cond_exec_mqmach\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mqmach")])

;; Dual complex number product-sum (halfword)

(define_expand "mcpxrs"
  [(parallel [(set (match_operand:SI 0 "acc_operand" "=a")
		   (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_dup 4)]
			      UNSPEC_MCPX))
	      (set (match_operand:QI 3 "accg_operand" "=B")
		   (unspec:QI [(const_int 0)] UNSPEC_MCPX))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MCPXRS);")

(define_expand "mcpxru"
  [(parallel [(set (match_operand:SI 0 "acc_operand" "=a")
		   (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_dup 4)]
			      UNSPEC_MCPX))
	      (set (match_operand:QI 3 "accg_operand" "=B")
		   (unspec:QI [(const_int 0)] UNSPEC_MCPX))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MCPXRU);")

(define_expand "mcpxis"
  [(parallel [(set (match_operand:SI 0 "acc_operand" "=a")
		   (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_dup 4)]
			      UNSPEC_MCPX))
	      (set (match_operand:QI 3 "accg_operand" "=B")
		   (unspec:QI [(const_int 0)] UNSPEC_MCPX))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MCPXIS);")

(define_expand "mcpxiu"
  [(parallel [(set (match_operand:SI 0 "acc_operand" "=a")
		   (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_dup 4)]
			      UNSPEC_MCPX))
	      (set (match_operand:QI 3 "accg_operand" "=B")
		   (unspec:QI [(const_int 0)] UNSPEC_MCPX))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MCPXIU);")

(define_insn "*mcpx"
  [(parallel [(set (match_operand:SI 0 "acc_operand" "=a")
		   (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
			       (match_operand:SI 2 "fpr_operand" "f")
			       (match_operand:SI 3 "const_int_operand" "n")]
			      UNSPEC_MCPX))
	      (set (match_operand:QI 4 "accg_operand" "=B")
		   (unspec:QI [(const_int 0)] UNSPEC_MCPX))])]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[3]))
  {
  default:		   break;
  case FRV_BUILTIN_MCPXRS: return \"mcpxrs %1, %2, %0\";
  case FRV_BUILTIN_MCPXRU: return \"mcpxru %1, %2, %0\";
  case FRV_BUILTIN_MCPXIS: return \"mcpxis %1, %2, %0\";
  case FRV_BUILTIN_MCPXIU: return \"mcpxiu %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mcpx\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mcpx")])

(define_insn "*cond_exec_mcpx"
  [(cond_exec
    (match_operator 0 "ccr_eqne_operator"
		    [(match_operand 1 "cr_operand" "C")
		     (const_int 0)])
    (parallel [(set (match_operand:SI 2 "acc_operand" "=a")
		    (unspec:SI [(match_operand:SI 3 "fpr_operand" "f")
				(match_operand:SI 4 "fpr_operand" "f")
				(match_operand:SI 5 "const_int_operand" "n")]
			       UNSPEC_MCPX))
	       (set (match_operand:QI 6 "accg_operand" "=B")
		    (unspec:QI [(const_int 0)] UNSPEC_MCPX))]))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[5]))
  {
  default:		   break;
  case FRV_BUILTIN_MCPXRS: return \"cmcpxrs %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MCPXRU: return \"cmcpxru %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MCPXIS: return \"cmcpxis %3, %4, %2, %1, %e0\";
  case FRV_BUILTIN_MCPXIU: return \"cmcpxiu %3, %4, %2, %1, %e0\";
  }

  fatal_insn (\"Bad media insn, cond_exec_mcpx\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mcpx")])

;; Quad complex number product-sum (halfword): type "mqcpx"

(define_expand "mqcpxrs"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "=b")
		   (unspec:DI [(match_operand:DI 1 "fpr_operand" "f")
			       (match_operand:DI 2 "fpr_operand" "f")
			       (match_dup 4)]
			      UNSPEC_MQCPX))
	      (set (match_operand:HI 3 "accg_operand" "=B")
		   (unspec:HI [(const_int 0)] UNSPEC_MQCPX))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQCPXRS);")

(define_expand "mqcpxru"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "=b")
		   (unspec:DI [(match_operand:DI 1 "fpr_operand" "f")
			       (match_operand:DI 2 "fpr_operand" "f")
			       (match_dup 4)]
			      UNSPEC_MQCPX))
	      (set (match_operand:HI 3 "accg_operand" "=B")
		   (unspec:HI [(const_int 0)] UNSPEC_MQCPX))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQCPXRU);")

(define_expand "mqcpxis"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "=b")
		   (unspec:DI [(match_operand:DI 1 "fpr_operand" "f")
			       (match_operand:DI 2 "fpr_operand" "f")
			       (match_dup 4)]
			      UNSPEC_MQCPX))
	      (set (match_operand:HI 3 "accg_operand" "=B")
		   (unspec:HI [(const_int 0)] UNSPEC_MQCPX))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQCPXIS);")

(define_expand "mqcpxiu"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "=b")
		   (unspec:DI [(match_operand:DI 1 "fpr_operand" "f")
			       (match_operand:DI 2 "fpr_operand" "f")
			       (match_dup 4)]
			      UNSPEC_MQCPX))
	      (set (match_operand:HI 3 "accg_operand" "=B")
		   (unspec:HI [(const_int 0)] UNSPEC_MQCPX))])]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQCPXIU);")

(define_insn "*mqcpx"
  [(set (match_operand:DI 0 "even_acc_operand" "=b")
        (unspec:DI [(match_operand:DI 1 "fpr_operand" "f")
                    (match_operand:DI 2 "fpr_operand" "f")
		    (match_operand:SI 3 "const_int_operand" "n")]
		   UNSPEC_MQCPX))
   (set (match_operand:HI 4 "accg_operand" "=B")
	(unspec:HI [(const_int 0)] UNSPEC_MQCPX))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[3]))
  {
  default:		    break;
  case FRV_BUILTIN_MQCPXRS: return \"mqcpxrs %1, %2, %0\";
  case FRV_BUILTIN_MQCPXRU: return \"mqcpxru %1, %2, %0\";
  case FRV_BUILTIN_MQCPXIS: return \"mqcpxis %1, %2, %0\";
  case FRV_BUILTIN_MQCPXIU: return \"mqcpxiu %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mqcpx\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mqcpx")])

;; Cut: type "mcut"

(define_expand "mcut"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "acc_operand" "a")
                    (match_operand:SI 2 "fpr_or_int6_operand" "fI")
		    (match_operand:QI 3 "accg_operand" "B")
		    (match_dup 4)]
		   UNSPEC_MCUT))]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MCUT);")

(define_expand "mcutss"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "acc_operand" "a")
                    (match_operand:SI 2 "fpr_or_int6_operand" "fI")
		    (match_operand:QI 3 "accg_operand" "B")
		    (match_dup 4)]
		   UNSPEC_MCUT))]
  "TARGET_MEDIA"
  "operands[4] = GEN_INT (FRV_BUILTIN_MCUTSS);")

(define_insn "*mcut"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "acc_operand" "a")
                    (match_operand:SI 2 "fpr_or_int6_operand" "fI")
		    (match_operand:QI 3 "accg_operand" "B")
		    (match_operand:SI 4 "const_int_operand" "n")]
		   UNSPEC_MCUT))]
  "TARGET_MEDIA"
  "*
{
  switch (INTVAL (operands[4]))
  {
  default:		   break;
  case FRV_BUILTIN_MCUT:   return \"mcut%i2 %1, %2, %0\";
  case FRV_BUILTIN_MCUTSS: return \"mcutss%i2 %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mcut\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mcut")])

;; Accumulator read: type "mrdacc"

(define_insn "mrdacc"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
	(unspec:SI [(match_operand:SI 1 "acc_operand" "a")] UNSPEC_MRDACC))]
  "TARGET_MEDIA"
  "mrdacc %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mrdacc")])

(define_insn "mrdaccg"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
	(unspec:SI [(match_operand:QI 1 "accg_operand" "B")] UNSPEC_MRDACCG))]
  "TARGET_MEDIA"
  "mrdaccg %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mrdacc")])

;; Accumulator write: type "mwtacc"

(define_insn "mwtacc"
  [(set (match_operand:SI 0 "acc_operand" "=a")
	(unspec:SI [(match_operand:SI 1 "fpr_operand" "f")] UNSPEC_MWTACC))]
  "TARGET_MEDIA"
  "mwtacc %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mwtacc")])

(define_insn "mwtaccg"
  [(set (match_operand:QI 0 "accg_operand" "=B")
	(unspec:QI [(match_operand:SI 1 "fpr_operand" "f")] UNSPEC_MWTACCG))]
  "TARGET_MEDIA"
  "mwtaccg %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mwtacc")])

;; Trap: This one executes on the control unit, not the media units.

(define_insn "mtrap"
  [(unspec_volatile [(const_int 0)] UNSPEC_MTRAP)]
  "TARGET_MEDIA"
  "mtrap"
  [(set_attr "length" "4")
   (set_attr "type" "trap")])

;; Clear single accumulator: type "mclracc"

(define_insn "mclracc_internal"
  [(set (match_operand:SI 0 "acc_operand" "=a")
	(unspec:SI [(const_int 0)] UNSPEC_MCLRACC))
   (set (match_operand:QI 1 "accg_operand" "=B")
	(unspec:QI [(const_int 0)] UNSPEC_MCLRACC))]
  "TARGET_MEDIA"
  "mclracc %0,#0"
  [(set_attr "length" "4")
   (set_attr "type" "mclracc")])

(define_expand "mclracc"
  [(parallel [(set (match_operand:SI 0 "acc_operand" "=a")
		   (unspec:SI [(const_int 0)] UNSPEC_MCLRACC))
	      (set (match_dup 1)
		   (unspec:QI [(const_int 0)] UNSPEC_MCLRACC))])]
  "TARGET_MEDIA"
  "
{
  if (GET_CODE (operands[0]) != REG || !ACC_P (REGNO (operands[0])))
    FAIL;

  operands[1] = frv_matching_accg_for_acc (operands[0]);
}")

;; Clear all accumulators: type "mclracca"

(define_insn "mclracca8_internal"
  [(set (match_operand:V4SI 0 "quad_acc_operand" "=b")
	(unspec:V4SI [(const_int 0)] UNSPEC_MCLRACCA))
   (set (match_operand:V4SI 1 "quad_acc_operand" "=b")
	(unspec:V4SI [(const_int 0)] UNSPEC_MCLRACCA))
   (set (match_operand:V4QI 2 "accg_operand" "=B")
	(unspec:V4QI [(const_int 0)] UNSPEC_MCLRACCA))
   (set (match_operand:V4QI 3 "accg_operand" "=B")
	(unspec:V4QI [(const_int 0)] UNSPEC_MCLRACCA))]
  "TARGET_MEDIA && TARGET_ACC_8"
  "mclracc acc0,#1"
  [(set_attr "length" "4")
   (set_attr "type" "mclracca")])

(define_insn "mclracca4_internal"
  [(set (match_operand:V4SI 0 "quad_acc_operand" "=b")
	(unspec:V4SI [(const_int 0)] UNSPEC_MCLRACCA))
   (set (match_operand:V4QI 1 "accg_operand" "=B")
	(unspec:V4QI [(const_int 0)] UNSPEC_MCLRACCA))]
  "TARGET_MEDIA && TARGET_ACC_4"
  "mclracc acc0,#1"
  [(set_attr "length" "4")
   (set_attr "type" "mclracca")])

(define_expand "mclracca8"
  [(parallel [(set (match_dup 0) (unspec:V4SI [(const_int 0)] UNSPEC_MCLRACCA))
	      (set (match_dup 1) (unspec:V4SI [(const_int 0)] UNSPEC_MCLRACCA))
	      (set (match_dup 2) (unspec:V4QI [(const_int 0)] UNSPEC_MCLRACCA))
	      (set (match_dup 3) (unspec:V4QI [(const_int 0)] UNSPEC_MCLRACCA))])]
  "TARGET_MEDIA && TARGET_ACC_8"
  "
{
  operands[0] = gen_rtx_REG (V4SImode, ACC_FIRST);
  operands[1] = gen_rtx_REG (V4SImode, ACC_FIRST + (~3 & ACC_MASK));
  operands[2] = gen_rtx_REG (V4QImode, ACCG_FIRST);
  operands[3] = gen_rtx_REG (V4QImode, ACCG_FIRST + (~3 & ACC_MASK));
}")

(define_expand "mclracca4"
  [(parallel [(set (match_dup 0) (unspec:V4SI [(const_int 0)] UNSPEC_MCLRACCA))
	      (set (match_dup 1) (unspec:V4QI [(const_int 0)] UNSPEC_MCLRACCA))])]
  "TARGET_MEDIA && TARGET_ACC_4"
  "
{
  operands[0] = gen_rtx_REG (V4SImode, ACC_FIRST);
  operands[1] = gen_rtx_REG (V4QImode, ACCG_FIRST);
}")

(define_insn "mcop1"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")] UNSPEC_MCOP1))]
  "TARGET_MEDIA_REV1"
  "mcop1 %1, %2, %0"
  [(set_attr "length" "4")
;; What is the class of the insn ???
   (set_attr "type" "multi")])

(define_insn "mcop2"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")
                    (match_operand:SI 2 "fpr_operand" "f")] UNSPEC_MCOP2))]
  "TARGET_MEDIA_REV1"
  "mcop2 %1, %2, %0"
  [(set_attr "length" "4")
;; What is the class of the insn ???
   (set_attr "type" "multi")])

(define_insn "*mdunpackh_internal"
  [(set (match_operand:V4SI 0 "quad_fpr_operand" "=x")
        (unspec:V4SI [(match_operand:DI 1 "even_fpr_operand" "h")]
		     UNSPEC_MDUNPACKH_INTERNAL))]
  "TARGET_MEDIA_REV1"
  "mdunpackh %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mdunpackh")])

(define_insn_and_split "mdunpackh"
  [(set (match_operand:V4SI 0 "memory_operand" "=o")
        (unspec:V4SI [(match_operand:DI 1 "even_fpr_operand" "h")]
		     UNSPEC_MDUNPACKH))
   (clobber (match_scratch:V4SI 2 "=x"))]
  "TARGET_MEDIA_REV1"
  "#"
  "reload_completed"
  [(set (match_dup 2)
	(unspec:V4SI [(match_dup 1)] UNSPEC_MDUNPACKH_INTERNAL))
   (set (match_dup 3)
	(match_dup 4))
   (set (match_dup 5)
	(match_dup 6))]
  "
{
  operands[3] = change_address (operands[0], DImode, NULL_RTX);
  operands[4] = gen_rtx_REG (DImode, REGNO (operands[2]));
  operands[5] = frv_index_memory (operands[0], DImode, 1);
  operands[6] = gen_rtx_REG (DImode, REGNO (operands[2])+2);
}"
  [(set_attr "length" "20")
   (set_attr "type" "multi")])

(define_insn "*mbtohe_internal"
  [(set (match_operand:V4SI 0 "quad_fpr_operand" "=x")
        (unspec:V4SI [(match_operand:SI 1 "fpr_operand" "f")]
		     UNSPEC_MBTOHE_INTERNAL))]
  "TARGET_MEDIA_REV1"
  "mbtohe %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mbhconve")])

(define_insn_and_split "mbtohe"
  [(set (match_operand:V4SI 0 "memory_operand" "=o")
        (unspec:V4SI [(match_operand:SI 1 "fpr_operand" "f")]
		     UNSPEC_MBTOHE))
   (clobber (match_scratch:V4SI 2 "=x"))]
  "TARGET_MEDIA_REV1"
  "#"
  "reload_completed"
  [(set (match_dup 2)
	(unspec:V4SI [(match_dup 1)] UNSPEC_MBTOHE_INTERNAL))
   (set (match_dup 3)
	(match_dup 4))
   (set (match_dup 5)
	(match_dup 6))]
  "
{
  operands[3] = change_address (operands[0], DImode, NULL_RTX);
  operands[4] = gen_rtx_REG (DImode, REGNO (operands[2]));
  operands[5] = frv_index_memory (operands[0], DImode, 1);
  operands[6] = gen_rtx_REG (DImode, REGNO (operands[2])+2);
}"
  [(set_attr "length" "20")
   (set_attr "type" "multi")])

;; Quad product-sum (halfword) instructions only found on the FR400.
;; type "mqmach"

(define_expand "mqxmachs"
  [(parallel [(set (match_operand:V4SI 0 "quad_acc_operand" "")
	           (unspec:V4SI [(match_dup 0)
		   	         (match_operand:DI 1 "even_fpr_operand" "")
			         (match_operand:DI 2 "even_fpr_operand" "")
				 (match_operand:V4QI 3 "accg_operand" "")
				 (match_dup 4)]
				UNSPEC_MQMACH2))
		(set (match_dup 3)
		     (unspec:V4QI [(const_int 0)] UNSPEC_MQMACH2))])]
  "TARGET_MEDIA_REV2"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQXMACHS);")

(define_expand "mqxmacxhs"
  [(parallel [(set (match_operand:V4SI 0 "quad_acc_operand" "")
		   (unspec:V4SI [(match_dup 0)
				 (match_operand:DI 1 "even_fpr_operand" "")
				 (match_operand:DI 2 "even_fpr_operand" "")
				 (match_operand:V4QI 3 "accg_operand" "")
				 (match_dup 4)]
				UNSPEC_MQMACH2))
	      (set (match_dup 3)
		   (unspec:V4QI [(const_int 0)] UNSPEC_MQMACH2))])]
  "TARGET_MEDIA_REV2"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQXMACXHS);")

(define_expand "mqmacxhs"
  [(parallel [(set (match_operand:V4SI 0 "quad_acc_operand" "")
		   (unspec:V4SI [(match_dup 0)
				 (match_operand:DI 1 "even_fpr_operand" "")
				 (match_operand:DI 2 "even_fpr_operand" "")
				 (match_operand:V4QI 3 "accg_operand" "")
				 (match_dup 4)]
				UNSPEC_MQMACH2))
	      (set (match_dup 3)
		   (unspec:V4QI [(const_int 0)] UNSPEC_MQMACH2))])]
  "TARGET_MEDIA_REV2"
  "operands[4] = GEN_INT (FRV_BUILTIN_MQMACXHS);")

(define_insn "*mqmach2"
  [(set (match_operand:V4SI 0 "quad_acc_operand" "+A")
        (unspec:V4SI [(match_dup 0)
		      (match_operand:DI 1 "even_fpr_operand" "h")
		      (match_operand:DI 2 "even_fpr_operand" "h")
		      (match_operand:V4QI 3 "accg_operand" "+B")
		      (match_operand:SI 4 "const_int_operand" "n")]
		     UNSPEC_MQMACH2))
   (set (match_dup 3)
	(unspec:V4QI [(const_int 0)] UNSPEC_MQMACH2))]
  "TARGET_MEDIA_REV2"
  "*
{
  switch (INTVAL (operands[4]))
  {
  default:		      break;
  case FRV_BUILTIN_MQXMACHS:  return \"mqxmachs %1, %2, %0\";
  case FRV_BUILTIN_MQXMACXHS: return \"mqxmacxhs %1, %2, %0\";
  case FRV_BUILTIN_MQMACXHS:  return \"mqmacxhs %1, %2, %0\";
  }

  fatal_insn (\"Bad media insn, mqmach2\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mqmach")])

;; Accumulator addition/subtraction: type "maddacc"

(define_expand "maddaccs"
  [(parallel [(set (match_operand:SI 0 "acc_operand" "")
		   (unspec:SI [(match_operand:DI 1 "even_acc_operand" "")]
			      UNSPEC_MADDACC))
	      (set (match_operand:QI 2 "accg_operand" "")
		   (unspec:QI [(match_operand:HI 3 "accg_operand" "")
			       (match_dup 4)]
			      UNSPEC_MADDACC))])]
  "TARGET_MEDIA_REV2"
  "operands[4] = GEN_INT (FRV_BUILTIN_MADDACCS);")

(define_expand "msubaccs"
  [(parallel [(set (match_operand:SI 0 "acc_operand" "")
		   (unspec:SI [(match_operand:DI 1 "even_acc_operand" "")]
			      UNSPEC_MADDACC))
	      (set (match_operand:QI 2 "accg_operand" "")
		   (unspec:QI [(match_operand:HI 3 "accg_operand" "")
			       (match_dup 4)]
			      UNSPEC_MADDACC))])]
  "TARGET_MEDIA_REV2"
  "operands[4] = GEN_INT (FRV_BUILTIN_MSUBACCS);")

(define_insn "masaccs"
  [(set (match_operand:DI 0 "even_acc_operand" "=b")
	(unspec:DI [(match_operand:DI 1 "even_acc_operand" "b")]
		   UNSPEC_MASACCS))
   (set (match_operand:HI 2 "accg_operand" "=B")
	(unspec:HI [(match_operand:HI 3 "accg_operand" "B")]
		   UNSPEC_MASACCS))]
  "TARGET_MEDIA_REV2"
  "masaccs %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "maddacc")])

(define_insn "*maddacc"
  [(set (match_operand:SI 0 "acc_operand" "=a")
	(unspec:SI [(match_operand:DI 1 "even_acc_operand" "b")]
		   UNSPEC_MADDACC))
   (set (match_operand:QI 2 "accg_operand" "=B")
	(unspec:QI [(match_operand:HI 3 "accg_operand" "B")
		    (match_operand:SI 4 "const_int_operand" "n")]
		   UNSPEC_MADDACC))]
  "TARGET_MEDIA_REV2"
  "*
{
  switch (INTVAL (operands[4]))
  {
  default:		     break;
  case FRV_BUILTIN_MADDACCS: return \"maddaccs %1, %0\";
  case FRV_BUILTIN_MSUBACCS: return \"msubaccs %1, %0\";
  }

  fatal_insn (\"Bad media insn, maddacc\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "maddacc")])

;; Dual accumulator addition/subtraction: type "mdaddacc"

(define_expand "mdaddaccs"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "")
		   (unspec:DI [(match_operand:V4SI 1 "quad_acc_operand" "")]
			      UNSPEC_MDADDACC))
	      (set (match_operand:HI 2 "accg_operand" "")
		   (unspec:HI [(match_operand:V4QI 3 "accg_operand" "")
			       (match_dup 4)]
			      UNSPEC_MDADDACC))])]
  "TARGET_MEDIA_REV2"
  "operands[4] = GEN_INT (FRV_BUILTIN_MDADDACCS);")

(define_expand "mdsubaccs"
  [(parallel [(set (match_operand:DI 0 "even_acc_operand" "")
		   (unspec:DI [(match_operand:V4SI 1 "quad_acc_operand" "")]
			      UNSPEC_MDADDACC))
	      (set (match_operand:HI 2 "accg_operand" "")
	      	   (unspec:HI [(match_operand:V4QI 3 "accg_operand" "")
			       (match_dup 4)]
			      UNSPEC_MDADDACC))])]
  "TARGET_MEDIA_REV2"
  "operands[4] = GEN_INT (FRV_BUILTIN_MDSUBACCS);")

(define_insn "mdasaccs"
  [(set (match_operand:V4SI 0 "quad_acc_operand" "=A")
	(unspec:V4SI [(match_operand:V4SI 1 "quad_acc_operand" "A")]
		     UNSPEC_MDASACCS))
   (set (match_operand:V4QI 2 "accg_operand" "=B")
	(unspec:V4QI [(match_operand:V4QI 3 "accg_operand" "B")]
		     UNSPEC_MDASACCS))]
  "TARGET_MEDIA_REV2"
  "mdasaccs %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mdaddacc")])

(define_insn "*mdaddacc"
  [(set (match_operand:DI 0 "even_acc_operand" "=b")
	(unspec:DI [(match_operand:V4SI 1 "quad_acc_operand" "A")]
		   UNSPEC_MDADDACC))
   (set (match_operand:HI 2 "accg_operand" "=B")
	(unspec:HI [(match_operand:V4QI 3 "accg_operand" "B")
		    (match_operand:SI 4 "const_int_operand" "n")]
		   UNSPEC_MDADDACC))]
  "TARGET_MEDIA_REV2"
  "*
{
  switch (INTVAL (operands[4]))
  {
  default:		      break;
  case FRV_BUILTIN_MDADDACCS: return \"mdaddaccs %1, %0\";
  case FRV_BUILTIN_MDSUBACCS: return \"mdsubaccs %1, %0\";
  }

  fatal_insn (\"Bad media insn, mdaddacc\", insn);
}"
  [(set_attr "length" "4")
   (set_attr "type" "mdaddacc")])

;; Dual absolute (halfword): type "mabsh"

(define_insn "mabshs"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:SI 1 "fpr_operand" "f")] UNSPEC_MABSHS))]
  "TARGET_MEDIA_REV2"
  "mabshs %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mabsh")])

;; Dual rotate: type "mdrot"

(define_insn "mdrotli"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:DI 1 "even_fpr_operand" "h")
		    (match_operand:SI 2 "uint5_operand" "I")]
		   UNSPEC_MDROTLI))]
  "TARGET_MEDIA_REV2"
  "mdrotli %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mdrot")])

;; Dual coupling (concatenation): type "mcpl"

(define_insn "mcplhi"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:DI 1 "fpr_operand" "h")
		    (match_operand:SI 2 "uint4_operand" "I")]
		   UNSPEC_MCPLHI))]
  "TARGET_MEDIA_REV2"
  "mcplhi %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mcpl")])

(define_insn "mcpli"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
        (unspec:SI [(match_operand:DI 1 "fpr_operand" "h")
		    (match_operand:SI 2 "uint5_operand" "I")]
		   UNSPEC_MCPLI))]
  "TARGET_MEDIA_REV2"
  "mcpli %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mcpl")])

;; Dual cut: type "mdcut"

(define_insn "mdcutssi"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:DI 1 "even_acc_operand" "b")
		    (match_operand:SI 2 "int6_operand" "I")
		    (match_operand:HI 3 "accg_operand" "B")]
		   UNSPEC_MDCUTSSI))]
  "TARGET_MEDIA_REV2"
  "mdcutssi %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mdcut")])

;; Quad saturate (halfword): type "mqsath"

(define_insn "mqsaths"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:DI 1 "even_fpr_operand" "h")
		    (match_operand:DI 2 "even_fpr_operand" "h")]
		   UNSPEC_MQSATHS))]
  "TARGET_MEDIA_REV2"
  "mqsaths %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mqsath")])

;; Quad limit instructions: type "mqlimh"

(define_insn "mqlclrhs"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:DI 1 "even_fpr_operand" "h")
		    (match_operand:DI 2 "even_fpr_operand" "h")]
		   UNSPEC_MQLCLRHS))]
  "TARGET_MEDIA_FR450"
  "mqlclrhs %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mqlimh")])

(define_insn "mqlmths"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:DI 1 "even_fpr_operand" "h")
		    (match_operand:DI 2 "even_fpr_operand" "h")]
		   UNSPEC_MQLMTHS))]
  "TARGET_MEDIA_FR450"
  "mqlmths %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mqlimh")])

(define_insn "mqsllhi"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:DI 1 "even_fpr_operand" "h")
		    (match_operand:SI 2 "int6_operand" "I")]
		   UNSPEC_MQSLLHI))]
  "TARGET_MEDIA_FR450"
  "mqsllhi %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mqshift")])

(define_insn "mqsrahi"
  [(set (match_operand:DI 0 "even_fpr_operand" "=h")
        (unspec:DI [(match_operand:DI 1 "even_fpr_operand" "h")
		    (match_operand:SI 2 "int6_operand" "I")]
		   UNSPEC_MQSRAHI))]
  "TARGET_MEDIA_FR450"
  "mqsrahi %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mqshift")])

;; Set hi/lo instructions: type "mset"

(define_insn "mhsetlos"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
	(unspec:SI [(match_operand:SI 1 "fpr_operand" "0")
		    (match_operand:SI 2 "int12_operand" "NOP")]
		   UNSPEC_MHSETLOS))]
  "TARGET_MEDIA_REV2"
  "mhsetlos %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mset")])

(define_insn "mhsetloh"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
	(unspec:SI [(match_operand:SI 1 "fpr_operand" "0")
		    (match_operand:SI 2 "int5_operand" "I")]
		   UNSPEC_MHSETLOH))]
  "TARGET_MEDIA_REV2"
  "mhsetloh %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mset")])

(define_insn "mhsethis"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
	(unspec:SI [(match_operand:SI 1 "fpr_operand" "0")
		    (match_operand:SI 2 "int12_operand" "NOP")]
		   UNSPEC_MHSETHIS))]
  "TARGET_MEDIA_REV2"
  "mhsethis %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mset")])

(define_insn "mhsethih"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
	(unspec:SI [(match_operand:SI 1 "fpr_operand" "0")
		    (match_operand:SI 2 "int5_operand" "I")]
		   UNSPEC_MHSETHIH))]
  "TARGET_MEDIA_REV2"
  "mhsethih %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mset")])

(define_insn "mhdsets"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
	(unspec:SI [(match_operand:SI 1 "int12_operand" "NOP")]
		   UNSPEC_MHDSETS))]
  "TARGET_MEDIA_REV2"
  "mhdsets %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mset")])

(define_insn "mhdseth"
  [(set (match_operand:SI 0 "fpr_operand" "=f")
	(unspec:SI [(match_operand:SI 1 "fpr_operand" "0")
		    (match_operand:SI 2 "int5_operand" "I")]
		   UNSPEC_MHDSETH))]
  "TARGET_MEDIA_REV2"
  "mhdseth %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mset")])

;;-----------------------------------------------------------------------------

(define_expand "symGOT2reg"
  [(match_operand:SI 0 "" "")
   (match_operand:SI 1 "" "")
   (match_operand:SI 2 "" "")
   (match_operand:SI 3 "" "")]
  ""
  "
{
  rtx insn;

  insn = emit_insn (gen_symGOT2reg_i (operands[0], operands[1], operands[2], operands[3]));

  MEM_READONLY_P (SET_SRC (PATTERN (insn))) = 1;

  REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_EQUAL, operands[1],
					REG_NOTES (insn));

  DONE;
}")

(define_expand "symGOT2reg_i"
  [(set (match_operand:SI 0 "" "")
	(mem:SI (plus:SI (match_operand:SI 2 "" "")
			 (const:SI (unspec:SI [(match_operand:SI 1 "" "")
					       (match_operand:SI 3 "" "")]
					      UNSPEC_GOT)))))]
  ""
  "")

(define_expand "symGOT2reg_hilo"
  [(set (match_dup 6)
	(high:SI (const:SI (unspec:SI [(match_operand:SI 1 "" "")
				       (match_dup 4)] UNSPEC_GOT))))
   (set (match_dup 5)
	(lo_sum:SI (match_dup 6)
		   (const:SI (unspec:SI [(match_dup 1)
					 (match_operand:SI 3 "" "")]
					UNSPEC_GOT))))
   (set (match_operand:SI 0 "" "")
	(mem:SI (plus:SI (match_dup 5)
			 (match_operand:SI 2 "" ""))))
   ]
  ""
  "
{
  if (no_new_pseudos)
    operands[6] = operands[5] = operands[0];
  else
    {
      operands[6] = gen_reg_rtx (SImode);
      operands[5] = gen_reg_rtx (SImode);
    }

  operands[4] = GEN_INT (INTVAL (operands[3]) + 1);
  operands[3] = GEN_INT (INTVAL (operands[3]) + 2);
}")

(define_expand "symGOTOFF2reg_hilo"
  [(set (match_dup 6)
	(high:SI (const:SI (unspec:SI [(match_operand:SI 1 "" "")
				       (match_dup 4)] UNSPEC_GOT))))
   (set (match_dup 5)
	(lo_sum:SI (match_dup 6)
		   (const:SI (unspec:SI [(match_dup 1)
					 (match_operand:SI 3 "" "")]
					UNSPEC_GOT))))
   (set (match_operand:SI 0 "" "")
	(plus:SI (match_dup 5)
		 (match_operand:SI 2 "" "")))
   ]
  ""
  "
{
  if (no_new_pseudos)
    operands[6] = operands[5] = operands[0];
  else
    {
      operands[6] = gen_reg_rtx (SImode);
      operands[5] = gen_reg_rtx (SImode);
    }

  operands[4] = GEN_INT (INTVAL (operands[3]) + 1);
  operands[3] = GEN_INT (INTVAL (operands[3]) + 2);
}")

(define_expand "symGOTOFF2reg"
  [(match_operand:SI 0 "" "")
   (match_operand:SI 1 "" "")
   (match_operand:SI 2 "" "")
   (match_operand:SI 3 "" "")]
  ""
  "
{
  rtx insn = emit_insn (gen_symGOTOFF2reg_i (operands[0], operands[1], operands[2], operands[3]));

  REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_EQUAL, operands[1],
					REG_NOTES (insn));

  DONE;
}")

(define_expand "symGOTOFF2reg_i"
  [(set (match_operand:SI 0 "" "")
	(plus:SI (match_operand:SI 2 "" "")
		 (const:SI
		  (unspec:SI [(match_operand:SI 1 "" "")
			     (match_operand:SI 3 "" "")]
			     UNSPEC_GOT))))]
  ""
  "")

(define_expand "symGPREL2reg"
  [(match_operand:SI 0 "" "")
   (match_operand:SI 1 "" "")
   (match_operand:SI 2 "" "")
   (match_operand:SI 3 "" "")
   (match_dup 4)]
  ""
  "
{
  rtx insn;

  if (no_new_pseudos)
    operands[4] = operands[0];
  else
    operands[4] = gen_reg_rtx (SImode);

  emit_insn (frv_gen_GPsym2reg (operands[4], operands[2]));

  insn = emit_insn (gen_symGOTOFF2reg_i (operands[0], operands[1],
					 operands[4], operands[3]));

  REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_EQUAL, operands[1],
					REG_NOTES (insn));

  DONE;
}")

(define_expand "symGPREL2reg_hilo"
  [(match_operand:SI 0 "" "")
   (match_operand:SI 1 "" "")
   (match_operand:SI 2 "" "")
   (match_operand:SI 3 "" "")
   (match_dup 4)]
  ""
  "
{
  rtx insn;

  if (no_new_pseudos)
    {
      emit_insn (gen_symGOT2reg (operands[0], operands[1], operands[2],
				 GEN_INT (R_FRV_GOT12)));
      DONE;
    }

  operands[4] = gen_reg_rtx (SImode);

  emit_insn (frv_gen_GPsym2reg (operands[4], operands[2]));

  insn = emit_insn (gen_symGOTOFF2reg_hilo (operands[0], operands[1],
					    operands[4], operands[3]));

  REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_EQUAL, operands[1],
					REG_NOTES (insn));

  DONE;
}")

(define_constants
  [
   (UNSPEC_SMUL			154)
   (UNSPEC_UMUL			155)
   (UNSPEC_SMU			156)
   (UNSPEC_ADDSS		157)
   (UNSPEC_SUBSS		158)
   (UNSPEC_SLASS		159)
   (UNSPEC_SCAN			160)
   (UNSPEC_INTSS                161)
   (UNSPEC_SCUTSS		162)
   (UNSPEC_PREFETCH0		163)
   (UNSPEC_PREFETCH		164)
   (UNSPEC_IACCreadll		165)
   (UNSPEC_IACCreadl		166)
   (UNSPEC_IACCsetll		167)
   (UNSPEC_IACCsetl		168)
   (UNSPEC_SMASS		169)
   (UNSPEC_SMSSS		170)
   (UNSPEC_IMUL			171)

   (IACC0_REG			171)
])

(define_insn "smul"
  [(set (match_operand:DI 0 "integer_register_operand" "=d")
        (unspec:DI [(match_operand:SI 1 "integer_register_operand" "d")
		    (match_operand:SI 2 "integer_register_operand" "d")]
		   UNSPEC_SMUL))]
  ""
  "smul %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mul")])

(define_insn "umul"
  [(set (match_operand:DI 0 "integer_register_operand" "=d")
        (unspec:DI [(match_operand:SI 1 "integer_register_operand" "d")
		    (match_operand:SI 2 "integer_register_operand" "d")]
		   UNSPEC_UMUL))]
  ""
  "umul %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "mul")])

(define_insn "smass"
  [(set (reg:DI IACC0_REG)
	(unspec:DI [(match_operand:SI 0 "integer_register_operand" "d")
		    (match_operand:SI 1 "integer_register_operand" "d")
		    (reg:DI IACC0_REG)]
		   UNSPEC_SMASS))]
  "TARGET_FR405_BUILTINS"
  "smass %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "macc")])

(define_insn "smsss"
  [(set (reg:DI IACC0_REG)
	(unspec:DI [(match_operand:SI 0 "integer_register_operand" "d")
		    (match_operand:SI 1 "integer_register_operand" "d")
		    (reg:DI IACC0_REG)]
		   UNSPEC_SMSSS))]
  "TARGET_FR405_BUILTINS"
  "smsss %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "macc")])

(define_insn "smu"
  [(set (reg:DI IACC0_REG)
	(unspec:DI [(match_operand:SI 0 "integer_register_operand" "d")
		    (match_operand:SI 1 "integer_register_operand" "d")]
		   UNSPEC_SMU))]
  "TARGET_FR405_BUILTINS"
  "smu %1, %0"
  [(set_attr "length" "4")
   (set_attr "type" "macc")])

(define_insn "addss"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
        (unspec:SI [(match_operand:SI 1 "integer_register_operand" "d")
		    (match_operand:SI 2 "integer_register_operand" "d")]
		   UNSPEC_ADDSS))]
  "TARGET_FR405_BUILTINS"
  "addss %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "subss"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
        (unspec:SI [(match_operand:SI 1 "integer_register_operand" "d")
		    (match_operand:SI 2 "integer_register_operand" "d")]
		   UNSPEC_SUBSS))]
  "TARGET_FR405_BUILTINS"
  "subss %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "slass"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
        (unspec:SI [(match_operand:SI 1 "integer_register_operand" "d")
		    (match_operand:SI 2 "integer_register_operand" "d")]
		   UNSPEC_SLASS))]
  "TARGET_FR405_BUILTINS"
  "slass %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "int")])

(define_insn "scan"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
        (unspec:SI [(match_operand:SI 1 "integer_register_operand" "d")
		    (match_operand:SI 2 "integer_register_operand" "d")]
		   UNSPEC_SCAN))]
  ""
  "scan %1, %2, %0"
  [(set_attr "length" "4")
   (set_attr "type" "scan")])

(define_insn "scutss"
  [(set (match_operand:SI 0 "integer_register_operand" "=d")
	(unspec:SI [(match_operand:SI 1 "integer_register_operand" "d")
		    (reg:DI IACC0_REG)]
		   UNSPEC_SCUTSS))]
  "TARGET_FR405_BUILTINS"
  "scutss %1,%0"
  [(set_attr "length" "4")
   (set_attr "type" "cut")])

(define_insn "frv_prefetch0"
  [(prefetch (unspec:SI [(match_operand:SI 0 "register_operand" "r")]
			UNSPEC_PREFETCH0)
	     (const_int 0)
	     (const_int 0))]
  ""
  "dcpl %0, gr0, #0"
  [(set_attr "length" "4")])

(define_insn "frv_prefetch"
  [(prefetch (unspec:SI [(match_operand:SI 0 "register_operand" "r")]
			UNSPEC_PREFETCH)
	     (const_int 0)
	     (const_int 0))]
  "TARGET_FR500_FR550_BUILTINS"
  "nop.p\\n\\tnldub @(%0, gr0), gr0"
  [(set_attr "length" "8")])

;; TLS patterns

(define_insn "call_gettlsoff"
  [(set (match_operand:SI 0 "register_operand" "=D09")
	(unspec:SI
	 [(match_operand:SI 1 "symbolic_operand" "")]
	 UNSPEC_GETTLSOFF))
   (clobber (reg:SI GR8_REG))
   (clobber (reg:SI LRREG))
   (use (match_operand:SI 2 "register_operand" "D15"))]
  "HAVE_AS_TLS"
  "call #gettlsoff(%a1)"
  [(set_attr "length" "4")
   (set_attr "type" "load_or_call")])

;; We have to expand this like a libcall (it sort of actually is)
;; because otherwise sched may move, for example, an insn that sets up
;; GR8 for a subsequence call before the *tls_indirect_call insn, and
;; then reload won't be able to fix things up.
(define_expand "tls_indirect_call"
  [(set (reg:DI GR8_REG)
	(match_operand:DI 2 "register_operand" ""))
   (parallel
    [(set (reg:SI GR9_REG)
	  (unspec:SI
	   [(match_operand:SI 1 "symbolic_operand" "")
	   (reg:DI GR8_REG)]
	   UNSPEC_TLS_INDIRECT_CALL))
    (clobber (reg:SI GR8_REG))
    (clobber (reg:SI LRREG))
    (use (match_operand:SI 3 "register_operand" ""))])
   (set (match_operand:SI 0 "register_operand" "")
	(reg:SI GR9_REG))]
  "HAVE_AS_TLS")

(define_insn "*tls_indirect_call"
  [(set (reg:SI GR9_REG)
	(unspec:SI
	 [(match_operand:SI 0 "symbolic_operand" "")
	  (reg:DI GR8_REG)]
	 UNSPEC_TLS_INDIRECT_CALL))
   (clobber (reg:SI GR8_REG))
   (clobber (reg:SI LRREG))
   ;; If there was a way to represent the fact that we don't need GR9
   ;; or GR15 to be set before this instruction (it could be in
   ;; parallel), we could use it here.  This change wouldn't apply to
   ;; call_gettlsoff, thought, since the linker may turn the latter
   ;; into ldi @(gr15,offset),gr9.
   (use (match_operand:SI 1 "register_operand" "D15"))]
  "HAVE_AS_TLS"
  "calll #gettlsoff(%a0)@(gr8,gr0)"
  [(set_attr "length" "4")
   (set_attr "type" "jumpl")])

(define_insn "tls_load_gottlsoff12"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI
	 [(match_operand:SI 1 "symbolic_operand" "")
	  (match_operand:SI 2 "register_operand" "r")]
	 UNSPEC_TLS_LOAD_GOTTLSOFF12))]
  "HAVE_AS_TLS"
  "ldi @(%2, #gottlsoff12(%1)), %0"
  [(set_attr "length" "4")])

(define_expand "tlsoff_hilo"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(high:SI (const:SI (unspec:SI
			    [(match_operand:SI 1 "symbolic_operand" "")
			     (match_operand:SI 2 "immediate_operand" "n")]
			    UNSPEC_GOT))))
   (set (match_dup 0)
	(lo_sum:SI (match_dup 0)
		   (const:SI (unspec:SI [(match_dup 1)
					 (match_dup 3)] UNSPEC_GOT))))]
  ""
  "
{
  operands[3] = GEN_INT (INTVAL (operands[2]) + 1);
}")

;; Just like movdi_ldd, but with relaxation annotations.
(define_insn "tls_tlsdesc_ldd"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(mem:DI (unspec:SI
			     [(match_operand:SI 1 "register_operand" "r")
			      (match_operand:SI 2 "register_operand" "r")
			      (match_operand:SI 3 "symbolic_operand" "")]
			     UNSPEC_TLS_TLSDESC_LDD_AUX))]
		   UNSPEC_TLS_TLSDESC_LDD))]
  ""
  "ldd #tlsdesc(%a3)@(%1,%2), %0"
  [(set_attr "length" "4")
   (set_attr "type" "gload")])

(define_insn "tls_tlsoff_ld"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mem:SI (unspec:SI
		 [(match_operand:SI 1 "register_operand" "r")
		  (match_operand:SI 2 "register_operand" "r")
		  (match_operand:SI 3 "symbolic_operand" "")]
		 UNSPEC_TLS_TLSOFF_LD)))]
  ""
  "ld #tlsoff(%a3)@(%1,%2), %0"
  [(set_attr "length" "4")
   (set_attr "type" "gload")])

(define_insn "tls_lddi"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:SI 1 "symbolic_operand" "")
		    (match_operand:SI 2 "register_operand" "d")]
		   UNSPEC_TLS_LDDI))]
  ""
  "lddi @(%2, #gottlsdesc12(%a1)), %0"
  [(set_attr "length" "4")
   (set_attr "type" "gload")])

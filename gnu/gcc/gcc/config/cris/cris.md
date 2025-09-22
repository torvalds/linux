;; GCC machine description for CRIS cpu cores.
;; Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
;; Free Software Foundation, Inc.
;; Contributed by Axis Communications.

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

;; The original PO technology requires these to be ordered by speed,
;; so that assigner will pick the fastest.

;; See files "md.texi" and "rtl.def" for documentation on define_insn,
;; match_*, et. al.
;;
;; The function cris_notice_update_cc in cris.c handles condition code
;; updates for most instructions, helped by the "cc" attribute.

;; There are several instructions that are orthogonal in size, and seems
;; they could be matched by a single pattern without a specified size
;; for the operand that is orthogonal.  However, this did not work on
;; gcc-2.7.2 (and probably not on gcc-2.8.1), relating to that when a
;; constant is substituted into an operand, the actual mode must be
;; deduced from the pattern.  There is reasonable hope that that has been
;; fixed, so FIXME: try again.

;; You will notice that three-operand alternatives ("=r", "r", "!To")
;; are marked with a "!" constraint modifier to avoid being reloaded
;; into.  This is because gcc would otherwise prefer to use the constant
;; pool and its offsettable address instead of reloading to an
;; ("=r", "0", "i") alternative.  Also, the constant-pool support was not
;; only suboptimal but also buggy in 2.7.2, ??? maybe only in 2.6.3.

;; All insns that look like (set (...) (plus (...) (reg:SI 8)))
;; get problems when reloading r8 (frame pointer) to r14 + offs (stack
;; pointer).  Thus the instructions that get into trouble have specific
;; checks against matching frame_pointer_rtx.
;; ??? But it should be re-checked for gcc > 2.7.2
;; FIXME: This changed some time ago (from 2000-03-16) for gcc-2.9x.

;; FIXME: When PIC, all [rX=rY+S] could be enabled to match
;; [rX=gotless_symbol].
;; The movsi for a gotless symbol could be split (post reload).

;; UNSPEC Usage:
;; 0 PLT reference from call expansion: operand 0 is the address,
;;   the mode is VOIDmode.  Always wrapped in CONST.
;; 1 Stack frame deallocation barrier.
;; 2 The address of the global offset table as a source operand.
;; 3 The address of a global-offset-table-relative symbol + offset.
;; 4 The offset within GOT of a symbol.
;; 5 The offset within GOT of a symbol that has a PLT.

(define_constants ; FIXME: reorder sanely.
  [(CRIS_UNSPEC_PLT 0)
   (CRIS_UNSPEC_FRAME_DEALLOC 1)
   (CRIS_UNSPEC_GOT 2)
   (CRIS_UNSPEC_GOTREL 3)
   (CRIS_UNSPEC_GOTREAD 4)
   (CRIS_UNSPEC_PLTGOTREAD 5)])

;; Register numbers.
(define_constants
  [(CRIS_GOT_REGNUM 0)
   (CRIS_STATIC_CHAIN_REGNUM 7)
   (CRIS_FP_REGNUM 8)
   (CRIS_SP_REGNUM 14)
   (CRIS_SRP_REGNUM 16)
   (CRIS_MOF_REGNUM 17)
   (CRIS_AP_REGNUM 18)
   (CRIS_CC0_REGNUM 19)]
)

;; We need an attribute to define whether an instruction can be put in
;; a branch-delay slot or not, and whether it has a delay slot.
;;
;; Branches and return instructions have a delay slot, and cannot
;; themselves be put in a delay slot.  This has changed *for short
;; branches only* between architecture variants, but the possible win
;; is presumed negligible compared to the added complexity of the machine
;; description: one would have to add always-correct infrastructure to
;; distinguish short branches.
;;
;; Whether an instruction can be put in a delay slot depends on the
;; instruction (all short instructions except jumps and branches)
;; and the addressing mode (must not be prefixed or referring to pc).
;; In short, any "slottable" instruction must be 16 bit and not refer
;; to pc, or alter it.
;;
;; The possible values are "yes", "no" and "has_slot".  Yes/no means if
;; the insn is slottable or not.  Has_slot means that the insn is a
;; return insn or branch insn (which are not considered slottable since
;; that is generally true).  Having the seemingly illogical value
;; "has_slot" means we do not have to add another attribute just to say
;; that an insn has a delay-slot, since it also infers that it is not
;; slottable.  Better names for the attribute were found to be longer and
;; not add readability to the machine description.
;;
;; The default that is defined here for this attribute is "no", not
;; slottable, not having a delay-slot, so there's no need to worry about
;; it being wrong for non-branch and return instructions.
;;  The default could depend on the kind of insn and the addressing
;; mode, but that would need more attributes and hairier, more error
;; prone code.
;;
;;  There is an extra memory constraint, 'Q', which recognizes an indirect
;; register.  The constraints 'Q' and '>' together match all possible
;; memory operands that are slottable.
;;  For other operands, you need to check if it has a valid "slottable"
;; quick-immediate operand, where the particular signedness-variation
;; may match the constraints 'I' or 'J'.), and include it in the
;; constraint pattern for the slottable pattern.  An alternative using
;; only "r" constraints is most often slottable.

(define_attr "slottable" "no,yes,has_slot" (const_string "no"))

;; We also need attributes to sanely determine the condition code
;; state.  See cris_notice_update_cc for how this is used.

(define_attr "cc" "none,clobber,normal" (const_string "normal"))

;; At the moment, this attribute is just used to help bb-reorder do its
;; work; the default 0 doesn't help it.  Many insns have other lengths,
;; though none are shorter.
(define_attr "length" "" (const_int 2))

;; A branch or return has one delay-slot.  The instruction in the
;; delay-slot is always executed, independent of whether the branch is
;; taken or not.  Note that besides setting "slottable" to "has_slot",
;; there also has to be a "%#" at the end of a "delayed" instruction
;; output pattern (for "jump" this means "ba %l0%#"), so print_operand can
;; catch it and print a "nop" if necessary.  This method was stolen from
;; sparc.md.

(define_delay (eq_attr "slottable" "has_slot")
  [(eq_attr "slottable" "yes") (nil) (nil)])

;; Iterator definitions.

;; For the "usual" pattern size alternatives.
(define_mode_macro BWD [SI HI QI])
(define_mode_macro WD [SI HI])
(define_mode_macro BW [HI QI])
(define_mode_attr S [(SI "HI") (HI "QI")])
(define_mode_attr s [(SI "hi") (HI "qi")])
(define_mode_attr m [(SI ".d") (HI ".w") (QI ".b")])
(define_mode_attr mm [(SI ".w") (HI ".b")])
(define_mode_attr nbitsm1 [(SI "31") (HI "15") (QI "7")])

;; For the sign_extend+zero_extend variants.
(define_code_macro szext [sign_extend zero_extend])
(define_code_attr u [(sign_extend "") (zero_extend "u")])
(define_code_attr su [(sign_extend "s") (zero_extend "u")])

;; For the shift variants.
(define_code_macro shift [ashiftrt lshiftrt ashift])
(define_code_macro shiftrt [ashiftrt lshiftrt])
(define_code_attr shlr [(ashiftrt "ashr") (lshiftrt "lshr") (ashift "ashl")])
(define_code_attr slr [(ashiftrt "asr") (lshiftrt "lsr") (ashift "lsl")])

(define_code_macro ncond [eq ne gtu ltu geu leu])
(define_code_macro ocond [gt le])
(define_code_macro rcond [lt ge])
(define_code_attr CC [(eq "eq") (ne "ne") (gt "gt") (gtu "hi") (lt "lt")
		      (ltu "lo") (ge "ge") (geu "hs") (le "le") (leu "ls")])
(define_code_attr rCC [(eq "ne") (ne "eq") (gt "le") (gtu "ls") (lt "ge")
		       (ltu "hs") (ge "lt") (geu "lo") (le "gt") (leu "hi")])
(define_code_attr oCC [(lt "mi") (ge "pl")])
(define_code_attr roCC [(lt "pl") (ge "mi")])

;; Operand and operator predicates.

(include "predicates.md")

;; Test insns.

;; DImode
;;
;; Allow register and offsettable mem operands only; post-increment is
;; not worth the trouble.

(define_insn "tstdi"
  [(set (cc0)
	(match_operand:DI 0 "nonimmediate_operand" "r,o"))]
  ""
  "test.d %M0\;ax\;test.d %H0")

;; No test insns with side-effect on the mem addressing.
;;
;; See note on cmp-insns with side-effects (or lack of them)

;; Normal named test patterns from SI on.
;; FIXME: Seems they should change to be in order smallest..largest.

(define_insn "tst<mode>"
  [(set (cc0)
	(match_operand:BWD 0 "nonimmediate_operand" "r,Q>,m"))]
  ""
  "test<m> %0"
  [(set_attr "slottable" "yes,yes,no")])

;; It seems that the position of the sign-bit and the fact that 0.0 is
;; all 0-bits would make "tstsf" a straight-forward implementation;
;; either "test.d" it for positive/negative or "btstq 30,r" it for
;; zeroness.
;;
;; FIXME: Do that some time; check next_cc0_user to determine if
;; zero or negative is tested for.

;; Compare insns.

;; We could optimize the sizes of the immediate operands for various
;; cases, but that is not worth it because of the very little usage of
;; DImode for anything else but a structure/block-mode.  Just do the
;; obvious stuff for the straight-forward constraint letters.

(define_insn "cmpdi"
  [(set (cc0)
	(compare (match_operand:DI 0 "nonimmediate_operand" "r,r,r,r,r,r,o")
		 (match_operand:DI 1 "general_operand" "K,I,P,n,r,o,r")))]
  ""
  "@
   cmpq %1,%M0\;ax\;cmpq 0,%H0
   cmpq %1,%M0\;ax\;cmpq -1,%H0
   cmp%e1.%z1 %1,%M0\;ax\;cmpq %H1,%H0
   cmp.d %M1,%M0\;ax\;cmp.d %H1,%H0
   cmp.d %M1,%M0\;ax\;cmp.d %H1,%H0
   cmp.d %M1,%M0\;ax\;cmp.d %H1,%H0
   cmp.d %M0,%M1\;ax\;cmp.d %H0,%H1")

;; Note that compare insns with side effect addressing mode (e.g.):
;;
;; cmp.S [rx=ry+i],rz;
;; cmp.S [%3=%1+%2],%0
;;
;; are *not* usable for gcc since the reloader *does not accept*
;; cc0-changing insns with side-effects other than setting the condition
;; codes.  The reason is that the reload stage *may* cause another insn to
;; be output after the main instruction, in turn invalidating cc0 for the
;; insn using the test.  (This does not apply to the CRIS case, since a
;; reload for output -- move to memory -- does not change the condition
;; code.  Unfortunately we have no way to describe that at the moment.  I
;; think code would improve being in the order of one percent faster.

;; We have cmps and cmpu (compare reg w. sign/zero extended mem).
;; These are mostly useful for compares in SImode, using 8 or 16-bit
;; constants, but sometimes gcc will find its way to use it for other
;; (memory) operands.  Avoid side-effect patterns, though (see above).

(define_insn "*cmp_ext<mode>"
  [(set (cc0)
	(compare
	 (match_operand:SI 0 "register_operand" "r,r")
	 (match_operator:SI 2 "cris_extend_operator"
			 [(match_operand:BW 1 "memory_operand" "Q>,m")])))]
  ""
  "cmp%e2<m> %1,%0"
  [(set_attr "slottable" "yes,no")])

;; Swap operands; it seems the canonical look (if any) is not enforced.
;;
;; FIXME: Investigate that.

(define_insn "*cmp_swapext<mode>"
  [(set (cc0)
	(compare
	 (match_operator:SI 2 "cris_extend_operator"
			    [(match_operand:BW 0 "memory_operand" "Q>,m")])
	 (match_operand:SI 1 "register_operand" "r,r")))]
  ""
  "cmp%e2<m> %0,%1" ; The function cris_notice_update_cc knows about
		     ; swapped operands to compares.
  [(set_attr "slottable" "yes,no")])

;; The "normal" compare patterns, from SI on.

(define_insn "cmpsi"
  [(set (cc0)
	(compare
	 (match_operand:SI 0 "nonimmediate_operand" "r,r,r, r,Q>,Q>,r,r,m,m")
	 (match_operand:SI 1 "general_operand"	    "I,r,Q>,M,M, r, P,g,M,r")))]
  ""
  "@
   cmpq %1,%0
   cmp.d %1,%0
   cmp.d %1,%0
   test.d %0
   test.d %0
   cmp.d %0,%1
   cmp%e1.%z1 %1,%0
   cmp.d %1,%0
   test.d %0
   cmp.d %0,%1"
  [(set_attr "slottable" "yes,yes,yes,yes,yes,yes,no,no,no,no")])

(define_insn "cmp<mode>"
  [(set (cc0)
	(compare
	 (match_operand:BW 0 "nonimmediate_operand" "r,r, r,Q>,Q>,r,m,m")
	 (match_operand:BW 1 "general_operand"	    "r,Q>,M,M, r, g,M,r")))]
  ""
  "@
   cmp<m> %1,%0
   cmp<m> %1,%0
   test<m> %0
   test<m> %0
   cmp<m> %0,%1
   cmp<m> %1,%0
   test<m> %0
   cmp<m> %0,%1"
  [(set_attr "slottable" "yes,yes,yes,yes,yes,no,no,no")])

;; Pattern matching the BTST insn.
;; It is useful for "if (i & val)" constructs, where val is an exact
;; power of 2, or if val + 1 is a power of two, where we check for a bunch
;; of zeros starting at bit 0).

;; SImode.  This mode is the only one needed, since gcc automatically
;; extends subregs for lower-size modes.  FIXME: Add testcase.
(define_insn "*btst"
  [(set (cc0)
	(zero_extract
	 (match_operand:SI 0 "nonmemory_operand" "r,r,r,r,r,r,n")
	 (match_operand:SI 1 "const_int_operand" "K,n,K,n,K,n,n")
	 (match_operand:SI 2 "nonmemory_operand" "M,M,K,n,r,r,r")))]
  ;; Either it is a single bit, or consecutive ones starting at 0.
  "GET_CODE (operands[1]) == CONST_INT
   && (operands[1] == const1_rtx || operands[2] == const0_rtx)
   && (REG_S_P (operands[0])
       || (operands[1] == const1_rtx
	   && REG_S_P (operands[2])
	   && GET_CODE (operands[0]) == CONST_INT
	   && exact_log2 (INTVAL (operands[0])) >= 0))"

;; The last "&&" condition above should be caught by some kind of
;; canonicalization in gcc, but we can easily help with it here.
;;  It results from expressions of the type
;; "power_of_2_value & (1 << y)".
;;
;; Since there may be codes with tests in on bits (in constant position)
;; beyond the size of a word, handle that by assuming those bits are 0.
;; GCC should handle that, but it's a matter of easily-added belts while
;; having suspenders.

  "@
   btstq (%1-1),%0
   test.d %0
   btstq %2,%0
   clearf nz
   btst %2,%0
   clearf nz
   cmpq %p0,%2"
 [(set_attr "slottable" "yes")])

;; Move insns.

;; The whole mandatory movdi family is here; expander, "anonymous"
;; recognizer and splitter.  We're forced to have a movdi pattern,
;; although GCC should be able to split it up itself.  Normally it can,
;; but if other insns have DI operands (as is the case here), reload
;; must be able to generate or match a movdi.  many testcases fail at
;; -O3 or -fssa if we don't have this.  FIXME: Fix GCC...  See
;; <URL:http://gcc.gnu.org/ml/gcc-patches/2000-04/msg00104.html>.
;; However, a patch from Richard Kenner (similar to the cause of
;; discussion at the URL above), indicates otherwise.  See
;; <URL:http://gcc.gnu.org/ml/gcc-patches/2000-04/msg00554.html>.
;; The truth has IMO is not been decided yet, so check from time to
;; time by disabling the movdi patterns.

;; To appease testcase gcc.c-torture/execute/920501-2.c (and others) at
;; -O0, we need a movdi as a temporary measure.  Here's how things fail:
;;  A cmpdi RTX needs reloading (global):
;;    (insn 185 326 186 (set (cc0)
;;	    (compare (mem/f:DI (reg/v:SI 22) 0)
;;		(const_int 1 [0x1]))) 4 {cmpdi} (nil)
;;	(nil))
;; Now, reg 22 is reloaded for input address, and the mem is also moved
;; out of the instruction (into a register), since one of the operands
;; must be a register.  Reg 22 is reloaded (into reg 10), and the mem is
;; moved out and synthesized in SImode parts (reg 9, reg 10 - should be ok
;; wrt. overlap).  The bad things happen with the synthesis in
;; emit_move_insn_1; the location where to substitute reg 10 is lost into
;; two new RTX:es, both still having reg 22.  Later on, the left-over reg
;; 22 is recognized to have an equivalent in memory which is substituted
;; straight in, and we end up with an unrecognizable insn:
;;    (insn 325 324 326 (set (reg:SI 9 r9)
;;    	      (mem/f:SI (mem:SI (plus:SI (reg:SI 8 r8)
;;    			  (const_int -84 [0xffffffac])) 0) 0)) -1 (nil)
;;    	  (nil))
;; which is the first part of the reloaded synthesized "movdi".
;;  The right thing would be to add equivalent replacement locations for
;; insn with pseudos that need more reloading.  The question is where.

(define_expand "movdi"
  [(set (match_operand:DI 0 "nonimmediate_operand" "")
	(match_operand:DI 1 "general_operand" ""))]
  ""
{
  if (GET_CODE (operands[0]) == MEM && operands[1] != const0_rtx)
    operands[1] = copy_to_mode_reg (DImode, operands[1]);

  /* Some other ports (as of 2001-09-10 for example mcore and romp) also
     prefer to split up constants early, like this.  The testcase in
     gcc.c-torture/execute/961213-1.c shows that CSE2 gets confused by the
     resulting subreg sets when using the construct from mcore (as of FSF
     CVS, version -r 1.5), and it believes that the high part (the last one
     emitted) is the final value.  This construct from romp seems more
     robust, especially considering the head comments from
     emit_no_conflict_block.  */
  if ((GET_CODE (operands[1]) == CONST_INT
       || GET_CODE (operands[1]) == CONST_DOUBLE)
      && ! reload_completed
      && ! reload_in_progress)
    {
      rtx insns;
      rtx op0 = operands[0];
      rtx op1 = operands[1];

      start_sequence ();
      emit_move_insn (operand_subword (op0, 0, 1, DImode),
		      operand_subword (op1, 0, 1, DImode));
      emit_move_insn (operand_subword (op0, 1, 1, DImode),
		      operand_subword (op1, 1, 1, DImode));
      insns = get_insns ();
      end_sequence ();

      emit_no_conflict_block (insns, op0, op1, 0, op1);
      DONE;
    }
})

(define_insn "*movdi_insn"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=r,rx,m")
	(match_operand:DI 1 "general_operand" "rx,g,rxM"))]
  "register_operand (operands[0], DImode)
   || register_operand (operands[1], DImode)
   || operands[1] == const0_rtx"
  "#")

(define_split
  [(set (match_operand:DI 0 "nonimmediate_operand" "")
	(match_operand:DI 1 "general_operand" ""))]
  "reload_completed"
  [(match_dup 2)]
  "operands[2] = cris_split_movdx (operands);")

;; Side-effect patterns for move.S1 [rx=ry+rx.S2],rw
;; and move.S1 [rx=ry+i],rz
;;  Then movs.S1 and movu.S1 for both modes.
;;
;; move.S1 [rx=ry+rz.S],rw avoiding when rx is ry, or rw is rx
;; FIXME: These could have anonymous mode for operand 0.

(define_insn "*mov_side<mode>_biap"
  [(set (match_operand:BW 0 "register_operand" "=r,r")
	(mem:BW (plus:SI
		 (mult:SI (match_operand:SI 1 "register_operand" "r,r")
			  (match_operand:SI 2 "const_int_operand" "n,n"))
		 (match_operand:SI 3 "register_operand" "r,r"))))
   (set (match_operand:SI 4 "register_operand" "=*3,r")
	(plus:SI (mult:SI (match_dup 1)
			  (match_dup 2))
		 (match_dup 3)))]
  "cris_side_effect_mode_ok (MULT, operands, 4, 3, 1, 2, 0)"
  "@
   #
   move<m> [%4=%3+%1%T2],%0")

(define_insn "*mov_sidesisf_biap"
  [(set (match_operand 0 "register_operand" "=r,r,x,x")
	(mem (plus:SI
	      (mult:SI (match_operand:SI 1 "register_operand" "r,r,r,r")
		       (match_operand:SI 2 "const_int_operand" "n,n,n,n"))
	      (match_operand:SI 3 "register_operand" "r,r,r,r"))))
   (set (match_operand:SI 4 "register_operand" "=*3,r,*3,r")
	(plus:SI (mult:SI (match_dup 1)
			  (match_dup 2))
		 (match_dup 3)))]
  "GET_MODE_SIZE (GET_MODE (operands[0])) == UNITS_PER_WORD
   && cris_side_effect_mode_ok (MULT, operands, 4, 3, 1, 2, 0)"
  "@
   #
   move.%s0 [%4=%3+%1%T2],%0
   #
   move [%4=%3+%1%T2],%0")

;; move.S1 [rx=ry+i],rz
;; avoiding move.S1 [ry=ry+i],rz
;; and      move.S1 [rz=ry+i],rz
;; Note that "i" is allowed to be a register.

(define_insn "*mov_side<mode>"
  [(set (match_operand:BW 0 "register_operand" "=r,r,r,r,r")
	(mem:BW
	 (plus:SI (match_operand:SI 1 "cris_bdap_operand" "%r,r,r,R,R")
		  (match_operand:SI 2 "cris_bdap_operand" "r>Rn,r,>Rn,r,r"))))
   (set (match_operand:SI 3 "register_operand" "=*1,r,r,*2,r")
	(plus:SI (match_dup 1)
		 (match_dup 2)))]
  "cris_side_effect_mode_ok (PLUS, operands, 3, 1, 2, -1, 0)"
{
  if ((which_alternative == 0 || which_alternative == 3)
      && (GET_CODE (operands[2]) != CONST_INT
	  || INTVAL (operands[2]) > 127
	  || INTVAL (operands[2]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'J')))
    return "#";
  if (which_alternative == 4)
    return "move<m> [%3=%2%S1],%0";
  return "move<m> [%3=%1%S2],%0";
})

(define_insn "*mov_sidesisf"
  [(set (match_operand 0 "register_operand" "=r,r,r,x,x,x,r,r,x,x")
	(mem
	 (plus:SI
	  (match_operand:SI 1 "cris_bdap_operand" "%r,r,r,r,r,r,R,R,R,R")
	  (match_operand:SI 2 "cris_bdap_operand" "r>Rn,r,>Rn,r>Rn,r,>Rn,r,r,r,r"))))
   (set (match_operand:SI 3 "register_operand" "=*1,r,r,*1,r,r,*2,r,*2,r")
	(plus:SI (match_dup 1)
		 (match_dup 2)))]
  "GET_MODE_SIZE (GET_MODE (operands[0])) == UNITS_PER_WORD
   && cris_side_effect_mode_ok (PLUS, operands, 3, 1, 2, -1, 0)"
{
  if ((which_alternative == 0
       || which_alternative == 3
       || which_alternative == 6
       || which_alternative == 8)
      && (GET_CODE (operands[2]) != CONST_INT
	  || INTVAL (operands[2]) > 127
	  || INTVAL (operands[2]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'J')))
    return "#";
  if (which_alternative < 3)
    return "move.%s0 [%3=%1%S2],%0";
  if (which_alternative == 7)
    return "move.%s0 [%3=%2%S1],%0";
  if (which_alternative == 9)
    return "move [%3=%2%S1],%0";
  return "move [%3=%1%S2],%0";
})

;; Other way around; move to memory.

;; Note that the condition (which for side-effect patterns is usually a
;; call to cris_side_effect_mode_ok), isn't consulted for register
;; allocation preferences -- constraints is the method for that.  The
;; drawback is that we can't exclude register allocation to cause
;; "move.s rw,[rx=ry+rz.S]" when rw==rx without also excluding rx==ry or
;; rx==rz if we use an earlyclobber modifier for the constraint for rx.
;; Instead of that, we recognize and split the cases where dangerous
;; register combinations are spotted: where a register is set in the
;; side-effect, and used in the main insn.  We don't handle the case where
;; the set in the main insn overlaps the set in the side-effect; that case
;; must be handled in gcc.  We handle just the case where the set in the
;; side-effect overlaps the input operand of the main insn (i.e. just
;; moves to memory).

;;
;; move.s rz,[ry=rx+rw.S]

(define_insn "*mov_side<mode>_biap_mem"
  [(set (mem:BW (plus:SI
		 (mult:SI (match_operand:SI 0 "register_operand" "r,r,r")
			  (match_operand:SI 1 "const_int_operand" "n,n,n"))
		 (match_operand:SI 2 "register_operand" "r,r,r")))
	(match_operand:BW 3 "register_operand" "r,r,r"))
   (set (match_operand:SI 4 "register_operand" "=*2,!3,r")
	(plus:SI (mult:SI (match_dup 0)
			  (match_dup 1))
		 (match_dup 2)))]
  "cris_side_effect_mode_ok (MULT, operands, 4, 2, 0, 1, 3)"
  "@
   #
   #
   move<m> %3,[%4=%2+%0%T1]")

(define_insn "*mov_sidesisf_biap_mem"
  [(set (mem (plus:SI
	      (mult:SI (match_operand:SI 0 "register_operand" "r,r,r,r,r,r")
		       (match_operand:SI 1 "const_int_operand" "n,n,n,n,n,n"))
	      (match_operand:SI 2 "register_operand" "r,r,r,r,r,r")))
	(match_operand 3 "register_operand" "r,r,r,x,x,x"))
   (set (match_operand:SI 4 "register_operand" "=*2,!3,r,*2,!3,r")
	(plus:SI (mult:SI (match_dup 0)
			  (match_dup 1))
		 (match_dup 2)))]
  "GET_MODE_SIZE (GET_MODE (operands[3])) == UNITS_PER_WORD
   && cris_side_effect_mode_ok (MULT, operands, 4, 2, 0, 1, 3)"
  "@
   #
   #
   move.%s3 %3,[%4=%2+%0%T1]
   #
   #
   move %3,[%4=%2+%0%T1]")

;; Split for the case above where we're out of luck with register
;; allocation (again, the condition isn't checked for that), and we end up
;; with the set in the side-effect getting the same register as the input
;; register.

(define_split
  [(parallel
    [(set (match_operator
	   6 "cris_mem_op"
	   [(plus:SI
	     (mult:SI (match_operand:SI 0 "register_operand" "")
		      (match_operand:SI 1 "const_int_operand" ""))
	     (match_operand:SI 2 "register_operand" ""))])
	  (match_operand 3 "register_operand" ""))
     (set (match_operand:SI 4 "register_operand" "")
	  (plus:SI (mult:SI (match_dup 0)
			    (match_dup 1))
		   (match_dup 2)))])]
  "reload_completed && reg_overlap_mentioned_p (operands[4], operands[3])"
  [(set (match_dup 5) (match_dup 3))
   (set (match_dup 4) (match_dup 2))
   (set (match_dup 4)
	(plus:SI (mult:SI (match_dup 0)
			  (match_dup 1))
		 (match_dup 4)))]
  "operands[5]
     = replace_equiv_address (operands[6],
			      gen_rtx_PLUS (SImode,
					    gen_rtx_MULT (SImode,
							  operands[0],
							  operands[1]),
					    operands[2]));")

;; move.s rx,[ry=rz+i]
;; FIXME: These could have anonymous mode for operand 2.

;; QImode

(define_insn "*mov_side<mode>_mem"
  [(set (mem:BW
	 (plus:SI (match_operand:SI 0 "cris_bdap_operand" "%r,r,r,r,R,R,R")
		  (match_operand:SI 1 "cris_bdap_operand" "r>Rn,r>Rn,r,>Rn,r,r,r")))
	(match_operand:BW 2 "register_operand" "r,r,r,r,r,r,r"))
   (set (match_operand:SI 3 "register_operand" "=*0,!*2,r,r,*1,!*2,r")
	(plus:SI (match_dup 0)
		 (match_dup 1)))]
  "cris_side_effect_mode_ok (PLUS, operands, 3, 0, 1, -1, 2)"
{
  if ((which_alternative == 0 || which_alternative == 4)
      && (GET_CODE (operands[1]) != CONST_INT
	  || INTVAL (operands[1]) > 127
	  || INTVAL (operands[1]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[1]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[1]), 'J')))
    return "#";
  if (which_alternative == 1 || which_alternative == 5)
    return "#";
  if (which_alternative == 6)
    return "move.%s2 %2,[%3=%1%S0]";
  return "move<m> %2,[%3=%0%S1]";
})

;; SImode

(define_insn "*mov_sidesisf_mem"
  [(set (mem
	 (plus:SI
	  (match_operand:SI
	   0 "cris_bdap_operand"
	   			"%r,  r,   r,r,  r,   r,r,  R,R,  R,R, R")
	  (match_operand:SI
	   1 "cris_bdap_operand"
	   			"r>Rn,r>Rn,r,>Rn,r>Rn,r,>Rn,r,r,  r,r, r")))
	(match_operand 2 "register_operand"
		       		"r,   r,   r,r,  x,   x,x,  r,r,  r,x, x"))
   (set (match_operand:SI 3 "register_operand"
			  	"=*0,!2,   r,r,  *0,  r,r, *1,!*2,r,*1,r")
	(plus:SI (match_dup 0)
		 (match_dup 1)))]
  "GET_MODE_SIZE (GET_MODE (operands[2])) == UNITS_PER_WORD
   && cris_side_effect_mode_ok (PLUS, operands, 3, 0, 1, -1, 2)"
{
  if ((which_alternative == 0 || which_alternative == 4)
      && (GET_CODE (operands[1]) != CONST_INT
	  || INTVAL (operands[1]) > 127
	  || INTVAL (operands[1]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[1]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[1]), 'J')))
    return "#";
  if (which_alternative == 1
      || which_alternative == 7
      || which_alternative == 8
      || which_alternative == 10)
    return "#";
  if (which_alternative < 4)
    return "move.%s2 %2,[%3=%0%S1]";
  if (which_alternative == 9)
    return "move.%s2 %2,[%3=%1%S0]";
  if (which_alternative == 11)
    return "move %2,[%3=%1%S0]";
  return "move %2,[%3=%0%S1]";
})

;; Like the biap case, a split where the set in the side-effect gets the
;; same register as the input register to the main insn, since the
;; condition isn't checked at register allocation.

(define_split
  [(parallel
    [(set (match_operator
	   4 "cris_mem_op"
	   [(plus:SI
	     (match_operand:SI 0 "cris_bdap_operand" "")
	     (match_operand:SI 1 "cris_bdap_operand" ""))])
	  (match_operand 2 "register_operand" ""))
     (set (match_operand:SI 3 "register_operand" "")
	  (plus:SI (match_dup 0) (match_dup 1)))])]
  "reload_completed && reg_overlap_mentioned_p (operands[3], operands[2])"
  [(set (match_dup 4) (match_dup 2))
   (set (match_dup 3) (match_dup 0))
   (set (match_dup 3) (plus:SI (match_dup 3) (match_dup 1)))]
  "")

;; Clear memory side-effect patterns.  It is hard to get to the mode if
;; the MEM was anonymous, so there will be one for each mode.

;;  clear.[bwd] [ry=rx+rw.s2]

(define_insn "*clear_side<mode>_biap"
  [(set (mem:BWD (plus:SI
		  (mult:SI (match_operand:SI 0 "register_operand" "r,r")
			   (match_operand:SI 1 "const_int_operand" "n,n"))
		  (match_operand:SI 2 "register_operand" "r,r")))
	(const_int 0))
   (set (match_operand:SI 3 "register_operand" "=*2,r")
	(plus:SI (mult:SI (match_dup 0)
			  (match_dup 1))
		 (match_dup 2)))]
  "cris_side_effect_mode_ok (MULT, operands, 3, 2, 0, 1, -1)"
  "@
   #
   clear<m> [%3=%2+%0%T1]")

;; clear.[bwd] [ry=rz+i]

(define_insn "*clear_side<mode>"
  [(set (mem:BWD
	 (plus:SI (match_operand:SI 0 "cris_bdap_operand" "%r,r,r,R,R")
		  (match_operand:SI 1 "cris_bdap_operand" "r>Rn,r,>Rn,r,r")))
	(const_int 0))
   (set (match_operand:SI 2 "register_operand" "=*0,r,r,*1,r")
	(plus:SI (match_dup 0)
		 (match_dup 1)))]
  "cris_side_effect_mode_ok (PLUS, operands, 2, 0, 1, -1, -1)"
{
  if ((which_alternative == 0 || which_alternative == 3)
      && (GET_CODE (operands[1]) != CONST_INT
	  || INTVAL (operands[1]) > 127
	  || INTVAL (operands[1]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[1]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[1]), 'J')))
    return "#";
  if (which_alternative == 4)
    return "clear<m> [%2=%1%S0]";
  return "clear<m> [%2=%0%S1]";
})

;; Normal move patterns from SI on.

(define_expand "movsi"
  [(set
    (match_operand:SI 0 "nonimmediate_operand" "")
    (match_operand:SI 1 "cris_general_operand_or_symbol" ""))]
  ""
{
  /* If the output goes to a MEM, make sure we have zero or a register as
     input.  */
  if (GET_CODE (operands[0]) == MEM
      && ! REG_S_P (operands[1])
      && operands[1] != const0_rtx
      && ! no_new_pseudos)
    operands[1] = force_reg (SImode, operands[1]);

  /* If we're generating PIC and have an incoming symbol, validize it to a
     general operand or something that will match a special pattern.

     FIXME: Do we *have* to recognize anything that would normally be a
     valid symbol?  Can we exclude global PIC addresses with an added
     offset?  */
    if (flag_pic
	&& CONSTANT_ADDRESS_P (operands[1])
	&& !cris_valid_pic_const (operands[1]))
      {
	enum cris_pic_symbol_type t = cris_pic_symbol_type_of (operands[1]);

	gcc_assert (t != cris_no_symbol);

	if (! REG_S_P (operands[0]))
	  {
	    /* We must have a register as destination for what we're about to
	       do, and for the patterns we generate.  */
	    CRIS_ASSERT (!no_new_pseudos);
	    operands[1] = force_reg (SImode, operands[1]);
	  }
	else
	  {
	    /* FIXME: add a REG_EQUAL (or is it REG_EQUIV) note to the
	       destination register for the symbol.  It might not be
	       worth it.  Measure.  */
	    current_function_uses_pic_offset_table = 1;
	    if (t == cris_gotrel_symbol)
	      {
		/* Change a "move.d sym(+offs),rN" into (allocate register rM)
		   "move.d (const (plus (unspec [sym]
		    CRIS_UNSPEC_GOTREL) offs)),rM" "add.d rPIC,rM,rN"  */
		rtx tem, rm, rn = operands[0];
		rtx sym = GET_CODE (operands[1]) != CONST
		  ? operands[1] : get_related_value (operands[1]);
		HOST_WIDE_INT offs = get_integer_term (operands[1]);

		gcc_assert (! no_new_pseudos);
		tem = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, sym),
				      CRIS_UNSPEC_GOTREL);
		if (offs != 0)
		  tem = plus_constant (tem, offs);
		rm = gen_reg_rtx (Pmode);
		emit_move_insn (rm, gen_rtx_CONST (Pmode, tem));
	        if (expand_binop (Pmode, add_optab, rm, pic_offset_table_rtx,
				  rn, 0, OPTAB_LIB_WIDEN) != rn)
		  internal_error ("expand_binop failed in movsi gotrel");
		DONE;
	      }
	    else if (t == cris_got_symbol)
	      {
		/* Change a "move.d sym,rN" into (allocate register rM, rO)
		   "move.d (const (unspec [sym] CRIS_UNSPEC_GOTREAD)),rM"
		   "add.d rPIC,rM,rO", "move.d [rO],rN" with
		   the memory access marked as read-only.  */
		rtx tem, mem, rm, ro, rn = operands[0];
		gcc_assert (! no_new_pseudos);
		tem = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, operands[1]),
				      CRIS_UNSPEC_GOTREAD);
		rm = gen_reg_rtx (Pmode);
		emit_move_insn (rm, gen_rtx_CONST (Pmode, tem));
		ro = gen_reg_rtx (Pmode);
	        if (expand_binop (Pmode, add_optab, rm, pic_offset_table_rtx,
				  ro, 0, OPTAB_LIB_WIDEN) != ro)
		  internal_error ("expand_binop failed in movsi got");
		mem = gen_rtx_MEM (Pmode, ro);

		/* This MEM doesn't alias anything.  Whether it
		   aliases other same symbols is unimportant.  */
		set_mem_alias_set (mem, new_alias_set ());
		MEM_NOTRAP_P (mem) = 1;
		MEM_READONLY_P (mem) = 1;
		emit_move_insn (rn, mem);
		DONE;
	      }
	    else
	      {
		/* We get here when we have to change something that would
		   be recognizable if it wasn't PIC.  A ``sym'' is ok for
		   PIC symbols both with and without a GOT entry.  And ``sym
		   + offset'' is ok for local symbols, so the only thing it
		   could be, is a global symbol with an offset.  Check and
		   abort if not.  */
		rtx reg = gen_reg_rtx (Pmode);
		rtx sym = get_related_value (operands[1]);
		HOST_WIDE_INT offs = get_integer_term (operands[1]);

		gcc_assert (! no_new_pseudos
			    && t == cris_got_symbol_needing_fixup
			    && sym != NULL_RTX && offs != 0);

		emit_move_insn (reg, sym);
		if (expand_binop (SImode, add_optab, reg,
				  GEN_INT (offs), operands[0], 0,
				  OPTAB_LIB_WIDEN) != operands[0])
		  internal_error ("expand_binop failed in movsi got+offs");
		DONE;
	      }
	  }
      }
})

(define_insn "*movsi_got_load"
  [(set (reg:SI CRIS_GOT_REGNUM) (unspec:SI [(const_int 0)] CRIS_UNSPEC_GOT))]
  "flag_pic"
  "move.d $pc,%:\;sub.d .:GOTOFF,%:"
  [(set_attr "cc" "clobber")])

(define_insn "*movsi_internal"
  [(set
    (match_operand:SI 0 "nonimmediate_operand" "=r,r, r,Q>,r,Q>,g,r,r, r,g,rQ>,x,  m,x")
    ;; Note that we prefer not to use the S alternative (if for some reason
    ;; it competes with others), but g matches S.
    (match_operand:SI 1 "general_operand"	"r,Q>,M,M, I,r, M,n,!S,g,r,x,  rQ>,x,gi"))]
  ""
{
  /* Better to have c-switch here; it is worth it to optimize the size of
     move insns.  The alternative would be to try to find more constraint
     letters.  FIXME: Check again.  It seems this could shrink a bit.  */
  switch (which_alternative)
    {
    case 0:
    case 1:
    case 5:
    case 9:
    case 10:
      return "move.d %1,%0";

    case 11:
    case 12:
    case 13:
    case 14:
      return "move %d1,%0";

    case 2:
    case 3:
    case 6:
      return "clear.d %0";

      /* Constants -32..31 except 0.  */
    case 4:
      return "moveq %1,%0";

      /* We can win a little on constants -32768..-33, 32..65535.  */
    case 7:
      if (INTVAL (operands[1]) > 0 && INTVAL (operands[1]) < 65536)
	{
	  if (INTVAL (operands[1]) < 256)
	    return "movu.b %1,%0";
	  return "movu.w %1,%0";
	}
      else if (INTVAL (operands[1]) >= -32768 && INTVAL (operands[1]) < 32768)
	{
	  if (INTVAL (operands[1]) >= -128 && INTVAL (operands[1]) < 128)
	    return "movs.b %1,%0";
	  return "movs.w %1,%0";
	}
      return "move.d %1,%0";

    case 8:
      {
	rtx tem = operands[1];
	gcc_assert (GET_CODE (tem) == CONST);
	tem = XEXP (tem, 0);
	if (GET_CODE (tem) == PLUS
	    && GET_CODE (XEXP (tem, 0)) == UNSPEC
	    && XINT (XEXP (tem, 0), 1) == CRIS_UNSPEC_GOTREL
	    && GET_CODE (XEXP (tem, 1)) == CONST_INT)
	  tem = XEXP (tem, 0);
	gcc_assert (GET_CODE (tem) == UNSPEC);
	switch (XINT (tem, 1))
	  {
	  case CRIS_UNSPEC_GOTREAD:
	  case CRIS_UNSPEC_PLTGOTREAD:
	    /* Using sign-extend mostly to be consistent with the
	       indexed addressing mode.  */
	    if (flag_pic == 1)
	      return "movs.w %1,%0";
	  case CRIS_UNSPEC_GOTREL:
	  case CRIS_UNSPEC_PLT:
	    return "move.d %1,%0";

	  default:
	    gcc_unreachable ();
	  }
      }
    default:
      return "BOGUS: %1 to %0";
    }
}
  [(set_attr "slottable" "yes,yes,yes,yes,yes,yes,no,no,no,no,no,yes,yes,no,no")
   (set_attr "cc" "*,*,*,*,*,*,*,*,*,*,*,none,none,none,none")])

;; Extend operations with side-effect from mem to register, using
;; MOVS/MOVU.  These are from mem to register only.
;;
;; [rx=ry+rz.S]
;;
;; QImode to HImode
;;
;; FIXME: Can we omit extend to HImode, since GCC should truncate for
;; HImode by itself?  Perhaps use only anonymous modes?

(define_insn "*ext_sideqihi_biap"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(match_operator:HI
	 5 "cris_extend_operator"
	 [(mem:QI (plus:SI
		   (mult:SI (match_operand:SI 1 "register_operand" "r,r")
			    (match_operand:SI 2 "const_int_operand" "n,n"))
		   (match_operand:SI 3 "register_operand" "r,r")))]))
   (set (match_operand:SI 4 "register_operand" "=*3,r")
	(plus:SI (mult:SI (match_dup 1)
			  (match_dup 2))
		 (match_dup 3)))]
  "cris_side_effect_mode_ok (MULT, operands, 4, 3, 1, 2, 0)"
  "@
   #
   mov%e5.%m5 [%4=%3+%1%T2],%0")

(define_insn "*ext_side<mode>si_biap"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(match_operator:SI
	 5 "cris_extend_operator"
	 [(mem:BW (plus:SI
		   (mult:SI (match_operand:SI 1 "register_operand" "r,r")
			    (match_operand:SI 2 "const_int_operand" "n,n"))
		   (match_operand:SI 3 "register_operand" "r,r")))]))
   (set (match_operand:SI 4 "register_operand" "=*3,r")
	(plus:SI (mult:SI (match_dup 1)
			  (match_dup 2))
		 (match_dup 3)))]
  "cris_side_effect_mode_ok (MULT, operands, 4, 3, 1, 2, 0)"
  "@
   #
   mov%e5<m> [%4=%3+%1%T2],%0")

;; Same but [rx=ry+i]

;; QImode to HImode

(define_insn "*ext_sideqihi"
  [(set (match_operand:HI 0 "register_operand" "=r,r,r,r,r")
	(match_operator:HI
	 4 "cris_extend_operator"
	 [(mem:QI (plus:SI
		   (match_operand:SI 1 "cris_bdap_operand" "%r,r,r,R,R")
		   (match_operand:SI 2 "cris_bdap_operand" "r>Rn,r,>Rn,r,r")))]))
   (set (match_operand:SI 3 "register_operand" "=*1,r,r,*2,r")
	(plus:SI (match_dup 1)
		 (match_dup 2)))]
  "cris_side_effect_mode_ok (PLUS, operands, 3, 1, 2, -1, 0)"
{
  if ((which_alternative == 0 || which_alternative == 3)
      && (GET_CODE (operands[2]) != CONST_INT
	  || INTVAL (operands[2]) > 127
	  || INTVAL (operands[2]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'J')))
    return "#";
  if (which_alternative == 4)
    return "mov%e4.%m4 [%3=%2%S1],%0";
  return "mov%e4.%m4 [%3=%1%S2],%0";
})

(define_insn "*ext_side<mode>si"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r,r")
	(match_operator:SI
	 4 "cris_extend_operator"
	 [(mem:BW (plus:SI
		   (match_operand:SI 1 "cris_bdap_operand" "%r,r,r,R,R")
		   (match_operand:SI 2 "cris_bdap_operand" "r>Rn,r,>Rn,r,r")))]))
   (set (match_operand:SI 3 "register_operand" "=*1,r,r,*2,r")
	(plus:SI (match_dup 1)
		 (match_dup 2)))]
  "cris_side_effect_mode_ok (PLUS, operands, 3, 1, 2, -1, 0)"
{
  if ((which_alternative == 0 || which_alternative == 3)
      && (GET_CODE (operands[2]) != CONST_INT
	  || INTVAL (operands[2]) > 127
	  || INTVAL (operands[2]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'J')))
    return "#";
  if (which_alternative == 4)
    return "mov%e4<m> [%3=%2%S1],%0";
  return "mov%e4<m> [%3=%1%S2],%0";
})

;; FIXME: See movsi.

(define_insn "movhi"
  [(set
    (match_operand:HI 0 "nonimmediate_operand" "=r,r, r,Q>,r,Q>,r,r,r,g,g,r,r,x")
    (match_operand:HI 1 "general_operand"	"r,Q>,M,M, I,r, L,O,n,M,r,g,x,r"))]
  ""
{
  switch (which_alternative)
    {
    case 0:
    case 1:
    case 5:
    case 10:
    case 11:
      return "move.w %1,%0";
    case 12:
    case 13:
      return "move %1,%0";
    case 2:
    case 3:
    case 9:
      return "clear.w %0";
    case 4:
      return "moveq %1,%0";
    case 6:
    case 8:
      if (INTVAL (operands[1]) < 256 && INTVAL (operands[1]) >= -128)
	{
	  if (INTVAL (operands[1]) > 0)
	    return "movu.b %1,%0";
	  return "movs.b %1,%0";
	}
      return "move.w %1,%0";
    case 7:
      return "movEq %b1,%0";
    default:
      return "BOGUS: %1 to %0";
  }
}
  [(set_attr "slottable" "yes,yes,yes,yes,yes,yes,no,yes,no,no,no,no,yes,yes")
   (set_attr "cc" "*,*,none,none,*,none,*,clobber,*,none,none,*,none,none")])

(define_insn "movstricthi"
  [(set
    (strict_low_part
     (match_operand:HI 0 "nonimmediate_operand" "+r,r, r,Q>,Q>,g,r,g"))
    (match_operand:HI 1 "general_operand"	 "r,Q>,M,M, r, M,g,r"))]
  ""
  "@
   move.w %1,%0
   move.w %1,%0
   clear.w %0
   clear.w %0
   move.w %1,%0
   clear.w %0
   move.w %1,%0
   move.w %1,%0"
  [(set_attr "slottable" "yes,yes,yes,yes,yes,no,no,no")])

(define_expand "reload_in<mode>"
  [(set (match_operand:BW 2 "register_operand" "=r")
	(match_operand:BW 1 "memory_operand" "m"))
   (set (match_operand:BW 0 "register_operand" "=x")
	(match_dup 2))]
  ""
  "")

(define_expand "reload_out<mode>"
  [(set (match_operand:BW 2 "register_operand" "=&r")
	(match_operand:BW 1 "register_operand" "x"))
   (set (match_operand:BW 0 "memory_operand" "=m")
	(match_dup 2))]
  ""
  "")

(define_insn "movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=r,Q>,r, r,Q>,r,g,g,r,r,r,x")
	(match_operand:QI 1 "general_operand"	    "r,r, Q>,M,M, I,M,r,O,g,x,r"))]
  ""
  "@
   move.b %1,%0
   move.b %1,%0
   move.b %1,%0
   clear.b %0
   clear.b %0
   moveq %1,%0
   clear.b %0
   move.b %1,%0
   moveq %b1,%0
   move.b %1,%0
   move %1,%0
   move %1,%0"
  [(set_attr "slottable" "yes,yes,yes,yes,yes,yes,no,no,yes,no,yes,yes")
   (set_attr "cc" "*,*,*,*,*,*,*,*,clobber,*,none,none")])

(define_insn "movstrictqi"
  [(set (strict_low_part
	 (match_operand:QI 0 "nonimmediate_operand" "+r,Q>,r, r,Q>,g,g,r"))
	(match_operand:QI 1 "general_operand"	     "r,r, Q>,M,M, M,r,g"))]
  ""
  "@
   move.b %1,%0
   move.b %1,%0
   move.b %1,%0
   clear.b %0
   clear.b %0
   clear.b %0
   move.b %1,%0
   move.b %1,%0"
  [(set_attr "slottable" "yes,yes,yes,yes,yes,no,no,no")])

;; The valid "quick" bit-patterns are, except for 0.0, denormalized
;; values REALLY close to 0, and some NaN:s (I think; their exponent is
;; all ones); the worthwhile one is "0.0".
;; It will use clear, so we know ALL types of immediate 0 never change cc.

(define_insn "movsf"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=r,Q>,r, r,Q>,g,g,r,r,x,Q>,m,x, x")
	(match_operand:SF 1 "general_operand"       "r,r, Q>,G,G, G,r,g,x,r,x, x,Q>,g"))]
  ""
  "@
   move.d %1,%0
   move.d %1,%0
   move.d %1,%0
   clear.d %0
   clear.d %0
   clear.d %0
   move.d %1,%0
   move.d %1,%0
   move %1,%0
   move %1,%0
   move %1,%0
   move %1,%0
   move %1,%0
   move %1,%0"
  [(set_attr "slottable" "yes,yes,yes,yes,yes,no,no,no,yes,yes,yes,no,yes,no")])

;; Note that the memory layout of the registers is the reverse of that
;; of the standard patterns "load_multiple" and "store_multiple".
(define_insn "*cris_load_multiple"
  [(match_parallel 0 "cris_load_multiple_op"
		   [(set (match_operand:SI 1 "register_operand" "=r,r")
			 (match_operand:SI 2 "memory_operand" "Q,m"))])]
  ""
  "movem %O0,%o0"
  [(set_attr "cc" "none")
   (set_attr "slottable" "yes,no")
   ;; Not true, but setting the length to 0 causes return sequences (ret
   ;; movem) to have the cost they had when (return) included the movem
   ;; and reduces the performance penalty taken for needing to emit an
   ;; epilogue (in turn copied by bb-reorder) instead of return patterns.
   ;; FIXME: temporary change until all insn lengths are correctly
   ;; described.  FIXME: have better target control over bb-reorder.
   (set_attr "length" "0")])

(define_insn "*cris_store_multiple"
  [(match_parallel 0 "cris_store_multiple_op"
		   [(set (match_operand:SI 2 "memory_operand" "=Q,m")
			 (match_operand:SI 1 "register_operand" "r,r"))])]
  ""
  "movem %o0,%O0"
  [(set_attr "cc" "none")
   (set_attr "slottable" "yes,no")])


;; Sign- and zero-extend insns with standard names.
;;  Those for integer source operand are ordered with the widest source
;; type first.

;; Sign-extend.

(define_insn "extendsidi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (match_operand:SI 1 "general_operand" "g")))]
  ""
  "move.d %1,%M0\;smi %H0\;neg.d %H0,%H0")

(define_insn "extend<mode>di2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (match_operand:BW 1 "general_operand" "g")))]
  ""
  "movs<m> %1,%M0\;smi %H0\;neg.d %H0,%H0")

(define_insn "extend<mode>si2"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r")
	(sign_extend:SI (match_operand:BW 1 "general_operand" "r,Q>,g")))]
  ""
  "movs<m> %1,%0"
  [(set_attr "slottable" "yes,yes,no")])

;; To do a byte->word extension, extend to dword, exept that the top half
;; of the register will be clobbered.  FIXME: Perhaps this is not needed.

(define_insn "extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=r,r,r")
	(sign_extend:HI (match_operand:QI 1 "general_operand" "r,Q>,g")))]
  ""
  "movs.b %1,%0"
  [(set_attr "slottable" "yes,yes,no")])


;; Zero-extend.  The DImode ones are synthesized by gcc, so we don't
;; specify them here.

(define_insn "zero_extend<mode>si2"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r")
	(zero_extend:SI
	 (match_operand:BW 1 "nonimmediate_operand" "r,Q>,m")))]
  ""
  "movu<m> %1,%0"
  [(set_attr "slottable" "yes,yes,no")])

;; Same comment as sign-extend QImode to HImode above applies.

(define_insn "zero_extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=r,r,r")
	(zero_extend:HI
	 (match_operand:QI 1 "nonimmediate_operand" "r,Q>,m")))]
  ""
  "movu.b %1,%0"
  [(set_attr "slottable" "yes,yes,no")])

;; All kinds of arithmetic and logical instructions.
;;
;; First, anonymous patterns to match addressing modes with
;; side-effects.
;;
;; op.S [rx=ry+I],rz; (add, sub, or, and, bound).
;;
;; [rx=ry+rz.S]

(define_insn "*op_side<mode>_biap"
  [(set (match_operand:BWD 0 "register_operand" "=r,r")
	(match_operator:BWD
	 6 "cris_orthogonal_operator"
	 [(match_operand:BWD 1 "register_operand" "0,0")
	  (mem:BWD (plus:SI
		    (mult:SI (match_operand:SI 2 "register_operand" "r,r")
			     (match_operand:SI 3 "const_int_operand" "n,n"))
		    (match_operand:SI 4 "register_operand" "r,r")))]))
   (set (match_operand:SI 5 "register_operand" "=*4,r")
	(plus:SI (mult:SI (match_dup 2)
			  (match_dup 3))
		 (match_dup 4)))]
  "cris_side_effect_mode_ok (MULT, operands, 5, 4, 2, 3, 0)"
  "@
   #
   %x6<m> [%5=%4+%2%T3],%0")

;; [rx=ry+i] ([%4=%2+%3])

(define_insn "*op_side<mode>"
  [(set (match_operand:BWD 0 "register_operand" "=r,r,r,r,r")
	(match_operator:BWD
	 5 "cris_orthogonal_operator"
	 [(match_operand:BWD 1 "register_operand" "0,0,0,0,0")
	  (mem:BWD (plus:SI
		   (match_operand:SI 2 "cris_bdap_operand" "%r,r,r,R,R")
		   (match_operand:SI 3 "cris_bdap_operand" "r>Rn,r,>Rn,r,r")))]))
   (set (match_operand:SI 4 "register_operand" "=*2,r,r,*3,r")
	(plus:SI (match_dup 2)
		 (match_dup 3)))]
  "cris_side_effect_mode_ok (PLUS, operands, 4, 2, 3, -1, 0)"
{
  if ((which_alternative == 0 || which_alternative == 3)
      && (GET_CODE (operands[3]) != CONST_INT
	  || INTVAL (operands[3]) > 127
	  || INTVAL (operands[3]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'J')))
    return "#";
  if (which_alternative == 4)
    return "%x5.%s0 [%4=%3%S2],%0";
  return "%x5<m> [%4=%2%S3],%0";
})

;; To match all cases for commutative operations we may have to have the
;; following pattern for add, or & and.  I do not know really, but it does
;; not break anything.
;;
;; FIXME: This really ought to be checked.
;;
;; op.S [rx=ry+I],rz;
;;
;; [rx=ry+rz.S]

(define_insn "*op_swap_side<mode>_biap"
  [(set (match_operand:BWD 0 "register_operand" "=r,r")
	(match_operator:BWD
	 6 "cris_commutative_orth_op"
	 [(mem:BWD (plus:SI
		   (mult:SI (match_operand:SI 2 "register_operand" "r,r")
			    (match_operand:SI 3 "const_int_operand" "n,n"))
		   (match_operand:SI 4 "register_operand" "r,r")))
	  (match_operand:BWD 1 "register_operand" "0,0")]))
   (set (match_operand:SI 5 "register_operand" "=*4,r")
	(plus:SI (mult:SI (match_dup 2)
			  (match_dup 3))
		 (match_dup 4)))]
  "cris_side_effect_mode_ok (MULT, operands, 5, 4, 2, 3, 0)"
  "@
   #
   %x6<m> [%5=%4+%2%T3],%0")

;; [rx=ry+i] ([%4=%2+%3])
;; FIXME: These could have anonymous mode for operand 0.

;; QImode

(define_insn "*op_swap_side<mode>"
  [(set (match_operand:BWD 0 "register_operand" "=r,r,r,r,r")
	(match_operator:BWD
	 5 "cris_commutative_orth_op"
	 [(mem:BWD
	   (plus:SI (match_operand:SI 2 "cris_bdap_operand" "%r,r,r,R,R")
		    (match_operand:SI 3 "cris_bdap_operand" "r>Rn,r,>Rn,r,r")))
	  (match_operand:BWD 1 "register_operand" "0,0,0,0,0")]))
   (set (match_operand:SI 4 "register_operand" "=*2,r,r,*3,r")
	(plus:SI (match_dup 2)
		 (match_dup 3)))]
  "cris_side_effect_mode_ok (PLUS, operands, 4, 2, 3, -1, 0)"
{
  if ((which_alternative == 0 || which_alternative == 3)
      && (GET_CODE (operands[3]) != CONST_INT
	  || INTVAL (operands[3]) > 127
	  || INTVAL (operands[3]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'J')))
    return "#";
  if (which_alternative == 4)
    return "%x5<m> [%4=%3%S2],%0";
  return "%x5<m> [%4=%2%S3],%0";
})

;; Add operations, standard names.

;; Note that for the 'P' constraint, the high part can be -1 or 0.  We
;; output the insn through the 'A' output modifier as "adds.w" and "addq",
;; respectively.
(define_insn "adddi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r,r,&r,&r")
	(plus:DI (match_operand:DI 1 "register_operand" "%0,0,0,0,r")
		 (match_operand:DI 2 "general_operand" "J,N,P,g,!To")))]
  ""
  "@
   addq %2,%M0\;ax\;addq 0,%H0
   subq %n2,%M0\;ax\;subq 0,%H0
   add%e2.%z2 %2,%M0\;ax\;%A2 %H2,%H0
   add.d %M2,%M0\;ax\;add.d %H2,%H0
   add.d %M2,%M1,%M0\;ax\;add.d %H2,%H1,%H0")

(define_insn "addsi3"
  [(set (match_operand:SI 0 "register_operand"  "=r,r, r,r,r,r, r,r,  r")
	(plus:SI
	 (match_operand:SI 1 "register_operand" "%0,0, 0,0,0,0, 0,r,  r")
	 (match_operand:SI 2 "general_operand"   "r,Q>,J,N,n,!S,g,!To,0")))]

;; The last constraint is due to that after reload, the '%' is not
;; honored, and canonicalization doesn't care about keeping the same
;; register as in destination.  This will happen after insn splitting.
;; gcc <= 2.7.2.  FIXME: Check for gcc-2.9x

 ""
{
  switch (which_alternative)
    {
    case 0:
    case 1:
      return "add.d %2,%0";
    case 2:
      return "addq %2,%0";
    case 3:
      return "subq %n2,%0";
    case 4:
      /* 'Known value', but not in -63..63.
	 Check if addu/subu may be used.  */
      if (INTVAL (operands[2]) > 0)
	{
	  if (INTVAL (operands[2]) < 256)
	    return "addu.b %2,%0";
	  if (INTVAL (operands[2]) < 65536)
	    return "addu.w %2,%0";
	}
      else
	{
	  if (INTVAL (operands[2]) >= -255)
	    return "subu.b %n2,%0";
	  if (INTVAL (operands[2]) >= -65535)
	    return "subu.w %n2,%0";
	}
      return "add.d %2,%0";
    case 5:
      {
	rtx tem = operands[2];
	gcc_assert (GET_CODE (tem) == CONST);
	tem = XEXP (tem, 0);
	if (GET_CODE (tem) == PLUS
	    && GET_CODE (XEXP (tem, 0)) == UNSPEC
	    && XINT (XEXP (tem, 0), 1) == CRIS_UNSPEC_GOTREL
	    && GET_CODE (XEXP (tem, 1)) == CONST_INT)
	  tem = XEXP (tem, 0);
	gcc_assert (GET_CODE (tem) == UNSPEC);
	switch (XINT (tem, 1))
	  {
	  case CRIS_UNSPEC_GOTREAD:
	  case CRIS_UNSPEC_PLTGOTREAD:
	    /* Using sign-extend mostly to be consistent with the
	       indexed addressing mode.  */
	    if (flag_pic == 1)
	      return "adds.w %2,%0";
	    /* Fall through.  */
	  case CRIS_UNSPEC_PLT:
	  case CRIS_UNSPEC_GOTREL:
	    return "add.d %2,%0";
	  default:
	    gcc_unreachable ();
	  }
      }
    case 6:
      return "add.d %2,%0";
    case 7:
      return "add.d %2,%1,%0";
    case 8:
      return "add.d %1,%0";
    default:
      return "BOGUS addsi %2+%1 to %0";
    }
}
 [(set_attr "slottable" "yes,yes,yes,yes,no,no,no,no,yes")])

(define_insn "addhi3"
  [(set (match_operand:HI 0 "register_operand"		"=r,r, r,r,r,r")
	(plus:HI (match_operand:HI 1 "register_operand" "%0,0, 0,0,0,r")
		 (match_operand:HI 2 "general_operand"   "r,Q>,J,N,g,!To")))]
  ""
  "@
   add.w %2,%0
   add.w %2,%0
   addq %2,%0
   subq %n2,%0
   add.w %2,%0
   add.w %2,%1,%0"
  [(set_attr "slottable" "yes,yes,yes,yes,no,no")
   (set_attr "cc" "normal,normal,clobber,clobber,normal,normal")])

(define_insn "addqi3"
  [(set (match_operand:QI 0 "register_operand"		"=r,r, r,r,r,r,r")
	(plus:QI (match_operand:QI 1 "register_operand" "%0,0, 0,0,0,0,r")
		 (match_operand:QI 2 "general_operand"	 "r,Q>,J,N,O,g,!To")))]
  ""
  "@
   add.b %2,%0
   add.b %2,%0
   addq %2,%0
   subq %n2,%0
   subQ -%b2,%0
   add.b %2,%0
   add.b %2,%1,%0"
  [(set_attr "slottable" "yes,yes,yes,yes,yes,no,no")
   (set_attr "cc" "normal,normal,clobber,clobber,clobber,normal,normal")])

;; Subtract.
;;
;; Note that because of insn canonicalization these will *seldom* but
;; rarely be used with a known constant as an operand.

;; Note that for the 'P' constraint, the high part can be -1 or 0.  We
;; output the insn through the 'D' output modifier as "subs.w" and "subq",
;; respectively.
(define_insn "subdi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r,r,&r,&r")
	(minus:DI (match_operand:DI 1 "register_operand" "0,0,0,0,r")
		  (match_operand:DI 2 "general_operand" "J,N,P,g,!To")))]
  ""
  "@
   subq %2,%M0\;ax\;subq 0,%H0
   addq %n2,%M0\;ax\;addq 0,%H0
   sub%e2.%z2 %2,%M0\;ax\;%D2 %H2,%H0
   sub.d %M2,%M0\;ax\;sub.d %H2,%H0
   sub.d %M2,%M1,%M0\;ax\;sub.d %H2,%H1,%H0")

(define_insn "subsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r, r,r,r,r,r,r")
	(minus:SI
	 (match_operand:SI 1 "register_operand" "0,0, 0,0,0,0,0,r")
	 (match_operand:SI 2 "general_operand"	"r,Q>,J,N,P,n,g,!To")))]
  ""

;; This does not do the optimal: "addu.w 65535,r0" when %2 is negative.
;; But then again, %2 should not be negative.

  "@
   sub.d %2,%0
   sub.d %2,%0
   subq %2,%0
   addq %n2,%0
   sub%e2.%z2 %2,%0
   sub.d %2,%0
   sub.d %2,%0
   sub.d %2,%1,%0"
  [(set_attr "slottable" "yes,yes,yes,yes,no,no,no,no")])

(define_insn "sub<mode>3"
  [(set (match_operand:BW 0 "register_operand"		"=r,r, r,r,r,r")
	(minus:BW (match_operand:BW 1 "register_operand" "0,0, 0,0,0,r")
		  (match_operand:BW 2 "general_operand"  "r,Q>,J,N,g,!To")))]
  ""
  "@
   sub<m> %2,%0
   sub<m> %2,%0
   subq %2,%0
   addq %n2,%0
   sub<m> %2,%0
   sub<m> %2,%1,%0"
  [(set_attr "slottable" "yes,yes,yes,yes,no,no")
   (set_attr "cc" "normal,normal,clobber,clobber,normal,normal")])

;; CRIS has some add/sub-with-sign/zero-extend instructions.
;;  Although these perform sign/zero-extension to SImode, they are
;; equally applicable for the HImode case.
;; FIXME: Check; GCC should handle the widening.
;;  Note that these must be located after the normal add/sub patterns,
;; so not to get constants into any less specific operands.
;;
;; Extend with add/sub and side-effect.
;;
;; ADDS/SUBS/ADDU/SUBU and BOUND, which needs a check for zero_extend
;;
;; adds/subs/addu/subu bound [rx=ry+rz.S]

;; QImode to HImode
;; FIXME: GCC should widen.

(define_insn "*extopqihi_side_biap"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(match_operator:HI
	 6 "cris_additive_operand_extend_operator"
	 [(match_operand:HI 1 "register_operand" "0,0")
	  (match_operator:HI
	   7 "cris_extend_operator"
	   [(mem:QI (plus:SI
		     (mult:SI (match_operand:SI 2 "register_operand" "r,r")
			      (match_operand:SI 3 "const_int_operand" "n,n"))
		     (match_operand:SI 4 "register_operand" "r,r")))])]))
   (set (match_operand:SI 5 "register_operand" "=*4,r")
	(plus:SI (mult:SI (match_dup 2)
			  (match_dup 3))
		 (match_dup 4)))]
  "cris_side_effect_mode_ok (MULT, operands, 5, 4, 2, 3, 0)"
  "@
   #
   %x6%e7.%m7 [%5=%4+%2%T3],%0")

(define_insn "*extop<mode>si_side_biap"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(match_operator:SI
	 6 "cris_operand_extend_operator"
	 [(match_operand:SI 1 "register_operand" "0,0")
	  (match_operator:SI
	   7 "cris_extend_operator"
	   [(mem:BW (plus:SI
		     (mult:SI (match_operand:SI 2 "register_operand" "r,r")
			      (match_operand:SI 3 "const_int_operand" "n,n"))
		     (match_operand:SI 4 "register_operand" "r,r")))])]))
   (set (match_operand:SI 5 "register_operand" "=*4,r")
	(plus:SI (mult:SI (match_dup 2)
			  (match_dup 3))
		 (match_dup 4)))]
  "(GET_CODE (operands[6]) != UMIN || GET_CODE (operands[7]) == ZERO_EXTEND)
   && cris_side_effect_mode_ok (MULT, operands, 5, 4, 2, 3, 0)"
  "@
   #
   %x6%e7<m> [%5=%4+%2%T3],%0")


;; [rx=ry+i]

;; QImode to HImode

(define_insn "*extopqihi_side"
  [(set (match_operand:HI 0 "register_operand" "=r,r,r,r,r")
	(match_operator:HI
	 5 "cris_additive_operand_extend_operator"
	 [(match_operand:HI 1 "register_operand" "0,0,0,0,0")
	  (match_operator:HI
	   6 "cris_extend_operator"
	   [(mem:QI
	     (plus:SI (match_operand:SI 2 "cris_bdap_operand" "%r,r,r,R,R")
		      (match_operand:SI 3 "cris_bdap_operand" "r>Rn,r,>Rn,r,r")
		      ))])]))
   (set (match_operand:SI 4 "register_operand" "=*2,r,r,*3,r")
	(plus:SI (match_dup 2)
		 (match_dup 3)))]
  "cris_side_effect_mode_ok (PLUS, operands, 4, 2, 3, -1, 0)"
{
  if ((which_alternative == 0 || which_alternative == 3)
      && (GET_CODE (operands[3]) != CONST_INT
	  || INTVAL (operands[3]) > 127
	  || INTVAL (operands[3]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'J')))
    return "#";
  if (which_alternative == 4)
    return "%x5%E6.%m6 [%4=%3%S2],%0";
  return "%x5%E6.%m6 [%4=%2%S3],%0";
})

(define_insn "*extop<mode>si_side"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r,r")
	(match_operator:SI
	 5 "cris_operand_extend_operator"
	 [(match_operand:SI 1 "register_operand" "0,0,0,0,0")
	  (match_operator:SI
	   6 "cris_extend_operator"
	   [(mem:BW
	     (plus:SI (match_operand:SI 2 "cris_bdap_operand" "%r,r,r,R,R")
		      (match_operand:SI 3 "cris_bdap_operand" "r>Rn,r,>Rn,r,r")
		      ))])]))
   (set (match_operand:SI 4 "register_operand" "=*2,r,r,*3,r")
	(plus:SI (match_dup 2)
		 (match_dup 3)))]
  "(GET_CODE (operands[5]) != UMIN || GET_CODE (operands[6]) == ZERO_EXTEND)
   && cris_side_effect_mode_ok (PLUS, operands, 4, 2, 3, -1, 0)"
{
  if ((which_alternative == 0 || which_alternative == 3)
      && (GET_CODE (operands[3]) != CONST_INT
	  || INTVAL (operands[3]) > 127
	  || INTVAL (operands[3]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'J')))
    return "#";
  if (which_alternative == 4)
    return "%x5%E6<m> [%4=%3%S2],%0";
  return "%x5%E6<m> [%4=%2%S3],%0";
})


;; As with op.S we may have to add special pattern to match commuted
;; operands to adds/addu  and bound
;;
;; adds/addu/bound [rx=ry+rz.S]

;; QImode to HImode
;; FIXME: GCC should widen.

(define_insn "*extopqihi_swap_side_biap"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(plus:HI
	 (match_operator:HI
	  6 "cris_extend_operator"
	  [(mem:QI (plus:SI
		    (mult:SI (match_operand:SI 2 "register_operand" "r,r")
			     (match_operand:SI 3 "const_int_operand" "n,n"))
		    (match_operand:SI 4 "register_operand" "r,r")))])
	 (match_operand:HI 1 "register_operand" "0,0")))
   (set (match_operand:SI 5 "register_operand" "=*4,r")
	(plus:SI (mult:SI (match_dup 2)
			  (match_dup 3))
		 (match_dup 4)))]
  "cris_side_effect_mode_ok (MULT, operands, 5, 4, 2, 3, 0)"
  "@
   #
   add%e6.b [%5=%4+%2%T3],%0")

(define_insn "*extop<mode>si_swap_side_biap"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(match_operator:SI
	 7 "cris_plus_or_bound_operator"
	 [(match_operator:SI
	   6 "cris_extend_operator"
	   [(mem:BW (plus:SI
		     (mult:SI (match_operand:SI 2 "register_operand" "r,r")
			      (match_operand:SI 3 "const_int_operand" "n,n"))
		     (match_operand:SI 4 "register_operand" "r,r")))])
	  (match_operand:SI 1 "register_operand" "0,0")]))
   (set (match_operand:SI 5 "register_operand" "=*4,r")
	(plus:SI (mult:SI (match_dup 2)
			  (match_dup 3))
		 (match_dup 4)))]
  "(GET_CODE (operands[7]) != UMIN || GET_CODE (operands[6]) == ZERO_EXTEND)
   && cris_side_effect_mode_ok (MULT, operands, 5, 4, 2, 3, 0)"
  "@
   #
   %x7%E6<m> [%5=%4+%2%T3],%0")

;; [rx=ry+i]
;; FIXME: GCC should widen.

;; QImode to HImode

(define_insn "*extopqihi_swap_side"
  [(set (match_operand:HI 0 "register_operand" "=r,r,r,r,r")
	(plus:HI
	 (match_operator:HI
	  5 "cris_extend_operator"
	  [(mem:QI (plus:SI
		    (match_operand:SI 2 "cris_bdap_operand" "%r,r,r,R,R")
		    (match_operand:SI 3 "cris_bdap_operand" "r>Rn,r,>Rn,r,r")))])
	 (match_operand:HI 1 "register_operand" "0,0,0,0,0")))
   (set (match_operand:SI 4 "register_operand" "=*2,r,r,*3,r")
	(plus:SI (match_dup 2)
		 (match_dup 3)))]
  "cris_side_effect_mode_ok (PLUS, operands, 4, 2, 3, -1, 0)"
{
  if ((which_alternative == 0 || which_alternative == 3)
      && (GET_CODE (operands[3]) != CONST_INT
	  || INTVAL (operands[3]) > 127
	  || INTVAL (operands[3]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'J')))
    return "#";
  if (which_alternative == 4)
    return "add%e5.b [%4=%3%S2],%0";
  return "add%e5.b [%4=%2%S3],%0";
})

(define_insn "*extop<mode>si_swap_side"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r,r")
	(match_operator:SI
	 6 "cris_plus_or_bound_operator"
	 [(match_operator:SI
	   5 "cris_extend_operator"
	   [(mem:BW (plus:SI
		     (match_operand:SI 2 "cris_bdap_operand" "%r,r,r,R,R")
		     (match_operand:SI 3 "cris_bdap_operand" "r>Rn,r,>Rn,r,r")))])
	  (match_operand:SI 1 "register_operand" "0,0,0,0,0")]))
   (set (match_operand:SI 4 "register_operand" "=*2,r,r,*3,r")
	(plus:SI (match_dup 2)
		 (match_dup 3)))]
  "(GET_CODE (operands[6]) != UMIN || GET_CODE (operands[5]) == ZERO_EXTEND)
   && cris_side_effect_mode_ok (PLUS, operands, 4, 2, 3, -1, 0)"
{
  if ((which_alternative == 0 || which_alternative == 3)
      && (GET_CODE (operands[3]) != CONST_INT
	  || INTVAL (operands[3]) > 127
	  || INTVAL (operands[3]) < -128
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'N')
	  || CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'J')))
    return "#";
  if (which_alternative == 4)
    return \"%x6%E5.%m5 [%4=%3%S2],%0\";
  return "%x6%E5<m> [%4=%2%S3],%0";
})

;; Extend versions (zero/sign) of normal add/sub (no side-effects).

;; QImode to HImode
;; FIXME: GCC should widen.

(define_insn "*extopqihi"
  [(set (match_operand:HI 0 "register_operand" "=r,r,r,r")
	(match_operator:HI
	 3 "cris_additive_operand_extend_operator"
	 [(match_operand:HI 1 "register_operand" "0,0,0,r")
	  (match_operator:HI
	   4 "cris_extend_operator"
	   [(match_operand:QI 2 "nonimmediate_operand" "r,Q>,m,!To")])]))]
  "GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD
   && (operands[1] != frame_pointer_rtx || GET_CODE (operands[3]) != PLUS)"
  "@
   %x3%E4.%m4 %2,%0
   %x3%E4.%m4 %2,%0
   %x3%E4.%m4 %2,%0
   %x3%E4.%m4 %2,%1,%0"
  [(set_attr "slottable" "yes,yes,no,no")
   (set_attr "cc" "clobber")])

;; QImode to SImode

(define_insn "*extop<mode>si"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r")
	(match_operator:SI
	 3 "cris_operand_extend_operator"
	 [(match_operand:SI 1 "register_operand" "0,0,0,r")
	  (match_operator:SI
	   4 "cris_extend_operator"
	   [(match_operand:BW 2 "nonimmediate_operand" "r,Q>,m,!To")])]))]
  "(GET_CODE (operands[3]) != UMIN || GET_CODE (operands[4]) == ZERO_EXTEND)
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD
   && (operands[1] != frame_pointer_rtx || GET_CODE (operands[3]) != PLUS)"
  "@
   %x3%E4<m> %2,%0
   %x3%E4<m> %2,%0
   %x3%E4<m> %2,%0
   %x3%E4<m> %2,%1,%0"
  [(set_attr "slottable" "yes,yes,no,no")])


;; As with the side-effect patterns, may have to have swapped operands for add.
;; FIXME: *should* be redundant to gcc.

;; QImode to HImode

(define_insn "*extopqihi_swap"
  [(set (match_operand:HI 0 "register_operand" "=r,r,r,r")
	(plus:HI
	 (match_operator:HI
	  3 "cris_extend_operator"
	  [(match_operand:QI 2 "nonimmediate_operand" "r,Q>,m,!To")])
	 (match_operand:HI 1 "register_operand" "0,0,0,r")))]
  "operands[1] != frame_pointer_rtx"
  "@
   add%e3.b %2,%0
   add%e3.b %2,%0
   add%e3.b %2,%0
   add%e3.b %2,%1,%0"
  [(set_attr "slottable" "yes,yes,no,no")
   (set_attr "cc" "clobber")])

(define_insn "*extop<mode>si_swap"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r")
	(match_operator:SI
	 4 "cris_plus_or_bound_operator"
	 [(match_operator:SI
	   3 "cris_extend_operator"
	   [(match_operand:BW 2 "nonimmediate_operand" "r,Q>,m,!To")])
	  (match_operand:SI 1 "register_operand" "0,0,0,r")]))]
  "(GET_CODE (operands[4]) != UMIN || GET_CODE (operands[3]) == ZERO_EXTEND)
   && operands[1] != frame_pointer_rtx"
  "@
   %x4%E3<m> %2,%0
   %x4%E3<m> %2,%0
   %x4%E3<m> %2,%0
   %x4%E3<m> %2,%1,%0"
  [(set_attr "slottable" "yes,yes,no,no")])

;; This is the special case when we use what corresponds to the
;; instruction above in "casesi".  Do *not* change it to use the generic
;; pattern and "REG 15" as pc; I did that and it led to madness and
;; maintenance problems: Instead of (as imagined) recognizing and removing
;; or replacing this pattern with something simpler, other variant
;; patterns were recognized or combined, including some prefix variants
;; where the value in pc is not that of the next instruction (which means
;; this instruction actually *is* special and *should* be marked as such).
;; When switching from the "generic pattern match" approach to this simpler
;; approach, there were insignificant differences in gcc, ipps and
;; product code, somehow due to scratching reload behind the ear or
;; something.  Testcase "gcc" looked .01% slower and 4 bytes bigger;
;; product code became .001% smaller but "looked better".  The testcase
;; "ipps" was just different at register allocation).
;;
;; Assumptions in the jump optimizer forces us to use IF_THEN_ELSE in this
;; pattern with the default-label as the else, with the "if" being
;; index-is-less-than the max number of cases plus one.  The default-label
;; is attached to the end of the case-table at time of output.

(define_insn "*casesi_adds_w"
  [(set (pc)
	(if_then_else
	 (ltu (match_operand:SI 0 "register_operand" "r")
	      (match_operand:SI 1 "const_int_operand" "n"))
	 (plus:SI (sign_extend:SI
		   (mem:HI
		    (plus:SI (mult:SI (match_dup 0) (const_int 2))
			     (pc))))
		  (pc))
	 (label_ref (match_operand 2 "" ""))))
   (use (label_ref (match_operand 3 "" "")))]

  "operands[0] != frame_pointer_rtx"

  "adds.w [$pc+%0.w],$pc"
  [(set_attr "cc" "clobber")])

;; Multiply instructions.

;; Sometimes powers of 2 (which are normally canonicalized to a
;; left-shift) appear here, as a result of address reloading.
;; As a special, for values 3 and 5, we can match with an addi, so add those.
;;
;; FIXME: This may be unnecessary now.
;; Explicitly named for convenience of having a gen_... function.

(define_insn "addi_mul"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mult:SI
	 (match_operand:SI 1 "register_operand" "%0")
	 (match_operand:SI 2 "const_int_operand" "n")))]
  "operands[0] != frame_pointer_rtx
   && operands[1] != frame_pointer_rtx
   && GET_CODE (operands[2]) == CONST_INT
   && (INTVAL (operands[2]) == 2
       || INTVAL (operands[2]) == 4 || INTVAL (operands[2]) == 3
       || INTVAL (operands[2]) == 5)"
{
  if (INTVAL (operands[2]) == 2)
    return "lslq 1,%0";
  else if (INTVAL (operands[2]) == 4)
    return "lslq 2,%0";
  else if (INTVAL (operands[2]) == 3)
    return "addi %0.w,%0";
  else if (INTVAL (operands[2]) == 5)
    return "addi %0.d,%0";
  return "BAD: adr_mulsi: %0=%1*%2";
}
[(set_attr "slottable" "yes")
 ;; No flags are changed if this insn is "addi", but it does not seem
 ;; worth the trouble to distinguish that to the lslq cases.
 (set_attr "cc" "clobber")])

;; The addi insn as it is normally used.

(define_insn "*addi"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI
	 (mult:SI (match_operand:SI 2 "register_operand" "r")
		  (match_operand:SI 3 "const_int_operand" "n"))
	 (match_operand:SI 1 "register_operand" "0")))]
  "operands[0] != frame_pointer_rtx
   && operands[1] != frame_pointer_rtx
   && GET_CODE (operands[3]) == CONST_INT
   && (INTVAL (operands[3]) == 1
       || INTVAL (operands[3]) == 2 || INTVAL (operands[3]) == 4)"
  "addi %2%T3,%0"
  [(set_attr "slottable" "yes")
   (set_attr "cc" "none")])

;; The mstep instruction.  Probably not useful by itself; it's to
;; non-linear wrt. the other insns.  We used to expand to it, so at least
;; it's correct.

(define_insn "mstep_shift"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(if_then_else:SI
	 (lt:SI (cc0) (const_int 0))
	 (plus:SI (ashift:SI (match_operand:SI 1 "register_operand" "0")
			     (const_int 1))
		  (match_operand:SI 2 "register_operand" "r"))
	 (ashift:SI (match_operand:SI 3 "register_operand" "0")
		    (const_int 1))))]
  ""
  "mstep %2,%0"
  [(set_attr "slottable" "yes")])

;; When illegitimate addresses are legitimized, sometimes gcc forgets
;; to canonicalize the multiplications.
;;
;; FIXME: Check gcc > 2.7.2, remove and possibly fix in gcc.

(define_insn "mstep_mul"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(if_then_else:SI
	 (lt:SI (cc0) (const_int 0))
	 (plus:SI (mult:SI (match_operand:SI 1 "register_operand" "0")
			   (const_int 2))
		  (match_operand:SI 2 "register_operand" "r"))
	 (mult:SI (match_operand:SI 3 "register_operand" "0")
		  (const_int 2))))]
  "operands[0] != frame_pointer_rtx
   && operands[1] != frame_pointer_rtx
   && operands[2] != frame_pointer_rtx
   && operands[3] != frame_pointer_rtx"
  "mstep %2,%0"
  [(set_attr "slottable" "yes")])

(define_insn "<u>mul<s><mode>3"
  [(set (match_operand:WD 0 "register_operand" "=r")
	(mult:WD
	 (szext:WD (match_operand:<S> 1 "register_operand" "%0"))
	 (szext:WD (match_operand:<S> 2 "register_operand" "r"))))
   (clobber (match_scratch:SI 3 "=h"))]
  "TARGET_HAS_MUL_INSNS"
  "%!mul<su><mm> %2,%0"
  [(set (attr "slottable")
	(if_then_else (ne (symbol_ref "TARGET_MUL_BUG") (const_int 0))
		      (const_string "no")
		      (const_string "yes")))
   ;; For umuls.[bwd] it's just N unusable here, but let's be safe.
   ;; For muls.b, this really extends to SImode, so cc should be
   ;; considered clobbered.
   ;; For muls.w, it's just N unusable here, but let's be safe.
   (set_attr "cc" "clobber")])

;; Note that gcc does not make use of such a thing as umulqisi3.  It gets
;; confused and will erroneously use it instead of umulhisi3, failing (at
;; least) gcc.c-torture/execute/arith-rand.c at all optimization levels.
;; Inspection of optab code shows that there must be only one widening
;; multiplication per mode widened to.

(define_insn "mulsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mult:SI (match_operand:SI 1 "register_operand" "%0")
		 (match_operand:SI 2 "register_operand" "r")))
   (clobber (match_scratch:SI 3 "=h"))]
  "TARGET_HAS_MUL_INSNS"
  "%!muls.d %2,%0"
  [(set (attr "slottable")
	(if_then_else (ne (symbol_ref "TARGET_MUL_BUG") (const_int 0))
		      (const_string "no")
		      (const_string "yes")))
   ;; Just N unusable here, but let's be safe.
   (set_attr "cc" "clobber")])

;; A few multiply variations.

;; When needed, we can get the high 32 bits from the overflow
;; register.  We don't care to split and optimize these.
;;
;; Note that cc0 is still valid after the move-from-overflow-register
;; insn; no special precaution need to be taken in cris_notice_update_cc.

(define_insn "<u>mulsidi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mult:DI
	 (szext:DI (match_operand:SI 1 "register_operand" "%0"))
	 (szext:DI (match_operand:SI 2 "register_operand" "r"))))
   (clobber (match_scratch:SI 3 "=h"))]
  "TARGET_HAS_MUL_INSNS"
  "%!mul<su>.d %2,%M0\;move $mof,%H0")

;; These two patterns may be expressible by other means, perhaps by making
;; [u]?mulsidi3 a define_expand.

;; Due to register allocation braindamage, the clobber 1,2 alternatives
;; cause a move into the clobbered register *before* the insn, then
;; after the insn, mof is moved too, rather than the clobber assigned
;; the last mof target.  This became apparent when making MOF and SRP
;; visible registers, with the necessary tweak to smulsi3_highpart.
;; Because these patterns are used in division by constants, that damage
;; is visible (ipps regression tests).  Therefore the last two
;; alternatives, "helping" reload to avoid an unnecessary move, but
;; punished by force of one "?".  Check code from "int d (int a) {return
;; a / 1000;}" and unsigned.  FIXME: Comment above was for 3.2, revisit.

(define_insn "<su>mulsi3_highpart"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=h,h,?r,?r")
	(truncate:SI
	 (lshiftrt:DI
	  (mult:DI
	   (szext:DI (match_operand:SI 1 "register_operand" "r,r,0,r"))
	   (szext:DI (match_operand:SI 2 "register_operand" "r,r,r,0")))
	  (const_int 32))))
   (clobber (match_scratch:SI 3 "=1,2,h,h"))]
  "TARGET_HAS_MUL_INSNS"
  "@
   %!mul<su>.d %2,%1
   %!mul<su>.d %1,%2
   %!mul<su>.d %2,%1\;move $mof,%0
   %!mul<su>.d %1,%2\;move $mof,%0"
  [(set_attr "slottable" "yes,yes,no,no")
   (set_attr "cc" "clobber")])

;; Divide and modulus instructions.  CRIS only has a step instruction.

(define_insn "dstep_shift"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(if_then_else:SI
	 (geu:SI (ashift:SI (match_operand:SI 1 "register_operand" "0")
			    (const_int 1))
	      (match_operand:SI 2 "register_operand" "r"))
	 (minus:SI (ashift:SI (match_operand:SI 3 "register_operand" "0")
			(const_int 1))
		   (match_operand:SI 4 "register_operand" "2"))
	 (ashift:SI (match_operand:SI 5 "register_operand" "0")
			(const_int 1))))]
  ""
  "dstep %2,%0"
  [(set_attr "slottable" "yes")])

;; Here's a variant with mult instead of ashift.
;;
;; FIXME: This should be investigated.  Which one matches through combination?

(define_insn "dstep_mul"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(if_then_else:SI
	 (geu:SI (mult:SI (match_operand:SI 1 "register_operand" "0")
			  (const_int 2))
	      (match_operand:SI 2 "register_operand" "r"))
	 (minus:SI (mult:SI (match_operand:SI 3 "register_operand" "0")
			    (const_int 2))
		   (match_operand:SI 4 "register_operand" "2"))
	 (mult:SI (match_operand:SI 5 "register_operand" "0")
		  (const_int 2))))]
  "operands[0] != frame_pointer_rtx
   && operands[1] != frame_pointer_rtx
   && operands[2] != frame_pointer_rtx
   && operands[3] != frame_pointer_rtx"
  "dstep %2,%0"
  [(set_attr "slottable" "yes")])

;; Logical operators.

;; Bitwise "and".

;; There is no use in defining "anddi3", because gcc can expand this by
;; itself, and make reasonable code without interference.

;; If the first operand is memory or a register and is the same as the
;; second operand, and the third operand is -256 or -65536, we can use
;; CLEAR instead.  Or, if the first operand is a register, and the third
;; operand is 255 or 65535, we can zero_extend.
;; GCC isn't smart enough to recognize these cases (yet), and they seem
;; to be common enough to be worthwhile.
;; FIXME: This should be made obsolete.

(define_expand "andsi3"
  [(set (match_operand:SI 0 "nonimmediate_operand"	   "")
	(and:SI (match_operand:SI 1 "nonimmediate_operand" "")
		(match_operand:SI 2 "general_operand"	 "")))]
  ""
{
  if (! (GET_CODE (operands[2]) == CONST_INT
	 && (((INTVAL (operands[2]) == -256
	       || INTVAL (operands[2]) == -65536)
	      && rtx_equal_p (operands[1], operands[0]))
	     || ((INTVAL (operands[2]) == 255
		  || INTVAL (operands[2]) == 65535)
		 && REG_P (operands[0])))))
    {
      /* Make intermediate steps if operand0 is not a register or
	 operand1 is not a register, and hope that the reload pass will
	 make something useful out of it.  Note that the operands are
	 *not* canonicalized.  For the moment, I chicken out on this,
	 because all or most ports do not describe 'and' with
	 canonicalized operands, and I seem to remember magic in reload,
	 checking that operand1 has constraint '%0', in which case
	 operand0 and operand1 must have similar predicates.
	 FIXME: Investigate.  */
      rtx reg0 = REG_P (operands[0]) ? operands[0] : gen_reg_rtx (SImode);
      rtx reg1 = operands[1];

      if (! REG_P (reg1))
	{
	  emit_move_insn (reg0, reg1);
	  reg1 = reg0;
	}

      emit_insn (gen_rtx_SET (SImode, reg0,
			  gen_rtx_AND (SImode, reg1, operands[2])));

      /* Make sure we get the right *final* destination.  */
      if (! REG_P (operands[0]))
	emit_move_insn (operands[0], reg0);

      DONE;
    }
})

;; Some special cases of andsi3.

(define_insn "*andsi_movu"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r")
	(and:SI (match_operand:SI 1 "nonimmediate_operand" "%r,Q,To")
		(match_operand:SI 2 "const_int_operand" "n,n,n")))]
  "(INTVAL (operands[2]) == 255 || INTVAL (operands[2]) == 65535)
   && (GET_CODE (operands[1]) != MEM || ! MEM_VOLATILE_P (operands[1]))"
  "movu.%z2 %1,%0"
  [(set_attr "slottable" "yes,yes,no")])

(define_insn "*andsi_clear"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,r,Q,Q,To,To")
	(and:SI (match_operand:SI 1 "nonimmediate_operand" "%0,0,0,0,0,0")
		(match_operand:SI 2 "const_int_operand" "P,n,P,n,P,n")))]
  "(INTVAL (operands[2]) == -65536 || INTVAL (operands[2]) == -256)
   && (GET_CODE (operands[0]) != MEM || ! MEM_VOLATILE_P (operands[0]))"
  "@
   cLear.b %0
   cLear.w %0
   cLear.b %0
   cLear.w %0
   cLear.b %0
   cLear.w %0"
  [(set_attr "slottable" "yes,yes,yes,yes,no,no")
   (set_attr "cc" "none")])

;; This is a catch-all pattern, taking care of everything that was not
;; matched in the insns above.
;;
;; Sidenote: the tightening from "nonimmediate_operand" to
;; "register_operand" for operand 1 actually increased the register
;; pressure (worse code).  That will hopefully change with an
;; improved reload pass.

(define_insn "*expanded_andsi"
  [(set (match_operand:SI 0 "register_operand"	       "=r,r,r, r,r")
	(and:SI (match_operand:SI 1 "register_operand" "%0,0,0, 0,r")
		(match_operand:SI 2 "general_operand"   "I,r,Q>,g,!To")))]
  ""
  "@
   andq %2,%0
   and.d %2,%0
   and.d %2,%0
   and.d %2,%0
   and.d %2,%1,%0"
  [(set_attr "slottable" "yes,yes,yes,no,no")])

;; For both QI and HI we may use the quick patterns.  This results in
;; useless condition codes, but that is used rarely enough for it to
;; normally be a win (could check ahead for use of cc0, but seems to be
;; more pain than win).

;; FIXME: See note for andsi3

(define_expand "andhi3"
  [(set (match_operand:HI 0 "nonimmediate_operand" "")
	(and:HI (match_operand:HI 1 "nonimmediate_operand" "")
		(match_operand:HI 2 "general_operand"  "")))]
  ""
{
  if (! (GET_CODE (operands[2]) == CONST_INT
	 && (((INTVAL (operands[2]) == -256
	       || INTVAL (operands[2]) == 65280)
	      && rtx_equal_p (operands[1], operands[0]))
	     || (INTVAL (operands[2]) == 255
		 && REG_P (operands[0])))))
    {
      /* See comment for andsi3.  */
      rtx reg0 = REG_P (operands[0]) ? operands[0] : gen_reg_rtx (HImode);
      rtx reg1 = operands[1];

      if (! REG_P (reg1))
	{
	  emit_move_insn (reg0, reg1);
	  reg1 = reg0;
	}

      emit_insn (gen_rtx_SET (HImode, reg0,
			  gen_rtx_AND (HImode, reg1, operands[2])));

      /* Make sure we get the right destination.  */
      if (! REG_P (operands[0]))
	emit_move_insn (operands[0], reg0);

      DONE;
    }
})

;; Some fast andhi3 special cases.

(define_insn "*andhi_movu"
  [(set (match_operand:HI 0 "register_operand" "=r,r,r")
	(and:HI (match_operand:HI 1 "nonimmediate_operand" "r,Q,To")
		(const_int 255)))]
  "GET_CODE (operands[1]) != MEM || ! MEM_VOLATILE_P (operands[1])"
  "mOvu.b %1,%0"
  [(set_attr "slottable" "yes,yes,no")])

(define_insn "*andhi_clear"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,Q,To")
	(and:HI (match_operand:HI 1 "nonimmediate_operand" "0,0,0")
		(const_int -256)))]
  "GET_CODE (operands[0]) != MEM || ! MEM_VOLATILE_P (operands[0])"
  "cLear.b %0"
  [(set_attr "slottable" "yes,yes,no")
   (set_attr "cc" "none")])

;; Catch-all andhi3 pattern.

(define_insn "*expanded_andhi"
  [(set (match_operand:HI 0 "register_operand"	       "=r,r,r, r,r,r,r")
	(and:HI (match_operand:HI 1 "register_operand" "%0,0,0, 0,0,0,r")
		(match_operand:HI 2 "general_operand"   "I,r,Q>,L,O,g,!To")))]

;; Sidenote: the tightening from "general_operand" to
;; "register_operand" for operand 1 actually increased the register
;; pressure (worse code).  That will hopefully change with an
;; improved reload pass.

  ""
  "@
   andq %2,%0
   and.w %2,%0
   and.w %2,%0
   and.w %2,%0
   anDq %b2,%0
   and.w %2,%0
   and.w %2,%1,%0"
  [(set_attr "slottable" "yes,yes,yes,no,yes,no,no")
   (set_attr "cc" "clobber,normal,normal,normal,clobber,normal,normal")])

;; A strict_low_part pattern.

(define_insn "*andhi_lowpart"
  [(set (strict_low_part
	 (match_operand:HI 0 "register_operand"	       "=r,r, r,r,r,r"))
	(and:HI (match_operand:HI 1 "register_operand" "%0,0, 0,0,0,r")
		(match_operand:HI 2 "general_operand"   "r,Q>,L,O,g,!To")))]
  ""
  "@
   and.w %2,%0
   and.w %2,%0
   and.w %2,%0
   anDq %b2,%0
   and.w %2,%0
   and.w %2,%1,%0"
  [(set_attr "slottable" "yes,yes,no,yes,no,no")
   (set_attr "cc" "normal,normal,normal,clobber,normal,normal")])

(define_insn "andqi3"
  [(set (match_operand:QI 0 "register_operand"	       "=r,r,r, r,r,r")
	(and:QI (match_operand:QI 1 "register_operand" "%0,0,0, 0,0,r")
		(match_operand:QI 2 "general_operand"   "I,r,Q>,O,g,!To")))]
  ""
  "@
   andq %2,%0
   and.b %2,%0
   and.b %2,%0
   andQ %b2,%0
   and.b %2,%0
   and.b %2,%1,%0"
  [(set_attr "slottable" "yes,yes,yes,yes,no,no")
   (set_attr "cc" "clobber,normal,normal,clobber,normal,normal")])

(define_insn "*andqi_lowpart"
  [(set (strict_low_part
	 (match_operand:QI 0 "register_operand"	       "=r,r, r,r,r"))
	(and:QI (match_operand:QI 1 "register_operand" "%0,0, 0,0,r")
		(match_operand:QI 2 "general_operand"   "r,Q>,O,g,!To")))]
  ""
  "@
   and.b %2,%0
   and.b %2,%0
   andQ %b2,%0
   and.b %2,%0
   and.b %2,%1,%0"
  [(set_attr "slottable" "yes,yes,yes,no,no")
   (set_attr "cc" "normal,normal,clobber,normal,normal")])

;; Bitwise or.

;; Same comment as anddi3 applies here - no need for such a pattern.

;; It seems there's no need to jump through hoops to get good code such as
;; with andsi3.

(define_insn "iorsi3"
  [(set (match_operand:SI 0 "register_operand"	       "=r,r,r, r,r,r")
	(ior:SI (match_operand:SI 1 "register_operand" "%0,0,0, 0,0,r")
		(match_operand:SI 2 "general_operand"  "I, r,Q>,n,g,!To")))]
  ""
  "@
   orq %2,%0
   or.d %2,%0
   or.d %2,%0
   oR.%s2 %2,%0
   or.d %2,%0
   or.d %2,%1,%0"
  [(set_attr "slottable" "yes,yes,yes,no,no,no")
   (set_attr "cc" "normal,normal,normal,clobber,normal,normal")])

(define_insn "iorhi3"
  [(set (match_operand:HI 0 "register_operand"	       "=r,r,r, r,r,r,r")
	(ior:HI (match_operand:HI 1 "register_operand" "%0,0,0, 0,0,0,r")
		(match_operand:HI 2 "general_operand"   "I,r,Q>,L,O,g,!To")))]
  ""
  "@
   orq %2,%0
   or.w %2,%0
   or.w %2,%0
   or.w %2,%0
   oRq %b2,%0
   or.w %2,%0
   or.w %2,%1,%0"
  [(set_attr "slottable" "yes,yes,yes,no,yes,no,no")
   (set_attr "cc" "clobber,normal,normal,normal,clobber,normal,normal")])

(define_insn "iorqi3"
  [(set (match_operand:QI 0 "register_operand"	       "=r,r,r, r,r,r")
	(ior:QI (match_operand:QI 1 "register_operand" "%0,0,0, 0,0,r")
		(match_operand:QI 2 "general_operand"   "I,r,Q>,O,g,!To")))]
  ""
  "@
   orq %2,%0
   or.b %2,%0
   or.b %2,%0
   orQ %b2,%0
   or.b %2,%0
   or.b %2,%1,%0"
  [(set_attr "slottable" "yes,yes,yes,yes,no,no")
   (set_attr "cc" "clobber,normal,normal,clobber,normal,normal")])

;; Exclusive-or

;; See comment about "anddi3" for xordi3 - no need for such a pattern.
;; FIXME: Do we really need the shorter variants?

(define_insn "xorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(xor:SI (match_operand:SI 1 "register_operand" "%0")
		(match_operand:SI 2 "register_operand" "r")))]
  ""
  "xor %2,%0"
  [(set_attr "slottable" "yes")])

(define_insn "xor<mode>3"
  [(set (match_operand:BW 0 "register_operand" "=r")
	(xor:BW (match_operand:BW 1 "register_operand" "%0")
		(match_operand:BW 2 "register_operand" "r")))]
  ""
  "xor %2,%0"
  [(set_attr "slottable" "yes")
   (set_attr "cc" "clobber")])

;; Negation insns.

;; Questionable use, here mostly as a (slightly usable) define_expand
;; example.

(define_expand "negsf2"
  [(set (match_dup 2)
        (match_dup 3))
   (parallel [(set (match_operand:SF 0 "register_operand" "=r")
                   (neg:SF (match_operand:SF 1
                            "register_operand" "0")))
              (use (match_dup 2))])]
  ""
{
  operands[2] = gen_reg_rtx (SImode);
  operands[3] = GEN_INT (1 << 31);
})

(define_insn "*expanded_negsf2"
  [(set (match_operand:SF 0 "register_operand" "=r")
	(neg:SF (match_operand:SF 1 "register_operand" "0")))
   (use (match_operand:SI 2 "register_operand" "r"))]
  ""
  "xor %2,%0"
  [(set_attr "slottable" "yes")])

;; No "negdi2" although we could make one up that may be faster than
;; the one in libgcc.

(define_insn "neg<mode>2"
  [(set (match_operand:BWD 0 "register_operand" "=r")
	(neg:BWD (match_operand:BWD 1 "register_operand" "r")))]
  ""
  "neg<m> %1,%0"
  [(set_attr "slottable" "yes")])

;; One-complements.

;; See comment on anddi3 - no need for a DImode pattern.
;; See also xor comment.

(define_insn "one_cmplsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(not:SI (match_operand:SI 1 "register_operand" "0")))]
  ""
  "not %0"
  [(set_attr "slottable" "yes")])

(define_insn "one_cmpl<mode>2"
  [(set (match_operand:BW 0 "register_operand" "=r")
	(not:BW (match_operand:BW 1 "register_operand" "0")))]
  ""
  "not %0"
  [(set_attr "slottable" "yes")
   (set_attr "cc" "clobber")])

;; Arithmetic/Logical shift right (and SI left).

(define_insn "<shlr>si3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(shift:SI (match_operand:SI 1 "register_operand" "0")
		  (match_operand:SI 2 "nonmemory_operand" "Kr")))]
  ""
{
  if (REG_S_P (operands[2]))
    return "<slr>.d %2,%0";

  return "<slr>q %2,%0";
}
  [(set_attr "slottable" "yes")])

;; Since gcc gets lost, and forgets to zero-extend the source (or mask
;; the destination) when it changes shifts of lower modes into SImode,
;; it is better to make these expands an anonymous patterns instead of
;; the more correct define_insns.  This occurs when gcc thinks that is
;; is better to widen to SImode and use immediate shift count.

;; FIXME: Is this legacy or still true for gcc >= 2.7.2?

;; FIXME: Can't parametrize sign_extend and zero_extend (before
;; mentioning "shiftrt"), so we need two patterns.
(define_expand "ashr<mode>3"
  [(set (match_dup 3)
	(sign_extend:SI (match_operand:BW 1 "nonimmediate_operand" "")))
   (set (match_dup 4)
	(zero_extend:SI (match_operand:BW 2 "nonimmediate_operand" "")))
   (set (match_dup 5) (ashiftrt:SI (match_dup 3) (match_dup 4)))
   (set (match_operand:BW 0 "general_operand" "")
	(subreg:BW (match_dup 5) 0))]
  ""
{
  int i;

  for (i = 3; i < 6; i++)
    operands[i] = gen_reg_rtx (SImode);
})

(define_expand "lshr<mode>3"
  [(set (match_dup 3)
	(zero_extend:SI (match_operand:BW 1 "nonimmediate_operand" "")))
   (set (match_dup 4)
	(zero_extend:SI (match_operand:BW 2 "nonimmediate_operand" "")))
   (set (match_dup 5) (lshiftrt:SI (match_dup 3) (match_dup 4)))
   (set (match_operand:BW 0 "general_operand" "")
	(subreg:BW (match_dup 5) 0))]
  ""
{
  int i;

  for (i = 3; i < 6; i++)
    operands[i] = gen_reg_rtx (SImode);
})

(define_insn "*expanded_<shlr><mode>"
  [(set (match_operand:BW 0 "register_operand" "=r")
	(shiftrt:BW (match_operand:BW 1 "register_operand" "0")
		    (match_operand:BW 2 "register_operand" "r")))]
  ""
  "<slr><m> %2,%0"
  [(set_attr "slottable" "yes")])

(define_insn "*<shlr><mode>_lowpart"
  [(set (strict_low_part (match_operand:BW 0 "register_operand" "+r"))
	(shiftrt:BW (match_dup 0)
		    (match_operand:BW 1 "register_operand" "r")))]
  ""
  "<slr><m> %1,%0"
  [(set_attr "slottable" "yes")])

;; Arithmetic/logical shift left.

;; For narrower modes than SI, we can use lslq although it makes cc
;; unusable.  The win is that we do not have to reload the shift-count
;; into a register.

(define_insn "ashl<mode>3"
  [(set (match_operand:BW 0 "register_operand" "=r,r")
	(ashift:BW (match_operand:BW 1 "register_operand" "0,0")
		   (match_operand:BW 2 "nonmemory_operand" "r,K")))]
  ""
{
  return
    (GET_CODE (operands[2]) == CONST_INT
     && INTVAL (operands[2]) > <nbitsm1>)
    ? "moveq 0,%0"
    : (CONSTANT_P (operands[2])
       ? "lslq %2,%0" : "lsl<m> %2,%0");
}
  [(set_attr "slottable" "yes")
   (set_attr "cc" "normal,clobber")])

;; A strict_low_part matcher.

(define_insn "*ashl<mode>_lowpart"
  [(set (strict_low_part (match_operand:BW 0 "register_operand" "+r"))
	(ashift:BW (match_dup 0)
		   (match_operand:HI 1 "register_operand" "r")))]
  ""
  "lsl<m> %1,%0"
  [(set_attr "slottable" "yes")])

;; Various strange insns that gcc likes.

;; Fortunately, it is simple to construct an abssf (although it may not
;; be very much used in practice).

(define_insn "abssf2"
  [(set (match_operand:SF 0 "register_operand" "=r")
	(abs:SF (match_operand:SF 1 "register_operand" "0")))]
  ""
  "lslq 1,%0\;lsrq 1,%0")

(define_insn "abssi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(abs:SI (match_operand:SI 1 "register_operand" "r")))]
  ""
  "abs %1,%0"
  [(set_attr "slottable" "yes")])

;; FIXME: GCC should be able to do these expansions itself.

(define_expand "abs<mode>2"
  [(set (match_dup 2)
	(sign_extend:SI (match_operand:BW 1 "general_operand" "")))
   (set (match_dup 3) (abs:SI (match_dup 2)))
   (set (match_operand:BW 0 "register_operand" "")
	(subreg:BW (match_dup 3) 0))]
  ""
  "operands[2] = gen_reg_rtx (SImode); operands[3] = gen_reg_rtx (SImode);")

;; Bound-insn.  Defined to be the same as an unsigned minimum, which is an
;; operation supported by gcc.  Used in casesi, but used now and then in
;; normal code too.

(define_insn "uminsi3"
  [(set (match_operand:SI 0 "register_operand"		 "=r,r, r,r")
	(umin:SI  (match_operand:SI 1 "register_operand" "%0,0, 0,r")
		  (match_operand:SI 2 "general_operand"   "r,Q>,g,!To")))]
  ""
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      /* Constant operands are zero-extended, so only 32-bit operands
	 may be negative.  */
      if (INTVAL (operands[2]) >= 0)
	{
	  if (INTVAL (operands[2]) < 256)
	    return "bound.b %2,%0";

	  if (INTVAL (operands[2]) < 65536)
	    return "bound.w %2,%0";
	}
    }
  else if (which_alternative == 3)
    return "bound.d %2,%1,%0";

  return "bound.d %2,%0";
}
 [(set_attr "slottable" "yes,yes,no,no")])

;; Jump and branch insns.

(define_insn "jump"
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  ""
  "ba %l0%#"
  [(set_attr "slottable" "has_slot")])

;; Testcase gcc.c-torture/compile/991213-3.c fails if we allow a constant
;; here, since the insn is not recognized as an indirect jump by
;; jmp_uses_reg_or_mem used by computed_jump_p.  Perhaps it is a kludge to
;; change from general_operand to nonimmediate_operand (at least the docs
;; should be changed), but then again the pattern is called indirect_jump.
(define_insn "indirect_jump"
  [(set (pc) (match_operand:SI 0 "nonimmediate_operand" "rm"))]
  ""
  "jump %0")

;; Return insn.  Used whenever the epilogue is very simple; if it is only
;; a single ret or jump [sp+].  No allocated stack space or saved
;; registers are allowed.
;; Note that for this pattern, although named, it is ok to check the
;; context of the insn in the test, not only compiler switches.

(define_expand "return"
  [(return)]
  "cris_simple_epilogue ()"
  "cris_expand_return (cris_return_address_on_stack ()); DONE;")

(define_insn "*return_expanded"
  [(return)]
  ""
{
  return cris_return_address_on_stack_for_return ()
    ? "jump [$sp+]" : "ret%#";
}
  [(set (attr "slottable")
 	(if_then_else
 	 (ne (symbol_ref
	      "(cris_return_address_on_stack_for_return ())")
 	     (const_int 0))
 	 (const_string "no")
	 (const_string "has_slot")))])

(define_expand "prologue"
  [(const_int 0)]
  "TARGET_PROLOGUE_EPILOGUE"
  "cris_expand_prologue (); DONE;")

;; Note that the (return) from the expander itself is always the last
;; insn in the epilogue.
(define_expand "epilogue"
  [(const_int 0)]
  "TARGET_PROLOGUE_EPILOGUE"
  "cris_expand_epilogue (); DONE;")

;; Conditional branches.

;; We suffer from the same overflow-bit-gets-in-the-way problem as
;; e.g. m68k, so we have to check if overflow bit is set on all "signed"
;; conditions.

(define_insn "b<ncond:code>"
  [(set (pc)
	(if_then_else (ncond (cc0)
			     (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "b<CC> %l0%#"
  [(set_attr "slottable" "has_slot")])

(define_insn "b<ocond:code>"
  [(set (pc)
	(if_then_else (ocond (cc0)
			     (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  return
    (cc_prev_status.flags & CC_NO_OVERFLOW)
    ? 0 : "b<CC> %l0%#";
}
  [(set_attr "slottable" "has_slot")])

(define_insn "b<rcond:code>"
  [(set (pc)
	(if_then_else (rcond (cc0)
			     (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  return
    (cc_prev_status.flags & CC_NO_OVERFLOW)
    ? "b<oCC> %l0%#" : "b<CC> %l0%#";
}
  [(set_attr "slottable" "has_slot")])

;; Reversed anonymous patterns to the ones above, as mandated.

(define_insn "*b<ncond:code>_reversed"
  [(set (pc)
	(if_then_else (ncond (cc0)
			     (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""
  "b<rCC> %l0%#"
  [(set_attr "slottable" "has_slot")])

(define_insn "*b<ocond:code>_reversed"
  [(set (pc)
	(if_then_else (ocond (cc0)
			     (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""
{
  return
    (cc_prev_status.flags & CC_NO_OVERFLOW)
    ? 0 : "b<rCC> %l0%#";
}
  [(set_attr "slottable" "has_slot")])

(define_insn "*b<rcond:code>_reversed"
  [(set (pc)
	(if_then_else (rcond (cc0)
			     (const_int 0))
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
  ""
{
  return
    (cc_prev_status.flags & CC_NO_OVERFLOW)
    ? "b<roCC> %l0%#" : "b<rCC> %l0%#";
}
  [(set_attr "slottable" "has_slot")])

;; Set on condition: sCC.

;; Like bCC, we have to check the overflow bit for
;; signed conditions.

(define_insn "s<ncond:code>"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ncond:SI (cc0) (const_int 0)))]
  ""
  "s<CC> %0"
  [(set_attr "slottable" "yes")
   (set_attr "cc" "none")])

(define_insn "s<rcond:code>"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(rcond:SI (cc0) (const_int 0)))]
  ""
{
  return
    (cc_prev_status.flags & CC_NO_OVERFLOW)
    ? "s<oCC> %0" : "s<CC> %0";
}
  [(set_attr "slottable" "yes")
   (set_attr "cc" "none")])

(define_insn "s<ocond:code>"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ocond:SI (cc0) (const_int 0)))]
  ""
{
  return
    (cc_prev_status.flags & CC_NO_OVERFLOW)
    ? 0 : "s<CC> %0";
}
  [(set_attr "slottable" "yes")
   (set_attr "cc" "none")])

;; Call insns.

;; We need to make these patterns "expand", since the real operand is
;; hidden in a (mem:QI ) inside operand[0] (call_value: operand[1]),
;; and cannot be checked if it were a "normal" pattern.
;;  Note that "call" and "call_value" are *always* called with a
;; mem-operand for operand 0 and 1 respective.  What happens for combined
;; instructions is a different issue.

(define_expand "call"
  [(parallel [(call (match_operand:QI 0 "cris_mem_call_operand" "")
		    (match_operand 1 "general_operand" ""))
	      (clobber (reg:SI CRIS_SRP_REGNUM))])]
  ""
{
  gcc_assert (GET_CODE (operands[0]) == MEM);
  if (flag_pic)
    cris_expand_pic_call_address (&operands[0]);
})

;; Accept *anything* as operand 1.  Accept operands for operand 0 in
;; order of preference.

(define_insn "*expanded_call"
  [(call (mem:QI (match_operand:SI
		  0 "cris_general_operand_or_plt_symbol" "r,Q>,g"))
	 (match_operand 1 "" ""))
   (clobber (reg:SI CRIS_SRP_REGNUM))]
  ""
  "jsr %0")

;; Parallel when calculating and reusing address of indirect pointer
;; with simple offset.  (Makes most sense with PIC.)  It looks a bit
;; wrong not to have the clobber last, but that's the way combine
;; generates it (except it doesn' look into the *inner* mem, so this
;; just matches a peephole2).  FIXME: investigate that.
(define_insn "*expanded_call_side"
  [(call (mem:QI
	  (mem:SI
	   (plus:SI (match_operand:SI 0 "cris_bdap_operand" "%r,  r,r")
		    (match_operand:SI 1 "cris_bdap_operand" "r>Rn,r,>Rn"))))
	 (match_operand 2 "" ""))
   (clobber (reg:SI CRIS_SRP_REGNUM))
   (set (match_operand:SI 3 "register_operand" "=*0,r,r")
	(plus:SI (match_dup 0)
		 (match_dup 1)))]
  "! TARGET_AVOID_GOTPLT"
  "jsr [%3=%0%S1]")

(define_expand "call_value"
  [(parallel [(set (match_operand 0 "" "")
		   (call (match_operand:QI 1 "cris_mem_call_operand" "")
			 (match_operand 2 "" "")))
	      (clobber (reg:SI CRIS_SRP_REGNUM))])]
  ""
{
  gcc_assert (GET_CODE (operands[1]) == MEM);
  if (flag_pic)
    cris_expand_pic_call_address (&operands[1]);
})

;; Accept *anything* as operand 2.  The validity other than "general" of
;; operand 0 will be checked elsewhere.  Accept operands for operand 1 in
;; order of preference (Q includes r, but r is shorter, faster).
;;  We also accept a PLT symbol.  We output it as [rPIC+sym:GOTPLT] rather
;; than requiring getting rPIC + sym:PLT into a register.

(define_insn "*expanded_call_value"
  [(set (match_operand 0 "nonimmediate_operand" "=g,g,g")
	(call (mem:QI (match_operand:SI
		       1 "cris_general_operand_or_plt_symbol" "r,Q>,g"))
	      (match_operand 2 "" "")))
   (clobber (reg:SI CRIS_SRP_REGNUM))]
  ""
  "Jsr %1"
  [(set_attr "cc" "clobber")])

;; See similar call special-case.
(define_insn "*expanded_call_value_side"
  [(set (match_operand 0 "nonimmediate_operand" "=g,g,g")
	(call
	 (mem:QI
	  (mem:SI
	   (plus:SI (match_operand:SI 1 "cris_bdap_operand" "%r,  r,r")
		    (match_operand:SI 2 "cris_bdap_operand" "r>Rn,r,>Rn"))))
	      (match_operand 3 "" "")))
   (clobber (reg:SI CRIS_SRP_REGNUM))
   (set (match_operand:SI 4 "register_operand" "=*1,r,r")
	(plus:SI (match_dup 1)
		 (match_dup 2)))]
  "! TARGET_AVOID_GOTPLT"
  "Jsr [%4=%1%S2]"
  [(set_attr "cc" "clobber")])

;; Used in debugging.  No use for the direct pattern; unfilled
;; delayed-branches are taken care of by other means.

(define_insn "nop"
  [(const_int 0)]
  ""
  "nop"
  [(set_attr "cc" "none")])

;; We need to stop accesses to the stack after the memory is
;; deallocated.  Unfortunately, reorg doesn't look at naked clobbers,
;; e.g. (insn ... (clobber (mem:BLK (stack_pointer_rtx)))) and we don't
;; want to use a naked (unspec_volatile) as that would stop any
;; scheduling in the epilogue.  Hence we model it as a "real" insn that
;; sets the memory in an unspecified manner.  FIXME: Unfortunately it
;; still has the effect of an unspec_volatile.
(define_insn "cris_frame_deallocated_barrier"
  [(set (mem:BLK (reg:SI CRIS_SP_REGNUM))
	(unspec:BLK [(const_int 0)] CRIS_UNSPEC_FRAME_DEALLOC))]
  ""
  ""
  [(set_attr "length" "0")])

;; We expand on casesi so we can use "bound" and "add offset fetched from
;; a table to pc" (adds.w [pc+%0.w],pc).

;; Note: if you change the "parallel" (or add anything after it) in
;; this expansion, you must change the macro ASM_OUTPUT_CASE_END
;; accordingly, to add the default case at the end of the jump-table.

(define_expand "casesi"
  [(set (match_dup 5) (match_operand:SI 0 "general_operand" ""))
   (set (match_dup 6)
	(minus:SI (match_dup 5)
		  (match_operand:SI 1 "const_int_operand" "n")))
   (set (match_dup 7)
	(umin:SI (match_dup 6)
		 (match_operand:SI 2 "const_int_operand" "n")))
   (parallel
    [(set (pc)
	  (if_then_else
	   (ltu (match_dup 7) (match_dup 2))
	   (plus:SI (sign_extend:SI
		     (mem:HI
		      (plus:SI (mult:SI (match_dup 7) (const_int 2))
			       (pc))))
		    (pc))
	   (label_ref (match_operand 4 "" ""))))
     (use (label_ref (match_operand 3 "" "")))])]
  ""
{
  operands[2] = plus_constant (operands[2], 1);
  operands[5] = gen_reg_rtx (SImode);
  operands[6] = gen_reg_rtx (SImode);
  operands[7] = gen_reg_rtx (SImode);
})

;; Split-patterns.  Some of them have modes unspecified.  This
;; should always be ok; if for no other reason sparc.md has it as
;; well.
;;
;; When register_operand is specified for an operand, we can get a
;; subreg as well (Axis-990331), so don't just assume that REG_P is true
;; for a register_operand and that REGNO can be used as is.  It is best to
;; guard with REG_P, unless it is worth it to adjust for the subreg case.

;; op [rx + 0],ry,rz
;; The index to rx is optimized into zero, and gone.

;; First, recognize bound [rx],ry,rz; where [rx] is zero-extended,
;; and add/sub [rx],ry,rz, with zero or sign-extend on [rx].
;; Split this into:
;;  move ry,rz
;;  op [rx],rz
;; Lose if rz=ry or rx=rz.
;; Call this op-extend-split

(define_split
  [(set (match_operand 0 "register_operand" "")
	(match_operator
	 4 "cris_operand_extend_operator"
	 [(match_operand 1 "register_operand" "")
	  (match_operator
	   3 "cris_extend_operator"
	   [(match_operand 2 "memory_operand" "")])]))]
  "REG_P (operands[0])
   && REG_P (operands[1])
   && REGNO (operands[1]) != REGNO (operands[0])
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD
   && REG_P (XEXP (operands[2], 0))
   && REGNO (XEXP (operands[2], 0)) != REGNO (operands[0])"
  [(set (match_dup 0)
	(match_dup 1))
   (set (match_dup 0)
	(match_op_dup
	 4 [(match_dup 0)
	    (match_op_dup 3 [(match_dup 2)])]))]
  "")

;; As op-extend-split, but recognize and split op [rz],ry,rz into
;;  ext [rz],rz
;;  op ry,rz
;; Do this for plus or bound only, being commutative operations, since we
;; have swapped the operands.
;; Call this op-extend-split-rx=rz

(define_split
  [(set (match_operand 0 "register_operand" "")
	(match_operator
	 4 "cris_plus_or_bound_operator"
	 [(match_operand 1 "register_operand" "")
	  (match_operator
	   3 "cris_extend_operator"
	   [(match_operand 2 "memory_operand" "")])]))]
  "REG_P (operands[0])
   && REG_P (operands[1])
   && REGNO (operands[1]) != REGNO (operands[0])
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD
   && REG_P (XEXP (operands[2], 0))
   && REGNO (XEXP (operands[2], 0)) == REGNO (operands[0])"
  [(set (match_dup 0)
	(match_op_dup 3 [(match_dup 2)]))
   (set (match_dup 0)
	(match_op_dup
	 4 [(match_dup 0)
	    (match_dup 1)]))]
  "")

;; As the op-extend-split, but swapped operands, and only for
;; plus or bound, being the commutative extend-operators.  FIXME: Why is
;; this needed?  Is it?
;; Call this op-extend-split-swapped

(define_split
  [(set (match_operand 0 "register_operand" "")
	(match_operator
	 4 "cris_plus_or_bound_operator"
	 [(match_operator
	   3 "cris_extend_operator"
	   [(match_operand 2 "memory_operand" "")])
	  (match_operand 1 "register_operand" "")]))]
  "REG_P (operands[0])
   && REG_P (operands[1])
   && REGNO (operands[1]) != REGNO (operands[0])
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD
   && REG_P (XEXP (operands[2], 0))
   && REGNO (XEXP (operands[2], 0)) != REGNO (operands[0])"
  [(set (match_dup 0)
	(match_dup 1))
   (set (match_dup 0)
	(match_op_dup
	 4 [(match_dup 0)
	    (match_op_dup 3 [(match_dup 2)])]))]
  "")

;; As op-extend-split-rx=rz, but swapped operands, only for plus or
;; bound.  Call this op-extend-split-swapped-rx=rz.

(define_split
  [(set (match_operand 0 "register_operand" "")
	(match_operator
	 4 "cris_plus_or_bound_operator"
	 [(match_operator
	   3 "cris_extend_operator"
	   [(match_operand 2 "memory_operand" "")])
	  (match_operand 1 "register_operand" "")]))]
  "REG_P (operands[0])
   && REG_P (operands[1])
   && REGNO (operands[1]) != REGNO (operands[0])
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD
   && REG_P (XEXP (operands[2], 0))
   && REGNO (XEXP (operands[2], 0)) == REGNO (operands[0])"
  [(set (match_dup 0)
	(match_op_dup 3 [(match_dup 2)]))
   (set (match_dup 0)
	(match_op_dup
	 4 [(match_dup 0)
	    (match_dup 1)]))]
  "")

;; As op-extend-split, but the mem operand is not extended.
;;
;; op [rx],ry,rz changed into
;;  move ry,rz
;;  op [rx],rz
;; lose if ry=rz or rx=rz
;; Call this op-extend.

(define_split
  [(set (match_operand 0 "register_operand" "")
	(match_operator
	 3 "cris_orthogonal_operator"
	 [(match_operand 1 "register_operand" "")
	  (match_operand 2 "memory_operand" "")]))]
  "REG_P (operands[0])
   && REG_P (operands[1])
   && REGNO (operands[1]) != REGNO (operands[0])
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD
   && REG_P (XEXP (operands[2], 0))
   && REGNO (XEXP (operands[2], 0)) != REGNO (operands[0])"
  [(set (match_dup 0)
	(match_dup 1))
   (set (match_dup 0)
	(match_op_dup
	 3 [(match_dup 0)
	    (match_dup 2)]))]
  "")

;; As op-extend-split-rx=rz, non-extended.
;; Call this op-split-rx=rz

(define_split
  [(set (match_operand 0 "register_operand" "")
	(match_operator
	 3 "cris_commutative_orth_op"
	 [(match_operand 2 "memory_operand" "")
	  (match_operand 1 "register_operand" "")]))]
  "REG_P (operands[0])
   && REG_P (operands[1])
   && REGNO (operands[1]) != REGNO (operands[0])
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD
   && REG_P (XEXP (operands[2], 0))
   && REGNO (XEXP (operands[2], 0)) != REGNO (operands[0])"
  [(set (match_dup 0)
	(match_dup 1))
   (set (match_dup 0)
	(match_op_dup
	 3 [(match_dup 0)
	    (match_dup 2)]))]
  "")

;; As op-extend-split-swapped, nonextended.
;; Call this op-split-swapped.

(define_split
  [(set (match_operand 0 "register_operand" "")
	(match_operator
	 3 "cris_commutative_orth_op"
	 [(match_operand 1 "register_operand" "")
	  (match_operand 2 "memory_operand" "")]))]
  "REG_P (operands[0]) && REG_P (operands[1])
   && REGNO (operands[1]) != REGNO (operands[0])
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD
   && REG_P (XEXP (operands[2], 0))
   && REGNO (XEXP (operands[2], 0)) == REGNO (operands[0])"
  [(set (match_dup 0)
	(match_dup 2))
   (set (match_dup 0)
	(match_op_dup
	 3 [(match_dup 0)
	    (match_dup 1)]))]
  "")

;; As op-extend-split-swapped-rx=rz, non-extended.
;; Call this op-split-swapped-rx=rz.

(define_split
  [(set (match_operand 0 "register_operand" "")
	(match_operator
	 3 "cris_orthogonal_operator"
	 [(match_operand 2 "memory_operand" "")
	  (match_operand 1 "register_operand" "")]))]
  "REG_P (operands[0]) && REG_P (operands[1])
   && REGNO (operands[1]) != REGNO (operands[0])
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD
   && REG_P (XEXP (operands[2], 0))
   && REGNO (XEXP (operands[2], 0)) == REGNO (operands[0])"
  [(set (match_dup 0)
	(match_dup 2))
   (set (match_dup 0)
	(match_op_dup
	 3 [(match_dup 0)
	    (match_dup 1)]))]
  "")

;; Splits for all cases in side-effect insns where (possibly after reload
;; and register allocation) rx and ry in [rx=ry+i] are equal.

;; move.S1 [rx=rx+rz.S2],ry

(define_split
  [(parallel
    [(set (match_operand 0 "register_operand" "")
	  (match_operator
	   6 "cris_mem_op"
	   [(plus:SI
	     (mult:SI (match_operand:SI 1 "register_operand" "")
		      (match_operand:SI 2 "const_int_operand" ""))
	     (match_operand:SI 3 "register_operand" ""))]))
     (set (match_operand:SI 4 "register_operand" "")
	  (plus:SI (mult:SI (match_dup 1)
			    (match_dup 2))
		    (match_dup 3)))])]
  "REG_P (operands[3]) && REG_P (operands[4])
   && REGNO (operands[3]) == REGNO (operands[4])"
  [(set (match_dup 4) (plus:SI (mult:SI (match_dup 1) (match_dup 2))
			       (match_dup 3)))
   (set (match_dup 0) (match_dup 5))]
  "operands[5] = replace_equiv_address (operands[6], operands[3]);")

;; move.S1 [rx=rx+i],ry

(define_split
  [(parallel
    [(set (match_operand 0 "register_operand" "")
	  (match_operator
	   5 "cris_mem_op"
	   [(plus:SI (match_operand:SI 1 "cris_bdap_operand" "")
		     (match_operand:SI 2 "cris_bdap_operand" ""))]))
     (set (match_operand:SI 3 "register_operand" "")
	   (plus:SI (match_dup 1)
		    (match_dup 2)))])]
  "(rtx_equal_p (operands[3], operands[1])
    || rtx_equal_p (operands[3], operands[2]))"
  [(set (match_dup 3) (plus:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (match_dup 4))]
{
  operands[4] = replace_equiv_address (operands[5], operands[3]);
  cris_order_for_addsi3 (operands, 1);
})

;; move.S1 ry,[rx=rx+rz.S2]

(define_split
  [(parallel
    [(set (match_operator
	   6 "cris_mem_op"
	   [(plus:SI
	     (mult:SI (match_operand:SI 0 "register_operand" "")
		      (match_operand:SI 1 "const_int_operand" ""))
	     (match_operand:SI 2 "register_operand" ""))])
	  (match_operand 3 "register_operand" ""))
     (set (match_operand:SI 4 "register_operand" "")
	   (plus:SI (mult:SI (match_dup 0)
			     (match_dup 1))
		    (match_dup 2)))])]
  "REG_P (operands[2]) && REG_P (operands[4])
   && REGNO (operands[4]) == REGNO (operands[2])"
  [(set (match_dup 4) (plus:SI (mult:SI (match_dup 0) (match_dup 1))
				(match_dup 2)))
   (set (match_dup 5) (match_dup 3))]
  "operands[5] = replace_equiv_address (operands[6], operands[4]);")

;; move.S1 ry,[rx=rx+i]

(define_split
  [(parallel
    [(set (match_operator
	   6 "cris_mem_op"
	   [(plus:SI (match_operand:SI 0 "cris_bdap_operand" "")
		     (match_operand:SI 1 "cris_bdap_operand" ""))])
	  (match_operand 2 "register_operand" ""))
     (set (match_operand:SI 3 "register_operand" "")
	   (plus:SI (match_dup 0)
		   (match_dup 1)))])]
  "(rtx_equal_p (operands[3], operands[0])
    || rtx_equal_p (operands[3], operands[1]))"
  [(set (match_dup 3) (plus:SI (match_dup 0) (match_dup 1)))
   (set (match_dup 5) (match_dup 2))]
{
  operands[5] = replace_equiv_address (operands[6], operands[3]);
  cris_order_for_addsi3 (operands, 0);
})

;; clear.[bwd] [rx=rx+rz.S2]

(define_split
  [(parallel
    [(set (mem:BWD (plus:SI
		    (mult:SI (match_operand:SI 0 "register_operand" "")
			     (match_operand:SI 1 "const_int_operand" ""))
		    (match_operand:SI 2 "register_operand" "")))
	   (const_int 0))
     (set (match_operand:SI 3 "register_operand" "")
	   (plus:SI (mult:SI (match_dup 0)
			     (match_dup 1))
		    (match_dup 2)))])]
  "REG_P (operands[2]) && REG_P (operands[3])
   && REGNO (operands[3]) == REGNO (operands[2])"
  [(set (match_dup 3) (plus:SI (mult:SI (match_dup 0) (match_dup 1))
				(match_dup 2)))
   (set (mem:BWD (match_dup 3)) (const_int 0))]
  "")

;; clear.[bwd] [rx=rx+i]

(define_split
  [(parallel
    [(set (mem:BWD
	   (plus:SI (match_operand:SI 0 "cris_bdap_operand" "")
		    (match_operand:SI 1 "cris_bdap_operand" "")))
	   (const_int 0))
     (set (match_operand:SI 2 "register_operand" "")
	   (plus:SI (match_dup 0)
		    (match_dup 1)))])]
  "(rtx_equal_p (operands[0], operands[2])
    || rtx_equal_p (operands[2], operands[1]))"
  [(set (match_dup 2) (plus:SI (match_dup 0) (match_dup 1)))
   (set (mem:BWD (match_dup 2)) (const_int 0))]
  "cris_order_for_addsi3 (operands, 0);")

;; mov(s|u).S1 [rx=rx+rz.S2],ry

(define_split
  [(parallel
    [(set (match_operand 0 "register_operand" "")
	  (match_operator
	    5 "cris_extend_operator"
	    [(mem (plus:SI
		   (mult:SI (match_operand:SI 1 "register_operand" "")
			    (match_operand:SI 2 "const_int_operand" ""))
		   (match_operand:SI 3 "register_operand" "")))]))
     (set (match_operand:SI 4 "register_operand" "")
	   (plus:SI (mult:SI (match_dup 1)
			     (match_dup 2))
		    (match_dup 3)))])]
  "REG_P (operands[3])
   && REG_P (operands[4])
   && REGNO (operands[3]) == REGNO (operands[4])"
  [(set (match_dup 4) (plus:SI (mult:SI (match_dup 1) (match_dup 2))
				(match_dup 3)))
   (set (match_dup 0) (match_op_dup 5 [(match_dup 6)]))]
  "operands[6] = replace_equiv_address (XEXP (operands[5], 0), operands[4]);")

;; mov(s|u).S1 [rx=rx+i],ry

(define_split
  [(parallel
    [(set (match_operand 0 "register_operand" "")
	  (match_operator
	    4 "cris_extend_operator"
	    [(mem (plus:SI
		   (match_operand:SI 1 "cris_bdap_operand" "")
		   (match_operand:SI 2 "cris_bdap_operand" "")))]))
     (set (match_operand:SI 3 "register_operand" "")
	   (plus:SI (match_dup 1)
		    (match_dup 2)))])]
  "(rtx_equal_p (operands[1], operands[3])
    || rtx_equal_p (operands[2], operands[3]))"
  [(set (match_dup 3) (plus:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (match_op_dup 4 [(match_dup 5)]))]
{
  operands[5] = replace_equiv_address (XEXP (operands[4], 0), operands[3]);
  cris_order_for_addsi3 (operands, 1);
})

;; op.S1 [rx=rx+i],ry

(define_split
  [(parallel
    [(set (match_operand 0 "register_operand" "")
	  (match_operator
	    5 "cris_orthogonal_operator"
	    [(match_operand 1 "register_operand" "")
	     (mem (plus:SI
		   (match_operand:SI 2 "cris_bdap_operand" "")
		   (match_operand:SI 3 "cris_bdap_operand" "")))]))
     (set (match_operand:SI 4 "register_operand" "")
	   (plus:SI (match_dup 2)
		    (match_dup 3)))])]
  "(rtx_equal_p (operands[4], operands[2])
    || rtx_equal_p (operands[4], operands[3]))"
  [(set (match_dup 4) (plus:SI (match_dup 2) (match_dup 3)))
   (set (match_dup 0) (match_op_dup 5 [(match_dup 1) (match_dup 6)]))]
{
  operands[6] = replace_equiv_address (XEXP (operands[5], 1), operands[4]);
  cris_order_for_addsi3 (operands, 2);
})

;; op.S1 [rx=rx+rz.S2],ry

(define_split
  [(parallel
    [(set (match_operand 0 "register_operand" "")
	  (match_operator
	    6 "cris_orthogonal_operator"
	    [(match_operand 1 "register_operand" "")
	     (mem (plus:SI
		   (mult:SI (match_operand:SI 2 "register_operand" "")
			    (match_operand:SI 3 "const_int_operand" ""))
		   (match_operand:SI 4 "register_operand" "")))]))
     (set (match_operand:SI 5 "register_operand" "")
	   (plus:SI (mult:SI (match_dup 2)
			     (match_dup 3))
		   (match_dup 4)))])]
  "REG_P (operands[4])
   && REG_P (operands[5])
   && REGNO (operands[5]) == REGNO (operands[4])"
  [(set (match_dup 5) (plus:SI (mult:SI (match_dup 2) (match_dup 3))
				(match_dup 4)))
   (set (match_dup 0) (match_op_dup 6 [(match_dup 1) (match_dup 7)]))]
  "operands[7] = replace_equiv_address (XEXP (operands[6], 1), operands[5]);")

;; op.S1 [rx=rx+rz.S2],ry (swapped)

(define_split
  [(parallel
    [(set (match_operand 0 "register_operand" "")
	  (match_operator
	    6 "cris_commutative_orth_op"
	    [(mem (plus:SI
		   (mult:SI (match_operand:SI 2 "register_operand" "")
			    (match_operand:SI 3 "const_int_operand" ""))
		   (match_operand:SI 4 "register_operand" "")))
	     (match_operand 1 "register_operand" "")]))
     (set (match_operand:SI 5 "register_operand" "")
	   (plus:SI (mult:SI (match_dup 2)
			     (match_dup 3))
		    (match_dup 4)))])]
  "REG_P (operands[4])
   && REG_P (operands[5])
   && REGNO (operands[5]) == REGNO (operands[4])"
  [(set (match_dup 5) (plus:SI (mult:SI (match_dup 2) (match_dup 3))
			       (match_dup 4)))
   (set (match_dup 0) (match_op_dup 6 [(match_dup 7) (match_dup 1)]))]
  "operands[7] = replace_equiv_address (XEXP (operands[6], 0), operands[5]);")

;; op.S1 [rx=rx+i],ry (swapped)

(define_split
  [(parallel
    [(set (match_operand 0 "register_operand" "")
	  (match_operator
	    5 "cris_commutative_orth_op"
	    [(mem
	      (plus:SI (match_operand:SI 2 "cris_bdap_operand" "")
		       (match_operand:SI 3 "cris_bdap_operand" "")))
	     (match_operand 1 "register_operand" "")]))
     (set (match_operand:SI 4 "register_operand" "")
	  (plus:SI (match_dup 2)
		    (match_dup 3)))])]
  "(rtx_equal_p (operands[4], operands[2])
    || rtx_equal_p (operands[4], operands[3]))"
  [(set (match_dup 4) (plus:SI (match_dup 2) (match_dup 3)))
   (set (match_dup 0) (match_op_dup 5 [(match_dup 6) (match_dup 1)]))]
{
  operands[6] = replace_equiv_address (XEXP (operands[5], 0), operands[4]);
  cris_order_for_addsi3 (operands, 2);
})

;; op(s|u).S1 [rx=rx+rz.S2],ry

(define_split
  [(parallel
    [(set (match_operand 0 "register_operand" "")
	  (match_operator
	    6 "cris_operand_extend_operator"
	    [(match_operand 1 "register_operand" "")
	     (match_operator
	      7 "cris_extend_operator"
	      [(mem (plus:SI
		     (mult:SI (match_operand:SI 2 "register_operand" "")
			      (match_operand:SI 3 "const_int_operand" ""))
		     (match_operand:SI 4 "register_operand" "")))])]))
     (set (match_operand:SI 5 "register_operand" "")
	   (plus:SI (mult:SI (match_dup 2)
			     (match_dup 3))
		    (match_dup 4)))])]
  "REG_P (operands[4])
   && REG_P (operands[5])
   && REGNO (operands[5]) == REGNO (operands[4])"
  [(set (match_dup 5) (plus:SI (mult:SI (match_dup 2) (match_dup 3))
			       (match_dup 4)))
   (set (match_dup 0) (match_op_dup 6 [(match_dup 1) (match_dup 8)]))]
  "operands[8] = gen_rtx_fmt_e (GET_CODE (operands[7]), GET_MODE (operands[7]),
				replace_equiv_address (XEXP (operands[7], 0),
						       operands[5]));")

;; op(s|u).S1 [rx=rx+i],ry

(define_split
  [(parallel
    [(set (match_operand 0 "register_operand" "")
	  (match_operator
	    5 "cris_operand_extend_operator"
	    [(match_operand 1 "register_operand" "")
	     (match_operator
	      6 "cris_extend_operator"
	      [(mem
		(plus:SI (match_operand:SI 2 "cris_bdap_operand" "")
			 (match_operand:SI 3 "cris_bdap_operand" "")
			 ))])]))
     (set (match_operand:SI 4 "register_operand" "")
	   (plus:SI (match_dup 2)
		    (match_dup 3)))])]
  "(rtx_equal_p (operands[4], operands[2])
    || rtx_equal_p (operands[4], operands[3]))"
  [(set (match_dup 4) (plus:SI (match_dup 2) (match_dup 3)))
   (set (match_dup 0) (match_op_dup 5 [(match_dup 1) (match_dup 7)]))]
{
  operands[7] = gen_rtx_fmt_e (GET_CODE (operands[6]), GET_MODE (operands[6]),
			       replace_equiv_address (XEXP (operands[6], 0),
						      operands[4]));
  cris_order_for_addsi3 (operands, 2);
})

;; op(s|u).S1 [rx=rx+rz.S2],ry (swapped, plus or bound)

(define_split
  [(parallel
    [(set (match_operand 0 "register_operand" "")
	  (match_operator
	    7 "cris_plus_or_bound_operator"
	    [(match_operator
	      6 "cris_extend_operator"
	      [(mem (plus:SI
		     (mult:SI (match_operand:SI 2 "register_operand" "")
			      (match_operand:SI 3 "const_int_operand" ""))
		     (match_operand:SI 4 "register_operand" "")))])
	     (match_operand 1 "register_operand" "")]))
     (set (match_operand:SI 5 "register_operand" "")
	   (plus:SI (mult:SI (match_dup 2)
			     (match_dup 3))
		    (match_dup 4)))])]
  "REG_P (operands[4]) && REG_P (operands[5])
   && REGNO (operands[5]) == REGNO (operands[4])"
  [(set (match_dup 5) (plus:SI (mult:SI (match_dup 2) (match_dup 3))
			       (match_dup 4)))
   (set (match_dup 0) (match_op_dup 6 [(match_dup 8) (match_dup 1)]))]
  "operands[8] = gen_rtx_fmt_e (GET_CODE (operands[6]), GET_MODE (operands[6]),
				replace_equiv_address (XEXP (operands[6], 0),
						       operands[5]));")

;; op(s|u).S1 [rx=rx+i],ry (swapped, plus or bound)

(define_split
  [(parallel
    [(set (match_operand 0 "register_operand" "")
	  (match_operator
	    6 "cris_plus_or_bound_operator"
	    [(match_operator
	      5 "cris_extend_operator"
	     [(mem (plus:SI
		    (match_operand:SI 2 "cris_bdap_operand" "")
		    (match_operand:SI 3 "cris_bdap_operand" "")))])
	     (match_operand 1 "register_operand" "")]))
     (set (match_operand:SI 4 "register_operand" "")
	   (plus:SI (match_dup 2)
		    (match_dup 3)))])]
  "(rtx_equal_p (operands[4], operands[2])
    || rtx_equal_p (operands[4], operands[3]))"
  [(set (match_dup 4) (plus:SI (match_dup 2) (match_dup 3)))
   (set (match_dup 0) (match_op_dup 6 [(match_dup 7) (match_dup 1)]))]
{
  operands[7] = gen_rtx_fmt_e (GET_CODE (operands[5]), GET_MODE (operands[5]),
			       replace_equiv_address (XEXP (operands[5], 0),
						      operands[4]));
  cris_order_for_addsi3 (operands, 2);
})

;; Splits for addressing prefixes that have no side-effects, so we can
;; fill a delay slot.  Never split if we lose something, though.

;; If we have a
;;  move [indirect_ref],rx
;; where indirect ref = {const, [r+], [r]}, it costs as much as
;;  move indirect_ref,rx
;;  move [rx],rx
;; Take care not to allow indirect_ref = register.

;; We're not allowed to generate copies of registers with different mode
;; until after reload; copying pseudos upsets reload.  CVS as of
;; 2001-08-24, unwind-dw2-fde.c, _Unwind_Find_FDE ICE in
;; cselib_invalidate_regno.

(define_split ; indir_to_reg_split
  [(set (match_operand 0 "register_operand" "")
	(match_operand 1 "indirect_operand" ""))]
  "reload_completed
   && REG_P (operands[0])
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD
   && (GET_CODE (XEXP (operands[1], 0)) == MEM
       || CONSTANT_P (XEXP (operands[1], 0)))
   && REGNO (operands[0]) < CRIS_LAST_GENERAL_REGISTER"
  [(set (match_dup 2) (match_dup 4))
   (set (match_dup 0) (match_dup 3))]
  "operands[2] = gen_rtx_REG (Pmode, REGNO (operands[0]));
   operands[3] = replace_equiv_address (operands[1], operands[2]);
   operands[4] = XEXP (operands[1], 0);")

;; As the above, but MOVS and MOVU.

(define_split
  [(set (match_operand 0 "register_operand" "")
	(match_operator
	 4 "cris_extend_operator"
	 [(match_operand 1 "indirect_operand" "")]))]
  "reload_completed
   && REG_P (operands[0])
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD
   && (GET_CODE (XEXP (operands[1], 0)) == MEM
       || CONSTANT_P (XEXP (operands[1], 0)))"
  [(set (match_dup 2) (match_dup 5))
   (set (match_dup 0) (match_op_dup 4 [(match_dup 3)]))]
  "operands[2] = gen_rtx_REG (Pmode, REGNO (operands[0]));
   operands[3] = replace_equiv_address (XEXP (operands[4], 0), operands[2]);
   operands[5] = XEXP (operands[1], 0);")

;; Various peephole optimizations.
;;
;; Watch out: when you exchange one set of instructions for another, the
;; condition codes setting must be the same, or you have to CC_INIT or
;; whatever is appropriate, in the pattern before you emit the
;; assembly text.  This is best done here, not in cris_notice_update_cc,
;; to keep changes local to their cause.
;;
;; Do not add patterns that you do not know will be matched.
;; Please also add a self-contained testcase.

;; We have trouble with and:s and shifts.  Maybe something is broken in
;; gcc?  Or it could just be that bit-field insn expansion is a bit
;; suboptimal when not having extzv insns.
;; Testcase for the following four peepholes: gcc.dg/cris-peep2-xsrand.c

(define_peephole2 ; asrandb (peephole casesi+31)
  [(set (match_operand:SI 0 "register_operand" "")
	(ashiftrt:SI (match_dup 0)
		     (match_operand:SI 1 "const_int_operand" "")))
   (set (match_dup 0)
	(and:SI (match_dup 0)
		(match_operand 2 "const_int_operand" "")))]
  "INTVAL (operands[2]) > 31
   && INTVAL (operands[2]) < 255
   && INTVAL (operands[1]) > 23
   /* Check that the and-operation enables us to use logical-shift.  */
   && (INTVAL (operands[2])
	  & ((HOST_WIDE_INT) -1 << (32 - INTVAL (operands[1])))) == 0"
  [(set (match_dup 0) (lshiftrt:SI (match_dup 0) (match_dup 1)))
   (set (match_dup 3) (and:QI (match_dup 3) (match_dup 4)))]
  ;; FIXME: CC0 is valid except for the M bit.
{
  operands[3] = gen_rtx_REG (QImode, REGNO (operands[0]));
  operands[4] = GEN_INT (trunc_int_for_mode (INTVAL (operands[2]), QImode));
})

(define_peephole2 ; asrandw (peephole casesi+32)
  [(set (match_operand:SI 0 "register_operand" "")
	(ashiftrt:SI (match_dup 0)
		     (match_operand:SI 1 "const_int_operand" "")))
   (set (match_dup 0)
	(and:SI (match_dup 0) (match_operand 2 "const_int_operand" "")))]
  "INTVAL (operands[2]) > 31
   && INTVAL (operands[2]) < 65535
   && INTVAL (operands[2]) != 255
   && INTVAL (operands[1]) > 15
   /* Check that the and-operation enables us to use logical-shift.  */
   && (INTVAL (operands[2])
       & ((HOST_WIDE_INT) -1 << (32 - INTVAL (operands[1])))) == 0"
  [(set (match_dup 0) (lshiftrt:SI (match_dup 0) (match_dup 1)))
   (set (match_dup 3) (and:HI (match_dup 3) (match_dup 4)))]
  ;; FIXME: CC0 is valid except for the M bit.
{
  operands[3] = gen_rtx_REG (HImode, REGNO (operands[0]));
  operands[4] = GEN_INT (trunc_int_for_mode (INTVAL (operands[2]), HImode));
})

(define_peephole2 ; lsrandb (peephole casesi+33)
  [(set (match_operand:SI 0 "register_operand" "")
	(lshiftrt:SI (match_dup 0)
		     (match_operand:SI 1 "const_int_operand" "")))
   (set (match_dup 0)
	(and:SI (match_dup 0) (match_operand 2 "const_int_operand" "")))]
  "INTVAL (operands[2]) > 31
   && INTVAL (operands[2]) < 255
   && INTVAL (operands[1]) > 23"
  [(set (match_dup 0) (lshiftrt:SI (match_dup 0) (match_dup 1)))
   (set (match_dup 3) (and:QI (match_dup 3) (match_dup 4)))]
  ;; FIXME: CC0 is valid except for the M bit.
{
  operands[3] = gen_rtx_REG (QImode, REGNO (operands[0]));
  operands[4] = GEN_INT (trunc_int_for_mode (INTVAL (operands[2]), QImode));
})

(define_peephole2 ; lsrandw (peephole casesi+34)
  [(set (match_operand:SI 0 "register_operand" "")
	(lshiftrt:SI (match_dup 0)
		     (match_operand:SI 1 "const_int_operand" "")))
   (set (match_dup 0)
	(and:SI (match_dup 0) (match_operand 2 "const_int_operand" "")))]
  "INTVAL (operands[2]) > 31 && INTVAL (operands[2]) < 65535
   && INTVAL (operands[2]) != 255
   && INTVAL (operands[1]) > 15"
  [(set (match_dup 0) (lshiftrt:SI (match_dup 0) (match_dup 1)))
   (set (match_dup 3) (and:HI (match_dup 3) (match_dup 4)))]
  ;; FIXME: CC0 is valid except for the M bit.
{
  operands[3] = gen_rtx_REG (HImode, REGNO (operands[0]));
  operands[4] = GEN_INT (trunc_int_for_mode (INTVAL (operands[2]), HImode));
})


;; Change
;;  add.d n,rx
;;  move [rx],ry
;; into
;;  move [rx=rx+n],ry
;; when -128 <= n <= 127.
;; This will reduce the size of the assembler code for n = [-128..127],
;; and speed up accordingly.  Don't match if the previous insn is
;; (set rx rz) because that combination is matched by another peephole.
;; No stable test-case.

(define_peephole2 ; moversideqi (peephole casesi+35)
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "register_operand" "")
		 (match_operand:SI 2 "const_int_operand" "")))
   (set (match_operand 3 "register_operand" "")
	(match_operator 4 "cris_mem_op" [(match_dup 0)]))]
  "GET_MODE_SIZE (GET_MODE (operands[4])) <= UNITS_PER_WORD
   && REGNO (operands[3]) != REGNO (operands[0])
   && (BASE_P (operands[1]) || BASE_P (operands[2]))
   && ! CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'J')
   && ! CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'N')
   && (INTVAL (operands[2]) >= -128 && INTVAL (operands[2]) < 128)"
  [(parallel
    [(set (match_dup 3) (match_dup 5))
     (set (match_dup 0) (plus:SI (match_dup 1) (match_dup 2)))])]
  ;; Checking the previous insn is a bit too awkward for the condition.
{
  rtx prev = prev_nonnote_insn (curr_insn);
  if (prev != NULL_RTX)
    {
      rtx set = single_set (prev);
      if (set != NULL_RTX
	  && REG_S_P (SET_DEST (set))
	  && REGNO (SET_DEST (set)) == REGNO (operands[0])
	  && REG_S_P (SET_SRC (set)))
	FAIL;
    }
  operands[5]
    = replace_equiv_address (operands[4],
			     gen_rtx_PLUS (SImode,
					   operands[1], operands[2]));
})

;; Vice versa: move ry,[rx=rx+n]

(define_peephole2 ; movemsideqi (peephole casesi+36)
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "register_operand" "")
		 (match_operand:SI 2 "const_int_operand" "")))
   (set (match_operator 3 "cris_mem_op" [(match_dup 0)])
	(match_operand 4 "register_operand" ""))]
  "GET_MODE_SIZE (GET_MODE (operands[4])) <= UNITS_PER_WORD
   && REGNO (operands[4]) != REGNO (operands[0])
   && (BASE_P (operands[1]) || BASE_P (operands[2]))
   && ! CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'J')
   && ! CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'N')
   && (INTVAL (operands[2]) >= -128 && INTVAL (operands[2]) < 128)"
  [(parallel
    [(set (match_dup 5) (match_dup 4))
     (set (match_dup 0) (plus:SI (match_dup 1) (match_dup 2)))])]
  "operands[5]
     = replace_equiv_address (operands[3],
			      gen_rtx_PLUS (SImode,
					    operands[1], operands[2]));")

;; As above, change:
;;  add.d n,rx
;;  op.d [rx],ry
;; into:
;;  op.d [rx=rx+n],ry
;; Saves when n = [-128..127].
;;
;; Splitting and joining combinations for side-effect modes are slightly
;; out of hand.  They probably will not save the time they take typing in,
;; not to mention the bugs that creep in.  FIXME: Get rid of as many of
;; the splits and peepholes as possible.
;; No stable test-case.

(define_peephole2 ; mover2side (peephole casesi+37)
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "register_operand" "")
		 (match_operand:SI 2 "const_int_operand" "")))
   (set (match_operand 3 "register_operand" "")
	  (match_operator 4 "cris_orthogonal_operator"
			  [(match_dup 3)
			   (match_operator
			    5 "cris_mem_op" [(match_dup 0)])]))]
  ;; FIXME: What about DFmode?
  ;; Change to GET_MODE_SIZE (GET_MODE (operands[3])) <= UNITS_PER_WORD?
  "GET_MODE (operands[3]) != DImode
   && REGNO (operands[0]) != REGNO (operands[3])
   && ! CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'J')
   && ! CONST_OK_FOR_LETTER_P (INTVAL (operands[2]), 'N')
   && INTVAL (operands[2]) >= -128
   && INTVAL (operands[2]) <= 127"
  [(parallel
    [(set (match_dup 3) (match_op_dup 4 [(match_dup 3) (match_dup 6)]))
     (set (match_dup 0) (plus:SI (match_dup 1) (match_dup 2)))])]
  "operands[6]
     = replace_equiv_address (operands[5],
			      gen_rtx_PLUS (SImode,
					    operands[1], operands[2]));")

;; Sometimes, for some reason the pattern
;;  move x,rx
;;  add y,rx
;;  move [rx],rz
;; will occur.  Solve this, and likewise for to-memory.
;; No stable test-case.

(define_peephole2 ; moverside (peephole casesi+38)
  [(set (match_operand:SI 0 "register_operand" "")
	(match_operand:SI 1 "cris_bdap_biap_operand" ""))
   (set (match_dup 0)
	(plus:SI (match_operand:SI 2 "cris_bdap_biap_operand" "")
		 (match_operand:SI 3 "cris_bdap_biap_operand" "")))
   (set (match_operand 4 "register_operand" "")
	(match_operator 5 "cris_mem_op" [(match_dup 0)]))]
  "(rtx_equal_p (operands[2], operands[0])
    || rtx_equal_p (operands[3], operands[0]))
   && cris_side_effect_mode_ok (PLUS, operands, 0,
				(REG_S_P (operands[1])
				 ? 1
				 : (rtx_equal_p (operands[2], operands[0])
				    ? 3 : 2)),
				(! REG_S_P (operands[1])
				 ? 1
				 : (rtx_equal_p (operands[2], operands[0])
				    ? 3 : 2)),
				-1, 4)"
  [(parallel
    [(set (match_dup 4) (match_dup 6))
     (set (match_dup 0) (plus:SI (match_dup 7) (match_dup 8)))])]
{
  rtx otherop
    = rtx_equal_p (operands[2], operands[0]) ? operands[3] : operands[2];

  /* Make sure we have canonical RTX so we match the insn pattern -
     not a constant in the first operand.  We also require the order
     (plus reg mem) to match the final pattern.  */
  if (CONSTANT_P (otherop) || MEM_P (otherop))
    {
      operands[7] = operands[1];
      operands[8] = otherop;
    }
  else
    {
      operands[7] = otherop;
      operands[8] = operands[1];
    }
  operands[6]
    = replace_equiv_address (operands[5],
			     gen_rtx_PLUS (SImode,
					   operands[7], operands[8]));
})

;; As above but to memory.
;; FIXME: Split movemside and moverside into variants and prune
;; the ones that don't trig.
;; No stable test-case.

(define_peephole2 ; movemside (peephole casesi+39)
  [(set (match_operand:SI 0 "register_operand" "")
	(match_operand:SI 1 "cris_bdap_biap_operand" ""))
   (set (match_dup 0)
	(plus:SI (match_operand:SI 2 "cris_bdap_biap_operand" "")
		 (match_operand:SI 3 "cris_bdap_biap_operand" "")))
   (set (match_operator 4 "cris_mem_op" [(match_dup 0)])
	(match_operand 5 "register_operand" ""))]
  "(rtx_equal_p (operands[2], operands[0])
    || rtx_equal_p (operands[3], operands[0]))
   && cris_side_effect_mode_ok (PLUS, operands, 0,
				(REG_S_P (operands[1])
				 ? 1
				 : (rtx_equal_p (operands[2], operands[0])
				    ? 3 : 2)),
				(! REG_S_P (operands[1])
				   ? 1
				 : (rtx_equal_p (operands[2], operands[0])
				    ? 3 : 2)),
				-1, 5)"
  [(parallel
    [(set (match_dup 6) (match_dup 5))
     (set (match_dup 0) (plus:SI (match_dup 7) (match_dup 8)))])]
{
  rtx otherop
    = rtx_equal_p (operands[2], operands[0]) ? operands[3] : operands[2];

  /* Make sure we have canonical RTX so we match the insn pattern -
     not a constant in the first operand.  We also require the order
     (plus reg mem) to match the final pattern.  */
  if (CONSTANT_P (otherop) || MEM_P (otherop))
    {
      operands[7] = operands[1];
      operands[8] = otherop;
    }
  else
    {
      operands[7] = otherop;
      operands[8] = operands[1];
    }
  operands[6]
    = replace_equiv_address (operands[4],
			     gen_rtx_PLUS (SImode,
					   operands[7], operands[8]));
})

;; Another spotted bad code:
;;   move rx,ry
;;   move [ry],ry
;; No stable test-case.

(define_peephole2 ; movei (peephole casesi+42)
  [(set (match_operand:SI 0 "register_operand" "")
	(match_operand:SI 1 "register_operand" ""))
   (set (match_operand 2 "register_operand" "")
	(match_operator 3 "cris_mem_op" [(match_dup 0)]))]
  "REGNO (operands[0]) == REGNO (operands[2])
   && (REGNO_REG_CLASS (REGNO (operands[0]))
       == REGNO_REG_CLASS (REGNO (operands[1])))
   && GET_MODE_SIZE (GET_MODE (operands[2])) <= UNITS_PER_WORD"
  [(set (match_dup 2) (match_dup 4))]
  "operands[4] = replace_equiv_address (operands[3], operands[1]);")

;;   move.d [r10+16],r9
;;   and.d r12,r9
;; change to
;;   and.d [r10+16],r12,r9
;; With generalization of the operation, the size and the addressing mode.
;;  This seems to be the result of a quirk in register allocation
;; missing the three-operand cases when having different predicates.
;; Maybe that it matters that it is a commutative operation.
;;  This pattern helps that situation, but there's still the increased
;; register pressure.
;;  Note that adding the noncommutative variant did not show any matches
;; in ipps and cc1, so it's not here.
;; No stable test-case.

(define_peephole2 ; op3 (peephole casesi+44)
  [(set (match_operand 0 "register_operand" "")
	(match_operator
	 6 "cris_mem_op"
	 [(plus:SI
	   (match_operand:SI 1 "cris_bdap_biap_operand" "")
	   (match_operand:SI 2 "cris_bdap_biap_operand" ""))]))
   (set (match_dup 0)
	(match_operator
	 5 "cris_commutative_orth_op"
	 [(match_operand 3 "register_operand" "")
	  (match_operand 4 "register_operand" "")]))]
  "(rtx_equal_p (operands[3], operands[0])
    || rtx_equal_p (operands[4], operands[0]))
   && ! rtx_equal_p (operands[3], operands[4])
   && (REG_S_P (operands[1]) || REG_S_P (operands[2]))
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= UNITS_PER_WORD"
  [(set (match_dup 0) (match_op_dup 5 [(match_dup 7) (match_dup 6)]))]
  "operands[7]
     = rtx_equal_p (operands[3], operands[0]) ? operands[4] : operands[3];")

;;  I cannot tell GCC (2.1, 2.7.2) how to correctly reload an instruction
;; that looks like
;;   and.b some_byte,const,reg_32
;; where reg_32 is the destination of the "three-address" code optimally.
;; It should be:
;;   movu.b some_byte,reg_32
;;   and.b const,reg_32
;; but is turns into:
;;   move.b some_byte,reg_32
;;   and.d const,reg_32
;; Fix it here.
;; Testcases: gcc.dg/cris-peep2-andu1.c gcc.dg/cris-peep2-andu2.c

(define_peephole2 ; andu (casesi+45)
  [(set (match_operand:SI 0 "register_operand" "")
	(match_operand:SI 1 "nonimmediate_operand" ""))
   (set (match_operand:SI 2 "register_operand" "")
	(and:SI (match_dup 0)
		(match_operand:SI 3 "const_int_operand" "")))]
   ;; Since the size of the memory access could be made different here,
   ;; don't do this for a mem-volatile access.
  "REGNO (operands[2]) == REGNO (operands[0])
   && INTVAL (operands[3]) <= 65535 && INTVAL (operands[3]) >= 0
   && ! CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'I')
   && (GET_CODE (operands[1]) != MEM || ! MEM_VOLATILE_P (operands[1]))"
  ;; FIXME: CC0 valid except for M (i.e. CC_NOT_NEGATIVE).
  [(set (match_dup 0) (match_dup 4))
   (set (match_dup 5) (match_dup 6))]
{
  enum machine_mode zmode = INTVAL (operands[3]) <= 255 ? QImode : HImode;
  enum machine_mode amode
    = CONST_OK_FOR_LETTER_P (INTVAL (operands[3]), 'O') ? SImode : zmode;
  rtx op1
    = (REG_S_P (operands[1])
       ? gen_rtx_REG (zmode, REGNO (operands[1]))
       : adjust_address (operands[1], zmode, 0));
  operands[4]
    = gen_rtx_ZERO_EXTEND (SImode, op1);
  operands[5] = gen_rtx_REG (amode, REGNO (operands[0]));
  operands[6]
    = gen_rtx_AND (amode, gen_rtx_REG (amode, REGNO (operands[0])),
		   GEN_INT (trunc_int_for_mode (INTVAL (operands[3]),
						amode == SImode
						? QImode : amode)));
})

;; Try and avoid GOTPLT reads escaping a call: transform them into
;; PLT.  Curiously (but thankfully), peepholes for instructions
;; *without side-effects* that just feed a call (or call_value) are
;; not matched neither in a build or test-suite, so those patterns are
;; omitted.

;; A "normal" move where we don't check the consumer.

(define_peephole2 ; gotplt-to-plt
  [(set
    (match_operand:SI 0 "register_operand" "")
    (match_operator:SI
     1 "cris_mem_op"
     [(plus:SI
       (reg:SI CRIS_GOT_REGNUM)
       (const:SI
	(unspec:SI [(match_operand:SI 2 "cris_general_operand_or_symbol" "")]
		   CRIS_UNSPEC_PLTGOTREAD)))]))]
  "flag_pic
   && cris_valid_pic_const (XEXP (XEXP (operands[1], 0), 1))
   && REGNO_REG_CLASS (REGNO (operands[0])) == REGNO_REG_CLASS (0)"
  [(set (match_dup 0) (const:SI (unspec:SI [(match_dup 2)] CRIS_UNSPEC_PLT)))
   (set (match_dup 0) (plus:SI (match_dup 0) (reg:SI CRIS_GOT_REGNUM)))]
  "")

;; And one set with a side-effect getting the PLTGOT offset.
;; First call and call_value variants.

(define_peephole2 ; gotplt-to-plt-side-call
  [(parallel
    [(set
      (match_operand:SI 0 "register_operand" "")
      (match_operator:SI
       1 "cris_mem_op"
       [(plus:SI
	 (reg:SI CRIS_GOT_REGNUM)
	 (const:SI
	  (unspec:SI [(match_operand:SI
		       2 "cris_general_operand_or_symbol" "")]
		     CRIS_UNSPEC_PLTGOTREAD)))]))
     (set (match_operand:SI 3 "register_operand" "")
	  (plus:SI (reg:SI CRIS_GOT_REGNUM)
		   (const:SI
		    (unspec:SI [(match_dup 2)] CRIS_UNSPEC_PLTGOTREAD))))])
  (parallel [(call (mem:QI (match_dup 0))
		    (match_operand 4 "" ""))
	      (clobber (reg:SI CRIS_SRP_REGNUM))])]
  "flag_pic
   && cris_valid_pic_const (XEXP (XEXP (operands[1], 0), 1))
   && peep2_reg_dead_p (2, operands[0])"
  [(parallel [(call (mem:QI (match_dup 1))
		    (match_dup 4))
	      (clobber (reg:SI CRIS_SRP_REGNUM))
	      (set (match_dup 3)
		   (plus:SI (reg:SI CRIS_GOT_REGNUM)
			    (const:SI
			     (unspec:SI [(match_dup 2)]
					CRIS_UNSPEC_PLTGOTREAD))))])]
  "")

(define_peephole2 ; gotplt-to-plt-side-call-value
  [(parallel
    [(set
      (match_operand:SI 0 "register_operand" "")
      (match_operator:SI
       1 "cris_mem_op"
       [(plus:SI
	 (reg:SI CRIS_GOT_REGNUM)
	 (const:SI
	  (unspec:SI [(match_operand:SI
		       2 "cris_general_operand_or_symbol" "")]
		     CRIS_UNSPEC_PLTGOTREAD)))]))
     (set (match_operand:SI 3 "register_operand" "")
	  (plus:SI (reg:SI CRIS_GOT_REGNUM)
		   (const:SI
		    (unspec:SI [(match_dup 2)] CRIS_UNSPEC_PLTGOTREAD))))])
   (parallel [(set (match_operand 5 "" "")
		   (call (mem:QI (match_dup 0))
			 (match_operand 4 "" "")))
	      (clobber (reg:SI CRIS_SRP_REGNUM))])]
  "flag_pic
   && cris_valid_pic_const (XEXP (XEXP (operands[1], 0), 1))
   && peep2_reg_dead_p (2, operands[0])"
  [(parallel [(set (match_dup 5)
		   (call (mem:QI (match_dup 1))
			 (match_dup 4)))
	      (clobber (reg:SI CRIS_SRP_REGNUM))
	      (set (match_dup 3)
		   (plus:SI (reg:SI CRIS_GOT_REGNUM)
			    (const:SI
			     (unspec:SI [(match_dup 2)]
					CRIS_UNSPEC_PLTGOTREAD))))])]
  "")

(define_peephole2 ; gotplt-to-plt-side
  [(parallel
    [(set
      (match_operand:SI 0 "register_operand" "")
      (match_operator:SI
       1 "cris_mem_op"
       [(plus:SI
	 (reg:SI CRIS_GOT_REGNUM)
	 (const:SI
	  (unspec:SI [(match_operand:SI
		       2 "cris_general_operand_or_symbol" "")]
		     CRIS_UNSPEC_PLTGOTREAD)))]))
     (set (match_operand:SI 3 "register_operand" "")
	  (plus:SI (reg:SI CRIS_GOT_REGNUM)
		   (const:SI
		    (unspec:SI [(match_dup 2)] CRIS_UNSPEC_PLTGOTREAD))))])]
  "flag_pic
   && cris_valid_pic_const (XEXP (XEXP (operands[1], 0), 1))
   && REGNO_REG_CLASS (REGNO (operands[0])) == REGNO_REG_CLASS (0)"
  [(set (match_dup 3)
	(const:SI (unspec:SI [(match_dup 2)] CRIS_UNSPEC_PLTGOTREAD)))
   (set (match_dup 3) (plus:SI (match_dup 3) (reg:SI CRIS_GOT_REGNUM)))
   (set (match_dup 0) (const:SI (unspec:SI [(match_dup 2)] CRIS_UNSPEC_PLT)))
   (set (match_dup 0) (plus:SI (match_dup 0) (reg:SI CRIS_GOT_REGNUM)))]
  "")

;; Local variables:
;; mode:emacs-lisp
;; comment-start: ";; "
;; eval: (set-syntax-table (copy-sequence (syntax-table)))
;; eval: (modify-syntax-entry ?[ "(]")
;; eval: (modify-syntax-entry ?] ")[")
;; eval: (modify-syntax-entry ?{ "(}")
;; eval: (modify-syntax-entry ?} "){")
;; eval: (setq indent-tabs-mode t)
;; End:

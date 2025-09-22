;;  Machine description for Sunplus S+CORE
;;  Copyright (C) 2005
;;  Free Software Foundation, Inc.
;;  Contributed by Sunnorth.

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

; branch        conditional branch
; jump          unconditional jump
; call          unconditional call
; load          load instruction(s)
; store         store instruction(s)
; cmp           integer compare
; arith         integer arithmetic instruction
; move          data movement within same register set
; const         load constant
; nop           no operation
; mul           integer multiply
; div           integer divide
; cndmv         conditional moves
; fce           transfer from hi/lo registers
; tce           transfer to   hi/lo registers
; fsr           transfer from special registers
; tsr           transfer to   special registers
; pseudo        pseudo instruction

(define_constants
  [(CC_REGNUM       33)
   (T_REGNUM        34)
   (RA_REGNUM       3)
   (SP_REGNUM       0)
   (AT_REGNUM       1)
   (FP_REGNUM       2)
   (RT_REGNUM       4)
   (GP_REGNUM       28)
   (EH_REGNUM       29)
   (HI_REGNUM       48)
   (LO_REGNUM       49)
   (CN_REGNUM       50)
   (LC_REGNUM       51)
   (SC_REGNUM       52)])

(define_constants
   [(BITTST         0)
    (CPLOAD         1)
    (CPRESTORE      2)

    (SCB            3)
    (SCW            4)
    (SCE            5)
    (SCLC           6)

    (LCB            7)
    (LCW            8)
    (LCE            9)

    (SFFS           10)])

(define_attr "type"
  "unknown,branch,jump,call,load,store,cmp,arith,move,const,nop,mul,div,cndmv,fce,tce,fsr,tsr,fcr,tcr,pseudo"
  (const_string "unknown"))

(define_attr "mode" "unknown,none,QI,HI,SI,DI"
  (const_string "unknown"))

(define_attr "up_c" "yes,no"
  (const_string "no"))

(include "score7.md")
(include "predicates.md")
(include "misc.md")
(include "mac.md")

(define_expand "movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand")
        (match_operand:QI 1 "general_operand"))]
  ""
{
  if (MEM_P (operands[0])
      && !register_operand (operands[1], QImode))
    {
      operands[1] = force_reg (QImode, operands[1]);
    }
})

(define_insn "*movqi_insns"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=d,d,d,m,d,*x,d,*a")
        (match_operand:QI 1 "general_operand" "i,d,m,d,*x,d,*a,d"))]
  "!MEM_P (operands[0]) || register_operand (operands[1], QImode)"
{
  switch (which_alternative)
    {
    case 0: return mdp_limm (operands);
    case 1: return mdp_move (operands);
    case 2: return mdp_linsn (operands, MDA_BYTE, false);
    case 3: return mdp_sinsn (operands, MDA_BYTE);
    case 4: return TARGET_MAC ? \"mf%1%S0 %0\" : \"mf%1    %0\";
    case 5: return TARGET_MAC ? \"mt%0%S1 %1\" : \"mt%0    %1\";
    case 6: return \"mfsr    %0, %1\";
    case 7: return \"mtsr    %1, %0\";
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith,move,load,store,fce,tce,fsr,tsr")
   (set_attr "mode" "QI")])

(define_expand "movhi"
  [(set (match_operand:HI 0 "nonimmediate_operand")
        (match_operand:HI 1 "general_operand"))]
  ""
{
  if (MEM_P (operands[0])
      && !register_operand (operands[1], HImode))
    {
      operands[1] = force_reg (HImode, operands[1]);
    }
})

(define_insn "*movhi_insns"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=d,d,d,m,d,*x,d,*a")
        (match_operand:HI 1 "general_operand" "i,d,m,d,*x,d,*a,d"))]
  "!MEM_P (operands[0]) || register_operand (operands[1], HImode)"
{
  switch (which_alternative)
    {
    case 0: return mdp_limm (operands);
    case 1: return mdp_move (operands);
    case 2: return mdp_linsn (operands, MDA_HWORD, false);
    case 3: return mdp_sinsn (operands, MDA_HWORD);
    case 4: return TARGET_MAC ? \"mf%1%S0 %0\" : \"mf%1    %0\";
    case 5: return TARGET_MAC ? \"mt%0%S1 %1\" : \"mt%0    %1\";
    case 6: return \"mfsr    %0, %1\";
    case 7: return \"mtsr    %1, %0\";
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith,move,load,store,fce,tce,fsr,tsr")
   (set_attr "mode" "HI")])

(define_expand "movsi"
  [(set (match_operand:SI 0 "nonimmediate_operand")
        (match_operand:SI 1 "general_operand"))]
  ""
{
  if (MEM_P (operands[0])
      && !register_operand (operands[1], SImode))
    {
      operands[1] = force_reg (SImode, operands[1]);
    }
})

(define_insn "*movsi_insns"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=d,d,d,m,d,*x,d,*a,d,*c")
        (match_operand:SI 1 "general_operand" "i,d,m,d,*x,d,*a,d,*c,d"))]
  "!MEM_P (operands[0]) || register_operand (operands[1], SImode)"
{
  switch (which_alternative)
    {
    case 0:
      if (GET_CODE (operands[1]) != CONST_INT)
        return \"la      %0, %1\";
      else
        return mdp_limm (operands);
    case 1: return mdp_move (operands);
    case 2: return mdp_linsn (operands, MDA_WORD, false);
    case 3: return mdp_sinsn (operands, MDA_WORD);
    case 4: return TARGET_MAC ? \"mf%1%S0 %0\" : \"mf%1    %0\";
    case 5: return TARGET_MAC ? \"mt%0%S1 %1\" : \"mt%0    %1\";
    case 6: return \"mfsr    %0, %1\";
    case 7: return \"mtsr    %1, %0\";
    case 8: return \"mfcr    %0, %1\";
    case 9: return \"mtcr    %1, %0\";
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith,move,load,store,fce,tce,fsr,tsr,fcr,tcr")
   (set_attr "mode" "SI")])

(define_insn_and_split "movdi"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=d,d,d,m,d,*x")
        (match_operand:DI 1 "general_operand" "i,d,m,d,*x,d"))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  mds_movdi (operands);
  DONE;
})

(define_expand "movsf"
  [(set (match_operand:SF 0 "nonimmediate_operand")
        (match_operand:SF 1 "general_operand"))]
  ""
{
  if (MEM_P (operands[0])
      && !register_operand (operands[1], SFmode))
    {
      operands[1] = force_reg (SFmode, operands[1]);
    }
})

(define_insn "*movsf_insns"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=d,d,d,m")
        (match_operand:SF 1 "general_operand" "i,d,m,d"))]
  "!MEM_P (operands[0]) || register_operand (operands[1], SFmode)"
{
  switch (which_alternative)
    {
    case 0: return \"li      %0, %D1\";;
    case 1: return mdp_move (operands);
    case 2: return mdp_linsn (operands, MDA_WORD, false);
    case 3: return mdp_sinsn (operands, MDA_WORD);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith,move,load,store")
   (set_attr "mode" "SI")])

(define_insn_and_split "movdf"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=d,d,d,m")
        (match_operand:DF 1 "general_operand" "i,d,m,d"))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  mds_movdi (operands);
  DONE;
})

(define_insn "addsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,d,d,d")
        (plus:SI (match_operand:SI 1 "register_operand" "0,0,d,d")
                 (match_operand:SI 2 "arith_operand" "I,L,N,d")))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"addis %0, %U2\";
    case 1: return mdp_select_add_imm (operands, false);
    case 2: return \"addri %0, %1, %c2\";
    case 3: return mdp_select (operands, "add", true, "", false);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "*addsi3_cmp"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (plus:SI
                        (match_operand:SI 1 "register_operand" "0,0,d,d")
                        (match_operand:SI 2 "arith_operand" "I,L,N,d"))
                       (const_int 0)))
   (clobber (match_scratch:SI 0 "=d,d,d,d"))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"addis.c %0, %U2\";
    case 1: return mdp_select_add_imm (operands, true);
    case 2: return \"addri.c %0, %1, %c2\";
    case 3: return mdp_select (operands, "add", true, "", true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "*addsi3_ucc"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (plus:SI
                        (match_operand:SI 1 "register_operand" "0,0,d,d")
                        (match_operand:SI 2 "arith_operand" "I,L,N,d"))
                       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=d,d,d,d")
        (plus:SI (match_dup 1) (match_dup 2)))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"addis.c %0, %U2\";
    case 1: return mdp_select_add_imm (operands, true);
    case 2: return \"addri.c %0, %1, %c2\";
    case 3: return mdp_select (operands, "add", true, "", true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "adddi3"
  [(set (match_operand:DI 0 "register_operand" "=e,d")
        (plus:DI (match_operand:DI 1 "register_operand" "0,d")
                 (match_operand:DI 2 "register_operand" "e,d")))
  (clobber (reg:CC CC_REGNUM))]
  ""
  "@
   add!    %L0, %L2\;addc!   %H0, %H2
   add.c   %L0, %L1, %L2\;addc    %H0, %H1, %H2"
  [(set_attr "type" "arith")
   (set_attr "mode" "DI")])

(define_insn "subsi3"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (minus:SI (match_operand:SI 1 "register_operand" "d")
                  (match_operand:SI 2 "register_operand" "d")))]
  ""
{
  return mdp_select (operands, "sub", false, "", false);
}
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "*subsi3_cmp"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (minus:SI (match_operand:SI 1 "register_operand" "d")
                                 (match_operand:SI 2 "register_operand" "d"))
                       (const_int 0)))
   (clobber (match_scratch:SI 0 "=d"))]
  ""
{
  return mdp_select (operands, "sub", false, "", true);
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_peephole2
  [(set (match_operand:SI 0 "g32reg_operand" "")
        (minus:SI (match_operand:SI 1 "g32reg_operand" "")
                  (match_operand:SI 2 "g32reg_operand" "")))
   (set (reg:CC CC_REGNUM)
        (compare:CC (match_dup 1) (match_dup 2)))]
  ""
  [(set (reg:CC CC_REGNUM)
        (compare:CC (match_dup 1) (match_dup 2)))
   (set (match_dup 0)
        (minus:SI (match_dup 1) (match_dup 2)))])

(define_insn "subsi3_ucc_pcmp"
  [(parallel
       [(set (reg:CC CC_REGNUM)
             (compare:CC (match_operand:SI 1 "register_operand" "d")
                         (match_operand:SI 2 "register_operand" "d")))
        (set (match_operand:SI 0 "register_operand" "=d")
             (minus:SI (match_dup 1) (match_dup 2)))])]
  ""
{
  return mdp_select (operands, "sub", false, "", true);
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "subsi3_ucc"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (minus:SI (match_operand:SI 1 "register_operand" "d")
                                 (match_operand:SI 2 "register_operand" "d"))
                       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=d")
        (minus:SI (match_dup 1) (match_dup 2)))]
  ""
{
  return mdp_select (operands, "sub", false, "", true);
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "subdi3"
  [(set (match_operand:DI 0 "register_operand" "=e,d")
        (minus:DI (match_operand:DI 1 "register_operand" "0,d")
                  (match_operand:DI 2 "register_operand" "e,d")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "@
   sub!    %L0, %L2\;subc    %H0, %H1, %H2
   sub.c   %L0, %L1, %L2\;subc    %H0, %H1, %H2"
  [(set_attr "type" "arith")
   (set_attr "mode" "DI")])

(define_insn "andsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,d,d,d")
        (and:SI (match_operand:SI 1 "register_operand" "0,0,d,d")
                (match_operand:SI 2 "arith_operand" "I,K,M,d")))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"andis %0, %U2\";
    case 1: return \"andi  %0, %c2";
    case 2: return \"andri %0, %1, %c2\";
    case 3: return mdp_select (operands, "and", true, "", false);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "andsi3_cmp"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (and:SI (match_operand:SI 1 "register_operand" "0,0,0,d")
                               (match_operand:SI 2 "arith_operand" "I,K,M,d"))
                       (const_int 0)))
   (clobber (match_scratch:SI 0 "=d,d,d,d"))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"andis.c %0, %U2\";
    case 1: return \"andi.c  %0, %c2";
    case 2: return \"andri.c %0, %1, %c2\";
    case 3: return mdp_select (operands, "and", true, "", true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "*andsi3_ucc"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (and:SI
                        (match_operand:SI 1 "register_operand" "0,0,d,d")
                        (match_operand:SI 2 "arith_operand" "I,K,M,d"))
                       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=d,d,d,d")
        (and:SI (match_dup 1) (match_dup 2)))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"andis.c %0, %U2\";
    case 1: return \"andi.c  %0, %c2";
    case 2: return \"andri.c %0, %1, %c2\";
    case 3: return mdp_select (operands, "and", true, "", true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn_and_split "*zero_extract_andi"
  [(set (reg:CC CC_REGNUM)
        (compare:CC (zero_extract:SI
                     (match_operand:SI 0 "register_operand" "d")
                     (match_operand:SI 1 "const_uimm5" "")
                     (match_operand:SI 2 "const_uimm5" ""))
                    (const_int 0)))]
  ""
  "#"
  ""
  [(const_int 1)]
{
  mds_zero_extract_andi (operands);
  DONE;
})

(define_insn "iorsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,d,d,d")
        (ior:SI (match_operand:SI 1 "register_operand" "0,0,d,d")
                (match_operand:SI 2 "arith_operand" "I,K,M,d")))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"oris %0, %U2\";
    case 1: return \"ori  %0, %c2\";
    case 2: return \"orri %0, %1, %c2\";
    case 3: return mdp_select (operands, "or", true, "", false);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "iorsi3_ucc"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (ior:SI
                        (match_operand:SI 1 "register_operand" "0,0,d,d")
                        (match_operand:SI 2 "arith_operand" "I,K,M,d"))
                       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=d,d,d,d")
        (ior:SI (match_dup 1) (match_dup 2)))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"oris.c %0, %U2\";
    case 1: return \"ori.c  %0, %c2\";
    case 2: return \"orri.c %0, %1, %c2\";
    case 3: return mdp_select (operands, "or", true, "", true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "iorsi3_cmp"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (ior:SI
                        (match_operand:SI 1 "register_operand" "0,0,d,d")
                        (match_operand:SI 2 "arith_operand" "I,K,M,d"))
                       (const_int 0)))
   (clobber (match_scratch:SI 0 "=d,d,d,d"))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"oris.c %0, %U2\";
    case 1: return \"ori.c  %0, %c2\";
    case 2: return \"orri.c %0, %1, %c2\";
    case 3: return mdp_select (operands, "or", true, "", true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "xorsi3"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (xor:SI (match_operand:SI 1 "register_operand" "d")
                (match_operand:SI 2 "register_operand" "d")))]
  ""
{
  return mdp_select (operands, "xor", true, "", false);
}
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "xorsi3_ucc"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (xor:SI (match_operand:SI 1 "register_operand" "d")
                               (match_operand:SI 2 "register_operand" "d"))
                       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=d")
        (xor:SI (match_dup 1) (match_dup 2)))]
  ""
{
  return mdp_select (operands, "xor", true, "", true);
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "xorsi3_cmp"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (xor:SI (match_operand:SI 1 "register_operand" "d")
                               (match_operand:SI 2 "register_operand" "d"))
                       (const_int 0)))
   (clobber (match_scratch:SI 0 "=d"))]
  ""
{
  return mdp_select (operands, "xor", true, "", true);
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
        (sign_extend:SI (match_operand:QI 1 "nonimmediate_operand" "d,m")))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"extsb   %0, %1\";
    case 1: return mdp_linsn (operands, MDA_BYTE, true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith,load")
   (set_attr "mode" "SI")])

(define_insn "*extendqisi2_ucc"
  [(set (reg:CC_N CC_REGNUM)
        (compare:CC_N (ashiftrt:SI
                       (ashift:SI (match_operand:SI 1 "register_operand" "d")
                                  (const_int 24))
                       (const_int 24))
                      (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=d")
        (sign_extend:SI (match_operand:QI 2 "register_operand" "0")))]
  ""
  "extsb.c %0, %1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "*extendqisi2_cmp"
  [(set (reg:CC_N CC_REGNUM)
        (compare:CC_N (ashiftrt:SI
                       (ashift:SI (match_operand:SI 1 "register_operand" "d")
                                  (const_int 24))
                       (const_int 24))
                      (const_int 0)))
   (clobber (match_scratch:SI 0 "=d"))]
  ""
  "extsb.c %0, %1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
        (sign_extend:SI (match_operand:HI 1 "nonimmediate_operand" "d,m")))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"extsh   %0, %1\";
    case 1: return mdp_linsn (operands, MDA_HWORD, true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith, load")
   (set_attr "mode" "SI")])

(define_insn "*extendhisi2_ucc"
  [(set (reg:CC_N CC_REGNUM)
        (compare:CC_N (ashiftrt:SI
                       (ashift:SI (match_operand:SI 1 "register_operand" "d")
                                  (const_int 16))
                       (const_int 16))
                      (const_int 0)))
  (set (match_operand:SI 0 "register_operand" "=d")
       (sign_extend:SI (match_operand:HI 2 "register_operand" "0")))]
  ""
  "extsh.c %0, %1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "*extendhisi2_cmp"
  [(set (reg:CC_N CC_REGNUM)
        (compare:CC_N (ashiftrt:SI
                       (ashift:SI (match_operand:SI 1 "register_operand" "d")
                                  (const_int 16))
                       (const_int 16))
                      (const_int 0)))
   (clobber (match_scratch:SI 0 "=d"))]
  ""
  "extsh.c %0, %1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "zero_extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
        (zero_extend:SI (match_operand:QI 1 "nonimmediate_operand" "d,m")))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"extzb   %0, %1\";
    case 1: return mdp_linsn (operands, MDA_BYTE, false);
    default: gcc_unreachable ();
    }
  }
  [(set_attr "type" "arith, load")
   (set_attr "mode" "SI")])

(define_insn "*zero_extendqisi2_ucc"
  [(set (reg:CC_N CC_REGNUM)
        (compare:CC_N (lshiftrt:SI
                       (ashift:SI (match_operand:SI 1 "register_operand" "d")
                                  (const_int 24))
                       (const_int 24))
                      (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=d")
        (zero_extend:SI (match_operand:QI 2 "register_operand" "0")))]
  ""
  "extzb.c %0, %1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "*zero_extendqisi2_cmp"
  [(set (reg:CC_N CC_REGNUM)
        (compare:CC_N (lshiftrt:SI
                       (ashift:SI (match_operand:SI 1 "register_operand" "d")
                                  (const_int 24))
                       (const_int 24))
                      (const_int 0)))
   (clobber (match_scratch:SI 0 "=d"))]
  ""
  "extzb.c %0, %1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "zero_extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
        (zero_extend:SI (match_operand:HI 1 "nonimmediate_operand" "d,m")))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"extzh   %0, %1\";
    case 1: return mdp_linsn (operands, MDA_HWORD, false);
    default: gcc_unreachable ();
    }
  }
  [(set_attr "type" "arith, load")
   (set_attr "mode" "SI")])

(define_insn "*zero_extendhisi2_ucc"
  [(set (reg:CC_N CC_REGNUM)
        (compare:CC_N (lshiftrt:SI
                       (ashift:SI (match_operand:SI 1 "register_operand" "d")
                                  (const_int 16))
                       (const_int 16))
                      (const_int 0)))
  (set (match_operand:SI 0 "register_operand" "=d")
       (zero_extend:SI (match_operand:HI 2 "register_operand" "0")))]
  ""
  "extzh.c %0, %1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "*zero_extendhisi2_cmp"
  [(set (reg:CC_N CC_REGNUM)
        (compare:CC_N (lshiftrt:SI
                       (ashift:SI (match_operand:SI 1 "register_operand" "d")
                                  (const_int 16))
                       (const_int 16))
                      (const_int 0)))
   (clobber (match_scratch:SI 0 "=d"))]
  ""
  "extzh.c %0, %1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "mulsi3"
  [(set (match_operand:SI 0 "register_operand" "=l")
        (mult:SI (match_operand:SI 1 "register_operand" "d")
                 (match_operand:SI 2 "register_operand" "d")))
  (clobber (reg:SI HI_REGNUM))]
  "!TARGET_SCORE5U"
  "mul     %1, %2"
  [(set_attr "type" "mul")
   (set_attr "mode" "SI")])

(define_insn "mulsidi3"
  [(set (match_operand:DI 0 "register_operand" "=x")
        (mult:DI (sign_extend:DI
                  (match_operand:SI 1 "register_operand" "d"))
                 (sign_extend:DI
                  (match_operand:SI 2 "register_operand" "d"))))]
  "!TARGET_SCORE5U"
  "mul     %1, %2"
  [(set_attr "type" "mul")
   (set_attr "mode" "DI")])

(define_insn "umulsidi3"
  [(set (match_operand:DI 0 "register_operand" "=x")
        (mult:DI (zero_extend:DI
                  (match_operand:SI 1 "register_operand" "d"))
                 (zero_extend:DI
                  (match_operand:SI 2 "register_operand" "d"))))]
  "!TARGET_SCORE5U"
  "mulu    %1, %2"
  [(set_attr "type" "mul")
   (set_attr "mode" "DI")])

(define_insn "divmodsi4"
  [(set (match_operand:SI 0 "register_operand" "=l")
        (div:SI (match_operand:SI 1 "register_operand" "d")
                (match_operand:SI 2 "register_operand" "d")))
   (set (match_operand:SI 3 "register_operand" "=h")
        (mod:SI (match_dup 1) (match_dup 2)))]
  "!TARGET_SCORE5U"
  "div     %1, %2"
  [(set_attr "type" "div")
   (set_attr "mode" "SI")])

(define_insn "udivmodsi4"
  [(set (match_operand:SI 0 "register_operand" "=l")
        (udiv:SI (match_operand:SI 1 "register_operand" "d")
                 (match_operand:SI 2 "register_operand" "d")))
   (set (match_operand:SI 3 "register_operand" "=h")
        (umod:SI (match_dup 1) (match_dup 2)))]
  "!TARGET_SCORE5U"
  "divu    %1, %2"
  [(set_attr "type" "div")
   (set_attr "mode" "SI")])

(define_insn "ashlsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
        (ashift:SI (match_operand:SI 1 "register_operand" "d,d")
                   (match_operand:SI 2 "arith_operand" "J,d")))]
  ""
  "@
   slli    %0, %1, %c2
   sll     %0, %1, %2"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "ashlsi3_ucc"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (ashift:SI
                        (match_operand:SI 1 "register_operand" "d,d")
                        (match_operand:SI 2 "arith_operand" "J,d"))
                       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=d,d")
        (ashift:SI (match_dup 1) (match_dup 2)))]
  ""
{
  switch (which_alternative)
    {
    case 0: return mdp_select (operands, "slli", false, "c", true);
    case 1: return mdp_select (operands, "sll", false, "", true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "ashlsi3_cmp"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (ashift:SI
                        (match_operand:SI 1 "register_operand" "d,d")
                        (match_operand:SI 2 "arith_operand" "J,d"))
                       (const_int 0)))
   (clobber (match_scratch:SI 0 "=d,d"))]
  ""
{
  switch (which_alternative)
    {
    case 0: return mdp_select (operands, "slli", false, "c", true);
    case 1: return mdp_select (operands, "sll", false, "", true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "ashrsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
        (ashiftrt:SI (match_operand:SI 1 "register_operand" "d,d")
                     (match_operand:SI 2 "arith_operand" "J,d")))]
  ""
  "@
   srai    %0, %1, %c2
   sra     %0, %1, %2"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "ashrsi3_ucc"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (ashiftrt:SI
                        (match_operand:SI 1 "register_operand" "d,d")
                        (match_operand:SI 2 "arith_operand" "J,d"))
                       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=d,d")
        (ashiftrt:SI (match_dup 1) (match_dup 2)))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"srai.c  %0, %1, %c2\";
    case 1: return mdp_select (operands, "sra", false, "", true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "ashrsi3_cmp"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (ashiftrt:SI
                        (match_operand:SI 1 "register_operand" "d,d")
                        (match_operand:SI 2 "arith_operand" "J,d"))
                       (const_int 0)))
   (clobber (match_scratch:SI 0 "=d,d"))]
  ""
{
  switch (which_alternative)
    {
    case 0: return \"srai.c  %0, %1, %c2\";
    case 1: return mdp_select (operands, "sra", false, "", true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
        (lshiftrt:SI (match_operand:SI 1 "register_operand" "d,d")
                     (match_operand:SI 2 "arith_operand" "J,d")))]
  ""
  "@
   srli    %0, %1, %c2
   srl     %0, %1, %2"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "lshrsi3_ucc"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (lshiftrt:SI
                        (match_operand:SI 1 "register_operand" "d,d")
                        (match_operand:SI 2 "arith_operand" "J,d"))
                       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=d,d")
        (lshiftrt:SI (match_dup 1) (match_dup 2)))]
  ""
{
  switch (which_alternative)
    {
    case 0: return mdp_select (operands, "srli", false, "c", true);
    case 1: return mdp_select (operands, "srl", false, "", true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "lshrsi3_cmp"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (lshiftrt:SI
                        (match_operand:SI 1 "register_operand" "d,d")
                        (match_operand:SI 2 "arith_operand" "J,d"))
                       (const_int 0)))
   (clobber (match_scratch:SI 0 "=d,d"))]
  ""
{
  switch (which_alternative)
    {
    case 0: return mdp_select (operands, "srli", false, "c", true);
    case 1: return mdp_select (operands, "srl", false, "", true);
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "negsi2"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (neg:SI (match_operand:SI 1 "register_operand" "d")))]
  ""
  "neg     %0, %1"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "*negsi2_cmp"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (neg:SI (match_operand:SI 1 "register_operand" "e,d"))
                       (const_int 0)))
   (clobber (match_scratch:SI 0 "=e,d"))]
  ""
  "@
   neg!    %0, %1
   neg.c   %0, %1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "negsi2_ucc"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (neg:SI (match_operand:SI 1 "register_operand" "e,d"))
                       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=e,d")
        (neg:SI (match_dup 1)))]
  ""
  "@
   neg!    %0, %1
   neg.c   %0, %1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "one_cmplsi2"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (not:SI (match_operand:SI 1 "register_operand" "d")))]
  ""
  "not     %0, %1"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "one_cmplsi2_ucc"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (not:SI (match_operand:SI 1 "register_operand" "e,d"))
                       (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=e,d")
        (not:SI (match_dup 1)))]
  ""
  "@
   not!    %0, %1
   not.c   %0, %1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "one_cmplsi2_cmp"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (not:SI (match_operand:SI 1 "register_operand" "e,d"))
                       (const_int 0)))
   (clobber (match_scratch:SI 0 "=e,d"))]
  ""
  "@
   not!    %0, %1
   not.c   %0, %1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_insn "rotlsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
        (rotate:SI (match_operand:SI 1 "register_operand" "d,d")
                   (match_operand:SI 2 "arith_operand" "J,d")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "@
   roli.c  %0, %1, %c2
   rol.c   %0, %1, %2"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "rotrsi3"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
        (rotatert:SI (match_operand:SI 1 "register_operand" "d,d")
                     (match_operand:SI 2 "arith_operand" "J,d")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "@
   rori.c  %0, %1, %c2
   ror.c   %0, %1, %2"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_expand "cmpsi"
  [(match_operand:SI 0 "register_operand" "")
   (match_operand:SI 1 "arith_operand" "")]
  ""
{
  cmp_op0 = operands[0];
  cmp_op1 = operands[1];
  DONE;
})

(define_insn "cmpsi_nz"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (match_operand:SI 0 "register_operand" "d,e,d")
                       (match_operand:SI 1 "arith_operand" "L,e,d")))]
  ""
  "@
   cmpi.c  %0, %c1
   cmp!    %0, %1
   cmp.c   %0, %1"
   [(set_attr "type" "cmp")
    (set_attr "up_c" "yes")
    (set_attr "mode" "SI")])

(define_insn "cmpsi_n"
  [(set (reg:CC_N CC_REGNUM)
        (compare:CC_N (match_operand:SI 0 "register_operand" "d,e,d")
                      (match_operand:SI 1 "arith_operand" "L,e,d")))]
  ""
  "@
   cmpi.c  %0, %c1
   cmp!    %0, %1
   cmp.c   %0, %1"
   [(set_attr "type" "cmp")
    (set_attr "up_c" "yes")
    (set_attr "mode" "SI")])

(define_insn "*cmpsi_to_addsi"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (match_operand:SI 1 "register_operand" "0,d")
                       (neg:SI (match_operand:SI 2 "register_operand" "e,d"))))
   (clobber (match_scratch:SI 0 "=e,d"))]
  ""
  "@
   add!    %0, %2
   add.c   %0, %1, %2"
   [(set_attr "type" "cmp")
    (set_attr "up_c" "yes")
    (set_attr "mode" "SI")])

(define_insn "cmpsi_cc"
  [(set (reg:CC CC_REGNUM)
        (compare:CC (match_operand:SI 0 "register_operand" "d,e,d")
                    (match_operand:SI 1 "arith_operand" "L,e,d")))]
  ""
  "@
   cmpi.c  %0, %c1
   cmp!    %0, %1
   cmp.c   %0, %1"
  [(set_attr "type" "cmp")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_expand "beq"
  [(set (pc)
        (if_then_else (eq (reg:CC CC_REGNUM) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
{
  mda_gen_cmp (CCmode);
})

(define_expand "bne"
  [(set (pc)
        (if_then_else (ne (reg:CC CC_REGNUM) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
{
  mda_gen_cmp (CCmode);
})

(define_expand "bgt"
  [(set (pc)
        (if_then_else (gt (reg:CC CC_REGNUM) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
{
  mda_gen_cmp (CCmode);
})

(define_expand "ble"
  [(set (pc)
        (if_then_else (le (reg:CC CC_REGNUM) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
{
  mda_gen_cmp (CCmode);
})

(define_expand "bge"
  [(set (pc)
        (if_then_else (ge (reg:CC CC_REGNUM) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
{
  mda_gen_cmp (CCmode);
})

(define_expand "blt"
  [(set (pc)
        (if_then_else (lt (reg:CC CC_REGNUM) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
{
  mda_gen_cmp (CCmode);
})

(define_expand "bgtu"
  [(set (pc)
        (if_then_else (gtu (reg:CC CC_REGNUM) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
{
  mda_gen_cmp (CCmode);
})

(define_expand "bleu"
  [(set (pc)
        (if_then_else (leu (reg:CC CC_REGNUM) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
{
  mda_gen_cmp (CCmode);
})

(define_expand "bgeu"
  [(set (pc)
        (if_then_else (geu (reg:CC CC_REGNUM) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
{
  mda_gen_cmp (CCmode);
})

(define_expand "bltu"
  [(set (pc)
        (if_then_else (ltu (reg:CC CC_REGNUM) (const_int 0))
                      (label_ref (match_operand 0 "" ""))
                      (pc)))]
  ""
{
  mda_gen_cmp (CCmode);
})

(define_insn "branch_n"
  [(set (pc)
        (if_then_else
         (match_operator 0 "branch_n_operator"
                         [(reg:CC_N CC_REGNUM)
                          (const_int 0)])
         (label_ref (match_operand 1 "" ""))
         (pc)))]
  ""
  "b%C0    %1"
  [(set_attr "type" "branch")])

(define_insn "branch_nz"
  [(set (pc)
        (if_then_else
         (match_operator 0 "branch_nz_operator"
                         [(reg:CC_NZ CC_REGNUM)
                          (const_int 0)])
         (label_ref (match_operand 1 "" ""))
         (pc)))]
  ""
  "b%C0    %1"
  [(set_attr "type" "branch")])

(define_insn "branch_cc"
  [(set (pc)
        (if_then_else
         (match_operator 0 "comparison_operator"
                         [(reg:CC CC_REGNUM)
                          (const_int 0)])
         (label_ref (match_operand 1 "" ""))
         (pc)))]
  ""
  "b%C0    %1"
  [(set_attr "type" "branch")])

(define_insn "jump"
  [(set (pc)
        (label_ref (match_operand 0 "" "")))]
  ""
{
  if (!flag_pic)
    return \"j      %0\";
  else
    return \"b      %0\";
}
  [(set_attr "type" "jump")])

(define_expand "sibcall"
  [(parallel [(call (match_operand 0 "" "")
                    (match_operand 1 "" ""))
              (use (match_operand 2 "" ""))])]
  ""
{
  mdx_call (operands, true);
  DONE;
})

(define_insn "sibcall_internal"
  [(call (mem:SI (match_operand:SI 0 "call_insn_operand" "t,Z"))
         (match_operand 1 "" ""))
   (clobber (reg:SI RT_REGNUM))]
  "SIBLING_CALL_P (insn)"
{
  if (!flag_pic)
    switch (which_alternative)
      {
      case 0: return \"br%S0   %0\";
      case 1: return \"j       %0\";
      default: gcc_unreachable ();
      }
  else
    switch (which_alternative)
      {
      case 0: return \"mv      r29, %0\;br      r29\";
      case 1: return \"la      r29, %0\;br      r29\";
      default: gcc_unreachable ();
      }
}
  [(set_attr "type" "call")])

(define_expand "sibcall_value"
  [(parallel [(set (match_operand 0 "" "")
              (call (match_operand 1 "" "") (match_operand 2 "" "")))
              (use (match_operand 3 "" ""))])]
  ""
{
  mdx_call_value (operands, true);
  DONE;
})

(define_insn "sibcall_value_internal"
  [(set (match_operand 0 "register_operand" "=d,d")
        (call (mem:SI (match_operand:SI 1 "call_insn_operand" "t,Z"))
              (match_operand 2 "" "")))
   (clobber (reg:SI RT_REGNUM))]
  "SIBLING_CALL_P (insn)"
{
  if (!flag_pic)
    switch (which_alternative)
      {
      case 0: return \"br%S1   %1\";
      case 1: return \"j       %1\";
      default: gcc_unreachable ();
      }
  else
    switch (which_alternative)
      {
      case 0: return \"mv      r29, %1\;br      r29\";
      case 1: return \"la      r29, %1\;br      r29\";
      default: gcc_unreachable ();
      }
}
  [(set_attr "type" "call")])

(define_expand "call"
  [(parallel [(call (match_operand 0 "" "") (match_operand 1 "" ""))
              (use (match_operand 2 "" ""))])]
  ""
{
  mdx_call (operands, false);
  DONE;
})

(define_insn "call_internal"
  [(call (mem:SI (match_operand:SI 0 "call_insn_operand" "d,Z"))
         (match_operand 1 "" ""))
   (clobber (reg:SI RA_REGNUM))]
  ""
{
  if (!flag_pic)
    switch (which_alternative)
      {
      case 0: return \"brl%S0  %0\";
      case 1: return \"jl      %0\";
      default: gcc_unreachable ();
      }
  else
     switch (which_alternative)
      {
      case 0: return \"mv      r29, %0\;brl     r29\";
      case 1: return \"la      r29, %0\;brl     r29\";
      default: gcc_unreachable ();
      }
}
  [(set_attr "type" "call")])

(define_expand "call_value"
  [(parallel [(set (match_operand 0 "" "")
                   (call (match_operand 1 "" "") (match_operand 2 "" "")))
              (use (match_operand 3 "" ""))])]
  ""
{
  mdx_call_value (operands, false);
  DONE;
})

(define_insn "call_value_internal"
  [(set (match_operand 0 "register_operand" "=d,d")
        (call (mem:SI (match_operand:SI 1 "call_insn_operand" "d,Z"))
              (match_operand 2 "" "")))
   (clobber (reg:SI RA_REGNUM))]
  ""
{
  if (!flag_pic)
    switch (which_alternative)
      {
      case 0: return \"brl%S1  %1\";
      case 1: return \"jl      %1\";
      default: gcc_unreachable ();
      }
  else
    switch (which_alternative)
      {
      case 0: return \"mv      r29, %1\;brl     r29\";
      case 1: return \"la      r29, %1\;brl     r29\";
      default: gcc_unreachable ();
      }
}
  [(set_attr "type" "call")])

(define_expand "indirect_jump"
  [(set (pc) (match_operand 0 "register_operand" "d"))]
  ""
{
  rtx dest;
  dest = operands[0];
  if (GET_CODE (dest) != REG
      || GET_MODE (dest) != Pmode)
    operands[0] = copy_to_mode_reg (Pmode, dest);

  emit_jump_insn (gen_indirect_jump_internal1 (operands[0]));
  DONE;
})

(define_insn "indirect_jump_internal1"
  [(set (pc) (match_operand:SI 0 "register_operand" "d"))]
  ""
  "br%S0   %0"
  [(set_attr "type" "jump")])

(define_expand "tablejump"
  [(set (pc)
        (match_operand 0 "register_operand" "d"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
{
  if (GET_MODE (operands[0]) != ptr_mode)
    gcc_unreachable ();
  emit_jump_insn (gen_tablejump_internal1 (operands[0], operands[1]));
  DONE;
})

(define_insn "tablejump_internal1"
  [(set (pc)
        (match_operand:SI 0 "register_operand" "d"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
  "*
   if (flag_pic)
     return \"mv      r29, %0\;.cpadd  r29\;br      r29\";
   else
     return \"br%S0   %0\";
  "
  [(set_attr "type" "jump")])

(define_expand "prologue"
  [(const_int 1)]
  ""
{
  mdx_prologue ();
  DONE;
})

(define_expand "epilogue"
  [(const_int 2)]
  ""
{
  mdx_epilogue (false);
  DONE;
})

(define_expand "sibcall_epilogue"
  [(const_int 2)]
  ""
{
  mdx_epilogue (true);
  DONE;
})

(define_insn "return_internal"
  [(return)
   (use (match_operand 0 "pmode_register_operand" "d"))]
  ""
  "br%S0   %0")

(define_insn "nop"
  [(const_int 0)]
  ""
  "#nop!"
)

(define_insn "cpload"
  [(unspec_volatile:SI [(const_int 1)] CPLOAD)]
  "flag_pic"
  ".cpload r29"
)

(define_insn "cprestore_use_fp"
  [(unspec_volatile:SI [(match_operand:SI 0 "" "")] CPRESTORE)
   (use (reg:SI FP_REGNUM))]
  "flag_pic"
  ".cprestore r2, %0"
)

(define_insn "cprestore_use_sp"
  [(unspec_volatile:SI [(match_operand:SI 0 "" "")] CPRESTORE)
   (use (reg:SI SP_REGNUM))]
  "flag_pic"
  ".cprestore r0, %0"
)

(define_expand "doloop_end"
  [(use (match_operand 0 "" ""))    ; loop pseudo
   (use (match_operand 1 "" ""))    ; iterations; zero if unknown
   (use (match_operand 2 "" ""))    ; max iterations
   (use (match_operand 3 "" ""))    ; loop level
   (use (match_operand 4 "" ""))]   ; label
  "!TARGET_NHWLOOP"
  {
    if (INTVAL (operands[3]) > 1)
      FAIL;

    if (GET_MODE (operands[0]) == SImode)
      {
        rtx sr0 = gen_rtx_REG (SImode, CN_REGNUM);
        emit_jump_insn (gen_doloop_end_si (sr0, operands[4]));
      }
    else
      FAIL;

    DONE;
  })

(define_insn "doloop_end_si"
  [(set (pc)
        (if_then_else
         (ne (match_operand:SI 0 "sr0_operand" "")
             (const_int 0))
         (label_ref (match_operand 1 "" ""))
         (pc)))
   (set (match_dup 0)
        (plus:SI (match_dup 0)
                 (const_int -1)))
   (clobber (reg:CC CC_REGNUM))
]
  "!TARGET_NHWLOOP"
  "bcnz %1"
  [(set_attr "type" "branch")])

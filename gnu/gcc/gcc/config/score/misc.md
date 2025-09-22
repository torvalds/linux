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

(define_insn "pushsi"
  [(set (match_operand:SI 0 "push_operand" "=<")
        (match_operand:SI 1 "register_operand" "d"))]
  ""
  "push!   %1, [r0]"
  [(set_attr "type" "store")
   (set_attr "mode" "SI")])

(define_insn "popsi"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (match_operand:SI 1 "pop_operand" ">"))]
  ""
  "pop!    %0, [r0]"
  [(set_attr "type" "store")
   (set_attr "mode" "SI")])

(define_peephole2
  [(set (match_operand:SI 0 "g32reg_operand" "")
        (match_operand:SI 1 "loreg_operand" ""))
   (set (match_operand:SI 2 "g32reg_operand" "")
        (match_operand:SI 3 "hireg_operand" ""))]
  ""
  [(parallel
       [(set (match_dup 0) (match_dup 1))
        (set (match_dup 2) (match_dup 3))])])

(define_peephole2
  [(set (match_operand:SI 0 "g32reg_operand" "")
        (match_operand:SI 1 "hireg_operand" ""))
   (set (match_operand:SI 2 "g32reg_operand" "")
        (match_operand:SI 3 "loreg_operand" ""))]
  ""
  [(parallel
       [(set (match_dup 2) (match_dup 3))
        (set (match_dup 0) (match_dup 1))])])

(define_insn "movhilo"
  [(parallel
       [(set (match_operand:SI 0 "register_operand" "=d")
             (match_operand:SI 1 "loreg_operand" ""))
        (set (match_operand:SI 2 "register_operand" "=d")
             (match_operand:SI 3 "hireg_operand" ""))])]
  ""
  "mfcehl  %2, %0"
  [(set_attr "type" "fce")
   (set_attr "mode" "SI")])

(define_expand "movsicc"
  [(set (match_operand:SI 0 "register_operand" "")
        (if_then_else:SI (match_operator 1 "comparison_operator"
                          [(reg:CC CC_REGNUM) (const_int 0)])
                         (match_operand:SI 2 "register_operand" "")
                         (match_operand:SI 3 "register_operand" "")))]
  ""
{
  mdx_movsicc (operands);
})

(define_insn "movsicc_internal"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (if_then_else:SI (match_operator 1 "comparison_operator"
                          [(reg:CC CC_REGNUM) (const_int 0)])
                         (match_operand:SI 2 "arith_operand" "d")
                         (match_operand:SI 3 "arith_operand" "0")))]
  ""
  "mv%C1   %0, %2"
  [(set_attr "type" "cndmv")
   (set_attr "mode" "SI")])

(define_insn "zero_extract_bittst"
  [(set (reg:CC_NZ CC_REGNUM)
        (compare:CC_NZ (unspec:SI
                        [(match_operand:SI 0 "register_operand" "*e,d")
                         (match_operand:SI 1 "const_uimm5" "")]
                        BITTST)
                       (const_int 0)))]
  ""
  "@
   bittst!  %0, %c1
   bittst.c %0, %c1"
  [(set_attr "type" "arith")
   (set_attr "up_c" "yes")
   (set_attr "mode" "SI")])

(define_expand "extzv"
  [(set (match_operand:SI 0 "register_operand" "")
        (zero_extract (match_operand:SI 1 "memory_operand" "")
                      (match_operand:SI 2 "immediate_operand" "")
                      (match_operand:SI 3 "immediate_operand" "")))]
  "!TARGET_SCORE5U && !TARGET_LITTLE_ENDIAN && TARGET_ULS"
{
  if (mdx_unaligned_load (operands))
    DONE;
  else
    FAIL;
})

(define_expand "insv"
  [(set (zero_extract (match_operand:SI 0 "memory_operand" "")
                      (match_operand:SI 1 "immediate_operand" "")
                      (match_operand:SI 2 "immediate_operand" ""))
        (match_operand:SI 3 "register_operand" ""))]
  "!TARGET_SCORE5U && !TARGET_LITTLE_ENDIAN && TARGET_ULS"
{
  if (mdx_unaligned_store (operands))
    DONE;
  else
    FAIL;
})

(define_expand "extv"
  [(set (match_operand:SI 0 "register_operand" "")
        (sign_extract (match_operand:SI 1 "memory_operand" "")
                      (match_operand:SI 2 "immediate_operand" "")
                      (match_operand:SI 3 "immediate_operand" "")))]
  "!TARGET_SCORE5U && !TARGET_LITTLE_ENDIAN && TARGET_ULS"
{
  if (mdx_unaligned_load (operands))
    DONE;
  else
    FAIL;
})

(define_expand "movmemsi"
  [(parallel [(set (match_operand:BLK 0 "general_operand")
                   (match_operand:BLK 1 "general_operand"))
              (use (match_operand:SI 2 ""))
              (use (match_operand:SI 3 "const_int_operand"))])]
  "!TARGET_SCORE5U && TARGET_ULS"
{
  if (mdx_block_move (operands))
    DONE;
  else
    FAIL;
})

(define_insn "move_lbu_a"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (match_operand:SI 2 "const_simm12" "")))
   (set (match_operand:QI 3 "register_operand" "=d")
        (mem:QI (match_dup 1)))]
  ""
  "lbu     %3, [%1]+, %2"
  [(set_attr "type" "load")
   (set_attr "mode" "QI")])

(define_insn "move_lhu_a"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (match_operand:SI 2 "const_simm12" "")))
   (set (match_operand:HI 3 "register_operand" "=d")
        (mem:HI (match_dup 1)))]
  ""
  "lhu     %3, [%1]+, %2"
  [(set_attr "type" "load")
   (set_attr "mode" "HI")])

(define_insn "move_lw_a"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (match_operand:SI 2 "const_simm12" "")))
   (set (match_operand:SI 3 "register_operand" "=d")
        (mem:SI (match_dup 1)))]
  ""
  "lw      %3, [%1]+, %2"
  [(set_attr "type" "load")
   (set_attr "mode" "SI")])

(define_insn "move_sb_a"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (match_operand:SI 2 "const_simm12" "")))
   (set (mem:QI (match_dup 1))
        (match_operand:QI 3 "register_operand" "d"))]
  ""
  "sb      %3, [%1]+, %2"
  [(set_attr "type" "store")
   (set_attr "mode" "QI")])

(define_insn "move_sh_a"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (match_operand:SI 2 "const_simm12" "")))
   (set (mem:HI (match_dup 1))
        (match_operand:HI 3 "register_operand" "d"))]
  ""
  "sh      %3, [%1]+, %2"
  [(set_attr "type" "store")
   (set_attr "mode" "HI")])

(define_insn "move_sw_a"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (match_operand:SI 2 "const_simm12" "")))
   (set (mem:SI (match_dup 1))
        (match_operand:SI 3 "register_operand" "d"))]
  ""
  "sw      %3, [%1]+, %2"
  [(set_attr "type" "store")
   (set_attr "mode" "SI")])

(define_insn "move_lbu_b"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (match_operand:SI 2 "const_simm12" "")))
   (set (match_operand:QI 3 "register_operand" "=d")
        (mem:QI (plus:SI (match_dup 1)
                         (match_dup 2))))]
  ""
  "lbu     %3, [%1, %2]+"
  [(set_attr "type" "load")
   (set_attr "mode" "QI")])

(define_insn "move_lhu_b"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (match_operand:SI 2 "const_simm12" "")))
   (set (match_operand:HI 3 "register_operand" "=d")
        (mem:HI (plus:SI (match_dup 1)
                         (match_dup 2))))]
  ""
  "lhu     %3, [%1, %2]+"
  [(set_attr "type" "load")
   (set_attr "mode" "HI")])

(define_insn "move_lw_b"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (match_operand:SI 2 "const_simm12" "")))
   (set (match_operand:SI 3 "register_operand" "=d")
        (mem:SI (plus:SI (match_dup 1)
                         (match_dup 2))))]
  ""
  "lw      %3, [%1, %2]+"
  [(set_attr "type" "load")
   (set_attr "mode" "SI")])

(define_insn "move_sb_b"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (match_operand:SI 2 "const_simm12" "")))
   (set (mem:QI (plus:SI (match_dup 1)
                         (match_dup 2)))
        (match_operand:QI 3 "register_operand" "d"))]
  ""
  "sb      %3, [%1, %2]+"
  [(set_attr "type" "store")
   (set_attr "mode" "QI")])

(define_insn "move_sh_b"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (match_operand:SI 2 "const_simm12" "")))
   (set (mem:HI (plus:SI (match_dup 1)
                         (match_dup 2)))
        (match_operand:HI 3 "register_operand" "d"))]
  ""
  "sh      %3, [%1, %2]+"
  [(set_attr "type" "store")
   (set_attr "mode" "HI")])

(define_insn "move_sw_b"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (match_operand:SI 2 "const_simm12" "")))
   (set (mem:SI (plus:SI (match_dup 1)
                         (match_dup 2)))
        (match_operand:SI 3 "register_operand" "d"))]
  ""
  "sw      %3, [%1, %2]+"
  [(set_attr "type" "store")
   (set_attr "mode" "SI")])

(define_insn "move_lcb"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (const_int 4)))
   (set (reg:SI LC_REGNUM)
        (unspec:SI [(mem:BLK (match_dup 1))] LCB))]
  "!TARGET_SCORE5U && !TARGET_LITTLE_ENDIAN && TARGET_ULS"
  "lcb     [%1]+"
  [(set_attr "type" "load")
   (set_attr "mode" "SI")])

(define_insn "move_lcw"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (const_int 4)))
   (set (match_operand:SI 2 "register_operand" "=d")
        (unspec:SI [(mem:BLK (match_dup 1))
                    (reg:SI LC_REGNUM)] LCW))
   (set (reg:SI LC_REGNUM)
        (unspec:SI [(mem:BLK (match_dup 1))] LCB))]
  "!TARGET_SCORE5U && !TARGET_LITTLE_ENDIAN && TARGET_ULS"
  "lcw     %2, [%1]+"
  [(set_attr "type" "load")
   (set_attr "mode" "SI")])

(define_insn "move_lce"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (const_int 4)))
   (set (match_operand:SI 2 "register_operand" "=d")
        (unspec:SI [(mem:BLK (match_dup 1))
                    (reg:SI LC_REGNUM)] LCE))]
  "!TARGET_SCORE5U && !TARGET_LITTLE_ENDIAN && TARGET_ULS"
  "lce     %2, [%1]+"
  [(set_attr "type" "load")
   (set_attr "mode" "SI")])

(define_insn "move_scb"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (const_int 4)))
   (set (mem:BLK (match_dup 1))
        (unspec:BLK [(match_operand:SI 2 "register_operand" "d")] SCB))
   (set (reg:SI SC_REGNUM)
        (unspec:SI [(match_dup 2)] SCLC))]
  "!TARGET_SCORE5U && !TARGET_LITTLE_ENDIAN && TARGET_ULS"
  "scb     %2, [%1]+"
  [(set_attr "type" "store")
   (set_attr "mode" "SI")])

(define_insn "move_scw"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (const_int 4)))
   (set (mem:BLK (match_dup 1))
        (unspec:BLK [(match_operand:SI 2 "register_operand" "d")
                     (reg:SI SC_REGNUM)] SCW))
   (set (reg:SI SC_REGNUM)
        (unspec:SI [(match_dup 2)] SCLC))]
  "!TARGET_SCORE5U && !TARGET_LITTLE_ENDIAN && TARGET_ULS"
  "scw     %2, [%1]+"
  [(set_attr "type" "store")
   (set_attr "mode" "SI")])

(define_insn "move_sce"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (plus:SI (match_operand:SI 1 "register_operand" "0")
                 (const_int 4)))
   (set (mem:BLK (match_dup 1))
        (unspec:BLK [(reg:SI SC_REGNUM)] SCE))]
  "!TARGET_SCORE5U && !TARGET_LITTLE_ENDIAN && TARGET_ULS"
  "sce     [%1]+"
  [(set_attr "type" "store")
   (set_attr "mode" "SI")])

(define_insn "andsi3_extzh"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (and:SI (match_operand:SI 1 "register_operand" "d")
                (const_int 65535)))]
  ""
  "extzh   %0, %1"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])


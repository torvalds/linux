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

(define_insn "smaxsi3"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (smax:SI (match_operand:SI 1 "register_operand" "d")
                 (match_operand:SI 2 "register_operand" "d")))]
  "TARGET_MAC || TARGET_SCORE7D"
  "max     %0, %1, %2"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "sminsi3"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (smin:SI (match_operand:SI 1 "register_operand" "d")
                 (match_operand:SI 2 "register_operand" "d")))]
  "TARGET_MAC || TARGET_SCORE7D"
  "min     %0, %1, %2"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "abssi2"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (abs:SI (match_operand:SI 1 "register_operand" "d")))]
  "TARGET_MAC || TARGET_SCORE7D"
  "abs     %0, %1"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "clzsi2"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (clz:SI (match_operand:SI 1 "register_operand" "d")))]
  "TARGET_MAC || TARGET_SCORE7D"
  "clz     %0, %1"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_insn "sffs"
  [(set (match_operand:SI 0 "register_operand" "=d")
        (unspec:SI [(match_operand:SI 1 "register_operand" "d")] SFFS))]
  "TARGET_MAC || TARGET_SCORE7D"
  "bitrev  %0, %1, r0\;clz     %0, %0\;addi    %0, 0x1"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

(define_expand "ffssi2"
  [(set (match_operand:SI 0 "register_operand")
        (ffs:SI (match_operand:SI 1 "register_operand")))]
  "TARGET_MAC || TARGET_SCORE7D"
{
  emit_insn (gen_sffs (operands[0], operands[1]));
  emit_insn (gen_rtx_SET (VOIDmode, gen_rtx_REG (CC_NZmode, CC_REGNUM),
                          gen_rtx_COMPARE (CC_NZmode, operands[0],
                                           GEN_INT (33))));
  emit_insn (gen_movsicc_internal (operands[0],
             gen_rtx_fmt_ee (EQ, VOIDmode, operands[0], GEN_INT (33)),
             GEN_INT (0),
             operands[0]));
  DONE;
})

(define_peephole2
  [(set (match_operand:SI 0 "loreg_operand" "")
        (match_operand:SI 1 "register_operand" ""))
   (set (match_operand:SI 2 "hireg_operand" "")
        (match_operand:SI 3 "register_operand" ""))]
  "TARGET_MAC || TARGET_SCORE7D"
  [(parallel
       [(set (match_dup 0) (match_dup 1))
        (set (match_dup 2) (match_dup 3))])])

(define_peephole2
  [(set (match_operand:SI 0 "hireg_operand" "")
        (match_operand:SI 1 "register_operand" ""))
   (set (match_operand:SI 2 "loreg_operand" "")
        (match_operand:SI 3 "register_operand" ""))]
  "TARGET_MAC || TARGET_SCORE7D"
  [(parallel
       [(set (match_dup 2) (match_dup 3))
        (set (match_dup 0) (match_dup 1))])])

(define_insn "movtohilo"
  [(parallel
       [(set (match_operand:SI 0 "loreg_operand" "=l")
             (match_operand:SI 1 "register_operand" "d"))
        (set (match_operand:SI 2 "hireg_operand" "=h")
             (match_operand:SI 3 "register_operand" "d"))])]
  "TARGET_MAC || TARGET_SCORE7D"
  "mtcehl  %3, %1"
  [(set_attr "type" "fce")
   (set_attr "mode" "SI")])

(define_insn "mulsi3addsi"
  [(set (match_operand:SI 0 "register_operand" "=l,l,d")
        (plus:SI (mult:SI (match_operand:SI 2 "register_operand" "d,d,d")
                          (match_operand:SI 3 "register_operand" "d,d,d"))
                 (match_operand:SI 1 "register_operand" "0,d,l")))
   (clobber (reg:SI HI_REGNUM))]
  "TARGET_MAC || TARGET_SCORE7D"
  "@
   mad     %2, %3
   mtcel%S1 %1\;mad     %2, %3
   mad      %2, %3\;mfcel%S0 %0"
  [(set_attr "mode" "SI")])

(define_insn "mulsi3subsi"
  [(set (match_operand:SI 0 "register_operand" "=l,l,d")
        (minus:SI (match_operand:SI 1 "register_operand" "0,d,l")
                  (mult:SI (match_operand:SI 2 "register_operand" "d,d,d")
                           (match_operand:SI 3 "register_operand" "d,d,d"))))
   (clobber (reg:SI HI_REGNUM))]
  "TARGET_MAC || TARGET_SCORE7D"
  "@
   msb     %2, %3
   mtcel%S1 %1\;msb     %2, %3
   msb     %2, %3\;mfcel%S0 %0"
  [(set_attr "mode" "SI")])

(define_insn "mulsidi3adddi"
  [(set (match_operand:DI 0 "register_operand" "=x")
        (plus:DI (mult:DI
                  (sign_extend:DI (match_operand:SI 2 "register_operand" "%d"))
                  (sign_extend:DI (match_operand:SI 3 "register_operand" "d")))
                 (match_operand:DI 1 "register_operand" "0")))]
  "TARGET_MAC || TARGET_SCORE7D"
  "mad     %2, %3"
  [(set_attr "mode" "DI")])

(define_insn "umulsidi3adddi"
  [(set (match_operand:DI 0 "register_operand" "=x")
        (plus:DI (mult:DI
                  (zero_extend:DI (match_operand:SI 2 "register_operand" "%d"))
                  (zero_extend:DI (match_operand:SI 3 "register_operand" "d")))
                 (match_operand:DI 1 "register_operand" "0")))]
  "TARGET_MAC || TARGET_SCORE7D"
  "madu    %2, %3"
  [(set_attr "mode" "DI")])

(define_insn "mulsidi3subdi"
  [(set (match_operand:DI 0 "register_operand" "=x")
        (minus:DI
         (match_operand:DI 1 "register_operand" "0")
         (mult:DI
          (sign_extend:DI (match_operand:SI 2 "register_operand" "%d"))
          (sign_extend:DI (match_operand:SI 3 "register_operand" "d")))))]
  "TARGET_MAC || TARGET_SCORE7D"
  "msb     %2, %3"
  [(set_attr "mode" "DI")])

(define_insn "umulsidi3subdi"
  [(set (match_operand:DI 0 "register_operand" "=x")
        (minus:DI
         (match_operand:DI 1 "register_operand" "0")
         (mult:DI (zero_extend:DI
                   (match_operand:SI 2 "register_operand" "%d"))
                  (zero_extend:DI
                   (match_operand:SI 3 "register_operand" "d")))))]
  "TARGET_MAC || TARGET_SCORE7D"
  "msbu    %2, %3"
  [(set_attr "mode" "DI")])

;; Constraint definitions for MIPS.
;; Copyright (C) 2006 Free Software Foundation, Inc.
;;
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

;; Register constraints

(define_register_constraint "d" "BASE_REG_CLASS"
  "An address register.  This is equivalent to @code{r} unless
   generating MIPS16 code.")

(define_register_constraint "t" "T_REG"
  "@internal")

(define_register_constraint "f" "TARGET_HARD_FLOAT ? FP_REGS : NO_REGS"
  "A floating-point register (if available).")

(define_register_constraint "h" "HI_REG"
  "The @code{hi} register.")

(define_register_constraint "l" "LO_REG"
  "The @code{lo} register.")

(define_register_constraint "x" "MD_REGS"
  "The @code{hi} and @code{lo} registers.")

(define_register_constraint "b" "ALL_REGS"
  "@internal")

(define_register_constraint "c" "TARGET_ABICALLS ? PIC_FN_ADDR_REG
				 : TARGET_MIPS16 ? M16_NA_REGS
				 : GR_REGS"
  "A register suitable for use in an indirect jump.  This will always be
   @code{$25} for @option{-mabicalls}.")

(define_register_constraint "e" "LEA_REGS"
  "@internal")

(define_register_constraint "j" "PIC_FN_ADDR_REG"
  "@internal")

(define_register_constraint "v" "V1_REG"
  "@internal")

(define_register_constraint "y" "GR_REGS"
  "Equivalent to @code{r}; retained for backwards compatibility.")

(define_register_constraint "z" "ST_REGS"
  "A floating-point condition code register.")

(define_register_constraint "A" "DSP_ACC_REGS"
  "@internal")

(define_register_constraint "a" "ACC_REGS"
  "@internal")

(define_register_constraint "B" "COP0_REGS"
  "@internal")

(define_register_constraint "C" "COP2_REGS"
  "@internal")

(define_register_constraint "D" "COP3_REGS"
  "@internal")

;; Integer constraints

(define_constraint "I"
  "A signed 16-bit constant (for arithmetic instructions)."
  (and (match_code "const_int")
       (match_test "SMALL_OPERAND (ival)")))

(define_constraint "J"
  "Integer zero."
  (and (match_code "const_int")
       (match_test "ival == 0")))

(define_constraint "K"
  "An unsigned 16-bit constant (for logic instructions)."
  (and (match_code "const_int")
       (match_test "SMALL_OPERAND_UNSIGNED (ival)")))
 
(define_constraint "L"
  "A signed 32-bit constant in which the lower 16 bits are zero.
   Such constants can be loaded using @code{lui}."
  (and (match_code "const_int")
       (match_test "LUI_OPERAND (ival)")))

(define_constraint "M"
  "A constant that cannot be loaded using @code{lui}, @code{addiu}
   or @code{ori}."
  (and (match_code "const_int")
       (match_test "!SMALL_OPERAND (ival)")
       (match_test "!SMALL_OPERAND_UNSIGNED (ival)")
       (match_test "!LUI_OPERAND (ival)")))

(define_constraint "N"
  "A constant in the range -65535 to -1 (inclusive)."
  (and (match_code "const_int")
       (match_test "ival >= -0xffff && ival < 0")))

(define_constraint "O"
  "A signed 15-bit constant."
  (and (match_code "const_int")
       (match_test "ival >= -0x4000 && ival < 0x4000")))

(define_constraint "P"
  "A constant in the range 1 to 65535 (inclusive)."
  (and (match_code "const_int")
       (match_test "ival > 0 && ival < 0x10000")))

;; Floating-point constraints

(define_constraint "G"
  "Floating-point zero."
  (and (match_code "const_double")
       (match_test "op == CONST0_RTX (mode)")))

;; General constraints

(define_constraint "Q"
  "@internal"
  (match_operand 0 "const_arith_operand"))

(define_memory_constraint "R"
  "An address that can be used in a non-macro load or store."
  (and (match_code "mem")
       (match_test "mips_fetch_insns (op) == 1")))

(define_constraint "S"
  "@internal
   A constant call address."
  (and (match_operand 0 "call_insn_operand")
       (match_test "CONSTANT_P (op)")))

(define_constraint "T"
  "@internal
   A constant @code{move_operand} that cannot be safely loaded into @code{$25}
   using @code{la}."
  (and (match_operand 0 "move_operand")
       (match_test "CONSTANT_P (op)")
       (match_test "mips_dangerous_for_la25_p (op)")))

(define_constraint "U"
  "@internal
   A constant @code{move_operand} that can be safely loaded into @code{$25}
   using @code{la}."
  (and (match_operand 0 "move_operand")
       (match_test "CONSTANT_P (op)")
       (match_test "!mips_dangerous_for_la25_p (op)")))

(define_memory_constraint "W"
  "@internal
   A memory address based on a member of @code{BASE_REG_CLASS}.  This is
   true for all non-mips16 references (although it can sometimes be implicit
   if @samp{!TARGET_EXPLICIT_RELOCS}).  For MIPS16, it excludes stack and
   constant-pool references."
  (and (match_code "mem")
       (match_operand 0 "memory_operand")
       (ior (match_test "!TARGET_MIPS16")
	    (and (not (match_operand 0 "stack_operand"))
		 (not (match_test "CONSTANT_P (XEXP (op, 0))"))))))

(define_constraint "YG"
  "@internal
   A vector zero."
  (and (match_code "const_vector")
       (match_test "op == CONST0_RTX (mode)")))

(define_constraint "YA"
  "@internal
   An unsigned 6-bit constant."
  (and (match_code "const_int")
       (match_test "UIMM6_OPERAND (ival)")))

(define_constraint "YB"
  "@internal
   A signed 10-bit constant."
  (and (match_code "const_int")
       (match_test "IMM10_OPERAND (ival)")))

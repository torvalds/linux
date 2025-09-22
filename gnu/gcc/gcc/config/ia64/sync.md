;; GCC machine description for IA-64 synchronization instructions.
;; Copyright (C) 2005
;; Free Software Foundation, Inc.
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

(define_mode_macro IMODE [QI HI SI DI])
(define_mode_macro I124MODE [QI HI SI])
(define_mode_macro I48MODE [SI DI])
(define_mode_attr modesuffix [(QI "1") (HI "2") (SI "4") (DI "8")])

(define_code_macro FETCHOP [plus minus ior xor and])
(define_code_attr fetchop_name
  [(plus "add") (minus "sub") (ior "ior") (xor "xor") (and "and")])

(define_insn "memory_barrier"
  [(set (mem:BLK (match_scratch:DI 0 "X"))
	(unspec:BLK [(mem:BLK (match_scratch:DI 1 "X"))] UNSPEC_MF))]
  ""
  "mf"
  [(set_attr "itanium_class" "syst_m")])

(define_insn "fetchadd_acq_<mode>"
  [(set (match_operand:I48MODE 0 "gr_register_operand" "=r")
	(match_operand:I48MODE 1 "not_postinc_memory_operand" "+S"))
   (set (match_dup 1)
	(unspec:I48MODE [(match_dup 1)
			 (match_operand:I48MODE 2 "fetchadd_operand" "n")]
		        UNSPEC_FETCHADD_ACQ))]
  ""
  "fetchadd<modesuffix>.acq %0 = %1, %2"
  [(set_attr "itanium_class" "sem")])

(define_expand "sync_<fetchop_name><mode>"
  [(set (match_operand:IMODE 0 "memory_operand" "")
	(FETCHOP:IMODE (match_dup 0)
	  (match_operand:IMODE 1 "general_operand" "")))]
  ""
{
  ia64_expand_atomic_op (<CODE>, operands[0], operands[1], NULL, NULL);
  DONE;
})

(define_expand "sync_nand<mode>"
  [(set (match_operand:IMODE 0 "memory_operand" "")
	(and:IMODE (not:IMODE (match_dup 0))
	  (match_operand:IMODE 1 "general_operand" "")))]
  ""
{
  ia64_expand_atomic_op (NOT, operands[0], operands[1], NULL, NULL);
  DONE;
})

(define_expand "sync_old_<fetchop_name><mode>"
  [(set (match_operand:IMODE 0 "gr_register_operand" "")
	(FETCHOP:IMODE 
	  (match_operand:IMODE 1 "memory_operand" "")
	  (match_operand:IMODE 2 "general_operand" "")))]
  ""
{
  ia64_expand_atomic_op (<CODE>, operands[1], operands[2], operands[0], NULL);
  DONE;
})

(define_expand "sync_old_nand<mode>"
  [(set (match_operand:IMODE 0 "gr_register_operand" "")
	(and:IMODE 
	  (not:IMODE (match_operand:IMODE 1 "memory_operand" ""))
	  (match_operand:IMODE 2 "general_operand" "")))]
  ""
{
  ia64_expand_atomic_op (NOT, operands[1], operands[2], operands[0], NULL);
  DONE;
})

(define_expand "sync_new_<fetchop_name><mode>"
  [(set (match_operand:IMODE 0 "gr_register_operand" "")
	(FETCHOP:IMODE 
	  (match_operand:IMODE 1 "memory_operand" "")
	  (match_operand:IMODE 2 "general_operand" "")))]
  ""
{
  ia64_expand_atomic_op (<CODE>, operands[1], operands[2], NULL, operands[0]);
  DONE;
})

(define_expand "sync_new_nand<mode>"
  [(set (match_operand:IMODE 0 "gr_register_operand" "")
	(and:IMODE 
	  (not:IMODE (match_operand:IMODE 1 "memory_operand" ""))
	  (match_operand:IMODE 2 "general_operand" "")))]
  ""
{
  ia64_expand_atomic_op (NOT, operands[1], operands[2], NULL, operands[0]);
  DONE;
})

(define_expand "sync_compare_and_swap<mode>"
  [(match_operand:IMODE 0 "gr_register_operand" "")
   (match_operand:IMODE 1 "memory_operand" "")
   (match_operand:IMODE 2 "gr_register_operand" "")
   (match_operand:IMODE 3 "gr_register_operand" "")]
  ""
{
  rtx ccv = gen_rtx_REG (DImode, AR_CCV_REGNUM);
  rtx dst;

  convert_move (ccv, operands[2], 1);

  dst = operands[0];
  if (GET_MODE (dst) != DImode)
    dst = gen_reg_rtx (DImode);

  emit_insn (gen_memory_barrier ());
  emit_insn (gen_cmpxchg_rel_<mode> (dst, operands[1], ccv, operands[3]));

  if (dst != operands[0])
    emit_move_insn (operands[0], gen_lowpart (<MODE>mode, dst));
  DONE;
})

(define_insn "cmpxchg_rel_<mode>"
  [(set (match_operand:DI 0 "gr_register_operand" "=r")
	(zero_extend:DI
	  (match_operand:I124MODE 1 "not_postinc_memory_operand" "+S")))
   (set (match_dup 1)
        (unspec:I124MODE
	  [(match_dup 1)
	   (match_operand:DI 2 "ar_ccv_reg_operand" "")
	   (match_operand:I124MODE 3 "gr_register_operand" "r")]
	  UNSPEC_CMPXCHG_ACQ))]
  ""
  "cmpxchg<modesuffix>.rel %0 = %1, %3, %2"
  [(set_attr "itanium_class" "sem")])

(define_insn "cmpxchg_rel_di"
  [(set (match_operand:DI 0 "gr_register_operand" "=r")
	(match_operand:DI 1 "not_postinc_memory_operand" "+S"))
   (set (match_dup 1)
        (unspec:DI [(match_dup 1)
		    (match_operand:DI 2 "ar_ccv_reg_operand" "")
		    (match_operand:DI 3 "gr_register_operand" "r")]
		   UNSPEC_CMPXCHG_ACQ))]
  ""
  "cmpxchg8.rel %0 = %1, %3, %2"
  [(set_attr "itanium_class" "sem")])

(define_insn "sync_lock_test_and_set<mode>"
  [(set (match_operand:IMODE 0 "gr_register_operand" "=r")
        (match_operand:IMODE 1 "not_postinc_memory_operand" "+S"))
   (set (match_dup 1)
        (match_operand:IMODE 2 "gr_register_operand" "r"))]
  ""
  "xchg<modesuffix> %0 = %1, %2"
  [(set_attr "itanium_class" "sem")])

(define_expand "sync_lock_release<mode>"
  [(set (match_operand:IMODE 0 "memory_operand" "")
	(match_operand:IMODE 1 "gr_reg_or_0_operand" ""))]
  ""
{
  gcc_assert (MEM_VOLATILE_P (operands[0]));
})

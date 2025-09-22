;; GCC machine description for Alpha synchronization instructions.
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

(define_mode_macro I12MODE [QI HI])
(define_mode_macro I48MODE [SI DI])
(define_mode_attr modesuffix [(SI "l") (DI "q")])

(define_code_macro FETCHOP [plus minus ior xor and])
(define_code_attr fetchop_name
  [(plus "add") (minus "sub") (ior "ior") (xor "xor") (and "and")])
(define_code_attr fetchop_pred
  [(plus "add_operand") (minus "reg_or_8bit_operand")
   (ior "or_operand") (xor "or_operand") (and "and_operand")])
(define_code_attr fetchop_constr
  [(plus "rKL") (minus "rI") (ior "rIN") (xor "rIN") (and "riNHM")])


(define_expand "memory_barrier"
  [(set (mem:BLK (match_dup 0))
	(unspec:BLK [(mem:BLK (match_dup 0))] UNSPEC_MB))]
  ""
{
  operands[0] = gen_rtx_MEM (BLKmode, gen_rtx_SCRATCH (DImode));
  MEM_VOLATILE_P (operands[0]) = 1;
})

(define_insn "*mb_internal"
  [(set (match_operand:BLK 0 "" "")
	(unspec:BLK [(match_operand:BLK 1 "" "")] UNSPEC_MB))]
  ""
  "mb"
  [(set_attr "type" "mb")])

(define_insn "load_locked_<mode>"
  [(set (match_operand:I48MODE 0 "register_operand" "=r")
	(unspec_volatile:I48MODE
	  [(match_operand:I48MODE 1 "memory_operand" "m")]
	  UNSPECV_LL))]
  ""
  "ld<modesuffix>_l %0,%1"
  [(set_attr "type" "ld_l")])

(define_insn "store_conditional_<mode>"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (unspec_volatile:DI [(const_int 0)] UNSPECV_SC))
   (set (match_operand:I48MODE 1 "memory_operand" "=m")
	(match_operand:I48MODE 2 "reg_or_0_operand" "0"))]
  ""
  "st<modesuffix>_c %0,%1"
  [(set_attr "type" "st_c")])

;; The Alpha Architecture Handbook says that it is UNPREDICTABLE whether
;; the lock is cleared by a TAKEN branch.  If we were to honor that, it
;; would mean that we could not expand a ll/sc sequence until after the
;; final basic-block reordering pass.  Fortunately, it appears that no
;; Alpha implementation ever built actually clears the lock on branches,
;; taken or not.

(define_insn_and_split "sync_<fetchop_name><mode>"
  [(set (match_operand:I48MODE 0 "memory_operand" "+m")
	(unspec:I48MODE
	  [(FETCHOP:I48MODE (match_dup 0)
	     (match_operand:I48MODE 1 "<fetchop_pred>" "<fetchop_constr>"))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:I48MODE 2 "=&r"))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  alpha_split_atomic_op (<CODE>, operands[0], operands[1],
			 NULL, NULL, operands[2]);
  DONE;
}
  [(set_attr "type" "multi")])

(define_insn_and_split "sync_nand<mode>"
  [(set (match_operand:I48MODE 0 "memory_operand" "+m")
	(unspec:I48MODE
	  [(and:I48MODE (not:I48MODE (match_dup 0))
	     (match_operand:I48MODE 1 "register_operand" "r"))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:I48MODE 2 "=&r"))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  alpha_split_atomic_op (NOT, operands[0], operands[1],
			 NULL, NULL, operands[2]);
  DONE;
}
  [(set_attr "type" "multi")])

(define_insn_and_split "sync_old_<fetchop_name><mode>"
  [(set (match_operand:I48MODE 0 "register_operand" "=&r")
	(match_operand:I48MODE 1 "memory_operand" "+m"))
   (set (match_dup 1)
	(unspec:I48MODE
	  [(FETCHOP:I48MODE (match_dup 1)
	     (match_operand:I48MODE 2 "<fetchop_pred>" "<fetchop_constr>"))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:I48MODE 3 "=&r"))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  alpha_split_atomic_op (<CODE>, operands[1], operands[2],
			 operands[0], NULL, operands[3]);
  DONE;
}
  [(set_attr "type" "multi")])

(define_insn_and_split "sync_old_nand<mode>"
  [(set (match_operand:I48MODE 0 "register_operand" "=&r")
	(match_operand:I48MODE 1 "memory_operand" "+m"))
   (set (match_dup 1)
	(unspec:I48MODE
	  [(and:I48MODE (not:I48MODE (match_dup 1))
	     (match_operand:I48MODE 2 "register_operand" "r"))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:I48MODE 3 "=&r"))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  alpha_split_atomic_op (NOT, operands[1], operands[2],
			 operands[0], NULL, operands[3]);
  DONE;
}
  [(set_attr "type" "multi")])

(define_insn_and_split "sync_new_<fetchop_name><mode>"
  [(set (match_operand:I48MODE 0 "register_operand" "=&r")
	(FETCHOP:I48MODE 
	  (match_operand:I48MODE 1 "memory_operand" "+m")
	  (match_operand:I48MODE 2 "<fetchop_pred>" "<fetchop_constr>")))
   (set (match_dup 1)
	(unspec:I48MODE
	  [(FETCHOP:I48MODE (match_dup 1) (match_dup 2))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:I48MODE 3 "=&r"))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  alpha_split_atomic_op (<CODE>, operands[1], operands[2],
			 NULL, operands[0], operands[3]);
  DONE;
}
  [(set_attr "type" "multi")])

(define_insn_and_split "sync_new_nand<mode>"
  [(set (match_operand:I48MODE 0 "register_operand" "=&r")
	(and:I48MODE 
	  (not:I48MODE (match_operand:I48MODE 1 "memory_operand" "+m"))
	  (match_operand:I48MODE 2 "register_operand" "r")))
   (set (match_dup 1)
	(unspec:I48MODE
	  [(and:I48MODE (not:I48MODE (match_dup 1)) (match_dup 2))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:I48MODE 3 "=&r"))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  alpha_split_atomic_op (NOT, operands[1], operands[2],
			 NULL, operands[0], operands[3]);
  DONE;
}
  [(set_attr "type" "multi")])

(define_expand "sync_compare_and_swap<mode>"
  [(match_operand:I12MODE 0 "register_operand" "")
   (match_operand:I12MODE 1 "memory_operand" "")
   (match_operand:I12MODE 2 "register_operand" "")
   (match_operand:I12MODE 3 "add_operand" "")]
  ""
{
  alpha_expand_compare_and_swap_12 (operands[0], operands[1],
				    operands[2], operands[3]);
  DONE;
})

(define_insn_and_split "sync_compare_and_swap<mode>_1"
  [(set (match_operand:DI 0 "register_operand" "=&r,&r")
	(zero_extend:DI
	  (mem:I12MODE (match_operand:DI 1 "register_operand" "r,r"))))
   (set (mem:I12MODE (match_dup 1))
	(unspec:I12MODE
	  [(match_operand:DI 2 "reg_or_8bit_operand" "J,rI")
	   (match_operand:DI 3 "register_operand" "r,r")
	   (match_operand:DI 4 "register_operand" "r,r")]
	  UNSPEC_CMPXCHG))
   (clobber (match_scratch:DI 5 "=&r,&r"))
   (clobber (match_scratch:DI 6 "=X,&r"))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  alpha_split_compare_and_swap_12 (<MODE>mode, operands[0], operands[1],
				   operands[2], operands[3], operands[4],
				   operands[5], operands[6]);
  DONE;
}
  [(set_attr "type" "multi")])

(define_expand "sync_compare_and_swap<mode>"
  [(parallel
     [(set (match_operand:I48MODE 0 "register_operand" "")
	   (match_operand:I48MODE 1 "memory_operand" ""))
      (set (match_dup 1)
	   (unspec:I48MODE
	     [(match_operand:I48MODE 2 "reg_or_8bit_operand" "")
	      (match_operand:I48MODE 3 "add_operand" "rKL")]
	     UNSPEC_CMPXCHG))
      (clobber (match_scratch:I48MODE 4 "=&r"))])]
  ""
{
  if (<MODE>mode == SImode)
    operands[2] = convert_modes (DImode, SImode, operands[2], 0);
})

(define_insn_and_split "*sync_compare_and_swap<mode>"
  [(set (match_operand:I48MODE 0 "register_operand" "=&r")
	(match_operand:I48MODE 1 "memory_operand" "+m"))
   (set (match_dup 1)
	(unspec:I48MODE
	  [(match_operand:DI 2 "reg_or_8bit_operand" "rI")
	   (match_operand:I48MODE 3 "add_operand" "rKL")]
	  UNSPEC_CMPXCHG))
   (clobber (match_scratch:I48MODE 4 "=&r"))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  alpha_split_compare_and_swap (operands[0], operands[1], operands[2],
				operands[3], operands[4]);
  DONE;
}
  [(set_attr "type" "multi")])

(define_expand "sync_lock_test_and_set<mode>"
  [(match_operand:I12MODE 0 "register_operand" "")
   (match_operand:I12MODE 1 "memory_operand" "")
   (match_operand:I12MODE 2 "register_operand" "")]
  ""
{
  alpha_expand_lock_test_and_set_12 (operands[0], operands[1], operands[2]);
  DONE;
})

(define_insn_and_split "sync_lock_test_and_set<mode>_1"
  [(set (match_operand:DI 0 "register_operand" "=&r")
	(zero_extend:DI
	  (mem:I12MODE (match_operand:DI 1 "register_operand" "r"))))
   (set (mem:I12MODE (match_dup 1))
	(unspec:I12MODE
	  [(match_operand:DI 2 "reg_or_8bit_operand" "rI")
	   (match_operand:DI 3 "register_operand" "r")]
	  UNSPEC_XCHG))
   (clobber (match_scratch:DI 4 "=&r"))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  alpha_split_lock_test_and_set_12 (<MODE>mode, operands[0], operands[1],
				    operands[2], operands[3], operands[4]);
  DONE;
}
  [(set_attr "type" "multi")])

(define_insn_and_split "sync_lock_test_and_set<mode>"
  [(set (match_operand:I48MODE 0 "register_operand" "=&r")
	(match_operand:I48MODE 1 "memory_operand" "+m"))
   (set (match_dup 1)
	(unspec:I48MODE
	  [(match_operand:I48MODE 2 "add_operand" "rKL")]
	  UNSPEC_XCHG))
   (clobber (match_scratch:I48MODE 3 "=&r"))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  alpha_split_lock_test_and_set (operands[0], operands[1],
				 operands[2], operands[3]);
  DONE;
}
  [(set_attr "type" "multi")])

;; Scheduling description for UltraSPARC-I/II.
;;   Copyright (C) 2002, 2004 Free Software Foundation, Inc.
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

;; UltraSPARC-I and II are quad-issue processors.  Interesting features
;; to note:
;;
;; - Buffered loads, they can queue waiting for the actual data until
;;   an instruction actually tries to reference the destination register
;;   as an input
;; - Two integer units.  Only one of them can do shifts, and the other
;;   is the only one which may do condition code setting instructions.
;;   Complicating things further, a shift may go only into the first
;;   slot in a dispatched group.  And if you have a non-condition code
;;   setting instruction and one that does set the condition codes.  The
;;   former must be issued first in order for both of them to issue.
;; - Stores can issue before the value being stored is available.  As long
;;   as the input data becomes ready before the store is to move out of the
;;   store buffer, it will not cause a stall.
;; - Branches may issue in the same cycle as an instruction setting the
;;   condition codes being tested by that branch.  This does not apply
;;   to floating point, only integer.

(define_automaton "ultrasparc_0,ultrasparc_1")

(define_cpu_unit "us1_fdivider,us1_fpm" "ultrasparc_0");
(define_cpu_unit "us1_fpa,us1_load_writeback" "ultrasparc_1")
(define_cpu_unit "us1_fps_0,us1_fps_1,us1_fpd_0,us1_fpd_1" "ultrasparc_1")
(define_cpu_unit "us1_slot0,us1_slot1,us1_slot2,us1_slot3" "ultrasparc_1")
(define_cpu_unit "us1_ieu0,us1_ieu1,us1_cti,us1_lsu" "ultrasparc_1")

(define_reservation "us1_slot012" "(us1_slot0 | us1_slot1 | us1_slot2)")
(define_reservation "us1_slotany" "(us1_slot0 | us1_slot1 | us1_slot2 | us1_slot3)")
(define_reservation "us1_single_issue" "us1_slot0 + us1_slot1 + us1_slot2 + us1_slot3")

(define_reservation "us1_fp_single" "(us1_fps_0 | us1_fps_1)")
(define_reservation "us1_fp_double" "(us1_fpd_0 | us1_fpd_1)")

;; This is a simplified representation of the issue at hand.
;; For most cases, going from one FP precision type insn to another
;; just breaks up the insn group.  However for some cases, such
;; a situation causes the second insn to stall 2 more cycles.
(exclusion_set "us1_fps_0,us1_fps_1" "us1_fpd_0,us1_fpd_1")

;; If we have to schedule an ieu1 specific instruction and we want
;; to reserve the ieu0 unit as well, we must reserve it first.  So for
;; example we could not schedule this sequence:
;;	COMPARE		IEU1
;;	IALU		IEU0
;; but we could schedule them together like this:
;;	IALU		IEU0
;;	COMPARE		IEU1
;; This basically requires that ieu0 is reserved before ieu1 when
;; it is required that both be reserved.
(absence_set "us1_ieu0" "us1_ieu1")

;; This defines the slotting order.  Most IEU instructions can only
;; execute in the first three slots, FPU and branches can go into
;; any slot.  We represent instructions which "break the group"
;; as requiring reservation of us1_slot0.
(absence_set "us1_slot0" "us1_slot1,us1_slot2,us1_slot3")
(absence_set "us1_slot1" "us1_slot2,us1_slot3")
(absence_set "us1_slot2" "us1_slot3")

(define_insn_reservation "us1_single" 1
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "multi,savew,flushw,iflush,trap"))
  "us1_single_issue")

(define_insn_reservation "us1_simple_ieuN" 1
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "ialu"))
  "(us1_ieu0 | us1_ieu1) + us1_slot012")

(define_insn_reservation "us1_simple_ieu0" 1
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "shift"))
  "us1_ieu0 + us1_slot012")

(define_insn_reservation "us1_simple_ieu1" 1
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "compare"))
  "us1_ieu1 + us1_slot012")

(define_insn_reservation "us1_ialuX" 1
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "ialuX"))
  "us1_single_issue")

(define_insn_reservation "us1_cmove" 2
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "cmove"))
  "us1_single_issue, nothing")

(define_insn_reservation "us1_imul" 1
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "imul"))
  "us1_single_issue")

(define_insn_reservation "us1_idiv" 1
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "idiv"))
  "us1_single_issue")

;; For loads, the "delayed return mode" behavior of the chip
;; is represented using the us1_load_writeback resource.
(define_insn_reservation "us1_load" 2
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "load,fpload"))
  "us1_lsu + us1_slot012, us1_load_writeback")

(define_insn_reservation "us1_load_signed" 3
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "sload"))
  "us1_lsu + us1_slot012, nothing, us1_load_writeback")

(define_insn_reservation "us1_store" 1
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "store,fpstore"))
  "us1_lsu + us1_slot012")

(define_insn_reservation "us1_branch" 1
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "branch"))
  "us1_cti + us1_slotany")

(define_insn_reservation "us1_call_jmpl" 1
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "call,sibcall,call_no_delay_slot,uncond_branch"))
  "us1_cti + us1_ieu1 + us1_slot0")

(define_insn_reservation "us1_fmov_single" 1
  (and (and (eq_attr "cpu" "ultrasparc")
            (eq_attr "type" "fpmove"))
       (eq_attr "fptype" "single"))
  "us1_fpa + us1_fp_single + us1_slotany")

(define_insn_reservation "us1_fmov_double" 1
  (and (and (eq_attr "cpu" "ultrasparc")
            (eq_attr "type" "fpmove"))
       (eq_attr "fptype" "double"))
  "us1_fpa + us1_fp_double + us1_slotany")

(define_insn_reservation "us1_fcmov_single" 2
  (and (and (eq_attr "cpu" "ultrasparc")
            (eq_attr "type" "fpcmove,fpcrmove"))
       (eq_attr "fptype" "single"))
  "us1_fpa + us1_fp_single + us1_slotany, nothing")

(define_insn_reservation "us1_fcmov_double" 2
  (and (and (eq_attr "cpu" "ultrasparc")
            (eq_attr "type" "fpcmove,fpcrmove"))
       (eq_attr "fptype" "double"))
  "us1_fpa + us1_fp_double + us1_slotany, nothing")

(define_insn_reservation "us1_faddsub_single" 4
  (and (and (eq_attr "cpu" "ultrasparc")
            (eq_attr "type" "fp"))
       (eq_attr "fptype" "single"))
  "us1_fpa + us1_fp_single + us1_slotany, nothing*3")

(define_insn_reservation "us1_faddsub_double" 4
  (and (and (eq_attr "cpu" "ultrasparc")
            (eq_attr "type" "fp"))
       (eq_attr "fptype" "double"))
  "us1_fpa + us1_fp_double + us1_slotany, nothing*3")

(define_insn_reservation "us1_fpcmp_single" 1
  (and (and (eq_attr "cpu" "ultrasparc")
            (eq_attr "type" "fpcmp"))
       (eq_attr "fptype" "single"))
  "us1_fpa + us1_fp_single + us1_slotany")

(define_insn_reservation "us1_fpcmp_double" 1
  (and (and (eq_attr "cpu" "ultrasparc")
            (eq_attr "type" "fpcmp"))
       (eq_attr "fptype" "double"))
  "us1_fpa + us1_fp_double + us1_slotany")

(define_insn_reservation "us1_fmult_single" 4
  (and (and (eq_attr "cpu" "ultrasparc")
            (eq_attr "type" "fpmul"))
       (eq_attr "fptype" "single"))
  "us1_fpm + us1_fp_single + us1_slotany, nothing*3")

(define_insn_reservation "us1_fmult_double" 4
  (and (and (eq_attr "cpu" "ultrasparc")
            (eq_attr "type" "fpmul"))
       (eq_attr "fptype" "double"))
  "us1_fpm + us1_fp_double + us1_slotany, nothing*3")

;; This is actually in theory dangerous, because it is possible
;; for the chip to prematurely dispatch the dependent instruction
;; in the G stage, resulting in a 9 cycle stall.  However I have never
;; been able to trigger this case myself even with hand written code,
;; so it must require some rare complicated pipeline state.
(define_bypass 3
   "us1_faddsub_single,us1_faddsub_double,us1_fmult_single,us1_fmult_double"
   "us1_faddsub_single,us1_faddsub_double,us1_fmult_single,us1_fmult_double")

;; Floating point divide and square root use the multiplier unit
;; for final rounding 3 cycles before the divide/sqrt is complete.

(define_insn_reservation "us1_fdivs"
  13
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "fpdivs,fpsqrts"))
  "(us1_fpm + us1_fdivider + us1_slot0), us1_fdivider*8, (us1_fpm + us1_fdivider), us1_fdivider*2"
  )

(define_bypass
  12
  "us1_fdivs"
  "us1_faddsub_single,us1_faddsub_double,us1_fmult_single,us1_fmult_double")

(define_insn_reservation "us1_fdivd"
  23
  (and (eq_attr "cpu" "ultrasparc")
    (eq_attr "type" "fpdivd,fpsqrtd"))
  "(us1_fpm + us1_fdivider + us1_slot0), us1_fdivider*18, (us1_fpm + us1_fdivider), us1_fdivider*2"
  )
(define_bypass
  22
  "us1_fdivd"
  "us1_faddsub_single,us1_faddsub_double,us1_fmult_single,us1_fmult_double")

;; Any store may multi issue with the insn creating the source
;; data as long as that creating insn is not an FPU div/sqrt.
;; We need a special guard function because this bypass does
;; not apply to the address inputs of the store.
(define_bypass 0 "us1_simple_ieuN,us1_simple_ieu1,us1_simple_ieu0,us1_faddsub_single,us1_faddsub_double,us1_fmov_single,us1_fmov_double,us1_fcmov_single,us1_fcmov_double,us1_fmult_single,us1_fmult_double" "us1_store"
   "store_data_bypass_p")

;; An integer branch may execute in the same cycle as the compare
;; creating the condition codes.
(define_bypass 0 "us1_simple_ieu1" "us1_branch")

;; VIS scheduling
(define_insn_reservation "us1_fga_single"
  2
  (and (and
         (eq_attr "cpu" "ultrasparc")
         (eq_attr "type" "fga"))
       (eq_attr "fptype" "single"))
  "us1_fpa + us1_fp_single + us1_slotany, nothing")

(define_bypass 1 "us1_fga_single" "us1_fga_single")

(define_insn_reservation "us1_fga_double"
  2
  (and (and
         (eq_attr "cpu" "ultrasparc")
         (eq_attr "type" "fga"))
       (eq_attr "fptype" "double"))
  "us1_fpa + us1_fp_double + us1_slotany, nothing")

(define_bypass 1 "us1_fga_double" "us1_fga_double")

(define_insn_reservation "us1_fgm_single"
  4
  (and (and
         (eq_attr "cpu" "ultrasparc")
         (eq_attr "type" "fgm_pack,fgm_mul,fgm_cmp"))
       (eq_attr "fptype" "single"))
  "us1_fpm + us1_fp_single + us1_slotany, nothing*3")

(define_bypass 3 "us1_fgm_single" "us1_fga_single")

(define_insn_reservation "us1_fgm_double"
  4
  (and (and
         (eq_attr "cpu" "ultrasparc")
         (eq_attr "type" "fgm_pack,fgm_mul,fgm_cmp"))
       (eq_attr "fptype" "double"))
  "us1_fpm + us1_fp_double + us1_slotany, nothing*3")

(define_bypass 3 "us1_fgm_double" "us1_fga_double")

(define_insn_reservation "us1_pdist"
  4
  (and (eq_attr "cpu" "ultrasparc")
       (eq_attr "type" "fgm_pdist"))
  "us1_fpm + us1_fp_double + us1_slotany, nothing*3")

(define_bypass 3 "us1_pdist" "us1_fga_double,us1_fga_single")
(define_bypass 1 "us1_pdist" "us1_pdist")

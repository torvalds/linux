;; Scheduling description for Niagara.
;;   Copyright (C) 2006 Free Software Foundation, Inc.
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

;; Niagara is a single-issue processor.

(define_automaton "niagara_0")

(define_cpu_unit "niag_pipe" "niagara_0")

(define_insn_reservation "niag_5cycle" 5
  (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "multi,flushw,iflush,trap"))
  "niag_pipe*5")

(define_insn_reservation "niag_4cycle" 4
  (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "savew"))
  "niag_pipe*4")

/* Most basic operations are single-cycle. */
(define_insn_reservation "niag_ialu" 1
 (and (eq_attr "cpu" "niagara")
   (eq_attr "type" "ialu,shift,compare,cmove"))
 "niag_pipe")

(define_insn_reservation "niag_imul" 11
 (and (eq_attr "cpu" "niagara")
   (eq_attr "type" "imul"))
 "niag_pipe*11")

(define_insn_reservation "niag_idiv" 72
 (and (eq_attr "cpu" "niagara")
   (eq_attr "type" "idiv"))
 "niag_pipe*72")

(define_insn_reservation "niag_branch" 3
  (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "call,sibcall,call_no_delay_slot,uncond_branch,branch"))
  "niag_pipe*3")

(define_insn_reservation "niag_3cycle_load" 3
  (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "load"))
  "niag_pipe*3")

(define_insn_reservation "niag_9cycle_load" 9
  (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "fpload"))
  "niag_pipe*9")

(define_insn_reservation "niag_1cycle_store" 1
  (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "store"))
  "niag_pipe")

(define_insn_reservation "niag_8cycle_store" 8
  (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "fpstore"))
  "niag_pipe*8")

/* Things incorrectly modelled here:
 *  FPADD{s,d}: 26 cycles
 *  FPSUB{s,d}: 26 cycles
 *  FABSD: 26 cycles
 *  F{s,d}TO{s,d}: 26 cycles
 *  F{s,d}TO{i,x}: 26 cycles
 *  FSMULD: 29 cycles
 */
(define_insn_reservation "niag_fmov" 8
  (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "fpmove,fpcmove,fpcrmove"))
  "niag_pipe*8")

(define_insn_reservation "niag_fpcmp" 26
  (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "fpcmp"))
  "niag_pipe*26")

(define_insn_reservation "niag_fmult" 29
 (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "fpmul"))
  "niag_pipe*29")

(define_insn_reservation "niag_fdivs" 54
  (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "fpdivs"))
  "niag_pipe*54")

(define_insn_reservation "niag_fdivd" 83
  (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "fpdivd"))
  "niag_pipe*83")

/* Things incorrectly modelled here:
 *  FPADD{16,32}: 10 cycles
 *  FPSUB{16,32}: 10 cycles
 *  FALIGNDATA: 10 cycles
 */
(define_insn_reservation "niag_vis" 8
  (and (eq_attr "cpu" "niagara")
    (eq_attr "type" "fga,fgm_pack,fgm_mul,fgm_cmp,fgm_pdist"))
  "niag_pipe*8")

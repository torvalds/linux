;; DFA scheduling description for SH-5 SHmedia instructions.
;; Copyright (C) 2004, 2005 Free Software Foundation, Inc.

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

;; This is just a conversion of the old model using define_function_unit.

;; When executing SHmedia code, the SH-5 is a fairly straightforward
;; single-issue machine.  It has four pipelines, the branch unit (br),
;; the integer and multimedia unit (imu), the load/store unit (lsu), and
;; the floating point unit (fpu).

(define_automaton "sh5inst_pipe, sh5fpu_pipe")

(define_cpu_unit "sh5issue" "sh5inst_pipe")

(define_cpu_unit "sh5fds" "sh5fpu_pipe")

;; Every instruction on SH-5 occupies the issue resource for at least one
;; cycle.
(define_insn_reservation "shmedia1" 1
  (and (eq_attr "pipe_model" "sh5media")
       (eq_attr "type" "!pt_media,ptabs_media,invalidate_line_media,dmpy_media,load_media,fload_media,fcmp_media,fmove_media,fparith_media,dfparith_media,fpconv_media,dfpconv_media,dfmul_media,store_media,fstore_media,mcmp_media,mac_media,d2mpy_media,atrans_media,ustore_media"))
  "sh5issue")

;; Specify the various types of instruction which have latency > 1
(define_insn_reservation "shmedia2" 2
  (and (eq_attr "pipe_model" "sh5media")
       (eq_attr "type" "mcmp_media"))
  "sh5issue")

(define_insn_reservation "shmedia3" 3
  (and (eq_attr "pipe_model" "sh5media")
       (eq_attr "type" "dmpy_media,load_media,fcmp_media,mac_media"))
  "sh5issue")
;; but see sh_adjust_cost for mac_media exception.

(define_insn_reservation "shmedia4" 4
  (and (eq_attr "pipe_model" "sh5media")
       (eq_attr "type" "fload_media,fmove_media"))
  "sh5issue")

(define_insn_reservation "shmedia_d2mpy" 4
  (and (eq_attr "pipe_model" "sh5media")
       (eq_attr "type" "d2mpy_media"))
  "sh5issue*2")

(define_insn_reservation "shmedia5" 5
  (and (eq_attr "pipe_model" "sh5media")
       (eq_attr "type" "pt_media,ptabs_media"))
  "sh5issue")

(define_insn_reservation "shmedia6" 6
  (and (eq_attr "pipe_model" "sh5media")
       (eq_attr "type" "fparith_media,dfparith_media,fpconv_media,dfpconv_media"))
  "sh5issue")

(define_insn_reservation "shmedia_invalidate" 7
  (and (eq_attr "pipe_model" "sh5media")
       (eq_attr "type" "invalidate_line_media"))
  "sh5issue*7")

(define_insn_reservation "shmedia_dfmul" 9
  (and (eq_attr "pipe_model" "sh5media") (eq_attr "type" "dfmul_media"))
  "sh5issue*4")

(define_insn_reservation "shmedia_atrans" 10
  (and (eq_attr "pipe_model" "sh5media") (eq_attr "type" "atrans_media"))
  "sh5issue*5")

;; Floating-point divide and square-root occupy an additional resource,
;; which is not internally pipelined.  However, other instructions
;; can continue to issue.
(define_insn_reservation "shmedia_fdiv" 19
  (and (eq_attr "pipe_model" "sh5media") (eq_attr "type" "fdiv_media"))
  "sh5issue+sh5fds,sh5fds*18")

(define_insn_reservation "shmedia_dfdiv" 35
  (and (eq_attr "pipe_model" "sh5media") (eq_attr "type" "dfdiv_media"))
  "sh5issue+sh5fds,sh5fds*34")

;; Itanium1 (original Itanium) DFA descriptions for insn scheduling
;; and bundling.
;; Copyright (C) 2002, 2004, 2005 Free Software Foundation, Inc.
;; Contributed by Vladimir Makarov <vmakarov@redhat.com>.
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
;; Boston, MA 02110-1301, USA.  */
;;


/* This is description of pipeline hazards based on DFA.  The
   following constructions can be used for this:
   
   o define_cpu_unit string [string]) describes a cpu functional unit
     (separated by comma).

     1st operand: Names of cpu function units.
     2nd operand: Name of automaton (see comments for
     DEFINE_AUTOMATON).

     All define_reservations and define_cpu_units should have unique
     names which cannot be "nothing".

   o (exclusion_set string string) means that each CPU function unit
     in the first string cannot be reserved simultaneously with each
     unit whose name is in the second string and vise versa.  CPU
     units in the string are separated by commas. For example, it is
     useful for description CPU with fully pipelined floating point
     functional unit which can execute simultaneously only single
     floating point insns or only double floating point insns.

   o (presence_set string string) means that each CPU function unit in
     the first string cannot be reserved unless at least one of
     pattern of units whose names are in the second string is
     reserved.  This is an asymmetric relation.  CPU units or unit
     patterns in the strings are separated by commas.  Pattern is one
     unit name or unit names separated by white-spaces.
 
     For example, it is useful for description that slot1 is reserved
     after slot0 reservation for a VLIW processor.  We could describe
     it by the following construction

         (presence_set "slot1" "slot0")

     Or slot1 is reserved only after slot0 and unit b0 reservation.
     In this case we could write

         (presence_set "slot1" "slot0 b0")

     All CPU functional units in a set should belong to the same
     automaton.

   o (final_presence_set string string) is analogous to
     `presence_set'.  The difference between them is when checking is
     done.  When an instruction is issued in given automaton state
     reflecting all current and planned unit reservations, the
     automaton state is changed.  The first state is a source state,
     the second one is a result state.  Checking for `presence_set' is
     done on the source state reservation, checking for
     `final_presence_set' is done on the result reservation.  This
     construction is useful to describe a reservation which is
     actually two subsequent reservations.  For example, if we use

         (presence_set "slot1" "slot0")

     the following insn will be never issued (because slot1 requires
     slot0 which is absent in the source state).

         (define_reservation "insn_and_nop" "slot0 + slot1")

     but it can be issued if we use analogous `final_presence_set'.

   o (absence_set string string) means that each CPU function unit in
     the first string can be reserved only if each pattern of units
     whose names are in the second string is not reserved.  This is an
     asymmetric relation (actually exclusion set is analogous to this
     one but it is symmetric).  CPU units or unit patterns in the
     string are separated by commas.  Pattern is one unit name or unit
     names separated by white-spaces.

     For example, it is useful for description that slot0 cannot be
     reserved after slot1 or slot2 reservation for a VLIW processor.
     We could describe it by the following construction

        (absence_set "slot2" "slot0, slot1")

     Or slot2 cannot be reserved if slot0 and unit b0 are reserved or
     slot1 and unit b1 are reserved .  In this case we could write

        (absence_set "slot2" "slot0 b0, slot1 b1")

     All CPU functional units in a set should to belong the same
     automaton.

   o (final_absence_set string string) is analogous to `absence_set' but
     checking is done on the result (state) reservation.  See comments
     for final_presence_set.

   o (define_bypass number out_insn_names in_insn_names) names bypass with
     given latency (the first number) from insns given by the first
     string (see define_insn_reservation) into insns given by the
     second string.  Insn names in the strings are separated by
     commas.

   o (define_automaton string) describes names of an automaton
     generated and used for pipeline hazards recognition.  The names
     are separated by comma.  Actually it is possibly to generate the
     single automaton but unfortunately it can be very large.  If we
     use more one automata, the summary size of the automata usually
     is less than the single one.  The automaton name is used in
     define_cpu_unit.  All automata should have unique names.

   o (automata_option string) describes option for generation of
     automata.  Currently there are the following options:

     o "no-minimization" which makes no minimization of automata.
       This is only worth to do when we are debugging the description
       and need to look more accurately at reservations of states.

     o "ndfa" which makes automata with nondetermenistic reservation
        by insns.

   o (define_reservation string string) names reservation (the first
     string) of cpu functional units (the 2nd string).  Sometimes unit
     reservations for different insns contain common parts.  In such
     case, you describe common part and use one its name (the 1st
     parameter) in regular expression in define_insn_reservation.  All
     define_reservations, define results and define_cpu_units should
     have unique names which cannot be "nothing".

   o (define_insn_reservation name default_latency condition regexpr)
     describes reservation of cpu functional units (the 3nd operand)
     for instruction which is selected by the condition (the 2nd
     parameter).  The first parameter is used for output of debugging
     information.  The reservations are described by a regular
     expression according the following syntax:

       regexp = regexp "," oneof
              | oneof

       oneof = oneof "|" allof
             | allof

       allof = allof "+" repeat
             | repeat
 
       repeat = element "*" number
              | element

       element = cpu_function_name
               | reservation_name
               | result_name
               | "nothing"
               | "(" regexp ")"

       1. "," is used for describing start of the next cycle in
          reservation.

       2. "|" is used for describing the reservation described by the
          first regular expression *or* the reservation described by
          the second regular expression *or* etc.

       3. "+" is used for describing the reservation described by the
          first regular expression *and* the reservation described by
          the second regular expression *and* etc.

       4. "*" is used for convenience and simply means sequence in
          which the regular expression are repeated NUMBER times with
          cycle advancing (see ",").

       5. cpu function unit name which means reservation.

       6. reservation name -- see define_reservation.

       7. string "nothing" means no units reservation.

*/

(define_automaton "one")

;;   All possible combinations of bundles/syllables
(define_cpu_unit "1_0m.ii, 1_0m.mi, 1_0m.fi, 1_0m.mf, 1_0b.bb, 1_0m.bb,\
                  1_0m.ib, 1_0m.mb, 1_0m.fb, 1_0m.lx" "one")
(define_cpu_unit "1_0mi.i, 1_0mm.i, 1_0mf.i, 1_0mm.f, 1_0bb.b, 1_0mb.b,\
                  1_0mi.b, 1_0mm.b, 1_0mf.b, 1_0mlx." "one")
(define_cpu_unit "1_0mii., 1_0mmi., 1_0mfi., 1_0mmf., 1_0bbb., 1_0mbb.,\
                  1_0mib., 1_0mmb., 1_0mfb." "one")

(define_cpu_unit "1_1m.ii, 1_1m.mi, 1_1m.fi, 1_1b.bb, 1_1m.bb,\
                  1_1m.ib, 1_1m.mb, 1_1m.fb, 1_1m.lx" "one")
(define_cpu_unit "1_1mi.i, 1_1mm.i, 1_1mf.i, 1_1bb.b, 1_1mb.b,\
                  1_1mi.b, 1_1mm.b, 1_1mf.b, 1_1mlx." "one")
(define_cpu_unit "1_1mii., 1_1mmi., 1_1mfi., 1_1bbb., 1_1mbb.,\
                  1_1mib., 1_1mmb., 1_1mfb." "one")

;; Slot 1
(exclusion_set "1_0m.ii"
   "1_0m.mi, 1_0m.fi, 1_0m.mf, 1_0b.bb, 1_0m.bb, 1_0m.ib, 1_0m.mb, 1_0m.fb,\
    1_0m.lx")
(exclusion_set "1_0m.mi"
   "1_0m.fi, 1_0m.mf, 1_0b.bb, 1_0m.bb, 1_0m.ib, 1_0m.mb, 1_0m.fb, 1_0m.lx")
(exclusion_set "1_0m.fi"
   "1_0m.mf, 1_0b.bb, 1_0m.bb, 1_0m.ib, 1_0m.mb, 1_0m.fb, 1_0m.lx")
(exclusion_set "1_0m.mf"
   "1_0b.bb, 1_0m.bb, 1_0m.ib, 1_0m.mb, 1_0m.fb, 1_0m.lx")
(exclusion_set "1_0b.bb" "1_0m.bb, 1_0m.ib, 1_0m.mb, 1_0m.fb, 1_0m.lx")
(exclusion_set "1_0m.bb" "1_0m.ib, 1_0m.mb, 1_0m.fb, 1_0m.lx")
(exclusion_set "1_0m.ib" "1_0m.mb, 1_0m.fb, 1_0m.lx")
(exclusion_set "1_0m.mb" "1_0m.fb, 1_0m.lx")
(exclusion_set "1_0m.fb" "1_0m.lx")

;; Slot 2
(exclusion_set "1_0mi.i"
   "1_0mm.i, 1_0mf.i, 1_0mm.f, 1_0bb.b, 1_0mb.b, 1_0mi.b, 1_0mm.b, 1_0mf.b,\
    1_0mlx.")
(exclusion_set "1_0mm.i"
   "1_0mf.i, 1_0mm.f, 1_0bb.b, 1_0mb.b, 1_0mi.b, 1_0mm.b, 1_0mf.b, 1_0mlx.")
(exclusion_set "1_0mf.i"
   "1_0mm.f, 1_0bb.b, 1_0mb.b, 1_0mi.b, 1_0mm.b, 1_0mf.b, 1_0mlx.")
(exclusion_set "1_0mm.f"
   "1_0bb.b, 1_0mb.b, 1_0mi.b, 1_0mm.b, 1_0mf.b, 1_0mlx.")
(exclusion_set "1_0bb.b" "1_0mb.b, 1_0mi.b, 1_0mm.b, 1_0mf.b, 1_0mlx.")
(exclusion_set "1_0mb.b" "1_0mi.b, 1_0mm.b, 1_0mf.b, 1_0mlx.")
(exclusion_set "1_0mi.b" "1_0mm.b, 1_0mf.b, 1_0mlx.")
(exclusion_set "1_0mm.b" "1_0mf.b, 1_0mlx.")
(exclusion_set "1_0mf.b" "1_0mlx.")

;; Slot 3
(exclusion_set "1_0mii."
   "1_0mmi., 1_0mfi., 1_0mmf., 1_0bbb., 1_0mbb., 1_0mib., 1_0mmb., 1_0mfb.,\
    1_0mlx.")
(exclusion_set "1_0mmi."
   "1_0mfi., 1_0mmf., 1_0bbb., 1_0mbb., 1_0mib., 1_0mmb., 1_0mfb., 1_0mlx.")
(exclusion_set "1_0mfi."
   "1_0mmf., 1_0bbb., 1_0mbb., 1_0mib., 1_0mmb., 1_0mfb., 1_0mlx.")
(exclusion_set "1_0mmf."
   "1_0bbb., 1_0mbb., 1_0mib., 1_0mmb., 1_0mfb., 1_0mlx.")
(exclusion_set "1_0bbb." "1_0mbb., 1_0mib., 1_0mmb., 1_0mfb., 1_0mlx.")
(exclusion_set "1_0mbb." "1_0mib., 1_0mmb., 1_0mfb., 1_0mlx.")
(exclusion_set "1_0mib." "1_0mmb., 1_0mfb., 1_0mlx.")
(exclusion_set "1_0mmb." "1_0mfb., 1_0mlx.")
(exclusion_set "1_0mfb." "1_0mlx.")

;; Slot 4
(exclusion_set "1_1m.ii"
   "1_1m.mi, 1_1m.fi, 1_1b.bb, 1_1m.bb, 1_1m.ib, 1_1m.mb, 1_1m.fb, 1_1m.lx")
(exclusion_set "1_1m.mi"
   "1_1m.fi, 1_1b.bb, 1_1m.bb, 1_1m.ib, 1_1m.mb, 1_1m.fb, 1_1m.lx")
(exclusion_set "1_1m.fi"
   "1_1b.bb, 1_1m.bb, 1_1m.ib, 1_1m.mb, 1_1m.fb, 1_1m.lx")
(exclusion_set "1_1b.bb" "1_1m.bb, 1_1m.ib, 1_1m.mb, 1_1m.fb, 1_1m.lx")
(exclusion_set "1_1m.bb" "1_1m.ib, 1_1m.mb, 1_1m.fb, 1_1m.lx")
(exclusion_set "1_1m.ib" "1_1m.mb, 1_1m.fb, 1_1m.lx")
(exclusion_set "1_1m.mb" "1_1m.fb, 1_1m.lx")
(exclusion_set "1_1m.fb" "1_1m.lx")

;; Slot 5
(exclusion_set "1_1mi.i"
   "1_1mm.i, 1_1mf.i, 1_1bb.b, 1_1mb.b, 1_1mi.b, 1_1mm.b, 1_1mf.b, 1_1mlx.")
(exclusion_set "1_1mm.i"
   "1_1mf.i, 1_1bb.b, 1_1mb.b, 1_1mi.b, 1_1mm.b, 1_1mf.b, 1_1mlx.")
(exclusion_set "1_1mf.i"
   "1_1bb.b, 1_1mb.b, 1_1mi.b, 1_1mm.b, 1_1mf.b, 1_1mlx.")
(exclusion_set "1_1bb.b" "1_1mb.b, 1_1mi.b, 1_1mm.b, 1_1mf.b, 1_1mlx.")
(exclusion_set "1_1mb.b" "1_1mi.b, 1_1mm.b, 1_1mf.b, 1_1mlx.")
(exclusion_set "1_1mi.b" "1_1mm.b, 1_1mf.b, 1_1mlx.")
(exclusion_set "1_1mm.b" "1_1mf.b, 1_1mlx.")
(exclusion_set "1_1mf.b" "1_1mlx.")

;; Slot 6
(exclusion_set "1_1mii."
   "1_1mmi., 1_1mfi., 1_1bbb., 1_1mbb., 1_1mib., 1_1mmb., 1_1mfb., 1_1mlx.")
(exclusion_set "1_1mmi."
   "1_1mfi., 1_1bbb., 1_1mbb., 1_1mib., 1_1mmb., 1_1mfb., 1_1mlx.")
(exclusion_set "1_1mfi."
   "1_1bbb., 1_1mbb., 1_1mib., 1_1mmb., 1_1mfb., 1_1mlx.")
(exclusion_set "1_1bbb." "1_1mbb., 1_1mib., 1_1mmb., 1_1mfb., 1_1mlx.")
(exclusion_set "1_1mbb." "1_1mib., 1_1mmb., 1_1mfb., 1_1mlx.")
(exclusion_set "1_1mib." "1_1mmb., 1_1mfb., 1_1mlx.")
(exclusion_set "1_1mmb." "1_1mfb., 1_1mlx.")
(exclusion_set "1_1mfb." "1_1mlx.")

(final_presence_set "1_0mi.i" "1_0m.ii")
(final_presence_set "1_0mii." "1_0mi.i")
(final_presence_set "1_1mi.i" "1_1m.ii")
(final_presence_set "1_1mii." "1_1mi.i")

(final_presence_set "1_0mm.i" "1_0m.mi")
(final_presence_set "1_0mmi." "1_0mm.i")
(final_presence_set "1_1mm.i" "1_1m.mi")
(final_presence_set "1_1mmi." "1_1mm.i")

(final_presence_set "1_0mf.i" "1_0m.fi")
(final_presence_set "1_0mfi." "1_0mf.i")
(final_presence_set "1_1mf.i" "1_1m.fi")
(final_presence_set "1_1mfi." "1_1mf.i")

(final_presence_set "1_0mm.f" "1_0m.mf")
(final_presence_set "1_0mmf." "1_0mm.f")

(final_presence_set "1_0bb.b" "1_0b.bb")
(final_presence_set "1_0bbb." "1_0bb.b")
(final_presence_set "1_1bb.b" "1_1b.bb")
(final_presence_set "1_1bbb." "1_1bb.b")

(final_presence_set "1_0mb.b" "1_0m.bb")
(final_presence_set "1_0mbb." "1_0mb.b")
(final_presence_set "1_1mb.b" "1_1m.bb")
(final_presence_set "1_1mbb." "1_1mb.b")

(final_presence_set "1_0mi.b" "1_0m.ib")
(final_presence_set "1_0mib." "1_0mi.b")
(final_presence_set "1_1mi.b" "1_1m.ib")
(final_presence_set "1_1mib." "1_1mi.b")

(final_presence_set "1_0mm.b" "1_0m.mb")
(final_presence_set "1_0mmb." "1_0mm.b")
(final_presence_set "1_1mm.b" "1_1m.mb")
(final_presence_set "1_1mmb." "1_1mm.b")

(final_presence_set "1_0mf.b" "1_0m.fb")
(final_presence_set "1_0mfb." "1_0mf.b")
(final_presence_set "1_1mf.b" "1_1m.fb")
(final_presence_set "1_1mfb." "1_1mf.b")

(final_presence_set "1_0mlx." "1_0m.lx")
(final_presence_set "1_1mlx." "1_1m.lx")

(final_presence_set
   "1_1m.ii,1_1m.mi,1_1m.fi,1_1b.bb,1_1m.bb,1_1m.ib,1_1m.mb,1_1m.fb,1_1m.lx"
   "1_0mii.,1_0mmi.,1_0mfi.,1_0mmf.,1_0bbb.,1_0mbb.,1_0mib.,1_0mmb.,1_0mfb.,\
    1_0mlx.")

;;  Microarchitecture units:
(define_cpu_unit
   "1_um0, 1_um1, 1_ui0, 1_ui1, 1_uf0, 1_uf1, 1_ub0, 1_ub1, 1_ub2,\
    1_unb0, 1_unb1, 1_unb2" "one")

(exclusion_set "1_ub0" "1_unb0")
(exclusion_set "1_ub1" "1_unb1")
(exclusion_set "1_ub2" "1_unb2")

;; The following rules are used to decrease number of alternatives.
;; They are consequences of Itanium microarchitecture.  They also
;; describe the following rules mentioned in Itanium
;; microarchitecture: rules mentioned in Itanium microarchitecture:
;; o "MMF: Always splits issue before the first M and after F regardless
;;   of surrounding bundles and stops".
;; o "BBB/MBB: Always splits issue after either of these bundles".
;; o "MIB BBB: Split issue after the first bundle in this pair".

(exclusion_set "1_0m.mf,1_0mm.f,1_0mmf."
   "1_1m.ii,1_1m.mi,1_1m.fi,1_1b.bb,1_1m.bb,1_1m.ib,1_1m.mb,1_1m.fb,1_1m.lx")
(exclusion_set "1_0b.bb,1_0bb.b,1_0bbb.,1_0m.bb,1_0mb.b,1_0mbb."
   "1_1m.ii,1_1m.mi,1_1m.fi,1_1b.bb,1_1m.bb,1_1m.ib,1_1m.mb,1_1m.fb,1_1m.lx")
(exclusion_set "1_0m.ib,1_0mi.b,1_0mib." "1_1b.bb")

;;  For exceptions of M, I, B, F insns:
(define_cpu_unit "1_not_um1, 1_not_ui1, 1_not_uf1" "one")

(final_absence_set "1_not_um1"  "1_um1")
(final_absence_set "1_not_ui1"  "1_ui1")
(final_absence_set "1_not_uf1"  "1_uf1")

;;; "MIB/MFB/MMB: Splits issue after any of these bundles unless the
;;; B-slot contains a nop.b or a brp instruction".
;;;   "The B in an MIB/MFB/MMB bundle disperses to B0 if it is a brp or
;;; nop.b, otherwise it disperses to B2".
(final_absence_set
   "1_1m.ii, 1_1m.mi, 1_1m.fi, 1_1b.bb, 1_1m.bb, 1_1m.ib, 1_1m.mb, 1_1m.fb,\
    1_1m.lx"
   "1_0mib. 1_ub2, 1_0mfb. 1_ub2, 1_0mmb. 1_ub2")

;; This is necessary to start new processor cycle when we meet stop bit.
(define_cpu_unit "1_stop" "one")
(final_absence_set
   "1_0m.ii,1_0mi.i,1_0mii.,1_0m.mi,1_0mm.i,1_0mmi.,1_0m.fi,1_0mf.i,1_0mfi.,\
    1_0m.mf,1_0mm.f,1_0mmf.,1_0b.bb,1_0bb.b,1_0bbb.,1_0m.bb,1_0mb.b,1_0mbb.,\
    1_0m.ib,1_0mi.b,1_0mib.,1_0m.mb,1_0mm.b,1_0mmb.,1_0m.fb,1_0mf.b,1_0mfb.,\
    1_0m.lx,1_0mlx., \
    1_1m.ii,1_1mi.i,1_1mii.,1_1m.mi,1_1mm.i,1_1mmi.,1_1m.fi,1_1mf.i,1_1mfi.,\
    1_1b.bb,1_1bb.b,1_1bbb.,1_1m.bb,1_1mb.b,1_1mbb.,1_1m.ib,1_1mi.b,1_1mib.,\
    1_1m.mb,1_1mm.b,1_1mmb.,1_1m.fb,1_1mf.b,1_1mfb.,1_1m.lx,1_1mlx."
   "1_stop")

;; M and I instruction is dispersed to the lowest numbered M or I unit
;; not already in use.  An I slot in the 3rd position of 2nd bundle is
;; always dispersed to I1
(final_presence_set "1_um1" "1_um0")
(final_presence_set "1_ui1" "1_ui0, 1_1mii., 1_1mmi., 1_1mfi.")

;; Insns

;; M and I instruction is dispersed to the lowest numbered M or I unit
;; not already in use.  An I slot in the 3rd position of 2nd bundle is
;; always dispersed to I1
(define_reservation "1_M0"
  "1_0m.ii+1_um0|1_0m.mi+1_um0|1_0mm.i+(1_um0|1_um1)\
   |1_0m.fi+1_um0|1_0m.mf+1_um0|1_0mm.f+1_um1\
   |1_0m.bb+1_um0|1_0m.ib+1_um0|1_0m.mb+1_um0\
   |1_0mm.b+1_um1|1_0m.fb+1_um0|1_0m.lx+1_um0\
   |1_1mm.i+1_um1|1_1mm.b+1_um1\
   |(1_1m.ii|1_1m.mi|1_1m.fi|1_1m.bb|1_1m.ib|1_1m.mb|1_1m.fb|1_1m.lx)\
    +(1_um0|1_um1)")

(define_reservation "1_M1"
  "(1_0mii.+(1_ui0|1_ui1)|1_0mmi.+1_ui0|1_0mfi.+1_ui0\
    |1_0mib.+1_unb0|1_0mfb.+1_unb0|1_0mmb.+1_unb0)\
     +(1_1m.ii|1_1m.mi|1_1m.fi|1_1m.bb|1_1m.ib|1_1m.mb|1_1m.fb|1_1m.lx)\
     +(1_um0|1_um1)")

(define_reservation "1_M" "1_M0|1_M1")

;;  Exceptions for dispersal rules.
;; "An I slot in the 3rd position of 2nd bundle is always dispersed to I1".
(define_reservation "1_I0"
  "1_0mi.i+1_ui0|1_0mii.+(1_ui0|1_ui1)|1_0mmi.+1_ui0|1_0mfi.+1_ui0\
   |1_0mi.b+1_ui0|(1_1mi.i|1_1mi.b)+(1_ui0|1_ui1)\
   |1_1mii.+1_ui1|1_1mmi.+1_ui1|1_1mfi.+1_ui1")

(define_reservation "1_I1"
  "1_0m.ii+1_um0+1_0mi.i+1_ui0|1_0mm.i+(1_um0|1_um1)+1_0mmi.+1_ui0\
   |1_0mf.i+1_uf0+1_0mfi.+1_ui0|1_0m.ib+1_um0+1_0mi.b+1_ui0\
   |(1_1m.ii+(1_um0|1_um1)+1_1mi.i\
   |1_1m.ib+(1_um0|1_um1)+1_1mi.b)+(1_ui0|1_ui1)\
   |1_1mm.i+1_um1+1_1mmi.+1_ui1|1_1mf.i+1_uf1+1_1mfi.+1_ui1")

(define_reservation "1_I" "1_I0|1_I1")

;; "An F slot in the 1st bundle disperses to F0".
;; "An F slot in the 2st bundle disperses to F1".
(define_reservation "1_F0"
   "1_0mf.i+1_uf0|1_0mmf.+1_uf0|1_0mf.b+1_uf0|1_1mf.i+1_uf1|1_1mf.b+1_uf1")

(define_reservation "1_F1"
   "1_0m.fi+1_um0+1_0mf.i+1_uf0|1_0mm.f+(1_um0|1_um1)+1_0mmf.+1_uf0\
    |1_0m.fb+1_um0+1_0mf.b+1_uf0|1_1m.fi+(1_um0|1_um1)+1_1mf.i+1_uf1\
    |1_1m.fb+(1_um0|1_um1)+1_1mf.b+1_uf1")

(define_reservation "1_F2"
   "1_0m.mf+1_um0+1_0mm.f+1_um1+1_0mmf.+1_uf0\
    |(1_0mii.+(1_ui0|1_ui1)|1_0mmi.+1_ui0|1_0mfi.+1_ui0\
      |1_0mib.+1_unb0|1_0mmb.+1_unb0|1_0mfb.+1_unb0)\
     +(1_1m.fi+(1_um0|1_um1)+1_1mf.i+1_uf1\
       |1_1m.fb+(1_um0|1_um1)+1_1mf.b+1_uf1)")

(define_reservation "1_F" "1_F0|1_F1|1_F2")

;;; "Each B slot in MBB or BBB bundle disperses to the corresponding B
;;; unit. That is, a B slot in 1st position is dispersed to B0.  In the
;;; 2nd position it is dispersed to B2".
(define_reservation "1_NB"
    "1_0b.bb+1_unb0|1_0bb.b+1_unb1|1_0bbb.+1_unb2\
     |1_0mb.b+1_unb1|1_0mbb.+1_unb2\
     |1_0mib.+1_unb0|1_0mmb.+1_unb0|1_0mfb.+1_unb0\
     |1_1b.bb+1_unb0|1_1bb.b+1_unb1\
     |1_1bbb.+1_unb2|1_1mb.b+1_unb1|1_1mbb.+1_unb2|1_1mib.+1_unb0\
     |1_1mmb.+1_unb0|1_1mfb.+1_unb0")

(define_reservation "1_B0"
   "1_0b.bb+1_ub0|1_0bb.b+1_ub1|1_0bbb.+1_ub2\
    |1_0mb.b+1_ub1|1_0mbb.+1_ub2|1_0mib.+1_ub2\
    |1_0mfb.+1_ub2|1_1b.bb+1_ub0|1_1bb.b+1_ub1\
    |1_1bbb.+1_ub2|1_1mb.b+1_ub1\
    |1_1mib.+1_ub2|1_1mmb.+1_ub2|1_1mfb.+1_ub2")

(define_reservation "1_B1"
   "1_0m.bb+1_um0+1_0mb.b+1_ub1|1_0mi.b+1_ui0+1_0mib.+1_ub2\
    |1_0mf.b+1_uf0+1_0mfb.+1_ub2\
    |(1_0mii.+(1_ui0|1_ui1)|1_0mmi.+1_ui0|1_0mfi.+1_ui0)+1_1b.bb+1_ub0\
    |1_1m.bb+(1_um0|1_um1)+1_1mb.b+1_ub1\
    |1_1mi.b+(1_ui0|1_ui1)+1_1mib.+1_ub2\
    |1_1mm.b+1_um1+1_1mmb.+1_ub2\
    |1_1mf.b+1_uf1+1_1mfb.+1_ub2")

(define_reservation "1_B" "1_B0|1_B1")

;; MLX bunlde uses ports equivalent to MFI bundles.
(define_reservation "1_L0" "1_0mlx.+1_ui0+1_uf0|1_1mlx.+(1_ui0|1_ui1)+1_uf1")
(define_reservation "1_L1"
   "1_0m.lx+1_um0+1_0mlx.+1_ui0+1_uf0\
   |1_1m.lx+(1_um0|1_um1)+1_1mlx.+(1_ui0|1_ui1)+1_uf1")
(define_reservation "1_L2"
   "(1_0mii.+(1_ui0|1_ui1)|1_0mmi.+1_ui0|1_0mfi.+1_ui0\
     |1_0mib.+1_unb0|1_0mmb.+1_unb0|1_0mfb.+1_unb0)
    +1_1m.lx+(1_um0|1_um1)+1_1mlx.+1_ui1+1_uf1")
(define_reservation "1_L" "1_L0|1_L1|1_L2")

(define_reservation "1_A" "1_M|1_I")

(define_insn_reservation "1_stop_bit" 0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "stop_bit"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_stop|1_m0_stop|1_m1_stop|1_mi0_stop|1_mi1_stop")

(define_insn_reservation "1_br"      0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "br"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_B")
(define_insn_reservation "1_scall"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "scall"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_B")
(define_insn_reservation "1_fcmp"    2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "fcmp"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_F+1_not_uf1")
(define_insn_reservation "1_fcvtfx"  7
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "fcvtfx"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_F")

(define_insn_reservation "1_fld"     9
  (and (and (and (eq_attr "cpu" "itanium")
		 (eq_attr "itanium_class" "fld"))
	    (eq_attr "check_load" "no"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")
(define_insn_reservation "1_fldc"    0
  (and (and (and (eq_attr "cpu" "itanium")
		 (eq_attr "itanium_class" "fld"))
	    (eq_attr "check_load" "yes"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")

(define_insn_reservation "1_fldp"    9
  (and (and (and (eq_attr "cpu" "itanium")
		 (eq_attr "itanium_class" "fldp"))
	    (eq_attr "check_load" "no"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")
(define_insn_reservation "1_fldpc"   0
  (and (and (and (eq_attr "cpu" "itanium")
		 (eq_attr "itanium_class" "fldp"))
	    (eq_attr "check_load" "yes"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")

(define_insn_reservation "1_fmac"    5
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "fmac"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_F")
(define_insn_reservation "1_fmisc"   5
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "fmisc"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_F+1_not_uf1")

;; There is only one insn `mov = ar.bsp' for frar_i:
(define_insn_reservation "1_frar_i" 13
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "frar_i"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_I+1_not_ui1")
;; There is only two insns `mov = ar.unat' or `mov = ar.ccv' for frar_m:
(define_insn_reservation "1_frar_m"  6
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "frar_m"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_M+1_not_um1")
(define_insn_reservation "1_frbr"    2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "frbr"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_I+1_not_ui1")
(define_insn_reservation "1_frfr"    2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "frfr"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_M+1_not_um1")
(define_insn_reservation "1_frpr"    2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "frpr"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_I+1_not_ui1")

(define_insn_reservation "1_ialu"      1
    (and (and (eq_attr "cpu" "itanium")
              (eq_attr "itanium_class" "ialu"))
         (eq (symbol_ref
              "bundling_p || ia64_produce_address_p (insn)")
              (const_int 0)))
    "1_A")
(define_insn_reservation "1_ialu_addr" 1
    (and (and (eq_attr "cpu" "itanium")
              (eq_attr "itanium_class" "ialu"))
         (eq (symbol_ref
              "!bundling_p && ia64_produce_address_p (insn)")
             (const_int 1)))
    "1_M")
(define_insn_reservation "1_icmp"    1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "icmp"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_A")
(define_insn_reservation "1_ilog"    1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "ilog"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_A")
(define_insn_reservation "1_mmalua" 2
    (and (and (eq_attr "cpu" "itanium")
              (eq_attr "itanium_class" "mmalua"))
         (eq (symbol_ref "bundling_p") (const_int 0)))
    "1_A")
(define_insn_reservation "1_ishf"    1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "ishf"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
    "1_I+1_not_ui1")
(define_insn_reservation "1_ld"      2
  (and (and (and (eq_attr "cpu" "itanium")
		 (eq_attr "itanium_class" "ld"))
	    (eq_attr "check_load" "no"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")
(define_insn_reservation "1_ldc"     0
  (and (and (and (eq_attr "cpu" "itanium")
		 (eq_attr "itanium_class" "ld"))
	    (eq_attr "check_load" "yes"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")
(define_insn_reservation "1_long_i"  1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "long_i"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_L")
(define_insn_reservation "1_mmmul"   2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "mmmul"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_I+1_not_ui1")
(define_insn_reservation "1_mmshf"   2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "mmshf"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_I")
(define_insn_reservation "1_mmshfi"  1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "mmshfi"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_I")

;; Now we have only one insn (flushrs) of such class.  We assume that flushrs
;; is the 1st syllable of the bundle after stop bit.
(define_insn_reservation "1_rse_m"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "rse_m"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "(1_0m.ii|1_0m.mi|1_0m.fi|1_0m.mf|1_0b.bb|1_0m.bb\
    |1_0m.ib|1_0m.mb|1_0m.fb|1_0m.lx)+1_um0")
(define_insn_reservation "1_sem"     0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "sem"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_M+1_not_um1")
(define_insn_reservation "1_stf"     1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "stf"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")
(define_insn_reservation "1_st"      1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "st"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")
(define_insn_reservation "1_syst_m0" 0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "syst_m0"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_M+1_not_um1")
(define_insn_reservation "1_syst_m"  0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "syst_m"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")
(define_insn_reservation "1_tbit"    1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "tbit"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_I+1_not_ui1")

;; There is only ony insn `mov ar.pfs =' for toar_i:
(define_insn_reservation "1_toar_i"  0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "toar_i"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_I+1_not_ui1")
;; There are only ony 2 insns `mov ar.ccv =' and `mov ar.unat =' for toar_m:
(define_insn_reservation "1_toar_m"  5
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "toar_m"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_M+1_not_um1")
(define_insn_reservation "1_tobr"    1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "tobr"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_I+1_not_ui1")
(define_insn_reservation "1_tofr"    9
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "tofr"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")
(define_insn_reservation "1_topr"    1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "topr"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_I+1_not_ui1")
(define_insn_reservation "1_xmpy"    7
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "xmpy"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_F")
(define_insn_reservation "1_xtd"     1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "xtd"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_I")

(define_insn_reservation "1_chk_s_i" 0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "chk_s_i"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_A")
(define_insn_reservation "1_chk_s_f" 0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "chk_s_f"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")
(define_insn_reservation "1_chk_a"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "chk_a"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")

(define_insn_reservation "1_lfetch"  0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "lfetch"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M")

(define_insn_reservation "1_nop_m"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "nop_m"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_M0")
(define_insn_reservation "1_nop_b"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "nop_b"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_NB")
(define_insn_reservation "1_nop_i"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "nop_i"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_I0")
(define_insn_reservation "1_nop_f"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "nop_f"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_F0")
(define_insn_reservation "1_nop_x"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "nop_x"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_L0")

;; We assume that there is no insn issued on the same cycle as unknown insn.
(define_cpu_unit "1_empty" "one")
(exclusion_set "1_empty"
    "1_0m.ii,1_0m.mi,1_0m.fi,1_0m.mf,1_0b.bb,1_0m.bb,1_0m.ib,1_0m.mb,1_0m.fb,\
     1_0m.lx")

(define_insn_reservation "1_unknown" 1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "unknown"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "1_empty")

(define_insn_reservation "1_nop" 1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "nop"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "1_M0|1_NB|1_I0|1_F0")

(define_insn_reservation "1_ignore" 0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "ignore"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "nothing")


(define_cpu_unit
   "1_0m_bs, 1_0mi_bs, 1_0mm_bs, 1_0mf_bs, 1_0b_bs, 1_0bb_bs, 1_0mb_bs"
   "one")
(define_cpu_unit
   "1_1m_bs, 1_1mi_bs, 1_1mm_bs, 1_1mf_bs, 1_1b_bs, 1_1bb_bs, 1_1mb_bs"
   "one")

(define_cpu_unit "1_m_cont, 1_mi_cont, 1_mm_cont, 1_mf_cont, 1_mb_cont,\
	          1_b_cont, 1_bb_cont" "one")

;; For stop in the middle of the bundles.
(define_cpu_unit "1_m_stop, 1_m0_stop, 1_m1_stop, 1_0mmi_cont" "one")
(define_cpu_unit "1_mi_stop, 1_mi0_stop, 1_mi1_stop, 1_0mii_cont" "one")

(final_presence_set "1_0m_bs"
   "1_0m.ii, 1_0m.mi, 1_0m.mf, 1_0m.fi, 1_0m.bb,\
    1_0m.ib, 1_0m.fb, 1_0m.mb, 1_0m.lx")
(final_presence_set "1_1m_bs"
   "1_1m.ii, 1_1m.mi, 1_1m.fi, 1_1m.bb, 1_1m.ib, 1_1m.fb, 1_1m.mb,\
    1_1m.lx")
(final_presence_set "1_0mi_bs"  "1_0mi.i, 1_0mi.i")
(final_presence_set "1_1mi_bs"  "1_1mi.i, 1_1mi.i")
(final_presence_set "1_0mm_bs"  "1_0mm.i, 1_0mm.f, 1_0mm.b")
(final_presence_set "1_1mm_bs"  "1_1mm.i, 1_1mm.b")
(final_presence_set "1_0mf_bs"  "1_0mf.i, 1_0mf.b")
(final_presence_set "1_1mf_bs"  "1_1mf.i, 1_1mf.b")
(final_presence_set "1_0b_bs"  "1_0b.bb")
(final_presence_set "1_1b_bs"  "1_1b.bb")
(final_presence_set "1_0bb_bs"  "1_0bb.b")
(final_presence_set "1_1bb_bs"  "1_1bb.b")
(final_presence_set "1_0mb_bs"  "1_0mb.b")
(final_presence_set "1_1mb_bs"  "1_1mb.b")

(exclusion_set "1_0m_bs"
   "1_0mi.i, 1_0mm.i, 1_0mm.f, 1_0mf.i, 1_0mb.b,\
    1_0mi.b, 1_0mf.b, 1_0mm.b, 1_0mlx., 1_m0_stop")
(exclusion_set "1_1m_bs"
   "1_1mi.i, 1_1mm.i, 1_1mf.i, 1_1mb.b, 1_1mi.b, 1_1mf.b, 1_1mm.b,\
    1_1mlx., 1_m1_stop")
(exclusion_set "1_0mi_bs"  "1_0mii., 1_0mib., 1_mi0_stop")
(exclusion_set "1_1mi_bs"  "1_1mii., 1_1mib., 1_mi1_stop")
(exclusion_set "1_0mm_bs"  "1_0mmi., 1_0mmf., 1_0mmb.")
(exclusion_set "1_1mm_bs"  "1_1mmi., 1_1mmb.")
(exclusion_set "1_0mf_bs"  "1_0mfi., 1_0mfb.")
(exclusion_set "1_1mf_bs"  "1_1mfi., 1_1mfb.")
(exclusion_set "1_0b_bs"  "1_0bb.b")
(exclusion_set "1_1b_bs"  "1_1bb.b")
(exclusion_set "1_0bb_bs"  "1_0bbb.")
(exclusion_set "1_1bb_bs"  "1_1bbb.")
(exclusion_set "1_0mb_bs"  "1_0mbb.")
(exclusion_set "1_1mb_bs"  "1_1mbb.")

(exclusion_set
   "1_0m_bs, 1_0mi_bs, 1_0mm_bs, 1_0mf_bs, 1_0b_bs, 1_0bb_bs, 1_0mb_bs,
    1_1m_bs, 1_1mi_bs, 1_1mm_bs, 1_1mf_bs, 1_1b_bs, 1_1bb_bs, 1_1mb_bs"
   "1_stop")

(final_presence_set
   "1_0mi.i, 1_0mm.i, 1_0mf.i, 1_0mm.f, 1_0mb.b,\
    1_0mi.b, 1_0mm.b, 1_0mf.b, 1_0mlx."
   "1_m_cont")
(final_presence_set "1_0mii., 1_0mib." "1_mi_cont")
(final_presence_set "1_0mmi., 1_0mmf., 1_0mmb." "1_mm_cont")
(final_presence_set "1_0mfi., 1_0mfb." "1_mf_cont")
(final_presence_set "1_0bb.b" "1_b_cont")
(final_presence_set "1_0bbb." "1_bb_cont")
(final_presence_set "1_0mbb." "1_mb_cont")

(exclusion_set
   "1_0m.ii, 1_0m.mi, 1_0m.fi, 1_0m.mf, 1_0b.bb, 1_0m.bb,\
    1_0m.ib, 1_0m.mb, 1_0m.fb, 1_0m.lx"
   "1_m_cont, 1_mi_cont, 1_mm_cont, 1_mf_cont,\
    1_mb_cont, 1_b_cont, 1_bb_cont")

(exclusion_set "1_empty"
               "1_m_cont,1_mi_cont,1_mm_cont,1_mf_cont,\
                1_mb_cont,1_b_cont,1_bb_cont")

;; For m;mi bundle
(final_presence_set "1_m0_stop" "1_0m.mi")
(final_presence_set "1_0mm.i" "1_0mmi_cont")
(exclusion_set "1_0mmi_cont"
   "1_0m.ii, 1_0m.mi, 1_0m.fi, 1_0m.mf, 1_0b.bb, 1_0m.bb,\
    1_0m.ib, 1_0m.mb, 1_0m.fb, 1_0m.lx")
(exclusion_set "1_m0_stop" "1_0mm.i")
(final_presence_set "1_m1_stop" "1_1m.mi")
(exclusion_set "1_m1_stop" "1_1mm.i")
(final_presence_set "1_m_stop" "1_m0_stop, 1_m1_stop")

;; For mi;i bundle
(final_presence_set "1_mi0_stop" "1_0mi.i")
(final_presence_set "1_0mii." "1_0mii_cont")
(exclusion_set "1_0mii_cont"
   "1_0m.ii, 1_0m.mi, 1_0m.fi, 1_0m.mf, 1_0b.bb, 1_0m.bb,\
    1_0m.ib, 1_0m.mb, 1_0m.fb, 1_0m.lx")
(exclusion_set "1_mi0_stop" "1_0mii.")
(final_presence_set "1_mi1_stop" "1_1mi.i")
(exclusion_set "1_mi1_stop" "1_1mii.")
(final_presence_set "1_mi_stop" "1_mi0_stop, 1_mi1_stop")

(final_absence_set
   "1_0m.ii,1_0mi.i,1_0mii.,1_0m.mi,1_0mm.i,1_0mmi.,1_0m.fi,1_0mf.i,1_0mfi.,\
    1_0m.mf,1_0mm.f,1_0mmf.,1_0b.bb,1_0bb.b,1_0bbb.,1_0m.bb,1_0mb.b,1_0mbb.,\
    1_0m.ib,1_0mi.b,1_0mib.,1_0m.mb,1_0mm.b,1_0mmb.,1_0m.fb,1_0mf.b,1_0mfb.,\
    1_0m.lx,1_0mlx., \
    1_1m.ii,1_1mi.i,1_1mii.,1_1m.mi,1_1mm.i,1_1mmi.,1_1m.fi,1_1mf.i,1_1mfi.,\
    1_1b.bb,1_1bb.b,1_1bbb.,1_1m.bb,1_1mb.b,1_1mbb.,\
    1_1m.ib,1_1mi.b,1_1mib.,1_1m.mb,1_1mm.b,1_1mmb.,1_1m.fb,1_1mf.b,1_1mfb.,\
    1_1m.lx,1_1mlx."
   "1_m0_stop,1_m1_stop,1_mi0_stop,1_mi1_stop")

(define_cpu_unit "1_m_cont_only, 1_b_cont_only" "one")
(define_cpu_unit "1_mi_cont_only, 1_mm_cont_only, 1_mf_cont_only" "one")
(define_cpu_unit "1_mb_cont_only, 1_bb_cont_only" "one")

(final_presence_set "1_m_cont_only" "1_m_cont")
(exclusion_set "1_m_cont_only"
  "1_0mi.i, 1_0mm.i, 1_0mf.i, 1_0mm.f, 1_0mb.b,\
   1_0mi.b, 1_0mm.b, 1_0mf.b, 1_0mlx.")

(final_presence_set "1_b_cont_only" "1_b_cont")
(exclusion_set "1_b_cont_only"  "1_0bb.b")

(final_presence_set "1_mi_cont_only" "1_mi_cont")
(exclusion_set "1_mi_cont_only" "1_0mii., 1_0mib.")

(final_presence_set "1_mm_cont_only" "1_mm_cont")
(exclusion_set "1_mm_cont_only" "1_0mmi., 1_0mmf., 1_0mmb.")

(final_presence_set "1_mf_cont_only" "1_mf_cont")
(exclusion_set "1_mf_cont_only" "1_0mfi., 1_0mfb.")

(final_presence_set "1_mb_cont_only" "1_mb_cont")
(exclusion_set "1_mb_cont_only" "1_0mbb.")

(final_presence_set "1_bb_cont_only" "1_bb_cont")
(exclusion_set "1_bb_cont_only" "1_0bbb.")

(define_insn_reservation "1_pre_cycle" 0
   (and (and (eq_attr "cpu" "itanium")
             (eq_attr "itanium_class" "pre_cycle"))
        (eq (symbol_ref "bundling_p") (const_int 0)))
                         "(1_0m_bs, 1_m_cont)                     \
                          | (1_0mi_bs, (1_mi_cont|nothing))       \
                          | (1_0mm_bs, 1_mm_cont)                 \
                          | (1_0mf_bs, (1_mf_cont|nothing))       \
                          | (1_0b_bs, (1_b_cont|nothing))         \
                          | (1_0bb_bs, (1_bb_cont|nothing))       \
                          | (1_0mb_bs, (1_mb_cont|nothing))       \
                          | (1_1m_bs, 1_m_cont)                   \
                          | (1_1mi_bs, (1_mi_cont|nothing))       \
                          | (1_1mm_bs, 1_mm_cont)                 \
                          | (1_1mf_bs, (1_mf_cont|nothing))       \
                          | (1_1b_bs, (1_b_cont|nothing))         \
                          | (1_1bb_bs, (1_bb_cont|nothing))       \
                          | (1_1mb_bs, (1_mb_cont|nothing))       \
                          | (1_m_cont_only, (1_m_cont|nothing))   \
                          | (1_b_cont_only,  (1_b_cont|nothing))  \
                          | (1_mi_cont_only, (1_mi_cont|nothing)) \
                          | (1_mm_cont_only, (1_mm_cont|nothing)) \
                          | (1_mf_cont_only, (1_mf_cont|nothing)) \
                          | (1_mb_cont_only, (1_mb_cont|nothing)) \
                          | (1_bb_cont_only, (1_bb_cont|nothing)) \
                          | (1_m_stop, (1_0mmi_cont|nothing))     \
                          | (1_mi_stop, (1_0mii_cont|nothing))")

;; Bypasses:
(define_bypass  1 "1_fcmp" "1_br,1_scall")
;; ??? I found 7 cycle delay for 1_fmac -> 1_fcmp for Itanium1
(define_bypass  7 "1_fmac" "1_fmisc,1_fcvtfx,1_xmpy,1_fcmp")

;; ???
(define_bypass  3 "1_frbr" "1_mmmul,1_mmshf")
(define_bypass 14 "1_frar_i" "1_mmmul,1_mmshf")
(define_bypass  7 "1_frar_m" "1_mmmul,1_mmshf")

;; ????
;; There is only one insn `mov ar.pfs =' for toar_i.
(define_bypass  0 "1_tobr,1_topr,1_toar_i" "1_br,1_scall")

(define_bypass  3 "1_ialu,1_ialu_addr" "1_mmmul,1_mmshf,1_mmalua")
;; ??? howto describe ialu for I slot only.  We use ialu_addr for that
;;(define_bypass  2 "1_ialu" "1_ld"  "ia64_ld_address_bypass_p")
;; ??? howto describe ialu st/address for I slot only.  We use ialu_addr
;;   for that.
;;(define_bypass  2 "1_ialu" "1_st"  "ia64_st_address_bypass_p")

(define_bypass  0 "1_icmp" "1_br,1_scall")

(define_bypass  3 "1_ilog" "1_mmmul,1_mmshf")

(define_bypass  2 "1_ilog,1_xtd" "1_ld"  "ia64_ld_address_bypass_p")
(define_bypass  2 "1_ilog,1_xtd" "1_st"  "ia64_st_address_bypass_p")

(define_bypass  3 "1_ld,1_ldc" "1_mmmul,1_mmshf")
(define_bypass  3 "1_ld" "1_ld"  "ia64_ld_address_bypass_p")
(define_bypass  3 "1_ld" "1_st"  "ia64_st_address_bypass_p")

;; Intel docs say only LD, ST, IALU, ILOG, ISHF consumers have latency 4,
;;      but HP engineers say any non-MM operation.
(define_bypass  4 "1_mmmul,1_mmshf,1_mmalua"
     "1_br,1_fcmp,1_fcvtfx,1_fld,1_fldc,1_fmac,1_fmisc,1_frar_i,1_frar_m,\
      1_frbr,1_frfr,1_frpr,1_ialu,1_icmp,1_ilog,1_ishf,1_ld,1_ldc,1_chk_s_i,1_chk_s_f,1_chk_a,\
      1_long_i,1_rse_m,1_sem,1_stf,1_st,1_syst_m0,1_syst_m,\
      1_tbit,1_toar_i,1_toar_m,1_tobr,1_tofr,1_topr,1_xmpy,1_xtd")

;; ??? how to describe that if scheduled < 4 cycle then latency is 10 cycles.
;; (define_bypass  10 "1_mmmul,1_mmshf" "1_ialu,1_ilog,1_ishf,1_st,1_ld")

(define_bypass  0 "1_tbit" "1_br,1_scall")

(define_bypass  8 "1_tofr"  "1_frfr,1_stf")
(define_bypass  7 "1_fmisc,1_fcvtfx,1_fmac,1_xmpy"  "1_frfr")
(define_bypass  8 "1_fmisc,1_fcvtfx,1_fmac,1_xmpy"  "1_stf")

;; We don't use here fcmp because scall may be predicated.
(define_bypass  0 "1_fcvtfx,1_fld,1_fldc,1_fmac,1_fmisc,1_frar_i,1_frar_m,\
                   1_frbr,1_frfr,1_frpr,1_ialu,1_ialu_addr,1_ilog,1_ishf,\
	           1_ld,1_ldc,1_long_i,1_mmalua,1_mmmul,1_mmshf,1_mmshfi,\
                   1_toar_m,1_tofr,1_xmpy,1_xtd" "1_scall")

(define_bypass  0 "1_unknown,1_ignore,1_stop_bit,1_br,1_fcmp,1_fcvtfx,\
                   1_fld,1_fldc,1_fmac,1_fmisc,1_frar_i,1_frar_m,1_frbr,1_frfr,\
                   1_frpr,1_ialu,1_ialu_addr,1_icmp,1_ilog,1_ishf,1_ld,1_ldc,\
                   1_chk_s_i,1_chk_s_f,1_chk_a,1_long_i,1_mmalua,1_mmmul,1_mmshf,1_mmshfi,1_nop,\
                   1_nop_b,1_nop_f,1_nop_i,1_nop_m,1_nop_x,1_rse_m,1_scall,\
                   1_sem,1_stf,1_st,1_syst_m0,1_syst_m,1_tbit,1_toar_i,\
                   1_toar_m,1_tobr,1_tofr,1_topr,1_xmpy,1_xtd,1_lfetch"
                  "1_ignore")


;; Bundling

(define_automaton "oneb")

;; Pseudo units for quicker searching for position in two packet window.  */
(define_query_cpu_unit "1_1,1_2,1_3,1_4,1_5,1_6" "oneb")

;;   All possible combinations of bundles/syllables
(define_cpu_unit
   "1b_0m.ii, 1b_0m.mi, 1b_0m.fi, 1b_0m.mf, 1b_0b.bb, 1b_0m.bb,\
    1b_0m.ib, 1b_0m.mb, 1b_0m.fb, 1b_0m.lx" "oneb")
(define_cpu_unit
   "1b_0mi.i, 1b_0mm.i, 1b_0mf.i, 1b_0mm.f, 1b_0bb.b, 1b_0mb.b,\
    1b_0mi.b, 1b_0mm.b, 1b_0mf.b" "oneb")
(define_query_cpu_unit
   "1b_0mii., 1b_0mmi., 1b_0mfi., 1b_0mmf., 1b_0bbb., 1b_0mbb.,\
    1b_0mib., 1b_0mmb., 1b_0mfb., 1b_0mlx." "oneb")

(define_cpu_unit "1b_1m.ii, 1b_1m.mi, 1b_1m.fi, 1b_1b.bb, 1b_1m.bb,\
                  1b_1m.ib, 1b_1m.mb, 1b_1m.fb, 1b_1m.lx" "oneb")
(define_cpu_unit "1b_1mi.i, 1b_1mm.i, 1b_1mf.i, 1b_1bb.b, 1b_1mb.b,\
                  1b_1mi.b, 1b_1mm.b, 1b_1mf.b" "oneb")
(define_query_cpu_unit "1b_1mii., 1b_1mmi., 1b_1mfi., 1b_1bbb., 1b_1mbb.,\
                        1b_1mib., 1b_1mmb., 1b_1mfb., 1b_1mlx." "oneb")

;; Slot 1
(exclusion_set "1b_0m.ii"
   "1b_0m.mi, 1b_0m.fi, 1b_0m.mf, 1b_0b.bb, 1b_0m.bb,\
    1b_0m.ib, 1b_0m.mb, 1b_0m.fb, 1b_0m.lx")
(exclusion_set "1b_0m.mi"
   "1b_0m.fi, 1b_0m.mf, 1b_0b.bb, 1b_0m.bb, 1b_0m.ib,\
    1b_0m.mb, 1b_0m.fb, 1b_0m.lx")
(exclusion_set "1b_0m.fi"
   "1b_0m.mf, 1b_0b.bb, 1b_0m.bb, 1b_0m.ib, 1b_0m.mb, 1b_0m.fb, 1b_0m.lx")
(exclusion_set "1b_0m.mf"
   "1b_0b.bb, 1b_0m.bb, 1b_0m.ib, 1b_0m.mb, 1b_0m.fb, 1b_0m.lx")
(exclusion_set "1b_0b.bb" "1b_0m.bb, 1b_0m.ib, 1b_0m.mb, 1b_0m.fb, 1b_0m.lx")
(exclusion_set "1b_0m.bb" "1b_0m.ib, 1b_0m.mb, 1b_0m.fb, 1b_0m.lx")
(exclusion_set "1b_0m.ib" "1b_0m.mb, 1b_0m.fb, 1b_0m.lx")
(exclusion_set "1b_0m.mb" "1b_0m.fb, 1b_0m.lx")
(exclusion_set "1b_0m.fb" "1b_0m.lx")

;; Slot 2
(exclusion_set "1b_0mi.i"
   "1b_0mm.i, 1b_0mf.i, 1b_0mm.f, 1b_0bb.b, 1b_0mb.b,\
    1b_0mi.b, 1b_0mm.b, 1b_0mf.b, 1b_0mlx.")
(exclusion_set "1b_0mm.i"
   "1b_0mf.i, 1b_0mm.f, 1b_0bb.b, 1b_0mb.b,\
    1b_0mi.b, 1b_0mm.b, 1b_0mf.b, 1b_0mlx.")
(exclusion_set "1b_0mf.i"
   "1b_0mm.f, 1b_0bb.b, 1b_0mb.b, 1b_0mi.b, 1b_0mm.b, 1b_0mf.b, 1b_0mlx.")
(exclusion_set "1b_0mm.f"
   "1b_0bb.b, 1b_0mb.b, 1b_0mi.b, 1b_0mm.b, 1b_0mf.b, 1b_0mlx.")
(exclusion_set "1b_0bb.b" "1b_0mb.b, 1b_0mi.b, 1b_0mm.b, 1b_0mf.b, 1b_0mlx.")
(exclusion_set "1b_0mb.b" "1b_0mi.b, 1b_0mm.b, 1b_0mf.b, 1b_0mlx.")
(exclusion_set "1b_0mi.b" "1b_0mm.b, 1b_0mf.b, 1b_0mlx.")
(exclusion_set "1b_0mm.b" "1b_0mf.b, 1b_0mlx.")
(exclusion_set "1b_0mf.b" "1b_0mlx.")

;; Slot 3
(exclusion_set "1b_0mii."
   "1b_0mmi., 1b_0mfi., 1b_0mmf., 1b_0bbb., 1b_0mbb.,\
    1b_0mib., 1b_0mmb., 1b_0mfb., 1b_0mlx.")
(exclusion_set "1b_0mmi."
   "1b_0mfi., 1b_0mmf., 1b_0bbb., 1b_0mbb.,\
    1b_0mib., 1b_0mmb., 1b_0mfb., 1b_0mlx.")
(exclusion_set "1b_0mfi."
   "1b_0mmf., 1b_0bbb., 1b_0mbb., 1b_0mib., 1b_0mmb., 1b_0mfb., 1b_0mlx.")
(exclusion_set "1b_0mmf."
   "1b_0bbb., 1b_0mbb., 1b_0mib., 1b_0mmb., 1b_0mfb., 1b_0mlx.")
(exclusion_set "1b_0bbb." "1b_0mbb., 1b_0mib., 1b_0mmb., 1b_0mfb., 1b_0mlx.")
(exclusion_set "1b_0mbb." "1b_0mib., 1b_0mmb., 1b_0mfb., 1b_0mlx.")
(exclusion_set "1b_0mib." "1b_0mmb., 1b_0mfb., 1b_0mlx.")
(exclusion_set "1b_0mmb." "1b_0mfb., 1b_0mlx.")
(exclusion_set "1b_0mfb." "1b_0mlx.")

;; Slot 4
(exclusion_set "1b_1m.ii"
   "1b_1m.mi, 1b_1m.fi, 1b_1b.bb, 1b_1m.bb,\
    1b_1m.ib, 1b_1m.mb, 1b_1m.fb, 1b_1m.lx")
(exclusion_set "1b_1m.mi"
   "1b_1m.fi, 1b_1b.bb, 1b_1m.bb, 1b_1m.ib, 1b_1m.mb, 1b_1m.fb, 1b_1m.lx")
(exclusion_set "1b_1m.fi"
   "1b_1b.bb, 1b_1m.bb, 1b_1m.ib, 1b_1m.mb, 1b_1m.fb, 1b_1m.lx")
(exclusion_set "1b_1b.bb" "1b_1m.bb, 1b_1m.ib, 1b_1m.mb, 1b_1m.fb, 1b_1m.lx")
(exclusion_set "1b_1m.bb" "1b_1m.ib, 1b_1m.mb, 1b_1m.fb, 1b_1m.lx")
(exclusion_set "1b_1m.ib" "1b_1m.mb, 1b_1m.fb, 1b_1m.lx")
(exclusion_set "1b_1m.mb" "1b_1m.fb, 1b_1m.lx")
(exclusion_set "1b_1m.fb" "1b_1m.lx")

;; Slot 5
(exclusion_set "1b_1mi.i"
   "1b_1mm.i, 1b_1mf.i, 1b_1bb.b, 1b_1mb.b,\
    1b_1mi.b, 1b_1mm.b, 1b_1mf.b, 1b_1mlx.")
(exclusion_set "1b_1mm.i"
   "1b_1mf.i, 1b_1bb.b, 1b_1mb.b, 1b_1mi.b, 1b_1mm.b, 1b_1mf.b, 1b_1mlx.")
(exclusion_set "1b_1mf.i"
   "1b_1bb.b, 1b_1mb.b, 1b_1mi.b, 1b_1mm.b, 1b_1mf.b, 1b_1mlx.")
(exclusion_set "1b_1bb.b" "1b_1mb.b, 1b_1mi.b, 1b_1mm.b, 1b_1mf.b, 1b_1mlx.")
(exclusion_set "1b_1mb.b" "1b_1mi.b, 1b_1mm.b, 1b_1mf.b, 1b_1mlx.")
(exclusion_set "1b_1mi.b" "1b_1mm.b, 1b_1mf.b, 1b_1mlx.")
(exclusion_set "1b_1mm.b" "1b_1mf.b, 1b_1mlx.")
(exclusion_set "1b_1mf.b" "1b_1mlx.")

;; Slot 6
(exclusion_set "1b_1mii."
   "1b_1mmi., 1b_1mfi., 1b_1bbb., 1b_1mbb.,\
    1b_1mib., 1b_1mmb., 1b_1mfb., 1b_1mlx.")
(exclusion_set "1b_1mmi."
   "1b_1mfi., 1b_1bbb., 1b_1mbb., 1b_1mib., 1b_1mmb., 1b_1mfb., 1b_1mlx.")
(exclusion_set "1b_1mfi."
   "1b_1bbb., 1b_1mbb., 1b_1mib., 1b_1mmb., 1b_1mfb., 1b_1mlx.")
(exclusion_set "1b_1bbb." "1b_1mbb., 1b_1mib., 1b_1mmb., 1b_1mfb., 1b_1mlx.")
(exclusion_set "1b_1mbb." "1b_1mib., 1b_1mmb., 1b_1mfb., 1b_1mlx.")
(exclusion_set "1b_1mib." "1b_1mmb., 1b_1mfb., 1b_1mlx.")
(exclusion_set "1b_1mmb." "1b_1mfb., 1b_1mlx.")
(exclusion_set "1b_1mfb." "1b_1mlx.")

(final_presence_set "1b_0mi.i" "1b_0m.ii")
(final_presence_set "1b_0mii." "1b_0mi.i")
(final_presence_set "1b_1mi.i" "1b_1m.ii")
(final_presence_set "1b_1mii." "1b_1mi.i")

(final_presence_set "1b_0mm.i" "1b_0m.mi")
(final_presence_set "1b_0mmi." "1b_0mm.i")
(final_presence_set "1b_1mm.i" "1b_1m.mi")
(final_presence_set "1b_1mmi." "1b_1mm.i")

(final_presence_set "1b_0mf.i" "1b_0m.fi")
(final_presence_set "1b_0mfi." "1b_0mf.i")
(final_presence_set "1b_1mf.i" "1b_1m.fi")
(final_presence_set "1b_1mfi." "1b_1mf.i")

(final_presence_set "1b_0mm.f" "1b_0m.mf")
(final_presence_set "1b_0mmf." "1b_0mm.f")

(final_presence_set "1b_0bb.b" "1b_0b.bb")
(final_presence_set "1b_0bbb." "1b_0bb.b")
(final_presence_set "1b_1bb.b" "1b_1b.bb")
(final_presence_set "1b_1bbb." "1b_1bb.b")

(final_presence_set "1b_0mb.b" "1b_0m.bb")
(final_presence_set "1b_0mbb." "1b_0mb.b")
(final_presence_set "1b_1mb.b" "1b_1m.bb")
(final_presence_set "1b_1mbb." "1b_1mb.b")

(final_presence_set "1b_0mi.b" "1b_0m.ib")
(final_presence_set "1b_0mib." "1b_0mi.b")
(final_presence_set "1b_1mi.b" "1b_1m.ib")
(final_presence_set "1b_1mib." "1b_1mi.b")

(final_presence_set "1b_0mm.b" "1b_0m.mb")
(final_presence_set "1b_0mmb." "1b_0mm.b")
(final_presence_set "1b_1mm.b" "1b_1m.mb")
(final_presence_set "1b_1mmb." "1b_1mm.b")

(final_presence_set "1b_0mf.b" "1b_0m.fb")
(final_presence_set "1b_0mfb." "1b_0mf.b")
(final_presence_set "1b_1mf.b" "1b_1m.fb")
(final_presence_set "1b_1mfb." "1b_1mf.b")

(final_presence_set "1b_0mlx." "1b_0m.lx")
(final_presence_set "1b_1mlx." "1b_1m.lx")

(final_presence_set
   "1b_1m.ii,1b_1m.mi,1b_1m.fi,1b_1b.bb,1b_1m.bb,\
    1b_1m.ib,1b_1m.mb,1b_1m.fb,1b_1m.lx"
   "1b_0mii.,1b_0mmi.,1b_0mfi.,1b_0mmf.,1b_0bbb.,1b_0mbb.,\
    1b_0mib.,1b_0mmb.,1b_0mfb.,1b_0mlx.")

;;  Microarchitecture units:
(define_cpu_unit
   "1b_um0, 1b_um1, 1b_ui0, 1b_ui1, 1b_uf0, 1b_uf1, 1b_ub0, 1b_ub1, 1b_ub2,\
    1b_unb0, 1b_unb1, 1b_unb2" "oneb")

(exclusion_set "1b_ub0" "1b_unb0")
(exclusion_set "1b_ub1" "1b_unb1")
(exclusion_set "1b_ub2" "1b_unb2")

;; The following rules are used to decrease number of alternatives.
;; They are consequences of Itanium microarchitecture.  They also
;; describe the following rules mentioned in Itanium
;; microarchitecture: rules mentioned in Itanium microarchitecture:
;; o "MMF: Always splits issue before the first M and after F regardless
;;   of surrounding bundles and stops".
;; o "BBB/MBB: Always splits issue after either of these bundles".
;; o "MIB BBB: Split issue after the first bundle in this pair".

(exclusion_set "1b_0m.mf,1b_0mm.f,1b_0mmf."
   "1b_1m.ii,1b_1m.mi,1b_1m.fi,1b_1b.bb,1b_1m.bb,\
    1b_1m.ib,1b_1m.mb,1b_1m.fb,1b_1m.lx")
(exclusion_set "1b_0b.bb,1b_0bb.b,1b_0bbb.,1b_0m.bb,1b_0mb.b,1b_0mbb."
   "1b_1m.ii,1b_1m.mi,1b_1m.fi,1b_1b.bb,1b_1m.bb,\
    1b_1m.ib,1b_1m.mb,1b_1m.fb,1b_1m.lx")
(exclusion_set "1b_0m.ib,1b_0mi.b,1b_0mib." "1b_1b.bb")

;;  For exceptions of M, I, B, F insns:
(define_cpu_unit "1b_not_um1, 1b_not_ui1, 1b_not_uf1" "oneb")

(final_absence_set "1b_not_um1"  "1b_um1")
(final_absence_set "1b_not_ui1"  "1b_ui1")
(final_absence_set "1b_not_uf1"  "1b_uf1")

;;; "MIB/MFB/MMB: Splits issue after any of these bundles unless the
;;; B-slot contains a nop.b or a brp instruction".
;;;   "The B in an MIB/MFB/MMB bundle disperses to B0 if it is a brp or
;;; nop.b, otherwise it disperses to B2".
(final_absence_set
   "1b_1m.ii, 1b_1m.mi, 1b_1m.fi, 1b_1b.bb, 1b_1m.bb,\
    1b_1m.ib, 1b_1m.mb, 1b_1m.fb, 1b_1m.lx"
   "1b_0mib. 1b_ub2, 1b_0mfb. 1b_ub2, 1b_0mmb. 1b_ub2")

;; This is necessary to start new processor cycle when we meet stop bit.
(define_cpu_unit "1b_stop" "oneb")
(final_absence_set
   "1b_0m.ii,1b_0mi.i,1b_0mii.,1b_0m.mi,1b_0mm.i,1b_0mmi.,\
    1b_0m.fi,1b_0mf.i,1b_0mfi.,\
    1b_0m.mf,1b_0mm.f,1b_0mmf.,1b_0b.bb,1b_0bb.b,1b_0bbb.,\
    1b_0m.bb,1b_0mb.b,1b_0mbb.,\
    1b_0m.ib,1b_0mi.b,1b_0mib.,1b_0m.mb,1b_0mm.b,1b_0mmb.,\
    1b_0m.fb,1b_0mf.b,1b_0mfb.,1b_0m.lx,1b_0mlx., \
    1b_1m.ii,1b_1mi.i,1b_1mii.,1b_1m.mi,1b_1mm.i,1b_1mmi.,\
    1b_1m.fi,1b_1mf.i,1b_1mfi.,\
    1b_1b.bb,1b_1bb.b,1b_1bbb.,1b_1m.bb,1b_1mb.b,1b_1mbb.,\
    1b_1m.ib,1b_1mi.b,1b_1mib.,\
    1b_1m.mb,1b_1mm.b,1b_1mmb.,1b_1m.fb,1b_1mf.b,1b_1mfb.,1b_1m.lx,1b_1mlx."
   "1b_stop")

;; M and I instruction is dispersed to the lowest numbered M or I unit
;; not already in use.  An I slot in the 3rd position of 2nd bundle is
;; always dispersed to I1
(final_presence_set "1b_um1" "1b_um0")
(final_presence_set "1b_ui1" "1b_ui0, 1b_1mii., 1b_1mmi., 1b_1mfi.")

;; Insns

;; M and I instruction is dispersed to the lowest numbered M or I unit
;; not already in use.  An I slot in the 3rd position of 2nd bundle is
;; always dispersed to I1
(define_reservation "1b_M"
  "1b_0m.ii+1_1+1b_um0|1b_0m.mi+1_1+1b_um0|1b_0mm.i+1_2+(1b_um0|1b_um1)\
   |1b_0m.fi+1_1+1b_um0|1b_0m.mf+1_1+1b_um0|1b_0mm.f+1_2+1b_um1\
   |1b_0m.bb+1_1+1b_um0|1b_0m.ib+1_1+1b_um0|1b_0m.mb+1_1+1b_um0\
   |1b_0mm.b+1_2+1b_um1|1b_0m.fb+1_1+1b_um0|1b_0m.lx+1_1+1b_um0\
   |1b_1mm.i+1_5+1b_um1|1b_1mm.b+1_5+1b_um1\
   |(1b_1m.ii+1_4|1b_1m.mi+1_4|1b_1m.fi+1_4|1b_1m.bb+1_4|1b_1m.ib+1_4\
     |1b_1m.mb+1_4|1b_1m.fb+1_4|1b_1m.lx+1_4)\
    +(1b_um0|1b_um1)")

;;  Exceptions for dispersal rules.
;; "An I slot in the 3rd position of 2nd bundle is always dispersed to I1".
(define_reservation "1b_I"
  "1b_0mi.i+1_2+1b_ui0|1b_0mii.+1_3+(1b_ui0|1b_ui1)|1b_0mmi.+1_3+1b_ui0\
   |1b_0mfi.+1_3+1b_ui0|1b_0mi.b+1_2+1b_ui0\
   |(1b_1mi.i+1_5|1b_1mi.b+1_5)+(1b_ui0|1b_ui1)\
   |1b_1mii.+1_6+1b_ui1|1b_1mmi.+1_6+1b_ui1|1b_1mfi.+1_6+1b_ui1")

;; "An F slot in the 1st bundle disperses to F0".
;; "An F slot in the 2st bundle disperses to F1".
(define_reservation "1b_F"
   "1b_0mf.i+1_2+1b_uf0|1b_0mmf.+1_3+1b_uf0|1b_0mf.b+1_2+1b_uf0\
    |1b_1mf.i+1_5+1b_uf1|1b_1mf.b+1_5+1b_uf1")

;;; "Each B slot in MBB or BBB bundle disperses to the corresponding B
;;; unit. That is, a B slot in 1st position is dispersed to B0.  In the
;;; 2nd position it is dispersed to B2".
(define_reservation "1b_NB"
    "1b_0b.bb+1_1+1b_unb0|1b_0bb.b+1_2+1b_unb1|1b_0bbb.+1_3+1b_unb2\
     |1b_0mb.b+1_2+1b_unb1|1b_0mbb.+1_3+1b_unb2\
     |1b_0mib.+1_3+1b_unb0|1b_0mmb.+1_3+1b_unb0|1b_0mfb.+1_3+1b_unb0\
     |1b_1b.bb+1_4+1b_unb0|1b_1bb.b+1_5+1b_unb1\
     |1b_1bbb.+1_6+1b_unb2|1b_1mb.b+1_5+1b_unb1|1b_1mbb.+1_6+1b_unb2\
     |1b_1mib.+1_6+1b_unb0|1b_1mmb.+1_6+1b_unb0|1b_1mfb.+1_6+1b_unb0")

(define_reservation "1b_B"
   "1b_0b.bb+1_1+1b_ub0|1b_0bb.b+1_2+1b_ub1|1b_0bbb.+1_3+1b_ub2\
    |1b_0mb.b+1_2+1b_ub1|1b_0mbb.+1_3+1b_ub2|1b_0mib.+1_3+1b_ub2\
    |1b_0mfb.+1_3+1b_ub2|1b_1b.bb+1_4+1b_ub0|1b_1bb.b+1_5+1b_ub1\
    |1b_1bbb.+1_6+1b_ub2|1b_1mb.b+1_5+1b_ub1\
    |1b_1mib.+1_6+1b_ub2|1b_1mmb.+1_6+1b_ub2|1b_1mfb.+1_6+1b_ub2")

(define_reservation "1b_L" "1b_0mlx.+1_3+1b_ui0+1b_uf0\
                           |1b_1mlx.+1_6+(1b_ui0|1b_ui1)+1b_uf1")

;; We assume that there is no insn issued on the same cycle as unknown insn.
(define_cpu_unit "1b_empty" "oneb")
(exclusion_set "1b_empty"
    "1b_0m.ii,1b_0m.mi,1b_0m.fi,1b_0m.mf,1b_0b.bb,1b_0m.bb,\
     1b_0m.ib,1b_0m.mb,1b_0m.fb,1b_0m.lx")

(define_cpu_unit
   "1b_0m_bs, 1b_0mi_bs, 1b_0mm_bs, 1b_0mf_bs, 1b_0b_bs, 1b_0bb_bs, 1b_0mb_bs"
   "oneb")
(define_cpu_unit
   "1b_1m_bs, 1b_1mi_bs, 1b_1mm_bs, 1b_1mf_bs, 1b_1b_bs, 1b_1bb_bs, 1b_1mb_bs"
   "oneb")

(define_cpu_unit "1b_m_cont, 1b_mi_cont, 1b_mm_cont, 1b_mf_cont, 1b_mb_cont,\
	          1b_b_cont, 1b_bb_cont" "oneb")

;; For stop in the middle of the bundles.
(define_cpu_unit "1b_m_stop, 1b_m0_stop, 1b_m1_stop, 1b_0mmi_cont" "oneb")
(define_cpu_unit "1b_mi_stop, 1b_mi0_stop, 1b_mi1_stop, 1b_0mii_cont" "oneb")

(final_presence_set "1b_0m_bs"
   "1b_0m.ii, 1b_0m.mi, 1b_0m.mf, 1b_0m.fi, 1b_0m.bb,\
    1b_0m.ib, 1b_0m.fb, 1b_0m.mb, 1b_0m.lx")
(final_presence_set "1b_1m_bs"
   "1b_1m.ii, 1b_1m.mi, 1b_1m.fi, 1b_1m.bb, 1b_1m.ib, 1b_1m.fb, 1b_1m.mb,\
    1b_1m.lx")
(final_presence_set "1b_0mi_bs"  "1b_0mi.i, 1b_0mi.i")
(final_presence_set "1b_1mi_bs"  "1b_1mi.i, 1b_1mi.i")
(final_presence_set "1b_0mm_bs"  "1b_0mm.i, 1b_0mm.f, 1b_0mm.b")
(final_presence_set "1b_1mm_bs"  "1b_1mm.i, 1b_1mm.b")
(final_presence_set "1b_0mf_bs"  "1b_0mf.i, 1b_0mf.b")
(final_presence_set "1b_1mf_bs"  "1b_1mf.i, 1b_1mf.b")
(final_presence_set "1b_0b_bs"  "1b_0b.bb")
(final_presence_set "1b_1b_bs"  "1b_1b.bb")
(final_presence_set "1b_0bb_bs"  "1b_0bb.b")
(final_presence_set "1b_1bb_bs"  "1b_1bb.b")
(final_presence_set "1b_0mb_bs"  "1b_0mb.b")
(final_presence_set "1b_1mb_bs"  "1b_1mb.b")

(exclusion_set "1b_0m_bs"
   "1b_0mi.i, 1b_0mm.i, 1b_0mm.f, 1b_0mf.i, 1b_0mb.b,\
    1b_0mi.b, 1b_0mf.b, 1b_0mm.b, 1b_0mlx., 1b_m0_stop")
(exclusion_set "1b_1m_bs"
   "1b_1mi.i, 1b_1mm.i, 1b_1mf.i, 1b_1mb.b, 1b_1mi.b, 1b_1mf.b, 1b_1mm.b,\
    1b_1mlx., 1b_m1_stop")
(exclusion_set "1b_0mi_bs"  "1b_0mii., 1b_0mib., 1b_mi0_stop")
(exclusion_set "1b_1mi_bs"  "1b_1mii., 1b_1mib., 1b_mi1_stop")
(exclusion_set "1b_0mm_bs"  "1b_0mmi., 1b_0mmf., 1b_0mmb.")
(exclusion_set "1b_1mm_bs"  "1b_1mmi., 1b_1mmb.")
(exclusion_set "1b_0mf_bs"  "1b_0mfi., 1b_0mfb.")
(exclusion_set "1b_1mf_bs"  "1b_1mfi., 1b_1mfb.")
(exclusion_set "1b_0b_bs"  "1b_0bb.b")
(exclusion_set "1b_1b_bs"  "1b_1bb.b")
(exclusion_set "1b_0bb_bs"  "1b_0bbb.")
(exclusion_set "1b_1bb_bs"  "1b_1bbb.")
(exclusion_set "1b_0mb_bs"  "1b_0mbb.")
(exclusion_set "1b_1mb_bs"  "1b_1mbb.")

(exclusion_set
   "1b_0m_bs, 1b_0mi_bs, 1b_0mm_bs, 1b_0mf_bs, 1b_0b_bs, 1b_0bb_bs, 1b_0mb_bs,
    1b_1m_bs, 1b_1mi_bs, 1b_1mm_bs, 1b_1mf_bs, 1b_1b_bs, 1b_1bb_bs, 1b_1mb_bs"
   "1b_stop")

(final_presence_set
   "1b_0mi.i, 1b_0mm.i, 1b_0mf.i, 1b_0mm.f, 1b_0mb.b,\
    1b_0mi.b, 1b_0mm.b, 1b_0mf.b, 1b_0mlx."
   "1b_m_cont")
(final_presence_set "1b_0mii., 1b_0mib." "1b_mi_cont")
(final_presence_set "1b_0mmi., 1b_0mmf., 1b_0mmb." "1b_mm_cont")
(final_presence_set "1b_0mfi., 1b_0mfb." "1b_mf_cont")
(final_presence_set "1b_0bb.b" "1b_b_cont")
(final_presence_set "1b_0bbb." "1b_bb_cont")
(final_presence_set "1b_0mbb." "1b_mb_cont")

(exclusion_set
   "1b_0m.ii, 1b_0m.mi, 1b_0m.fi, 1b_0m.mf, 1b_0b.bb, 1b_0m.bb,\
    1b_0m.ib, 1b_0m.mb, 1b_0m.fb, 1b_0m.lx"
   "1b_m_cont, 1b_mi_cont, 1b_mm_cont, 1b_mf_cont,\
    1b_mb_cont, 1b_b_cont, 1b_bb_cont")

(exclusion_set "1b_empty"
               "1b_m_cont,1b_mi_cont,1b_mm_cont,1b_mf_cont,\
                1b_mb_cont,1b_b_cont,1b_bb_cont")

;; For m;mi bundle
(final_presence_set "1b_m0_stop" "1b_0m.mi")
(final_presence_set "1b_0mm.i" "1b_0mmi_cont")
(exclusion_set "1b_0mmi_cont"
   "1b_0m.ii, 1b_0m.mi, 1b_0m.fi, 1b_0m.mf, 1b_0b.bb, 1b_0m.bb,\
    1b_0m.ib, 1b_0m.mb, 1b_0m.fb, 1b_0m.lx")
(exclusion_set "1b_m0_stop" "1b_0mm.i")
(final_presence_set "1b_m1_stop" "1b_1m.mi")
(exclusion_set "1b_m1_stop" "1b_1mm.i")
(final_presence_set "1b_m_stop" "1b_m0_stop, 1b_m1_stop")

;; For mi;i bundle
(final_presence_set "1b_mi0_stop" "1b_0mi.i")
(final_presence_set "1b_0mii." "1b_0mii_cont")
(exclusion_set "1b_0mii_cont"
   "1b_0m.ii, 1b_0m.mi, 1b_0m.fi, 1b_0m.mf, 1b_0b.bb, 1b_0m.bb,\
    1b_0m.ib, 1b_0m.mb, 1b_0m.fb, 1b_0m.lx")
(exclusion_set "1b_mi0_stop" "1b_0mii.")
(final_presence_set "1b_mi1_stop" "1b_1mi.i")
(exclusion_set "1b_mi1_stop" "1b_1mii.")
(final_presence_set "1b_mi_stop" "1b_mi0_stop, 1b_mi1_stop")

(final_absence_set
   "1b_0m.ii,1b_0mi.i,1b_0mii.,1b_0m.mi,1b_0mm.i,1b_0mmi.,\
    1b_0m.fi,1b_0mf.i,1b_0mfi.,1b_0m.mf,1b_0mm.f,1b_0mmf.,\
    1b_0b.bb,1b_0bb.b,1b_0bbb.,1b_0m.bb,1b_0mb.b,1b_0mbb.,\
    1b_0m.ib,1b_0mi.b,1b_0mib.,1b_0m.mb,1b_0mm.b,1b_0mmb.,\
    1b_0m.fb,1b_0mf.b,1b_0mfb.,1b_0m.lx,1b_0mlx., \
    1b_1m.ii,1b_1mi.i,1b_1mii.,1b_1m.mi,1b_1mm.i,1b_1mmi.,\
    1b_1m.fi,1b_1mf.i,1b_1mfi.,\
    1b_1b.bb,1b_1bb.b,1b_1bbb.,1b_1m.bb,1b_1mb.b,1b_1mbb.,\
    1b_1m.ib,1b_1mi.b,1b_1mib.,1b_1m.mb,1b_1mm.b,1b_1mmb.,\
    1b_1m.fb,1b_1mf.b,1b_1mfb.,1b_1m.lx,1b_1mlx."
   "1b_m0_stop,1b_m1_stop,1b_mi0_stop,1b_mi1_stop")

(define_reservation "1b_A" "1b_M|1b_I")

(define_insn_reservation "1b_stop_bit" 0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "stop_bit"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_stop|1b_m0_stop|1b_m1_stop|1b_mi0_stop|1b_mi1_stop")
(define_insn_reservation "1b_br"      0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "br"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_B")
(define_insn_reservation "1b_scall"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "scall"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_B")
(define_insn_reservation "1b_fcmp"    2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "fcmp"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_F+1b_not_uf1")
(define_insn_reservation "1b_fcvtfx"  7
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "fcvtfx"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_F")

(define_insn_reservation "1b_fld"     9
  (and (and (and (eq_attr "cpu" "itanium")
		 (eq_attr "itanium_class" "fld"))
	    (eq_attr "check_load" "no"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")
(define_insn_reservation "1b_fldc"    0
  (and (and (and (eq_attr "cpu" "itanium")
		 (eq_attr "itanium_class" "fld"))
	    (eq_attr "check_load" "yes"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")

(define_insn_reservation "1b_fldp"    9
  (and (and (and (eq_attr "cpu" "itanium")
		 (eq_attr "itanium_class" "fldp"))
	    (eq_attr "check_load" "no"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")
(define_insn_reservation "1b_fldpc"   0
  (and (and (and (eq_attr "cpu" "itanium")
		 (eq_attr "itanium_class" "fldp"))
	    (eq_attr "check_load" "yes"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")

(define_insn_reservation "1b_fmac"    5
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "fmac"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_F")
(define_insn_reservation "1b_fmisc"   5
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "fmisc"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_F+1b_not_uf1")
(define_insn_reservation "1b_frar_i" 13
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "frar_i"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_I+1b_not_ui1")
(define_insn_reservation "1b_frar_m"  6
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "frar_m"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_M+1b_not_um1")
(define_insn_reservation "1b_frbr"    2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "frbr"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_I+1b_not_ui1")
(define_insn_reservation "1b_frfr"    2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "frfr"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_M+1b_not_um1")
(define_insn_reservation "1b_frpr"    2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "frpr"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_I+1b_not_ui1")
(define_insn_reservation "1b_ialu"      1
    (and (and (eq_attr "cpu" "itanium")
              (eq_attr "itanium_class" "ialu"))
         (ne (symbol_ref
	      "bundling_p && !ia64_produce_address_p (insn)")
             (const_int 0)))
    "1b_A")
(define_insn_reservation "1b_ialu_addr" 1
    (and (and (eq_attr "cpu" "itanium")
              (eq_attr "itanium_class" "ialu"))
         (eq (symbol_ref
              "bundling_p && ia64_produce_address_p (insn)")
             (const_int 1)))
    "1b_M")
(define_insn_reservation "1b_icmp"    1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "icmp"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_A")
(define_insn_reservation "1b_ilog"    1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "ilog"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_A")
(define_insn_reservation "1b_mmalua"  2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "mmalua"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_A")
(define_insn_reservation "1b_ishf"    1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "ishf"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_I+1b_not_ui1")

(define_insn_reservation "1b_ld"      2
  (and (and (and (eq_attr "cpu" "itanium")
		 (eq_attr "itanium_class" "ld"))
	    (eq_attr "check_load" "no"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")
(define_insn_reservation "1b_ldc"     0
  (and (and (and (eq_attr "cpu" "itanium")
		 (eq_attr "itanium_class" "ld"))
	    (eq_attr "check_load" "yes"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")

(define_insn_reservation "1b_long_i"  1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "long_i"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_L")
(define_insn_reservation "1b_mmmul"   2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "mmmul"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_I+1b_not_ui1")
(define_insn_reservation "1b_mmshf"   2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "mmshf"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_I")
(define_insn_reservation "1b_mmshfi"  2
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "mmshfi"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_I")
(define_insn_reservation "1b_rse_m"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "rse_m"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
   "(1b_0m.ii|1b_0m.mi|1b_0m.fi|1b_0m.mf|1b_0b.bb|1b_0m.bb\
     |1b_0m.ib|1b_0m.mb|1b_0m.fb|1b_0m.lx)+1_1+1b_um0")
(define_insn_reservation "1b_sem"     0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "sem"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_M+1b_not_um1")
(define_insn_reservation "1b_stf"     1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "stf"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")
(define_insn_reservation "1b_st"      1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "st"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")
(define_insn_reservation "1b_syst_m0" 0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "syst_m0"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_M+1b_not_um1")
(define_insn_reservation "1b_syst_m"  0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "syst_m"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")
(define_insn_reservation "1b_tbit"    1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "tbit"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_I+1b_not_ui1")
(define_insn_reservation "1b_toar_i"  0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "toar_i"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_I+1b_not_ui1")
(define_insn_reservation "1b_toar_m"  5
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "toar_m"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_M+1b_not_um1")
(define_insn_reservation "1b_tobr"    1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "tobr"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_I+1b_not_ui1")
(define_insn_reservation "1b_tofr"    9
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "tofr"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")
(define_insn_reservation "1b_topr"    1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "topr"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_I+1b_not_ui1")
(define_insn_reservation "1b_xmpy"    7
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "xmpy"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_F")
(define_insn_reservation "1b_xtd"     1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "xtd"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_I")

(define_insn_reservation "1b_chk_s_i" 0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "chk_s_i"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_A")
(define_insn_reservation "1b_chk_s_f" 0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "chk_s_f"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")
(define_insn_reservation "1b_chk_a"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "chk_a"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")

(define_insn_reservation "1b_lfetch"  0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "lfetch"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")
(define_insn_reservation "1b_nop_m"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "nop_m"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_M")
(define_insn_reservation "1b_nop_b"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "nop_b"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_NB")
(define_insn_reservation "1b_nop_i"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "nop_i"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_I")
(define_insn_reservation "1b_nop_f"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "nop_f"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_F")
(define_insn_reservation "1b_nop_x"   0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "nop_x"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "1b_L")
(define_insn_reservation "1b_unknown" 1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "unknown"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_empty")
(define_insn_reservation "1b_nop" 1
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "nop"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "1b_M|1b_NB|1b_I|1b_F")
(define_insn_reservation "1b_ignore" 0
  (and (and (eq_attr "cpu" "itanium")
            (eq_attr "itanium_class" "ignore"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "nothing")

(define_insn_reservation "1b_pre_cycle" 0
   (and (and (eq_attr "cpu" "itanium")
             (eq_attr "itanium_class" "pre_cycle"))
        (ne (symbol_ref "bundling_p") (const_int 0)))
                         "(1b_0m_bs, 1b_m_cont)     \
                          | (1b_0mi_bs, 1b_mi_cont) \
                          | (1b_0mm_bs, 1b_mm_cont) \
                          | (1b_0mf_bs, 1b_mf_cont) \
                          | (1b_0b_bs, 1b_b_cont)   \
                          | (1b_0bb_bs, 1b_bb_cont) \
                          | (1b_0mb_bs, 1b_mb_cont) \
                          | (1b_1m_bs, 1b_m_cont)   \
                          | (1b_1mi_bs, 1b_mi_cont) \
                          | (1b_1mm_bs, 1b_mm_cont) \
                          | (1b_1mf_bs, 1b_mf_cont) \
                          | (1b_1b_bs, 1b_b_cont)   \
                          | (1b_1bb_bs, 1b_bb_cont) \
                          | (1b_1mb_bs, 1b_mb_cont) \
                          | (1b_m_stop, 1b_0mmi_cont)   \
                          | (1b_mi_stop, 1b_0mii_cont)")


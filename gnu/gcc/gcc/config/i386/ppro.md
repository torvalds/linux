;; Scheduling for the Intel P6 family of processors
;; Copyright (C) 2004, 2005 Free Software Foundation, Inc.
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

;; The P6 family includes the Pentium Pro, Pentium II, Pentium III, Celeron
;; and Xeon lines of CPUs.  The DFA scheduler description in this file is
;; based on information that can be found in the following three documents:
;;
;;    "P6 Family of Processors Hardware Developer's Manual",
;;    Intel, September 1999.
;;
;;    "Intel Architecture Optimization Manual",
;;    Intel, 1999 (Order Number: 245127-001).
;;
;;    "How to optimize for the Pentium family of microprocessors",
;;    by Agner Fog, PhD.
;;
;; The P6 pipeline has three major components:
;;   1) the FETCH/DECODE unit, an in-order issue front-end
;;   2) the DISPATCH/EXECUTE unit, which is the out-of-order core
;;   3) the RETIRE unit, an in-order retirement unit
;;
;; So, the P6 CPUs have out-of-order cores, but the instruction decoder and
;; retirement unit are naturally in-order.
;;
;;                       BUS INTERFACE UNIT
;;                     /                   \
;;                L1 ICACHE             L1 DCACHE
;;              /     |     \              |     \
;;       DECODER0  DECODER1  DECODER2  DISP/EXEC  RETIRE
;;              \     |     /              |        |
;;            INSTRUCTION POOL   __________|_______/
;;          (inc. reorder buffer)
;;
;; Since the P6 CPUs execute instructions out-of-order, the most important
;; consideration in performance tuning is making sure enough micro-ops are
;; ready for execution in the out-of-order core, while not stalling the
;; decoder.
;;
;; TODO:
;; - Find a less crude way to model complex instructions, in
;;   particular how many cycles they take to be decoded.
;; - Include decoder latencies in the total reservation latencies.
;;   This isn't necessary right now because we assume for every
;;   instruction that it never blocks a decoder.
;; - Figure out where the p0 and p1 reservations come from.  These
;;   appear not to be in the manual (e.g. why is cld "(p0+p1)*2"
;;   better than "(p0|p1)*4" ???)
;; - Lots more because I'm sure this is still far from optimal :-)

;; The ppro_idiv and ppro_fdiv automata are used to model issue
;; latencies of idiv and fdiv type insns.
(define_automaton "ppro_decoder,ppro_core,ppro_idiv,ppro_fdiv,ppro_load,ppro_store")

;; Simple instructions of the register-register form have only one uop.
;; Load instructions are also only one uop.  Store instructions decode to
;; two uops, and simple read-modify instructions also take two uops.
;; Simple instructions of the register-memory form have two to three uops.
;; Simple read-modify-write instructions have four uops.  The rules for
;; the decoder are simple:
;;  - an instruction with 1 uop can be decoded by any of the three
;;    decoders in one cycle.
;;  - an instruction with 1 to 4 uops can be decoded only by decoder 0
;;    but still in only one cycle.
;;  - a complex (microcode) instruction can also only be decoded by
;;    decoder 0, and this takes an unspecified number of cycles.
;;    
;; The goal is to schedule such that we have a few-one-one uops sequence
;; in each cycle, to decode as many instructions per cycle as possible.
(define_cpu_unit "decoder0" "ppro_decoder")
(define_cpu_unit "decoder1" "ppro_decoder")
(define_cpu_unit "decoder2" "ppro_decoder")

;; We first wish to find an instruction for decoder0, so exclude
;; decoder1 and decoder2 from being reserved until decoder 0 is
;; reserved.
(presence_set "decoder1" "decoder0")
(presence_set "decoder2" "decoder0")

;; Most instructions can be decoded on any of the three decoders.
(define_reservation "decodern" "(decoder0|decoder1|decoder2)")

;; The out-of-order core has five pipelines.  During each cycle, the core
;; may dispatch zero or one uop on the port of any of the five pipelines
;; so the maximum number of dispatched uops per cycle is 5.  In practicer,
;; 3 uops per cycle is more realistic.
;;
;; Two of the five pipelines contain several execution units:
;;
;; Port 0	Port 1		Port 2		Port 3		Port 4
;; ALU		ALU		LOAD		SAC		SDA
;; FPU		JUE
;; AGU		MMX
;; MMX		P3FPU
;; P3FPU
;;
;; (SAC=Store Address Calculation, SDA=Store Data Unit, P3FPU = SSE unit,
;;  JUE = Jump Execution Unit, AGU = Address Generation Unit)
;;
(define_cpu_unit "p0,p1" "ppro_core")
(define_cpu_unit "p2" "ppro_load")
(define_cpu_unit "p3,p4" "ppro_store")
(define_cpu_unit "idiv" "ppro_idiv")
(define_cpu_unit "fdiv" "ppro_fdiv")

;; Only the irregular instructions have to be modeled here.  A load
;; increases the latency by 2 or 3, or by nothing if the manual gives
;; a latency already.  Store latencies are not accounted for.
;;
;; The simple instructions follow a very regular pattern of 1 uop per
;; reg-reg operation, 1 uop per load on port 2. and 2 uops per store
;; on port 4 and port 3.  These instructions are modelled at the bottom
;; of this file.
;;
;; For microcoded instructions we don't know how many uops are produced.
;; These instructions are the "complex" ones in the Intel manuals.  All
;; we _do_ know is that they typically produce four or more uops, so
;; they can only be decoded on decoder0.  Modelling their latencies
;; doesn't make sense because we don't know how these instructions are
;; executed in the core.  So we just model that they can only be decoded
;; on decoder 0, and say that it takes a little while before the result
;; is available.
(define_insn_reservation "ppro_complex_insn" 6
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (eq_attr "type" "other,multi,call,callv,str"))
			 "decoder0")

;; imov with memory operands does not use the integer units.
(define_insn_reservation "ppro_imov" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "imov")))
			 "decodern,(p0|p1)")

(define_insn_reservation "ppro_imov_load" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (eq_attr "type" "imov")))
			 "decodern,p2")

(define_insn_reservation "ppro_imov_store" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "store")
				   (eq_attr "type" "imov")))
			 "decoder0,p4+p3")

;; imovx always decodes to one uop, and also doesn't use the integer
;; units if it has memory operands.
(define_insn_reservation "ppro_imovx" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "imovx")))
			 "decodern,(p0|p1)")

(define_insn_reservation "ppro_imovx_load" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (eq_attr "type" "imovx")))
			 "decodern,p2")

;; lea executes on port 0 with latency one and throughput 1.
(define_insn_reservation "ppro_lea" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "lea")))
			 "decodern,p0")

;; Shift and rotate execute on port 0 with latency and throughput 1.
;; The load and store units need to be reserved when memory operands
;; are involved.
(define_insn_reservation "ppro_shift_rotate" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "ishift,ishift1,rotate,rotate1")))
			 "decodern,p0")

(define_insn_reservation "ppro_shift_rotate_mem" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "!none")
				   (eq_attr "type" "ishift,ishift1,rotate,rotate1")))
			 "decoder0,p2+p0,p4+p3")

(define_insn_reservation "ppro_cld" 2
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (eq_attr "type" "cld"))
			 "decoder0,(p0+p1)*2")

;; The P6 has a sophisticated branch prediction mechanism to minimize
;; latencies due to branching.  In particular, it has a fast way to
;; execute branches that are taken multiple times (such as in loops).
;; Branches not taken suffer no penalty, and correctly predicted
;; branches cost only one fetch cycle.  Mispredicted branches are very
;; costly: typically 15 cycles and possibly as many as 26 cycles.
;;
;; Unfortunately all this makes it quite difficult to properly model
;; the latencies for the compiler.  Here I've made the choice to be
;; optimistic and assume branches are often predicted correctly, so
;; they have latency 1, and the decoders are not blocked.
;;
;; In addition, the model assumes a branch always decodes to only 1 uop,
;; which is not exactly true because there are a few instructions that
;; decode to 2 uops or microcode.  But this probably gives the best
;; results because we can assume these instructions can decode on all
;; decoders.
(define_insn_reservation "ppro_branch" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "ibr")))
			 "decodern,p1")

;; ??? Indirect branches probably have worse latency than this.
(define_insn_reservation "ppro_indirect_branch" 6
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "!none")
				   (eq_attr "type" "ibr")))
			 "decoder0,p2+p1")

(define_insn_reservation "ppro_leave" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (eq_attr "type" "leave"))
			 "decoder0,p2+(p0|p1),(p0|p1)")

;; imul has throughput one, but latency 4, and can only execute on port 0.
(define_insn_reservation "ppro_imul" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "imul")))
			 "decodern,p0")

(define_insn_reservation "ppro_imul_mem" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "!none")
				   (eq_attr "type" "imul")))
			 "decoder0,p2+p0")

;; div and idiv are very similar, so we model them the same.
;; QI, HI, and SI have issue latency 12, 21, and 37, respectively.
;; These issue latencies are modelled via the ppro_div automaton.
(define_insn_reservation "ppro_idiv_QI" 19
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "QI")
					(eq_attr "type" "idiv"))))
			 "decoder0,(p0+idiv)*2,(p0|p1)+idiv,idiv*9")

(define_insn_reservation "ppro_idiv_QI_load" 19
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "QI")
					(eq_attr "type" "idiv"))))
			 "decoder0,p2+p0+idiv,p0+idiv,(p0|p1)+idiv,idiv*9")

(define_insn_reservation "ppro_idiv_HI" 23
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "HI")
					(eq_attr "type" "idiv"))))
			 "decoder0,(p0+idiv)*3,(p0|p1)+idiv,idiv*17")

(define_insn_reservation "ppro_idiv_HI_load" 23
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "HI")
					(eq_attr "type" "idiv"))))
			 "decoder0,p2+p0+idiv,p0+idiv,(p0|p1)+idiv,idiv*18")

(define_insn_reservation "ppro_idiv_SI" 39
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "SI")
					(eq_attr "type" "idiv"))))
			 "decoder0,(p0+idiv)*3,(p0|p1)+idiv,idiv*33")

(define_insn_reservation "ppro_idiv_SI_load" 39
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "SI")
					(eq_attr "type" "idiv"))))
			 "decoder0,p2+p0+idiv,p0+idiv,(p0|p1)+idiv,idiv*34")

;; Floating point operations always execute on port 0.
;; ??? where do these latencies come from? fadd has latency 3 and
;;     has throughput "1/cycle (align with FADD)".  What do they
;;     mean and how can we model that?
(define_insn_reservation "ppro_fop" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none,unknown")
				   (eq_attr "type" "fop")))
			 "decodern,p0")

(define_insn_reservation "ppro_fop_load" 5
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (eq_attr "type" "fop")))
			 "decoder0,p2+p0,p0")

(define_insn_reservation "ppro_fop_store" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "store")
				   (eq_attr "type" "fop")))
			 "decoder0,p0,p0,p0+p4+p3")

(define_insn_reservation "ppro_fop_both" 5
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "both")
				   (eq_attr "type" "fop")))
			 "decoder0,p2+p0,p0+p4+p3")

(define_insn_reservation "ppro_fsgn" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (eq_attr "type" "fsgn"))
			 "decodern,p0")

(define_insn_reservation "ppro_fistp" 5
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (eq_attr "type" "fistp"))
			 "decoder0,p0*2,p4+p3")

(define_insn_reservation "ppro_fcmov" 2
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (eq_attr "type" "fcmov"))
			 "decoder0,p0*2")

(define_insn_reservation "ppro_fcmp" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "fcmp")))
			 "decodern,p0")

(define_insn_reservation "ppro_fcmp_load" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (eq_attr "type" "fcmp")))
			 "decoder0,p2+p0")

(define_insn_reservation "ppro_fmov" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "fmov")))
			 "decodern,p0")

(define_insn_reservation "ppro_fmov_load" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "!XF")
					(eq_attr "type" "fmov"))))
			 "decodern,p2")

(define_insn_reservation "ppro_fmov_XF_load" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "XF")
					(eq_attr "type" "fmov"))))
			 "decoder0,(p2+p0)*2")

(define_insn_reservation "ppro_fmov_store" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "store")
				   (and (eq_attr "mode" "!XF")
					(eq_attr "type" "fmov"))))
			 "decodern,p0")

(define_insn_reservation "ppro_fmov_XF_store" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "store")
				   (and (eq_attr "mode" "XF")
					(eq_attr "type" "fmov"))))
			 "decoder0,(p0+p4),(p0+p3)")

;; fmul executes on port 0 with latency 5.  It has issue latency 2,
;; but we don't model this.
(define_insn_reservation "ppro_fmul" 5
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "fmul")))
			 "decoder0,p0*2")

(define_insn_reservation "ppro_fmul_load" 6
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (eq_attr "type" "fmul")))
			 "decoder0,p2+p0,p0")

;; fdiv latencies depend on the mode of the operands.  XFmode gives
;; a latency of 38 cycles, DFmode gives 32, and SFmode gives latency 18.
;; Division by a power of 2 takes only 9 cycles, but we cannot model
;; that.  Throughput is equal to latency - 1, which we model using the
;; ppro_div automaton.
(define_insn_reservation "ppro_fdiv_SF" 18
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "fdiv,fpspc"))))
			 "decodern,p0+fdiv,fdiv*16")

(define_insn_reservation "ppro_fdiv_SF_load" 19
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "fdiv,fpspc"))))
			 "decoder0,p2+p0+fdiv,fdiv*16")

(define_insn_reservation "ppro_fdiv_DF" 32
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "DF")
					(eq_attr "type" "fdiv,fpspc"))))
			 "decodern,p0+fdiv,fdiv*30")

(define_insn_reservation "ppro_fdiv_DF_load" 33
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "DF")
					(eq_attr "type" "fdiv,fpspc"))))
			 "decoder0,p2+p0+fdiv,fdiv*30")

(define_insn_reservation "ppro_fdiv_XF" 38
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "XF")
					(eq_attr "type" "fdiv,fpspc"))))
			 "decodern,p0+fdiv,fdiv*36")

(define_insn_reservation "ppro_fdiv_XF_load" 39
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "XF")
					(eq_attr "type" "fdiv,fpspc"))))
			 "decoder0,p2+p0+fdiv,fdiv*36")

;; MMX instructions can execute on either port 0 or port 1 with a
;; throughput of 1/cycle.
;;   on port 0:	- ALU (latency 1)
;;		- Multiplier Unit (latency 3)
;;   on port 1:	- ALU (latency 1)
;;		- Shift Unit (latency 1)
;;
;; MMX instructions are either of the type reg-reg, or read-modify, and
;; except for mmxshft and mmxmul they can execute on port 0 or port 1,
;; so they behave as "simple" instructions that need no special modelling.
;; We only have to model mmxshft and mmxmul.
(define_insn_reservation "ppro_mmx_shft" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "mmxshft")))
			 "decodern,p1")

(define_insn_reservation "ppro_mmx_shft_load" 2
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "mmxshft")))
			 "decoder0,p2+p1")

(define_insn_reservation "ppro_mmx_mul" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "mmxmul")))
			 "decodern,p0")

(define_insn_reservation "ppro_mmx_mul_load" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (eq_attr "type" "mmxmul")))
			 "decoder0,p2+p0")

(define_insn_reservation "ppro_sse_mmxcvt" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "mode" "DI")
				   (eq_attr "type" "mmxcvt")))
			 "decodern,p1")

;; FIXME: These are Pentium III only, but we cannot tell here if
;; we're generating code for PentiumPro/Pentium II or Pentium III
;; (define_insn_reservation "ppro_sse_mmxshft" 2
;;			 (and (eq_attr "cpu" "pentiumpro,generic32")
;;			      (and (eq_attr "mode" "DI")
;;				   (eq_attr "type" "mmxshft")))
;;			 "decodern,p0")

;; SSE is very complicated, and takes a bit more effort.
;; ??? I assumed that all SSE instructions decode on decoder0,
;;     but is this correct?

;; The sfence instruction.
(define_insn_reservation "ppro_sse_sfence" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "unknown")
				   (eq_attr "type" "sse")))
			 "decoder0,p4+p3")

;; FIXME: This reservation is all wrong when we're scheduling sqrtss.
(define_insn_reservation "ppro_sse_SF" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "mode" "SF")
				   (eq_attr "type" "sse")))
			 "decodern,p0")

(define_insn_reservation "ppro_sse_add_SF" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "sseadd"))))
			 "decodern,p1")

(define_insn_reservation "ppro_sse_add_SF_load" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "sseadd"))))
			 "decoder0,p2+p1")

(define_insn_reservation "ppro_sse_cmp_SF" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "ssecmp"))))
			 "decoder0,p1")

(define_insn_reservation "ppro_sse_cmp_SF_load" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "ssecmp"))))
			 "decoder0,p2+p1")

(define_insn_reservation "ppro_sse_comi_SF" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "ssecomi"))))
			 "decodern,p0")

(define_insn_reservation "ppro_sse_comi_SF_load" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "ssecomi"))))
			 "decoder0,p2+p0")

(define_insn_reservation "ppro_sse_mul_SF" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "ssemul"))))
			"decodern,p0")

(define_insn_reservation "ppro_sse_mul_SF_load" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "ssemul"))))
			"decoder0,p2+p0")

;; FIXME: ssediv doesn't close p0 for 17 cycles, surely???
(define_insn_reservation "ppro_sse_div_SF" 18
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "ssediv"))))
			 "decoder0,p0*17")

(define_insn_reservation "ppro_sse_div_SF_load" 18
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "ssediv"))))
			 "decoder0,(p2+p0),p0*16")

(define_insn_reservation "ppro_sse_icvt_SF" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "mode" "SF")
				   (eq_attr "type" "sseicvt")))
			 "decoder0,(p2+p1)*2")

(define_insn_reservation "ppro_sse_icvt_SI" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "mode" "SI")
				   (eq_attr "type" "sseicvt")))
			 "decoder0,(p2+p1)")

(define_insn_reservation "ppro_sse_mov_SF" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "ssemov"))))
			 "decoder0,(p0|p1)")

(define_insn_reservation "ppro_sse_mov_SF_load" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "ssemov"))))
			 "decoder0,p2+(p0|p1)")

(define_insn_reservation "ppro_sse_mov_SF_store" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "store")
				   (and (eq_attr "mode" "SF")
					(eq_attr "type" "ssemov"))))
			 "decoder0,p4+p3")

(define_insn_reservation "ppro_sse_V4SF" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "mode" "V4SF")
				   (eq_attr "type" "sse")))
			 "decoder0,p1*2")

(define_insn_reservation "ppro_sse_add_V4SF" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "sseadd"))))
			 "decoder0,p1*2")

(define_insn_reservation "ppro_sse_add_V4SF_load" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "sseadd"))))
			 "decoder0,(p2+p1)*2")

(define_insn_reservation "ppro_sse_cmp_V4SF" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "ssecmp"))))
			 "decoder0,p1*2")

(define_insn_reservation "ppro_sse_cmp_V4SF_load" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "ssecmp"))))
			 "decoder0,(p2+p1)*2")

(define_insn_reservation "ppro_sse_cvt_V4SF" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none,unknown")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "ssecvt"))))
			 "decoder0,p1*2")

(define_insn_reservation "ppro_sse_cvt_V4SF_other" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "!none,unknown")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "ssecmp"))))
			 "decoder0,p1,p4+p3")

(define_insn_reservation "ppro_sse_mul_V4SF" 5
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "ssemul"))))
			"decoder0,p0*2")

(define_insn_reservation "ppro_sse_mul_V4SF_load" 5
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "ssemul"))))
			"decoder0,(p2+p0)*2")

;; FIXME: p0 really closed this long???
(define_insn_reservation "ppro_sse_div_V4SF" 48
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "ssediv"))))
			 "decoder0,p0*34")

(define_insn_reservation "ppro_sse_div_V4SF_load" 48
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "ssediv"))))
			 "decoder0,(p2+p0)*2,p0*32")

(define_insn_reservation "ppro_sse_log_V4SF" 2
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "sselog,sselog1"))))
			 "decodern,p1")

(define_insn_reservation "ppro_sse_log_V4SF_load" 2
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "sselog,sselog1"))))
			 "decoder0,(p2+p1)")

(define_insn_reservation "ppro_sse_mov_V4SF" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "ssemov"))))
			 "decoder0,(p0|p1)*2")

(define_insn_reservation "ppro_sse_mov_V4SF_load" 2
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "ssemov"))))
			 "decoder0,p2*2")

(define_insn_reservation "ppro_sse_mov_V4SF_store" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "store")
				   (and (eq_attr "mode" "V4SF")
					(eq_attr "type" "ssemov"))))
			 "decoder0,(p4+p3)*2")

;; All other instructions are modelled as simple instructions.
;; We have already modelled all i387 floating point instructions, so all
;; other instructions execute on either port 0 or port 1.  This includes
;; the ALU units, and the MMX units.
;;
;; reg-reg instructions produce 1 uop so they can be decoded on any of
;; the three decoders.
(define_insn_reservation "ppro_insn" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "none,unknown")
				   (eq_attr "type" "alu,alu1,negnot,incdec,icmp,test,setcc,icmov,push,pop,fxch,sseiadd,sseishft,sseimul,mmx,mmxadd,mmxcmp")))
			 "decodern,(p0|p1)")

;; read-modify and register-memory instructions have 2 or three uops,
;; so they have to be decoded on decoder0.
(define_insn_reservation "ppro_insn_load" 3
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "load")
				   (eq_attr "type" "alu,alu1,negnot,incdec,icmp,test,setcc,icmov,push,pop,fxch,sseiadd,sseishft,sseimul,mmx,mmxadd,mmxcmp")))
			 "decoder0,p2+(p0|p1)")

(define_insn_reservation "ppro_insn_store" 1
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "store")
				   (eq_attr "type" "alu,alu1,negnot,incdec,icmp,test,setcc,icmov,push,pop,fxch,sseiadd,sseishft,sseimul,mmx,mmxadd,mmxcmp")))
			 "decoder0,(p0|p1),p4+p3")

;; read-modify-store instructions produce 4 uops so they have to be
;; decoded on decoder0 as well.
(define_insn_reservation "ppro_insn_both" 4
			 (and (eq_attr "cpu" "pentiumpro,generic32")
			      (and (eq_attr "memory" "both")
				   (eq_attr "type" "alu,alu1,negnot,incdec,icmp,test,setcc,icmov,push,pop,fxch,sseiadd,sseishft,sseimul,mmx,mmxadd,mmxcmp")))
			 "decoder0,p2+(p0|p1),p4+p3")


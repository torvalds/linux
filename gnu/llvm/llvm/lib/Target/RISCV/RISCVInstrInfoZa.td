//===-- RISCVInstrInfoZa.td - RISC-V Atomic instructions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions from the standard atomic 'Za*'
// extensions:
//   - Zawrs (v1.0) : Wait-on-Reservation-Set.
//   - Zacas (v1.0-rc1) : Atomic Compare-and-Swap.
//   - Zabha (v1.0-rc1) : Byte and Halfword Atomic Memory Operations.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Zacas (Atomic Compare-and-Swap)
//===----------------------------------------------------------------------===//

def GPRPairRV32Operand : AsmOperandClass {
  let Name = "GPRPairRV32";
  let ParserMethod = "parseGPRPair<false>";
  let PredicateMethod = "isGPRPair";
  let RenderMethod = "addRegOperands";
}

def GPRPairRV64Operand : AsmOperandClass {
  let Name = "GPRPairRV64";
  let ParserMethod = "parseGPRPair<true>";
  let PredicateMethod = "isGPRPair";
  let RenderMethod = "addRegOperands";
}

def GPRPairRV32 : RegisterOperand<GPRPair> {
  let ParserMatchClass = GPRPairRV32Operand;
}

def GPRPairRV64 : RegisterOperand<GPRPair> {
  let ParserMatchClass = GPRPairRV64Operand;
}

let hasSideEffects = 0, mayLoad = 1, mayStore = 1, Constraints = "$rd = $rd_wb" in
class AMO_cas<bits<5> funct5, bit aq, bit rl, bits<3> funct3, string opcodestr,
              DAGOperand RC>
    : RVInstRAtomic<funct5, aq, rl, funct3, OPC_AMO,
                    (outs RC:$rd_wb), (ins RC:$rd, GPRMemZeroOffset:$rs1, RC:$rs2),
                    opcodestr, "$rd, $rs2, $rs1">;

multiclass AMO_cas_aq_rl<bits<5> funct5, bits<3> funct3, string opcodestr,
                         DAGOperand RC> {
  def ""     : AMO_cas<funct5, 0, 0, funct3, opcodestr, RC>;
  def _AQ    : AMO_cas<funct5, 1, 0, funct3, opcodestr # ".aq", RC>;
  def _RL    : AMO_cas<funct5, 0, 1, funct3, opcodestr # ".rl", RC>;
  def _AQ_RL : AMO_cas<funct5, 1, 1, funct3, opcodestr # ".aqrl", RC>;
}

let Predicates = [HasStdExtZacas], IsSignExtendingOpW = 1 in {
defm AMOCAS_W : AMO_cas_aq_rl<0b00101, 0b010, "amocas.w", GPR>;
} // Predicates = [HasStdExtZacas]

let Predicates = [HasStdExtZacas, IsRV32], DecoderNamespace = "RV32Zacas"  in {
defm AMOCAS_D_RV32 : AMO_cas_aq_rl<0b00101, 0b011, "amocas.d", GPRPairRV32>;
} // Predicates = [HasStdExtZacas, IsRV32]

let Predicates = [HasStdExtZacas, IsRV64] in {
defm AMOCAS_D_RV64 : AMO_cas_aq_rl<0b00101, 0b011, "amocas.d", GPR>;
defm AMOCAS_Q : AMO_cas_aq_rl<0b00101, 0b100, "amocas.q", GPRPairRV64>;
} // Predicates = [HasStdExtZacas, IsRV64]

multiclass AMOCASPat<string AtomicOp, string BaseInst, ValueType vt = XLenVT,
                     list<Predicate> ExtraPreds = []> {
  let Predicates = !listconcat([HasStdExtZacas, NotHasStdExtZtso], ExtraPreds) in {
    def : Pat<(!cast<PatFrag>(AtomicOp#"_monotonic") (vt GPR:$addr),
                                                     (vt GPR:$cmp),
                                                     (vt GPR:$new)),
              (!cast<RVInst>(BaseInst) GPR:$cmp, GPR:$addr, GPR:$new)>;
    def : Pat<(!cast<PatFrag>(AtomicOp#"_acquire") (vt GPR:$addr),
                                                   (vt GPR:$cmp),
                                                   (vt GPR:$new)),
              (!cast<RVInst>(BaseInst#"_AQ") GPR:$cmp, GPR:$addr, GPR:$new)>;
    def : Pat<(!cast<PatFrag>(AtomicOp#"_release") (vt GPR:$addr),
                                                   (vt GPR:$cmp),
                                                   (vt GPR:$new)),
              (!cast<RVInst>(BaseInst#"_RL") GPR:$cmp, GPR:$addr, GPR:$new)>;
    def : Pat<(!cast<PatFrag>(AtomicOp#"_acq_rel") (vt GPR:$addr),
                                                   (vt GPR:$cmp),
                                                   (vt GPR:$new)),
              (!cast<RVInst>(BaseInst#"_AQ_RL") GPR:$cmp, GPR:$addr, GPR:$new)>;
    def : Pat<(!cast<PatFrag>(AtomicOp#"_seq_cst") (vt GPR:$addr),
                                                   (vt GPR:$cmp),
                                                   (vt GPR:$new)),
              (!cast<RVInst>(BaseInst#"_AQ_RL") GPR:$cmp, GPR:$addr, GPR:$new)>;
  } // Predicates = !listconcat([HasStdExtZacas, NotHasStdExtZtso], ExtraPreds)
  let Predicates = !listconcat([HasStdExtZacas, HasStdExtZtso], ExtraPreds) in {
    def : Pat<(!cast<PatFrag>(AtomicOp#"_monotonic") (vt GPR:$addr),
                                                     (vt GPR:$cmp),
                                                     (vt GPR:$new)),
              (!cast<RVInst>(BaseInst) GPR:$cmp, GPR:$addr, GPR:$new)>;
    def : Pat<(!cast<PatFrag>(AtomicOp#"_acquire") (vt GPR:$addr),
                                                   (vt GPR:$cmp),
                                                   (vt GPR:$new)),
              (!cast<RVInst>(BaseInst) GPR:$cmp, GPR:$addr, GPR:$new)>;
    def : Pat<(!cast<PatFrag>(AtomicOp#"_release") (vt GPR:$addr),
                                                   (vt GPR:$cmp),
                                                   (vt GPR:$new)),
              (!cast<RVInst>(BaseInst) GPR:$cmp, GPR:$addr, GPR:$new)>;
    def : Pat<(!cast<PatFrag>(AtomicOp#"_acq_rel") (vt GPR:$addr),
                                                   (vt GPR:$cmp),
                                                   (vt GPR:$new)),
              (!cast<RVInst>(BaseInst) GPR:$cmp, GPR:$addr, GPR:$new)>;
    def : Pat<(!cast<PatFrag>(AtomicOp#"_seq_cst") (vt GPR:$addr),
                                                   (vt GPR:$cmp),
                                                   (vt GPR:$new)),
              (!cast<RVInst>(BaseInst) GPR:$cmp, GPR:$addr, GPR:$new)>;
  } // Predicates = !listconcat([HasStdExtZacas, HasStdExtZtso], ExtraPreds)
}

defm : AMOCASPat<"atomic_cmp_swap_i32", "AMOCAS_W">;
defm : AMOCASPat<"atomic_cmp_swap_i64", "AMOCAS_D_RV64", i64, [IsRV64]>;

//===----------------------------------------------------------------------===//
// Zawrs (Wait-on-Reservation-Set)
//===----------------------------------------------------------------------===//

let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class WRSInst<bits<12> funct12, string opcodestr>
    : RVInstI<0b000, OPC_SYSTEM, (outs), (ins), opcodestr, ""> {
  let rs1 = 0;
  let rd = 0;
  let imm12 = funct12;
}

let Predicates = [HasStdExtZawrs] in {
def WRS_NTO : WRSInst<0b000000001101, "wrs.nto">, Sched<[]>;
def WRS_STO : WRSInst<0b000000011101, "wrs.sto">, Sched<[]>;
} // Predicates = [HasStdExtZawrs]

//===----------------------------------------------------------------------===//
// Zabha (Byte and Halfword Atomic Memory Operations)
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZabha] in {
defm AMOSWAP_B  : AMO_rr_aq_rl<0b00001, 0b000, "amoswap.b">,
                  Sched<[WriteAtomicB, ReadAtomicBA, ReadAtomicBD]>;
defm AMOADD_B   : AMO_rr_aq_rl<0b00000, 0b000, "amoadd.b">,
                  Sched<[WriteAtomicB, ReadAtomicBA, ReadAtomicBD]>;
defm AMOXOR_B   : AMO_rr_aq_rl<0b00100, 0b000, "amoxor.b">,
                  Sched<[WriteAtomicB, ReadAtomicBA, ReadAtomicBD]>;
defm AMOAND_B   : AMO_rr_aq_rl<0b01100, 0b000, "amoand.b">,
                  Sched<[WriteAtomicB, ReadAtomicBA, ReadAtomicBD]>;
defm AMOOR_B    : AMO_rr_aq_rl<0b01000, 0b000, "amoor.b">,
                  Sched<[WriteAtomicB, ReadAtomicBA, ReadAtomicBD]>;
defm AMOMIN_B   : AMO_rr_aq_rl<0b10000, 0b000, "amomin.b">,
                  Sched<[WriteAtomicB, ReadAtomicBA, ReadAtomicBD]>;
defm AMOMAX_B   : AMO_rr_aq_rl<0b10100, 0b000, "amomax.b">,
                  Sched<[WriteAtomicB, ReadAtomicBA, ReadAtomicBD]>;
defm AMOMINU_B  : AMO_rr_aq_rl<0b11000, 0b000, "amominu.b">,
                  Sched<[WriteAtomicB, ReadAtomicBA, ReadAtomicBD]>;
defm AMOMAXU_B  : AMO_rr_aq_rl<0b11100, 0b000, "amomaxu.b">,
                  Sched<[WriteAtomicB, ReadAtomicBA, ReadAtomicBD]>;

defm AMOSWAP_H  : AMO_rr_aq_rl<0b00001, 0b001, "amoswap.h">,
                  Sched<[WriteAtomicH, ReadAtomicHA, ReadAtomicHD]>;
defm AMOADD_H   : AMO_rr_aq_rl<0b00000, 0b001, "amoadd.h">,
                  Sched<[WriteAtomicH, ReadAtomicHA, ReadAtomicHD]>;
defm AMOXOR_H   : AMO_rr_aq_rl<0b00100, 0b001, "amoxor.h">,
                  Sched<[WriteAtomicH, ReadAtomicHA, ReadAtomicHD]>;
defm AMOAND_H   : AMO_rr_aq_rl<0b01100, 0b001, "amoand.h">,
                  Sched<[WriteAtomicH, ReadAtomicHA, ReadAtomicHD]>;
defm AMOOR_H    : AMO_rr_aq_rl<0b01000, 0b001, "amoor.h">,
                  Sched<[WriteAtomicH, ReadAtomicHA, ReadAtomicHD]>;
defm AMOMIN_H   : AMO_rr_aq_rl<0b10000, 0b001, "amomin.h">,
                  Sched<[WriteAtomicH, ReadAtomicHA, ReadAtomicHD]>;
defm AMOMAX_H   : AMO_rr_aq_rl<0b10100, 0b001, "amomax.h">,
                  Sched<[WriteAtomicH, ReadAtomicHA, ReadAtomicHD]>;
defm AMOMINU_H  : AMO_rr_aq_rl<0b11000, 0b001, "amominu.h">,
                  Sched<[WriteAtomicH, ReadAtomicHA, ReadAtomicHD]>;
defm AMOMAXU_H  : AMO_rr_aq_rl<0b11100, 0b001, "amomaxu.h">,
                  Sched<[WriteAtomicH, ReadAtomicHA, ReadAtomicHD]>;
}

// If Zacas extension is also implemented, Zabha further provides AMOCAS.[B|H].
let Predicates = [HasStdExtZabha, HasStdExtZacas] in {
defm AMOCAS_B : AMO_cas_aq_rl<0b00101, 0b000, "amocas.b", GPR>;
defm AMOCAS_H : AMO_cas_aq_rl<0b00101, 0b001, "amocas.h", GPR>;
}

/// AMOs

defm : AMOPat<"atomic_swap_i8", "AMOSWAP_B", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_add_i8", "AMOADD_B", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_and_i8", "AMOAND_B", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_or_i8", "AMOOR_B", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_xor_i8", "AMOXOR_B", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_max_i8", "AMOMAX_B", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_min_i8", "AMOMIN_B", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_umax_i8", "AMOMAXU_B", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_umin_i8", "AMOMINU_B", XLenVT, [HasStdExtZabha]>;

defm : AMOPat<"atomic_swap_i16", "AMOSWAP_H", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_add_i16", "AMOADD_H", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_and_i16", "AMOAND_H", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_or_i16", "AMOOR_H", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_xor_i16", "AMOXOR_H", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_max_i16", "AMOMAX_H", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_min_i16", "AMOMIN_H", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_umax_i16", "AMOMAXU_H", XLenVT, [HasStdExtZabha]>;
defm : AMOPat<"atomic_load_umin_i16", "AMOMINU_H", XLenVT, [HasStdExtZabha]>;

/// AMOCAS

defm : AMOCASPat<"atomic_cmp_swap_i8", "AMOCAS_B", XLenVT, [HasStdExtZabha]>;
defm : AMOCASPat<"atomic_cmp_swap_i16", "AMOCAS_H", XLenVT, [HasStdExtZabha]>;

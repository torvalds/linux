//===-- RISCVInstrInfoA.td - RISC-V 'A' instructions -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions from the standard 'A', Atomic
// Instructions extension.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction class templates
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
class LR_r<bit aq, bit rl, bits<3> funct3, string opcodestr>
    : RVInstRAtomic<0b00010, aq, rl, funct3, OPC_AMO,
                    (outs GPR:$rd), (ins GPRMemZeroOffset:$rs1),
                    opcodestr, "$rd, $rs1"> {
  let rs2 = 0;
}

multiclass LR_r_aq_rl<bits<3> funct3, string opcodestr> {
  def ""     : LR_r<0, 0, funct3, opcodestr>;
  def _AQ    : LR_r<1, 0, funct3, opcodestr # ".aq">;
  def _RL    : LR_r<0, 1, funct3, opcodestr # ".rl">;
  def _AQ_RL : LR_r<1, 1, funct3, opcodestr # ".aqrl">;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
class SC_r<bit aq, bit rl, bits<3> funct3, string opcodestr>
    : RVInstRAtomic<0b00011, aq, rl, funct3, OPC_AMO,
                    (outs GPR:$rd), (ins GPRMemZeroOffset:$rs1, GPR:$rs2),
                    opcodestr, "$rd, $rs2, $rs1">;

multiclass SC_r_aq_rl<bits<3> funct3, string opcodestr> {
  def ""     : SC_r<0, 0, funct3, opcodestr>;
  def _AQ    : SC_r<1, 0, funct3, opcodestr # ".aq">;
  def _RL    : SC_r<0, 1, funct3, opcodestr # ".rl">;
  def _AQ_RL : SC_r<1, 1, funct3, opcodestr # ".aqrl">;
}

let hasSideEffects = 0, mayLoad = 1, mayStore = 1 in
class AMO_rr<bits<5> funct5, bit aq, bit rl, bits<3> funct3, string opcodestr>
    : RVInstRAtomic<funct5, aq, rl, funct3, OPC_AMO,
                    (outs GPR:$rd), (ins GPRMemZeroOffset:$rs1, GPR:$rs2),
                    opcodestr, "$rd, $rs2, $rs1">;

multiclass AMO_rr_aq_rl<bits<5> funct5, bits<3> funct3, string opcodestr> {
  def ""     : AMO_rr<funct5, 0, 0, funct3, opcodestr>;
  def _AQ    : AMO_rr<funct5, 1, 0, funct3, opcodestr # ".aq">;
  def _RL    : AMO_rr<funct5, 0, 1, funct3, opcodestr # ".rl">;
  def _AQ_RL : AMO_rr<funct5, 1, 1, funct3, opcodestr # ".aqrl">;
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtAOrZalrsc], IsSignExtendingOpW = 1 in {
defm LR_W       : LR_r_aq_rl<0b010, "lr.w">, Sched<[WriteAtomicLDW, ReadAtomicLDW]>;
defm SC_W       : SC_r_aq_rl<0b010, "sc.w">,
                  Sched<[WriteAtomicSTW, ReadAtomicSTW, ReadAtomicSTW]>;
} // Predicates = [HasStdExtAOrZalrsc], IsSignExtendingOpW = 1

let Predicates = [HasStdExtAOrZaamo], IsSignExtendingOpW = 1 in {
defm AMOSWAP_W  : AMO_rr_aq_rl<0b00001, 0b010, "amoswap.w">,
                  Sched<[WriteAtomicW, ReadAtomicWA, ReadAtomicWD]>;
defm AMOADD_W   : AMO_rr_aq_rl<0b00000, 0b010, "amoadd.w">,
                  Sched<[WriteAtomicW, ReadAtomicWA, ReadAtomicWD]>;
defm AMOXOR_W   : AMO_rr_aq_rl<0b00100, 0b010, "amoxor.w">,
                  Sched<[WriteAtomicW, ReadAtomicWA, ReadAtomicWD]>;
defm AMOAND_W   : AMO_rr_aq_rl<0b01100, 0b010, "amoand.w">,
                  Sched<[WriteAtomicW, ReadAtomicWA, ReadAtomicWD]>;
defm AMOOR_W    : AMO_rr_aq_rl<0b01000, 0b010, "amoor.w">,
                  Sched<[WriteAtomicW, ReadAtomicWA, ReadAtomicWD]>;
defm AMOMIN_W   : AMO_rr_aq_rl<0b10000, 0b010, "amomin.w">,
                  Sched<[WriteAtomicW, ReadAtomicWA, ReadAtomicWD]>;
defm AMOMAX_W   : AMO_rr_aq_rl<0b10100, 0b010, "amomax.w">,
                  Sched<[WriteAtomicW, ReadAtomicWA, ReadAtomicWD]>;
defm AMOMINU_W  : AMO_rr_aq_rl<0b11000, 0b010, "amominu.w">,
                  Sched<[WriteAtomicW, ReadAtomicWA, ReadAtomicWD]>;
defm AMOMAXU_W  : AMO_rr_aq_rl<0b11100, 0b010, "amomaxu.w">,
                  Sched<[WriteAtomicW, ReadAtomicWA, ReadAtomicWD]>;
} // Predicates = [HasStdExtAOrZaamo], IsSignExtendingOpW = 1

let Predicates = [HasStdExtAOrZalrsc, IsRV64] in {
defm LR_D       : LR_r_aq_rl<0b011, "lr.d">, Sched<[WriteAtomicLDD, ReadAtomicLDD]>;
defm SC_D       : SC_r_aq_rl<0b011, "sc.d">,
                  Sched<[WriteAtomicSTD, ReadAtomicSTD, ReadAtomicSTD]>;
} // Predicates = [HasStdExtAOrZalrsc, IsRV64]

let Predicates = [HasStdExtAOrZaamo, IsRV64] in {
defm AMOSWAP_D  : AMO_rr_aq_rl<0b00001, 0b011, "amoswap.d">,
                  Sched<[WriteAtomicD, ReadAtomicDA, ReadAtomicDD]>;
defm AMOADD_D   : AMO_rr_aq_rl<0b00000, 0b011, "amoadd.d">,
                  Sched<[WriteAtomicD, ReadAtomicDA, ReadAtomicDD]>;
defm AMOXOR_D   : AMO_rr_aq_rl<0b00100, 0b011, "amoxor.d">,
                  Sched<[WriteAtomicD, ReadAtomicDA, ReadAtomicDD]>;
defm AMOAND_D   : AMO_rr_aq_rl<0b01100, 0b011, "amoand.d">,
                  Sched<[WriteAtomicD, ReadAtomicDA, ReadAtomicDD]>;
defm AMOOR_D    : AMO_rr_aq_rl<0b01000, 0b011, "amoor.d">,
                  Sched<[WriteAtomicD, ReadAtomicDA, ReadAtomicDD]>;
defm AMOMIN_D   : AMO_rr_aq_rl<0b10000, 0b011, "amomin.d">,
                  Sched<[WriteAtomicD, ReadAtomicDA, ReadAtomicDD]>;
defm AMOMAX_D   : AMO_rr_aq_rl<0b10100, 0b011, "amomax.d">,
                  Sched<[WriteAtomicD, ReadAtomicDA, ReadAtomicDD]>;
defm AMOMINU_D  : AMO_rr_aq_rl<0b11000, 0b011, "amominu.d">,
                  Sched<[WriteAtomicD, ReadAtomicDA, ReadAtomicDD]>;
defm AMOMAXU_D  : AMO_rr_aq_rl<0b11100, 0b011, "amomaxu.d">,
                  Sched<[WriteAtomicD, ReadAtomicDA, ReadAtomicDD]>;
} // Predicates = [HasStdExtAOrZaamo, IsRV64]

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//

// Atomic load/store are available under both +a and +force-atomics.
// Fences will be inserted for atomic load/stores according to the logic in
// RISCVTargetLowering::{emitLeadingFence,emitTrailingFence}.
let Predicates = [HasAtomicLdSt] in {
  def : LdPat<atomic_load_8,  LB>;
  def : LdPat<atomic_load_16, LH>;
  def : LdPat<atomic_load_32, LW>;

  def : StPat<atomic_store_8,  SB, GPR, XLenVT>;
  def : StPat<atomic_store_16, SH, GPR, XLenVT>;
  def : StPat<atomic_store_32, SW, GPR, XLenVT>;
}

let Predicates = [HasAtomicLdSt, IsRV64] in {
  def : LdPat<atomic_load_64, LD, i64>;
  def : StPat<atomic_store_64, SD, GPR, i64>;
}

/// AMOs

multiclass AMOPat<string AtomicOp, string BaseInst, ValueType vt = XLenVT,
                  list<Predicate> ExtraPreds = []> {
let Predicates = !listconcat([HasStdExtA, NotHasStdExtZtso], ExtraPreds) in {
  def : PatGprGpr<!cast<PatFrag>(AtomicOp#"_monotonic"),
                  !cast<RVInst>(BaseInst), vt>;
  def : PatGprGpr<!cast<PatFrag>(AtomicOp#"_acquire"),
                  !cast<RVInst>(BaseInst#"_AQ"), vt>;
  def : PatGprGpr<!cast<PatFrag>(AtomicOp#"_release"),
                  !cast<RVInst>(BaseInst#"_RL"), vt>;
  def : PatGprGpr<!cast<PatFrag>(AtomicOp#"_acq_rel"),
                  !cast<RVInst>(BaseInst#"_AQ_RL"), vt>;
  def : PatGprGpr<!cast<PatFrag>(AtomicOp#"_seq_cst"),
                  !cast<RVInst>(BaseInst#"_AQ_RL"), vt>;
}
let Predicates = !listconcat([HasStdExtA, HasStdExtZtso], ExtraPreds) in {
  def : PatGprGpr<!cast<PatFrag>(AtomicOp#"_monotonic"),
                  !cast<RVInst>(BaseInst), vt>;
  def : PatGprGpr<!cast<PatFrag>(AtomicOp#"_acquire"),
                  !cast<RVInst>(BaseInst), vt>;
  def : PatGprGpr<!cast<PatFrag>(AtomicOp#"_release"),
                  !cast<RVInst>(BaseInst), vt>;
  def : PatGprGpr<!cast<PatFrag>(AtomicOp#"_acq_rel"),
                  !cast<RVInst>(BaseInst), vt>;
  def : PatGprGpr<!cast<PatFrag>(AtomicOp#"_seq_cst"),
                  !cast<RVInst>(BaseInst), vt>;
}
}

defm : AMOPat<"atomic_swap_i32", "AMOSWAP_W">;
defm : AMOPat<"atomic_load_add_i32", "AMOADD_W">;
defm : AMOPat<"atomic_load_and_i32", "AMOAND_W">;
defm : AMOPat<"atomic_load_or_i32", "AMOOR_W">;
defm : AMOPat<"atomic_load_xor_i32", "AMOXOR_W">;
defm : AMOPat<"atomic_load_max_i32", "AMOMAX_W">;
defm : AMOPat<"atomic_load_min_i32", "AMOMIN_W">;
defm : AMOPat<"atomic_load_umax_i32", "AMOMAXU_W">;
defm : AMOPat<"atomic_load_umin_i32", "AMOMINU_W">;

defm : AMOPat<"atomic_swap_i64", "AMOSWAP_D", i64, [IsRV64]>;
defm : AMOPat<"atomic_load_add_i64", "AMOADD_D", i64, [IsRV64]>;
defm : AMOPat<"atomic_load_and_i64", "AMOAND_D", i64, [IsRV64]>;
defm : AMOPat<"atomic_load_or_i64", "AMOOR_D", i64, [IsRV64]>;
defm : AMOPat<"atomic_load_xor_i64", "AMOXOR_D", i64, [IsRV64]>;
defm : AMOPat<"atomic_load_max_i64", "AMOMAX_D", i64, [IsRV64]>;
defm : AMOPat<"atomic_load_min_i64", "AMOMIN_D", i64, [IsRV64]>;
defm : AMOPat<"atomic_load_umax_i64", "AMOMAXU_D", i64, [IsRV64]>;
defm : AMOPat<"atomic_load_umin_i64", "AMOMINU_D", i64, [IsRV64]>;


/// Pseudo AMOs

class PseudoAMO : Pseudo<(outs GPR:$res, GPR:$scratch),
                         (ins GPR:$addr, GPR:$incr, ixlenimm:$ordering), []> {
  let Constraints = "@earlyclobber $res,@earlyclobber $scratch";
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 0;
}

class PseudoMaskedAMO
    : Pseudo<(outs GPR:$res, GPR:$scratch),
             (ins GPR:$addr, GPR:$incr, GPR:$mask, ixlenimm:$ordering), []> {
  let Constraints = "@earlyclobber $res,@earlyclobber $scratch";
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 0;
}

class PseudoMaskedAMOMinMax
    : Pseudo<(outs GPR:$res, GPR:$scratch1, GPR:$scratch2),
             (ins GPR:$addr, GPR:$incr, GPR:$mask, ixlenimm:$sextshamt,
              ixlenimm:$ordering), []> {
  let Constraints = "@earlyclobber $res,@earlyclobber $scratch1,"
                    "@earlyclobber $scratch2";
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 0;
}

class PseudoMaskedAMOUMinUMax
    : Pseudo<(outs GPR:$res, GPR:$scratch1, GPR:$scratch2),
             (ins GPR:$addr, GPR:$incr, GPR:$mask, ixlenimm:$ordering), []> {
  let Constraints = "@earlyclobber $res,@earlyclobber $scratch1,"
                    "@earlyclobber $scratch2";
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 0;
}

class PseudoMaskedAMOPat<Intrinsic intrin, Pseudo AMOInst>
    : Pat<(intrin GPR:$addr, GPR:$incr, GPR:$mask, timm:$ordering),
          (AMOInst GPR:$addr, GPR:$incr, GPR:$mask, timm:$ordering)>;

class PseudoMaskedAMOMinMaxPat<Intrinsic intrin, Pseudo AMOInst>
    : Pat<(intrin GPR:$addr, GPR:$incr, GPR:$mask, GPR:$shiftamt,
           timm:$ordering),
          (AMOInst GPR:$addr, GPR:$incr, GPR:$mask, GPR:$shiftamt,
           timm:$ordering)>;

let Predicates = [HasStdExtA] in {

let Size = 20 in
def PseudoAtomicLoadNand32 : PseudoAMO;
// Ordering constants must be kept in sync with the AtomicOrdering enum in
// AtomicOrdering.h.
def : Pat<(XLenVT (atomic_load_nand_i32_monotonic GPR:$addr, GPR:$incr)),
          (PseudoAtomicLoadNand32 GPR:$addr, GPR:$incr, 2)>;
def : Pat<(XLenVT (atomic_load_nand_i32_acquire GPR:$addr, GPR:$incr)),
          (PseudoAtomicLoadNand32 GPR:$addr, GPR:$incr, 4)>;
def : Pat<(XLenVT (atomic_load_nand_i32_release GPR:$addr, GPR:$incr)),
          (PseudoAtomicLoadNand32 GPR:$addr, GPR:$incr, 5)>;
def : Pat<(XLenVT (atomic_load_nand_i32_acq_rel GPR:$addr, GPR:$incr)),
          (PseudoAtomicLoadNand32 GPR:$addr, GPR:$incr, 6)>;
def : Pat<(XLenVT (atomic_load_nand_i32_seq_cst GPR:$addr, GPR:$incr)),
          (PseudoAtomicLoadNand32 GPR:$addr, GPR:$incr, 7)>;

let Size = 28 in
def PseudoMaskedAtomicSwap32 : PseudoMaskedAMO;
def : PseudoMaskedAMOPat<int_riscv_masked_atomicrmw_xchg_i32,
                         PseudoMaskedAtomicSwap32>;
let Size = 28 in
def PseudoMaskedAtomicLoadAdd32 : PseudoMaskedAMO;
def : PseudoMaskedAMOPat<int_riscv_masked_atomicrmw_add_i32,
                         PseudoMaskedAtomicLoadAdd32>;
let Size = 28 in
def PseudoMaskedAtomicLoadSub32 : PseudoMaskedAMO;
def : PseudoMaskedAMOPat<int_riscv_masked_atomicrmw_sub_i32,
                         PseudoMaskedAtomicLoadSub32>;
let Size = 32 in
def PseudoMaskedAtomicLoadNand32 : PseudoMaskedAMO;
def : PseudoMaskedAMOPat<int_riscv_masked_atomicrmw_nand_i32,
                         PseudoMaskedAtomicLoadNand32>;
let Size = 44 in
def PseudoMaskedAtomicLoadMax32 : PseudoMaskedAMOMinMax;
def : PseudoMaskedAMOMinMaxPat<int_riscv_masked_atomicrmw_max_i32,
                               PseudoMaskedAtomicLoadMax32>;
let Size = 44 in
def PseudoMaskedAtomicLoadMin32 : PseudoMaskedAMOMinMax;
def : PseudoMaskedAMOMinMaxPat<int_riscv_masked_atomicrmw_min_i32,
                               PseudoMaskedAtomicLoadMin32>;
let Size = 36 in
def PseudoMaskedAtomicLoadUMax32 : PseudoMaskedAMOUMinUMax;
def : PseudoMaskedAMOPat<int_riscv_masked_atomicrmw_umax_i32,
                         PseudoMaskedAtomicLoadUMax32>;
let Size = 36 in
def PseudoMaskedAtomicLoadUMin32 : PseudoMaskedAMOUMinUMax;
def : PseudoMaskedAMOPat<int_riscv_masked_atomicrmw_umin_i32,
                         PseudoMaskedAtomicLoadUMin32>;
} // Predicates = [HasStdExtA]

let Predicates = [HasStdExtA, IsRV64] in {

let Size = 20 in
def PseudoAtomicLoadNand64 : PseudoAMO;
// Ordering constants must be kept in sync with the AtomicOrdering enum in
// AtomicOrdering.h.
def : Pat<(i64 (atomic_load_nand_i64_monotonic GPR:$addr, GPR:$incr)),
          (PseudoAtomicLoadNand64 GPR:$addr, GPR:$incr, 2)>;
def : Pat<(i64 (atomic_load_nand_i64_acquire GPR:$addr, GPR:$incr)),
          (PseudoAtomicLoadNand64 GPR:$addr, GPR:$incr, 4)>;
def : Pat<(i64 (atomic_load_nand_i64_release GPR:$addr, GPR:$incr)),
          (PseudoAtomicLoadNand64 GPR:$addr, GPR:$incr, 5)>;
def : Pat<(i64 (atomic_load_nand_i64_acq_rel GPR:$addr, GPR:$incr)),
          (PseudoAtomicLoadNand64 GPR:$addr, GPR:$incr, 6)>;
def : Pat<(i64 (atomic_load_nand_i64_seq_cst GPR:$addr, GPR:$incr)),
          (PseudoAtomicLoadNand64 GPR:$addr, GPR:$incr, 7)>;

def : PseudoMaskedAMOPat<int_riscv_masked_atomicrmw_xchg_i64,
                         PseudoMaskedAtomicSwap32>;
def : PseudoMaskedAMOPat<int_riscv_masked_atomicrmw_add_i64,
                         PseudoMaskedAtomicLoadAdd32>;
def : PseudoMaskedAMOPat<int_riscv_masked_atomicrmw_sub_i64,
                         PseudoMaskedAtomicLoadSub32>;
def : PseudoMaskedAMOPat<int_riscv_masked_atomicrmw_nand_i64,
                         PseudoMaskedAtomicLoadNand32>;
def : PseudoMaskedAMOMinMaxPat<int_riscv_masked_atomicrmw_max_i64,
                               PseudoMaskedAtomicLoadMax32>;
def : PseudoMaskedAMOMinMaxPat<int_riscv_masked_atomicrmw_min_i64,
                               PseudoMaskedAtomicLoadMin32>;
def : PseudoMaskedAMOPat<int_riscv_masked_atomicrmw_umax_i64,
                         PseudoMaskedAtomicLoadUMax32>;
def : PseudoMaskedAMOPat<int_riscv_masked_atomicrmw_umin_i64,
                         PseudoMaskedAtomicLoadUMin32>;
} // Predicates = [HasStdExtA, IsRV64]


/// Compare and exchange

class PseudoCmpXchg
    : Pseudo<(outs GPR:$res, GPR:$scratch),
             (ins GPR:$addr, GPR:$cmpval, GPR:$newval, ixlenimm:$ordering), []> {
  let Constraints = "@earlyclobber $res,@earlyclobber $scratch";
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 0;
  let Size = 16;
}

// Ordering constants must be kept in sync with the AtomicOrdering enum in
// AtomicOrdering.h.
multiclass PseudoCmpXchgPat<string Op, Pseudo CmpXchgInst,
                            ValueType vt = XLenVT> {
  def : Pat<(vt (!cast<PatFrag>(Op#"_monotonic") GPR:$addr, GPR:$cmp, GPR:$new)),
            (CmpXchgInst GPR:$addr, GPR:$cmp, GPR:$new, 2)>;
  def : Pat<(vt (!cast<PatFrag>(Op#"_acquire") GPR:$addr, GPR:$cmp, GPR:$new)),
            (CmpXchgInst GPR:$addr, GPR:$cmp, GPR:$new, 4)>;
  def : Pat<(vt (!cast<PatFrag>(Op#"_release") GPR:$addr, GPR:$cmp, GPR:$new)),
            (CmpXchgInst GPR:$addr, GPR:$cmp, GPR:$new, 5)>;
  def : Pat<(vt (!cast<PatFrag>(Op#"_acq_rel") GPR:$addr, GPR:$cmp, GPR:$new)),
            (CmpXchgInst GPR:$addr, GPR:$cmp, GPR:$new, 6)>;
  def : Pat<(vt (!cast<PatFrag>(Op#"_seq_cst") GPR:$addr, GPR:$cmp, GPR:$new)),
            (CmpXchgInst GPR:$addr, GPR:$cmp, GPR:$new, 7)>;
}

let Predicates = [HasStdExtA, NoStdExtZacas] in {
def PseudoCmpXchg32 : PseudoCmpXchg;
defm : PseudoCmpXchgPat<"atomic_cmp_swap_i32", PseudoCmpXchg32>;
}

let Predicates = [HasStdExtA, NoStdExtZacas, IsRV64] in {
def PseudoCmpXchg64 : PseudoCmpXchg;
defm : PseudoCmpXchgPat<"atomic_cmp_swap_i64", PseudoCmpXchg64, i64>;
}

let Predicates = [HasStdExtA] in {
def PseudoMaskedCmpXchg32
    : Pseudo<(outs GPR:$res, GPR:$scratch),
             (ins GPR:$addr, GPR:$cmpval, GPR:$newval, GPR:$mask,
              ixlenimm:$ordering), []> {
  let Constraints = "@earlyclobber $res,@earlyclobber $scratch";
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 0;
  let Size = 32;
}

def : Pat<(int_riscv_masked_cmpxchg_i32
            GPR:$addr, GPR:$cmpval, GPR:$newval, GPR:$mask, timm:$ordering),
          (PseudoMaskedCmpXchg32
            GPR:$addr, GPR:$cmpval, GPR:$newval, GPR:$mask, timm:$ordering)>;
} // Predicates = [HasStdExtA]

let Predicates = [HasStdExtA, IsRV64] in {
def : Pat<(int_riscv_masked_cmpxchg_i64
            GPR:$addr, GPR:$cmpval, GPR:$newval, GPR:$mask, timm:$ordering),
          (PseudoMaskedCmpXchg32
            GPR:$addr, GPR:$cmpval, GPR:$newval, GPR:$mask, timm:$ordering)>;
} // Predicates = [HasStdExtA, IsRV64]

//===----------------------------------------------------------------------===//
// Experimental RV64 i32 legalization patterns.
//===----------------------------------------------------------------------===//

class PatGprGprA<SDPatternOperator OpNode, RVInst Inst, ValueType vt>
    : Pat<(vt (OpNode (XLenVT GPR:$rs1), (vt GPR:$rs2))), (Inst GPR:$rs1, GPR:$rs2)>;

multiclass AMOPat2<string AtomicOp, string BaseInst, ValueType vt = XLenVT,
                   list<Predicate> ExtraPreds = []> {
let Predicates = !listconcat([HasStdExtA, NotHasStdExtZtso], ExtraPreds) in {
  def : PatGprGprA<!cast<PatFrag>(AtomicOp#"_monotonic"),
                   !cast<RVInst>(BaseInst), vt>;
  def : PatGprGprA<!cast<PatFrag>(AtomicOp#"_acquire"),
                   !cast<RVInst>(BaseInst#"_AQ"), vt>;
  def : PatGprGprA<!cast<PatFrag>(AtomicOp#"_release"),
                   !cast<RVInst>(BaseInst#"_RL"), vt>;
  def : PatGprGprA<!cast<PatFrag>(AtomicOp#"_acq_rel"),
                   !cast<RVInst>(BaseInst#"_AQ_RL"), vt>;
  def : PatGprGprA<!cast<PatFrag>(AtomicOp#"_seq_cst"),
                   !cast<RVInst>(BaseInst#"_AQ_RL"), vt>;
}
let Predicates = !listconcat([HasStdExtA, HasStdExtZtso], ExtraPreds) in {
  def : PatGprGprA<!cast<PatFrag>(AtomicOp#"_monotonic"),
                   !cast<RVInst>(BaseInst), vt>;
  def : PatGprGprA<!cast<PatFrag>(AtomicOp#"_acquire"),
                   !cast<RVInst>(BaseInst), vt>;
  def : PatGprGprA<!cast<PatFrag>(AtomicOp#"_release"),
                   !cast<RVInst>(BaseInst), vt>;
  def : PatGprGprA<!cast<PatFrag>(AtomicOp#"_acq_rel"),
                   !cast<RVInst>(BaseInst), vt>;
  def : PatGprGprA<!cast<PatFrag>(AtomicOp#"_seq_cst"),
                   !cast<RVInst>(BaseInst), vt>;
}
}

defm : AMOPat2<"atomic_swap_i32", "AMOSWAP_W", i32>;
defm : AMOPat2<"atomic_load_add_i32", "AMOADD_W", i32>;
defm : AMOPat2<"atomic_load_and_i32", "AMOAND_W", i32>;
defm : AMOPat2<"atomic_load_or_i32", "AMOOR_W", i32>;
defm : AMOPat2<"atomic_load_xor_i32", "AMOXOR_W", i32>;
defm : AMOPat2<"atomic_load_max_i32", "AMOMAX_W", i32>;
defm : AMOPat2<"atomic_load_min_i32", "AMOMIN_W", i32>;
defm : AMOPat2<"atomic_load_umax_i32", "AMOMAXU_W", i32>;
defm : AMOPat2<"atomic_load_umin_i32", "AMOMINU_W", i32>;

let Predicates = [HasStdExtA, IsRV64] in
defm : PseudoCmpXchgPat<"atomic_cmp_swap_i32", PseudoCmpXchg32, i32>;

let Predicates = [HasAtomicLdSt] in {
  def : LdPat<atomic_load_8,  LB, i32>;
  def : LdPat<atomic_load_16, LH, i32>;
  def : LdPat<atomic_load_32, LW, i32>;

  def : StPat<atomic_store_8,  SB, GPR, i32>;
  def : StPat<atomic_store_16, SH, GPR, i32>;
  def : StPat<atomic_store_32, SW, GPR, i32>;
}


//==- RISCVSchedSyntacoreSCR3.td - Syntacore SCR3 Scheduling Definitions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//

// This model covers SYNTACORE_SCR3_RV32IMC and SYNTACORE_RV64IMAC
// configurations (syntacore-scr3-rv32/64).
// Overview: https://syntacore.com/products/scr3

// SCR3 is single-issue in-order processor
class SyntacoreSCR3Model : SchedMachineModel {
  let MicroOpBufferSize = 0;
  let IssueWidth = 1;
  let LoadLatency = 2;
  let MispredictPenalty = 3;
  let CompleteModel = 0;
  let UnsupportedFeatures = [HasStdExtD, HasStdExtZbkb, HasStdExtZbkc, HasStdExtZbkx,
                             HasStdExtZknd, HasStdExtZkne, HasStdExtZknh,
                             HasStdExtZksed, HasStdExtZksh, HasStdExtZkr,
                             HasVInstructions];
}

// Branching
multiclass SCR3_Branching<ProcResourceKind BRU> {
  def : WriteRes<WriteJmp, [BRU]>;
  def : WriteRes<WriteJal, [BRU]>;
  def : WriteRes<WriteJalr, [BRU]>;
}

// Single-cycle integer arithmetic and logic
multiclass SCR3_IntALU<ProcResourceKind ALU> {
  def : WriteRes<WriteIALU, [ALU]>;
  def : WriteRes<WriteIALU32, [ALU]>;
  def : WriteRes<WriteShiftImm, [ALU]>;
  def : WriteRes<WriteShiftImm32, [ALU]>;
  def : WriteRes<WriteShiftReg, [ALU]>;
  def : WriteRes<WriteShiftReg32, [ALU]>;
}

// Integer multiplication
multiclass SCR3_IntMul<ProcResourceKind MUL> {
  let Latency = 2 in {
    def : WriteRes<WriteIMul, [MUL]>;
    def : WriteRes<WriteIMul32, [MUL]>;
  }
}

// Integer division
multiclass SCR3_IntDiv<ProcResourceKind DIV, int DivLatency> {
  let Latency = DivLatency, ReleaseAtCycles = [DivLatency] in {
    def : WriteRes<WriteIDiv, [DIV]>;
    def : WriteRes<WriteIDiv32, [DIV]>;
    def : WriteRes<WriteIRem, [DIV]>;
    def : WriteRes<WriteIRem32, [DIV]>;
  }
}

// Load/store instructions on SCR3 have latency 2
multiclass SCR3_Memory<ProcResourceKind LSU> {
  let Latency = 2 in {
    def : WriteRes<WriteSTB, [LSU]>;
    def : WriteRes<WriteSTH, [LSU]>;
    def : WriteRes<WriteSTW, [LSU]>;
    def : WriteRes<WriteSTD, [LSU]>;
    def : WriteRes<WriteLDB, [LSU]>;
    def : WriteRes<WriteLDH, [LSU]>;
    def : WriteRes<WriteLDW, [LSU]>;
    def : WriteRes<WriteLDD, [LSU]>;
  }
}

// Atomic memory
multiclass SCR3_AtomicMemory<ProcResourceKind LSU> {
  let Latency = 20 in {
    def : WriteRes<WriteAtomicLDW, [LSU]>;
    def : WriteRes<WriteAtomicLDD, [LSU]>;
    def : WriteRes<WriteAtomicW, [LSU]>;
    def : WriteRes<WriteAtomicD, [LSU]>;
    def : WriteRes<WriteAtomicSTW, [LSU]>;
    def : WriteRes<WriteAtomicSTD, [LSU]>;
  }
}

// Others
multiclass SCR3_Other {
  def : WriteRes<WriteCSR, []>;
  def : WriteRes<WriteNop, []>;

  def : InstRW<[WriteIALU], (instrs COPY)>;
}


multiclass SCR3_Unsupported {
  defm : UnsupportedSchedD;
  defm : UnsupportedSchedF;
  defm : UnsupportedSchedSFB;
  defm : UnsupportedSchedV;
  defm : UnsupportedSchedXsfvcp;
  defm : UnsupportedSchedZabha;
  defm : UnsupportedSchedZba;
  defm : UnsupportedSchedZbb;
  defm : UnsupportedSchedZbc;
  defm : UnsupportedSchedZbs;
  defm : UnsupportedSchedZbkb;
  defm : UnsupportedSchedZbkx;
  defm : UnsupportedSchedZfa;
  defm : UnsupportedSchedZfh;
  defm : UnsupportedSchedZvk;
}

// Bypasses (none)
multiclass SCR3_NoReadAdvances {
  def : ReadAdvance<ReadJmp, 0>;
  def : ReadAdvance<ReadJalr, 0>;
  def : ReadAdvance<ReadCSR, 0>;
  def : ReadAdvance<ReadStoreData, 0>;
  def : ReadAdvance<ReadMemBase, 0>;
  def : ReadAdvance<ReadIALU, 0>;
  def : ReadAdvance<ReadIALU32, 0>;
  def : ReadAdvance<ReadShiftImm, 0>;
  def : ReadAdvance<ReadShiftImm32, 0>;
  def : ReadAdvance<ReadShiftReg, 0>;
  def : ReadAdvance<ReadShiftReg32, 0>;
  def : ReadAdvance<ReadIDiv, 0>;
  def : ReadAdvance<ReadIDiv32, 0>;
  def : ReadAdvance<ReadIRem, 0>;
  def : ReadAdvance<ReadIRem32, 0>;
  def : ReadAdvance<ReadIMul, 0>;
  def : ReadAdvance<ReadIMul32, 0>;
  def : ReadAdvance<ReadAtomicWA, 0>;
  def : ReadAdvance<ReadAtomicWD, 0>;
  def : ReadAdvance<ReadAtomicDA, 0>;
  def : ReadAdvance<ReadAtomicDD, 0>;
  def : ReadAdvance<ReadAtomicLDW, 0>;
  def : ReadAdvance<ReadAtomicLDD, 0>;
  def : ReadAdvance<ReadAtomicSTW, 0>;
  def : ReadAdvance<ReadAtomicSTD, 0>;
}

def SyntacoreSCR3RV32Model : SyntacoreSCR3Model;

let SchedModel = SyntacoreSCR3RV32Model in {
  let BufferSize = 0 in {
    def SCR3RV32_ALU : ProcResource<1>;
    def SCR3RV32_MUL : ProcResource<1>;
    def SCR3RV32_DIV : ProcResource<1>;
    def SCR3RV32_LSU : ProcResource<1>;
    def SCR3RV32_CFU : ProcResource<1>;
  }

  defm : SCR3_Branching<SCR3RV32_CFU>;
  defm : SCR3_IntALU<SCR3RV32_ALU>;
  defm : SCR3_IntMul<SCR3RV32_MUL>;
  defm : SCR3_IntDiv<SCR3RV32_DIV, /* div latency = */ 8>;
  defm : SCR3_Memory<SCR3RV32_LSU>;
  defm : SCR3_AtomicMemory<SCR3RV32_LSU>;
  defm : SCR3_Other;

  defm : SCR3_Unsupported;
  defm : SCR3_NoReadAdvances;
}

def SyntacoreSCR3RV64Model : SyntacoreSCR3Model;

let SchedModel = SyntacoreSCR3RV64Model in {
  let BufferSize = 0 in {
    def SCR3RV64_ALU : ProcResource<1>;
    def SCR3RV64_MUL : ProcResource<1>;
    def SCR3RV64_DIV : ProcResource<1>;
    def SCR3RV64_LSU : ProcResource<1>;
    def SCR3RV64_CFU : ProcResource<1>;
  }

  defm : SCR3_Branching<SCR3RV64_CFU>;
  defm : SCR3_IntALU<SCR3RV64_ALU>;
  defm : SCR3_IntMul<SCR3RV64_MUL>;
  defm : SCR3_IntDiv<SCR3RV64_DIV, /* div latency = */ 11>;
  defm : SCR3_Memory<SCR3RV64_LSU>;
  defm : SCR3_AtomicMemory<SCR3RV64_LSU>;
  defm : SCR3_Other;

  defm : SCR3_Unsupported;
  defm : SCR3_NoReadAdvances;
}

//==- RISCVSchedXiangShanNanHu.td - XS-NanHu Scheduling Defs -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//

// XiangShan is a high-performance open-source RISC-V processor developed by
// the Institute of Computing Technology (ICT), Chinese Academy of Sciences.
// Source: https://github.com/OpenXiangShan/XiangShan
// Documentation: https://github.com/OpenXiangShan/XiangShan-doc

// XiangShan-NanHu is the second generation of XiangShan processor series.
// Overview: https://xiangshan-doc.readthedocs.io/zh-cn/latest/integration/overview/

def XiangShanNanHuModel : SchedMachineModel {
  let MicroOpBufferSize = 256;
  let LoopMicroOpBufferSize = 48;  // Instruction queue size
  let IssueWidth = 6;  // 6-way decode and dispatch
  let LoadLatency = 4;
  let MispredictPenalty = 11; // Based on estimate of pipeline depth.
  let CompleteModel = 0;
  let UnsupportedFeatures = [HasStdExtZcmt, HasStdExtZkr, HasVInstructions,
                             HasVInstructionsI64];
}

let SchedModel = XiangShanNanHuModel in {

// The reservation stations are distributed and grouped as 32-entry or 16-entry smaller ones.
let BufferSize = 16 in {
  def XS2ALU : ProcResource<4>;
  def XS2MDU : ProcResource<2>;
  def XS2MISC : ProcResource<1>;

  def XS2FMAC : ProcResource<4>;
  def XS2FMISC : ProcResource<2>;

  // Load/Store queues are ignored.
  def XS2LD : ProcResource<2>;
  def XS2ST : ProcResource<2>;
}

// Branching
def : WriteRes<WriteJmp, [XS2MISC]>;
def : WriteRes<WriteJal, [XS2MISC]>;
def : WriteRes<WriteJalr, [XS2MISC]>;

// Integer arithmetic and logic
let Latency = 1 in {
def : WriteRes<WriteIALU, [XS2ALU]>;
def : WriteRes<WriteIALU32, [XS2ALU]>;
def : WriteRes<WriteShiftImm, [XS2ALU]>;
def : WriteRes<WriteShiftImm32, [XS2ALU]>;
def : WriteRes<WriteShiftReg, [XS2ALU]>;
def : WriteRes<WriteShiftReg32, [XS2ALU]>;
}

// Integer multiplication
let Latency = 3 in {
def : WriteRes<WriteIMul, [XS2MDU]>;
def : WriteRes<WriteIMul32, [XS2MDU]>;
}

// Integer division/remainder
// SRT16 algorithm
let Latency = 20, ReleaseAtCycles = [20] in {
def : WriteRes<WriteIDiv32, [XS2MDU]>;
def : WriteRes<WriteIDiv, [XS2MDU]>;
def : WriteRes<WriteIRem32, [XS2MDU]>;
def : WriteRes<WriteIRem, [XS2MDU]>;
}

// Zb*
let Latency = 1 in {
// Zba
def : WriteRes<WriteSHXADD, [XS2ALU]>;
def : WriteRes<WriteSHXADD32, [XS2ALU]>;

// Zbb
def : WriteRes<WriteRotateImm, [XS2ALU]>;
def : WriteRes<WriteRotateImm32, [XS2ALU]>;
def : WriteRes<WriteRotateReg, [XS2ALU]>;
def : WriteRes<WriteRotateReg32, [XS2ALU]>;
def : WriteRes<WriteORCB, [XS2ALU]>;
def : WriteRes<WriteIMinMax, [XS2ALU]>;
def : WriteRes<WriteREV8, [XS2ALU]>;

// Zbkb
def : WriteRes<WriteBREV8, [XS2ALU]>;
def : WriteRes<WritePACK, [XS2ALU]>;
def : WriteRes<WritePACK32, [XS2ALU]>;
def : WriteRes<WriteZIP, [XS2ALU]>;

// Zbs
def : WriteRes<WriteSingleBit, [XS2ALU]>;
def : WriteRes<WriteSingleBitImm, [XS2ALU]>;
def : WriteRes<WriteBEXT, [XS2ALU]>;
def : WriteRes<WriteBEXTI, [XS2ALU]>;
}

let Latency = 3 in {
// Zbb
def : WriteRes<WriteCLZ, [XS2MDU]>;
def : WriteRes<WriteCLZ32, [XS2MDU]>;
def : WriteRes<WriteCTZ, [XS2MDU]>;
def : WriteRes<WriteCTZ32, [XS2MDU]>;
def : WriteRes<WriteCPOP, [XS2MDU]>;
def : WriteRes<WriteCPOP32, [XS2MDU]>;

// Zbkc
def : WriteRes<WriteCLMUL, [XS2MDU]>;

// Zbkx
def : WriteRes<WriteXPERM, [XS2MDU]>;
}

// Memory
def : WriteRes<WriteSTB, [XS2ST]>;
def : WriteRes<WriteSTH, [XS2ST]>;
def : WriteRes<WriteSTW, [XS2ST]>;
def : WriteRes<WriteSTD, [XS2ST]>;
def : WriteRes<WriteFST32, [XS2ST]>;
def : WriteRes<WriteFST64, [XS2ST]>;
def : WriteRes<WriteAtomicSTW, [XS2ST]>;
def : WriteRes<WriteAtomicSTD, [XS2ST]>;

let Latency = 5 in {
def : WriteRes<WriteLDB, [XS2LD]>;
def : WriteRes<WriteLDH, [XS2LD]>;
def : WriteRes<WriteLDW, [XS2LD]>;
def : WriteRes<WriteLDD, [XS2LD]>;

def : WriteRes<WriteAtomicW, [XS2LD]>;
def : WriteRes<WriteAtomicD, [XS2LD]>;
def : WriteRes<WriteAtomicLDW, [XS2LD]>;
def : WriteRes<WriteAtomicLDD, [XS2LD]>;

def : WriteRes<WriteFLD32, [XS2LD]>;
def : WriteRes<WriteFLD64, [XS2LD]>;
}

// XiangShan-NanHu uses FuDian FPU instead of Berkeley HardFloat.
// Documentation: https://github.com/OpenXiangShan/fudian

let Latency = 3 in {
def : WriteRes<WriteFAdd32, [XS2FMAC]>;
def : WriteRes<WriteFSGNJ32, [XS2FMAC]>;
def : WriteRes<WriteFMinMax32, [XS2FMAC]>;
def : WriteRes<WriteFAdd64, [XS2FMAC]>;
def : WriteRes<WriteFSGNJ64, [XS2FMAC]>;
def : WriteRes<WriteFMinMax64, [XS2FMAC]>;

def : WriteRes<WriteFCvtI32ToF32, [XS2FMAC]>;
def : WriteRes<WriteFCvtI32ToF64, [XS2FMAC]>;
def : WriteRes<WriteFCvtI64ToF32, [XS2FMAC]>;
def : WriteRes<WriteFCvtI64ToF64, [XS2FMAC]>;
def : WriteRes<WriteFCvtF32ToI32, [XS2FMAC]>;
def : WriteRes<WriteFCvtF32ToI64, [XS2FMAC]>;
def : WriteRes<WriteFCvtF64ToI32, [XS2FMAC]>;
def : WriteRes<WriteFCvtF64ToI64, [XS2FMAC]>;
def : WriteRes<WriteFCvtF32ToF64, [XS2FMAC]>;
def : WriteRes<WriteFCvtF64ToF32, [XS2FMAC]>;

def : WriteRes<WriteFClass32, [XS2FMAC]>;
def : WriteRes<WriteFClass64, [XS2FMAC]>;
def : WriteRes<WriteFCmp32, [XS2FMAC]>;
def : WriteRes<WriteFCmp64, [XS2FMAC]>;
def : WriteRes<WriteFMovF32ToI32, [XS2FMAC]>;
def : WriteRes<WriteFMovI32ToF32, [XS2FMAC]>;
def : WriteRes<WriteFMovF64ToI64, [XS2FMAC]>;
def : WriteRes<WriteFMovI64ToF64, [XS2FMAC]>;
}

// FP multiplication
let Latency = 3 in {
def : WriteRes<WriteFMul32, [XS2FMAC]>;
def : WriteRes<WriteFMul64, [XS2FMAC]>;
}

let Latency = 5 in {
def : WriteRes<WriteFMA32, [XS2FMAC]>;
def : WriteRes<WriteFMA64, [XS2FMAC]>;
}

// FP division
def : WriteRes<WriteFDiv32, [XS2FMISC]> {
    let Latency = 11;
}
def : WriteRes<WriteFDiv64, [XS2FMISC]> {
    let Latency = 18;
}

def : WriteRes<WriteFSqrt32, [XS2FMISC]> {
    let Latency = 17;
}
def : WriteRes<WriteFSqrt64, [XS2FMISC]> {
    let Latency = 31;
}

// Others
def : WriteRes<WriteCSR, [XS2MISC]>;
def : WriteRes<WriteNop, []>;

def : InstRW<[WriteIALU], (instrs COPY)>;

// Bypass and advance

class XS2LoadToALUBypass<SchedRead read>
    : ReadAdvance<read, 1, [WriteLDB, WriteLDH, WriteLDW, WriteLDD, WriteAtomicW, WriteAtomicD, WriteAtomicLDW, WriteAtomicLDD]>;

def : ReadAdvance<ReadJmp, 0>;
def : ReadAdvance<ReadJalr, 0>;
def : ReadAdvance<ReadCSR, 0>;
def : ReadAdvance<ReadStoreData, 0>;
def : ReadAdvance<ReadMemBase, 0>;
def : XS2LoadToALUBypass<ReadIALU>;
def : XS2LoadToALUBypass<ReadIALU32>;
def : XS2LoadToALUBypass<ReadShiftImm>;
def : XS2LoadToALUBypass<ReadShiftImm32>;
def : XS2LoadToALUBypass<ReadShiftReg>;
def : XS2LoadToALUBypass<ReadShiftReg32>;
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
def : ReadAdvance<ReadFStoreData, 0>;
def : ReadAdvance<ReadFMemBase, 0>;
def : ReadAdvance<ReadFAdd32, 0>;
def : ReadAdvance<ReadFAdd64, 0>;
def : ReadAdvance<ReadFMul32, 0>;
def : ReadAdvance<ReadFMul64, 0>;
def : ReadAdvance<ReadFMA32, 0>;
def : ReadAdvance<ReadFMA32Addend, 2>; // Cascade FMA
def : ReadAdvance<ReadFMA64, 0>;
def : ReadAdvance<ReadFMA64Addend, 2>; // Cascade FMA
def : ReadAdvance<ReadFDiv32, 0>;
def : ReadAdvance<ReadFDiv64, 0>;
def : ReadAdvance<ReadFSqrt32, 0>;
def : ReadAdvance<ReadFSqrt64, 0>;
def : ReadAdvance<ReadFCmp32, 0>;
def : ReadAdvance<ReadFCmp64, 0>;
def : ReadAdvance<ReadFSGNJ32, 0>;
def : ReadAdvance<ReadFSGNJ64, 0>;
def : ReadAdvance<ReadFMinMax32, 0>;
def : ReadAdvance<ReadFMinMax64, 0>;
def : ReadAdvance<ReadFCvtF32ToI32, 0>;
def : ReadAdvance<ReadFCvtF32ToI64, 0>;
def : ReadAdvance<ReadFCvtF64ToI32, 0>;
def : ReadAdvance<ReadFCvtF64ToI64, 0>;
def : ReadAdvance<ReadFCvtI32ToF32, 0>;
def : ReadAdvance<ReadFCvtI32ToF64, 0>;
def : ReadAdvance<ReadFCvtI64ToF32, 0>;
def : ReadAdvance<ReadFCvtI64ToF64, 0>;
def : ReadAdvance<ReadFCvtF32ToF64, 0>;
def : ReadAdvance<ReadFCvtF64ToF32, 0>;
def : ReadAdvance<ReadFMovF32ToI32, 0>;
def : ReadAdvance<ReadFMovI32ToF32, 0>;
def : ReadAdvance<ReadFMovF64ToI64, 0>;
def : ReadAdvance<ReadFMovI64ToF64, 0>;
def : ReadAdvance<ReadFClass32, 0>;
def : ReadAdvance<ReadFClass64, 0>;

// Zb*
// Zba
def : XS2LoadToALUBypass<ReadSHXADD>;
def : XS2LoadToALUBypass<ReadSHXADD32>;
// Zbb
def : XS2LoadToALUBypass<ReadRotateImm>;
def : XS2LoadToALUBypass<ReadRotateImm32>;
def : XS2LoadToALUBypass<ReadRotateReg>;
def : XS2LoadToALUBypass<ReadRotateReg32>;
def : ReadAdvance<ReadCLZ, 0>;
def : ReadAdvance<ReadCLZ32, 0>;
def : ReadAdvance<ReadCTZ, 0>;
def : ReadAdvance<ReadCTZ32, 0>;
def : ReadAdvance<ReadCPOP, 0>;
def : ReadAdvance<ReadCPOP32, 0>;
def : XS2LoadToALUBypass<ReadORCB>;
def : XS2LoadToALUBypass<ReadIMinMax>;
def : XS2LoadToALUBypass<ReadREV8>;
// Zbkc
def : ReadAdvance<ReadCLMUL, 0>;
// Zbs
def : XS2LoadToALUBypass<ReadSingleBit>;
def : XS2LoadToALUBypass<ReadSingleBitImm>;
// Zbkb
def : XS2LoadToALUBypass<ReadBREV8>;
def : XS2LoadToALUBypass<ReadPACK>;
def : XS2LoadToALUBypass<ReadPACK32>;
def : XS2LoadToALUBypass<ReadZIP>;
// Zbkx
def : ReadAdvance<ReadXPERM, 0>;

//===----------------------------------------------------------------------===//
// Unsupported extensions
defm : UnsupportedSchedV;
defm : UnsupportedSchedZfa;
defm : UnsupportedSchedZfh;
defm : UnsupportedSchedSFB;
defm : UnsupportedSchedZabha;
defm : UnsupportedSchedXsfvcp;
defm : UnsupportedSchedZvk;
}

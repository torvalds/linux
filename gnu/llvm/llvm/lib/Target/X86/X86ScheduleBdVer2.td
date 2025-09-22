//=- X86ScheduleBdVer2.td - X86 BdVer2 (Piledriver) Scheduling * tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for AMD bdver2 (Piledriver) to support
// instruction scheduling and other instruction cost heuristics.
// Based on:
//  * AMD Software Optimization Guide for AMD Family 15h Processors.
//    https://support.amd.com/TechDocs/47414_15h_sw_opt_guide.pdf
//  * The microarchitecture of Intel, AMD and VIA CPUs, By Agner Fog
//    http://www.agner.org/optimize/microarchitecture.pdf
//  * https://www.realworldtech.com/bulldozer/
//    Yes, that is for Bulldozer aka bdver1, not Piledriver aka bdver2.
//
//===----------------------------------------------------------------------===//

def BdVer2Model : SchedMachineModel {
  let IssueWidth = 4; // Up to 4 IPC can be decoded, issued, retired.
  let MicroOpBufferSize = 128; // RCU reorder buffer size, which is unconfirmed.
  let LoopMicroOpBufferSize = -1; // There does not seem to be a loop buffer.
  let LoadLatency = 4; // L1 data cache has a 4-cycle load-to-use latency.
  let HighLatency = 25; // FIXME: any better choice?
  let MispredictPenalty = 20; // Minimum branch misdirection penalty.

  let PostRAScheduler = 1; // Enable Post RegAlloc Scheduler pass.

  // FIXME: Incomplete. This flag is set to allow the scheduler to assign
  //        a default model to unrecognized opcodes.
  let CompleteModel = 0;
} // SchedMachineModel

let SchedModel = BdVer2Model in {


//===----------------------------------------------------------------------===//
// Pipes
//===----------------------------------------------------------------------===//

// There are total of eight pipes.

//===----------------------------------------------------------------------===//
// Integer execution pipes
//

// Two EX (ALU) pipes.
def PdEX0  : ProcResource<1>; // ALU, Integer Pipe0
def PdEX1  : ProcResource<1>; // ALU, Integer Pipe1
def PdEX01 : ProcResGroup<[PdEX0, PdEX1]>;

// Two AGLU pipes, identical.
def PdAGLU01 : ProcResource<2>; // AGU, Integer Pipe[23]

//===----------------------------------------------------------------------===//
// Floating point execution pipes
//

// Four FPU pipes.

def PdFPU0 : ProcResource<1>; // Vector/FPU Pipe0
def PdFPU1 : ProcResource<1>; // Vector/FPU Pipe1
def PdFPU2 : ProcResource<1>; // Vector/FPU Pipe2
def PdFPU3 : ProcResource<1>; // Vector/FPU Pipe3

// FPU grouping
def PdFPU01 : ProcResGroup<[PdFPU0, PdFPU1]>;
def PdFPU23 : ProcResGroup<[PdFPU2, PdFPU3]>;


//===----------------------------------------------------------------------===//
// RCU
//===----------------------------------------------------------------------===//

// The Retire Control Unit on Piledriver can retire up to 4 macro-ops per cycle.
// On the other hand, the RCU reorder buffer size for Piledriver does not
// seem be specified in any trustworthy source.
// But as per https://www.realworldtech.com/bulldozer/6/ the Bulldozer had
// RCU reorder buffer size of 128. So that is a good guess for now.
def PdRCU : RetireControlUnit<128, 4>;


//===----------------------------------------------------------------------===//
// Pipelines
//===----------------------------------------------------------------------===//

// There are total of two pipelines, each one with it's own scheduler.

//===----------------------------------------------------------------------===//
// Integer Pipeline Scheduling
//

// There is one Integer Scheduler per core.

// Integer physical register file has 96 registers of 64-bit.
def PdIntegerPRF : RegisterFile<96, [GR64, CCR]>;

// Unified Integer, Memory Scheduler has 40 entries.
def PdEX : ProcResGroup<[PdEX0, PdEX1, PdAGLU01]> {
  // Up to 4 IPC can be decoded, issued, retired.
  let BufferSize = 40;
}


//===----------------------------------------------------------------------===//
// FPU Pipeline Scheduling
//

// The FPU unit is shared between the two cores.

// FP physical register file has 160 registers of 128-bit.
// Operations on 256-bit data types are cracked into two COPs.
def PdFpuPRF : RegisterFile<160, [VR64, VR128, VR256], [1, 1, 2]>;

// Unified FP Scheduler has 64 entries,
def PdFPU : ProcResGroup<[PdFPU0, PdFPU1, PdFPU2, PdFPU3]> {
  // Up to 4 IPC can be decoded, issued, retired.
  let BufferSize = 64;
}


//===----------------------------------------------------------------------===//
// Functional units
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Load-Store Units
//

let Super = PdAGLU01 in
def PdLoad  : ProcResource<2> {
  // For Piledriver, the load queue is 40 entries deep.
  let BufferSize = 40;
}

def PdLoadQueue : LoadQueue<PdLoad>;

let Super = PdAGLU01 in
def PdStore : ProcResource<1> {
  // For Piledriver, the store queue is 24 entries deep.
  let BufferSize = 24;
}

def PdStoreQueue : StoreQueue<PdStore>;

//===----------------------------------------------------------------------===//
// Integer Execution Units
//

def PdDiv    : ProcResource<1>; // PdEX0; unpipelined integer division
def PdCount  : ProcResource<1>; // PdEX0; POPCNT, LZCOUNT

def PdMul    : ProcResource<1>; // PdEX1; integer multiplication
def PdBranch : ProcResource<1>; // PdEX1; JMP, fused branches

//===----------------------------------------------------------------------===//
// Floating-Point Units
//

// Two FMAC/FPFMA units.
def PdFPFMA  : ProcResource<2>; // PdFPU0, PdFPU1

// One 128-bit integer multiply-accumulate unit.
def PdFPMMA  : ProcResource<1>; // PdFPU0

// One fp conversion unit.
def PdFPCVT  : ProcResource<1>; // PdFPU0

// One unit for shuffles, packs, permutes, shifts.
def PdFPXBR  : ProcResource<1>; // PdFPU1

// Two 128-bit packed integer units.
def PdFPMAL  : ProcResource<2>; // PdFPU2, PdFPU3

// One FP store unit.
def PdFPSTO  : ProcResource<1>; // PdFPU3


//===----------------------------------------------------------------------===//
// Basic helper classes.
//===----------------------------------------------------------------------===//

// Many SchedWrites are defined in pairs with and without a folded load.
// Instructions with folded loads are usually micro-fused, so they only appear
// as two micro-ops when dispatched by the schedulers.
// This multiclass defines the resource usage for variants with and without
// folded loads.
multiclass PdWriteRes<SchedWrite SchedRW,
                      list<ProcResourceKind> ExePorts, int Lat = 1,
                      list<int> Res = [], int UOps = 1> {
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ReleaseAtCycles = Res;
    let NumMicroOps = UOps;
  }
}

multiclass __pdWriteResPair<X86FoldableSchedWrite SchedRW,
                            list<ProcResourceKind> ExePorts, int Lat,
                            list<int> Res, int UOps,
                            int LoadLat, int LoadRes, int LoadUOps> {
  defm : PdWriteRes<SchedRW, ExePorts, Lat, Res, UOps>;

  defm : PdWriteRes<SchedRW.Folded,
                    !listconcat([PdLoad], ExePorts),
                    !add(Lat, LoadLat),
                    !if(!and(!empty(Res), !eq(LoadRes, 1)),
                      [],
                      !listconcat([LoadRes],
                        !if(!empty(Res),
                          !listsplat(1, !size(ExePorts)),
                          Res))),
                    !add(UOps, LoadUOps)>;
}

multiclass PdWriteResExPair<X86FoldableSchedWrite SchedRW,
                            list<ProcResourceKind> ExePorts, int Lat = 1,
                            list<int> Res = [], int UOps = 1,
                            int LoadUOps = 0> {
  defm : __pdWriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                          /*LoadLat*/4, /*LoadRes*/3, LoadUOps>;
}

multiclass PdWriteResXMMPair<X86FoldableSchedWrite SchedRW,
                             list<ProcResourceKind> ExePorts, int Lat = 1,
                             list<int> Res = [], int UOps = 1,
                             int LoadUOps = 0> {
  defm : __pdWriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                           /*LoadLat*/5, /*LoadRes*/3, LoadUOps>;
}

multiclass PdWriteResYMMPair<X86FoldableSchedWrite SchedRW,
                             list<ProcResourceKind> ExePorts, int Lat,
                             list<int> Res = [], int UOps = 2,
                             int LoadUOps = 0> {
  defm : __pdWriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                           /*LoadLat*/5, /*LoadRes*/3, LoadUOps>;
}

//===----------------------------------------------------------------------===//
// Here be dragons.
//===----------------------------------------------------------------------===//

// L1 data cache has a 4-cycle load-to-use latency, so ReadAfterLd registers
// needn't be available until 4 cycles after the memory operand.
def : ReadAdvance<ReadAfterLd, 4>;

// Vector loads are 5 cycles, so ReadAfterVec*Ld registers needn't be available
// until 5 cycles after the memory operand.
def : ReadAdvance<ReadAfterVecLd, 5>;
def : ReadAdvance<ReadAfterVecXLd, 5>;
def : ReadAdvance<ReadAfterVecYLd, 5>;

// Transfer from int domain to ivec domain incurs additional latency of 8..10cy
// Reference: Agner, Microarchitecture, "AMD Bulldozer, Piledriver, Steamroller
// and Excavator pipeline", "Data delay between different execution domains"
def : ReadAdvance<ReadInt2Fpu, -10>;

// A folded store needs a cycle on the PdStore for the store data.
def : WriteRes<WriteRMW, [PdStore]>;

////////////////////////////////////////////////////////////////////////////////
// Loads, stores, and moves, not folded with other operations.
////////////////////////////////////////////////////////////////////////////////

def : WriteRes<WriteLoad,    [PdLoad]> { let Latency = 5; let ReleaseAtCycles = [2]; }
def : WriteRes<WriteStore,   [PdStore]>;
def : WriteRes<WriteStoreNT, [PdStore]>;
def : WriteRes<WriteMove,    [PdEX01]> { let ReleaseAtCycles = [2]; }
defm : X86WriteResUnsupported<WriteVecMaskedGatherWriteback>;

// Load/store MXCSR.
// FIXME: These are copy and pasted from WriteLoad/Store.
def : WriteRes<WriteLDMXCSR, [PdLoad]> { let Latency = 5; }
def : WriteRes<WriteSTMXCSR, [PdStore]> { let NumMicroOps = 2; let ReleaseAtCycles = [18]; }

// Treat misc copies as a move.
def : InstRW<[WriteMove], (instrs COPY)>;

////////////////////////////////////////////////////////////////////////////////
// Idioms that clear a register, like xorps %xmm0, %xmm0.
// These can often bypass execution ports completely.
////////////////////////////////////////////////////////////////////////////////

def : WriteRes<WriteZero, [/*No ExePorts*/]>;

////////////////////////////////////////////////////////////////////////////////
// Branches don't produce values, so they have no latency, but they still
// consume resources. Indirect branches can fold loads.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResExPair<WriteJump,  [PdEX1, PdBranch]>;

////////////////////////////////////////////////////////////////////////////////
// Special case scheduling classes.
////////////////////////////////////////////////////////////////////////////////

def : WriteRes<WriteSystem,     [PdEX01]> { let Latency = 100; }
def : WriteRes<WriteMicrocoded, [PdEX01]> { let Latency = 100; }
def : WriteRes<WriteFence,      [PdStore]>;

def PdWriteXLAT : SchedWriteRes<[PdEX01]> {
  let Latency = 6;
}
def : InstRW<[PdWriteXLAT], (instrs XLAT)>;

def PdWriteLARrr : SchedWriteRes<[PdEX01]> {
  let Latency = 184;
  let ReleaseAtCycles = [375];
  let NumMicroOps = 45;
}
def : InstRW<[PdWriteLARrr], (instregex "LAR(16|32|64)rr",
                                        "LSL(16|32|64)rr")>;

// Nops don't have dependencies, so there's no actual latency, but we set this
// to '1' to tell the scheduler that the nop uses an ALU slot for a cycle.
def : WriteRes<WriteNop, [PdEX01]> { let ReleaseAtCycles = [2]; }

////////////////////////////////////////////////////////////////////////////////
// Arithmetic.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResExPair<WriteALU,     [PdEX01], 1, [2]>;

def PdWriteALURMW : SchedWriteRes<[PdLoad, PdEX01, PdStore]> {
  let Latency = 6;
  let ReleaseAtCycles = [3, 2, 1];
  let NumMicroOps = 1;
}
def : SchedAlias<WriteALURMW, PdWriteALURMW>;

def PdWriteLXADD : SchedWriteRes<[PdEX01]> {
  let Latency = 6;
  let ReleaseAtCycles = [88];
  let NumMicroOps = 4;
}
def : InstRW<[PdWriteLXADD], (instrs LXADD8, LXADD16, LXADD32, LXADD64)>;

def PdWriteBMI1 : SchedWriteRes<[PdEX01]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteBMI1],
             (instrs BLCFILL32rr, BLCFILL64rr, BLCI32rr, BLCI64rr,
                     BLCIC32rr, BLCIC64rr, BLCMSK32rr, BLCMSK64rr,
                     BLCS32rr, BLCS64rr, BLSFILL32rr, BLSFILL64rr,
                     BLSIC32rr, BLSIC64rr, T1MSKC32rr, T1MSKC64rr,
                     TZMSK32rr, TZMSK64rr)>;

def PdWriteBMI1m : SchedWriteRes<[PdLoad, PdEX01]> {
  let Latency = 6;
  let ReleaseAtCycles = [3, 3];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteBMI1m],
             (instrs BLCFILL32rm, BLCFILL64rm, BLCI32rm, BLCI64rm,
                     BLCIC32rm, BLCIC64rm, BLCMSK32rm, BLCMSK64rm,
                     BLCS32rm, BLCS64rm, BLSFILL32rm, BLSFILL64rm,
                     BLSIC32rm, BLSIC64rm, T1MSKC32rm, T1MSKC64rm,
                     TZMSK32rm, TZMSK64rm)>;

defm : PdWriteResExPair<WriteADC,    [PdEX01],                  1,  [2]>;

def PdWriteADCSBB64ri32 : SchedWriteRes<[PdEX01]> {
  let ReleaseAtCycles = [3];
}
def : InstRW<[PdWriteADCSBB64ri32], (instrs ADC64ri32, SBB64ri32)>;

defm : PdWriteRes<WriteBSWAP32,      [PdEX01]>;
defm : PdWriteRes<WriteBSWAP64,      [PdEX01]>;
defm : PdWriteRes<WriteCMPXCHG,      [PdEX1],                   3,  [3],        5>;
defm : PdWriteRes<WriteCMPXCHGRMW,   [PdEX1, PdStore, PdLoad],  3,  [44, 1, 1], 2>;
defm : PdWriteRes<WriteXCHG,         [PdEX1],                   1,  [],         2>;

def PdWriteCMPXCHG8rr : SchedWriteRes<[PdEX1]> {
  let Latency = 3;
  let ReleaseAtCycles = [3];
  let NumMicroOps = 3;
}
def : InstRW<[PdWriteCMPXCHG8rr], (instrs CMPXCHG8rr)>;

def PdWriteCMPXCHG8rm : SchedWriteRes<[PdEX1]> {
  let Latency = 3;
  let ReleaseAtCycles = [23];
  let NumMicroOps = 5;
}
def : InstRW<[PdWriteCMPXCHG8rm], (instrs CMPXCHG8rm)>;

def PdWriteCMPXCHG16rm_CMPXCHG32rm_CMPXCHG64rm : SchedWriteRes<[PdEX1]> {
  let Latency = 3;
  let ReleaseAtCycles = [21];
  let NumMicroOps = 6;
}
def : InstRW<[PdWriteCMPXCHG16rm_CMPXCHG32rm_CMPXCHG64rm],
             (instrs CMPXCHG16rm, CMPXCHG32rm, CMPXCHG64rm)>;

def PdWriteCMPXCHG8B : SchedWriteRes<[PdEX1]> {
  let Latency = 3;
  let ReleaseAtCycles = [26];
  let NumMicroOps = 18;
}
def : InstRW<[PdWriteCMPXCHG8B], (instrs CMPXCHG8B)>;

def PdWriteCMPXCHG16B : SchedWriteRes<[PdEX1]> {
  let Latency = 3;
  let ReleaseAtCycles = [69];
  let NumMicroOps = 22;
}
def : InstRW<[PdWriteCMPXCHG16B], (instrs CMPXCHG16B)>;

def PdWriteXADDm : SchedWriteRes<[PdEX1]> {
  let Latency = 6;
  let ReleaseAtCycles = [20];
  let NumMicroOps = 4;
}
def : InstRW<[PdWriteXADDm], (instrs XADD8rm, XADD16rm, XADD32rm, XADD64rm)>;

defm : PdWriteResExPair<WriteIMul8,     [PdEX1, PdMul],          4,  [1, 4]>;
defm : PdWriteResExPair<WriteIMul16,    [PdEX1, PdMul],          4,  [1, 5],    2>;
defm : PdWriteResExPair<WriteIMul16Imm, [PdEX1, PdMul],          5,  [1, 5],    2>;
defm : PdWriteResExPair<WriteIMul16Reg, [PdEX1, PdMul],          4,  [1, 2]>;
defm : PdWriteResExPair<WriteIMul32,    [PdEX1, PdMul],          4,  [1, 4]>;
defm : PdWriteResExPair<WriteIMul32Imm, [PdEX1, PdMul],          4,  [1, 2],    1, 1>;
defm : PdWriteResExPair<WriteIMul32Reg, [PdEX1, PdMul],          4,  [1, 2]>;
defm : PdWriteResExPair<WriteIMul64,    [PdEX1, PdMul],          6,  [1, 6]>;
defm : PdWriteResExPair<WriteIMul64Imm, [PdEX1, PdMul],          6,  [1, 4],1, 1>;
defm : PdWriteResExPair<WriteIMul64Reg, [PdEX1, PdMul],          6,  [1, 4]>;

// BMI2 MULX
defm : X86WriteResUnsupported<WriteIMulH>;
defm : X86WriteResUnsupported<WriteIMulHLd>;
defm : X86WriteResPairUnsupported<WriteMULX32>;
defm : X86WriteResPairUnsupported<WriteMULX64>;

defm : PdWriteResExPair<WriteDiv8,    [PdEX1, PdDiv],           12,  [1, 12]>;
defm : PdWriteResExPair<WriteDiv16,   [PdEX1, PdDiv],           15,  [1, 15],   2>;
defm : PdWriteResExPair<WriteDiv32,   [PdEX1, PdDiv],           14,  [1, 14],   2>;
defm : PdWriteResExPair<WriteDiv64,   [PdEX1, PdDiv],           14,  [1, 14],   2>;

defm : PdWriteResExPair<WriteIDiv8,   [PdEX1, PdDiv],           12,  [1, 12]>;
defm : PdWriteResExPair<WriteIDiv16,  [PdEX1, PdDiv],           15,  [1, 17],   2>;
defm : PdWriteResExPair<WriteIDiv32,  [PdEX1, PdDiv],           14,  [1, 25],   2>;
defm : PdWriteResExPair<WriteIDiv64,  [PdEX1, PdDiv],           14,  [1, 14],   2>;

defm : PdWriteResExPair<WriteCRC32,   [PdEX01],                  2,  [4],       3>;

def PdWriteCRC32r32r16 : SchedWriteRes<[PdEX01]> {
  let Latency = 5;
  let ReleaseAtCycles = [10];
  let NumMicroOps = 5;
}
def : InstRW<[PdWriteCRC32r32r16], (instrs CRC32r32r16)>;

def PdWriteCRC32r32r32 : SchedWriteRes<[PdEX01]> {
  let Latency = 6;
  let ReleaseAtCycles = [12];
  let NumMicroOps = 7;
}
def : InstRW<[PdWriteCRC32r32r32], (instrs CRC32r32r32)>;

def PdWriteCRC32r64r64 : SchedWriteRes<[PdEX01]> {
  let Latency = 10;
  let ReleaseAtCycles = [17];
  let NumMicroOps = 11;
}
def : InstRW<[PdWriteCRC32r64r64], (instrs CRC32r64r64)>;

defm : PdWriteResExPair<WriteCMOV,    [PdEX01]>; // Conditional move.

def PdWriteCMOVm : SchedWriteRes<[PdLoad, PdEX01]> {
  let Latency = 5;
  let ReleaseAtCycles = [3, 3];
  let NumMicroOps = 2;
}

def PdWriteCMOVmVar : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<CheckImmOperand_s<7, "X86::COND_BE">>, [PdWriteCMOVm]>,
  SchedVar<MCSchedPredicate<CheckImmOperand_s<7, "X86::COND_A">>,  [PdWriteCMOVm]>,
  SchedVar<MCSchedPredicate<CheckImmOperand_s<7, "X86::COND_L">>,  [PdWriteCMOVm]>,
  SchedVar<MCSchedPredicate<CheckImmOperand_s<7, "X86::COND_GE">>, [PdWriteCMOVm]>,
  SchedVar<MCSchedPredicate<CheckImmOperand_s<7, "X86::COND_LE">>, [PdWriteCMOVm]>,
  SchedVar<MCSchedPredicate<CheckImmOperand_s<7, "X86::COND_G">>,  [PdWriteCMOVm]>,
  SchedVar<NoSchedPred, [WriteCMOV.Folded]>
]>;

def : InstRW<[PdWriteCMOVmVar], (instrs CMOV16rm, CMOV32rm, CMOV64rm)>;

defm : PdWriteRes<WriteFCMOV,        [PdFPU0, PdFPFMA]>; // x87 conditional move.

def : WriteRes<WriteSETCC,           [PdEX01]>; // Setcc.
def : WriteRes<WriteSETCCStore,      [PdEX01, PdStore]>;

def PdWriteSETGEmSETGmSETLEmSETLm : SchedWriteRes<[PdEX01]> {
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}

def PdSETGEmSETGmSETLEmSETLm :  SchedWriteVariant<[
  SchedVar<MCSchedPredicate<CheckImmOperand_s<5, "X86::COND_GE">>, [PdWriteSETGEmSETGmSETLEmSETLm]>,
  SchedVar<MCSchedPredicate<CheckImmOperand_s<5, "X86::COND_G">>,  [PdWriteSETGEmSETGmSETLEmSETLm]>,
  SchedVar<MCSchedPredicate<CheckImmOperand_s<5, "X86::COND_LE">>, [PdWriteSETGEmSETGmSETLEmSETLm]>,
  SchedVar<MCSchedPredicate<CheckImmOperand_s<5, "X86::COND_L">>,  [PdWriteSETGEmSETGmSETLEmSETLm]>,
  SchedVar<NoSchedPred,                                            [WriteSETCCStore]>
]>;
def : InstRW<[PdSETGEmSETGmSETLEmSETLm], (instrs SETCCm)>;

defm : PdWriteRes<WriteLAHFSAHF,      [PdEX01],          2,  [4],       2>;

def PdWriteLAHF : SchedWriteRes<[PdEX01]> {
  let Latency = 2;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 4;
}
def : InstRW<[PdWriteLAHF], (instrs LAHF)>;

def PdWriteSAHF : SchedWriteRes<[PdEX01]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteSAHF], (instrs SAHF)>;

defm : PdWriteRes<WriteBitTest,          [PdEX01],         1, [2],      1>;
defm : PdWriteRes<WriteBitTestImmLd,     [PdEX01, PdLoad], 5, [2,  3],  1>;
defm : PdWriteRes<WriteBitTestRegLd,     [PdEX01, PdLoad], 5, [7,  2],  7>;
defm : PdWriteRes<WriteBitTestSet,       [PdEX01],         2, [2],      2>;
defm : PdWriteRes<WriteBitTestSetImmLd,  [PdEX01, PdLoad], 6, [1,  1],  4>;
defm : PdWriteRes<WriteBitTestSetRegLd,  [PdEX01, PdLoad], 6, [1,  1], 10>;

def PdWriteBTSIm : SchedWriteRes<[PdEX01, PdLoad]> {
  let Latency = 7;
  let ReleaseAtCycles = [42, 1];
  let NumMicroOps = 4;
}
def : SchedAlias<WriteBitTestSetImmRMW, PdWriteBTSIm>;
def PdWriteBTSRm : SchedWriteRes<[PdEX01, PdLoad]> {
  let Latency = 7;
  let ReleaseAtCycles = [44, 1];
  let NumMicroOps = 10;
}
def : SchedAlias<WriteBitTestSetRegRMW, PdWriteBTSRm>;

// This is for simple LEAs with one or two input operands.
def : WriteRes<WriteLEA,              [PdEX01]> { let ReleaseAtCycles = [2]; }

// This write is used for slow LEA instructions.
def PdWrite3OpsLEA : SchedWriteRes<[PdEX01]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
}

// On Piledriver, a slow LEA is either a 3Ops LEA (base, index, offset),
// or an LEA with a `Scale` value different than 1.
def PdSlowLEAPredicate : MCSchedPredicate<
  CheckAny<[
    // A 3-operand LEA (base, index, offset).
    IsThreeOperandsLEAFn,
    // An LEA with a "Scale" different than 1.
    CheckAll<[
      CheckIsImmOperand<2>,
      CheckNot<CheckImmOperand<2, 1>>
    ]>
  ]>
>;

def PdWriteLEA : SchedWriteVariant<[
    SchedVar<PdSlowLEAPredicate, [PdWrite3OpsLEA]>,
    SchedVar<NoSchedPred,        [WriteLEA]>
]>;

def : InstRW<[PdWriteLEA], (instrs LEA32r, LEA64r, LEA64_32r)>;

def PdWriteLEA16r : SchedWriteRes<[PdEX01]> {
  let ReleaseAtCycles = [3];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteLEA16r], (instrs LEA16r)>;

// Bit counts.
defm : PdWriteResExPair<WriteBSF,     [PdEX01],          3,  [6],     6, 2>;
defm : PdWriteResExPair<WriteBSR,     [PdEX01],          4,  [8],     7, 2>;
defm : PdWriteResExPair<WritePOPCNT,  [PdEX01],          4,  [4]>;
defm : PdWriteResExPair<WriteLZCNT,   [PdEX0],           2,  [2],     2>;
defm : PdWriteResExPair<WriteTZCNT,   [PdEX0],           2,  [2],     2>;

// BMI1 BEXTR, BMI2 BZHI
defm : PdWriteResExPair<WriteBEXTR,   [PdEX01],          2,  [2],    2>;
defm : PdWriteResExPair<WriteBLS,     [PdEX01],          2,  [2],    2>;
defm : PdWriteResExPair<WriteBZHI,    [PdEX01]>;

def PdWriteBEXTRI : SchedWriteRes<[PdEX01]> {
  let Latency = 2;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteBEXTRI], (instrs BEXTRI32ri, BEXTRI64ri)>;

def PdWriteBEXTRIm : SchedWriteRes<[PdEX01]> {
  let Latency = 2;
  let ReleaseAtCycles = [5];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteBEXTRIm], (instrs BEXTRI32mi, BEXTRI64mi)>;

////////////////////////////////////////////////////////////////////////////////
// Integer shifts and rotates.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResExPair<WriteShift,    [PdEX01], 1, [2]>;
defm : PdWriteResExPair<WriteShiftCL,  [PdEX01]>;
defm : PdWriteResExPair<WriteRotate,   [PdEX01], 1, [2]>;
defm : PdWriteResExPair<WriteRotateCL, [PdEX01]>;

def PdWriteRCL8rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 12;
  let ReleaseAtCycles = [24];
  let NumMicroOps = 26;
}
def : InstRW<[PdWriteRCL8rCL], (instrs RCL8rCL)>;

def PdWriteRCR8ri : SchedWriteRes<[PdEX01]> {
  let Latency = 12;
  let ReleaseAtCycles = [23];
  let NumMicroOps = 23;
}
def : InstRW<[PdWriteRCR8ri], (instrs RCR8ri)>;

def PdWriteRCR8rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 11;
  let ReleaseAtCycles = [22];
  let NumMicroOps = 24;
}
def : InstRW<[PdWriteRCR8rCL], (instrs RCR8rCL)>;

def PdWriteRCL16rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 10;
  let ReleaseAtCycles = [20];
  let NumMicroOps = 22;
}
def : InstRW<[PdWriteRCL16rCL], (instrs RCL16rCL)>;

def PdWriteRCR16ri : SchedWriteRes<[PdEX01]> {
  let Latency = 10;
  let ReleaseAtCycles = [19];
  let NumMicroOps = 19;
}
def : InstRW<[PdWriteRCR16ri], (instrs RCR16ri)>;

def PdWriteRCL3264rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 7;
  let ReleaseAtCycles = [14];
  let NumMicroOps = 17;
}
def : InstRW<[PdWriteRCL3264rCL], (instrs RCL32rCL, RCL64rCL)>;

def PdWriteRCR3264rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 7;
  let ReleaseAtCycles = [13];
  let NumMicroOps = 16;
}
def : InstRW<[PdWriteRCR3264rCL], (instrs RCR32rCL, RCR64rCL)>;

def PdWriteRCR32riRCR64ri : SchedWriteRes<[PdEX01]> {
  let Latency = 7;
  let ReleaseAtCycles = [14];
  let NumMicroOps = 15;
}
def : InstRW<[PdWriteRCR32riRCR64ri], (instrs RCR32ri, RCR64ri)>;


def PdWriteRCR16rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 9;
  let ReleaseAtCycles = [18];
  let NumMicroOps = 20;
}
def : InstRW<[PdWriteRCR16rCL], (instrs RCR16rCL)>;

def PdWriteRCL16ri : SchedWriteRes<[PdEX01]> {
  let Latency = 11;
  let ReleaseAtCycles = [21];
  let NumMicroOps = 21;
}
def : InstRW<[PdWriteRCL16ri], (instrs RCL16ri)>;

def PdWriteRCL3264ri : SchedWriteRes<[PdEX01]> {
  let Latency = 8;
  let ReleaseAtCycles = [15];
  let NumMicroOps = 16;
}
def : InstRW<[PdWriteRCL3264ri], (instrs RCL32ri, RCL64ri)>;

def PdWriteRCL8ri : SchedWriteRes<[PdEX01]> {
  let Latency = 13;
  let ReleaseAtCycles = [25];
  let NumMicroOps = 25;
}
def : InstRW<[PdWriteRCL8ri], (instrs RCL8ri)>;

// SHLD/SHRD.
defm : PdWriteRes<WriteSHDrri,       [PdEX01],         3, [6], 6>;
defm : PdWriteRes<WriteSHDrrcl,      [PdEX01],         3, [8], 7>;

def PdWriteSHLD16rrCLSHLD32rrCLSHRD32rrCL : SchedWriteRes<[PdEX01]> {
  let Latency = 3;
  let ReleaseAtCycles = [6];
  let NumMicroOps = 7;
}
def : InstRW<[PdWriteSHLD16rrCLSHLD32rrCLSHRD32rrCL], (instrs SHLD16rrCL,
                                                              SHLD32rrCL,
                                                              SHRD32rrCL)>;

defm : PdWriteRes<WriteSHDmri,       [PdLoad, PdEX01], 4, [1, 22], 8>;
defm : PdWriteRes<WriteSHDmrcl,      [PdLoad, PdEX01], 4, [1, 22], 8>;

////////////////////////////////////////////////////////////////////////////////
// Floating point. This covers both scalar and vector operations.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteRes<WriteFLD0,               [PdFPU1, PdFPSTO], 3>;
defm : PdWriteRes<WriteFLD1,               [PdFPU1, PdFPSTO], 3>;
defm : PdWriteRes<WriteFLDC,               [PdFPU1, PdFPSTO], 3>;

defm : PdWriteRes<WriteFLoad,              [PdLoad, PdFPU01, PdFPFMA], 5, [3, 1, 3]>;
defm : PdWriteRes<WriteFLoadX,             [PdLoad, PdFPU01, PdFPFMA], 5, [3, 1, 3]>;
defm : PdWriteRes<WriteFLoadY,             [PdLoad, PdFPU01, PdFPFMA], 5, [3, 1, 3], 2>;

defm : PdWriteRes<WriteFMaskedLoad,        [PdLoad, PdFPU01, PdFPFMA], 6, [3, 1, 4]>;
defm : PdWriteRes<WriteFMaskedLoadY,       [PdLoad, PdFPU01, PdFPFMA], 6, [3, 2, 4], 2>;

defm : PdWriteRes<WriteFStore,             [PdStore, PdFPU23, PdFPSTO], 2, [1,  3, 1]>;
defm : PdWriteRes<WriteFStoreX,            [PdStore, PdFPU23, PdFPSTO], 1, [1,  3, 1]>;
defm : PdWriteRes<WriteFStoreY,            [PdStore, PdFPU23, PdFPSTO], 1, [1, 36, 2], 4>;

def PdWriteMOVHPm : SchedWriteRes<[PdStore, PdFPU23,  PdFPSTO]> {
  let Latency = 2;
  let ReleaseAtCycles = [1, 3, 1];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteMOVHPm], (instrs MOVHPDmr, MOVHPSmr, VMOVHPDmr, VMOVHPSmr)>;

def PdWriteVMOVUPDYmrVMOVUPSYmr : SchedWriteRes<[PdStore, PdFPU1,  PdFPSTO]> {
  let NumMicroOps = 8;
}
def : InstRW<[PdWriteVMOVUPDYmrVMOVUPSYmr], (instrs VMOVUPDYmr, VMOVUPSYmr)>;

defm : PdWriteRes<WriteFStoreNT,           [PdStore, PdFPU1,  PdFPSTO], 3>;
defm : PdWriteRes<WriteFStoreNTX,          [PdStore, PdFPU1,  PdFPSTO], 3>;
defm : PdWriteRes<WriteFStoreNTY,          [PdStore, PdFPU1,  PdFPSTO], 3, [2, 2, 2], 4>;

defm : PdWriteRes<WriteFMaskedStore32,     [PdStore, PdFPU01, PdFPFMA], 6, [1, 1, 188], 18>;
defm : PdWriteRes<WriteFMaskedStore64,     [PdStore, PdFPU01, PdFPFMA], 6, [1, 1, 188], 18>;
defm : PdWriteRes<WriteFMaskedStore32Y,    [PdStore, PdFPU01, PdFPFMA], 6, [2, 2, 376], 34>;
defm : PdWriteRes<WriteFMaskedStore64Y,    [PdStore, PdFPU01, PdFPFMA], 6, [2, 2, 376], 34>;

defm : PdWriteRes<WriteFMove,              [PdFPU01, PdFPFMA]>;
defm : PdWriteRes<WriteFMoveX,             [PdFPU01, PdFPFMA], 1, [1, 2]>;
defm : PdWriteRes<WriteFMoveY,             [PdFPU01, PdFPFMA], 2, [2, 2], 2>;
defm : X86WriteResUnsupported<WriteFMoveZ>;

defm : PdWriteRes<WriteEMMS,               [PdFPU01, PdFPFMA], 2>;

defm : PdWriteResXMMPair<WriteFAdd,         [PdFPU0, PdFPFMA],  5>;
defm : PdWriteResXMMPair<WriteFAddX,        [PdFPU0, PdFPFMA],  5>;
defm : PdWriteResYMMPair<WriteFAddY,        [PdFPU0, PdFPFMA],  5, [1, 2]>;
defm : X86WriteResPairUnsupported<WriteFAddZ>;

def PdWriteX87Add: SchedWriteRes<[PdLoad, PdFPU0, PdFPFMA]> {
  let Latency = 5;
  let ReleaseAtCycles = [3, 1, 10];
}
def : InstRW<[PdWriteX87Add], (instrs ADD_FI16m,  ADD_FI32m,  ADD_F32m,  ADD_F64m,
                                      SUB_FI16m,  SUB_FI32m,  SUB_F32m,  SUB_F64m,
                                      SUBR_FI16m, SUBR_FI32m, SUBR_F32m, SUBR_F64m)>;

defm : PdWriteResXMMPair<WriteFAdd64,       [PdFPU0, PdFPFMA],  5>;
defm : PdWriteResXMMPair<WriteFAdd64X,      [PdFPU0, PdFPFMA],  5>;
defm : PdWriteResYMMPair<WriteFAdd64Y,      [PdFPU0, PdFPFMA],  5, [1, 2]>;
defm : X86WriteResPairUnsupported<WriteFAdd64Z>;

defm : PdWriteResXMMPair<WriteFCmp,         [PdFPU0, PdFPFMA],  2>;
defm : PdWriteResXMMPair<WriteFCmpX,        [PdFPU0, PdFPFMA],  2>;
defm : PdWriteResYMMPair<WriteFCmpY,        [PdFPU0, PdFPFMA],  2, [1, 2]>;
defm : X86WriteResPairUnsupported<WriteFCmpZ>;

defm : PdWriteResXMMPair<WriteFCmp64,       [PdFPU0, PdFPFMA],  2>;
defm : PdWriteResXMMPair<WriteFCmp64X,      [PdFPU0, PdFPFMA],  2>;
defm : PdWriteResYMMPair<WriteFCmp64Y,      [PdFPU0, PdFPFMA],  2, [1, 2]>;
defm : X86WriteResPairUnsupported<WriteFCmp64Z>;

defm : PdWriteResXMMPair<WriteFCom,         [PdFPU0, PdFPFMA, PdEX0], 1, [], 2>;
defm : PdWriteResXMMPair<WriteFComX,        [PdFPU0, PdFPFMA, PdEX0], 1, [], 2>;

def PdWriteFCOMPm : SchedWriteRes<[PdFPU1, PdFPFMA]> {
  let Latency = 6;
}
def : InstRW<[PdWriteFCOMPm], (instrs FCOM32m, FCOM64m, FCOMP32m, FCOMP64m)>;

def PdWriteTST_F_UCOM_FPPr : SchedWriteRes<[PdFPU1, PdFPFMA]>;
def : InstRW<[PdWriteTST_F_UCOM_FPPr], (instrs TST_F, UCOM_FPPr)>;

defm : PdWriteResXMMPair<WriteFMul,         [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResXMMPair<WriteFMulX,        [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResYMMPair<WriteFMulY,        [PdFPU1, PdFPFMA],  5, [1, 2]>;
defm : X86WriteResPairUnsupported<WriteFMulZ>;

def PdWriteX87Mul: SchedWriteRes<[PdLoad, PdFPU1, PdFPFMA]> {
  let Latency = 5;
  let ReleaseAtCycles = [3, 1, 10];
}
def : InstRW<[PdWriteX87Mul], (instrs MUL_FI16m, MUL_FI32m, MUL_F32m, MUL_F64m)>;

defm : PdWriteResXMMPair<WriteFMul64,       [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResXMMPair<WriteFMul64X,      [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResYMMPair<WriteFMul64Y,      [PdFPU1, PdFPFMA],  5, [1, 2]>;
defm : X86WriteResPairUnsupported<WriteFMul64Z>;

defm : PdWriteResXMMPair<WriteFMA,          [PdFPU, PdFPFMA], 5, [1, 3]>;
defm : PdWriteResXMMPair<WriteFMAX,         [PdFPU, PdFPFMA], 5, [1, 3]>;
defm : PdWriteResYMMPair<WriteFMAY,         [PdFPU, PdFPFMA], 5, [1, 3]>;
defm : X86WriteResPairUnsupported<WriteFMAZ>;

defm : PdWriteResXMMPair<WriteDPPD,         [PdFPU1, PdFPFMA], 15, [1, 10], 15, 2>;

defm : PdWriteResXMMPair<WriteDPPS,         [PdFPU1, PdFPFMA], 25, [1, 14],  16, 2>;
defm : PdWriteResYMMPair<WriteDPPSY,        [PdFPU1, PdFPFMA], 27, [2, 25], /*or 29*/ 25, 4>;

def PdWriteVDPPSrri : SchedWriteRes<[PdFPU1, PdFPFMA]> {
  let Latency = 27;
  let ReleaseAtCycles = [1, 14];
  let NumMicroOps = 17;
}
def : InstRW<[PdWriteVDPPSrri], (instrs VDPPSrri)>;

defm : PdWriteResXMMPair<WriteFRcp,         [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResXMMPair<WriteFRcpX,        [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResYMMPair<WriteFRcpY,        [PdFPU1, PdFPFMA],  5, [2, 1]>;
defm : X86WriteResPairUnsupported<WriteFRcpZ>;

defm : PdWriteResXMMPair<WriteFRsqrt,       [PdFPU1, PdFPFMA],  5, [1, 2]>;
defm : PdWriteResXMMPair<WriteFRsqrtX,      [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResYMMPair<WriteFRsqrtY,      [PdFPU1, PdFPFMA],  5, [2, 2]>;
defm : X86WriteResPairUnsupported<WriteFRsqrtZ>;

defm : PdWriteResXMMPair<WriteFDiv,         [PdFPU1, PdFPFMA], 9, [1, 9]>;
defm : PdWriteResXMMPair<WriteFDivX,        [PdFPU1, PdFPFMA], 9, [1, 9]>;
defm : PdWriteResYMMPair<WriteFDivY,        [PdFPU1, PdFPFMA], 9, [2, 18]>;
defm : X86WriteResPairUnsupported<WriteFDivZ>;

def PdWriteX87Div: SchedWriteRes<[PdLoad, PdFPU0, PdFPFMA]> {
  let Latency = 9;
  let ReleaseAtCycles = [3, 1, 18];
}
def : InstRW<[PdWriteX87Div], (instrs DIV_FI16m,  DIV_FI32m,
                                      DIVR_FI16m, DIVR_FI32m,
                                      DIV_F32m,   DIV_F64m,
                                      DIVR_F32m,  DIVR_F64m)>;

defm : PdWriteResXMMPair<WriteFDiv64,       [PdFPU1, PdFPFMA], 9, [1, 9]>;
defm : PdWriteResXMMPair<WriteFDiv64X,      [PdFPU1, PdFPFMA], 9, [1, 9]>;
defm : PdWriteResYMMPair<WriteFDiv64Y,      [PdFPU1, PdFPFMA], 9, [2, 18]>;
defm : X86WriteResPairUnsupported<WriteFDiv64Z>;

defm : PdWriteResXMMPair<WriteFSqrt,        [PdFPU1, PdFPFMA], 9, [1, 9]>;
defm : PdWriteResXMMPair<WriteFSqrtX,       [PdFPU1, PdFPFMA], 9, [1, 9]>;
defm : PdWriteResYMMPair<WriteFSqrtY,       [PdFPU1, PdFPFMA], 9, [2, 18]>;
defm : X86WriteResPairUnsupported<WriteFSqrtZ>;

defm : PdWriteResXMMPair<WriteFSqrt64,      [PdFPU1, PdFPFMA], 9, [1, 9]>;
defm : PdWriteResXMMPair<WriteFSqrt64X,     [PdFPU1, PdFPFMA], 9, [1, 9]>;
defm : PdWriteResYMMPair<WriteFSqrt64Y,     [PdFPU1, PdFPFMA], 9, [2, 18]>;
defm : X86WriteResPairUnsupported<WriteFSqrt64Z>;

defm : PdWriteResXMMPair<WriteFSqrt80,      [PdFPU1, PdFPFMA],  1, [1, 18]>;
defm : PdWriteResXMMPair<WriteFSign,        [PdFPU1, PdFPFMA],  1, [1, 4]>;

defm : PdWriteResXMMPair<WriteFRnd,         [PdFPU1, PdFPSTO],  4, []>;
defm : PdWriteResYMMPair<WriteFRndY,        [PdFPU1, PdFPSTO],  4, [2, 1], 2>;
defm : X86WriteResPairUnsupported<WriteFRndZ>;

def PdWriteVFRCZP : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 10;
  let ReleaseAtCycles = [2, 1];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteVFRCZP], (instrs VFRCZPDrr, VFRCZPSrr)>;

def PdWriteVFRCZS : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 10;
  let ReleaseAtCycles = [10, 1];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteVFRCZS], (instrs VFRCZSDrr, VFRCZSSrr)>;

def PdWriteVFRCZm : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 15;
  let ReleaseAtCycles = [2, 1];
  let NumMicroOps = 3;
}
def : InstRW<[PdWriteVFRCZm], (instrs VFRCZPDrm, VFRCZPSrm,
                                      VFRCZSDrm, VFRCZSSrm)>;

def PdWriteVFRCZY : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 10;
  let ReleaseAtCycles = [3, 1];
  let NumMicroOps = 4;
}
def : InstRW<[PdWriteVFRCZY], (instrs VFRCZPSYrr, VFRCZPDYrr)>;

def PdWriteVFRCZYm : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 15;
  let ReleaseAtCycles = [4, 1];
  let NumMicroOps = 8;
}
def : InstRW<[PdWriteVFRCZYm], (instrs VFRCZPSYrm, VFRCZPDYrm)>;

defm : PdWriteResXMMPair<WriteFLogic,       [PdFPU23, PdFPMAL],  2>;
defm : PdWriteResYMMPair<WriteFLogicY,      [PdFPU23, PdFPMAL],  2, [2, 2]>;
defm : X86WriteResPairUnsupported<WriteFLogicZ>;

defm : PdWriteResXMMPair<WriteFTest,        [PdFPU0, PdFPFMA, PdEX0],  1, [], 2>;
defm : PdWriteResYMMPair<WriteFTestY,       [PdFPU01, PdFPFMA, PdEX0], 1, [4, 4, 1], 4, 2>;
defm : X86WriteResPairUnsupported<WriteFTestZ>;

defm : PdWriteResXMMPair<WriteFShuffle,     [PdFPU01, PdFPFMA],  2, [1, 2]>;
defm : PdWriteResYMMPair<WriteFShuffleY,    [PdFPU01, PdFPFMA],  2, [2, 4], 2>;
defm : X86WriteResPairUnsupported<WriteFShuffleZ>;

def PdWriteVBROADCASTF128 : SchedWriteRes<[PdFPU01, PdFPFMA]> {
  let Latency = 7;
  let ReleaseAtCycles = [1, 3];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteVBROADCASTF128], (instrs VBROADCASTF128rm)>;

defm : PdWriteResXMMPair<WriteFVarShuffle,  [PdFPU1, PdFPXBR],  3>;
defm : PdWriteResYMMPair<WriteFVarShuffleY, [PdFPU1, PdFPXBR],  3, [2, 2], 2>;
defm : X86WriteResPairUnsupported<WriteFVarShuffleZ>;

defm : PdWriteResXMMPair<WriteFBlend,       [PdFPU23, PdFPMAL],  2>;
defm : PdWriteResYMMPair<WriteFBlendY,      [PdFPU23, PdFPMAL],  2, [2, 2], 2>;
defm : X86WriteResPairUnsupported<WriteFBlendZ>;

defm : PdWriteResXMMPair<WriteFVarBlend,    [PdFPU1, PdFPXBR],  2>;
defm : PdWriteResYMMPair<WriteFVarBlendY,   [PdFPU1, PdFPXBR],  2, [2, 2], 2>;
defm : X86WriteResPairUnsupported<WriteFVarBlendZ>;

defm : PdWriteResXMMPair<WriteFShuffle256,  [PdFPU01, PdFPFMA],  2, [1, 3], 2>;
defm : X86WriteResPairUnsupported<WriteFVarShuffle256>;

def PdWriteVEXTRACTF128rr : SchedWriteRes<[PdFPU01, PdFPFMA]> {
  let Latency = 2;
  let ReleaseAtCycles = [1, 2];
}
def : InstRW<[PdWriteVEXTRACTF128rr], (instrs VEXTRACTF128rr)>;

def PdWriteVEXTRACTF128mr : SchedWriteRes<[PdFPU01, PdFPFMA]> {
  let Latency = 7;
  let ReleaseAtCycles = [1, 4];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteVEXTRACTF128mr], (instrs VEXTRACTF128mr)>;

def PdWriteVPERM2F128rr : SchedWriteRes<[PdFPU01, PdFPFMA]> {
  let Latency = 4;
  let ReleaseAtCycles = [1, 6];
  let NumMicroOps = 8;
}
def : InstRW<[PdWriteVPERM2F128rr], (instrs VPERM2F128rr)>;

def PdWriteVPERM2F128rm : SchedWriteRes<[PdFPU01, PdFPFMA]> {
  let Latency = 8; // 4 + 4
  let ReleaseAtCycles = [1, 8];
  let NumMicroOps = 10;
}
def : InstRW<[PdWriteVPERM2F128rm], (instrs VPERM2F128rm)>;

////////////////////////////////////////////////////////////////////////////////
// Conversions.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResXMMPair<WriteCvtSS2I,   [PdFPU0, PdFPCVT, PdFPSTO, PdFPFMA, PdEX0], 13, [], 2>;

defm : PdWriteResXMMPair<WriteCvtPS2I,   [PdFPU0, PdFPCVT, PdFPSTO], 4>;
defm : PdWriteResYMMPair<WriteCvtPS2IY,  [PdFPU0, PdFPCVT, PdFPSTO], 4, [1, 2, 1]>;
defm : X86WriteResPairUnsupported<WriteCvtPS2IZ>;

defm : PdWriteResXMMPair<WriteCvtSD2I,   [PdFPU0, PdFPCVT, PdFPSTO, PdFPFMA, PdEX0], 13, [], 2>;

defm : PdWriteResXMMPair<WriteCvtPD2I,   [PdFPU0, PdFPCVT, PdFPSTO],          8, [],        2>;
defm : PdWriteResYMMPair<WriteCvtPD2IY,  [PdFPU0, PdFPCVT, PdFPSTO, PdFPFMA], 8, [1, 2, 1, 1], 4>;
defm : X86WriteResPairUnsupported<WriteCvtPD2IZ>;

def PdWriteMMX_CVTTPD2PIrr : SchedWriteRes<[PdFPU0, PdFPCVT, PdFPSTO]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteMMX_CVTTPD2PIrr], (instrs MMX_CVTTPD2PIrr)>;

// FIXME: f+3 ST, LD+STC latency
defm : PdWriteResXMMPair<WriteCvtI2SS,   [PdFPU0, PdFPCVT, PdFPSTO], 4, [], 2>;
// FIXME: .Folded version is one NumMicroOp *less*..

defm : PdWriteResXMMPair<WriteCvtI2PS,   [PdFPU0, PdFPCVT, PdFPSTO], 4>;
defm : PdWriteResYMMPair<WriteCvtI2PSY,  [PdFPU0, PdFPCVT, PdFPSTO], 4, [1, 2, 1]>;
defm : X86WriteResPairUnsupported<WriteCvtI2PSZ>;

defm : PdWriteResXMMPair<WriteCvtI2SD,   [PdFPU0, PdFPCVT, PdFPSTO], 4, [], 2>;
// FIXME: .Folded version is one NumMicroOp *less*..

def PdWriteCVTSI642SDrr_CVTSI642SSrr_CVTSI2SDr_CVTSI2SSrr : SchedWriteRes<[PdFPU0, PdFPCVT, PdFPSTO]> {
  let Latency = 13;
  let ReleaseAtCycles = [1, 3, 1];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteCVTSI642SDrr_CVTSI642SSrr_CVTSI2SDr_CVTSI2SSrr], (instrs CVTSI642SDrr, CVTSI642SSrr, CVTSI2SDrr, CVTSI2SSrr)>;

defm : PdWriteResXMMPair<WriteCvtI2PD,   [PdFPU0, PdFPCVT, PdFPSTO], 8, [],     2>;
defm : PdWriteResYMMPair<WriteCvtI2PDY,  [PdFPU0, PdFPCVT, PdFPSTO], 8, [1, 2, 1], 4, 1>;
defm : X86WriteResPairUnsupported<WriteCvtI2PDZ>;

defm : PdWriteResXMMPair<WriteCvtSS2SD,  [PdFPU0, PdFPCVT, PdFPSTO], 4, [1, 2, 1]>;

defm : PdWriteResXMMPair<WriteCvtPS2PD,  [PdFPU0, PdFPCVT, PdFPSTO], 8, [],     2>;
defm : PdWriteResYMMPair<WriteCvtPS2PDY, [PdFPU0, PdFPCVT, PdFPSTO], 8, [1, 2, 1], 4, 1>;
defm : X86WriteResPairUnsupported<WriteCvtPS2PDZ>;

defm : PdWriteResXMMPair<WriteCvtSD2SS,  [PdFPU0, PdFPCVT, PdFPSTO], 4, [1, 2, 1]>;

defm : PdWriteResXMMPair<WriteCvtPD2PS,  [PdFPU0, PdFPCVT, PdFPSTO],          8, [],        2>;
defm : PdWriteResYMMPair<WriteCvtPD2PSY, [PdFPU0, PdFPCVT, PdFPSTO, PdFPFMA], 8, [1, 2, 1, 1], 4>;
defm : X86WriteResPairUnsupported<WriteCvtPD2PSZ>;

def PdWriteMMX_CVTPD2PIrrMMX_CVTPI2PDrr : SchedWriteRes<[PdFPU0, PdFPCVT, PdFPSTO]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteMMX_CVTPD2PIrrMMX_CVTPI2PDrr], (instrs MMX_CVTPD2PIrr,
                                                            MMX_CVTPI2PDrr)>;

def PdWriteMMX_CVTPI2PSrr : SchedWriteRes<[PdFPU0, PdFPCVT, PdFPSTO]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteMMX_CVTPI2PSrr], (instrs MMX_CVTPI2PSrr)>;

defm : PdWriteResXMMPair<WriteCvtPH2PS,  [PdFPU0, PdFPCVT, PdFPSTO], 8, [1, 2, 1], 2, 1>;
defm : PdWriteResYMMPair<WriteCvtPH2PSY, [PdFPU0, PdFPCVT, PdFPSTO], 8, [1, 2, 1], 4, 3>;
defm : X86WriteResPairUnsupported<WriteCvtPH2PSZ>;

defm : PdWriteRes<WriteCvtPS2PH,        [PdFPU0, PdFPCVT, PdFPSTO],          8, [1, 2, 1],    2>;
defm : PdWriteRes<WriteCvtPS2PHY,       [PdFPU0, PdFPCVT, PdFPSTO, PdFPFMA], 8, [1, 2, 1, 1], 4>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZ>;

defm : PdWriteRes<WriteCvtPS2PHSt,      [PdFPU0, PdFPCVT, PdFPSTO, PdStore],          4, [1, 2, 1, 1],    3>;
defm : PdWriteRes<WriteCvtPS2PHYSt,     [PdFPU0, PdFPCVT, PdFPSTO, PdFPFMA, PdStore], 4, [1, 2, 1, 1, 1], 4>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZSt>;

////////////////////////////////////////////////////////////////////////////////
// Vector integer operations.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteRes<WriteVecLoad,             [PdLoad, PdFPU01, PdFPMAL], 5, [3, 1, 3]>;
defm : PdWriteRes<WriteVecLoadX,            [PdLoad, PdFPU01, PdFPMAL], 5, [3, 1, 3]>;
defm : PdWriteRes<WriteVecLoadY,            [PdLoad, PdFPU01, PdFPMAL], 5, [3, 2, 3], 2>;

defm : PdWriteRes<WriteVecLoadNT,           [PdLoad, PdFPU01, PdFPMAL], 5, [3, 1, 4]>;
defm : PdWriteRes<WriteVecLoadNTY,          [PdLoad, PdFPU01, PdFPMAL], 5, [3, 2, 4]>;

defm : PdWriteRes<WriteVecMaskedLoad,       [PdLoad, PdFPU01, PdFPMAL], 6, [3, 1, 2]>;
defm : PdWriteRes<WriteVecMaskedLoadY,      [PdLoad, PdFPU01, PdFPMAL], 6, [3, 2, 4], 2>;

defm : PdWriteRes<WriteVecStore,            [PdStore, PdFPU23, PdFPSTO], 2, [1, 3,  1]>;
defm : PdWriteRes<WriteVecStoreX,           [PdStore, PdFPU23, PdFPSTO], 1, [1, 3,  1]>;
defm : PdWriteRes<WriteVecStoreY,           [PdStore, PdFPU23, PdFPSTO], 1, [2, 36, 2], 4>;

def PdWriteVMOVDQUYmr : SchedWriteRes<[PdStore, PdFPU1,   PdFPSTO]> {
  let NumMicroOps = 8;
}
def : InstRW<[PdWriteVMOVDQUYmr], (instrs VMOVDQUYmr)>;

defm : PdWriteRes<WriteVecStoreNT,          [PdStore, PdFPU1,   PdFPSTO], 2>;
defm : PdWriteRes<WriteVecStoreNTY,         [PdStore, PdFPU1,   PdFPSTO], 2, [2, 2, 2], 4>;

defm : X86WriteResUnsupported<WriteVecMaskedStore32>;
defm : X86WriteResUnsupported<WriteVecMaskedStore32Y>;
defm : X86WriteResUnsupported<WriteVecMaskedStore64>;
defm : X86WriteResUnsupported<WriteVecMaskedStore64Y>;

defm : PdWriteRes<WriteVecMove,             [PdFPU01, PdFPMAL], 2>;
defm : PdWriteRes<WriteVecMoveX,            [PdFPU01, PdFPMAL], 1, [1, 2]>;
defm : PdWriteRes<WriteVecMoveY,            [PdFPU01, PdFPMAL], 2, [2, 2], 2>;
defm : X86WriteResUnsupported<WriteVecMoveZ>;

def PdWriteMOVDQArr : SchedWriteRes<[PdFPU01, PdFPMAL]> {
}
def : InstRW<[PdWriteMOVDQArr], (instrs MOVDQArr)>;

def PdWriteMOVQ2DQrr : SchedWriteRes<[PdFPU01, PdFPMAL]> {
  let Latency = 4;
}
def : InstRW<[PdWriteMOVQ2DQrr], (instrs MMX_MOVQ2DQrr)>;

defm : PdWriteRes<WriteVecMoveToGpr,        [PdFPU0, PdFPFMA, PdEX0], 11>;
defm : PdWriteRes<WriteVecMoveFromGpr,      [PdFPU01, PdFPFMA], 11, [1, 2], 2>;

defm : PdWriteResXMMPair<WriteVecALU,        [PdFPU23, PdFPMAL], 2>;
defm : PdWriteResXMMPair<WriteVecALUX,       [PdFPU23, PdFPMAL], 2>;
defm : X86WriteResPairUnsupported<WriteVecALUY>;
defm : X86WriteResPairUnsupported<WriteVecALUZ>;

defm : PdWriteResXMMPair<WriteVecShift,      [PdFPU1, PdFPXBR], 3>;
defm : PdWriteResXMMPair<WriteVecShiftX,     [PdFPU1, PdFPXBR], 3>;
defm : X86WriteResPairUnsupported<WriteVecShiftY>;
defm : X86WriteResPairUnsupported<WriteVecShiftZ>;

defm : PdWriteResXMMPair<WriteVecShiftImm,   [PdFPU1, PdFPXBR], 2>;
defm : PdWriteResXMMPair<WriteVecShiftImmX,  [PdFPU1, PdFPXBR], 2>;
defm : X86WriteResPairUnsupported<WriteVecShiftImmY>;
defm : X86WriteResPairUnsupported<WriteVecShiftImmZ>;

defm : PdWriteResXMMPair<WriteVecIMul,       [PdFPU0, PdFPMMA], 4>;
defm : PdWriteResXMMPair<WriteVecIMulX,      [PdFPU0, PdFPMMA], 4>;
defm : X86WriteResPairUnsupported<WriteVecIMulY>;
defm : X86WriteResPairUnsupported<WriteVecIMulZ>;

defm : PdWriteResXMMPair<WritePMULLD,        [PdFPU0, PdFPU01, PdFPMMA, PdFPMAL], 5, [2, 1, 2, 1]>;
defm : X86WriteResPairUnsupported<WritePMULLDY>;
defm : X86WriteResPairUnsupported<WritePMULLDZ>;

def PdWriteVPMACS : SchedWriteRes<[PdFPU0, PdFPMMA, PdFPMAL]> {
  let Latency = 4;
}
def : InstRW<[PdWriteVPMACS], (instrs VPMACSDQHrr, VPMACSDQLrr, VPMACSSDQHrr,
                                      VPMACSSDQLrr)>;

// FIXME: Investigate RR vs RM differences.
defm : PdWriteRes<WriteMPSAD,   [PdFPU0, PdFPMMA], 8, [1, 4], 8>;
defm : PdWriteRes<WriteMPSADLd, [PdFPU0, PdFPMMA, PdLoad], 14, [1, 4, 3],  8>;
defm : X86WriteResPairUnsupported<WriteMPSADY>;
defm : X86WriteResPairUnsupported<WriteMPSADZ>;

defm : PdWriteResXMMPair<WritePSADBW,        [PdFPU01, PdFPMAL], 4, [1, 2], 2>;
defm : PdWriteResXMMPair<WritePSADBWX,       [PdFPU01, PdFPMAL], 4, [1, 2], 2>;
defm : X86WriteResPairUnsupported<WritePSADBWY>;
defm : X86WriteResPairUnsupported<WritePSADBWZ>;

defm : PdWriteResXMMPair<WritePHMINPOS,      [PdFPU0,  PdFPMAL], 4, [], 2>;

defm : PdWriteResXMMPair<WriteShuffle,       [PdFPU1, PdFPXBR], 2>;
defm : PdWriteResXMMPair<WriteShuffleX,      [PdFPU1, PdFPXBR], 2>;
defm : PdWriteResYMMPair<WriteShuffleY,      [PdFPU1, PdFPXBR], 2, [2, 2]>;
defm : X86WriteResPairUnsupported<WriteShuffleZ>;

defm : PdWriteResXMMPair<WriteVarShuffle,    [PdFPU1, PdFPXBR], 3>;
defm : PdWriteResXMMPair<WriteVarShuffleX,   [PdFPU1, PdFPXBR], 3>;
defm : X86WriteResPairUnsupported<WriteVarShuffleY>;
defm : X86WriteResPairUnsupported<WriteVarShuffleZ>;

def PdWriteVPPERM : SchedWriteRes<[PdFPU1, PdFPXBR]> {
  let Latency = 2;
  let ReleaseAtCycles = [1, 1];
}
def : InstRW<[PdWriteVPPERM], (instrs VPPERMrrr, VPPERMrrr_REV)>;

def PdWriteVPPERMLd : SchedWriteRes<[PdFPU1, PdFPXBR, PdLoad]> {
  let Latency = 7;
  let ReleaseAtCycles = [1, 1, 3];
}
def : InstRW<[PdWriteVPPERMLd], (instrs VPPERMrrm, VPPERMrmr)>;

defm : PdWriteResXMMPair<WriteBlend,         [PdFPU23, PdFPMAL], 2>;
defm : X86WriteResPairUnsupported<WriteBlendY>;
defm : X86WriteResPairUnsupported<WriteBlendZ>;

defm : PdWriteResXMMPair<WriteVarBlend,      [PdFPU1, PdFPXBR], 2>;
defm : X86WriteResPairUnsupported<WriteVarBlendY>;
defm : X86WriteResPairUnsupported<WriteVarBlendZ>;

defm : PdWriteResXMMPair<WriteVecLogic,      [PdFPU23, PdFPMAL], 2>;
defm : PdWriteResXMMPair<WriteVecLogicX,     [PdFPU23, PdFPMAL], 2>;
defm : X86WriteResPairUnsupported<WriteVecLogicY>;
defm : X86WriteResPairUnsupported<WriteVecLogicZ>;

defm : PdWriteResXMMPair<WriteVecTest,       [PdFPU0, PdFPFMA, PdEX0],  1, [], 2>;
defm : PdWriteResYMMPair<WriteVecTestY,      [PdFPU01, PdFPFMA, PdEX0], 1, [2, 4, 1], 4, 2>;
defm : X86WriteResPairUnsupported<WriteVecTestZ>;

defm : PdWriteResXMMPair<WriteShuffle256,    [PdFPU01, PdFPMAL]>;
defm : PdWriteResXMMPair<WriteVPMOV256,      [PdFPU01, PdFPMAL]>;
defm : PdWriteResXMMPair<WriteVarShuffle256, [PdFPU01, PdFPMAL]>;

defm : PdWriteResXMMPair<WriteVarVecShift,   [PdFPU1, PdFPXBR], 3>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftY>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftZ>;

////////////////////////////////////////////////////////////////////////////////
// Vector insert/extract operations.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteRes<WriteVecInsert,    [PdFPU01, PdFPMAL], 2, [1, 3], 2>;
defm : PdWriteRes<WriteVecInsertLd,  [PdFPU01, PdFPMAL, PdLoad], 6, [1, 4, 3], 2>;

defm : PdWriteRes<WriteVecExtract,   [PdFPU0, PdFPFMA, PdEX0], 12, [1, 3, 1], 2>;
defm : PdWriteRes<WriteVecExtractSt, [PdFPU1, PdFPSTO, PdStore], 13, [2, 1, 1], 2>;

def PdWriteEXTRQ : SchedWriteRes<[PdFPU01, PdFPMAL]> {
  let Latency = 3;
  let ReleaseAtCycles = [1, 3];
}
def : InstRW<[PdWriteEXTRQ], (instrs EXTRQ, EXTRQI)>;

////////////////////////////////////////////////////////////////////////////////
// SSE42 String instructions.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResXMMPair<WritePCmpIStrI, [PdFPU1, PdFPFMA, PdEX0], 11, [1, 6, 1], 7, 1>;
defm : PdWriteResXMMPair<WritePCmpIStrM, [PdFPU1, PdFPFMA, PdEX0],  7, [1, 8, 1], 7, 2>;

defm : PdWriteResXMMPair<WritePCmpEStrI, [PdFPU1, PdStore, PdLoad, PdFPMAL, PdFPFMA, PdEX0], 14, [1, 10, 10, 10, 1, 1], 27, 1>;
defm : PdWriteResXMMPair<WritePCmpEStrM, [PdFPU1, PdStore, PdLoad, PdFPMAL, PdFPFMA, PdEX0], 10, [1, 10, 10, 10, 1, 1], 27, 1>;

////////////////////////////////////////////////////////////////////////////////
// MOVMSK Instructions.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteRes<WriteFMOVMSK, [PdFPU0, PdFPFMA, PdEX0],   12, [], 2>;

defm : PdWriteRes<WriteVecMOVMSK, [PdFPU0, PdFPFMA, PdEX0], 12, [], 2>;
defm : X86WriteResUnsupported<WriteVecMOVMSKY>;
// defm : X86WriteResUnsupported<WriteVecMOVMSKZ>;

defm : PdWriteRes<WriteMMXMOVMSK, [PdFPU0, PdFPFMA, PdEX0], 10, [], 2>;

////////////////////////////////////////////////////////////////////////////////
// AES Instructions.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResXMMPair<WriteAESIMC,    [PdFPU0, PdFPMMA], 5>;
defm : PdWriteResXMMPair<WriteAESKeyGen, [PdFPU0, PdFPMMA], 5>;
defm : PdWriteResXMMPair<WriteAESDecEnc, [PdFPU0, PdFPMMA], 9, [], 2>;

////////////////////////////////////////////////////////////////////////////////
// Horizontal add/sub  instructions.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResXMMPair<WriteFHAdd,  [PdFPU0, PdFPFMA], 11, [1, 5],     3, 1>;
defm : PdWriteResYMMPair<WriteFHAddY, [PdFPU0, PdFPFMA], 11, [1, 8], 8, 2>;
defm : X86WriteResPairUnsupported<WriteFHAddZ>;

defm : PdWriteResXMMPair<WritePHAdd,  [PdFPU01, PdFPMAL], 5, [1, 4], 3, 1>;
defm : PdWriteResXMMPair<WritePHAddX, [PdFPU01, PdFPMAL], 2, [1, 2]>;
defm : X86WriteResPairUnsupported<WritePHAddY>;
defm : X86WriteResPairUnsupported<WritePHAddZ>;

def : InstRW<[WritePHAdd], (instrs PHADDDrr, PHSUBDrr,
                                   PHADDWrr, PHSUBWrr,
                                   PHADDSWrr, PHSUBSWrr,
                                   VPHADDDrr, VPHSUBDrr,
                                   VPHADDWrr, VPHSUBWrr,
                                   VPHADDSWrr, VPHSUBSWrr)>;

def : InstRW<[WritePHAdd.Folded], (instrs PHADDDrm, PHSUBDrm,
                                          PHADDWrm, PHSUBWrm,
                                          PHADDSWrm, PHSUBSWrm,
                                          VPHADDDrm, VPHSUBDrm,
                                          VPHADDWrm, VPHSUBWrm,
                                          VPHADDSWrm, VPHSUBSWrm)>;

////////////////////////////////////////////////////////////////////////////////
// Carry-less multiplication instructions.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResXMMPair<WriteCLMul, [PdFPU0, PdFPMMA], 12, [1, 7], 5, 1>;

def PdWriteVPCLMULQDQrri : SchedWriteRes<[PdFPU0, PdFPMMA]> {
  let Latency = 12;
  let ReleaseAtCycles = [1, 7];
  let NumMicroOps = 6;
}
def : InstRW<[PdWriteVPCLMULQDQrri], (instrs VPCLMULQDQrri)>;

////////////////////////////////////////////////////////////////////////////////
// SSE4A instructions.
////////////////////////////////////////////////////////////////////////////////

def PdWriteINSERTQ : SchedWriteRes<[PdFPU01, PdFPMAL]> {
  let Latency = 3;
  let ReleaseAtCycles = [1, 2];
}
def : InstRW<[PdWriteINSERTQ], (instrs INSERTQ)>;

def PdWriteINSERTQI : SchedWriteRes<[PdFPU01, PdFPMAL]> {
  let Latency = 3;
  let ReleaseAtCycles = [1, 3];
}
def : InstRW<[PdWriteINSERTQI], (instrs INSERTQI)>;

////////////////////////////////////////////////////////////////////////////////
// AVX instructions.
////////////////////////////////////////////////////////////////////////////////

def PdWriteVBROADCASTYLd : SchedWriteRes<[PdLoad, PdFPU01, PdFPFMA]> {
  let Latency = 6;
  let ReleaseAtCycles = [1, 2, 4];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteVBROADCASTYLd, ReadAfterLd], (instrs VBROADCASTSDYrm,
                                                          VBROADCASTSSYrm)>;

def PdWriteVZEROALL : SchedWriteRes<[]> {
  let Latency = 90;
  let NumMicroOps = 32;
}
def : InstRW<[PdWriteVZEROALL], (instrs VZEROALL)>;

def PdWriteVZEROUPPER : SchedWriteRes<[]> {
  let Latency = 46;
  let NumMicroOps = 16;
}
def : InstRW<[PdWriteVZEROUPPER], (instrs VZEROUPPER)>;

///////////////////////////////////////////////////////////////////////////////
//  SchedWriteVariant definitions.
///////////////////////////////////////////////////////////////////////////////

def PdWriteZeroLatency : SchedWriteRes<[]> {
  let Latency = 0;
}

def PdWriteZeroIdiom : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [PdWriteZeroLatency]>,
  SchedVar<MCSchedPredicate<TruePred>,           [WriteALU]>
]>;
def : InstRW<[PdWriteZeroIdiom], (instrs SUB32rr, SUB64rr,
                                         XOR32rr, XOR64rr)>;

def PdWriteFZeroIdiom : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [PdWriteZeroLatency]>,
  SchedVar<MCSchedPredicate<TruePred>,           [WriteFLogic]>
]>;
def : InstRW<[PdWriteFZeroIdiom], (instrs XORPSrr,  VXORPSrr,
                                          XORPDrr,  VXORPDrr,
                                          ANDNPSrr, VANDNPSrr,
                                          ANDNPDrr, VANDNPDrr)>;

// VXORPSYrr, VXORPDYrr, VANDNPSYrr, VANDNPDYrr "zero-idioms" have latency of 1.

def PdWriteVZeroIdiomLogic : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [PdWriteZeroLatency]>,
  SchedVar<MCSchedPredicate<TruePred>,           [WriteVecLogic]>
]>;
def : InstRW<[PdWriteVZeroIdiomLogic], (instrs MMX_PXORrr, MMX_PANDNrr)>;

def PdWriteVZeroIdiomLogicX : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [PdWriteZeroLatency]>,
  SchedVar<MCSchedPredicate<TruePred>,           [WriteVecLogicX]>
]>;
def : InstRW<[PdWriteVZeroIdiomLogicX], (instrs PXORrr,  VPXORrr,
                                                PANDNrr, VPANDNrr)>;

def PdWriteVZeroIdiomALU : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [PdWriteZeroLatency]>,
  SchedVar<MCSchedPredicate<TruePred>,           [WriteVecALU]>
]>;
def : InstRW<[PdWriteVZeroIdiomALU], (instrs MMX_PSUBBrr,   MMX_PSUBDrr,
                                             MMX_PSUBQrr,   MMX_PSUBWrr,
                                             MMX_PCMPGTBrr,
                                             MMX_PCMPGTDrr,
                                             MMX_PCMPGTWrr)>;

def PdWriteVZeroIdiomALUX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [PdWriteZeroLatency]>,
    SchedVar<MCSchedPredicate<TruePred>,           [WriteVecALUX]>
]>;
def : InstRW<[PdWriteVZeroIdiomALUX], (instrs PSUBBrr, VPSUBBrr,
                                              PSUBDrr, VPSUBDrr,
                                              PSUBQrr, VPSUBQrr,
                                              PSUBWrr, VPSUBWrr,
                                              PCMPGTBrr, VPCMPGTBrr,
                                              PCMPGTDrr, VPCMPGTDrr,
                                              PCMPGTWrr, VPCMPGTWrr)>;

///////////////////////////////////////////////////////////////////////////////
// Dependency breaking instructions.
///////////////////////////////////////////////////////////////////////////////

// VPCMPGTQ, but not PCMPGTQ!

def : IsZeroIdiomFunction<[
  // GPR Zero-idioms.
  DepBreakingClass<[ SUB32rr, SUB64rr, XOR32rr, XOR64rr ], ZeroIdiomPredicate>,

  // MMX Zero-idioms.
  DepBreakingClass<[
    MMX_PXORrr, MMX_PANDNrr, MMX_PSUBBrr,
    MMX_PSUBDrr, MMX_PSUBQrr, MMX_PSUBWrr,
    MMX_PSUBSBrr, MMX_PSUBSWrr, MMX_PSUBUSBrr, MMX_PSUBUSWrr,
    MMX_PCMPGTBrr, MMX_PCMPGTDrr, MMX_PCMPGTWrr
  ], ZeroIdiomPredicate>,

  // SSE Zero-idioms.
  DepBreakingClass<[
    // fp variants.
    XORPSrr, XORPDrr, ANDNPSrr, ANDNPDrr,

    // int variants.
    PXORrr, PANDNrr,
    PSUBBrr, PSUBWrr, PSUBDrr, PSUBQrr,
    PSUBSBrr, PSUBSWrr, PSUBUSBrr, PSUBUSWrr,
    PCMPGTBrr, PCMPGTDrr, PCMPGTWrr
  ], ZeroIdiomPredicate>,

  // AVX Zero-idioms.
  DepBreakingClass<[
    // xmm fp variants.
    VXORPSrr, VXORPDrr, VANDNPSrr, VANDNPDrr,

    // xmm int variants.
    VPXORrr, VPANDNrr,
    VPSUBBrr, VPSUBWrr, VPSUBDrr, VPSUBQrr,
    VPSUBSBrr, VPSUBSWrr, VPSUBUSBrr, VPSUBUSWrr,
    VPCMPGTBrr, VPCMPGTWrr, VPCMPGTDrr, VPCMPGTQrr,

    // ymm variants.
    VXORPSYrr, VXORPDYrr, VANDNPSYrr, VANDNPDYrr
  ], ZeroIdiomPredicate>
]>;

def : IsDepBreakingFunction<[
  // GPR
  DepBreakingClass<[ SBB32rr, SBB64rr ], ZeroIdiomPredicate>,
  DepBreakingClass<[ CMP32rr, CMP64rr ], CheckSameRegOperand<0, 1> >,

  // MMX
  DepBreakingClass<[
    MMX_PCMPEQBrr, MMX_PCMPEQDrr, MMX_PCMPEQWrr
  ], ZeroIdiomPredicate>,

  // SSE
  DepBreakingClass<[
    PCMPEQBrr, PCMPEQWrr, PCMPEQDrr
    // But not PCMPEQQrr.
  ], ZeroIdiomPredicate>,

  // AVX
  DepBreakingClass<[
    VPCMPEQBrr, VPCMPEQWrr, VPCMPEQDrr
    // But not VPCMPEQQrr.
  ], ZeroIdiomPredicate>
]>;


} // SchedModel

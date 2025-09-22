//==- RISCVSchedSiFive7.td - SiFive7 Scheduling Definitions --*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//

/// c is true if mx has the worst case behavior compared to LMULs in MxList.
/// On the SiFive7, the worst case LMUL is the Largest LMUL
/// and the worst case sew is the smallest SEW for that LMUL.
class SiFive7IsWorstCaseMX<string mx, list<string> MxList> {
  defvar LLMUL = LargestLMUL<MxList>.r;
  bit c = !eq(mx, LLMUL);
}

/// c is true if mx and sew have the worst case behavior compared to LMULs in
/// MxList. On the SiFive7, the worst case LMUL is the Largest LMUL
/// and the worst case sew is the smallest SEW for that LMUL.
class SiFive7IsWorstCaseMXSEW<string mx, int sew, list<string> MxList,
                               bit isF = 0> {
  defvar LLMUL = LargestLMUL<MxList>.r;
  defvar SSEW = SmallestSEW<mx, isF>.r;
  bit c = !and(!eq(mx, LLMUL), !eq(sew, SSEW));
}

/// Number of DLEN parts = (LMUL * VLEN) / DLEN.
/// Since DLEN = VLEN / 2, Num DLEN parts = 2 * LMUL.
class SiFive7GetCyclesDefault<string mx> {
  int c = !cond(
    !eq(mx, "M1") : 2,
    !eq(mx, "M2") : 4,
    !eq(mx, "M4") : 8,
    !eq(mx, "M8") : 16,
    !eq(mx, "MF2") : 1,
    !eq(mx, "MF4") : 1,
    !eq(mx, "MF8") : 1
  );
}

class SiFive7GetCyclesNarrowing<string mx> {
  int c = !cond(
    !eq(mx, "M1") : 4,
    !eq(mx, "M2") : 8,
    !eq(mx, "M4") : 16,
    !eq(mx, "MF2") : 2,
    !eq(mx, "MF4") : 1,
    !eq(mx, "MF8") : 1
  );
}

class SiFive7GetCyclesVMask<string mx> {
  int c = !cond(
    !eq(mx, "M1") : 1,
    !eq(mx, "M2") : 1,
    !eq(mx, "M4") : 1,
    !eq(mx, "M8") : 2,
    !eq(mx, "MF2") : 1,
    !eq(mx, "MF4") : 1,
    !eq(mx, "MF8") : 1
  );
}

/// VLDM and VSTM can't read/write more than 2 DLENs of data.
/// 2 DLENs when LMUL=8. 1 DLEN for all other DLENs
class SiFive7GetMaskLoadStoreCycles<string mx> {
  int c = !cond(
    !eq(mx, "M8")  : 2,
    true : 1
  );
}

// Cycles for nf=2 segmented loads and stores are calculated using the
// formula (2 * VLEN * LMUL) / DLEN = 4 * LMUL
class SiFive7GetCyclesSegmentedSeg2<string mx> {
  int c = !cond(
    !eq(mx, "M1") :  4,
    !eq(mx, "M2") :  8,
    !eq(mx, "M4") :  16,
    !eq(mx, "M8") :  32,
    !eq(mx, "MF2") : 2,
    !eq(mx, "MF4") : 1,
    !eq(mx, "MF8") : 1
  );
}

// Cycles for segmented loads and stores are calculated using the
// formula vl * ceil((SEW * nf) / DLEN), where SEW * nf is the segment size.
class SiFive7GetCyclesSegmented<string mx, int sew, int nf> {
  defvar VLEN = 512;
  defvar DLEN = 256;
  // (VLEN * LMUL) / SEW
  defvar VLUpperBound  = !cond(
    !eq(mx, "M1") : !div(VLEN, sew),
    !eq(mx, "M2") : !div(!mul(VLEN, 2), sew),
    !eq(mx, "M4") : !div(!mul(VLEN, 4), sew),
    !eq(mx, "M8") : !div(!mul(VLEN, 8), sew),
    !eq(mx, "MF2") : !div(!div(VLEN, 2), sew),
    !eq(mx, "MF4") : !div(!div(VLEN, 4), sew),
    !eq(mx, "MF8") : !div(!div(VLEN, 8), sew),
  );
  // We can calculate ceil(a/b) using (a + b - 1) / b.
  defvar a = !mul(sew, nf);
  defvar b = DLEN;
  int c = !mul(VLUpperBound, !div(!sub(!add(a, b), 1), b));
}

class SiFive7GetCyclesOnePerElement<string mx, int sew> {
  // FIXME: On SiFive7, VLEN is 512. Although a user can request the compiler
  // to use a different VLEN, this model will not make scheduling decisions
  // based on the user specified VLEN.
  // c = ceil(VLEN / SEW) * LMUL
  // Note: c >= 1 since the smallest VLEN is 512 / 8 = 8, and the
  // largest division performed on VLEN is in MF8 case with division
  // by 8. Therefore, there is no need to ceil the result.
  int VLEN = !div(512, sew);
  int c = !cond(
    !eq(mx, "M1")  : VLEN,
    !eq(mx, "M2")  : !mul(VLEN, 2),
    !eq(mx, "M4")  : !mul(VLEN, 4),
    !eq(mx, "M8")  : !mul(VLEN, 8),
    !eq(mx, "MF2") : !div(VLEN, 2),
    !eq(mx, "MF4") : !div(VLEN, 4),
    !eq(mx, "MF8") : !div(VLEN, 8)
  );
}

class SiFive7GetDivOrSqrtFactor<int sew> {
  int c = !cond(
    // TODO: Add SchedSEWSetFP upstream and remove the SEW=8 case.
    !eq(sew, 8) : 15,
    !eq(sew, 16) : 15,
    !eq(sew, 32) : 28,
    !eq(sew, 64) : 57
  );
}

/// Cycles for reductions take approximately VL*SEW/DLEN + 5(4 + log(DLEN/SEW))
/// cycles.
class SiFive7GetReductionCycles<string mx, int sew> {
  // VLUpperBound*SEW/DLEN is equivalent to 2*LMUL since
  // VLUpperBound=(VLEN*LMUL)/SEW.
  defvar VLEN = 512;
  defvar DLEN = !div(VLEN, 2);
  defvar TwoTimesLMUL = !cond(
    !eq(mx, "M1") : 2,
    !eq(mx, "M2") : 4,
    !eq(mx, "M4") : 8,
    !eq(mx, "M8") : 16,
    !eq(mx, "MF2") : 1,
    !eq(mx, "MF4") : 1,
    !eq(mx, "MF8") : 1
  );
  int c = !add(
    TwoTimesLMUL,
    !mul(5, !add(4, !logtwo(!div(DLEN, sew))))
  );
}

/// Cycles for ordered reductions take approximatley 6*VL cycles
class SiFive7GetOrderedReductionCycles<string mx, int sew> {
  defvar VLEN = 512;
  // (VLEN * LMUL) / SEW
  defvar VLUpperBound  = !cond(
    !eq(mx, "M1") : !div(VLEN, sew),
    !eq(mx, "M2") : !div(!mul(VLEN, 2), sew),
    !eq(mx, "M4") : !div(!mul(VLEN, 4), sew),
    !eq(mx, "M8") : !div(!mul(VLEN, 8), sew),
    !eq(mx, "MF2") : !div(!div(VLEN, 2), sew),
    !eq(mx, "MF4") : !div(!div(VLEN, 4), sew),
    !eq(mx, "MF8") : !div(!div(VLEN, 8), sew),
  );
  int c = !mul(6, VLUpperBound);
}

class SiFive7AnyToGPRBypass<SchedRead read, int cycles = 2>
    : ReadAdvance<read, cycles, [WriteIALU, WriteIALU32,
                                 WriteShiftImm, WriteShiftImm32,
                                 WriteShiftReg, WriteShiftReg32,
                                 WriteSHXADD, WriteSHXADD32,
                                 WriteRotateImm, WriteRotateImm32,
                                 WriteRotateReg, WriteRotateReg32,
                                 WriteSingleBit, WriteSingleBitImm,
                                 WriteBEXT, WriteBEXTI,
                                 WriteCLZ, WriteCLZ32, WriteCTZ, WriteCTZ32,
                                 WriteCPOP, WriteCPOP32,
                                 WriteREV8, WriteORCB, WriteIMinMax, WriteSFB,
                                 WriteIMul, WriteIMul32,
                                 WriteIDiv, WriteIDiv32,
                                 WriteIRem, WriteIRem32,
                                 WriteLDB, WriteLDH, WriteLDW, WriteLDD]>;

// SiFive7 machine model for scheduling and other instruction cost heuristics.
def SiFive7Model : SchedMachineModel {
  let MicroOpBufferSize = 0; // Explicitly set to zero since SiFive7 is in-order.
  let IssueWidth = 2;        // 2 micro-ops are dispatched per cycle.
  let LoadLatency = 3;
  let MispredictPenalty = 3;
  let CompleteModel = 0;
  let EnableIntervals = true;
  let UnsupportedFeatures = [HasStdExtZbkb, HasStdExtZbkc, HasStdExtZbkx,
                             HasStdExtZcmt, HasStdExtZknd, HasStdExtZkne,
                             HasStdExtZknh, HasStdExtZksed, HasStdExtZksh,
                             HasStdExtZkr];
}

// The SiFive7 microarchitecture has three pipelines: A, B, V.
// Pipe A can handle memory, integer alu and vector operations.
// Pipe B can handle integer alu, control flow, integer multiply and divide,
// and floating point computation.
// The V pipeline is modeled by the VCQ, VA, VL, and VS resources.
let SchedModel = SiFive7Model in {
let BufferSize = 0 in {
def SiFive7PipeA       : ProcResource<1>;
def SiFive7PipeB       : ProcResource<1>;
def SiFive7IDiv        : ProcResource<1>; // Int Division
def SiFive7FDiv        : ProcResource<1>; // FP Division/Sqrt
def SiFive7VA          : ProcResource<1>; // Arithmetic sequencer
def SiFive7VL          : ProcResource<1>; // Load sequencer
def SiFive7VS          : ProcResource<1>; // Store sequencer
// The VCQ accepts instructions from the the A Pipe and holds them until the
// vector unit is ready to dequeue them. The unit dequeues up to one instruction
// per cycle, in order, as soon as the sequencer for that type of instruction is
// avaliable. This resource is meant to be used for 1 cycle by all vector
// instructions, to model that only one vector instruction may be dequed at a
// time. The actual dequeueing into the sequencer is modeled by the VA, VL, and
// VS sequencer resources below. Each of them will only accept a single
// instruction at a time and remain busy for the number of cycles associated
// with that instruction.
def SiFive7VCQ         : ProcResource<1>; // Vector Command Queue
}

def SiFive7PipeAB : ProcResGroup<[SiFive7PipeA, SiFive7PipeB]>;

// Branching
let Latency = 3 in {
def : WriteRes<WriteJmp, [SiFive7PipeB]>;
def : WriteRes<WriteJal, [SiFive7PipeB]>;
def : WriteRes<WriteJalr, [SiFive7PipeB]>;
}

//Short forward branch
def : WriteRes<WriteSFB, [SiFive7PipeA, SiFive7PipeB]> {
  let Latency = 3;
  let NumMicroOps = 2;
}

// Integer arithmetic and logic
let Latency = 3 in {
def : WriteRes<WriteIALU, [SiFive7PipeAB]>;
def : WriteRes<WriteIALU32, [SiFive7PipeAB]>;
def : WriteRes<WriteShiftImm, [SiFive7PipeAB]>;
def : WriteRes<WriteShiftImm32, [SiFive7PipeAB]>;
def : WriteRes<WriteShiftReg, [SiFive7PipeAB]>;
def : WriteRes<WriteShiftReg32, [SiFive7PipeAB]>;
}

// Integer multiplication
let Latency = 3 in {
def : WriteRes<WriteIMul, [SiFive7PipeB]>;
def : WriteRes<WriteIMul32, [SiFive7PipeB]>;
}

// Integer division
def : WriteRes<WriteIDiv, [SiFive7PipeB, SiFive7IDiv]> {
  let Latency = 66;
  let ReleaseAtCycles = [1, 65];
}
def : WriteRes<WriteIDiv32,  [SiFive7PipeB, SiFive7IDiv]> {
  let Latency = 34;
  let ReleaseAtCycles = [1, 33];
}

// Integer remainder
def : WriteRes<WriteIRem, [SiFive7PipeB, SiFive7IDiv]> {
  let Latency = 66;
  let ReleaseAtCycles = [1, 65];
}
def : WriteRes<WriteIRem32,  [SiFive7PipeB, SiFive7IDiv]> {
  let Latency = 34;
  let ReleaseAtCycles = [1, 33];
}

// Bitmanip
let Latency = 3 in {
// Rotates are in the late-B ALU.
def : WriteRes<WriteRotateImm, [SiFive7PipeB]>;
def : WriteRes<WriteRotateImm32, [SiFive7PipeB]>;
def : WriteRes<WriteRotateReg, [SiFive7PipeB]>;
def : WriteRes<WriteRotateReg32, [SiFive7PipeB]>;

// clz[w]/ctz[w] are in the late-B ALU.
def : WriteRes<WriteCLZ, [SiFive7PipeB]>;
def : WriteRes<WriteCLZ32, [SiFive7PipeB]>;
def : WriteRes<WriteCTZ, [SiFive7PipeB]>;
def : WriteRes<WriteCTZ32, [SiFive7PipeB]>;

// cpop[w] look exactly like multiply.
def : WriteRes<WriteCPOP, [SiFive7PipeB]>;
def : WriteRes<WriteCPOP32, [SiFive7PipeB]>;

// orc.b is in the late-B ALU.
def : WriteRes<WriteORCB, [SiFive7PipeB]>;

// min/max are in the late-B ALU
def : WriteRes<WriteIMinMax, [SiFive7PipeB]>;

// rev8 is in the late-A and late-B ALUs.
def : WriteRes<WriteREV8, [SiFive7PipeAB]>;

// shNadd[.uw] is on the early-B and late-B ALUs.
def : WriteRes<WriteSHXADD, [SiFive7PipeB]>;
def : WriteRes<WriteSHXADD32, [SiFive7PipeB]>;
}

// Single-bit instructions
// BEXT[I] instruction is available on all ALUs and the other instructions
// are only available on the SiFive7B pipe.
let Latency = 3 in {
def : WriteRes<WriteSingleBit, [SiFive7PipeB]>;
def : WriteRes<WriteSingleBitImm, [SiFive7PipeB]>;
def : WriteRes<WriteBEXT, [SiFive7PipeAB]>;
def : WriteRes<WriteBEXTI, [SiFive7PipeAB]>;
}

// Memory
def : WriteRes<WriteSTB, [SiFive7PipeA]>;
def : WriteRes<WriteSTH, [SiFive7PipeA]>;
def : WriteRes<WriteSTW, [SiFive7PipeA]>;
def : WriteRes<WriteSTD, [SiFive7PipeA]>;
def : WriteRes<WriteFST16, [SiFive7PipeA]>;
def : WriteRes<WriteFST32, [SiFive7PipeA]>;
def : WriteRes<WriteFST64, [SiFive7PipeA]>;

let Latency = 3 in {
def : WriteRes<WriteLDB, [SiFive7PipeA]>;
def : WriteRes<WriteLDH, [SiFive7PipeA]>;
def : WriteRes<WriteLDW, [SiFive7PipeA]>;
def : WriteRes<WriteLDD, [SiFive7PipeA]>;
}

let Latency = 2 in {
def : WriteRes<WriteFLD16, [SiFive7PipeA]>;
def : WriteRes<WriteFLD32, [SiFive7PipeA]>;
def : WriteRes<WriteFLD64, [SiFive7PipeA]>;
}

// Atomic memory
def : WriteRes<WriteAtomicSTW, [SiFive7PipeA]>;
def : WriteRes<WriteAtomicSTD, [SiFive7PipeA]>;

let Latency = 3 in {
def : WriteRes<WriteAtomicW, [SiFive7PipeA]>;
def : WriteRes<WriteAtomicD, [SiFive7PipeA]>;
def : WriteRes<WriteAtomicLDW, [SiFive7PipeA]>;
def : WriteRes<WriteAtomicLDD, [SiFive7PipeA]>;
}

// Half precision.
let Latency = 5 in {
def : WriteRes<WriteFAdd16, [SiFive7PipeB]>;
def : WriteRes<WriteFMul16, [SiFive7PipeB]>;
def : WriteRes<WriteFMA16, [SiFive7PipeB]>;
}
let Latency = 3 in {
def : WriteRes<WriteFSGNJ16, [SiFive7PipeB]>;
def : WriteRes<WriteFMinMax16, [SiFive7PipeB]>;
}

let Latency = 14, ReleaseAtCycles = [1, 13] in {
def :  WriteRes<WriteFDiv16, [SiFive7PipeB, SiFive7FDiv]>;
def :  WriteRes<WriteFSqrt16, [SiFive7PipeB, SiFive7FDiv]>;
}

// Single precision.
let Latency = 5 in {
def : WriteRes<WriteFAdd32, [SiFive7PipeB]>;
def : WriteRes<WriteFMul32, [SiFive7PipeB]>;
def : WriteRes<WriteFMA32, [SiFive7PipeB]>;
}
let Latency = 3 in {
def : WriteRes<WriteFSGNJ32, [SiFive7PipeB]>;
def : WriteRes<WriteFMinMax32, [SiFive7PipeB]>;
}

def : WriteRes<WriteFDiv32, [SiFive7PipeB, SiFive7FDiv]> { let Latency = 27;
                                                         let ReleaseAtCycles = [1, 26]; }
def : WriteRes<WriteFSqrt32, [SiFive7PipeB, SiFive7FDiv]> { let Latency = 27;
                                                          let ReleaseAtCycles = [1, 26]; }

// Double precision
let Latency = 7 in {
def : WriteRes<WriteFAdd64, [SiFive7PipeB]>;
def : WriteRes<WriteFMul64, [SiFive7PipeB]>;
def : WriteRes<WriteFMA64, [SiFive7PipeB]>;
}
let Latency = 3 in {
def : WriteRes<WriteFSGNJ64, [SiFive7PipeB]>;
def : WriteRes<WriteFMinMax64, [SiFive7PipeB]>;
}

def : WriteRes<WriteFDiv64, [SiFive7PipeB, SiFive7FDiv]> { let Latency = 56;
                                                         let ReleaseAtCycles = [1, 55]; }
def : WriteRes<WriteFSqrt64, [SiFive7PipeB, SiFive7FDiv]> { let Latency = 56;
                                                          let ReleaseAtCycles = [1, 55]; }

// Conversions
let Latency = 3 in {
def : WriteRes<WriteFCvtI32ToF16, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtI32ToF32, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtI32ToF64, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtI64ToF16, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtI64ToF32, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtI64ToF64, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtF16ToI32, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtF16ToI64, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtF16ToF32, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtF16ToF64, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtF32ToI32, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtF32ToI64, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtF32ToF16, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtF32ToF64, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtF64ToI32, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtF64ToI64, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtF64ToF16, [SiFive7PipeB]>;
def : WriteRes<WriteFCvtF64ToF32, [SiFive7PipeB]>;

def : WriteRes<WriteFClass16, [SiFive7PipeB]>;
def : WriteRes<WriteFClass32, [SiFive7PipeB]>;
def : WriteRes<WriteFClass64, [SiFive7PipeB]>;
def : WriteRes<WriteFCmp16, [SiFive7PipeB]>;
def : WriteRes<WriteFCmp32, [SiFive7PipeB]>;
def : WriteRes<WriteFCmp64, [SiFive7PipeB]>;
def : WriteRes<WriteFMovI16ToF16, [SiFive7PipeB]>;
def : WriteRes<WriteFMovF16ToI16, [SiFive7PipeB]>;
def : WriteRes<WriteFMovI32ToF32, [SiFive7PipeB]>;
def : WriteRes<WriteFMovF32ToI32, [SiFive7PipeB]>;
def : WriteRes<WriteFMovI64ToF64, [SiFive7PipeB]>;
def : WriteRes<WriteFMovF64ToI64, [SiFive7PipeB]>;
}

// 6. Configuration-Setting Instructions
let Latency = 3 in {
def : WriteRes<WriteVSETVLI, [SiFive7PipeA]>;
def : WriteRes<WriteVSETIVLI, [SiFive7PipeA]>;
def : WriteRes<WriteVSETVL, [SiFive7PipeA]>;
}

// 7. Vector Loads and Stores
// Unit-stride loads and stores can operate at the full bandwidth of the memory
// pipe. The memory pipe is DLEN bits wide on x280.
foreach mx = SchedMxList in {
  defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVLDE",    [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDFF",   [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
  }
  let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in
  defm "" : LMULWriteResMX<"WriteVSTE",    [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
}

foreach mx = SchedMxList in {
  defvar Cycles = SiFive7GetMaskLoadStoreCycles<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in
  defm "" : LMULWriteResMX<"WriteVLDM",    [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
  let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in
  defm "" : LMULWriteResMX<"WriteVSTM",    [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
}

// Strided loads and stores operate at one element per cycle and should be
// scheduled accordingly. Indexed loads and stores operate at one element per
// cycle, and they stall the machine until all addresses have been generated,
// so they cannot be scheduled. Indexed and strided loads and stores have LMUL
// specific suffixes, but since SEW is already encoded in the name of the
// resource, we do not need to use LMULSEWXXX constructors. However, we do
// use the SEW from the name to determine the number of Cycles.

// This predicate is true when the rs2 operand of vlse or vsse is x0, false
// otherwise.
def VLDSX0Pred : MCSchedPredicate<CheckRegOperand<3, X0>>;

foreach mx = SchedMxList in {
  defvar VLDSX0Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar Cycles = SiFive7GetCyclesOnePerElement<mx, 8>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  defm SiFive7 : LMULWriteResMXVariant<"WriteVLDS8",  VLDSX0Pred, [SiFive7VCQ, SiFive7VL],
                                       4, [0, 1], [1, !add(1, VLDSX0Cycles)], !add(3, Cycles),
                                       [0, 1], [1, !add(1, Cycles)], mx, IsWorstCase>;
  let Latency = !add(3, Cycles), AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVLDUX8", [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDOX8", [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
  }
  let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVSTS8",  [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTUX8", [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTOX8", [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
  }
}
// TODO: The MxLists need to be filtered by EEW. We only need to support
// LMUL >= SEW_min/ELEN. Here, the smallest EEW prevents us from having MF8
// since LMUL >= 16/64.
foreach mx = ["MF4", "MF2", "M1", "M2", "M4", "M8"] in {
  defvar VLDSX0Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar Cycles = SiFive7GetCyclesOnePerElement<mx, 16>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  defm SiFive7 : LMULWriteResMXVariant<"WriteVLDS16",  VLDSX0Pred, [SiFive7VCQ, SiFive7VL],
                                       4, [0, 1], [1, !add(1, VLDSX0Cycles)], !add(3, Cycles),
                                       [0, 1], [1, !add(1, Cycles)], mx, IsWorstCase>;
  let Latency = !add(3, Cycles), AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVLDUX16", [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDOX16", [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
  }
  let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVSTS16",  [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTUX16", [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTOX16", [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
  }
}
foreach mx = ["MF2", "M1", "M2", "M4", "M8"] in {
  defvar VLDSX0Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar Cycles = SiFive7GetCyclesOnePerElement<mx, 32>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  defm SiFive7 : LMULWriteResMXVariant<"WriteVLDS32",  VLDSX0Pred, [SiFive7VCQ, SiFive7VL],
                                       4, [0, 1], [1, !add(1, VLDSX0Cycles)], !add(3, Cycles),
                                       [0, 1], [1, !add(1, Cycles)], mx, IsWorstCase>;
  let Latency = !add(3, Cycles), AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVLDUX32", [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDOX32", [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
  }
  let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVSTS32",  [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTUX32", [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTOX32", [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
  }
}
foreach mx = ["M1", "M2", "M4", "M8"] in {
  defvar VLDSX0Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar Cycles = SiFive7GetCyclesOnePerElement<mx, 64>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  defm SiFive7 : LMULWriteResMXVariant<"WriteVLDS64",  VLDSX0Pred, [SiFive7VCQ, SiFive7VL],
                                       4, [0, 1], [1, !add(1, VLDSX0Cycles)], !add(3, Cycles),
                                       [0, 1], [1, !add(1, Cycles)], mx, IsWorstCase>;
  let Latency = !add(3, Cycles), AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVLDUX64", [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDOX64", [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
  }
  let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVSTS64",  [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTUX64", [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTOX64", [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
  }
}

// VLD*R is LMUL aware
let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 2)] in
  def : WriteRes<WriteVLD1R,  [SiFive7VCQ, SiFive7VL]>;
let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 4)] in
  def : WriteRes<WriteVLD2R,  [SiFive7VCQ, SiFive7VL]>;
let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 8)] in
  def : WriteRes<WriteVLD4R,  [SiFive7VCQ, SiFive7VL]>;
let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 16)] in
  def : WriteRes<WriteVLD8R,  [SiFive7VCQ, SiFive7VL]>;
// VST*R is LMUL aware
let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 2)] in
  def : WriteRes<WriteVST1R,   [SiFive7VCQ, SiFive7VS]>;
let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 4)] in
  def : WriteRes<WriteVST2R,   [SiFive7VCQ, SiFive7VS]>;
let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 8)] in
  def : WriteRes<WriteVST4R,   [SiFive7VCQ, SiFive7VS]>;
let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 16)] in
  def : WriteRes<WriteVST8R,   [SiFive7VCQ, SiFive7VS]>;

// Segmented Loads and Stores
// Unit-stride segmented loads and stores are effectively converted into strided
// segment loads and stores. Strided segment loads and stores operate at up to
// one segment per cycle if the segment fits within one aligned memory beat.
// Indexed segment loads and stores operate at the same rate as strided ones,
// but they stall the machine until all addresses have been generated.
foreach mx = SchedMxList in {
  foreach eew = [8, 16, 32, 64] in {
    defvar Cycles = SiFive7GetCyclesSegmentedSeg2<mx>.c;
    defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
    // Does not chain so set latency high
    let Latency = !add(3, Cycles), AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
      defm "" : LMULWriteResMX<"WriteVLSEG2e" # eew,   [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
      defm "" : LMULWriteResMX<"WriteVLSEGFF2e" # eew, [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
    }
    let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in
    defm "" : LMULWriteResMX<"WriteVSSEG2e" # eew,   [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
    foreach nf=3-8 in {
      defvar Cycles = SiFive7GetCyclesSegmented<mx, eew, nf>.c;
      defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
      // Does not chain so set latency high
      let Latency = !add(3, Cycles), AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
        defm "" : LMULWriteResMX<"WriteVLSEG" # nf # "e" # eew,   [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
        defm "" : LMULWriteResMX<"WriteVLSEGFF" # nf # "e" # eew, [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
      }
      let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in
      defm "" : LMULWriteResMX<"WriteVSSEG" # nf # "e" # eew,   [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
    }
  }
}
foreach mx = SchedMxList in {
  foreach nf=2-8 in {
    foreach eew = [8, 16, 32, 64] in {
      defvar Cycles = SiFive7GetCyclesSegmented<mx, eew, nf>.c;
      defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
      // Does not chain so set latency high
      let Latency = !add(3, Cycles), AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
        defm "" : LMULWriteResMX<"WriteVLSSEG" # nf # "e" # eew,  [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
        defm "" : LMULWriteResMX<"WriteVLUXSEG" # nf # "e" # eew, [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
        defm "" : LMULWriteResMX<"WriteVLOXSEG" # nf # "e" # eew, [SiFive7VCQ, SiFive7VL], mx, IsWorstCase>;
      }
      let Latency = 1, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
        defm "" : LMULWriteResMX<"WriteVSSSEG" # nf # "e" # eew,  [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
        defm "" : LMULWriteResMX<"WriteVSUXSEG" # nf # "e" # eew, [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
        defm "" : LMULWriteResMX<"WriteVSOXSEG" # nf # "e" # eew, [SiFive7VCQ, SiFive7VS], mx, IsWorstCase>;
      }
    }
  }
}

// 11. Vector Integer Arithmetic Instructions
foreach mx = SchedMxList in {
  defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVIALUV",     [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIALUX",     [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIALUI",     [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVICALUV",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVICALUX",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVICALUI",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVShiftV",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVShiftX",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVShiftI",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMinMaxV",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMinMaxX",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMulV",     [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMulX",     [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMulAddV",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMulAddX",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMergeV",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMergeX",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMergeI",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMovV",     [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMovX",     [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMovI",     [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
  // Mask results can't chain.
  let Latency = !add(Cycles, 3), AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVICmpV",     [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVICmpX",     [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVICmpI",     [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
}
foreach mx = SchedMxList in {
  defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVExtV",      [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
}
foreach mx = SchedMxList in {
  foreach sew = SchedSEWSet<mx>.val in {
    defvar Cycles = !mul(SiFive7GetDivOrSqrtFactor<sew>.c,
                         !div(SiFive7GetCyclesOnePerElement<mx, sew>.c, 4));
    defvar IsWorstCase = SiFive7IsWorstCaseMXSEW<mx, sew, SchedMxList>.c;
    let Latency = Cycles, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVIDivV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVIDivX", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
    }
  }
}

// Widening
foreach mx = SchedMxListW in {
  defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxListW>.c;
  let Latency = 8, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVIWALUV",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIWALUX",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIWALUI",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIWMulV",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIWMulX",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIWMulAddV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIWMulAddX", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
}
// Narrowing
foreach mx = SchedMxListW in {
  defvar Cycles = SiFive7GetCyclesNarrowing<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxListW>.c;
  let Latency = 8, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVNShiftV",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVNShiftX",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVNShiftI",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
}

// 12. Vector Fixed-Point Arithmetic Instructions
foreach mx = SchedMxList in {
  defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 8, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVSALUV",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSALUX",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSALUI",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVAALUV",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVAALUX",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSMulV",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSMulX",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSShiftV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSShiftX", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSShiftI", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
}
// Narrowing
foreach mx = SchedMxListW in {
  defvar Cycles = SiFive7GetCyclesNarrowing<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxListW>.c;
  let Latency = 8, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVNClipV",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVNClipX",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVNClipI",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
}

// 13. Vector Floating-Point Instructions
foreach mx = SchedMxListF in {
  foreach sew = SchedSEWSet<mx, isF=1>.val in {
    defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
    defvar IsWorstCase = SiFive7IsWorstCaseMXSEW<mx, sew, SchedMxListF, isF=1>.c;
    let Latency = 8, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFALUV",  [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFALUF",  [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFMulV",  [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFMulF",  [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFMulAddV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFMulAddF", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFRecpV",   [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFCvtIToFV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
    }
    let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFMinMaxV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFMinMaxF", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFSgnjV",   [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFSgnjF",   [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
    }
  }
}
foreach mx = SchedMxList in {
  defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 8, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVFCvtFToIV",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
  let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVFClassV",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVFMergeV",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVFMovV",      [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
  // Mask results can't chain.
  let Latency = !add(Cycles, 3), AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVFCmpV",      [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVFCmpF",      [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
}
foreach mx = SchedMxListF in {
  foreach sew = SchedSEWSet<mx, isF=1>.val in {
    defvar Cycles = !mul(SiFive7GetDivOrSqrtFactor<sew>.c,
                         !div(SiFive7GetCyclesOnePerElement<mx, sew>.c, 4));
    defvar IsWorstCase = SiFive7IsWorstCaseMXSEW<mx, sew, SchedMxListF, 1>.c;
    let Latency = Cycles, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFSqrtV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFDivV",  [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFDivF",  [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
    }
  }
}

// Widening
foreach mx = SchedMxListW in {
  foreach sew = SchedSEWSet<mx, isF=0, isWidening=1>.val in {
    defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
    defvar IsWorstCase = SiFive7IsWorstCaseMXSEW<mx, sew, SchedMxListW>.c;
    let Latency = 8, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in
    defm "" : LMULSEWWriteResMXSEW<"WriteVFWCvtIToFV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
  }
}
foreach mx = SchedMxListFW in {
  foreach sew = SchedSEWSet<mx, isF=1, isWidening=1>.val in {
    defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
    defvar IsWorstCase = SiFive7IsWorstCaseMXSEW<mx, sew, SchedMxListFW, isF=1>.c;
    let Latency = 8, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWALUV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWALUF", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWMulV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWMulF", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWMulAddV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWMulAddF", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWCvtFToFV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
    }
  }
  defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxListFW>.c;
  let Latency = 8, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in
  defm "" : LMULWriteResMX<"WriteVFWCvtFToIV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
}
// Narrowing
foreach mx = SchedMxListW in {
  defvar Cycles = SiFive7GetCyclesNarrowing<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxListW>.c;
  let Latency = 8, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVFNCvtFToIV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
}
foreach mx = SchedMxListFW in {
  foreach sew = SchedSEWSet<mx, isF=1, isWidening=1>.val in {
    defvar Cycles = SiFive7GetCyclesNarrowing<mx>.c;
    defvar IsWorstCase = SiFive7IsWorstCaseMXSEW<mx, sew, SchedMxListFW, isF=1>.c;
    let Latency = 8, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFNCvtIToFV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFNCvtFToFV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
    }
  }
}

// 14. Vector Reduction Operations
foreach mx = SchedMxList in {
  foreach sew = SchedSEWSet<mx>.val in {
    defvar Cycles = SiFive7GetReductionCycles<mx, sew>.c;
    defvar IsWorstCase = SiFive7IsWorstCaseMXSEW<mx, sew, SchedMxList>.c;
    let Latency = Cycles, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVIRedV_From", [SiFive7VCQ, SiFive7VA],
                                     mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVIRedMinMaxV_From", [SiFive7VCQ, SiFive7VA],
                                     mx, sew, IsWorstCase>;
    }
  }
}

foreach mx = SchedMxListWRed in {
  foreach sew = SchedSEWSet<mx, 0, 1>.val in {
    defvar Cycles = SiFive7GetReductionCycles<mx, sew>.c;
    defvar IsWorstCase = SiFive7IsWorstCaseMXSEW<mx, sew, SchedMxListWRed>.c;
    let Latency = Cycles, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in
    defm "" : LMULSEWWriteResMXSEW<"WriteVIWRedV_From", [SiFive7VCQ, SiFive7VA],
                                   mx, sew, IsWorstCase>;
  }
}

foreach mx = SchedMxListF in {
  foreach sew = SchedSEWSet<mx, 1>.val in {
    defvar RedCycles = SiFive7GetReductionCycles<mx, sew>.c;
    defvar IsWorstCase = SiFive7IsWorstCaseMXSEW<mx, sew, SchedMxListF, 1>.c;
    let Latency = RedCycles, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, RedCycles)] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFRedV_From", [SiFive7VCQ, SiFive7VA],
                                     mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFRedMinMaxV_From", [SiFive7VCQ, SiFive7VA],
                                     mx, sew, IsWorstCase>;
    }
    defvar OrdRedCycles = SiFive7GetOrderedReductionCycles<mx, sew>.c;
    let Latency = OrdRedCycles, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, OrdRedCycles)] in
    defm "" : LMULSEWWriteResMXSEW<"WriteVFRedOV_From", [SiFive7VCQ, SiFive7VA],
                                   mx, sew, IsWorstCase>;
  }
}

foreach mx = SchedMxListFWRed in {
  foreach sew = SchedSEWSet<mx, 1, 1>.val in {
    defvar RedCycles = SiFive7GetReductionCycles<mx, sew>.c;
    defvar IsWorstCase = SiFive7IsWorstCaseMXSEW<mx, sew, SchedMxListFWRed, 1>.c;
    let Latency = RedCycles, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, RedCycles)] in
    defm "" : LMULSEWWriteResMXSEW<"WriteVFWRedV_From", [SiFive7VCQ, SiFive7VA],
                                   mx, sew, IsWorstCase>;
    defvar OrdRedCycles = SiFive7GetOrderedReductionCycles<mx, sew>.c;
    let Latency = OrdRedCycles, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, OrdRedCycles)] in
    defm "" : LMULSEWWriteResMXSEW<"WriteVFWRedOV_From", [SiFive7VCQ, SiFive7VA],
                                   mx, sew, IsWorstCase>;
  }
}

// 15. Vector Mask Instructions
foreach mx = SchedMxList in {
  defvar Cycles = SiFive7GetCyclesVMask<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVMALUV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVMPopV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVMFFSV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVMSFSV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
}
foreach mx = SchedMxList in {
  defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVIotaV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIdxV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
}

// 16. Vector Permutation Instructions
let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 1)] in {
  def : WriteRes<WriteVMovSX, [SiFive7VCQ, SiFive7VA]>;
  def : WriteRes<WriteVMovXS, [SiFive7VCQ, SiFive7VA]>;
  def : WriteRes<WriteVMovSF, [SiFive7VCQ, SiFive7VA]>;
  def : WriteRes<WriteVMovFS, [SiFive7VCQ, SiFive7VA]>;
}
foreach mx = SchedMxList in {
  defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 8, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVRGatherVX",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVRGatherVI",    [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
}

foreach mx = SchedMxList in {
  foreach sew = SchedSEWSet<mx>.val in {
    defvar Cycles = SiFive7GetCyclesOnePerElement<mx, sew>.c;
    defvar IsWorstCase = SiFive7IsWorstCaseMXSEW<mx, sew, SchedMxList>.c;
    let Latency = !add(Cycles, 3), AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVRGatherVV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVRGatherEI16VV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVCompressV", [SiFive7VCQ, SiFive7VA], mx, sew, IsWorstCase>;
    }
  }
}

foreach mx = SchedMxList in {
  defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVSlideUpX",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSlideDownX", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSlideI",     [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVISlide1X",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVFSlide1F",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
  }
}

// VMov*V is LMUL Aware
let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 2)] in
  def : WriteRes<WriteVMov1V,     [SiFive7VCQ, SiFive7VA]>;
let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 4)] in
  def : WriteRes<WriteVMov2V,     [SiFive7VCQ, SiFive7VA]>;
let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 8)] in
  def : WriteRes<WriteVMov4V,     [SiFive7VCQ, SiFive7VA]>;
let Latency = 4, AcquireAtCycles = [0, 1], ReleaseAtCycles = [1, !add(1, 16)] in
  def : WriteRes<WriteVMov8V,     [SiFive7VCQ, SiFive7VA]>;

// Others
def : WriteRes<WriteCSR, [SiFive7PipeB]>;
def : WriteRes<WriteNop, []>;
let Latency = 3 in
  def : WriteRes<WriteRdVLENB, [SiFive7PipeB]>;

def : InstRW<[WriteIALU], (instrs COPY)>;

// VCIX
//
// In principle we don't know the latency of any VCIX instructions. But instead
// of taking the default of 1, which can lead to issues [1], we assume that they
// have a fairly high latency.
//
// [1] https://github.com/llvm/llvm-project/issues/83391
foreach mx = SchedMxList in {
  defvar Cycles = SiFive7GetCyclesDefault<mx>.c;
  defvar IsWorstCase = SiFive7IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = !mul(Cycles, 10),
      AcquireAtCycles = [0, 1],
      ReleaseAtCycles = [1, !add(1, Cycles)] in {
    defm "" : LMULWriteResMX<"WriteVC_V_I",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_V_X",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_V_IV",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_V_VV",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_V_XV",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_V_IVV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_V_IVW", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_V_VVV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_V_VVW", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_V_XVV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_V_XVW", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    foreach f = ["FPR16", "FPR32", "FPR64"] in {
      defm "" : LMULWriteResMX<"WriteVC_V_" # f # "V",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
      defm "" : LMULWriteResMX<"WriteVC_V_" # f # "VV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
      defm "" : LMULWriteResMX<"WriteVC_V_" # f # "VW", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    }
    defm "" : LMULWriteResMX<"WriteVC_I",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_X",   [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_IV",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_VV",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_XV",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_IVV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_IVW", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_VVV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_VVW", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_XVV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVC_XVW", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    foreach f = ["FPR16", "FPR32", "FPR64"] in {
      defm "" : LMULWriteResMX<"WriteVC_" # f # "V",  [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
      defm "" : LMULWriteResMX<"WriteVC_" # f # "VV", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
      defm "" : LMULWriteResMX<"WriteVC_" # f # "VW", [SiFive7VCQ, SiFive7VA], mx, IsWorstCase>;
    }
  }
}

//===----------------------------------------------------------------------===//

// Bypass and advance
def : SiFive7AnyToGPRBypass<ReadJmp>;
def : SiFive7AnyToGPRBypass<ReadJalr>;
def : ReadAdvance<ReadCSR, 0>;
def : SiFive7AnyToGPRBypass<ReadStoreData>;
def : ReadAdvance<ReadMemBase, 0>;
def : SiFive7AnyToGPRBypass<ReadIALU>;
def : SiFive7AnyToGPRBypass<ReadIALU32>;
def : SiFive7AnyToGPRBypass<ReadShiftImm>;
def : SiFive7AnyToGPRBypass<ReadShiftImm32>;
def : SiFive7AnyToGPRBypass<ReadShiftReg>;
def : SiFive7AnyToGPRBypass<ReadShiftReg32>;
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
def : ReadAdvance<ReadFAdd16, 0>;
def : ReadAdvance<ReadFAdd32, 0>;
def : ReadAdvance<ReadFAdd64, 0>;
def : ReadAdvance<ReadFMul16, 0>;
def : ReadAdvance<ReadFMA16, 0>;
def : ReadAdvance<ReadFMA16Addend, 0>;
def : ReadAdvance<ReadFMul32, 0>;
def : ReadAdvance<ReadFMul64, 0>;
def : ReadAdvance<ReadFMA32, 0>;
def : ReadAdvance<ReadFMA32Addend, 0>;
def : ReadAdvance<ReadFMA64, 0>;
def : ReadAdvance<ReadFMA64Addend, 0>;
def : ReadAdvance<ReadFDiv16, 0>;
def : ReadAdvance<ReadFDiv32, 0>;
def : ReadAdvance<ReadFDiv64, 0>;
def : ReadAdvance<ReadFSqrt16, 0>;
def : ReadAdvance<ReadFSqrt32, 0>;
def : ReadAdvance<ReadFSqrt64, 0>;
def : ReadAdvance<ReadFCmp16, 0>;
def : ReadAdvance<ReadFCmp32, 0>;
def : ReadAdvance<ReadFCmp64, 0>;
def : ReadAdvance<ReadFSGNJ16, 0>;
def : ReadAdvance<ReadFSGNJ32, 0>;
def : ReadAdvance<ReadFSGNJ64, 0>;
def : ReadAdvance<ReadFMinMax16, 0>;
def : ReadAdvance<ReadFMinMax32, 0>;
def : ReadAdvance<ReadFMinMax64, 0>;
def : ReadAdvance<ReadFCvtF16ToI32, 0>;
def : ReadAdvance<ReadFCvtF16ToI64, 0>;
def : ReadAdvance<ReadFCvtF32ToI32, 0>;
def : ReadAdvance<ReadFCvtF32ToI64, 0>;
def : ReadAdvance<ReadFCvtF64ToI32, 0>;
def : ReadAdvance<ReadFCvtF64ToI64, 0>;
def : ReadAdvance<ReadFCvtI32ToF16, 0>;
def : ReadAdvance<ReadFCvtI32ToF32, 0>;
def : ReadAdvance<ReadFCvtI32ToF64, 0>;
def : ReadAdvance<ReadFCvtI64ToF16, 0>;
def : ReadAdvance<ReadFCvtI64ToF32, 0>;
def : ReadAdvance<ReadFCvtI64ToF64, 0>;
def : ReadAdvance<ReadFCvtF32ToF64, 0>;
def : ReadAdvance<ReadFCvtF64ToF32, 0>;
def : ReadAdvance<ReadFCvtF16ToF32, 0>;
def : ReadAdvance<ReadFCvtF32ToF16, 0>;
def : ReadAdvance<ReadFCvtF16ToF64, 0>;
def : ReadAdvance<ReadFCvtF64ToF16, 0>;
def : ReadAdvance<ReadFMovF16ToI16, 0>;
def : ReadAdvance<ReadFMovI16ToF16, 0>;
def : ReadAdvance<ReadFMovF32ToI32, 0>;
def : ReadAdvance<ReadFMovI32ToF32, 0>;
def : ReadAdvance<ReadFMovF64ToI64, 0>;
def : ReadAdvance<ReadFMovI64ToF64, 0>;
def : ReadAdvance<ReadFClass16, 0>;
def : ReadAdvance<ReadFClass32, 0>;
def : ReadAdvance<ReadFClass64, 0>;

def : SiFive7AnyToGPRBypass<ReadSFBJmp, 0>;
def : SiFive7AnyToGPRBypass<ReadSFBALU, 0>;

// Bitmanip
def : SiFive7AnyToGPRBypass<ReadRotateImm>;
def : SiFive7AnyToGPRBypass<ReadRotateImm32>;
def : SiFive7AnyToGPRBypass<ReadRotateReg>;
def : SiFive7AnyToGPRBypass<ReadRotateReg32>;
def : SiFive7AnyToGPRBypass<ReadCLZ>;
def : SiFive7AnyToGPRBypass<ReadCLZ32>;
def : SiFive7AnyToGPRBypass<ReadCTZ>;
def : SiFive7AnyToGPRBypass<ReadCTZ32>;
def : ReadAdvance<ReadCPOP, 0>;
def : ReadAdvance<ReadCPOP32, 0>;
def : SiFive7AnyToGPRBypass<ReadORCB>;
def : SiFive7AnyToGPRBypass<ReadIMinMax>;
def : SiFive7AnyToGPRBypass<ReadREV8>;
def : SiFive7AnyToGPRBypass<ReadSHXADD>;
def : SiFive7AnyToGPRBypass<ReadSHXADD32>;
// Single-bit instructions
def : SiFive7AnyToGPRBypass<ReadSingleBit>;
def : SiFive7AnyToGPRBypass<ReadSingleBitImm>;

// 6. Configuration-Setting Instructions
def : ReadAdvance<ReadVSETVLI, 2>;
def : ReadAdvance<ReadVSETVL, 2>;

// 7. Vector Loads and Stores
def : ReadAdvance<ReadVLDX, 0>;
def : ReadAdvance<ReadVSTX, 0>;
defm "" : LMULReadAdvance<"ReadVSTEV", 0>;
defm "" : LMULReadAdvance<"ReadVSTM", 0>;
def : ReadAdvance<ReadVLDSX, 0>;
def : ReadAdvance<ReadVSTSX, 0>;
defm "" : LMULReadAdvance<"ReadVSTS8V", 0>;
defm "" : LMULReadAdvance<"ReadVSTS16V", 0>;
defm "" : LMULReadAdvance<"ReadVSTS32V", 0>;
defm "" : LMULReadAdvance<"ReadVSTS64V", 0>;
defm "" : LMULReadAdvance<"ReadVLDUXV", 0>;
defm "" : LMULReadAdvance<"ReadVLDOXV", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX8", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX16", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX32", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX64", 0>;
defm "" : LMULReadAdvance<"ReadVSTUXV", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX8V", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX16V", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX32V", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX64V", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX8", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX16", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX32", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX64", 0>;
defm "" : LMULReadAdvance<"ReadVSTOXV", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX8V", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX16V", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX32V", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX64V", 0>;
// LMUL Aware
def : ReadAdvance<ReadVST1R, 0>;
def : ReadAdvance<ReadVST2R, 0>;
def : ReadAdvance<ReadVST4R, 0>;
def : ReadAdvance<ReadVST8R, 0>;

// 12. Vector Integer Arithmetic Instructions
defm : LMULReadAdvance<"ReadVIALUV", 0>;
defm : LMULReadAdvance<"ReadVIALUX", 0>;
defm : LMULReadAdvanceW<"ReadVIWALUV", 0>;
defm : LMULReadAdvanceW<"ReadVIWALUX", 0>;
defm : LMULReadAdvance<"ReadVExtV", 0>;
defm : LMULReadAdvance<"ReadVICALUV", 0>;
defm : LMULReadAdvance<"ReadVICALUX", 0>;
defm : LMULReadAdvance<"ReadVShiftV", 0>;
defm : LMULReadAdvance<"ReadVShiftX", 0>;
defm : LMULReadAdvanceW<"ReadVNShiftV", 0>;
defm : LMULReadAdvanceW<"ReadVNShiftX", 0>;
defm : LMULReadAdvance<"ReadVICmpV", 0>;
defm : LMULReadAdvance<"ReadVICmpX", 0>;
defm : LMULReadAdvance<"ReadVIMinMaxV", 0>;
defm : LMULReadAdvance<"ReadVIMinMaxX", 0>;
defm : LMULReadAdvance<"ReadVIMulV", 0>;
defm : LMULReadAdvance<"ReadVIMulX", 0>;
defm : LMULSEWReadAdvance<"ReadVIDivV", 0>;
defm : LMULSEWReadAdvance<"ReadVIDivX", 0>;
defm : LMULReadAdvanceW<"ReadVIWMulV", 0>;
defm : LMULReadAdvanceW<"ReadVIWMulX", 0>;
defm : LMULReadAdvance<"ReadVIMulAddV", 0>;
defm : LMULReadAdvance<"ReadVIMulAddX", 0>;
defm : LMULReadAdvanceW<"ReadVIWMulAddV", 0>;
defm : LMULReadAdvanceW<"ReadVIWMulAddX", 0>;
defm : LMULReadAdvance<"ReadVIMergeV", 0>;
defm : LMULReadAdvance<"ReadVIMergeX", 0>;
defm : LMULReadAdvance<"ReadVIMovV", 0>;
defm : LMULReadAdvance<"ReadVIMovX", 0>;

// 13. Vector Fixed-Point Arithmetic Instructions
defm "" : LMULReadAdvance<"ReadVSALUV", 0>;
defm "" : LMULReadAdvance<"ReadVSALUX", 0>;
defm "" : LMULReadAdvance<"ReadVAALUV", 0>;
defm "" : LMULReadAdvance<"ReadVAALUX", 0>;
defm "" : LMULReadAdvance<"ReadVSMulV", 0>;
defm "" : LMULReadAdvance<"ReadVSMulX", 0>;
defm "" : LMULReadAdvance<"ReadVSShiftV", 0>;
defm "" : LMULReadAdvance<"ReadVSShiftX", 0>;
defm "" : LMULReadAdvanceW<"ReadVNClipV", 0>;
defm "" : LMULReadAdvanceW<"ReadVNClipX", 0>;

// 14. Vector Floating-Point Instructions
defm "" : LMULSEWReadAdvanceF<"ReadVFALUV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFALUF", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWALUV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWALUF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMulV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMulF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFDivV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFDivF", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWMulV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWMulF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMulAddV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMulAddF", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWMulAddV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWMulAddF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFSqrtV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFRecpV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMinMaxV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMinMaxF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFSgnjV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFSgnjF", 0>;
defm "" : LMULReadAdvance<"ReadVFCmpV", 0>;
defm "" : LMULReadAdvance<"ReadVFCmpF", 0>;
defm "" : LMULReadAdvance<"ReadVFClassV", 0>;
defm "" : LMULReadAdvance<"ReadVFMergeV", 0>;
defm "" : LMULReadAdvance<"ReadVFMergeF", 0>;
defm "" : LMULReadAdvance<"ReadVFMovF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFCvtIToFV", 0>;
defm "" : LMULReadAdvance<"ReadVFCvtFToIV", 0>;
defm "" : LMULSEWReadAdvanceW<"ReadVFWCvtIToFV", 0>;
defm "" : LMULReadAdvanceFW<"ReadVFWCvtFToIV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWCvtFToFV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFNCvtIToFV", 0>;
defm "" : LMULReadAdvanceW<"ReadVFNCvtFToIV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFNCvtFToFV", 0>;

// 15. Vector Reduction Operations
def : ReadAdvance<ReadVIRedV, 0>;
def : ReadAdvance<ReadVIRedV0, 0>;
def : ReadAdvance<ReadVIWRedV, 0>;
def : ReadAdvance<ReadVIWRedV0, 0>;
def : ReadAdvance<ReadVFRedV, 0>;
def : ReadAdvance<ReadVFRedV0, 0>;
def : ReadAdvance<ReadVFRedOV, 0>;
def : ReadAdvance<ReadVFRedOV0, 0>;
def : ReadAdvance<ReadVFWRedV, 0>;
def : ReadAdvance<ReadVFWRedV0, 0>;
def : ReadAdvance<ReadVFWRedOV, 0>;
def : ReadAdvance<ReadVFWRedOV0, 0>;

// 16. Vector Mask Instructions
defm "" : LMULReadAdvance<"ReadVMALUV", 0>;
defm "" : LMULReadAdvance<"ReadVMPopV", 0>;
defm "" : LMULReadAdvance<"ReadVMFFSV", 0>;
defm "" : LMULReadAdvance<"ReadVMSFSV", 0>;
defm "" : LMULReadAdvance<"ReadVIotaV", 0>;

// 17. Vector Permutation Instructions
def : ReadAdvance<ReadVMovXS, 0>;
def : ReadAdvance<ReadVMovSX_V, 0>;
def : ReadAdvance<ReadVMovSX_X, 0>;
def : ReadAdvance<ReadVMovFS, 0>;
def : ReadAdvance<ReadVMovSF_V, 0>;
def : ReadAdvance<ReadVMovSF_F, 0>;
defm "" : LMULReadAdvance<"ReadVISlideV", 0>;
defm "" : LMULReadAdvance<"ReadVISlideX", 0>;
defm "" : LMULReadAdvance<"ReadVFSlideV", 0>;
defm "" : LMULReadAdvance<"ReadVFSlideF", 0>;
defm "" : LMULSEWReadAdvance<"ReadVRGatherVV_data", 0>;
defm "" : LMULSEWReadAdvance<"ReadVRGatherVV_index", 0>;
defm "" : LMULSEWReadAdvance<"ReadVRGatherEI16VV_data", 0>;
defm "" : LMULSEWReadAdvance<"ReadVRGatherEI16VV_index", 0>;
defm "" : LMULReadAdvance<"ReadVRGatherVX_data", 0>;
defm "" : LMULReadAdvance<"ReadVRGatherVX_index", 0>;
defm "" : LMULReadAdvance<"ReadVRGatherVI_data", 0>;
defm "" : LMULSEWReadAdvance<"ReadVCompressV", 0>;
// LMUL Aware
def : ReadAdvance<ReadVMov1V, 0>;
def : ReadAdvance<ReadVMov2V, 0>;
def : ReadAdvance<ReadVMov4V, 0>;
def : ReadAdvance<ReadVMov8V, 0>;

// Others
def : ReadAdvance<ReadVMask, 0>;
def : ReadAdvance<ReadVMergeOp_WorstCase, 0>;
foreach mx = SchedMxList in {
  def : ReadAdvance<!cast<SchedRead>("ReadVMergeOp_" # mx), 0>;
  foreach sew = SchedSEWSet<mx>.val in
    def : ReadAdvance<!cast<SchedRead>("ReadVMergeOp_" # mx  # "_E" # sew), 0>;
}

//===----------------------------------------------------------------------===//
// Unsupported extensions
defm : UnsupportedSchedZabha;
defm : UnsupportedSchedZbc;
defm : UnsupportedSchedZbkb;
defm : UnsupportedSchedZbkx;
defm : UnsupportedSchedZfa;
defm : UnsupportedSchedZvk;
}

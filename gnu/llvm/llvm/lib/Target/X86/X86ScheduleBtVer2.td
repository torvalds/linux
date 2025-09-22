//=- X86ScheduleBtVer2.td - X86 BtVer2 (Jaguar) Scheduling ---*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for AMD btver2 (Jaguar) to support
// instruction scheduling and other instruction cost heuristics. Based off AMD Software
// Optimization Guide for AMD Family 16h Processors & Instruction Latency appendix.
//
//===----------------------------------------------------------------------===//

def BtVer2Model : SchedMachineModel {
  // All x86 instructions are modeled as a single micro-op, and btver2 can
  // decode 2 instructions per cycle.
  let IssueWidth = 2;
  let MicroOpBufferSize = 64; // Retire Control Unit
  let LoadLatency = 5; // FPU latency (worse case cf Integer 3 cycle latency)
  let HighLatency = 25;
  let MispredictPenalty = 14; // Minimum branch misdirection penalty
  let PostRAScheduler = 1;

  // FIXME: SSE4/AVX is unimplemented. This flag is set to allow
  // the scheduler to assign a default model to unrecognized opcodes.
  let CompleteModel = 0;
}

let SchedModel = BtVer2Model in {

// Jaguar can issue up to 6 micro-ops in one cycle
def JALU0 : ProcResource<1>; // Integer Pipe0: integer ALU0 (also handle FP->INT jam)
def JALU1 : ProcResource<1>; // Integer Pipe1: integer ALU1/MUL/DIV
def JLAGU : ProcResource<1>; // Integer Pipe2: LAGU
def JSAGU : ProcResource<1>; // Integer Pipe3: SAGU (also handles 3-operand LEA)
def JFPU0 : ProcResource<1>; // Vector/FPU Pipe0: VALU0/VIMUL/FPA
def JFPU1 : ProcResource<1>; // Vector/FPU Pipe1: VALU1/STC/FPM

// The Integer PRF for Jaguar is 64 entries, and it holds the architectural and
// speculative version of the 64-bit integer registers.
// Reference: www.realworldtech.com/jaguar/4/
//
// The processor always keeps the different parts of an integer register
// together. An instruction that writes to a part of a register will therefore
// have a false dependence on any previous write to the same register or any
// part of it.
// Reference: Section 21.10 "AMD Bobcat and Jaguar pipeline: Partial register
// access" - Agner Fog's "microarchitecture.pdf".
def JIntegerPRF : RegisterFile<64, [GR64, CCR], [1, 1], [1, 0],
                               0,  // Max moves that can be eliminated per cycle.
                               1>; // Restrict move elimination to zero regs.

// The Jaguar FP Retire Queue renames SIMD and FP uOps onto a pool of 72 SSE
// registers. Operations on 256-bit data types are cracked into two COPs.
// Reference: www.realworldtech.com/jaguar/4/

// The PRF in the floating point unit can eliminate a move from a MMX or SSE
// register that is know to be zero (i.e. it has been zeroed using a zero-idiom
// dependency breaking instruction, or via VZEROALL).
// Reference: Section 21.8 "AMD Bobcat and Jaguar pipeline: Dependency-breaking
// instructions" - Agner Fog's "microarchitecture.pdf"
def JFpuPRF: RegisterFile<72, [VR64, VR128, VR256], [1, 1, 2], [1, 1, 0],
                          0,  // Max moves that can be eliminated per cycle.
                          1>; // Restrict move elimination to zero regs.

// The retire control unit (RCU) can track up to 64 macro-ops in-flight. It can
// retire up to two macro-ops per cycle.
// Reference: "Software Optimization Guide for AMD Family 16h Processors"
def JRCU : RetireControlUnit<64, 2>;

// Integer Pipe Scheduler
def JALU01 : ProcResGroup<[JALU0, JALU1]> {
  let BufferSize=20;
}

// AGU Pipe Scheduler
def JLSAGU : ProcResGroup<[JLAGU, JSAGU]> {
  let BufferSize=12;
}

// Fpu Pipe Scheduler
def JFPU01 : ProcResGroup<[JFPU0, JFPU1]> {
  let BufferSize=18;
}

// Functional units
def JDiv    : ProcResource<1>; // integer division
def JMul    : ProcResource<1>; // integer multiplication
def JVALU0  : ProcResource<1>; // vector integer
def JVALU1  : ProcResource<1>; // vector integer
def JVIMUL  : ProcResource<1>; // vector integer multiplication
def JSTC    : ProcResource<1>; // vector store/convert
def JFPM    : ProcResource<1>; // FP multiplication
def JFPA    : ProcResource<1>; // FP addition

// Functional unit groups
def JFPX  : ProcResGroup<[JFPA, JFPM]>;
def JVALU : ProcResGroup<[JVALU0, JVALU1]>;

// Integer loads are 3 cycles, so ReadAfterLd registers needn't be available until 3
// cycles after the memory operand.
def : ReadAdvance<ReadAfterLd, 3>;

// Vector loads are 5 cycles, so ReadAfterVec*Ld registers needn't be available until 5
// cycles after the memory operand.
def : ReadAdvance<ReadAfterVecLd, 5>;
def : ReadAdvance<ReadAfterVecXLd, 5>;
def : ReadAdvance<ReadAfterVecYLd, 5>;

/// "Additional 6 cycle transfer operation which moves a floating point
/// operation input value from the integer unit to the floating point unit.
/// Reference: AMDfam16h SOG (Appendix A "Instruction Latencies", Section A.2).
def : ReadAdvance<ReadInt2Fpu, -6>;

// Many SchedWrites are defined in pairs with and without a folded load.
// Instructions with folded loads are usually micro-fused, so they only appear
// as two micro-ops when dispatched by the schedulers.
// This multiclass defines the resource usage for variants with and without
// folded loads.
multiclass JWriteResIntPair<X86FoldableSchedWrite SchedRW,
                            list<ProcResourceKind> ExePorts,
                            int Lat, list<int> Res = [], int UOps = 1,
                            int LoadUOps = 0> {
  // Register variant is using a single cycle on ExePort.
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ReleaseAtCycles = Res;
    let NumMicroOps = UOps;
  }

  // Memory variant also uses a cycle on JLAGU and adds 3 cycles to the
  // latency.
  def : WriteRes<SchedRW.Folded, !listconcat([JLAGU], ExePorts)> {
    let Latency = !add(Lat, 3);
    let ReleaseAtCycles = !if(!empty(Res), [], !listconcat([1], Res));
    let NumMicroOps = !add(UOps, LoadUOps);
  }
}

multiclass JWriteResFpuPair<X86FoldableSchedWrite SchedRW,
                            list<ProcResourceKind> ExePorts,
                            int Lat, list<int> Res = [], int UOps = 1,
                            int LoadUOps = 0> {
  // Register variant is using a single cycle on ExePort.
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ReleaseAtCycles = Res;
    let NumMicroOps = UOps;
  }

  // Memory variant also uses a cycle on JLAGU and adds 5 cycles to the
  // latency.
  def : WriteRes<SchedRW.Folded, !listconcat([JLAGU], ExePorts)> {
    let Latency = !add(Lat, 5);
    let ReleaseAtCycles = !if(!empty(Res), [], !listconcat([1], Res));
    let NumMicroOps = !add(UOps, LoadUOps);
  }
}

multiclass JWriteResYMMPair<X86FoldableSchedWrite SchedRW,
                            list<ProcResourceKind> ExePorts,
                            int Lat, list<int> Res = [2], int UOps = 2,
                            int LoadUOps = 0> {
  // Register variant is using a single cycle on ExePort.
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ReleaseAtCycles = Res;
    let NumMicroOps = UOps;
  }

  // Memory variant also uses 2 cycles on JLAGU and adds 5 cycles to the
  // latency.
  def : WriteRes<SchedRW.Folded, !listconcat([JLAGU], ExePorts)> {
    let Latency = !add(Lat, 5);
    let ReleaseAtCycles = !listconcat([2], Res);
    let NumMicroOps = !add(UOps, LoadUOps);
  }
}

// Instructions that have local forwarding disabled have an extra +1cy latency.

// A folded store needs a cycle on the SAGU for the store data, most RMW
// instructions don't need an extra uop.  ALU RMW operations don't seem to
// benefit from STLF, and their observed latency is 6cy. That is the reason why
// this write adds two extra cycles (instead of just 1cy for the store).
defm : X86WriteRes<WriteRMW, [JSAGU], 2, [1], 0>;

////////////////////////////////////////////////////////////////////////////////
// Arithmetic.
////////////////////////////////////////////////////////////////////////////////

defm : JWriteResIntPair<WriteALU,    [JALU01], 1>;
defm : JWriteResIntPair<WriteADC,    [JALU01], 1, [2]>;

defm : X86WriteRes<WriteBSWAP32,     [JALU01], 1, [1], 1>;
defm : X86WriteRes<WriteBSWAP64,     [JALU01], 1, [1], 1>;
defm : X86WriteRes<WriteCMPXCHG,     [JALU01], 3, [3], 5>;
defm : X86WriteRes<WriteCMPXCHGRMW,  [JALU01, JSAGU, JLAGU], 11, [3, 1, 1], 6>;
defm : X86WriteRes<WriteXCHG,        [JALU01], 1, [2], 2>;

defm : JWriteResIntPair<WriteIMul8,     [JALU1, JMul], 3, [1, 1], 1>;
defm : JWriteResIntPair<WriteIMul16,    [JALU1, JMul], 3, [1, 3], 3>;
defm : JWriteResIntPair<WriteIMul16Imm, [JALU1, JMul], 4, [1, 2], 2>;
defm : JWriteResIntPair<WriteIMul16Reg, [JALU1, JMul], 3, [1, 1], 1>;
defm : JWriteResIntPair<WriteIMul32,    [JALU1, JMul], 3, [1, 2], 2>;
defm : JWriteResIntPair<WriteIMul32Imm, [JALU1, JMul], 3, [1, 1], 1>;
defm : JWriteResIntPair<WriteIMul32Reg, [JALU1, JMul], 3, [1, 1], 1>;
defm : JWriteResIntPair<WriteIMul64,    [JALU1, JMul], 6, [1, 4], 2>;  
defm : JWriteResIntPair<WriteIMul64Imm, [JALU1, JMul], 6, [1, 4], 1>;
defm : JWriteResIntPair<WriteIMul64Reg, [JALU1, JMul], 6, [1, 4], 1>;
defm : X86WriteResUnsupported<WriteIMulH>;
defm : X86WriteResUnsupported<WriteIMulHLd>;
defm : X86WriteResPairUnsupported<WriteMULX32>;
defm : X86WriteResPairUnsupported<WriteMULX64>;

defm : JWriteResIntPair<WriteDiv8,   [JALU1, JDiv], 12, [1, 12], 1>;
defm : JWriteResIntPair<WriteDiv16,  [JALU1, JDiv], 17, [1, 17], 2>;
defm : JWriteResIntPair<WriteDiv32,  [JALU1, JDiv], 25, [1, 25], 2>;
defm : JWriteResIntPair<WriteDiv64,  [JALU1, JDiv], 41, [1, 41], 2>;
defm : JWriteResIntPair<WriteIDiv8,  [JALU1, JDiv], 12, [1, 12], 1>;
defm : JWriteResIntPair<WriteIDiv16, [JALU1, JDiv], 17, [1, 17], 2>;
defm : JWriteResIntPair<WriteIDiv32, [JALU1, JDiv], 25, [1, 25], 2>;
defm : JWriteResIntPair<WriteIDiv64, [JALU1, JDiv], 41, [1, 41], 2>;

defm : JWriteResIntPair<WriteCRC32,  [JALU01], 3, [4], 3>;

defm : JWriteResIntPair<WriteCMOV,  [JALU01], 1>; // Conditional move.
defm : X86WriteRes<WriteFCMOV, [JFPU0, JFPA], 3, [1,1], 1>; // x87 conditional move.
def  : WriteRes<WriteSETCC, [JALU01]>; // Setcc.
def  : WriteRes<WriteSETCCStore, [JALU01,JSAGU]>;
def  : WriteRes<WriteLAHFSAHF, [JALU01]>;

defm : X86WriteRes<WriteBitTest,         [JALU01], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestImmLd,    [JALU01,JLAGU], 4, [1,1], 1>;
defm : X86WriteRes<WriteBitTestRegLd,    [JALU01,JLAGU], 4, [1,1], 5>;
defm : X86WriteRes<WriteBitTestSet,      [JALU01], 1, [1], 2>;
defm : X86WriteRes<WriteBitTestSetImmLd, [JALU01,JLAGU], 4, [1,1], 4>;
defm : X86WriteRes<WriteBitTestSetRegLd, [JALU01,JLAGU], 4, [1,1], 8>;

// This is for simple LEAs with one or two input operands.
def : WriteRes<WriteLEA, [JALU01]>;

// Bit counts.
defm : JWriteResIntPair<WriteBSF, [JALU01], 4, [8], 7>;
defm : JWriteResIntPair<WriteBSR, [JALU01], 5, [8], 8>;
defm : JWriteResIntPair<WritePOPCNT,         [JALU01], 1>;
defm : JWriteResIntPair<WriteLZCNT,          [JALU01], 1>;
defm : JWriteResIntPair<WriteTZCNT,          [JALU01], 2, [2], 2>;

// BMI1 BEXTR/BLS, BMI2 BZHI
defm : JWriteResIntPair<WriteBEXTR, [JALU01], 1>;
defm : JWriteResIntPair<WriteBLS,   [JALU01], 2, [2], 2>;
defm : X86WriteResPairUnsupported<WriteBZHI>;

////////////////////////////////////////////////////////////////////////////////
// Integer shifts and rotates.
////////////////////////////////////////////////////////////////////////////////

defm : JWriteResIntPair<WriteShift,    [JALU01], 1>;
defm : JWriteResIntPair<WriteShiftCL,  [JALU01], 1>;
defm : JWriteResIntPair<WriteRotate,   [JALU01], 1>;
defm : JWriteResIntPair<WriteRotateCL, [JALU01], 1>;

// SHLD/SHRD.
defm : X86WriteRes<WriteSHDrri, [JALU01], 3, [6], 6>;
defm : X86WriteRes<WriteSHDrrcl,[JALU01], 4, [8], 7>;
defm : X86WriteRes<WriteSHDmri, [JLAGU, JALU01], 9, [1, 22], 8>;
defm : X86WriteRes<WriteSHDmrcl,[JLAGU, JALU01], 9, [1, 22], 8>;

////////////////////////////////////////////////////////////////////////////////
// Loads, stores, and moves, not folded with other operations.
////////////////////////////////////////////////////////////////////////////////

def : WriteRes<WriteLoad,    [JLAGU]> { let Latency = 3; }
def : WriteRes<WriteStore,   [JSAGU]>;
def : WriteRes<WriteStoreNT, [JSAGU]>;
def : WriteRes<WriteMove,    [JALU01]>;
defm : X86WriteResUnsupported<WriteVecMaskedGatherWriteback>;

// Load/store MXCSR.
def : WriteRes<WriteLDMXCSR, [JLAGU]> { let Latency = 3; }
def : WriteRes<WriteSTMXCSR, [JSAGU]>;

// Treat misc copies as a move.
def : InstRW<[WriteMove], (instrs COPY)>;

////////////////////////////////////////////////////////////////////////////////
// Idioms that clear a register, like xorps %xmm0, %xmm0.
// These can often bypass execution ports completely.
////////////////////////////////////////////////////////////////////////////////

def : WriteRes<WriteZero,  []>;

////////////////////////////////////////////////////////////////////////////////
// Branches don't produce values, so they have no latency, but they still
// consume resources. Indirect branches can fold loads.
////////////////////////////////////////////////////////////////////////////////

defm : JWriteResIntPair<WriteJump,  [JALU01], 1>;

////////////////////////////////////////////////////////////////////////////////
// Special case scheduling classes.
////////////////////////////////////////////////////////////////////////////////

def : WriteRes<WriteSystem,     [JALU01]> { let Latency = 100; }
def : WriteRes<WriteMicrocoded, [JALU01]> { let Latency = 100; }
def : WriteRes<WriteFence,  [JSAGU]>;

// Nops don't have dependencies, so there's no actual latency, but we set this
// to '1' to tell the scheduler that the nop uses an ALU slot for a cycle.
def : WriteRes<WriteNop, [JALU01]> { let Latency = 1; }

def JWriteCMPXCHG8rr : SchedWriteRes<[JALU01]> {
  let Latency = 3;
  let ReleaseAtCycles = [3];
  let NumMicroOps = 3;
}

def JWriteLOCK_CMPXCHG8rm : SchedWriteRes<[JALU01, JLAGU, JSAGU]> {
  let Latency = 16;
  let ReleaseAtCycles = [3,16,16];
  let NumMicroOps = 5;
}

def JWriteLOCK_CMPXCHGrm : SchedWriteRes<[JALU01, JLAGU, JSAGU]> {
  let Latency = 17;
  let ReleaseAtCycles = [3,17,17];
  let NumMicroOps = 6;
}

def JWriteCMPXCHG8rm : SchedWriteRes<[JALU01, JLAGU, JSAGU]> {
  let Latency = 11;
  let ReleaseAtCycles = [3,1,1];
  let NumMicroOps = 5;
}

def JWriteCMPXCHG8B : SchedWriteRes<[JALU01, JLAGU, JSAGU]> {
  let Latency = 11;
  let ReleaseAtCycles = [3,1,1];
  let NumMicroOps = 18;
}

def JWriteCMPXCHG16B : SchedWriteRes<[JALU01, JLAGU, JSAGU]> {
  let Latency = 32;
  let ReleaseAtCycles = [6,1,1];
  let NumMicroOps = 28;
}

def JWriteLOCK_CMPXCHG8B : SchedWriteRes<[JALU01, JLAGU, JSAGU]> {
  let Latency = 19;
  let ReleaseAtCycles = [3,19,19];
  let NumMicroOps = 18;
}

def JWriteLOCK_CMPXCHG16B : SchedWriteRes<[JALU01, JLAGU, JSAGU]> {
  let Latency = 38;
  let ReleaseAtCycles = [6,38,38];
  let NumMicroOps = 28;
}

def JWriteCMPXCHGVariant :  SchedWriteVariant<[
  SchedVar<MCSchedPredicate<IsAtomicCompareAndSwap8B>,  [JWriteLOCK_CMPXCHG8B]>,
  SchedVar<MCSchedPredicate<IsAtomicCompareAndSwap16B>, [JWriteLOCK_CMPXCHG16B]>,
  SchedVar<MCSchedPredicate<IsAtomicCompareAndSwap_8>,  [JWriteLOCK_CMPXCHG8rm]>,
  SchedVar<MCSchedPredicate<IsAtomicCompareAndSwap>,    [JWriteLOCK_CMPXCHGrm]>,
  SchedVar<MCSchedPredicate<IsCompareAndSwap8B>,        [JWriteCMPXCHG8B]>,
  SchedVar<MCSchedPredicate<IsCompareAndSwap16B>,       [JWriteCMPXCHG16B]>,
  SchedVar<MCSchedPredicate<IsRegMemCompareAndSwap_8>,  [JWriteCMPXCHG8rm]>,
  SchedVar<MCSchedPredicate<IsRegMemCompareAndSwap>,    [WriteCMPXCHGRMW]>,
  SchedVar<MCSchedPredicate<IsRegRegCompareAndSwap_8>,  [JWriteCMPXCHG8rr]>,
  SchedVar<NoSchedPred,                                 [WriteCMPXCHG]>
]>;

// The first five reads are contributed by the memory load operand.
// We ignore those reads and set a read-advance for the other input operands
// including the implicit read of RAX.
def : InstRW<[JWriteCMPXCHGVariant,
              ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault,
              ReadAfterLd, ReadAfterLd], (instrs LCMPXCHG8, LCMPXCHG16,
                                                 LCMPXCHG32, LCMPXCHG64,
                                                 CMPXCHG8rm, CMPXCHG16rm,
                                                 CMPXCHG32rm, CMPXCHG64rm)>;

def : InstRW<[JWriteCMPXCHGVariant], (instrs CMPXCHG8rr, CMPXCHG16rr,
                                             CMPXCHG32rr, CMPXCHG64rr)>;

def : InstRW<[JWriteCMPXCHGVariant,
              // Ignore reads contributed by the memory operand.
              ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault,
              // Add a read-advance to every implicit register read.
              ReadAfterLd, ReadAfterLd, ReadAfterLd, ReadAfterLd], (instrs LCMPXCHG8B, LCMPXCHG16B,
                                                                           CMPXCHG8B, CMPXCHG16B)>;

def JWriteLOCK_ALURMW : SchedWriteRes<[JALU01, JLAGU, JSAGU]> {
  let Latency = 19;
  let ReleaseAtCycles = [1,19,19];
  let NumMicroOps = 1;
}

def JWriteLOCK_ALURMWVariant :  SchedWriteVariant<[
  SchedVar<MCSchedPredicate<CheckLockPrefix>, [JWriteLOCK_ALURMW]>,
  SchedVar<NoSchedPred,                       [WriteALURMW]>
]>;
def : InstRW<[JWriteLOCK_ALURMWVariant], (instrs INC8m, INC16m, INC32m, INC64m,
                                                 DEC8m, DEC16m, DEC32m, DEC64m,
                                                 NOT8m, NOT16m, NOT32m, NOT64m,
                                                 NEG8m, NEG16m, NEG32m, NEG64m)>;

def JWriteXCHG8rr_XADDrr : SchedWriteRes<[JALU01]> {
  let Latency = 2;
  let ReleaseAtCycles = [3];
  let NumMicroOps = 3;
}
def : InstRW<[JWriteXCHG8rr_XADDrr], (instrs XCHG8rr, XADD8rr, XADD16rr,
                                                      XADD32rr, XADD64rr)>;

// This write defines the latency of the in/out register operand of a non-atomic
// XADDrm. This is the first of a pair of writes that model non-atomic
// XADDrm instructions (the second write definition is JWriteXADDrm_LdSt_Part).
//
// We need two writes because the instruction latency differs from the output
// register operand latency. In particular, the first write describes the first
// (and only) output register operand of the instruction.  However, the
// instruction latency is set to the MAX of all the write latencies. That's why
// a second write is needed in this case (see example below).
//
// Example:
//     XADD %ecx, (%rsp)      ## Instruction latency: 11cy
//                            ## ECX write Latency: 3cy
//
// Register ECX becomes available in 3 cycles. That is because the value of ECX
// is exchanged with the value read from the stack pointer, and the load-to-use
// latency is assumed to be 3cy.
def JWriteXADDrm_XCHG_Part : SchedWriteRes<[JALU01]> {
  let Latency = 3;  // load-to-use latency
  let ReleaseAtCycles = [3];
  let NumMicroOps = 3;
}

// This write defines the latency of the in/out register operand of an atomic
// XADDrm. This is the first of a sequence of two writes used to model atomic
// XADD instructions. The second write of the sequence is JWriteXCHGrm_LdSt_Part.
//
//
// Example:
//    LOCK XADD %ecx, (%rsp)     ## Instruction Latency: 16cy
//                               ## ECX write Latency: 11cy
//
// The value of ECX becomes available only after 11cy from the start of
// execution. This write is used to specifically set that operand latency. 
def JWriteLOCK_XADDrm_XCHG_Part : SchedWriteRes<[JALU01]> {
  let Latency = 11;
  let ReleaseAtCycles = [3];
  let NumMicroOps = 3;
}

// This write defines the latency of the in/out register operand of an atomic
// XCHGrm. This write is the first of a sequence of two writes that describe
// atomic XCHG operations. We need two writes because the instruction latency
// differs from the output register write latency.  We want to make sure that
// the output register operand becomes visible after 11cy. However, we want to
// set the instruction latency to 16cy.
def JWriteXCHGrm_XCHG_Part : SchedWriteRes<[JALU01]> {
  let Latency = 11;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}

def JWriteXADDrm_LdSt_Part : SchedWriteRes<[JLAGU, JSAGU]> {
  let Latency = 11;
  let ReleaseAtCycles = [1, 1];
  let NumMicroOps = 1;
}

def JWriteXCHGrm_LdSt_Part : SchedWriteRes<[JLAGU, JSAGU]> {
  let Latency = 16;
  let ReleaseAtCycles = [16, 16];
  let NumMicroOps = 1;
}

def JWriteXADDrm_Part1 : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<CheckLockPrefix>, [JWriteLOCK_XADDrm_XCHG_Part]>,
  SchedVar<NoSchedPred,                       [JWriteXADDrm_XCHG_Part]>
]>;

def JWriteXADDrm_Part2 : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<CheckLockPrefix>, [JWriteXCHGrm_LdSt_Part]>,
  SchedVar<NoSchedPred,                       [JWriteXADDrm_LdSt_Part]>
]>;

def : InstRW<[JWriteXADDrm_Part1, JWriteXADDrm_Part2, ReadAfterLd],
                 (instrs XADD8rm, XADD16rm, XADD32rm, XADD64rm,
                         LXADD8, LXADD16, LXADD32, LXADD64)>;

def : InstRW<[JWriteXCHGrm_XCHG_Part, JWriteXCHGrm_LdSt_Part, ReadAfterLd],
                 (instrs XCHG8rm, XCHG16rm, XCHG32rm, XCHG64rm)>;


////////////////////////////////////////////////////////////////////////////////
// Floating point. This covers both scalar and vector operations.
////////////////////////////////////////////////////////////////////////////////

defm : X86WriteRes<WriteFLD0,          [JFPU1, JSTC], 3, [1,1], 1>;
defm : X86WriteRes<WriteFLD1,          [JFPU1, JSTC], 3, [1,1], 1>;
defm : X86WriteRes<WriteFLDC,          [JFPU1, JSTC], 3, [1,1], 1>;
defm : X86WriteRes<WriteFLoad,         [JLAGU, JFPU01, JFPX], 5, [1, 1, 1], 1>;
defm : X86WriteRes<WriteFLoadX,        [JLAGU], 5, [1], 1>;
defm : X86WriteRes<WriteFLoadY,        [JLAGU], 5, [2], 2>;
defm : X86WriteRes<WriteFMaskedLoad,   [JLAGU, JFPU01, JFPX], 6, [1, 2, 2], 1>;
defm : X86WriteRes<WriteFMaskedLoadY,  [JLAGU, JFPU01, JFPX], 6, [2, 4, 4], 2>;

defm : X86WriteRes<WriteFStore,        [JSAGU, JFPU1,  JSTC], 2, [1, 1, 1], 1>;
defm : X86WriteRes<WriteFStoreX,       [JSAGU, JFPU1,  JSTC], 1, [1, 1, 1], 1>;
defm : X86WriteRes<WriteFStoreY,       [JSAGU, JFPU1,  JSTC], 1, [2, 2, 2], 2>;
defm : X86WriteRes<WriteFStoreNT,      [JSAGU, JFPU1,  JSTC], 3, [1, 1, 1], 1>;
defm : X86WriteRes<WriteFStoreNTX,     [JSAGU, JFPU1,  JSTC], 3, [1, 1, 1], 1>;
defm : X86WriteRes<WriteFStoreNTY,     [JSAGU, JFPU1,  JSTC], 3, [2, 2, 2], 1>;

defm : X86WriteRes<WriteFMaskedStore32,  [JFPU0, JFPA, JFPU1, JSTC, JLAGU, JSAGU, JALU01], 16, [1,1, 5, 5,4,4,4], 19>;
defm : X86WriteRes<WriteFMaskedStore64,  [JFPU0, JFPA, JFPU1, JSTC, JLAGU, JSAGU, JALU01], 13, [1,1, 2, 2,2,2,2], 10>;
defm : X86WriteRes<WriteFMaskedStore32Y, [JFPU0, JFPA, JFPU1, JSTC, JLAGU, JSAGU, JALU01], 22, [1,1,10,10,8,8,8], 36>;
defm : X86WriteRes<WriteFMaskedStore64Y, [JFPU0, JFPA, JFPU1, JSTC, JLAGU, JSAGU, JALU01], 16, [1,1, 4, 4,4,4,4], 18>;

defm : X86WriteRes<WriteFMove,         [JFPU01, JFPX], 1, [1, 1], 1>;
defm : X86WriteRes<WriteFMoveX,        [JFPU01, JFPX], 1, [1, 1], 1>;
defm : X86WriteRes<WriteFMoveY,        [JFPU01, JFPX], 1, [2, 2], 2>;
defm : X86WriteResUnsupported<WriteFMoveZ>;

defm : X86WriteRes<WriteEMMS,          [JFPU01, JFPX], 2, [1, 1], 1>;

defm : JWriteResFpuPair<WriteFAdd,         [JFPU0, JFPA],  3>;
defm : JWriteResFpuPair<WriteFAddX,        [JFPU0, JFPA],  3>;
defm : JWriteResYMMPair<WriteFAddY,        [JFPU0, JFPA],  3, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteFAddZ>;
defm : JWriteResFpuPair<WriteFAdd64,       [JFPU0, JFPA],  3>;
defm : JWriteResFpuPair<WriteFAdd64X,      [JFPU0, JFPA],  3>;
defm : JWriteResYMMPair<WriteFAdd64Y,      [JFPU0, JFPA],  3, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteFAdd64Z>;
defm : JWriteResFpuPair<WriteFCmp,         [JFPU0, JFPA],  2>;
defm : JWriteResFpuPair<WriteFCmpX,        [JFPU0, JFPA],  2>;
defm : JWriteResYMMPair<WriteFCmpY,        [JFPU0, JFPA],  2, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteFCmpZ>;
defm : JWriteResFpuPair<WriteFCmp64,       [JFPU0, JFPA],  2>;
defm : JWriteResFpuPair<WriteFCmp64X,      [JFPU0, JFPA],  2>;
defm : JWriteResYMMPair<WriteFCmp64Y,      [JFPU0, JFPA],  2, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteFCmp64Z>;
defm : JWriteResFpuPair<WriteFCom,  [JFPU0, JFPA, JALU0],  3>;
defm : JWriteResFpuPair<WriteFComX, [JFPU0, JFPA, JALU0],  3>;
defm : JWriteResFpuPair<WriteFMul,         [JFPU1, JFPM],  2>;
defm : JWriteResFpuPair<WriteFMulX,        [JFPU1, JFPM],  2>;
defm : JWriteResYMMPair<WriteFMulY,        [JFPU1, JFPM],  2, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteFMulZ>;
defm : JWriteResFpuPair<WriteFMul64,       [JFPU1, JFPM],  4, [1,2]>;
defm : JWriteResFpuPair<WriteFMul64X,      [JFPU1, JFPM],  4, [1,2]>;
defm : JWriteResYMMPair<WriteFMul64Y,      [JFPU1, JFPM],  4, [2,4], 2>;
defm : X86WriteResPairUnsupported<WriteFMul64Z>;
defm : X86WriteResPairUnsupported<WriteFMA>;
defm : X86WriteResPairUnsupported<WriteFMAX>;
defm : X86WriteResPairUnsupported<WriteFMAY>;
defm : X86WriteResPairUnsupported<WriteFMAZ>;
defm : JWriteResFpuPair<WriteDPPD,   [JFPU1, JFPM, JFPA],  9, [1, 3, 3],  3>;
defm : JWriteResFpuPair<WriteDPPS,   [JFPU1, JFPM, JFPA], 11, [1, 3, 3],  5>;
defm : JWriteResYMMPair<WriteDPPSY,  [JFPU1, JFPM, JFPA], 12, [2, 6, 6], 10>;
defm : JWriteResFpuPair<WriteFRcp,         [JFPU1, JFPM],  2>;
defm : JWriteResFpuPair<WriteFRcpX,        [JFPU1, JFPM],  2>;
defm : JWriteResYMMPair<WriteFRcpY,        [JFPU1, JFPM],  2, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteFRcpZ>;
defm : JWriteResFpuPair<WriteFRsqrt,       [JFPU1, JFPM],  2>;
defm : JWriteResFpuPair<WriteFRsqrtX,      [JFPU1, JFPM],  2>;
defm : JWriteResYMMPair<WriteFRsqrtY,      [JFPU1, JFPM],  2, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteFRsqrtZ>;
defm : JWriteResFpuPair<WriteFDiv,         [JFPU1, JFPM], 19, [1, 19]>;
defm : JWriteResFpuPair<WriteFDivX,        [JFPU1, JFPM], 19, [1, 19]>;
defm : JWriteResYMMPair<WriteFDivY,        [JFPU1, JFPM], 38, [2, 38], 2>;
defm : X86WriteResPairUnsupported<WriteFDivZ>;
defm : JWriteResFpuPair<WriteFDiv64,       [JFPU1, JFPM], 19, [1, 19]>;
defm : JWriteResFpuPair<WriteFDiv64X,      [JFPU1, JFPM], 19, [1, 19]>;
defm : JWriteResYMMPair<WriteFDiv64Y,      [JFPU1, JFPM], 38, [2, 38], 2>;
defm : X86WriteResPairUnsupported<WriteFDiv64Z>;
defm : JWriteResFpuPair<WriteFSqrt,        [JFPU1, JFPM], 21, [1, 21]>;
defm : JWriteResFpuPair<WriteFSqrtX,       [JFPU1, JFPM], 21, [1, 21]>;
defm : JWriteResYMMPair<WriteFSqrtY,       [JFPU1, JFPM], 42, [2, 42], 2>;
defm : X86WriteResPairUnsupported<WriteFSqrtZ>;
defm : JWriteResFpuPair<WriteFSqrt64,      [JFPU1, JFPM], 27, [1, 27]>;
defm : JWriteResFpuPair<WriteFSqrt64X,     [JFPU1, JFPM], 27, [1, 27]>;
defm : JWriteResYMMPair<WriteFSqrt64Y,     [JFPU1, JFPM], 54, [2, 54], 2>;
defm : X86WriteResPairUnsupported<WriteFSqrt64Z>;
defm : JWriteResFpuPair<WriteFSqrt80,      [JFPU1, JFPM], 35, [1, 35]>;
defm : JWriteResFpuPair<WriteFSign,        [JFPU1, JFPM],  2>;
defm : JWriteResFpuPair<WriteFRnd,         [JFPU1, JSTC],  3>;
defm : JWriteResYMMPair<WriteFRndY,        [JFPU1, JSTC],  3, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteFRndZ>;
defm : JWriteResFpuPair<WriteFLogic,      [JFPU01, JFPX],  1>;
defm : JWriteResYMMPair<WriteFLogicY,     [JFPU01, JFPX],  1, [2, 2], 2>;
defm : X86WriteResPairUnsupported<WriteFLogicZ>;
defm : JWriteResFpuPair<WriteFTest,       [JFPU0, JFPA, JALU0], 3>;
defm : JWriteResYMMPair<WriteFTestY ,     [JFPU01, JFPX, JFPA, JALU0], 4, [2, 2, 2, 1], 3>;
defm : X86WriteResPairUnsupported<WriteFTestZ>;
defm : JWriteResFpuPair<WriteFShuffle,    [JFPU01, JFPX],  1>;
defm : JWriteResYMMPair<WriteFShuffleY,   [JFPU01, JFPX],  1, [2, 2], 2>;
defm : X86WriteResPairUnsupported<WriteFShuffleZ>;
defm : JWriteResFpuPair<WriteFVarShuffle, [JFPU01, JFPX],  3, [1, 4], 3>; // +1cy latency.
defm : JWriteResYMMPair<WriteFVarShuffleY,[JFPU01, JFPX],  4, [2, 6], 6>; // +1cy latency.
defm : X86WriteResPairUnsupported<WriteFVarShuffleZ>;
defm : JWriteResFpuPair<WriteFBlend,      [JFPU01, JFPX],  1>;
defm : JWriteResYMMPair<WriteFBlendY,     [JFPU01, JFPX],  1, [2, 2], 2>;
defm : X86WriteResPairUnsupported<WriteFBlendZ>;
defm : JWriteResFpuPair<WriteFVarBlend,   [JFPU01, JFPX],  2, [4, 4], 3>;
defm : JWriteResYMMPair<WriteFVarBlendY,  [JFPU01, JFPX],  3, [6, 6], 6>;
defm : X86WriteResPairUnsupported<WriteFVarBlendZ>;
defm : JWriteResFpuPair<WriteFShuffle256, [JFPU01, JFPX],  1, [2, 2], 2>;
defm : X86WriteResPairUnsupported<WriteFVarShuffle256>;

////////////////////////////////////////////////////////////////////////////////
// Conversions.
////////////////////////////////////////////////////////////////////////////////

defm : JWriteResFpuPair<WriteCvtSS2I,      [JFPU1, JSTC, JFPU0, JFPA, JALU0], 7, [1,1,1,1,1], 2>;
defm : JWriteResFpuPair<WriteCvtPS2I,      [JFPU1, JSTC], 3, [1,1], 1>;
defm : JWriteResYMMPair<WriteCvtPS2IY,     [JFPU1, JSTC], 3, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteCvtPS2IZ>;
defm : JWriteResFpuPair<WriteCvtSD2I,      [JFPU1, JSTC, JFPU0, JFPA, JALU0], 7, [1,1,1,1,1], 2>;
defm : JWriteResFpuPair<WriteCvtPD2I,      [JFPU1, JSTC], 3, [1,1], 1>;
defm : JWriteResYMMPair<WriteCvtPD2IY,     [JFPU1, JSTC, JFPX], 6, [2,2,4], 3>;
defm : X86WriteResPairUnsupported<WriteCvtPD2IZ>;

defm : X86WriteRes<WriteCvtI2SS,           [JFPU1, JSTC], 4, [1,1], 2>;
defm : X86WriteRes<WriteCvtI2SSLd,         [JLAGU, JFPU1, JSTC], 9, [1,1,1], 1>;
defm : JWriteResFpuPair<WriteCvtI2PS,      [JFPU1, JSTC], 3, [1,1], 1>;
defm : JWriteResYMMPair<WriteCvtI2PSY,     [JFPU1, JSTC], 3, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteCvtI2PSZ>;
defm : X86WriteRes<WriteCvtI2SD,           [JFPU1, JSTC], 4, [1,1], 2>;
defm : X86WriteRes<WriteCvtI2SDLd,         [JLAGU, JFPU1, JSTC], 9, [1,1,1], 1>;
defm : JWriteResFpuPair<WriteCvtI2PD,      [JFPU1, JSTC], 3, [1,1], 1>;
defm : JWriteResYMMPair<WriteCvtI2PDY,     [JFPU1, JSTC], 3, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteCvtI2PDZ>;

defm : JWriteResFpuPair<WriteCvtSS2SD,      [JFPU1, JSTC], 7, [1,2], 2>;
defm : JWriteResFpuPair<WriteCvtPS2PD,      [JFPU1, JSTC], 2, [1,1], 1>;
defm : JWriteResYMMPair<WriteCvtPS2PDY,     [JFPU1, JSTC], 2, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteCvtPS2PDZ>;

defm : JWriteResFpuPair<WriteCvtSD2SS,    [JFPU1, JSTC], 7, [1,2], 2>;
defm : JWriteResFpuPair<WriteCvtPD2PS,    [JFPU1, JSTC], 3, [1,1], 1>;
defm : JWriteResYMMPair<WriteCvtPD2PSY,   [JFPU1, JSTC, JFPX], 6, [2,2,4], 3>;
defm : X86WriteResPairUnsupported<WriteCvtPD2PSZ>;

defm : JWriteResFpuPair<WriteCvtPH2PS,     [JFPU1, JSTC], 3, [1,1], 1>;
defm : JWriteResYMMPair<WriteCvtPH2PSY,    [JFPU1, JSTC], 3, [2,2], 2>;
defm : X86WriteResPairUnsupported<WriteCvtPH2PSZ>;

defm : X86WriteRes<WriteCvtPS2PH,                 [JFPU1, JSTC], 3, [1,1], 1>;
defm : X86WriteRes<WriteCvtPS2PHY,          [JFPU1, JSTC, JFPX], 6, [2,2,2], 3>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZ>;
defm : X86WriteRes<WriteCvtPS2PHSt,        [JFPU1, JSTC, JSAGU], 4, [1,1,1], 1>;
defm : X86WriteRes<WriteCvtPS2PHYSt, [JFPU1, JSTC, JFPX, JSAGU], 7, [2,2,2,1], 3>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZSt>;

////////////////////////////////////////////////////////////////////////////////
// Vector integer operations.
////////////////////////////////////////////////////////////////////////////////

defm : X86WriteRes<WriteVecLoad,          [JLAGU, JFPU01, JVALU], 5, [1, 1, 1], 1>;
defm : X86WriteRes<WriteVecLoadX,         [JLAGU], 5, [1], 1>;
defm : X86WriteRes<WriteVecLoadY,         [JLAGU], 5, [2], 2>;
defm : X86WriteRes<WriteVecLoadNT,        [JLAGU, JFPU01, JVALU], 5, [1, 1, 1], 1>;
defm : X86WriteRes<WriteVecLoadNTY,       [JLAGU, JFPU01, JVALU], 5, [1, 1, 1], 1>;
defm : X86WriteRes<WriteVecMaskedLoad,    [JLAGU, JFPU01, JVALU], 6, [1, 2, 2], 1>;
defm : X86WriteRes<WriteVecMaskedLoadY,   [JLAGU, JFPU01, JVALU], 6, [2, 4, 4], 2>;

defm : X86WriteRes<WriteVecStore,         [JSAGU, JFPU1,   JSTC], 2, [1, 1, 1], 1>;
defm : X86WriteRes<WriteVecStoreX,        [JSAGU, JFPU1,   JSTC], 1, [1, 1, 1], 1>;
defm : X86WriteRes<WriteVecStoreY,        [JSAGU, JFPU1,   JSTC], 1, [2, 2, 2], 2>;
defm : X86WriteRes<WriteVecStoreNT,       [JSAGU, JFPU1,   JSTC], 2, [1, 1, 1], 1>;
defm : X86WriteRes<WriteVecStoreNTY,      [JSAGU, JFPU1,   JSTC], 2, [2, 2, 2], 1>;
defm : X86WriteResUnsupported<WriteVecMaskedStore32>;
defm : X86WriteResUnsupported<WriteVecMaskedStore64>;
defm : X86WriteResUnsupported<WriteVecMaskedStore32Y>;
defm : X86WriteResUnsupported<WriteVecMaskedStore64Y>;

defm : X86WriteRes<WriteVecMove,          [JFPU01, JVALU], 1, [1, 1], 1>;
defm : X86WriteRes<WriteVecMoveX,         [JFPU01, JVALU], 1, [1, 1], 1>;
defm : X86WriteRes<WriteVecMoveY,         [JFPU01, JVALU], 1, [2, 2], 2>;
defm : X86WriteResUnsupported<WriteVecMoveZ>;
defm : X86WriteRes<WriteVecMoveToGpr,     [JFPU0, JFPA, JALU0], 4, [1, 1, 1], 1>;
defm : X86WriteRes<WriteVecMoveFromGpr,   [JFPU01, JFPX], 8, [1, 1], 2>;

defm : JWriteResFpuPair<WriteVecALU,      [JFPU01, JVALU], 1>;
defm : JWriteResFpuPair<WriteVecALUX,     [JFPU01, JVALU], 1>;
defm : X86WriteResPairUnsupported<WriteVecALUY>;
defm : X86WriteResPairUnsupported<WriteVecALUZ>;
defm : JWriteResFpuPair<WriteVecShift,    [JFPU01, JVALU], 1>;
defm : JWriteResFpuPair<WriteVecShiftX,   [JFPU01, JVALU], 2>; // +1cy latency.
defm : X86WriteResPairUnsupported<WriteVecShiftY>;
defm : X86WriteResPairUnsupported<WriteVecShiftZ>;
defm : JWriteResFpuPair<WriteVecShiftImm, [JFPU01, JVALU], 1>;
defm : JWriteResFpuPair<WriteVecShiftImmX,[JFPU01, JVALU], 2>; // +1cy latency.
defm : X86WriteResPairUnsupported<WriteVecShiftImmY>;
defm : X86WriteResPairUnsupported<WriteVecShiftImmZ>;
defm : X86WriteResPairUnsupported<WriteVarVecShift>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftY>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftZ>;
defm : JWriteResFpuPair<WriteVecIMul,     [JFPU0, JVIMUL], 2>;
defm : JWriteResFpuPair<WriteVecIMulX,    [JFPU0, JVIMUL], 2>;
defm : X86WriteResPairUnsupported<WriteVecIMulY>;
defm : X86WriteResPairUnsupported<WriteVecIMulZ>;
defm : JWriteResFpuPair<WritePMULLD,      [JFPU0, JFPU01, JVIMUL, JVALU], 4, [2, 1, 2, 1], 3>;
defm : X86WriteResPairUnsupported<WritePMULLDY>;
defm : X86WriteResPairUnsupported<WritePMULLDZ>;
defm : JWriteResFpuPair<WriteMPSAD,       [JFPU0, JVIMUL], 3, [1, 2], 3>;
defm : X86WriteResPairUnsupported<WriteMPSADY>;
defm : X86WriteResPairUnsupported<WriteMPSADZ>;
defm : JWriteResFpuPair<WritePSADBW,      [JFPU01, JVALU], 2>;
defm : JWriteResFpuPair<WritePSADBWX,     [JFPU01, JVALU], 2>;
defm : X86WriteResPairUnsupported<WritePSADBWY>;
defm : X86WriteResPairUnsupported<WritePSADBWZ>;
defm : JWriteResFpuPair<WritePHMINPOS,    [JFPU01, JVALU], 2>;
defm : JWriteResFpuPair<WriteShuffle,     [JFPU01, JVALU], 1>;
defm : JWriteResFpuPair<WriteShuffleX,    [JFPU01, JVALU], 1>;
defm : X86WriteResPairUnsupported<WriteShuffleY>;
defm : X86WriteResPairUnsupported<WriteShuffleZ>;
defm : JWriteResFpuPair<WriteVarShuffle,  [JFPU01, JVALU], 2, [1, 1], 1>;
defm : JWriteResFpuPair<WriteVarShuffleX, [JFPU01, JVALU], 2, [1, 4], 3>;
defm : X86WriteResPairUnsupported<WriteVarShuffleY>;
defm : X86WriteResPairUnsupported<WriteVarShuffleZ>;
defm : JWriteResFpuPair<WriteBlend,       [JFPU01, JVALU], 1>;
defm : X86WriteResPairUnsupported<WriteBlendY>;
defm : X86WriteResPairUnsupported<WriteBlendZ>;
defm : JWriteResFpuPair<WriteVarBlend,    [JFPU01, JVALU], 2, [4, 4], 3>;
defm : X86WriteResPairUnsupported<WriteVarBlendY>;
defm : X86WriteResPairUnsupported<WriteVarBlendZ>;
defm : JWriteResFpuPair<WriteVecLogic,    [JFPU01, JVALU], 1>;
defm : JWriteResFpuPair<WriteVecLogicX,   [JFPU01, JVALU], 1>;
defm : X86WriteResPairUnsupported<WriteVecLogicY>;
defm : X86WriteResPairUnsupported<WriteVecLogicZ>;
defm : JWriteResFpuPair<WriteVecTest,     [JFPU0, JFPA, JALU0], 3>;
defm : JWriteResYMMPair<WriteVecTestY,    [JFPU01, JFPX, JFPA, JALU0], 4, [2, 2, 2, 1], 3>;
defm : X86WriteResPairUnsupported<WriteVecTestZ>;
defm : X86WriteResPairUnsupported<WriteShuffle256>;
defm : X86WriteResPairUnsupported<WriteVPMOV256>;
defm : X86WriteResPairUnsupported<WriteVarShuffle256>;

////////////////////////////////////////////////////////////////////////////////
// Vector insert/extract operations.
////////////////////////////////////////////////////////////////////////////////

defm : X86WriteRes<WriteVecInsert,      [JFPU01, JVALU], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecInsertLd,    [JFPU01, JVALU, JLAGU], 4, [1,1,1], 1>;
defm : X86WriteRes<WriteVecExtract,     [JFPU0, JFPA, JALU0], 3, [1,1,1], 1>;
defm : X86WriteRes<WriteVecExtractSt,   [JFPU1, JSTC, JSAGU], 3, [1,1,1], 1>;

////////////////////////////////////////////////////////////////////////////////
// SSE42 String instructions.
////////////////////////////////////////////////////////////////////////////////

defm : JWriteResFpuPair<WritePCmpIStrI, [JFPU1, JVALU1, JFPU0, JFPA, JALU0], 7, [2, 2, 1, 1, 1], 3>;
defm : JWriteResFpuPair<WritePCmpIStrM, [JFPU1, JVALU1, JFPU0, JFPA, JALU0], 8, [2, 2, 1, 1, 1], 3>;
defm : JWriteResFpuPair<WritePCmpEStrI, [JFPU1, JSAGU, JLAGU, JVALU, JVALU1, JFPA, JALU0], 14, [1, 2, 2, 6, 4, 1, 1], 9>;
defm : JWriteResFpuPair<WritePCmpEStrM, [JFPU1, JSAGU, JLAGU, JVALU, JVALU1, JFPA, JALU0], 14, [1, 2, 2, 6, 4, 1, 1], 9>;

////////////////////////////////////////////////////////////////////////////////
// MOVMSK Instructions.
////////////////////////////////////////////////////////////////////////////////

def  : WriteRes<WriteFMOVMSK,    [JFPU0, JFPA, JALU0]> { let Latency = 3; }
def  : WriteRes<WriteVecMOVMSK,  [JFPU0, JFPA, JALU0]> { let Latency = 3; }
defm : X86WriteResUnsupported<WriteVecMOVMSKY>;
def  : WriteRes<WriteMMXMOVMSK,  [JFPU0, JFPA, JALU0]> { let Latency = 3; }

////////////////////////////////////////////////////////////////////////////////
// AES Instructions.
////////////////////////////////////////////////////////////////////////////////

defm : JWriteResFpuPair<WriteAESIMC,      [JFPU0, JVIMUL], 2>;
defm : JWriteResFpuPair<WriteAESKeyGen,   [JFPU0, JVIMUL], 2>;
defm : JWriteResFpuPair<WriteAESDecEnc,   [JFPU01, JVALU, JFPU0, JVIMUL], 3, [1,1,1,1], 2>;

////////////////////////////////////////////////////////////////////////////////
// Horizontal add/sub  instructions.
////////////////////////////////////////////////////////////////////////////////

defm : JWriteResFpuPair<WriteFHAdd,         [JFPU0, JFPA], 4>;            // +1cy latency.
defm : JWriteResYMMPair<WriteFHAddY,        [JFPU0, JFPA], 4, [2,2], 2>;  // +1cy latency.
defm : JWriteResFpuPair<WritePHAdd,         [JFPU01, JVALU], 1>;
defm : JWriteResFpuPair<WritePHAddX,        [JFPU01, JVALU], 2>;          // +1cy latency.
defm : X86WriteResPairUnsupported<WritePHAddY>;

////////////////////////////////////////////////////////////////////////////////
// Carry-less multiplication instructions.
////////////////////////////////////////////////////////////////////////////////

defm : JWriteResFpuPair<WriteCLMul,       [JFPU0, JVIMUL], 2>;

////////////////////////////////////////////////////////////////////////////////
// SSE4A instructions.
////////////////////////////////////////////////////////////////////////////////

def JWriteINSERTQ: SchedWriteRes<[JFPU01, JVALU]> {
  let Latency = 2;
  let ReleaseAtCycles = [1, 4];
}
def : InstRW<[JWriteINSERTQ], (instrs INSERTQ, INSERTQI)>;

////////////////////////////////////////////////////////////////////////////////
// AVX instructions.
////////////////////////////////////////////////////////////////////////////////

def JWriteVecExtractF128: SchedWriteRes<[JFPU01, JFPX]>;
def : InstRW<[JWriteVecExtractF128], (instrs VEXTRACTF128rr)>;

def JWriteVBROADCASTYLd: SchedWriteRes<[JLAGU, JFPU01, JFPX]> {
  let Latency = 6;
  let ReleaseAtCycles = [1, 2, 4];
  let NumMicroOps = 2;
}
def : InstRW<[JWriteVBROADCASTYLd], (instrs VBROADCASTSDYrm,
                                            VBROADCASTSSYrm,
                                            VBROADCASTF128rm)>;

def JWriteJVZEROALL: SchedWriteRes<[]> {
  let Latency = 90;
  let NumMicroOps = 73;
}
def : InstRW<[JWriteJVZEROALL], (instrs VZEROALL)>;

def JWriteJVZEROUPPER: SchedWriteRes<[]> {
  let Latency = 46;
  let NumMicroOps = 37;
}
def : InstRW<[JWriteJVZEROUPPER], (instrs VZEROUPPER)>;

///////////////////////////////////////////////////////////////////////////////
//  SSE2/AVX Store Selected Bytes of Double Quadword - (V)MASKMOVDQ
///////////////////////////////////////////////////////////////////////////////

def JWriteMASKMOVDQU: SchedWriteRes<[JFPU0, JFPA, JFPU1, JSTC, JLAGU, JSAGU, JALU01]> {
  let Latency = 34;
  let ReleaseAtCycles = [1, 1, 2, 2, 2, 16, 42];
  let NumMicroOps = 63;
}
def : InstRW<[JWriteMASKMOVDQU], (instrs MASKMOVDQU, MASKMOVDQU64,
                                         VMASKMOVDQU, VMASKMOVDQU64)>;

///////////////////////////////////////////////////////////////////////////////
//  SchedWriteVariant definitions.
///////////////////////////////////////////////////////////////////////////////

def JWriteZeroLatency : SchedWriteRes<[]> {
  let Latency = 0;
}

def JWriteZeroIdiomYmm : SchedWriteRes<[JFPU01, JFPX]> {
  let NumMicroOps = 2;
}

// Certain instructions that use the same register for both source
// operands do not have a real dependency on the previous contents of the
// register, and thus, do not have to wait before completing. They can be
// optimized out at register renaming stage.
// Reference: Section 10.8 of the "Software Optimization Guide for AMD Family
// 15h Processors".
// Reference: Agner's Fog "The microarchitecture of Intel, AMD and VIA CPUs",
// Section 21.8 [Dependency-breaking instructions].

def JWriteZeroIdiom : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [JWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteALU]>
]>;
def : InstRW<[JWriteZeroIdiom], (instrs SUB32rr, SUB64rr,
                                        XOR32rr, XOR64rr)>;

def JWriteFZeroIdiom : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [JWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteFLogic]>
]>;
def : InstRW<[JWriteFZeroIdiom], (instrs XORPSrr, VXORPSrr, XORPDrr, VXORPDrr,
                                         ANDNPSrr, VANDNPSrr,
                                         ANDNPDrr, VANDNPDrr)>;

def JWriteFZeroIdiomY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [JWriteZeroIdiomYmm]>,
    SchedVar<NoSchedPred,                          [WriteFLogicY]>
]>;
def : InstRW<[JWriteFZeroIdiomY], (instrs VXORPSYrr, VXORPDYrr,
                                          VANDNPSYrr, VANDNPDYrr)>;

def JWriteVZeroIdiomLogic : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [JWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecLogic]>
]>;
def : InstRW<[JWriteVZeroIdiomLogic], (instrs MMX_PXORrr, MMX_PANDNrr)>;

def JWriteVZeroIdiomLogicX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [JWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecLogicX]>
]>;
def : InstRW<[JWriteVZeroIdiomLogicX], (instrs PXORrr, VPXORrr,
                                               PANDNrr, VPANDNrr)>;

def JWriteVZeroIdiomALU : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [JWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecALU]>
]>;
def : InstRW<[JWriteVZeroIdiomALU], (instrs MMX_PSUBBrr, MMX_PSUBDrr,
                                            MMX_PSUBQrr, MMX_PSUBWrr,
                                            MMX_PSUBSBrr, MMX_PSUBSWrr,
                                            MMX_PSUBUSBrr, MMX_PSUBUSWrr,
                                            MMX_PCMPGTBrr, MMX_PCMPGTDrr,
                                            MMX_PCMPGTWrr)>;

def JWriteVZeroIdiomALUX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [JWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecALUX]>
]>;
def : InstRW<[JWriteVZeroIdiomALUX], (instrs PSUBBrr, VPSUBBrr,
                                             PSUBDrr, VPSUBDrr,
                                             PSUBQrr, VPSUBQrr,
                                             PSUBWrr, VPSUBWrr,
                                             PSUBSBrr, VPSUBSBrr,
                                             PSUBSWrr, VPSUBSWrr,
                                             PSUBUSBrr, VPSUBUSBrr,
                                             PSUBUSWrr, VPSUBUSWrr,
                                             PCMPGTBrr, VPCMPGTBrr,
                                             PCMPGTDrr, VPCMPGTDrr,
                                             PCMPGTQrr, VPCMPGTQrr,
                                             PCMPGTWrr, VPCMPGTWrr)>;

def JWriteVPERM2F128 : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<ZeroIdiomVPERMPredicate>, [JWriteZeroIdiomYmm]>,
  SchedVar<NoSchedPred,                               [WriteFShuffle256]>
]>;
def : InstRW<[JWriteVPERM2F128], (instrs VPERM2F128rr)>;

// This write is used for slow LEA instructions.
def JWrite3OpsLEA : SchedWriteRes<[JALU1, JSAGU]> {
  let Latency = 2;
}

// On Jaguar, a slow LEA is either a 3Ops LEA (base, index, offset), or an LEA
// with a `Scale` value different than 1.
def JSlowLEAPredicate : MCSchedPredicate<
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

def JWriteLEA : SchedWriteVariant<[
    SchedVar<JSlowLEAPredicate, [JWrite3OpsLEA]>,
    SchedVar<NoSchedPred,       [WriteLEA]>
]>;

def : InstRW<[JWriteLEA], (instrs LEA32r, LEA64r, LEA64_32r)>;

def JSlowLEA16r : SchedWriteRes<[JALU01]> {
  let Latency = 3;
  let ReleaseAtCycles = [4];
}

def : InstRW<[JSlowLEA16r], (instrs LEA16r)>;

///////////////////////////////////////////////////////////////////////////////
// Dependency breaking instructions.
///////////////////////////////////////////////////////////////////////////////

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
    PCMPGTBrr, PCMPGTDrr, PCMPGTQrr, PCMPGTWrr
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
  ], ZeroIdiomPredicate>,

  DepBreakingClass<[ VPERM2F128rr ], ZeroIdiomVPERMPredicate>
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
    PCMPEQBrr, PCMPEQWrr, PCMPEQDrr, PCMPEQQrr
  ], ZeroIdiomPredicate>,

  // AVX
  DepBreakingClass<[
    VPCMPEQBrr, VPCMPEQWrr, VPCMPEQDrr, VPCMPEQQrr
  ], ZeroIdiomPredicate>
]>;

def : IsOptimizableRegisterMove<[
  InstructionEquivalenceClass<[
    // GPR variants.
    MOV32rr, MOV64rr,

    // MMX variants.
    MMX_MOVQ64rr,

    // SSE variants.
    MOVAPSrr, MOVUPSrr,
    MOVAPDrr, MOVUPDrr,
    MOVDQArr, MOVDQUrr,

    // AVX variants.
    VMOVAPSrr, VMOVUPSrr,
    VMOVAPDrr, VMOVUPDrr,
    VMOVDQArr, VMOVDQUrr
  ], TruePred >
]>;

} // SchedModel

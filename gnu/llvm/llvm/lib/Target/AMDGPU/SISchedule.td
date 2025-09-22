//===-- SISchedule.td - SI Scheduling definitions -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MachineModel definitions for Southern Islands (SI)
//
//===----------------------------------------------------------------------===//

def : PredicateProlog<[{
  const SIInstrInfo *TII =
    static_cast<const SIInstrInfo*>(SchedModel->getInstrInfo());
  (void)TII;
}]>;

def WriteBranch : SchedWrite;
def WriteExport : SchedWrite;
def WriteLDS    : SchedWrite;
def WriteSALU   : SchedWrite;
def WriteSMEM   : SchedWrite;
def WriteVMEM   : SchedWrite;
def WriteBarrier : SchedWrite;

def MIVGPRRead  : SchedRead;
def MIMFMARead  : SchedRead;

// Normal 16 or 32 bit VALU instructions
def Write32Bit         : SchedWrite;
// Conversion to or from F32 (but not converting F64 to or from F32)
def WriteFloatCvt      : SchedWrite;
// F16 or F32 transcendental instructions (these are quarter rate)
def WriteTrans32       : SchedWrite;
// Other quarter rate VALU instructions
def WriteQuarterRate32 : SchedWrite;

def WriteFloatFMA   : SchedWrite;

// Slow quarter rate f64 instruction.
def WriteDouble : SchedWrite;

// half rate f64 instruction (same as v_add_f64)
def WriteDoubleAdd  : SchedWrite;

// Conversion to or from f64 instruction
def WriteDoubleCvt  : SchedWrite;

// F64 "transcendental" (actually only reciprocal and/or square root)
// instructions
def WriteTrans64    : SchedWrite;

// Half rate 64-bit instructions.
def Write64Bit : SchedWrite;

// Integer multiplications.
def WriteIntMul : SchedWrite;

// mAI multipass instructions.
def Write2PassMAI  : SchedWrite;
def Write4PassMAI  : SchedWrite;
def Write8PassMAI  : SchedWrite;
def Write16PassMAI : SchedWrite;
def Write4PassDGEMM : SchedWrite;
def Write8PassDGEMM : SchedWrite;

// Scalar float instructions
def WriteSFPU : SchedWrite;

// F16 or F32 pseudo scalar transcendental instructions
def WritePseudoScalarTrans : SchedWrite;

// FIXME: Should there be a class for instructions which are VALU
// instructions and have VALU rates, but write to the SALU (i.e. VOPC
// instructions)

class SISchedMachineModel : SchedMachineModel {
  let CompleteModel = 1;
  // MicroOpBufferSize = 1 means that instructions will always be added
  // the ready queue when they become available.  This exposes them
  // to the register pressure analysis.
  let MicroOpBufferSize = 1;
  let IssueWidth = 1;
  let PostRAScheduler = 1;

  // FIXME:Approximate 2 * branch cost.  Try to hack around bad
  // early-ifcvt heuristics. These need improvement to avoid the OOE
  // heuristics.
  int MispredictPenalty = 20;
}

def SIFullSpeedModel : SISchedMachineModel;
def SIQuarterSpeedModel : SISchedMachineModel;
def SIDPFullSpeedModel : SISchedMachineModel;
def SIDPGFX940FullSpeedModel : SISchedMachineModel;
def GFX10SpeedModel : SISchedMachineModel;
def GFX11SpeedModel : SISchedMachineModel;
def GFX12SpeedModel : SISchedMachineModel;

// XXX: Are the resource counts correct?
def HWBranch : ProcResource<1> {
  let BufferSize = 1;
}
def HWExport : ProcResource<1> {
  let BufferSize = 1;
}
def HWLGKM   : ProcResource<1> {
  let BufferSize = 1;
}
def HWSALU   : ProcResource<1> {
  let BufferSize = 1;
}
def HWVMEM   : ProcResource<1> {
  let BufferSize = 1;
}
def HWVALU   : ProcResource<1> {
  let BufferSize = 1;
}
def HWTransVALU : ProcResource<1> { // Transcendental VALU
  let BufferSize = 1;
}
def HWRC   : ProcResource<1> { // Register destination cache
  let BufferSize = 1;
}
def HWXDL   : ProcResource<1> { // MFMA CU
  let BufferSize = 0;
}

class HWWriteRes<SchedWrite write, list<ProcResourceKind> resources,
                 int latency> : WriteRes<write, resources> {
  let Latency = latency;
}

class HWVALUWriteRes<SchedWrite write, int latency> :
  HWWriteRes<write, [HWVALU], latency>;

class UnsupportedWriteRes<SchedWrite write> : WriteRes<write, []> {
  let Unsupported = 1;
}

def PredMIReadVGPR : SchedPredicate<[{TII->hasVGPRUses(*MI)}]>;

def MIReadVGPR : SchedReadVariant<[
      SchedVar<PredMIReadVGPR, [MIVGPRRead]>,
      SchedVar<NoSchedPred, [ReadDefault]>]>;

// The latency numbers are taken from AMD Accelerated Parallel Processing
// guide. They may not be accurate.

// The latency values are 1 / (operations / cycle) / 4.
multiclass SICommonWriteRes {

  let RetireOOO = 1 in { // llvm-mca specific flag
  def : HWWriteRes<WriteBranch,  [HWBranch], 8>;
  def : HWWriteRes<WriteExport,  [HWExport], 4>;
  def : HWWriteRes<WriteLDS,     [HWLGKM],   5>; // Can be between 2 and 64
  def : HWWriteRes<WriteSALU,    [HWSALU],   1>;
  def : HWWriteRes<WriteSMEM,    [HWLGKM],   5>;
  def : HWWriteRes<WriteVMEM,    [HWVMEM],   80>;
  def : HWWriteRes<WriteBarrier, [HWBranch], 500>; // XXX: Guessed ???

  def : HWVALUWriteRes<Write32Bit,         1>;
  def : HWVALUWriteRes<WriteFloatCvt,      4>;
  def : HWVALUWriteRes<WriteTrans32,       4>;
  def : HWVALUWriteRes<WriteQuarterRate32, 4>;

  let ReleaseAtCycles = [4] in
  def : HWVALUWriteRes<Write4PassDGEMM,    4>;
  let ReleaseAtCycles = [8] in
  def : HWVALUWriteRes<Write8PassDGEMM,    8>;

  let ReleaseAtCycles = [2] in
  def : HWWriteRes<Write2PassMAI,  [HWXDL], 2>;
  let ReleaseAtCycles = [4] in
  def : HWWriteRes<Write4PassMAI,  [HWXDL], 4>;
  let ReleaseAtCycles = [8] in
  def : HWWriteRes<Write8PassMAI,  [HWXDL], 8>;
  let ReleaseAtCycles = [16] in
  def : HWWriteRes<Write16PassMAI, [HWXDL], 16>;

  def : UnsupportedWriteRes<WriteSFPU>;
  def : UnsupportedWriteRes<WritePseudoScalarTrans>;
  } // End RetireOOO = 1

  def : ReadAdvance<MIVGPRRead, -2>;

  // Technically mfma reads can be from 0 to 4 cycles but that does not make
  // sense to model because its register setup is huge. In particular if we
  // properly model read advance as -2 for a vgpr read it will result in a
  // bad scheduling of acc writes before that mfma. To avoid it we would
  // need to consume 2 or 4 more vgprs to be initialized before the acc
  // write sequence. Just assume worst case here.
  def : ReadAdvance<MIMFMARead, -4>;
}

def PredIsVGPR32Copy : SchedPredicate<[{TII->isVGPRCopy(*MI) && TII->getOpSize(*MI, 0) <= 32}]>;
def PredIsVGPR64Copy : SchedPredicate<[{TII->isVGPRCopy(*MI) && TII->getOpSize(*MI, 0) > 32}]>;
def WriteCopy : SchedWriteVariant<[
    SchedVar<PredIsVGPR32Copy, [Write32Bit]>,
    SchedVar<PredIsVGPR64Copy, [Write64Bit]>,
    SchedVar<NoSchedPred, [WriteSALU]>]>;

let SchedModel = SIFullSpeedModel in {

defm : SICommonWriteRes;

let RetireOOO = 1 in { // llvm-mca specific flag
def : HWVALUWriteRes<Write64Bit,       2>;
def : HWVALUWriteRes<WriteIntMul,      4>;
def : HWVALUWriteRes<WriteFloatFMA,    1>;
def : HWVALUWriteRes<WriteDouble,      4>;
def : HWVALUWriteRes<WriteDoubleAdd,   2>;
def : HWVALUWriteRes<WriteDoubleCvt,   4>;
def : HWVALUWriteRes<WriteTrans64,     4>;
} // End RetireOOO = 1

def : InstRW<[WriteCopy], (instrs COPY)>;

} // End SchedModel = SIFullSpeedModel

let SchedModel = SIQuarterSpeedModel in {

defm : SICommonWriteRes;

let RetireOOO = 1 in { // llvm-mca specific flag
def : HWVALUWriteRes<Write64Bit,       2>;
def : HWVALUWriteRes<WriteIntMul,      4>;
def : HWVALUWriteRes<WriteFloatFMA,    16>;
def : HWVALUWriteRes<WriteDouble,      16>;
def : HWVALUWriteRes<WriteDoubleAdd,    8>;
def : HWVALUWriteRes<WriteDoubleCvt,    4>;
def : HWVALUWriteRes<WriteTrans64,     16>;
} // End RetireOOO = 1

def : InstRW<[WriteCopy], (instrs COPY)>;
def : InstRW<[Write64Bit, MIReadVGPR], (instregex "^V_ACCVGPR_WRITE_B32_e64$")>;
def : InstRW<[Write2PassMAI,  MIMFMARead], (instregex "^V_MFMA_..._4X4X")>;
def : InstRW<[Write8PassMAI,  MIMFMARead], (instregex "^V_MFMA_..._16X16X")>;
def : InstRW<[Write16PassMAI, MIMFMARead], (instregex "^V_MFMA_..._32X32X")>;

}  // End SchedModel = SIQuarterSpeedModel

let SchedModel = SIDPFullSpeedModel in {

defm : SICommonWriteRes;

let RetireOOO = 1 in { // llvm-mca specific flag
def : HWVALUWriteRes<WriteFloatFMA,    1>;
def : HWVALUWriteRes<WriteDouble,      1>;
def : HWVALUWriteRes<WriteDoubleAdd,   1>;
def : HWVALUWriteRes<WriteDoubleCvt,   1>;
def : HWVALUWriteRes<WriteTrans64,     4>;
def : HWVALUWriteRes<WriteIntMul,      1>;
def : HWVALUWriteRes<Write64Bit,       1>;
} // End RetireOOO = 1

def : InstRW<[WriteCopy], (instrs COPY)>;
def : InstRW<[Write64Bit], (instregex "^V_ACCVGPR_WRITE_B32_e64$")>;
def : InstRW<[Write2PassMAI,   MIMFMARead], (instregex "^V_MFMA_.32_4X4X")>;
def : InstRW<[Write8PassMAI,   MIMFMARead], (instregex "^V_MFMA_.32_16X16X")>;
def : InstRW<[Write16PassMAI,  MIMFMARead], (instregex "^V_MFMA_.32_32X32X")>;
def : InstRW<[Write4PassDGEMM, MIMFMARead], (instregex "^V_MFMA_.64_4X4X")>;
def : InstRW<[Write8PassDGEMM, MIMFMARead], (instregex "^V_MFMA_.64_16X16X")>;

} // End SchedModel = SIDPFullSpeedModel

let SchedModel = SIDPGFX940FullSpeedModel in {

defm : SICommonWriteRes;

def : HWVALUWriteRes<WriteFloatFMA,    1>;
def : HWVALUWriteRes<WriteDouble,      1>;
def : HWVALUWriteRes<WriteDoubleAdd,   1>;
def : HWVALUWriteRes<WriteDoubleCvt,   1>;
def : HWVALUWriteRes<WriteTrans64,     4>;
def : HWVALUWriteRes<WriteIntMul,      1>;
def : HWVALUWriteRes<Write64Bit,       1>;

def : InstRW<[WriteCopy], (instrs COPY)>;
def : InstRW<[Write64Bit], (instregex "^V_ACCVGPR_WRITE_B32_e64$")>;
def : InstRW<[Write2PassMAI,   MIMFMARead], (instregex "^V_MFMA_.32_4X4X")>;

def : InstRW<[Write4PassMAI,   MIMFMARead], (instregex "^V_MFMA_.32_16X16X8X")>;
def : InstRW<[Write4PassMAI,   MIMFMARead], (instregex "^V_MFMA_.32_16X16X16")>;
def : InstRW<[Write4PassMAI,   MIMFMARead], (instregex "^V_MFMA_.32_16X16X32")>;
def : InstRW<[Write8PassMAI,   MIMFMARead], (instregex "^V_MFMA_.32_16X16X[14][FBI]")>;

def : InstRW<[Write8PassMAI,   MIMFMARead], (instregex "^V_MFMA_.32_32X32X4XF")>;
def : InstRW<[Write8PassMAI,   MIMFMARead], (instregex "^V_MFMA_.32_32X32X8")>;
def : InstRW<[Write8PassMAI,   MIMFMARead], (instregex "^V_MFMA_.32_32X32X16")>;
def : InstRW<[Write16PassMAI,  MIMFMARead], (instregex "^V_MFMA_.32_32X32X[124][FBI]")>;

def : InstRW<[Write4PassDGEMM, MIMFMARead], (instregex "^V_MFMA_.64_4X4X")>;
def : InstRW<[Write8PassDGEMM, MIMFMARead], (instregex "^V_MFMA_.64_16X16X")>;

def : InstRW<[Write4PassMAI,   MIMFMARead], (instregex "^V_SMFMAC_.32_16X16X")>;
def : InstRW<[Write8PassMAI,   MIMFMARead], (instregex "^V_SMFMAC_.32_32X32X")>;

} // End SchedModel = SIDPGFX940FullSpeedModel

let SchedModel = GFX10SpeedModel in {

// The latency values are 1 / (operations / cycle).
// Add 1 stall cycle for VGPR read.
let RetireOOO = 1 in { // llvm-mca specific flag
def : HWWriteRes<Write32Bit,         [HWVALU, HWRC],   5>;
def : HWWriteRes<WriteFloatCvt,      [HWVALU, HWRC],   5>;
def : HWWriteRes<Write64Bit,         [HWVALU, HWRC],   6>;
def : HWWriteRes<WriteTrans32,       [HWTransVALU, HWRC], 10>;
def : HWWriteRes<WriteQuarterRate32, [HWVALU, HWRC],   8>;
def : HWWriteRes<WriteFloatFMA,      [HWVALU, HWRC],   5>;
def : HWWriteRes<WriteDouble,        [HWVALU, HWRC],   22>;
def : HWWriteRes<WriteDoubleAdd,     [HWVALU, HWRC],   22>;
def : HWWriteRes<WriteDoubleCvt,     [HWVALU, HWRC],   22>;
def : HWWriteRes<WriteIntMul,        [HWVALU, HWRC],   8>;
def : HWWriteRes<WriteTrans64,       [HWVALU, HWTransVALU, HWRC], 24>;

def : HWWriteRes<WriteBranch,        [HWBranch],       32>;
def : HWWriteRes<WriteExport,        [HWExport, HWRC], 16>;
def : HWWriteRes<WriteLDS,           [HWLGKM,   HWRC], 20>;
def : HWWriteRes<WriteSALU,          [HWSALU,   HWRC], 2>;
def : HWWriteRes<WriteSMEM,          [HWLGKM,   HWRC], 20>;
def : HWWriteRes<WriteVMEM,          [HWVMEM,   HWRC], 320>;
def : HWWriteRes<WriteBarrier,       [HWBranch],       2000>;

def : UnsupportedWriteRes<WriteSFPU>;
def : UnsupportedWriteRes<WritePseudoScalarTrans>;
} // End RetireOOO = 1

def : InstRW<[WriteCopy], (instrs COPY)>;

}  // End SchedModel = GFX10SpeedModel

let SchedModel = GFX11SpeedModel in {

// The latency values are 1 / (operations / cycle).
// Add 1 stall cycle for VGPR read.
let RetireOOO = 1 in { // llvm-mca specific flag
def : HWWriteRes<Write32Bit,         [HWVALU, HWRC],   5>;
def : HWWriteRes<WriteFloatCvt,      [HWVALU, HWRC],   5>;
def : HWWriteRes<Write64Bit,         [HWVALU, HWRC],   6>;
def : HWWriteRes<WriteTrans32,       [HWTransVALU, HWRC], 10>;
def : HWWriteRes<WriteQuarterRate32, [HWVALU, HWRC],   8>;
def : HWWriteRes<WriteFloatFMA,      [HWVALU, HWRC],   5>;
def : HWWriteRes<WriteDouble,        [HWVALU, HWRC],   38>;
def : HWWriteRes<WriteDoubleAdd,     [HWVALU, HWRC],   38>;
def : HWWriteRes<WriteDoubleCvt,     [HWVALU, HWRC],   38>;
def : HWWriteRes<WriteIntMul,        [HWVALU, HWRC],   8>;
def : HWWriteRes<WriteTrans64,       [HWVALU, HWTransVALU, HWRC], 40>;

def : HWWriteRes<WriteBranch,        [HWBranch],       32>;
def : HWWriteRes<WriteExport,        [HWExport, HWRC], 16>;
def : HWWriteRes<WriteLDS,           [HWLGKM,   HWRC], 20>;
def : HWWriteRes<WriteSALU,          [HWSALU,   HWRC], 2>;
def : HWWriteRes<WriteSFPU,          [HWSALU,   HWRC], 4>;
def : HWWriteRes<WriteSMEM,          [HWLGKM,   HWRC], 20>;
def : HWWriteRes<WriteVMEM,          [HWVMEM,   HWRC], 320>;
def : HWWriteRes<WriteBarrier,       [HWBranch],       2000>;
} // End RetireOOO = 1

def : UnsupportedWriteRes<WritePseudoScalarTrans>;

def : InstRW<[WriteCopy], (instrs COPY)>;

}  // End SchedModel = GFX11SpeedModel

let SchedModel = GFX12SpeedModel in {

def : HWWriteRes<Write32Bit,             [HWVALU, HWRC],   5>;
def : HWWriteRes<WriteFloatCvt,          [HWVALU, HWRC],   5>;
def : HWWriteRes<Write64Bit,             [HWVALU, HWRC],   6>;
def : HWWriteRes<WriteTrans32,           [HWVALU, HWRC],   10>;
def : HWWriteRes<WriteQuarterRate32,     [HWVALU, HWRC],   8>;
def : HWWriteRes<WriteFloatFMA,          [HWVALU, HWRC],   5>;
def : HWWriteRes<WriteDouble,            [HWVALU, HWRC],   38>;
def : HWWriteRes<WriteDoubleAdd,         [HWVALU, HWRC],   38>;
def : HWWriteRes<WriteDoubleCvt,         [HWVALU, HWRC],   38>;
def : HWWriteRes<WriteIntMul,            [HWVALU, HWRC],   8>;
def : HWWriteRes<WriteTrans64,           [HWVALU, HWRC],   40>;
def : HWWriteRes<WritePseudoScalarTrans, [HWVALU, HWRC],   7>;

def : HWWriteRes<WriteBranch,            [HWBranch],       32>;
def : HWWriteRes<WriteExport,            [HWExport, HWRC], 16>;
def : HWWriteRes<WriteLDS,               [HWLGKM,   HWRC], 20>;
def : HWWriteRes<WriteSALU,              [HWSALU,   HWRC], 2>;
def : HWWriteRes<WriteSFPU,              [HWSALU,   HWRC], 4>;
def : HWWriteRes<WriteSMEM,              [HWLGKM,   HWRC], 20>;
def : HWWriteRes<WriteVMEM,              [HWVMEM,   HWRC], 320>;
def : HWWriteRes<WriteBarrier,           [HWBranch],       2000>;

def : InstRW<[WriteCopy], (instrs COPY)>;

}  // End SchedModel = GFX12SpeedModel

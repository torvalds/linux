//=- AArch64SchedA57.td - ARM Cortex-A57 Scheduling Defs -----*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for ARM Cortex-A57 to support
// instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// The Cortex-A57 is a traditional superscalar microprocessor with a
// conservative 3-wide in-order stage for decode and dispatch. Combined with the
// much wider out-of-order issue stage, this produced a need to carefully
// schedule micro-ops so that all three decoded each cycle are successfully
// issued as the reservation station(s) simply don't stay occupied for long.
// Therefore, IssueWidth is set to the narrower of the two at three, while still
// modeling the machine as out-of-order.

def CortexA57Model : SchedMachineModel {
  let IssueWidth        =   3; // 3-way decode and dispatch
  let MicroOpBufferSize = 128; // 128 micro-op re-order buffer
  let LoadLatency       =   4; // Optimistic load latency
  let MispredictPenalty =  14; // Fetch + Decode/Rename/Dispatch + Branch

  // Enable partial & runtime unrolling. The magic number is chosen based on
  // experiments and benchmarking data.
  let LoopMicroOpBufferSize = 16;
  let CompleteModel = 1;

  list<Predicate> UnsupportedFeatures = !listconcat(SVEUnsupported.F,
                                                    PAUnsupported.F,
                                                    SMEUnsupported.F,
                                                    [HasMTE, HasCSSC]);
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available on Cortex-A57.
// Cortex A-57 has 8 pipelines that each has its own 8-entry queue where
// micro-ops wait for their operands and then issue out-of-order.

def A57UnitB : ProcResource<1>;  // Type B micro-ops
def A57UnitI : ProcResource<2>;  // Type I micro-ops
def A57UnitM : ProcResource<1>;  // Type M micro-ops
def A57UnitL : ProcResource<1>;  // Type L micro-ops
def A57UnitS : ProcResource<1>;  // Type S micro-ops
def A57UnitX : ProcResource<1>;  // Type X micro-ops
def A57UnitW : ProcResource<1>;  // Type W micro-ops
let SchedModel = CortexA57Model in {
  def A57UnitV : ProcResGroup<[A57UnitX, A57UnitW]>;    // Type V micro-ops
}

let SchedModel = CortexA57Model in {

//===----------------------------------------------------------------------===//
// Define customized scheduler read/write types specific to the Cortex-A57.

include "AArch64SchedA57WriteRes.td"

//===----------------------------------------------------------------------===//
// Map the target-defined scheduler read/write resources and latency for
// Cortex-A57. The Cortex-A57 types are directly associated with resources, so
// defining the aliases precludes the need for mapping them using WriteRes. The
// aliases are sufficient for creating a coarse, working model. As the model
// evolves, InstRWs will be used to override some of these SchedAliases.
//
// WARNING: Using SchedAliases is convenient and works well for latency and
//          resource lookup for instructions. However, this creates an entry in
//          AArch64WriteLatencyTable with a WriteResourceID of 0, breaking
//          any SchedReadAdvance since the lookup will fail.

def : SchedAlias<WriteImm,   A57Write_1cyc_1I>;
def : SchedAlias<WriteI,     A57Write_1cyc_1I>;
def : SchedAlias<WriteISReg, A57Write_2cyc_1M>;
def : SchedAlias<WriteIEReg, A57Write_2cyc_1M>;
def : SchedAlias<WriteExtr,  A57Write_1cyc_1I>;
def : SchedAlias<WriteIS,    A57Write_1cyc_1I>;
def : SchedAlias<WriteID32,  A57Write_19cyc_1M>;
def : SchedAlias<WriteID64,  A57Write_35cyc_1M>;
def : WriteRes<WriteIM32, [A57UnitM]> { let Latency = 3; }
def : WriteRes<WriteIM64, [A57UnitM]> { let Latency = 5; }
def : SchedAlias<WriteBr,    A57Write_1cyc_1B>;
def : SchedAlias<WriteBrReg, A57Write_1cyc_1B>;
def : SchedAlias<WriteLD,    A57Write_4cyc_1L>;
def : SchedAlias<WriteST,    A57Write_1cyc_1S>;
def : SchedAlias<WriteSTP,   A57Write_1cyc_1S>;
def : SchedAlias<WriteAdr,   A57Write_1cyc_1I>;
def : SchedAlias<WriteLDIdx, A57Write_4cyc_1I_1L>;
def : SchedAlias<WriteSTIdx, A57Write_1cyc_1I_1S>;
def : SchedAlias<WriteF,     A57Write_3cyc_1V>;
def : SchedAlias<WriteFCmp,  A57Write_3cyc_1V>;
def : SchedAlias<WriteFCvt,  A57Write_5cyc_1V>;
def : SchedAlias<WriteFCopy, A57Write_5cyc_1L>;
def : SchedAlias<WriteFImm,  A57Write_3cyc_1V>;
def : WriteRes<WriteFMul, [A57UnitV]> { let Latency = 5;}
def : SchedAlias<WriteFDiv,  A57Write_17cyc_1W>;
def : SchedAlias<WriteVd,    A57Write_3cyc_1V>;
def : SchedAlias<WriteVq,    A57Write_3cyc_1V>;
def : SchedAlias<WriteVLD,   A57Write_5cyc_1L>;
def : SchedAlias<WriteVST,   A57Write_1cyc_1S>;

def : WriteRes<WriteAtomic,  []> { let Unsupported = 1; }

def : WriteRes<WriteSys,     []> { let Latency = 1; }
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }

def : WriteRes<WriteLDHi,    []> { let Latency = 4; }

// Forwarding logic is only modeled for multiply and accumulate
def : ReadAdvance<ReadI,       0>;
def : ReadAdvance<ReadISReg,   0>;
def : ReadAdvance<ReadIEReg,   0>;
def : ReadAdvance<ReadIM,      0>;
def : ReadAdvance<ReadIMA,     2, [WriteIM32, WriteIM64]>;
def : ReadAdvance<ReadID,      0>;
def : ReadAdvance<ReadExtrHi,  0>;
def : ReadAdvance<ReadST,      0>;
def : ReadAdvance<ReadAdrBase, 0>;
def : ReadAdvance<ReadVLD,     0>;


//===----------------------------------------------------------------------===//
// Specialize the coarse model by associating instruction groups with the
// subtarget-defined types. As the modeled is refined, this will override most
// of the above ShchedAlias mappings.

// Miscellaneous
// -----------------------------------------------------------------------------

def : InstRW<[WriteI], (instrs COPY)>;


// Branch Instructions
// -----------------------------------------------------------------------------

def : InstRW<[A57Write_1cyc_1B_1I], (instrs BL)>;
def : InstRW<[A57Write_2cyc_1B_1I], (instrs BLR)>;


// Shifted Register with Shift == 0
// ----------------------------------------------------------------------------

def A57WriteISReg : SchedWriteVariant<[
       SchedVar<RegShiftedPred, [WriteISReg]>,
       SchedVar<NoSchedPred, [WriteI]>]>;
def : InstRW<[A57WriteISReg], (instregex ".*rs$")>;


// Divide and Multiply Instructions
// -----------------------------------------------------------------------------

// Multiply high
def : InstRW<[A57Write_6cyc_1M], (instrs SMULHrr, UMULHrr)>;


// Miscellaneous Data-Processing Instructions
// -----------------------------------------------------------------------------

def : InstRW<[A57Write_1cyc_1I],    (instrs EXTRWrri)>;
def : InstRW<[A57Write_3cyc_1I_1M], (instrs EXTRXrri)>;
def : InstRW<[A57Write_2cyc_1M],    (instregex "BFM")>;


// Cryptography Extensions
// -----------------------------------------------------------------------------

def A57ReadAES  : SchedReadAdvance<3, [A57Write_3cyc_1W]>;
def : InstRW<[A57Write_3cyc_1W], (instregex "^AES[DE]")>;
def : InstRW<[A57Write_3cyc_1W, A57ReadAES], (instregex "^AESI?MC")>;
def : InstRW<[A57Write_6cyc_2V], (instregex "^SHA1SU0")>;
def : InstRW<[A57Write_3cyc_1W], (instregex "^SHA1(H|SU1)")>;
def : InstRW<[A57Write_6cyc_2W], (instregex "^SHA1[CMP]")>;
def : InstRW<[A57Write_3cyc_1W], (instregex "^SHA256SU0")>;
def : InstRW<[A57Write_6cyc_2W], (instregex "^SHA256(H|H2|SU1)")>;
def : InstRW<[A57Write_3cyc_1W], (instregex "^CRC32")>;


// Vector Load
// -----------------------------------------------------------------------------

def : InstRW<[A57Write_8cyc_1L_1V],           (instregex "LD1i(8|16|32)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_1L_1V], (instregex "LD1i(8|16|32)_POST$")>;
def : InstRW<[A57Write_5cyc_1L],            (instregex "LD1i(64)$")>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L],  (instregex "LD1i(64)_POST$")>;

def : InstRW<[A57Write_8cyc_1L_1V],           (instregex "LD1Rv(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_1L_1V], (instregex "LD1Rv(8b|4h|2s)_POST$")>;
def : InstRW<[A57Write_5cyc_1L],            (instregex "LD1Rv(1d)$")>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L],  (instregex "LD1Rv(1d)_POST$")>;
def : InstRW<[A57Write_8cyc_1L_1V],           (instregex "LD1Rv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_1L_1V], (instregex "LD1Rv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[A57Write_5cyc_1L],              (instregex "LD1Onev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L],    (instregex "LD1Onev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[A57Write_5cyc_1L],              (instregex "LD1Onev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L],    (instregex "LD1Onev(16b|8h|4s|2d)_POST$")>;
def : InstRW<[A57Write_5cyc_1L],              (instregex "LD1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L],    (instregex "LD1Twov(8b|4h|2s|1d)_POST$")>;
def : InstRW<[A57Write_6cyc_2L],             (instregex "LD1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A57Write_6cyc_2L],   (instregex "LD1Twov(16b|8h|4s|2d)_POST$")>;
def : InstRW<[A57Write_6cyc_2L],             (instregex "LD1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, A57Write_6cyc_2L],   (instregex "LD1Threev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[A57Write_7cyc_3L],            (instregex "LD1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A57Write_7cyc_3L],  (instregex "LD1Threev(16b|8h|4s|2d)_POST$")>;
def : InstRW<[A57Write_6cyc_2L],             (instregex "LD1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, A57Write_6cyc_2L],   (instregex "LD1Fourv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[A57Write_8cyc_4L],           (instregex "LD1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_4L], (instregex "LD1Fourv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[A57Write_8cyc_1L_2V],           (instregex "LD2i(8|16)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_1L_2V], (instregex "LD2i(8|16)_POST$")>;
def : InstRW<[A57Write_6cyc_2L],            (instregex "LD2i(32)$")>;
def : InstRW<[WriteAdr, A57Write_6cyc_2L],  (instregex "LD2i(32)_POST$")>;
def : InstRW<[A57Write_8cyc_1L_1V],            (instregex "LD2i(64)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_1L_1V],  (instregex "LD2i(64)_POST$")>;

def : InstRW<[A57Write_8cyc_1L_1V],            (instregex "LD2Rv(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_1L_1V],  (instregex "LD2Rv(8b|4h|2s)_POST$")>;
def : InstRW<[A57Write_5cyc_1L],             (instregex "LD2Rv(1d)$")>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L],   (instregex "LD2Rv(1d)_POST$")>;
def : InstRW<[A57Write_8cyc_1L_2V],           (instregex "LD2Rv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_1L_2V], (instregex "LD2Rv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[A57Write_8cyc_1L_1V],             (instregex "LD2Twov(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_1L_1V],   (instregex "LD2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[A57Write_9cyc_2L_2V],           (instregex "LD2Twov(16b|8h|4s)$")>;
def : InstRW<[WriteAdr, A57Write_9cyc_2L_2V], (instregex "LD2Twov(16b|8h|4s)_POST$")>;
def : InstRW<[A57Write_6cyc_2L],             (instregex "LD2Twov(2d)$")>;
def : InstRW<[WriteAdr, A57Write_6cyc_2L],   (instregex "LD2Twov(2d)_POST$")>;

def : InstRW<[A57Write_9cyc_1L_3V],           (instregex "LD3i(8|16)$")>;
def : InstRW<[WriteAdr, A57Write_9cyc_1L_3V], (instregex "LD3i(8|16)_POST$")>;
def : InstRW<[A57Write_8cyc_1L_2V],            (instregex "LD3i(32)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_1L_2V],  (instregex "LD3i(32)_POST$")>;
def : InstRW<[A57Write_6cyc_2L],             (instregex "LD3i(64)$")>;
def : InstRW<[WriteAdr, A57Write_6cyc_2L],   (instregex "LD3i(64)_POST$")>;

def : InstRW<[A57Write_8cyc_1L_2V],             (instregex "LD3Rv(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_1L_2V],   (instregex "LD3Rv(8b|4h|2s)_POST$")>;
def : InstRW<[A57Write_6cyc_2L],              (instregex "LD3Rv(1d)$")>;
def : InstRW<[WriteAdr, A57Write_6cyc_2L],    (instregex "LD3Rv(1d)_POST$")>;
def : InstRW<[A57Write_9cyc_1L_3V],            (instregex "LD3Rv(16b|8h|4s)$")>;
def : InstRW<[WriteAdr, A57Write_9cyc_1L_3V],  (instregex "LD3Rv(16b|8h|4s)_POST$")>;
def : InstRW<[A57Write_9cyc_2L_3V],           (instregex "LD3Rv(2d)$")>;
def : InstRW<[WriteAdr, A57Write_9cyc_2L_3V], (instregex "LD3Rv(2d)_POST$")>;

def : InstRW<[A57Write_9cyc_2L_2V],               (instregex "LD3Threev(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, A57Write_9cyc_2L_2V],     (instregex "LD3Threev(8b|4h|2s)_POST$")>;
def : InstRW<[A57Write_10cyc_3L_4V],           (instregex "LD3Threev(16b|8h|4s)$")>;
def : InstRW<[WriteAdr, A57Write_10cyc_3L_4V], (instregex "LD3Threev(16b|8h|4s)_POST$")>;
def : InstRW<[A57Write_8cyc_4L],               (instregex "LD3Threev(2d)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_4L],     (instregex "LD3Threev(2d)_POST$")>;

def : InstRW<[A57Write_9cyc_2L_3V],           (instregex "LD4i(8|16)$")>;
def : InstRW<[WriteAdr, A57Write_9cyc_2L_3V], (instregex "LD4i(8|16)_POST$")>;
def : InstRW<[A57Write_8cyc_1L_2V],             (instregex "LD4i(32)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_1L_2V],   (instregex "LD4i(32)_POST$")>;
def : InstRW<[A57Write_9cyc_2L_3V],           (instregex "LD4i(64)$")>;
def : InstRW<[WriteAdr, A57Write_9cyc_2L_3V], (instregex "LD4i(64)_POST$")>;

def : InstRW<[A57Write_8cyc_1L_2V],              (instregex "LD4Rv(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_1L_2V],    (instregex "LD4Rv(8b|4h|2s)_POST$")>;
def : InstRW<[A57Write_6cyc_2L],               (instregex "LD4Rv(1d)$")>;
def : InstRW<[WriteAdr, A57Write_6cyc_2L],     (instregex "LD4Rv(1d)_POST$")>;
def : InstRW<[A57Write_9cyc_2L_3V],            (instregex "LD4Rv(16b|8h|4s)$")>;
def : InstRW<[WriteAdr, A57Write_9cyc_2L_3V],  (instregex "LD4Rv(16b|8h|4s)_POST$")>;
def : InstRW<[A57Write_9cyc_2L_4V],           (instregex "LD4Rv(2d)$")>;
def : InstRW<[WriteAdr, A57Write_9cyc_2L_4V], (instregex "LD4Rv(2d)_POST$")>;

def : InstRW<[A57Write_9cyc_2L_2V],                (instregex "LD4Fourv(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, A57Write_9cyc_2L_2V],      (instregex "LD4Fourv(8b|4h|2s)_POST$")>;
def : InstRW<[A57Write_11cyc_4L_4V],           (instregex "LD4Fourv(16b|8h|4s)$")>;
def : InstRW<[WriteAdr, A57Write_11cyc_4L_4V], (instregex "LD4Fourv(16b|8h|4s)_POST$")>;
def : InstRW<[A57Write_8cyc_4L],                (instregex "LD4Fourv(2d)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_4L],      (instregex "LD4Fourv(2d)_POST$")>;

// Vector Store
// -----------------------------------------------------------------------------

def : InstRW<[A57Write_1cyc_1S],            (instregex "ST1i(8|16|32)$")>;
def : InstRW<[WriteAdr, A57Write_1cyc_1S],  (instregex "ST1i(8|16|32)_POST$")>;
def : InstRW<[A57Write_3cyc_1S_1V],           (instregex "ST1i(64)$")>;
def : InstRW<[WriteAdr, A57Write_3cyc_1S_1V], (instregex "ST1i(64)_POST$")>;

def : InstRW<[A57Write_1cyc_1S],                  (instregex "ST1Onev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, A57Write_1cyc_1S],        (instregex "ST1Onev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[A57Write_2cyc_2S],                 (instregex "ST1Onev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A57Write_2cyc_2S],       (instregex "ST1Onev(16b|8h|4s|2d)_POST$")>;
def : InstRW<[A57Write_2cyc_2S],                 (instregex "ST1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, A57Write_2cyc_2S],       (instregex "ST1Twov(8b|4h|2s|1d)_POST$")>;
def : InstRW<[A57Write_4cyc_4S],               (instregex "ST1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A57Write_4cyc_4S],     (instregex "ST1Twov(16b|8h|4s|2d)_POST$")>;
def : InstRW<[A57Write_3cyc_3S],                (instregex "ST1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, A57Write_3cyc_3S],      (instregex "ST1Threev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[A57Write_6cyc_6S],             (instregex "ST1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A57Write_6cyc_6S],   (instregex "ST1Threev(16b|8h|4s|2d)_POST$")>;
def : InstRW<[A57Write_4cyc_4S],               (instregex "ST1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, A57Write_4cyc_4S],     (instregex "ST1Fourv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[A57Write_8cyc_8S],           (instregex "ST1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_8S], (instregex "ST1Fourv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[A57Write_3cyc_1S_1V],           (instregex "ST2i(8|16|32)$")>;
def : InstRW<[WriteAdr, A57Write_3cyc_1S_1V], (instregex "ST2i(8|16|32)_POST$")>;
def : InstRW<[A57Write_2cyc_2S],           (instregex "ST2i(64)$")>;
def : InstRW<[WriteAdr, A57Write_2cyc_2S], (instregex "ST2i(64)_POST$")>;

def : InstRW<[A57Write_3cyc_2S_1V],              (instregex "ST2Twov(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, A57Write_3cyc_2S_1V],    (instregex "ST2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[A57Write_4cyc_4S_2V],           (instregex "ST2Twov(16b|8h|4s)$")>;
def : InstRW<[WriteAdr, A57Write_4cyc_4S_2V], (instregex "ST2Twov(16b|8h|4s)_POST$")>;
def : InstRW<[A57Write_4cyc_4S],             (instregex "ST2Twov(2d)$")>;
def : InstRW<[WriteAdr, A57Write_4cyc_4S],   (instregex "ST2Twov(2d)_POST$")>;

def : InstRW<[A57Write_3cyc_1S_1V],            (instregex "ST3i(8|16)$")>;
def : InstRW<[WriteAdr, A57Write_3cyc_1S_1V],  (instregex "ST3i(8|16)_POST$")>;
def : InstRW<[A57Write_3cyc_3S],           (instregex "ST3i(32)$")>;
def : InstRW<[WriteAdr, A57Write_3cyc_3S], (instregex "ST3i(32)_POST$")>;
def : InstRW<[A57Write_3cyc_2S_1V],           (instregex "ST3i(64)$")>;
def : InstRW<[WriteAdr, A57Write_3cyc_2S_1V], (instregex "ST3i(64)_POST$")>;

def : InstRW<[A57Write_3cyc_3S_2V],                 (instregex "ST3Threev(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, A57Write_3cyc_3S_2V],       (instregex "ST3Threev(8b|4h|2s)_POST$")>;
def : InstRW<[A57Write_6cyc_6S_4V],           (instregex "ST3Threev(16b|8h|4s)$")>;
def : InstRW<[WriteAdr, A57Write_6cyc_6S_4V], (instregex "ST3Threev(16b|8h|4s)_POST$")>;
def : InstRW<[A57Write_6cyc_6S],                (instregex "ST3Threev(2d)$")>;
def : InstRW<[WriteAdr, A57Write_6cyc_6S],      (instregex "ST3Threev(2d)_POST$")>;

def : InstRW<[A57Write_3cyc_1S_1V],             (instregex "ST4i(8|16)$")>;
def : InstRW<[WriteAdr, A57Write_3cyc_1S_1V],   (instregex "ST4i(8|16)_POST$")>;
def : InstRW<[A57Write_4cyc_4S],           (instregex "ST4i(32)$")>;
def : InstRW<[WriteAdr, A57Write_4cyc_4S], (instregex "ST4i(32)_POST$")>;
def : InstRW<[A57Write_3cyc_2S_1V],            (instregex "ST4i(64)$")>;
def : InstRW<[WriteAdr, A57Write_3cyc_2S_1V],  (instregex "ST4i(64)_POST$")>;

def : InstRW<[A57Write_4cyc_4S_2V],                  (instregex "ST4Fourv(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, A57Write_4cyc_4S_2V],        (instregex "ST4Fourv(8b|4h|2s)_POST$")>;
def : InstRW<[A57Write_8cyc_8S_4V],           (instregex "ST4Fourv(16b|8h|4s)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_8S_4V], (instregex "ST4Fourv(16b|8h|4s)_POST$")>;
def : InstRW<[A57Write_8cyc_8S],                (instregex "ST4Fourv(2d)$")>;
def : InstRW<[WriteAdr, A57Write_8cyc_8S],      (instregex "ST4Fourv(2d)_POST$")>;

// Vector - Integer
// -----------------------------------------------------------------------------

// Reference for forms in this group
//   D form - v8i8, v4i16, v2i32
//   Q form - v16i8, v8i16, v4i32
//   D form - v1i8, v1i16, v1i32, v1i64
//   Q form - v16i8, v8i16, v4i32, v2i64
//   D form - v8i8_v8i16, v4i16_v4i32, v2i32_v2i64
//   Q form - v16i8_v8i16, v8i16_v4i32, v4i32_v2i64

// Cortex A57 Software Optimization Guide Sec 3.14
// Advance for absolute diff accum, pairwise add and accumulate, shift accumulate
def A57ReadIVA3 : SchedReadAdvance<3, [A57Write_4cyc_1X_NonMul_Forward, A57Write_5cyc_2X_NonMul_Forward]>;

// ASIMD absolute diff accum, D-form
def : InstRW<[A57Write_4cyc_1X_NonMul_Forward, A57ReadIVA3], (instregex "^[SU]ABA(v8i8|v4i16|v2i32)$")>;
// ASIMD absolute diff accum, Q-form
def : InstRW<[A57Write_5cyc_2X_NonMul_Forward, A57ReadIVA3], (instregex "^[SU]ABA(v16i8|v8i16|v4i32)$")>;
// ASIMD absolute diff accum long
def : InstRW<[A57Write_4cyc_1X_NonMul_Forward, A57ReadIVA3], (instregex "^[SU]ABAL")>;

// ASIMD arith, reduce, 4H/4S
def : InstRW<[A57Write_4cyc_1X], (instregex "^[SU]?ADDL?V(v8i8|v4i16|v2i32)v$")>;
// ASIMD arith, reduce, 8B/8H
def : InstRW<[A57Write_7cyc_1V_1X], (instregex "^[SU]?ADDL?V(v8i16|v4i32)v$")>;
// ASIMD arith, reduce, 16B
def : InstRW<[A57Write_8cyc_2X], (instregex "^[SU]?ADDL?Vv16i8v$")>;

// ASIMD max/min, reduce, 4H/4S
def : InstRW<[A57Write_4cyc_1X], (instregex "^[SU](MIN|MAX)V(v4i16|v4i32)v$")>;
// ASIMD max/min, reduce, 8B/8H
def : InstRW<[A57Write_7cyc_1V_1X], (instregex "^[SU](MIN|MAX)V(v8i8|v8i16)v$")>;
// ASIMD max/min, reduce, 16B
def : InstRW<[A57Write_8cyc_2X], (instregex "^[SU](MIN|MAX)Vv16i8v$")>;

// ASIMD multiply, D-form
// MUL
def : InstRW<[A57Write_5cyc_1W_Mul_Forward], (instregex "^MUL(v8i8|v4i16|v2i32|v1i8|v1i16|v1i32|v1i64)(_indexed)?$")>;
// PMUL, SQDMULH, SQRDMULH
def : InstRW<[A57Write_5cyc_1W], (instregex "^(PMUL|SQR?DMULH)(v8i8|v4i16|v2i32|v1i8|v1i16|v1i32|v1i64)(_indexed)?$")>;

// ASIMD multiply, Q-form
// MUL
def : InstRW<[A57Write_6cyc_2W_Mul_Forward], (instregex "^MUL(v16i8|v8i16|v4i32)(_indexed)?$")>;
// PMUL, SQDMULH, SQRDMULH
def : InstRW<[A57Write_6cyc_2W], (instregex "^(PMUL|SQR?DMULH)(v16i8|v8i16|v4i32)(_indexed)?$")>;

// Cortex A57 Software Optimization Guide Sec 3.14
def A57ReadIVMA4   : SchedReadAdvance<4 , [A57Write_5cyc_1W_Mul_Forward, A57Write_6cyc_2W_Mul_Forward]>;
def A57ReadIVMA3   : SchedReadAdvance<3 , [A57Write_5cyc_1W_Mul_Forward, A57Write_6cyc_2W_Mul_Forward]>;

// ASIMD multiply accumulate, D-form
def : InstRW<[A57Write_5cyc_1W_Mul_Forward, A57ReadIVMA4], (instregex "^ML[AS](v8i8|v4i16|v2i32)(_indexed)?$")>;
// ASIMD multiply accumulate, Q-form
def : InstRW<[A57Write_6cyc_2W_Mul_Forward, A57ReadIVMA4], (instregex "^ML[AS](v16i8|v8i16|v4i32)(_indexed)?$")>;

// ASIMD multiply accumulate long
// ASIMD multiply accumulate saturating long
def : InstRW<[A57Write_5cyc_1W_Mul_Forward, A57ReadIVMA4], (instregex "^(S|U)ML[AS]L")>;
def : InstRW<[A57Write_5cyc_1W_Mul_Forward, A57ReadIVMA3], (instregex "^SQDML[AS]L")>;

// ASIMD multiply long
def : InstRW<[A57Write_5cyc_1W_Mul_Forward], (instregex "^(S|U)MULL")>;
def : InstRW<[A57Write_5cyc_1W], (instregex "^SQDMULL")>;
def : InstRW<[A57Write_5cyc_1W], (instregex "^PMULL(v8i8|v16i8)")>;
def : InstRW<[A57Write_3cyc_1W], (instregex "^PMULL(v1i64|v2i64)")>;

// ASIMD pairwise add and accumulate
// ASIMD shift accumulate
def : InstRW<[A57Write_4cyc_1X_NonMul_Forward, A57ReadIVA3], (instregex "^[SU]ADALP")>;
def : InstRW<[A57Write_4cyc_1X_NonMul_Forward, A57ReadIVA3], (instregex "^(S|SR|U|UR)SRA")>;

// ASIMD shift by immed, complex
def : InstRW<[A57Write_4cyc_1X], (instregex "^[SU]?(Q|R){1,2}SHR")>;
def : InstRW<[A57Write_4cyc_1X], (instregex "^SQSHLU")>;


// ASIMD shift by register, basic, Q-form
def : InstRW<[A57Write_4cyc_2X], (instregex "^[SU]SHL(v16i8|v8i16|v4i32|v2i64)")>;

// ASIMD shift by register, complex, D-form
def : InstRW<[A57Write_4cyc_1X], (instregex "^[SU][QR]{1,2}SHL(v1i8|v1i16|v1i32|v1i64|v8i8|v4i16|v2i32|b|d|h|s)")>;

// ASIMD shift by register, complex, Q-form
def : InstRW<[A57Write_5cyc_2X], (instregex "^[SU][QR]{1,2}SHL(v16i8|v8i16|v4i32|v2i64)")>;


// Vector - Floating Point
// -----------------------------------------------------------------------------

// Reference for forms in this group
//   D form - v2f32
//   Q form - v4f32, v2f64
//   D form - 32, 64
//   D form - v1i32, v1i64
//   D form - v2i32
//   Q form - v4i32, v2i64

// ASIMD FP arith, normal, D-form
def : InstRW<[A57Write_5cyc_1V], (instregex "^(FABD|FADD|FSUB)(v2f32|32|64|v2i32p)")>;
// ASIMD FP arith, normal, Q-form
def : InstRW<[A57Write_5cyc_2V], (instregex "^(FABD|FADD|FSUB)(v4f32|v2f64|v2i64p)")>;

// ASIMD FP arith, pairwise, D-form
def : InstRW<[A57Write_5cyc_1V], (instregex "^FADDP(v2f32|32|64|v2i32)")>;
// ASIMD FP arith, pairwise, Q-form
def : InstRW<[A57Write_9cyc_3V], (instregex "^FADDP(v4f32|v2f64|v2i64)")>;

// ASIMD FP compare, D-form
def : InstRW<[A57Write_5cyc_1V], (instregex "^(FACGE|FACGT|FCMEQ|FCMGE|FCMGT|FCMLE|FCMLT)(v2f32|32|64|v1i32|v2i32|v1i64)")>;
// ASIMD FP compare, Q-form
def : InstRW<[A57Write_5cyc_2V], (instregex "^(FACGE|FACGT|FCMEQ|FCMGE|FCMGT|FCMLE|FCMLT)(v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP convert, long and narrow
def : InstRW<[A57Write_8cyc_3V], (instregex "^FCVT(L|N|XN)v")>;
// ASIMD FP convert, other, D-form
def : InstRW<[A57Write_5cyc_1V], (instregex "^[FSU]CVT([AMNPZ][SU])?(_Int)?(v2f32|v1i32|v2i32|v1i64)")>;
// ASIMD FP convert, other, Q-form
def : InstRW<[A57Write_5cyc_2V], (instregex "^[FSU]CVT([AMNPZ][SU])?(_Int)?(v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP divide, D-form, F32
def : InstRW<[A57Write_17cyc_1W], (instregex "FDIVv2f32")>;
// ASIMD FP divide, Q-form, F32
def : InstRW<[A57Write_34cyc_2W], (instregex "FDIVv4f32")>;
// ASIMD FP divide, Q-form, F64
def : InstRW<[A57Write_64cyc_2W], (instregex "FDIVv2f64")>;

// Note: These were simply duplicated from ASIMD FDIV because of missing documentation
// ASIMD FP square root, D-form, F32
def : InstRW<[A57Write_17cyc_1W], (instregex "FSQRTv2f32")>;
// ASIMD FP square root, Q-form, F32
def : InstRW<[A57Write_34cyc_2W], (instregex "FSQRTv4f32")>;
// ASIMD FP square root, Q-form, F64
def : InstRW<[A57Write_64cyc_2W], (instregex "FSQRTv2f64")>;

// ASIMD FP max/min, normal, D-form
def : InstRW<[A57Write_5cyc_1V], (instregex "^(FMAX|FMIN)(NM)?(v2f32)")>;
// ASIMD FP max/min, normal, Q-form
def : InstRW<[A57Write_5cyc_2V], (instregex "^(FMAX|FMIN)(NM)?(v4f32|v2f64)")>;
// ASIMD FP max/min, pairwise, D-form
def : InstRW<[A57Write_5cyc_1V], (instregex "^(FMAX|FMIN)(NM)?P(v2f32|v2i32)")>;
// ASIMD FP max/min, pairwise, Q-form
def : InstRW<[A57Write_9cyc_3V], (instregex "^(FMAX|FMIN)(NM)?P(v4f32|v2f64|v2i64)")>;
// ASIMD FP max/min, reduce
def : InstRW<[A57Write_10cyc_3V], (instregex "^(FMAX|FMIN)(NM)?Vv")>;

// ASIMD FP multiply, D-form, FZ
def : InstRW<[A57Write_5cyc_1V_FP_Forward], (instregex "^FMULX?(v2f32|v1i32|v2i32|v1i64|32|64)")>;
// ASIMD FP multiply, Q-form, FZ
def : InstRW<[A57Write_5cyc_2V_FP_Forward], (instregex "^FMULX?(v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP multiply accumulate, D-form, FZ
// ASIMD FP multiply accumulate, Q-form, FZ
def A57WriteFPVMAD : SchedWriteRes<[A57UnitV]> { let Latency = 9;  }
def A57WriteFPVMAQ : SchedWriteRes<[A57UnitV, A57UnitV]> { let Latency = 10;  }

// Cortex A57 Software Optimization Guide Sec 3.15
// Advances from FP mul and mul-accum to mul-accum
def A57ReadFPVMA5  : SchedReadAdvance<5, [A57WriteFPVMAD, A57WriteFPVMAQ, A57Write_5cyc_1V_FP_Forward, A57Write_5cyc_2V_FP_Forward]>;
def A57ReadFPVMA6  : SchedReadAdvance<6, [A57WriteFPVMAD, A57WriteFPVMAQ, A57Write_5cyc_1V_FP_Forward, A57Write_5cyc_2V_FP_Forward]>;

def : InstRW<[A57WriteFPVMAD, A57ReadFPVMA5], (instregex "^FML[AS](v2f32|v1i32|v2i32|v1i64)")>;
def : InstRW<[A57WriteFPVMAQ, A57ReadFPVMA6], (instregex "^FML[AS](v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP round, D-form
def : InstRW<[A57Write_5cyc_1V], (instregex "^FRINT[AIMNPXZ](v2f32)")>;
// ASIMD FP round, Q-form
def : InstRW<[A57Write_5cyc_2V], (instregex "^FRINT[AIMNPXZ](v4f32|v2f64)")>;


// Vector - Miscellaneous
// -----------------------------------------------------------------------------

// Reference for forms in this group
//   D form - v8i8, v4i16, v2i32
//   Q form - v16i8, v8i16, v4i32
//   D form - v1i8, v1i16, v1i32, v1i64
//   Q form - v16i8, v8i16, v4i32, v2i64

// ASIMD bitwise insert, Q-form
def : InstRW<[A57Write_3cyc_2V], (instregex "^(BIF|BIT|BSL|BSP)v16i8")>;

// ASIMD duplicate, gen reg, D-form and Q-form
def : InstRW<[A57Write_8cyc_1L_1V], (instregex "^DUP(i8|i16|i32|i64)$")>;
def : InstRW<[A57Write_8cyc_1L_1V], (instregex "^DUPv.+gpr")>;

// ASIMD move, saturating
def : InstRW<[A57Write_4cyc_1X], (instregex "^[SU]QXTU?N")>;

// ASIMD reciprocal estimate, D-form
def : InstRW<[A57Write_5cyc_1V], (instregex "^[FU](RECP|RSQRT)(E|X)(v2f32|v1i32|v2i32|v1i64)")>;
// ASIMD reciprocal estimate, Q-form
def : InstRW<[A57Write_5cyc_2V], (instregex "^[FU](RECP|RSQRT)(E|X)(v2f64|v4f32|v4i32)")>;

// ASIMD reciprocal step, D-form, FZ
def : InstRW<[A57Write_9cyc_1V], (instregex "^F(RECP|RSQRT)S(v2f32|v1i32|v2i32|v1i64|32|64)")>;
// ASIMD reciprocal step, Q-form, FZ
def : InstRW<[A57Write_9cyc_2V], (instregex "^F(RECP|RSQRT)S(v2f64|v4f32|v4i32)")>;

// ASIMD table lookup, D-form
def : InstRW<[A57Write_3cyc_1V], (instregex "^TB[LX]v8i8One")>;
def : InstRW<[A57Write_6cyc_2V], (instregex "^TB[LX]v8i8Two")>;
def : InstRW<[A57Write_9cyc_3V], (instregex "^TB[LX]v8i8Three")>;
def : InstRW<[A57Write_12cyc_4V], (instregex "^TB[LX]v8i8Four")>;
// ASIMD table lookup, Q-form
def : InstRW<[A57Write_6cyc_3V], (instregex "^TB[LX]v16i8One")>;
def : InstRW<[A57Write_9cyc_5V], (instregex "^TB[LX]v16i8Two")>;
def : InstRW<[A57Write_12cyc_7V], (instregex "^TB[LX]v16i8Three")>;
def : InstRW<[A57Write_15cyc_9V], (instregex "^TB[LX]v16i8Four")>;

// ASIMD transfer, element to gen reg
def : InstRW<[A57Write_6cyc_1I_1L], (instregex "^[SU]MOVv")>;

// ASIMD transfer, gen reg to element
def : InstRW<[A57Write_8cyc_1L_1V], (instregex "^INSv")>;

// ASIMD unzip/zip, Q-form
def : InstRW<[A57Write_6cyc_3V], (instregex "^(UZP|ZIP)(1|2)(v16i8|v8i16|v4i32|v2i64)")>;


// Remainder
// -----------------------------------------------------------------------------

def : InstRW<[A57Write_5cyc_1V], (instregex "^F(ADD|SUB)[DS]rr")>;

// Cortex A57 Software Optimization Guide Sec 3.10
def A57WriteFPMA  : SchedWriteRes<[A57UnitV]> { let Latency = 9;  }
def A57ReadFPMA5  : SchedReadAdvance<5, [A57WriteFPMA, WriteFMul]>;
def A57ReadFPM    : SchedReadAdvance<0>;
def : InstRW<[A57WriteFPMA, A57ReadFPM, A57ReadFPM, A57ReadFPMA5], (instregex "^FN?M(ADD|SUB)[DS]rrr")>;

def : InstRW<[A57Write_10cyc_1L_1V], (instregex "^[FSU]CVT[AMNPZ][SU](_Int)?[SU]?[XW]?[DS]?[rds]i?")>;
def : InstRW<[A57Write_10cyc_1L_1V], (instregex "^[SU]CVTF")>;

def : InstRW<[A57Write_32cyc_1W], (instrs FDIVDrr)>;
def : InstRW<[A57Write_17cyc_1W], (instrs FDIVSrr)>;

def : InstRW<[A57Write_5cyc_1V], (instregex "^F(MAX|MIN).+rr")>;

def : InstRW<[A57Write_5cyc_1V], (instregex "^FRINT.+r")>;

def : InstRW<[A57Write_32cyc_1W], (instrs FSQRTDr)>;
def : InstRW<[A57Write_17cyc_1W], (instrs FSQRTSr)>;

def : InstRW<[A57Write_5cyc_1L, WriteLDHi], (instrs LDNPDi)>;
def : InstRW<[A57Write_6cyc_2L, WriteLDHi], (instrs LDNPQi)>;
def : InstRW<[A57Write_5cyc_1L, WriteLDHi], (instrs LDNPSi)>;
def : InstRW<[A57Write_5cyc_1L, WriteLDHi], (instrs LDPDi)>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L, WriteLDHi], (instrs LDPDpost)>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L, WriteLDHi], (instrs LDPDpre)>;
def : InstRW<[A57Write_6cyc_2L, WriteLDHi], (instrs LDPQi)>;
def : InstRW<[WriteAdr, A57Write_6cyc_2L, WriteLDHi], (instrs LDPQpost)>;
def : InstRW<[WriteAdr, A57Write_6cyc_2L, WriteLDHi], (instrs LDPQpre)>;
def : InstRW<[A57Write_5cyc_1I_2L, WriteLDHi], (instrs LDPSWi)>;
def : InstRW<[WriteAdr, A57Write_5cyc_1I_2L, WriteLDHi], (instrs LDPSWpost)>;
def : InstRW<[WriteAdr, A57Write_5cyc_1I_2L, WriteLDHi], (instrs LDPSWpre)>;
def : InstRW<[A57Write_5cyc_1L, WriteLDHi], (instrs LDPSi)>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L, WriteLDHi], (instrs LDPSpost)>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L, WriteLDHi], (instrs LDPSpre)>;
def : InstRW<[A57Write_5cyc_1L, WriteI], (instrs LDRBpost)>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L], (instrs LDRBpre)>;
def : InstRW<[A57Write_5cyc_1L, ReadAdrBase], (instrs LDRBroW)>;
def : InstRW<[A57Write_5cyc_1L, ReadAdrBase], (instrs LDRBroX)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDRBui)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDRDl)>;
def : InstRW<[A57Write_5cyc_1L, WriteI], (instrs LDRDpost)>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L], (instrs LDRDpre)>;
def : InstRW<[A57Write_5cyc_1L, ReadAdrBase], (instrs LDRDroW)>;
def : InstRW<[A57Write_5cyc_1L, ReadAdrBase], (instrs LDRDroX)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDRDui)>;
def : InstRW<[A57Write_5cyc_1I_1L, ReadAdrBase], (instrs LDRHHroW)>;
def : InstRW<[A57Write_5cyc_1I_1L, ReadAdrBase], (instrs LDRHHroX)>;
def : InstRW<[A57Write_5cyc_1L, WriteI], (instrs LDRHpost)>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L], (instrs LDRHpre)>;
def : InstRW<[A57Write_6cyc_1I_1L, ReadAdrBase], (instrs LDRHroW)>;
def : InstRW<[A57Write_6cyc_1I_1L, ReadAdrBase], (instrs LDRHroX)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDRHui)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDRQl)>;
def : InstRW<[A57Write_5cyc_1L, WriteI], (instrs LDRQpost)>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L], (instrs LDRQpre)>;
def : InstRW<[A57Write_6cyc_1I_1L, ReadAdrBase], (instrs LDRQroW)>;
def : InstRW<[A57Write_6cyc_1I_1L, ReadAdrBase], (instrs LDRQroX)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDRQui)>;
def : InstRW<[A57Write_5cyc_1I_1L, ReadAdrBase], (instrs LDRSHWroW)>;
def : InstRW<[A57Write_5cyc_1I_1L, ReadAdrBase], (instrs LDRSHWroX)>;
def : InstRW<[A57Write_5cyc_1I_1L, ReadAdrBase], (instrs LDRSHXroW)>;
def : InstRW<[A57Write_5cyc_1I_1L, ReadAdrBase], (instrs LDRSHXroX)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDRSl)>;
def : InstRW<[A57Write_5cyc_1L, WriteI], (instrs LDRSpost)>;
def : InstRW<[WriteAdr, A57Write_5cyc_1L], (instrs LDRSpre)>;
def : InstRW<[A57Write_5cyc_1L, ReadAdrBase], (instrs LDRSroW)>;
def : InstRW<[A57Write_5cyc_1L, ReadAdrBase], (instrs LDRSroX)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDRSui)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDURBi)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDURDi)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDURHi)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDURQi)>;
def : InstRW<[A57Write_5cyc_1L], (instrs LDURSi)>;

def : InstRW<[A57Write_2cyc_2S], (instrs STNPDi)>;
def : InstRW<[A57Write_4cyc_1I_4S], (instrs STNPQi)>;
def : InstRW<[A57Write_2cyc_2S], (instrs STNPXi)>;
def : InstRW<[A57Write_2cyc_2S], (instrs STPDi)>;
def : InstRW<[WriteAdr, A57Write_2cyc_1I_2S], (instrs STPDpost)>;
def : InstRW<[WriteAdr, A57Write_2cyc_1I_2S], (instrs STPDpre)>;
def : InstRW<[A57Write_4cyc_1I_4S], (instrs STPQi)>;
def : InstRW<[WriteAdr, A57Write_4cyc_1I_4S], (instrs STPQpost)>;
def : InstRW<[WriteAdr, A57Write_4cyc_2I_4S], (instrs STPQpre)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S], (instrs STPSpost)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S], (instrs STPSpre)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S], (instrs STPWpost)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S], (instrs STPWpre)>;
def : InstRW<[A57Write_2cyc_2S], (instrs STPXi)>;
def : InstRW<[WriteAdr, A57Write_2cyc_1I_2S], (instrs STPXpost)>;
def : InstRW<[WriteAdr, A57Write_2cyc_1I_2S], (instrs STPXpre)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S, ReadAdrBase], (instrs STRBBpost)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S, ReadAdrBase], (instrs STRBBpre)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S, ReadAdrBase], (instrs STRBpost)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S], (instrs STRBpre)>;
def : InstRW<[A57Write_3cyc_1I_1S, ReadAdrBase], (instrs STRBroW)>;
def : InstRW<[A57Write_3cyc_1I_1S, ReadAdrBase], (instrs STRBroX)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S, ReadAdrBase], (instrs STRDpost)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S], (instrs STRDpre)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S, ReadAdrBase], (instrs STRHHpost)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S, ReadAdrBase], (instrs STRHHpre)>;
def : InstRW<[A57Write_3cyc_1I_1S, ReadAdrBase], (instrs STRHHroW)>;
def : InstRW<[A57Write_3cyc_1I_1S, ReadAdrBase], (instrs STRHHroX)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S, ReadAdrBase], (instrs STRHpost)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S], (instrs STRHpre)>;
def : InstRW<[A57Write_3cyc_1I_1S, ReadAdrBase], (instrs STRHroW)>;
def : InstRW<[A57Write_3cyc_1I_1S, ReadAdrBase], (instrs STRHroX)>;
def : InstRW<[WriteAdr, A57Write_2cyc_1I_2S, ReadAdrBase], (instrs STRQpost)>;
def : InstRW<[WriteAdr, A57Write_2cyc_1I_2S], (instrs STRQpre)>;
def : InstRW<[A57Write_2cyc_1I_2S, ReadAdrBase], (instrs STRQroW)>;
def : InstRW<[A57Write_2cyc_1I_2S, ReadAdrBase], (instrs STRQroX)>;
def : InstRW<[A57Write_2cyc_1I_2S], (instrs STRQui)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S, ReadAdrBase], (instrs STRSpost)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S], (instrs STRSpre)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S, ReadAdrBase], (instrs STRWpost)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S, ReadAdrBase], (instrs STRWpre)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S, ReadAdrBase], (instrs STRXpost)>;
def : InstRW<[WriteAdr, A57Write_1cyc_1I_1S, ReadAdrBase], (instrs STRXpre)>;
def : InstRW<[A57Write_2cyc_2S], (instrs STURQi)>;

} // SchedModel = CortexA57Model

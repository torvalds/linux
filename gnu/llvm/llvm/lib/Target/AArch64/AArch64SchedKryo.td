//==- AArch64SchedKryo.td - Qualcomm Kryo Scheduling Defs ---*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Qualcomm Kryo to support
// instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// The issue width is set to five, matching the five issue queues for expanded
// uops. Now, the latency spreadsheet has information based on fragmented uops,
// but these do not actually take up an issue queue.

def KryoModel : SchedMachineModel {
  let IssueWidth        =   5; // 5-wide issue for expanded uops
  let MicroOpBufferSize = 128; // Out-of-order with temporary unified issue buffer
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
  // FIXME: Remove when all errors have been fixed.
  let FullInstRWOverlapCheck = 0;
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available on Kryo.

let SchedModel = KryoModel in {
  def KryoUnitXA : ProcResource<1>;                   // Type X(A) micro-ops
  def KryoUnitXB : ProcResource<1>;                   // Type X(B) micro-ops
  def KryoUnitYA : ProcResource<1>;                   // Type Y(A) micro-ops
  def KryoUnitYB : ProcResource<1>;                   // Type Y(B) micro-ops
  def KryoUnitX : ProcResGroup<[KryoUnitXA,          // Type X micro-ops
                                KryoUnitXB]>;
  def KryoUnitY : ProcResGroup<[KryoUnitYA,          // Type Y micro-ops
                                KryoUnitYB]>;
  def KryoUnitXY : ProcResGroup<[KryoUnitXA,         // Type XY micro-ops
                                 KryoUnitXB,
                                 KryoUnitYA,
                                 KryoUnitYB]>;
  def KryoUnitLSA : ProcResource<1>;                  // Type LS(A) micro-ops
  def KryoUnitLSB : ProcResource<1>;                  // Type LS(B) micro-ops
  def KryoUnitLS : ProcResGroup<[KryoUnitLSA,        // Type LS micro-ops
                                 KryoUnitLSB]>;
}

let SchedModel = KryoModel in {

//===----------------------------------------------------------------------===//
// Map the target-defined scheduler read/write resources and latency for
// Kryo.

def : WriteRes<WriteImm,   [KryoUnitXY]> { let Latency = 1; }
def : WriteRes<WriteI,     [KryoUnitXY]> { let Latency = 1; }
def : WriteRes<WriteISReg, [KryoUnitXY, KryoUnitXY]>
      { let Latency = 2; let NumMicroOps = 2; }
def : WriteRes<WriteIEReg, [KryoUnitXY, KryoUnitXY]>
      { let Latency = 2; let NumMicroOps = 2; }
def : WriteRes<WriteExtr,  [KryoUnitXY, KryoUnitX]>
      { let Latency = 2; let NumMicroOps = 2; }
def : WriteRes<WriteIS,    [KryoUnitXY]> { let Latency = 2; }
def : WriteRes<WriteID32,  [KryoUnitXA, KryoUnitY]>
      { let Latency = 8; let NumMicroOps = 1; } // Fragent -1
def : WriteRes<WriteID64,  [KryoUnitXA, KryoUnitY]>
      { let Latency = 8; let NumMicroOps = 1; } // Fragent -1
def : WriteRes<WriteIM32,  [KryoUnitX]> { let Latency = 5; }
def : WriteRes<WriteIM64,  [KryoUnitX]> { let Latency = 5; }
def : WriteRes<WriteBr,    [KryoUnitXY]> { let Latency = 1; }
def : WriteRes<WriteBrReg, [KryoUnitXY]> { let Latency = 1; }
def : WriteRes<WriteLD,    [KryoUnitLS]> { let Latency = 4; }
def : WriteRes<WriteST,    [KryoUnitLS]> { let Latency = 4; }
def : WriteRes<WriteSTP,   [KryoUnitLS]> { let Latency = 4; }
def : WriteRes<WriteAdr,   [KryoUnitXY]> { let Latency = 6; }
def : WriteRes<WriteLDIdx, [KryoUnitLS]> { let Latency = 4; }
def : WriteRes<WriteSTIdx, [KryoUnitLS]> { let Latency = 4; }
def : WriteRes<WriteF,     [KryoUnitXY, KryoUnitXY]>
      { let Latency = 3; let NumMicroOps = 2; }
def : WriteRes<WriteFCmp,  [KryoUnitXY]> { let Latency = 2; }
def : WriteRes<WriteFCvt,  [KryoUnitX]> { let Latency = 4; }
def : WriteRes<WriteFCopy, [KryoUnitXY]> { let Latency = 6; }
def : WriteRes<WriteFImm,  [KryoUnitXY]> { let Latency = 6; }
def : WriteRes<WriteFMul,  [KryoUnitX, KryoUnitX]>
      { let Latency = 6; let NumMicroOps = 2; }
def : WriteRes<WriteFDiv,  [KryoUnitXA, KryoUnitY]>
      { let Latency = 12; let NumMicroOps = 2; } // Fragent -1 / NoRSV +1
def : WriteRes<WriteVd,    [KryoUnitXY]> { let Latency = 6; }
def : WriteRes<WriteVq,    [KryoUnitXY]> { let Latency = 6; }
def : WriteRes<WriteVLD,   [KryoUnitLS]> { let Latency = 4; }
def : WriteRes<WriteVST,   [KryoUnitLS]> { let Latency = 4; }

def : WriteRes<WriteSys,     []> { let Latency = 1; }
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }

def : WriteRes<WriteLDHi,    []> { let Latency = 4; }

def : WriteRes<WriteAtomic, []> { let Unsupported = 1; }

// No forwarding logic is modelled yet.
def : ReadAdvance<ReadI,       0>;
def : ReadAdvance<ReadISReg,   0>;
def : ReadAdvance<ReadIEReg,   0>;
def : ReadAdvance<ReadIM,      0>;
def : ReadAdvance<ReadIMA,     0>;
def : ReadAdvance<ReadID,      0>;
def : ReadAdvance<ReadExtrHi,  0>;
def : ReadAdvance<ReadAdrBase, 0>;
def : ReadAdvance<ReadVLD,     0>;
def : ReadAdvance<ReadST,      0>;


//===----------------------------------------------------------------------===//
// Specialize the coarse model by associating instruction groups with the
// subtarget-defined types. As the modeled is refined, this will override most
// of the above SchedWriteRes and SchedAlias mappings.

// Miscellaneous
// -----------------------------------------------------------------------------

def : InstRW<[WriteI], (instrs COPY)>;


// Detailed Refinedments
// -----------------------------------------------------------------------------
include "AArch64SchedKryoDetails.td"


} // SchedModel = KryoModel

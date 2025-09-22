//==-- AArch64Schedule.td - AArch64 Scheduling Definitions -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Define TII for use in SchedVariant Predicates.
// const MachineInstr *MI and const TargetSchedModel *SchedModel
// are defined by default.
def : PredicateProlog<[{
  const AArch64InstrInfo *TII =
    static_cast<const AArch64InstrInfo*>(SchedModel->getInstrInfo());
  (void)TII;
}]>;

// AArch64 Scheduler Definitions

def WriteImm       : SchedWrite; // MOVN, MOVZ
// TODO: Provide variants for MOV32/64imm Pseudos that dynamically
// select the correct sequence of WriteImms.

def WriteI         : SchedWrite; // ALU
def WriteISReg     : SchedWrite; // ALU of Shifted-Reg
def WriteIEReg     : SchedWrite; // ALU of Extended-Reg
def ReadI          : SchedRead;  // ALU
def ReadISReg      : SchedRead;  // ALU of Shifted-Reg
def ReadIEReg      : SchedRead;  // ALU of Extended-Reg
def WriteExtr      : SchedWrite; // EXTR shifts a reg pair
def ReadExtrHi     : SchedRead;  // Read the high reg of the EXTR pair
def WriteIS        : SchedWrite; // Shift/Scale
def WriteID32      : SchedWrite; // 32-bit Divide
def WriteID64      : SchedWrite; // 64-bit Divide
def ReadID         : SchedRead;  // 32/64-bit Divide
def WriteIM32      : SchedWrite; // 32-bit Multiply
def WriteIM64      : SchedWrite; // 64-bit Multiply
def ReadIM         : SchedRead;  // 32/64-bit Multiply
def ReadIMA        : SchedRead;  // 32/64-bit Multiply Accumulate
def WriteBr        : SchedWrite; // Branch
def WriteBrReg     : SchedWrite; // Indirect Branch

def WriteLD        : SchedWrite; // Load from base addr plus immediate offset
def WriteST        : SchedWrite; // Store to base addr plus immediate offset
def WriteSTP       : SchedWrite; // Store a register pair.
def WriteAdr       : SchedWrite; // Address pre/post increment.

def WriteLDIdx : SchedWrite; // Load from a register index (maybe scaled).
def WriteSTIdx : SchedWrite; // Store to a register index (maybe scaled).
def ReadST     : SchedRead;  // Read the stored value.
def ReadAdrBase : SchedRead; // Read the base resister of a reg-offset LD/ST.

// Serialized two-level address load.
// EXAMPLE: LOADGot
def WriteLDAdr : WriteSequence<[WriteAdr, WriteLD]>;

// Serialized two-level address lookup.
// EXAMPLE: MOVaddr...
def WriteAdrAdr : WriteSequence<[WriteAdr, WriteAdr]>;

// The second register of a load-pair.
// LDP,LDPSW,LDNP,LDXP,LDAXP
def WriteLDHi : SchedWrite;

// Store-exclusive is a store followed by a dependent load.
def WriteSTX : WriteSequence<[WriteST, WriteLD]>;

def WriteSys     : SchedWrite; // Long, variable latency system ops.
def WriteBarrier : SchedWrite; // Memory barrier.
def WriteHint    : SchedWrite; // Hint instruction.

def WriteF       : SchedWrite; // General floating-point ops.
def WriteFCmp    : SchedWrite; // Floating-point compare.
def WriteFCvt    : SchedWrite; // Float conversion.
def WriteFCopy   : SchedWrite; // Float-int register copy.
def WriteFImm    : SchedWrite; // Floating-point immediate.
def WriteFMul    : SchedWrite; // Floating-point multiply.
def WriteFDiv    : SchedWrite; // Floating-point division.

def WriteVd  : SchedWrite; // 64bit Vector D ops.
def WriteVq  : SchedWrite; // 128bit Vector Q ops.
def WriteVLD : SchedWrite; // Vector loads.
def WriteVST : SchedWrite; // Vector stores.

def WriteAtomic : SchedWrite; // Atomic memory operations (CAS, Swap, LDOP)

// Read the unwritten lanes of the VLD's destination registers.
def ReadVLD : SchedRead;

// Sequential vector load and shuffle.
def WriteVLDShuffle     : WriteSequence<[WriteVLD, WriteVq]>;
def WriteVLDPairShuffle : WriteSequence<[WriteVLD, WriteVq, WriteVq]>;

// Store a shuffled vector.
def WriteVSTShuffle : WriteSequence<[WriteVq, WriteVST]>;
def WriteVSTPairShuffle : WriteSequence<[WriteVq, WriteVq, WriteVST]>;

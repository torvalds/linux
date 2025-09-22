//===-- X86SchedPredicates.td - X86 Scheduling Predicates --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines scheduling predicate definitions that are common to
// all X86 subtargets.
//
//===----------------------------------------------------------------------===//

// A predicate used to identify dependency-breaking instructions that clear the
// content of the destination register. Note that this predicate only checks if
// input registers are the same. This predicate doesn't make any assumptions on
// the expected instruction opcodes, because different processors may implement
// different zero-idioms.
def ZeroIdiomPredicate : CheckSameRegOperand<1, 2>;

// A predicate used to identify VPERM that have bits 3 and 7 of their mask set.
// On some processors, these VPERM instructions are zero-idioms.
def ZeroIdiomVPERMPredicate : CheckAll<[
  ZeroIdiomPredicate,
  CheckImmOperand<3, 0x88>
]>;

// A predicate used to check if a LEA instruction uses all three source
// operands: base, index, and offset.
def IsThreeOperandsLEAPredicate: CheckAll<[
  // isRegOperand(Base)
  CheckIsRegOperand<1>,
  CheckNot<CheckInvalidRegOperand<1>>,

  // isRegOperand(Index)
  CheckIsRegOperand<3>,
  CheckNot<CheckInvalidRegOperand<3>>,

  // hasLEAOffset(Offset)
  CheckAny<[
    CheckAll<[
      CheckIsImmOperand<4>,
      CheckNot<CheckZeroOperand<4>>
    ]>,
    CheckNonPortable<"MI.getOperand(4).isGlobal()">
  ]>
]>;

def LEACases : MCOpcodeSwitchCase<
    [LEA32r, LEA64r, LEA64_32r, LEA16r],
    MCReturnStatement<IsThreeOperandsLEAPredicate>
>;

// Used to generate the body of a TII member function.
def IsThreeOperandsLEABody :
    MCOpcodeSwitchStatement<[LEACases], MCReturnStatement<FalsePred>>;

// This predicate evaluates to true only if the input machine instruction is a
// 3-operands LEA.  Tablegen automatically generates a new method for it in
// X86GenInstrInfo.
def IsThreeOperandsLEAFn :
    TIIPredicate<"isThreeOperandsLEA", IsThreeOperandsLEABody>;

// A predicate to check for COND_A and COND_BE CMOVs which have an extra uop
// on recent Intel CPUs.
def IsCMOVArr_Or_CMOVBErr : CheckAny<[
  CheckImmOperand_s<3, "X86::COND_A">,
  CheckImmOperand_s<3, "X86::COND_BE">
]>;

def IsCMOVArm_Or_CMOVBErm : CheckAny<[
  CheckImmOperand_s<7, "X86::COND_A">,
  CheckImmOperand_s<7, "X86::COND_BE">
]>;

// A predicate to check for COND_A and COND_BE SETCCs which have an extra uop
// on recent Intel CPUs.
def IsSETAr_Or_SETBEr : CheckAny<[
  CheckImmOperand_s<1, "X86::COND_A">,
  CheckImmOperand_s<1, "X86::COND_BE">
]>;

def IsSETAm_Or_SETBEm : CheckAny<[
  CheckImmOperand_s<5, "X86::COND_A">,
  CheckImmOperand_s<5, "X86::COND_BE">
]>;

// A predicate used to check if an instruction has a LOCK prefix.
def CheckLockPrefix : CheckFunctionPredicate<
  "X86_MC::hasLockPrefix",
  "X86InstrInfo::hasLockPrefix"
>;

def IsRegRegCompareAndSwap_8 : CheckOpcode<[ CMPXCHG8rr ]>;

def IsRegMemCompareAndSwap_8 : CheckOpcode<[
  LCMPXCHG8, CMPXCHG8rm
]>;

def IsRegRegCompareAndSwap_16_32_64  : CheckOpcode<[
  CMPXCHG16rr, CMPXCHG32rr, CMPXCHG64rr
]>;

def IsRegMemCompareAndSwap_16_32_64  : CheckOpcode<[
  CMPXCHG16rm, CMPXCHG32rm, CMPXCHG64rm,
  LCMPXCHG16, LCMPXCHG32, LCMPXCHG64,
  LCMPXCHG8B, LCMPXCHG16B
]>;

def IsCompareAndSwap8B  : CheckOpcode<[ CMPXCHG8B, LCMPXCHG8B ]>;
def IsCompareAndSwap16B : CheckOpcode<[ CMPXCHG16B, LCMPXCHG16B ]>;

def IsRegMemCompareAndSwap  : CheckOpcode<
  !listconcat(
    IsRegMemCompareAndSwap_8.ValidOpcodes,
    IsRegMemCompareAndSwap_16_32_64.ValidOpcodes
  )>;

def IsRegRegCompareAndSwap  : CheckOpcode<
  !listconcat(
    IsRegRegCompareAndSwap_8.ValidOpcodes,
    IsRegRegCompareAndSwap_16_32_64.ValidOpcodes
  )>;

def IsAtomicCompareAndSwap_8 : CheckAll<[
  CheckLockPrefix,
  IsRegMemCompareAndSwap_8
]>;

def IsAtomicCompareAndSwap : CheckAll<[
  CheckLockPrefix,
  IsRegMemCompareAndSwap
]>;

def IsAtomicCompareAndSwap8B : CheckAll<[
  CheckLockPrefix,
  IsCompareAndSwap8B
]>;

def IsAtomicCompareAndSwap16B : CheckAll<[
  CheckLockPrefix,
  IsCompareAndSwap16B
]>;

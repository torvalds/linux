//===- CodeGen/Analysis.h - CodeGen LLVM IR Analysis Utilities --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares several CodeGen-specific LLVM IR analysis utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ANALYSIS_H
#define LLVM_CODEGEN_ANALYSIS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/IR/Instructions.h"

namespace llvm {
template <typename T> class SmallVectorImpl;
class GlobalValue;
class LLT;
class MachineBasicBlock;
class MachineFunction;
class TargetLoweringBase;
class TargetLowering;
class TargetMachine;
struct EVT;

/// Compute the linearized index of a member in a nested
/// aggregate/struct/array.
///
/// Given an LLVM IR aggregate type and a sequence of insertvalue or
/// extractvalue indices that identify a member, return the linearized index of
/// the start of the member, i.e the number of element in memory before the
/// sought one. This is disconnected from the number of bytes.
///
/// \param Ty is the type indexed by \p Indices.
/// \param Indices is an optional pointer in the indices list to the current
/// index.
/// \param IndicesEnd is the end of the indices list.
/// \param CurIndex is the current index in the recursion.
///
/// \returns \p CurIndex plus the linear index in \p Ty  the indices list.
unsigned ComputeLinearIndex(Type *Ty,
                            const unsigned *Indices,
                            const unsigned *IndicesEnd,
                            unsigned CurIndex = 0);

inline unsigned ComputeLinearIndex(Type *Ty,
                                   ArrayRef<unsigned> Indices,
                                   unsigned CurIndex = 0) {
  return ComputeLinearIndex(Ty, Indices.begin(), Indices.end(), CurIndex);
}

/// ComputeValueVTs - Given an LLVM IR type, compute a sequence of
/// EVTs that represent all the individual underlying
/// non-aggregate types that comprise it.
///
/// If Offsets is non-null, it points to a vector to be filled in
/// with the in-memory offsets of each of the individual values.
///
void ComputeValueVTs(const TargetLowering &TLI, const DataLayout &DL, Type *Ty,
                     SmallVectorImpl<EVT> &ValueVTs,
                     SmallVectorImpl<EVT> *MemVTs,
                     SmallVectorImpl<TypeSize> *Offsets = nullptr,
                     TypeSize StartingOffset = TypeSize::getZero());
void ComputeValueVTs(const TargetLowering &TLI, const DataLayout &DL, Type *Ty,
                     SmallVectorImpl<EVT> &ValueVTs,
                     SmallVectorImpl<EVT> *MemVTs,
                     SmallVectorImpl<uint64_t> *FixedOffsets,
                     uint64_t StartingOffset);

/// Variant of ComputeValueVTs that don't produce memory VTs.
inline void ComputeValueVTs(const TargetLowering &TLI, const DataLayout &DL,
                            Type *Ty, SmallVectorImpl<EVT> &ValueVTs,
                            SmallVectorImpl<TypeSize> *Offsets = nullptr,
                            TypeSize StartingOffset = TypeSize::getZero()) {
  ComputeValueVTs(TLI, DL, Ty, ValueVTs, nullptr, Offsets, StartingOffset);
}
inline void ComputeValueVTs(const TargetLowering &TLI, const DataLayout &DL,
                            Type *Ty, SmallVectorImpl<EVT> &ValueVTs,
                            SmallVectorImpl<uint64_t> *FixedOffsets,
                            uint64_t StartingOffset) {
  ComputeValueVTs(TLI, DL, Ty, ValueVTs, nullptr, FixedOffsets, StartingOffset);
}

/// computeValueLLTs - Given an LLVM IR type, compute a sequence of
/// LLTs that represent all the individual underlying
/// non-aggregate types that comprise it.
///
/// If Offsets is non-null, it points to a vector to be filled in
/// with the in-memory offsets of each of the individual values.
///
void computeValueLLTs(const DataLayout &DL, Type &Ty,
                      SmallVectorImpl<LLT> &ValueTys,
                      SmallVectorImpl<uint64_t> *Offsets = nullptr,
                      uint64_t StartingOffset = 0);

/// ExtractTypeInfo - Returns the type info, possibly bitcast, encoded in V.
GlobalValue *ExtractTypeInfo(Value *V);

/// getFCmpCondCode - Return the ISD condition code corresponding to
/// the given LLVM IR floating-point condition code.  This includes
/// consideration of global floating-point math flags.
///
ISD::CondCode getFCmpCondCode(FCmpInst::Predicate Pred);

/// getFCmpCodeWithoutNaN - Given an ISD condition code comparing floats,
/// return the equivalent code if we're allowed to assume that NaNs won't occur.
ISD::CondCode getFCmpCodeWithoutNaN(ISD::CondCode CC);

/// getICmpCondCode - Return the ISD condition code corresponding to
/// the given LLVM IR integer condition code.
ISD::CondCode getICmpCondCode(ICmpInst::Predicate Pred);

/// getICmpCondCode - Return the LLVM IR integer condition code
/// corresponding to the given ISD integer condition code.
ICmpInst::Predicate getICmpCondCode(ISD::CondCode Pred);

/// Test if the given instruction is in a position to be optimized
/// with a tail-call. This roughly means that it's in a block with
/// a return and there's nothing that needs to be scheduled
/// between it and the return.
///
/// This function only tests target-independent requirements.
bool isInTailCallPosition(const CallBase &Call, const TargetMachine &TM,
                          bool ReturnsFirstArg = false);

/// Test if given that the input instruction is in the tail call position, if
/// there is an attribute mismatch between the caller and the callee that will
/// inhibit tail call optimizations.
/// \p AllowDifferingSizes is an output parameter which, if forming a tail call
/// is permitted, determines whether it's permitted only if the size of the
/// caller's and callee's return types match exactly.
bool attributesPermitTailCall(const Function *F, const Instruction *I,
                              const ReturnInst *Ret,
                              const TargetLoweringBase &TLI,
                              bool *AllowDifferingSizes = nullptr);

/// Test if given that the input instruction is in the tail call position if the
/// return type or any attributes of the function will inhibit tail call
/// optimization.
bool returnTypeIsEligibleForTailCall(const Function *F, const Instruction *I,
                                     const ReturnInst *Ret,
                                     const TargetLoweringBase &TLI,
                                     bool ReturnsFirstArg = false);

/// Returns true if the parent of \p CI returns CI's first argument after
/// calling \p CI.
bool funcReturnsFirstArgOfCall(const CallInst &CI);

DenseMap<const MachineBasicBlock *, int>
getEHScopeMembership(const MachineFunction &MF);

} // End llvm namespace

#endif

//===- MemoryOpRemark.h - Memory operation remark analysis -*- C++ ------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provide more information about instructions that copy, move, or initialize
// memory, including those with a "auto-init" !annotation metadata.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_MEMORYOPREMARK_H
#define LLVM_TRANSFORMS_UTILS_MEMORYOPREMARK_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include <optional>

namespace llvm {

class CallInst;
class DataLayout;
class DiagnosticInfoIROptimization;
class Instruction;
class IntrinsicInst;
class Value;
class OptimizationRemarkEmitter;
class StoreInst;

// FIXME: Once we get to more remarks like this one, we need to re-evaluate how
// much of this logic should actually go into the remark emitter.
struct MemoryOpRemark {
  OptimizationRemarkEmitter &ORE;
  StringRef RemarkPass;
  const DataLayout &DL;
  const TargetLibraryInfo &TLI;

  MemoryOpRemark(OptimizationRemarkEmitter &ORE, StringRef RemarkPass,
                 const DataLayout &DL, const TargetLibraryInfo &TLI)
      : ORE(ORE), RemarkPass(RemarkPass), DL(DL), TLI(TLI) {}

  virtual ~MemoryOpRemark();

  /// \return true iff the instruction is understood by MemoryOpRemark.
  static bool canHandle(const Instruction *I, const TargetLibraryInfo &TLI);

  void visit(const Instruction *I);

protected:
  virtual std::string explainSource(StringRef Type) const;

  enum RemarkKind { RK_Store, RK_Unknown, RK_IntrinsicCall, RK_Call };
  virtual StringRef remarkName(RemarkKind RK) const;

  virtual DiagnosticKind diagnosticKind() const { return DK_OptimizationRemarkAnalysis; }

private:
  template<typename ...Ts>
  std::unique_ptr<DiagnosticInfoIROptimization> makeRemark(Ts... Args);

  /// Emit a remark using information from the store's destination, size, etc.
  void visitStore(const StoreInst &SI);
  /// Emit a generic auto-init remark.
  void visitUnknown(const Instruction &I);
  /// Emit a remark using information from known intrinsic calls.
  void visitIntrinsicCall(const IntrinsicInst &II);
  /// Emit a remark using information from known function calls.
  void visitCall(const CallInst &CI);

  /// Add callee information to a remark: whether it's known, the function name,
  /// etc.
  template <typename FTy>
  void visitCallee(FTy F, bool KnownLibCall, DiagnosticInfoIROptimization &R);
  /// Add operand information to a remark based on knowledge we have for known
  /// libcalls.
  void visitKnownLibCall(const CallInst &CI, LibFunc LF,
                         DiagnosticInfoIROptimization &R);
  /// Add the memory operation size to a remark.
  void visitSizeOperand(Value *V, DiagnosticInfoIROptimization &R);

  struct VariableInfo {
    std::optional<StringRef> Name;
    std::optional<uint64_t> Size;
    bool isEmpty() const { return !Name && !Size; }
  };
  /// Gather more information about \p V as a variable. This can be debug info,
  /// information from the alloca, etc. Since \p V can represent more than a
  /// single variable, they will all be added to the remark.
  void visitPtr(Value *V, bool IsSrc, DiagnosticInfoIROptimization &R);
  void visitVariable(const Value *V, SmallVectorImpl<VariableInfo> &Result);
};

/// Special case for -ftrivial-auto-var-init remarks.
struct AutoInitRemark : public MemoryOpRemark {
  AutoInitRemark(OptimizationRemarkEmitter &ORE, StringRef RemarkPass,
                 const DataLayout &DL, const TargetLibraryInfo &TLI)
      : MemoryOpRemark(ORE, RemarkPass, DL, TLI) {}

  /// \return true iff the instruction is understood by AutoInitRemark.
  static bool canHandle(const Instruction *I);

protected:
  std::string explainSource(StringRef Type) const override;
  StringRef remarkName(RemarkKind RK) const override;
  DiagnosticKind diagnosticKind() const override {
    return DK_OptimizationRemarkMissed;
  }
};

} // namespace llvm

#endif

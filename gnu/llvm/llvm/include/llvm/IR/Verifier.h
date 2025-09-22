//===- Verifier.h - LLVM IR Verifier ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the function verifier interface, that can be used for
// validation checking of input to the system, and for checking that
// transformations haven't done something bad.
//
// Note that this does not provide full 'java style' security and verifications,
// instead it just tries to ensure that code is well formed.
//
// To see what specifically is checked, look at the top of Verifier.cpp
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_VERIFIER_H
#define LLVM_IR_VERIFIER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/PassManager.h"
#include <utility>

namespace llvm {

class APInt;
class Function;
class FunctionPass;
class Instruction;
class MDNode;
class Module;
class raw_ostream;
struct VerifierSupport;

/// Verify that the TBAA Metadatas are valid.
class TBAAVerifier {
  VerifierSupport *Diagnostic = nullptr;

  /// Helper to diagnose a failure
  template <typename... Tys> void CheckFailed(Tys &&... Args);

  /// Cache of TBAA base nodes that have already been visited.  This cachce maps
  /// a node that has been visited to a pair (IsInvalid, BitWidth) where
  ///
  ///  \c IsInvalid is true iff the node is invalid.
  ///  \c BitWidth, if non-zero, is the bitwidth of the integer used to denoting
  ///    the offset of the access.  If zero, only a zero offset is allowed.
  ///
  /// \c BitWidth has no meaning if \c IsInvalid is true.
  using TBAABaseNodeSummary = std::pair<bool, unsigned>;
  DenseMap<const MDNode *, TBAABaseNodeSummary> TBAABaseNodes;

  /// Maps an alleged scalar TBAA node to a boolean that is true if the said
  /// TBAA node is a valid scalar TBAA node or false otherwise.
  DenseMap<const MDNode *, bool> TBAAScalarNodes;

  /// \name Helper functions used by \c visitTBAAMetadata.
  /// @{
  MDNode *getFieldNodeFromTBAABaseNode(Instruction &I, const MDNode *BaseNode,
                                       APInt &Offset, bool IsNewFormat);
  TBAAVerifier::TBAABaseNodeSummary verifyTBAABaseNode(Instruction &I,
                                                       const MDNode *BaseNode,
                                                       bool IsNewFormat);
  TBAABaseNodeSummary verifyTBAABaseNodeImpl(Instruction &I,
                                             const MDNode *BaseNode,
                                             bool IsNewFormat);

  bool isValidScalarTBAANode(const MDNode *MD);
  /// @}

public:
  TBAAVerifier(VerifierSupport *Diagnostic = nullptr)
      : Diagnostic(Diagnostic) {}
  /// Visit an instruction and return true if it is valid, return false if an
  /// invalid TBAA is attached.
  bool visitTBAAMetadata(Instruction &I, const MDNode *MD);
};

/// Check a function for errors, useful for use when debugging a
/// pass.
///
/// If there are no errors, the function returns false. If an error is found,
/// a message describing the error is written to OS (if non-null) and true is
/// returned.
bool verifyFunction(const Function &F, raw_ostream *OS = nullptr);

/// Check a module for errors.
///
/// If there are no errors, the function returns false. If an error is
/// found, a message describing the error is written to OS (if
/// non-null) and true is returned.
///
/// \return true if the module is broken. If BrokenDebugInfo is
/// supplied, DebugInfo verification failures won't be considered as
/// error and instead *BrokenDebugInfo will be set to true. Debug
/// info errors can be "recovered" from by stripping the debug info.
bool verifyModule(const Module &M, raw_ostream *OS = nullptr,
                  bool *BrokenDebugInfo = nullptr);

FunctionPass *createVerifierPass(bool FatalErrors = true);

/// Check a module for errors, and report separate error states for IR
/// and debug info errors.
class VerifierAnalysis : public AnalysisInfoMixin<VerifierAnalysis> {
  friend AnalysisInfoMixin<VerifierAnalysis>;

  static AnalysisKey Key;

public:
  struct Result {
    bool IRBroken, DebugInfoBroken;
  };

  Result run(Module &M, ModuleAnalysisManager &);
  Result run(Function &F, FunctionAnalysisManager &);
  static bool isRequired() { return true; }
};

/// Create a verifier pass.
///
/// Check a module or function for validity. This is essentially a pass wrapped
/// around the above verifyFunction and verifyModule routines and
/// functionality. When the pass detects a verification error it is always
/// printed to stderr, and by default they are fatal. You can override that by
/// passing \c false to \p FatalErrors.
///
/// Note that this creates a pass suitable for the legacy pass manager. It has
/// nothing to do with \c VerifierPass.
class VerifierPass : public PassInfoMixin<VerifierPass> {
  bool FatalErrors;

public:
  explicit VerifierPass(bool FatalErrors = true) : FatalErrors(FatalErrors) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // end namespace llvm

#endif // LLVM_IR_VERIFIER_H

//===-- BitcodeWriterPass.h - Bitcode writing pass --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides a bitcode writing pass.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITCODEWRITERPASS_H
#define LLVM_BITCODE_BITCODEWRITERPASS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;
class ModulePass;
class Pass;
class raw_ostream;

/// Create and return a pass that writes the module to the specified
/// ostream. Note that this pass is designed for use with the legacy pass
/// manager.
///
/// If \c ShouldPreserveUseListOrder, encode use-list order so it can be
/// reproduced when deserialized.
ModulePass *createBitcodeWriterPass(raw_ostream &Str,
                                    bool ShouldPreserveUseListOrder = false);

/// Check whether a pass is a BitcodeWriterPass.
bool isBitcodeWriterPass(Pass *P);

/// Pass for writing a module of IR out to a bitcode file.
///
/// Note that this is intended for use with the new pass manager. To construct
/// a pass for the legacy pass manager, use the function above.
class BitcodeWriterPass : public PassInfoMixin<BitcodeWriterPass> {
  raw_ostream &OS;
  bool ShouldPreserveUseListOrder;
  bool EmitSummaryIndex;
  bool EmitModuleHash;

public:
  /// Construct a bitcode writer pass around a particular output stream.
  ///
  /// If \c ShouldPreserveUseListOrder, encode use-list order so it can be
  /// reproduced when deserialized.
  ///
  /// If \c EmitSummaryIndex, emit the summary index (currently
  /// for use in ThinLTO optimization).
  explicit BitcodeWriterPass(raw_ostream &OS,
                             bool ShouldPreserveUseListOrder = false,
                             bool EmitSummaryIndex = false,
                             bool EmitModuleHash = false)
      : OS(OS), ShouldPreserveUseListOrder(ShouldPreserveUseListOrder),
  EmitSummaryIndex(EmitSummaryIndex), EmitModuleHash(EmitModuleHash) {}

  /// Run the bitcode writer pass, and output the module to the selected
  /// output stream.
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);

  static bool isRequired() { return true; }
};

}

#endif

//===-- EmbedBitcodePass.h - Embeds bitcode into global ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides a pass which clones the current module and runs the
/// provided pass pipeline on the clone. The optimized module is stored into a
/// global variable in the `.llvm.lto` section. Primarily, this pass is used
/// to support the FatLTO pipeline, but could be used to generate a bitcode
/// section for any arbitrary pass pipeline without changing the current module.
///
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_TRANSFORMS_IPO_EMBEDBITCODEPASS_H
#define LLVM_TRANSFORMS_IPO_EMBEDBITCODEPASS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;
class Pass;

struct EmbedBitcodeOptions {
  EmbedBitcodeOptions() : EmbedBitcodeOptions(false, false) {}
  EmbedBitcodeOptions(bool IsThinLTO, bool EmitLTOSummary)
      : IsThinLTO(IsThinLTO), EmitLTOSummary(EmitLTOSummary) {}
  bool IsThinLTO;
  bool EmitLTOSummary;
};

/// Pass embeds a copy of the module optimized with the provided pass pipeline
/// into a global variable.
class EmbedBitcodePass : public PassInfoMixin<EmbedBitcodePass> {
  bool IsThinLTO;
  bool EmitLTOSummary;

public:
  EmbedBitcodePass(EmbedBitcodeOptions Opts)
      : EmbedBitcodePass(Opts.IsThinLTO, Opts.EmitLTOSummary) {}
  EmbedBitcodePass(bool IsThinLTO, bool EmitLTOSummary)
      : IsThinLTO(IsThinLTO), EmitLTOSummary(EmitLTOSummary) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);

  static bool isRequired() { return true; }
};

} // end namespace llvm.

#endif

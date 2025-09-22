//===- DXILShaderFlags.cpp - DXIL Shader Flags helper objects -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains helper objects and APIs for working with DXIL
///       Shader Flags.
///
//===----------------------------------------------------------------------===//

#include "DXILShaderFlags.h"
#include "DirectX.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::dxil;

static void updateFlags(ComputedShaderFlags &Flags, const Instruction &I) {
  Type *Ty = I.getType();
  if (Ty->isDoubleTy()) {
    Flags.Doubles = true;
    switch (I.getOpcode()) {
    case Instruction::FDiv:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
      Flags.DX11_1_DoubleExtensions = true;
      break;
    }
  }
}

ComputedShaderFlags ComputedShaderFlags::computeFlags(Module &M) {
  ComputedShaderFlags Flags;
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        updateFlags(Flags, I);
  return Flags;
}

void ComputedShaderFlags::print(raw_ostream &OS) const {
  uint64_t FlagVal = (uint64_t) * this;
  OS << formatv("; Shader Flags Value: {0:x8}\n;\n", FlagVal);
  if (FlagVal == 0)
    return;
  OS << "; Note: shader requires additional functionality:\n";
#define SHADER_FEATURE_FLAG(FeatureBit, DxilModuleNum, FlagName, Str)          \
  if (FlagName)                                                                \
    (OS << ";").indent(7) << Str << "\n";
#include "llvm/BinaryFormat/DXContainerConstants.def"
  OS << "; Note: extra DXIL module flags:\n";
#define DXIL_MODULE_FLAG(DxilModuleBit, FlagName, Str)                         \
  if (FlagName)                                                                \
    (OS << ";").indent(7) << Str << "\n";
#include "llvm/BinaryFormat/DXContainerConstants.def"
  OS << ";\n";
}

AnalysisKey ShaderFlagsAnalysis::Key;

ComputedShaderFlags ShaderFlagsAnalysis::run(Module &M,
                                             ModuleAnalysisManager &AM) {
  return ComputedShaderFlags::computeFlags(M);
}

PreservedAnalyses ShaderFlagsAnalysisPrinter::run(Module &M,
                                                  ModuleAnalysisManager &AM) {
  ComputedShaderFlags Flags = AM.getResult<ShaderFlagsAnalysis>(M);
  Flags.print(OS);
  return PreservedAnalyses::all();
}

char ShaderFlagsAnalysisWrapper::ID = 0;

INITIALIZE_PASS(ShaderFlagsAnalysisWrapper, "dx-shader-flag-analysis",
                "DXIL Shader Flag Analysis", true, true)

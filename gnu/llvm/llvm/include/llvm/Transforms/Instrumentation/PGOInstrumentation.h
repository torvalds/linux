//===- Transforms/Instrumentation/PGOInstrumentation.h ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for IR based instrumentation passes (
/// (profile-gen, and profile-use).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_PGOINSTRUMENTATION_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_PGOINSTRUMENTATION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include <cstdint>
#include <string>

namespace llvm {

extern cl::opt<bool> DebugInfoCorrelate;

class Function;
class Instruction;
class Module;

namespace vfs {
class FileSystem;
} // namespace vfs

/// The instrumentation (profile-instr-gen) pass for IR based PGO.
// We use this pass to create COMDAT profile variables for context
// sensitive PGO (CSPGO). The reason to have a pass for this is CSPGO
// can be run after LTO/ThinLTO linking. Lld linker needs to see
// all the COMDAT variables before linking. So we have this pass
// always run before linking for CSPGO.
class PGOInstrumentationGenCreateVar
    : public PassInfoMixin<PGOInstrumentationGenCreateVar> {
public:
  PGOInstrumentationGenCreateVar(std::string CSInstrName = "",
                                 bool Sampling = false)
      : CSInstrName(CSInstrName), ProfileSampling(Sampling) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  std::string CSInstrName;
  bool ProfileSampling;
};

/// The instrumentation (profile-instr-gen) pass for IR based PGO.
class PGOInstrumentationGen : public PassInfoMixin<PGOInstrumentationGen> {
public:
  PGOInstrumentationGen(bool IsCS = false) : IsCS(IsCS) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  // If this is a context sensitive instrumentation.
  bool IsCS;
};

/// The profile annotation (profile-instr-use) pass for IR based PGO.
class PGOInstrumentationUse : public PassInfoMixin<PGOInstrumentationUse> {
public:
  PGOInstrumentationUse(std::string Filename = "",
                        std::string RemappingFilename = "", bool IsCS = false,
                        IntrusiveRefCntPtr<vfs::FileSystem> FS = nullptr);

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  std::string ProfileFileName;
  std::string ProfileRemappingFileName;
  // If this is a context sensitive instrumentation.
  bool IsCS;
  IntrusiveRefCntPtr<vfs::FileSystem> FS;
};

/// The indirect function call promotion pass.
class PGOIndirectCallPromotion : public PassInfoMixin<PGOIndirectCallPromotion> {
public:
  PGOIndirectCallPromotion(bool IsInLTO = false, bool SamplePGO = false)
      : InLTO(IsInLTO), SamplePGO(SamplePGO) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  bool InLTO;
  bool SamplePGO;
};

/// The profile size based optimization pass for memory intrinsics.
class PGOMemOPSizeOpt : public PassInfoMixin<PGOMemOPSizeOpt> {
public:
  PGOMemOPSizeOpt() = default;

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &MAM);
};

void setProfMetadata(Module *M, Instruction *TI, ArrayRef<uint64_t> EdgeCounts,
                     uint64_t MaxCount);

void setIrrLoopHeaderMetadata(Module *M, Instruction *TI, uint64_t Count);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_PGOINSTRUMENTATION_H

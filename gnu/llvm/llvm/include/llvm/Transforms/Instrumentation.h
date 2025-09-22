//===- Transforms/Instrumentation.h - Instrumentation passes ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines constructor functions for instrumentation passes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

namespace llvm {

class Triple;
class OptimizationRemarkEmitter;
class Comdat;
class CallBase;

/// Instrumentation passes often insert conditional checks into entry blocks.
/// Call this function before splitting the entry block to move instructions
/// that must remain in the entry block up before the split point. Static
/// allocas and llvm.localescape calls, for example, must remain in the entry
/// block.
BasicBlock::iterator PrepareToSplitEntryBlock(BasicBlock &BB,
                                              BasicBlock::iterator IP);

// Create a constant for Str so that we can pass it to the run-time lib.
GlobalVariable *createPrivateGlobalForString(Module &M, StringRef Str,
                                             bool AllowMerging,
                                             const char *NamePrefix = "");

// Returns F.getComdat() if it exists.
// Otherwise creates a new comdat, sets F's comdat, and returns it.
// Returns nullptr on failure.
Comdat *getOrCreateFunctionComdat(Function &F, Triple &T);

// Place global in a large section for x86-64 ELF binaries to mitigate
// relocation overflow pressure. This can be be used for metadata globals that
// aren't directly accessed by code, which has no performance impact.
void setGlobalVariableLargeSection(const Triple &TargetTriple,
                                   GlobalVariable &GV);

// Insert GCOV profiling instrumentation
struct GCOVOptions {
  static GCOVOptions getDefault();

  // Specify whether to emit .gcno files.
  bool EmitNotes;

  // Specify whether to modify the program to emit .gcda files when run.
  bool EmitData;

  // A four-byte version string. The meaning of a version string is described in
  // gcc's gcov-io.h
  char Version[4];

  // Add the 'noredzone' attribute to added runtime library calls.
  bool NoRedZone;

  // Use atomic profile counter increments.
  bool Atomic = false;

  // Regexes separated by a semi-colon to filter the files to instrument.
  std::string Filter;

  // Regexes separated by a semi-colon to filter the files to not instrument.
  std::string Exclude;
};

// The pgo-specific indirect call promotion function declared below is used by
// the pgo-driven indirect call promotion and sample profile passes. It's a
// wrapper around llvm::promoteCall, et al. that additionally computes !prof
// metadata. We place it in a pgo namespace so it's not confused with the
// generic utilities.
namespace pgo {

// Helper function that transforms CB (either an indirect-call instruction, or
// an invoke instruction , to a conditional call to F. This is like:
//     if (Inst.CalledValue == F)
//        F(...);
//     else
//        Inst(...);
//     end
// TotalCount is the profile count value that the instruction executes.
// Count is the profile count value that F is the target function.
// These two values are used to update the branch weight.
// If \p AttachProfToDirectCall is true, a prof metadata is attached to the
// new direct call to contain \p Count.
// Returns the promoted direct call instruction.
CallBase &promoteIndirectCall(CallBase &CB, Function *F, uint64_t Count,
                              uint64_t TotalCount, bool AttachProfToDirectCall,
                              OptimizationRemarkEmitter *ORE);
} // namespace pgo

/// Options for the frontend instrumentation based profiling pass.
struct InstrProfOptions {
  // Add the 'noredzone' attribute to added runtime library calls.
  bool NoRedZone = false;

  // Do counter register promotion
  bool DoCounterPromotion = false;

  // Use atomic profile counter increments.
  bool Atomic = false;

  // Use BFI to guide register promotion
  bool UseBFIInPromotion = false;

  // Use sampling to reduce the profile instrumentation runtime overhead.
  bool Sampling = false;

  // Name of the profile file to use as output
  std::string InstrProfileOutput;

  InstrProfOptions() = default;
};

// Create the variable for profile sampling.
void createProfileSamplingVar(Module &M);

// Options for sanitizer coverage instrumentation.
struct SanitizerCoverageOptions {
  enum Type {
    SCK_None = 0,
    SCK_Function,
    SCK_BB,
    SCK_Edge
  } CoverageType = SCK_None;
  bool IndirectCalls = false;
  bool TraceBB = false;
  bool TraceCmp = false;
  bool TraceDiv = false;
  bool TraceGep = false;
  bool Use8bitCounters = false;
  bool TracePC = false;
  bool TracePCGuard = false;
  bool Inline8bitCounters = false;
  bool InlineBoolFlag = false;
  bool PCTable = false;
  bool NoPrune = false;
  bool StackDepth = false;
  bool TraceLoads = false;
  bool TraceStores = false;
  bool CollectControlFlow = false;

  SanitizerCoverageOptions() = default;
};

/// Calculate what to divide by to scale counts.
///
/// Given the maximum count, calculate a divisor that will scale all the
/// weights to strictly less than std::numeric_limits<uint32_t>::max().
static inline uint64_t calculateCountScale(uint64_t MaxCount) {
  return MaxCount < std::numeric_limits<uint32_t>::max()
             ? 1
             : MaxCount / std::numeric_limits<uint32_t>::max() + 1;
}

/// Scale an individual branch count.
///
/// Scale a 64-bit weight down to 32-bits using \c Scale.
///
static inline uint32_t scaleBranchCount(uint64_t Count, uint64_t Scale) {
  uint64_t Scaled = Count / Scale;
  assert(Scaled <= std::numeric_limits<uint32_t>::max() && "overflow 32-bits");
  return Scaled;
}

// Use to ensure the inserted instrumentation has a DebugLocation; if none is
// attached to the source instruction, try to use a DILocation with offset 0
// scoped to surrounding function (if it has a DebugLocation).
//
// Some non-call instructions may be missing debug info, but when inserting
// instrumentation calls, some builds (e.g. LTO) want calls to have debug info
// if the enclosing function does.
struct InstrumentationIRBuilder : IRBuilder<> {
  static void ensureDebugInfo(IRBuilder<> &IRB, const Function &F) {
    if (IRB.getCurrentDebugLocation())
      return;
    if (DISubprogram *SP = F.getSubprogram())
      IRB.SetCurrentDebugLocation(DILocation::get(SP->getContext(), 0, 0, SP));
  }

  explicit InstrumentationIRBuilder(Instruction *IP) : IRBuilder<>(IP) {
    ensureDebugInfo(*this, *IP->getFunction());
  }
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_H

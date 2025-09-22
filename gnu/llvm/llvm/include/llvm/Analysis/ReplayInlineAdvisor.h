//===- ReplayInlineAdvisor.h - Replay Inline Advisor interface -*- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_ANALYSIS_REPLAYINLINEADVISOR_H
#define LLVM_ANALYSIS_REPLAYINLINEADVISOR_H

#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/InlineAdvisor.h"

namespace llvm {
class CallBase;
class Function;
class LLVMContext;
class Module;

struct CallSiteFormat {
  enum class Format : int {
    Line,
    LineColumn,
    LineDiscriminator,
    LineColumnDiscriminator
  };

  bool outputColumn() const {
    return OutputFormat == Format::LineColumn ||
           OutputFormat == Format::LineColumnDiscriminator;
  }

  bool outputDiscriminator() const {
    return OutputFormat == Format::LineDiscriminator ||
           OutputFormat == Format::LineColumnDiscriminator;
  }

  Format OutputFormat;
};

/// Replay Inliner Setup
struct ReplayInlinerSettings {
  enum class Scope : int { Function, Module };
  enum class Fallback : int { Original, AlwaysInline, NeverInline };

  StringRef ReplayFile;
  Scope ReplayScope;
  Fallback ReplayFallback;
  CallSiteFormat ReplayFormat;
};

/// Get call site location as a string with the given format
std::string formatCallSiteLocation(DebugLoc DLoc, const CallSiteFormat &Format);

std::unique_ptr<InlineAdvisor>
getReplayInlineAdvisor(Module &M, FunctionAnalysisManager &FAM,
                       LLVMContext &Context,
                       std::unique_ptr<InlineAdvisor> OriginalAdvisor,
                       const ReplayInlinerSettings &ReplaySettings,
                       bool EmitRemarks, InlineContext IC);

/// Replay inline advisor that uses optimization remarks from inlining of
/// previous build to guide current inlining. This is useful for inliner tuning.
class ReplayInlineAdvisor : public InlineAdvisor {
public:
  ReplayInlineAdvisor(Module &M, FunctionAnalysisManager &FAM,
                      LLVMContext &Context,
                      std::unique_ptr<InlineAdvisor> OriginalAdvisor,
                      const ReplayInlinerSettings &ReplaySettings,
                      bool EmitRemarks, InlineContext IC);
  std::unique_ptr<InlineAdvice> getAdviceImpl(CallBase &CB) override;
  bool areReplayRemarksLoaded() const { return HasReplayRemarks; }

private:
  bool hasInlineAdvice(Function &F) const {
    return (ReplaySettings.ReplayScope ==
            ReplayInlinerSettings::Scope::Module) ||
           CallersToReplay.contains(F.getName());
  }
  std::unique_ptr<InlineAdvisor> OriginalAdvisor;
  bool HasReplayRemarks = false;
  const ReplayInlinerSettings ReplaySettings;
  bool EmitRemarks = false;

  StringMap<bool> InlineSitesFromRemarks;
  StringSet<> CallersToReplay;
};
} // namespace llvm
#endif // LLVM_ANALYSIS_REPLAYINLINEADVISOR_H

//===-- Logger.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/FlowSensitive/Logger.h"
#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/TypeErasedDataflowAnalysis.h"
#include "llvm/Support/WithColor.h"

namespace clang::dataflow {

Logger &Logger::null() {
  struct NullLogger final : Logger {};
  static auto *Instance = new NullLogger();
  return *Instance;
}

namespace {
struct TextualLogger final : Logger {
  llvm::raw_ostream &OS;
  const CFG *CurrentCFG;
  const CFGBlock *CurrentBlock;
  const CFGElement *CurrentElement;
  unsigned CurrentElementIndex;
  bool ShowColors;
  llvm::DenseMap<const CFGBlock *, unsigned> VisitCount;
  TypeErasedDataflowAnalysis *CurrentAnalysis;

  TextualLogger(llvm::raw_ostream &OS)
      : OS(OS), ShowColors(llvm::WithColor::defaultAutoDetectFunction()(OS)) {}

  virtual void beginAnalysis(const AdornedCFG &ACFG,
                             TypeErasedDataflowAnalysis &Analysis) override {
    {
      llvm::WithColor Header(OS, llvm::raw_ostream::Colors::RED, /*Bold=*/true);
      OS << "=== Beginning data flow analysis ===\n";
    }
    auto &D = ACFG.getDecl();
    D.print(OS);
    OS << "\n";
    D.dump(OS);
    CurrentCFG = &ACFG.getCFG();
    CurrentCFG->print(OS, Analysis.getASTContext().getLangOpts(), ShowColors);
    CurrentAnalysis = &Analysis;
  }
  virtual void endAnalysis() override {
    llvm::WithColor Header(OS, llvm::raw_ostream::Colors::RED, /*Bold=*/true);
    unsigned Blocks = 0, Steps = 0;
    for (const auto &E : VisitCount) {
      ++Blocks;
      Steps += E.second;
    }
    llvm::errs() << "=== Finished analysis: " << Blocks << " blocks in "
                 << Steps << " total steps ===\n";
  }
  virtual void enterBlock(const CFGBlock &Block, bool PostVisit) override {
    unsigned Count = ++VisitCount[&Block];
    {
      llvm::WithColor Header(OS, llvm::raw_ostream::Colors::RED, /*Bold=*/true);
      OS << "=== Entering block B" << Block.getBlockID();
      if (PostVisit)
        OS << " (post-visit)";
      else
        OS << " (iteration " << Count << ")";
      OS << " ===\n";
    }
    Block.print(OS, CurrentCFG, CurrentAnalysis->getASTContext().getLangOpts(),
                ShowColors);
    CurrentBlock = &Block;
    CurrentElement = nullptr;
    CurrentElementIndex = 0;
  }
  virtual void enterElement(const CFGElement &Element) override {
    ++CurrentElementIndex;
    CurrentElement = &Element;
    {
      llvm::WithColor Subheader(OS, llvm::raw_ostream::Colors::CYAN,
                                /*Bold=*/true);
      OS << "Processing element B" << CurrentBlock->getBlockID() << "."
         << CurrentElementIndex << ": ";
      Element.dumpToStream(OS);
    }
  }
  void recordState(TypeErasedDataflowAnalysisState &State) override {
    {
      llvm::WithColor Subheader(OS, llvm::raw_ostream::Colors::CYAN,
                                /*Bold=*/true);
      OS << "Computed state for B" << CurrentBlock->getBlockID() << "."
         << CurrentElementIndex << ":\n";
    }
    // FIXME: currently the environment dump is verbose and unenlightening.
    // FIXME: dump the user-defined lattice, too.
    State.Env.dump(OS);
    OS << "\n";
  }
  void blockConverged() override {
    OS << "B" << CurrentBlock->getBlockID() << " has converged!\n";
  }
  virtual void logText(llvm::StringRef S) override { OS << S << "\n"; }
};
} // namespace

std::unique_ptr<Logger> Logger::textual(llvm::raw_ostream &OS) {
  return std::make_unique<TextualLogger>(OS);
}

} // namespace clang::dataflow

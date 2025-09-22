//===- AnalyzerOptions.cpp - Analysis Engine Options ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains special accessors for analyzer configuration options
// with string representations.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

using namespace clang;
using namespace ento;
using namespace llvm;

void AnalyzerOptions::printFormattedEntry(
    llvm::raw_ostream &Out,
    std::pair<StringRef, StringRef> EntryDescPair,
    size_t InitialPad, size_t EntryWidth, size_t MinLineWidth) {

  llvm::formatted_raw_ostream FOut(Out);

  const size_t PadForDesc = InitialPad + EntryWidth;

  FOut.PadToColumn(InitialPad) << EntryDescPair.first;
  // If the buffer's length is greater than PadForDesc, print a newline.
  if (FOut.getColumn() > PadForDesc)
    FOut << '\n';

  FOut.PadToColumn(PadForDesc);

  if (MinLineWidth == 0) {
    FOut << EntryDescPair.second;
    return;
  }

  for (char C : EntryDescPair.second) {
    if (FOut.getColumn() > MinLineWidth && C == ' ') {
      FOut << '\n';
      FOut.PadToColumn(PadForDesc);
      continue;
    }
    FOut << C;
  }
}

ExplorationStrategyKind
AnalyzerOptions::getExplorationStrategy() const {
  auto K =
      llvm::StringSwitch<std::optional<ExplorationStrategyKind>>(
          ExplorationStrategy)
          .Case("dfs", ExplorationStrategyKind::DFS)
          .Case("bfs", ExplorationStrategyKind::BFS)
          .Case("unexplored_first", ExplorationStrategyKind::UnexploredFirst)
          .Case("unexplored_first_queue",
                ExplorationStrategyKind::UnexploredFirstQueue)
          .Case("unexplored_first_location_queue",
                ExplorationStrategyKind::UnexploredFirstLocationQueue)
          .Case("bfs_block_dfs_contents",
                ExplorationStrategyKind::BFSBlockDFSContents)
          .Default(std::nullopt);
  assert(K && "User mode is invalid.");
  return *K;
}

CTUPhase1InliningKind AnalyzerOptions::getCTUPhase1Inlining() const {
  auto K = llvm::StringSwitch<std::optional<CTUPhase1InliningKind>>(
               CTUPhase1InliningMode)
               .Case("none", CTUPhase1InliningKind::None)
               .Case("small", CTUPhase1InliningKind::Small)
               .Case("all", CTUPhase1InliningKind::All)
               .Default(std::nullopt);
  assert(K && "CTU inlining mode is invalid.");
  return *K;
}

IPAKind AnalyzerOptions::getIPAMode() const {
  auto K = llvm::StringSwitch<std::optional<IPAKind>>(IPAMode)
               .Case("none", IPAK_None)
               .Case("basic-inlining", IPAK_BasicInlining)
               .Case("inlining", IPAK_Inlining)
               .Case("dynamic", IPAK_DynamicDispatch)
               .Case("dynamic-bifurcate", IPAK_DynamicDispatchBifurcate)
               .Default(std::nullopt);
  assert(K && "IPA Mode is invalid.");

  return *K;
}

bool
AnalyzerOptions::mayInlineCXXMemberFunction(
                                          CXXInlineableMemberKind Param) const {
  if (getIPAMode() < IPAK_Inlining)
    return false;

  auto K = llvm::StringSwitch<std::optional<CXXInlineableMemberKind>>(
               CXXMemberInliningMode)
               .Case("constructors", CIMK_Constructors)
               .Case("destructors", CIMK_Destructors)
               .Case("methods", CIMK_MemberFunctions)
               .Case("none", CIMK_None)
               .Default(std::nullopt);

  assert(K && "Invalid c++ member function inlining mode.");

  return *K >= Param;
}

StringRef AnalyzerOptions::getCheckerStringOption(StringRef CheckerName,
                                                  StringRef OptionName,
                                                  bool SearchInParents) const {
  assert(!CheckerName.empty() &&
         "Empty checker name! Make sure the checker object (including it's "
         "bases!) if fully initialized before calling this function!");

  ConfigTable::const_iterator E = Config.end();
  do {
    ConfigTable::const_iterator I =
        Config.find((Twine(CheckerName) + ":" + OptionName).str());
    if (I != E)
      return StringRef(I->getValue());
    size_t Pos = CheckerName.rfind('.');
    if (Pos == StringRef::npos)
      break;

    CheckerName = CheckerName.substr(0, Pos);
  } while (!CheckerName.empty() && SearchInParents);

  llvm_unreachable("Unknown checker option! Did you call getChecker*Option "
                   "with incorrect parameters? User input must've been "
                   "verified by CheckerRegistry.");

  return "";
}

StringRef AnalyzerOptions::getCheckerStringOption(const ento::CheckerBase *C,
                                                  StringRef OptionName,
                                                  bool SearchInParents) const {
  return getCheckerStringOption(
                           C->getTagDescription(), OptionName, SearchInParents);
}

bool AnalyzerOptions::getCheckerBooleanOption(StringRef CheckerName,
                                              StringRef OptionName,
                                              bool SearchInParents) const {
  auto Ret =
      llvm::StringSwitch<std::optional<bool>>(
          getCheckerStringOption(CheckerName, OptionName, SearchInParents))
          .Case("true", true)
          .Case("false", false)
          .Default(std::nullopt);

  assert(Ret &&
         "This option should be either 'true' or 'false', and should've been "
         "validated by CheckerRegistry!");

  return *Ret;
}

bool AnalyzerOptions::getCheckerBooleanOption(const ento::CheckerBase *C,
                                              StringRef OptionName,
                                              bool SearchInParents) const {
  return getCheckerBooleanOption(
             C->getTagDescription(), OptionName, SearchInParents);
}

int AnalyzerOptions::getCheckerIntegerOption(StringRef CheckerName,
                                             StringRef OptionName,
                                             bool SearchInParents) const {
  int Ret = 0;
  bool HasFailed = getCheckerStringOption(CheckerName, OptionName,
                                          SearchInParents)
                     .getAsInteger(0, Ret);
  assert(!HasFailed &&
         "This option should be numeric, and should've been validated by "
         "CheckerRegistry!");
  (void)HasFailed;
  return Ret;
}

int AnalyzerOptions::getCheckerIntegerOption(const ento::CheckerBase *C,
                                             StringRef OptionName,
                                             bool SearchInParents) const {
  return getCheckerIntegerOption(
                           C->getTagDescription(), OptionName, SearchInParents);
}

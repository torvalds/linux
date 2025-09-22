//===- CheckerRegistry.h - Maintains all available checkers -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/CheckerRegistryData.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "llvm/ADT/Twine.h"
#include <map>

using namespace clang;
using namespace ento;

//===----------------------------------------------------------------------===//
// Methods of CmdLineOption, PackageInfo and CheckerInfo.
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void CmdLineOption::dump() const {
  dumpToStream(llvm::errs());
}

LLVM_DUMP_METHOD void
CmdLineOption::dumpToStream(llvm::raw_ostream &Out) const {
  // The description can be just checked in Checkers.inc, the point here is to
  // debug whether we succeeded in parsing it.
  Out << OptionName << " (" << OptionType << ", "
      << (IsHidden ? "hidden, " : "") << DevelopmentStatus << ") default: \""
      << DefaultValStr;
}

static StringRef toString(StateFromCmdLine Kind) {
  switch (Kind) {
  case StateFromCmdLine::State_Disabled:
    return "Disabled";
  case StateFromCmdLine::State_Enabled:
    return "Enabled";
  case StateFromCmdLine::State_Unspecified:
    return "Unspecified";
  }
  llvm_unreachable("Unhandled StateFromCmdLine enum");
}

LLVM_DUMP_METHOD void CheckerInfo::dump() const { dumpToStream(llvm::errs()); }

LLVM_DUMP_METHOD void CheckerInfo::dumpToStream(llvm::raw_ostream &Out) const {
  // The description can be just checked in Checkers.inc, the point here is to
  // debug whether we succeeded in parsing it. Same with documentation uri.
  Out << FullName << " (" << toString(State) << (IsHidden ? ", hidden" : "")
      << ")\n";
  Out << "  Options:\n";
  for (const CmdLineOption &Option : CmdLineOptions) {
    Out << "    ";
    Option.dumpToStream(Out);
    Out << '\n';
  }
  Out << "  Dependencies:\n";
  for (const CheckerInfo *Dependency : Dependencies) {
    Out << "  " << Dependency->FullName << '\n';
  }
  Out << "  Weak dependencies:\n";
  for (const CheckerInfo *Dependency : WeakDependencies) {
    Out << "    " << Dependency->FullName << '\n';
  }
}

LLVM_DUMP_METHOD void PackageInfo::dump() const { dumpToStream(llvm::errs()); }

LLVM_DUMP_METHOD void PackageInfo::dumpToStream(llvm::raw_ostream &Out) const {
  Out << FullName << "\n";
  Out << "  Options:\n";
  for (const CmdLineOption &Option : CmdLineOptions) {
    Out << "    ";
    Option.dumpToStream(Out);
    Out << '\n';
  }
}

static constexpr char PackageSeparator = '.';

static bool isInPackage(const CheckerInfo &Checker, StringRef PackageName) {
  // Does the checker's full name have the package as a prefix?
  if (!Checker.FullName.starts_with(PackageName))
    return false;

  // Is the package actually just the name of a specific checker?
  if (Checker.FullName.size() == PackageName.size())
    return true;

  // Is the checker in the package (or a subpackage)?
  if (Checker.FullName[PackageName.size()] == PackageSeparator)
    return true;

  return false;
}

CheckerInfoListRange
CheckerRegistryData::getMutableCheckersForCmdLineArg(StringRef CmdLineArg) {
  auto It = checker_registry::binaryFind(Checkers, CmdLineArg);

  if (!isInPackage(*It, CmdLineArg))
    return {Checkers.end(), Checkers.end()};

  // See how large the package is.
  // If the package doesn't exist, assume the option refers to a single
  // checker.
  size_t Size = 1;
  llvm::StringMap<size_t>::const_iterator PackageSize =
      PackageSizes.find(CmdLineArg);

  if (PackageSize != PackageSizes.end())
    Size = PackageSize->getValue();

  return {It, It + Size};
}
//===----------------------------------------------------------------------===//
// Printing functions.
//===----------------------------------------------------------------------===//

void CheckerRegistryData::printCheckerWithDescList(
    const AnalyzerOptions &AnOpts, raw_ostream &Out,
    size_t MaxNameChars) const {
  // FIXME: Print available packages.

  Out << "CHECKERS:\n";

  // Find the maximum option length.
  size_t OptionFieldWidth = 0;
  for (const auto &Checker : Checkers) {
    // Limit the amount of padding we are willing to give up for alignment.
    //   Package.Name     Description  [Hidden]
    size_t NameLength = Checker.FullName.size();
    if (NameLength <= MaxNameChars)
      OptionFieldWidth = std::max(OptionFieldWidth, NameLength);
  }

  const size_t InitialPad = 2;

  auto Print = [=](llvm::raw_ostream &Out, const CheckerInfo &Checker,
                   StringRef Description) {
    AnalyzerOptions::printFormattedEntry(Out, {Checker.FullName, Description},
                                         InitialPad, OptionFieldWidth);
    Out << '\n';
  };

  for (const auto &Checker : Checkers) {
    // The order of this if branches is significant, we wouldn't like to display
    // developer checkers even in the alpha output. For example,
    // alpha.cplusplus.IteratorModeling is a modeling checker, hence it's hidden
    // by default, and users (even when the user is a developer of an alpha
    // checker) shouldn't normally tinker with whether they should be enabled.

    if (Checker.IsHidden) {
      if (AnOpts.ShowCheckerHelpDeveloper)
        Print(Out, Checker, Checker.Desc);
      continue;
    }

    if (Checker.FullName.starts_with("alpha")) {
      if (AnOpts.ShowCheckerHelpAlpha)
        Print(Out, Checker,
              ("(Enable only for development!) " + Checker.Desc).str());
      continue;
    }

    if (AnOpts.ShowCheckerHelp)
      Print(Out, Checker, Checker.Desc);
  }
}

void CheckerRegistryData::printEnabledCheckerList(raw_ostream &Out) const {
  for (const auto *i : EnabledCheckers)
    Out << i->FullName << '\n';
}

void CheckerRegistryData::printCheckerOptionList(const AnalyzerOptions &AnOpts,
                                                 raw_ostream &Out) const {
  Out << "OVERVIEW: Clang Static Analyzer Checker and Package Option List\n\n";
  Out << "USAGE: -analyzer-config <OPTION1=VALUE,OPTION2=VALUE,...>\n\n";
  Out << "       -analyzer-config OPTION1=VALUE, -analyzer-config "
         "OPTION2=VALUE, ...\n\n";
  Out << "OPTIONS:\n\n";

  // It's usually ill-advised to use multimap, but clang will terminate after
  // this function.
  std::multimap<StringRef, const CmdLineOption &> OptionMap;

  for (const CheckerInfo &Checker : Checkers) {
    for (const CmdLineOption &Option : Checker.CmdLineOptions) {
      OptionMap.insert({Checker.FullName, Option});
    }
  }

  for (const PackageInfo &Package : Packages) {
    for (const CmdLineOption &Option : Package.CmdLineOptions) {
      OptionMap.insert({Package.FullName, Option});
    }
  }

  auto Print = [](llvm::raw_ostream &Out, StringRef FullOption,
                  StringRef Desc) {
    AnalyzerOptions::printFormattedEntry(Out, {FullOption, Desc},
                                         /*InitialPad*/ 2,
                                         /*EntryWidth*/ 50,
                                         /*MinLineWidth*/ 90);
    Out << "\n\n";
  };
  for (const std::pair<const StringRef, const CmdLineOption &> &Entry :
       OptionMap) {
    const CmdLineOption &Option = Entry.second;
    std::string FullOption = (Entry.first + ":" + Option.OptionName).str();

    std::string Desc =
        ("(" + Option.OptionType + ") " + Option.Description + " (default: " +
         (Option.DefaultValStr.empty() ? "\"\"" : Option.DefaultValStr) + ")")
            .str();

    // The list of these if branches is significant, we wouldn't like to
    // display hidden alpha checker options for
    // -analyzer-checker-option-help-alpha.

    if (Option.IsHidden) {
      if (AnOpts.ShowCheckerOptionDeveloperList)
        Print(Out, FullOption, Desc);
      continue;
    }

    if (Option.DevelopmentStatus == "alpha" ||
        Entry.first.starts_with("alpha")) {
      if (AnOpts.ShowCheckerOptionAlphaList)
        Print(Out, FullOption,
              llvm::Twine("(Enable only for development!) " + Desc).str());
      continue;
    }

    if (AnOpts.ShowCheckerOptionList)
      Print(Out, FullOption, Desc);
  }
}

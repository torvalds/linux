//===- CheckerRegistryData.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the data structures to which the TableGen file Checkers.td
// maps to, as well as what was parsed from the the specific invocation (whether
// a checker/package is enabled, their options values, etc).
//
// The parsing of the invocation is done by CheckerRegistry, which is found in
// the Frontend library. This allows the Core and Checkers libraries to utilize
// this information, such as enforcing rules on checker dependency bug emission,
// ensuring all checker options were queried, etc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_CHECKERREGISTRYDATA_H
#define LLVM_CLANG_STATICANALYZER_CORE_CHECKERREGISTRYDATA_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

namespace clang {

class AnalyzerOptions;

namespace ento {

class CheckerManager;

/// Initialization functions perform any necessary setup for a checker.
/// They should include a call to CheckerManager::registerChecker.
using RegisterCheckerFn = void (*)(CheckerManager &);
using ShouldRegisterFunction = bool (*)(const CheckerManager &);

/// Specifies a command line option. It may either belong to a checker or a
/// package.
struct CmdLineOption {
  StringRef OptionType;
  StringRef OptionName;
  StringRef DefaultValStr;
  StringRef Description;
  StringRef DevelopmentStatus;
  bool IsHidden;

  CmdLineOption(StringRef OptionType, StringRef OptionName,
                StringRef DefaultValStr, StringRef Description,
                StringRef DevelopmentStatus, bool IsHidden)
      : OptionType(OptionType), OptionName(OptionName),
        DefaultValStr(DefaultValStr), Description(Description),
        DevelopmentStatus(DevelopmentStatus), IsHidden(IsHidden) {

    assert((OptionType == "bool" || OptionType == "string" ||
            OptionType == "int") &&
           "Unknown command line option type!");

    assert((OptionType != "bool" ||
            (DefaultValStr == "true" || DefaultValStr == "false")) &&
           "Invalid value for boolean command line option! Maybe incorrect "
           "parameters to the addCheckerOption or addPackageOption method?");

    int Tmp;
    assert((OptionType != "int" || !DefaultValStr.getAsInteger(0, Tmp)) &&
           "Invalid value for integer command line option! Maybe incorrect "
           "parameters to the addCheckerOption or addPackageOption method?");
    (void)Tmp;

    assert((DevelopmentStatus == "alpha" || DevelopmentStatus == "beta" ||
            DevelopmentStatus == "released") &&
           "Invalid development status!");
  }

  LLVM_DUMP_METHOD void dump() const;
  LLVM_DUMP_METHOD void dumpToStream(llvm::raw_ostream &Out) const;
};

using CmdLineOptionList = llvm::SmallVector<CmdLineOption, 0>;

struct CheckerInfo;

using CheckerInfoList = std::vector<CheckerInfo>;
using CheckerInfoListRange = llvm::iterator_range<CheckerInfoList::iterator>;
using ConstCheckerInfoList = llvm::SmallVector<const CheckerInfo *, 0>;
using CheckerInfoSet = llvm::SetVector<const CheckerInfo *>;

/// Specifies a checker. Note that this isn't what we call a checker object,
/// it merely contains everything required to create one.
struct CheckerInfo {
  enum class StateFromCmdLine {
    // This checker wasn't explicitly enabled or disabled.
    State_Unspecified,
    // This checker was explicitly disabled.
    State_Disabled,
    // This checker was explicitly enabled.
    State_Enabled
  };

  RegisterCheckerFn Initialize = nullptr;
  ShouldRegisterFunction ShouldRegister = nullptr;
  StringRef FullName;
  StringRef Desc;
  StringRef DocumentationUri;
  CmdLineOptionList CmdLineOptions;
  bool IsHidden = false;
  StateFromCmdLine State = StateFromCmdLine::State_Unspecified;

  ConstCheckerInfoList Dependencies;
  ConstCheckerInfoList WeakDependencies;

  bool isEnabled(const CheckerManager &mgr) const {
    return State == StateFromCmdLine::State_Enabled && ShouldRegister(mgr);
  }

  bool isDisabled(const CheckerManager &mgr) const {
    return State == StateFromCmdLine::State_Disabled || !ShouldRegister(mgr);
  }

  // Since each checker must have a different full name, we can identify
  // CheckerInfo objects by them.
  bool operator==(const CheckerInfo &Rhs) const {
    return FullName == Rhs.FullName;
  }

  CheckerInfo(RegisterCheckerFn Fn, ShouldRegisterFunction sfn, StringRef Name,
              StringRef Desc, StringRef DocsUri, bool IsHidden)
      : Initialize(Fn), ShouldRegister(sfn), FullName(Name), Desc(Desc),
        DocumentationUri(DocsUri), IsHidden(IsHidden) {}

  // Used for lower_bound.
  explicit CheckerInfo(StringRef FullName) : FullName(FullName) {}

  LLVM_DUMP_METHOD void dump() const;
  LLVM_DUMP_METHOD void dumpToStream(llvm::raw_ostream &Out) const;
};

using StateFromCmdLine = CheckerInfo::StateFromCmdLine;

/// Specifies a package. Each package option is implicitly an option for all
/// checkers within the package.
struct PackageInfo {
  StringRef FullName;
  CmdLineOptionList CmdLineOptions;

  // Since each package must have a different full name, we can identify
  // CheckerInfo objects by them.
  bool operator==(const PackageInfo &Rhs) const {
    return FullName == Rhs.FullName;
  }

  explicit PackageInfo(StringRef FullName) : FullName(FullName) {}

  LLVM_DUMP_METHOD void dump() const;
  LLVM_DUMP_METHOD void dumpToStream(llvm::raw_ostream &Out) const;
};

using PackageInfoList = llvm::SmallVector<PackageInfo, 0>;

namespace checker_registry {

template <class T> struct FullNameLT {
  bool operator()(const T &Lhs, const T &Rhs) {
    return Lhs.FullName < Rhs.FullName;
  }
};

using PackageNameLT = FullNameLT<PackageInfo>;
using CheckerNameLT = FullNameLT<CheckerInfo>;

template <class CheckerOrPackageInfoList>
std::conditional_t<std::is_const<CheckerOrPackageInfoList>::value,
                   typename CheckerOrPackageInfoList::const_iterator,
                   typename CheckerOrPackageInfoList::iterator>
binaryFind(CheckerOrPackageInfoList &Collection, StringRef FullName) {

  using CheckerOrPackage = typename CheckerOrPackageInfoList::value_type;
  using CheckerOrPackageFullNameLT = FullNameLT<CheckerOrPackage>;

  assert(llvm::is_sorted(Collection, CheckerOrPackageFullNameLT{}) &&
         "In order to efficiently gather checkers/packages, this function "
         "expects them to be already sorted!");

  return llvm::lower_bound(Collection, CheckerOrPackage(FullName),
                           CheckerOrPackageFullNameLT{});
}
} // namespace checker_registry

struct CheckerRegistryData {
public:
  CheckerInfoSet EnabledCheckers;

  CheckerInfoList Checkers;
  PackageInfoList Packages;
  /// Used for counting how many checkers belong to a certain package in the
  /// \c Checkers field. For convenience purposes.
  llvm::StringMap<size_t> PackageSizes;

  /// Contains all (FullName, CmdLineOption) pairs. Similarly to dependencies,
  /// we only modify the actual CheckerInfo and PackageInfo objects once all
  /// of them have been added.
  llvm::SmallVector<std::pair<StringRef, CmdLineOption>, 0> PackageOptions;
  llvm::SmallVector<std::pair<StringRef, CmdLineOption>, 0> CheckerOptions;

  llvm::SmallVector<std::pair<StringRef, StringRef>, 0> Dependencies;
  llvm::SmallVector<std::pair<StringRef, StringRef>, 0> WeakDependencies;

  CheckerInfoListRange getMutableCheckersForCmdLineArg(StringRef CmdLineArg);

  /// Prints the name and description of all checkers in this registry.
  /// This output is not intended to be machine-parseable.
  void printCheckerWithDescList(const AnalyzerOptions &AnOpts, raw_ostream &Out,
                                size_t MaxNameChars = 30) const;
  void printEnabledCheckerList(raw_ostream &Out) const;
  void printCheckerOptionList(const AnalyzerOptions &AnOpts,
                              raw_ostream &Out) const;
};

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_CHECKERREGISTRYDATA_H

//===- CheckerRegistry.cpp - Maintains all available checkers -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace clang;
using namespace ento;
using namespace checker_registry;
using llvm::sys::DynamicLibrary;

//===----------------------------------------------------------------------===//
// Utilities.
//===----------------------------------------------------------------------===//

static bool isCompatibleAPIVersion(const char *VersionString) {
  // If the version string is null, its not an analyzer plugin.
  if (!VersionString)
    return false;

  // For now, none of the static analyzer API is considered stable.
  // Versions must match exactly.
  return strcmp(VersionString, CLANG_ANALYZER_API_VERSION_STRING) == 0;
}

static constexpr char PackageSeparator = '.';

//===----------------------------------------------------------------------===//
// Methods of CheckerRegistry.
//===----------------------------------------------------------------------===//

CheckerRegistry::CheckerRegistry(
    CheckerRegistryData &Data, ArrayRef<std::string> Plugins,
    DiagnosticsEngine &Diags, AnalyzerOptions &AnOpts,
    ArrayRef<std::function<void(CheckerRegistry &)>> CheckerRegistrationFns)
    : Data(Data), Diags(Diags), AnOpts(AnOpts) {

  // Register builtin checkers.
#define GET_CHECKERS
#define CHECKER(FULLNAME, CLASS, HELPTEXT, DOC_URI, IS_HIDDEN)                 \
  addChecker(register##CLASS, shouldRegister##CLASS, FULLNAME, HELPTEXT,       \
             DOC_URI, IS_HIDDEN);

#define GET_PACKAGES
#define PACKAGE(FULLNAME) addPackage(FULLNAME);

#include "clang/StaticAnalyzer/Checkers/Checkers.inc"
#undef CHECKER
#undef GET_CHECKERS
#undef PACKAGE
#undef GET_PACKAGES

  // Register checkers from plugins.
  for (const std::string &Plugin : Plugins) {
    // Get access to the plugin.
    std::string ErrorMsg;
    DynamicLibrary Lib =
        DynamicLibrary::getPermanentLibrary(Plugin.c_str(), &ErrorMsg);
    if (!Lib.isValid()) {
      Diags.Report(diag::err_fe_unable_to_load_plugin) << Plugin << ErrorMsg;
      continue;
    }

    // See if its compatible with this build of clang.
    const char *PluginAPIVersion = static_cast<const char *>(
        Lib.getAddressOfSymbol("clang_analyzerAPIVersionString"));

    if (!isCompatibleAPIVersion(PluginAPIVersion)) {
      Diags.Report(diag::warn_incompatible_analyzer_plugin_api)
          << llvm::sys::path::filename(Plugin);
      Diags.Report(diag::note_incompatible_analyzer_plugin_api)
          << CLANG_ANALYZER_API_VERSION_STRING << PluginAPIVersion;
      continue;
    }

    using RegisterPluginCheckerFn = void (*)(CheckerRegistry &);
    // Register its checkers.
    RegisterPluginCheckerFn RegisterPluginCheckers =
        reinterpret_cast<RegisterPluginCheckerFn>(
            Lib.getAddressOfSymbol("clang_registerCheckers"));
    if (RegisterPluginCheckers)
      RegisterPluginCheckers(*this);
  }

  // Register statically linked checkers, that aren't generated from the tblgen
  // file, but rather passed their registry function as a parameter in
  // checkerRegistrationFns.

  for (const auto &Fn : CheckerRegistrationFns)
    Fn(*this);

  // Sort checkers for efficient collection.
  // FIXME: Alphabetical sort puts 'experimental' in the middle.
  // Would it be better to name it '~experimental' or something else
  // that's ASCIIbetically last?
  llvm::sort(Data.Packages, checker_registry::PackageNameLT{});
  llvm::sort(Data.Checkers, checker_registry::CheckerNameLT{});

#define GET_CHECKER_DEPENDENCIES

#define CHECKER_DEPENDENCY(FULLNAME, DEPENDENCY)                               \
  addDependency(FULLNAME, DEPENDENCY);

#define GET_CHECKER_WEAK_DEPENDENCIES

#define CHECKER_WEAK_DEPENDENCY(FULLNAME, DEPENDENCY)                          \
  addWeakDependency(FULLNAME, DEPENDENCY);

#define GET_CHECKER_OPTIONS
#define CHECKER_OPTION(TYPE, FULLNAME, CMDFLAG, DESC, DEFAULT_VAL,             \
                       DEVELOPMENT_STATUS, IS_HIDDEN)                          \
  addCheckerOption(TYPE, FULLNAME, CMDFLAG, DEFAULT_VAL, DESC,                 \
                   DEVELOPMENT_STATUS, IS_HIDDEN);

#define GET_PACKAGE_OPTIONS
#define PACKAGE_OPTION(TYPE, FULLNAME, CMDFLAG, DESC, DEFAULT_VAL,             \
                       DEVELOPMENT_STATUS, IS_HIDDEN)                          \
  addPackageOption(TYPE, FULLNAME, CMDFLAG, DEFAULT_VAL, DESC,                 \
                   DEVELOPMENT_STATUS, IS_HIDDEN);

#include "clang/StaticAnalyzer/Checkers/Checkers.inc"
#undef CHECKER_DEPENDENCY
#undef GET_CHECKER_DEPENDENCIES
#undef CHECKER_WEAK_DEPENDENCY
#undef GET_CHECKER_WEAK_DEPENDENCIES
#undef CHECKER_OPTION
#undef GET_CHECKER_OPTIONS
#undef PACKAGE_OPTION
#undef GET_PACKAGE_OPTIONS

  resolveDependencies<true>();
  resolveDependencies<false>();

#ifndef NDEBUG
  for (auto &DepPair : Data.Dependencies) {
    for (auto &WeakDepPair : Data.WeakDependencies) {
      // Some assertions to enforce that strong dependencies are relations in
      // between purely modeling checkers, and weak dependencies are about
      // diagnostics.
      assert(WeakDepPair != DepPair &&
             "A checker cannot strong and weak depend on the same checker!");
      assert(WeakDepPair.first != DepPair.second &&
             "A strong dependency mustn't have weak dependencies!");
      assert(WeakDepPair.second != DepPair.second &&
             "A strong dependency mustn't be a weak dependency as well!");
    }
  }
#endif

  resolveCheckerAndPackageOptions();

  // Parse '-analyzer-checker' and '-analyzer-disable-checker' options from the
  // command line.
  for (const std::pair<std::string, bool> &Opt : AnOpts.CheckersAndPackages) {
    CheckerInfoListRange CheckerForCmdLineArg =
        Data.getMutableCheckersForCmdLineArg(Opt.first);

    if (CheckerForCmdLineArg.begin() == CheckerForCmdLineArg.end()) {
      Diags.Report(diag::err_unknown_analyzer_checker_or_package) << Opt.first;
      Diags.Report(diag::note_suggest_disabling_all_checkers);
    }

    for (CheckerInfo &checker : CheckerForCmdLineArg) {
      checker.State = Opt.second ? StateFromCmdLine::State_Enabled
                                 : StateFromCmdLine::State_Disabled;
    }
  }
  validateCheckerOptions();
}

//===----------------------------------------------------------------------===//
// Dependency resolving.
//===----------------------------------------------------------------------===//

template <typename IsEnabledFn>
static bool collectStrongDependencies(const ConstCheckerInfoList &Deps,
                                      const CheckerManager &Mgr,
                                      CheckerInfoSet &Ret,
                                      IsEnabledFn IsEnabled);

/// Collects weak dependencies in \p enabledData.Checkers.
template <typename IsEnabledFn>
static void collectWeakDependencies(const ConstCheckerInfoList &Deps,
                                    const CheckerManager &Mgr,
                                    CheckerInfoSet &Ret, IsEnabledFn IsEnabled);

void CheckerRegistry::initializeRegistry(const CheckerManager &Mgr) {
  // First, we calculate the list of enabled checkers as specified by the
  // invocation. Weak dependencies will not enable their unspecified strong
  // depenencies, but its only after resolving strong dependencies for all
  // checkers when we know whether they will be enabled.
  CheckerInfoSet Tmp;
  auto IsEnabledFromCmdLine = [&](const CheckerInfo *Checker) {
    return !Checker->isDisabled(Mgr);
  };
  for (const CheckerInfo &Checker : Data.Checkers) {
    if (!Checker.isEnabled(Mgr))
      continue;

    CheckerInfoSet Deps;
    if (!collectStrongDependencies(Checker.Dependencies, Mgr, Deps,
                                   IsEnabledFromCmdLine)) {
      // If we failed to enable any of the dependencies, don't enable this
      // checker.
      continue;
    }

    Tmp.insert(Deps.begin(), Deps.end());

    // Enable the checker.
    Tmp.insert(&Checker);
  }

  // Calculate enabled checkers with the correct registration order. As this is
  // done recursively, its arguably cheaper, but for sure less error prone to
  // recalculate from scratch.
  auto IsEnabled = [&](const CheckerInfo *Checker) {
    return Tmp.contains(Checker);
  };
  for (const CheckerInfo &Checker : Data.Checkers) {
    if (!Checker.isEnabled(Mgr))
      continue;

    CheckerInfoSet Deps;

    collectWeakDependencies(Checker.WeakDependencies, Mgr, Deps, IsEnabled);

    if (!collectStrongDependencies(Checker.Dependencies, Mgr, Deps,
                                   IsEnabledFromCmdLine)) {
      // If we failed to enable any of the dependencies, don't enable this
      // checker.
      continue;
    }

    // Note that set_union also preserves the order of insertion.
    Data.EnabledCheckers.set_union(Deps);
    Data.EnabledCheckers.insert(&Checker);
  }
}

template <typename IsEnabledFn>
static bool collectStrongDependencies(const ConstCheckerInfoList &Deps,
                                      const CheckerManager &Mgr,
                                      CheckerInfoSet &Ret,
                                      IsEnabledFn IsEnabled) {

  for (const CheckerInfo *Dependency : Deps) {
    if (!IsEnabled(Dependency))
      return false;

    // Collect dependencies recursively.
    if (!collectStrongDependencies(Dependency->Dependencies, Mgr, Ret,
                                   IsEnabled))
      return false;
    Ret.insert(Dependency);
  }

  return true;
}

template <typename IsEnabledFn>
static void collectWeakDependencies(const ConstCheckerInfoList &WeakDeps,
                                    const CheckerManager &Mgr,
                                    CheckerInfoSet &Ret,
                                    IsEnabledFn IsEnabled) {

  for (const CheckerInfo *Dependency : WeakDeps) {
    // Don't enable this checker if strong dependencies are unsatisfied, but
    // assume that weak dependencies are transitive.
    collectWeakDependencies(Dependency->WeakDependencies, Mgr, Ret, IsEnabled);

    if (IsEnabled(Dependency) &&
        collectStrongDependencies(Dependency->Dependencies, Mgr, Ret,
                                  IsEnabled))
      Ret.insert(Dependency);
  }
}

template <bool IsWeak> void CheckerRegistry::resolveDependencies() {
  for (const std::pair<StringRef, StringRef> &Entry :
       (IsWeak ? Data.WeakDependencies : Data.Dependencies)) {

    auto CheckerIt = binaryFind(Data.Checkers, Entry.first);
    assert(CheckerIt != Data.Checkers.end() &&
           CheckerIt->FullName == Entry.first &&
           "Failed to find the checker while attempting to set up its "
           "dependencies!");

    auto DependencyIt = binaryFind(Data.Checkers, Entry.second);
    assert(DependencyIt != Data.Checkers.end() &&
           DependencyIt->FullName == Entry.second &&
           "Failed to find the dependency of a checker!");

    // We do allow diagnostics from unit test/example dependency checkers.
    assert((DependencyIt->FullName.starts_with("test") ||
            DependencyIt->FullName.starts_with("example") || IsWeak ||
            DependencyIt->IsHidden) &&
           "Strong dependencies are modeling checkers, and as such "
           "non-user facing! Mark them hidden in Checkers.td!");

    if (IsWeak)
      CheckerIt->WeakDependencies.emplace_back(&*DependencyIt);
    else
      CheckerIt->Dependencies.emplace_back(&*DependencyIt);
  }
}

void CheckerRegistry::addDependency(StringRef FullName, StringRef Dependency) {
  Data.Dependencies.emplace_back(FullName, Dependency);
}

void CheckerRegistry::addWeakDependency(StringRef FullName,
                                        StringRef Dependency) {
  Data.WeakDependencies.emplace_back(FullName, Dependency);
}

//===----------------------------------------------------------------------===//
// Checker option resolving and validating.
//===----------------------------------------------------------------------===//

/// Insert the checker/package option to AnalyzerOptions' config table, and
/// validate it, if the user supplied it on the command line.
static void insertAndValidate(StringRef FullName, const CmdLineOption &Option,
                              AnalyzerOptions &AnOpts,
                              DiagnosticsEngine &Diags) {

  std::string FullOption = (FullName + ":" + Option.OptionName).str();

  auto It =
      AnOpts.Config.insert({FullOption, std::string(Option.DefaultValStr)});

  // Insertation was successful -- CmdLineOption's constructor will validate
  // whether values received from plugins or TableGen files are correct.
  if (It.second)
    return;

  // Insertion failed, the user supplied this package/checker option on the
  // command line. If the supplied value is invalid, we'll restore the option
  // to it's default value, and if we're in non-compatibility mode, we'll also
  // emit an error.

  StringRef SuppliedValue = It.first->getValue();

  if (Option.OptionType == "bool") {
    if (SuppliedValue != "true" && SuppliedValue != "false") {
      if (AnOpts.ShouldEmitErrorsOnInvalidConfigValue) {
        Diags.Report(diag::err_analyzer_checker_option_invalid_input)
            << FullOption << "a boolean value";
      }

      It.first->setValue(std::string(Option.DefaultValStr));
    }
    return;
  }

  if (Option.OptionType == "int") {
    int Tmp;
    bool HasFailed = SuppliedValue.getAsInteger(0, Tmp);
    if (HasFailed) {
      if (AnOpts.ShouldEmitErrorsOnInvalidConfigValue) {
        Diags.Report(diag::err_analyzer_checker_option_invalid_input)
            << FullOption << "an integer value";
      }

      It.first->setValue(std::string(Option.DefaultValStr));
    }
    return;
  }
}

template <class T>
static void insertOptionToCollection(StringRef FullName, T &Collection,
                                     const CmdLineOption &Option,
                                     AnalyzerOptions &AnOpts,
                                     DiagnosticsEngine &Diags) {
  auto It = binaryFind(Collection, FullName);
  assert(It != Collection.end() &&
         "Failed to find the checker while attempting to add a command line "
         "option to it!");

  insertAndValidate(FullName, Option, AnOpts, Diags);

  It->CmdLineOptions.emplace_back(Option);
}

void CheckerRegistry::resolveCheckerAndPackageOptions() {
  for (const std::pair<StringRef, CmdLineOption> &CheckerOptEntry :
       Data.CheckerOptions) {
    insertOptionToCollection(CheckerOptEntry.first, Data.Checkers,
                             CheckerOptEntry.second, AnOpts, Diags);
  }

  for (const std::pair<StringRef, CmdLineOption> &PackageOptEntry :
       Data.PackageOptions) {
    insertOptionToCollection(PackageOptEntry.first, Data.Packages,
                             PackageOptEntry.second, AnOpts, Diags);
  }
}

void CheckerRegistry::addPackage(StringRef FullName) {
  Data.Packages.emplace_back(PackageInfo(FullName));
}

void CheckerRegistry::addPackageOption(StringRef OptionType,
                                       StringRef PackageFullName,
                                       StringRef OptionName,
                                       StringRef DefaultValStr,
                                       StringRef Description,
                                       StringRef DevelopmentStatus,
                                       bool IsHidden) {
  Data.PackageOptions.emplace_back(
      PackageFullName, CmdLineOption{OptionType, OptionName, DefaultValStr,
                                     Description, DevelopmentStatus, IsHidden});
}

void CheckerRegistry::addChecker(RegisterCheckerFn Rfn,
                                 ShouldRegisterFunction Sfn, StringRef Name,
                                 StringRef Desc, StringRef DocsUri,
                                 bool IsHidden) {
  Data.Checkers.emplace_back(Rfn, Sfn, Name, Desc, DocsUri, IsHidden);

  // Record the presence of the checker in its packages.
  StringRef PackageName, LeafName;
  std::tie(PackageName, LeafName) = Name.rsplit(PackageSeparator);
  while (!LeafName.empty()) {
    Data.PackageSizes[PackageName] += 1;
    std::tie(PackageName, LeafName) = PackageName.rsplit(PackageSeparator);
  }
}

void CheckerRegistry::addCheckerOption(StringRef OptionType,
                                       StringRef CheckerFullName,
                                       StringRef OptionName,
                                       StringRef DefaultValStr,
                                       StringRef Description,
                                       StringRef DevelopmentStatus,
                                       bool IsHidden) {
  Data.CheckerOptions.emplace_back(
      CheckerFullName, CmdLineOption{OptionType, OptionName, DefaultValStr,
                                     Description, DevelopmentStatus, IsHidden});
}

void CheckerRegistry::initializeManager(CheckerManager &CheckerMgr) const {
  // Initialize the CheckerManager with all enabled checkers.
  for (const auto *Checker : Data.EnabledCheckers) {
    CheckerMgr.setCurrentCheckerName(CheckerNameRef(Checker->FullName));
    Checker->Initialize(CheckerMgr);
  }
}

static void isOptionContainedIn(const CmdLineOptionList &OptionList,
                                StringRef SuppliedChecker,
                                StringRef SuppliedOption,
                                const AnalyzerOptions &AnOpts,
                                DiagnosticsEngine &Diags) {

  if (!AnOpts.ShouldEmitErrorsOnInvalidConfigValue)
    return;

  auto SameOptName = [SuppliedOption](const CmdLineOption &Opt) {
    return Opt.OptionName == SuppliedOption;
  };

  if (llvm::none_of(OptionList, SameOptName)) {
    Diags.Report(diag::err_analyzer_checker_option_unknown)
        << SuppliedChecker << SuppliedOption;
    return;
  }
}

void CheckerRegistry::validateCheckerOptions() const {
  for (const auto &Config : AnOpts.Config) {

    StringRef SuppliedCheckerOrPackage;
    StringRef SuppliedOption;
    std::tie(SuppliedCheckerOrPackage, SuppliedOption) =
        Config.getKey().split(':');

    if (SuppliedOption.empty())
      continue;

    // AnalyzerOptions' config table contains the user input, so an entry could
    // look like this:
    //
    //   cor:NoFalsePositives=true
    //
    // Since lower_bound would look for the first element *not less* than "cor",
    // it would return with an iterator to the first checker in the core, so we
    // we really have to use find here, which uses operator==.
    auto CheckerIt =
        llvm::find(Data.Checkers, CheckerInfo(SuppliedCheckerOrPackage));
    if (CheckerIt != Data.Checkers.end()) {
      isOptionContainedIn(CheckerIt->CmdLineOptions, SuppliedCheckerOrPackage,
                          SuppliedOption, AnOpts, Diags);
      continue;
    }

    const auto *PackageIt =
        llvm::find(Data.Packages, PackageInfo(SuppliedCheckerOrPackage));
    if (PackageIt != Data.Packages.end()) {
      isOptionContainedIn(PackageIt->CmdLineOptions, SuppliedCheckerOrPackage,
                          SuppliedOption, AnOpts, Diags);
      continue;
    }

    Diags.Report(diag::err_unknown_analyzer_checker_or_package)
        << SuppliedCheckerOrPackage;
  }
}

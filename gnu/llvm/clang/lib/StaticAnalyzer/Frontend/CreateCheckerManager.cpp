//===- CheckerManager.h - Static Analyzer Checker Manager -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the Static Analyzer Checker Manager.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include <memory>

namespace clang {
namespace ento {

CheckerManager::CheckerManager(
    ASTContext &Context, AnalyzerOptions &AOptions, const Preprocessor &PP,
    ArrayRef<std::string> plugins,
    ArrayRef<std::function<void(CheckerRegistry &)>> checkerRegistrationFns)
    : Context(&Context), LangOpts(Context.getLangOpts()), AOptions(AOptions),
      PP(&PP), Diags(Context.getDiagnostics()),
      RegistryData(std::make_unique<CheckerRegistryData>()) {
  CheckerRegistry Registry(*RegistryData, plugins, Context.getDiagnostics(),
                           AOptions, checkerRegistrationFns);
  Registry.initializeRegistry(*this);
  Registry.initializeManager(*this);
  finishedCheckerRegistration();
}

CheckerManager::CheckerManager(AnalyzerOptions &AOptions,
                               const LangOptions &LangOpts,
                               DiagnosticsEngine &Diags,
                               ArrayRef<std::string> plugins)
    : LangOpts(LangOpts), AOptions(AOptions), Diags(Diags),
      RegistryData(std::make_unique<CheckerRegistryData>()) {
  CheckerRegistry Registry(*RegistryData, plugins, Diags, AOptions, {});
  Registry.initializeRegistry(*this);
}

CheckerManager::~CheckerManager() {
  for (const auto &CheckerDtor : CheckerDtors)
    CheckerDtor();
}

} // namespace ento
} // namespace clang

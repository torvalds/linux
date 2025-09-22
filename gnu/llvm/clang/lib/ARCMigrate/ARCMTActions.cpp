//===--- ARCMTActions.cpp - ARC Migrate Tool Frontend Actions ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/ARCMigrate/ARCMTActions.h"
#include "clang/ARCMigrate/ARCMT.h"
#include "clang/Frontend/CompilerInstance.h"

using namespace clang;
using namespace arcmt;

bool CheckAction::BeginInvocation(CompilerInstance &CI) {
  if (arcmt::checkForManualIssues(CI.getInvocation(), getCurrentInput(),
                                  CI.getPCHContainerOperations(),
                                  CI.getDiagnostics().getClient()))
    return false; // errors, stop the action.

  // We only want to see warnings reported from arcmt::checkForManualIssues.
  CI.getDiagnostics().setIgnoreAllWarnings(true);
  return true;
}

CheckAction::CheckAction(std::unique_ptr<FrontendAction> WrappedAction)
  : WrapperFrontendAction(std::move(WrappedAction)) {}

bool ModifyAction::BeginInvocation(CompilerInstance &CI) {
  return !arcmt::applyTransformations(CI.getInvocation(), getCurrentInput(),
                                      CI.getPCHContainerOperations(),
                                      CI.getDiagnostics().getClient());
}

ModifyAction::ModifyAction(std::unique_ptr<FrontendAction> WrappedAction)
  : WrapperFrontendAction(std::move(WrappedAction)) {}

bool MigrateAction::BeginInvocation(CompilerInstance &CI) {
  if (arcmt::migrateWithTemporaryFiles(
          CI.getInvocation(), getCurrentInput(), CI.getPCHContainerOperations(),
          CI.getDiagnostics().getClient(), MigrateDir, EmitPremigrationARCErrors,
          PlistOut))
    return false; // errors, stop the action.

  // We only want to see diagnostics emitted by migrateWithTemporaryFiles.
  CI.getDiagnostics().setIgnoreAllWarnings(true);
  return true;
}

MigrateAction::MigrateAction(std::unique_ptr<FrontendAction> WrappedAction,
                             StringRef migrateDir,
                             StringRef plistOut,
                             bool emitPremigrationARCErrors)
  : WrapperFrontendAction(std::move(WrappedAction)), MigrateDir(migrateDir),
    PlistOut(plistOut), EmitPremigrationARCErrors(emitPremigrationARCErrors) {
  if (MigrateDir.empty())
    MigrateDir = "."; // user current directory if none is given.
}

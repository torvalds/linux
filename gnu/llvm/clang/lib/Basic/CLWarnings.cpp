//===--- CLWarnings.h - Maps some cl.exe warning ids  -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Diagnostic-related interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/CLWarnings.h"
#include "clang/Basic/DiagnosticCategories.h"
#include <optional>

using namespace clang;

std::optional<diag::Group>
clang::diagGroupFromCLWarningID(unsigned CLWarningID) {
  switch (CLWarningID) {
  case 4005: return diag::Group::MacroRedefined;
  case 4018: return diag::Group::SignCompare;
  case 4100: return diag::Group::UnusedParameter;
  case 4910: return diag::Group::DllexportExplicitInstantiationDecl;
  case 4996: return diag::Group::DeprecatedDeclarations;
  }
  return {};
}

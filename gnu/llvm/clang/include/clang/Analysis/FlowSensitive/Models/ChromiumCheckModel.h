//===-- ChromiumCheckModel.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a dataflow model for Chromium's family of CHECK functions.
//
//===----------------------------------------------------------------------===//
#ifndef CLANG_ANALYSIS_FLOWSENSITIVE_MODELS_CHROMIUMCHECKMODEL_H
#define CLANG_ANALYSIS_FLOWSENSITIVE_MODELS_CHROMIUMCHECKMODEL_H

#include "clang/AST/DeclCXX.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "llvm/ADT/DenseSet.h"

namespace clang {
namespace dataflow {

/// Models the behavior of Chromium's CHECK, DCHECK, etc. macros, so that code
/// after a call to `*CHECK` can rely on the condition being true.
class ChromiumCheckModel : public DataflowModel {
public:
  ChromiumCheckModel() = default;
  bool transfer(const CFGElement &Element, Environment &Env) override;

private:
  /// Declarations for `::logging::CheckError::.*Check`, lazily initialized.
  llvm::SmallDenseSet<const CXXMethodDecl *> CheckDecls;
};

} // namespace dataflow
} // namespace clang

#endif // CLANG_ANALYSIS_FLOWSENSITIVE_MODELS_CHROMIUMCHECKMODEL_H

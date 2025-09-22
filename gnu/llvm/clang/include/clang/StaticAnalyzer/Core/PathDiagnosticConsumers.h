//===--- PathDiagnosticConsumers.h - Path Diagnostic Clients ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the interface to create different path diagostic clients.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHDIAGNOSTICCONSUMERS_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHDIAGNOSTICCONSUMERS_H

#include "clang/Analysis/PathDiagnostic.h"

#include <string>
#include <vector>

namespace clang {

class MacroExpansionContext;
class Preprocessor;

namespace cross_tu {
class CrossTranslationUnitContext;
}

namespace ento {

class PathDiagnosticConsumer;
typedef std::vector<PathDiagnosticConsumer*> PathDiagnosticConsumers;

#define ANALYSIS_DIAGNOSTICS(NAME, CMDFLAG, DESC, CREATEFN)                    \
  void CREATEFN(PathDiagnosticConsumerOptions Diagopts,                        \
                PathDiagnosticConsumers &C, const std::string &Prefix,         \
                const Preprocessor &PP,                                        \
                const cross_tu::CrossTranslationUnitContext &CTU,              \
                const MacroExpansionContext &MacroExpansions);
#include "clang/StaticAnalyzer/Core/Analyses.def"

} // end 'ento' namespace
} // end 'clang' namespace

#endif

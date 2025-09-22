//===- LinkDiagnosticInfo.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_LINKER_LINK_DIAGNOSTIC_INFO_H
#define LLVM_LIB_LINKER_LINK_DIAGNOSTIC_INFO_H

#include "llvm/IR/DiagnosticInfo.h"

namespace llvm {
class LinkDiagnosticInfo : public DiagnosticInfo {
  const Twine &Msg;

public:
  LinkDiagnosticInfo(DiagnosticSeverity Severity, const Twine &Msg);
  void print(DiagnosticPrinter &DP) const override;
};
}

#endif

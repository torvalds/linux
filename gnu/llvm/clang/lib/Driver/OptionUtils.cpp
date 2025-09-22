//===--- OptionUtils.cpp - Utilities for command line arguments -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Driver/OptionUtils.h"
#include "llvm/Option/ArgList.h"

using namespace clang;
using namespace llvm::opt;

namespace {
template <typename IntTy>
IntTy getLastArgIntValueImpl(const ArgList &Args, OptSpecifier Id,
                             IntTy Default, DiagnosticsEngine *Diags,
                             unsigned Base) {
  IntTy Res = Default;
  if (Arg *A = Args.getLastArg(Id)) {
    if (StringRef(A->getValue()).getAsInteger(Base, Res)) {
      if (Diags)
        Diags->Report(diag::err_drv_invalid_int_value)
            << A->getAsString(Args) << A->getValue();
    }
  }
  return Res;
}
} // namespace

namespace clang {

int getLastArgIntValue(const ArgList &Args, OptSpecifier Id, int Default,
                       DiagnosticsEngine *Diags, unsigned Base) {
  return getLastArgIntValueImpl<int>(Args, Id, Default, Diags, Base);
}

uint64_t getLastArgUInt64Value(const ArgList &Args, OptSpecifier Id,
                               uint64_t Default, DiagnosticsEngine *Diags,
                               unsigned Base) {
  return getLastArgIntValueImpl<uint64_t>(Args, Id, Default, Diags, Base);
}

} // namespace clang

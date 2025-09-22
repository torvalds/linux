//===- OptionUtils.h - Utilities for command line arguments -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This header contains utilities for command line arguments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_OPTIONUTILS_H
#define LLVM_CLANG_DRIVER_OPTIONUTILS_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "llvm/Option/OptSpecifier.h"

namespace llvm {

namespace opt {

class ArgList;

} // namespace opt

} // namespace llvm

namespace clang {
/// Return the value of the last argument as an integer, or a default. If Diags
/// is non-null, emits an error if the argument is given, but non-integral.
int getLastArgIntValue(const llvm::opt::ArgList &Args,
                       llvm::opt::OptSpecifier Id, int Default,
                       DiagnosticsEngine *Diags = nullptr, unsigned Base = 0);

inline int getLastArgIntValue(const llvm::opt::ArgList &Args,
                              llvm::opt::OptSpecifier Id, int Default,
                              DiagnosticsEngine &Diags, unsigned Base = 0) {
  return getLastArgIntValue(Args, Id, Default, &Diags, Base);
}

uint64_t getLastArgUInt64Value(const llvm::opt::ArgList &Args,
                               llvm::opt::OptSpecifier Id, uint64_t Default,
                               DiagnosticsEngine *Diags = nullptr,
                               unsigned Base = 0);

inline uint64_t getLastArgUInt64Value(const llvm::opt::ArgList &Args,
                                      llvm::opt::OptSpecifier Id,
                                      uint64_t Default,
                                      DiagnosticsEngine &Diags,
                                      unsigned Base = 0) {
  return getLastArgUInt64Value(Args, Id, Default, &Diags, Base);
}

} // namespace clang

#endif // LLVM_CLANG_DRIVER_OPTIONUTILS_H

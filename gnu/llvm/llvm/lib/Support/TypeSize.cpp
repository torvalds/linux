//===- TypeSize.cpp - Wrapper around type sizes------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/TypeSize.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/WithColor.h"

#include "DebugOptions.h"

using namespace llvm;

#ifndef STRICT_FIXED_SIZE_VECTORS
namespace {
struct CreateScalableErrorAsWarning {
  /// The ScalableErrorAsWarning is a temporary measure to suppress errors from
  /// using the wrong interface on a scalable vector.
  static void *call() {
    return new cl::opt<bool>(
        "treat-scalable-fixed-error-as-warning", cl::Hidden,
        cl::desc(
            "Treat issues where a fixed-width property is requested from a "
            "scalable type as a warning, instead of an error"));
  }
};
} // namespace
static ManagedStatic<cl::opt<bool>, CreateScalableErrorAsWarning>
    ScalableErrorAsWarning;
void llvm::initTypeSizeOptions() { *ScalableErrorAsWarning; }
#else
void llvm::initTypeSizeOptions() {}
#endif

void llvm::reportInvalidSizeRequest(const char *Msg) {
#ifndef STRICT_FIXED_SIZE_VECTORS
  if (*ScalableErrorAsWarning) {
    WithColor::warning() << "Invalid size request on a scalable vector; " << Msg
                         << "\n";
    return;
  }
#endif
  report_fatal_error("Invalid size request on a scalable vector.");
}

TypeSize::operator TypeSize::ScalarTy() const {
  if (isScalable()) {
    reportInvalidSizeRequest(
        "Cannot implicitly convert a scalable size to a fixed-width size in "
        "`TypeSize::operator ScalarTy()`");
    return getKnownMinValue();
  }
  return getFixedValue();
}

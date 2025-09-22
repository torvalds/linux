//===--- LazyDetector.h - Lazy ToolChain Detection --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_LAZYDETECTOR_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_LAZYDETECTOR_H

#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"
#include <optional>

namespace clang {

/// Simple wrapper for toolchain detector with costly initialization. This
/// delays the creation of the actual detector until its first usage.

template <class T> class LazyDetector {
  const driver::Driver &D;
  llvm::Triple Triple;
  const llvm::opt::ArgList &Args;

  std::optional<T> Detector;

public:
  LazyDetector(const driver::Driver &D, const llvm::Triple &Triple,
               const llvm::opt::ArgList &Args)
      : D(D), Triple(Triple), Args(Args) {}
  T *operator->() {
    if (!Detector)
      Detector.emplace(D, Triple, Args);
    return &*Detector;
  }
  const T *operator->() const {
    return const_cast<T const *>(
        const_cast<LazyDetector &>(*this).operator->());
  }
};

} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_LAZYDETECTOR_H

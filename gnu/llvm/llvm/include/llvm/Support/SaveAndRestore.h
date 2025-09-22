//===-- SaveAndRestore.h - Utility  -------------------------------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides utility classes that use RAII to save and restore
/// values.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SAVEANDRESTORE_H
#define LLVM_SUPPORT_SAVEANDRESTORE_H

#include <utility>

namespace llvm {

/// A utility class that uses RAII to save and restore the value of a variable.
template <typename T> struct SaveAndRestore {
  SaveAndRestore(T &X) : X(X), OldValue(X) {}
  SaveAndRestore(T &X, const T &NewValue) : X(X), OldValue(X) { X = NewValue; }
  SaveAndRestore(T &X, T &&NewValue) : X(X), OldValue(std::move(X)) {
    X = std::move(NewValue);
  }
  ~SaveAndRestore() { X = std::move(OldValue); }
  const T &get() { return OldValue; }

private:
  T &X;
  T OldValue;
};

// User-defined CTAD guides.
template <typename T> SaveAndRestore(T &) -> SaveAndRestore<T>;
template <typename T> SaveAndRestore(T &, const T &) -> SaveAndRestore<T>;
template <typename T> SaveAndRestore(T &, T &&) -> SaveAndRestore<T>;

} // namespace llvm

#endif

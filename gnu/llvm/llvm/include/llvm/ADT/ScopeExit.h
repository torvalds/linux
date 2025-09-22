//===- llvm/ADT/ScopeExit.h - Execute code at scope exit --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the make_scope_exit function, which executes user-defined
/// cleanup logic at scope exit.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SCOPEEXIT_H
#define LLVM_ADT_SCOPEEXIT_H

#include "llvm/Support/Compiler.h"

#include <type_traits>
#include <utility>

namespace llvm {
namespace detail {

template <typename Callable> class scope_exit {
  Callable ExitFunction;
  bool Engaged = true; // False once moved-from or release()d.

public:
  template <typename Fp>
  explicit scope_exit(Fp &&F) : ExitFunction(std::forward<Fp>(F)) {}

  scope_exit(scope_exit &&Rhs)
      : ExitFunction(std::move(Rhs.ExitFunction)), Engaged(Rhs.Engaged) {
    Rhs.release();
  }
  scope_exit(const scope_exit &) = delete;
  scope_exit &operator=(scope_exit &&) = delete;
  scope_exit &operator=(const scope_exit &) = delete;

  void release() { Engaged = false; }

  ~scope_exit() {
    if (Engaged)
      ExitFunction();
  }
};

} // end namespace detail

// Keeps the callable object that is passed in, and execute it at the
// destruction of the returned object (usually at the scope exit where the
// returned object is kept).
//
// Interface is specified by p0052r2.
template <typename Callable>
[[nodiscard]] detail::scope_exit<std::decay_t<Callable>>
make_scope_exit(Callable &&F) {
  return detail::scope_exit<std::decay_t<Callable>>(std::forward<Callable>(F));
}

} // end namespace llvm

#endif

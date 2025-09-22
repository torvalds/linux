//===- FuzzerExtFunctions.h - Interface to external functions ---*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Defines an interface to (possibly optional) functions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_EXT_FUNCTIONS_H
#define LLVM_FUZZER_EXT_FUNCTIONS_H

#include <stddef.h>
#include <stdint.h>

namespace fuzzer {

struct ExternalFunctions {
  // Initialize function pointers. Functions that are not available will be set
  // to nullptr.  Do not call this constructor  before ``main()`` has been
  // entered.
  ExternalFunctions();

#define EXT_FUNC(NAME, RETURN_TYPE, FUNC_SIG, WARN)                            \
  RETURN_TYPE(*NAME) FUNC_SIG = nullptr

#include "FuzzerExtFunctions.def"

#undef EXT_FUNC
};
} // namespace fuzzer

#endif

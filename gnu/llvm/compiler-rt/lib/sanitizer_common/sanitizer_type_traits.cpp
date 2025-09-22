//===-- sanitizer_type_traits.cpp -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements a subset of C++ type traits. This is so we can avoid depending
// on system C++ headers.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_type_traits.h"

namespace __sanitizer {

const bool true_type::value;
const bool false_type::value;

}  // namespace __sanitizer

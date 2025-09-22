//===- llvm/Support/CBindingWrapping.h - C Interface Wrapping ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the wrapping macros for the C interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CBINDINGWRAPPING_H
#define LLVM_SUPPORT_CBINDINGWRAPPING_H

#include "llvm-c/Types.h"
#include "llvm/Support/Casting.h"

#define DEFINE_SIMPLE_CONVERSION_FUNCTIONS(ty, ref)     \
  inline ty *unwrap(ref P) {                            \
    return reinterpret_cast<ty*>(P);                    \
  }                                                     \
                                                        \
  inline ref wrap(const ty *P) {                        \
    return reinterpret_cast<ref>(const_cast<ty*>(P));   \
  }

#define DEFINE_ISA_CONVERSION_FUNCTIONS(ty, ref)        \
  DEFINE_SIMPLE_CONVERSION_FUNCTIONS(ty, ref)           \
                                                        \
  template<typename T>                                  \
  inline T *unwrap(ref P) {                             \
    return cast<T>(unwrap(P));                          \
  }

#define DEFINE_STDCXX_CONVERSION_FUNCTIONS(ty, ref)     \
  DEFINE_SIMPLE_CONVERSION_FUNCTIONS(ty, ref)           \
                                                        \
  template<typename T>                                  \
  inline T *unwrap(ref P) {                             \
    T *Q = (T*)unwrap(P);                               \
    assert(Q && "Invalid cast!");                       \
    return Q;                                           \
  }

#endif

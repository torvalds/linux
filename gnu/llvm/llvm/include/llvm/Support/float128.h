//===-- llvm/Support/float128.h - Compiler abstraction support --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FLOAT128
#define LLVM_FLOAT128

namespace llvm {

#if defined(__clang__) && defined(__FLOAT128__) &&                             \
    defined(__SIZEOF_INT128__) && !defined(__LONG_DOUBLE_IBM128__)
#define HAS_IEE754_FLOAT128
typedef __float128 float128;
#elif defined(__FLOAT128__) && defined(__SIZEOF_INT128__) &&                   \
    !defined(__LONG_DOUBLE_IBM128__) &&                                        \
    (defined(__GNUC__) || defined(__GNUG__))
#define HAS_IEE754_FLOAT128
typedef _Float128 float128;
#endif

} // namespace llvm
#endif // LLVM_FLOAT128

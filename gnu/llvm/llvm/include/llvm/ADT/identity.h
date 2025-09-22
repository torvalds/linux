//===- llvm/ADT/Identity.h - Provide std::identity from C++20 ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an implementation of std::identity from C++20.
//
// No library is required when using these functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_IDENTITY_H
#define LLVM_ADT_IDENTITY_H


namespace llvm {

// Similar to `std::identity` from C++20.
template <class Ty> struct identity {
  using is_transparent = void;
  using argument_type = Ty;

  Ty &operator()(Ty &self) const {
    return self;
  }
  const Ty &operator()(const Ty &self) const {
    return self;
  }
};

} // end namespace llvm

#endif // LLVM_ADT_IDENTITY_H

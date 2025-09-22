// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBCXXRT
#  error this header may only be used when targeting libcxxrt
#endif

namespace std {

bad_exception::~bad_exception() noexcept {}

const char* bad_exception::what() const noexcept { return "std::bad_exception"; }

} // namespace std

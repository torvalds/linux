// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __GLIBCXX__
#  error header can only be used when targeting libstdc++ or libsupc++
#endif

namespace std {

bad_alloc::bad_alloc() noexcept {}

bad_array_new_length::bad_array_new_length() noexcept {}

bad_cast::bad_cast() noexcept {}

bad_typeid::bad_typeid() noexcept {}

} // namespace std

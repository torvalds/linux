//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_ABI_VCRUNTIME
#  error This file may only be used when deferring to vcruntime
#endif

namespace std {
logic_error::logic_error(std::string const& s) : exception(s.c_str()) {}
runtime_error::runtime_error(std::string const& s) : exception(s.c_str()) {}
} // namespace std

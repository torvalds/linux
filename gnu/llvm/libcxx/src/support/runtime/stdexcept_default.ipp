//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../../include/refstring.h"

/* For _LIBCPPABI_VERSION */
#if !defined(_LIBCPP_BUILDING_HAS_NO_ABI_LIBRARY) && (defined(LIBCXX_BUILDING_LIBCXXABI) || defined(LIBCXXRT))
#  include <cxxabi.h>
#endif

static_assert(sizeof(std::__libcpp_refstring) == sizeof(const char*), "");

namespace std // purposefully not using versioning namespace
{

logic_error::logic_error(const string& msg) : __imp_(msg.c_str()) {}

logic_error::logic_error(const char* msg) : __imp_(msg) {}

logic_error::logic_error(const logic_error& le) noexcept : __imp_(le.__imp_) {}

logic_error& logic_error::operator=(const logic_error& le) noexcept {
  __imp_ = le.__imp_;
  return *this;
}

runtime_error::runtime_error(const string& msg) : __imp_(msg.c_str()) {}

runtime_error::runtime_error(const char* msg) : __imp_(msg) {}

runtime_error::runtime_error(const runtime_error& re) noexcept : __imp_(re.__imp_) {}

runtime_error& runtime_error::operator=(const runtime_error& re) noexcept {
  __imp_ = re.__imp_;
  return *this;
}

#if !defined(_LIBCPPABI_VERSION) && !defined(LIBSTDCXX)

const char* logic_error::what() const noexcept { return __imp_.c_str(); }

const char* runtime_error::what() const noexcept { return __imp_.c_str(); }

logic_error::~logic_error() noexcept {}
domain_error::~domain_error() noexcept {}
invalid_argument::~invalid_argument() noexcept {}
length_error::~length_error() noexcept {}
out_of_range::~out_of_range() noexcept {}

runtime_error::~runtime_error() noexcept {}
range_error::~range_error() noexcept {}
overflow_error::~overflow_error() noexcept {}
underflow_error::~underflow_error() noexcept {}

#endif

} // namespace std

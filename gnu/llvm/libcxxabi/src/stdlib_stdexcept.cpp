//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "stdexcept"
#include "new"
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include "include/refstring.h" // from libc++

static_assert(sizeof(std::__libcpp_refstring) == sizeof(const char *), "");

namespace std  // purposefully not using versioning namespace
{

logic_error::~logic_error() noexcept {}

const char*
logic_error::what() const noexcept
{
    return __imp_.c_str();
}

runtime_error::~runtime_error() noexcept {}

const char*
runtime_error::what() const noexcept
{
    return __imp_.c_str();
}

domain_error::~domain_error() noexcept {}
invalid_argument::~invalid_argument() noexcept {}
length_error::~length_error() noexcept {}
out_of_range::~out_of_range() noexcept {}

range_error::~range_error() noexcept {}
overflow_error::~overflow_error() noexcept {}
underflow_error::~underflow_error() noexcept {}

}  // std

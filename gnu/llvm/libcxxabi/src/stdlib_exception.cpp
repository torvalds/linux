//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <new>
#include <exception>

namespace std
{

// exception

exception::~exception() noexcept
{
}

const char* exception::what() const noexcept
{
  return "std::exception";
}

// bad_exception

bad_exception::~bad_exception() noexcept
{
}

const char* bad_exception::what() const noexcept
{
  return "std::bad_exception";
}


//  bad_alloc

bad_alloc::bad_alloc() noexcept
{
}

bad_alloc::~bad_alloc() noexcept
{
}

const char*
bad_alloc::what() const noexcept
{
    return "std::bad_alloc";
}

// bad_array_new_length

bad_array_new_length::bad_array_new_length() noexcept
{
}

bad_array_new_length::~bad_array_new_length() noexcept
{
}

const char*
bad_array_new_length::what() const noexcept
{
    return "bad_array_new_length";
}

}  // std

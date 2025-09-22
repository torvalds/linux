//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <typeinfo>

namespace std
{

// type_info

type_info::~type_info()
{
}

// bad_cast

bad_cast::bad_cast() noexcept
{
}

bad_cast::~bad_cast() noexcept
{
}

const char*
bad_cast::what() const noexcept
{
  return "std::bad_cast";
}

// bad_typeid

bad_typeid::bad_typeid() noexcept
{
}

bad_typeid::~bad_typeid() noexcept
{
}

const char*
bad_typeid::what() const noexcept
{
  return "std::bad_typeid";
}

}  // std

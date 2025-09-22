// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  using std::max_align_t _LIBCPP_USING_IF_EXISTS;
  using std::nullptr_t;
  using std::ptrdiff_t _LIBCPP_USING_IF_EXISTS;
  using std::size_t _LIBCPP_USING_IF_EXISTS;

  using std::byte;

  // [support.types.byteops], byte type operations
  using std::operator<<=;
  using std::operator<<;
  using std::operator>>=;
  using std::operator>>;
  using std::operator|=;
  using std::operator|;
  using std::operator&=;
  using std::operator&;
  using std::operator^=;
  using std::operator^;
  using std::operator~;
  using std::to_integer;
} // namespace std

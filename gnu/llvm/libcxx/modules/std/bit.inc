// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [bit.cast], bit_cast
  using std::bit_cast;

#if _LIBCPP_STD_VER >= 23
  // [bit.byteswap], byteswap
  using std::byteswap;
#endif

  // [bit.pow.two], integral powers of 2
  using std::bit_ceil;
  using std::bit_floor;
  using std::bit_width;
  using std::has_single_bit;

  // [bit.rotate], rotating
  using std::rotl;
  using std::rotr;

  // [bit.count], counting
  using std::countl_one;
  using std::countl_zero;
  using std::countr_one;
  using std::countr_zero;
  using std::popcount;

  // [bit.endian], endian
  using std::endian;
} // namespace std

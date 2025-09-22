// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {

  // [array], class template array
  using std::array;

  using std::operator==;
  using std::operator<=>;

  // [array.special], specialized algorithms
  using std::swap;

  // [array.creation], array creation functions
  using std::to_array;

  // [array.tuple], tuple interface
  using std::get;
  using std::tuple_element;
  using std::tuple_size;

} // namespace std

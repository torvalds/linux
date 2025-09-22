// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [map], class template map
  using std::map;

  using std::operator==;
  using std::operator<=>;

  using std::swap;

  // [map.erasure], erasure for map
  using std::erase_if;

  // [multimap], class template multimap
  using std::multimap;

  namespace pmr {
    using std::pmr::map;
    using std::pmr::multimap;
  } // namespace pmr
} // namespace std

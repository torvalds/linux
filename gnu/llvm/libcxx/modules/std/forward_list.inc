// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [forward.list], class template forward_list
  using std::forward_list;

  using std::operator==;
  using std::operator<=>;

  using std::swap;

  // [forward.list.erasure], erasure
  using std::erase;
  using std::erase_if;

  namespace pmr {
    using std::pmr::forward_list;
  }
} // namespace std

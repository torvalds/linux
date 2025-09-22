// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [vector], class template vector
  using std::vector;

  using std::operator==;
  using std::operator<=>;

  using std::swap;

  // [vector.erasure], erasure
  using std::erase;
  using std::erase_if;

  namespace pmr {
    using std::pmr::vector;
  }

  // hash support
  using std::hash;

#if _LIBCPP_STD_VER >= 23
  // [vector.bool.fmt], formatter specialization for vector<bool>
  using std::formatter;
#endif
} // namespace std

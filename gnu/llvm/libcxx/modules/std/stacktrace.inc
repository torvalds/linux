// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
#if 0
  // [stacktrace.entry], class stacktrace_­entry
  using std::stacktrace_entry;

  // [stacktrace.basic], class template basic_­stacktrace
  using std::basic_stacktrace;

  // basic_­stacktrace typedef-names
  using std::stacktrace;

  // [stacktrace.basic.nonmem], non-member functions
  using std::swap;

  using std::to_string;

  using std::operator<<;

  namespace pmr {
    using std::pmr::stacktrace;
  }

  // [stacktrace.basic.hash], hash support
  using std::hash;
#endif
} // namespace std

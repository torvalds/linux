// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [optional.optional], class template optional
  using std::optional;

  // [optional.nullopt], no-value state indicator
  using std::nullopt;
  using std::nullopt_t;

  // [optional.bad.access], class bad_optional_access
  using std::bad_optional_access;

  // [optional.relops], relational operators
  using std::operator==;
  using std::operator!=;
  using std::operator<;
  using std::operator>;
  using std::operator<=;
  using std::operator>=;
  using std::operator<=>;

  // [optional.specalg], specialized algorithms
  using std::swap;

  using std::make_optional;

  // [optional.hash], hash support
  using std::hash;
} // namespace std

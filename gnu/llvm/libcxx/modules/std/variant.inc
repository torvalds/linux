// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [variant.variant], class template variant
  using std::variant;

  // [variant.helper], variant helper classes
  using std::variant_alternative;
  using std::variant_npos;
  using std::variant_size;
  using std::variant_size_v;

  // [variant.get], value access
  using std::get;
  using std::get_if;
  using std::holds_alternative;
  using std::variant_alternative_t;

  // [variant.relops], relational operators
  using std::operator==;
  using std::operator!=;
  using std::operator<;
  using std::operator>;
  using std::operator<=;
  using std::operator>=;
  using std::operator<=>;

  // [variant.visit], visitation
  using std::visit;

  // [variant.monostate], class monostate
  using std::monostate;

  // [variant.specalg], specialized algorithms
  using std::swap;

  // [variant.bad.access], class bad_variant_access
  using std::bad_variant_access;

  // [variant.hash], hash support
  using std::hash;
} // namespace std

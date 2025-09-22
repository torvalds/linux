// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [tuple.tuple], class template tuple
  using std::tuple;

  // [tuple.like], concept tuple-like

#if _LIBCPP_STD_VER >= 23
  // [tuple.common.ref], common_reference related specializations
  using std::basic_common_reference;
  using std::common_type;
#endif

  // [tuple.creation], tuple creation functions
  using std::ignore;

  using std::forward_as_tuple;
  using std::make_tuple;
  using std::tie;
  using std::tuple_cat;

  // [tuple.apply], calling a function with a tuple of arguments
  using std::apply;

  using std::make_from_tuple;

  // [tuple.helper], tuple helper classes
  using std::tuple_element;
  using std::tuple_size;

  // [tuple.elem], element access
  using std::get;
  using std::tuple_element_t;

  // [tuple.rel], relational operators
  using std::operator==;
  using std::operator<=>;

  // [tuple.traits], allocator-related traits
  using std::uses_allocator;

  // [tuple.special], specialized algorithms
  using std::swap;

  // [tuple.helper], tuple helper classes
  using std::tuple_size_v;
} // namespace std

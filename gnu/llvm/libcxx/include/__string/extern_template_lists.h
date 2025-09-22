//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___STRING_EXTERN_TEMPLATE_LISTS_H
#define _LIBCPP___STRING_EXTERN_TEMPLATE_LISTS_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

// clang-format off

// We maintain 2 ABI lists:
// - _LIBCPP_STRING_V1_EXTERN_TEMPLATE_LIST
// - _LIBCPP_STRING_UNSTABLE_EXTERN_TEMPLATE_LIST
// As the name implies, the ABI lists define the V1 (Stable) and unstable ABI.
//
// For unstable, we may explicitly remove function that are external in V1,
// and add (new) external functions to better control inlining and compiler
// optimization opportunities.
//
// For stable, the ABI list should rarely change, except for adding new
// functions supporting new c++ version / API changes. Typically entries
// must never be removed from the stable list.
#define _LIBCPP_STRING_V1_EXTERN_TEMPLATE_LIST(_Func, _CharType) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::replace(size_type, size_type, value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::rfind(value_type const*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::__init(value_type const*, size_type, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::basic_string(basic_string const&)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::replace(size_type, size_type, value_type const*)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::basic_string(basic_string const&, allocator<_CharType> const&)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::find_last_not_of(value_type const*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::~basic_string()) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::find_first_not_of(value_type const*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::insert(size_type, size_type, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::operator=(value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::__init(value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI const _CharType& basic_string<_CharType>::at(size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::insert(size_type, value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::find_first_of(value_type const*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::replace(size_type, size_type, size_type, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::assign(value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::reserve(size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::append(value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::assign(basic_string const&, size_type, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::copy(value_type*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::basic_string(basic_string const&, size_type, size_type, allocator<_CharType> const&)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::find(value_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::__init(size_type, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::insert(size_type, value_type const*)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::find_last_of(value_type const*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::__grow_by(size_type, size_type, size_type, size_type, size_type, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::__grow_by_and_replace(size_type, size_type, size_type, size_type, size_type, size_type, value_type const*)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::push_back(value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::append(size_type, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::rfind(value_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI const basic_string<_CharType>::size_type basic_string<_CharType>::npos) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::assign(size_type, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::erase(size_type, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::append(basic_string const&, size_type, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI int basic_string<_CharType>::compare(value_type const*) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI int basic_string<_CharType>::compare(size_type, size_type, value_type const*) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI _CharType& basic_string<_CharType>::at(size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::assign(value_type const*)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::find(value_type const*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI int basic_string<_CharType>::compare(size_type, size_type, basic_string const&, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI int basic_string<_CharType>::compare(size_type, size_type, value_type const*, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::operator=(basic_string const&)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::append(value_type const*)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::replace(size_type, size_type, basic_string const&, size_type, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::iterator basic_string<_CharType>::insert(basic_string::const_iterator, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::resize(size_type, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::insert(size_type, basic_string const&, size_type, size_type))

#define _LIBCPP_STRING_UNSTABLE_EXTERN_TEMPLATE_LIST(_Func, _CharType) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::replace(size_type, size_type, value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::rfind(value_type const*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::__init(value_type const*, size_type, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::replace(size_type, size_type, value_type const*)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::find_last_not_of(value_type const*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::~basic_string()) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::find_first_not_of(value_type const*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::insert(size_type, size_type, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::operator=(value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::__init(value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::__init_copy_ctor_external(value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI const _CharType& basic_string<_CharType>::at(size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::insert(size_type, value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::find_first_of(value_type const*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::replace(size_type, size_type, size_type, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::__assign_external(value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::__assign_external(value_type const*)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::reserve(size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::append(value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::assign(basic_string const&, size_type, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::copy(value_type*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::basic_string(basic_string const&, size_type, size_type, allocator<_CharType> const&)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::find(value_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::__init(size_type, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::insert(size_type, value_type const*)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::find_last_of(value_type const*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::__grow_by_and_replace(size_type, size_type, size_type, size_type, size_type, size_type, value_type const*)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::__assign_no_alias<false>(value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::__assign_no_alias<true>(value_type const*, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::push_back(value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::append(size_type, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::rfind(value_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI const basic_string<_CharType>::size_type basic_string<_CharType>::npos) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::assign(size_type, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::__erase_external_with_move(size_type, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::append(basic_string const&, size_type, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI int basic_string<_CharType>::compare(value_type const*) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI int basic_string<_CharType>::compare(size_type, size_type, value_type const*) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI _CharType& basic_string<_CharType>::at(size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::size_type basic_string<_CharType>::find(value_type const*, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI int basic_string<_CharType>::compare(size_type, size_type, basic_string const&, size_type, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI int basic_string<_CharType>::compare(size_type, size_type, value_type const*, size_type) const) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::append(value_type const*)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::replace(size_type, size_type, basic_string const&, size_type, size_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>::iterator basic_string<_CharType>::insert(basic_string::const_iterator, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI void basic_string<_CharType>::resize(size_type, value_type)) \
  _Func(_LIBCPP_EXPORTED_FROM_ABI basic_string<_CharType>& basic_string<_CharType>::insert(size_type, basic_string const&, size_type, size_type))

// clang-format on

#endif // _LIBCPP___STRING_EXTERN_TEMPLATE_LISTS_H

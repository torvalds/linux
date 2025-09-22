//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_INTEGER_SEQUENCE_H
#define _LIBCPP___UTILITY_INTEGER_SEQUENCE_H

#include <__config>
#include <__type_traits/is_integral.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <size_t...>
struct __tuple_indices;

template <class _IdxType, _IdxType... _Values>
struct __integer_sequence {
  template <template <class _OIdxType, _OIdxType...> class _ToIndexSeq, class _ToIndexType>
  using __convert = _ToIndexSeq<_ToIndexType, _Values...>;

  template <size_t _Sp>
  using __to_tuple_indices = __tuple_indices<(_Values + _Sp)...>;
};

#if __has_builtin(__make_integer_seq)
template <size_t _Ep, size_t _Sp>
using __make_indices_imp =
    typename __make_integer_seq<__integer_sequence, size_t, _Ep - _Sp>::template __to_tuple_indices<_Sp>;
#elif __has_builtin(__integer_pack)
template <size_t _Ep, size_t _Sp>
using __make_indices_imp =
    typename __integer_sequence<size_t, __integer_pack(_Ep - _Sp)...>::template __to_tuple_indices<_Sp>;
#else
#  error "No known way to get an integer pack from the compiler"
#endif

#if _LIBCPP_STD_VER >= 14

template <class _Tp, _Tp... _Ip>
struct _LIBCPP_TEMPLATE_VIS integer_sequence {
  typedef _Tp value_type;
  static_assert(is_integral<_Tp>::value, "std::integer_sequence can only be instantiated with an integral type");
  static _LIBCPP_HIDE_FROM_ABI constexpr size_t size() noexcept { return sizeof...(_Ip); }
};

template <size_t... _Ip>
using index_sequence = integer_sequence<size_t, _Ip...>;

#  if __has_builtin(__make_integer_seq)

template <class _Tp, _Tp _Ep>
using make_integer_sequence _LIBCPP_NODEBUG = __make_integer_seq<integer_sequence, _Tp, _Ep>;

#  elif __has_builtin(__integer_pack)

template <class _Tp, _Tp _SequenceSize>
using make_integer_sequence _LIBCPP_NODEBUG = integer_sequence<_Tp, __integer_pack(_SequenceSize)...>;

#  else
#    error "No known way to get an integer pack from the compiler"
#  endif

template <size_t _Np>
using make_index_sequence = make_integer_sequence<size_t, _Np>;

template <class... _Tp>
using index_sequence_for = make_index_sequence<sizeof...(_Tp)>;

#  if _LIBCPP_STD_VER >= 20
// Executes __func for every element in an index_sequence.
template <size_t... _Index, class _Function>
_LIBCPP_HIDE_FROM_ABI constexpr void __for_each_index_sequence(index_sequence<_Index...>, _Function __func) {
  (__func.template operator()<_Index>(), ...);
}
#  endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP_STD_VER >= 14

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_INTEGER_SEQUENCE_H

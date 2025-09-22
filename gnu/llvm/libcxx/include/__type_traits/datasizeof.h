//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_DATASIZEOF_H
#define _LIBCPP___TYPE_TRAITS_DATASIZEOF_H

#include <__config>
#include <__type_traits/is_class.h>
#include <__type_traits/is_final.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

// This trait provides the size of a type excluding any tail padding.
//
// It is useful in contexts where performing an operation using the full size of the class (including padding) may
// have unintended side effects, such as overwriting a derived class' member when writing the tail padding of a class
// through a pointer-to-base.

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_keyword(__datasizeof) || __has_extension(datasizeof)
template <class _Tp>
inline const size_t __datasizeof_v = __datasizeof(_Tp);
#else
// NOLINTNEXTLINE(readability-redundant-preprocessor) This is https://llvm.org/PR64825
#  if __has_cpp_attribute(__no_unique_address__)
template <class _Tp>
struct _FirstPaddingByte {
  [[__no_unique_address__]] _Tp __v_;
  char __first_padding_byte_;
};
#  else
template <class _Tp, bool = __libcpp_is_final<_Tp>::value || !is_class<_Tp>::value>
struct _FirstPaddingByte : _Tp {
  char __first_padding_byte_;
};

template <class _Tp>
struct _FirstPaddingByte<_Tp, true> {
  _Tp __v_;
  char __first_padding_byte_;
};
#  endif // __has_cpp_attribute(__no_unique_address__)

// _FirstPaddingByte<> is sometimes non-standard layout. Using `offsetof` is UB in that case, but GCC and Clang allow
// the use as an extension.
_LIBCPP_DIAGNOSTIC_PUSH
_LIBCPP_CLANG_DIAGNOSTIC_IGNORED("-Winvalid-offsetof")
_LIBCPP_GCC_DIAGNOSTIC_IGNORED("-Winvalid-offsetof")
template <class _Tp>
inline const size_t __datasizeof_v = offsetof(_FirstPaddingByte<_Tp>, __first_padding_byte_);
_LIBCPP_DIAGNOSTIC_POP
#endif   // __has_extension(datasizeof)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_DATASIZEOF_H

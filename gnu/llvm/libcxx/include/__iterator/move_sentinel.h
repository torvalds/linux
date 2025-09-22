//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_MOVE_SENTINEL_H
#define _LIBCPP___ITERATOR_MOVE_SENTINEL_H

#include <__concepts/assignable.h>
#include <__concepts/convertible_to.h>
#include <__concepts/semiregular.h>
#include <__config>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <semiregular _Sent>
class _LIBCPP_TEMPLATE_VIS move_sentinel {
public:
  _LIBCPP_HIDE_FROM_ABI move_sentinel() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit move_sentinel(_Sent __s) : __last_(std::move(__s)) {}

  template <class _S2>
    requires convertible_to<const _S2&, _Sent>
  _LIBCPP_HIDE_FROM_ABI constexpr move_sentinel(const move_sentinel<_S2>& __s) : __last_(__s.base()) {}

  template <class _S2>
    requires assignable_from<_Sent&, const _S2&>
  _LIBCPP_HIDE_FROM_ABI constexpr move_sentinel& operator=(const move_sentinel<_S2>& __s) {
    __last_ = __s.base();
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Sent base() const { return __last_; }

private:
  _Sent __last_ = _Sent();
};

_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(move_sentinel);

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ITERATOR_MOVE_SENTINEL_H

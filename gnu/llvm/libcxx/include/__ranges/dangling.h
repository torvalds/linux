// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_DANGLING_H
#define _LIBCPP___RANGES_DANGLING_H

#include <__config>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__type_traits/conditional.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {
struct dangling {
  dangling() = default;
  _LIBCPP_HIDE_FROM_ABI constexpr dangling(auto&&...) noexcept {}
};

template <range _Rp>
using borrowed_iterator_t = _If<borrowed_range<_Rp>, iterator_t<_Rp>, dangling>;

// borrowed_subrange_t defined in <__ranges/subrange.h>
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_DANGLING_H

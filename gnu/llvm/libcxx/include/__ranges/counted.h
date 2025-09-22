// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_COUNTED_H
#define _LIBCPP___RANGES_COUNTED_H

#include <__concepts/convertible_to.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/counted_iterator.h>
#include <__iterator/default_sentinel.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iterator_traits.h>
#include <__memory/pointer_traits.h>
#include <__ranges/subrange.h>
#include <__type_traits/decay.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <cstddef>
#include <span>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges::views {

namespace __counted {

struct __fn {
  template <contiguous_iterator _It>
  _LIBCPP_HIDE_FROM_ABI static constexpr auto
  __go(_It __it,
       iter_difference_t<_It> __count) noexcept(noexcept(span(std::to_address(__it), static_cast<size_t>(__count))))
  // Deliberately omit return-type SFINAE, because to_address is not SFINAE-friendly
  {
    return span(std::to_address(__it), static_cast<size_t>(__count));
  }

  template <random_access_iterator _It>
  _LIBCPP_HIDE_FROM_ABI static constexpr auto __go(_It __it, iter_difference_t<_It> __count) noexcept(
      noexcept(subrange(__it, __it + __count))) -> decltype(subrange(__it, __it + __count)) {
    return subrange(__it, __it + __count);
  }

  template <class _It>
  _LIBCPP_HIDE_FROM_ABI static constexpr auto __go(_It __it, iter_difference_t<_It> __count) noexcept(
      noexcept(subrange(counted_iterator(std::move(__it), __count), default_sentinel)))
      -> decltype(subrange(counted_iterator(std::move(__it), __count), default_sentinel)) {
    return subrange(counted_iterator(std::move(__it), __count), default_sentinel);
  }

  template <class _It, convertible_to<iter_difference_t<_It>> _Diff>
    requires input_or_output_iterator<decay_t<_It>>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_It&& __it, _Diff&& __count) const
      noexcept(noexcept(__go(std::forward<_It>(__it), std::forward<_Diff>(__count))))
          -> decltype(__go(std::forward<_It>(__it), std::forward<_Diff>(__count))) {
    return __go(std::forward<_It>(__it), std::forward<_Diff>(__count));
  }
};

} // namespace __counted

inline namespace __cpo {
inline constexpr auto counted = __counted::__fn{};
} // namespace __cpo

} // namespace ranges::views

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_COUNTED_H

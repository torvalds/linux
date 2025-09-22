//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_SHUFFLE_H
#define _LIBCPP___ALGORITHM_RANGES_SHUFFLE_H

#include <__algorithm/iterator_operations.h>
#include <__algorithm/shuffle.h>
#include <__algorithm/uniform_random_bit_generator_adaptor.h>
#include <__config>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/next.h>
#include <__iterator/permutable.h>
#include <__random/uniform_random_bit_generator.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__type_traits/remove_reference.h>
#include <__utility/forward.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
namespace __shuffle {

struct __fn {
  template <random_access_iterator _Iter, sentinel_for<_Iter> _Sent, class _Gen>
    requires permutable<_Iter> && uniform_random_bit_generator<remove_reference_t<_Gen>>
  _LIBCPP_HIDE_FROM_ABI _Iter operator()(_Iter __first, _Sent __last, _Gen&& __gen) const {
    _ClassicGenAdaptor<_Gen> __adapted_gen(__gen);
    return std::__shuffle<_RangeAlgPolicy>(std::move(__first), std::move(__last), __adapted_gen);
  }

  template <random_access_range _Range, class _Gen>
    requires permutable<iterator_t<_Range>> && uniform_random_bit_generator<remove_reference_t<_Gen>>
  _LIBCPP_HIDE_FROM_ABI borrowed_iterator_t<_Range> operator()(_Range&& __range, _Gen&& __gen) const {
    return (*this)(ranges::begin(__range), ranges::end(__range), std::forward<_Gen>(__gen));
  }
};

} // namespace __shuffle

inline namespace __cpo {
inline constexpr auto shuffle = __shuffle::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_SHUFFLE_H

//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_SAMPLE_H
#define _LIBCPP___ALGORITHM_RANGES_SAMPLE_H

#include <__algorithm/iterator_operations.h>
#include <__algorithm/sample.h>
#include <__algorithm/uniform_random_bit_generator_adaptor.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iterator_traits.h>
#include <__random/uniform_random_bit_generator.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
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
namespace __sample {

struct __fn {
  template <input_iterator _Iter, sentinel_for<_Iter> _Sent, weakly_incrementable _OutIter, class _Gen>
    requires(forward_iterator<_Iter> || random_access_iterator<_OutIter>) && indirectly_copyable<_Iter, _OutIter> &&
            uniform_random_bit_generator<remove_reference_t<_Gen>>
  _LIBCPP_HIDE_FROM_ABI _OutIter
  operator()(_Iter __first, _Sent __last, _OutIter __out_first, iter_difference_t<_Iter> __n, _Gen&& __gen) const {
    _ClassicGenAdaptor<_Gen> __adapted_gen(__gen);
    return std::__sample<_RangeAlgPolicy>(
        std::move(__first), std::move(__last), std::move(__out_first), __n, __adapted_gen);
  }

  template <input_range _Range, weakly_incrementable _OutIter, class _Gen>
    requires(forward_range<_Range> || random_access_iterator<_OutIter>) &&
            indirectly_copyable<iterator_t<_Range>, _OutIter> && uniform_random_bit_generator<remove_reference_t<_Gen>>
  _LIBCPP_HIDE_FROM_ABI _OutIter
  operator()(_Range&& __range, _OutIter __out_first, range_difference_t<_Range> __n, _Gen&& __gen) const {
    return (*this)(
        ranges::begin(__range), ranges::end(__range), std::move(__out_first), __n, std::forward<_Gen>(__gen));
  }
};

} // namespace __sample

inline namespace __cpo {
inline constexpr auto sample = __sample::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_SAMPLE_H

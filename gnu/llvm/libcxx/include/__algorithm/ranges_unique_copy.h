//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_UNIQUE_COPY_H
#define _LIBCPP___ALGORITHM_RANGES_UNIQUE_COPY_H

#include <__algorithm/in_out_result.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/make_projected.h>
#include <__algorithm/unique_copy.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/projected.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {

template <class _InIter, class _OutIter>
using unique_copy_result = in_out_result<_InIter, _OutIter>;

namespace __unique_copy {

template <class _InIter, class _OutIter>
concept __can_reread_from_output = (input_iterator<_OutIter> && same_as<iter_value_t<_InIter>, iter_value_t<_OutIter>>);

struct __fn {
  template <class _InIter, class _OutIter>
  static consteval auto __get_algo_tag() {
    if constexpr (forward_iterator<_InIter>) {
      return __unique_copy_tags::__reread_from_input_tag{};
    } else if constexpr (__can_reread_from_output<_InIter, _OutIter>) {
      return __unique_copy_tags::__reread_from_output_tag{};
    } else if constexpr (indirectly_copyable_storable<_InIter, _OutIter>) {
      return __unique_copy_tags::__read_from_tmp_value_tag{};
    }
  }

  template <class _InIter, class _OutIter>
  using __algo_tag_t = decltype(__get_algo_tag<_InIter, _OutIter>());

  template <input_iterator _InIter,
            sentinel_for<_InIter> _Sent,
            weakly_incrementable _OutIter,
            class _Proj                                                    = identity,
            indirect_equivalence_relation<projected<_InIter, _Proj>> _Comp = ranges::equal_to>
    requires indirectly_copyable<_InIter, _OutIter> &&
             (forward_iterator<_InIter> ||
              (input_iterator<_OutIter> && same_as<iter_value_t<_InIter>, iter_value_t<_OutIter>>) ||
              indirectly_copyable_storable<_InIter, _OutIter>)
  _LIBCPP_HIDE_FROM_ABI constexpr unique_copy_result<_InIter, _OutIter>
  operator()(_InIter __first, _Sent __last, _OutIter __result, _Comp __comp = {}, _Proj __proj = {}) const {
    auto __ret = std::__unique_copy<_RangeAlgPolicy>(
        std::move(__first),
        std::move(__last),
        std::move(__result),
        std::__make_projected(__comp, __proj),
        __algo_tag_t<_InIter, _OutIter>());
    return {std::move(__ret.first), std::move(__ret.second)};
  }

  template <input_range _Range,
            weakly_incrementable _OutIter,
            class _Proj                                                               = identity,
            indirect_equivalence_relation<projected<iterator_t<_Range>, _Proj>> _Comp = ranges::equal_to>
    requires indirectly_copyable<iterator_t<_Range>, _OutIter> &&
             (forward_iterator<iterator_t<_Range>> ||
              (input_iterator<_OutIter> && same_as<range_value_t<_Range>, iter_value_t<_OutIter>>) ||
              indirectly_copyable_storable<iterator_t<_Range>, _OutIter>)
  _LIBCPP_HIDE_FROM_ABI constexpr unique_copy_result<borrowed_iterator_t<_Range>, _OutIter>
  operator()(_Range&& __range, _OutIter __result, _Comp __comp = {}, _Proj __proj = {}) const {
    auto __ret = std::__unique_copy<_RangeAlgPolicy>(
        ranges::begin(__range),
        ranges::end(__range),
        std::move(__result),
        std::__make_projected(__comp, __proj),
        __algo_tag_t<iterator_t<_Range>, _OutIter>());
    return {std::move(__ret.first), std::move(__ret.second)};
  }
};

} // namespace __unique_copy

inline namespace __cpo {
inline constexpr auto unique_copy = __unique_copy::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_UNIQUE_COPY_H

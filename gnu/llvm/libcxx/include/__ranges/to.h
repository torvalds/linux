// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_TO_H
#define _LIBCPP___RANGES_TO_H

#include <__algorithm/ranges_copy.h>
#include <__concepts/constructible.h>
#include <__concepts/convertible_to.h>
#include <__concepts/derived_from.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__functional/bind_back.h>
#include <__iterator/back_insert_iterator.h>
#include <__iterator/insert_iterator.h>
#include <__iterator/iterator_traits.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/from_range.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/ref_view.h>
#include <__ranges/size.h>
#include <__ranges/transform_view.h>
#include <__type_traits/add_pointer.h>
#include <__type_traits/is_const.h>
#include <__type_traits/is_volatile.h>
#include <__type_traits/type_identity.h>
#include <__utility/declval.h>
#include <__utility/forward.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

namespace ranges {

template <class _Container>
constexpr bool __reservable_container =
    sized_range<_Container> && requires(_Container& __c, range_size_t<_Container> __n) {
      __c.reserve(__n);
      { __c.capacity() } -> same_as<decltype(__n)>;
      { __c.max_size() } -> same_as<decltype(__n)>;
    };

template <class _Container, class _Ref>
constexpr bool __container_insertable = requires(_Container& __c, _Ref&& __ref) {
  requires(
      requires { __c.push_back(std::forward<_Ref>(__ref)); } ||
      requires { __c.insert(__c.end(), std::forward<_Ref>(__ref)); });
};

template <class _Ref, class _Container>
_LIBCPP_HIDE_FROM_ABI constexpr auto __container_inserter(_Container& __c) {
  if constexpr (requires { __c.push_back(std::declval<_Ref>()); }) {
    return std::back_inserter(__c);
  } else {
    return std::inserter(__c, __c.end());
  }
}

// Note: making this a concept allows short-circuiting the second condition.
template <class _Container, class _Range>
concept __try_non_recursive_conversion =
    !input_range<_Container> || convertible_to<range_reference_t<_Range>, range_value_t<_Container>>;

template <class _Container, class _Range, class... _Args>
concept __constructible_from_iter_pair =
    common_range<_Range> && requires { typename iterator_traits<iterator_t<_Range>>::iterator_category; } &&
    derived_from<typename iterator_traits<iterator_t<_Range>>::iterator_category, input_iterator_tag> &&
    constructible_from<_Container, iterator_t<_Range>, sentinel_t<_Range>, _Args...>;

template <class>
concept __always_false = false;

// `ranges::to` base template -- the `_Container` type is a simple type template parameter.
template <class _Container, input_range _Range, class... _Args>
  requires(!view<_Container>)
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr _Container to(_Range&& __range, _Args&&... __args) {
  // Mandates: C is a cv-unqualified class type.
  static_assert(!is_const_v<_Container>, "The target container cannot be const-qualified, please remove the const");
  static_assert(
      !is_volatile_v<_Container>, "The target container cannot be volatile-qualified, please remove the volatile");

  // First see if the non-recursive case applies -- the conversion target is either:
  // - a range with a convertible value type;
  // - a non-range type which might support being created from the input argument(s) (e.g. an `optional`).
  if constexpr (__try_non_recursive_conversion<_Container, _Range>) {
    // Case 1 -- construct directly from the given range.
    if constexpr (constructible_from<_Container, _Range, _Args...>) {
      return _Container(std::forward<_Range>(__range), std::forward<_Args>(__args)...);
    }

    // Case 2 -- construct using the `from_range_t` tagged constructor.
    else if constexpr (constructible_from<_Container, from_range_t, _Range, _Args...>) {
      return _Container(from_range, std::forward<_Range>(__range), std::forward<_Args>(__args)...);
    }

    // Case 3 -- construct from a begin-end iterator pair.
    else if constexpr (__constructible_from_iter_pair<_Container, _Range, _Args...>) {
      return _Container(ranges::begin(__range), ranges::end(__range), std::forward<_Args>(__args)...);
    }

    // Case 4 -- default-construct (or construct from the extra arguments) and insert, reserving the size if possible.
    else if constexpr (constructible_from<_Container, _Args...> &&
                       __container_insertable<_Container, range_reference_t<_Range>>) {
      _Container __result(std::forward<_Args>(__args)...);
      if constexpr (sized_range<_Range> && __reservable_container<_Container>) {
        __result.reserve(static_cast<range_size_t<_Container>>(ranges::size(__range)));
      }

      ranges::copy(__range, ranges::__container_inserter<range_reference_t<_Range>>(__result));

      return __result;

    } else {
      static_assert(__always_false<_Container>, "ranges::to: unable to convert to the given container type.");
    }

    // Try the recursive case.
  } else if constexpr (input_range<range_reference_t<_Range>>) {
    return ranges::to<_Container>(
        ref_view(__range) | views::transform([](auto&& __elem) {
          return ranges::to<range_value_t<_Container>>(std::forward<decltype(__elem)>(__elem));
        }),
        std::forward<_Args>(__args)...);

  } else {
    static_assert(__always_false<_Container>, "ranges::to: unable to convert to the given container type.");
  }
}

template <class _Range>
struct __minimal_input_iterator {
  using iterator_category = input_iterator_tag;
  using value_type        = range_value_t<_Range>;
  using difference_type   = ptrdiff_t;
  using pointer           = add_pointer_t<range_reference_t<_Range>>;
  using reference         = range_reference_t<_Range>;

  reference operator*() const;
  pointer operator->() const;
  __minimal_input_iterator& operator++();
  __minimal_input_iterator operator++(int);
  bool operator==(const __minimal_input_iterator&) const;
};

// Deduces the full type of the container from the given template template parameter.
template <template <class...> class _Container, input_range _Range, class... _Args>
struct _Deducer {
  _LIBCPP_HIDE_FROM_ABI static constexpr auto __deduce_func() {
    using _InputIter = __minimal_input_iterator<_Range>;

    // Case 1 -- can construct directly from the given range.
    if constexpr (requires { _Container(std::declval<_Range>(), std::declval<_Args>()...); }) {
      using _Result = decltype( //
          _Container(std::declval<_Range>(), std::declval<_Args>()...));
      return type_identity<_Result>{};

      // Case 2 -- can construct from the given range using the `from_range_t` tagged constructor.
    } else if constexpr ( //
        requires { _Container(from_range, std::declval<_Range>(), std::declval<_Args>()...); }) {
      using _Result = //
          decltype(_Container(from_range, std::declval<_Range>(), std::declval<_Args>()...));
      return type_identity<_Result>{};

      // Case 3 -- can construct from a begin-end iterator pair.
    } else if constexpr ( //
        requires { _Container(std::declval<_InputIter>(), std::declval<_InputIter>(), std::declval<_Args>()...); }) {
      using _Result =
          decltype(_Container(std::declval<_InputIter>(), std::declval<_InputIter>(), std::declval<_Args>()...));
      return type_identity<_Result>{};

    } else {
      static_assert(__always_false<_Range>,
                    "ranges::to: unable to deduce the container type from the template template argument.");
    }
  }

  using type = typename decltype(__deduce_func())::type;
};

// `ranges::to` specialization -- `_Container` is a template template parameter requiring deduction to figure out the
// container element type.
template <template <class...> class _Container, input_range _Range, class... _Args>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto to(_Range&& __range, _Args&&... __args) {
  using _DeduceExpr = typename _Deducer<_Container, _Range, _Args...>::type;
  return ranges::to<_DeduceExpr>(std::forward<_Range>(__range), std::forward<_Args>(__args)...);
}

// Range adaptor closure object 1 -- wrapping the `ranges::to` version where `_Container` is a simple type template
// parameter.
template <class _Container, class... _Args>
  requires(!view<_Container>)
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto to(_Args&&... __args) {
  // Mandates: C is a cv-unqualified class type.
  static_assert(!is_const_v<_Container>, "The target container cannot be const-qualified, please remove the const");
  static_assert(
      !is_volatile_v<_Container>, "The target container cannot be volatile-qualified, please remove the volatile");

  auto __to_func = []<input_range _Range, class... _Tail>(_Range&& __range, _Tail&&... __tail) static
    requires requires { //
      /**/ ranges::to<_Container>(std::forward<_Range>(__range), std::forward<_Tail>(__tail)...);
    }
  { return ranges::to<_Container>(std::forward<_Range>(__range), std::forward<_Tail>(__tail)...); };

  return __range_adaptor_closure_t(std::__bind_back(__to_func, std::forward<_Args>(__args)...));
}

// Range adaptor closure object 2 -- wrapping the `ranges::to` version where `_Container` is a template template
// parameter.
template <template <class...> class _Container, class... _Args>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto to(_Args&&... __args) {
  // clang-format off
  auto __to_func = []<input_range _Range, class... _Tail,
                      class _DeducedExpr = typename _Deducer<_Container, _Range, _Tail...>::type>
    (_Range&& __range, _Tail&& ... __tail) static
      requires requires { //
      /**/ ranges::to<_DeducedExpr>(std::forward<_Range>(__range), std::forward<_Tail>(__tail)...);
    }
  {
    return ranges::to<_DeducedExpr>(std::forward<_Range>(__range), std::forward<_Tail>(__tail)...);
  };
  // clang-format on

  return __range_adaptor_closure_t(std::__bind_back(__to_func, std::forward<_Args>(__args)...));
}

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_TO_H

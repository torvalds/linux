// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_MOVABLE_BOX_H
#define _LIBCPP___RANGES_MOVABLE_BOX_H

#include <__concepts/constructible.h>
#include <__concepts/copyable.h>
#include <__concepts/movable.h>
#include <__config>
#include <__memory/addressof.h>
#include <__memory/construct_at.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__utility/move.h>
#include <optional>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// __movable_box allows turning a type that is move-constructible (but maybe not move-assignable) into
// a type that is both move-constructible and move-assignable. It does that by introducing an empty state
// and basically doing destroy-then-copy-construct in the assignment operator. The empty state is necessary
// to handle the case where the copy construction fails after destroying the object.
//
// In some cases, we can completely avoid the use of an empty state; we provide a specialization of
// __movable_box that does this, see below for the details.

// until C++23, `__movable_box` was named `__copyable_box` and required the stored type to be copy-constructible, not
// just move-constructible; we preserve the old behavior in pre-C++23 modes.
template <class _Tp>
concept __movable_box_object =
#  if _LIBCPP_STD_VER >= 23
    move_constructible<_Tp>
#  else
    copy_constructible<_Tp>
#  endif
    && is_object_v<_Tp>;

namespace ranges {
// Primary template - uses std::optional and introduces an empty state in case assignment fails.
template <__movable_box_object _Tp>
class __movable_box {
  _LIBCPP_NO_UNIQUE_ADDRESS optional<_Tp> __val_;

public:
  template <class... _Args>
    requires is_constructible_v<_Tp, _Args...>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __movable_box(in_place_t, _Args&&... __args) noexcept(
      is_nothrow_constructible_v<_Tp, _Args...>)
      : __val_(in_place, std::forward<_Args>(__args)...) {}

  _LIBCPP_HIDE_FROM_ABI constexpr __movable_box() noexcept(is_nothrow_default_constructible_v<_Tp>)
    requires default_initializable<_Tp>
      : __val_(in_place) {}

  _LIBCPP_HIDE_FROM_ABI __movable_box(__movable_box const&) = default;
  _LIBCPP_HIDE_FROM_ABI __movable_box(__movable_box&&)      = default;

  _LIBCPP_HIDE_FROM_ABI constexpr __movable_box&
  operator=(__movable_box const& __other) noexcept(is_nothrow_copy_constructible_v<_Tp>)
#  if _LIBCPP_STD_VER >= 23
    requires copy_constructible<_Tp>
#  endif
  {
    if (this != std::addressof(__other)) {
      if (__other.__has_value())
        __val_.emplace(*__other);
      else
        __val_.reset();
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI __movable_box& operator=(__movable_box&&)
    requires movable<_Tp>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr __movable_box&
  operator=(__movable_box&& __other) noexcept(is_nothrow_move_constructible_v<_Tp>) {
    if (this != std::addressof(__other)) {
      if (__other.__has_value())
        __val_.emplace(std::move(*__other));
      else
        __val_.reset();
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Tp const& operator*() const noexcept { return *__val_; }
  _LIBCPP_HIDE_FROM_ABI constexpr _Tp& operator*() noexcept { return *__val_; }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp* operator->() const noexcept { return __val_.operator->(); }
  _LIBCPP_HIDE_FROM_ABI constexpr _Tp* operator->() noexcept { return __val_.operator->(); }

  _LIBCPP_HIDE_FROM_ABI constexpr bool __has_value() const noexcept { return __val_.has_value(); }
};

// This partial specialization implements an optimization for when we know we don't need to store
// an empty state to represent failure to perform an assignment. For copy-assignment, this happens:
//
// 1. If the type is copyable (which includes copy-assignment), we can use the type's own assignment operator
//    directly and avoid using std::optional.
// 2. If the type is not copyable, but it is nothrow-copy-constructible, then we can implement assignment as
//    destroy-and-then-construct and we know it will never fail, so we don't need an empty state.
//
// The exact same reasoning can be applied for move-assignment, with copyable replaced by movable and
// nothrow-copy-constructible replaced by nothrow-move-constructible. This specialization is enabled
// whenever we can apply any of these optimizations for both the copy assignment and the move assignment
// operator.

#  if _LIBCPP_STD_VER >= 23
template <class _Tp>
concept __doesnt_need_empty_state =
    (copy_constructible<_Tp>
         // 1. If copy_constructible<T> is true, movable-box<T> should store only a T if either T models
         //    copyable, or is_nothrow_move_constructible_v<T> && is_nothrow_copy_constructible_v<T> is true.
         ? copyable<_Tp> || (is_nothrow_move_constructible_v<_Tp> && is_nothrow_copy_constructible_v<_Tp>)
         // 2. Otherwise, movable-box<T> should store only a T if either T models movable or
         //    is_nothrow_move_constructible_v<T> is true.
         : movable<_Tp> || is_nothrow_move_constructible_v<_Tp>);

// When _Tp doesn't have an assignment operator, we must implement __movable_box's assignment operator
// by doing destroy_at followed by construct_at. However, that implementation strategy leads to UB if the nested
// _Tp is potentially overlapping, as it is doing a non-transparent replacement of the sub-object, which means that
// we're not considered "nested" inside the movable-box anymore, and since we're not nested within it, [basic.life]/1.5
// says that we essentially just reused the storage of the movable-box for a completely unrelated object and ended the
// movable-box's lifetime.
// https://github.com/llvm/llvm-project/issues/70494#issuecomment-1845646490
//
// Hence, when the _Tp doesn't have an assignment operator, we can't risk making it a potentially-overlapping
// subobject because of the above, and we don't use [[no_unique_address]] in that case.
template <class _Tp>
concept __can_use_no_unique_address = (copy_constructible<_Tp> ? copyable<_Tp> : movable<_Tp>);

#  else

template <class _Tp>
concept __doesnt_need_empty_state_for_copy = copyable<_Tp> || is_nothrow_copy_constructible_v<_Tp>;

template <class _Tp>
concept __doesnt_need_empty_state_for_move = movable<_Tp> || is_nothrow_move_constructible_v<_Tp>;

template <class _Tp>
concept __doesnt_need_empty_state = __doesnt_need_empty_state_for_copy<_Tp> && __doesnt_need_empty_state_for_move<_Tp>;

template <class _Tp>
concept __can_use_no_unique_address = copyable<_Tp>;
#  endif

template <class _Tp>
struct __movable_box_holder {
  _Tp __val_;

  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __movable_box_holder(in_place_t, _Args&&... __args)
      : __val_(std::forward<_Args>(__args)...) {}
};

template <class _Tp>
  requires __can_use_no_unique_address<_Tp>
struct __movable_box_holder<_Tp> {
  _LIBCPP_NO_UNIQUE_ADDRESS _Tp __val_;

  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __movable_box_holder(in_place_t, _Args&&... __args)
      : __val_(std::forward<_Args>(__args)...) {}
};

template <__movable_box_object _Tp>
  requires __doesnt_need_empty_state<_Tp>
class __movable_box<_Tp> {
  _LIBCPP_NO_UNIQUE_ADDRESS __movable_box_holder<_Tp> __holder_;

public:
  template <class... _Args>
    requires is_constructible_v<_Tp, _Args...>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __movable_box(in_place_t __inplace, _Args&&... __args) noexcept(
      is_nothrow_constructible_v<_Tp, _Args...>)
      : __holder_(__inplace, std::forward<_Args>(__args)...) {}

  _LIBCPP_HIDE_FROM_ABI constexpr __movable_box() noexcept(is_nothrow_default_constructible_v<_Tp>)
    requires default_initializable<_Tp>
      : __holder_(in_place_t{}) {}

  _LIBCPP_HIDE_FROM_ABI __movable_box(__movable_box const&) = default;
  _LIBCPP_HIDE_FROM_ABI __movable_box(__movable_box&&)      = default;

  // Implementation of assignment operators in case we perform optimization (1)
  _LIBCPP_HIDE_FROM_ABI __movable_box& operator=(__movable_box const&)
    requires copyable<_Tp>
  = default;
  _LIBCPP_HIDE_FROM_ABI __movable_box& operator=(__movable_box&&)
    requires movable<_Tp>
  = default;

  // Implementation of assignment operators in case we perform optimization (2)
  _LIBCPP_HIDE_FROM_ABI constexpr __movable_box& operator=(__movable_box const& __other) noexcept {
    static_assert(is_nothrow_copy_constructible_v<_Tp>);
    static_assert(!__can_use_no_unique_address<_Tp>);
    if (this != std::addressof(__other)) {
      std::destroy_at(std::addressof(__holder_.__val_));
      std::construct_at(std::addressof(__holder_.__val_), __other.__holder_.__val_);
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __movable_box& operator=(__movable_box&& __other) noexcept {
    static_assert(is_nothrow_move_constructible_v<_Tp>);
    static_assert(!__can_use_no_unique_address<_Tp>);
    if (this != std::addressof(__other)) {
      std::destroy_at(std::addressof(__holder_.__val_));
      std::construct_at(std::addressof(__holder_.__val_), std::move(__other.__holder_.__val_));
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Tp const& operator*() const noexcept { return __holder_.__val_; }
  _LIBCPP_HIDE_FROM_ABI constexpr _Tp& operator*() noexcept { return __holder_.__val_; }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp* operator->() const noexcept { return std::addressof(__holder_.__val_); }
  _LIBCPP_HIDE_FROM_ABI constexpr _Tp* operator->() noexcept { return std::addressof(__holder_.__val_); }

  _LIBCPP_HIDE_FROM_ABI constexpr bool __has_value() const noexcept { return true; }
};
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_MOVABLE_BOX_H

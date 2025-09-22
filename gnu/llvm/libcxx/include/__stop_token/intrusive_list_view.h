// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___STOP_TOKEN_INTRUSIVE_LIST_VIEW_H
#define _LIBCPP___STOP_TOKEN_INTRUSIVE_LIST_VIEW_H

#include <__assert>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _Derived>
struct __intrusive_node_base {
  _Derived* __next_ = nullptr;
  _Derived* __prev_ = nullptr;
};

// This class is a view of underlying double-linked list.
// It does not own the nodes. It provides user-friendly
// operations on the linked list.
template <class _Node>
struct __intrusive_list_view {
  _LIBCPP_HIDE_FROM_ABI __intrusive_list_view()                                        = default;
  _LIBCPP_HIDE_FROM_ABI __intrusive_list_view(__intrusive_list_view const&)            = default;
  _LIBCPP_HIDE_FROM_ABI __intrusive_list_view(__intrusive_list_view&&)                 = default;
  _LIBCPP_HIDE_FROM_ABI __intrusive_list_view& operator=(__intrusive_list_view const&) = default;
  _LIBCPP_HIDE_FROM_ABI __intrusive_list_view& operator=(__intrusive_list_view&&)      = default;
  _LIBCPP_HIDE_FROM_ABI ~__intrusive_list_view()                                       = default;

  _LIBCPP_HIDE_FROM_ABI bool __empty() const noexcept { return __head_ == nullptr; }

  _LIBCPP_HIDE_FROM_ABI void __push_front(_Node* __node) noexcept {
    __node->__next_ = __head_;
    if (__head_) {
      __head_->__prev_ = __node;
    }
    __head_ = __node;
  }

  _LIBCPP_HIDE_FROM_ABI _Node* __pop_front() noexcept {
    _Node* __front = __head_;
    __head_        = __head_->__next_;
    if (__head_) {
      __head_->__prev_ = nullptr;
    }
    // OK not to set __front->__next_ = nullptr as __front is not part of the list anymore
    return __front;
  }

  _LIBCPP_HIDE_FROM_ABI void __remove(_Node* __node) noexcept {
    if (__node->__prev_) {
      // prev exists, set its next to our next to skip __node
      __node->__prev_->__next_ = __node->__next_;
      if (__node->__next_) {
        __node->__next_->__prev_ = __node->__prev_;
      }
    } else {
      _LIBCPP_ASSERT_INTERNAL(__node == __head_, "Node to be removed has no prev node, so it has to be the head");
      __pop_front();
    }
  }

  _LIBCPP_HIDE_FROM_ABI bool __is_head(_Node* __node) noexcept { return __node == __head_; }

private:
  _Node* __head_ = nullptr;
};

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___STOP_TOKEN_INTRUSIVE_LIST_VIEW_H

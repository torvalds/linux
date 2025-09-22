//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_ALIASING_ITERATOR_H
#define _LIBCPP___ITERATOR_ALIASING_ITERATOR_H

#include <__config>
#include <__iterator/iterator_traits.h>
#include <__memory/pointer_traits.h>
#include <__type_traits/is_trivial.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

// This iterator wrapper is used to type-pun an iterator to return a different type. This is done without UB by not
// actually punning the type, but instead inspecting the object representation of the base type and copying that into
// an instance of the alias type. For that reason the alias type has to be trivial. The alias is returned as a prvalue
// when derferencing the iterator, since it is temporary storage. This wrapper is used to vectorize some algorithms.

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _BaseIter, class _Alias>
struct __aliasing_iterator_wrapper {
  class __iterator {
    _BaseIter __base_ = nullptr;

    using __iter_traits     = iterator_traits<_BaseIter>;
    using __base_value_type = typename __iter_traits::value_type;

    static_assert(__has_random_access_iterator_category<_BaseIter>::value,
                  "The base iterator has to be a random access iterator!");

  public:
    using iterator_category = random_access_iterator_tag;
    using value_type        = _Alias;
    using difference_type   = ptrdiff_t;
    using reference         = value_type&;
    using pointer           = value_type*;

    static_assert(is_trivial<value_type>::value);
    static_assert(sizeof(__base_value_type) == sizeof(value_type));

    _LIBCPP_HIDE_FROM_ABI __iterator() = default;
    _LIBCPP_HIDE_FROM_ABI __iterator(_BaseIter __base) _NOEXCEPT : __base_(__base) {}

    _LIBCPP_HIDE_FROM_ABI __iterator& operator++() _NOEXCEPT {
      ++__base_;
      return *this;
    }

    _LIBCPP_HIDE_FROM_ABI __iterator operator++(int) _NOEXCEPT {
      __iterator __tmp(*this);
      ++__base_;
      return __tmp;
    }

    _LIBCPP_HIDE_FROM_ABI __iterator& operator--() _NOEXCEPT {
      --__base_;
      return *this;
    }

    _LIBCPP_HIDE_FROM_ABI __iterator operator--(int) _NOEXCEPT {
      __iterator __tmp(*this);
      --__base_;
      return __tmp;
    }

    _LIBCPP_HIDE_FROM_ABI friend __iterator operator+(__iterator __iter, difference_type __n) _NOEXCEPT {
      return __iterator(__iter.__base_ + __n);
    }

    _LIBCPP_HIDE_FROM_ABI friend __iterator operator+(difference_type __n, __iterator __iter) _NOEXCEPT {
      return __iterator(__n + __iter.__base_);
    }

    _LIBCPP_HIDE_FROM_ABI __iterator& operator+=(difference_type __n) _NOEXCEPT {
      __base_ += __n;
      return *this;
    }

    _LIBCPP_HIDE_FROM_ABI friend __iterator operator-(__iterator __iter, difference_type __n) _NOEXCEPT {
      return __iterator(__iter.__base_ - __n);
    }

    _LIBCPP_HIDE_FROM_ABI friend difference_type operator-(__iterator __lhs, __iterator __rhs) _NOEXCEPT {
      return __lhs.__base_ - __rhs.__base_;
    }

    _LIBCPP_HIDE_FROM_ABI __iterator& operator-=(difference_type __n) _NOEXCEPT {
      __base_ -= __n;
      return *this;
    }

    _LIBCPP_HIDE_FROM_ABI _BaseIter __base() const _NOEXCEPT { return __base_; }

    _LIBCPP_HIDE_FROM_ABI _Alias operator*() const _NOEXCEPT {
      _Alias __val;
      __builtin_memcpy(&__val, std::__to_address(__base_), sizeof(value_type));
      return __val;
    }

    _LIBCPP_HIDE_FROM_ABI value_type operator[](difference_type __n) const _NOEXCEPT { return *(*this + __n); }

    _LIBCPP_HIDE_FROM_ABI friend bool operator==(const __iterator& __lhs, const __iterator& __rhs) _NOEXCEPT {
      return __lhs.__base_ == __rhs.__base_;
    }

    _LIBCPP_HIDE_FROM_ABI friend bool operator!=(const __iterator& __lhs, const __iterator& __rhs) _NOEXCEPT {
      return __lhs.__base_ != __rhs.__base_;
    }
  };
};

// This is required to avoid ADL instantiations on _BaseT
template <class _BaseT, class _Alias>
using __aliasing_iterator = typename __aliasing_iterator_wrapper<_BaseT, _Alias>::__iterator;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_ALIASING_ITERATOR_H

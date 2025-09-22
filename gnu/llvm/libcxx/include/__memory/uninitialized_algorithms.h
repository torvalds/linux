// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_UNINITIALIZED_ALGORITHMS_H
#define _LIBCPP___MEMORY_UNINITIALIZED_ALGORITHMS_H

#include <__algorithm/copy.h>
#include <__algorithm/move.h>
#include <__algorithm/unwrap_iter.h>
#include <__algorithm/unwrap_range.h>
#include <__config>
#include <__iterator/iterator_traits.h>
#include <__iterator/reverse_iterator.h>
#include <__memory/addressof.h>
#include <__memory/allocator_traits.h>
#include <__memory/construct_at.h>
#include <__memory/pointer_traits.h>
#include <__memory/voidify.h>
#include <__type_traits/extent.h>
#include <__type_traits/is_array.h>
#include <__type_traits/is_constant_evaluated.h>
#include <__type_traits/is_trivially_assignable.h>
#include <__type_traits/is_trivially_constructible.h>
#include <__type_traits/is_trivially_relocatable.h>
#include <__type_traits/is_unbounded_array.h>
#include <__type_traits/negation.h>
#include <__type_traits/remove_const.h>
#include <__type_traits/remove_extent.h>
#include <__utility/exception_guard.h>
#include <__utility/move.h>
#include <__utility/pair.h>
#include <new>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

struct __always_false {
  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool operator()(_Args&&...) const _NOEXCEPT {
    return false;
  }
};

// uninitialized_copy

template <class _ValueType, class _InputIterator, class _Sentinel1, class _ForwardIterator, class _EndPredicate>
inline _LIBCPP_HIDE_FROM_ABI pair<_InputIterator, _ForwardIterator> __uninitialized_copy(
    _InputIterator __ifirst, _Sentinel1 __ilast, _ForwardIterator __ofirst, _EndPredicate __stop_copying) {
  _ForwardIterator __idx = __ofirst;
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif
    for (; __ifirst != __ilast && !__stop_copying(__idx); ++__ifirst, (void)++__idx)
      ::new (std::__voidify(*__idx)) _ValueType(*__ifirst);
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    std::__destroy(__ofirst, __idx);
    throw;
  }
#endif

  return pair<_InputIterator, _ForwardIterator>(std::move(__ifirst), std::move(__idx));
}

template <class _InputIterator, class _ForwardIterator>
_LIBCPP_HIDE_FROM_ABI _ForwardIterator
uninitialized_copy(_InputIterator __ifirst, _InputIterator __ilast, _ForwardIterator __ofirst) {
  typedef typename iterator_traits<_ForwardIterator>::value_type _ValueType;
  auto __result = std::__uninitialized_copy<_ValueType>(
      std::move(__ifirst), std::move(__ilast), std::move(__ofirst), __always_false());
  return std::move(__result.second);
}

// uninitialized_copy_n

template <class _ValueType, class _InputIterator, class _Size, class _ForwardIterator, class _EndPredicate>
inline _LIBCPP_HIDE_FROM_ABI pair<_InputIterator, _ForwardIterator>
__uninitialized_copy_n(_InputIterator __ifirst, _Size __n, _ForwardIterator __ofirst, _EndPredicate __stop_copying) {
  _ForwardIterator __idx = __ofirst;
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif
    for (; __n > 0 && !__stop_copying(__idx); ++__ifirst, (void)++__idx, (void)--__n)
      ::new (std::__voidify(*__idx)) _ValueType(*__ifirst);
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    std::__destroy(__ofirst, __idx);
    throw;
  }
#endif

  return pair<_InputIterator, _ForwardIterator>(std::move(__ifirst), std::move(__idx));
}

template <class _InputIterator, class _Size, class _ForwardIterator>
inline _LIBCPP_HIDE_FROM_ABI _ForwardIterator
uninitialized_copy_n(_InputIterator __ifirst, _Size __n, _ForwardIterator __ofirst) {
  typedef typename iterator_traits<_ForwardIterator>::value_type _ValueType;
  auto __result =
      std::__uninitialized_copy_n<_ValueType>(std::move(__ifirst), __n, std::move(__ofirst), __always_false());
  return std::move(__result.second);
}

// uninitialized_fill

template <class _ValueType, class _ForwardIterator, class _Sentinel, class _Tp>
inline _LIBCPP_HIDE_FROM_ABI _ForwardIterator
__uninitialized_fill(_ForwardIterator __first, _Sentinel __last, const _Tp& __x) {
  _ForwardIterator __idx = __first;
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif
    for (; __idx != __last; ++__idx)
      ::new (std::__voidify(*__idx)) _ValueType(__x);
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    std::__destroy(__first, __idx);
    throw;
  }
#endif

  return __idx;
}

template <class _ForwardIterator, class _Tp>
inline _LIBCPP_HIDE_FROM_ABI void
uninitialized_fill(_ForwardIterator __first, _ForwardIterator __last, const _Tp& __x) {
  typedef typename iterator_traits<_ForwardIterator>::value_type _ValueType;
  (void)std::__uninitialized_fill<_ValueType>(__first, __last, __x);
}

// uninitialized_fill_n

template <class _ValueType, class _ForwardIterator, class _Size, class _Tp>
inline _LIBCPP_HIDE_FROM_ABI _ForwardIterator
__uninitialized_fill_n(_ForwardIterator __first, _Size __n, const _Tp& __x) {
  _ForwardIterator __idx = __first;
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif
    for (; __n > 0; ++__idx, (void)--__n)
      ::new (std::__voidify(*__idx)) _ValueType(__x);
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    std::__destroy(__first, __idx);
    throw;
  }
#endif

  return __idx;
}

template <class _ForwardIterator, class _Size, class _Tp>
inline _LIBCPP_HIDE_FROM_ABI _ForwardIterator
uninitialized_fill_n(_ForwardIterator __first, _Size __n, const _Tp& __x) {
  typedef typename iterator_traits<_ForwardIterator>::value_type _ValueType;
  return std::__uninitialized_fill_n<_ValueType>(__first, __n, __x);
}

#if _LIBCPP_STD_VER >= 17

// uninitialized_default_construct

template <class _ValueType, class _ForwardIterator, class _Sentinel>
inline _LIBCPP_HIDE_FROM_ABI _ForwardIterator
__uninitialized_default_construct(_ForwardIterator __first, _Sentinel __last) {
  auto __idx = __first;
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#  endif
    for (; __idx != __last; ++__idx)
      ::new (std::__voidify(*__idx)) _ValueType;
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    std::__destroy(__first, __idx);
    throw;
  }
#  endif

  return __idx;
}

template <class _ForwardIterator>
inline _LIBCPP_HIDE_FROM_ABI void uninitialized_default_construct(_ForwardIterator __first, _ForwardIterator __last) {
  using _ValueType = typename iterator_traits<_ForwardIterator>::value_type;
  (void)std::__uninitialized_default_construct<_ValueType>(std::move(__first), std::move(__last));
}

// uninitialized_default_construct_n

template <class _ValueType, class _ForwardIterator, class _Size>
inline _LIBCPP_HIDE_FROM_ABI _ForwardIterator __uninitialized_default_construct_n(_ForwardIterator __first, _Size __n) {
  auto __idx = __first;
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#  endif
    for (; __n > 0; ++__idx, (void)--__n)
      ::new (std::__voidify(*__idx)) _ValueType;
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    std::__destroy(__first, __idx);
    throw;
  }
#  endif

  return __idx;
}

template <class _ForwardIterator, class _Size>
inline _LIBCPP_HIDE_FROM_ABI _ForwardIterator uninitialized_default_construct_n(_ForwardIterator __first, _Size __n) {
  using _ValueType = typename iterator_traits<_ForwardIterator>::value_type;
  return std::__uninitialized_default_construct_n<_ValueType>(std::move(__first), __n);
}

// uninitialized_value_construct

template <class _ValueType, class _ForwardIterator, class _Sentinel>
inline _LIBCPP_HIDE_FROM_ABI _ForwardIterator
__uninitialized_value_construct(_ForwardIterator __first, _Sentinel __last) {
  auto __idx = __first;
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#  endif
    for (; __idx != __last; ++__idx)
      ::new (std::__voidify(*__idx)) _ValueType();
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    std::__destroy(__first, __idx);
    throw;
  }
#  endif

  return __idx;
}

template <class _ForwardIterator>
inline _LIBCPP_HIDE_FROM_ABI void uninitialized_value_construct(_ForwardIterator __first, _ForwardIterator __last) {
  using _ValueType = typename iterator_traits<_ForwardIterator>::value_type;
  (void)std::__uninitialized_value_construct<_ValueType>(std::move(__first), std::move(__last));
}

// uninitialized_value_construct_n

template <class _ValueType, class _ForwardIterator, class _Size>
inline _LIBCPP_HIDE_FROM_ABI _ForwardIterator __uninitialized_value_construct_n(_ForwardIterator __first, _Size __n) {
  auto __idx = __first;
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#  endif
    for (; __n > 0; ++__idx, (void)--__n)
      ::new (std::__voidify(*__idx)) _ValueType();
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    std::__destroy(__first, __idx);
    throw;
  }
#  endif

  return __idx;
}

template <class _ForwardIterator, class _Size>
inline _LIBCPP_HIDE_FROM_ABI _ForwardIterator uninitialized_value_construct_n(_ForwardIterator __first, _Size __n) {
  using _ValueType = typename iterator_traits<_ForwardIterator>::value_type;
  return std::__uninitialized_value_construct_n<_ValueType>(std::move(__first), __n);
}

// uninitialized_move

template <class _ValueType,
          class _InputIterator,
          class _Sentinel1,
          class _ForwardIterator,
          class _EndPredicate,
          class _IterMove>
inline _LIBCPP_HIDE_FROM_ABI pair<_InputIterator, _ForwardIterator> __uninitialized_move(
    _InputIterator __ifirst,
    _Sentinel1 __ilast,
    _ForwardIterator __ofirst,
    _EndPredicate __stop_moving,
    _IterMove __iter_move) {
  auto __idx = __ofirst;
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#  endif
    for (; __ifirst != __ilast && !__stop_moving(__idx); ++__idx, (void)++__ifirst) {
      ::new (std::__voidify(*__idx)) _ValueType(__iter_move(__ifirst));
    }
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    std::__destroy(__ofirst, __idx);
    throw;
  }
#  endif

  return {std::move(__ifirst), std::move(__idx)};
}

template <class _InputIterator, class _ForwardIterator>
inline _LIBCPP_HIDE_FROM_ABI _ForwardIterator
uninitialized_move(_InputIterator __ifirst, _InputIterator __ilast, _ForwardIterator __ofirst) {
  using _ValueType = typename iterator_traits<_ForwardIterator>::value_type;
  auto __iter_move = [](auto&& __iter) -> decltype(auto) { return std::move(*__iter); };

  auto __result = std::__uninitialized_move<_ValueType>(
      std::move(__ifirst), std::move(__ilast), std::move(__ofirst), __always_false(), __iter_move);
  return std::move(__result.second);
}

// uninitialized_move_n

template <class _ValueType,
          class _InputIterator,
          class _Size,
          class _ForwardIterator,
          class _EndPredicate,
          class _IterMove>
inline _LIBCPP_HIDE_FROM_ABI pair<_InputIterator, _ForwardIterator> __uninitialized_move_n(
    _InputIterator __ifirst, _Size __n, _ForwardIterator __ofirst, _EndPredicate __stop_moving, _IterMove __iter_move) {
  auto __idx = __ofirst;
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#  endif
    for (; __n > 0 && !__stop_moving(__idx); ++__idx, (void)++__ifirst, --__n)
      ::new (std::__voidify(*__idx)) _ValueType(__iter_move(__ifirst));
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    std::__destroy(__ofirst, __idx);
    throw;
  }
#  endif

  return {std::move(__ifirst), std::move(__idx)};
}

template <class _InputIterator, class _Size, class _ForwardIterator>
inline _LIBCPP_HIDE_FROM_ABI pair<_InputIterator, _ForwardIterator>
uninitialized_move_n(_InputIterator __ifirst, _Size __n, _ForwardIterator __ofirst) {
  using _ValueType = typename iterator_traits<_ForwardIterator>::value_type;
  auto __iter_move = [](auto&& __iter) -> decltype(auto) { return std::move(*__iter); };

  return std::__uninitialized_move_n<_ValueType>(
      std::move(__ifirst), __n, std::move(__ofirst), __always_false(), __iter_move);
}

// TODO: Rewrite this to iterate left to right and use reverse_iterators when calling
// Destroys every element in the range [first, last) FROM RIGHT TO LEFT using allocator
// destruction. If elements are themselves C-style arrays, they are recursively destroyed
// in the same manner.
//
// This function assumes that destructors do not throw, and that the allocator is bound to
// the correct type.
template <class _Alloc,
          class _BidirIter,
          __enable_if_t<__has_bidirectional_iterator_category<_BidirIter>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI constexpr void
__allocator_destroy_multidimensional(_Alloc& __alloc, _BidirIter __first, _BidirIter __last) noexcept {
  using _ValueType = typename iterator_traits<_BidirIter>::value_type;
  static_assert(is_same_v<typename allocator_traits<_Alloc>::value_type, _ValueType>,
                "The allocator should already be rebound to the correct type");

  if (__first == __last)
    return;

  if constexpr (is_array_v<_ValueType>) {
    static_assert(!__libcpp_is_unbounded_array<_ValueType>::value,
                  "arrays of unbounded arrays don't exist, but if they did we would mess up here");

    using _Element = remove_extent_t<_ValueType>;
    __allocator_traits_rebind_t<_Alloc, _Element> __elem_alloc(__alloc);
    do {
      --__last;
      decltype(auto) __array = *__last;
      std::__allocator_destroy_multidimensional(__elem_alloc, __array, __array + extent_v<_ValueType>);
    } while (__last != __first);
  } else {
    do {
      --__last;
      allocator_traits<_Alloc>::destroy(__alloc, std::addressof(*__last));
    } while (__last != __first);
  }
}

// Constructs the object at the given location using the allocator's construct method.
//
// If the object being constructed is an array, each element of the array is allocator-constructed,
// recursively. If an exception is thrown during the construction of an array, the initialized
// elements are destroyed in reverse order of initialization using allocator destruction.
//
// This function assumes that the allocator is bound to the correct type.
template <class _Alloc, class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr void __allocator_construct_at_multidimensional(_Alloc& __alloc, _Tp* __loc) {
  static_assert(is_same_v<typename allocator_traits<_Alloc>::value_type, _Tp>,
                "The allocator should already be rebound to the correct type");

  if constexpr (is_array_v<_Tp>) {
    using _Element = remove_extent_t<_Tp>;
    __allocator_traits_rebind_t<_Alloc, _Element> __elem_alloc(__alloc);
    size_t __i   = 0;
    _Tp& __array = *__loc;

    // If an exception is thrown, destroy what we have constructed so far in reverse order.
    auto __guard = std::__make_exception_guard([&]() {
      std::__allocator_destroy_multidimensional(__elem_alloc, __array, __array + __i);
    });

    for (; __i != extent_v<_Tp>; ++__i) {
      std::__allocator_construct_at_multidimensional(__elem_alloc, std::addressof(__array[__i]));
    }
    __guard.__complete();
  } else {
    allocator_traits<_Alloc>::construct(__alloc, __loc);
  }
}

// Constructs the object at the given location using the allocator's construct method, passing along
// the provided argument.
//
// If the object being constructed is an array, the argument is also assumed to be an array. Each
// each element of the array being constructed is allocator-constructed from the corresponding
// element of the argument array. If an exception is thrown during the construction of an array,
// the initialized elements are destroyed in reverse order of initialization using allocator
// destruction.
//
// This function assumes that the allocator is bound to the correct type.
template <class _Alloc, class _Tp, class _Arg>
_LIBCPP_HIDE_FROM_ABI constexpr void
__allocator_construct_at_multidimensional(_Alloc& __alloc, _Tp* __loc, _Arg const& __arg) {
  static_assert(is_same_v<typename allocator_traits<_Alloc>::value_type, _Tp>,
                "The allocator should already be rebound to the correct type");

  if constexpr (is_array_v<_Tp>) {
    static_assert(is_array_v<_Arg>,
                  "Provided non-array initialization argument to __allocator_construct_at_multidimensional when "
                  "trying to construct an array.");

    using _Element = remove_extent_t<_Tp>;
    __allocator_traits_rebind_t<_Alloc, _Element> __elem_alloc(__alloc);
    size_t __i   = 0;
    _Tp& __array = *__loc;

    // If an exception is thrown, destroy what we have constructed so far in reverse order.
    auto __guard = std::__make_exception_guard([&]() {
      std::__allocator_destroy_multidimensional(__elem_alloc, __array, __array + __i);
    });
    for (; __i != extent_v<_Tp>; ++__i) {
      std::__allocator_construct_at_multidimensional(__elem_alloc, std::addressof(__array[__i]), __arg[__i]);
    }
    __guard.__complete();
  } else {
    allocator_traits<_Alloc>::construct(__alloc, __loc, __arg);
  }
}

// Given a range starting at it and containing n elements, initializes each element in the
// range from left to right using the construct method of the allocator (rebound to the
// correct type).
//
// If an exception is thrown, the initialized elements are destroyed in reverse order of
// initialization using allocator_traits destruction. If the elements in the range are C-style
// arrays, they are initialized element-wise using allocator construction, and recursively so.
template <class _Alloc,
          class _BidirIter,
          class _Tp,
          class _Size = typename iterator_traits<_BidirIter>::difference_type>
_LIBCPP_HIDE_FROM_ABI constexpr void
__uninitialized_allocator_fill_n_multidimensional(_Alloc& __alloc, _BidirIter __it, _Size __n, _Tp const& __value) {
  using _ValueType = typename iterator_traits<_BidirIter>::value_type;
  __allocator_traits_rebind_t<_Alloc, _ValueType> __value_alloc(__alloc);
  _BidirIter __begin = __it;

  // If an exception is thrown, destroy what we have constructed so far in reverse order.
  auto __guard =
      std::__make_exception_guard([&]() { std::__allocator_destroy_multidimensional(__value_alloc, __begin, __it); });
  for (; __n != 0; --__n, ++__it) {
    std::__allocator_construct_at_multidimensional(__value_alloc, std::addressof(*__it), __value);
  }
  __guard.__complete();
}

// Same as __uninitialized_allocator_fill_n_multidimensional, but doesn't pass any initialization argument
// to the allocator's construct method, which results in value initialization.
template <class _Alloc, class _BidirIter, class _Size = typename iterator_traits<_BidirIter>::difference_type>
_LIBCPP_HIDE_FROM_ABI constexpr void
__uninitialized_allocator_value_construct_n_multidimensional(_Alloc& __alloc, _BidirIter __it, _Size __n) {
  using _ValueType = typename iterator_traits<_BidirIter>::value_type;
  __allocator_traits_rebind_t<_Alloc, _ValueType> __value_alloc(__alloc);
  _BidirIter __begin = __it;

  // If an exception is thrown, destroy what we have constructed so far in reverse order.
  auto __guard =
      std::__make_exception_guard([&]() { std::__allocator_destroy_multidimensional(__value_alloc, __begin, __it); });
  for (; __n != 0; --__n, ++__it) {
    std::__allocator_construct_at_multidimensional(__value_alloc, std::addressof(*__it));
  }
  __guard.__complete();
}

#endif // _LIBCPP_STD_VER >= 17

// Destroy all elements in [__first, __last) from left to right using allocator destruction.
template <class _Alloc, class _Iter, class _Sent>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void
__allocator_destroy(_Alloc& __alloc, _Iter __first, _Sent __last) {
  for (; __first != __last; ++__first)
    allocator_traits<_Alloc>::destroy(__alloc, std::__to_address(__first));
}

template <class _Alloc, class _Iter>
class _AllocatorDestroyRangeReverse {
public:
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14
  _AllocatorDestroyRangeReverse(_Alloc& __alloc, _Iter& __first, _Iter& __last)
      : __alloc_(__alloc), __first_(__first), __last_(__last) {}

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 void operator()() const {
    std::__allocator_destroy(__alloc_, std::reverse_iterator<_Iter>(__last_), std::reverse_iterator<_Iter>(__first_));
  }

private:
  _Alloc& __alloc_;
  _Iter& __first_;
  _Iter& __last_;
};

// Copy-construct [__first1, __last1) in [__first2, __first2 + N), where N is distance(__first1, __last1).
//
// The caller has to ensure that __first2 can hold at least N uninitialized elements. If an exception is thrown the
// already copied elements are destroyed in reverse order of their construction.
template <class _Alloc, class _Iter1, class _Sent1, class _Iter2>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Iter2
__uninitialized_allocator_copy_impl(_Alloc& __alloc, _Iter1 __first1, _Sent1 __last1, _Iter2 __first2) {
  auto __destruct_first = __first2;
  auto __guard =
      std::__make_exception_guard(_AllocatorDestroyRangeReverse<_Alloc, _Iter2>(__alloc, __destruct_first, __first2));
  while (__first1 != __last1) {
    allocator_traits<_Alloc>::construct(__alloc, std::__to_address(__first2), *__first1);
    ++__first1;
    ++__first2;
  }
  __guard.__complete();
  return __first2;
}

template <class _Alloc, class _Type>
struct __allocator_has_trivial_copy_construct : _Not<__has_construct<_Alloc, _Type*, const _Type&> > {};

template <class _Type>
struct __allocator_has_trivial_copy_construct<allocator<_Type>, _Type> : true_type {};

template <class _Alloc,
          class _In,
          class _RawTypeIn = __remove_const_t<_In>,
          class _Out,
          __enable_if_t<
              // using _RawTypeIn because of the allocator<T const> extension
              is_trivially_copy_constructible<_RawTypeIn>::value && is_trivially_copy_assignable<_RawTypeIn>::value &&
                  is_same<__remove_const_t<_In>, __remove_const_t<_Out> >::value &&
                  __allocator_has_trivial_copy_construct<_Alloc, _RawTypeIn>::value,
              int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Out*
__uninitialized_allocator_copy_impl(_Alloc&, _In* __first1, _In* __last1, _Out* __first2) {
  // TODO: Remove the const_cast once we drop support for std::allocator<T const>
  if (__libcpp_is_constant_evaluated()) {
    while (__first1 != __last1) {
      std::__construct_at(std::__to_address(__first2), *__first1);
      ++__first1;
      ++__first2;
    }
    return __first2;
  } else {
    return std::copy(__first1, __last1, const_cast<_RawTypeIn*>(__first2));
  }
}

template <class _Alloc, class _Iter1, class _Sent1, class _Iter2>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Iter2
__uninitialized_allocator_copy(_Alloc& __alloc, _Iter1 __first1, _Sent1 __last1, _Iter2 __first2) {
  auto __unwrapped_range = std::__unwrap_range(__first1, __last1);
  auto __result          = std::__uninitialized_allocator_copy_impl(
      __alloc, __unwrapped_range.first, __unwrapped_range.second, std::__unwrap_iter(__first2));
  return std::__rewrap_iter(__first2, __result);
}

template <class _Alloc, class _Type>
struct __allocator_has_trivial_move_construct : _Not<__has_construct<_Alloc, _Type*, _Type&&> > {};

template <class _Type>
struct __allocator_has_trivial_move_construct<allocator<_Type>, _Type> : true_type {};

template <class _Alloc, class _Tp>
struct __allocator_has_trivial_destroy : _Not<__has_destroy<_Alloc, _Tp*> > {};

template <class _Tp, class _Up>
struct __allocator_has_trivial_destroy<allocator<_Tp>, _Up> : true_type {};

// __uninitialized_allocator_relocate relocates the objects in [__first, __last) into __result.
// Relocation means that the objects in [__first, __last) are placed into __result as-if by move-construct and destroy,
// except that the move constructor and destructor may never be called if they are known to be equivalent to a memcpy.
//
// Preconditions:  __result doesn't contain any objects and [__first, __last) contains objects
// Postconditions: __result contains the objects from [__first, __last) and
//                 [__first, __last) doesn't contain any objects
//
// The strong exception guarantee is provided if any of the following are true:
// - is_nothrow_move_constructible<_Tp>
// - is_copy_constructible<_Tp>
// - __libcpp_is_trivially_relocatable<_Tp>
template <class _Alloc, class _Tp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 void
__uninitialized_allocator_relocate(_Alloc& __alloc, _Tp* __first, _Tp* __last, _Tp* __result) {
  static_assert(__is_cpp17_move_insertable<_Alloc>::value,
                "The specified type does not meet the requirements of Cpp17MoveInsertable");
  if (__libcpp_is_constant_evaluated() || !__libcpp_is_trivially_relocatable<_Tp>::value ||
      !__allocator_has_trivial_move_construct<_Alloc, _Tp>::value ||
      !__allocator_has_trivial_destroy<_Alloc, _Tp>::value) {
    auto __destruct_first = __result;
    auto __guard =
        std::__make_exception_guard(_AllocatorDestroyRangeReverse<_Alloc, _Tp*>(__alloc, __destruct_first, __result));
    auto __iter = __first;
    while (__iter != __last) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
      allocator_traits<_Alloc>::construct(__alloc, __result, std::move_if_noexcept(*__iter));
#else
      allocator_traits<_Alloc>::construct(__alloc, __result, std::move(*__iter));
#endif
      ++__iter;
      ++__result;
    }
    __guard.__complete();
    std::__allocator_destroy(__alloc, __first, __last);
  } else {
    __builtin_memcpy(const_cast<__remove_const_t<_Tp>*>(__result), __first, sizeof(_Tp) * (__last - __first));
  }
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___MEMORY_UNINITIALIZED_ALGORITHMS_H

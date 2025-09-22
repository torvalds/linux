//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_TRANSACTION_H
#define _LIBCPP___UTILITY_TRANSACTION_H

#include <__assert>
#include <__config>
#include <__type_traits/is_nothrow_constructible.h>
#include <__utility/exchange.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// __exception_guard is a helper class for writing code with the strong exception guarantee.
//
// When writing code that can throw an exception, one can store rollback instructions in an
// exception guard so that if an exception is thrown at any point during the lifetime of the
// exception guard, it will be rolled back automatically. When the exception guard is done, one
// must mark it as being complete so it isn't rolled back when the exception guard is destroyed.
//
// Exception guards are not default constructible, they can't be copied or assigned to, but
// they can be moved around for convenience.
//
// __exception_guard is a no-op in -fno-exceptions mode to produce better code-gen. This means
// that we don't provide the strong exception guarantees. However, Clang doesn't generate cleanup
// code with exceptions disabled, so even if we wanted to provide the strong exception guarantees
// we couldn't. This is also only relevant for constructs with a stack of
// -fexceptions > -fno-exceptions > -fexceptions code, since the exception can't be caught where
// exceptions are disabled. While -fexceptions > -fno-exceptions is quite common
// (e.g. libc++.dylib > -fno-exceptions), having another layer with exceptions enabled seems a lot
// less common, especially one that tries to catch an exception through -fno-exceptions code.
//
// __exception_guard can help greatly simplify code that would normally be cluttered by
// `#if _LIBCPP_HAS_NO_EXCEPTIONS`. For example:
//
//    template <class Iterator, class Size, class OutputIterator>
//    Iterator uninitialized_copy_n(Iterator iter, Size n, OutputIterator out) {
//        typedef typename iterator_traits<Iterator>::value_type value_type;
//        __exception_guard guard([start=out, &out] {
//            std::destroy(start, out);
//        });
//
//        for (; n > 0; ++iter, ++out, --n) {
//            ::new ((void*)std::addressof(*out)) value_type(*iter);
//        }
//        guard.__complete();
//        return out;
//    }
//

template <class _Rollback>
struct __exception_guard_exceptions {
  __exception_guard_exceptions() = delete;

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 explicit __exception_guard_exceptions(_Rollback __rollback)
      : __rollback_(std::move(__rollback)), __completed_(false) {}

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20
  __exception_guard_exceptions(__exception_guard_exceptions&& __other)
      _NOEXCEPT_(is_nothrow_move_constructible<_Rollback>::value)
      : __rollback_(std::move(__other.__rollback_)), __completed_(__other.__completed_) {
    __other.__completed_ = true;
  }

  __exception_guard_exceptions(__exception_guard_exceptions const&)            = delete;
  __exception_guard_exceptions& operator=(__exception_guard_exceptions const&) = delete;
  __exception_guard_exceptions& operator=(__exception_guard_exceptions&&)      = delete;

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void __complete() _NOEXCEPT { __completed_ = true; }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 ~__exception_guard_exceptions() {
    if (!__completed_)
      __rollback_();
  }

private:
  _Rollback __rollback_;
  bool __completed_;
};

_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(__exception_guard_exceptions);

template <class _Rollback>
struct __exception_guard_noexceptions {
  __exception_guard_noexceptions() = delete;
  _LIBCPP_HIDE_FROM_ABI
  _LIBCPP_CONSTEXPR_SINCE_CXX20 _LIBCPP_NODEBUG explicit __exception_guard_noexceptions(_Rollback) {}

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _LIBCPP_NODEBUG
  __exception_guard_noexceptions(__exception_guard_noexceptions&& __other)
      _NOEXCEPT_(is_nothrow_move_constructible<_Rollback>::value)
      : __completed_(__other.__completed_) {
    __other.__completed_ = true;
  }

  __exception_guard_noexceptions(__exception_guard_noexceptions const&)            = delete;
  __exception_guard_noexceptions& operator=(__exception_guard_noexceptions const&) = delete;
  __exception_guard_noexceptions& operator=(__exception_guard_noexceptions&&)      = delete;

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _LIBCPP_NODEBUG void __complete() _NOEXCEPT {
    __completed_ = true;
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _LIBCPP_NODEBUG ~__exception_guard_noexceptions() {
    _LIBCPP_ASSERT_INTERNAL(__completed_, "__exception_guard not completed with exceptions disabled");
  }

private:
  bool __completed_ = false;
};

_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(__exception_guard_noexceptions);

#ifdef _LIBCPP_HAS_NO_EXCEPTIONS
template <class _Rollback>
using __exception_guard = __exception_guard_noexceptions<_Rollback>;
#else
template <class _Rollback>
using __exception_guard = __exception_guard_exceptions<_Rollback>;
#endif

template <class _Rollback>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR __exception_guard<_Rollback> __make_exception_guard(_Rollback __rollback) {
  return __exception_guard<_Rollback>(std::move(__rollback));
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___UTILITY_TRANSACTION_H

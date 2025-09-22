//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___EXCEPTION_NESTED_EXCEPTION_H
#define _LIBCPP___EXCEPTION_NESTED_EXCEPTION_H

#include <__config>
#include <__exception/exception_ptr.h>
#include <__memory/addressof.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_base_of.h>
#include <__type_traits/is_class.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_final.h>
#include <__type_traits/is_polymorphic.h>
#include <__utility/forward.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

namespace std { // purposefully not using versioning namespace

class _LIBCPP_EXPORTED_FROM_ABI nested_exception {
  exception_ptr __ptr_;

public:
  nested_exception() _NOEXCEPT;
  _LIBCPP_HIDE_FROM_ABI nested_exception(const nested_exception&) _NOEXCEPT            = default;
  _LIBCPP_HIDE_FROM_ABI nested_exception& operator=(const nested_exception&) _NOEXCEPT = default;
  virtual ~nested_exception() _NOEXCEPT;

  // access functions
  _LIBCPP_NORETURN void rethrow_nested() const;
  _LIBCPP_HIDE_FROM_ABI exception_ptr nested_ptr() const _NOEXCEPT { return __ptr_; }
};

template <class _Tp>
struct __nested : public _Tp, public nested_exception {
  _LIBCPP_HIDE_FROM_ABI explicit __nested(const _Tp& __t) : _Tp(__t) {}
};

#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
template <class _Tp, class _Up, bool>
struct __throw_with_nested;

template <class _Tp, class _Up>
struct __throw_with_nested<_Tp, _Up, true> {
  _LIBCPP_NORETURN static inline _LIBCPP_HIDE_FROM_ABI void __do_throw(_Tp&& __t) {
    throw __nested<_Up>(std::forward<_Tp>(__t));
  }
};

template <class _Tp, class _Up>
struct __throw_with_nested<_Tp, _Up, false> {
  _LIBCPP_NORETURN static inline _LIBCPP_HIDE_FROM_ABI void __do_throw(_Tp&& __t) { throw std::forward<_Tp>(__t); }
};
#endif

template <class _Tp>
_LIBCPP_NORETURN _LIBCPP_HIDE_FROM_ABI void throw_with_nested(_Tp&& __t) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  using _Up = __decay_t<_Tp>;
  static_assert(is_copy_constructible<_Up>::value, "type thrown must be CopyConstructible");
  __throw_with_nested<_Tp,
                      _Up,
                      is_class<_Up>::value && !is_base_of<nested_exception, _Up>::value &&
                          !__libcpp_is_final<_Up>::value>::__do_throw(std::forward<_Tp>(__t));
#else
  ((void)__t);
  // FIXME: Make this abort
#endif
}

template <class _From, class _To>
struct __can_dynamic_cast
    : _BoolConstant< is_polymorphic<_From>::value &&
                     (!is_base_of<_To, _From>::value || is_convertible<const _From*, const _To*>::value)> {};

template <class _Ep, __enable_if_t< __can_dynamic_cast<_Ep, nested_exception>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI void rethrow_if_nested(const _Ep& __e) {
  const nested_exception* __nep = dynamic_cast<const nested_exception*>(std::addressof(__e));
  if (__nep)
    __nep->rethrow_nested();
}

template <class _Ep, __enable_if_t<!__can_dynamic_cast<_Ep, nested_exception>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI void rethrow_if_nested(const _Ep&) {}

} // namespace std

#endif // _LIBCPP___EXCEPTION_NESTED_EXCEPTION_H

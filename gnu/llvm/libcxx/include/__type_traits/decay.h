//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_DECAY_H
#define _LIBCPP___TYPE_TRAITS_DECAY_H

#include <__config>
#include <__type_traits/add_pointer.h>
#include <__type_traits/conditional.h>
#include <__type_traits/is_array.h>
#include <__type_traits/is_function.h>
#include <__type_traits/is_referenceable.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/remove_extent.h>
#include <__type_traits/remove_reference.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_builtin(__decay)
template <class _Tp>
using __decay_t _LIBCPP_NODEBUG = __decay(_Tp);

template <class _Tp>
struct decay {
  using type _LIBCPP_NODEBUG = __decay_t<_Tp>;
};

#else
template <class _Up, bool>
struct __decay {
  typedef _LIBCPP_NODEBUG __remove_cv_t<_Up> type;
};

template <class _Up>
struct __decay<_Up, true> {
public:
  typedef _LIBCPP_NODEBUG
      __conditional_t<is_array<_Up>::value,
                      __add_pointer_t<__remove_extent_t<_Up> >,
                      __conditional_t<is_function<_Up>::value, typename add_pointer<_Up>::type, __remove_cv_t<_Up> > >
          type;
};

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS decay {
private:
  typedef _LIBCPP_NODEBUG __libcpp_remove_reference_t<_Tp> _Up;

public:
  typedef _LIBCPP_NODEBUG typename __decay<_Up, __libcpp_is_referenceable<_Up>::value>::type type;
};

template <class _Tp>
using __decay_t = typename decay<_Tp>::type;
#endif // __has_builtin(__decay)

#if _LIBCPP_STD_VER >= 14
template <class _Tp>
using decay_t = __decay_t<_Tp>;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_DECAY_H

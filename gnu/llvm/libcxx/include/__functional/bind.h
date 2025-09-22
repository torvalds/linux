// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_BIND_H
#define _LIBCPP___FUNCTIONAL_BIND_H

#include <__config>
#include <__functional/invoke.h>
#include <__functional/weak_result_type.h>
#include <__fwd/functional.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_reference_wrapper.h>
#include <__type_traits/is_void.h>
#include <cstddef>
#include <tuple>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
struct is_bind_expression
    : _If< _IsSame<_Tp, __remove_cvref_t<_Tp> >::value, false_type, is_bind_expression<__remove_cvref_t<_Tp> > > {};

#if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_bind_expression_v = is_bind_expression<_Tp>::value;
#endif

template <class _Tp>
struct is_placeholder
    : _If< _IsSame<_Tp, __remove_cvref_t<_Tp> >::value,
           integral_constant<int, 0>,
           is_placeholder<__remove_cvref_t<_Tp> > > {};

#if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr int is_placeholder_v = is_placeholder<_Tp>::value;
#endif

namespace placeholders {

template <int _Np>
struct __ph {};

// C++17 recommends that we implement placeholders as `inline constexpr`, but allows
// implementing them as `extern <implementation-defined>`. Libc++ implements them as
// `extern const` in all standard modes to avoid an ABI break in C++03: making them
// `inline constexpr` requires removing their definition in the shared library to
// avoid ODR violations, which is an ABI break.
//
// In practice, since placeholders are empty, `extern const` is almost impossible
// to distinguish from `inline constexpr` from a usage stand point.
_LIBCPP_EXPORTED_FROM_ABI extern const __ph<1> _1;
_LIBCPP_EXPORTED_FROM_ABI extern const __ph<2> _2;
_LIBCPP_EXPORTED_FROM_ABI extern const __ph<3> _3;
_LIBCPP_EXPORTED_FROM_ABI extern const __ph<4> _4;
_LIBCPP_EXPORTED_FROM_ABI extern const __ph<5> _5;
_LIBCPP_EXPORTED_FROM_ABI extern const __ph<6> _6;
_LIBCPP_EXPORTED_FROM_ABI extern const __ph<7> _7;
_LIBCPP_EXPORTED_FROM_ABI extern const __ph<8> _8;
_LIBCPP_EXPORTED_FROM_ABI extern const __ph<9> _9;
_LIBCPP_EXPORTED_FROM_ABI extern const __ph<10> _10;

} // namespace placeholders

template <int _Np>
struct is_placeholder<placeholders::__ph<_Np> > : public integral_constant<int, _Np> {};

#ifndef _LIBCPP_CXX03_LANG

template <class _Tp, class _Uj>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Tp& __mu(reference_wrapper<_Tp> __t, _Uj&) {
  return __t.get();
}

template <class _Ti, class... _Uj, size_t... _Indx>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 typename __invoke_of<_Ti&, _Uj...>::type
__mu_expand(_Ti& __ti, tuple<_Uj...>& __uj, __tuple_indices<_Indx...>) {
  return __ti(std::forward<_Uj>(std::get<_Indx>(__uj))...);
}

template <class _Ti, class... _Uj, __enable_if_t<is_bind_expression<_Ti>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 typename __invoke_of<_Ti&, _Uj...>::type
__mu(_Ti& __ti, tuple<_Uj...>& __uj) {
  typedef typename __make_tuple_indices<sizeof...(_Uj)>::type __indices;
  return std::__mu_expand(__ti, __uj, __indices());
}

template <bool _IsPh, class _Ti, class _Uj>
struct __mu_return2 {};

template <class _Ti, class _Uj>
struct __mu_return2<true, _Ti, _Uj> {
  typedef typename tuple_element<is_placeholder<_Ti>::value - 1, _Uj>::type type;
};

template <class _Ti, class _Uj, __enable_if_t<0 < is_placeholder<_Ti>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20
typename __mu_return2<0 < is_placeholder<_Ti>::value, _Ti, _Uj>::type
__mu(_Ti&, _Uj& __uj) {
  const size_t __indx = is_placeholder<_Ti>::value - 1;
  return std::forward<typename tuple_element<__indx, _Uj>::type>(std::get<__indx>(__uj));
}

template <class _Ti,
          class _Uj,
          __enable_if_t<!is_bind_expression<_Ti>::value && is_placeholder<_Ti>::value == 0 &&
                            !__is_reference_wrapper<_Ti>::value,
                        int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Ti& __mu(_Ti& __ti, _Uj&) {
  return __ti;
}

template <class _Ti, bool _IsReferenceWrapper, bool _IsBindEx, bool _IsPh, class _TupleUj>
struct __mu_return_impl;

template <bool _Invokable, class _Ti, class... _Uj>
struct __mu_return_invokable // false
{
  typedef __nat type;
};

template <class _Ti, class... _Uj>
struct __mu_return_invokable<true, _Ti, _Uj...> {
  typedef typename __invoke_of<_Ti&, _Uj...>::type type;
};

template <class _Ti, class... _Uj>
struct __mu_return_impl<_Ti, false, true, false, tuple<_Uj...> >
    : public __mu_return_invokable<__invokable<_Ti&, _Uj...>::value, _Ti, _Uj...> {};

template <class _Ti, class _TupleUj>
struct __mu_return_impl<_Ti, false, false, true, _TupleUj> {
  typedef typename tuple_element<is_placeholder<_Ti>::value - 1, _TupleUj>::type&& type;
};

template <class _Ti, class _TupleUj>
struct __mu_return_impl<_Ti, true, false, false, _TupleUj> {
  typedef typename _Ti::type& type;
};

template <class _Ti, class _TupleUj>
struct __mu_return_impl<_Ti, false, false, false, _TupleUj> {
  typedef _Ti& type;
};

template <class _Ti, class _TupleUj>
struct __mu_return
    : public __mu_return_impl<
          _Ti,
          __is_reference_wrapper<_Ti>::value,
          is_bind_expression<_Ti>::value,
          0 < is_placeholder<_Ti>::value && is_placeholder<_Ti>::value <= tuple_size<_TupleUj>::value,
          _TupleUj> {};

template <class _Fp, class _BoundArgs, class _TupleUj>
struct __is_valid_bind_return {
  static const bool value = false;
};

template <class _Fp, class... _BoundArgs, class _TupleUj>
struct __is_valid_bind_return<_Fp, tuple<_BoundArgs...>, _TupleUj> {
  static const bool value = __invokable<_Fp, typename __mu_return<_BoundArgs, _TupleUj>::type...>::value;
};

template <class _Fp, class... _BoundArgs, class _TupleUj>
struct __is_valid_bind_return<_Fp, const tuple<_BoundArgs...>, _TupleUj> {
  static const bool value = __invokable<_Fp, typename __mu_return<const _BoundArgs, _TupleUj>::type...>::value;
};

template <class _Fp, class _BoundArgs, class _TupleUj, bool = __is_valid_bind_return<_Fp, _BoundArgs, _TupleUj>::value>
struct __bind_return;

template <class _Fp, class... _BoundArgs, class _TupleUj>
struct __bind_return<_Fp, tuple<_BoundArgs...>, _TupleUj, true> {
  typedef typename __invoke_of< _Fp&, typename __mu_return< _BoundArgs, _TupleUj >::type... >::type type;
};

template <class _Fp, class... _BoundArgs, class _TupleUj>
struct __bind_return<_Fp, const tuple<_BoundArgs...>, _TupleUj, true> {
  typedef typename __invoke_of< _Fp&, typename __mu_return< const _BoundArgs, _TupleUj >::type... >::type type;
};

template <class _Fp, class _BoundArgs, size_t... _Indx, class _Args>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 typename __bind_return<_Fp, _BoundArgs, _Args>::type
__apply_functor(_Fp& __f, _BoundArgs& __bound_args, __tuple_indices<_Indx...>, _Args&& __args) {
  return std::__invoke(__f, std::__mu(std::get<_Indx>(__bound_args), __args)...);
}

template <class _Fp, class... _BoundArgs>
class __bind : public __weak_result_type<__decay_t<_Fp> > {
protected:
  using _Fd = __decay_t<_Fp>;
  typedef tuple<__decay_t<_BoundArgs>...> _Td;

private:
  _Fd __f_;
  _Td __bound_args_;

  typedef typename __make_tuple_indices<sizeof...(_BoundArgs)>::type __indices;

public:
  template <
      class _Gp,
      class... _BA,
      __enable_if_t<is_constructible<_Fd, _Gp>::value && !is_same<__libcpp_remove_reference_t<_Gp>, __bind>::value,
                    int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 explicit __bind(_Gp&& __f, _BA&&... __bound_args)
      : __f_(std::forward<_Gp>(__f)), __bound_args_(std::forward<_BA>(__bound_args)...) {}

  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 typename __bind_return<_Fd, _Td, tuple<_Args&&...> >::type
  operator()(_Args&&... __args) {
    return std::__apply_functor(__f_, __bound_args_, __indices(), tuple<_Args&&...>(std::forward<_Args>(__args)...));
  }

  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20
  typename __bind_return<const _Fd, const _Td, tuple<_Args&&...> >::type
  operator()(_Args&&... __args) const {
    return std::__apply_functor(__f_, __bound_args_, __indices(), tuple<_Args&&...>(std::forward<_Args>(__args)...));
  }
};

template <class _Fp, class... _BoundArgs>
struct is_bind_expression<__bind<_Fp, _BoundArgs...> > : public true_type {};

template <class _Rp, class _Fp, class... _BoundArgs>
class __bind_r : public __bind<_Fp, _BoundArgs...> {
  typedef __bind<_Fp, _BoundArgs...> base;
  typedef typename base::_Fd _Fd;
  typedef typename base::_Td _Td;

public:
  typedef _Rp result_type;

  template <
      class _Gp,
      class... _BA,
      __enable_if_t<is_constructible<_Fd, _Gp>::value && !is_same<__libcpp_remove_reference_t<_Gp>, __bind_r>::value,
                    int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 explicit __bind_r(_Gp&& __f, _BA&&... __bound_args)
      : base(std::forward<_Gp>(__f), std::forward<_BA>(__bound_args)...) {}

  template <
      class... _Args,
      __enable_if_t<is_convertible<typename __bind_return<_Fd, _Td, tuple<_Args&&...> >::type, result_type>::value ||
                        is_void<_Rp>::value,
                    int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 result_type operator()(_Args&&... __args) {
    typedef __invoke_void_return_wrapper<_Rp> _Invoker;
    return _Invoker::__call(static_cast<base&>(*this), std::forward<_Args>(__args)...);
  }

  template <class... _Args,
            __enable_if_t<is_convertible<typename __bind_return<const _Fd, const _Td, tuple<_Args&&...> >::type,
                                         result_type>::value ||
                              is_void<_Rp>::value,
                          int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 result_type operator()(_Args&&... __args) const {
    typedef __invoke_void_return_wrapper<_Rp> _Invoker;
    return _Invoker::__call(static_cast<base const&>(*this), std::forward<_Args>(__args)...);
  }
};

template <class _Rp, class _Fp, class... _BoundArgs>
struct is_bind_expression<__bind_r<_Rp, _Fp, _BoundArgs...> > : public true_type {};

template <class _Fp, class... _BoundArgs>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 __bind<_Fp, _BoundArgs...>
bind(_Fp&& __f, _BoundArgs&&... __bound_args) {
  typedef __bind<_Fp, _BoundArgs...> type;
  return type(std::forward<_Fp>(__f), std::forward<_BoundArgs>(__bound_args)...);
}

template <class _Rp, class _Fp, class... _BoundArgs>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 __bind_r<_Rp, _Fp, _BoundArgs...>
bind(_Fp&& __f, _BoundArgs&&... __bound_args) {
  typedef __bind_r<_Rp, _Fp, _BoundArgs...> type;
  return type(std::forward<_Fp>(__f), std::forward<_BoundArgs>(__bound_args)...);
}

#endif // _LIBCPP_CXX03_LANG

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FUNCTIONAL_BIND_H

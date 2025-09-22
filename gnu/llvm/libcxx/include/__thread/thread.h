// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___THREAD_THREAD_H
#define _LIBCPP___THREAD_THREAD_H

#include <__condition_variable/condition_variable.h>
#include <__config>
#include <__exception/terminate.h>
#include <__functional/hash.h>
#include <__functional/unary_function.h>
#include <__memory/unique_ptr.h>
#include <__mutex/mutex.h>
#include <__system_error/system_error.h>
#include <__thread/id.h>
#include <__thread/support.h>
#include <__utility/forward.h>
#include <tuple>

#ifndef _LIBCPP_HAS_NO_LOCALIZATION
#  include <locale>
#  include <sstream>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
class __thread_specific_ptr;
class _LIBCPP_EXPORTED_FROM_ABI __thread_struct;
class _LIBCPP_HIDDEN __thread_struct_imp;
class __assoc_sub_state;

_LIBCPP_EXPORTED_FROM_ABI __thread_specific_ptr<__thread_struct>& __thread_local_data();

class _LIBCPP_EXPORTED_FROM_ABI __thread_struct {
  __thread_struct_imp* __p_;

  __thread_struct(const __thread_struct&);
  __thread_struct& operator=(const __thread_struct&);

public:
  __thread_struct();
  ~__thread_struct();

  void notify_all_at_thread_exit(condition_variable*, mutex*);
  void __make_ready_at_thread_exit(__assoc_sub_state*);
};

template <class _Tp>
class __thread_specific_ptr {
  __libcpp_tls_key __key_;

  // Only __thread_local_data() may construct a __thread_specific_ptr
  // and only with _Tp == __thread_struct.
  static_assert(is_same<_Tp, __thread_struct>::value, "");
  __thread_specific_ptr();
  friend _LIBCPP_EXPORTED_FROM_ABI __thread_specific_ptr<__thread_struct>& __thread_local_data();

  _LIBCPP_HIDDEN static void _LIBCPP_TLS_DESTRUCTOR_CC __at_thread_exit(void*);

public:
  typedef _Tp* pointer;

  __thread_specific_ptr(const __thread_specific_ptr&)            = delete;
  __thread_specific_ptr& operator=(const __thread_specific_ptr&) = delete;
  ~__thread_specific_ptr();

  _LIBCPP_HIDE_FROM_ABI pointer get() const { return static_cast<_Tp*>(__libcpp_tls_get(__key_)); }
  _LIBCPP_HIDE_FROM_ABI pointer operator*() const { return *get(); }
  _LIBCPP_HIDE_FROM_ABI pointer operator->() const { return get(); }
  void set_pointer(pointer __p);
};

template <class _Tp>
void _LIBCPP_TLS_DESTRUCTOR_CC __thread_specific_ptr<_Tp>::__at_thread_exit(void* __p) {
  delete static_cast<pointer>(__p);
}

template <class _Tp>
__thread_specific_ptr<_Tp>::__thread_specific_ptr() {
  int __ec = __libcpp_tls_create(&__key_, &__thread_specific_ptr::__at_thread_exit);
  if (__ec)
    __throw_system_error(__ec, "__thread_specific_ptr construction failed");
}

template <class _Tp>
__thread_specific_ptr<_Tp>::~__thread_specific_ptr() {
  // __thread_specific_ptr is only created with a static storage duration
  // so this destructor is only invoked during program termination. Invoking
  // pthread_key_delete(__key_) may prevent other threads from deleting their
  // thread local data. For this reason we leak the key.
}

template <class _Tp>
void __thread_specific_ptr<_Tp>::set_pointer(pointer __p) {
  _LIBCPP_ASSERT_INTERNAL(get() == nullptr, "Attempting to overwrite thread local data");
  std::__libcpp_tls_set(__key_, __p);
}

template <>
struct _LIBCPP_TEMPLATE_VIS hash<__thread_id> : public __unary_function<__thread_id, size_t> {
  _LIBCPP_HIDE_FROM_ABI size_t operator()(__thread_id __v) const _NOEXCEPT {
    return hash<__libcpp_thread_id>()(__v.__id_);
  }
};

#ifndef _LIBCPP_HAS_NO_LOCALIZATION
template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, __thread_id __id) {
  // [thread.thread.id]/9
  //   Effects: Inserts the text representation for charT of id into out.
  //
  // [thread.thread.id]/2
  //   The text representation for the character type charT of an
  //   object of type thread::id is an unspecified sequence of charT
  //   such that, for two objects of type thread::id x and y, if
  //   x == y is true, the thread::id objects have the same text
  //   representation, and if x != y is true, the thread::id objects
  //   have distinct text representations.
  //
  // Since various flags in the output stream can affect how the
  // thread id is represented (e.g. numpunct or showbase), we
  // use a temporary stream instead and just output the thread
  // id representation as a string.

  basic_ostringstream<_CharT, _Traits> __sstr;
  __sstr.imbue(locale::classic());
  __sstr << __id.__id_;
  return __os << __sstr.str();
}
#endif // _LIBCPP_HAS_NO_LOCALIZATION

class _LIBCPP_EXPORTED_FROM_ABI thread {
  __libcpp_thread_t __t_;

  thread(const thread&);
  thread& operator=(const thread&);

public:
  typedef __thread_id id;
  typedef __libcpp_thread_t native_handle_type;

  _LIBCPP_HIDE_FROM_ABI thread() _NOEXCEPT : __t_(_LIBCPP_NULL_THREAD) {}
#ifndef _LIBCPP_CXX03_LANG
  template <class _Fp, class... _Args, __enable_if_t<!is_same<__remove_cvref_t<_Fp>, thread>::value, int> = 0>
  _LIBCPP_METHOD_TEMPLATE_IMPLICIT_INSTANTIATION_VIS explicit thread(_Fp&& __f, _Args&&... __args);
#else // _LIBCPP_CXX03_LANG
  template <class _Fp>
  _LIBCPP_METHOD_TEMPLATE_IMPLICIT_INSTANTIATION_VIS explicit thread(_Fp __f);
#endif
  ~thread();

  _LIBCPP_HIDE_FROM_ABI thread(thread&& __t) _NOEXCEPT : __t_(__t.__t_) { __t.__t_ = _LIBCPP_NULL_THREAD; }

  _LIBCPP_HIDE_FROM_ABI thread& operator=(thread&& __t) _NOEXCEPT {
    if (!__libcpp_thread_isnull(&__t_))
      terminate();
    __t_     = __t.__t_;
    __t.__t_ = _LIBCPP_NULL_THREAD;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI void swap(thread& __t) _NOEXCEPT { std::swap(__t_, __t.__t_); }

  _LIBCPP_HIDE_FROM_ABI bool joinable() const _NOEXCEPT { return !__libcpp_thread_isnull(&__t_); }
  void join();
  void detach();
  _LIBCPP_HIDE_FROM_ABI id get_id() const _NOEXCEPT { return __libcpp_thread_get_id(&__t_); }
  _LIBCPP_HIDE_FROM_ABI native_handle_type native_handle() _NOEXCEPT { return __t_; }

  static unsigned hardware_concurrency() _NOEXCEPT;
};

#ifndef _LIBCPP_CXX03_LANG

template <class _TSp, class _Fp, class... _Args, size_t... _Indices>
inline _LIBCPP_HIDE_FROM_ABI void __thread_execute(tuple<_TSp, _Fp, _Args...>& __t, __tuple_indices<_Indices...>) {
  std::__invoke(std::move(std::get<1>(__t)), std::move(std::get<_Indices>(__t))...);
}

template <class _Fp>
_LIBCPP_HIDE_FROM_ABI void* __thread_proxy(void* __vp) {
  // _Fp = tuple< unique_ptr<__thread_struct>, Functor, Args...>
  unique_ptr<_Fp> __p(static_cast<_Fp*>(__vp));
  __thread_local_data().set_pointer(std::get<0>(*__p.get()).release());
  typedef typename __make_tuple_indices<tuple_size<_Fp>::value, 2>::type _Index;
  std::__thread_execute(*__p.get(), _Index());
  return nullptr;
}

template <class _Fp, class... _Args, __enable_if_t<!is_same<__remove_cvref_t<_Fp>, thread>::value, int> >
thread::thread(_Fp&& __f, _Args&&... __args) {
  typedef unique_ptr<__thread_struct> _TSPtr;
  _TSPtr __tsp(new __thread_struct);
  typedef tuple<_TSPtr, __decay_t<_Fp>, __decay_t<_Args>...> _Gp;
  unique_ptr<_Gp> __p(new _Gp(std::move(__tsp), std::forward<_Fp>(__f), std::forward<_Args>(__args)...));
  int __ec = std::__libcpp_thread_create(&__t_, &__thread_proxy<_Gp>, __p.get());
  if (__ec == 0)
    __p.release();
  else
    __throw_system_error(__ec, "thread constructor failed");
}

#else // _LIBCPP_CXX03_LANG

template <class _Fp>
struct __thread_invoke_pair {
  // This type is used to pass memory for thread local storage and a functor
  // to a newly created thread because std::pair doesn't work with
  // std::unique_ptr in C++03.
  _LIBCPP_HIDE_FROM_ABI __thread_invoke_pair(_Fp& __f) : __tsp_(new __thread_struct), __fn_(__f) {}
  unique_ptr<__thread_struct> __tsp_;
  _Fp __fn_;
};

template <class _Fp>
_LIBCPP_HIDE_FROM_ABI void* __thread_proxy_cxx03(void* __vp) {
  unique_ptr<_Fp> __p(static_cast<_Fp*>(__vp));
  __thread_local_data().set_pointer(__p->__tsp_.release());
  (__p->__fn_)();
  return nullptr;
}

template <class _Fp>
thread::thread(_Fp __f) {
  typedef __thread_invoke_pair<_Fp> _InvokePair;
  typedef unique_ptr<_InvokePair> _PairPtr;
  _PairPtr __pp(new _InvokePair(__f));
  int __ec = std::__libcpp_thread_create(&__t_, &__thread_proxy_cxx03<_InvokePair>, __pp.get());
  if (__ec == 0)
    __pp.release();
  else
    __throw_system_error(__ec, "thread constructor failed");
}

#endif // _LIBCPP_CXX03_LANG

inline _LIBCPP_HIDE_FROM_ABI void swap(thread& __x, thread& __y) _NOEXCEPT { __x.swap(__y); }

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___THREAD_THREAD_H

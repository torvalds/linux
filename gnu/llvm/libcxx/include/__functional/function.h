// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_FUNCTION_H
#define _LIBCPP___FUNCTIONAL_FUNCTION_H

#include <__assert>
#include <__config>
#include <__exception/exception.h>
#include <__functional/binary_function.h>
#include <__functional/invoke.h>
#include <__functional/unary_function.h>
#include <__iterator/iterator_traits.h>
#include <__memory/addressof.h>
#include <__memory/allocator.h>
#include <__memory/allocator_destructor.h>
#include <__memory/allocator_traits.h>
#include <__memory/builtin_new_allocator.h>
#include <__memory/compressed_pair.h>
#include <__memory/unique_ptr.h>
#include <__type_traits/aligned_storage.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_core_convertible.h>
#include <__type_traits/is_scalar.h>
#include <__type_traits/is_trivially_constructible.h>
#include <__type_traits/is_trivially_destructible.h>
#include <__type_traits/is_void.h>
#include <__type_traits/strip_signature.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <__utility/piecewise_construct.h>
#include <__utility/swap.h>
#include <__verbose_abort>
#include <new>
#include <tuple>
#include <typeinfo>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#ifndef _LIBCPP_CXX03_LANG

_LIBCPP_BEGIN_NAMESPACE_STD

// bad_function_call

_LIBCPP_DIAGNOSTIC_PUSH
#  if !_LIBCPP_AVAILABILITY_HAS_BAD_FUNCTION_CALL_KEY_FUNCTION
_LIBCPP_CLANG_DIAGNOSTIC_IGNORED("-Wweak-vtables")
#  endif
class _LIBCPP_EXPORTED_FROM_ABI bad_function_call : public exception {
public:
  _LIBCPP_HIDE_FROM_ABI bad_function_call() _NOEXCEPT                                    = default;
  _LIBCPP_HIDE_FROM_ABI bad_function_call(const bad_function_call&) _NOEXCEPT            = default;
  _LIBCPP_HIDE_FROM_ABI bad_function_call& operator=(const bad_function_call&) _NOEXCEPT = default;
// Note that when a key function is not used, every translation unit that uses
// bad_function_call will end up containing a weak definition of the vtable and
// typeinfo.
#  if _LIBCPP_AVAILABILITY_HAS_BAD_FUNCTION_CALL_KEY_FUNCTION
  ~bad_function_call() _NOEXCEPT override;
#  else
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL ~bad_function_call() _NOEXCEPT override {}
#  endif

#  ifdef _LIBCPP_ABI_BAD_FUNCTION_CALL_GOOD_WHAT_MESSAGE
  const char* what() const _NOEXCEPT override;
#  endif
};
_LIBCPP_DIAGNOSTIC_POP

_LIBCPP_NORETURN inline _LIBCPP_HIDE_FROM_ABI void __throw_bad_function_call() {
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  throw bad_function_call();
#  else
  _LIBCPP_VERBOSE_ABORT("bad_function_call was thrown in -fno-exceptions mode");
#  endif
}

template <class _Fp>
class _LIBCPP_TEMPLATE_VIS function; // undefined

namespace __function {

template <class _Rp>
struct __maybe_derive_from_unary_function {};

template <class _Rp, class _A1>
struct __maybe_derive_from_unary_function<_Rp(_A1)> : public __unary_function<_A1, _Rp> {};

template <class _Rp>
struct __maybe_derive_from_binary_function {};

template <class _Rp, class _A1, class _A2>
struct __maybe_derive_from_binary_function<_Rp(_A1, _A2)> : public __binary_function<_A1, _A2, _Rp> {};

template <class _Fp>
_LIBCPP_HIDE_FROM_ABI bool __not_null(_Fp const&) {
  return true;
}

template <class _Fp>
_LIBCPP_HIDE_FROM_ABI bool __not_null(_Fp* __ptr) {
  return __ptr;
}

template <class _Ret, class _Class>
_LIBCPP_HIDE_FROM_ABI bool __not_null(_Ret _Class::*__ptr) {
  return __ptr;
}

template <class _Fp>
_LIBCPP_HIDE_FROM_ABI bool __not_null(function<_Fp> const& __f) {
  return !!__f;
}

#  ifdef _LIBCPP_HAS_EXTENSION_BLOCKS
template <class _Rp, class... _Args>
_LIBCPP_HIDE_FROM_ABI bool __not_null(_Rp (^__p)(_Args...)) {
  return __p;
}
#  endif

} // namespace __function

namespace __function {

// __alloc_func holds a functor and an allocator.

template <class _Fp, class _Ap, class _FB>
class __alloc_func;
template <class _Fp, class _FB>
class __default_alloc_func;

template <class _Fp, class _Ap, class _Rp, class... _ArgTypes>
class __alloc_func<_Fp, _Ap, _Rp(_ArgTypes...)> {
  __compressed_pair<_Fp, _Ap> __f_;

public:
  typedef _LIBCPP_NODEBUG _Fp _Target;
  typedef _LIBCPP_NODEBUG _Ap _Alloc;

  _LIBCPP_HIDE_FROM_ABI const _Target& __target() const { return __f_.first(); }

  // WIN32 APIs may define __allocator, so use __get_allocator instead.
  _LIBCPP_HIDE_FROM_ABI const _Alloc& __get_allocator() const { return __f_.second(); }

  _LIBCPP_HIDE_FROM_ABI explicit __alloc_func(_Target&& __f)
      : __f_(piecewise_construct, std::forward_as_tuple(std::move(__f)), std::forward_as_tuple()) {}

  _LIBCPP_HIDE_FROM_ABI explicit __alloc_func(const _Target& __f, const _Alloc& __a)
      : __f_(piecewise_construct, std::forward_as_tuple(__f), std::forward_as_tuple(__a)) {}

  _LIBCPP_HIDE_FROM_ABI explicit __alloc_func(const _Target& __f, _Alloc&& __a)
      : __f_(piecewise_construct, std::forward_as_tuple(__f), std::forward_as_tuple(std::move(__a))) {}

  _LIBCPP_HIDE_FROM_ABI explicit __alloc_func(_Target&& __f, _Alloc&& __a)
      : __f_(piecewise_construct, std::forward_as_tuple(std::move(__f)), std::forward_as_tuple(std::move(__a))) {}

  _LIBCPP_HIDE_FROM_ABI _Rp operator()(_ArgTypes&&... __arg) {
    typedef __invoke_void_return_wrapper<_Rp> _Invoker;
    return _Invoker::__call(__f_.first(), std::forward<_ArgTypes>(__arg)...);
  }

  _LIBCPP_HIDE_FROM_ABI __alloc_func* __clone() const {
    typedef allocator_traits<_Alloc> __alloc_traits;
    typedef __rebind_alloc<__alloc_traits, __alloc_func> _AA;
    _AA __a(__f_.second());
    typedef __allocator_destructor<_AA> _Dp;
    unique_ptr<__alloc_func, _Dp> __hold(__a.allocate(1), _Dp(__a, 1));
    ::new ((void*)__hold.get()) __alloc_func(__f_.first(), _Alloc(__a));
    return __hold.release();
  }

  _LIBCPP_HIDE_FROM_ABI void destroy() _NOEXCEPT { __f_.~__compressed_pair<_Target, _Alloc>(); }

  _LIBCPP_HIDE_FROM_ABI static void __destroy_and_delete(__alloc_func* __f) {
    typedef allocator_traits<_Alloc> __alloc_traits;
    typedef __rebind_alloc<__alloc_traits, __alloc_func> _FunAlloc;
    _FunAlloc __a(__f->__get_allocator());
    __f->destroy();
    __a.deallocate(__f, 1);
  }
};

template <class _Fp, class _Rp, class... _ArgTypes>
class __default_alloc_func<_Fp, _Rp(_ArgTypes...)> {
  _Fp __f_;

public:
  typedef _LIBCPP_NODEBUG _Fp _Target;

  _LIBCPP_HIDE_FROM_ABI const _Target& __target() const { return __f_; }

  _LIBCPP_HIDE_FROM_ABI explicit __default_alloc_func(_Target&& __f) : __f_(std::move(__f)) {}

  _LIBCPP_HIDE_FROM_ABI explicit __default_alloc_func(const _Target& __f) : __f_(__f) {}

  _LIBCPP_HIDE_FROM_ABI _Rp operator()(_ArgTypes&&... __arg) {
    typedef __invoke_void_return_wrapper<_Rp> _Invoker;
    return _Invoker::__call(__f_, std::forward<_ArgTypes>(__arg)...);
  }

  _LIBCPP_HIDE_FROM_ABI __default_alloc_func* __clone() const {
    __builtin_new_allocator::__holder_t __hold = __builtin_new_allocator::__allocate_type<__default_alloc_func>(1);
    __default_alloc_func* __res                = ::new ((void*)__hold.get()) __default_alloc_func(__f_);
    (void)__hold.release();
    return __res;
  }

  _LIBCPP_HIDE_FROM_ABI void destroy() _NOEXCEPT { __f_.~_Target(); }

  _LIBCPP_HIDE_FROM_ABI static void __destroy_and_delete(__default_alloc_func* __f) {
    __f->destroy();
    __builtin_new_allocator::__deallocate_type<__default_alloc_func>(__f, 1);
  }
};

// __base provides an abstract interface for copyable functors.

template <class _Fp>
class _LIBCPP_TEMPLATE_VIS __base;

template <class _Rp, class... _ArgTypes>
class __base<_Rp(_ArgTypes...)> {
public:
  __base(const __base&)            = delete;
  __base& operator=(const __base&) = delete;

  _LIBCPP_HIDE_FROM_ABI __base() {}
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual ~__base() {}
  virtual __base* __clone() const             = 0;
  virtual void __clone(__base*) const         = 0;
  virtual void destroy() _NOEXCEPT            = 0;
  virtual void destroy_deallocate() _NOEXCEPT = 0;
  virtual _Rp operator()(_ArgTypes&&...)      = 0;
#  ifndef _LIBCPP_HAS_NO_RTTI
  virtual const void* target(const type_info&) const _NOEXCEPT = 0;
  virtual const std::type_info& target_type() const _NOEXCEPT  = 0;
#  endif // _LIBCPP_HAS_NO_RTTI
};

// __func implements __base for a given functor type.

template <class _FD, class _Alloc, class _FB>
class __func;

template <class _Fp, class _Alloc, class _Rp, class... _ArgTypes>
class __func<_Fp, _Alloc, _Rp(_ArgTypes...)> : public __base<_Rp(_ArgTypes...)> {
  __alloc_func<_Fp, _Alloc, _Rp(_ArgTypes...)> __f_;

public:
  _LIBCPP_HIDE_FROM_ABI explicit __func(_Fp&& __f) : __f_(std::move(__f)) {}

  _LIBCPP_HIDE_FROM_ABI explicit __func(const _Fp& __f, const _Alloc& __a) : __f_(__f, __a) {}

  _LIBCPP_HIDE_FROM_ABI explicit __func(const _Fp& __f, _Alloc&& __a) : __f_(__f, std::move(__a)) {}

  _LIBCPP_HIDE_FROM_ABI explicit __func(_Fp&& __f, _Alloc&& __a) : __f_(std::move(__f), std::move(__a)) {}

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual __base<_Rp(_ArgTypes...)>* __clone() const;
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual void __clone(__base<_Rp(_ArgTypes...)>*) const;
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual void destroy() _NOEXCEPT;
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual void destroy_deallocate() _NOEXCEPT;
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual _Rp operator()(_ArgTypes&&... __arg);
#  ifndef _LIBCPP_HAS_NO_RTTI
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual const void* target(const type_info&) const _NOEXCEPT;
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual const std::type_info& target_type() const _NOEXCEPT;
#  endif // _LIBCPP_HAS_NO_RTTI
};

template <class _Fp, class _Alloc, class _Rp, class... _ArgTypes>
__base<_Rp(_ArgTypes...)>* __func<_Fp, _Alloc, _Rp(_ArgTypes...)>::__clone() const {
  typedef allocator_traits<_Alloc> __alloc_traits;
  typedef __rebind_alloc<__alloc_traits, __func> _Ap;
  _Ap __a(__f_.__get_allocator());
  typedef __allocator_destructor<_Ap> _Dp;
  unique_ptr<__func, _Dp> __hold(__a.allocate(1), _Dp(__a, 1));
  ::new ((void*)__hold.get()) __func(__f_.__target(), _Alloc(__a));
  return __hold.release();
}

template <class _Fp, class _Alloc, class _Rp, class... _ArgTypes>
void __func<_Fp, _Alloc, _Rp(_ArgTypes...)>::__clone(__base<_Rp(_ArgTypes...)>* __p) const {
  ::new ((void*)__p) __func(__f_.__target(), __f_.__get_allocator());
}

template <class _Fp, class _Alloc, class _Rp, class... _ArgTypes>
void __func<_Fp, _Alloc, _Rp(_ArgTypes...)>::destroy() _NOEXCEPT {
  __f_.destroy();
}

template <class _Fp, class _Alloc, class _Rp, class... _ArgTypes>
void __func<_Fp, _Alloc, _Rp(_ArgTypes...)>::destroy_deallocate() _NOEXCEPT {
  typedef allocator_traits<_Alloc> __alloc_traits;
  typedef __rebind_alloc<__alloc_traits, __func> _Ap;
  _Ap __a(__f_.__get_allocator());
  __f_.destroy();
  __a.deallocate(this, 1);
}

template <class _Fp, class _Alloc, class _Rp, class... _ArgTypes>
_Rp __func<_Fp, _Alloc, _Rp(_ArgTypes...)>::operator()(_ArgTypes&&... __arg) {
  return __f_(std::forward<_ArgTypes>(__arg)...);
}

#  ifndef _LIBCPP_HAS_NO_RTTI

template <class _Fp, class _Alloc, class _Rp, class... _ArgTypes>
const void* __func<_Fp, _Alloc, _Rp(_ArgTypes...)>::target(const type_info& __ti) const _NOEXCEPT {
  if (__ti == typeid(_Fp))
    return std::addressof(__f_.__target());
  return nullptr;
}

template <class _Fp, class _Alloc, class _Rp, class... _ArgTypes>
const std::type_info& __func<_Fp, _Alloc, _Rp(_ArgTypes...)>::target_type() const _NOEXCEPT {
  return typeid(_Fp);
}

#  endif // _LIBCPP_HAS_NO_RTTI

// __value_func creates a value-type from a __func.

template <class _Fp>
class __value_func;

template <class _Rp, class... _ArgTypes>
class __value_func<_Rp(_ArgTypes...)> {
  _LIBCPP_SUPPRESS_DEPRECATED_PUSH
  typename aligned_storage<3 * sizeof(void*)>::type __buf_;
  _LIBCPP_SUPPRESS_DEPRECATED_POP

  typedef __base<_Rp(_ArgTypes...)> __func;
  __func* __f_;

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_CFI static __func* __as_base(void* __p) { return reinterpret_cast<__func*>(__p); }

public:
  _LIBCPP_HIDE_FROM_ABI __value_func() _NOEXCEPT : __f_(nullptr) {}

  template <class _Fp, class _Alloc>
  _LIBCPP_HIDE_FROM_ABI __value_func(_Fp&& __f, const _Alloc& __a) : __f_(nullptr) {
    typedef allocator_traits<_Alloc> __alloc_traits;
    typedef __function::__func<_Fp, _Alloc, _Rp(_ArgTypes...)> _Fun;
    typedef __rebind_alloc<__alloc_traits, _Fun> _FunAlloc;

    if (__function::__not_null(__f)) {
      _FunAlloc __af(__a);
      if (sizeof(_Fun) <= sizeof(__buf_) && is_nothrow_copy_constructible<_Fp>::value &&
          is_nothrow_copy_constructible<_FunAlloc>::value) {
        __f_ = ::new ((void*)&__buf_) _Fun(std::move(__f), _Alloc(__af));
      } else {
        typedef __allocator_destructor<_FunAlloc> _Dp;
        unique_ptr<__func, _Dp> __hold(__af.allocate(1), _Dp(__af, 1));
        ::new ((void*)__hold.get()) _Fun(std::move(__f), _Alloc(__a));
        __f_ = __hold.release();
      }
    }
  }

  template <class _Fp, __enable_if_t<!is_same<__decay_t<_Fp>, __value_func>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI explicit __value_func(_Fp&& __f) : __value_func(std::forward<_Fp>(__f), allocator<_Fp>()) {}

  _LIBCPP_HIDE_FROM_ABI __value_func(const __value_func& __f) {
    if (__f.__f_ == nullptr)
      __f_ = nullptr;
    else if ((void*)__f.__f_ == &__f.__buf_) {
      __f_ = __as_base(&__buf_);
      __f.__f_->__clone(__f_);
    } else
      __f_ = __f.__f_->__clone();
  }

  _LIBCPP_HIDE_FROM_ABI __value_func(__value_func&& __f) _NOEXCEPT {
    if (__f.__f_ == nullptr)
      __f_ = nullptr;
    else if ((void*)__f.__f_ == &__f.__buf_) {
      __f_ = __as_base(&__buf_);
      __f.__f_->__clone(__f_);
    } else {
      __f_     = __f.__f_;
      __f.__f_ = nullptr;
    }
  }

  _LIBCPP_HIDE_FROM_ABI ~__value_func() {
    if ((void*)__f_ == &__buf_)
      __f_->destroy();
    else if (__f_)
      __f_->destroy_deallocate();
  }

  _LIBCPP_HIDE_FROM_ABI __value_func& operator=(__value_func&& __f) {
    *this = nullptr;
    if (__f.__f_ == nullptr)
      __f_ = nullptr;
    else if ((void*)__f.__f_ == &__f.__buf_) {
      __f_ = __as_base(&__buf_);
      __f.__f_->__clone(__f_);
    } else {
      __f_     = __f.__f_;
      __f.__f_ = nullptr;
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI __value_func& operator=(nullptr_t) {
    __func* __f = __f_;
    __f_        = nullptr;
    if ((void*)__f == &__buf_)
      __f->destroy();
    else if (__f)
      __f->destroy_deallocate();
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI _Rp operator()(_ArgTypes&&... __args) const {
    if (__f_ == nullptr)
      __throw_bad_function_call();
    return (*__f_)(std::forward<_ArgTypes>(__args)...);
  }

  _LIBCPP_HIDE_FROM_ABI void swap(__value_func& __f) _NOEXCEPT {
    if (&__f == this)
      return;
    if ((void*)__f_ == &__buf_ && (void*)__f.__f_ == &__f.__buf_) {
      _LIBCPP_SUPPRESS_DEPRECATED_PUSH
      typename aligned_storage<sizeof(__buf_)>::type __tempbuf;
      _LIBCPP_SUPPRESS_DEPRECATED_POP
      __func* __t = __as_base(&__tempbuf);
      __f_->__clone(__t);
      __f_->destroy();
      __f_ = nullptr;
      __f.__f_->__clone(__as_base(&__buf_));
      __f.__f_->destroy();
      __f.__f_ = nullptr;
      __f_     = __as_base(&__buf_);
      __t->__clone(__as_base(&__f.__buf_));
      __t->destroy();
      __f.__f_ = __as_base(&__f.__buf_);
    } else if ((void*)__f_ == &__buf_) {
      __f_->__clone(__as_base(&__f.__buf_));
      __f_->destroy();
      __f_     = __f.__f_;
      __f.__f_ = __as_base(&__f.__buf_);
    } else if ((void*)__f.__f_ == &__f.__buf_) {
      __f.__f_->__clone(__as_base(&__buf_));
      __f.__f_->destroy();
      __f.__f_ = __f_;
      __f_     = __as_base(&__buf_);
    } else
      std::swap(__f_, __f.__f_);
  }

  _LIBCPP_HIDE_FROM_ABI explicit operator bool() const _NOEXCEPT { return __f_ != nullptr; }

#  ifndef _LIBCPP_HAS_NO_RTTI
  _LIBCPP_HIDE_FROM_ABI const std::type_info& target_type() const _NOEXCEPT {
    if (__f_ == nullptr)
      return typeid(void);
    return __f_->target_type();
  }

  template <typename _Tp>
  _LIBCPP_HIDE_FROM_ABI const _Tp* target() const _NOEXCEPT {
    if (__f_ == nullptr)
      return nullptr;
    return (const _Tp*)__f_->target(typeid(_Tp));
  }
#  endif // _LIBCPP_HAS_NO_RTTI
};

// Storage for a functor object, to be used with __policy to manage copy and
// destruction.
union __policy_storage {
  mutable char __small[sizeof(void*) * 2];
  void* __large;
};

// True if _Fun can safely be held in __policy_storage.__small.
template <typename _Fun>
struct __use_small_storage
    : public integral_constant<
          bool,
          sizeof(_Fun) <= sizeof(__policy_storage)&& _LIBCPP_ALIGNOF(_Fun) <= _LIBCPP_ALIGNOF(__policy_storage) &&
              is_trivially_copy_constructible<_Fun>::value && is_trivially_destructible<_Fun>::value> {};

// Policy contains information about how to copy, destroy, and move the
// underlying functor. You can think of it as a vtable of sorts.
struct __policy {
  // Used to copy or destroy __large values. null for trivial objects.
  void* (*const __clone)(const void*);
  void (*const __destroy)(void*);

  // True if this is the null policy (no value).
  const bool __is_null;

  // The target type. May be null if RTTI is disabled.
  const std::type_info* const __type_info;

  // Returns a pointer to a static policy object suitable for the functor
  // type.
  template <typename _Fun>
  _LIBCPP_HIDE_FROM_ABI static const __policy* __create() {
    return __choose_policy<_Fun>(__use_small_storage<_Fun>());
  }

  _LIBCPP_HIDE_FROM_ABI static const __policy* __create_empty() {
    static constexpr __policy __policy = {
        nullptr,
        nullptr,
        true,
#  ifndef _LIBCPP_HAS_NO_RTTI
        &typeid(void)
#  else
        nullptr
#  endif
    };
    return &__policy;
  }

private:
  template <typename _Fun>
  _LIBCPP_HIDE_FROM_ABI static void* __large_clone(const void* __s) {
    const _Fun* __f = static_cast<const _Fun*>(__s);
    return __f->__clone();
  }

  template <typename _Fun>
  _LIBCPP_HIDE_FROM_ABI static void __large_destroy(void* __s) {
    _Fun::__destroy_and_delete(static_cast<_Fun*>(__s));
  }

  template <typename _Fun>
  _LIBCPP_HIDE_FROM_ABI static const __policy* __choose_policy(/* is_small = */ false_type) {
    static constexpr __policy __policy = {
        &__large_clone<_Fun>,
        &__large_destroy<_Fun>,
        false,
#  ifndef _LIBCPP_HAS_NO_RTTI
        &typeid(typename _Fun::_Target)
#  else
        nullptr
#  endif
    };
    return &__policy;
  }

  template <typename _Fun>
  _LIBCPP_HIDE_FROM_ABI static const __policy* __choose_policy(/* is_small = */ true_type) {
    static constexpr __policy __policy = {
        nullptr,
        nullptr,
        false,
#  ifndef _LIBCPP_HAS_NO_RTTI
        &typeid(typename _Fun::_Target)
#  else
        nullptr
#  endif
    };
    return &__policy;
  }
};

// Used to choose between perfect forwarding or pass-by-value. Pass-by-value is
// faster for types that can be passed in registers.
template <typename _Tp>
using __fast_forward = __conditional_t<is_scalar<_Tp>::value, _Tp, _Tp&&>;

// __policy_invoker calls an instance of __alloc_func held in __policy_storage.

template <class _Fp>
struct __policy_invoker;

template <class _Rp, class... _ArgTypes>
struct __policy_invoker<_Rp(_ArgTypes...)> {
  typedef _Rp (*__Call)(const __policy_storage*, __fast_forward<_ArgTypes>...);

  __Call __call_;

  // Creates an invoker that throws bad_function_call.
  _LIBCPP_HIDE_FROM_ABI __policy_invoker() : __call_(&__call_empty) {}

  // Creates an invoker that calls the given instance of __func.
  template <typename _Fun>
  _LIBCPP_HIDE_FROM_ABI static __policy_invoker __create() {
    return __policy_invoker(&__call_impl<_Fun>);
  }

private:
  _LIBCPP_HIDE_FROM_ABI explicit __policy_invoker(__Call __c) : __call_(__c) {}

  _LIBCPP_HIDE_FROM_ABI static _Rp __call_empty(const __policy_storage*, __fast_forward<_ArgTypes>...) {
    __throw_bad_function_call();
  }

  template <typename _Fun>
  _LIBCPP_HIDE_FROM_ABI static _Rp __call_impl(const __policy_storage* __buf, __fast_forward<_ArgTypes>... __args) {
    _Fun* __f = reinterpret_cast<_Fun*>(__use_small_storage<_Fun>::value ? &__buf->__small : __buf->__large);
    return (*__f)(std::forward<_ArgTypes>(__args)...);
  }
};

// __policy_func uses a __policy and __policy_invoker to create a type-erased,
// copyable functor.

template <class _Fp>
class __policy_func;

template <class _Rp, class... _ArgTypes>
class __policy_func<_Rp(_ArgTypes...)> {
  // Inline storage for small objects.
  __policy_storage __buf_;

  // Calls the value stored in __buf_. This could technically be part of
  // policy, but storing it here eliminates a level of indirection inside
  // operator().
  typedef __function::__policy_invoker<_Rp(_ArgTypes...)> __invoker;
  __invoker __invoker_;

  // The policy that describes how to move / copy / destroy __buf_. Never
  // null, even if the function is empty.
  const __policy* __policy_;

public:
  _LIBCPP_HIDE_FROM_ABI __policy_func() : __policy_(__policy::__create_empty()) {}

  template <class _Fp, class _Alloc>
  _LIBCPP_HIDE_FROM_ABI __policy_func(_Fp&& __f, const _Alloc& __a) : __policy_(__policy::__create_empty()) {
    typedef __alloc_func<_Fp, _Alloc, _Rp(_ArgTypes...)> _Fun;
    typedef allocator_traits<_Alloc> __alloc_traits;
    typedef __rebind_alloc<__alloc_traits, _Fun> _FunAlloc;

    if (__function::__not_null(__f)) {
      __invoker_ = __invoker::template __create<_Fun>();
      __policy_  = __policy::__create<_Fun>();

      _FunAlloc __af(__a);
      if (__use_small_storage<_Fun>()) {
        ::new ((void*)&__buf_.__small) _Fun(std::move(__f), _Alloc(__af));
      } else {
        typedef __allocator_destructor<_FunAlloc> _Dp;
        unique_ptr<_Fun, _Dp> __hold(__af.allocate(1), _Dp(__af, 1));
        ::new ((void*)__hold.get()) _Fun(std::move(__f), _Alloc(__af));
        __buf_.__large = __hold.release();
      }
    }
  }

  template <class _Fp, __enable_if_t<!is_same<__decay_t<_Fp>, __policy_func>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI explicit __policy_func(_Fp&& __f) : __policy_(__policy::__create_empty()) {
    typedef __default_alloc_func<_Fp, _Rp(_ArgTypes...)> _Fun;

    if (__function::__not_null(__f)) {
      __invoker_ = __invoker::template __create<_Fun>();
      __policy_  = __policy::__create<_Fun>();
      if (__use_small_storage<_Fun>()) {
        ::new ((void*)&__buf_.__small) _Fun(std::move(__f));
      } else {
        __builtin_new_allocator::__holder_t __hold = __builtin_new_allocator::__allocate_type<_Fun>(1);
        __buf_.__large                             = ::new ((void*)__hold.get()) _Fun(std::move(__f));
        (void)__hold.release();
      }
    }
  }

  _LIBCPP_HIDE_FROM_ABI __policy_func(const __policy_func& __f)
      : __buf_(__f.__buf_), __invoker_(__f.__invoker_), __policy_(__f.__policy_) {
    if (__policy_->__clone)
      __buf_.__large = __policy_->__clone(__f.__buf_.__large);
  }

  _LIBCPP_HIDE_FROM_ABI __policy_func(__policy_func&& __f)
      : __buf_(__f.__buf_), __invoker_(__f.__invoker_), __policy_(__f.__policy_) {
    if (__policy_->__destroy) {
      __f.__policy_  = __policy::__create_empty();
      __f.__invoker_ = __invoker();
    }
  }

  _LIBCPP_HIDE_FROM_ABI ~__policy_func() {
    if (__policy_->__destroy)
      __policy_->__destroy(__buf_.__large);
  }

  _LIBCPP_HIDE_FROM_ABI __policy_func& operator=(__policy_func&& __f) {
    *this          = nullptr;
    __buf_         = __f.__buf_;
    __invoker_     = __f.__invoker_;
    __policy_      = __f.__policy_;
    __f.__policy_  = __policy::__create_empty();
    __f.__invoker_ = __invoker();
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI __policy_func& operator=(nullptr_t) {
    const __policy* __p = __policy_;
    __policy_           = __policy::__create_empty();
    __invoker_          = __invoker();
    if (__p->__destroy)
      __p->__destroy(__buf_.__large);
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI _Rp operator()(_ArgTypes&&... __args) const {
    return __invoker_.__call_(std::addressof(__buf_), std::forward<_ArgTypes>(__args)...);
  }

  _LIBCPP_HIDE_FROM_ABI void swap(__policy_func& __f) {
    std::swap(__invoker_, __f.__invoker_);
    std::swap(__policy_, __f.__policy_);
    std::swap(__buf_, __f.__buf_);
  }

  _LIBCPP_HIDE_FROM_ABI explicit operator bool() const _NOEXCEPT { return !__policy_->__is_null; }

#  ifndef _LIBCPP_HAS_NO_RTTI
  _LIBCPP_HIDE_FROM_ABI const std::type_info& target_type() const _NOEXCEPT { return *__policy_->__type_info; }

  template <typename _Tp>
  _LIBCPP_HIDE_FROM_ABI const _Tp* target() const _NOEXCEPT {
    if (__policy_->__is_null || typeid(_Tp) != *__policy_->__type_info)
      return nullptr;
    if (__policy_->__clone) // Out of line storage.
      return reinterpret_cast<const _Tp*>(__buf_.__large);
    else
      return reinterpret_cast<const _Tp*>(&__buf_.__small);
  }
#  endif // _LIBCPP_HAS_NO_RTTI
};

#  if defined(_LIBCPP_HAS_BLOCKS_RUNTIME)

extern "C" void* _Block_copy(const void*);
extern "C" void _Block_release(const void*);

template <class _Rp1, class... _ArgTypes1, class _Alloc, class _Rp, class... _ArgTypes>
class __func<_Rp1 (^)(_ArgTypes1...), _Alloc, _Rp(_ArgTypes...)> : public __base<_Rp(_ArgTypes...)> {
  typedef _Rp1 (^__block_type)(_ArgTypes1...);
  __block_type __f_;

public:
  _LIBCPP_HIDE_FROM_ABI explicit __func(__block_type const& __f)
#    ifdef _LIBCPP_HAS_OBJC_ARC
      : __f_(__f)
#    else
      : __f_(reinterpret_cast<__block_type>(__f ? _Block_copy(__f) : nullptr))
#    endif
  {
  }

  // [TODO] add && to save on a retain

  _LIBCPP_HIDE_FROM_ABI explicit __func(__block_type __f, const _Alloc& /* unused */)
#    ifdef _LIBCPP_HAS_OBJC_ARC
      : __f_(__f)
#    else
      : __f_(reinterpret_cast<__block_type>(__f ? _Block_copy(__f) : nullptr))
#    endif
  {
  }

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual __base<_Rp(_ArgTypes...)>* __clone() const {
    _LIBCPP_ASSERT_INTERNAL(
        false,
        "Block pointers are just pointers, so they should always fit into "
        "std::function's small buffer optimization. This function should "
        "never be invoked.");
    return nullptr;
  }

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual void __clone(__base<_Rp(_ArgTypes...)>* __p) const {
    ::new ((void*)__p) __func(__f_);
  }

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual void destroy() _NOEXCEPT {
#    ifndef _LIBCPP_HAS_OBJC_ARC
    if (__f_)
      _Block_release(__f_);
#    endif
    __f_ = 0;
  }

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual void destroy_deallocate() _NOEXCEPT {
    _LIBCPP_ASSERT_INTERNAL(
        false,
        "Block pointers are just pointers, so they should always fit into "
        "std::function's small buffer optimization. This function should "
        "never be invoked.");
  }

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual _Rp operator()(_ArgTypes&&... __arg) {
    return std::__invoke(__f_, std::forward<_ArgTypes>(__arg)...);
  }

#    ifndef _LIBCPP_HAS_NO_RTTI
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual const void* target(type_info const& __ti) const _NOEXCEPT {
    if (__ti == typeid(__func::__block_type))
      return &__f_;
    return (const void*)nullptr;
  }

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL virtual const std::type_info& target_type() const _NOEXCEPT {
    return typeid(__func::__block_type);
  }
#    endif // _LIBCPP_HAS_NO_RTTI
};

#  endif // _LIBCPP_HAS_EXTENSION_BLOCKS

} // namespace __function

template <class _Rp, class... _ArgTypes>
class _LIBCPP_TEMPLATE_VIS function<_Rp(_ArgTypes...)>
    : public __function::__maybe_derive_from_unary_function<_Rp(_ArgTypes...)>,
      public __function::__maybe_derive_from_binary_function<_Rp(_ArgTypes...)> {
#  ifndef _LIBCPP_ABI_OPTIMIZED_FUNCTION
  typedef __function::__value_func<_Rp(_ArgTypes...)> __func;
#  else
  typedef __function::__policy_func<_Rp(_ArgTypes...)> __func;
#  endif

  __func __f_;

  template <class _Fp,
            bool = _And< _IsNotSame<__remove_cvref_t<_Fp>, function>, __invokable<_Fp, _ArgTypes...> >::value>
  struct __callable;
  template <class _Fp>
  struct __callable<_Fp, true> {
    static const bool value =
        is_void<_Rp>::value || __is_core_convertible<typename __invoke_of<_Fp, _ArgTypes...>::type, _Rp>::value;
  };
  template <class _Fp>
  struct __callable<_Fp, false> {
    static const bool value = false;
  };

  template <class _Fp>
  using _EnableIfLValueCallable = __enable_if_t<__callable<_Fp&>::value>;

public:
  typedef _Rp result_type;

  // construct/copy/destroy:
  _LIBCPP_HIDE_FROM_ABI function() _NOEXCEPT {}
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_HIDE_FROM_ABI function(nullptr_t) _NOEXCEPT {}
  _LIBCPP_HIDE_FROM_ABI function(const function&);
  _LIBCPP_HIDE_FROM_ABI function(function&&) _NOEXCEPT;
  template <class _Fp, class = _EnableIfLValueCallable<_Fp>>
  _LIBCPP_HIDE_FROM_ABI function(_Fp);

#  if _LIBCPP_STD_VER <= 14
  template <class _Alloc>
  _LIBCPP_HIDE_FROM_ABI function(allocator_arg_t, const _Alloc&) _NOEXCEPT {}
  template <class _Alloc>
  _LIBCPP_HIDE_FROM_ABI function(allocator_arg_t, const _Alloc&, nullptr_t) _NOEXCEPT {}
  template <class _Alloc>
  _LIBCPP_HIDE_FROM_ABI function(allocator_arg_t, const _Alloc&, const function&);
  template <class _Alloc>
  _LIBCPP_HIDE_FROM_ABI function(allocator_arg_t, const _Alloc&, function&&);
  template <class _Fp, class _Alloc, class = _EnableIfLValueCallable<_Fp>>
  _LIBCPP_HIDE_FROM_ABI function(allocator_arg_t, const _Alloc& __a, _Fp __f);
#  endif

  _LIBCPP_HIDE_FROM_ABI function& operator=(const function&);
  _LIBCPP_HIDE_FROM_ABI function& operator=(function&&) _NOEXCEPT;
  _LIBCPP_HIDE_FROM_ABI function& operator=(nullptr_t) _NOEXCEPT;
  template <class _Fp, class = _EnableIfLValueCallable<__decay_t<_Fp>>>
  _LIBCPP_HIDE_FROM_ABI function& operator=(_Fp&&);

  _LIBCPP_HIDE_FROM_ABI ~function();

  // function modifiers:
  _LIBCPP_HIDE_FROM_ABI void swap(function&) _NOEXCEPT;

#  if _LIBCPP_STD_VER <= 14
  template <class _Fp, class _Alloc>
  _LIBCPP_HIDE_FROM_ABI void assign(_Fp&& __f, const _Alloc& __a) {
    function(allocator_arg, __a, std::forward<_Fp>(__f)).swap(*this);
  }
#  endif

  // function capacity:
  _LIBCPP_HIDE_FROM_ABI explicit operator bool() const _NOEXCEPT { return static_cast<bool>(__f_); }

  // deleted overloads close possible hole in the type system
  template <class _R2, class... _ArgTypes2>
  bool operator==(const function<_R2(_ArgTypes2...)>&) const = delete;
#  if _LIBCPP_STD_VER <= 17
  template <class _R2, class... _ArgTypes2>
  bool operator!=(const function<_R2(_ArgTypes2...)>&) const = delete;
#  endif

public:
  // function invocation:
  _LIBCPP_HIDE_FROM_ABI _Rp operator()(_ArgTypes...) const;

#  ifndef _LIBCPP_HAS_NO_RTTI
  // function target access:
  _LIBCPP_HIDE_FROM_ABI const std::type_info& target_type() const _NOEXCEPT;
  template <typename _Tp>
  _LIBCPP_HIDE_FROM_ABI _Tp* target() _NOEXCEPT;
  template <typename _Tp>
  _LIBCPP_HIDE_FROM_ABI const _Tp* target() const _NOEXCEPT;
#  endif // _LIBCPP_HAS_NO_RTTI
};

#  if _LIBCPP_STD_VER >= 17
template <class _Rp, class... _Ap>
function(_Rp (*)(_Ap...)) -> function<_Rp(_Ap...)>;

template <class _Fp, class _Stripped = typename __strip_signature<decltype(&_Fp::operator())>::type>
function(_Fp) -> function<_Stripped>;
#  endif // _LIBCPP_STD_VER >= 17

template <class _Rp, class... _ArgTypes>
function<_Rp(_ArgTypes...)>::function(const function& __f) : __f_(__f.__f_) {}

#  if _LIBCPP_STD_VER <= 14
template <class _Rp, class... _ArgTypes>
template <class _Alloc>
function<_Rp(_ArgTypes...)>::function(allocator_arg_t, const _Alloc&, const function& __f) : __f_(__f.__f_) {}
#  endif

template <class _Rp, class... _ArgTypes>
function<_Rp(_ArgTypes...)>::function(function&& __f) _NOEXCEPT : __f_(std::move(__f.__f_)) {}

#  if _LIBCPP_STD_VER <= 14
template <class _Rp, class... _ArgTypes>
template <class _Alloc>
function<_Rp(_ArgTypes...)>::function(allocator_arg_t, const _Alloc&, function&& __f) : __f_(std::move(__f.__f_)) {}
#  endif

template <class _Rp, class... _ArgTypes>
template <class _Fp, class>
function<_Rp(_ArgTypes...)>::function(_Fp __f) : __f_(std::move(__f)) {}

#  if _LIBCPP_STD_VER <= 14
template <class _Rp, class... _ArgTypes>
template <class _Fp, class _Alloc, class>
function<_Rp(_ArgTypes...)>::function(allocator_arg_t, const _Alloc& __a, _Fp __f) : __f_(std::move(__f), __a) {}
#  endif

template <class _Rp, class... _ArgTypes>
function<_Rp(_ArgTypes...)>& function<_Rp(_ArgTypes...)>::operator=(const function& __f) {
  function(__f).swap(*this);
  return *this;
}

template <class _Rp, class... _ArgTypes>
function<_Rp(_ArgTypes...)>& function<_Rp(_ArgTypes...)>::operator=(function&& __f) _NOEXCEPT {
  __f_ = std::move(__f.__f_);
  return *this;
}

template <class _Rp, class... _ArgTypes>
function<_Rp(_ArgTypes...)>& function<_Rp(_ArgTypes...)>::operator=(nullptr_t) _NOEXCEPT {
  __f_ = nullptr;
  return *this;
}

template <class _Rp, class... _ArgTypes>
template <class _Fp, class>
function<_Rp(_ArgTypes...)>& function<_Rp(_ArgTypes...)>::operator=(_Fp&& __f) {
  function(std::forward<_Fp>(__f)).swap(*this);
  return *this;
}

template <class _Rp, class... _ArgTypes>
function<_Rp(_ArgTypes...)>::~function() {}

template <class _Rp, class... _ArgTypes>
void function<_Rp(_ArgTypes...)>::swap(function& __f) _NOEXCEPT {
  __f_.swap(__f.__f_);
}

template <class _Rp, class... _ArgTypes>
_Rp function<_Rp(_ArgTypes...)>::operator()(_ArgTypes... __arg) const {
  return __f_(std::forward<_ArgTypes>(__arg)...);
}

#  ifndef _LIBCPP_HAS_NO_RTTI

template <class _Rp, class... _ArgTypes>
const std::type_info& function<_Rp(_ArgTypes...)>::target_type() const _NOEXCEPT {
  return __f_.target_type();
}

template <class _Rp, class... _ArgTypes>
template <typename _Tp>
_Tp* function<_Rp(_ArgTypes...)>::target() _NOEXCEPT {
  return (_Tp*)(__f_.template target<_Tp>());
}

template <class _Rp, class... _ArgTypes>
template <typename _Tp>
const _Tp* function<_Rp(_ArgTypes...)>::target() const _NOEXCEPT {
  return __f_.template target<_Tp>();
}

#  endif // _LIBCPP_HAS_NO_RTTI

template <class _Rp, class... _ArgTypes>
inline _LIBCPP_HIDE_FROM_ABI bool operator==(const function<_Rp(_ArgTypes...)>& __f, nullptr_t) _NOEXCEPT {
  return !__f;
}

#  if _LIBCPP_STD_VER <= 17

template <class _Rp, class... _ArgTypes>
inline _LIBCPP_HIDE_FROM_ABI bool operator==(nullptr_t, const function<_Rp(_ArgTypes...)>& __f) _NOEXCEPT {
  return !__f;
}

template <class _Rp, class... _ArgTypes>
inline _LIBCPP_HIDE_FROM_ABI bool operator!=(const function<_Rp(_ArgTypes...)>& __f, nullptr_t) _NOEXCEPT {
  return (bool)__f;
}

template <class _Rp, class... _ArgTypes>
inline _LIBCPP_HIDE_FROM_ABI bool operator!=(nullptr_t, const function<_Rp(_ArgTypes...)>& __f) _NOEXCEPT {
  return (bool)__f;
}

#  endif // _LIBCPP_STD_VER <= 17

template <class _Rp, class... _ArgTypes>
inline _LIBCPP_HIDE_FROM_ABI void swap(function<_Rp(_ArgTypes...)>& __x, function<_Rp(_ArgTypes...)>& __y) _NOEXCEPT {
  return __x.swap(__y);
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_CXX03_LANG

_LIBCPP_POP_MACROS

#endif // _LIBCPP___FUNCTIONAL_FUNCTION_H

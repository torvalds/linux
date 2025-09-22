//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_RESOURCE_POLYMORPHIC_ALLOCATOR_H
#define _LIBCPP___MEMORY_RESOURCE_POLYMORPHIC_ALLOCATOR_H

#include <__assert>
#include <__config>
#include <__fwd/pair.h>
#include <__memory_resource/memory_resource.h>
#include <__utility/exception_guard.h>
#include <cstddef>
#include <limits>
#include <new>
#include <tuple>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_STD

namespace pmr {

// [mem.poly.allocator.class]

template <class _ValueType
#  if _LIBCPP_STD_VER >= 20
          = byte
#  endif
          >
class _LIBCPP_AVAILABILITY_PMR _LIBCPP_TEMPLATE_VIS polymorphic_allocator {

public:
  using value_type = _ValueType;

  // [mem.poly.allocator.ctor]

  _LIBCPP_HIDE_FROM_ABI polymorphic_allocator() noexcept : __res_(std::pmr::get_default_resource()) {}

  _LIBCPP_HIDE_FROM_ABI polymorphic_allocator(memory_resource* __r) noexcept : __res_(__r) {}

  _LIBCPP_HIDE_FROM_ABI polymorphic_allocator(const polymorphic_allocator&) = default;

  template <class _Tp>
  _LIBCPP_HIDE_FROM_ABI polymorphic_allocator(const polymorphic_allocator<_Tp>& __other) noexcept
      : __res_(__other.resource()) {}

  polymorphic_allocator& operator=(const polymorphic_allocator&) = delete;

  // [mem.poly.allocator.mem]

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI _ValueType* allocate(size_t __n) {
    if (__n > __max_size()) {
      __throw_bad_array_new_length();
    }
    return static_cast<_ValueType*>(__res_->allocate(__n * sizeof(_ValueType), alignof(_ValueType)));
  }

  _LIBCPP_HIDE_FROM_ABI void deallocate(_ValueType* __p, size_t __n) {
    _LIBCPP_ASSERT_VALID_DEALLOCATION(
        __n <= __max_size(),
        "deallocate() called for a size which exceeds max_size(), leading to a memory leak "
        "(the argument will overflow and result in too few objects being deleted)");
    __res_->deallocate(__p, __n * sizeof(_ValueType), alignof(_ValueType));
  }

#  if _LIBCPP_STD_VER >= 20

  [[nodiscard]] [[using __gnu__: __alloc_size__(2), __alloc_align__(3)]] _LIBCPP_HIDE_FROM_ABI void*
  allocate_bytes(size_t __nbytes, size_t __alignment = alignof(max_align_t)) {
    return __res_->allocate(__nbytes, __alignment);
  }

  _LIBCPP_HIDE_FROM_ABI void deallocate_bytes(void* __ptr, size_t __nbytes, size_t __alignment = alignof(max_align_t)) {
    __res_->deallocate(__ptr, __nbytes, __alignment);
  }

  template <class _Type>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI _Type* allocate_object(size_t __n = 1) {
    if (numeric_limits<size_t>::max() / sizeof(_Type) < __n)
      std::__throw_bad_array_new_length();
    return static_cast<_Type*>(allocate_bytes(__n * sizeof(_Type), alignof(_Type)));
  }

  template <class _Type>
  _LIBCPP_HIDE_FROM_ABI void deallocate_object(_Type* __ptr, size_t __n = 1) {
    deallocate_bytes(__ptr, __n * sizeof(_Type), alignof(_Type));
  }

  template <class _Type, class... _CtorArgs>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI _Type* new_object(_CtorArgs&&... __ctor_args) {
    _Type* __ptr = allocate_object<_Type>();
    auto __guard = std::__make_exception_guard([&] { deallocate_object(__ptr); });
    construct(__ptr, std::forward<_CtorArgs>(__ctor_args)...);
    __guard.__complete();
    return __ptr;
  }

  template <class _Type>
  _LIBCPP_HIDE_FROM_ABI void delete_object(_Type* __ptr) {
    destroy(__ptr);
    deallocate_object(__ptr);
  }

#  endif // _LIBCPP_STD_VER >= 20

  template <class _Tp, class... _Ts>
  _LIBCPP_HIDE_FROM_ABI void construct(_Tp* __p, _Ts&&... __args) {
    std::__user_alloc_construct_impl(
        typename __uses_alloc_ctor<_Tp, polymorphic_allocator&, _Ts...>::type(),
        __p,
        *this,
        std::forward<_Ts>(__args)...);
  }

  template <class _T1, class _T2, class... _Args1, class... _Args2>
  _LIBCPP_HIDE_FROM_ABI void
  construct(pair<_T1, _T2>* __p, piecewise_construct_t, tuple<_Args1...> __x, tuple<_Args2...> __y) {
    ::new ((void*)__p) pair<_T1, _T2>(
        piecewise_construct,
        __transform_tuple(typename __uses_alloc_ctor< _T1, polymorphic_allocator&, _Args1... >::type(),
                          std::move(__x),
                          typename __make_tuple_indices<sizeof...(_Args1)>::type{}),
        __transform_tuple(typename __uses_alloc_ctor< _T2, polymorphic_allocator&, _Args2... >::type(),
                          std::move(__y),
                          typename __make_tuple_indices<sizeof...(_Args2)>::type{}));
  }

  template <class _T1, class _T2>
  _LIBCPP_HIDE_FROM_ABI void construct(pair<_T1, _T2>* __p) {
    construct(__p, piecewise_construct, tuple<>(), tuple<>());
  }

  template <class _T1, class _T2, class _Up, class _Vp>
  _LIBCPP_HIDE_FROM_ABI void construct(pair<_T1, _T2>* __p, _Up&& __u, _Vp&& __v) {
    construct(__p,
              piecewise_construct,
              std::forward_as_tuple(std::forward<_Up>(__u)),
              std::forward_as_tuple(std::forward<_Vp>(__v)));
  }

  template <class _T1, class _T2, class _U1, class _U2>
  _LIBCPP_HIDE_FROM_ABI void construct(pair<_T1, _T2>* __p, const pair<_U1, _U2>& __pr) {
    construct(__p, piecewise_construct, std::forward_as_tuple(__pr.first), std::forward_as_tuple(__pr.second));
  }

  template <class _T1, class _T2, class _U1, class _U2>
  _LIBCPP_HIDE_FROM_ABI void construct(pair<_T1, _T2>* __p, pair<_U1, _U2>&& __pr) {
    construct(__p,
              piecewise_construct,
              std::forward_as_tuple(std::forward<_U1>(__pr.first)),
              std::forward_as_tuple(std::forward<_U2>(__pr.second)));
  }

  template <class _Tp>
  _LIBCPP_HIDE_FROM_ABI void destroy(_Tp* __p) {
    __p->~_Tp();
  }

  _LIBCPP_HIDE_FROM_ABI polymorphic_allocator select_on_container_copy_construction() const noexcept {
    return polymorphic_allocator();
  }

  _LIBCPP_HIDE_FROM_ABI memory_resource* resource() const noexcept { return __res_; }

private:
  template <class... _Args, size_t... _Is>
  _LIBCPP_HIDE_FROM_ABI tuple<_Args&&...>
  __transform_tuple(integral_constant<int, 0>, tuple<_Args...>&& __t, __tuple_indices<_Is...>) {
    return std::forward_as_tuple(std::get<_Is>(std::move(__t))...);
  }

  template <class... _Args, size_t... _Is>
  _LIBCPP_HIDE_FROM_ABI tuple<allocator_arg_t const&, polymorphic_allocator&, _Args&&...>
  __transform_tuple(integral_constant<int, 1>, tuple<_Args...>&& __t, __tuple_indices<_Is...>) {
    using _Tup = tuple<allocator_arg_t const&, polymorphic_allocator&, _Args&&...>;
    return _Tup(allocator_arg, *this, std::get<_Is>(std::move(__t))...);
  }

  template <class... _Args, size_t... _Is>
  _LIBCPP_HIDE_FROM_ABI tuple<_Args&&..., polymorphic_allocator&>
  __transform_tuple(integral_constant<int, 2>, tuple<_Args...>&& __t, __tuple_indices<_Is...>) {
    using _Tup = tuple<_Args&&..., polymorphic_allocator&>;
    return _Tup(std::get<_Is>(std::move(__t))..., *this);
  }

  _LIBCPP_HIDE_FROM_ABI size_t __max_size() const noexcept {
    return numeric_limits<size_t>::max() / sizeof(value_type);
  }

  memory_resource* __res_;
};

// [mem.poly.allocator.eq]

template <class _Tp, class _Up>
inline _LIBCPP_HIDE_FROM_ABI bool
operator==(const polymorphic_allocator<_Tp>& __lhs, const polymorphic_allocator<_Up>& __rhs) noexcept {
  return *__lhs.resource() == *__rhs.resource();
}

#  if _LIBCPP_STD_VER <= 17

template <class _Tp, class _Up>
inline _LIBCPP_HIDE_FROM_ABI bool
operator!=(const polymorphic_allocator<_Tp>& __lhs, const polymorphic_allocator<_Up>& __rhs) noexcept {
  return !(__lhs == __rhs);
}

#  endif

} // namespace pmr

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_POP_MACROS

#endif // _LIBCPP___MEMORY_RESOURCE_POLYMORPHIC_ALLOCATOR_H

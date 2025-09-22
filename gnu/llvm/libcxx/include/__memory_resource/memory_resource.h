//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_RESOURCE_MEMORY_RESOURCE_H
#define _LIBCPP___MEMORY_RESOURCE_MEMORY_RESOURCE_H

#include <__config>
#include <__fwd/memory_resource.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_STD

namespace pmr {

// [mem.res.class]

class _LIBCPP_AVAILABILITY_PMR _LIBCPP_EXPORTED_FROM_ABI memory_resource {
  static const size_t __max_align = alignof(max_align_t);

public:
  virtual ~memory_resource();

  [[nodiscard]] [[using __gnu__: __returns_nonnull__, __alloc_size__(2), __alloc_align__(3)]]
  _LIBCPP_HIDE_FROM_ABI void* allocate(size_t __bytes, size_t __align = __max_align) {
    return do_allocate(__bytes, __align);
  }

  [[__gnu__::__nonnull__]] _LIBCPP_HIDE_FROM_ABI void
  deallocate(void* __p, size_t __bytes, size_t __align = __max_align) {
    do_deallocate(__p, __bytes, __align);
  }

  _LIBCPP_HIDE_FROM_ABI bool is_equal(const memory_resource& __other) const noexcept { return do_is_equal(__other); }

private:
  virtual void* do_allocate(size_t, size_t)                       = 0;
  virtual void do_deallocate(void*, size_t, size_t)               = 0;
  virtual bool do_is_equal(memory_resource const&) const noexcept = 0;
};

// [mem.res.eq]

inline _LIBCPP_AVAILABILITY_PMR _LIBCPP_HIDE_FROM_ABI bool
operator==(const memory_resource& __lhs, const memory_resource& __rhs) noexcept {
  return &__lhs == &__rhs || __lhs.is_equal(__rhs);
}

#  if _LIBCPP_STD_VER <= 17

inline _LIBCPP_AVAILABILITY_PMR _LIBCPP_HIDE_FROM_ABI bool
operator!=(const memory_resource& __lhs, const memory_resource& __rhs) noexcept {
  return !(__lhs == __rhs);
}

#  endif

// [mem.res.global]

[[__gnu__::__returns_nonnull__]] _LIBCPP_AVAILABILITY_PMR _LIBCPP_EXPORTED_FROM_ABI memory_resource*
get_default_resource() noexcept;

[[__gnu__::__returns_nonnull__]] _LIBCPP_AVAILABILITY_PMR _LIBCPP_EXPORTED_FROM_ABI memory_resource*
set_default_resource(memory_resource*) noexcept;

[[using __gnu__: __returns_nonnull__, __const__]] _LIBCPP_AVAILABILITY_PMR _LIBCPP_EXPORTED_FROM_ABI memory_resource*
new_delete_resource() noexcept;

[[using __gnu__: __returns_nonnull__, __const__]] _LIBCPP_AVAILABILITY_PMR _LIBCPP_EXPORTED_FROM_ABI memory_resource*
null_memory_resource() noexcept;

} // namespace pmr

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___MEMORY_RESOURCE_MEMORY_RESOURCE_H

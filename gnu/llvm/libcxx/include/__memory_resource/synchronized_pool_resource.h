//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_RESOURCE_SYNCHRONIZED_POOL_RESOURCE_H
#define _LIBCPP___MEMORY_RESOURCE_SYNCHRONIZED_POOL_RESOURCE_H

#include <__config>
#include <__memory_resource/memory_resource.h>
#include <__memory_resource/pool_options.h>
#include <__memory_resource/unsynchronized_pool_resource.h>
#include <cstddef>
#include <mutex>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_STD

namespace pmr {

// [mem.res.pool.overview]

class _LIBCPP_AVAILABILITY_PMR _LIBCPP_EXPORTED_FROM_ABI synchronized_pool_resource : public memory_resource {
public:
  _LIBCPP_HIDE_FROM_ABI synchronized_pool_resource(const pool_options& __opts, memory_resource* __upstream)
      : __unsync_(__opts, __upstream) {}

  _LIBCPP_HIDE_FROM_ABI synchronized_pool_resource()
      : synchronized_pool_resource(pool_options(), get_default_resource()) {}

  _LIBCPP_HIDE_FROM_ABI explicit synchronized_pool_resource(memory_resource* __upstream)
      : synchronized_pool_resource(pool_options(), __upstream) {}

  _LIBCPP_HIDE_FROM_ABI explicit synchronized_pool_resource(const pool_options& __opts)
      : synchronized_pool_resource(__opts, get_default_resource()) {}

  synchronized_pool_resource(const synchronized_pool_resource&) = delete;

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL ~synchronized_pool_resource() override = default;

  synchronized_pool_resource& operator=(const synchronized_pool_resource&) = delete;

  _LIBCPP_HIDE_FROM_ABI void release() {
#  if !defined(_LIBCPP_HAS_NO_THREADS)
    unique_lock<mutex> __lk(__mut_);
#  endif
    __unsync_.release();
  }

  _LIBCPP_HIDE_FROM_ABI memory_resource* upstream_resource() const { return __unsync_.upstream_resource(); }

  _LIBCPP_HIDE_FROM_ABI pool_options options() const { return __unsync_.options(); }

protected:
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL void* do_allocate(size_t __bytes, size_t __align) override {
#  if !defined(_LIBCPP_HAS_NO_THREADS)
    unique_lock<mutex> __lk(__mut_);
#  endif
    return __unsync_.allocate(__bytes, __align);
  }

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL void do_deallocate(void* __p, size_t __bytes, size_t __align) override {
#  if !defined(_LIBCPP_HAS_NO_THREADS)
    unique_lock<mutex> __lk(__mut_);
#  endif
    return __unsync_.deallocate(__p, __bytes, __align);
  }

  bool do_is_equal(const memory_resource& __other) const noexcept override; // key function

private:
#  if !defined(_LIBCPP_HAS_NO_THREADS)
  mutex __mut_;
#  endif
  unsynchronized_pool_resource __unsync_;
};

} // namespace pmr

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___MEMORY_RESOURCE_SYNCHRONIZED_POOL_RESOURCE_H

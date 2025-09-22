//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_RESOURCE_UNSYNCHRONIZED_POOL_RESOURCE_H
#define _LIBCPP___MEMORY_RESOURCE_UNSYNCHRONIZED_POOL_RESOURCE_H

#include <__config>
#include <__memory_resource/memory_resource.h>
#include <__memory_resource/pool_options.h>
#include <cstddef>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_STD

namespace pmr {

// [mem.res.pool.overview]

class _LIBCPP_AVAILABILITY_PMR _LIBCPP_EXPORTED_FROM_ABI unsynchronized_pool_resource : public memory_resource {
  class __fixed_pool;

  class __adhoc_pool {
    struct __chunk_footer;
    __chunk_footer* __first_;

  public:
    _LIBCPP_HIDE_FROM_ABI explicit __adhoc_pool() : __first_(nullptr) {}

    void __release_ptr(memory_resource* __upstream);
    void* __do_allocate(memory_resource* __upstream, size_t __bytes, size_t __align);
    void __do_deallocate(memory_resource* __upstream, void* __p, size_t __bytes, size_t __align);
  };

  static const size_t __min_blocks_per_chunk = 16;
  static const size_t __min_bytes_per_chunk  = 1024;
  static const size_t __max_blocks_per_chunk = (size_t(1) << 20);
  static const size_t __max_bytes_per_chunk  = (size_t(1) << 30);

  static const int __log2_smallest_block_size      = 3;
  static const size_t __smallest_block_size        = 8;
  static const size_t __default_largest_block_size = (size_t(1) << 20);
  static const size_t __max_largest_block_size     = (size_t(1) << 30);

  size_t __pool_block_size(int __i) const;
  int __log2_pool_block_size(int __i) const;
  int __pool_index(size_t __bytes, size_t __align) const;

public:
  unsynchronized_pool_resource(const pool_options& __opts, memory_resource* __upstream);

  _LIBCPP_HIDE_FROM_ABI unsynchronized_pool_resource()
      : unsynchronized_pool_resource(pool_options(), get_default_resource()) {}

  _LIBCPP_HIDE_FROM_ABI explicit unsynchronized_pool_resource(memory_resource* __upstream)
      : unsynchronized_pool_resource(pool_options(), __upstream) {}

  _LIBCPP_HIDE_FROM_ABI explicit unsynchronized_pool_resource(const pool_options& __opts)
      : unsynchronized_pool_resource(__opts, get_default_resource()) {}

  unsynchronized_pool_resource(const unsynchronized_pool_resource&) = delete;

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL ~unsynchronized_pool_resource() override { release(); }

  unsynchronized_pool_resource& operator=(const unsynchronized_pool_resource&) = delete;

  void release();

  _LIBCPP_HIDE_FROM_ABI memory_resource* upstream_resource() const { return __res_; }

  [[__gnu__::__pure__]] pool_options options() const;

protected:
  void* do_allocate(size_t __bytes, size_t __align) override; // key function

  void do_deallocate(void* __p, size_t __bytes, size_t __align) override;

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL bool do_is_equal(const memory_resource& __other) const _NOEXCEPT override {
    return &__other == this;
  }

private:
  memory_resource* __res_;
  __adhoc_pool __adhoc_pool_;
  __fixed_pool* __fixed_pools_;
  int __num_fixed_pools_;
  uint32_t __options_max_blocks_per_chunk_;
};

} // namespace pmr

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___MEMORY_RESOURCE_UNSYNCHRONIZED_POOL_RESOURCE_H

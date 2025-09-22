//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_RESOURCE_MONOTONIC_BUFFER_RESOURCE_H
#define _LIBCPP___MEMORY_RESOURCE_MONOTONIC_BUFFER_RESOURCE_H

#include <__config>
#include <__memory/addressof.h>
#include <__memory_resource/memory_resource.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_STD

namespace pmr {

// [mem.res.monotonic.buffer]

class _LIBCPP_AVAILABILITY_PMR _LIBCPP_EXPORTED_FROM_ABI monotonic_buffer_resource : public memory_resource {
  static const size_t __default_buffer_capacity  = 1024;
  static const size_t __default_buffer_alignment = 16;

  struct __chunk_footer {
    __chunk_footer* __next_;
    char* __start_;
    char* __cur_;
    size_t __align_;
    _LIBCPP_HIDE_FROM_ABI size_t __allocation_size() {
      return (reinterpret_cast<char*>(this) - __start_) + sizeof(*this);
    }
    void* __try_allocate_from_chunk(size_t, size_t);
  };

  struct __initial_descriptor {
    char* __start_;
    char* __cur_;
    union {
      char* __end_;
      size_t __size_;
    };
    void* __try_allocate_from_chunk(size_t, size_t);
  };

public:
  _LIBCPP_HIDE_FROM_ABI monotonic_buffer_resource()
      : monotonic_buffer_resource(nullptr, __default_buffer_capacity, get_default_resource()) {}

  _LIBCPP_HIDE_FROM_ABI explicit monotonic_buffer_resource(size_t __initial_size)
      : monotonic_buffer_resource(nullptr, __initial_size, get_default_resource()) {}

  _LIBCPP_HIDE_FROM_ABI monotonic_buffer_resource(void* __buffer, size_t __buffer_size)
      : monotonic_buffer_resource(__buffer, __buffer_size, get_default_resource()) {}

  _LIBCPP_HIDE_FROM_ABI explicit monotonic_buffer_resource(memory_resource* __upstream)
      : monotonic_buffer_resource(nullptr, __default_buffer_capacity, __upstream) {}

  _LIBCPP_HIDE_FROM_ABI monotonic_buffer_resource(size_t __initial_size, memory_resource* __upstream)
      : monotonic_buffer_resource(nullptr, __initial_size, __upstream) {}

  _LIBCPP_HIDE_FROM_ABI monotonic_buffer_resource(void* __buffer, size_t __buffer_size, memory_resource* __upstream)
      : __res_(__upstream) {
    __initial_.__start_ = static_cast<char*>(__buffer);
    if (__buffer != nullptr) {
      __initial_.__cur_ = static_cast<char*>(__buffer) + __buffer_size;
      __initial_.__end_ = static_cast<char*>(__buffer) + __buffer_size;
    } else {
      __initial_.__cur_  = nullptr;
      __initial_.__size_ = __buffer_size;
    }
    __chunks_ = nullptr;
  }

  monotonic_buffer_resource(const monotonic_buffer_resource&) = delete;

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL ~monotonic_buffer_resource() override { release(); }

  monotonic_buffer_resource& operator=(const monotonic_buffer_resource&) = delete;

  _LIBCPP_HIDE_FROM_ABI void release() {
    if (__initial_.__start_ != nullptr)
      __initial_.__cur_ = __initial_.__end_;
    while (__chunks_ != nullptr) {
      __chunk_footer* __next = __chunks_->__next_;
      __res_->deallocate(__chunks_->__start_, __chunks_->__allocation_size(), __chunks_->__align_);
      __chunks_ = __next;
    }
  }

  _LIBCPP_HIDE_FROM_ABI memory_resource* upstream_resource() const { return __res_; }

protected:
  void* do_allocate(size_t __bytes, size_t __alignment) override; // key function

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL void do_deallocate(void*, size_t, size_t) override {}

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL bool do_is_equal(const memory_resource& __other) const _NOEXCEPT override {
    return this == std::addressof(__other);
  }

private:
  __initial_descriptor __initial_;
  __chunk_footer* __chunks_;
  memory_resource* __res_;
};

} // namespace pmr

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___MEMORY_RESOURCE_MONOTONIC_BUFFER_RESOURCE_H

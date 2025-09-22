//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <memory>
#include <memory_resource>

#ifndef _LIBCPP_HAS_NO_ATOMIC_HEADER
#  include <atomic>
#elif !defined(_LIBCPP_HAS_NO_THREADS)
#  include <mutex>
#  if defined(__ELF__) && defined(_LIBCPP_LINK_PTHREAD_LIB)
#    pragma comment(lib, "pthread")
#  endif
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace pmr {

// memory_resource

memory_resource::~memory_resource() = default;

// new_delete_resource()

#ifdef _LIBCPP_HAS_NO_ALIGNED_ALLOCATION
static bool is_aligned_to(void* ptr, size_t align) {
  void* p2     = ptr;
  size_t space = 1;
  void* result = std::align(align, 1, p2, space);
  return (result == ptr);
}
#endif

class _LIBCPP_EXPORTED_FROM_ABI __new_delete_memory_resource_imp : public memory_resource {
  void* do_allocate(size_t bytes, size_t align) override {
#ifndef _LIBCPP_HAS_NO_ALIGNED_ALLOCATION
    return std::__libcpp_allocate(bytes, align);
#else
    if (bytes == 0)
      bytes = 1;
    void* result = std::__libcpp_allocate(bytes, align);
    if (!is_aligned_to(result, align)) {
      std::__libcpp_deallocate(result, bytes, align);
      __throw_bad_alloc();
    }
    return result;
#endif
  }

  void do_deallocate(void* p, size_t bytes, size_t align) override { std::__libcpp_deallocate(p, bytes, align); }

  bool do_is_equal(const memory_resource& other) const noexcept override { return &other == this; }
};

// null_memory_resource()

class _LIBCPP_EXPORTED_FROM_ABI __null_memory_resource_imp : public memory_resource {
  void* do_allocate(size_t, size_t) override { __throw_bad_alloc(); }
  void do_deallocate(void*, size_t, size_t) override {}
  bool do_is_equal(const memory_resource& other) const noexcept override { return &other == this; }
};

namespace {

union ResourceInitHelper {
  struct {
    __new_delete_memory_resource_imp new_delete_res;
    __null_memory_resource_imp null_res;
  } resources;
  char dummy;
  constexpr ResourceInitHelper() : resources() {}
  ~ResourceInitHelper() {}
};

// Pretend we're inside a system header so the compiler doesn't flag the use of the init_priority
// attribute with a value that's reserved for the implementation (we're the implementation).
#include "memory_resource_init_helper.h"

} // end namespace

memory_resource* new_delete_resource() noexcept { return &res_init.resources.new_delete_res; }

memory_resource* null_memory_resource() noexcept { return &res_init.resources.null_res; }

// default_memory_resource()

static memory_resource* __default_memory_resource(bool set = false, memory_resource* new_res = nullptr) noexcept {
#ifndef _LIBCPP_HAS_NO_ATOMIC_HEADER
  static constinit atomic<memory_resource*> __res{&res_init.resources.new_delete_res};
  if (set) {
    new_res = new_res ? new_res : new_delete_resource();
    // TODO: Can a weaker ordering be used?
    return std::atomic_exchange_explicit(&__res, new_res, memory_order_acq_rel);
  } else {
    return std::atomic_load_explicit(&__res, memory_order_acquire);
  }
#elif !defined(_LIBCPP_HAS_NO_THREADS)
  static constinit memory_resource* res = &res_init.resources.new_delete_res;
  static mutex res_lock;
  if (set) {
    new_res = new_res ? new_res : new_delete_resource();
    lock_guard<mutex> guard(res_lock);
    memory_resource* old_res = res;
    res                      = new_res;
    return old_res;
  } else {
    lock_guard<mutex> guard(res_lock);
    return res;
  }
#else
  static constinit memory_resource* res = &res_init.resources.new_delete_res;
  if (set) {
    new_res                  = new_res ? new_res : new_delete_resource();
    memory_resource* old_res = res;
    res                      = new_res;
    return old_res;
  } else {
    return res;
  }
#endif
}

memory_resource* get_default_resource() noexcept { return __default_memory_resource(); }

memory_resource* set_default_resource(memory_resource* __new_res) noexcept {
  return __default_memory_resource(true, __new_res);
}

// 23.12.5, mem.res.pool

static size_t roundup(size_t count, size_t alignment) {
  size_t mask = alignment - 1;
  return (count + mask) & ~mask;
}

struct unsynchronized_pool_resource::__adhoc_pool::__chunk_footer {
  __chunk_footer* __next_;
  char* __start_;
  size_t __align_;
  size_t __allocation_size() { return (reinterpret_cast<char*>(this) - __start_) + sizeof(*this); }
};

void unsynchronized_pool_resource::__adhoc_pool::__release_ptr(memory_resource* upstream) {
  while (__first_ != nullptr) {
    __chunk_footer* next = __first_->__next_;
    upstream->deallocate(__first_->__start_, __first_->__allocation_size(), __first_->__align_);
    __first_ = next;
  }
}

void* unsynchronized_pool_resource::__adhoc_pool::__do_allocate(memory_resource* upstream, size_t bytes, size_t align) {
  const size_t footer_size  = sizeof(__chunk_footer);
  const size_t footer_align = alignof(__chunk_footer);

  if (align < footer_align)
    align = footer_align;

  size_t aligned_capacity = roundup(bytes, footer_align) + footer_size;

  void* result = upstream->allocate(aligned_capacity, align);

  __chunk_footer* h = (__chunk_footer*)((char*)result + aligned_capacity - footer_size);
  h->__next_        = __first_;
  h->__start_       = (char*)result;
  h->__align_       = align;
  __first_          = h;
  return result;
}

void unsynchronized_pool_resource::__adhoc_pool::__do_deallocate(
    memory_resource* upstream, void* p, size_t bytes, size_t align) {
  _LIBCPP_ASSERT_NON_NULL(__first_ != nullptr, "deallocating a block that was not allocated with this allocator");
  if (__first_->__start_ == p) {
    __chunk_footer* next = __first_->__next_;
    upstream->deallocate(p, __first_->__allocation_size(), __first_->__align_);
    __first_ = next;
  } else {
    for (__chunk_footer* h = __first_; h->__next_ != nullptr; h = h->__next_) {
      if (h->__next_->__start_ == p) {
        __chunk_footer* next = h->__next_->__next_;
        upstream->deallocate(p, h->__next_->__allocation_size(), h->__next_->__align_);
        h->__next_ = next;
        return;
      }
    }
    // The request to deallocate memory ends up being a no-op, likely resulting in a memory leak.
    _LIBCPP_ASSERT_VALID_DEALLOCATION(false, "deallocating a block that was not allocated with this allocator");
  }
}

class unsynchronized_pool_resource::__fixed_pool {
  struct __chunk_footer {
    __chunk_footer* __next_;
    char* __start_;
    size_t __align_;
    size_t __allocation_size() { return (reinterpret_cast<char*>(this) - __start_) + sizeof(*this); }
  };

  struct __vacancy_header {
    __vacancy_header* __next_vacancy_;
  };

  __chunk_footer* __first_chunk_     = nullptr;
  __vacancy_header* __first_vacancy_ = nullptr;

public:
  explicit __fixed_pool() = default;

  void __release_ptr(memory_resource* upstream) {
    __first_vacancy_ = nullptr;
    while (__first_chunk_ != nullptr) {
      __chunk_footer* next = __first_chunk_->__next_;
      upstream->deallocate(__first_chunk_->__start_, __first_chunk_->__allocation_size(), __first_chunk_->__align_);
      __first_chunk_ = next;
    }
  }

  void* __try_allocate_from_vacancies() {
    if (__first_vacancy_ != nullptr) {
      void* result     = __first_vacancy_;
      __first_vacancy_ = __first_vacancy_->__next_vacancy_;
      return result;
    }
    return nullptr;
  }

  void* __allocate_in_new_chunk(memory_resource* upstream, size_t block_size, size_t chunk_size) {
    _LIBCPP_ASSERT_INTERNAL(chunk_size % block_size == 0, "");
    static_assert(__default_alignment >= alignof(std::max_align_t), "");
    static_assert(__default_alignment >= alignof(__chunk_footer), "");
    static_assert(__default_alignment >= alignof(__vacancy_header), "");

    const size_t footer_size  = sizeof(__chunk_footer);
    const size_t footer_align = alignof(__chunk_footer);

    size_t aligned_capacity = roundup(chunk_size, footer_align) + footer_size;

    void* result = upstream->allocate(aligned_capacity, __default_alignment);

    __chunk_footer* h = (__chunk_footer*)((char*)result + aligned_capacity - footer_size);
    h->__next_        = __first_chunk_;
    h->__start_       = (char*)result;
    h->__align_       = __default_alignment;
    __first_chunk_    = h;

    if (chunk_size > block_size) {
      __vacancy_header* last_vh = this->__first_vacancy_;
      for (size_t i = block_size; i != chunk_size; i += block_size) {
        __vacancy_header* vh = (__vacancy_header*)((char*)result + i);
        vh->__next_vacancy_  = last_vh;
        last_vh              = vh;
      }
      this->__first_vacancy_ = last_vh;
    }
    return result;
  }

  void __evacuate(void* p) {
    __vacancy_header* vh = (__vacancy_header*)(p);
    vh->__next_vacancy_  = __first_vacancy_;
    __first_vacancy_     = vh;
  }

  size_t __previous_chunk_size_in_bytes() const { return __first_chunk_ ? __first_chunk_->__allocation_size() : 0; }

  static const size_t __default_alignment = alignof(max_align_t);
};

size_t unsynchronized_pool_resource::__pool_block_size(int i) const { return size_t(1) << __log2_pool_block_size(i); }

int unsynchronized_pool_resource::__log2_pool_block_size(int i) const { return (i + __log2_smallest_block_size); }

int unsynchronized_pool_resource::__pool_index(size_t bytes, size_t align) const {
  if (align > alignof(std::max_align_t) || bytes > (size_t(1) << __num_fixed_pools_))
    return __num_fixed_pools_;
  else {
    int i = 0;
    bytes = (bytes > align) ? bytes : align;
    bytes -= 1;
    bytes >>= __log2_smallest_block_size;
    while (bytes != 0) {
      bytes >>= 1;
      i += 1;
    }
    return i;
  }
}

unsynchronized_pool_resource::unsynchronized_pool_resource(const pool_options& opts, memory_resource* upstream)
    : __res_(upstream), __fixed_pools_(nullptr) {
  size_t largest_block_size;
  if (opts.largest_required_pool_block == 0)
    largest_block_size = __default_largest_block_size;
  else if (opts.largest_required_pool_block < __smallest_block_size)
    largest_block_size = __smallest_block_size;
  else if (opts.largest_required_pool_block > __max_largest_block_size)
    largest_block_size = __max_largest_block_size;
  else
    largest_block_size = opts.largest_required_pool_block;

  if (opts.max_blocks_per_chunk == 0)
    __options_max_blocks_per_chunk_ = __max_blocks_per_chunk;
  else if (opts.max_blocks_per_chunk < __min_blocks_per_chunk)
    __options_max_blocks_per_chunk_ = __min_blocks_per_chunk;
  else if (opts.max_blocks_per_chunk > __max_blocks_per_chunk)
    __options_max_blocks_per_chunk_ = __max_blocks_per_chunk;
  else
    __options_max_blocks_per_chunk_ = opts.max_blocks_per_chunk;

  __num_fixed_pools_ = 1;
  size_t capacity    = __smallest_block_size;
  while (capacity < largest_block_size) {
    capacity <<= 1;
    __num_fixed_pools_ += 1;
  }
}

pool_options unsynchronized_pool_resource::options() const {
  pool_options p;
  p.max_blocks_per_chunk        = __options_max_blocks_per_chunk_;
  p.largest_required_pool_block = __pool_block_size(__num_fixed_pools_ - 1);
  return p;
}

void unsynchronized_pool_resource::release() {
  __adhoc_pool_.__release_ptr(__res_);
  if (__fixed_pools_ != nullptr) {
    const int n = __num_fixed_pools_;
    for (int i = 0; i < n; ++i)
      __fixed_pools_[i].__release_ptr(__res_);
    __res_->deallocate(__fixed_pools_, __num_fixed_pools_ * sizeof(__fixed_pool), alignof(__fixed_pool));
    __fixed_pools_ = nullptr;
  }
}

void* unsynchronized_pool_resource::do_allocate(size_t bytes, size_t align) {
  // A pointer to allocated storage (6.6.4.4.1) with a size of at least bytes.
  // The size and alignment of the allocated memory shall meet the requirements for
  // a class derived from memory_resource (23.12).
  // If the pool selected for a block of size bytes is unable to satisfy the memory request
  // from its own internal data structures, it will call upstream_resource()->allocate()
  // to obtain more memory. If bytes is larger than that which the largest pool can handle,
  // then memory will be allocated using upstream_resource()->allocate().

  int i = __pool_index(bytes, align);
  if (i == __num_fixed_pools_)
    return __adhoc_pool_.__do_allocate(__res_, bytes, align);
  else {
    if (__fixed_pools_ == nullptr) {
      __fixed_pools_ =
          (__fixed_pool*)__res_->allocate(__num_fixed_pools_ * sizeof(__fixed_pool), alignof(__fixed_pool));
      __fixed_pool* first = __fixed_pools_;
      __fixed_pool* last  = __fixed_pools_ + __num_fixed_pools_;
      for (__fixed_pool* pool = first; pool != last; ++pool)
        ::new ((void*)pool) __fixed_pool;
    }
    void* result = __fixed_pools_[i].__try_allocate_from_vacancies();
    if (result == nullptr) {
      auto min = [](size_t a, size_t b) { return a < b ? a : b; };
      auto max = [](size_t a, size_t b) { return a < b ? b : a; };

      size_t prev_chunk_size_in_bytes  = __fixed_pools_[i].__previous_chunk_size_in_bytes();
      size_t prev_chunk_size_in_blocks = prev_chunk_size_in_bytes >> __log2_pool_block_size(i);

      size_t chunk_size_in_blocks;

      if (prev_chunk_size_in_blocks == 0) {
        size_t min_blocks_per_chunk = max(__min_bytes_per_chunk >> __log2_pool_block_size(i), __min_blocks_per_chunk);
        chunk_size_in_blocks        = min_blocks_per_chunk;
      } else {
        static_assert(__max_bytes_per_chunk <= SIZE_MAX - (__max_bytes_per_chunk / 4), "unsigned overflow is possible");
        chunk_size_in_blocks = prev_chunk_size_in_blocks + (prev_chunk_size_in_blocks / 4);
      }

      size_t max_blocks_per_chunk =
          min((__max_bytes_per_chunk >> __log2_pool_block_size(i)),
              min(__max_blocks_per_chunk, __options_max_blocks_per_chunk_));
      if (chunk_size_in_blocks > max_blocks_per_chunk)
        chunk_size_in_blocks = max_blocks_per_chunk;

      size_t block_size = __pool_block_size(i);

      size_t chunk_size_in_bytes = (chunk_size_in_blocks << __log2_pool_block_size(i));
      result                     = __fixed_pools_[i].__allocate_in_new_chunk(__res_, block_size, chunk_size_in_bytes);
    }
    return result;
  }
}

void unsynchronized_pool_resource::do_deallocate(void* p, size_t bytes, size_t align) {
  // Returns the memory at p to the pool. It is unspecified if,
  // or under what circumstances, this operation will result in
  // a call to upstream_resource()->deallocate().

  int i = __pool_index(bytes, align);
  if (i == __num_fixed_pools_)
    return __adhoc_pool_.__do_deallocate(__res_, p, bytes, align);
  else {
    _LIBCPP_ASSERT_NON_NULL(
        __fixed_pools_ != nullptr, "deallocating a block that was not allocated with this allocator");
    __fixed_pools_[i].__evacuate(p);
  }
}

bool synchronized_pool_resource::do_is_equal(const memory_resource& other) const noexcept { return &other == this; }

// 23.12.6, mem.res.monotonic.buffer

static void* align_down(size_t align, size_t size, void*& ptr, size_t& space) {
  if (size > space)
    return nullptr;

  char* p1      = static_cast<char*>(ptr);
  char* new_ptr = reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(p1 - size) & ~(align - 1));

  if (new_ptr < (p1 - space))
    return nullptr;

  ptr = new_ptr;
  space -= p1 - new_ptr;

  return ptr;
}

void* monotonic_buffer_resource::__initial_descriptor::__try_allocate_from_chunk(size_t bytes, size_t align) {
  if (!__cur_)
    return nullptr;
  void* new_ptr       = static_cast<void*>(__cur_);
  size_t new_capacity = (__cur_ - __start_);
  void* aligned_ptr   = align_down(align, bytes, new_ptr, new_capacity);
  if (aligned_ptr != nullptr)
    __cur_ = static_cast<char*>(new_ptr);
  return aligned_ptr;
}

void* monotonic_buffer_resource::__chunk_footer::__try_allocate_from_chunk(size_t bytes, size_t align) {
  void* new_ptr       = static_cast<void*>(__cur_);
  size_t new_capacity = (__cur_ - __start_);
  void* aligned_ptr   = align_down(align, bytes, new_ptr, new_capacity);
  if (aligned_ptr != nullptr)
    __cur_ = static_cast<char*>(new_ptr);
  return aligned_ptr;
}

void* monotonic_buffer_resource::do_allocate(size_t bytes, size_t align) {
  const size_t footer_size  = sizeof(__chunk_footer);
  const size_t footer_align = alignof(__chunk_footer);

  auto previous_allocation_size = [&]() {
    if (__chunks_ != nullptr)
      return __chunks_->__allocation_size();

    size_t newsize = (__initial_.__start_ != nullptr) ? (__initial_.__end_ - __initial_.__start_) : __initial_.__size_;

    return roundup(newsize, footer_align) + footer_size;
  };

  if (void* result = __initial_.__try_allocate_from_chunk(bytes, align))
    return result;
  if (__chunks_ != nullptr) {
    if (void* result = __chunks_->__try_allocate_from_chunk(bytes, align))
      return result;
  }

  // Allocate a brand-new chunk.

  if (align < footer_align)
    align = footer_align;

  size_t aligned_capacity  = roundup(bytes, footer_align) + footer_size;
  size_t previous_capacity = previous_allocation_size();

  if (aligned_capacity <= previous_capacity) {
    size_t newsize   = 2 * (previous_capacity - footer_size);
    aligned_capacity = roundup(newsize, footer_align) + footer_size;
  }

  char* start            = (char*)__res_->allocate(aligned_capacity, align);
  auto end               = start + aligned_capacity - footer_size;
  __chunk_footer* footer = (__chunk_footer*)(end);
  footer->__next_        = __chunks_;
  footer->__start_       = start;
  footer->__cur_         = end;
  footer->__align_       = align;
  __chunks_              = footer;

  return __chunks_->__try_allocate_from_chunk(bytes, align);
}

} // namespace pmr

_LIBCPP_END_NAMESPACE_STD

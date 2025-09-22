//===-- dfsan.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of DataFlowSanitizer.
//
// Private DFSan header.
//===----------------------------------------------------------------------===//

#ifndef DFSAN_H
#define DFSAN_H

#include "sanitizer_common/sanitizer_internal_defs.h"

#include "dfsan_platform.h"

using __sanitizer::u32;
using __sanitizer::u8;
using __sanitizer::uptr;

// Copy declarations from public sanitizer/dfsan_interface.h header here.
typedef u8 dfsan_label;
typedef u32 dfsan_origin;

extern "C" {
void dfsan_add_label(dfsan_label label, void *addr, uptr size);
void dfsan_set_label(dfsan_label label, void *addr, uptr size);
dfsan_label dfsan_read_label(const void *addr, uptr size);
dfsan_label dfsan_union(dfsan_label l1, dfsan_label l2);
// Zero out [offset, offset+size) from __dfsan_arg_tls.
void dfsan_clear_arg_tls(uptr offset, uptr size);
// Zero out the TLS storage.
void dfsan_clear_thread_local_state();

// Set DFSan label and origin TLS of argument for a call.
// Note that offset may not correspond with argument number.
// Some arguments (aggregate/array) will use several offsets.
void dfsan_set_arg_tls(uptr offset, dfsan_label label);
void dfsan_set_arg_origin_tls(uptr offset, dfsan_origin o);

// Return the origin associated with the first taint byte in the size bytes
// from the address addr.
dfsan_origin dfsan_read_origin_of_first_taint(const void *addr, uptr size);

// Set the data within [addr, addr+size) with label and origin.
void dfsan_set_label_origin(dfsan_label label, dfsan_origin origin, void *addr,
                            uptr size);

// Copy or move the origins of the len bytes from src to dst.
void dfsan_mem_origin_transfer(const void *dst, const void *src, uptr len);

// Copy shadow bytes from src to dst.
// Note this preserves distinct taint labels at specific offsets.
void dfsan_mem_shadow_transfer(void *dst, const void *src, uptr len);
}  // extern "C"

template <typename T>
void dfsan_set_label(dfsan_label label, T &data) {
  dfsan_set_label(label, (void *)&data, sizeof(T));
}

namespace __dfsan {

extern bool dfsan_inited;
extern bool dfsan_init_is_running;

void initialize_interceptors();

inline dfsan_label *shadow_for(void *ptr) {
  return (dfsan_label *)MEM_TO_SHADOW(ptr);
}

inline const dfsan_label *shadow_for(const void *ptr) {
  return shadow_for(const_cast<void *>(ptr));
}

inline uptr unaligned_origin_for(uptr ptr) { return MEM_TO_ORIGIN(ptr); }

inline dfsan_origin *origin_for(void *ptr) {
  auto aligned_addr = unaligned_origin_for(reinterpret_cast<uptr>(ptr)) &
                      ~(sizeof(dfsan_origin) - 1);
  return reinterpret_cast<dfsan_origin *>(aligned_addr);
}

inline const dfsan_origin *origin_for(const void *ptr) {
  return origin_for(const_cast<void *>(ptr));
}

void dfsan_copy_memory(void *dst, const void *src, uptr size);

void dfsan_allocator_init();
void dfsan_deallocate(void *ptr);

void *dfsan_malloc(uptr size);
void *dfsan_calloc(uptr nmemb, uptr size);
void *dfsan_realloc(void *ptr, uptr size);
void *dfsan_reallocarray(void *ptr, uptr nmemb, uptr size);
void *dfsan_valloc(uptr size);
void *dfsan_pvalloc(uptr size);
void *dfsan_aligned_alloc(uptr alignment, uptr size);
void *dfsan_memalign(uptr alignment, uptr size);
int dfsan_posix_memalign(void **memptr, uptr alignment, uptr size);

void dfsan_init();

}  // namespace __dfsan

#endif  // DFSAN_H

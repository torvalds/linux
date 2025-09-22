//===-- scudo/interface.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_INTERFACE_H_
#define SCUDO_INTERFACE_H_

#include <stddef.h>
#include <stdint.h>

extern "C" {

__attribute__((weak)) const char *__scudo_default_options(void);

// Post-allocation & pre-deallocation hooks.
__attribute__((weak)) void __scudo_allocate_hook(void *ptr, size_t size);
__attribute__((weak)) void __scudo_deallocate_hook(void *ptr);

// `realloc` involves both deallocation and allocation but they are not reported
// atomically. In one specific case which may keep taking a snapshot right in
// the middle of `realloc` reporting the deallocation and allocation, it may
// confuse the user by missing memory from `realloc`. To alleviate that case,
// define the two `realloc` hooks to get the knowledge of the bundled hook
// calls. These hooks are optional and should only be used when a hooks user
// wants to track reallocs more closely.
//
// See more details in the comment of `realloc` in wrapper_c.inc.
__attribute__((weak)) void
__scudo_realloc_allocate_hook(void *old_ptr, void *new_ptr, size_t size);
__attribute__((weak)) void __scudo_realloc_deallocate_hook(void *old_ptr);

void __scudo_print_stats(void);

typedef void (*iterate_callback)(uintptr_t base, size_t size, void *arg);

// Determine the likely cause of a tag check fault or other memory protection
// error on a system with memory tagging support. The results are returned via
// the error_info data structure. Up to three possible causes are returned in
// the reports array, in decreasing order of probability. The remaining elements
// of reports are zero-initialized.
//
// This function may be called from a different process from the one that
// crashed. In this case, various data structures must be copied from the
// crashing process to the process that analyzes the crash.
//
// This interface is not guaranteed to be stable and may change at any time.
// Furthermore, the version of scudo in the crashing process must be the same as
// the version in the process that analyzes the crash.
//
// fault_addr is the fault address. On aarch64 this is available in the system
// register FAR_ELx, or siginfo.si_addr in Linux 5.11 or above. This address
// must include the pointer tag; this is available if SA_EXPOSE_TAGBITS was set
// in sigaction.sa_flags when the signal handler was registered. Note that the
// kernel strips the tag from the field sigcontext.fault_address, so this
// address is not suitable to be passed as fault_addr.
//
// stack_depot is a pointer to the stack depot data structure, which may be
// obtained by calling the function __scudo_get_stack_depot_addr() in the
// crashing process. The size of the stack depot is available by calling the
// function __scudo_get_stack_depot_size().
//
// region_info is a pointer to the region info data structure, which may be
// obtained by calling the function __scudo_get_region_info_addr() in the
// crashing process. The size of the region info is available by calling the
// function __scudo_get_region_info_size().
//
// memory is a pointer to a region of memory surrounding the fault address.
// The more memory available via this pointer, the more likely it is that the
// function will be able to analyze a crash correctly. It is recommended to
// provide an amount of memory equal to 16 * the primary allocator's largest
// size class either side of the fault address.
//
// memory_tags is a pointer to an array of memory tags for the memory accessed
// via memory. Each byte of this array corresponds to a region of memory of size
// equal to the architecturally defined memory tag granule size (16 on aarch64).
//
// memory_addr is the start address of memory in the crashing process's address
// space.
//
// memory_size is the size of the memory region referred to by the memory
// pointer.
void __scudo_get_error_info(struct scudo_error_info *error_info,
                            uintptr_t fault_addr, const char *stack_depot,
                            size_t stack_depot_size, const char *region_info,
                            const char *ring_buffer, size_t ring_buffer_size,
                            const char *memory, const char *memory_tags,
                            uintptr_t memory_addr, size_t memory_size);

enum scudo_error_type {
  UNKNOWN,
  USE_AFTER_FREE,
  BUFFER_OVERFLOW,
  BUFFER_UNDERFLOW,
};

struct scudo_error_report {
  enum scudo_error_type error_type;

  uintptr_t allocation_address;
  uintptr_t allocation_size;

  uint32_t allocation_tid;
  uintptr_t allocation_trace[64];

  uint32_t deallocation_tid;
  uintptr_t deallocation_trace[64];
};

struct scudo_error_info {
  struct scudo_error_report reports[3];
};

const char *__scudo_get_stack_depot_addr(void);
size_t __scudo_get_stack_depot_size(void);

const char *__scudo_get_region_info_addr(void);
size_t __scudo_get_region_info_size(void);

const char *__scudo_get_ring_buffer_addr(void);
size_t __scudo_get_ring_buffer_size(void);

#ifndef M_DECAY_TIME
#define M_DECAY_TIME -100
#endif

#ifndef M_PURGE
#define M_PURGE -101
#endif

#ifndef M_PURGE_ALL
#define M_PURGE_ALL -104
#endif

// Tune the allocator's choice of memory tags to make it more likely that
// a certain class of memory errors will be detected. The value argument should
// be one of the M_MEMTAG_TUNING_* constants below.
#ifndef M_MEMTAG_TUNING
#define M_MEMTAG_TUNING -102
#endif

// Per-thread memory initialization tuning. The value argument should be one of:
// 1: Disable automatic heap initialization and, where possible, memory tagging,
//    on this thread.
// 0: Normal behavior.
#ifndef M_THREAD_DISABLE_MEM_INIT
#define M_THREAD_DISABLE_MEM_INIT -103
#endif

#ifndef M_CACHE_COUNT_MAX
#define M_CACHE_COUNT_MAX -200
#endif

#ifndef M_CACHE_SIZE_MAX
#define M_CACHE_SIZE_MAX -201
#endif

#ifndef M_TSDS_COUNT_MAX
#define M_TSDS_COUNT_MAX -202
#endif

// Tune for buffer overflows.
#ifndef M_MEMTAG_TUNING_BUFFER_OVERFLOW
#define M_MEMTAG_TUNING_BUFFER_OVERFLOW 0
#endif

// Tune for use-after-free.
#ifndef M_MEMTAG_TUNING_UAF
#define M_MEMTAG_TUNING_UAF 1
#endif

// Print internal stats to the log.
#ifndef M_LOG_STATS
#define M_LOG_STATS -205
#endif

} // extern "C"

#endif // SCUDO_INTERFACE_H_

//===-- flags.inc -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_FLAG
#error "Define SCUDO_FLAG prior to including this file!"
#endif

SCUDO_FLAG(int, quarantine_size_kb, 0,
           "Size (in kilobytes) of quarantine used to delay the actual "
           "deallocation of chunks. Lower value may reduce memory usage but "
           "decrease the effectiveness of the mitigation.")

SCUDO_FLAG(int, thread_local_quarantine_size_kb, 0,
           "Size (in kilobytes) of per-thread cache used to offload the global "
           "quarantine. Lower value may reduce memory usage but might increase "
           "the contention on the global quarantine.")

SCUDO_FLAG(int, quarantine_max_chunk_size, 0,
           "Size (in bytes) up to which chunks will be quarantined (if lower "
           "than or equal to).")

SCUDO_FLAG(bool, dealloc_type_mismatch, false,
           "Terminate on a type mismatch in allocation-deallocation functions, "
           "eg: malloc/delete, new/free, new/delete[], etc.")

SCUDO_FLAG(bool, delete_size_mismatch, true,
           "Terminate on a size mismatch between a sized-delete and the actual "
           "size of a chunk (as provided to new/new[]).")

SCUDO_FLAG(bool, zero_contents, false, "Zero chunk contents on allocation.")

SCUDO_FLAG(bool, pattern_fill_contents, false,
           "Pattern fill chunk contents on allocation.")

SCUDO_FLAG(bool, may_return_null, true,
           "Indicate whether the allocator should terminate instead of "
           "returning NULL in otherwise non-fatal error scenarios, eg: OOM, "
           "invalid allocation alignments, etc.")

SCUDO_FLAG(int, release_to_os_interval_ms, 5000,
           "Interval (in milliseconds) at which to attempt release of unused "
           "memory to the OS. Negative values disable the feature.")

SCUDO_FLAG(int, allocation_ring_buffer_size, 32768,
           "Entries to keep in the allocation ring buffer for scudo. "
           "Values less or equal to zero disable the buffer.")

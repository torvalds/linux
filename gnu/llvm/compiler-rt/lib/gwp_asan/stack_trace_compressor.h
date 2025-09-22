//===-- stack_trace_compressor.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef GWP_ASAN_STACK_TRACE_COMPRESSOR_
#define GWP_ASAN_STACK_TRACE_COMPRESSOR_

#include <stddef.h>
#include <stdint.h>

// These functions implement stack frame compression and decompression. We store
// the zig-zag encoded pointer difference between frame[i] and frame[i - 1] as
// a variable-length integer. This can reduce the memory overhead of stack
// traces by 50%.

namespace gwp_asan {
namespace compression {

// For the stack trace in `Unpacked` with length `UnpackedSize`, pack it into
// the buffer `Packed` maximum length `PackedMaxSize`. The return value is the
// number of bytes that were written to the output buffer.
size_t pack(const uintptr_t *Unpacked, size_t UnpackedSize, uint8_t *Packed,
            size_t PackedMaxSize);

// From the packed stack trace in `Packed` of length `PackedSize`, write the
// unpacked stack trace of maximum length `UnpackedMaxSize` into `Unpacked`.
// Returns the number of full entries unpacked, or zero on error.
size_t unpack(const uint8_t *Packed, size_t PackedSize, uintptr_t *Unpacked,
              size_t UnpackedMaxSize);

} // namespace compression
} // namespace gwp_asan

#endif // GWP_ASAN_STACK_TRACE_COMPRESSOR_

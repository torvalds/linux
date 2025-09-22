//===-- msan_poisoning.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
//===----------------------------------------------------------------------===//

#ifndef MSAN_POISONING_H
#define MSAN_POISONING_H

#include "msan.h"

namespace __msan {

// Return origin for the first poisoned byte in the memory range, or 0.
u32 GetOriginIfPoisoned(uptr addr, uptr size);

// Walk [addr, addr+size) app memory region, copying origin tags from the
// corresponding positions in [src_origin, src_origin+size) where the
// corresponding shadow in [src_shadow, src_shadow+size) is non-zero.
void SetOriginIfPoisoned(uptr addr, uptr src_shadow, uptr size, u32 src_origin);

// Copy origin from src (app address) to dst (app address), creating chained
// origin ids as necessary, without overriding origin for fully initialized
// quads.
void CopyOrigin(const void *dst, const void *src, uptr size, StackTrace *stack);

// memmove() shadow and origin. Dst and src are application addresses.
// See CopyOrigin() for the origin copying logic.
void MoveShadowAndOrigin(const void *dst, const void *src, uptr size,
                         StackTrace *stack);

// memcpy() shadow and origin. Dst and src are application addresses.
// See CopyOrigin() for the origin copying logic.
void CopyShadowAndOrigin(const void *dst, const void *src, uptr size,
                         StackTrace *stack);

// memcpy() app memory, and do "the right thing" to the corresponding shadow and
// origin regions.
void CopyMemory(void *dst, const void *src, uptr size, StackTrace *stack);

// Fill shadow will value. Ptr is an application address.
void SetShadow(const void *ptr, uptr size, u8 value);

// Set origin for the memory region.
void SetOrigin(const void *dst, uptr size, u32 origin);

// Mark memory region uninitialized, with origins.
void PoisonMemory(const void *dst, uptr size, StackTrace *stack);

}  // namespace __msan

#endif  // MSAN_POISONING_H

//===-- hwasan_poisoning.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
//===----------------------------------------------------------------------===//

#ifndef HWASAN_POISONING_H
#define HWASAN_POISONING_H

#include "hwasan.h"

namespace __hwasan {
uptr TagMemory(uptr p, uptr size, tag_t tag);
uptr TagMemoryAligned(uptr p, uptr size, tag_t tag);

}  // namespace __hwasan

#endif  // HWASAN_POISONING_H

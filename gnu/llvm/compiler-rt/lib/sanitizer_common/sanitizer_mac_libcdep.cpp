//===-- sanitizer_mac_libcdep.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries and
// implements OSX-specific functions.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_APPLE
#include "sanitizer_mac.h"

#include <sys/mman.h>

namespace __sanitizer {

void RestrictMemoryToMaxAddress(uptr max_address) {
  uptr size_to_mmap = GetMaxUserVirtualAddress() + 1 - max_address;
  void *res = MmapFixedNoAccess(max_address, size_to_mmap, "high gap");
  CHECK(res != MAP_FAILED);
}

}  // namespace __sanitizer

#endif  // SANITIZER_APPLE

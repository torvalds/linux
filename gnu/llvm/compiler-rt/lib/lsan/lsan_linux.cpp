//=-- lsan_linux.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer. Linux/NetBSD/Fuchsia-specific code.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"

#if SANITIZER_LINUX || SANITIZER_NETBSD || SANITIZER_FUCHSIA

#  include "lsan_allocator.h"
#  include "lsan_thread.h"

namespace __lsan {

static THREADLOCAL ThreadContextLsanBase *current_thread = nullptr;
ThreadContextLsanBase *GetCurrentThread() { return current_thread; }
void SetCurrentThread(ThreadContextLsanBase *tctx) { current_thread = tctx; }

static THREADLOCAL AllocatorCache allocator_cache;
AllocatorCache *GetAllocatorCache() { return &allocator_cache; }

void ReplaceSystemMalloc() {}

} // namespace __lsan

#endif  // SANITIZER_LINUX || SANITIZER_NETBSD || SANITIZER_FUCHSIA

//=-- lsan_fuchsia.h ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Standalone LSan RTL code specific to Fuchsia.
//
//===---------------------------------------------------------------------===//

#ifndef LSAN_FUCHSIA_H
#define LSAN_FUCHSIA_H

#include "lsan_thread.h"
#include "sanitizer_common/sanitizer_platform.h"

#if !SANITIZER_FUCHSIA
#error "lsan_fuchsia.h is used only on Fuchsia systems (SANITIZER_FUCHSIA)"
#endif

namespace __lsan {

class ThreadContext final : public ThreadContextLsanBase {
 public:
  explicit ThreadContext(int tid);
  void OnCreated(void *arg) override;
  void OnStarted(void *arg) override;
};

}  // namespace __lsan

#endif  // LSAN_FUCHSIA_H

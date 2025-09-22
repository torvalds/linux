//=-- lsan_posix.h -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Standalone LSan RTL code common to POSIX-like systems.
//
//===---------------------------------------------------------------------===//

#ifndef LSAN_POSIX_H
#define LSAN_POSIX_H

#include "lsan_thread.h"
#include "sanitizer_common/sanitizer_platform.h"

#if !SANITIZER_POSIX
#error "lsan_posix.h is used only on POSIX-like systems (SANITIZER_POSIX)"
#endif

namespace __sanitizer {
struct DTLS;
}

namespace __lsan {

class ThreadContext final : public ThreadContextLsanBase {
 public:
  explicit ThreadContext(int tid);
  void OnStarted(void *arg) override;
  uptr tls_begin() { return tls_begin_; }
  uptr tls_end() { return tls_end_; }
  DTLS *dtls() { return dtls_; }

 private:
  uptr tls_begin_ = 0;
  uptr tls_end_ = 0;
  DTLS *dtls_ = nullptr;
};

void ThreadStart(u32 tid, tid_t os_id,
                 ThreadType thread_type = ThreadType::Regular);

}  // namespace __lsan

#endif  // LSAN_POSIX_H

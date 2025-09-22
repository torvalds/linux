//===-- SingleStepCheck.h ------------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_SingleStepCheck_H_
#define liblldb_SingleStepCheck_H_

#include <memory>
#include <sched.h>
#include <sys/types.h>

namespace lldb_private {
namespace process_linux {

// arm64 linux had a bug which prevented single-stepping and watchpoints from
// working on non-boot cpus, due to them being incorrectly initialized after
// coming out of suspend.  This issue is particularly affecting android M, which
// uses suspend ("doze mode") quite aggressively. This code detects that
// situation and makes single-stepping work by doing all the step operations on
// the boot cpu.
//
// The underlying issue has been fixed in android N and linux 4.4. This code can
// be removed once these systems become obsolete.

#if defined(__arm64__) || defined(__aarch64__)
class SingleStepWorkaround {
  ::pid_t m_tid;
  cpu_set_t m_original_set;

  SingleStepWorkaround(const SingleStepWorkaround &) = delete;
  void operator=(const SingleStepWorkaround &) = delete;

public:
  SingleStepWorkaround(::pid_t tid, cpu_set_t original_set)
      : m_tid(tid), m_original_set(original_set) {}
  ~SingleStepWorkaround();

  static std::unique_ptr<SingleStepWorkaround> Get(::pid_t tid);
};
#else
class SingleStepWorkaround {
public:
  static std::unique_ptr<SingleStepWorkaround> Get(::pid_t tid) {
    return nullptr;
  }
};
#endif

} // end namespace process_linux
} // end namespace lldb_private

#endif // #ifndef liblldb_SingleStepCheck_H_

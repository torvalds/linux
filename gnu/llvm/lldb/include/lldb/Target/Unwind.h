//===-- Unwind.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_UNWIND_H
#define LLDB_TARGET_UNWIND_H

#include <mutex>

#include "lldb/lldb-private.h"

namespace lldb_private {

class Unwind {
protected:
  // Classes that inherit from Unwind can see and modify these
  Unwind(Thread &thread) : m_thread(thread) {}

public:
  virtual ~Unwind() = default;

  void Clear() {
    std::lock_guard<std::recursive_mutex> guard(m_unwind_mutex);
    DoClear();
  }

  uint32_t GetFrameCount() {
    std::lock_guard<std::recursive_mutex> guard(m_unwind_mutex);
    return DoGetFrameCount();
  }

  uint32_t GetFramesUpTo(uint32_t end_idx) {
    lldb::addr_t cfa;
    lldb::addr_t pc;
    uint32_t idx;
    bool behaves_like_zeroth_frame = (end_idx == 0);

    for (idx = 0; idx < end_idx; idx++) {
      if (!DoGetFrameInfoAtIndex(idx, cfa, pc, behaves_like_zeroth_frame)) {
        break;
      }
    }
    return idx;
  }

  bool GetFrameInfoAtIndex(uint32_t frame_idx, lldb::addr_t &cfa,
                           lldb::addr_t &pc, bool &behaves_like_zeroth_frame) {
    std::lock_guard<std::recursive_mutex> guard(m_unwind_mutex);
    return DoGetFrameInfoAtIndex(frame_idx, cfa, pc, behaves_like_zeroth_frame);
  }

  lldb::RegisterContextSP CreateRegisterContextForFrame(StackFrame *frame) {
    std::lock_guard<std::recursive_mutex> guard(m_unwind_mutex);
    return DoCreateRegisterContextForFrame(frame);
  }

  Thread &GetThread() { return m_thread; }

protected:
  // Classes that inherit from Unwind can see and modify these
  virtual void DoClear() = 0;

  virtual uint32_t DoGetFrameCount() = 0;

  virtual bool DoGetFrameInfoAtIndex(uint32_t frame_idx, lldb::addr_t &cfa,
                                     lldb::addr_t &pc,
                                     bool &behaves_like_zeroth_frame) = 0;

  virtual lldb::RegisterContextSP
  DoCreateRegisterContextForFrame(StackFrame *frame) = 0;

  Thread &m_thread;
  std::recursive_mutex m_unwind_mutex;

private:
  Unwind(const Unwind &) = delete;
  const Unwind &operator=(const Unwind &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_UNWIND_H

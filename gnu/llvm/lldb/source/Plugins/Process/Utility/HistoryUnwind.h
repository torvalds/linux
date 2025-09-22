//===-- HistoryUnwind.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_HISTORYUNWIND_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_HISTORYUNWIND_H

#include <vector>

#include "lldb/Target/Unwind.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class HistoryUnwind : public lldb_private::Unwind {
public:
  HistoryUnwind(Thread &thread, std::vector<lldb::addr_t> pcs,
                bool pcs_are_call_addresses = false);

  ~HistoryUnwind() override;

protected:
  void DoClear() override;

  lldb::RegisterContextSP
  DoCreateRegisterContextForFrame(StackFrame *frame) override;

  bool DoGetFrameInfoAtIndex(uint32_t frame_idx, lldb::addr_t &cfa,
                             lldb::addr_t &pc,
                             bool &behaves_like_zeroth_frame) override;
  uint32_t DoGetFrameCount() override;

private:
  std::vector<lldb::addr_t> m_pcs;
  /// This boolean indicates that the PCs in the non-0 frames are call
  /// addresses and not return addresses.
  bool m_pcs_are_call_addresses;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_HISTORYUNWIND_H

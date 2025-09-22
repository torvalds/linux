//===-- NativeProcessSoftwareSingleStep.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef lldb_NativeProcessSoftwareSingleStep_h
#define lldb_NativeProcessSoftwareSingleStep_h

#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/Host/common/NativeThreadProtocol.h"

#include <map>

namespace lldb_private {

class NativeProcessSoftwareSingleStep {
public:
  Status SetupSoftwareSingleStepping(NativeThreadProtocol &thread);

protected:
  // List of thread ids stepping with a breakpoint with the address of
  // the relevan breakpoint
  std::map<lldb::tid_t, lldb::addr_t> m_threads_stepping_with_breakpoint;
};

} // namespace lldb_private

#endif // #ifndef lldb_NativeProcessSoftwareSingleStep_h

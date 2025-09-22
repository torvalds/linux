//===-- NativeRegisterContextNetBSD.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef lldb_NativeRegisterContextNetBSD_h
#define lldb_NativeRegisterContextNetBSD_h

#include "lldb/Host/common/NativeThreadProtocol.h"

#include "Plugins/Process/Utility/NativeRegisterContextRegisterInfo.h"

namespace lldb_private {
namespace process_netbsd {

class NativeProcessNetBSD;

class NativeRegisterContextNetBSD
    : public virtual NativeRegisterContextRegisterInfo {
public:
  // This function is implemented in the NativeRegisterContextNetBSD_*
  // subclasses to create a new instance of the host specific
  // NativeRegisterContextNetBSD. The implementations can't collide as only one
  // NativeRegisterContextNetBSD_* variant should be compiled into the final
  // executable.
  static NativeRegisterContextNetBSD *
  CreateHostNativeRegisterContextNetBSD(const ArchSpec &target_arch,
                                        NativeThreadProtocol &native_thread);
  virtual llvm::Error
  CopyHardwareWatchpointsFrom(NativeRegisterContextNetBSD &source) = 0;

protected:
  Status DoRegisterSet(int req, void *buf);
  virtual NativeProcessNetBSD &GetProcess();
  virtual ::pid_t GetProcessPid();
};

} // namespace process_netbsd
} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextNetBSD_h

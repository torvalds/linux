//===-- NativeRegisterContextFreeBSD.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef lldb_NativeRegisterContextFreeBSD_h
#define lldb_NativeRegisterContextFreeBSD_h

#include "Plugins/Process/Utility/NativeRegisterContextRegisterInfo.h"

namespace lldb_private {
namespace process_freebsd {

class NativeProcessFreeBSD;
class NativeThreadFreeBSD;

class NativeRegisterContextFreeBSD
    : public virtual NativeRegisterContextRegisterInfo {
public:
  // This function is implemented in the NativeRegisterContextFreeBSD_*
  // subclasses to create a new instance of the host specific
  // NativeRegisterContextFreeBSD. The implementations can't collide as only one
  // NativeRegisterContextFreeBSD_* variant should be compiled into the final
  // executable.
  static NativeRegisterContextFreeBSD *
  CreateHostNativeRegisterContextFreeBSD(const ArchSpec &target_arch,
                                         NativeThreadFreeBSD &native_thread);
  virtual llvm::Error
  CopyHardwareWatchpointsFrom(NativeRegisterContextFreeBSD &source) = 0;

protected:
  virtual NativeProcessFreeBSD &GetProcess();
  virtual ::pid_t GetProcessPid();
};

} // namespace process_freebsd
} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextFreeBSD_h

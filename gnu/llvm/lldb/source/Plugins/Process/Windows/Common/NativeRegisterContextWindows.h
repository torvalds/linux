//===-- NativeRegisterContextWindows.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NativeRegisterContextWindows_h_
#define liblldb_NativeRegisterContextWindows_h_

#include "Plugins/Process/Utility/NativeRegisterContextRegisterInfo.h"
#include "lldb/Host/common/NativeThreadProtocol.h"
#include "lldb/Utility/DataBufferHeap.h"

namespace lldb_private {

class NativeThreadWindows;

class NativeRegisterContextWindows : public NativeRegisterContextRegisterInfo {
public:
  NativeRegisterContextWindows(
      NativeThreadProtocol &native_thread,
      RegisterInfoInterface *reg_info_interface_p);

  static std::unique_ptr<NativeRegisterContextWindows>
  CreateHostNativeRegisterContextWindows(const ArchSpec &target_arch,
                                         NativeThreadProtocol &native_thread);

protected:
  lldb::thread_t GetThreadHandle() const;
};

} // namespace lldb_private

#endif // liblldb_NativeRegisterContextWindows_h_

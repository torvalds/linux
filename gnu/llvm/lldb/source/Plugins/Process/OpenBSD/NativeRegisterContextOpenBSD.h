//===-- NativeRegisterContextOpenBSD.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_NativeRegisterContextOpenBSD_h
#define lldb_NativeRegisterContextOpenBSD_h

#include "lldb/Host/common/NativeThreadProtocol.h"

#include "Plugins/Process/OpenBSD/NativeProcessOpenBSD.h"
#include "Plugins/Process/Utility/NativeRegisterContextRegisterInfo.h"

namespace lldb_private {
namespace process_openbsd {

class NativeRegisterContextOpenBSD : public NativeRegisterContextRegisterInfo {
public:
  NativeRegisterContextOpenBSD(NativeThreadProtocol &native_thread,
                              RegisterInfoInterface *reg_info_interface_p);

  // This function is implemented in the NativeRegisterContextOpenBSD_*
  // subclasses to create a new instance of the host specific
  // NativeRegisterContextOpenBSD. The implementations can't collide as only one
  // NativeRegisterContextOpenBSD_* variant should be compiled into the final
  // executable.
  static std::unique_ptr<NativeRegisterContextOpenBSD>
  CreateHostNativeRegisterContextOpenBSD(const ArchSpec &target_arch,
                                        NativeThreadProtocol &native_thread);

protected:
  virtual Status ReadGPR();
  virtual Status WriteGPR();

  virtual Status ReadFPR();
  virtual Status WriteFPR();

  virtual void *GetGPRBuffer() { return nullptr; }
  virtual size_t GetGPRSize() {
    return GetRegisterInfoInterface().GetGPRSize();
  }

  virtual void *GetFPRBuffer() { return nullptr; }
  virtual size_t GetFPRSize() { return 0; }

  virtual Status DoReadGPR(void *buf);
  virtual Status DoWriteGPR(void *buf);

  virtual Status DoReadFPR(void *buf);
  virtual Status DoWriteFPR(void *buf);

  virtual NativeProcessOpenBSD &GetProcess();
  virtual ::pid_t GetProcessPid();
};

} // namespace process_openbsd
} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextOpenBSD_h

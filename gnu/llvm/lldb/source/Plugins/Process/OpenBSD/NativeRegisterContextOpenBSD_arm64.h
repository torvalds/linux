//===-- NativeRegisterContextOpenBSD_arm64.h --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_NativeRegisterContextOpenBSD_arm64_h
#define lldb_NativeRegisterContextOpenBSD_arm64_h

// clang-format off
#include <sys/types.h>
#include <machine/reg.h>
// clang-format on

#include "Plugins/Process/OpenBSD/NativeRegisterContextOpenBSD.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm64.h"
#include "Plugins/Process/Utility/lldb-arm64-register-enums.h"

namespace lldb_private {
namespace process_openbsd {

class NativeProcessOpenBSD;

class NativeRegisterContextOpenBSD_arm64 : public NativeRegisterContextOpenBSD {
public:
  NativeRegisterContextOpenBSD_arm64(const ArchSpec &target_arch,
                                     NativeThreadProtocol &native_thread);

  // NativeRegisterContextOpenBSD_arm64 subclasses NativeRegisterContextOpenBSD,
  // which is a subclass of NativeRegisterContextRegisterInfo, which is a
  // subclass of NativeRegisterContext.
  //
  // NativeRegisterContextOpenBSD defines an interface for reading and writing
  // registers using the system ptrace API. Subclasses of NativeRegisterContextOpenBSD
  // do not need to implement any of the interface defined by NativeRegisterContextOpenBSD,
  // since it is not machine dependent, but they do have to implement a bunch
  // of stuff from the NativeRegisterContext. Some of this is handled by a
  // generic implementation in NativeRegisterContextRegisterInfo, but most of
  // but most of it has to be implemented here.

  size_t GetGPRSize() override { return sizeof(m_gpr); }
  size_t GetFPRSize() override { return sizeof(m_fpr); }

  uint32_t GetUserRegisterCount() const override;

  uint32_t GetRegisterSetCount() const override;

  const RegisterSet *GetRegisterSet(uint32_t set_index) const override;

  Status ReadRegister(const RegisterInfo *reg_info,
                      RegisterValue &reg_value) override;

  Status WriteRegister(const RegisterInfo *reg_info,
                       const RegisterValue &reg_value) override;

  Status ReadAllRegisterValues(lldb::WritableDataBufferSP &data_sp) override;

  Status WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

protected:
  void *GetGPRBuffer() override { return &m_gpr; }
  void *GetFPRBuffer() override { return &m_fpr; }

private:
  // Private member types.
  enum { GPRegSet, FPRegSet, PACMaskRegSet };

  // Private member variables.
  struct reg m_gpr;
  struct fpreg m_fpr;
  register_t m_pacmask[2];

  int GetSetForNativeRegNum(int reg_num) const;

  int ReadRegisterSet(uint32_t set);
  int WriteRegisterSet(uint32_t set);

  RegisterInfoPOSIX_arm64 &GetRegisterInfo() const;

  Status ReadPACMask();
};

} // namespace process_openbsd
} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextOpenBSD_arm64_h

//===-- NativeRegisterContextFreeBSD_powerpc.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__powerpc__)

#ifndef lldb_NativeRegisterContextFreeBSD_powerpc_h
#define lldb_NativeRegisterContextFreeBSD_powerpc_h

// clang-format off
#include <sys/types.h>
#include <machine/reg.h>
// clang-format on

#include "Plugins/Process/FreeBSD/NativeRegisterContextFreeBSD.h"
#include "Plugins/Process/Utility/RegisterContextFreeBSD_powerpc.h"

#include <array>
#include <optional>

namespace lldb_private {
namespace process_freebsd {

class NativeProcessFreeBSD;

class NativeRegisterContextFreeBSD_powerpc
    : public NativeRegisterContextFreeBSD {
public:
  NativeRegisterContextFreeBSD_powerpc(const ArchSpec &target_arch,
                                       NativeThreadFreeBSD &native_thread);

  uint32_t GetRegisterSetCount() const override;

  uint32_t GetUserRegisterCount() const override;

  const RegisterSet *GetRegisterSet(uint32_t set_index) const override;

  Status ReadRegister(const RegisterInfo *reg_info,
                      RegisterValue &reg_value) override;

  Status WriteRegister(const RegisterInfo *reg_info,
                       const RegisterValue &reg_value) override;

  Status ReadAllRegisterValues(lldb::WritableDataBufferSP &data_sp) override;

  Status WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  llvm::Error
  CopyHardwareWatchpointsFrom(NativeRegisterContextFreeBSD &source) override;

private:
  enum RegSetKind {
    GPRegSet,
    FPRegSet,
  };
  std::array<uint8_t, sizeof(reg) + sizeof(fpreg)> m_reg_data;

  std::optional<RegSetKind> GetSetForNativeRegNum(uint32_t reg_num) const;

  Status ReadRegisterSet(RegSetKind set);
  Status WriteRegisterSet(RegSetKind set);

  RegisterContextFreeBSD_powerpc &GetRegisterInfo() const;
};

} // namespace process_freebsd
} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextFreeBSD_powerpc_h

#endif // defined (__powerpc__)

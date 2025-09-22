//===-- NativeRegisterContextFreeBSD_x86_64.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__i386__) || defined(__x86_64__)

#ifndef lldb_NativeRegisterContextFreeBSD_x86_64_h
#define lldb_NativeRegisterContextFreeBSD_x86_64_h

// clang-format off
#include <sys/param.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <machine/reg.h>
// clang-format on

#include <array>
#include <optional>

#include "Plugins/Process/FreeBSD/NativeRegisterContextFreeBSD.h"
#include "Plugins/Process/Utility/RegisterContext_x86.h"
#include "Plugins/Process/Utility/NativeRegisterContextDBReg_x86.h"
#include "Plugins/Process/Utility/lldb-x86-register-enums.h"

#define LLDB_INVALID_XSAVE_OFFSET UINT32_MAX

namespace lldb_private {
namespace process_freebsd {

class NativeProcessFreeBSD;

class NativeRegisterContextFreeBSD_x86_64
    : public NativeRegisterContextFreeBSD,
      public NativeRegisterContextDBReg_x86 {
public:
  NativeRegisterContextFreeBSD_x86_64(const ArchSpec &target_arch,
                                      NativeThreadFreeBSD &native_thread);
  uint32_t GetRegisterSetCount() const override;

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
  // Private member types.
  enum RegSetKind {
    GPRegSet,
    FPRegSet,
    DBRegSet,
    YMMRegSet,
    MPXRegSet,
    MaxRegSet = MPXRegSet,
  };

  // Private member variables.
  std::array<uint8_t, sizeof(struct reg)> m_gpr;
  std::array<uint8_t, 512> m_fpr; // FXSAVE
  std::array<uint8_t, sizeof(struct dbreg)> m_dbr;
  std::vector<uint8_t> m_xsave;
  std::array<uint32_t, MaxRegSet + 1> m_xsave_offsets;
  std::array<size_t, MaxRegSet + 1> m_regset_offsets;

  std::optional<RegSetKind> GetSetForNativeRegNum(uint32_t reg_num) const;

  Status ReadRegisterSet(RegSetKind set);
  Status WriteRegisterSet(RegSetKind set);

  uint8_t *GetOffsetRegSetData(RegSetKind set, size_t reg_offset);

  struct YMMSplitPtr {
    void *xmm;
    void *ymm_hi;
  };
  std::optional<YMMSplitPtr> GetYMMSplitReg(uint32_t reg);
};

} // namespace process_freebsd
} // namespace lldb_private

#endif // #ifndef lldb_NativeRegisterContextFreeBSD_x86_64_h

#endif // defined(__x86_64__)

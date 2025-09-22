//===-- RegisterInfoPOSIX_arm.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERINFOPOSIX_ARM_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERINFOPOSIX_ARM_H

#include "RegisterInfoAndSetInterface.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/lldb-private.h"

class RegisterInfoPOSIX_arm : public lldb_private::RegisterInfoAndSetInterface {
public:
  enum { GPRegSet = 0, FPRegSet};

  struct GPR {
    uint32_t r[16]; // R0-R15
    uint32_t cpsr;  // CPSR
  };

  struct QReg {
    uint8_t bytes[16];
  };

  struct FPU {
    union {
      uint32_t s[32];
      uint64_t d[32];
      QReg q[16]; // the 128-bit NEON registers
    } floats;
    uint32_t fpscr;
  };
  struct EXC {
    uint32_t exception;
    uint32_t fsr; /* Fault status */
    uint32_t far; /* Virtual Fault Address */
  };

  struct DBG {
    uint32_t bvr[16];
    uint32_t bcr[16];
    uint32_t wvr[16];
    uint32_t wcr[16];
  };

  RegisterInfoPOSIX_arm(const lldb_private::ArchSpec &target_arch);

  size_t GetGPRSize() const override;

  size_t GetFPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;

  const lldb_private::RegisterSet *
  GetRegisterSet(size_t reg_set) const override;

  size_t GetRegisterSetCount() const override;

  size_t GetRegisterSetFromRegisterIndex(uint32_t reg_index) const override;

private:
  const lldb_private::RegisterInfo *m_register_info_p;
  uint32_t m_register_info_count;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERINFOPOSIX_ARM_H

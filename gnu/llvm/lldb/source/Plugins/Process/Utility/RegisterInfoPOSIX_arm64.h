//===-- RegisterInfoPOSIX_arm64.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERINFOPOSIX_ARM64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERINFOPOSIX_ARM64_H

#include "RegisterInfoAndSetInterface.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/Flags.h"
#include "lldb/lldb-private.h"
#include <map>

enum class SVEState : uint8_t { Unknown, Disabled, FPSIMD, Full, Streaming };

class RegisterInfoPOSIX_arm64
    : public lldb_private::RegisterInfoAndSetInterface {
public:
  enum { GPRegSet = 0, FPRegSet };

  // AArch64 register set mask value
  enum {
    eRegsetMaskDefault = 0,
    eRegsetMaskSVE = 1,
    eRegsetMaskSSVE = 2,
    eRegsetMaskPAuth = 4,
    eRegsetMaskMTE = 8,
    eRegsetMaskTLS = 16,
    eRegsetMaskZA = 32,
    eRegsetMaskZT = 64,
    eRegsetMaskDynamic = ~1,
  };

  // AArch64 Register set FP/SIMD feature configuration
  enum {
    eVectorQuadwordAArch64,
    eVectorQuadwordAArch64SVE,
    eVectorQuadwordAArch64SVEMax = 256
  };

  // based on RegisterContextDarwin_arm64.h
  LLVM_PACKED_START
  struct GPR {
    uint64_t x[29]; // x0-x28
    uint64_t fp;    // x29
    uint64_t lr;    // x30
    uint64_t sp;    // x31
    uint64_t pc;    // pc
    uint32_t cpsr;  // cpsr
    uint32_t pad;
  };
  LLVM_PACKED_END

  // based on RegisterContextDarwin_arm64.h
  struct VReg {
    uint8_t bytes[16];
  };

  // based on RegisterContextDarwin_arm64.h
  struct FPU {
    VReg v[32];
    uint32_t fpsr;
    uint32_t fpcr;
  };

  // based on RegisterContextDarwin_arm64.h
  struct EXC {
    uint64_t far;       // Virtual Fault Address
    uint32_t esr;       // Exception syndrome
    uint32_t exception; // number of arm exception token
  };

  // based on RegisterContextDarwin_arm64.h
  struct DBG {
    uint64_t bvr[16];
    uint64_t bcr[16];
    uint64_t wvr[16];
    uint64_t wcr[16];
    uint64_t mdscr_el1;
  };

  RegisterInfoPOSIX_arm64(const lldb_private::ArchSpec &target_arch,
                          lldb_private::Flags opt_regsets);

  static size_t GetGPRSizeStatic();
  size_t GetGPRSize() const override { return GetGPRSizeStatic(); }

  size_t GetFPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;

  const lldb_private::RegisterSet *
  GetRegisterSet(size_t reg_set) const override;

  size_t GetRegisterSetCount() const override;

  size_t GetRegisterSetFromRegisterIndex(uint32_t reg_index) const override;

  void AddRegSetPAuth();

  void AddRegSetMTE();

  void AddRegSetTLS(bool has_tpidr2);

  void AddRegSetSME(bool has_zt);

  uint32_t ConfigureVectorLengthSVE(uint32_t sve_vq);

  void ConfigureVectorLengthZA(uint32_t za_vq);

  bool VectorSizeIsValid(uint32_t vq) {
    // coverity[unsigned_compare]
    if (vq >= eVectorQuadwordAArch64 && vq <= eVectorQuadwordAArch64SVEMax)
      return true;
    return false;
  }

  bool IsSVEPresent() const { return m_opt_regsets.AnySet(eRegsetMaskSVE); }
  bool IsSSVEPresent() const { return m_opt_regsets.AnySet(eRegsetMaskSSVE); }
  bool IsZAPresent() const { return m_opt_regsets.AnySet(eRegsetMaskZA); }
  bool IsZTPresent() const { return m_opt_regsets.AnySet(eRegsetMaskZT); }
  bool IsPAuthPresent() const { return m_opt_regsets.AnySet(eRegsetMaskPAuth); }
  bool IsMTEPresent() const { return m_opt_regsets.AnySet(eRegsetMaskMTE); }
  bool IsTLSPresent() const { return m_opt_regsets.AnySet(eRegsetMaskTLS); }

  bool IsSVEReg(unsigned reg) const;
  bool IsSVEZReg(unsigned reg) const;
  bool IsSVEPReg(unsigned reg) const;
  bool IsSVERegVG(unsigned reg) const;
  bool IsPAuthReg(unsigned reg) const;
  bool IsMTEReg(unsigned reg) const;
  bool IsTLSReg(unsigned reg) const;
  bool IsSMEReg(unsigned reg) const;
  bool IsSMERegZA(unsigned reg) const;
  bool IsSMERegZT(unsigned reg) const;

  uint32_t GetRegNumSVEZ0() const;
  uint32_t GetRegNumSVEFFR() const;
  uint32_t GetRegNumFPCR() const;
  uint32_t GetRegNumFPSR() const;
  uint32_t GetRegNumSVEVG() const;
  uint32_t GetRegNumSMESVG() const;
  uint32_t GetPAuthOffset() const;
  uint32_t GetMTEOffset() const;
  uint32_t GetTLSOffset() const;
  uint32_t GetSMEOffset() const;

private:
  typedef std::map<uint32_t, std::vector<lldb_private::RegisterInfo>>
      per_vq_register_infos;

  per_vq_register_infos m_per_vq_reg_infos;

  uint32_t m_vector_reg_vq = eVectorQuadwordAArch64;
  uint32_t m_za_reg_vq = eVectorQuadwordAArch64;

  // In normal operation this is const. Only when SVE or SME registers change
  // size is it either replaced or the content modified.
  const lldb_private::RegisterInfo *m_register_info_p;
  uint32_t m_register_info_count;

  const lldb_private::RegisterSet *m_register_set_p;
  uint32_t m_register_set_count;

  // Contains pair of [start, end] register numbers of a register set with start
  // and end included.
  std::map<uint32_t, std::pair<uint32_t, uint32_t>> m_per_regset_regnum_range;

  lldb_private::Flags m_opt_regsets;

  std::vector<lldb_private::RegisterInfo> m_dynamic_reg_infos;
  std::vector<lldb_private::RegisterSet> m_dynamic_reg_sets;

  std::vector<uint32_t> pauth_regnum_collection;
  std::vector<uint32_t> m_mte_regnum_collection;
  std::vector<uint32_t> m_tls_regnum_collection;
  std::vector<uint32_t> m_sme_regnum_collection;
};

#endif

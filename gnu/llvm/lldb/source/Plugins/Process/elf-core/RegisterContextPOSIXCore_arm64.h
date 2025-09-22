//===-- RegisterContextPOSIXCore_arm64.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERCONTEXTPOSIXCORE_ARM64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERCONTEXTPOSIXCORE_ARM64_H

#include "Plugins/Process/Utility/LinuxPTraceDefines_arm64sve.h"
#include "Plugins/Process/Utility/RegisterContextPOSIX_arm64.h"
#include "Plugins/Process/Utility/RegisterFlagsDetector_arm64.h"

#include "Plugins/Process/elf-core/RegisterUtilities.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"

class RegisterContextCorePOSIX_arm64 : public RegisterContextPOSIX_arm64 {
public:
  static std::unique_ptr<RegisterContextCorePOSIX_arm64>
  Create(lldb_private::Thread &thread, const lldb_private::ArchSpec &arch,
         const lldb_private::DataExtractor &gpregset,
         llvm::ArrayRef<lldb_private::CoreNote> notes);

  ~RegisterContextCorePOSIX_arm64() override;

  bool ReadRegister(const lldb_private::RegisterInfo *reg_info,
                    lldb_private::RegisterValue &value) override;

  bool WriteRegister(const lldb_private::RegisterInfo *reg_info,
                     const lldb_private::RegisterValue &value) override;

  bool ReadAllRegisterValues(lldb::WritableDataBufferSP &data_sp) override;

  bool WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  bool HardwareSingleStep(bool enable) override;

protected:
  RegisterContextCorePOSIX_arm64(
      lldb_private::Thread &thread,
      std::unique_ptr<RegisterInfoPOSIX_arm64> register_info,
      const lldb_private::DataExtractor &gpregset,
      llvm::ArrayRef<lldb_private::CoreNote> notes);

  bool ReadGPR() override;

  bool ReadFPR() override;

  bool WriteGPR() override;

  bool WriteFPR() override;

private:
  lldb_private::DataExtractor m_gpr_data;
  lldb_private::DataExtractor m_fpr_data;
  lldb_private::DataExtractor m_sve_data;
  lldb_private::DataExtractor m_pac_data;
  lldb_private::DataExtractor m_tls_data;
  lldb_private::DataExtractor m_za_data;
  lldb_private::DataExtractor m_mte_data;
  lldb_private::DataExtractor m_zt_data;

  SVEState m_sve_state = SVEState::Unknown;
  uint16_t m_sve_vector_length = 0;

  // These are pseudo registers derived from the values in SSVE and ZA data.
  struct __attribute__((packed)) sme_pseudo_regs {
    uint64_t ctrl_reg;
    uint64_t svg_reg;
  };
  static_assert(sizeof(sme_pseudo_regs) == 16);

  struct sme_pseudo_regs m_sme_pseudo_regs;

  lldb_private::Arm64RegisterFlagsDetector m_register_flags_detector;

  const uint8_t *GetSVEBuffer(uint64_t offset = 0);

  void ConfigureRegisterContext();

  uint32_t CalculateSVEOffset(const lldb_private::RegisterInfo *reg_info);

  uint64_t GetSVERegVG() { return m_sve_vector_length / 8; }
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERCONTEXTPOSIXCORE_ARM64_H

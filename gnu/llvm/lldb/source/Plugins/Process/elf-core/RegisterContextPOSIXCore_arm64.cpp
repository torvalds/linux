//===-- RegisterContextPOSIXCore_arm64.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterContextPOSIXCore_arm64.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm64.h"

#include "Plugins/Process/Utility/AuxVector.h"
#include "Plugins/Process/Utility/RegisterFlagsDetector_arm64.h"
#include "Plugins/Process/elf-core/ProcessElfCore.h"
#include "Plugins/Process/elf-core/RegisterUtilities.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegisterValue.h"

#include <memory>

using namespace lldb_private;

std::unique_ptr<RegisterContextCorePOSIX_arm64>
RegisterContextCorePOSIX_arm64::Create(Thread &thread, const ArchSpec &arch,
                                       const DataExtractor &gpregset,
                                       llvm::ArrayRef<CoreNote> notes) {
  Flags opt_regsets = RegisterInfoPOSIX_arm64::eRegsetMaskDefault;

  DataExtractor ssve_data =
      getRegset(notes, arch.GetTriple(), AARCH64_SSVE_Desc);
  if (ssve_data.GetByteSize() >= sizeof(sve::user_sve_header))
    opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskSSVE);

  DataExtractor sve_data = getRegset(notes, arch.GetTriple(), AARCH64_SVE_Desc);
  if (sve_data.GetByteSize() >= sizeof(sve::user_sve_header))
    opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskSVE);

  // Pointer Authentication register set data is based on struct
  // user_pac_mask declared in ptrace.h. See reference implementation
  // in Linux kernel source at arch/arm64/include/uapi/asm/ptrace.h.
  DataExtractor pac_data = getRegset(notes, arch.GetTriple(), AARCH64_PAC_Desc);
  if (pac_data.GetByteSize() >= sizeof(uint64_t) * 2)
    opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskPAuth);

  DataExtractor tls_data = getRegset(notes, arch.GetTriple(), AARCH64_TLS_Desc);
  // A valid note will always contain at least one register, "tpidr". It may
  // expand in future.
  if (tls_data.GetByteSize() >= sizeof(uint64_t))
    opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskTLS);

  DataExtractor za_data = getRegset(notes, arch.GetTriple(), AARCH64_ZA_Desc);
  // Nothing if ZA is not present, just the header if it is disabled.
  if (za_data.GetByteSize() >= sizeof(sve::user_za_header))
    opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskZA);

  DataExtractor mte_data = getRegset(notes, arch.GetTriple(), AARCH64_MTE_Desc);
  if (mte_data.GetByteSize() >= sizeof(uint64_t))
    opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskMTE);

  DataExtractor zt_data = getRegset(notes, arch.GetTriple(), AARCH64_ZT_Desc);
  // Although ZT0 can be in a disabled state like ZA can, the kernel reports
  // its content as 0s in that state. Therefore even a disabled ZT0 will have
  // a note containing those 0s. ZT0 is a 512 bit / 64 byte register.
  if (zt_data.GetByteSize() >= 64)
    opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskZT);

  auto register_info_up =
      std::make_unique<RegisterInfoPOSIX_arm64>(arch, opt_regsets);
  return std::unique_ptr<RegisterContextCorePOSIX_arm64>(
      new RegisterContextCorePOSIX_arm64(thread, std::move(register_info_up),
                                         gpregset, notes));
}

RegisterContextCorePOSIX_arm64::RegisterContextCorePOSIX_arm64(
    Thread &thread, std::unique_ptr<RegisterInfoPOSIX_arm64> register_info,
    const DataExtractor &gpregset, llvm::ArrayRef<CoreNote> notes)
    : RegisterContextPOSIX_arm64(thread, std::move(register_info)) {
  ::memset(&m_sme_pseudo_regs, 0, sizeof(m_sme_pseudo_regs));

  ProcessElfCore *process =
      static_cast<ProcessElfCore *>(thread.GetProcess().get());
  llvm::Triple::OSType os = process->GetArchitecture().GetTriple().getOS();
  if ((os == llvm::Triple::Linux) || (os == llvm::Triple::FreeBSD)) {
    AuxVector aux_vec(process->GetAuxvData());
    std::optional<uint64_t> auxv_at_hwcap = aux_vec.GetAuxValue(
        os == llvm::Triple::FreeBSD ? AuxVector::AUXV_FREEBSD_AT_HWCAP
                                    : AuxVector::AUXV_AT_HWCAP);
    std::optional<uint64_t> auxv_at_hwcap2 =
        aux_vec.GetAuxValue(AuxVector::AUXV_AT_HWCAP2);

    m_register_flags_detector.DetectFields(auxv_at_hwcap.value_or(0),
                                           auxv_at_hwcap2.value_or(0));
    m_register_flags_detector.UpdateRegisterInfo(GetRegisterInfo(),
                                                 GetRegisterCount());
  }

  m_gpr_data.SetData(std::make_shared<DataBufferHeap>(gpregset.GetDataStart(),
                                                      gpregset.GetByteSize()));
  m_gpr_data.SetByteOrder(gpregset.GetByteOrder());

  const llvm::Triple &target_triple =
      m_register_info_up->GetTargetArchitecture().GetTriple();
  m_fpr_data = getRegset(notes, target_triple, FPR_Desc);

  if (m_register_info_up->IsSSVEPresent()) {
    m_sve_data = getRegset(notes, target_triple, AARCH64_SSVE_Desc);
    lldb::offset_t flags_offset = 12;
    uint16_t flags = m_sve_data.GetU32(&flags_offset);
    if ((flags & sve::ptrace_regs_mask) == sve::ptrace_regs_sve)
      m_sve_state = SVEState::Streaming;
  }

  if (m_sve_state != SVEState::Streaming && m_register_info_up->IsSVEPresent())
    m_sve_data = getRegset(notes, target_triple, AARCH64_SVE_Desc);

  if (m_register_info_up->IsPAuthPresent())
    m_pac_data = getRegset(notes, target_triple, AARCH64_PAC_Desc);

  if (m_register_info_up->IsTLSPresent())
    m_tls_data = getRegset(notes, target_triple, AARCH64_TLS_Desc);

  if (m_register_info_up->IsZAPresent())
    m_za_data = getRegset(notes, target_triple, AARCH64_ZA_Desc);

  if (m_register_info_up->IsMTEPresent())
    m_mte_data = getRegset(notes, target_triple, AARCH64_MTE_Desc);

  if (m_register_info_up->IsZTPresent())
    m_zt_data = getRegset(notes, target_triple, AARCH64_ZT_Desc);

  ConfigureRegisterContext();
}

RegisterContextCorePOSIX_arm64::~RegisterContextCorePOSIX_arm64() = default;

bool RegisterContextCorePOSIX_arm64::ReadGPR() { return true; }

bool RegisterContextCorePOSIX_arm64::ReadFPR() { return false; }

bool RegisterContextCorePOSIX_arm64::WriteGPR() {
  assert(0);
  return false;
}

bool RegisterContextCorePOSIX_arm64::WriteFPR() {
  assert(0);
  return false;
}

const uint8_t *RegisterContextCorePOSIX_arm64::GetSVEBuffer(uint64_t offset) {
  return m_sve_data.GetDataStart() + offset;
}

void RegisterContextCorePOSIX_arm64::ConfigureRegisterContext() {
  if (m_sve_data.GetByteSize() > sizeof(sve::user_sve_header)) {
    uint64_t sve_header_field_offset = 8;
    m_sve_vector_length = m_sve_data.GetU16(&sve_header_field_offset);

    if (m_sve_state != SVEState::Streaming) {
      sve_header_field_offset = 12;
      uint16_t sve_header_flags_field =
          m_sve_data.GetU16(&sve_header_field_offset);
      if ((sve_header_flags_field & sve::ptrace_regs_mask) ==
          sve::ptrace_regs_fpsimd)
        m_sve_state = SVEState::FPSIMD;
      else if ((sve_header_flags_field & sve::ptrace_regs_mask) ==
               sve::ptrace_regs_sve)
        m_sve_state = SVEState::Full;
    }

    if (!sve::vl_valid(m_sve_vector_length)) {
      m_sve_state = SVEState::Disabled;
      m_sve_vector_length = 0;
    }
  } else
    m_sve_state = SVEState::Disabled;

  if (m_sve_state != SVEState::Disabled)
    m_register_info_up->ConfigureVectorLengthSVE(
        sve::vq_from_vl(m_sve_vector_length));

  if (m_sve_state == SVEState::Streaming)
    m_sme_pseudo_regs.ctrl_reg |= 1;

  if (m_za_data.GetByteSize() >= sizeof(sve::user_za_header)) {
    lldb::offset_t vlen_offset = 8;
    uint16_t svl = m_za_data.GetU16(&vlen_offset);
    m_sme_pseudo_regs.svg_reg = svl / 8;
    m_register_info_up->ConfigureVectorLengthZA(svl / 16);

    // If there is register data then ZA is active. The size of the note may be
    // misleading here so we use the size field of the embedded header.
    lldb::offset_t size_offset = 0;
    uint32_t size = m_za_data.GetU32(&size_offset);
    if (size > sizeof(sve::user_za_header))
      m_sme_pseudo_regs.ctrl_reg |= 1 << 1;
  }
}

uint32_t RegisterContextCorePOSIX_arm64::CalculateSVEOffset(
    const RegisterInfo *reg_info) {
  // Start of Z0 data is after GPRs plus 8 bytes of vg register
  uint32_t sve_reg_offset = LLDB_INVALID_INDEX32;
  if (m_sve_state == SVEState::FPSIMD) {
    const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
    sve_reg_offset = sve::ptrace_fpsimd_offset + (reg - GetRegNumSVEZ0()) * 16;
  } else if (m_sve_state == SVEState::Full ||
             m_sve_state == SVEState::Streaming) {
    uint32_t sve_z0_offset = GetGPRSize() + 16;
    sve_reg_offset =
        sve::SigRegsOffset() + reg_info->byte_offset - sve_z0_offset;
  }

  return sve_reg_offset;
}

bool RegisterContextCorePOSIX_arm64::ReadRegister(const RegisterInfo *reg_info,
                                                  RegisterValue &value) {
  Status error;
  lldb::offset_t offset;

  offset = reg_info->byte_offset;
  if (offset + reg_info->byte_size <= GetGPRSize()) {
    value.SetFromMemoryData(*reg_info, m_gpr_data.GetDataStart() + offset,
                            reg_info->byte_size, lldb::eByteOrderLittle, error);
    return error.Success();
  }

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (reg == LLDB_INVALID_REGNUM)
    return false;

  if (IsFPR(reg)) {
    if (m_sve_state == SVEState::Disabled) {
      // SVE is disabled take legacy route for FPU register access
      offset -= GetGPRSize();
      if (offset < m_fpr_data.GetByteSize()) {
        value.SetFromMemoryData(*reg_info, m_fpr_data.GetDataStart() + offset,
                                reg_info->byte_size, lldb::eByteOrderLittle,
                                error);
        return error.Success();
      }
    } else {
      // FPSR and FPCR will be located right after Z registers in
      // SVEState::FPSIMD while in SVEState::Full/SVEState::Streaming they will
      // be located at the end of register data after an alignment correction
      // based on currently selected vector length.
      uint32_t sve_reg_num = LLDB_INVALID_REGNUM;
      if (reg == GetRegNumFPSR()) {
        sve_reg_num = reg;
        if (m_sve_state == SVEState::Full || m_sve_state == SVEState::Streaming)
          offset = sve::PTraceFPSROffset(sve::vq_from_vl(m_sve_vector_length));
        else if (m_sve_state == SVEState::FPSIMD)
          offset = sve::ptrace_fpsimd_offset + (32 * 16);
      } else if (reg == GetRegNumFPCR()) {
        sve_reg_num = reg;
        if (m_sve_state == SVEState::Full || m_sve_state == SVEState::Streaming)
          offset = sve::PTraceFPCROffset(sve::vq_from_vl(m_sve_vector_length));
        else if (m_sve_state == SVEState::FPSIMD)
          offset = sve::ptrace_fpsimd_offset + (32 * 16) + 4;
      } else {
        // Extract SVE Z register value register number for this reg_info
        if (reg_info->value_regs &&
            reg_info->value_regs[0] != LLDB_INVALID_REGNUM)
          sve_reg_num = reg_info->value_regs[0];
        offset = CalculateSVEOffset(GetRegisterInfoAtIndex(sve_reg_num));
      }

      assert(sve_reg_num != LLDB_INVALID_REGNUM);
      assert(offset < m_sve_data.GetByteSize());
      value.SetFromMemoryData(*reg_info, GetSVEBuffer(offset),
                              reg_info->byte_size, lldb::eByteOrderLittle,
                              error);
    }
  } else if (IsSVE(reg)) {
    if (IsSVEVG(reg)) {
      value = GetSVERegVG();
      return true;
    }

    switch (m_sve_state) {
    case SVEState::FPSIMD: {
      // In FPSIMD state SVE payload mirrors legacy fpsimd struct and so just
      // copy 16 bytes of v register to the start of z register. All other
      // SVE register will be set to zero.
      uint64_t byte_size = 1;
      uint8_t zeros = 0;
      const uint8_t *src = &zeros;
      if (IsSVEZ(reg)) {
        byte_size = 16;
        offset = CalculateSVEOffset(reg_info);
        assert(offset < m_sve_data.GetByteSize());
        src = GetSVEBuffer(offset);
      }
      value.SetFromMemoryData(*reg_info, src, byte_size, lldb::eByteOrderLittle,
                              error);
    } break;
    case SVEState::Full:
    case SVEState::Streaming:
      offset = CalculateSVEOffset(reg_info);
      assert(offset < m_sve_data.GetByteSize());
      value.SetFromMemoryData(*reg_info, GetSVEBuffer(offset),
                              reg_info->byte_size, lldb::eByteOrderLittle,
                              error);
      break;
    case SVEState::Disabled:
    default:
      return false;
    }
  } else if (IsPAuth(reg)) {
    offset = reg_info->byte_offset - m_register_info_up->GetPAuthOffset();
    assert(offset < m_pac_data.GetByteSize());
    value.SetFromMemoryData(*reg_info, m_pac_data.GetDataStart() + offset,
                            reg_info->byte_size, lldb::eByteOrderLittle, error);
  } else if (IsTLS(reg)) {
    offset = reg_info->byte_offset - m_register_info_up->GetTLSOffset();
    assert(offset < m_tls_data.GetByteSize());
    value.SetFromMemoryData(*reg_info, m_tls_data.GetDataStart() + offset,
                            reg_info->byte_size, lldb::eByteOrderLittle, error);
  } else if (IsMTE(reg)) {
    offset = reg_info->byte_offset - m_register_info_up->GetMTEOffset();
    assert(offset < m_mte_data.GetByteSize());
    value.SetFromMemoryData(*reg_info, m_mte_data.GetDataStart() + offset,
                            reg_info->byte_size, lldb::eByteOrderLittle, error);
  } else if (IsSME(reg)) {
    // If you had SME in the process, active or otherwise, there will at least
    // be a ZA header. No header, no SME at all.
    if (m_za_data.GetByteSize() < sizeof(sve::user_za_header))
      return false;

    if (m_register_info_up->IsSMERegZA(reg)) {
      // Don't use the size of the note to tell whether ZA is enabled. There may
      // be non-register padding data after the header. Use the embedded
      // header's size field instead.
      lldb::offset_t size_offset = 0;
      uint32_t size = m_za_data.GetU32(&size_offset);
      bool za_enabled = size > sizeof(sve::user_za_header);

      size_t za_note_size = m_za_data.GetByteSize();
      // For a disabled ZA we fake a value of all 0s.
      if (!za_enabled) {
        uint64_t svl = m_sme_pseudo_regs.svg_reg * 8;
        za_note_size = sizeof(sve::user_za_header) + (svl * svl);
      }

      const uint8_t *src = nullptr;
      std::vector<uint8_t> disabled_za_data;

      if (za_enabled)
        src = m_za_data.GetDataStart();
      else {
        disabled_za_data.resize(za_note_size);
        std::fill(disabled_za_data.begin(), disabled_za_data.end(), 0);
        src = disabled_za_data.data();
      }

      value.SetFromMemoryData(*reg_info, src + sizeof(sve::user_za_header),
                              reg_info->byte_size, lldb::eByteOrderLittle,
                              error);
    } else if (m_register_info_up->IsSMERegZT(reg)) {
      value.SetFromMemoryData(*reg_info, m_zt_data.GetDataStart(),
                              reg_info->byte_size, lldb::eByteOrderLittle,
                              error);
    } else {
      offset = reg_info->byte_offset - m_register_info_up->GetSMEOffset();
      assert(offset < sizeof(m_sme_pseudo_regs));
      // Host endian since these values are derived instead of being read from a
      // core file note.
      value.SetFromMemoryData(
          *reg_info, reinterpret_cast<uint8_t *>(&m_sme_pseudo_regs) + offset,
          reg_info->byte_size, lldb_private::endian::InlHostByteOrder(), error);
    }
  } else
    return false;

  return error.Success();
}

bool RegisterContextCorePOSIX_arm64::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  return false;
}

bool RegisterContextCorePOSIX_arm64::WriteRegister(const RegisterInfo *reg_info,
                                                   const RegisterValue &value) {
  return false;
}

bool RegisterContextCorePOSIX_arm64::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  return false;
}

bool RegisterContextCorePOSIX_arm64::HardwareSingleStep(bool enable) {
  return false;
}

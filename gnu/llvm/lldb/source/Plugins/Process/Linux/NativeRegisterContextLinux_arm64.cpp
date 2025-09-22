//===-- NativeRegisterContextLinux_arm64.cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__arm64__) || defined(__aarch64__)

#include "NativeRegisterContextLinux_arm.h"
#include "NativeRegisterContextLinux_arm64.h"

#include "lldb/Host/HostInfo.h"
#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/Host/linux/Ptrace.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

#include "Plugins/Process/Linux/NativeProcessLinux.h"
#include "Plugins/Process/Linux/Procfs.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "Plugins/Process/Utility/MemoryTagManagerAArch64MTE.h"
#include "Plugins/Process/Utility/RegisterFlagsDetector_arm64.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm64.h"

// System includes - They have to be included after framework includes because
// they define some macros which collide with variable names in other modules
#include <sys/uio.h>
// NT_PRSTATUS and NT_FPREGSET definition
#include <elf.h>
#include <mutex>
#include <optional>

#ifndef NT_ARM_SVE
#define NT_ARM_SVE 0x405 /* ARM Scalable Vector Extension */
#endif

#ifndef NT_ARM_SSVE
#define NT_ARM_SSVE                                                            \
  0x40b /* ARM Scalable Matrix Extension, Streaming SVE mode */
#endif

#ifndef NT_ARM_ZA
#define NT_ARM_ZA 0x40c /* ARM Scalable Matrix Extension, Array Storage */
#endif

#ifndef NT_ARM_ZT
#define NT_ARM_ZT                                                              \
  0x40d /* ARM Scalable Matrix Extension 2, lookup table register */
#endif

#ifndef NT_ARM_PAC_MASK
#define NT_ARM_PAC_MASK 0x406 /* Pointer authentication code masks */
#endif

#ifndef NT_ARM_TAGGED_ADDR_CTRL
#define NT_ARM_TAGGED_ADDR_CTRL 0x409 /* Tagged address control register */
#endif

#define HWCAP_PACA (1 << 30)

#define HWCAP2_MTE (1 << 18)

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_linux;

// A NativeRegisterContext is constructed per thread, but all threads' registers
// will contain the same fields. Therefore this mutex prevents each instance
// competing with the other, and subsequent instances from having to detect the
// fields all over again.
static std::mutex g_register_flags_detector_mutex;
static Arm64RegisterFlagsDetector g_register_flags_detector;

std::unique_ptr<NativeRegisterContextLinux>
NativeRegisterContextLinux::CreateHostNativeRegisterContextLinux(
    const ArchSpec &target_arch, NativeThreadLinux &native_thread) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::arm:
    return std::make_unique<NativeRegisterContextLinux_arm>(target_arch,
                                                            native_thread);
  case llvm::Triple::aarch64: {
    // Configure register sets supported by this AArch64 target.
    // Read SVE header to check for SVE support.
    struct sve::user_sve_header sve_header;
    struct iovec ioVec;
    ioVec.iov_base = &sve_header;
    ioVec.iov_len = sizeof(sve_header);
    unsigned int regset = NT_ARM_SVE;

    Flags opt_regsets;
    if (NativeProcessLinux::PtraceWrapper(PTRACE_GETREGSET,
                                          native_thread.GetID(), &regset,
                                          &ioVec, sizeof(sve_header))
            .Success()) {
      opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskSVE);

      // We may also have the Scalable Matrix Extension (SME) which adds a
      // streaming SVE mode.
      ioVec.iov_len = sizeof(sve_header);
      regset = NT_ARM_SSVE;
      if (NativeProcessLinux::PtraceWrapper(PTRACE_GETREGSET,
                                            native_thread.GetID(), &regset,
                                            &ioVec, sizeof(sve_header))
              .Success())
        opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskSSVE);
    }

    sve::user_za_header za_header;
    ioVec.iov_base = &za_header;
    ioVec.iov_len = sizeof(za_header);
    regset = NT_ARM_ZA;
    if (NativeProcessLinux::PtraceWrapper(PTRACE_GETREGSET,
                                          native_thread.GetID(), &regset,
                                          &ioVec, sizeof(za_header))
            .Success())
      opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskZA);

    // SME's ZT0 is a 512 bit register.
    std::array<uint8_t, 64> zt_reg;
    ioVec.iov_base = zt_reg.data();
    ioVec.iov_len = zt_reg.size();
    regset = NT_ARM_ZT;
    if (NativeProcessLinux::PtraceWrapper(PTRACE_GETREGSET,
                                          native_thread.GetID(), &regset,
                                          &ioVec, zt_reg.size())
            .Success())
      opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskZT);

    NativeProcessLinux &process = native_thread.GetProcess();

    std::optional<uint64_t> auxv_at_hwcap =
        process.GetAuxValue(AuxVector::AUXV_AT_HWCAP);
    if (auxv_at_hwcap && (*auxv_at_hwcap & HWCAP_PACA))
      opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskPAuth);

    std::optional<uint64_t> auxv_at_hwcap2 =
        process.GetAuxValue(AuxVector::AUXV_AT_HWCAP2);
    if (auxv_at_hwcap2 && (*auxv_at_hwcap2 & HWCAP2_MTE))
      opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskMTE);

    opt_regsets.Set(RegisterInfoPOSIX_arm64::eRegsetMaskTLS);

    std::lock_guard<std::mutex> lock(g_register_flags_detector_mutex);
    if (!g_register_flags_detector.HasDetected())
      g_register_flags_detector.DetectFields(auxv_at_hwcap.value_or(0),
                                             auxv_at_hwcap2.value_or(0));

    auto register_info_up =
        std::make_unique<RegisterInfoPOSIX_arm64>(target_arch, opt_regsets);
    return std::make_unique<NativeRegisterContextLinux_arm64>(
        target_arch, native_thread, std::move(register_info_up));
  }
  default:
    llvm_unreachable("have no register context for architecture");
  }
}

llvm::Expected<ArchSpec>
NativeRegisterContextLinux::DetermineArchitecture(lldb::tid_t tid) {
  return DetermineArchitectureViaGPR(
      tid, RegisterInfoPOSIX_arm64::GetGPRSizeStatic());
}

NativeRegisterContextLinux_arm64::NativeRegisterContextLinux_arm64(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread,
    std::unique_ptr<RegisterInfoPOSIX_arm64> register_info_up)
    : NativeRegisterContextRegisterInfo(native_thread,
                                        register_info_up.release()),
      NativeRegisterContextLinux(native_thread) {
  g_register_flags_detector.UpdateRegisterInfo(
      GetRegisterInfoInterface().GetRegisterInfo(),
      GetRegisterInfoInterface().GetRegisterCount());

  ::memset(&m_fpr, 0, sizeof(m_fpr));
  ::memset(&m_gpr_arm64, 0, sizeof(m_gpr_arm64));
  ::memset(&m_hwp_regs, 0, sizeof(m_hwp_regs));
  ::memset(&m_hbp_regs, 0, sizeof(m_hbp_regs));
  ::memset(&m_sve_header, 0, sizeof(m_sve_header));
  ::memset(&m_pac_mask, 0, sizeof(m_pac_mask));
  ::memset(&m_tls_regs, 0, sizeof(m_tls_regs));
  ::memset(&m_sme_pseudo_regs, 0, sizeof(m_sme_pseudo_regs));
  std::fill(m_zt_reg.begin(), m_zt_reg.end(), 0);

  m_mte_ctrl_reg = 0;

  // 16 is just a maximum value, query hardware for actual watchpoint count
  m_max_hwp_supported = 16;
  m_max_hbp_supported = 16;

  m_refresh_hwdebug_info = true;

  m_gpr_is_valid = false;
  m_fpu_is_valid = false;
  m_sve_buffer_is_valid = false;
  m_sve_header_is_valid = false;
  m_pac_mask_is_valid = false;
  m_mte_ctrl_is_valid = false;
  m_tls_is_valid = false;
  m_zt_buffer_is_valid = false;

  // SME adds the tpidr2 register
  m_tls_size = GetRegisterInfo().IsSSVEPresent() ? sizeof(m_tls_regs)
                                                 : sizeof(m_tls_regs.tpidr_reg);

  if (GetRegisterInfo().IsSVEPresent() || GetRegisterInfo().IsSSVEPresent())
    m_sve_state = SVEState::Unknown;
  else
    m_sve_state = SVEState::Disabled;
}

RegisterInfoPOSIX_arm64 &
NativeRegisterContextLinux_arm64::GetRegisterInfo() const {
  return static_cast<RegisterInfoPOSIX_arm64 &>(*m_register_info_interface_up);
}

uint32_t NativeRegisterContextLinux_arm64::GetRegisterSetCount() const {
  return GetRegisterInfo().GetRegisterSetCount();
}

const RegisterSet *
NativeRegisterContextLinux_arm64::GetRegisterSet(uint32_t set_index) const {
  return GetRegisterInfo().GetRegisterSet(set_index);
}

uint32_t NativeRegisterContextLinux_arm64::GetUserRegisterCount() const {
  uint32_t count = 0;
  for (uint32_t set_index = 0; set_index < GetRegisterSetCount(); ++set_index)
    count += GetRegisterSet(set_index)->num_registers;
  return count;
}

Status
NativeRegisterContextLinux_arm64::ReadRegister(const RegisterInfo *reg_info,
                                               RegisterValue &reg_value) {
  Status error;

  if (!reg_info) {
    error.SetErrorString("reg_info NULL");
    return error;
  }

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];

  if (reg == LLDB_INVALID_REGNUM)
    return Status("no lldb regnum for %s", reg_info && reg_info->name
                                               ? reg_info->name
                                               : "<unknown register>");

  uint8_t *src;
  uint32_t offset = LLDB_INVALID_INDEX32;
  uint64_t sve_vg;
  std::vector<uint8_t> sve_reg_non_live;

  if (IsGPR(reg)) {
    error = ReadGPR();
    if (error.Fail())
      return error;

    offset = reg_info->byte_offset;
    assert(offset < GetGPRSize());
    src = (uint8_t *)GetGPRBuffer() + offset;

  } else if (IsFPR(reg)) {
    if (m_sve_state == SVEState::Disabled) {
      // SVE is disabled take legacy route for FPU register access
      error = ReadFPR();
      if (error.Fail())
        return error;

      offset = CalculateFprOffset(reg_info);
      assert(offset < GetFPRSize());
      src = (uint8_t *)GetFPRBuffer() + offset;
    } else {
      // SVE or SSVE enabled, we will read and cache SVE ptrace data.
      // In SIMD or Full mode, the data comes from the SVE regset. In streaming
      // mode it comes from the streaming SVE regset.
      error = ReadAllSVE();
      if (error.Fail())
        return error;

      // FPSR and FPCR will be located right after Z registers in
      // SVEState::FPSIMD while in SVEState::Full or SVEState::Streaming they
      // will be located at the end of register data after an alignment
      // correction based on currently selected vector length.
      uint32_t sve_reg_num = LLDB_INVALID_REGNUM;
      if (reg == GetRegisterInfo().GetRegNumFPSR()) {
        sve_reg_num = reg;
        if (m_sve_state == SVEState::Full || m_sve_state == SVEState::Streaming)
          offset = sve::PTraceFPSROffset(sve::vq_from_vl(m_sve_header.vl));
        else if (m_sve_state == SVEState::FPSIMD)
          offset = sve::ptrace_fpsimd_offset + (32 * 16);
      } else if (reg == GetRegisterInfo().GetRegNumFPCR()) {
        sve_reg_num = reg;
        if (m_sve_state == SVEState::Full || m_sve_state == SVEState::Streaming)
          offset = sve::PTraceFPCROffset(sve::vq_from_vl(m_sve_header.vl));
        else if (m_sve_state == SVEState::FPSIMD)
          offset = sve::ptrace_fpsimd_offset + (32 * 16) + 4;
      } else {
        // Extract SVE Z register value register number for this reg_info
        if (reg_info->value_regs &&
            reg_info->value_regs[0] != LLDB_INVALID_REGNUM)
          sve_reg_num = reg_info->value_regs[0];
        offset = CalculateSVEOffset(GetRegisterInfoAtIndex(sve_reg_num));
      }

      assert(offset < GetSVEBufferSize());
      src = (uint8_t *)GetSVEBuffer() + offset;
    }
  } else if (IsTLS(reg)) {
    error = ReadTLS();
    if (error.Fail())
      return error;

    offset = reg_info->byte_offset - GetRegisterInfo().GetTLSOffset();
    assert(offset < GetTLSBufferSize());
    src = (uint8_t *)GetTLSBuffer() + offset;
  } else if (IsSVE(reg)) {
    if (m_sve_state == SVEState::Disabled || m_sve_state == SVEState::Unknown)
      return Status("SVE disabled or not supported");

    if (GetRegisterInfo().IsSVERegVG(reg)) {
      sve_vg = GetSVERegVG();
      src = (uint8_t *)&sve_vg;
    } else {
      // SVE enabled, we will read and cache SVE ptrace data
      error = ReadAllSVE();
      if (error.Fail())
        return error;

      if (m_sve_state == SVEState::FPSIMD) {
        // In FPSIMD state SVE payload mirrors legacy fpsimd struct and so
        // just copy 16 bytes of v register to the start of z register. All
        // other SVE register will be set to zero.
        sve_reg_non_live.resize(reg_info->byte_size, 0);
        src = sve_reg_non_live.data();

        if (GetRegisterInfo().IsSVEZReg(reg)) {
          offset = CalculateSVEOffset(reg_info);
          assert(offset < GetSVEBufferSize());
          ::memcpy(sve_reg_non_live.data(), (uint8_t *)GetSVEBuffer() + offset,
                   16);
        }
      } else {
        offset = CalculateSVEOffset(reg_info);
        assert(offset < GetSVEBufferSize());
        src = (uint8_t *)GetSVEBuffer() + offset;
      }
    }
  } else if (IsPAuth(reg)) {
    error = ReadPAuthMask();
    if (error.Fail())
      return error;

    offset = reg_info->byte_offset - GetRegisterInfo().GetPAuthOffset();
    assert(offset < GetPACMaskSize());
    src = (uint8_t *)GetPACMask() + offset;
  } else if (IsMTE(reg)) {
    error = ReadMTEControl();
    if (error.Fail())
      return error;

    offset = reg_info->byte_offset - GetRegisterInfo().GetMTEOffset();
    assert(offset < GetMTEControlSize());
    src = (uint8_t *)GetMTEControl() + offset;
  } else if (IsSME(reg)) {
    if (GetRegisterInfo().IsSMERegZA(reg)) {
      error = ReadZAHeader();
      if (error.Fail())
        return error;

      // If there is only a header and no registers, ZA is inactive. Read as 0
      // in this case.
      if (m_za_header.size == sizeof(m_za_header)) {
        // This will get reconfigured/reset later, so we are safe to use it.
        // ZA is a square of VL * VL and the ptrace buffer also includes the
        // header itself.
        m_za_ptrace_payload.resize(((m_za_header.vl) * (m_za_header.vl)) +
                                   GetZAHeaderSize());
        std::fill(m_za_ptrace_payload.begin(), m_za_ptrace_payload.end(), 0);
      } else {
        // ZA is active, read the real register.
        error = ReadZA();
        if (error.Fail())
          return error;
      }

      // ZA is part of the SME set but uses a separate member buffer for
      // storage. Therefore its effective byte offset is always 0 even if it
      // isn't 0 within the SME register set.
      src = (uint8_t *)GetZABuffer() + GetZAHeaderSize();
    } else if (GetRegisterInfo().IsSMERegZT(reg)) {
      // Unlike ZA, the kernel will return register data for ZT0 when ZA is not
      // enabled. This data will be all 0s so we don't have to invent anything
      // like we did for ZA.
      error = ReadZT();
      if (error.Fail())
        return error;

      src = (uint8_t *)GetZTBuffer();
    } else {
      error = ReadSMESVG();
      if (error.Fail())
        return error;

      // This is a psuedo so it never fails.
      ReadSMEControl();

      offset = reg_info->byte_offset - GetRegisterInfo().GetSMEOffset();
      assert(offset < GetSMEPseudoBufferSize());
      src = (uint8_t *)GetSMEPseudoBuffer() + offset;
    }
  } else
    return Status("failed - register wasn't recognized to be a GPR or an FPR, "
                  "write strategy unknown");

  reg_value.SetFromMemoryData(*reg_info, src, reg_info->byte_size,
                              eByteOrderLittle, error);

  return error;
}

Status NativeRegisterContextLinux_arm64::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &reg_value) {
  Status error;

  if (!reg_info)
    return Status("reg_info NULL");

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];

  if (reg == LLDB_INVALID_REGNUM)
    return Status("no lldb regnum for %s", reg_info && reg_info->name
                                               ? reg_info->name
                                               : "<unknown register>");

  uint8_t *dst;
  uint32_t offset = LLDB_INVALID_INDEX32;
  std::vector<uint8_t> sve_reg_non_live;

  if (IsGPR(reg)) {
    error = ReadGPR();
    if (error.Fail())
      return error;

    assert(reg_info->byte_offset < GetGPRSize());
    dst = (uint8_t *)GetGPRBuffer() + reg_info->byte_offset;
    ::memcpy(dst, reg_value.GetBytes(), reg_info->byte_size);

    return WriteGPR();
  } else if (IsFPR(reg)) {
    if (m_sve_state == SVEState::Disabled) {
      // SVE is disabled take legacy route for FPU register access
      error = ReadFPR();
      if (error.Fail())
        return error;

      offset = CalculateFprOffset(reg_info);
      assert(offset < GetFPRSize());
      dst = (uint8_t *)GetFPRBuffer() + offset;
      ::memcpy(dst, reg_value.GetBytes(), reg_info->byte_size);

      return WriteFPR();
    } else {
      // SVE enabled, we will read and cache SVE ptrace data.
      error = ReadAllSVE();
      if (error.Fail())
        return error;

      // FPSR and FPCR will be located right after Z registers in
      // SVEState::FPSIMD while in SVEState::Full or SVEState::Streaming they
      // will be located at the end of register data after an alignment
      // correction based on currently selected vector length.
      uint32_t sve_reg_num = LLDB_INVALID_REGNUM;
      if (reg == GetRegisterInfo().GetRegNumFPSR()) {
        sve_reg_num = reg;
        if (m_sve_state == SVEState::Full || m_sve_state == SVEState::Streaming)
          offset = sve::PTraceFPSROffset(sve::vq_from_vl(m_sve_header.vl));
        else if (m_sve_state == SVEState::FPSIMD)
          offset = sve::ptrace_fpsimd_offset + (32 * 16);
      } else if (reg == GetRegisterInfo().GetRegNumFPCR()) {
        sve_reg_num = reg;
        if (m_sve_state == SVEState::Full || m_sve_state == SVEState::Streaming)
          offset = sve::PTraceFPCROffset(sve::vq_from_vl(m_sve_header.vl));
        else if (m_sve_state == SVEState::FPSIMD)
          offset = sve::ptrace_fpsimd_offset + (32 * 16) + 4;
      } else {
        // Extract SVE Z register value register number for this reg_info
        if (reg_info->value_regs &&
            reg_info->value_regs[0] != LLDB_INVALID_REGNUM)
          sve_reg_num = reg_info->value_regs[0];
        offset = CalculateSVEOffset(GetRegisterInfoAtIndex(sve_reg_num));
      }

      assert(offset < GetSVEBufferSize());
      dst = (uint8_t *)GetSVEBuffer() + offset;
      ::memcpy(dst, reg_value.GetBytes(), reg_info->byte_size);
      return WriteAllSVE();
    }
  } else if (IsSVE(reg)) {
    if (m_sve_state == SVEState::Disabled || m_sve_state == SVEState::Unknown)
      return Status("SVE disabled or not supported");
    else {
      // Target has SVE enabled, we will read and cache SVE ptrace data
      error = ReadAllSVE();
      if (error.Fail())
        return error;

      if (GetRegisterInfo().IsSVERegVG(reg)) {
        uint64_t vg_value = reg_value.GetAsUInt64();

        if (sve::vl_valid(vg_value * 8)) {
          if (m_sve_header_is_valid && vg_value == GetSVERegVG())
            return error;

          SetSVERegVG(vg_value);

          error = WriteSVEHeader();
          if (error.Success()) {
            // Changing VG during streaming mode also changes the size of ZA.
            if (m_sve_state == SVEState::Streaming)
              m_za_header_is_valid = false;
            ConfigureRegisterContext();
          }

          if (m_sve_header_is_valid && vg_value == GetSVERegVG())
            return error;
        }

        return Status("SVE vector length update failed.");
      }

      // If target supports SVE but currently in FPSIMD mode.
      if (m_sve_state == SVEState::FPSIMD) {
        // Here we will check if writing this SVE register enables
        // SVEState::Full
        bool set_sve_state_full = false;
        const uint8_t *reg_bytes = (const uint8_t *)reg_value.GetBytes();
        if (GetRegisterInfo().IsSVEZReg(reg)) {
          for (uint32_t i = 16; i < reg_info->byte_size; i++) {
            if (reg_bytes[i]) {
              set_sve_state_full = true;
              break;
            }
          }
        } else if (GetRegisterInfo().IsSVEPReg(reg) ||
                   reg == GetRegisterInfo().GetRegNumSVEFFR()) {
          for (uint32_t i = 0; i < reg_info->byte_size; i++) {
            if (reg_bytes[i]) {
              set_sve_state_full = true;
              break;
            }
          }
        }

        if (!set_sve_state_full && GetRegisterInfo().IsSVEZReg(reg)) {
          // We are writing a Z register which is zero beyond 16 bytes so copy
          // first 16 bytes only as SVE payload mirrors legacy fpsimd structure
          offset = CalculateSVEOffset(reg_info);
          assert(offset < GetSVEBufferSize());
          dst = (uint8_t *)GetSVEBuffer() + offset;
          ::memcpy(dst, reg_value.GetBytes(), 16);

          return WriteAllSVE();
        } else
          return Status("SVE state change operation not supported");
      } else {
        offset = CalculateSVEOffset(reg_info);
        assert(offset < GetSVEBufferSize());
        dst = (uint8_t *)GetSVEBuffer() + offset;
        ::memcpy(dst, reg_value.GetBytes(), reg_info->byte_size);
        return WriteAllSVE();
      }
    }
  } else if (IsMTE(reg)) {
    error = ReadMTEControl();
    if (error.Fail())
      return error;

    offset = reg_info->byte_offset - GetRegisterInfo().GetMTEOffset();
    assert(offset < GetMTEControlSize());
    dst = (uint8_t *)GetMTEControl() + offset;
    ::memcpy(dst, reg_value.GetBytes(), reg_info->byte_size);

    return WriteMTEControl();
  } else if (IsTLS(reg)) {
    error = ReadTLS();
    if (error.Fail())
      return error;

    offset = reg_info->byte_offset - GetRegisterInfo().GetTLSOffset();
    assert(offset < GetTLSBufferSize());
    dst = (uint8_t *)GetTLSBuffer() + offset;
    ::memcpy(dst, reg_value.GetBytes(), reg_info->byte_size);

    return WriteTLS();
  } else if (IsSME(reg)) {
    if (GetRegisterInfo().IsSMERegZA(reg)) {
      error = ReadZA();
      if (error.Fail())
        return error;

      // ZA is part of the SME set but not stored with the other SME registers.
      // So its byte offset is effectively always 0.
      dst = (uint8_t *)GetZABuffer() + GetZAHeaderSize();
      ::memcpy(dst, reg_value.GetBytes(), reg_info->byte_size);

      // While this is writing a header that contains a vector length, the only
      // way to change that is via the vg register. So here we assume the length
      // will always be the current length and no reconfigure is needed.
      return WriteZA();
    } else if (GetRegisterInfo().IsSMERegZT(reg)) {
      error = ReadZT();
      if (error.Fail())
        return error;

      dst = (uint8_t *)GetZTBuffer();
      ::memcpy(dst, reg_value.GetBytes(), reg_info->byte_size);

      return WriteZT();
    } else
      return Status("Writing to SVG or SVCR is not supported.");
  }

  return Status("Failed to write register value");
}

enum RegisterSetType : uint32_t {
  GPR,
  SVE, // Used for SVE and SSVE.
  FPR, // When there is no SVE, or SVE in FPSIMD mode.
  // Pointer authentication registers are read only, so not included here.
  MTE,
  TLS,
  SME,  // ZA only, because SVCR and SVG are pseudo registers.
  SME2, // ZT only.
};

static uint8_t *AddRegisterSetType(uint8_t *dst,
                                   RegisterSetType register_set_type) {
  *(reinterpret_cast<uint32_t *>(dst)) = register_set_type;
  return dst + sizeof(uint32_t);
}

static uint8_t *AddSavedRegistersData(uint8_t *dst, void *src, size_t size) {
  ::memcpy(dst, src, size);
  return dst + size;
}

static uint8_t *AddSavedRegisters(uint8_t *dst,
                                  enum RegisterSetType register_set_type,
                                  void *src, size_t size) {
  dst = AddRegisterSetType(dst, register_set_type);
  return AddSavedRegistersData(dst, src, size);
}

Status
NativeRegisterContextLinux_arm64::CacheAllRegisters(uint32_t &cached_size) {
  Status error;
  cached_size = sizeof(RegisterSetType) + GetGPRBufferSize();
  error = ReadGPR();
  if (error.Fail())
    return error;

  if (GetRegisterInfo().IsZAPresent()) {
    error = ReadZAHeader();
    if (error.Fail())
      return error;
    // Use header size here because the buffer may contain fake data when ZA is
    // disabled. We do not want to write this fake data (all 0s) because this
    // would tell the kernel that we want ZA to become active. Which is the
    // opposite of what we want in the case where it is currently inactive.
    cached_size += sizeof(RegisterSetType) + m_za_header.size;
    // For the same reason, we need to force it to be re-read so that it will
    // always contain the real header.
    m_za_buffer_is_valid = false;
    error = ReadZA();
    if (error.Fail())
      return error;

    // We will only be restoring ZT data if ZA is active. As writing to an
    // inactive ZT enables ZA, which may not be desireable.
    if (
        // If we have ZT0, or in other words, if we have SME2.
        GetRegisterInfo().IsZTPresent() &&
        // And ZA is active, which means that ZT0 is also active.
        m_za_header.size > sizeof(m_za_header)) {
      cached_size += sizeof(RegisterSetType) + GetZTBufferSize();
      // The kernel handles an inactive ZT0 for us, and it will read as 0s if
      // inactive (unlike ZA where we fake that behaviour).
      error = ReadZT();
      if (error.Fail())
        return error;
    }
  }

  // If SVE is enabled we need not copy FPR separately.
  if (GetRegisterInfo().IsSVEPresent() || GetRegisterInfo().IsSSVEPresent()) {
    // Store mode and register data.
    cached_size +=
        sizeof(RegisterSetType) + sizeof(m_sve_state) + GetSVEBufferSize();
    error = ReadAllSVE();
  } else {
    cached_size += sizeof(RegisterSetType) + GetFPRSize();
    error = ReadFPR();
  }
  if (error.Fail())
    return error;

  if (GetRegisterInfo().IsMTEPresent()) {
    cached_size += sizeof(RegisterSetType) + GetMTEControlSize();
    error = ReadMTEControl();
    if (error.Fail())
      return error;
  }

  // tpidr is always present but tpidr2 depends on SME.
  cached_size += sizeof(RegisterSetType) + GetTLSBufferSize();
  error = ReadTLS();

  return error;
}

Status NativeRegisterContextLinux_arm64::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  // AArch64 register data must contain GPRs and either FPR or SVE registers.
  // SVE registers can be non-streaming (aka SVE) or streaming (aka SSVE).
  // Finally an optional MTE register. Pointer Authentication (PAC) registers
  // are read-only and will be skipped.

  // In order to create register data checkpoint we first read all register
  // values if not done already and calculate total size of register set data.
  // We store all register values in data_sp by copying full PTrace data that
  // corresponds to register sets enabled by current register context.

  uint32_t reg_data_byte_size = 0;
  Status error = CacheAllRegisters(reg_data_byte_size);
  if (error.Fail())
    return error;

  data_sp.reset(new DataBufferHeap(reg_data_byte_size, 0));
  uint8_t *dst = data_sp->GetBytes();

  dst = AddSavedRegisters(dst, RegisterSetType::GPR, GetGPRBuffer(),
                          GetGPRBufferSize());

  // Streaming SVE and the ZA register both use the streaming vector length.
  // When you change this, the kernel will invalidate parts of the process
  // state. Therefore we need a specific order of restoration for each mode, if
  // we also have ZA to restore.
  //
  // Streaming mode enabled, ZA enabled:
  // * Write streaming registers. This sets SVCR.SM and clears SVCR.ZA.
  // * Write ZA, this set SVCR.ZA. The register data we provide is written to
  // ZA.
  // * Result is SVCR.SM and SVCR.ZA set, with the expected data in both
  //   register sets.
  //
  // Streaming mode disabled, ZA enabled:
  // * Write ZA. This sets SVCR.ZA, and the ZA content. In the majority of cases
  //   the streaming vector length is changing, so the thread is converted into
  //   an FPSIMD thread if it is not already one. This also clears SVCR.SM.
  // * Write SVE registers, which also clears SVCR.SM but most importantly, puts
  //   us into full SVE mode instead of FPSIMD mode (where the registers are
  //   actually the 128 bit Neon registers).
  // * Result is we have SVCR.SM = 0, SVCR.ZA = 1 and the expected register
  //   state.
  //
  // Restoring in different orders leads to things like the SVE registers being
  // truncated due to the FPSIMD mode and ZA being disabled or filled with 0s
  // (disabled and 0s looks the same from inside lldb since we fake the value
  // when it's disabled).
  //
  // For more information on this, look up the uses of the relevant NT_ARM_
  // constants and the functions vec_set_vector_length, sve_set_common and
  // za_set in the Linux Kernel.

  if ((m_sve_state != SVEState::Streaming) && GetRegisterInfo().IsZAPresent()) {
    // Use the header size not the buffer size, as we may be using the buffer
    // for fake data, which we do not want to write out.
    assert(m_za_header.size <= GetZABufferSize());
    dst = AddSavedRegisters(dst, RegisterSetType::SME, GetZABuffer(),
                            m_za_header.size);
  }

  if (GetRegisterInfo().IsSVEPresent() || GetRegisterInfo().IsSSVEPresent()) {
    dst = AddRegisterSetType(dst, RegisterSetType::SVE);
    *(reinterpret_cast<SVEState *>(dst)) = m_sve_state;
    dst += sizeof(m_sve_state);
    dst = AddSavedRegistersData(dst, GetSVEBuffer(), GetSVEBufferSize());
  } else {
    dst = AddSavedRegisters(dst, RegisterSetType::FPR, GetFPRBuffer(),
                            GetFPRSize());
  }

  if ((m_sve_state == SVEState::Streaming) && GetRegisterInfo().IsZAPresent()) {
    assert(m_za_header.size <= GetZABufferSize());
    dst = AddSavedRegisters(dst, RegisterSetType::SME, GetZABuffer(),
                            m_za_header.size);
  }

  // If ZT0 is present and we are going to be restoring an active ZA (which
  // implies an active ZT0), then restore ZT0 after ZA has been set. This
  // prevents us enabling ZA accidentally after the restore of ZA disabled it.
  // If we leave ZA/ZT0 inactive and read ZT0, the kernel returns 0s. Therefore
  // there's nothing for us to restore if ZA was originally inactive.
  if (
      // If we have SME2 and therefore ZT0.
      GetRegisterInfo().IsZTPresent() &&
      // And ZA is enabled.
      m_za_header.size > sizeof(m_za_header))
    dst = AddSavedRegisters(dst, RegisterSetType::SME2, GetZTBuffer(),
                            GetZTBufferSize());

  if (GetRegisterInfo().IsMTEPresent()) {
    dst = AddSavedRegisters(dst, RegisterSetType::MTE, GetMTEControl(),
                            GetMTEControlSize());
  }

  dst = AddSavedRegisters(dst, RegisterSetType::TLS, GetTLSBuffer(),
                          GetTLSBufferSize());

  return error;
}

static Status RestoreRegisters(void *buffer, const uint8_t **src, size_t len,
                               bool &is_valid, std::function<Status()> writer) {
  ::memcpy(buffer, *src, len);
  is_valid = true;
  *src += len;
  return writer();
}

Status NativeRegisterContextLinux_arm64::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  // AArch64 register data must contain GPRs, either FPR or SVE registers
  // (which can be streaming or non-streaming) and optional MTE register.
  // Pointer Authentication (PAC) registers are read-only and will be skipped.

  // We store all register values in data_sp by copying full PTrace data that
  // corresponds to register sets enabled by current register context. In order
  // to restore from register data checkpoint we will first restore GPRs, based
  // on size of remaining register data either SVE or FPRs should be restored
  // next. SVE is not enabled if we have register data size less than or equal
  // to size of GPR + FPR + MTE.

  Status error;
  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextLinux_arm64::%s invalid data_sp provided",
        __FUNCTION__);
    return error;
  }

  const uint8_t *src = data_sp->GetBytes();
  if (src == nullptr) {
    error.SetErrorStringWithFormat("NativeRegisterContextLinux_arm64::%s "
                                   "DataBuffer::GetBytes() returned a null "
                                   "pointer",
                                   __FUNCTION__);
    return error;
  }

  uint64_t reg_data_min_size =
      GetGPRBufferSize() + GetFPRSize() + 2 * (sizeof(RegisterSetType));
  if (data_sp->GetByteSize() < reg_data_min_size) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextLinux_arm64::%s data_sp contained insufficient "
        "register data bytes, expected at least %" PRIu64 ", actual %" PRIu64,
        __FUNCTION__, reg_data_min_size, data_sp->GetByteSize());
    return error;
  }

  const uint8_t *end = src + data_sp->GetByteSize();
  while (src < end) {
    const RegisterSetType kind =
        *reinterpret_cast<const RegisterSetType *>(src);
    src += sizeof(RegisterSetType);

    switch (kind) {
    case RegisterSetType::GPR:
      error = RestoreRegisters(
          GetGPRBuffer(), &src, GetGPRBufferSize(), m_gpr_is_valid,
          std::bind(&NativeRegisterContextLinux_arm64::WriteGPR, this));
      break;
    case RegisterSetType::SVE:
      // Restore to the correct mode, streaming or not.
      m_sve_state = static_cast<SVEState>(*src);
      src += sizeof(m_sve_state);

      // First write SVE header. We do not use RestoreRegisters because we do
      // not want src to be modified yet.
      ::memcpy(GetSVEHeader(), src, GetSVEHeaderSize());
      if (!sve::vl_valid(m_sve_header.vl)) {
        m_sve_header_is_valid = false;
        error.SetErrorStringWithFormat("NativeRegisterContextLinux_arm64::%s "
                                       "Invalid SVE header in data_sp",
                                       __FUNCTION__);
        return error;
      }
      m_sve_header_is_valid = true;
      error = WriteSVEHeader();
      if (error.Fail())
        return error;

      // SVE header has been written configure SVE vector length if needed.
      // This could change ZA data too, but that will be restored again later
      // anyway.
      ConfigureRegisterContext();

      // Write header and register data, incrementing src this time.
      error = RestoreRegisters(
          GetSVEBuffer(), &src, GetSVEBufferSize(), m_sve_buffer_is_valid,
          std::bind(&NativeRegisterContextLinux_arm64::WriteAllSVE, this));
      break;
    case RegisterSetType::FPR:
      error = RestoreRegisters(
          GetFPRBuffer(), &src, GetFPRSize(), m_fpu_is_valid,
          std::bind(&NativeRegisterContextLinux_arm64::WriteFPR, this));
      break;
    case RegisterSetType::MTE:
      error = RestoreRegisters(
          GetMTEControl(), &src, GetMTEControlSize(), m_mte_ctrl_is_valid,
          std::bind(&NativeRegisterContextLinux_arm64::WriteMTEControl, this));
      break;
    case RegisterSetType::TLS:
      error = RestoreRegisters(
          GetTLSBuffer(), &src, GetTLSBufferSize(), m_tls_is_valid,
          std::bind(&NativeRegisterContextLinux_arm64::WriteTLS, this));
      break;
    case RegisterSetType::SME:
      // To enable or disable ZA you write the regset with or without register
      // data. The kernel detects this by looking at the ioVec's length, not the
      // ZA header size you pass in. Therefore we must write header and register
      // data (if present) in one go every time. Read the header only first just
      // to get the size.
      ::memcpy(GetZAHeader(), src, GetZAHeaderSize());
      // Read the header and register data. Can't use the buffer size here, it
      // may be incorrect due to being filled with dummy data previously. Resize
      // this so WriteZA uses the correct size.
      m_za_ptrace_payload.resize(m_za_header.size);
      ::memcpy(GetZABuffer(), src, GetZABufferSize());
      m_za_buffer_is_valid = true;

      error = WriteZA();
      if (error.Fail())
        return error;

      // Update size of ZA, which resizes the ptrace payload potentially
      // trashing our copy of the data we just wrote.
      ConfigureRegisterContext();

      // ZA buffer now has proper size, read back the data we wrote above, from
      // ptrace.
      error = ReadZA();
      src += GetZABufferSize();
      break;
    case RegisterSetType::SME2:
      // Doing this would activate an inactive ZA, however we will only get here
      // if the state we are restoring had an active ZA. Restoring ZT0 will
      // always come after restoring ZA.
      error = RestoreRegisters(
          GetZTBuffer(), &src, GetZTBufferSize(), m_zt_buffer_is_valid,
          std::bind(&NativeRegisterContextLinux_arm64::WriteZT, this));
      break;
    }

    if (error.Fail())
      return error;
  }

  return error;
}

bool NativeRegisterContextLinux_arm64::IsGPR(unsigned reg) const {
  if (GetRegisterInfo().GetRegisterSetFromRegisterIndex(reg) ==
      RegisterInfoPOSIX_arm64::GPRegSet)
    return true;
  return false;
}

bool NativeRegisterContextLinux_arm64::IsFPR(unsigned reg) const {
  if (GetRegisterInfo().GetRegisterSetFromRegisterIndex(reg) ==
      RegisterInfoPOSIX_arm64::FPRegSet)
    return true;
  return false;
}

bool NativeRegisterContextLinux_arm64::IsSVE(unsigned reg) const {
  return GetRegisterInfo().IsSVEReg(reg);
}

bool NativeRegisterContextLinux_arm64::IsSME(unsigned reg) const {
  return GetRegisterInfo().IsSMEReg(reg);
}

bool NativeRegisterContextLinux_arm64::IsPAuth(unsigned reg) const {
  return GetRegisterInfo().IsPAuthReg(reg);
}

bool NativeRegisterContextLinux_arm64::IsMTE(unsigned reg) const {
  return GetRegisterInfo().IsMTEReg(reg);
}

bool NativeRegisterContextLinux_arm64::IsTLS(unsigned reg) const {
  return GetRegisterInfo().IsTLSReg(reg);
}

llvm::Error NativeRegisterContextLinux_arm64::ReadHardwareDebugInfo() {
  if (!m_refresh_hwdebug_info) {
    return llvm::Error::success();
  }

  ::pid_t tid = m_thread.GetID();

  int regset = NT_ARM_HW_WATCH;
  struct iovec ioVec;
  struct user_hwdebug_state dreg_state;
  Status error;

  ioVec.iov_base = &dreg_state;
  ioVec.iov_len = sizeof(dreg_state);
  error = NativeProcessLinux::PtraceWrapper(PTRACE_GETREGSET, tid, &regset,
                                            &ioVec, ioVec.iov_len);

  if (error.Fail())
    return error.ToError();

  m_max_hwp_supported = dreg_state.dbg_info & 0xff;

  regset = NT_ARM_HW_BREAK;
  error = NativeProcessLinux::PtraceWrapper(PTRACE_GETREGSET, tid, &regset,
                                            &ioVec, ioVec.iov_len);

  if (error.Fail())
    return error.ToError();

  m_max_hbp_supported = dreg_state.dbg_info & 0xff;
  m_refresh_hwdebug_info = false;

  return llvm::Error::success();
}

llvm::Error
NativeRegisterContextLinux_arm64::WriteHardwareDebugRegs(DREGType hwbType) {
  struct iovec ioVec;
  struct user_hwdebug_state dreg_state;
  int regset;

  memset(&dreg_state, 0, sizeof(dreg_state));
  ioVec.iov_base = &dreg_state;

  switch (hwbType) {
  case eDREGTypeWATCH:
    regset = NT_ARM_HW_WATCH;
    ioVec.iov_len = sizeof(dreg_state.dbg_info) + sizeof(dreg_state.pad) +
                    (sizeof(dreg_state.dbg_regs[0]) * m_max_hwp_supported);

    for (uint32_t i = 0; i < m_max_hwp_supported; i++) {
      dreg_state.dbg_regs[i].addr = m_hwp_regs[i].address;
      dreg_state.dbg_regs[i].ctrl = m_hwp_regs[i].control;
    }
    break;
  case eDREGTypeBREAK:
    regset = NT_ARM_HW_BREAK;
    ioVec.iov_len = sizeof(dreg_state.dbg_info) + sizeof(dreg_state.pad) +
                    (sizeof(dreg_state.dbg_regs[0]) * m_max_hbp_supported);

    for (uint32_t i = 0; i < m_max_hbp_supported; i++) {
      dreg_state.dbg_regs[i].addr = m_hbp_regs[i].address;
      dreg_state.dbg_regs[i].ctrl = m_hbp_regs[i].control;
    }
    break;
  }

  return NativeProcessLinux::PtraceWrapper(PTRACE_SETREGSET, m_thread.GetID(),
                                           &regset, &ioVec, ioVec.iov_len)
      .ToError();
}

Status NativeRegisterContextLinux_arm64::ReadGPR() {
  Status error;

  if (m_gpr_is_valid)
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetGPRBuffer();
  ioVec.iov_len = GetGPRBufferSize();

  error = ReadRegisterSet(&ioVec, GetGPRBufferSize(), NT_PRSTATUS);

  if (error.Success())
    m_gpr_is_valid = true;

  return error;
}

Status NativeRegisterContextLinux_arm64::WriteGPR() {
  Status error = ReadGPR();
  if (error.Fail())
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetGPRBuffer();
  ioVec.iov_len = GetGPRBufferSize();

  m_gpr_is_valid = false;

  return WriteRegisterSet(&ioVec, GetGPRBufferSize(), NT_PRSTATUS);
}

Status NativeRegisterContextLinux_arm64::ReadFPR() {
  Status error;

  if (m_fpu_is_valid)
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetFPRBuffer();
  ioVec.iov_len = GetFPRSize();

  error = ReadRegisterSet(&ioVec, GetFPRSize(), NT_FPREGSET);

  if (error.Success())
    m_fpu_is_valid = true;

  return error;
}

Status NativeRegisterContextLinux_arm64::WriteFPR() {
  Status error = ReadFPR();
  if (error.Fail())
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetFPRBuffer();
  ioVec.iov_len = GetFPRSize();

  m_fpu_is_valid = false;

  return WriteRegisterSet(&ioVec, GetFPRSize(), NT_FPREGSET);
}

void NativeRegisterContextLinux_arm64::InvalidateAllRegisters() {
  m_gpr_is_valid = false;
  m_fpu_is_valid = false;
  m_sve_buffer_is_valid = false;
  m_sve_header_is_valid = false;
  m_za_buffer_is_valid = false;
  m_za_header_is_valid = false;
  m_pac_mask_is_valid = false;
  m_mte_ctrl_is_valid = false;
  m_tls_is_valid = false;
  m_zt_buffer_is_valid = false;

  // Update SVE and ZA registers in case there is change in configuration.
  ConfigureRegisterContext();
}

unsigned NativeRegisterContextLinux_arm64::GetSVERegSet() {
  return m_sve_state == SVEState::Streaming ? NT_ARM_SSVE : NT_ARM_SVE;
}

Status NativeRegisterContextLinux_arm64::ReadSVEHeader() {
  Status error;

  if (m_sve_header_is_valid)
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetSVEHeader();
  ioVec.iov_len = GetSVEHeaderSize();

  error = ReadRegisterSet(&ioVec, GetSVEHeaderSize(), GetSVERegSet());

  if (error.Success())
    m_sve_header_is_valid = true;

  return error;
}

Status NativeRegisterContextLinux_arm64::ReadPAuthMask() {
  Status error;

  if (m_pac_mask_is_valid)
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetPACMask();
  ioVec.iov_len = GetPACMaskSize();

  error = ReadRegisterSet(&ioVec, GetPACMaskSize(), NT_ARM_PAC_MASK);

  if (error.Success())
    m_pac_mask_is_valid = true;

  return error;
}

Status NativeRegisterContextLinux_arm64::WriteSVEHeader() {
  Status error;

  error = ReadSVEHeader();
  if (error.Fail())
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetSVEHeader();
  ioVec.iov_len = GetSVEHeaderSize();

  m_sve_buffer_is_valid = false;
  m_sve_header_is_valid = false;
  m_fpu_is_valid = false;

  return WriteRegisterSet(&ioVec, GetSVEHeaderSize(), GetSVERegSet());
}

Status NativeRegisterContextLinux_arm64::ReadAllSVE() {
  Status error;
  if (m_sve_buffer_is_valid)
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetSVEBuffer();
  ioVec.iov_len = GetSVEBufferSize();

  error = ReadRegisterSet(&ioVec, GetSVEBufferSize(), GetSVERegSet());

  if (error.Success())
    m_sve_buffer_is_valid = true;

  return error;
}

Status NativeRegisterContextLinux_arm64::WriteAllSVE() {
  Status error;

  error = ReadAllSVE();
  if (error.Fail())
    return error;

  struct iovec ioVec;

  ioVec.iov_base = GetSVEBuffer();
  ioVec.iov_len = GetSVEBufferSize();

  m_sve_buffer_is_valid = false;
  m_sve_header_is_valid = false;
  m_fpu_is_valid = false;

  return WriteRegisterSet(&ioVec, GetSVEBufferSize(), GetSVERegSet());
}

Status NativeRegisterContextLinux_arm64::ReadSMEControl() {
  // The real register is SVCR and is accessible from EL0. However we don't want
  // to have to JIT code into the target process so we'll just recreate it using
  // what we know from ptrace.

  // Bit 0 indicates whether streaming mode is active.
  m_sme_pseudo_regs.ctrl_reg = m_sve_state == SVEState::Streaming;

  // Bit 1 indicates whether the array storage is active.
  // It is active if we can read the header and the size field tells us that
  // there is register data following it.
  Status error = ReadZAHeader();
  if (error.Success() && (m_za_header.size > sizeof(m_za_header)))
    m_sme_pseudo_regs.ctrl_reg |= 2;

  return error;
}

Status NativeRegisterContextLinux_arm64::ReadMTEControl() {
  Status error;

  if (m_mte_ctrl_is_valid)
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetMTEControl();
  ioVec.iov_len = GetMTEControlSize();

  error = ReadRegisterSet(&ioVec, GetMTEControlSize(), NT_ARM_TAGGED_ADDR_CTRL);

  if (error.Success())
    m_mte_ctrl_is_valid = true;

  return error;
}

Status NativeRegisterContextLinux_arm64::WriteMTEControl() {
  Status error;

  error = ReadMTEControl();
  if (error.Fail())
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetMTEControl();
  ioVec.iov_len = GetMTEControlSize();

  m_mte_ctrl_is_valid = false;

  return WriteRegisterSet(&ioVec, GetMTEControlSize(), NT_ARM_TAGGED_ADDR_CTRL);
}

Status NativeRegisterContextLinux_arm64::ReadTLS() {
  Status error;

  if (m_tls_is_valid)
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetTLSBuffer();
  ioVec.iov_len = GetTLSBufferSize();

  error = ReadRegisterSet(&ioVec, GetTLSBufferSize(), NT_ARM_TLS);

  if (error.Success())
    m_tls_is_valid = true;

  return error;
}

Status NativeRegisterContextLinux_arm64::WriteTLS() {
  Status error;

  error = ReadTLS();
  if (error.Fail())
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetTLSBuffer();
  ioVec.iov_len = GetTLSBufferSize();

  m_tls_is_valid = false;

  return WriteRegisterSet(&ioVec, GetTLSBufferSize(), NT_ARM_TLS);
}

Status NativeRegisterContextLinux_arm64::ReadZAHeader() {
  Status error;

  if (m_za_header_is_valid)
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetZAHeader();
  ioVec.iov_len = GetZAHeaderSize();

  error = ReadRegisterSet(&ioVec, GetZAHeaderSize(), NT_ARM_ZA);

  if (error.Success())
    m_za_header_is_valid = true;

  return error;
}

Status NativeRegisterContextLinux_arm64::ReadZA() {
  Status error;

  if (m_za_buffer_is_valid)
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetZABuffer();
  ioVec.iov_len = GetZABufferSize();

  error = ReadRegisterSet(&ioVec, GetZABufferSize(), NT_ARM_ZA);

  if (error.Success())
    m_za_buffer_is_valid = true;

  return error;
}

Status NativeRegisterContextLinux_arm64::WriteZA() {
  // Note that because the ZA ptrace payload contains the header also, this
  // method will write both. This is done because writing only the header
  // will disable ZA, even if .size in the header is correct for an enabled ZA.
  Status error;

  error = ReadZA();
  if (error.Fail())
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetZABuffer();
  ioVec.iov_len = GetZABufferSize();

  m_za_buffer_is_valid = false;
  m_za_header_is_valid = false;
  // Writing to ZA may enable ZA, which means ZT0 may change too.
  m_zt_buffer_is_valid = false;

  return WriteRegisterSet(&ioVec, GetZABufferSize(), NT_ARM_ZA);
}

Status NativeRegisterContextLinux_arm64::ReadZT() {
  Status error;

  if (m_zt_buffer_is_valid)
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetZTBuffer();
  ioVec.iov_len = GetZTBufferSize();

  error = ReadRegisterSet(&ioVec, GetZTBufferSize(), NT_ARM_ZT);
  m_zt_buffer_is_valid = error.Success();

  return error;
}

Status NativeRegisterContextLinux_arm64::WriteZT() {
  Status error;

  error = ReadZT();
  if (error.Fail())
    return error;

  struct iovec ioVec;
  ioVec.iov_base = GetZTBuffer();
  ioVec.iov_len = GetZTBufferSize();

  m_zt_buffer_is_valid = false;
  // Writing to an inactive ZT0 will enable ZA as well, which invalidates our
  // current copy of it.
  m_za_buffer_is_valid = false;
  m_za_header_is_valid = false;

  return WriteRegisterSet(&ioVec, GetZTBufferSize(), NT_ARM_ZT);
}

void NativeRegisterContextLinux_arm64::ConfigureRegisterContext() {
  // ConfigureRegisterContext gets called from InvalidateAllRegisters
  // on every stop and configures SVE vector length and whether we are in
  // streaming SVE mode.
  // If m_sve_state is set to SVEState::Disabled on first stop, code below will
  // be deemed non operational for the lifetime of current process.
  if (!m_sve_header_is_valid && m_sve_state != SVEState::Disabled) {
    // If we have SVE we may also have the SVE streaming mode that SME added.
    // We can read the header of either mode, but only the active mode will
    // have valid register data.

    // Check whether SME is present and the streaming SVE mode is active.
    m_sve_header_is_valid = false;
    m_sve_buffer_is_valid = false;
    m_sve_state = SVEState::Streaming;
    Status error = ReadSVEHeader();

    // Streaming mode is active if the header has the SVE active flag set.
    if (!(error.Success() && ((m_sve_header.flags & sve::ptrace_regs_mask) ==
                              sve::ptrace_regs_sve))) {
      // Non-streaming might be active instead.
      m_sve_header_is_valid = false;
      m_sve_buffer_is_valid = false;
      m_sve_state = SVEState::Full;
      error = ReadSVEHeader();
      if (error.Success()) {
        // If SVE is enabled thread can switch between SVEState::FPSIMD and
        // SVEState::Full on every stop.
        if ((m_sve_header.flags & sve::ptrace_regs_mask) ==
            sve::ptrace_regs_fpsimd)
          m_sve_state = SVEState::FPSIMD;
        // Else we are in SVEState::Full.
      } else {
        m_sve_state = SVEState::Disabled;
      }
    }

    if (m_sve_state == SVEState::Full || m_sve_state == SVEState::FPSIMD ||
        m_sve_state == SVEState::Streaming) {
      // On every stop we configure SVE vector length by calling
      // ConfigureVectorLengthSVE regardless of current SVEState of this thread.
      uint32_t vq = RegisterInfoPOSIX_arm64::eVectorQuadwordAArch64SVE;
      if (sve::vl_valid(m_sve_header.vl))
        vq = sve::vq_from_vl(m_sve_header.vl);

      GetRegisterInfo().ConfigureVectorLengthSVE(vq);
      m_sve_ptrace_payload.resize(sve::PTraceSize(vq, sve::ptrace_regs_sve));
    }
  }

  if (!m_za_header_is_valid) {
    Status error = ReadZAHeader();
    if (error.Success()) {
      uint32_t vq = RegisterInfoPOSIX_arm64::eVectorQuadwordAArch64SVE;
      if (sve::vl_valid(m_za_header.vl))
        vq = sve::vq_from_vl(m_za_header.vl);

      GetRegisterInfo().ConfigureVectorLengthZA(vq);
      m_za_ptrace_payload.resize(m_za_header.size);
      m_za_buffer_is_valid = false;
    }
  }
}

uint32_t NativeRegisterContextLinux_arm64::CalculateFprOffset(
    const RegisterInfo *reg_info) const {
  return reg_info->byte_offset - GetGPRSize();
}

uint32_t NativeRegisterContextLinux_arm64::CalculateSVEOffset(
    const RegisterInfo *reg_info) const {
  // Start of Z0 data is after GPRs plus 8 bytes of vg register
  uint32_t sve_reg_offset = LLDB_INVALID_INDEX32;
  if (m_sve_state == SVEState::FPSIMD) {
    const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
    sve_reg_offset = sve::ptrace_fpsimd_offset +
                     (reg - GetRegisterInfo().GetRegNumSVEZ0()) * 16;
    // Between non-streaming and streaming mode, the layout is identical.
  } else if (m_sve_state == SVEState::Full ||
             m_sve_state == SVEState::Streaming) {
    uint32_t sve_z0_offset = GetGPRSize() + 16;
    sve_reg_offset =
        sve::SigRegsOffset() + reg_info->byte_offset - sve_z0_offset;
  }
  return sve_reg_offset;
}

Status NativeRegisterContextLinux_arm64::ReadSMESVG() {
  // This register is the streaming vector length, so we will get it from
  // NT_ARM_ZA regardless of the current streaming mode.
  Status error = ReadZAHeader();
  if (error.Success())
    m_sme_pseudo_regs.svg_reg = m_za_header.vl / 8;

  return error;
}

std::vector<uint32_t> NativeRegisterContextLinux_arm64::GetExpeditedRegisters(
    ExpeditedRegs expType) const {
  std::vector<uint32_t> expedited_reg_nums =
      NativeRegisterContext::GetExpeditedRegisters(expType);
  // SVE, non-streaming vector length.
  if (m_sve_state == SVEState::FPSIMD || m_sve_state == SVEState::Full)
    expedited_reg_nums.push_back(GetRegisterInfo().GetRegNumSVEVG());
  // SME, streaming vector length. This is used by the ZA register which is
  // present even when streaming mode is not enabled.
  if (GetRegisterInfo().IsSSVEPresent())
    expedited_reg_nums.push_back(GetRegisterInfo().GetRegNumSMESVG());

  return expedited_reg_nums;
}

llvm::Expected<NativeRegisterContextLinux::MemoryTaggingDetails>
NativeRegisterContextLinux_arm64::GetMemoryTaggingDetails(int32_t type) {
  if (type == MemoryTagManagerAArch64MTE::eMTE_allocation) {
    return MemoryTaggingDetails{std::make_unique<MemoryTagManagerAArch64MTE>(),
                                PTRACE_PEEKMTETAGS, PTRACE_POKEMTETAGS};
  }

  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "Unknown AArch64 memory tag type %d", type);
}

lldb::addr_t NativeRegisterContextLinux_arm64::FixWatchpointHitAddress(
    lldb::addr_t hit_addr) {
  // Linux configures user-space virtual addresses with top byte ignored.
  // We set default value of mask such that top byte is masked out.
  lldb::addr_t mask = ~((1ULL << 56) - 1);

  // Try to read pointer authentication data_mask register and calculate a
  // consolidated data address mask after ignoring the top byte.
  if (ReadPAuthMask().Success())
    mask |= m_pac_mask.data_mask;

  return hit_addr & ~mask;
  ;
}

#endif // defined (__arm64__) || defined (__aarch64__)

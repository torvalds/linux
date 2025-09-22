//===-- RegisterInfoPOSIX_arm64.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include <cassert>
#include <cstddef>
#include <vector>

#include "lldb/lldb-defines.h"
#include "llvm/Support/Compiler.h"

#include "RegisterInfoPOSIX_arm64.h"

// Based on RegisterContextDarwin_arm64.cpp
#define GPR_OFFSET(idx) ((idx)*8)
#define GPR_OFFSET_NAME(reg)                                                   \
  (LLVM_EXTENSION offsetof(RegisterInfoPOSIX_arm64::GPR, reg))

#define FPU_OFFSET(idx) ((idx)*16 + sizeof(RegisterInfoPOSIX_arm64::GPR))
#define FPU_OFFSET_NAME(reg)                                                   \
  (LLVM_EXTENSION offsetof(RegisterInfoPOSIX_arm64::FPU, reg) +                \
   sizeof(RegisterInfoPOSIX_arm64::GPR))

// This information is based on AArch64 with SVE architecture reference manual.
// AArch64 with SVE has 32 Z and 16 P vector registers. There is also an FFR
// (First Fault) register and a VG (Vector Granule) pseudo register.

// SVE 16-byte quad word is the basic unit of expansion in vector length.
#define SVE_QUAD_WORD_BYTES 16

// Vector length is the multiplier which decides the no of quad words,
// (multiples of 128-bits or 16-bytes) present in a Z register. Vector length
// is decided during execution and can change at runtime. SVE AArch64 register
// infos have modes one for each valid value of vector length. A change in
// vector length requires register context to update sizes of SVE Z, P and FFR.
// Also register context needs to update byte offsets of all registers affected
// by the change in vector length.
#define SVE_REGS_DEFAULT_OFFSET_LINUX sizeof(RegisterInfoPOSIX_arm64::GPR)

#define SVE_OFFSET_VG SVE_REGS_DEFAULT_OFFSET_LINUX

#define EXC_OFFSET_NAME(reg)                                                   \
  (LLVM_EXTENSION offsetof(RegisterInfoPOSIX_arm64::EXC, reg) +                \
   sizeof(RegisterInfoPOSIX_arm64::GPR) +                                      \
   sizeof(RegisterInfoPOSIX_arm64::FPU))
#define DBG_OFFSET_NAME(reg)                                                   \
  (LLVM_EXTENSION offsetof(RegisterInfoPOSIX_arm64::DBG, reg) +                \
   sizeof(RegisterInfoPOSIX_arm64::GPR) +                                      \
   sizeof(RegisterInfoPOSIX_arm64::FPU) +                                      \
   sizeof(RegisterInfoPOSIX_arm64::EXC))

#define DEFINE_DBG(reg, i)                                                     \
  #reg, NULL,                                                                  \
      sizeof(((RegisterInfoPOSIX_arm64::DBG *) NULL)->reg[i]),                 \
              DBG_OFFSET_NAME(reg[i]), lldb::eEncodingUint, lldb::eFormatHex,  \
                              {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,       \
                               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,       \
                               dbg_##reg##i },                                 \
                               NULL, NULL, NULL,
#define REG_CONTEXT_SIZE                                                       \
  (sizeof(RegisterInfoPOSIX_arm64::GPR) +                                      \
   sizeof(RegisterInfoPOSIX_arm64::FPU) +                                      \
   sizeof(RegisterInfoPOSIX_arm64::EXC))

// Include RegisterInfos_arm64 to declare our g_register_infos_arm64 structure.
#define DECLARE_REGISTER_INFOS_ARM64_STRUCT
#include "RegisterInfos_arm64.h"
#include "RegisterInfos_arm64_sve.h"
#undef DECLARE_REGISTER_INFOS_ARM64_STRUCT

static lldb_private::RegisterInfo g_register_infos_pauth[] = {
    DEFINE_EXTENSION_REG(data_mask), DEFINE_EXTENSION_REG(code_mask)};

static lldb_private::RegisterInfo g_register_infos_mte[] = {
    DEFINE_EXTENSION_REG(mte_ctrl)};

static lldb_private::RegisterInfo g_register_infos_tls[] = {
    DEFINE_EXTENSION_REG(tpidr),
    // Only present when SME is present
    DEFINE_EXTENSION_REG(tpidr2)};

static lldb_private::RegisterInfo g_register_infos_sme[] = {
    DEFINE_EXTENSION_REG(svcr),
    DEFINE_EXTENSION_REG(svg),
    // 16 is a default size we will change later.
    {"za", nullptr, 16, 0, lldb::eEncodingVector, lldb::eFormatVectorOfUInt8,
     KIND_ALL_INVALID, nullptr, nullptr, nullptr}};

static lldb_private::RegisterInfo g_register_infos_sme2[] = {
    {"zt0", nullptr, 64, 0, lldb::eEncodingVector, lldb::eFormatVectorOfUInt8,
     KIND_ALL_INVALID, nullptr, nullptr, nullptr}};

// Number of register sets provided by this context.
enum {
  k_num_gpr_registers = gpr_w28 - gpr_x0 + 1,
  k_num_fpr_registers = fpu_fpcr - fpu_v0 + 1,
  k_num_sve_registers = sve_ffr - sve_vg + 1,
  k_num_mte_register = 1,
  // Number of TLS registers is dynamic so it is not listed here.
  k_num_pauth_register = 2,
  // SME2's ZT0 will also be added to this set if present. So this number is
  // only for SME1 registers.
  k_num_sme_register = 3,
  k_num_register_sets_default = 2,
  k_num_register_sets = 3
};

// ARM64 general purpose registers.
static const uint32_t g_gpr_regnums_arm64[] = {
    gpr_x0,  gpr_x1,   gpr_x2,  gpr_x3,
    gpr_x4,  gpr_x5,   gpr_x6,  gpr_x7,
    gpr_x8,  gpr_x9,   gpr_x10, gpr_x11,
    gpr_x12, gpr_x13,  gpr_x14, gpr_x15,
    gpr_x16, gpr_x17,  gpr_x18, gpr_x19,
    gpr_x20, gpr_x21,  gpr_x22, gpr_x23,
    gpr_x24, gpr_x25,  gpr_x26, gpr_x27,
    gpr_x28, gpr_fp,   gpr_lr,  gpr_sp,
    gpr_pc,  gpr_cpsr, gpr_w0,  gpr_w1,
    gpr_w2,  gpr_w3,   gpr_w4,  gpr_w5,
    gpr_w6,  gpr_w7,   gpr_w8,  gpr_w9,
    gpr_w10, gpr_w11,  gpr_w12, gpr_w13,
    gpr_w14, gpr_w15,  gpr_w16, gpr_w17,
    gpr_w18, gpr_w19,  gpr_w20, gpr_w21,
    gpr_w22, gpr_w23,  gpr_w24, gpr_w25,
    gpr_w26, gpr_w27,  gpr_w28, LLDB_INVALID_REGNUM};

static_assert(((sizeof g_gpr_regnums_arm64 / sizeof g_gpr_regnums_arm64[0]) -
               1) == k_num_gpr_registers,
              "g_gpr_regnums_arm64 has wrong number of register infos");

// ARM64 floating point registers.
static const uint32_t g_fpu_regnums_arm64[] = {
    fpu_v0,   fpu_v1,   fpu_v2,
    fpu_v3,   fpu_v4,   fpu_v5,
    fpu_v6,   fpu_v7,   fpu_v8,
    fpu_v9,   fpu_v10,  fpu_v11,
    fpu_v12,  fpu_v13,  fpu_v14,
    fpu_v15,  fpu_v16,  fpu_v17,
    fpu_v18,  fpu_v19,  fpu_v20,
    fpu_v21,  fpu_v22,  fpu_v23,
    fpu_v24,  fpu_v25,  fpu_v26,
    fpu_v27,  fpu_v28,  fpu_v29,
    fpu_v30,  fpu_v31,  fpu_s0,
    fpu_s1,   fpu_s2,   fpu_s3,
    fpu_s4,   fpu_s5,   fpu_s6,
    fpu_s7,   fpu_s8,   fpu_s9,
    fpu_s10,  fpu_s11,  fpu_s12,
    fpu_s13,  fpu_s14,  fpu_s15,
    fpu_s16,  fpu_s17,  fpu_s18,
    fpu_s19,  fpu_s20,  fpu_s21,
    fpu_s22,  fpu_s23,  fpu_s24,
    fpu_s25,  fpu_s26,  fpu_s27,
    fpu_s28,  fpu_s29,  fpu_s30,
    fpu_s31,  fpu_d0,   fpu_d1,
    fpu_d2,   fpu_d3,   fpu_d4,
    fpu_d5,   fpu_d6,   fpu_d7,
    fpu_d8,   fpu_d9,   fpu_d10,
    fpu_d11,  fpu_d12,  fpu_d13,
    fpu_d14,  fpu_d15,  fpu_d16,
    fpu_d17,  fpu_d18,  fpu_d19,
    fpu_d20,  fpu_d21,  fpu_d22,
    fpu_d23,  fpu_d24,  fpu_d25,
    fpu_d26,  fpu_d27,  fpu_d28,
    fpu_d29,  fpu_d30,  fpu_d31,
    fpu_fpsr, fpu_fpcr, LLDB_INVALID_REGNUM};
static_assert(((sizeof g_fpu_regnums_arm64 / sizeof g_fpu_regnums_arm64[0]) -
               1) == k_num_fpr_registers,
              "g_fpu_regnums_arm64 has wrong number of register infos");

// ARM64 SVE registers.
static const uint32_t g_sve_regnums_arm64[] = {
    sve_vg,  sve_z0,  sve_z1,
    sve_z2,  sve_z3,  sve_z4,
    sve_z5,  sve_z6,  sve_z7,
    sve_z8,  sve_z9,  sve_z10,
    sve_z11, sve_z12, sve_z13,
    sve_z14, sve_z15, sve_z16,
    sve_z17, sve_z18, sve_z19,
    sve_z20, sve_z21, sve_z22,
    sve_z23, sve_z24, sve_z25,
    sve_z26, sve_z27, sve_z28,
    sve_z29, sve_z30, sve_z31,
    sve_p0,  sve_p1,  sve_p2,
    sve_p3,  sve_p4,  sve_p5,
    sve_p6,  sve_p7,  sve_p8,
    sve_p9,  sve_p10, sve_p11,
    sve_p12, sve_p13, sve_p14,
    sve_p15, sve_ffr, LLDB_INVALID_REGNUM};
static_assert(((sizeof g_sve_regnums_arm64 / sizeof g_sve_regnums_arm64[0]) -
               1) == k_num_sve_registers,
              "g_sve_regnums_arm64 has wrong number of register infos");

// Register sets for ARM64.
static const lldb_private::RegisterSet g_reg_sets_arm64[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers,
     g_gpr_regnums_arm64},
    {"Floating Point Registers", "fpu", k_num_fpr_registers,
     g_fpu_regnums_arm64},
    {"Scalable Vector Extension Registers", "sve", k_num_sve_registers,
     g_sve_regnums_arm64}};

static const lldb_private::RegisterSet g_reg_set_pauth_arm64 = {
    "Pointer Authentication Registers", "pauth", k_num_pauth_register, nullptr};

static const lldb_private::RegisterSet g_reg_set_mte_arm64 = {
    "MTE Control Register", "mte", k_num_mte_register, nullptr};

// The size of the TLS set is dynamic, so not listed here.

static const lldb_private::RegisterSet g_reg_set_sme_arm64 = {
    "Scalable Matrix Extension Registers", "sme", k_num_sme_register, nullptr};

RegisterInfoPOSIX_arm64::RegisterInfoPOSIX_arm64(
    const lldb_private::ArchSpec &target_arch, lldb_private::Flags opt_regsets)
    : lldb_private::RegisterInfoAndSetInterface(target_arch),
      m_opt_regsets(opt_regsets) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32: {
    m_register_set_p = g_reg_sets_arm64;
    m_register_set_count = k_num_register_sets_default;
    m_per_regset_regnum_range[GPRegSet] = std::make_pair(gpr_x0, gpr_w28 + 1);
    m_per_regset_regnum_range[FPRegSet] = std::make_pair(fpu_v0, fpu_fpcr + 1);

    // Now configure register sets supported by current target. If we have a
    // dynamic register set like MTE, Pointer Authentication regset then we need
    // to create dynamic register infos and regset array. Push back all optional
    // register infos and regset and calculate register offsets accordingly.
    if (m_opt_regsets.AnySet(eRegsetMaskSVE | eRegsetMaskSSVE)) {
      m_register_info_p = g_register_infos_arm64_sve_le;
      m_register_info_count = sve_ffr + 1;
      m_per_regset_regnum_range[m_register_set_count++] =
          std::make_pair(sve_vg, sve_ffr + 1);
    } else {
      m_register_info_p = g_register_infos_arm64_le;
      m_register_info_count = fpu_fpcr + 1;
    }

    if (m_opt_regsets.AnySet(eRegsetMaskDynamic)) {
      llvm::ArrayRef<lldb_private::RegisterInfo> reg_infos_ref =
          llvm::ArrayRef(m_register_info_p, m_register_info_count);
      llvm::ArrayRef<lldb_private::RegisterSet> reg_sets_ref =
          llvm::ArrayRef(m_register_set_p, m_register_set_count);
      llvm::copy(reg_infos_ref, std::back_inserter(m_dynamic_reg_infos));
      llvm::copy(reg_sets_ref, std::back_inserter(m_dynamic_reg_sets));

      if (m_opt_regsets.AllSet(eRegsetMaskPAuth))
        AddRegSetPAuth();

      if (m_opt_regsets.AllSet(eRegsetMaskMTE))
        AddRegSetMTE();

      // The TLS set always contains tpidr but only has tpidr2 when SME is
      // present.
      AddRegSetTLS(m_opt_regsets.AllSet(eRegsetMaskSSVE));

      if (m_opt_regsets.AnySet(eRegsetMaskSSVE))
        AddRegSetSME(m_opt_regsets.AnySet(eRegsetMaskZT));

      m_register_info_count = m_dynamic_reg_infos.size();
      m_register_info_p = m_dynamic_reg_infos.data();
      m_register_set_p = m_dynamic_reg_sets.data();
      m_register_set_count = m_dynamic_reg_sets.size();
    }
    break;
  }
  default:
    assert(false && "Unhandled target architecture.");
  }
}

uint32_t RegisterInfoPOSIX_arm64::GetRegisterCount() const {
  return m_register_info_count;
}

size_t RegisterInfoPOSIX_arm64::GetGPRSizeStatic() {
  return sizeof(struct RegisterInfoPOSIX_arm64::GPR);
}

size_t RegisterInfoPOSIX_arm64::GetFPRSize() const {
  return sizeof(struct RegisterInfoPOSIX_arm64::FPU);
}

const lldb_private::RegisterInfo *
RegisterInfoPOSIX_arm64::GetRegisterInfo() const {
  return m_register_info_p;
}

size_t RegisterInfoPOSIX_arm64::GetRegisterSetCount() const {
  return m_register_set_count;
}

size_t RegisterInfoPOSIX_arm64::GetRegisterSetFromRegisterIndex(
    uint32_t reg_index) const {
  for (const auto &regset_range : m_per_regset_regnum_range) {
    if (reg_index >= regset_range.second.first &&
        reg_index < regset_range.second.second)
      return regset_range.first;
  }
  return LLDB_INVALID_REGNUM;
}

const lldb_private::RegisterSet *
RegisterInfoPOSIX_arm64::GetRegisterSet(size_t set_index) const {
  if (set_index < GetRegisterSetCount())
    return &m_register_set_p[set_index];
  return nullptr;
}

void RegisterInfoPOSIX_arm64::AddRegSetPAuth() {
  uint32_t pa_regnum = m_dynamic_reg_infos.size();
  for (uint32_t i = 0; i < k_num_pauth_register; i++) {
    pauth_regnum_collection.push_back(pa_regnum + i);
    m_dynamic_reg_infos.push_back(g_register_infos_pauth[i]);
    m_dynamic_reg_infos[pa_regnum + i].byte_offset =
        m_dynamic_reg_infos[pa_regnum + i - 1].byte_offset +
        m_dynamic_reg_infos[pa_regnum + i - 1].byte_size;
    m_dynamic_reg_infos[pa_regnum + i].kinds[lldb::eRegisterKindLLDB] =
        pa_regnum + i;
  }

  m_per_regset_regnum_range[m_register_set_count] =
      std::make_pair(pa_regnum, m_dynamic_reg_infos.size());
  m_dynamic_reg_sets.push_back(g_reg_set_pauth_arm64);
  m_dynamic_reg_sets.back().registers = pauth_regnum_collection.data();
}

void RegisterInfoPOSIX_arm64::AddRegSetMTE() {
  uint32_t mte_regnum = m_dynamic_reg_infos.size();
  m_mte_regnum_collection.push_back(mte_regnum);
  m_dynamic_reg_infos.push_back(g_register_infos_mte[0]);
  m_dynamic_reg_infos[mte_regnum].byte_offset =
      m_dynamic_reg_infos[mte_regnum - 1].byte_offset +
      m_dynamic_reg_infos[mte_regnum - 1].byte_size;
  m_dynamic_reg_infos[mte_regnum].kinds[lldb::eRegisterKindLLDB] = mte_regnum;

  m_per_regset_regnum_range[m_register_set_count] =
      std::make_pair(mte_regnum, mte_regnum + 1);
  m_dynamic_reg_sets.push_back(g_reg_set_mte_arm64);
  m_dynamic_reg_sets.back().registers = m_mte_regnum_collection.data();
}

void RegisterInfoPOSIX_arm64::AddRegSetTLS(bool has_tpidr2) {
  uint32_t tls_regnum = m_dynamic_reg_infos.size();
  uint32_t num_regs = has_tpidr2 ? 2 : 1;
  for (uint32_t i = 0; i < num_regs; i++) {
    m_tls_regnum_collection.push_back(tls_regnum + i);
    m_dynamic_reg_infos.push_back(g_register_infos_tls[i]);
    m_dynamic_reg_infos[tls_regnum + i].byte_offset =
        m_dynamic_reg_infos[tls_regnum + i - 1].byte_offset +
        m_dynamic_reg_infos[tls_regnum + i - 1].byte_size;
    m_dynamic_reg_infos[tls_regnum + i].kinds[lldb::eRegisterKindLLDB] =
        tls_regnum + i;
  }

  m_per_regset_regnum_range[m_register_set_count] =
      std::make_pair(tls_regnum, m_dynamic_reg_infos.size());
  m_dynamic_reg_sets.push_back(
      {"Thread Local Storage Registers", "tls", num_regs, nullptr});
  m_dynamic_reg_sets.back().registers = m_tls_regnum_collection.data();
}

void RegisterInfoPOSIX_arm64::AddRegSetSME(bool has_zt) {
  const uint32_t first_sme_regnum = m_dynamic_reg_infos.size();
  uint32_t sme_regnum = first_sme_regnum;

  for (uint32_t i = 0; i < k_num_sme_register; ++i, ++sme_regnum) {
    m_sme_regnum_collection.push_back(sme_regnum);
    m_dynamic_reg_infos.push_back(g_register_infos_sme[i]);
    m_dynamic_reg_infos[sme_regnum].byte_offset =
        m_dynamic_reg_infos[sme_regnum - 1].byte_offset +
        m_dynamic_reg_infos[sme_regnum - 1].byte_size;
    m_dynamic_reg_infos[sme_regnum].kinds[lldb::eRegisterKindLLDB] = sme_regnum;
  }

  lldb_private::RegisterSet sme_regset = g_reg_set_sme_arm64;

  if (has_zt) {
    m_sme_regnum_collection.push_back(sme_regnum);
    m_dynamic_reg_infos.push_back(g_register_infos_sme2[0]);
    m_dynamic_reg_infos[sme_regnum].byte_offset =
        m_dynamic_reg_infos[sme_regnum - 1].byte_offset +
        m_dynamic_reg_infos[sme_regnum - 1].byte_size;
    m_dynamic_reg_infos[sme_regnum].kinds[lldb::eRegisterKindLLDB] = sme_regnum;

    sme_regset.num_registers += 1;
  }

  m_per_regset_regnum_range[m_register_set_count] =
      std::make_pair(first_sme_regnum, m_dynamic_reg_infos.size());
  m_dynamic_reg_sets.push_back(sme_regset);
  m_dynamic_reg_sets.back().registers = m_sme_regnum_collection.data();

  // When vg is written during streaming mode, svg will also change, as vg and
  // svg in this state are both showing the streaming vector length.
  // We model this as vg invalidating svg. In non-streaming mode this doesn't
  // happen but to keep things simple we will invalidate svg anyway.
  //
  // This must be added now, rather than when vg is defined because SME is a
  // dynamic set that may or may not be present.
  static uint32_t vg_invalidates[] = {sme_regnum + 1 /*svg*/,
                                      LLDB_INVALID_REGNUM};
  m_dynamic_reg_infos[GetRegNumSVEVG()].invalidate_regs = vg_invalidates;
}

uint32_t RegisterInfoPOSIX_arm64::ConfigureVectorLengthSVE(uint32_t sve_vq) {
  // sve_vq contains SVE Quad vector length in context of AArch64 SVE.
  // SVE register infos if enabled cannot be disabled by selecting sve_vq = 0.
  // Also if an invalid or previously set vector length is passed to this
  // function then it will exit immediately with previously set vector length.
  if (!VectorSizeIsValid(sve_vq) || m_vector_reg_vq == sve_vq)
    return m_vector_reg_vq;

  // We cannot enable AArch64 only mode if SVE was enabled.
  if (sve_vq == eVectorQuadwordAArch64 &&
      m_vector_reg_vq > eVectorQuadwordAArch64)
    sve_vq = eVectorQuadwordAArch64SVE;

  m_vector_reg_vq = sve_vq;

  if (sve_vq == eVectorQuadwordAArch64)
    return m_vector_reg_vq;
  std::vector<lldb_private::RegisterInfo> &reg_info_ref =
      m_per_vq_reg_infos[sve_vq];

  if (reg_info_ref.empty()) {
    reg_info_ref = llvm::ArrayRef(m_register_info_p, m_register_info_count);

    uint32_t offset = SVE_REGS_DEFAULT_OFFSET_LINUX;
    reg_info_ref[fpu_fpsr].byte_offset = offset;
    reg_info_ref[fpu_fpcr].byte_offset = offset + 4;
    reg_info_ref[sve_vg].byte_offset = offset + 8;
    offset += 16;

    // Update Z registers size and offset
    uint32_t s_reg_base = fpu_s0;
    uint32_t d_reg_base = fpu_d0;
    uint32_t v_reg_base = fpu_v0;
    uint32_t z_reg_base = sve_z0;

    for (uint32_t index = 0; index < 32; index++) {
      reg_info_ref[s_reg_base + index].byte_offset = offset;
      reg_info_ref[d_reg_base + index].byte_offset = offset;
      reg_info_ref[v_reg_base + index].byte_offset = offset;
      reg_info_ref[z_reg_base + index].byte_offset = offset;

      reg_info_ref[z_reg_base + index].byte_size = sve_vq * SVE_QUAD_WORD_BYTES;
      offset += reg_info_ref[z_reg_base + index].byte_size;
    }

    // Update P registers and FFR size and offset
    for (uint32_t it = sve_p0; it <= sve_ffr; it++) {
      reg_info_ref[it].byte_offset = offset;
      reg_info_ref[it].byte_size = sve_vq * SVE_QUAD_WORD_BYTES / 8;
      offset += reg_info_ref[it].byte_size;
    }

    for (uint32_t it = sve_ffr + 1; it < m_register_info_count; it++) {
      reg_info_ref[it].byte_offset = offset;
      offset += reg_info_ref[it].byte_size;
    }

    m_per_vq_reg_infos[sve_vq] = reg_info_ref;
  }

  m_register_info_p = m_per_vq_reg_infos[sve_vq].data();
  return m_vector_reg_vq;
}

void RegisterInfoPOSIX_arm64::ConfigureVectorLengthZA(uint32_t za_vq) {
  if (!VectorSizeIsValid(za_vq) || m_za_reg_vq == za_vq)
    return;

  m_za_reg_vq = za_vq;

  // For SVE changes, we replace m_register_info_p completely. ZA is in a
  // dynamic set and is just 1 register so we make an exception to const here.
  lldb_private::RegisterInfo *non_const_reginfo =
      const_cast<lldb_private::RegisterInfo *>(m_register_info_p);
  non_const_reginfo[m_sme_regnum_collection[2]].byte_size =
      (za_vq * 16) * (za_vq * 16);
}

bool RegisterInfoPOSIX_arm64::IsSVEReg(unsigned reg) const {
  if (m_vector_reg_vq > eVectorQuadwordAArch64)
    return (sve_vg <= reg && reg <= sve_ffr);
  else
    return false;
}

bool RegisterInfoPOSIX_arm64::IsSVEZReg(unsigned reg) const {
  return (sve_z0 <= reg && reg <= sve_z31);
}

bool RegisterInfoPOSIX_arm64::IsSVEPReg(unsigned reg) const {
  return (sve_p0 <= reg && reg <= sve_p15);
}

bool RegisterInfoPOSIX_arm64::IsSVERegVG(unsigned reg) const {
  return sve_vg == reg;
}

bool RegisterInfoPOSIX_arm64::IsSMERegZA(unsigned reg) const {
  return reg == m_sme_regnum_collection[2];
}

bool RegisterInfoPOSIX_arm64::IsSMERegZT(unsigned reg) const {
  // ZT0 is part of the SME register set only if SME2 is present.
  return m_sme_regnum_collection.size() >= 4 &&
         reg == m_sme_regnum_collection[3];
}

bool RegisterInfoPOSIX_arm64::IsPAuthReg(unsigned reg) const {
  return llvm::is_contained(pauth_regnum_collection, reg);
}

bool RegisterInfoPOSIX_arm64::IsMTEReg(unsigned reg) const {
  return llvm::is_contained(m_mte_regnum_collection, reg);
}

bool RegisterInfoPOSIX_arm64::IsTLSReg(unsigned reg) const {
  return llvm::is_contained(m_tls_regnum_collection, reg);
}

bool RegisterInfoPOSIX_arm64::IsSMEReg(unsigned reg) const {
  return llvm::is_contained(m_sme_regnum_collection, reg);
}

uint32_t RegisterInfoPOSIX_arm64::GetRegNumSVEZ0() const { return sve_z0; }

uint32_t RegisterInfoPOSIX_arm64::GetRegNumSVEFFR() const { return sve_ffr; }

uint32_t RegisterInfoPOSIX_arm64::GetRegNumFPCR() const { return fpu_fpcr; }

uint32_t RegisterInfoPOSIX_arm64::GetRegNumFPSR() const { return fpu_fpsr; }

uint32_t RegisterInfoPOSIX_arm64::GetRegNumSVEVG() const { return sve_vg; }

uint32_t RegisterInfoPOSIX_arm64::GetRegNumSMESVG() const {
  return m_sme_regnum_collection[1];
}

uint32_t RegisterInfoPOSIX_arm64::GetPAuthOffset() const {
  return m_register_info_p[pauth_regnum_collection[0]].byte_offset;
}

uint32_t RegisterInfoPOSIX_arm64::GetMTEOffset() const {
  return m_register_info_p[m_mte_regnum_collection[0]].byte_offset;
}

uint32_t RegisterInfoPOSIX_arm64::GetTLSOffset() const {
  return m_register_info_p[m_tls_regnum_collection[0]].byte_offset;
}

uint32_t RegisterInfoPOSIX_arm64::GetSMEOffset() const {
  return m_register_info_p[m_sme_regnum_collection[0]].byte_offset;
}

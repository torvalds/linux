//===-- RegisterContextPOSIX_x86.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTPOSIX_X86_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTPOSIX_X86_H

#include "RegisterContext_x86.h"
#include "RegisterInfoInterface.h"
#include "RegisterInfos_x86_64_with_base_shared.h"
#include "lldb-x86-register-enums.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/Log.h"

class RegisterContextPOSIX_x86 : public lldb_private::RegisterContext {
public:
  RegisterContextPOSIX_x86(lldb_private::Thread &thread,
                           uint32_t concrete_frame_idx,
                           lldb_private::RegisterInfoInterface *register_info);

  ~RegisterContextPOSIX_x86() override;

  void Invalidate();

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  virtual size_t GetGPRSize();

  virtual size_t GetFXSAVEOffset();

  virtual unsigned GetRegisterSize(unsigned reg);

  virtual unsigned GetRegisterOffset(unsigned reg);

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t set) override;

  const char *GetRegisterName(unsigned reg);

  // Note: prefer kernel definitions over user-land
  enum FPRType {
    eNotValid = 0,
    eFSAVE, // TODO
    eFXSAVE,
    eSOFT, // TODO
    eXSAVE
  };

  static uint32_t g_contained_eax[];
  static uint32_t g_contained_ebx[];
  static uint32_t g_contained_ecx[];
  static uint32_t g_contained_edx[];
  static uint32_t g_contained_edi[];
  static uint32_t g_contained_esi[];
  static uint32_t g_contained_ebp[];
  static uint32_t g_contained_esp[];

  static uint32_t g_invalidate_eax[];
  static uint32_t g_invalidate_ebx[];
  static uint32_t g_invalidate_ecx[];
  static uint32_t g_invalidate_edx[];
  static uint32_t g_invalidate_edi[];
  static uint32_t g_invalidate_esi[];
  static uint32_t g_invalidate_ebp[];
  static uint32_t g_invalidate_esp[];

  static uint32_t g_contained_rax[];
  static uint32_t g_contained_rbx[];
  static uint32_t g_contained_rcx[];
  static uint32_t g_contained_rdx[];
  static uint32_t g_contained_rdi[];
  static uint32_t g_contained_rsi[];
  static uint32_t g_contained_rbp[];
  static uint32_t g_contained_rsp[];
  static uint32_t g_contained_r8[];
  static uint32_t g_contained_r9[];
  static uint32_t g_contained_r10[];
  static uint32_t g_contained_r11[];
  static uint32_t g_contained_r12[];
  static uint32_t g_contained_r13[];
  static uint32_t g_contained_r14[];
  static uint32_t g_contained_r15[];

  static uint32_t g_invalidate_rax[];
  static uint32_t g_invalidate_rbx[];
  static uint32_t g_invalidate_rcx[];
  static uint32_t g_invalidate_rdx[];
  static uint32_t g_invalidate_rdi[];
  static uint32_t g_invalidate_rsi[];
  static uint32_t g_invalidate_rbp[];
  static uint32_t g_invalidate_rsp[];
  static uint32_t g_invalidate_r8[];
  static uint32_t g_invalidate_r9[];
  static uint32_t g_invalidate_r10[];
  static uint32_t g_invalidate_r11[];
  static uint32_t g_invalidate_r12[];
  static uint32_t g_invalidate_r13[];
  static uint32_t g_invalidate_r14[];
  static uint32_t g_invalidate_r15[];

  static uint32_t g_contained_fip[];
  static uint32_t g_contained_fdp[];

  static uint32_t g_invalidate_fip[];
  static uint32_t g_invalidate_fdp[];

  static uint32_t g_contained_st0_32[];
  static uint32_t g_contained_st1_32[];
  static uint32_t g_contained_st2_32[];
  static uint32_t g_contained_st3_32[];
  static uint32_t g_contained_st4_32[];
  static uint32_t g_contained_st5_32[];
  static uint32_t g_contained_st6_32[];
  static uint32_t g_contained_st7_32[];

  static uint32_t g_invalidate_st0_32[];
  static uint32_t g_invalidate_st1_32[];
  static uint32_t g_invalidate_st2_32[];
  static uint32_t g_invalidate_st3_32[];
  static uint32_t g_invalidate_st4_32[];
  static uint32_t g_invalidate_st5_32[];
  static uint32_t g_invalidate_st6_32[];
  static uint32_t g_invalidate_st7_32[];

  static uint32_t g_contained_st0_64[];
  static uint32_t g_contained_st1_64[];
  static uint32_t g_contained_st2_64[];
  static uint32_t g_contained_st3_64[];
  static uint32_t g_contained_st4_64[];
  static uint32_t g_contained_st5_64[];
  static uint32_t g_contained_st6_64[];
  static uint32_t g_contained_st7_64[];

  static uint32_t g_invalidate_st0_64[];
  static uint32_t g_invalidate_st1_64[];
  static uint32_t g_invalidate_st2_64[];
  static uint32_t g_invalidate_st3_64[];
  static uint32_t g_invalidate_st4_64[];
  static uint32_t g_invalidate_st5_64[];
  static uint32_t g_invalidate_st6_64[];
  static uint32_t g_invalidate_st7_64[];

protected:
  FPRType
      m_fpr_type; // determines the type of data stored by union FPR, if any.
  lldb_private::FPR m_fpr;     // floating-point registers including extended
                               // register sets.
  lldb_private::YMM m_ymm_set; // copy of ymmh and xmm register halves.
  std::unique_ptr<lldb_private::RegisterInfoInterface>
      m_register_info_up; // Register Info Interface (FreeBSD or Linux)

  // Determines if an extended register set is supported on the processor
  // running the inferior process.
  virtual bool IsRegisterSetAvailable(size_t set_index);

  virtual const lldb_private::RegisterInfo *GetRegisterInfo();

  bool IsGPR(unsigned reg);

  bool IsFPR(unsigned reg);

  bool IsAVX(unsigned reg);

  bool CopyXSTATEtoYMM(uint32_t reg, lldb::ByteOrder byte_order);
  bool CopyYMMtoXSTATE(uint32_t reg, lldb::ByteOrder byte_order);
  bool IsFPR(unsigned reg, FPRType fpr_type);
  FPRType GetFPRType();

  virtual bool ReadGPR() = 0;
  virtual bool ReadFPR() = 0;
  virtual bool WriteGPR() = 0;
  virtual bool WriteFPR() = 0;
  virtual lldb_private::RegInfo &GetRegInfo();
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTPOSIX_X86_H

//===-- RegisterInfos_x86_64_with_base_shared.h -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/Process/Utility/lldb-x86-register-enums.h"
#include <stdint.h>

#ifndef lldb_RegisterInfos_x86_64_with_base_shared_h
#define lldb_RegisterInfos_x86_64_with_base_shared_h

#include "Plugins/Process/Utility/NativeRegisterContextRegisterInfo.h"

namespace lldb_private {

struct RegisterInfos_x86_64_with_base_shared {
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
};

struct RegInfo {
  uint32_t num_registers;
  uint32_t num_gpr_registers;
  uint32_t num_fpr_registers;
  uint32_t num_avx_registers;

  uint32_t last_gpr;
  uint32_t first_fpr;
  uint32_t last_fpr;

  uint32_t first_st;
  uint32_t last_st;
  uint32_t first_mm;
  uint32_t last_mm;
  uint32_t first_xmm;
  uint32_t last_xmm;
  uint32_t first_ymm;
  uint32_t last_ymm;

  uint32_t first_dr;
  uint32_t gpr_flags;
};

RegInfo &GetRegInfoShared(llvm::Triple::ArchType arch_type, bool with_base);

} // namespace lldb_private

#endif // ifndef lldb_RegisterInfos_x86_64_with_base_shared_h

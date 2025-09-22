//===-- RegisterContextLinux_i386.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTLINUX_X86_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTLINUX_X86_H

#include "RegisterInfoInterface.h"

namespace lldb_private {

class RegisterContextLinux_x86 : public RegisterInfoInterface {
public:
  RegisterContextLinux_x86(const ArchSpec &target_arch,
                           RegisterInfo orig_ax_info)
      : RegisterInfoInterface(target_arch), m_orig_ax_info(orig_ax_info) {}

  const RegisterInfo &GetOrigAxInfo() const { return m_orig_ax_info; }

private:
  lldb_private::RegisterInfo m_orig_ax_info;
};

} // namespace lldb_private

#endif

//===-- RegisterContextFreeBSD_powerpc.h -------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTFREEBSD_POWERPC_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTFREEBSD_POWERPC_H

#include "RegisterInfoInterface.h"

class RegisterContextFreeBSD_powerpc
    : public lldb_private::RegisterInfoInterface {
public:
  RegisterContextFreeBSD_powerpc(const lldb_private::ArchSpec &target_arch);
  ~RegisterContextFreeBSD_powerpc() override;

  size_t GetGPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;
};

class RegisterContextFreeBSD_powerpc32 : public RegisterContextFreeBSD_powerpc {
public:
  RegisterContextFreeBSD_powerpc32(const lldb_private::ArchSpec &target_arch);
  ~RegisterContextFreeBSD_powerpc32() override;

  size_t GetGPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;
};

class RegisterContextFreeBSD_powerpc64 : public RegisterContextFreeBSD_powerpc {
public:
  RegisterContextFreeBSD_powerpc64(const lldb_private::ArchSpec &target_arch);
  ~RegisterContextFreeBSD_powerpc64() override;

  size_t GetGPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTFREEBSD_POWERPC_H

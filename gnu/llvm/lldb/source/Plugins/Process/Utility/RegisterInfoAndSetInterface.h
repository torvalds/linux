//===-- RegisterInfoAndSetInterface.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERINFOANDSETINTERFACE_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERINFOANDSETINTERFACE_H

#include "RegisterInfoInterface.h"

#include "lldb/Utility/ArchSpec.h"
#include "lldb/lldb-private-types.h"
#include <vector>

namespace lldb_private {

class RegisterInfoAndSetInterface : public RegisterInfoInterface {
public:
  RegisterInfoAndSetInterface(const lldb_private::ArchSpec &target_arch)
      : RegisterInfoInterface(target_arch) {}

  virtual size_t GetFPRSize() const = 0;

  virtual const lldb_private::RegisterSet *
  GetRegisterSet(size_t reg_set) const = 0;

  virtual size_t GetRegisterSetCount() const = 0;

  virtual size_t GetRegisterSetFromRegisterIndex(uint32_t reg_index) const = 0;
};
} // namespace lldb_private

#endif

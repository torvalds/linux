//===-- NativeRegisterContextRegisterInfo.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_NATIVEREGISTERCONTEXTREGISTERINFO_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_NATIVEREGISTERCONTEXTREGISTERINFO_H

#include <memory>

#include "RegisterInfoInterface.h"
#include "lldb/Host/common/NativeRegisterContext.h"

namespace lldb_private {
class NativeRegisterContextRegisterInfo : public NativeRegisterContext {
public:
  ///
  /// Construct a NativeRegisterContextRegisterInfo, taking ownership
  /// of the register_info_interface pointer.
  ///
  NativeRegisterContextRegisterInfo(
      NativeThreadProtocol &thread,
      RegisterInfoInterface *register_info_interface);

  uint32_t GetRegisterCount() const override;

  uint32_t GetUserRegisterCount() const override;

  const RegisterInfo *GetRegisterInfoAtIndex(uint32_t reg_index) const override;

  const RegisterInfoInterface &GetRegisterInfoInterface() const;

protected:
  std::unique_ptr<RegisterInfoInterface> m_register_info_interface_up;
};
}
#endif

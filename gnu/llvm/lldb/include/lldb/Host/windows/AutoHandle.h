//===-- AutoHandle.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_lldb_Host_windows_AutoHandle_h_
#define LLDB_lldb_Host_windows_AutoHandle_h_

#include "lldb/Host/windows/windows.h"

namespace lldb_private {

class AutoHandle {
public:
  AutoHandle(HANDLE handle, HANDLE invalid_value = INVALID_HANDLE_VALUE)
      : m_handle(handle), m_invalid_value(invalid_value) {}

  ~AutoHandle() {
    if (m_handle != m_invalid_value)
      ::CloseHandle(m_handle);
  }

  bool IsValid() const { return m_handle != m_invalid_value; }

  HANDLE get() const { return m_handle; }

private:
  HANDLE m_handle;
  HANDLE m_invalid_value;
};
}

#endif

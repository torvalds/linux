//===-- HostThreadWindows.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_windows_HostThreadWindows_h_
#define lldb_Host_windows_HostThreadWindows_h_

#include "lldb/Host/HostNativeThreadBase.h"

#include "llvm/ADT/SmallString.h"

namespace lldb_private {

class HostThreadWindows : public HostNativeThreadBase {
  HostThreadWindows(const HostThreadWindows &) = delete;
  const HostThreadWindows &operator=(const HostThreadWindows &) = delete;

public:
  HostThreadWindows();
  HostThreadWindows(lldb::thread_t thread);
  virtual ~HostThreadWindows();

  void SetOwnsHandle(bool owns);

  Status Join(lldb::thread_result_t *result) override;
  Status Cancel() override;
  void Reset() override;
  bool EqualsThread(lldb::thread_t thread) const override;

  lldb::tid_t GetThreadId() const;

private:
  bool m_owns_handle;
};
}

#endif

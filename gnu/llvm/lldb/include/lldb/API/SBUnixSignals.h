//===-- SBUnixSignals.h -----------------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBUNIXSIGNALS_H
#define LLDB_API_SBUNIXSIGNALS_H

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBUnixSignals {
public:
  SBUnixSignals();

  SBUnixSignals(const lldb::SBUnixSignals &rhs);

  ~SBUnixSignals();

  const SBUnixSignals &operator=(const lldb::SBUnixSignals &rhs);

  void Clear();

  explicit operator bool() const;

  bool IsValid() const;

  const char *GetSignalAsCString(int32_t signo) const;

  int32_t GetSignalNumberFromName(const char *name) const;

  bool GetShouldSuppress(int32_t signo) const;

  bool SetShouldSuppress(int32_t signo, bool value);

  bool GetShouldStop(int32_t signo) const;

  bool SetShouldStop(int32_t signo, bool value);

  bool GetShouldNotify(int32_t signo) const;

  bool SetShouldNotify(int32_t signo, bool value);

  int32_t GetNumSignals() const;

  int32_t GetSignalAtIndex(int32_t index) const;

protected:
  friend class SBProcess;
  friend class SBPlatform;

  SBUnixSignals(lldb::ProcessSP &process_sp);

  SBUnixSignals(lldb::PlatformSP &platform_sp);

  lldb::UnixSignalsSP GetSP() const;

  void SetSP(const lldb::UnixSignalsSP &signals_sp);

private:
  lldb::UnixSignalsWP m_opaque_wp;
};

} // namespace lldb

#endif // LLDB_API_SBUNIXSIGNALS_H

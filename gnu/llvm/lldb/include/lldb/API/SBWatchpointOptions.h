//===-- SBWatchpointOptions.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBWATCHPOINTOPTIONS_H
#define LLDB_API_SBWATCHPOINTOPTIONS_H

#include "lldb/API/SBDefines.h"

class WatchpointOptionsImpl;

namespace lldb {

class LLDB_API SBWatchpointOptions {
public:
  SBWatchpointOptions();

  SBWatchpointOptions(const lldb::SBWatchpointOptions &rhs);

  ~SBWatchpointOptions();

  const SBWatchpointOptions &operator=(const lldb::SBWatchpointOptions &rhs);

  /// Stop when the watched memory region is read.
  void SetWatchpointTypeRead(bool read);
  bool GetWatchpointTypeRead() const;

  /// Stop when the watched memory region is written to/modified
  void SetWatchpointTypeWrite(lldb::WatchpointWriteType write_type);
  lldb::WatchpointWriteType GetWatchpointTypeWrite() const;

private:
  // This auto_pointer is made in the constructor and is always valid.
  mutable std::unique_ptr<WatchpointOptionsImpl> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBWATCHPOINTOPTIONS_H

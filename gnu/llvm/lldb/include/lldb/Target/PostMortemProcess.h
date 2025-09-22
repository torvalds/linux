//===-- PostMortemProcess.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_POSTMORTEMPROCESS_H
#define LLDB_TARGET_POSTMORTEMPROCESS_H

#include "lldb/Target/Process.h"

namespace lldb_private {

/// \class PostMortemProcess
/// Base class for all processes that don't represent a live process, such as
/// coredumps or processes traced in the past.
///
/// \a lldb_private::Process virtual functions overrides that are common
/// between these kinds of processes can have default implementations in this
/// class.
class PostMortemProcess : public Process {
  using Process::Process;

public:
  PostMortemProcess(lldb::TargetSP target_sp, lldb::ListenerSP listener_sp,
                    const FileSpec &core_file)
      : Process(target_sp, listener_sp), m_core_file(core_file) {}

  bool IsLiveDebugSession() const override { return false; }

  FileSpec GetCoreFile() const override { return m_core_file; }

protected:
  FileSpec m_core_file;
};

} // namespace lldb_private

#endif // LLDB_TARGET_POSTMORTEMPROCESS_H

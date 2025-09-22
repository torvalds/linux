//===-- Support.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/linux/Support.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "llvm/Support/MemoryBuffer.h"

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
lldb_private::getProcFile(::pid_t pid, ::pid_t tid, const llvm::Twine &file) {
  Log *log = GetLog(LLDBLog::Host);
  std::string File =
      ("/proc/" + llvm::Twine(pid) + "/task/" + llvm::Twine(tid) + "/" + file)
          .str();
  auto Ret = llvm::MemoryBuffer::getFileAsStream(File);
  if (!Ret)
    LLDB_LOG(log, "Failed to open {0}: {1}", File, Ret.getError().message());
  return Ret;
}

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
lldb_private::getProcFile(::pid_t pid, const llvm::Twine &file) {
  Log *log = GetLog(LLDBLog::Host);
  std::string File = ("/proc/" + llvm::Twine(pid) + "/" + file).str();
  auto Ret = llvm::MemoryBuffer::getFileAsStream(File);
  if (!Ret)
    LLDB_LOG(log, "Failed to open {0}: {1}", File, Ret.getError().message());
  return Ret;
}

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
lldb_private::getProcFile(const llvm::Twine &file) {
  Log *log = GetLog(LLDBLog::Host);
  std::string File = ("/proc/" + file).str();
  auto Ret = llvm::MemoryBuffer::getFileAsStream(File);
  if (!Ret)
    LLDB_LOG(log, "Failed to open {0}: {1}", File, Ret.getError().message());
  return Ret;
}

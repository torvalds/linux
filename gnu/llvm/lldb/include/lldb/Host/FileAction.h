//===-- FileAction.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_FILEACTION_H
#define LLDB_HOST_FILEACTION_H

#include "lldb/Utility/FileSpec.h"
#include <string>

namespace lldb_private {

class FileAction {
public:
  enum Action {
    eFileActionNone,
    eFileActionClose,
    eFileActionDuplicate,
    eFileActionOpen
  };

  FileAction();

  void Clear();

  bool Close(int fd);

  bool Duplicate(int fd, int dup_fd);

  bool Open(int fd, const FileSpec &file_spec, bool read, bool write);

  int GetFD() const { return m_fd; }

  Action GetAction() const { return m_action; }

  int GetActionArgument() const { return m_arg; }

  llvm::StringRef GetPath() const;

  const FileSpec &GetFileSpec() const;

  void Dump(Stream &stream) const;

protected:
  Action m_action = eFileActionNone; // The action for this file
  int m_fd = -1;                     // An existing file descriptor
  int m_arg = -1; // oflag for eFileActionOpen*, dup_fd for eFileActionDuplicate
  FileSpec
      m_file_spec; // A file spec to use for opening after fork or posix_spawn
};

} // namespace lldb_private

#endif

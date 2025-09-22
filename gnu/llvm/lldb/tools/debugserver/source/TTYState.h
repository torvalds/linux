//===-- TTYState.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 3/26/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_TTYSTATE_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_TTYSTATE_H

#include <cstdint>
#include <termios.h>

class TTYState {
public:
  TTYState();
  ~TTYState();

  bool GetTTYState(int fd, bool saveProcessGroup);
  bool SetTTYState() const;

  bool IsValid() const {
    return FileDescriptorValid() && TFlagsValid() && TTYStateValid();
  }
  bool FileDescriptorValid() const { return m_fd >= 0; }
  bool TFlagsValid() const { return m_tflags != -1; }
  bool TTYStateValid() const { return m_ttystateErr == 0; }
  bool ProcessGroupValid() const { return m_processGroup != -1; }

protected:
  int m_fd; // File descriptor
  int m_tflags;
  int m_ttystateErr;
  struct termios m_ttystate;
  pid_t m_processGroup;
};

class TTYStateSwitcher {
public:
  TTYStateSwitcher();
  ~TTYStateSwitcher();

  bool GetState(uint32_t idx, int fd, bool saveProcessGroup);
  bool SetState(uint32_t idx) const;
  uint32_t NumStates() const { return sizeof(m_ttystates) / sizeof(TTYState); }
  bool ValidStateIndex(uint32_t idx) const { return idx < NumStates(); }

protected:
  mutable uint32_t m_currentState;
  TTYState m_ttystates[2];
};

#endif

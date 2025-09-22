//===-- PseudoTerminal.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 1/8/08.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_PSEUDOTERMINAL_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_PSEUDOTERMINAL_H

#include <fcntl.h>
#include <string>
#include <termios.h>

class PseudoTerminal {
public:
  enum { invalid_fd = -1, invalid_pid = -1 };

  enum Status {
    success = 0,
    err_posix_openpt_failed = -2,
    err_grantpt_failed = -3,
    err_unlockpt_failed = -4,
    err_ptsname_failed = -5,
    err_open_secondary_failed = -6,
    err_fork_failed = -7,
    err_setsid_failed = -8,
    err_failed_to_acquire_controlling_terminal = -9,
    err_dup2_failed_on_stdin = -10,
    err_dup2_failed_on_stdout = -11,
    err_dup2_failed_on_stderr = -12
  };
  // Constructors and Destructors
  PseudoTerminal();
  ~PseudoTerminal();

  void ClosePrimary();
  void CloseSecondary();
  Status OpenFirstAvailablePrimary(int oflag);
  Status OpenSecondary(int oflag);
  int PrimaryFD() const { return m_primary_fd; }
  int SecondaryFD() const { return m_secondary_fd; }
  int ReleasePrimaryFD() {
    // Release ownership of the primary pseudo terminal file
    // descriptor without closing it. (the destructor for this
    // class will close it otherwise!)
    int fd = m_primary_fd;
    m_primary_fd = invalid_fd;
    return fd;
  }
  int ReleaseSecondaryFD() {
    // Release ownership of the secondary pseudo terminal file
    // descriptor without closing it (the destructor for this
    // class will close it otherwise!)
    int fd = m_secondary_fd;
    m_secondary_fd = invalid_fd;
    return fd;
  }

  const char *SecondaryName() const;

  pid_t Fork(Status &error);

protected:
  // Classes that inherit from PseudoTerminal can see and modify these
  int m_primary_fd;
  int m_secondary_fd;

private:
  PseudoTerminal(const PseudoTerminal &rhs) = delete;
  PseudoTerminal &operator=(const PseudoTerminal &rhs) = delete;
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_PSEUDOTERMINAL_H

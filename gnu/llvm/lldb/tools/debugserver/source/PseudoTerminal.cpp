//===-- PseudoTerminal.cpp --------------------------------------*- C++ -*-===//
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

#include "PseudoTerminal.h"
#include <cstdlib>
#include <sys/ioctl.h>
#include <unistd.h>

// PseudoTerminal constructor
PseudoTerminal::PseudoTerminal()
    : m_primary_fd(invalid_fd), m_secondary_fd(invalid_fd) {}

// Destructor
// The primary and secondary file descriptors will get closed if they are
// valid. Call the ReleasePrimaryFD()/ReleaseSecondaryFD() member functions
// to release any file descriptors that are needed beyond the lifespan
// of this object.
PseudoTerminal::~PseudoTerminal() {
  ClosePrimary();
  CloseSecondary();
}

// Close the primary file descriptor if it is valid.
void PseudoTerminal::ClosePrimary() {
  if (m_primary_fd > 0) {
    ::close(m_primary_fd);
    m_primary_fd = invalid_fd;
  }
}

// Close the secondary file descriptor if it is valid.
void PseudoTerminal::CloseSecondary() {
  if (m_secondary_fd > 0) {
    ::close(m_secondary_fd);
    m_secondary_fd = invalid_fd;
  }
}

// Open the first available pseudo terminal with OFLAG as the
// permissions. The file descriptor is store in the m_primary_fd member
// variable and can be accessed via the PrimaryFD() or ReleasePrimaryFD()
// accessors.
//
// Suggested value for oflag is O_RDWR|O_NOCTTY
//
// RETURNS:
//  Zero when successful, non-zero indicating an error occurred.
PseudoTerminal::Status PseudoTerminal::OpenFirstAvailablePrimary(int oflag) {
  // Open the primary side of a pseudo terminal
  m_primary_fd = ::posix_openpt(oflag);
  if (m_primary_fd < 0) {
    return err_posix_openpt_failed;
  }

  // Grant access to the secondary pseudo terminal
  if (::grantpt(m_primary_fd) < 0) {
    ClosePrimary();
    return err_grantpt_failed;
  }

  // Clear the lock flag on the secondary pseudo terminal
  if (::unlockpt(m_primary_fd) < 0) {
    ClosePrimary();
    return err_unlockpt_failed;
  }

  return success;
}

// Open the secondary pseudo terminal for the current primary pseudo
// terminal. A primary pseudo terminal should already be valid prior to
// calling this function (see PseudoTerminal::OpenFirstAvailablePrimary()).
// The file descriptor is stored in the m_secondary_fd member variable and
// can be accessed via the SecondaryFD() or ReleaseSecondaryFD() accessors.
//
// RETURNS:
//  Zero when successful, non-zero indicating an error occurred.
PseudoTerminal::Status PseudoTerminal::OpenSecondary(int oflag) {
  CloseSecondary();

  // Open the primary side of a pseudo terminal
  const char *secondary_name = SecondaryName();

  if (secondary_name == NULL)
    return err_ptsname_failed;

  m_secondary_fd = ::open(secondary_name, oflag);

  if (m_secondary_fd < 0)
    return err_open_secondary_failed;

  return success;
}

// Get the name of the secondary pseudo terminal. A primary pseudo terminal
// should already be valid prior to calling this function (see
// PseudoTerminal::OpenFirstAvailablePrimary()).
//
// RETURNS:
//  NULL if no valid primary pseudo terminal or if ptsname() fails.
//  The name of the secondary pseudo terminal as a NULL terminated C string
//  that comes from static memory, so a copy of the string should be
//  made as subsequent calls can change this value.
const char *PseudoTerminal::SecondaryName() const {
  if (m_primary_fd < 0)
    return NULL;
  return ::ptsname(m_primary_fd);
}

// Fork a child process that and have its stdio routed to a pseudo
// terminal.
//
// In the parent process when a valid pid is returned, the primary file
// descriptor can be used as a read/write access to stdio of the
// child process.
//
// In the child process the stdin/stdout/stderr will already be routed
// to the secondary pseudo terminal and the primary file descriptor will be
// closed as it is no longer needed by the child process.
//
// This class will close the file descriptors for the primary/secondary
// when the destructor is called, so be sure to call ReleasePrimaryFD()
// or ReleaseSecondaryFD() if any file descriptors are going to be used
// past the lifespan of this object.
//
// RETURNS:
//  in the parent process: the pid of the child, or -1 if fork fails
//  in the child process: zero

pid_t PseudoTerminal::Fork(PseudoTerminal::Status &error) {
  pid_t pid = invalid_pid;
  error = OpenFirstAvailablePrimary(O_RDWR | O_NOCTTY);

  if (error == 0) {
    // Successfully opened our primary pseudo terminal

    pid = ::fork();
    if (pid < 0) {
      // Fork failed
      error = err_fork_failed;
    } else if (pid == 0) {
      // Child Process
      ::setsid();

      error = OpenSecondary(O_RDWR);
      if (error == 0) {
        // Successfully opened secondary
        // We are done with the primary in the child process so lets close it
        ClosePrimary();

#if defined(TIOCSCTTY)
        // Acquire the controlling terminal
        if (::ioctl(m_secondary_fd, TIOCSCTTY, (char *)0) < 0)
          error = err_failed_to_acquire_controlling_terminal;
#endif
        // Duplicate all stdio file descriptors to the secondary pseudo terminal
        if (::dup2(m_secondary_fd, STDIN_FILENO) != STDIN_FILENO)
          error = error ? error : err_dup2_failed_on_stdin;
        if (::dup2(m_secondary_fd, STDOUT_FILENO) != STDOUT_FILENO)
          error = error ? error : err_dup2_failed_on_stdout;
        if (::dup2(m_secondary_fd, STDERR_FILENO) != STDERR_FILENO)
          error = error ? error : err_dup2_failed_on_stderr;
      }
    } else {
      // Parent Process
      // Do nothing and let the pid get returned!
    }
  }
  return pid;
}

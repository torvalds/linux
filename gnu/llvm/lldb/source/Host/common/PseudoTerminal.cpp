//===-- PseudoTerminal.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/FileSystem.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Errno.h"
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#if defined(TIOCSCTTY)
#include <sys/ioctl.h>
#endif

#include "lldb/Host/PosixApi.h"

#if defined(__APPLE__)
#include <Availability.h>
#endif

#if defined(__ANDROID__)
int posix_openpt(int flags);
#endif

using namespace lldb_private;

// PseudoTerminal constructor
PseudoTerminal::PseudoTerminal() = default;

// Destructor
//
// The destructor will close the primary and secondary file descriptors if they
// are valid and ownership has not been released using the
// ReleasePrimaryFileDescriptor() or the ReleaseSaveFileDescriptor() member
// functions.
PseudoTerminal::~PseudoTerminal() {
  ClosePrimaryFileDescriptor();
  CloseSecondaryFileDescriptor();
}

// Close the primary file descriptor if it is valid.
void PseudoTerminal::ClosePrimaryFileDescriptor() {
  if (m_primary_fd >= 0) {
    ::close(m_primary_fd);
    m_primary_fd = invalid_fd;
  }
}

// Close the secondary file descriptor if it is valid.
void PseudoTerminal::CloseSecondaryFileDescriptor() {
  if (m_secondary_fd >= 0) {
    ::close(m_secondary_fd);
    m_secondary_fd = invalid_fd;
  }
}

llvm::Error PseudoTerminal::OpenFirstAvailablePrimary(int oflag) {
#if LLDB_ENABLE_POSIX
  // Open the primary side of a pseudo terminal
  m_primary_fd = ::posix_openpt(oflag);
  if (m_primary_fd < 0) {
    return llvm::errorCodeToError(
        std::error_code(errno, std::generic_category()));
  }

  // Grant access to the secondary pseudo terminal
  if (::grantpt(m_primary_fd) < 0) {
    std::error_code EC(errno, std::generic_category());
    ClosePrimaryFileDescriptor();
    return llvm::errorCodeToError(EC);
  }

  // Clear the lock flag on the secondary pseudo terminal
  if (::unlockpt(m_primary_fd) < 0) {
    std::error_code EC(errno, std::generic_category());
    ClosePrimaryFileDescriptor();
    return llvm::errorCodeToError(EC);
  }

  return llvm::Error::success();
#else
  return llvm::errorCodeToError(llvm::errc::not_supported);
#endif
}

llvm::Error PseudoTerminal::OpenSecondary(int oflag) {
  CloseSecondaryFileDescriptor();

  std::string name = GetSecondaryName();
  m_secondary_fd = FileSystem::Instance().Open(name.c_str(), oflag);
  if (m_secondary_fd >= 0)
    return llvm::Error::success();

  return llvm::errorCodeToError(
      std::error_code(errno, std::generic_category()));
}

#if !HAVE_PTSNAME_R || defined(__APPLE__)
static std::string use_ptsname(int fd) {
  static std::mutex mutex;
  std::lock_guard<std::mutex> guard(mutex);
  const char *r = ptsname(fd);
  assert(r != nullptr);
  return r;
}
#endif

std::string PseudoTerminal::GetSecondaryName() const {
  assert(m_primary_fd >= 0);
#if HAVE_PTSNAME_R
#if defined(__APPLE__)
  if (__builtin_available(macos 10.13.4, iOS 11.3, tvOS 11.3, watchOS 4.4, *)) {
#endif
    char buf[PATH_MAX];
    buf[0] = '\0';
    int r = ptsname_r(m_primary_fd, buf, sizeof(buf));
    UNUSED_IF_ASSERT_DISABLED(r);
    assert(r == 0);
    return buf;
#if defined(__APPLE__)
  } else {
    return use_ptsname(m_primary_fd);
  }
#endif
#else
  return use_ptsname(m_primary_fd);
#endif
}

llvm::Expected<lldb::pid_t> PseudoTerminal::Fork() {
#if LLDB_ENABLE_POSIX
  if (llvm::Error Err = OpenFirstAvailablePrimary(O_RDWR | O_CLOEXEC))
    return std::move(Err);

  pid_t pid = ::fork();
  if (pid < 0) {
    return llvm::errorCodeToError(
        std::error_code(errno, std::generic_category()));
  }
  if (pid > 0) {
    // Parent process.
    return pid;
  }

  // Child Process
  ::setsid();

  if (llvm::Error Err = OpenSecondary(O_RDWR))
    return std::move(Err);

  // Primary FD should have O_CLOEXEC set, but let's close it just in
  // case...
  ClosePrimaryFileDescriptor();

#if defined(TIOCSCTTY)
  // Acquire the controlling terminal
  if (::ioctl(m_secondary_fd, TIOCSCTTY, (char *)0) < 0) {
    return llvm::errorCodeToError(
        std::error_code(errno, std::generic_category()));
  }
#endif
  // Duplicate all stdio file descriptors to the secondary pseudo terminal
  for (int fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
    if (::dup2(m_secondary_fd, fd) != fd) {
      return llvm::errorCodeToError(
          std::error_code(errno, std::generic_category()));
    }
  }
#endif
  return 0;
}

// The primary file descriptor accessor. This object retains ownership of the
// primary file descriptor when this accessor is used. Use
// ReleasePrimaryFileDescriptor() if you wish this object to release ownership
// of the primary file descriptor.
//
// Returns the primary file descriptor, or -1 if the primary file descriptor is
// not currently valid.
int PseudoTerminal::GetPrimaryFileDescriptor() const { return m_primary_fd; }

// The secondary file descriptor accessor.
//
// Returns the secondary file descriptor, or -1 if the secondary file descriptor
// is not currently valid.
int PseudoTerminal::GetSecondaryFileDescriptor() const {
  return m_secondary_fd;
}

// Release ownership of the primary pseudo terminal file descriptor without
// closing it. The destructor for this class will close the primary file
// descriptor if the ownership isn't released using this call and the primary
// file descriptor has been opened.
int PseudoTerminal::ReleasePrimaryFileDescriptor() {
  // Release ownership of the primary pseudo terminal file descriptor without
  // closing it. (the destructor for this class will close it otherwise!)
  int fd = m_primary_fd;
  m_primary_fd = invalid_fd;
  return fd;
}

// Release ownership of the secondary pseudo terminal file descriptor without
// closing it. The destructor for this class will close the secondary file
// descriptor if the ownership isn't released using this call and the secondary
// file descriptor has been opened.
int PseudoTerminal::ReleaseSecondaryFileDescriptor() {
  // Release ownership of the secondary pseudo terminal file descriptor without
  // closing it (the destructor for this class will close it otherwise!)
  int fd = m_secondary_fd;
  m_secondary_fd = invalid_fd;
  return fd;
}

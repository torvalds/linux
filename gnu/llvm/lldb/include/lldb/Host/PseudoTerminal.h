//===-- PseudoTerminal.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_PSEUDOTERMINAL_H
#define LLDB_HOST_PSEUDOTERMINAL_H

#include "lldb/lldb-defines.h"
#include "llvm/Support/Error.h"
#include <fcntl.h>
#include <string>

namespace lldb_private {

/// \class PseudoTerminal PseudoTerminal.h "lldb/Host/PseudoTerminal.h"
/// A pseudo terminal helper class.
///
/// The pseudo terminal class abstracts the use of pseudo terminals on the
/// host system.
class PseudoTerminal {
public:
  enum {
    invalid_fd = -1 ///< Invalid file descriptor value
  };

  /// Default constructor
  ///
  /// Constructs this object with invalid primary and secondary file
  /// descriptors.
  PseudoTerminal();

  /// Destructor
  ///
  /// The destructor will close the primary and secondary file descriptors if
  /// they are valid and ownership has not been released using one of: @li
  /// PseudoTerminal::ReleasePrimaryFileDescriptor() @li
  /// PseudoTerminal::ReleaseSaveFileDescriptor()
  ~PseudoTerminal();

  /// Close the primary file descriptor if it is valid.
  void ClosePrimaryFileDescriptor();

  /// Close the secondary file descriptor if it is valid.
  void CloseSecondaryFileDescriptor();

  /// Fork a child process that uses pseudo terminals for its stdio.
  ///
  /// In the parent process, a call to this function results in a pid being
  /// returned. If the pid is valid, the primary file descriptor can be used
  /// for read/write access to stdio of the child process.
  ///
  /// In the child process the stdin/stdout/stderr will already be routed to
  /// the secondary pseudo terminal and the primary file descriptor will be
  /// closed as it is no longer needed by the child process.
  ///
  /// This class will close the file descriptors for the primary/secondary when
  /// the destructor is called. The file handles can be released using either:
  /// @li PseudoTerminal::ReleasePrimaryFileDescriptor() @li
  /// PseudoTerminal::ReleaseSaveFileDescriptor()
  ///
  /// \return
  ///     \b Parent process: a child process ID that is greater
  ///         than zero, or an error if the fork fails.
  ///     \b Child process: zero.
  llvm::Expected<lldb::pid_t> Fork();

  /// The primary file descriptor accessor.
  ///
  /// This object retains ownership of the primary file descriptor when this
  /// accessor is used. Users can call the member function
  /// PseudoTerminal::ReleasePrimaryFileDescriptor() if this object should
  /// release ownership of the secondary file descriptor.
  ///
  /// \return
  ///     The primary file descriptor, or PseudoTerminal::invalid_fd
  ///     if the primary file  descriptor is not currently valid.
  ///
  /// \see PseudoTerminal::ReleasePrimaryFileDescriptor()
  int GetPrimaryFileDescriptor() const;

  /// The secondary file descriptor accessor.
  ///
  /// This object retains ownership of the secondary file descriptor when this
  /// accessor is used. Users can call the member function
  /// PseudoTerminal::ReleaseSecondaryFileDescriptor() if this object should
  /// release ownership of the secondary file descriptor.
  ///
  /// \return
  ///     The secondary file descriptor, or PseudoTerminal::invalid_fd
  ///     if the secondary file descriptor is not currently valid.
  ///
  /// \see PseudoTerminal::ReleaseSecondaryFileDescriptor()
  int GetSecondaryFileDescriptor() const;

  /// Get the name of the secondary pseudo terminal.
  ///
  /// A primary pseudo terminal should already be valid prior to
  /// calling this function.
  ///
  /// \return
  ///     The name of the secondary pseudo terminal.
  ///
  /// \see PseudoTerminal::OpenFirstAvailablePrimary()
  std::string GetSecondaryName() const;

  /// Open the first available pseudo terminal.
  ///
  /// Opens the first available pseudo terminal with \a oflag as the
  /// permissions. The opened primary file descriptor is stored in this object
  /// and can be accessed by calling the
  /// PseudoTerminal::GetPrimaryFileDescriptor() accessor. Clients can call the
  /// PseudoTerminal::ReleasePrimaryFileDescriptor() accessor function if they
  /// wish to use the primary file descriptor beyond the lifespan of this
  /// object.
  ///
  /// If this object still has a valid primary file descriptor when its
  /// destructor is called, it will close it.
  ///
  /// \param[in] oflag
  ///     Flags to use when calling \c posix_openpt(\a oflag).
  ///     A value of "O_RDWR|O_NOCTTY" is suggested.
  ///
  /// \see PseudoTerminal::GetPrimaryFileDescriptor() @see
  /// PseudoTerminal::ReleasePrimaryFileDescriptor()
  llvm::Error OpenFirstAvailablePrimary(int oflag);

  /// Open the secondary for the current primary pseudo terminal.
  ///
  /// A primary pseudo terminal should already be valid prior to
  /// calling this function. The opened secondary file descriptor is stored in
  /// this object and can be accessed by calling the
  /// PseudoTerminal::GetSecondaryFileDescriptor() accessor. Clients can call
  /// the PseudoTerminal::ReleaseSecondaryFileDescriptor() accessor function if
  /// they wish to use the secondary file descriptor beyond the lifespan of this
  /// object.
  ///
  /// If this object still has a valid secondary file descriptor when its
  /// destructor is called, it will close it.
  ///
  /// \param[in] oflag
  ///     Flags to use when calling \c open(\a oflag).
  ///
  /// \see PseudoTerminal::OpenFirstAvailablePrimary() @see
  /// PseudoTerminal::GetSecondaryFileDescriptor() @see
  /// PseudoTerminal::ReleaseSecondaryFileDescriptor()
  llvm::Error OpenSecondary(int oflag);

  /// Release the primary file descriptor.
  ///
  /// Releases ownership of the primary pseudo terminal file descriptor without
  /// closing it. The destructor for this class will close the primary file
  /// descriptor if the ownership isn't released using this call and the
  /// primary file descriptor has been opened.
  ///
  /// \return
  ///     The primary file descriptor, or PseudoTerminal::invalid_fd
  ///     if the mast file descriptor is not currently valid.
  int ReleasePrimaryFileDescriptor();

  /// Release the secondary file descriptor.
  ///
  /// Release ownership of the secondary pseudo terminal file descriptor without
  /// closing it. The destructor for this class will close the secondary file
  /// descriptor if the ownership isn't released using this call and the
  /// secondary file descriptor has been opened.
  ///
  /// \return
  ///     The secondary file descriptor, or PseudoTerminal::invalid_fd
  ///     if the secondary file descriptor is not currently valid.
  int ReleaseSecondaryFileDescriptor();

protected:
  // Member variables
  int m_primary_fd = invalid_fd;   ///< The file descriptor for the primary.
  int m_secondary_fd = invalid_fd; ///< The file descriptor for the secondary.

private:
  PseudoTerminal(const PseudoTerminal &) = delete;
  const PseudoTerminal &operator=(const PseudoTerminal &) = delete;
};

} // namespace lldb_private

#endif // LLDB_HOST_PSEUDOTERMINAL_H

//===-- Terminal.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_TERMINAL_H
#define LLDB_HOST_TERMINAL_H

#include "lldb/lldb-private.h"
#include "llvm/Support/Error.h"

namespace lldb_private {

class TerminalState;

class Terminal {
public:
  enum class Parity {
    No,
    Even,
    Odd,
    Space,
    Mark,
  };

  enum class ParityCheck {
    // No parity checking
    No,
    // Replace erraneous bytes with NUL
    ReplaceWithNUL,
    // Ignore erraneous bytes
    Ignore,
    // Mark erraneous bytes by prepending them with \xFF\x00; real \xFF
    // is escaped to \xFF\xFF
    Mark,
  };

  Terminal(int fd = -1) : m_fd(fd) {}

  ~Terminal() = default;

  bool IsATerminal() const;

  int GetFileDescriptor() const { return m_fd; }

  void SetFileDescriptor(int fd) { m_fd = fd; }

  bool FileDescriptorIsValid() const { return m_fd != -1; }

  void Clear() { m_fd = -1; }

  llvm::Error SetEcho(bool enabled);

  llvm::Error SetCanonical(bool enabled);

  llvm::Error SetRaw();

  llvm::Error SetBaudRate(unsigned int baud_rate);

  llvm::Error SetStopBits(unsigned int stop_bits);

  llvm::Error SetParity(Parity parity);

  llvm::Error SetParityCheck(ParityCheck parity_check);

  llvm::Error SetHardwareFlowControl(bool enabled);

protected:
  struct Data;

  int m_fd; // This may or may not be a terminal file descriptor

  llvm::Expected<Data> GetData();
  llvm::Error SetData(const Data &data);

  friend class TerminalState;
};

/// \class TerminalState Terminal.h "lldb/Host/Terminal.h"
/// A RAII-friendly terminal state saving/restoring class.
///
/// This class can be used to remember the terminal state for a file
/// descriptor and later restore that state as it originally was.
class TerminalState {
public:
  /// Construct a new instance and optionally save terminal state.
  ///
  /// \param[in] term
  ///     The Terminal instance holding the file descriptor to save the state
  ///     of.  If the instance is not associated with a fd, no state will
  ///     be saved.
  ///
  /// \param[in] save_process_group
  ///     If \b true, save the process group settings, else do not
  ///     save the process group settings for a TTY.
  TerminalState(Terminal term = -1, bool save_process_group = false);

  /// Destroy the instance, restoring terminal state if saved.  If restoring
  /// state is undesirable, the instance needs to be reset before destruction.
  ~TerminalState();

  /// Save the TTY state for \a fd.
  ///
  /// Save the current state of the TTY for the file descriptor "fd" and if
  /// "save_process_group" is true, attempt to save the process group info for
  /// the TTY.
  ///
  /// \param[in] term
  ///     The Terminal instance holding fd to save.
  ///
  /// \param[in] save_process_group
  ///     If \b true, save the process group settings, else do not
  ///     save the process group settings for a TTY.
  ///
  /// \return
  ///     Returns \b true if \a fd describes a TTY and if the state
  ///     was able to be saved, \b false otherwise.
  bool Save(Terminal term, bool save_process_group);

  /// Restore the TTY state to the cached state.
  ///
  /// Restore the state of the TTY using the cached values from a previous
  /// call to TerminalState::Save(int,bool).
  ///
  /// \return
  ///     Returns \b true if the TTY state was successfully restored,
  ///     \b false otherwise.
  bool Restore() const;

  /// Test for valid cached TTY state information.
  ///
  /// \return
  ///     Returns \b true if this object has valid saved TTY state
  ///     settings that can be used to restore a previous state,
  ///     \b false otherwise.
  bool IsValid() const;

  void Clear();

protected:
  /// Test if tflags is valid.
  ///
  /// \return
  ///     Returns \b true if \a m_tflags is valid and can be restored,
  ///     \b false otherwise.
  bool TFlagsIsValid() const;

  /// Test if ttystate is valid.
  ///
  /// \return
  ///     Returns \b true if \a m_ttystate is valid and can be
  ///     restored, \b false otherwise.
  bool TTYStateIsValid() const;

  /// Test if the process group information is valid.
  ///
  /// \return
  ///     Returns \b true if \a m_process_group is valid and can be
  ///     restored, \b false otherwise.
  bool ProcessGroupIsValid() const;

  // Member variables
  Terminal m_tty;                         ///< A terminal
  int m_tflags = -1;                      ///< Cached tflags information.
  std::unique_ptr<Terminal::Data> m_data; ///< Platform-specific implementation.
  lldb::pid_t m_process_group = -1;       ///< Cached process group information.
};

} // namespace lldb_private

#endif // LLDB_HOST_TERMINAL_H

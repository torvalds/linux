//===-- Terminal.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Terminal.h"

#include "lldb/Host/Config.h"
#include "lldb/Host/PosixApi.h"
#include "llvm/ADT/STLExtras.h"

#include <csignal>
#include <fcntl.h>
#include <optional>

#if LLDB_ENABLE_TERMIOS
#include <termios.h>
#endif

using namespace lldb_private;

struct Terminal::Data {
#if LLDB_ENABLE_TERMIOS
  struct termios m_termios; ///< Cached terminal state information.
#endif
};

bool Terminal::IsATerminal() const { return m_fd >= 0 && ::isatty(m_fd); }

#if !LLDB_ENABLE_TERMIOS
static llvm::Error termiosMissingError() {
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "termios support missing in LLDB");
}
#endif

llvm::Expected<Terminal::Data> Terminal::GetData() {
#if LLDB_ENABLE_TERMIOS
  if (!FileDescriptorIsValid())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid fd");

  if (!IsATerminal())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "fd not a terminal");

  Data data;
  if (::tcgetattr(m_fd, &data.m_termios) != 0)
    return llvm::createStringError(
        std::error_code(errno, std::generic_category()),
        "unable to get teletype attributes");
  return data;
#else // !LLDB_ENABLE_TERMIOS
  return termiosMissingError();
#endif // LLDB_ENABLE_TERMIOS
}

llvm::Error Terminal::SetData(const Terminal::Data &data) {
#if LLDB_ENABLE_TERMIOS
  assert(FileDescriptorIsValid());
  assert(IsATerminal());

  if (::tcsetattr(m_fd, TCSANOW, &data.m_termios) != 0)
    return llvm::createStringError(
        std::error_code(errno, std::generic_category()),
        "unable to set teletype attributes");
  return llvm::Error::success();
#else // !LLDB_ENABLE_TERMIOS
  return termiosMissingError();
#endif // LLDB_ENABLE_TERMIOS
}

llvm::Error Terminal::SetEcho(bool enabled) {
#if LLDB_ENABLE_TERMIOS
  llvm::Expected<Data> data = GetData();
  if (!data)
    return data.takeError();

  struct termios &fd_termios = data->m_termios;
  fd_termios.c_lflag &= ~ECHO;
  if (enabled)
    fd_termios.c_lflag |= ECHO;
  return SetData(data.get());
#else // !LLDB_ENABLE_TERMIOS
  return termiosMissingError();
#endif // LLDB_ENABLE_TERMIOS
}

llvm::Error Terminal::SetCanonical(bool enabled) {
#if LLDB_ENABLE_TERMIOS
  llvm::Expected<Data> data = GetData();
  if (!data)
    return data.takeError();

  struct termios &fd_termios = data->m_termios;
  fd_termios.c_lflag &= ~ICANON;
  if (enabled)
    fd_termios.c_lflag |= ICANON;
  return SetData(data.get());
#else // !LLDB_ENABLE_TERMIOS
  return termiosMissingError();
#endif // LLDB_ENABLE_TERMIOS
}

llvm::Error Terminal::SetRaw() {
#if LLDB_ENABLE_TERMIOS
  llvm::Expected<Data> data = GetData();
  if (!data)
    return data.takeError();

  struct termios &fd_termios = data->m_termios;
  ::cfmakeraw(&fd_termios);

  // Make sure only one character is needed to return from a read
  // (cfmakeraw() doesn't do this on NetBSD)
  fd_termios.c_cc[VMIN] = 1;
  fd_termios.c_cc[VTIME] = 0;

  return SetData(data.get());
#else // !LLDB_ENABLE_TERMIOS
  return termiosMissingError();
#endif // LLDB_ENABLE_TERMIOS
}

#if LLDB_ENABLE_TERMIOS
static std::optional<speed_t> baudRateToConst(unsigned int baud_rate) {
  switch (baud_rate) {
#if defined(B50)
  case 50:
    return B50;
#endif
#if defined(B75)
  case 75:
    return B75;
#endif
#if defined(B110)
  case 110:
    return B110;
#endif
#if defined(B134)
  case 134:
    return B134;
#endif
#if defined(B150)
  case 150:
    return B150;
#endif
#if defined(B200)
  case 200:
    return B200;
#endif
#if defined(B300)
  case 300:
    return B300;
#endif
#if defined(B600)
  case 600:
    return B600;
#endif
#if defined(B1200)
  case 1200:
    return B1200;
#endif
#if defined(B1800)
  case 1800:
    return B1800;
#endif
#if defined(B2400)
  case 2400:
    return B2400;
#endif
#if defined(B4800)
  case 4800:
    return B4800;
#endif
#if defined(B9600)
  case 9600:
    return B9600;
#endif
#if defined(B19200)
  case 19200:
    return B19200;
#endif
#if defined(B38400)
  case 38400:
    return B38400;
#endif
#if defined(B57600)
  case 57600:
    return B57600;
#endif
#if defined(B115200)
  case 115200:
    return B115200;
#endif
#if defined(B230400)
  case 230400:
    return B230400;
#endif
#if defined(B460800)
  case 460800:
    return B460800;
#endif
#if defined(B500000)
  case 500000:
    return B500000;
#endif
#if defined(B576000)
  case 576000:
    return B576000;
#endif
#if defined(B921600)
  case 921600:
    return B921600;
#endif
#if defined(B1000000)
  case 1000000:
    return B1000000;
#endif
#if defined(B1152000)
  case 1152000:
    return B1152000;
#endif
#if defined(B1500000)
  case 1500000:
    return B1500000;
#endif
#if defined(B2000000)
  case 2000000:
    return B2000000;
#endif
#if defined(B76800)
  case 76800:
    return B76800;
#endif
#if defined(B153600)
  case 153600:
    return B153600;
#endif
#if defined(B307200)
  case 307200:
    return B307200;
#endif
#if defined(B614400)
  case 614400:
    return B614400;
#endif
#if defined(B2500000)
  case 2500000:
    return B2500000;
#endif
#if defined(B3000000)
  case 3000000:
    return B3000000;
#endif
#if defined(B3500000)
  case 3500000:
    return B3500000;
#endif
#if defined(B4000000)
  case 4000000:
    return B4000000;
#endif
  default:
    return std::nullopt;
  }
}
#endif

llvm::Error Terminal::SetBaudRate(unsigned int baud_rate) {
#if LLDB_ENABLE_TERMIOS
  llvm::Expected<Data> data = GetData();
  if (!data)
    return data.takeError();

  struct termios &fd_termios = data->m_termios;
  std::optional<speed_t> val = baudRateToConst(baud_rate);
  if (!val) // invalid value
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "baud rate %d unsupported by the platform",
                                   baud_rate);
  if (::cfsetispeed(&fd_termios, *val) != 0)
    return llvm::createStringError(
        std::error_code(errno, std::generic_category()),
        "setting input baud rate failed");
  if (::cfsetospeed(&fd_termios, *val) != 0)
    return llvm::createStringError(
        std::error_code(errno, std::generic_category()),
        "setting output baud rate failed");
  return SetData(data.get());
#else // !LLDB_ENABLE_TERMIOS
  return termiosMissingError();
#endif // LLDB_ENABLE_TERMIOS
}

llvm::Error Terminal::SetStopBits(unsigned int stop_bits) {
#if LLDB_ENABLE_TERMIOS
  llvm::Expected<Data> data = GetData();
  if (!data)
    return data.takeError();

  struct termios &fd_termios = data->m_termios;
  switch (stop_bits) {
  case 1:
    fd_termios.c_cflag &= ~CSTOPB;
    break;
  case 2:
    fd_termios.c_cflag |= CSTOPB;
    break;
  default:
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "invalid stop bit count: %d (must be 1 or 2)", stop_bits);
  }
  return SetData(data.get());
#else // !LLDB_ENABLE_TERMIOS
  return termiosMissingError();
#endif // LLDB_ENABLE_TERMIOS
}

llvm::Error Terminal::SetParity(Terminal::Parity parity) {
#if LLDB_ENABLE_TERMIOS
  llvm::Expected<Data> data = GetData();
  if (!data)
    return data.takeError();

  struct termios &fd_termios = data->m_termios;
  fd_termios.c_cflag &= ~(
#if defined(CMSPAR)
      CMSPAR |
#endif
      PARENB | PARODD);

  if (parity != Parity::No) {
    fd_termios.c_cflag |= PARENB;
    if (parity == Parity::Odd || parity == Parity::Mark)
      fd_termios.c_cflag |= PARODD;
    if (parity == Parity::Mark || parity == Parity::Space) {
#if defined(CMSPAR)
      fd_termios.c_cflag |= CMSPAR;
#else
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "space/mark parity is not supported by the platform");
#endif
    }
  }
  return SetData(data.get());
#else // !LLDB_ENABLE_TERMIOS
  return termiosMissingError();
#endif // LLDB_ENABLE_TERMIOS
}

llvm::Error Terminal::SetParityCheck(Terminal::ParityCheck parity_check) {
#if LLDB_ENABLE_TERMIOS
  llvm::Expected<Data> data = GetData();
  if (!data)
    return data.takeError();

  struct termios &fd_termios = data->m_termios;
  fd_termios.c_iflag &= ~(IGNPAR | PARMRK | INPCK);

  if (parity_check != ParityCheck::No) {
    fd_termios.c_iflag |= INPCK;
    if (parity_check == ParityCheck::Ignore)
      fd_termios.c_iflag |= IGNPAR;
    else if (parity_check == ParityCheck::Mark)
      fd_termios.c_iflag |= PARMRK;
  }
  return SetData(data.get());
#else // !LLDB_ENABLE_TERMIOS
  return termiosMissingError();
#endif // LLDB_ENABLE_TERMIOS
}

llvm::Error Terminal::SetHardwareFlowControl(bool enabled) {
#if LLDB_ENABLE_TERMIOS
  llvm::Expected<Data> data = GetData();
  if (!data)
    return data.takeError();

#if defined(CRTSCTS)
  struct termios &fd_termios = data->m_termios;
  fd_termios.c_cflag &= ~CRTSCTS;
  if (enabled)
    fd_termios.c_cflag |= CRTSCTS;
  return SetData(data.get());
#else  // !defined(CRTSCTS)
  if (enabled)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "hardware flow control is not supported by the platform");
  return llvm::Error::success();
#endif // defined(CRTSCTS)
#else // !LLDB_ENABLE_TERMIOS
  return termiosMissingError();
#endif // LLDB_ENABLE_TERMIOS
}

TerminalState::TerminalState(Terminal term, bool save_process_group)
    : m_tty(term) {
  Save(term, save_process_group);
}

TerminalState::~TerminalState() { Restore(); }

void TerminalState::Clear() {
  m_tty.Clear();
  m_tflags = -1;
  m_data.reset();
  m_process_group = -1;
}

bool TerminalState::Save(Terminal term, bool save_process_group) {
  Clear();
  m_tty = term;
  if (m_tty.IsATerminal()) {
#if LLDB_ENABLE_POSIX
    int fd = m_tty.GetFileDescriptor();
    m_tflags = ::fcntl(fd, F_GETFL, 0);
#if LLDB_ENABLE_TERMIOS
    std::unique_ptr<Terminal::Data> new_data{new Terminal::Data()};
    if (::tcgetattr(fd, &new_data->m_termios) == 0)
      m_data = std::move(new_data);
#endif // LLDB_ENABLE_TERMIOS
    if (save_process_group)
      m_process_group = ::tcgetpgrp(fd);
#endif // LLDB_ENABLE_POSIX
  }
  return IsValid();
}

bool TerminalState::Restore() const {
#if LLDB_ENABLE_POSIX
  if (IsValid()) {
    const int fd = m_tty.GetFileDescriptor();
    if (TFlagsIsValid())
      fcntl(fd, F_SETFL, m_tflags);

#if LLDB_ENABLE_TERMIOS
    if (TTYStateIsValid())
      tcsetattr(fd, TCSANOW, &m_data->m_termios);
#endif // LLDB_ENABLE_TERMIOS

    if (ProcessGroupIsValid()) {
      // Save the original signal handler.
      void (*saved_sigttou_callback)(int) = nullptr;
      saved_sigttou_callback = (void (*)(int))signal(SIGTTOU, SIG_IGN);
      // Set the process group
      tcsetpgrp(fd, m_process_group);
      // Restore the original signal handler.
      signal(SIGTTOU, saved_sigttou_callback);
    }
    return true;
  }
#endif // LLDB_ENABLE_POSIX
  return false;
}

bool TerminalState::IsValid() const {
  return m_tty.FileDescriptorIsValid() &&
         (TFlagsIsValid() || TTYStateIsValid() || ProcessGroupIsValid());
}

bool TerminalState::TFlagsIsValid() const { return m_tflags != -1; }

bool TerminalState::TTYStateIsValid() const { return bool(m_data); }

bool TerminalState::ProcessGroupIsValid() const {
  return static_cast<int32_t>(m_process_group) != -1;
}

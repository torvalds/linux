//===-- ConnectionFileDescriptorPosix.cpp ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__APPLE__)
// Enable this special support for Apple builds where we can have unlimited
// select bounds. We tried switching to poll() and kqueue and we were panicing
// the kernel, so we have to stick with select for now.
#define _DARWIN_UNLIMITED_SELECT
#endif

#include "lldb/Host/posix/ConnectionFileDescriptorPosix.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Socket.h"
#include "lldb/Host/SocketAddress.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/SelectHelper.h"
#include "lldb/Utility/Timeout.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>

#if LLDB_ENABLE_POSIX
#include <termios.h>
#include <unistd.h>
#endif

#include <memory>
#include <sstream>

#include "llvm/Support/Errno.h"
#include "llvm/Support/ErrorHandling.h"
#if defined(__APPLE__)
#include "llvm/ADT/SmallVector.h"
#endif
#include "lldb/Host/Host.h"
#include "lldb/Host/Socket.h"
#include "lldb/Host/common/TCPSocket.h"
#include "lldb/Host/common/UDPSocket.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"

using namespace lldb;
using namespace lldb_private;

ConnectionFileDescriptor::ConnectionFileDescriptor(bool child_processes_inherit)
    : Connection(), m_pipe(), m_mutex(), m_shutting_down(false),

      m_child_processes_inherit(child_processes_inherit) {
  Log *log(GetLog(LLDBLog::Connection | LLDBLog::Object));
  LLDB_LOGF(log, "%p ConnectionFileDescriptor::ConnectionFileDescriptor ()",
            static_cast<void *>(this));
}

ConnectionFileDescriptor::ConnectionFileDescriptor(int fd, bool owns_fd)
    : Connection(), m_pipe(), m_mutex(), m_shutting_down(false),
      m_child_processes_inherit(false) {
  m_io_sp =
      std::make_shared<NativeFile>(fd, File::eOpenOptionReadWrite, owns_fd);

  Log *log(GetLog(LLDBLog::Connection | LLDBLog::Object));
  LLDB_LOGF(log,
            "%p ConnectionFileDescriptor::ConnectionFileDescriptor (fd = "
            "%i, owns_fd = %i)",
            static_cast<void *>(this), fd, owns_fd);
  OpenCommandPipe();
}

ConnectionFileDescriptor::ConnectionFileDescriptor(Socket *socket)
    : Connection(), m_pipe(), m_mutex(), m_shutting_down(false),
      m_child_processes_inherit(false) {
  InitializeSocket(socket);
}

ConnectionFileDescriptor::~ConnectionFileDescriptor() {
  Log *log(GetLog(LLDBLog::Connection | LLDBLog::Object));
  LLDB_LOGF(log, "%p ConnectionFileDescriptor::~ConnectionFileDescriptor ()",
            static_cast<void *>(this));
  Disconnect(nullptr);
  CloseCommandPipe();
}

void ConnectionFileDescriptor::OpenCommandPipe() {
  CloseCommandPipe();

  Log *log = GetLog(LLDBLog::Connection);
  // Make the command file descriptor here:
  Status result = m_pipe.CreateNew(m_child_processes_inherit);
  if (!result.Success()) {
    LLDB_LOGF(log,
              "%p ConnectionFileDescriptor::OpenCommandPipe () - could not "
              "make pipe: %s",
              static_cast<void *>(this), result.AsCString());
  } else {
    LLDB_LOGF(log,
              "%p ConnectionFileDescriptor::OpenCommandPipe() - success "
              "readfd=%d writefd=%d",
              static_cast<void *>(this), m_pipe.GetReadFileDescriptor(),
              m_pipe.GetWriteFileDescriptor());
  }
}

void ConnectionFileDescriptor::CloseCommandPipe() {
  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOGF(log, "%p ConnectionFileDescriptor::CloseCommandPipe()",
            static_cast<void *>(this));

  m_pipe.Close();
}

bool ConnectionFileDescriptor::IsConnected() const {
  return m_io_sp && m_io_sp->IsValid();
}

ConnectionStatus ConnectionFileDescriptor::Connect(llvm::StringRef path,
                                                   Status *error_ptr) {
  return Connect(
      path, [](llvm::StringRef) {}, error_ptr);
}

ConnectionStatus
ConnectionFileDescriptor::Connect(llvm::StringRef path,
                                  socket_id_callback_type socket_id_callback,
                                  Status *error_ptr) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOGF(log, "%p ConnectionFileDescriptor::Connect (url = '%s')",
            static_cast<void *>(this), path.str().c_str());

  OpenCommandPipe();

  if (path.empty()) {
    if (error_ptr)
      error_ptr->SetErrorString("invalid connect arguments");
    return eConnectionStatusError;
  }

  llvm::StringRef scheme;
  std::tie(scheme, path) = path.split("://");

  if (!path.empty()) {
    auto method =
        llvm::StringSwitch<ConnectionStatus (ConnectionFileDescriptor::*)(
            llvm::StringRef, socket_id_callback_type, Status *)>(scheme)
            .Case("listen", &ConnectionFileDescriptor::AcceptTCP)
            .Cases("accept", "unix-accept",
                   &ConnectionFileDescriptor::AcceptNamedSocket)
            .Case("unix-abstract-accept",
                  &ConnectionFileDescriptor::AcceptAbstractSocket)
            .Cases("connect", "tcp-connect",
                   &ConnectionFileDescriptor::ConnectTCP)
            .Case("udp", &ConnectionFileDescriptor::ConnectUDP)
            .Case("unix-connect", &ConnectionFileDescriptor::ConnectNamedSocket)
            .Case("unix-abstract-connect",
                  &ConnectionFileDescriptor::ConnectAbstractSocket)
#if LLDB_ENABLE_POSIX
            .Case("fd", &ConnectionFileDescriptor::ConnectFD)
            .Case("file", &ConnectionFileDescriptor::ConnectFile)
            .Case("serial", &ConnectionFileDescriptor::ConnectSerialPort)
#endif
            .Default(nullptr);

    if (method) {
      if (error_ptr)
        *error_ptr = Status();
      return (this->*method)(path, socket_id_callback, error_ptr);
    }
  }

  if (error_ptr)
    error_ptr->SetErrorStringWithFormat("unsupported connection URL: '%s'",
                                        path.str().c_str());
  return eConnectionStatusError;
}

bool ConnectionFileDescriptor::InterruptRead() {
  size_t bytes_written = 0;
  Status result = m_pipe.Write("i", 1, bytes_written);
  return result.Success();
}

ConnectionStatus ConnectionFileDescriptor::Disconnect(Status *error_ptr) {
  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOGF(log, "%p ConnectionFileDescriptor::Disconnect ()",
            static_cast<void *>(this));

  ConnectionStatus status = eConnectionStatusSuccess;

  if (!IsConnected()) {
    LLDB_LOGF(
        log, "%p ConnectionFileDescriptor::Disconnect(): Nothing to disconnect",
        static_cast<void *>(this));
    return eConnectionStatusSuccess;
  }

  // Try to get the ConnectionFileDescriptor's mutex.  If we fail, that is
  // quite likely because somebody is doing a blocking read on our file
  // descriptor.  If that's the case, then send the "q" char to the command
  // file channel so the read will wake up and the connection will then know to
  // shut down.
  std::unique_lock<std::recursive_mutex> locker(m_mutex, std::defer_lock);
  if (!locker.try_lock()) {
    if (m_pipe.CanWrite()) {
      size_t bytes_written = 0;
      Status result = m_pipe.Write("q", 1, bytes_written);
      LLDB_LOGF(log,
                "%p ConnectionFileDescriptor::Disconnect(): Couldn't get "
                "the lock, sent 'q' to %d, error = '%s'.",
                static_cast<void *>(this), m_pipe.GetWriteFileDescriptor(),
                result.AsCString());
    } else if (log) {
      LLDB_LOGF(log,
                "%p ConnectionFileDescriptor::Disconnect(): Couldn't get the "
                "lock, but no command pipe is available.",
                static_cast<void *>(this));
    }
    locker.lock();
  }

  // Prevents reads and writes during shutdown.
  m_shutting_down = true;

  Status error = m_io_sp->Close();
  if (error.Fail())
    status = eConnectionStatusError;
  if (error_ptr)
    *error_ptr = error;

  // Close any pipes we were using for async interrupts
  m_pipe.Close();

  m_uri.clear();
  m_shutting_down = false;
  return status;
}

size_t ConnectionFileDescriptor::Read(void *dst, size_t dst_len,
                                      const Timeout<std::micro> &timeout,
                                      ConnectionStatus &status,
                                      Status *error_ptr) {
  Log *log = GetLog(LLDBLog::Connection);

  std::unique_lock<std::recursive_mutex> locker(m_mutex, std::defer_lock);
  if (!locker.try_lock()) {
    LLDB_LOGF(log,
              "%p ConnectionFileDescriptor::Read () failed to get the "
              "connection lock.",
              static_cast<void *>(this));
    if (error_ptr)
      error_ptr->SetErrorString("failed to get the connection lock for read.");

    status = eConnectionStatusTimedOut;
    return 0;
  }

  if (m_shutting_down) {
    if (error_ptr)
      error_ptr->SetErrorString("shutting down");
    status = eConnectionStatusError;
    return 0;
  }

  status = BytesAvailable(timeout, error_ptr);
  if (status != eConnectionStatusSuccess)
    return 0;

  Status error;
  size_t bytes_read = dst_len;
  error = m_io_sp->Read(dst, bytes_read);

  if (log) {
    LLDB_LOGF(log,
              "%p ConnectionFileDescriptor::Read()  fd = %" PRIu64
              ", dst = %p, dst_len = %" PRIu64 ") => %" PRIu64 ", error = %s",
              static_cast<void *>(this),
              static_cast<uint64_t>(m_io_sp->GetWaitableHandle()),
              static_cast<void *>(dst), static_cast<uint64_t>(dst_len),
              static_cast<uint64_t>(bytes_read), error.AsCString());
  }

  if (bytes_read == 0) {
    error.Clear(); // End-of-file.  Do not automatically close; pass along for
                   // the end-of-file handlers.
    status = eConnectionStatusEndOfFile;
  }

  if (error_ptr)
    *error_ptr = error;

  if (error.Fail()) {
    uint32_t error_value = error.GetError();
    switch (error_value) {
    case EAGAIN: // The file was marked for non-blocking I/O, and no data were
                 // ready to be read.
      if (m_io_sp->GetFdType() == IOObject::eFDTypeSocket)
        status = eConnectionStatusTimedOut;
      else
        status = eConnectionStatusSuccess;
      return 0;

    case EFAULT:  // Buf points outside the allocated address space.
    case EINTR:   // A read from a slow device was interrupted before any data
                  // arrived by the delivery of a signal.
    case EINVAL:  // The pointer associated with fildes was negative.
    case EIO:     // An I/O error occurred while reading from the file system.
                  // The process group is orphaned.
                  // The file is a regular file, nbyte is greater than 0, the
                  // starting position is before the end-of-file, and the
                  // starting position is greater than or equal to the offset
                  // maximum established for the open file descriptor
                  // associated with fildes.
    case EISDIR:  // An attempt is made to read a directory.
    case ENOBUFS: // An attempt to allocate a memory buffer fails.
    case ENOMEM:  // Insufficient memory is available.
      status = eConnectionStatusError;
      break; // Break to close....

    case ENOENT:     // no such file or directory
    case EBADF:      // fildes is not a valid file or socket descriptor open for
                     // reading.
    case ENXIO:      // An action is requested of a device that does not exist..
                     // A requested action cannot be performed by the device.
    case ECONNRESET: // The connection is closed by the peer during a read
                     // attempt on a socket.
    case ENOTCONN:   // A read is attempted on an unconnected socket.
      status = eConnectionStatusLostConnection;
      break; // Break to close....

    case ETIMEDOUT: // A transmission timeout occurs during a read attempt on a
                    // socket.
      status = eConnectionStatusTimedOut;
      return 0;

    default:
      LLDB_LOG(log, "this = {0}, unexpected error: {1}", this,
               llvm::sys::StrError(error_value));
      status = eConnectionStatusError;
      break; // Break to close....
    }

    return 0;
  }
  return bytes_read;
}

size_t ConnectionFileDescriptor::Write(const void *src, size_t src_len,
                                       ConnectionStatus &status,
                                       Status *error_ptr) {
  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOGF(log,
            "%p ConnectionFileDescriptor::Write (src = %p, src_len = %" PRIu64
            ")",
            static_cast<void *>(this), static_cast<const void *>(src),
            static_cast<uint64_t>(src_len));

  if (!IsConnected()) {
    if (error_ptr)
      error_ptr->SetErrorString("not connected");
    status = eConnectionStatusNoConnection;
    return 0;
  }

  if (m_shutting_down) {
    if (error_ptr)
      error_ptr->SetErrorString("shutting down");
    status = eConnectionStatusError;
    return 0;
  }

  Status error;

  size_t bytes_sent = src_len;
  error = m_io_sp->Write(src, bytes_sent);

  if (log) {
    LLDB_LOGF(log,
              "%p ConnectionFileDescriptor::Write(fd = %" PRIu64
              ", src = %p, src_len = %" PRIu64 ") => %" PRIu64 " (error = %s)",
              static_cast<void *>(this),
              static_cast<uint64_t>(m_io_sp->GetWaitableHandle()),
              static_cast<const void *>(src), static_cast<uint64_t>(src_len),
              static_cast<uint64_t>(bytes_sent), error.AsCString());
  }

  if (error_ptr)
    *error_ptr = error;

  if (error.Fail()) {
    switch (error.GetError()) {
    case EAGAIN:
    case EINTR:
      status = eConnectionStatusSuccess;
      return 0;

    case ECONNRESET: // The connection is closed by the peer during a read
                     // attempt on a socket.
    case ENOTCONN:   // A read is attempted on an unconnected socket.
      status = eConnectionStatusLostConnection;
      break; // Break to close....

    default:
      status = eConnectionStatusError;
      break; // Break to close....
    }

    return 0;
  }

  status = eConnectionStatusSuccess;
  return bytes_sent;
}

std::string ConnectionFileDescriptor::GetURI() { return m_uri; }

// This ConnectionFileDescriptor::BytesAvailable() uses select() via
// SelectHelper
//
// PROS:
//  - select is consistent across most unix platforms
//  - The Apple specific version allows for unlimited fds in the fd_sets by
//    setting the _DARWIN_UNLIMITED_SELECT define prior to including the
//    required header files.
// CONS:
//  - on non-Apple platforms, only supports file descriptors up to FD_SETSIZE.
//     This implementation  will assert if it runs into that hard limit to let
//     users know that another ConnectionFileDescriptor::BytesAvailable() should
//     be used or a new version of ConnectionFileDescriptor::BytesAvailable()
//     should be written for the system that is running into the limitations.

ConnectionStatus
ConnectionFileDescriptor::BytesAvailable(const Timeout<std::micro> &timeout,
                                         Status *error_ptr) {
  // Don't need to take the mutex here separately since we are only called from
  // Read.  If we ever get used more generally we will need to lock here as
  // well.

  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOG(log, "this = {0}, timeout = {1}", this, timeout);

  // Make a copy of the file descriptors to make sure we don't have another
  // thread change these values out from under us and cause problems in the
  // loop below where like in FS_SET()
  const IOObject::WaitableHandle handle = m_io_sp->GetWaitableHandle();
  const int pipe_fd = m_pipe.GetReadFileDescriptor();

  if (handle != IOObject::kInvalidHandleValue) {
    SelectHelper select_helper;
    if (timeout)
      select_helper.SetTimeout(*timeout);

    select_helper.FDSetRead(handle);
#if defined(_WIN32)
    // select() won't accept pipes on Windows.  The entire Windows codepath
    // needs to be converted over to using WaitForMultipleObjects and event
    // HANDLEs, but for now at least this will allow ::select() to not return
    // an error.
    const bool have_pipe_fd = false;
#else
    const bool have_pipe_fd = pipe_fd >= 0;
#endif
    if (have_pipe_fd)
      select_helper.FDSetRead(pipe_fd);

    while (handle == m_io_sp->GetWaitableHandle()) {

      Status error = select_helper.Select();

      if (error_ptr)
        *error_ptr = error;

      if (error.Fail()) {
        switch (error.GetError()) {
        case EBADF: // One of the descriptor sets specified an invalid
                    // descriptor.
          return eConnectionStatusLostConnection;

        case EINVAL: // The specified time limit is invalid. One of its
                     // components is negative or too large.
        default:     // Other unknown error
          return eConnectionStatusError;

        case ETIMEDOUT:
          return eConnectionStatusTimedOut;

        case EAGAIN: // The kernel was (perhaps temporarily) unable to
                     // allocate the requested number of file descriptors, or
                     // we have non-blocking IO
        case EINTR:  // A signal was delivered before the time limit
          // expired and before any of the selected events occurred.
          break; // Lets keep reading to until we timeout
        }
      } else {
        if (select_helper.FDIsSetRead(handle))
          return eConnectionStatusSuccess;

        if (select_helper.FDIsSetRead(pipe_fd)) {
          // There is an interrupt or exit command in the command pipe Read the
          // data from that pipe:
          char c;

          ssize_t bytes_read =
              llvm::sys::RetryAfterSignal(-1, ::read, pipe_fd, &c, 1);
          assert(bytes_read == 1);
          UNUSED_IF_ASSERT_DISABLED(bytes_read);
          switch (c) {
          case 'q':
            LLDB_LOGF(log,
                      "%p ConnectionFileDescriptor::BytesAvailable() "
                      "got data: %c from the command channel.",
                      static_cast<void *>(this), c);
            return eConnectionStatusEndOfFile;
          case 'i':
            // Interrupt the current read
            return eConnectionStatusInterrupted;
          }
        }
      }
    }
  }

  if (error_ptr)
    error_ptr->SetErrorString("not connected");
  return eConnectionStatusLostConnection;
}

lldb::ConnectionStatus ConnectionFileDescriptor::AcceptSocket(
    Socket::SocketProtocol socket_protocol, llvm::StringRef socket_name,
    llvm::function_ref<void(Socket &)> post_listen_callback,
    Status *error_ptr) {
  Status error;
  std::unique_ptr<Socket> listening_socket =
      Socket::Create(socket_protocol, m_child_processes_inherit, error);
  Socket *accepted_socket;

  if (!error.Fail())
    error = listening_socket->Listen(socket_name, 5);

  if (!error.Fail()) {
    post_listen_callback(*listening_socket);
    error = listening_socket->Accept(accepted_socket);
  }

  if (!error.Fail()) {
    m_io_sp.reset(accepted_socket);
    m_uri.assign(socket_name.str());
    return eConnectionStatusSuccess;
  }

  if (error_ptr)
    *error_ptr = error;
  return eConnectionStatusError;
}

lldb::ConnectionStatus
ConnectionFileDescriptor::ConnectSocket(Socket::SocketProtocol socket_protocol,
                                        llvm::StringRef socket_name,
                                        Status *error_ptr) {
  Status error;
  std::unique_ptr<Socket> socket =
      Socket::Create(socket_protocol, m_child_processes_inherit, error);

  if (!error.Fail())
    error = socket->Connect(socket_name);

  if (!error.Fail()) {
    m_io_sp = std::move(socket);
    m_uri.assign(socket_name.str());
    return eConnectionStatusSuccess;
  }

  if (error_ptr)
    *error_ptr = error;
  return eConnectionStatusError;
}

ConnectionStatus ConnectionFileDescriptor::AcceptNamedSocket(
    llvm::StringRef socket_name, socket_id_callback_type socket_id_callback,
    Status *error_ptr) {
  return AcceptSocket(
      Socket::ProtocolUnixDomain, socket_name,
      [socket_id_callback, socket_name](Socket &listening_socket) {
        socket_id_callback(socket_name);
      },
      error_ptr);
}

ConnectionStatus ConnectionFileDescriptor::ConnectNamedSocket(
    llvm::StringRef socket_name, socket_id_callback_type socket_id_callback,
    Status *error_ptr) {
  return ConnectSocket(Socket::ProtocolUnixDomain, socket_name, error_ptr);
}

ConnectionStatus ConnectionFileDescriptor::AcceptAbstractSocket(
    llvm::StringRef socket_name, socket_id_callback_type socket_id_callback,
    Status *error_ptr) {
  return AcceptSocket(
      Socket::ProtocolUnixAbstract, socket_name,
      [socket_id_callback, socket_name](Socket &listening_socket) {
        socket_id_callback(socket_name);
      },
      error_ptr);
}

lldb::ConnectionStatus ConnectionFileDescriptor::ConnectAbstractSocket(
    llvm::StringRef socket_name, socket_id_callback_type socket_id_callback,
    Status *error_ptr) {
  return ConnectSocket(Socket::ProtocolUnixAbstract, socket_name, error_ptr);
}

ConnectionStatus
ConnectionFileDescriptor::AcceptTCP(llvm::StringRef socket_name,
                                    socket_id_callback_type socket_id_callback,
                                    Status *error_ptr) {
  ConnectionStatus ret = AcceptSocket(
      Socket::ProtocolTcp, socket_name,
      [socket_id_callback](Socket &listening_socket) {
        uint16_t port =
            static_cast<TCPSocket &>(listening_socket).GetLocalPortNumber();
        socket_id_callback(std::to_string(port));
      },
      error_ptr);
  if (ret == eConnectionStatusSuccess)
    m_uri.assign(
        static_cast<TCPSocket *>(m_io_sp.get())->GetRemoteConnectionURI());
  return ret;
}

ConnectionStatus
ConnectionFileDescriptor::ConnectTCP(llvm::StringRef socket_name,
                                     socket_id_callback_type socket_id_callback,
                                     Status *error_ptr) {
  return ConnectSocket(Socket::ProtocolTcp, socket_name, error_ptr);
}

ConnectionStatus
ConnectionFileDescriptor::ConnectUDP(llvm::StringRef s,
                                     socket_id_callback_type socket_id_callback,
                                     Status *error_ptr) {
  if (error_ptr)
    *error_ptr = Status();
  llvm::Expected<std::unique_ptr<UDPSocket>> socket =
      Socket::UdpConnect(s, m_child_processes_inherit);
  if (!socket) {
    if (error_ptr)
      *error_ptr = socket.takeError();
    else
      LLDB_LOG_ERROR(GetLog(LLDBLog::Connection), socket.takeError(),
                     "tcp connect failed: {0}");
    return eConnectionStatusError;
  }
  m_io_sp = std::move(*socket);
  m_uri.assign(std::string(s));
  return eConnectionStatusSuccess;
}

ConnectionStatus
ConnectionFileDescriptor::ConnectFD(llvm::StringRef s,
                                    socket_id_callback_type socket_id_callback,
                                    Status *error_ptr) {
#if LLDB_ENABLE_POSIX
  // Just passing a native file descriptor within this current process that
  // is already opened (possibly from a service or other source).
  int fd = -1;

  if (!s.getAsInteger(0, fd)) {
    // We have what looks to be a valid file descriptor, but we should make
    // sure it is. We currently are doing this by trying to get the flags
    // from the file descriptor and making sure it isn't a bad fd.
    errno = 0;
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1 || errno == EBADF) {
      if (error_ptr)
        error_ptr->SetErrorStringWithFormat("stale file descriptor: %s",
                                            s.str().c_str());
      m_io_sp.reset();
      return eConnectionStatusError;
    } else {
      // Don't take ownership of a file descriptor that gets passed to us
      // since someone else opened the file descriptor and handed it to us.
      // TODO: Since are using a URL to open connection we should
      // eventually parse options using the web standard where we have
      // "fd://123?opt1=value;opt2=value" and we can have an option be
      // "owns=1" or "owns=0" or something like this to allow us to specify
      // this. For now, we assume we must assume we don't own it.

      std::unique_ptr<TCPSocket> tcp_socket;
      tcp_socket = std::make_unique<TCPSocket>(fd, false, false);
      // Try and get a socket option from this file descriptor to see if
      // this is a socket and set m_is_socket accordingly.
      int resuse;
      bool is_socket =
          !!tcp_socket->GetOption(SOL_SOCKET, SO_REUSEADDR, resuse);
      if (is_socket)
        m_io_sp = std::move(tcp_socket);
      else
        m_io_sp =
            std::make_shared<NativeFile>(fd, File::eOpenOptionReadWrite, false);
      m_uri = s.str();
      return eConnectionStatusSuccess;
    }
  }

  if (error_ptr)
    error_ptr->SetErrorStringWithFormat("invalid file descriptor: \"%s\"",
                                        s.str().c_str());
  m_io_sp.reset();
  return eConnectionStatusError;
#endif // LLDB_ENABLE_POSIX
  llvm_unreachable("this function should be only called w/ LLDB_ENABLE_POSIX");
}

ConnectionStatus ConnectionFileDescriptor::ConnectFile(
    llvm::StringRef s, socket_id_callback_type socket_id_callback,
    Status *error_ptr) {
#if LLDB_ENABLE_POSIX
  std::string addr_str = s.str();
  // file:///PATH
  int fd = FileSystem::Instance().Open(addr_str.c_str(), O_RDWR);
  if (fd == -1) {
    if (error_ptr)
      error_ptr->SetErrorToErrno();
    return eConnectionStatusError;
  }

  if (::isatty(fd)) {
    // Set up serial terminal emulation
    struct termios options;
    ::tcgetattr(fd, &options);

    // Set port speed to maximum
    ::cfsetospeed(&options, B115200);
    ::cfsetispeed(&options, B115200);

    // Raw input, disable echo and signals
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    // Make sure only one character is needed to return from a read
    options.c_cc[VMIN] = 1;
    options.c_cc[VTIME] = 0;

    llvm::sys::RetryAfterSignal(-1, ::tcsetattr, fd, TCSANOW, &options);
  }

  m_io_sp = std::make_shared<NativeFile>(fd, File::eOpenOptionReadWrite, true);
  return eConnectionStatusSuccess;
#endif // LLDB_ENABLE_POSIX
  llvm_unreachable("this function should be only called w/ LLDB_ENABLE_POSIX");
}

ConnectionStatus ConnectionFileDescriptor::ConnectSerialPort(
    llvm::StringRef s, socket_id_callback_type socket_id_callback,
    Status *error_ptr) {
#if LLDB_ENABLE_POSIX
  llvm::StringRef path, qs;
  // serial:///PATH?k1=v1&k2=v2...
  std::tie(path, qs) = s.split('?');

  llvm::Expected<SerialPort::Options> serial_options =
      SerialPort::OptionsFromURL(qs);
  if (!serial_options) {
    if (error_ptr)
      *error_ptr = serial_options.takeError();
    else
      llvm::consumeError(serial_options.takeError());
    return eConnectionStatusError;
  }

  int fd = FileSystem::Instance().Open(path.str().c_str(), O_RDWR);
  if (fd == -1) {
    if (error_ptr)
      error_ptr->SetErrorToErrno();
    return eConnectionStatusError;
  }

  llvm::Expected<std::unique_ptr<SerialPort>> serial_sp = SerialPort::Create(
      fd, File::eOpenOptionReadWrite, serial_options.get(), true);
  if (!serial_sp) {
    if (error_ptr)
      *error_ptr = serial_sp.takeError();
    else
      llvm::consumeError(serial_sp.takeError());
    return eConnectionStatusError;
  }
  m_io_sp = std::move(serial_sp.get());

  return eConnectionStatusSuccess;
#endif // LLDB_ENABLE_POSIX
  llvm_unreachable("this function should be only called w/ LLDB_ENABLE_POSIX");
}

bool ConnectionFileDescriptor::GetChildProcessesInherit() const {
  return m_child_processes_inherit;
}

void ConnectionFileDescriptor::SetChildProcessesInherit(
    bool child_processes_inherit) {
  m_child_processes_inherit = child_processes_inherit;
}

void ConnectionFileDescriptor::InitializeSocket(Socket *socket) {
  m_io_sp.reset(socket);
  m_uri = socket->GetRemoteConnectionURI();
}

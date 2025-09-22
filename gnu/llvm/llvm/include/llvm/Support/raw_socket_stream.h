//===-- llvm/Support/raw_socket_stream.h - Socket streams --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains raw_ostream implementations for streams to communicate
// via UNIX sockets
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RAW_SOCKET_STREAM_H
#define LLVM_SUPPORT_RAW_SOCKET_STREAM_H

#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#include <atomic>
#include <chrono>

namespace llvm {

class raw_socket_stream;

#ifdef _WIN32
/// Ensures proper initialization and cleanup of winsock resources
///
/// Make sure that calls to WSAStartup and WSACleanup are balanced.
class WSABalancer {
public:
  WSABalancer();
  ~WSABalancer();
};
#endif // _WIN32

/// Manages a passive (i.e., listening) UNIX domain socket
///
/// The ListeningSocket class encapsulates a UNIX domain socket that can listen
/// and accept incoming connections. ListeningSocket is portable and supports
/// Windows builds begining with Insider Build 17063. ListeningSocket is
/// designed for server-side operations, working alongside \p raw_socket_streams
/// that function as client connections.
///
/// Usage example:
/// \code{.cpp}
/// std::string Path = "/path/to/socket"
/// Expected<ListeningSocket> S = ListeningSocket::createUnix(Path);
///
/// if (S) {
///   Expected<std::unique_ptr<raw_socket_stream>> connection = S->accept();
///   if (connection) {
///     // Use the accepted raw_socket_stream for communication.
///   }
/// }
/// \endcode
///
class ListeningSocket {

  std::atomic<int> FD;
  std::string SocketPath; // Not modified after construction

  /// If a separate thread calls ListeningSocket::shutdown, the ListeningSocket
  /// file descriptor (FD) could be closed while ::poll is waiting for it to be
  /// ready to perform a I/O operations. ::poll will continue to block even
  /// after FD is closed so use a self-pipe mechanism to get ::poll to return
  int PipeFD[2]; // Not modified after construction other then move constructor

  ListeningSocket(int SocketFD, StringRef SocketPath, int PipeFD[2]);

#ifdef _WIN32
  WSABalancer _;
#endif // _WIN32

public:
  ~ListeningSocket();
  ListeningSocket(ListeningSocket &&LS);
  ListeningSocket(const ListeningSocket &LS) = delete;
  ListeningSocket &operator=(const ListeningSocket &) = delete;

  /// Closes the FD, unlinks the socket file, and writes to PipeFD.
  ///
  /// After the construction of the ListeningSocket, shutdown is signal safe if
  /// it is called during the lifetime of the object. shutdown can be called
  /// concurrently with ListeningSocket::accept as writing to PipeFD will cause
  /// a blocking call to ::poll to return.
  ///
  /// Once shutdown is called there is no way to reinitialize ListeningSocket.
  void shutdown();

  /// Accepts an incoming connection on the listening socket. This method can
  /// optionally either block until a connection is available or timeout after a
  /// specified amount of time has passed. By default the method will block
  /// until the socket has recieved a connection. If the accept timesout this
  /// method will return std::errc:timed_out
  ///
  /// \param Timeout An optional timeout duration in milliseconds. Setting
  /// Timeout to a negative number causes ::accept to block indefinitely
  ///
  Expected<std::unique_ptr<raw_socket_stream>> accept(
      const std::chrono::milliseconds &Timeout = std::chrono::milliseconds(-1));

  /// Creates a listening socket bound to the specified file system path.
  /// Handles the socket creation, binding, and immediately starts listening for
  /// incoming connections.
  ///
  /// \param SocketPath The file system path where the socket will be created
  /// \param MaxBacklog The max number of connections in a socket's backlog
  ///
  static Expected<ListeningSocket> createUnix(
      StringRef SocketPath,
      int MaxBacklog = llvm::hardware_concurrency().compute_thread_count());
};

//===----------------------------------------------------------------------===//
//  raw_socket_stream
//===----------------------------------------------------------------------===//

class raw_socket_stream : public raw_fd_stream {
  uint64_t current_pos() const override { return 0; }
#ifdef _WIN32
  WSABalancer _;
#endif // _WIN32

public:
  raw_socket_stream(int SocketFD);
  ~raw_socket_stream();

  /// Create a \p raw_socket_stream connected to the UNIX domain socket at \p
  /// SocketPath.
  static Expected<std::unique_ptr<raw_socket_stream>>
  createConnectedUnix(StringRef SocketPath);

  /// Attempt to read from the raw_socket_stream's file descriptor.
  ///
  /// This method can optionally either block until data is read or an error has
  /// occurred or timeout after a specified amount of time has passed. By
  /// default the method will block until the socket has read data or
  /// encountered an error. If the read times out this method will return
  /// std::errc:timed_out
  ///
  /// \param Ptr The start of the buffer that will hold any read data
  /// \param Size The number of bytes to be read
  /// \param Timeout An optional timeout duration in milliseconds
  ///
  ssize_t read(
      char *Ptr, size_t Size,
      const std::chrono::milliseconds &Timeout = std::chrono::milliseconds(-1));
};

} // end namespace llvm

#endif

//===-- Communication.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_COMMUNICATION_H
#define LLDB_CORE_COMMUNICATION_H

#include "lldb/Utility/Timeout.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include <mutex>
#include <string>

namespace lldb_private {
class Connection;
class ConstString;
class Status;

/// \class Communication Communication.h "lldb/Core/Communication.h" An
/// abstract communications class.
///
/// Communication is an class that handles data communication between two data
/// sources. It uses a Connection class to do the real communication. This
/// approach has a couple of advantages: it allows a single instance of this
/// class to be used even though its connection can change. Connections could
/// negotiate for different connections based on abilities like starting with
/// Bluetooth and negotiating up to WiFi if available.
///
/// When using this class, all reads and writes happen synchronously on the
/// calling thread. There is also a ThreadedCommunication class that supports
/// multi-threaded mode.
class Communication {
public:
  /// Construct the Communication object.
  Communication();

  /// Destructor.
  ///
  /// The destructor is virtual since this class gets subclassed.
  virtual ~Communication();

  virtual void Clear();

  /// Connect using the current connection by passing \a url to its connect
  /// function. string.
  ///
  /// \param[in] url
  ///     A string that contains all information needed by the
  ///     subclass to connect to another client.
  ///
  /// \return
  ///     \b True if the connect succeeded, \b false otherwise. The
  ///     internal error object should be filled in with an
  ///     appropriate value based on the result of this function.
  ///
  /// \see Status& Communication::GetError ();
  /// \see bool Connection::Connect (const char *url);
  lldb::ConnectionStatus Connect(const char *url, Status *error_ptr);

  /// Disconnect the communications connection if one is currently connected.
  ///
  /// \return
  ///     \b True if the disconnect succeeded, \b false otherwise. The
  ///     internal error object should be filled in with an
  ///     appropriate value based on the result of this function.
  ///
  /// \see Status& Communication::GetError ();
  /// \see bool Connection::Disconnect ();
  virtual lldb::ConnectionStatus Disconnect(Status *error_ptr = nullptr);

  /// Check if the connection is valid.
  ///
  /// \return
  ///     \b True if this object is currently connected, \b false
  ///     otherwise.
  bool IsConnected() const;

  bool HasConnection() const;

  lldb_private::Connection *GetConnection() { return m_connection_sp.get(); }

  /// Read bytes from the current connection.
  ///
  /// If no read thread is running, this function call the connection's
  /// Connection::Read(...) function to get any available.
  ///
  /// \param[in] dst
  ///     A destination buffer that must be at least \a dst_len bytes
  ///     long.
  ///
  /// \param[in] dst_len
  ///     The number of bytes to attempt to read, and also the max
  ///     number of bytes that can be placed into \a dst.
  ///
  /// \param[in] timeout
  ///     A timeout value or std::nullopt for no timeout.
  ///
  /// \return
  ///     The number of bytes actually read.
  ///
  /// \see size_t Connection::Read (void *, size_t);
  virtual size_t Read(void *dst, size_t dst_len,
                      const Timeout<std::micro> &timeout,
                      lldb::ConnectionStatus &status, Status *error_ptr);

  /// The actual write function that attempts to write to the communications
  /// protocol.
  ///
  /// Subclasses must override this function.
  ///
  /// \param[in] src
  ///     A source buffer that must be at least \a src_len bytes
  ///     long.
  ///
  /// \param[in] src_len
  ///     The number of bytes to attempt to write, and also the
  ///     number of bytes are currently available in \a src.
  ///
  /// \return
  ///     The number of bytes actually Written.
  size_t Write(const void *src, size_t src_len, lldb::ConnectionStatus &status,
               Status *error_ptr);

  /// Repeatedly attempt writing until either \a src_len bytes are written
  /// or a permanent failure occurs.
  ///
  /// \param[in] src
  ///     A source buffer that must be at least \a src_len bytes
  ///     long.
  ///
  /// \param[in] src_len
  ///     The number of bytes to attempt to write, and also the
  ///     number of bytes are currently available in \a src.
  ///
  /// \return
  ///     The number of bytes actually Written.
  size_t WriteAll(const void *src, size_t src_len,
                  lldb::ConnectionStatus &status, Status *error_ptr);

  /// Sets the connection that it to be used by this class.
  ///
  /// By making a communication class that uses different connections it
  /// allows a single communication interface to negotiate and change its
  /// connection without any interruption to the client. It also allows the
  /// Communication class to be subclassed for packet based communication.
  ///
  /// \param[in] connection
  ///     A connection that this class will own and destroy.
  ///
  /// \see
  ///     class Connection
  virtual void SetConnection(std::unique_ptr<Connection> connection);

  static std::string ConnectionStatusAsString(lldb::ConnectionStatus status);

  bool GetCloseOnEOF() const { return m_close_on_eof; }

  void SetCloseOnEOF(bool b) { m_close_on_eof = b; }

protected:
  lldb::ConnectionSP m_connection_sp; ///< The connection that is current in use
                                      ///by this communications class.
  std::mutex
      m_write_mutex; ///< Don't let multiple threads write at the same time...
  bool m_close_on_eof;

  size_t ReadFromConnection(void *dst, size_t dst_len,
                            const Timeout<std::micro> &timeout,
                            lldb::ConnectionStatus &status, Status *error_ptr);

private:
  Communication(const Communication &) = delete;
  const Communication &operator=(const Communication &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_COMMUNICATION_H

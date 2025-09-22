//===-- IOStream.cpp --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IOStream.h"

#if defined(_WIN32)
#include <io.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <fstream>
#include <string>
#include <vector>

using namespace lldb_dap;

StreamDescriptor::StreamDescriptor() = default;

StreamDescriptor::StreamDescriptor(StreamDescriptor &&other) {
  *this = std::move(other);
}

StreamDescriptor::~StreamDescriptor() {
  if (!m_close)
    return;

  if (m_is_socket)
#if defined(_WIN32)
    ::closesocket(m_socket);
#else
    ::close(m_socket);
#endif
  else
    ::close(m_fd);
}

StreamDescriptor &StreamDescriptor::operator=(StreamDescriptor &&other) {
  m_close = other.m_close;
  other.m_close = false;
  m_is_socket = other.m_is_socket;
  if (m_is_socket)
    m_socket = other.m_socket;
  else
    m_fd = other.m_fd;
  return *this;
}

StreamDescriptor StreamDescriptor::from_socket(SOCKET s, bool close) {
  StreamDescriptor sd;
  sd.m_is_socket = true;
  sd.m_socket = s;
  sd.m_close = close;
  return sd;
}

StreamDescriptor StreamDescriptor::from_file(int fd, bool close) {
  StreamDescriptor sd;
  sd.m_is_socket = false;
  sd.m_fd = fd;
  sd.m_close = close;
  return sd;
}

bool OutputStream::write_full(llvm::StringRef str) {
  while (!str.empty()) {
    int bytes_written = 0;
    if (descriptor.m_is_socket)
      bytes_written = ::send(descriptor.m_socket, str.data(), str.size(), 0);
    else
      bytes_written = ::write(descriptor.m_fd, str.data(), str.size());

    if (bytes_written < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      return false;
    }
    str = str.drop_front(bytes_written);
  }

  return true;
}

bool InputStream::read_full(std::ofstream *log, size_t length,
                            std::string &text) {
  std::string data;
  data.resize(length);

  char *ptr = &data[0];
  while (length != 0) {
    int bytes_read = 0;
    if (descriptor.m_is_socket)
      bytes_read = ::recv(descriptor.m_socket, ptr, length, 0);
    else
      bytes_read = ::read(descriptor.m_fd, ptr, length);

    if (bytes_read == 0) {
      if (log)
        *log << "End of file (EOF) reading from input file.\n";
      return false;
    }
    if (bytes_read < 0) {
      int reason = 0;
#if defined(_WIN32)
      if (descriptor.m_is_socket)
        reason = WSAGetLastError();
      else
        reason = errno;
#else
      reason = errno;
      if (reason == EINTR || reason == EAGAIN)
        continue;
#endif

      if (log)
        *log << "Error " << reason << " reading from input file.\n";
      return false;
    }

    assert(bytes_read >= 0 && (size_t)bytes_read <= length);
    ptr += bytes_read;
    length -= bytes_read;
  }
  text += data;
  return true;
}

bool InputStream::read_line(std::ofstream *log, std::string &line) {
  line.clear();
  while (true) {
    if (!read_full(log, 1, line))
      return false;

    if (llvm::StringRef(line).ends_with("\r\n"))
      break;
  }
  line.erase(line.size() - 2);
  return true;
}

bool InputStream::read_expected(std::ofstream *log, llvm::StringRef expected) {
  std::string result;
  if (!read_full(log, expected.size(), result))
    return false;
  if (expected != result) {
    if (log)
      *log << "Warning: Expected '" << expected.str() << "', got '" << result
           << "\n";
  }
  return true;
}

//===-- IOStream.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_IOSTREAM_H
#define LLDB_TOOLS_LLDB_DAP_IOSTREAM_H

#include "llvm/Config/llvm-config.h" // for LLVM_ON_UNIX

#if defined(_WIN32)
// We need to #define NOMINMAX in order to skip `min()` and `max()` macro
// definitions that conflict with other system headers.
// We also need to #undef GetObject (which is defined to GetObjectW) because
// the JSON code we use also has methods named `GetObject()` and we conflict
// against these.
#define NOMINMAX
#include <windows.h>
#else
typedef int SOCKET;
#endif

#include "llvm/ADT/StringRef.h"

#include <fstream>
#include <string>

// Windows requires different system calls for dealing with sockets and other
// types of files, so we can't simply have one code path that just uses read
// and write everywhere.  So we need an abstraction in order to allow us to
// treat them identically.
namespace lldb_dap {
struct StreamDescriptor {
  StreamDescriptor();
  ~StreamDescriptor();
  StreamDescriptor(StreamDescriptor &&other);

  StreamDescriptor &operator=(StreamDescriptor &&other);

  static StreamDescriptor from_socket(SOCKET s, bool close);
  static StreamDescriptor from_file(int fd, bool close);

  bool m_is_socket = false;
  bool m_close = false;
  union {
    int m_fd;
    SOCKET m_socket;
  };
};

struct InputStream {
  StreamDescriptor descriptor;

  bool read_full(std::ofstream *log, size_t length, std::string &text);

  bool read_line(std::ofstream *log, std::string &line);

  bool read_expected(std::ofstream *log, llvm::StringRef expected);
};

struct OutputStream {
  StreamDescriptor descriptor;

  bool write_full(llvm::StringRef str);
};
} // namespace lldb_dap

#endif

//===-- StreamAsynchronousIO.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/StreamAsynchronousIO.h"

#include "lldb/Core/Debugger.h"
#include "lldb/lldb-enumerations.h"

using namespace lldb;
using namespace lldb_private;

StreamAsynchronousIO::StreamAsynchronousIO(Debugger &debugger, bool for_stdout,
                                           bool colors)
    : Stream(0, 4, eByteOrderBig, colors), m_debugger(debugger), m_data(),
      m_for_stdout(for_stdout) {}

StreamAsynchronousIO::~StreamAsynchronousIO() {
  // Flush when we destroy to make sure we display the data
  Flush();
}

void StreamAsynchronousIO::Flush() {
  if (!m_data.empty()) {
    m_debugger.PrintAsync(m_data.data(), m_data.size(), m_for_stdout);
    m_data = std::string();
  }
}

size_t StreamAsynchronousIO::WriteImpl(const void *s, size_t length) {
  m_data.append((const char *)s, length);
  return length;
}

//===-- StreamAsynchronousIO.h -----------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_STREAMASYNCHRONOUSIO_H
#define LLDB_CORE_STREAMASYNCHRONOUSIO_H

#include "lldb/Utility/Stream.h"

#include <string>

#include <cstddef>

namespace lldb_private {
class Debugger;

class StreamAsynchronousIO : public Stream {
public:
  StreamAsynchronousIO(Debugger &debugger, bool for_stdout, bool colors);

  ~StreamAsynchronousIO() override;

  void Flush() override;

protected:
  size_t WriteImpl(const void *src, size_t src_len) override;

private:
  Debugger &m_debugger;
  std::string m_data;
  bool m_for_stdout;
};

} // namespace lldb_private

#endif // LLDB_CORE_STREAMASYNCHRONOUSIO_H

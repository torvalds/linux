//===-- StreamString.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_STREAMSTRING_H
#define LLDB_UTILITY_STREAMSTRING_H

#include "lldb/Utility/Stream.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/StringRef.h"

#include <string>

#include <cstddef>
#include <cstdint>

namespace lldb_private {

class ScriptInterpreter;

class StreamString : public Stream {
public:
  StreamString(bool colors = false);

  StreamString(uint32_t flags, uint32_t addr_size, lldb::ByteOrder byte_order);

  ~StreamString() override;

  void Flush() override;

  void Clear();

  bool Empty() const;

  size_t GetSize() const;

  size_t GetSizeOfLastLine() const;

  llvm::StringRef GetString() const;

  const char *GetData() const { return m_packet.c_str(); }

  void FillLastLineToColumn(uint32_t column, char fill_char);

protected:
  friend class ScriptInterpreter;

  std::string m_packet;
  size_t WriteImpl(const void *s, size_t length) override;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_STREAMSTRING_H

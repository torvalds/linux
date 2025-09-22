//===-- TempFile.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace lldb_fuzzer {

class TempFile {
public:
  TempFile() = default;
  ~TempFile();

  static std::unique_ptr<TempFile> Create(uint8_t *data, size_t size);
  llvm::StringRef GetPath() { return m_path.str(); }

private:
  llvm::SmallString<128> m_path;
};

} // namespace lldb_fuzzer

//===-- llvm/Support/TarWriter.h - Tar archive file creator -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TARWRITER_H
#define LLVM_SUPPORT_TARWRITER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class TarWriter {
public:
  static Expected<std::unique_ptr<TarWriter>> create(StringRef OutputPath,
                                                     StringRef BaseDir);

  void append(StringRef Path, StringRef Data);

private:
  TarWriter(int FD, StringRef BaseDir);
  raw_fd_ostream OS;
  std::string BaseDir;
  StringSet<> Files;
};
}

#endif

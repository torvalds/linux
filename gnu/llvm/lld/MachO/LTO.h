//===- LTO.h ----------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_LTO_H
#define LLD_MACHO_LTO_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <vector>

namespace llvm::lto {
class LTO;
} // namespace llvm::lto

namespace lld::macho {

class BitcodeFile;
class ObjFile;

class BitcodeCompiler {
public:
  BitcodeCompiler();

  void add(BitcodeFile &f);
  std::vector<ObjFile *> compile();

private:
  std::unique_ptr<llvm::lto::LTO> ltoObj;
  std::vector<llvm::SmallString<0>> buf;
  std::vector<std::unique_ptr<llvm::MemoryBuffer>> files;
  std::unique_ptr<llvm::raw_fd_ostream> indexFile;
  llvm::DenseSet<StringRef> thinIndices;
  bool hasFiles = false;
};

} // namespace lld::macho

#endif

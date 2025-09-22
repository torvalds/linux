//===- LTO.h ----------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a way to combine bitcode files into one wasm
// file by compiling them using LLVM.
//
// If LTO is in use, your input files are not in regular wasm files
// but instead LLVM bitcode files. In that case, the linker has to
// convert bitcode files into the native format so that we can create
// a wasm file that contains native code. This file provides that
// functionality.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_WASM_LTO_H
#define LLD_WASM_LTO_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/SmallString.h"
#include "Writer.h"
#include <memory>
#include <vector>

namespace llvm {
namespace lto {
class LTO;
}
} // namespace llvm

namespace lld::wasm {

class BitcodeFile;
class InputFile;

class BitcodeCompiler {
public:
  BitcodeCompiler();
  ~BitcodeCompiler();

  void add(BitcodeFile &f);
  std::vector<StringRef> compile();

private:
  std::unique_ptr<llvm::lto::LTO> ltoObj;
  std::vector<SmallString<0>> buf;
  std::vector<std::unique_ptr<MemoryBuffer>> files;
};
} // namespace lld::wasm

#endif

//===- Disassembler.h - Text File Disassembler ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements the disassembler of strings of bytes written in
// hexadecimal, from standard input or from a file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MC_DISASSEMBLER_H
#define LLVM_TOOLS_LLVM_MC_DISASSEMBLER_H

#include <string>

namespace llvm {

class MemoryBuffer;
class Target;
class raw_ostream;
class SourceMgr;
class MCContext;
class MCSubtargetInfo;
class MCStreamer;
class MCTargetOptions;

class Disassembler {
public:
  static int disassemble(const Target &T, const std::string &Triple,
                         MCSubtargetInfo &STI, MCStreamer &Streamer,
                         MemoryBuffer &Buffer, SourceMgr &SM, MCContext &Ctx,
                         const MCTargetOptions &MCOptions);
};

} // namespace llvm

#endif

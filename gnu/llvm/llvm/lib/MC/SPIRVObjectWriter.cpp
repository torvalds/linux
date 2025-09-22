//===- llvm/MC/MCSPIRVObjectWriter.cpp - SPIR-V Object Writer ----*- C++ *-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCSPIRVObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/EndianStream.h"

using namespace llvm;

void SPIRVObjectWriter::writeHeader(const MCAssembler &Asm) {
  constexpr uint32_t MagicNumber = 0x07230203;
  constexpr uint32_t GeneratorID = 43;
  constexpr uint32_t GeneratorMagicNumber =
      (GeneratorID << 16) | (LLVM_VERSION_MAJOR);
  constexpr uint32_t Schema = 0;

  W.write<uint32_t>(MagicNumber);
  W.write<uint32_t>((VersionInfo.Major << 16) | (VersionInfo.Minor << 8));
  W.write<uint32_t>(GeneratorMagicNumber);
  W.write<uint32_t>(VersionInfo.Bound);
  W.write<uint32_t>(Schema);
}

void SPIRVObjectWriter::setBuildVersion(unsigned Major, unsigned Minor,
                                        unsigned Bound) {
  VersionInfo.Major = Major;
  VersionInfo.Minor = Minor;
  VersionInfo.Bound = Bound;
}

uint64_t SPIRVObjectWriter::writeObject(MCAssembler &Asm) {
  uint64_t StartOffset = W.OS.tell();
  writeHeader(Asm);
  for (const MCSection &S : Asm)
    Asm.writeSectionData(W.OS, &S);
  return W.OS.tell() - StartOffset;
}

std::unique_ptr<MCObjectWriter>
llvm::createSPIRVObjectWriter(std::unique_ptr<MCSPIRVObjectTargetWriter> MOTW,
                              raw_pwrite_stream &OS) {
  return std::make_unique<SPIRVObjectWriter>(std::move(MOTW), OS);
}

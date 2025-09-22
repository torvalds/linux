//===- FileWriter.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/GSYM/FileWriter.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;
using namespace gsym;

FileWriter::~FileWriter() { OS.flush(); }

void FileWriter::writeSLEB(int64_t S) {
  uint8_t Bytes[32];
  auto Length = encodeSLEB128(S, Bytes);
  assert(Length < sizeof(Bytes));
  OS.write(reinterpret_cast<const char *>(Bytes), Length);
}

void FileWriter::writeULEB(uint64_t U) {
  uint8_t Bytes[32];
  auto Length = encodeULEB128(U, Bytes);
  assert(Length < sizeof(Bytes));
  OS.write(reinterpret_cast<const char *>(Bytes), Length);
}

void FileWriter::writeU8(uint8_t U) {
  OS.write(reinterpret_cast<const char *>(&U), sizeof(U));
}

void FileWriter::writeU16(uint16_t U) {
  const uint16_t Swapped = support::endian::byte_swap(U, ByteOrder);
  OS.write(reinterpret_cast<const char *>(&Swapped), sizeof(Swapped));
}

void FileWriter::writeU32(uint32_t U) {
  const uint32_t Swapped = support::endian::byte_swap(U, ByteOrder);
  OS.write(reinterpret_cast<const char *>(&Swapped), sizeof(Swapped));
}

void FileWriter::writeU64(uint64_t U) {
  const uint64_t Swapped = support::endian::byte_swap(U, ByteOrder);
  OS.write(reinterpret_cast<const char *>(&Swapped), sizeof(Swapped));
}

void FileWriter::fixup32(uint32_t U, uint64_t Offset) {
  const uint32_t Swapped = support::endian::byte_swap(U, ByteOrder);
  OS.pwrite(reinterpret_cast<const char *>(&Swapped), sizeof(Swapped),
            Offset);
}

void FileWriter::writeData(llvm::ArrayRef<uint8_t> Data) {
  OS.write(reinterpret_cast<const char *>(Data.data()), Data.size());
}

void FileWriter::writeNullTerminated(llvm::StringRef Str) {
  OS << Str << '\0';
}

uint64_t FileWriter::tell() {
  return OS.tell();
}

void FileWriter::alignTo(size_t Align) {
  off_t Offset = OS.tell();
  off_t AlignedOffset = (Offset + Align - 1) / Align * Align;
  if (AlignedOffset == Offset)
    return;
  off_t PadCount = AlignedOffset - Offset;
  OS.write_zeros(PadCount);
}

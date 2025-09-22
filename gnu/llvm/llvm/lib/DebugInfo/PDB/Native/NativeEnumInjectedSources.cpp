//==- NativeEnumInjectedSources.cpp - Native Injected Source Enumerator --*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeEnumInjectedSources.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/HashTable.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTable.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"

namespace llvm {
namespace pdb {

namespace {

Expected<std::string> readStreamData(BinaryStream &Stream, uint64_t Limit) {
  uint64_t Offset = 0, DataLength = std::min(Limit, Stream.getLength());
  std::string Result;
  Result.reserve(DataLength);
  while (Offset < DataLength) {
    ArrayRef<uint8_t> Data;
    if (auto E = Stream.readLongestContiguousChunk(Offset, Data))
      return std::move(E);
    Data = Data.take_front(DataLength - Offset);
    Offset += Data.size();
    Result += toStringRef(Data);
  }
  return Result;
}

class NativeInjectedSource final : public IPDBInjectedSource {
  const SrcHeaderBlockEntry &Entry;
  const PDBStringTable &Strings;
  PDBFile &File;

public:
  NativeInjectedSource(const SrcHeaderBlockEntry &Entry,
                       PDBFile &File, const PDBStringTable &Strings)
      : Entry(Entry), Strings(Strings), File(File) {}

  uint32_t getCrc32() const override { return Entry.CRC; }
  uint64_t getCodeByteSize() const override { return Entry.FileSize; }

  std::string getFileName() const override {
    StringRef Ret = cantFail(Strings.getStringForID(Entry.FileNI),
                             "InjectedSourceStream should have rejected this");
    return std::string(Ret);
  }

  std::string getObjectFileName() const override {
    StringRef Ret = cantFail(Strings.getStringForID(Entry.ObjNI),
                             "InjectedSourceStream should have rejected this");
    return std::string(Ret);
  }

  std::string getVirtualFileName() const override {
    StringRef Ret = cantFail(Strings.getStringForID(Entry.VFileNI),
                             "InjectedSourceStream should have rejected this");
    return std::string(Ret);
  }

  uint32_t getCompression() const override { return Entry.Compression; }

  std::string getCode() const override {
    // Get name of stream storing the data.
    StringRef VName =
        cantFail(Strings.getStringForID(Entry.VFileNI),
                 "InjectedSourceStream should have rejected this");
    std::string StreamName = ("/src/files/" + VName).str();

    // Find stream with that name and read its data.
    // FIXME: Consider validating (or even loading) all this in
    // InjectedSourceStream so that no error can happen here.
    auto ExpectedFileStream = File.safelyCreateNamedStream(StreamName);
    if (!ExpectedFileStream) {
      consumeError(ExpectedFileStream.takeError());
      return "(failed to open data stream)";
    }

    auto Data = readStreamData(**ExpectedFileStream, Entry.FileSize);
    if (!Data) {
      consumeError(Data.takeError());
      return "(failed to read data)";
    }
    return *Data;
  }
};

} // namespace

NativeEnumInjectedSources::NativeEnumInjectedSources(
    PDBFile &File, const InjectedSourceStream &IJS,
    const PDBStringTable &Strings)
    : File(File), Stream(IJS), Strings(Strings), Cur(Stream.begin()) {}

uint32_t NativeEnumInjectedSources::getChildCount() const {
  return static_cast<uint32_t>(Stream.size());
}

std::unique_ptr<IPDBInjectedSource>
NativeEnumInjectedSources::getChildAtIndex(uint32_t N) const {
  if (N >= getChildCount())
    return nullptr;
  return std::make_unique<NativeInjectedSource>(std::next(Stream.begin(), N)->second,
                                           File, Strings);
}

std::unique_ptr<IPDBInjectedSource> NativeEnumInjectedSources::getNext() {
  if (Cur == Stream.end())
    return nullptr;
  return std::make_unique<NativeInjectedSource>((Cur++)->second, File, Strings);
}

void NativeEnumInjectedSources::reset() { Cur = Stream.begin(); }

}
}

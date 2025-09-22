//===- LinePrinter.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/LinePrinter.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/Native/InputFile.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/UDTLayout.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Regex.h"

#include <algorithm>

using namespace llvm;
using namespace llvm::msf;
using namespace llvm::pdb;

namespace {
bool IsItemExcluded(llvm::StringRef Item,
                    std::list<llvm::Regex> &IncludeFilters,
                    std::list<llvm::Regex> &ExcludeFilters) {
  if (Item.empty())
    return false;

  auto match_pred = [Item](llvm::Regex &R) { return R.match(Item); };

  // Include takes priority over exclude.  If the user specified include
  // filters, and none of them include this item, them item is gone.
  if (!IncludeFilters.empty() && !any_of(IncludeFilters, match_pred))
    return true;

  if (any_of(ExcludeFilters, match_pred))
    return true;

  return false;
}
} // namespace

using namespace llvm;

LinePrinter::LinePrinter(int Indent, bool UseColor, llvm::raw_ostream &Stream,
                         const FilterOptions &Filters)
    : OS(Stream), IndentSpaces(Indent), CurrentIndent(0), UseColor(UseColor),
      Filters(Filters) {
  SetFilters(ExcludeTypeFilters, Filters.ExcludeTypes.begin(),
             Filters.ExcludeTypes.end());
  SetFilters(ExcludeSymbolFilters, Filters.ExcludeSymbols.begin(),
             Filters.ExcludeSymbols.end());
  SetFilters(ExcludeCompilandFilters, Filters.ExcludeCompilands.begin(),
             Filters.ExcludeCompilands.end());

  SetFilters(IncludeTypeFilters, Filters.IncludeTypes.begin(),
             Filters.IncludeTypes.end());
  SetFilters(IncludeSymbolFilters, Filters.IncludeSymbols.begin(),
             Filters.IncludeSymbols.end());
  SetFilters(IncludeCompilandFilters, Filters.IncludeCompilands.begin(),
             Filters.IncludeCompilands.end());
}

void LinePrinter::Indent(uint32_t Amount) {
  if (Amount == 0)
    Amount = IndentSpaces;
  CurrentIndent += Amount;
}

void LinePrinter::Unindent(uint32_t Amount) {
  if (Amount == 0)
    Amount = IndentSpaces;
  CurrentIndent = std::max<int>(0, CurrentIndent - Amount);
}

void LinePrinter::NewLine() {
  OS << "\n";
  OS.indent(CurrentIndent);
}

void LinePrinter::print(const Twine &T) { OS << T; }

void LinePrinter::printLine(const Twine &T) {
  NewLine();
  OS << T;
}

bool LinePrinter::IsClassExcluded(const ClassLayout &Class) {
  if (IsTypeExcluded(Class.getName(), Class.getSize()))
    return true;
  if (Class.deepPaddingSize() < Filters.PaddingThreshold)
    return true;
  return false;
}

void LinePrinter::formatBinary(StringRef Label, ArrayRef<uint8_t> Data,
                               uint64_t StartOffset) {
  NewLine();
  OS << Label << " (";
  if (!Data.empty()) {
    OS << "\n";
    OS << format_bytes_with_ascii(Data, StartOffset, 32, 4,
                                  CurrentIndent + IndentSpaces, true);
    NewLine();
  }
  OS << ")";
}

void LinePrinter::formatBinary(StringRef Label, ArrayRef<uint8_t> Data,
                               uint64_t Base, uint64_t StartOffset) {
  NewLine();
  OS << Label << " (";
  if (!Data.empty()) {
    OS << "\n";
    Base += StartOffset;
    OS << format_bytes_with_ascii(Data, Base, 32, 4,
                                  CurrentIndent + IndentSpaces, true);
    NewLine();
  }
  OS << ")";
}

namespace {
struct Run {
  Run() = default;
  explicit Run(uint32_t Block) : Block(Block) {}
  uint32_t Block = 0;
  uint64_t ByteLen = 0;
};
} // namespace

static std::vector<Run> computeBlockRuns(uint32_t BlockSize,
                                         const msf::MSFStreamLayout &Layout) {
  std::vector<Run> Runs;
  if (Layout.Length == 0)
    return Runs;

  ArrayRef<support::ulittle32_t> Blocks = Layout.Blocks;
  assert(!Blocks.empty());
  uint64_t StreamBytesRemaining = Layout.Length;
  uint32_t CurrentBlock = Blocks[0];
  Runs.emplace_back(CurrentBlock);
  while (!Blocks.empty()) {
    Run *CurrentRun = &Runs.back();
    uint32_t NextBlock = Blocks.front();
    if (NextBlock < CurrentBlock || (NextBlock - CurrentBlock > 1)) {
      Runs.emplace_back(NextBlock);
      CurrentRun = &Runs.back();
    }
    uint64_t Used =
        std::min(static_cast<uint64_t>(BlockSize), StreamBytesRemaining);
    CurrentRun->ByteLen += Used;
    StreamBytesRemaining -= Used;
    CurrentBlock = NextBlock;
    Blocks = Blocks.drop_front();
  }
  return Runs;
}

static std::pair<Run, uint64_t> findRun(uint64_t Offset, ArrayRef<Run> Runs) {
  for (const auto &R : Runs) {
    if (Offset < R.ByteLen)
      return std::make_pair(R, Offset);
    Offset -= R.ByteLen;
  }
  llvm_unreachable("Invalid offset!");
}

void LinePrinter::formatMsfStreamData(StringRef Label, PDBFile &File,
                                      uint32_t StreamIdx,
                                      StringRef StreamPurpose, uint64_t Offset,
                                      uint64_t Size) {
  if (StreamIdx >= File.getNumStreams()) {
    formatLine("Stream {0}: Not present", StreamIdx);
    return;
  }
  if (Size + Offset > File.getStreamByteSize(StreamIdx)) {
    formatLine(
        "Stream {0}: Invalid offset and size, range out of stream bounds",
        StreamIdx);
    return;
  }

  auto S = File.createIndexedStream(StreamIdx);
  if (!S) {
    NewLine();
    formatLine("Stream {0}: Not present", StreamIdx);
    return;
  }

  uint64_t End =
      (Size == 0) ? S->getLength() : std::min(Offset + Size, S->getLength());
  Size = End - Offset;

  formatLine("Stream {0}: {1} (dumping {2:N} / {3:N} bytes)", StreamIdx,
             StreamPurpose, Size, S->getLength());
  AutoIndent Indent(*this);
  BinaryStreamRef Slice(*S);
  BinarySubstreamRef Substream;
  Substream.Offset = Offset;
  Substream.StreamData = Slice.drop_front(Offset).keep_front(Size);

  auto Layout = File.getStreamLayout(StreamIdx);
  formatMsfStreamData(Label, File, Layout, Substream);
}

void LinePrinter::formatMsfStreamData(StringRef Label, PDBFile &File,
                                      const msf::MSFStreamLayout &Stream,
                                      BinarySubstreamRef Substream) {
  BinaryStreamReader Reader(Substream.StreamData);

  auto Runs = computeBlockRuns(File.getBlockSize(), Stream);

  NewLine();
  OS << Label << " (";
  while (Reader.bytesRemaining() > 0) {
    OS << "\n";

    Run FoundRun;
    uint64_t RunOffset;
    std::tie(FoundRun, RunOffset) = findRun(Substream.Offset, Runs);
    assert(FoundRun.ByteLen >= RunOffset);
    uint64_t Len = FoundRun.ByteLen - RunOffset;
    Len = std::min(Len, Reader.bytesRemaining());
    uint64_t Base = FoundRun.Block * File.getBlockSize() + RunOffset;
    ArrayRef<uint8_t> Data;
    consumeError(Reader.readBytes(Data, Len));
    OS << format_bytes_with_ascii(Data, Base, 32, 4,
                                  CurrentIndent + IndentSpaces, true);
    if (Reader.bytesRemaining() > 0) {
      NewLine();
      OS << formatv("  {0}",
                    fmt_align("<discontinuity>", AlignStyle::Center, 114, '-'));
    }
    Substream.Offset += Len;
  }
  NewLine();
  OS << ")";
}

void LinePrinter::formatMsfStreamBlocks(
    PDBFile &File, const msf::MSFStreamLayout &StreamLayout) {
  auto Blocks = ArrayRef(StreamLayout.Blocks);
  uint64_t L = StreamLayout.Length;

  while (L > 0) {
    NewLine();
    assert(!Blocks.empty());
    OS << formatv("Block {0} (\n", uint32_t(Blocks.front()));
    uint64_t UsedBytes =
        std::min(L, static_cast<uint64_t>(File.getBlockSize()));
    ArrayRef<uint8_t> BlockData =
        cantFail(File.getBlockData(Blocks.front(), File.getBlockSize()));
    uint64_t BaseOffset = Blocks.front();
    BaseOffset *= File.getBlockSize();
    OS << format_bytes_with_ascii(BlockData, BaseOffset, 32, 4,
                                  CurrentIndent + IndentSpaces, true);
    NewLine();
    OS << ")";
    NewLine();
    L -= UsedBytes;
    Blocks = Blocks.drop_front();
  }
}

bool LinePrinter::IsTypeExcluded(llvm::StringRef TypeName, uint64_t Size) {
  if (IsItemExcluded(TypeName, IncludeTypeFilters, ExcludeTypeFilters))
    return true;
  if (Size < Filters.SizeThreshold)
    return true;
  return false;
}

bool LinePrinter::IsSymbolExcluded(llvm::StringRef SymbolName) {
  return IsItemExcluded(SymbolName, IncludeSymbolFilters, ExcludeSymbolFilters);
}

bool LinePrinter::IsCompilandExcluded(llvm::StringRef CompilandName) {
  return IsItemExcluded(CompilandName, IncludeCompilandFilters,
                        ExcludeCompilandFilters);
}

WithColor::WithColor(LinePrinter &P, PDB_ColorItem C)
    : OS(P.OS), UseColor(P.hasColor()) {
  if (UseColor)
    applyColor(C);
}

WithColor::~WithColor() {
  if (UseColor)
    OS.resetColor();
}

void WithColor::applyColor(PDB_ColorItem C) {
  switch (C) {
  case PDB_ColorItem::None:
    OS.resetColor();
    return;
  case PDB_ColorItem::Comment:
    OS.changeColor(raw_ostream::GREEN, false);
    return;
  case PDB_ColorItem::Address:
    OS.changeColor(raw_ostream::YELLOW, /*bold=*/true);
    return;
  case PDB_ColorItem::Keyword:
    OS.changeColor(raw_ostream::MAGENTA, true);
    return;
  case PDB_ColorItem::Register:
  case PDB_ColorItem::Offset:
    OS.changeColor(raw_ostream::YELLOW, false);
    return;
  case PDB_ColorItem::Type:
    OS.changeColor(raw_ostream::CYAN, true);
    return;
  case PDB_ColorItem::Identifier:
    OS.changeColor(raw_ostream::CYAN, false);
    return;
  case PDB_ColorItem::Path:
    OS.changeColor(raw_ostream::CYAN, false);
    return;
  case PDB_ColorItem::Padding:
  case PDB_ColorItem::SectionHeader:
    OS.changeColor(raw_ostream::RED, true);
    return;
  case PDB_ColorItem::LiteralValue:
    OS.changeColor(raw_ostream::GREEN, true);
    return;
  }
}

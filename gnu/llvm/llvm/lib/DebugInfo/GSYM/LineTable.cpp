//===- LineTable.cpp --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/GSYM/LineTable.h"
#include "llvm/DebugInfo/GSYM/FileWriter.h"
#include "llvm/Support/DataExtractor.h"

using namespace llvm;
using namespace gsym;

enum LineTableOpCode {
  EndSequence = 0x00,  ///< End of the line table.
  SetFile = 0x01,      ///< Set LineTableRow.file_idx, don't push a row.
  AdvancePC = 0x02,    ///< Increment LineTableRow.address, and push a row.
  AdvanceLine = 0x03,  ///< Set LineTableRow.file_line, don't push a row.
  FirstSpecial = 0x04, ///< All special opcodes push a row.
};

struct DeltaInfo {
  int64_t Delta;
  uint32_t Count;
  DeltaInfo(int64_t D, uint32_t C) : Delta(D), Count(C) {}
};

inline bool operator<(const DeltaInfo &LHS, int64_t Delta) {
  return LHS.Delta < Delta;
}

static bool encodeSpecial(int64_t MinLineDelta, int64_t MaxLineDelta,
                          int64_t LineDelta, uint64_t AddrDelta,
                          uint8_t &SpecialOp) {
  if (LineDelta < MinLineDelta)
    return false;
  if (LineDelta > MaxLineDelta)
    return false;
  int64_t LineRange = MaxLineDelta - MinLineDelta + 1;
  int64_t AdjustedOp = ((LineDelta - MinLineDelta) + AddrDelta * LineRange);
  int64_t Op = AdjustedOp + FirstSpecial;
  if (Op < 0)
    return false;
  if (Op > 255)
    return false;
  SpecialOp = (uint8_t)Op;
  return true;
}

typedef std::function<bool(const LineEntry &Row)> LineEntryCallback;

static llvm::Error parse(DataExtractor &Data, uint64_t BaseAddr,
                         LineEntryCallback const &Callback) {
  uint64_t Offset = 0;
  if (!Data.isValidOffset(Offset))
    return createStringError(std::errc::io_error,
        "0x%8.8" PRIx64 ": missing LineTable MinDelta", Offset);
  int64_t MinDelta = Data.getSLEB128(&Offset);
  if (!Data.isValidOffset(Offset))
    return createStringError(std::errc::io_error,
        "0x%8.8" PRIx64 ": missing LineTable MaxDelta", Offset);
  int64_t MaxDelta = Data.getSLEB128(&Offset);
  int64_t LineRange = MaxDelta - MinDelta + 1;
  if (!Data.isValidOffset(Offset))
    return createStringError(std::errc::io_error,
        "0x%8.8" PRIx64 ": missing LineTable FirstLine", Offset);
  const uint32_t FirstLine = (uint32_t)Data.getULEB128(&Offset);
  LineEntry Row(BaseAddr, 1, FirstLine);
  bool Done = false;
  while (!Done) {
    if (!Data.isValidOffset(Offset))
      return createStringError(std::errc::io_error,
          "0x%8.8" PRIx64 ": EOF found before EndSequence", Offset);
    uint8_t Op = Data.getU8(&Offset);
    switch (Op) {
    case EndSequence:
      Done = true;
      break;
    case SetFile:
      if (!Data.isValidOffset(Offset))
        return createStringError(std::errc::io_error,
            "0x%8.8" PRIx64 ": EOF found before SetFile value",
            Offset);
      Row.File = (uint32_t)Data.getULEB128(&Offset);
      break;
    case AdvancePC:
      if (!Data.isValidOffset(Offset))
        return createStringError(std::errc::io_error,
            "0x%8.8" PRIx64 ": EOF found before AdvancePC value",
            Offset);
      Row.Addr += Data.getULEB128(&Offset);
      // If the function callback returns false, we stop parsing.
      if (Callback(Row) == false)
        return Error::success();
      break;
    case AdvanceLine:
      if (!Data.isValidOffset(Offset))
        return createStringError(std::errc::io_error,
            "0x%8.8" PRIx64 ": EOF found before AdvanceLine value",
            Offset);
      Row.Line += Data.getSLEB128(&Offset);
      break;
    default: {
        // A byte that contains both address and line increment.
        uint8_t AdjustedOp = Op - FirstSpecial;
        int64_t LineDelta = MinDelta + (AdjustedOp % LineRange);
        uint64_t AddrDelta = (AdjustedOp / LineRange);
        Row.Line += LineDelta;
        Row.Addr += AddrDelta;
        // If the function callback returns false, we stop parsing.
        if (Callback(Row) == false)
          return Error::success();
        break;
      }
    }
  }
  return Error::success();
}

llvm::Error LineTable::encode(FileWriter &Out, uint64_t BaseAddr) const {
  // Users must verify the LineTable is valid prior to calling this funtion.
  // We don't want to emit any LineTable objects if they are not valid since
  // it will waste space in the GSYM file.
  if (!isValid())
    return createStringError(std::errc::invalid_argument,
                             "attempted to encode invalid LineTable object");

  int64_t MinLineDelta = INT64_MAX;
  int64_t MaxLineDelta = INT64_MIN;
  std::vector<DeltaInfo> DeltaInfos;
  if (Lines.size() == 1) {
    MinLineDelta = 0;
    MaxLineDelta = 0;
  } else {
    int64_t PrevLine = 1;
    bool First = true;
    for (const auto &line_entry : Lines) {
      if (First)
        First = false;
      else {
        int64_t LineDelta = (int64_t)line_entry.Line - PrevLine;
        auto End = DeltaInfos.end();
        auto Pos = std::lower_bound(DeltaInfos.begin(), End, LineDelta);
        if (Pos != End && Pos->Delta == LineDelta)
          ++Pos->Count;
        else
          DeltaInfos.insert(Pos, DeltaInfo(LineDelta, 1));
        if (LineDelta < MinLineDelta)
          MinLineDelta = LineDelta;
        if (LineDelta > MaxLineDelta)
          MaxLineDelta = LineDelta;
      }
      PrevLine = (int64_t)line_entry.Line;
    }
    assert(MinLineDelta <= MaxLineDelta);
  }
  // Set the min and max line delta intelligently based on the counts of
  // the line deltas. if our range is too large.
  const int64_t MaxLineRange = 14;
  if (MaxLineDelta - MinLineDelta > MaxLineRange) {
    uint32_t BestIndex = 0;
    uint32_t BestEndIndex = 0;
    uint32_t BestCount = 0;
    const size_t NumDeltaInfos = DeltaInfos.size();
    for (uint32_t I = 0; I < NumDeltaInfos; ++I) {
      const int64_t FirstDelta = DeltaInfos[I].Delta;
      uint32_t CurrCount = 0;
      uint32_t J;
      for (J = I; J < NumDeltaInfos; ++J) {
        auto LineRange = DeltaInfos[J].Delta - FirstDelta;
        if (LineRange > MaxLineRange)
          break;
        CurrCount += DeltaInfos[J].Count;
      }
      if (CurrCount > BestCount) {
        BestIndex = I;
        BestEndIndex = J - 1;
        BestCount = CurrCount;
      }
    }
    MinLineDelta = DeltaInfos[BestIndex].Delta;
    MaxLineDelta = DeltaInfos[BestEndIndex].Delta;
  }
  if (MinLineDelta == MaxLineDelta && MinLineDelta > 0 &&
      MinLineDelta < MaxLineRange)
    MinLineDelta = 0;
  assert(MinLineDelta <= MaxLineDelta);

  // Initialize the line entry state as a starting point. All line entries
  // will be deltas from this.
  LineEntry Prev(BaseAddr, 1, Lines.front().Line);

  // Write out the min and max line delta as signed LEB128.
  Out.writeSLEB(MinLineDelta);
  Out.writeSLEB(MaxLineDelta);
  // Write out the starting line number as a unsigned LEB128.
  Out.writeULEB(Prev.Line);

  for (const auto &Curr : Lines) {
    if (Curr.Addr < BaseAddr)
      return createStringError(std::errc::invalid_argument,
                               "LineEntry has address 0x%" PRIx64 " which is "
                               "less than the function start address 0x%"
                               PRIx64, Curr.Addr, BaseAddr);
    if (Curr.Addr < Prev.Addr)
      return createStringError(std::errc::invalid_argument,
                               "LineEntry in LineTable not in ascending order");
    const uint64_t AddrDelta = Curr.Addr - Prev.Addr;
    int64_t LineDelta = 0;
    if (Curr.Line > Prev.Line)
      LineDelta = Curr.Line - Prev.Line;
    else if (Prev.Line > Curr.Line)
      LineDelta = -((int32_t)(Prev.Line - Curr.Line));

    // Set the file if it doesn't match the current one.
    if (Curr.File != Prev.File) {
      Out.writeU8(SetFile);
      Out.writeULEB(Curr.File);
    }

    uint8_t SpecialOp;
    if (encodeSpecial(MinLineDelta, MaxLineDelta, LineDelta, AddrDelta,
                      SpecialOp)) {
      // Advance the PC and line and push a row.
      Out.writeU8(SpecialOp);
    } else {
      // We can't encode the address delta and line delta into
      // a single special opcode, we must do them separately.

      // Advance the line.
      if (LineDelta != 0) {
        Out.writeU8(AdvanceLine);
        Out.writeSLEB(LineDelta);
      }

      // Advance the PC and push a row.
      Out.writeU8(AdvancePC);
      Out.writeULEB(AddrDelta);
    }
    Prev = Curr;
  }
  Out.writeU8(EndSequence);
  return Error::success();
}

// Parse all line table entries into the "LineTable" vector. We can
// cache the results of this if needed, or we can call LineTable::lookup()
// below.
llvm::Expected<LineTable> LineTable::decode(DataExtractor &Data,
                                            uint64_t BaseAddr) {
  LineTable LT;
  llvm::Error Err = parse(Data, BaseAddr, [&](const LineEntry &Row) -> bool {
    LT.Lines.push_back(Row);
    return true; // Keep parsing by returning true.
  });
  if (Err)
    return std::move(Err);
  return LT;
}
// Parse the line table on the fly and find the row we are looking for.
// We will need to determine if we need to cache the line table by calling
// LineTable::parseAllEntries(...) or just call this function each time.
// There is a CPU vs memory tradeoff we will need to determined.
Expected<LineEntry> LineTable::lookup(DataExtractor &Data, uint64_t BaseAddr, uint64_t Addr) {
  LineEntry Result;
  llvm::Error Err = parse(Data, BaseAddr,
                          [Addr, &Result](const LineEntry &Row) -> bool {
    if (Addr < Row.Addr)
      return false; // Stop parsing, result contains the line table row!
    Result = Row;
    return true; // Keep parsing till we find the right row.
  });
  if (Err)
    return std::move(Err);
  if (Result.isValid())
    return Result;
  return createStringError(std::errc::invalid_argument,
                           "address 0x%" PRIx64 " is not in the line table",
                           Addr);
}

raw_ostream &llvm::gsym::operator<<(raw_ostream &OS, const LineTable &LT) {
  for (const auto &LineEntry : LT)
    OS << LineEntry << '\n';
  return OS;
}

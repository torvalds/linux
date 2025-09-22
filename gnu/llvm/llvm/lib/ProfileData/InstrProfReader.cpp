//===- InstrProfReader.cpp - Instrumented profiling reader ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for reading profiling data for clang's
// instrumentation based PGO and coverage.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/MemProf.h"
#include "llvm/ProfileData/ProfileCommon.h"
#include "llvm/ProfileData/SymbolRemappingReader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SwapByteOrder.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;

// Extracts the variant information from the top 32 bits in the version and
// returns an enum specifying the variants present.
static InstrProfKind getProfileKindFromVersion(uint64_t Version) {
  InstrProfKind ProfileKind = InstrProfKind::Unknown;
  if (Version & VARIANT_MASK_IR_PROF) {
    ProfileKind |= InstrProfKind::IRInstrumentation;
  }
  if (Version & VARIANT_MASK_CSIR_PROF) {
    ProfileKind |= InstrProfKind::ContextSensitive;
  }
  if (Version & VARIANT_MASK_INSTR_ENTRY) {
    ProfileKind |= InstrProfKind::FunctionEntryInstrumentation;
  }
  if (Version & VARIANT_MASK_BYTE_COVERAGE) {
    ProfileKind |= InstrProfKind::SingleByteCoverage;
  }
  if (Version & VARIANT_MASK_FUNCTION_ENTRY_ONLY) {
    ProfileKind |= InstrProfKind::FunctionEntryOnly;
  }
  if (Version & VARIANT_MASK_MEMPROF) {
    ProfileKind |= InstrProfKind::MemProf;
  }
  if (Version & VARIANT_MASK_TEMPORAL_PROF) {
    ProfileKind |= InstrProfKind::TemporalProfile;
  }
  return ProfileKind;
}

static Expected<std::unique_ptr<MemoryBuffer>>
setupMemoryBuffer(const Twine &Filename, vfs::FileSystem &FS) {
  auto BufferOrErr = Filename.str() == "-" ? MemoryBuffer::getSTDIN()
                                           : FS.getBufferForFile(Filename);
  if (std::error_code EC = BufferOrErr.getError())
    return errorCodeToError(EC);
  return std::move(BufferOrErr.get());
}

static Error initializeReader(InstrProfReader &Reader) {
  return Reader.readHeader();
}

/// Read a list of binary ids from a profile that consist of
/// a. uint64_t binary id length
/// b. uint8_t  binary id data
/// c. uint8_t  padding (if necessary)
/// This function is shared between raw and indexed profiles.
/// Raw profiles are in host-endian format, and indexed profiles are in
/// little-endian format. So, this function takes an argument indicating the
/// associated endian format to read the binary ids correctly.
static Error
readBinaryIdsInternal(const MemoryBuffer &DataBuffer,
                      ArrayRef<uint8_t> BinaryIdsBuffer,
                      std::vector<llvm::object::BuildID> &BinaryIds,
                      const llvm::endianness Endian) {
  using namespace support;

  const uint64_t BinaryIdsSize = BinaryIdsBuffer.size();
  const uint8_t *BinaryIdsStart = BinaryIdsBuffer.data();

  if (BinaryIdsSize == 0)
    return Error::success();

  const uint8_t *BI = BinaryIdsStart;
  const uint8_t *BIEnd = BinaryIdsStart + BinaryIdsSize;
  const uint8_t *End =
      reinterpret_cast<const uint8_t *>(DataBuffer.getBufferEnd());

  while (BI < BIEnd) {
    size_t Remaining = BIEnd - BI;
    // There should be enough left to read the binary id length.
    if (Remaining < sizeof(uint64_t))
      return make_error<InstrProfError>(
          instrprof_error::malformed,
          "not enough data to read binary id length");

    uint64_t BILen = endian::readNext<uint64_t>(BI, Endian);
    if (BILen == 0)
      return make_error<InstrProfError>(instrprof_error::malformed,
                                        "binary id length is 0");

    Remaining = BIEnd - BI;
    // There should be enough left to read the binary id data.
    if (Remaining < alignToPowerOf2(BILen, sizeof(uint64_t)))
      return make_error<InstrProfError>(
          instrprof_error::malformed, "not enough data to read binary id data");

    // Add binary id to the binary ids list.
    BinaryIds.push_back(object::BuildID(BI, BI + BILen));

    // Increment by binary id data length, which aligned to the size of uint64.
    BI += alignToPowerOf2(BILen, sizeof(uint64_t));
    if (BI > End)
      return make_error<InstrProfError>(
          instrprof_error::malformed,
          "binary id section is greater than buffer size");
  }

  return Error::success();
}

static void printBinaryIdsInternal(raw_ostream &OS,
                                   ArrayRef<llvm::object::BuildID> BinaryIds) {
  OS << "Binary IDs: \n";
  for (const auto &BI : BinaryIds) {
    for (auto I : BI)
      OS << format("%02x", I);
    OS << "\n";
  }
}

Expected<std::unique_ptr<InstrProfReader>>
InstrProfReader::create(const Twine &Path, vfs::FileSystem &FS,
                        const InstrProfCorrelator *Correlator,
                        std::function<void(Error)> Warn) {
  // Set up the buffer to read.
  auto BufferOrError = setupMemoryBuffer(Path, FS);
  if (Error E = BufferOrError.takeError())
    return std::move(E);
  return InstrProfReader::create(std::move(BufferOrError.get()), Correlator,
                                 Warn);
}

Expected<std::unique_ptr<InstrProfReader>>
InstrProfReader::create(std::unique_ptr<MemoryBuffer> Buffer,
                        const InstrProfCorrelator *Correlator,
                        std::function<void(Error)> Warn) {
  if (Buffer->getBufferSize() == 0)
    return make_error<InstrProfError>(instrprof_error::empty_raw_profile);

  std::unique_ptr<InstrProfReader> Result;
  // Create the reader.
  if (IndexedInstrProfReader::hasFormat(*Buffer))
    Result.reset(new IndexedInstrProfReader(std::move(Buffer)));
  else if (RawInstrProfReader64::hasFormat(*Buffer))
    Result.reset(new RawInstrProfReader64(std::move(Buffer), Correlator, Warn));
  else if (RawInstrProfReader32::hasFormat(*Buffer))
    Result.reset(new RawInstrProfReader32(std::move(Buffer), Correlator, Warn));
  else if (TextInstrProfReader::hasFormat(*Buffer))
    Result.reset(new TextInstrProfReader(std::move(Buffer)));
  else
    return make_error<InstrProfError>(instrprof_error::unrecognized_format);

  // Initialize the reader and return the result.
  if (Error E = initializeReader(*Result))
    return std::move(E);

  return std::move(Result);
}

Expected<std::unique_ptr<IndexedInstrProfReader>>
IndexedInstrProfReader::create(const Twine &Path, vfs::FileSystem &FS,
                               const Twine &RemappingPath) {
  // Set up the buffer to read.
  auto BufferOrError = setupMemoryBuffer(Path, FS);
  if (Error E = BufferOrError.takeError())
    return std::move(E);

  // Set up the remapping buffer if requested.
  std::unique_ptr<MemoryBuffer> RemappingBuffer;
  std::string RemappingPathStr = RemappingPath.str();
  if (!RemappingPathStr.empty()) {
    auto RemappingBufferOrError = setupMemoryBuffer(RemappingPathStr, FS);
    if (Error E = RemappingBufferOrError.takeError())
      return std::move(E);
    RemappingBuffer = std::move(RemappingBufferOrError.get());
  }

  return IndexedInstrProfReader::create(std::move(BufferOrError.get()),
                                        std::move(RemappingBuffer));
}

Expected<std::unique_ptr<IndexedInstrProfReader>>
IndexedInstrProfReader::create(std::unique_ptr<MemoryBuffer> Buffer,
                               std::unique_ptr<MemoryBuffer> RemappingBuffer) {
  // Create the reader.
  if (!IndexedInstrProfReader::hasFormat(*Buffer))
    return make_error<InstrProfError>(instrprof_error::bad_magic);
  auto Result = std::make_unique<IndexedInstrProfReader>(
      std::move(Buffer), std::move(RemappingBuffer));

  // Initialize the reader and return the result.
  if (Error E = initializeReader(*Result))
    return std::move(E);

  return std::move(Result);
}

bool TextInstrProfReader::hasFormat(const MemoryBuffer &Buffer) {
  // Verify that this really looks like plain ASCII text by checking a
  // 'reasonable' number of characters (up to profile magic size).
  size_t count = std::min(Buffer.getBufferSize(), sizeof(uint64_t));
  StringRef buffer = Buffer.getBufferStart();
  return count == 0 ||
         std::all_of(buffer.begin(), buffer.begin() + count,
                     [](char c) { return isPrint(c) || isSpace(c); });
}

// Read the profile variant flag from the header: ":FE" means this is a FE
// generated profile. ":IR" means this is an IR level profile. Other strings
// with a leading ':' will be reported an error format.
Error TextInstrProfReader::readHeader() {
  Symtab.reset(new InstrProfSymtab());

  while (Line->starts_with(":")) {
    StringRef Str = Line->substr(1);
    if (Str.equals_insensitive("ir"))
      ProfileKind |= InstrProfKind::IRInstrumentation;
    else if (Str.equals_insensitive("fe"))
      ProfileKind |= InstrProfKind::FrontendInstrumentation;
    else if (Str.equals_insensitive("csir")) {
      ProfileKind |= InstrProfKind::IRInstrumentation;
      ProfileKind |= InstrProfKind::ContextSensitive;
    } else if (Str.equals_insensitive("entry_first"))
      ProfileKind |= InstrProfKind::FunctionEntryInstrumentation;
    else if (Str.equals_insensitive("not_entry_first"))
      ProfileKind &= ~InstrProfKind::FunctionEntryInstrumentation;
    else if (Str.equals_insensitive("single_byte_coverage"))
      ProfileKind |= InstrProfKind::SingleByteCoverage;
    else if (Str.equals_insensitive("temporal_prof_traces")) {
      ProfileKind |= InstrProfKind::TemporalProfile;
      if (auto Err = readTemporalProfTraceData())
        return error(std::move(Err));
    } else
      return error(instrprof_error::bad_header);
    ++Line;
  }
  return success();
}

/// Temporal profile trace data is stored in the header immediately after
/// ":temporal_prof_traces". The first integer is the number of traces, the
/// second integer is the stream size, then the following lines are the actual
/// traces which consist of a weight and a comma separated list of function
/// names.
Error TextInstrProfReader::readTemporalProfTraceData() {
  if ((++Line).is_at_end())
    return error(instrprof_error::eof);

  uint32_t NumTraces;
  if (Line->getAsInteger(0, NumTraces))
    return error(instrprof_error::malformed);

  if ((++Line).is_at_end())
    return error(instrprof_error::eof);

  if (Line->getAsInteger(0, TemporalProfTraceStreamSize))
    return error(instrprof_error::malformed);

  for (uint32_t i = 0; i < NumTraces; i++) {
    if ((++Line).is_at_end())
      return error(instrprof_error::eof);

    TemporalProfTraceTy Trace;
    if (Line->getAsInteger(0, Trace.Weight))
      return error(instrprof_error::malformed);

    if ((++Line).is_at_end())
      return error(instrprof_error::eof);

    SmallVector<StringRef> FuncNames;
    Line->split(FuncNames, ",", /*MaxSplit=*/-1, /*KeepEmpty=*/false);
    for (auto &FuncName : FuncNames)
      Trace.FunctionNameRefs.push_back(
          IndexedInstrProf::ComputeHash(FuncName.trim()));
    TemporalProfTraces.push_back(std::move(Trace));
  }
  return success();
}

Error
TextInstrProfReader::readValueProfileData(InstrProfRecord &Record) {

#define CHECK_LINE_END(Line)                                                   \
  if (Line.is_at_end())                                                        \
    return error(instrprof_error::truncated);
#define READ_NUM(Str, Dst)                                                     \
  if ((Str).getAsInteger(10, (Dst)))                                           \
    return error(instrprof_error::malformed);
#define VP_READ_ADVANCE(Val)                                                   \
  CHECK_LINE_END(Line);                                                        \
  uint32_t Val;                                                                \
  READ_NUM((*Line), (Val));                                                    \
  Line++;

  if (Line.is_at_end())
    return success();

  uint32_t NumValueKinds;
  if (Line->getAsInteger(10, NumValueKinds)) {
    // No value profile data
    return success();
  }
  if (NumValueKinds == 0 || NumValueKinds > IPVK_Last + 1)
    return error(instrprof_error::malformed,
                 "number of value kinds is invalid");
  Line++;

  for (uint32_t VK = 0; VK < NumValueKinds; VK++) {
    VP_READ_ADVANCE(ValueKind);
    if (ValueKind > IPVK_Last)
      return error(instrprof_error::malformed, "value kind is invalid");
    ;
    VP_READ_ADVANCE(NumValueSites);
    if (!NumValueSites)
      continue;

    Record.reserveSites(VK, NumValueSites);
    for (uint32_t S = 0; S < NumValueSites; S++) {
      VP_READ_ADVANCE(NumValueData);

      std::vector<InstrProfValueData> CurrentValues;
      for (uint32_t V = 0; V < NumValueData; V++) {
        CHECK_LINE_END(Line);
        std::pair<StringRef, StringRef> VD = Line->rsplit(':');
        uint64_t TakenCount, Value;
        if (ValueKind == IPVK_IndirectCallTarget) {
          if (InstrProfSymtab::isExternalSymbol(VD.first)) {
            Value = 0;
          } else {
            if (Error E = Symtab->addFuncName(VD.first))
              return E;
            Value = IndexedInstrProf::ComputeHash(VD.first);
          }
        } else if (ValueKind == IPVK_VTableTarget) {
          if (InstrProfSymtab::isExternalSymbol(VD.first))
            Value = 0;
          else {
            if (Error E = Symtab->addVTableName(VD.first))
              return E;
            Value = IndexedInstrProf::ComputeHash(VD.first);
          }
        } else {
          READ_NUM(VD.first, Value);
        }
        READ_NUM(VD.second, TakenCount);
        CurrentValues.push_back({Value, TakenCount});
        Line++;
      }
      assert(CurrentValues.size() == NumValueData);
      Record.addValueData(ValueKind, S, CurrentValues, nullptr);
    }
  }
  return success();

#undef CHECK_LINE_END
#undef READ_NUM
#undef VP_READ_ADVANCE
}

Error TextInstrProfReader::readNextRecord(NamedInstrProfRecord &Record) {
  // Skip empty lines and comments.
  while (!Line.is_at_end() && (Line->empty() || Line->starts_with("#")))
    ++Line;
  // If we hit EOF while looking for a name, we're done.
  if (Line.is_at_end()) {
    return error(instrprof_error::eof);
  }

  // Read the function name.
  Record.Name = *Line++;
  if (Error E = Symtab->addFuncName(Record.Name))
    return error(std::move(E));

  // Read the function hash.
  if (Line.is_at_end())
    return error(instrprof_error::truncated);
  if ((Line++)->getAsInteger(0, Record.Hash))
    return error(instrprof_error::malformed,
                 "function hash is not a valid integer");

  // Read the number of counters.
  uint64_t NumCounters;
  if (Line.is_at_end())
    return error(instrprof_error::truncated);
  if ((Line++)->getAsInteger(10, NumCounters))
    return error(instrprof_error::malformed,
                 "number of counters is not a valid integer");
  if (NumCounters == 0)
    return error(instrprof_error::malformed, "number of counters is zero");

  // Read each counter and fill our internal storage with the values.
  Record.Clear();
  Record.Counts.reserve(NumCounters);
  for (uint64_t I = 0; I < NumCounters; ++I) {
    if (Line.is_at_end())
      return error(instrprof_error::truncated);
    uint64_t Count;
    if ((Line++)->getAsInteger(10, Count))
      return error(instrprof_error::malformed, "count is invalid");
    Record.Counts.push_back(Count);
  }

  // Bitmap byte information is indicated with special character.
  if (Line->starts_with("$")) {
    Record.BitmapBytes.clear();
    // Read the number of bitmap bytes.
    uint64_t NumBitmapBytes;
    if ((Line++)->drop_front(1).trim().getAsInteger(0, NumBitmapBytes))
      return error(instrprof_error::malformed,
                   "number of bitmap bytes is not a valid integer");
    if (NumBitmapBytes != 0) {
      // Read each bitmap and fill our internal storage with the values.
      Record.BitmapBytes.reserve(NumBitmapBytes);
      for (uint8_t I = 0; I < NumBitmapBytes; ++I) {
        if (Line.is_at_end())
          return error(instrprof_error::truncated);
        uint8_t BitmapByte;
        if ((Line++)->getAsInteger(0, BitmapByte))
          return error(instrprof_error::malformed,
                       "bitmap byte is not a valid integer");
        Record.BitmapBytes.push_back(BitmapByte);
      }
    }
  }

  // Check if value profile data exists and read it if so.
  if (Error E = readValueProfileData(Record))
    return error(std::move(E));

  return success();
}

template <class IntPtrT>
InstrProfKind RawInstrProfReader<IntPtrT>::getProfileKind() const {
  return getProfileKindFromVersion(Version);
}

template <class IntPtrT>
SmallVector<TemporalProfTraceTy> &
RawInstrProfReader<IntPtrT>::getTemporalProfTraces(
    std::optional<uint64_t> Weight) {
  if (TemporalProfTimestamps.empty()) {
    assert(TemporalProfTraces.empty());
    return TemporalProfTraces;
  }
  // Sort functions by their timestamps to build the trace.
  std::sort(TemporalProfTimestamps.begin(), TemporalProfTimestamps.end());
  TemporalProfTraceTy Trace;
  if (Weight)
    Trace.Weight = *Weight;
  for (auto &[TimestampValue, NameRef] : TemporalProfTimestamps)
    Trace.FunctionNameRefs.push_back(NameRef);
  TemporalProfTraces = {std::move(Trace)};
  return TemporalProfTraces;
}

template <class IntPtrT>
bool RawInstrProfReader<IntPtrT>::hasFormat(const MemoryBuffer &DataBuffer) {
  if (DataBuffer.getBufferSize() < sizeof(uint64_t))
    return false;
  uint64_t Magic =
    *reinterpret_cast<const uint64_t *>(DataBuffer.getBufferStart());
  return RawInstrProf::getMagic<IntPtrT>() == Magic ||
         llvm::byteswap(RawInstrProf::getMagic<IntPtrT>()) == Magic;
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readHeader() {
  if (!hasFormat(*DataBuffer))
    return error(instrprof_error::bad_magic);
  if (DataBuffer->getBufferSize() < sizeof(RawInstrProf::Header))
    return error(instrprof_error::bad_header);
  auto *Header = reinterpret_cast<const RawInstrProf::Header *>(
      DataBuffer->getBufferStart());
  ShouldSwapBytes = Header->Magic != RawInstrProf::getMagic<IntPtrT>();
  return readHeader(*Header);
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readNextHeader(const char *CurrentPos) {
  const char *End = DataBuffer->getBufferEnd();
  // Skip zero padding between profiles.
  while (CurrentPos != End && *CurrentPos == 0)
    ++CurrentPos;
  // If there's nothing left, we're done.
  if (CurrentPos == End)
    return make_error<InstrProfError>(instrprof_error::eof);
  // If there isn't enough space for another header, this is probably just
  // garbage at the end of the file.
  if (CurrentPos + sizeof(RawInstrProf::Header) > End)
    return make_error<InstrProfError>(instrprof_error::malformed,
                                      "not enough space for another header");
  // The writer ensures each profile is padded to start at an aligned address.
  if (reinterpret_cast<size_t>(CurrentPos) % alignof(uint64_t))
    return make_error<InstrProfError>(instrprof_error::malformed,
                                      "insufficient padding");
  // The magic should have the same byte order as in the previous header.
  uint64_t Magic = *reinterpret_cast<const uint64_t *>(CurrentPos);
  if (Magic != swap(RawInstrProf::getMagic<IntPtrT>()))
    return make_error<InstrProfError>(instrprof_error::bad_magic);

  // There's another profile to read, so we need to process the header.
  auto *Header = reinterpret_cast<const RawInstrProf::Header *>(CurrentPos);
  return readHeader(*Header);
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::createSymtab(InstrProfSymtab &Symtab) {
  if (Error E = Symtab.create(StringRef(NamesStart, NamesEnd - NamesStart),
                              StringRef(VNamesStart, VNamesEnd - VNamesStart)))
    return error(std::move(E));
  for (const RawInstrProf::ProfileData<IntPtrT> *I = Data; I != DataEnd; ++I) {
    const IntPtrT FPtr = swap(I->FunctionPointer);
    if (!FPtr)
      continue;
    Symtab.mapAddress(FPtr, swap(I->NameRef));
  }

  if (VTableBegin != nullptr && VTableEnd != nullptr) {
    for (const RawInstrProf::VTableProfileData<IntPtrT> *I = VTableBegin;
         I != VTableEnd; ++I) {
      const IntPtrT VPtr = swap(I->VTablePointer);
      if (!VPtr)
        continue;
      // Map both begin and end address to the name hash, since the instrumented
      // address could be somewhere in the middle.
      // VPtr is of type uint32_t or uint64_t so 'VPtr + I->VTableSize' marks
      // the end of vtable address.
      Symtab.mapVTableAddress(VPtr, VPtr + swap(I->VTableSize),
                              swap(I->VTableNameHash));
    }
  }
  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readHeader(
    const RawInstrProf::Header &Header) {
  Version = swap(Header.Version);
  if (GET_VERSION(Version) != RawInstrProf::Version)
    return error(instrprof_error::raw_profile_version_mismatch,
                 ("Profile uses raw profile format version = " +
                  Twine(GET_VERSION(Version)) +
                  "; expected version = " + Twine(RawInstrProf::Version) +
                  "\nPLEASE update this tool to version in the raw profile, or "
                  "regenerate raw profile with expected version.")
                     .str());

  uint64_t BinaryIdSize = swap(Header.BinaryIdsSize);
  // Binary id start just after the header if exists.
  const uint8_t *BinaryIdStart =
      reinterpret_cast<const uint8_t *>(&Header) + sizeof(RawInstrProf::Header);
  const uint8_t *BinaryIdEnd = BinaryIdStart + BinaryIdSize;
  const uint8_t *BufferEnd = (const uint8_t *)DataBuffer->getBufferEnd();
  if (BinaryIdSize % sizeof(uint64_t) || BinaryIdEnd > BufferEnd)
    return error(instrprof_error::bad_header);
  ArrayRef<uint8_t> BinaryIdsBuffer(BinaryIdStart, BinaryIdSize);
  if (!BinaryIdsBuffer.empty()) {
    if (Error Err = readBinaryIdsInternal(*DataBuffer, BinaryIdsBuffer,
                                          BinaryIds, getDataEndianness()))
      return Err;
  }

  CountersDelta = swap(Header.CountersDelta);
  BitmapDelta = swap(Header.BitmapDelta);
  NamesDelta = swap(Header.NamesDelta);
  auto NumData = swap(Header.NumData);
  auto PaddingBytesBeforeCounters = swap(Header.PaddingBytesBeforeCounters);
  auto CountersSize = swap(Header.NumCounters) * getCounterTypeSize();
  auto PaddingBytesAfterCounters = swap(Header.PaddingBytesAfterCounters);
  auto NumBitmapBytes = swap(Header.NumBitmapBytes);
  auto PaddingBytesAfterBitmapBytes = swap(Header.PaddingBytesAfterBitmapBytes);
  auto NamesSize = swap(Header.NamesSize);
  auto VTableNameSize = swap(Header.VNamesSize);
  auto NumVTables = swap(Header.NumVTables);
  ValueKindLast = swap(Header.ValueKindLast);

  auto DataSize = NumData * sizeof(RawInstrProf::ProfileData<IntPtrT>);
  auto PaddingBytesAfterNames = getNumPaddingBytes(NamesSize);
  auto PaddingBytesAfterVTableNames = getNumPaddingBytes(VTableNameSize);

  auto VTableSectionSize =
      NumVTables * sizeof(RawInstrProf::VTableProfileData<IntPtrT>);
  auto PaddingBytesAfterVTableProfData = getNumPaddingBytes(VTableSectionSize);

  // Profile data starts after profile header and binary ids if exist.
  ptrdiff_t DataOffset = sizeof(RawInstrProf::Header) + BinaryIdSize;
  ptrdiff_t CountersOffset = DataOffset + DataSize + PaddingBytesBeforeCounters;
  ptrdiff_t BitmapOffset =
      CountersOffset + CountersSize + PaddingBytesAfterCounters;
  ptrdiff_t NamesOffset =
      BitmapOffset + NumBitmapBytes + PaddingBytesAfterBitmapBytes;
  ptrdiff_t VTableProfDataOffset =
      NamesOffset + NamesSize + PaddingBytesAfterNames;
  ptrdiff_t VTableNameOffset = VTableProfDataOffset + VTableSectionSize +
                               PaddingBytesAfterVTableProfData;
  ptrdiff_t ValueDataOffset =
      VTableNameOffset + VTableNameSize + PaddingBytesAfterVTableNames;

  auto *Start = reinterpret_cast<const char *>(&Header);
  if (Start + ValueDataOffset > DataBuffer->getBufferEnd())
    return error(instrprof_error::bad_header);

  if (Correlator) {
    // These sizes in the raw file are zero because we constructed them in the
    // Correlator.
    if (!(DataSize == 0 && NamesSize == 0 && CountersDelta == 0 &&
          NamesDelta == 0))
      return error(instrprof_error::unexpected_correlation_info);
    Data = Correlator->getDataPointer();
    DataEnd = Data + Correlator->getDataSize();
    NamesStart = Correlator->getNamesPointer();
    NamesEnd = NamesStart + Correlator->getNamesSize();
  } else {
    Data = reinterpret_cast<const RawInstrProf::ProfileData<IntPtrT> *>(
        Start + DataOffset);
    DataEnd = Data + NumData;
    VTableBegin =
        reinterpret_cast<const RawInstrProf::VTableProfileData<IntPtrT> *>(
            Start + VTableProfDataOffset);
    VTableEnd = VTableBegin + NumVTables;
    NamesStart = Start + NamesOffset;
    NamesEnd = NamesStart + NamesSize;
    VNamesStart = Start + VTableNameOffset;
    VNamesEnd = VNamesStart + VTableNameSize;
  }

  CountersStart = Start + CountersOffset;
  CountersEnd = CountersStart + CountersSize;
  BitmapStart = Start + BitmapOffset;
  BitmapEnd = BitmapStart + NumBitmapBytes;
  ValueDataStart = reinterpret_cast<const uint8_t *>(Start + ValueDataOffset);

  std::unique_ptr<InstrProfSymtab> NewSymtab = std::make_unique<InstrProfSymtab>();
  if (Error E = createSymtab(*NewSymtab))
    return E;

  Symtab = std::move(NewSymtab);
  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readName(NamedInstrProfRecord &Record) {
  Record.Name = getName(Data->NameRef);
  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readFuncHash(NamedInstrProfRecord &Record) {
  Record.Hash = swap(Data->FuncHash);
  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readRawCounts(
    InstrProfRecord &Record) {
  uint32_t NumCounters = swap(Data->NumCounters);
  if (NumCounters == 0)
    return error(instrprof_error::malformed, "number of counters is zero");

  ptrdiff_t CounterBaseOffset = swap(Data->CounterPtr) - CountersDelta;
  if (CounterBaseOffset < 0)
    return error(
        instrprof_error::malformed,
        ("counter offset " + Twine(CounterBaseOffset) + " is negative").str());

  if (CounterBaseOffset >= CountersEnd - CountersStart)
    return error(instrprof_error::malformed,
                 ("counter offset " + Twine(CounterBaseOffset) +
                  " is greater than the maximum counter offset " +
                  Twine(CountersEnd - CountersStart - 1))
                     .str());

  uint64_t MaxNumCounters =
      (CountersEnd - (CountersStart + CounterBaseOffset)) /
      getCounterTypeSize();
  if (NumCounters > MaxNumCounters)
    return error(instrprof_error::malformed,
                 ("number of counters " + Twine(NumCounters) +
                  " is greater than the maximum number of counters " +
                  Twine(MaxNumCounters))
                     .str());

  Record.Counts.clear();
  Record.Counts.reserve(NumCounters);
  for (uint32_t I = 0; I < NumCounters; I++) {
    const char *Ptr =
        CountersStart + CounterBaseOffset + I * getCounterTypeSize();
    if (I == 0 && hasTemporalProfile()) {
      uint64_t TimestampValue = swap(*reinterpret_cast<const uint64_t *>(Ptr));
      if (TimestampValue != 0 &&
          TimestampValue != std::numeric_limits<uint64_t>::max()) {
        TemporalProfTimestamps.emplace_back(TimestampValue,
                                            swap(Data->NameRef));
        TemporalProfTraceStreamSize = 1;
      }
      if (hasSingleByteCoverage()) {
        // In coverage mode, getCounterTypeSize() returns 1 byte but our
        // timestamp field has size uint64_t. Increment I so that the next
        // iteration of this for loop points to the byte after the timestamp
        // field, i.e., I += 8.
        I += 7;
      }
      continue;
    }
    if (hasSingleByteCoverage()) {
      // A value of zero signifies the block is covered.
      Record.Counts.push_back(*Ptr == 0 ? 1 : 0);
    } else {
      uint64_t CounterValue = swap(*reinterpret_cast<const uint64_t *>(Ptr));
      if (CounterValue > MaxCounterValue && Warn)
        Warn(make_error<InstrProfError>(
            instrprof_error::counter_value_too_large, Twine(CounterValue)));

      Record.Counts.push_back(CounterValue);
    }
  }

  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readRawBitmapBytes(InstrProfRecord &Record) {
  uint32_t NumBitmapBytes = swap(Data->NumBitmapBytes);

  Record.BitmapBytes.clear();
  Record.BitmapBytes.reserve(NumBitmapBytes);

  // It's possible MCDC is either not enabled or only used for some functions
  // and not others. So if we record 0 bytes, just move on.
  if (NumBitmapBytes == 0)
    return success();

  // BitmapDelta decreases as we advance to the next data record.
  ptrdiff_t BitmapOffset = swap(Data->BitmapPtr) - BitmapDelta;
  if (BitmapOffset < 0)
    return error(
        instrprof_error::malformed,
        ("bitmap offset " + Twine(BitmapOffset) + " is negative").str());

  if (BitmapOffset >= BitmapEnd - BitmapStart)
    return error(instrprof_error::malformed,
                 ("bitmap offset " + Twine(BitmapOffset) +
                  " is greater than the maximum bitmap offset " +
                  Twine(BitmapEnd - BitmapStart - 1))
                     .str());

  uint64_t MaxNumBitmapBytes =
      (BitmapEnd - (BitmapStart + BitmapOffset)) / sizeof(uint8_t);
  if (NumBitmapBytes > MaxNumBitmapBytes)
    return error(instrprof_error::malformed,
                 ("number of bitmap bytes " + Twine(NumBitmapBytes) +
                  " is greater than the maximum number of bitmap bytes " +
                  Twine(MaxNumBitmapBytes))
                     .str());

  for (uint32_t I = 0; I < NumBitmapBytes; I++) {
    const char *Ptr = BitmapStart + BitmapOffset + I;
    Record.BitmapBytes.push_back(swap(*Ptr));
  }

  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readValueProfilingData(
    InstrProfRecord &Record) {
  Record.clearValueData();
  CurValueDataSize = 0;
  // Need to match the logic in value profile dumper code in compiler-rt:
  uint32_t NumValueKinds = 0;
  for (uint32_t I = 0; I < IPVK_Last + 1; I++)
    NumValueKinds += (Data->NumValueSites[I] != 0);

  if (!NumValueKinds)
    return success();

  Expected<std::unique_ptr<ValueProfData>> VDataPtrOrErr =
      ValueProfData::getValueProfData(
          ValueDataStart, (const unsigned char *)DataBuffer->getBufferEnd(),
          getDataEndianness());

  if (Error E = VDataPtrOrErr.takeError())
    return E;

  // Note that besides deserialization, this also performs the conversion for
  // indirect call targets.  The function pointers from the raw profile are
  // remapped into function name hashes.
  VDataPtrOrErr.get()->deserializeTo(Record, Symtab.get());
  CurValueDataSize = VDataPtrOrErr.get()->getSize();
  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readNextRecord(NamedInstrProfRecord &Record) {
  // Keep reading profiles that consist of only headers and no profile data and
  // counters.
  while (atEnd())
    // At this point, ValueDataStart field points to the next header.
    if (Error E = readNextHeader(getNextHeaderPos()))
      return error(std::move(E));

  // Read name and set it in Record.
  if (Error E = readName(Record))
    return error(std::move(E));

  // Read FuncHash and set it in Record.
  if (Error E = readFuncHash(Record))
    return error(std::move(E));

  // Read raw counts and set Record.
  if (Error E = readRawCounts(Record))
    return error(std::move(E));

  // Read raw bitmap bytes and set Record.
  if (Error E = readRawBitmapBytes(Record))
    return error(std::move(E));

  // Read value data and set Record.
  if (Error E = readValueProfilingData(Record))
    return error(std::move(E));

  // Iterate.
  advanceData();
  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readBinaryIds(
    std::vector<llvm::object::BuildID> &BinaryIds) {
  BinaryIds.insert(BinaryIds.begin(), this->BinaryIds.begin(),
                   this->BinaryIds.end());
  return Error::success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::printBinaryIds(raw_ostream &OS) {
  if (!BinaryIds.empty())
    printBinaryIdsInternal(OS, BinaryIds);
  return Error::success();
}

namespace llvm {

template class RawInstrProfReader<uint32_t>;
template class RawInstrProfReader<uint64_t>;

} // end namespace llvm

InstrProfLookupTrait::hash_value_type
InstrProfLookupTrait::ComputeHash(StringRef K) {
  return IndexedInstrProf::ComputeHash(HashType, K);
}

using data_type = InstrProfLookupTrait::data_type;
using offset_type = InstrProfLookupTrait::offset_type;

bool InstrProfLookupTrait::readValueProfilingData(
    const unsigned char *&D, const unsigned char *const End) {
  Expected<std::unique_ptr<ValueProfData>> VDataPtrOrErr =
      ValueProfData::getValueProfData(D, End, ValueProfDataEndianness);

  if (VDataPtrOrErr.takeError())
    return false;

  VDataPtrOrErr.get()->deserializeTo(DataBuffer.back(), nullptr);
  D += VDataPtrOrErr.get()->TotalSize;

  return true;
}

data_type InstrProfLookupTrait::ReadData(StringRef K, const unsigned char *D,
                                         offset_type N) {
  using namespace support;

  // Check if the data is corrupt. If so, don't try to read it.
  if (N % sizeof(uint64_t))
    return data_type();

  DataBuffer.clear();
  std::vector<uint64_t> CounterBuffer;
  std::vector<uint8_t> BitmapByteBuffer;

  const unsigned char *End = D + N;
  while (D < End) {
    // Read hash.
    if (D + sizeof(uint64_t) >= End)
      return data_type();
    uint64_t Hash = endian::readNext<uint64_t, llvm::endianness::little>(D);

    // Initialize number of counters for GET_VERSION(FormatVersion) == 1.
    uint64_t CountsSize = N / sizeof(uint64_t) - 1;
    // If format version is different then read the number of counters.
    if (GET_VERSION(FormatVersion) != IndexedInstrProf::ProfVersion::Version1) {
      if (D + sizeof(uint64_t) > End)
        return data_type();
      CountsSize = endian::readNext<uint64_t, llvm::endianness::little>(D);
    }
    // Read counter values.
    if (D + CountsSize * sizeof(uint64_t) > End)
      return data_type();

    CounterBuffer.clear();
    CounterBuffer.reserve(CountsSize);
    for (uint64_t J = 0; J < CountsSize; ++J)
      CounterBuffer.push_back(
          endian::readNext<uint64_t, llvm::endianness::little>(D));

    // Read bitmap bytes for GET_VERSION(FormatVersion) > 10.
    if (GET_VERSION(FormatVersion) > IndexedInstrProf::ProfVersion::Version10) {
      uint64_t BitmapBytes = 0;
      if (D + sizeof(uint64_t) > End)
        return data_type();
      BitmapBytes = endian::readNext<uint64_t, llvm::endianness::little>(D);
      // Read bitmap byte values.
      if (D + BitmapBytes * sizeof(uint8_t) > End)
        return data_type();
      BitmapByteBuffer.clear();
      BitmapByteBuffer.reserve(BitmapBytes);
      for (uint64_t J = 0; J < BitmapBytes; ++J)
        BitmapByteBuffer.push_back(static_cast<uint8_t>(
            endian::readNext<uint64_t, llvm::endianness::little>(D)));
    }

    DataBuffer.emplace_back(K, Hash, std::move(CounterBuffer),
                            std::move(BitmapByteBuffer));

    // Read value profiling data.
    if (GET_VERSION(FormatVersion) > IndexedInstrProf::ProfVersion::Version2 &&
        !readValueProfilingData(D, End)) {
      DataBuffer.clear();
      return data_type();
    }
  }
  return DataBuffer;
}

template <typename HashTableImpl>
Error InstrProfReaderIndex<HashTableImpl>::getRecords(
    StringRef FuncName, ArrayRef<NamedInstrProfRecord> &Data) {
  auto Iter = HashTable->find(FuncName);
  if (Iter == HashTable->end())
    return make_error<InstrProfError>(instrprof_error::unknown_function);

  Data = (*Iter);
  if (Data.empty())
    return make_error<InstrProfError>(instrprof_error::malformed,
                                      "profile data is empty");

  return Error::success();
}

template <typename HashTableImpl>
Error InstrProfReaderIndex<HashTableImpl>::getRecords(
    ArrayRef<NamedInstrProfRecord> &Data) {
  if (atEnd())
    return make_error<InstrProfError>(instrprof_error::eof);

  Data = *RecordIterator;

  if (Data.empty())
    return make_error<InstrProfError>(instrprof_error::malformed,
                                      "profile data is empty");

  return Error::success();
}

template <typename HashTableImpl>
InstrProfReaderIndex<HashTableImpl>::InstrProfReaderIndex(
    const unsigned char *Buckets, const unsigned char *const Payload,
    const unsigned char *const Base, IndexedInstrProf::HashT HashType,
    uint64_t Version) {
  FormatVersion = Version;
  HashTable.reset(HashTableImpl::Create(
      Buckets, Payload, Base,
      typename HashTableImpl::InfoType(HashType, Version)));
  RecordIterator = HashTable->data_begin();
}

template <typename HashTableImpl>
InstrProfKind InstrProfReaderIndex<HashTableImpl>::getProfileKind() const {
  return getProfileKindFromVersion(FormatVersion);
}

namespace {
/// A remapper that does not apply any remappings.
class InstrProfReaderNullRemapper : public InstrProfReaderRemapper {
  InstrProfReaderIndexBase &Underlying;

public:
  InstrProfReaderNullRemapper(InstrProfReaderIndexBase &Underlying)
      : Underlying(Underlying) {}

  Error getRecords(StringRef FuncName,
                   ArrayRef<NamedInstrProfRecord> &Data) override {
    return Underlying.getRecords(FuncName, Data);
  }
};
} // namespace

/// A remapper that applies remappings based on a symbol remapping file.
template <typename HashTableImpl>
class llvm::InstrProfReaderItaniumRemapper
    : public InstrProfReaderRemapper {
public:
  InstrProfReaderItaniumRemapper(
      std::unique_ptr<MemoryBuffer> RemapBuffer,
      InstrProfReaderIndex<HashTableImpl> &Underlying)
      : RemapBuffer(std::move(RemapBuffer)), Underlying(Underlying) {
  }

  /// Extract the original function name from a PGO function name.
  static StringRef extractName(StringRef Name) {
    // We can have multiple pieces separated by kGlobalIdentifierDelimiter (
    // semicolon now and colon in older profiles); there can be pieces both
    // before and after the mangled name. Find the first part that starts with
    // '_Z'; we'll assume that's the mangled name we want.
    std::pair<StringRef, StringRef> Parts = {StringRef(), Name};
    while (true) {
      Parts = Parts.second.split(GlobalIdentifierDelimiter);
      if (Parts.first.starts_with("_Z"))
        return Parts.first;
      if (Parts.second.empty())
        return Name;
    }
  }

  /// Given a mangled name extracted from a PGO function name, and a new
  /// form for that mangled name, reconstitute the name.
  static void reconstituteName(StringRef OrigName, StringRef ExtractedName,
                               StringRef Replacement,
                               SmallVectorImpl<char> &Out) {
    Out.reserve(OrigName.size() + Replacement.size() - ExtractedName.size());
    Out.insert(Out.end(), OrigName.begin(), ExtractedName.begin());
    Out.insert(Out.end(), Replacement.begin(), Replacement.end());
    Out.insert(Out.end(), ExtractedName.end(), OrigName.end());
  }

  Error populateRemappings() override {
    if (Error E = Remappings.read(*RemapBuffer))
      return E;
    for (StringRef Name : Underlying.HashTable->keys()) {
      StringRef RealName = extractName(Name);
      if (auto Key = Remappings.insert(RealName)) {
        // FIXME: We could theoretically map the same equivalence class to
        // multiple names in the profile data. If that happens, we should
        // return NamedInstrProfRecords from all of them.
        MappedNames.insert({Key, RealName});
      }
    }
    return Error::success();
  }

  Error getRecords(StringRef FuncName,
                   ArrayRef<NamedInstrProfRecord> &Data) override {
    StringRef RealName = extractName(FuncName);
    if (auto Key = Remappings.lookup(RealName)) {
      StringRef Remapped = MappedNames.lookup(Key);
      if (!Remapped.empty()) {
        if (RealName.begin() == FuncName.begin() &&
            RealName.end() == FuncName.end())
          FuncName = Remapped;
        else {
          // Try rebuilding the name from the given remapping.
          SmallString<256> Reconstituted;
          reconstituteName(FuncName, RealName, Remapped, Reconstituted);
          Error E = Underlying.getRecords(Reconstituted, Data);
          if (!E)
            return E;

          // If we failed because the name doesn't exist, fall back to asking
          // about the original name.
          if (Error Unhandled = handleErrors(
                  std::move(E), [](std::unique_ptr<InstrProfError> Err) {
                    return Err->get() == instrprof_error::unknown_function
                               ? Error::success()
                               : Error(std::move(Err));
                  }))
            return Unhandled;
        }
      }
    }
    return Underlying.getRecords(FuncName, Data);
  }

private:
  /// The memory buffer containing the remapping configuration. Remappings
  /// holds pointers into this buffer.
  std::unique_ptr<MemoryBuffer> RemapBuffer;

  /// The mangling remapper.
  SymbolRemappingReader Remappings;

  /// Mapping from mangled name keys to the name used for the key in the
  /// profile data.
  /// FIXME: Can we store a location within the on-disk hash table instead of
  /// redoing lookup?
  DenseMap<SymbolRemappingReader::Key, StringRef> MappedNames;

  /// The real profile data reader.
  InstrProfReaderIndex<HashTableImpl> &Underlying;
};

bool IndexedInstrProfReader::hasFormat(const MemoryBuffer &DataBuffer) {
  using namespace support;

  if (DataBuffer.getBufferSize() < 8)
    return false;
  uint64_t Magic = endian::read<uint64_t, llvm::endianness::little, aligned>(
      DataBuffer.getBufferStart());
  // Verify that it's magical.
  return Magic == IndexedInstrProf::Magic;
}

const unsigned char *
IndexedInstrProfReader::readSummary(IndexedInstrProf::ProfVersion Version,
                                    const unsigned char *Cur, bool UseCS) {
  using namespace IndexedInstrProf;
  using namespace support;

  if (Version >= IndexedInstrProf::Version4) {
    const IndexedInstrProf::Summary *SummaryInLE =
        reinterpret_cast<const IndexedInstrProf::Summary *>(Cur);
    uint64_t NFields = endian::byte_swap<uint64_t, llvm::endianness::little>(
        SummaryInLE->NumSummaryFields);
    uint64_t NEntries = endian::byte_swap<uint64_t, llvm::endianness::little>(
        SummaryInLE->NumCutoffEntries);
    uint32_t SummarySize =
        IndexedInstrProf::Summary::getSize(NFields, NEntries);
    std::unique_ptr<IndexedInstrProf::Summary> SummaryData =
        IndexedInstrProf::allocSummary(SummarySize);

    const uint64_t *Src = reinterpret_cast<const uint64_t *>(SummaryInLE);
    uint64_t *Dst = reinterpret_cast<uint64_t *>(SummaryData.get());
    for (unsigned I = 0; I < SummarySize / sizeof(uint64_t); I++)
      Dst[I] = endian::byte_swap<uint64_t, llvm::endianness::little>(Src[I]);

    SummaryEntryVector DetailedSummary;
    for (unsigned I = 0; I < SummaryData->NumCutoffEntries; I++) {
      const IndexedInstrProf::Summary::Entry &Ent = SummaryData->getEntry(I);
      DetailedSummary.emplace_back((uint32_t)Ent.Cutoff, Ent.MinBlockCount,
                                   Ent.NumBlocks);
    }
    std::unique_ptr<llvm::ProfileSummary> &Summary =
        UseCS ? this->CS_Summary : this->Summary;

    // initialize InstrProfSummary using the SummaryData from disk.
    Summary = std::make_unique<ProfileSummary>(
        UseCS ? ProfileSummary::PSK_CSInstr : ProfileSummary::PSK_Instr,
        DetailedSummary, SummaryData->get(Summary::TotalBlockCount),
        SummaryData->get(Summary::MaxBlockCount),
        SummaryData->get(Summary::MaxInternalBlockCount),
        SummaryData->get(Summary::MaxFunctionCount),
        SummaryData->get(Summary::TotalNumBlocks),
        SummaryData->get(Summary::TotalNumFunctions));
    return Cur + SummarySize;
  } else {
    // The older versions do not support a profile summary. This just computes
    // an empty summary, which will not result in accurate hot/cold detection.
    // We would need to call addRecord for all NamedInstrProfRecords to get the
    // correct summary. However, this version is old (prior to early 2016) and
    // has not been supporting an accurate summary for several years.
    InstrProfSummaryBuilder Builder(ProfileSummaryBuilder::DefaultCutoffs);
    Summary = Builder.getSummary();
    return Cur;
  }
}

Error IndexedMemProfReader::deserializeV012(const unsigned char *Start,
                                            const unsigned char *Ptr,
                                            uint64_t FirstWord) {
  // The value returned from RecordTableGenerator.Emit.
  const uint64_t RecordTableOffset =
      Version == memprof::Version0
          ? FirstWord
          : support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
  // The offset in the stream right before invoking
  // FrameTableGenerator.Emit.
  const uint64_t FramePayloadOffset =
      support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
  // The value returned from FrameTableGenerator.Emit.
  const uint64_t FrameTableOffset =
      support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);

  // The offset in the stream right before invoking
  // CallStackTableGenerator.Emit.
  uint64_t CallStackPayloadOffset = 0;
  // The value returned from CallStackTableGenerator.Emit.
  uint64_t CallStackTableOffset = 0;
  if (Version >= memprof::Version2) {
    CallStackPayloadOffset =
        support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
    CallStackTableOffset =
        support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
  }

  // Read the schema.
  auto SchemaOr = memprof::readMemProfSchema(Ptr);
  if (!SchemaOr)
    return SchemaOr.takeError();
  Schema = SchemaOr.get();

  // Now initialize the table reader with a pointer into data buffer.
  MemProfRecordTable.reset(MemProfRecordHashTable::Create(
      /*Buckets=*/Start + RecordTableOffset,
      /*Payload=*/Ptr,
      /*Base=*/Start, memprof::RecordLookupTrait(Version, Schema)));

  // Initialize the frame table reader with the payload and bucket offsets.
  MemProfFrameTable.reset(MemProfFrameHashTable::Create(
      /*Buckets=*/Start + FrameTableOffset,
      /*Payload=*/Start + FramePayloadOffset,
      /*Base=*/Start));

  if (Version >= memprof::Version2)
    MemProfCallStackTable.reset(MemProfCallStackHashTable::Create(
        /*Buckets=*/Start + CallStackTableOffset,
        /*Payload=*/Start + CallStackPayloadOffset,
        /*Base=*/Start));

  return Error::success();
}

Error IndexedMemProfReader::deserializeV3(const unsigned char *Start,
                                          const unsigned char *Ptr) {
  // The offset in the stream right before invoking
  // CallStackTableGenerator.Emit.
  const uint64_t CallStackPayloadOffset =
      support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
  // The offset in the stream right before invoking RecordTableGenerator.Emit.
  const uint64_t RecordPayloadOffset =
      support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
  // The value returned from RecordTableGenerator.Emit.
  const uint64_t RecordTableOffset =
      support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);

  // Read the schema.
  auto SchemaOr = memprof::readMemProfSchema(Ptr);
  if (!SchemaOr)
    return SchemaOr.takeError();
  Schema = SchemaOr.get();

  FrameBase = Ptr;
  CallStackBase = Start + CallStackPayloadOffset;

  // Now initialize the table reader with a pointer into data buffer.
  MemProfRecordTable.reset(MemProfRecordHashTable::Create(
      /*Buckets=*/Start + RecordTableOffset,
      /*Payload=*/Start + RecordPayloadOffset,
      /*Base=*/Start, memprof::RecordLookupTrait(memprof::Version3, Schema)));

  return Error::success();
}

Error IndexedMemProfReader::deserialize(const unsigned char *Start,
                                        uint64_t MemProfOffset) {
  const unsigned char *Ptr = Start + MemProfOffset;

  // Read the first 64-bit word, which may be RecordTableOffset in
  // memprof::MemProfVersion0 or the MemProf version number in
  // memprof::MemProfVersion1 and above.
  const uint64_t FirstWord =
      support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);

  if (FirstWord == memprof::Version1 || FirstWord == memprof::Version2 ||
      FirstWord == memprof::Version3) {
    // Everything is good.  We can proceed to deserialize the rest.
    Version = static_cast<memprof::IndexedVersion>(FirstWord);
  } else if (FirstWord >= 24) {
    // This is a heuristic/hack to detect memprof::MemProfVersion0,
    // which does not have a version field in the header.
    // In memprof::MemProfVersion0, FirstWord will be RecordTableOffset,
    // which should be at least 24 because of the MemProf header size.
    Version = memprof::Version0;
  } else {
    return make_error<InstrProfError>(
        instrprof_error::unsupported_version,
        formatv("MemProf version {} not supported; "
                "requires version between {} and {}, inclusive",
                FirstWord, memprof::MinimumSupportedVersion,
                memprof::MaximumSupportedVersion));
  }

  switch (Version) {
  case memprof::Version0:
  case memprof::Version1:
  case memprof::Version2:
    if (Error E = deserializeV012(Start, Ptr, FirstWord))
      return E;
    break;
  case memprof::Version3:
    if (Error E = deserializeV3(Start, Ptr))
      return E;
    break;
  }

#ifdef EXPENSIVE_CHECKS
  // Go through all the records and verify that CSId has been correctly
  // populated.  Do this only under EXPENSIVE_CHECKS.  Otherwise, we
  // would defeat the purpose of OnDiskIterableChainedHashTable.
  // Note that we can compare CSId against actual call stacks only for
  // Version0 and Version1 because IndexedAllocationInfo::CallStack and
  // IndexedMemProfRecord::CallSites are not populated in Version2.
  if (Version <= memprof::Version1)
    for (const auto &Record : MemProfRecordTable->data())
      verifyIndexedMemProfRecord(Record);
#endif

  return Error::success();
}

Error IndexedInstrProfReader::readHeader() {
  using namespace support;

  const unsigned char *Start =
      (const unsigned char *)DataBuffer->getBufferStart();
  const unsigned char *Cur = Start;
  if ((const unsigned char *)DataBuffer->getBufferEnd() - Cur < 24)
    return error(instrprof_error::truncated);

  auto HeaderOr = IndexedInstrProf::Header::readFromBuffer(Start);
  if (!HeaderOr)
    return HeaderOr.takeError();

  const IndexedInstrProf::Header *Header = &HeaderOr.get();
  Cur += Header->size();

  Cur = readSummary((IndexedInstrProf::ProfVersion)Header->Version, Cur,
                    /* UseCS */ false);
  if (Header->Version & VARIANT_MASK_CSIR_PROF)
    Cur = readSummary((IndexedInstrProf::ProfVersion)Header->Version, Cur,
                      /* UseCS */ true);
  // Read the hash type and start offset.
  IndexedInstrProf::HashT HashType =
      static_cast<IndexedInstrProf::HashT>(Header->HashType);
  if (HashType > IndexedInstrProf::HashT::Last)
    return error(instrprof_error::unsupported_hash_type);

  // The hash table with profile counts comes next.
  auto IndexPtr = std::make_unique<InstrProfReaderIndex<OnDiskHashTableImplV3>>(
      Start + Header->HashOffset, Cur, Start, HashType, Header->Version);

  // The MemProfOffset field in the header is only valid when the format
  // version is higher than 8 (when it was introduced).
  if (Header->getIndexedProfileVersion() >= 8 &&
      Header->Version & VARIANT_MASK_MEMPROF) {
    if (Error E = MemProfReader.deserialize(Start, Header->MemProfOffset))
      return E;
  }

  // BinaryIdOffset field in the header is only valid when the format version
  // is higher than 9 (when it was introduced).
  if (Header->getIndexedProfileVersion() >= 9) {
    const unsigned char *Ptr = Start + Header->BinaryIdOffset;
    // Read binary ids size.
    uint64_t BinaryIdsSize =
        support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
    if (BinaryIdsSize % sizeof(uint64_t))
      return error(instrprof_error::bad_header);
    // Set the binary ids start.
    BinaryIdsBuffer = ArrayRef<uint8_t>(Ptr, BinaryIdsSize);
    if (Ptr > (const unsigned char *)DataBuffer->getBufferEnd())
      return make_error<InstrProfError>(instrprof_error::malformed,
                                        "corrupted binary ids");
  }

  if (Header->getIndexedProfileVersion() >= 12) {
    const unsigned char *Ptr = Start + Header->VTableNamesOffset;

    uint64_t CompressedVTableNamesLen =
        support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);

    // Writer first writes the length of compressed string, and then the actual
    // content.
    const char *VTableNamePtr = (const char *)Ptr;
    if (VTableNamePtr > (const char *)DataBuffer->getBufferEnd())
      return make_error<InstrProfError>(instrprof_error::truncated);

    VTableName = StringRef(VTableNamePtr, CompressedVTableNamesLen);
  }

  if (Header->getIndexedProfileVersion() >= 10 &&
      Header->Version & VARIANT_MASK_TEMPORAL_PROF) {
    const unsigned char *Ptr = Start + Header->TemporalProfTracesOffset;
    const auto *PtrEnd = (const unsigned char *)DataBuffer->getBufferEnd();
    // Expect at least two 64 bit fields: NumTraces, and TraceStreamSize
    if (Ptr + 2 * sizeof(uint64_t) > PtrEnd)
      return error(instrprof_error::truncated);
    const uint64_t NumTraces =
        support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
    TemporalProfTraceStreamSize =
        support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
    for (unsigned i = 0; i < NumTraces; i++) {
      // Expect at least two 64 bit fields: Weight and NumFunctions
      if (Ptr + 2 * sizeof(uint64_t) > PtrEnd)
        return error(instrprof_error::truncated);
      TemporalProfTraceTy Trace;
      Trace.Weight =
          support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
      const uint64_t NumFunctions =
          support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
      // Expect at least NumFunctions 64 bit fields
      if (Ptr + NumFunctions * sizeof(uint64_t) > PtrEnd)
        return error(instrprof_error::truncated);
      for (unsigned j = 0; j < NumFunctions; j++) {
        const uint64_t NameRef =
            support::endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
        Trace.FunctionNameRefs.push_back(NameRef);
      }
      TemporalProfTraces.push_back(std::move(Trace));
    }
  }

  // Load the remapping table now if requested.
  if (RemappingBuffer) {
    Remapper =
        std::make_unique<InstrProfReaderItaniumRemapper<OnDiskHashTableImplV3>>(
            std::move(RemappingBuffer), *IndexPtr);
    if (Error E = Remapper->populateRemappings())
      return E;
  } else {
    Remapper = std::make_unique<InstrProfReaderNullRemapper>(*IndexPtr);
  }
  Index = std::move(IndexPtr);

  return success();
}

InstrProfSymtab &IndexedInstrProfReader::getSymtab() {
  if (Symtab)
    return *Symtab;

  auto NewSymtab = std::make_unique<InstrProfSymtab>();

  if (Error E = NewSymtab->initVTableNamesFromCompressedStrings(VTableName)) {
    auto [ErrCode, Msg] = InstrProfError::take(std::move(E));
    consumeError(error(ErrCode, Msg));
  }

  // finalizeSymtab is called inside populateSymtab.
  if (Error E = Index->populateSymtab(*NewSymtab)) {
    auto [ErrCode, Msg] = InstrProfError::take(std::move(E));
    consumeError(error(ErrCode, Msg));
  }

  Symtab = std::move(NewSymtab);
  return *Symtab;
}

Expected<InstrProfRecord> IndexedInstrProfReader::getInstrProfRecord(
    StringRef FuncName, uint64_t FuncHash, StringRef DeprecatedFuncName,
    uint64_t *MismatchedFuncSum) {
  ArrayRef<NamedInstrProfRecord> Data;
  uint64_t FuncSum = 0;
  auto Err = Remapper->getRecords(FuncName, Data);
  if (Err) {
    // If we don't find FuncName, try DeprecatedFuncName to handle profiles
    // built by older compilers.
    auto Err2 =
        handleErrors(std::move(Err), [&](const InstrProfError &IE) -> Error {
          if (IE.get() != instrprof_error::unknown_function)
            return make_error<InstrProfError>(IE);
          if (auto Err = Remapper->getRecords(DeprecatedFuncName, Data))
            return Err;
          return Error::success();
        });
    if (Err2)
      return std::move(Err2);
  }
  // Found it. Look for counters with the right hash.

  // A flag to indicate if the records are from the same type
  // of profile (i.e cs vs nocs).
  bool CSBitMatch = false;
  auto getFuncSum = [](ArrayRef<uint64_t> Counts) {
    uint64_t ValueSum = 0;
    for (uint64_t CountValue : Counts) {
      if (CountValue == (uint64_t)-1)
        continue;
      // Handle overflow -- if that happens, return max.
      if (std::numeric_limits<uint64_t>::max() - CountValue <= ValueSum)
        return std::numeric_limits<uint64_t>::max();
      ValueSum += CountValue;
    }
    return ValueSum;
  };

  for (const NamedInstrProfRecord &I : Data) {
    // Check for a match and fill the vector if there is one.
    if (I.Hash == FuncHash)
      return std::move(I);
    if (NamedInstrProfRecord::hasCSFlagInHash(I.Hash) ==
        NamedInstrProfRecord::hasCSFlagInHash(FuncHash)) {
      CSBitMatch = true;
      if (MismatchedFuncSum == nullptr)
        continue;
      FuncSum = std::max(FuncSum, getFuncSum(I.Counts));
    }
  }
  if (CSBitMatch) {
    if (MismatchedFuncSum != nullptr)
      *MismatchedFuncSum = FuncSum;
    return error(instrprof_error::hash_mismatch);
  }
  return error(instrprof_error::unknown_function);
}

static Expected<memprof::MemProfRecord>
getMemProfRecordV0(const memprof::IndexedMemProfRecord &IndexedRecord,
                   MemProfFrameHashTable &MemProfFrameTable) {
  memprof::FrameIdConverter<MemProfFrameHashTable> FrameIdConv(
      MemProfFrameTable);

  memprof::MemProfRecord Record =
      memprof::MemProfRecord(IndexedRecord, FrameIdConv);

  // Check that all frame ids were successfully converted to frames.
  if (FrameIdConv.LastUnmappedId) {
    return make_error<InstrProfError>(instrprof_error::hash_mismatch,
                                      "memprof frame not found for frame id " +
                                          Twine(*FrameIdConv.LastUnmappedId));
  }

  return Record;
}

static Expected<memprof::MemProfRecord>
getMemProfRecordV2(const memprof::IndexedMemProfRecord &IndexedRecord,
                   MemProfFrameHashTable &MemProfFrameTable,
                   MemProfCallStackHashTable &MemProfCallStackTable) {
  memprof::FrameIdConverter<MemProfFrameHashTable> FrameIdConv(
      MemProfFrameTable);

  memprof::CallStackIdConverter<MemProfCallStackHashTable> CSIdConv(
      MemProfCallStackTable, FrameIdConv);

  memprof::MemProfRecord Record = IndexedRecord.toMemProfRecord(CSIdConv);

  // Check that all call stack ids were successfully converted to call stacks.
  if (CSIdConv.LastUnmappedId) {
    return make_error<InstrProfError>(
        instrprof_error::hash_mismatch,
        "memprof call stack not found for call stack id " +
            Twine(*CSIdConv.LastUnmappedId));
  }

  // Check that all frame ids were successfully converted to frames.
  if (FrameIdConv.LastUnmappedId) {
    return make_error<InstrProfError>(instrprof_error::hash_mismatch,
                                      "memprof frame not found for frame id " +
                                          Twine(*FrameIdConv.LastUnmappedId));
  }

  return Record;
}

static Expected<memprof::MemProfRecord>
getMemProfRecordV3(const memprof::IndexedMemProfRecord &IndexedRecord,
                   const unsigned char *FrameBase,
                   const unsigned char *CallStackBase) {
  memprof::LinearFrameIdConverter FrameIdConv(FrameBase);
  memprof::LinearCallStackIdConverter CSIdConv(CallStackBase, FrameIdConv);
  memprof::MemProfRecord Record = IndexedRecord.toMemProfRecord(CSIdConv);
  return Record;
}

Expected<memprof::MemProfRecord>
IndexedMemProfReader::getMemProfRecord(const uint64_t FuncNameHash) const {
  // TODO: Add memprof specific errors.
  if (MemProfRecordTable == nullptr)
    return make_error<InstrProfError>(instrprof_error::invalid_prof,
                                      "no memprof data available in profile");
  auto Iter = MemProfRecordTable->find(FuncNameHash);
  if (Iter == MemProfRecordTable->end())
    return make_error<InstrProfError>(
        instrprof_error::unknown_function,
        "memprof record not found for function hash " + Twine(FuncNameHash));

  const memprof::IndexedMemProfRecord &IndexedRecord = *Iter;
  switch (Version) {
  case memprof::Version0:
  case memprof::Version1:
    assert(MemProfFrameTable && "MemProfFrameTable must be available");
    assert(!MemProfCallStackTable &&
           "MemProfCallStackTable must not be available");
    return getMemProfRecordV0(IndexedRecord, *MemProfFrameTable);
  case memprof::Version2:
    assert(MemProfFrameTable && "MemProfFrameTable must be available");
    assert(MemProfCallStackTable && "MemProfCallStackTable must be available");
    return getMemProfRecordV2(IndexedRecord, *MemProfFrameTable,
                              *MemProfCallStackTable);
  case memprof::Version3:
    assert(!MemProfFrameTable && "MemProfFrameTable must not be available");
    assert(!MemProfCallStackTable &&
           "MemProfCallStackTable must not be available");
    assert(FrameBase && "FrameBase must be available");
    assert(CallStackBase && "CallStackBase must be available");
    return getMemProfRecordV3(IndexedRecord, FrameBase, CallStackBase);
  }

  return make_error<InstrProfError>(
      instrprof_error::unsupported_version,
      formatv("MemProf version {} not supported; "
              "requires version between {} and {}, inclusive",
              Version, memprof::MinimumSupportedVersion,
              memprof::MaximumSupportedVersion));
}

Error IndexedInstrProfReader::getFunctionCounts(StringRef FuncName,
                                                uint64_t FuncHash,
                                                std::vector<uint64_t> &Counts) {
  Expected<InstrProfRecord> Record = getInstrProfRecord(FuncName, FuncHash);
  if (Error E = Record.takeError())
    return error(std::move(E));

  Counts = Record.get().Counts;
  return success();
}

Error IndexedInstrProfReader::getFunctionBitmap(StringRef FuncName,
                                                uint64_t FuncHash,
                                                BitVector &Bitmap) {
  Expected<InstrProfRecord> Record = getInstrProfRecord(FuncName, FuncHash);
  if (Error E = Record.takeError())
    return error(std::move(E));

  const auto &BitmapBytes = Record.get().BitmapBytes;
  size_t I = 0, E = BitmapBytes.size();
  Bitmap.resize(E * CHAR_BIT);
  BitVector::apply(
      [&](auto X) {
        using XTy = decltype(X);
        alignas(XTy) uint8_t W[sizeof(X)];
        size_t N = std::min(E - I, sizeof(W));
        std::memset(W, 0, sizeof(W));
        std::memcpy(W, &BitmapBytes[I], N);
        I += N;
        return support::endian::read<XTy, llvm::endianness::little,
                                     support::aligned>(W);
      },
      Bitmap, Bitmap);
  assert(I == E);

  return success();
}

Error IndexedInstrProfReader::readNextRecord(NamedInstrProfRecord &Record) {
  ArrayRef<NamedInstrProfRecord> Data;

  Error E = Index->getRecords(Data);
  if (E)
    return error(std::move(E));

  Record = Data[RecordIndex++];
  if (RecordIndex >= Data.size()) {
    Index->advanceToNextKey();
    RecordIndex = 0;
  }
  return success();
}

Error IndexedInstrProfReader::readBinaryIds(
    std::vector<llvm::object::BuildID> &BinaryIds) {
  return readBinaryIdsInternal(*DataBuffer, BinaryIdsBuffer, BinaryIds,
                               llvm::endianness::little);
}

Error IndexedInstrProfReader::printBinaryIds(raw_ostream &OS) {
  std::vector<llvm::object::BuildID> BinaryIds;
  if (Error E = readBinaryIds(BinaryIds))
    return E;
  printBinaryIdsInternal(OS, BinaryIds);
  return Error::success();
}

void InstrProfReader::accumulateCounts(CountSumOrPercent &Sum, bool IsCS) {
  uint64_t NumFuncs = 0;
  for (const auto &Func : *this) {
    if (isIRLevelProfile()) {
      bool FuncIsCS = NamedInstrProfRecord::hasCSFlagInHash(Func.Hash);
      if (FuncIsCS != IsCS)
        continue;
    }
    Func.accumulateCounts(Sum);
    ++NumFuncs;
  }
  Sum.NumEntries = NumFuncs;
}

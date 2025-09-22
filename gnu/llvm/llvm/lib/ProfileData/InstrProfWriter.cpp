//===- InstrProfWriter.cpp - Instrumented profiling writer ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing profiling data for clang's
// instrumentation based PGO and coverage.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/InstrProfWriter.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/MemProf.h"
#include "llvm/ProfileData/ProfileCommon.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OnDiskHashTable.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

// A struct to define how the data stream should be patched. For Indexed
// profiling, only uint64_t data type is needed.
struct PatchItem {
  uint64_t Pos;         // Where to patch.
  ArrayRef<uint64_t> D; // An array of source data.
};

namespace llvm {

// A wrapper class to abstract writer stream with support of bytes
// back patching.
class ProfOStream {
public:
  ProfOStream(raw_fd_ostream &FD)
      : IsFDOStream(true), OS(FD), LE(FD, llvm::endianness::little) {}
  ProfOStream(raw_string_ostream &STR)
      : IsFDOStream(false), OS(STR), LE(STR, llvm::endianness::little) {}

  [[nodiscard]] uint64_t tell() const { return OS.tell(); }
  void write(uint64_t V) { LE.write<uint64_t>(V); }
  void write32(uint32_t V) { LE.write<uint32_t>(V); }
  void writeByte(uint8_t V) { LE.write<uint8_t>(V); }

  // \c patch can only be called when all data is written and flushed.
  // For raw_string_ostream, the patch is done on the target string
  // directly and it won't be reflected in the stream's internal buffer.
  void patch(ArrayRef<PatchItem> P) {
    using namespace support;

    if (IsFDOStream) {
      raw_fd_ostream &FDOStream = static_cast<raw_fd_ostream &>(OS);
      const uint64_t LastPos = FDOStream.tell();
      for (const auto &K : P) {
        FDOStream.seek(K.Pos);
        for (uint64_t Elem : K.D)
          write(Elem);
      }
      // Reset the stream to the last position after patching so that users
      // don't accidentally overwrite data. This makes it consistent with
      // the string stream below which replaces the data directly.
      FDOStream.seek(LastPos);
    } else {
      raw_string_ostream &SOStream = static_cast<raw_string_ostream &>(OS);
      std::string &Data = SOStream.str(); // with flush
      for (const auto &K : P) {
        for (int I = 0, E = K.D.size(); I != E; I++) {
          uint64_t Bytes =
              endian::byte_swap<uint64_t, llvm::endianness::little>(K.D[I]);
          Data.replace(K.Pos + I * sizeof(uint64_t), sizeof(uint64_t),
                       (const char *)&Bytes, sizeof(uint64_t));
        }
      }
    }
  }

  // If \c OS is an instance of \c raw_fd_ostream, this field will be
  // true. Otherwise, \c OS will be an raw_string_ostream.
  bool IsFDOStream;
  raw_ostream &OS;
  support::endian::Writer LE;
};

class InstrProfRecordWriterTrait {
public:
  using key_type = StringRef;
  using key_type_ref = StringRef;

  using data_type = const InstrProfWriter::ProfilingData *const;
  using data_type_ref = const InstrProfWriter::ProfilingData *const;

  using hash_value_type = uint64_t;
  using offset_type = uint64_t;

  llvm::endianness ValueProfDataEndianness = llvm::endianness::little;
  InstrProfSummaryBuilder *SummaryBuilder;
  InstrProfSummaryBuilder *CSSummaryBuilder;

  InstrProfRecordWriterTrait() = default;

  static hash_value_type ComputeHash(key_type_ref K) {
    return IndexedInstrProf::ComputeHash(K);
  }

  static std::pair<offset_type, offset_type>
  EmitKeyDataLength(raw_ostream &Out, key_type_ref K, data_type_ref V) {
    using namespace support;

    endian::Writer LE(Out, llvm::endianness::little);

    offset_type N = K.size();
    LE.write<offset_type>(N);

    offset_type M = 0;
    for (const auto &ProfileData : *V) {
      const InstrProfRecord &ProfRecord = ProfileData.second;
      M += sizeof(uint64_t); // The function hash
      M += sizeof(uint64_t); // The size of the Counts vector
      M += ProfRecord.Counts.size() * sizeof(uint64_t);
      M += sizeof(uint64_t); // The size of the Bitmap vector
      M += ProfRecord.BitmapBytes.size() * sizeof(uint64_t);

      // Value data
      M += ValueProfData::getSize(ProfileData.second);
    }
    LE.write<offset_type>(M);

    return std::make_pair(N, M);
  }

  void EmitKey(raw_ostream &Out, key_type_ref K, offset_type N) {
    Out.write(K.data(), N);
  }

  void EmitData(raw_ostream &Out, key_type_ref, data_type_ref V, offset_type) {
    using namespace support;

    endian::Writer LE(Out, llvm::endianness::little);
    for (const auto &ProfileData : *V) {
      const InstrProfRecord &ProfRecord = ProfileData.second;
      if (NamedInstrProfRecord::hasCSFlagInHash(ProfileData.first))
        CSSummaryBuilder->addRecord(ProfRecord);
      else
        SummaryBuilder->addRecord(ProfRecord);

      LE.write<uint64_t>(ProfileData.first); // Function hash
      LE.write<uint64_t>(ProfRecord.Counts.size());
      for (uint64_t I : ProfRecord.Counts)
        LE.write<uint64_t>(I);

      LE.write<uint64_t>(ProfRecord.BitmapBytes.size());
      for (uint64_t I : ProfRecord.BitmapBytes)
        LE.write<uint64_t>(I);

      // Write value data
      std::unique_ptr<ValueProfData> VDataPtr =
          ValueProfData::serializeFrom(ProfileData.second);
      uint32_t S = VDataPtr->getSize();
      VDataPtr->swapBytesFromHost(ValueProfDataEndianness);
      Out.write((const char *)VDataPtr.get(), S);
    }
  }
};

} // end namespace llvm

InstrProfWriter::InstrProfWriter(
    bool Sparse, uint64_t TemporalProfTraceReservoirSize,
    uint64_t MaxTemporalProfTraceLength, bool WritePrevVersion,
    memprof::IndexedVersion MemProfVersionRequested, bool MemProfFullSchema)
    : Sparse(Sparse), MaxTemporalProfTraceLength(MaxTemporalProfTraceLength),
      TemporalProfTraceReservoirSize(TemporalProfTraceReservoirSize),
      InfoObj(new InstrProfRecordWriterTrait()),
      WritePrevVersion(WritePrevVersion),
      MemProfVersionRequested(MemProfVersionRequested),
      MemProfFullSchema(MemProfFullSchema) {}

InstrProfWriter::~InstrProfWriter() { delete InfoObj; }

// Internal interface for testing purpose only.
void InstrProfWriter::setValueProfDataEndianness(llvm::endianness Endianness) {
  InfoObj->ValueProfDataEndianness = Endianness;
}

void InstrProfWriter::setOutputSparse(bool Sparse) {
  this->Sparse = Sparse;
}

void InstrProfWriter::addRecord(NamedInstrProfRecord &&I, uint64_t Weight,
                                function_ref<void(Error)> Warn) {
  auto Name = I.Name;
  auto Hash = I.Hash;
  addRecord(Name, Hash, std::move(I), Weight, Warn);
}

void InstrProfWriter::overlapRecord(NamedInstrProfRecord &&Other,
                                    OverlapStats &Overlap,
                                    OverlapStats &FuncLevelOverlap,
                                    const OverlapFuncFilters &FuncFilter) {
  auto Name = Other.Name;
  auto Hash = Other.Hash;
  Other.accumulateCounts(FuncLevelOverlap.Test);
  if (!FunctionData.contains(Name)) {
    Overlap.addOneUnique(FuncLevelOverlap.Test);
    return;
  }
  if (FuncLevelOverlap.Test.CountSum < 1.0f) {
    Overlap.Overlap.NumEntries += 1;
    return;
  }
  auto &ProfileDataMap = FunctionData[Name];
  bool NewFunc;
  ProfilingData::iterator Where;
  std::tie(Where, NewFunc) =
      ProfileDataMap.insert(std::make_pair(Hash, InstrProfRecord()));
  if (NewFunc) {
    Overlap.addOneMismatch(FuncLevelOverlap.Test);
    return;
  }
  InstrProfRecord &Dest = Where->second;

  uint64_t ValueCutoff = FuncFilter.ValueCutoff;
  if (!FuncFilter.NameFilter.empty() && Name.contains(FuncFilter.NameFilter))
    ValueCutoff = 0;

  Dest.overlap(Other, Overlap, FuncLevelOverlap, ValueCutoff);
}

void InstrProfWriter::addRecord(StringRef Name, uint64_t Hash,
                                InstrProfRecord &&I, uint64_t Weight,
                                function_ref<void(Error)> Warn) {
  auto &ProfileDataMap = FunctionData[Name];

  bool NewFunc;
  ProfilingData::iterator Where;
  std::tie(Where, NewFunc) =
      ProfileDataMap.insert(std::make_pair(Hash, InstrProfRecord()));
  InstrProfRecord &Dest = Where->second;

  auto MapWarn = [&](instrprof_error E) {
    Warn(make_error<InstrProfError>(E));
  };

  if (NewFunc) {
    // We've never seen a function with this name and hash, add it.
    Dest = std::move(I);
    if (Weight > 1)
      Dest.scale(Weight, 1, MapWarn);
  } else {
    // We're updating a function we've seen before.
    Dest.merge(I, Weight, MapWarn);
  }

  Dest.sortValueData();
}

void InstrProfWriter::addMemProfRecord(
    const Function::GUID Id, const memprof::IndexedMemProfRecord &Record) {
  auto [Iter, Inserted] = MemProfData.Records.insert({Id, Record});
  // If we inserted a new record then we are done.
  if (Inserted) {
    return;
  }
  memprof::IndexedMemProfRecord &Existing = Iter->second;
  Existing.merge(Record);
}

bool InstrProfWriter::addMemProfFrame(const memprof::FrameId Id,
                                      const memprof::Frame &Frame,
                                      function_ref<void(Error)> Warn) {
  auto [Iter, Inserted] = MemProfData.Frames.insert({Id, Frame});
  // If a mapping already exists for the current frame id and it does not
  // match the new mapping provided then reset the existing contents and bail
  // out. We don't support the merging of memprof data whose Frame -> Id
  // mapping across profiles is inconsistent.
  if (!Inserted && Iter->second != Frame) {
    Warn(make_error<InstrProfError>(instrprof_error::malformed,
                                    "frame to id mapping mismatch"));
    return false;
  }
  return true;
}

bool InstrProfWriter::addMemProfCallStack(
    const memprof::CallStackId CSId,
    const llvm::SmallVector<memprof::FrameId> &CallStack,
    function_ref<void(Error)> Warn) {
  auto [Iter, Inserted] = MemProfData.CallStacks.insert({CSId, CallStack});
  // If a mapping already exists for the current call stack id and it does not
  // match the new mapping provided then reset the existing contents and bail
  // out. We don't support the merging of memprof data whose CallStack -> Id
  // mapping across profiles is inconsistent.
  if (!Inserted && Iter->second != CallStack) {
    Warn(make_error<InstrProfError>(instrprof_error::malformed,
                                    "call stack to id mapping mismatch"));
    return false;
  }
  return true;
}

void InstrProfWriter::addBinaryIds(ArrayRef<llvm::object::BuildID> BIs) {
  llvm::append_range(BinaryIds, BIs);
}

void InstrProfWriter::addTemporalProfileTrace(TemporalProfTraceTy Trace) {
  assert(Trace.FunctionNameRefs.size() <= MaxTemporalProfTraceLength);
  assert(!Trace.FunctionNameRefs.empty());
  if (TemporalProfTraceStreamSize < TemporalProfTraceReservoirSize) {
    // Simply append the trace if we have not yet hit our reservoir size limit.
    TemporalProfTraces.push_back(std::move(Trace));
  } else {
    // Otherwise, replace a random trace in the stream.
    std::uniform_int_distribution<uint64_t> Distribution(
        0, TemporalProfTraceStreamSize);
    uint64_t RandomIndex = Distribution(RNG);
    if (RandomIndex < TemporalProfTraces.size())
      TemporalProfTraces[RandomIndex] = std::move(Trace);
  }
  ++TemporalProfTraceStreamSize;
}

void InstrProfWriter::addTemporalProfileTraces(
    SmallVectorImpl<TemporalProfTraceTy> &SrcTraces, uint64_t SrcStreamSize) {
  for (auto &Trace : SrcTraces)
    if (Trace.FunctionNameRefs.size() > MaxTemporalProfTraceLength)
      Trace.FunctionNameRefs.resize(MaxTemporalProfTraceLength);
  llvm::erase_if(SrcTraces, [](auto &T) { return T.FunctionNameRefs.empty(); });
  // Assume that the source has the same reservoir size as the destination to
  // avoid needing to record it in the indexed profile format.
  bool IsDestSampled =
      (TemporalProfTraceStreamSize > TemporalProfTraceReservoirSize);
  bool IsSrcSampled = (SrcStreamSize > TemporalProfTraceReservoirSize);
  if (!IsDestSampled && IsSrcSampled) {
    // If one of the traces are sampled, ensure that it belongs to Dest.
    std::swap(TemporalProfTraces, SrcTraces);
    std::swap(TemporalProfTraceStreamSize, SrcStreamSize);
    std::swap(IsDestSampled, IsSrcSampled);
  }
  if (!IsSrcSampled) {
    // If the source stream is not sampled, we add each source trace normally.
    for (auto &Trace : SrcTraces)
      addTemporalProfileTrace(std::move(Trace));
    return;
  }
  // Otherwise, we find the traces that would have been removed if we added
  // the whole source stream.
  SmallSetVector<uint64_t, 8> IndicesToReplace;
  for (uint64_t I = 0; I < SrcStreamSize; I++) {
    std::uniform_int_distribution<uint64_t> Distribution(
        0, TemporalProfTraceStreamSize);
    uint64_t RandomIndex = Distribution(RNG);
    if (RandomIndex < TemporalProfTraces.size())
      IndicesToReplace.insert(RandomIndex);
    ++TemporalProfTraceStreamSize;
  }
  // Then we insert a random sample of the source traces.
  llvm::shuffle(SrcTraces.begin(), SrcTraces.end(), RNG);
  for (const auto &[Index, Trace] : llvm::zip(IndicesToReplace, SrcTraces))
    TemporalProfTraces[Index] = std::move(Trace);
}

void InstrProfWriter::mergeRecordsFromWriter(InstrProfWriter &&IPW,
                                             function_ref<void(Error)> Warn) {
  for (auto &I : IPW.FunctionData)
    for (auto &Func : I.getValue())
      addRecord(I.getKey(), Func.first, std::move(Func.second), 1, Warn);

  BinaryIds.reserve(BinaryIds.size() + IPW.BinaryIds.size());
  for (auto &I : IPW.BinaryIds)
    addBinaryIds(I);

  addTemporalProfileTraces(IPW.TemporalProfTraces,
                           IPW.TemporalProfTraceStreamSize);

  MemProfData.Frames.reserve(IPW.MemProfData.Frames.size());
  for (auto &[FrameId, Frame] : IPW.MemProfData.Frames) {
    // If we weren't able to add the frame mappings then it doesn't make sense
    // to try to merge the records from this profile.
    if (!addMemProfFrame(FrameId, Frame, Warn))
      return;
  }

  MemProfData.CallStacks.reserve(IPW.MemProfData.CallStacks.size());
  for (auto &[CSId, CallStack] : IPW.MemProfData.CallStacks) {
    if (!addMemProfCallStack(CSId, CallStack, Warn))
      return;
  }

  MemProfData.Records.reserve(IPW.MemProfData.Records.size());
  for (auto &[GUID, Record] : IPW.MemProfData.Records) {
    addMemProfRecord(GUID, Record);
  }
}

bool InstrProfWriter::shouldEncodeData(const ProfilingData &PD) {
  if (!Sparse)
    return true;
  for (const auto &Func : PD) {
    const InstrProfRecord &IPR = Func.second;
    if (llvm::any_of(IPR.Counts, [](uint64_t Count) { return Count > 0; }))
      return true;
    if (llvm::any_of(IPR.BitmapBytes, [](uint8_t Byte) { return Byte > 0; }))
      return true;
  }
  return false;
}

static void setSummary(IndexedInstrProf::Summary *TheSummary,
                       ProfileSummary &PS) {
  using namespace IndexedInstrProf;

  const std::vector<ProfileSummaryEntry> &Res = PS.getDetailedSummary();
  TheSummary->NumSummaryFields = Summary::NumKinds;
  TheSummary->NumCutoffEntries = Res.size();
  TheSummary->set(Summary::MaxFunctionCount, PS.getMaxFunctionCount());
  TheSummary->set(Summary::MaxBlockCount, PS.getMaxCount());
  TheSummary->set(Summary::MaxInternalBlockCount, PS.getMaxInternalCount());
  TheSummary->set(Summary::TotalBlockCount, PS.getTotalCount());
  TheSummary->set(Summary::TotalNumBlocks, PS.getNumCounts());
  TheSummary->set(Summary::TotalNumFunctions, PS.getNumFunctions());
  for (unsigned I = 0; I < Res.size(); I++)
    TheSummary->setEntry(I, Res[I]);
}

// Serialize Schema.
static void writeMemProfSchema(ProfOStream &OS,
                               const memprof::MemProfSchema &Schema) {
  OS.write(static_cast<uint64_t>(Schema.size()));
  for (const auto Id : Schema)
    OS.write(static_cast<uint64_t>(Id));
}

// Serialize MemProfRecordData.  Return RecordTableOffset.
static uint64_t writeMemProfRecords(
    ProfOStream &OS,
    llvm::MapVector<GlobalValue::GUID, memprof::IndexedMemProfRecord>
        &MemProfRecordData,
    memprof::MemProfSchema *Schema, memprof::IndexedVersion Version,
    llvm::DenseMap<memprof::CallStackId, memprof::LinearCallStackId>
        *MemProfCallStackIndexes = nullptr) {
  memprof::RecordWriterTrait RecordWriter(Schema, Version,
                                          MemProfCallStackIndexes);
  OnDiskChainedHashTableGenerator<memprof::RecordWriterTrait>
      RecordTableGenerator;
  for (auto &[GUID, Record] : MemProfRecordData) {
    // Insert the key (func hash) and value (memprof record).
    RecordTableGenerator.insert(GUID, Record, RecordWriter);
  }
  // Release the memory of this MapVector as it is no longer needed.
  MemProfRecordData.clear();

  // The call to Emit invokes RecordWriterTrait::EmitData which destructs
  // the memprof record copies owned by the RecordTableGenerator. This works
  // because the RecordTableGenerator is not used after this point.
  return RecordTableGenerator.Emit(OS.OS, RecordWriter);
}

// Serialize MemProfFrameData.  Return FrameTableOffset.
static uint64_t writeMemProfFrames(
    ProfOStream &OS,
    llvm::MapVector<memprof::FrameId, memprof::Frame> &MemProfFrameData) {
  OnDiskChainedHashTableGenerator<memprof::FrameWriterTrait>
      FrameTableGenerator;
  for (auto &[FrameId, Frame] : MemProfFrameData) {
    // Insert the key (frame id) and value (frame contents).
    FrameTableGenerator.insert(FrameId, Frame);
  }
  // Release the memory of this MapVector as it is no longer needed.
  MemProfFrameData.clear();

  return FrameTableGenerator.Emit(OS.OS);
}

// Serialize MemProfFrameData.  Return the mapping from FrameIds to their
// indexes within the frame array.
static llvm::DenseMap<memprof::FrameId, memprof::LinearFrameId>
writeMemProfFrameArray(
    ProfOStream &OS,
    llvm::MapVector<memprof::FrameId, memprof::Frame> &MemProfFrameData,
    llvm::DenseMap<memprof::FrameId, memprof::FrameStat> &FrameHistogram) {
  // Mappings from FrameIds to array indexes.
  llvm::DenseMap<memprof::FrameId, memprof::LinearFrameId> MemProfFrameIndexes;

  // Compute the order in which we serialize Frames.  The order does not matter
  // in terms of correctness, but we still compute it for deserialization
  // performance.  Specifically, if we serialize frequently used Frames one
  // after another, we have better cache utilization.  For two Frames that
  // appear equally frequently, we break a tie by serializing the one that tends
  // to appear earlier in call stacks.  We implement the tie-breaking mechanism
  // by computing the sum of indexes within call stacks for each Frame.  If we
  // still have a tie, then we just resort to compare two FrameIds, which is
  // just for stability of output.
  std::vector<std::pair<memprof::FrameId, const memprof::Frame *>> FrameIdOrder;
  FrameIdOrder.reserve(MemProfFrameData.size());
  for (const auto &[Id, Frame] : MemProfFrameData)
    FrameIdOrder.emplace_back(Id, &Frame);
  assert(MemProfFrameData.size() == FrameIdOrder.size());
  llvm::sort(FrameIdOrder,
             [&](const std::pair<memprof::FrameId, const memprof::Frame *> &L,
                 const std::pair<memprof::FrameId, const memprof::Frame *> &R) {
               const auto &SL = FrameHistogram[L.first];
               const auto &SR = FrameHistogram[R.first];
               // Popular FrameIds should come first.
               if (SL.Count != SR.Count)
                 return SL.Count > SR.Count;
               // If they are equally popular, then the one that tends to appear
               // earlier in call stacks should come first.
               if (SL.PositionSum != SR.PositionSum)
                 return SL.PositionSum < SR.PositionSum;
               // Compare their FrameIds for sort stability.
               return L.first < R.first;
             });

  // Serialize all frames while creating mappings from linear IDs to FrameIds.
  uint64_t Index = 0;
  MemProfFrameIndexes.reserve(FrameIdOrder.size());
  for (const auto &[Id, F] : FrameIdOrder) {
    F->serialize(OS.OS);
    MemProfFrameIndexes.insert({Id, Index});
    ++Index;
  }
  assert(MemProfFrameData.size() == Index);
  assert(MemProfFrameData.size() == MemProfFrameIndexes.size());

  // Release the memory of this MapVector as it is no longer needed.
  MemProfFrameData.clear();

  return MemProfFrameIndexes;
}

static uint64_t writeMemProfCallStacks(
    ProfOStream &OS,
    llvm::MapVector<memprof::CallStackId, llvm::SmallVector<memprof::FrameId>>
        &MemProfCallStackData) {
  OnDiskChainedHashTableGenerator<memprof::CallStackWriterTrait>
      CallStackTableGenerator;
  for (auto &[CSId, CallStack] : MemProfCallStackData)
    CallStackTableGenerator.insert(CSId, CallStack);
  // Release the memory of this vector as it is no longer needed.
  MemProfCallStackData.clear();

  return CallStackTableGenerator.Emit(OS.OS);
}

static llvm::DenseMap<memprof::CallStackId, memprof::LinearCallStackId>
writeMemProfCallStackArray(
    ProfOStream &OS,
    llvm::MapVector<memprof::CallStackId, llvm::SmallVector<memprof::FrameId>>
        &MemProfCallStackData,
    llvm::DenseMap<memprof::FrameId, memprof::LinearFrameId>
        &MemProfFrameIndexes,
    llvm::DenseMap<memprof::FrameId, memprof::FrameStat> &FrameHistogram) {
  llvm::DenseMap<memprof::CallStackId, memprof::LinearCallStackId>
      MemProfCallStackIndexes;

  memprof::CallStackRadixTreeBuilder Builder;
  Builder.build(std::move(MemProfCallStackData), MemProfFrameIndexes,
                FrameHistogram);
  for (auto I : Builder.getRadixArray())
    OS.write32(I);
  MemProfCallStackIndexes = Builder.takeCallStackPos();

  // Release the memory of this vector as it is no longer needed.
  MemProfCallStackData.clear();

  return MemProfCallStackIndexes;
}

// Write out MemProf Version0 as follows:
// uint64_t RecordTableOffset = RecordTableGenerator.Emit
// uint64_t FramePayloadOffset = Offset for the frame payload
// uint64_t FrameTableOffset = FrameTableGenerator.Emit
// uint64_t Num schema entries
// uint64_t Schema entry 0
// uint64_t Schema entry 1
// ....
// uint64_t Schema entry N - 1
// OnDiskChainedHashTable MemProfRecordData
// OnDiskChainedHashTable MemProfFrameData
static Error writeMemProfV0(ProfOStream &OS,
                            memprof::IndexedMemProfData &MemProfData) {
  uint64_t HeaderUpdatePos = OS.tell();
  OS.write(0ULL); // Reserve space for the memprof record table offset.
  OS.write(0ULL); // Reserve space for the memprof frame payload offset.
  OS.write(0ULL); // Reserve space for the memprof frame table offset.

  auto Schema = memprof::getFullSchema();
  writeMemProfSchema(OS, Schema);

  uint64_t RecordTableOffset =
      writeMemProfRecords(OS, MemProfData.Records, &Schema, memprof::Version0);

  uint64_t FramePayloadOffset = OS.tell();
  uint64_t FrameTableOffset = writeMemProfFrames(OS, MemProfData.Frames);

  uint64_t Header[] = {RecordTableOffset, FramePayloadOffset, FrameTableOffset};
  OS.patch({{HeaderUpdatePos, Header}});

  return Error::success();
}

// Write out MemProf Version1 as follows:
// uint64_t Version (NEW in V1)
// uint64_t RecordTableOffset = RecordTableGenerator.Emit
// uint64_t FramePayloadOffset = Offset for the frame payload
// uint64_t FrameTableOffset = FrameTableGenerator.Emit
// uint64_t Num schema entries
// uint64_t Schema entry 0
// uint64_t Schema entry 1
// ....
// uint64_t Schema entry N - 1
// OnDiskChainedHashTable MemProfRecordData
// OnDiskChainedHashTable MemProfFrameData
static Error writeMemProfV1(ProfOStream &OS,
                            memprof::IndexedMemProfData &MemProfData) {
  OS.write(memprof::Version1);
  uint64_t HeaderUpdatePos = OS.tell();
  OS.write(0ULL); // Reserve space for the memprof record table offset.
  OS.write(0ULL); // Reserve space for the memprof frame payload offset.
  OS.write(0ULL); // Reserve space for the memprof frame table offset.

  auto Schema = memprof::getFullSchema();
  writeMemProfSchema(OS, Schema);

  uint64_t RecordTableOffset =
      writeMemProfRecords(OS, MemProfData.Records, &Schema, memprof::Version1);

  uint64_t FramePayloadOffset = OS.tell();
  uint64_t FrameTableOffset = writeMemProfFrames(OS, MemProfData.Frames);

  uint64_t Header[] = {RecordTableOffset, FramePayloadOffset, FrameTableOffset};
  OS.patch({{HeaderUpdatePos, Header}});

  return Error::success();
}

// Write out MemProf Version2 as follows:
// uint64_t Version
// uint64_t RecordTableOffset = RecordTableGenerator.Emit
// uint64_t FramePayloadOffset = Offset for the frame payload
// uint64_t FrameTableOffset = FrameTableGenerator.Emit
// uint64_t CallStackPayloadOffset = Offset for the call stack payload (NEW V2)
// uint64_t CallStackTableOffset = CallStackTableGenerator.Emit (NEW in V2)
// uint64_t Num schema entries
// uint64_t Schema entry 0
// uint64_t Schema entry 1
// ....
// uint64_t Schema entry N - 1
// OnDiskChainedHashTable MemProfRecordData
// OnDiskChainedHashTable MemProfFrameData
// OnDiskChainedHashTable MemProfCallStackData (NEW in V2)
static Error writeMemProfV2(ProfOStream &OS,
                            memprof::IndexedMemProfData &MemProfData,
                            bool MemProfFullSchema) {
  OS.write(memprof::Version2);
  uint64_t HeaderUpdatePos = OS.tell();
  OS.write(0ULL); // Reserve space for the memprof record table offset.
  OS.write(0ULL); // Reserve space for the memprof frame payload offset.
  OS.write(0ULL); // Reserve space for the memprof frame table offset.
  OS.write(0ULL); // Reserve space for the memprof call stack payload offset.
  OS.write(0ULL); // Reserve space for the memprof call stack table offset.

  auto Schema = memprof::getHotColdSchema();
  if (MemProfFullSchema)
    Schema = memprof::getFullSchema();
  writeMemProfSchema(OS, Schema);

  uint64_t RecordTableOffset =
      writeMemProfRecords(OS, MemProfData.Records, &Schema, memprof::Version2);

  uint64_t FramePayloadOffset = OS.tell();
  uint64_t FrameTableOffset = writeMemProfFrames(OS, MemProfData.Frames);

  uint64_t CallStackPayloadOffset = OS.tell();
  uint64_t CallStackTableOffset =
      writeMemProfCallStacks(OS, MemProfData.CallStacks);

  uint64_t Header[] = {
      RecordTableOffset,      FramePayloadOffset,   FrameTableOffset,
      CallStackPayloadOffset, CallStackTableOffset,
  };
  OS.patch({{HeaderUpdatePos, Header}});

  return Error::success();
}

// Write out MemProf Version3 as follows:
// uint64_t Version
// uint64_t CallStackPayloadOffset = Offset for the call stack payload
// uint64_t RecordPayloadOffset = Offset for the record payload
// uint64_t RecordTableOffset = RecordTableGenerator.Emit
// uint64_t Num schema entries
// uint64_t Schema entry 0
// uint64_t Schema entry 1
// ....
// uint64_t Schema entry N - 1
// Frames serialized one after another
// Call stacks encoded as a radix tree
// OnDiskChainedHashTable MemProfRecordData
static Error writeMemProfV3(ProfOStream &OS,
                            memprof::IndexedMemProfData &MemProfData,
                            bool MemProfFullSchema) {
  OS.write(memprof::Version3);
  uint64_t HeaderUpdatePos = OS.tell();
  OS.write(0ULL); // Reserve space for the memprof call stack payload offset.
  OS.write(0ULL); // Reserve space for the memprof record payload offset.
  OS.write(0ULL); // Reserve space for the memprof record table offset.

  auto Schema = memprof::getHotColdSchema();
  if (MemProfFullSchema)
    Schema = memprof::getFullSchema();
  writeMemProfSchema(OS, Schema);

  llvm::DenseMap<memprof::FrameId, memprof::FrameStat> FrameHistogram =
      memprof::computeFrameHistogram(MemProfData.CallStacks);
  assert(MemProfData.Frames.size() == FrameHistogram.size());

  llvm::DenseMap<memprof::FrameId, memprof::LinearFrameId> MemProfFrameIndexes =
      writeMemProfFrameArray(OS, MemProfData.Frames, FrameHistogram);

  uint64_t CallStackPayloadOffset = OS.tell();
  llvm::DenseMap<memprof::CallStackId, memprof::LinearCallStackId>
      MemProfCallStackIndexes = writeMemProfCallStackArray(
          OS, MemProfData.CallStacks, MemProfFrameIndexes, FrameHistogram);

  uint64_t RecordPayloadOffset = OS.tell();
  uint64_t RecordTableOffset =
      writeMemProfRecords(OS, MemProfData.Records, &Schema, memprof::Version3,
                          &MemProfCallStackIndexes);

  uint64_t Header[] = {
      CallStackPayloadOffset,
      RecordPayloadOffset,
      RecordTableOffset,
  };
  OS.patch({{HeaderUpdatePos, Header}});

  return Error::success();
}

// Write out the MemProf data in a requested version.
static Error writeMemProf(ProfOStream &OS,
                          memprof::IndexedMemProfData &MemProfData,
                          memprof::IndexedVersion MemProfVersionRequested,
                          bool MemProfFullSchema) {
  switch (MemProfVersionRequested) {
  case memprof::Version0:
    return writeMemProfV0(OS, MemProfData);
  case memprof::Version1:
    return writeMemProfV1(OS, MemProfData);
  case memprof::Version2:
    return writeMemProfV2(OS, MemProfData, MemProfFullSchema);
  case memprof::Version3:
    return writeMemProfV3(OS, MemProfData, MemProfFullSchema);
  }

  return make_error<InstrProfError>(
      instrprof_error::unsupported_version,
      formatv("MemProf version {} not supported; "
              "requires version between {} and {}, inclusive",
              MemProfVersionRequested, memprof::MinimumSupportedVersion,
              memprof::MaximumSupportedVersion));
}

uint64_t InstrProfWriter::writeHeader(const IndexedInstrProf::Header &Header,
                                      const bool WritePrevVersion,
                                      ProfOStream &OS) {
  // Only write out the first four fields.
  for (int I = 0; I < 4; I++)
    OS.write(reinterpret_cast<const uint64_t *>(&Header)[I]);

  // Remember the offset of the remaining fields to allow back patching later.
  auto BackPatchStartOffset = OS.tell();

  // Reserve the space for back patching later.
  OS.write(0); // HashOffset
  OS.write(0); // MemProfOffset
  OS.write(0); // BinaryIdOffset
  OS.write(0); // TemporalProfTracesOffset
  if (!WritePrevVersion)
    OS.write(0); // VTableNamesOffset

  return BackPatchStartOffset;
}

Error InstrProfWriter::writeVTableNames(ProfOStream &OS) {
  std::vector<std::string> VTableNameStrs;
  for (StringRef VTableName : VTableNames.keys())
    VTableNameStrs.push_back(VTableName.str());

  std::string CompressedVTableNames;
  if (!VTableNameStrs.empty())
    if (Error E = collectGlobalObjectNameStrings(
            VTableNameStrs, compression::zlib::isAvailable(),
            CompressedVTableNames))
      return E;

  const uint64_t CompressedStringLen = CompressedVTableNames.length();

  // Record the length of compressed string.
  OS.write(CompressedStringLen);

  // Write the chars in compressed strings.
  for (auto &c : CompressedVTableNames)
    OS.writeByte(static_cast<uint8_t>(c));

  // Pad up to a multiple of 8.
  // InstrProfReader could read bytes according to 'CompressedStringLen'.
  const uint64_t PaddedLength = alignTo(CompressedStringLen, 8);

  for (uint64_t K = CompressedStringLen; K < PaddedLength; K++)
    OS.writeByte(0);

  return Error::success();
}

Error InstrProfWriter::writeImpl(ProfOStream &OS) {
  using namespace IndexedInstrProf;
  using namespace support;

  OnDiskChainedHashTableGenerator<InstrProfRecordWriterTrait> Generator;

  InstrProfSummaryBuilder ISB(ProfileSummaryBuilder::DefaultCutoffs);
  InfoObj->SummaryBuilder = &ISB;
  InstrProfSummaryBuilder CSISB(ProfileSummaryBuilder::DefaultCutoffs);
  InfoObj->CSSummaryBuilder = &CSISB;

  // Populate the hash table generator.
  SmallVector<std::pair<StringRef, const ProfilingData *>> OrderedData;
  for (const auto &I : FunctionData)
    if (shouldEncodeData(I.getValue()))
      OrderedData.emplace_back((I.getKey()), &I.getValue());
  llvm::sort(OrderedData, less_first());
  for (const auto &I : OrderedData)
    Generator.insert(I.first, I.second);

  // Write the header.
  IndexedInstrProf::Header Header;
  Header.Version = WritePrevVersion
                       ? IndexedInstrProf::ProfVersion::Version11
                       : IndexedInstrProf::ProfVersion::CurrentVersion;
  // The WritePrevVersion handling will either need to be removed or updated
  // if the version is advanced beyond 12.
  static_assert(IndexedInstrProf::ProfVersion::CurrentVersion ==
                IndexedInstrProf::ProfVersion::Version12);
  if (static_cast<bool>(ProfileKind & InstrProfKind::IRInstrumentation))
    Header.Version |= VARIANT_MASK_IR_PROF;
  if (static_cast<bool>(ProfileKind & InstrProfKind::ContextSensitive))
    Header.Version |= VARIANT_MASK_CSIR_PROF;
  if (static_cast<bool>(ProfileKind &
                        InstrProfKind::FunctionEntryInstrumentation))
    Header.Version |= VARIANT_MASK_INSTR_ENTRY;
  if (static_cast<bool>(ProfileKind & InstrProfKind::SingleByteCoverage))
    Header.Version |= VARIANT_MASK_BYTE_COVERAGE;
  if (static_cast<bool>(ProfileKind & InstrProfKind::FunctionEntryOnly))
    Header.Version |= VARIANT_MASK_FUNCTION_ENTRY_ONLY;
  if (static_cast<bool>(ProfileKind & InstrProfKind::MemProf))
    Header.Version |= VARIANT_MASK_MEMPROF;
  if (static_cast<bool>(ProfileKind & InstrProfKind::TemporalProfile))
    Header.Version |= VARIANT_MASK_TEMPORAL_PROF;

  const uint64_t BackPatchStartOffset =
      writeHeader(Header, WritePrevVersion, OS);

  // Reserve space to write profile summary data.
  uint32_t NumEntries = ProfileSummaryBuilder::DefaultCutoffs.size();
  uint32_t SummarySize = Summary::getSize(Summary::NumKinds, NumEntries);
  // Remember the summary offset.
  uint64_t SummaryOffset = OS.tell();
  for (unsigned I = 0; I < SummarySize / sizeof(uint64_t); I++)
    OS.write(0);
  uint64_t CSSummaryOffset = 0;
  uint64_t CSSummarySize = 0;
  if (static_cast<bool>(ProfileKind & InstrProfKind::ContextSensitive)) {
    CSSummaryOffset = OS.tell();
    CSSummarySize = SummarySize / sizeof(uint64_t);
    for (unsigned I = 0; I < CSSummarySize; I++)
      OS.write(0);
  }

  // Write the hash table.
  uint64_t HashTableStart = Generator.Emit(OS.OS, *InfoObj);

  // Write the MemProf profile data if we have it.
  uint64_t MemProfSectionStart = 0;
  if (static_cast<bool>(ProfileKind & InstrProfKind::MemProf)) {
    MemProfSectionStart = OS.tell();
    if (auto E = writeMemProf(OS, MemProfData, MemProfVersionRequested,
                              MemProfFullSchema))
      return E;
  }

  // BinaryIdSection has two parts:
  // 1. uint64_t BinaryIdsSectionSize
  // 2. list of binary ids that consist of:
  //    a. uint64_t BinaryIdLength
  //    b. uint8_t  BinaryIdData
  //    c. uint8_t  Padding (if necessary)
  uint64_t BinaryIdSectionStart = OS.tell();
  // Calculate size of binary section.
  uint64_t BinaryIdsSectionSize = 0;

  // Remove duplicate binary ids.
  llvm::sort(BinaryIds);
  BinaryIds.erase(llvm::unique(BinaryIds), BinaryIds.end());

  for (const auto &BI : BinaryIds) {
    // Increment by binary id length data type size.
    BinaryIdsSectionSize += sizeof(uint64_t);
    // Increment by binary id data length, aligned to 8 bytes.
    BinaryIdsSectionSize += alignToPowerOf2(BI.size(), sizeof(uint64_t));
  }
  // Write binary ids section size.
  OS.write(BinaryIdsSectionSize);

  for (const auto &BI : BinaryIds) {
    uint64_t BILen = BI.size();
    // Write binary id length.
    OS.write(BILen);
    // Write binary id data.
    for (unsigned K = 0; K < BILen; K++)
      OS.writeByte(BI[K]);
    // Write padding if necessary.
    uint64_t PaddingSize = alignToPowerOf2(BILen, sizeof(uint64_t)) - BILen;
    for (unsigned K = 0; K < PaddingSize; K++)
      OS.writeByte(0);
  }

  uint64_t VTableNamesSectionStart = OS.tell();

  if (!WritePrevVersion)
    if (Error E = writeVTableNames(OS))
      return E;

  uint64_t TemporalProfTracesSectionStart = 0;
  if (static_cast<bool>(ProfileKind & InstrProfKind::TemporalProfile)) {
    TemporalProfTracesSectionStart = OS.tell();
    OS.write(TemporalProfTraces.size());
    OS.write(TemporalProfTraceStreamSize);
    for (auto &Trace : TemporalProfTraces) {
      OS.write(Trace.Weight);
      OS.write(Trace.FunctionNameRefs.size());
      for (auto &NameRef : Trace.FunctionNameRefs)
        OS.write(NameRef);
    }
  }

  // Allocate space for data to be serialized out.
  std::unique_ptr<IndexedInstrProf::Summary> TheSummary =
      IndexedInstrProf::allocSummary(SummarySize);
  // Compute the Summary and copy the data to the data
  // structure to be serialized out (to disk or buffer).
  std::unique_ptr<ProfileSummary> PS = ISB.getSummary();
  setSummary(TheSummary.get(), *PS);
  InfoObj->SummaryBuilder = nullptr;

  // For Context Sensitive summary.
  std::unique_ptr<IndexedInstrProf::Summary> TheCSSummary = nullptr;
  if (static_cast<bool>(ProfileKind & InstrProfKind::ContextSensitive)) {
    TheCSSummary = IndexedInstrProf::allocSummary(SummarySize);
    std::unique_ptr<ProfileSummary> CSPS = CSISB.getSummary();
    setSummary(TheCSSummary.get(), *CSPS);
  }
  InfoObj->CSSummaryBuilder = nullptr;

  SmallVector<uint64_t, 8> HeaderOffsets = {HashTableStart, MemProfSectionStart,
                                            BinaryIdSectionStart,
                                            TemporalProfTracesSectionStart};
  if (!WritePrevVersion)
    HeaderOffsets.push_back(VTableNamesSectionStart);

  PatchItem PatchItems[] = {
      // Patch the Header fields
      {BackPatchStartOffset, HeaderOffsets},
      // Patch the summary data.
      {SummaryOffset,
       ArrayRef<uint64_t>(reinterpret_cast<uint64_t *>(TheSummary.get()),
                          SummarySize / sizeof(uint64_t))},
      {CSSummaryOffset,
       ArrayRef<uint64_t>(reinterpret_cast<uint64_t *>(TheCSSummary.get()),
                          CSSummarySize)}};

  OS.patch(PatchItems);

  for (const auto &I : FunctionData)
    for (const auto &F : I.getValue())
      if (Error E = validateRecord(F.second))
        return E;

  return Error::success();
}

Error InstrProfWriter::write(raw_fd_ostream &OS) {
  // Write the hash table.
  ProfOStream POS(OS);
  return writeImpl(POS);
}

Error InstrProfWriter::write(raw_string_ostream &OS) {
  ProfOStream POS(OS);
  return writeImpl(POS);
}

std::unique_ptr<MemoryBuffer> InstrProfWriter::writeBuffer() {
  std::string Data;
  raw_string_ostream OS(Data);
  // Write the hash table.
  if (Error E = write(OS))
    return nullptr;
  // Return this in an aligned memory buffer.
  return MemoryBuffer::getMemBufferCopy(Data);
}

static const char *ValueProfKindStr[] = {
#define VALUE_PROF_KIND(Enumerator, Value, Descr) #Enumerator,
#include "llvm/ProfileData/InstrProfData.inc"
};

Error InstrProfWriter::validateRecord(const InstrProfRecord &Func) {
  for (uint32_t VK = 0; VK <= IPVK_Last; VK++) {
    if (VK == IPVK_IndirectCallTarget || VK == IPVK_VTableTarget)
      continue;
    uint32_t NS = Func.getNumValueSites(VK);
    for (uint32_t S = 0; S < NS; S++) {
      DenseSet<uint64_t> SeenValues;
      for (const auto &V : Func.getValueArrayForSite(VK, S))
        if (!SeenValues.insert(V.Value).second)
          return make_error<InstrProfError>(instrprof_error::invalid_prof);
    }
  }

  return Error::success();
}

void InstrProfWriter::writeRecordInText(StringRef Name, uint64_t Hash,
                                        const InstrProfRecord &Func,
                                        InstrProfSymtab &Symtab,
                                        raw_fd_ostream &OS) {
  OS << Name << "\n";
  OS << "# Func Hash:\n" << Hash << "\n";
  OS << "# Num Counters:\n" << Func.Counts.size() << "\n";
  OS << "# Counter Values:\n";
  for (uint64_t Count : Func.Counts)
    OS << Count << "\n";

  if (Func.BitmapBytes.size() > 0) {
    OS << "# Num Bitmap Bytes:\n$" << Func.BitmapBytes.size() << "\n";
    OS << "# Bitmap Byte Values:\n";
    for (uint8_t Byte : Func.BitmapBytes) {
      OS << "0x";
      OS.write_hex(Byte);
      OS << "\n";
    }
    OS << "\n";
  }

  uint32_t NumValueKinds = Func.getNumValueKinds();
  if (!NumValueKinds) {
    OS << "\n";
    return;
  }

  OS << "# Num Value Kinds:\n" << Func.getNumValueKinds() << "\n";
  for (uint32_t VK = 0; VK < IPVK_Last + 1; VK++) {
    uint32_t NS = Func.getNumValueSites(VK);
    if (!NS)
      continue;
    OS << "# ValueKind = " << ValueProfKindStr[VK] << ":\n" << VK << "\n";
    OS << "# NumValueSites:\n" << NS << "\n";
    for (uint32_t S = 0; S < NS; S++) {
      auto VD = Func.getValueArrayForSite(VK, S);
      OS << VD.size() << "\n";
      for (const auto &V : VD) {
        if (VK == IPVK_IndirectCallTarget || VK == IPVK_VTableTarget)
          OS << Symtab.getFuncOrVarNameIfDefined(V.Value) << ":" << V.Count
             << "\n";
        else
          OS << V.Value << ":" << V.Count << "\n";
      }
    }
  }

  OS << "\n";
}

Error InstrProfWriter::writeText(raw_fd_ostream &OS) {
  // Check CS first since it implies an IR level profile.
  if (static_cast<bool>(ProfileKind & InstrProfKind::ContextSensitive))
    OS << "# CSIR level Instrumentation Flag\n:csir\n";
  else if (static_cast<bool>(ProfileKind & InstrProfKind::IRInstrumentation))
    OS << "# IR level Instrumentation Flag\n:ir\n";

  if (static_cast<bool>(ProfileKind &
                        InstrProfKind::FunctionEntryInstrumentation))
    OS << "# Always instrument the function entry block\n:entry_first\n";
  if (static_cast<bool>(ProfileKind & InstrProfKind::SingleByteCoverage))
    OS << "# Instrument block coverage\n:single_byte_coverage\n";
  InstrProfSymtab Symtab;

  using FuncPair = detail::DenseMapPair<uint64_t, InstrProfRecord>;
  using RecordType = std::pair<StringRef, FuncPair>;
  SmallVector<RecordType, 4> OrderedFuncData;

  for (const auto &I : FunctionData) {
    if (shouldEncodeData(I.getValue())) {
      if (Error E = Symtab.addFuncName(I.getKey()))
        return E;
      for (const auto &Func : I.getValue())
        OrderedFuncData.push_back(std::make_pair(I.getKey(), Func));
    }
  }

  for (const auto &VTableName : VTableNames)
    if (Error E = Symtab.addVTableName(VTableName.getKey()))
      return E;

  if (static_cast<bool>(ProfileKind & InstrProfKind::TemporalProfile))
    writeTextTemporalProfTraceData(OS, Symtab);

  llvm::sort(OrderedFuncData, [](const RecordType &A, const RecordType &B) {
    return std::tie(A.first, A.second.first) <
           std::tie(B.first, B.second.first);
  });

  for (const auto &record : OrderedFuncData) {
    const StringRef &Name = record.first;
    const FuncPair &Func = record.second;
    writeRecordInText(Name, Func.first, Func.second, Symtab, OS);
  }

  for (const auto &record : OrderedFuncData) {
    const FuncPair &Func = record.second;
    if (Error E = validateRecord(Func.second))
      return E;
  }

  return Error::success();
}

void InstrProfWriter::writeTextTemporalProfTraceData(raw_fd_ostream &OS,
                                                     InstrProfSymtab &Symtab) {
  OS << ":temporal_prof_traces\n";
  OS << "# Num Temporal Profile Traces:\n" << TemporalProfTraces.size() << "\n";
  OS << "# Temporal Profile Trace Stream Size:\n"
     << TemporalProfTraceStreamSize << "\n";
  for (auto &Trace : TemporalProfTraces) {
    OS << "# Weight:\n" << Trace.Weight << "\n";
    for (auto &NameRef : Trace.FunctionNameRefs)
      OS << Symtab.getFuncOrVarName(NameRef) << ",";
    OS << "\n";
  }
  OS << "\n";
}

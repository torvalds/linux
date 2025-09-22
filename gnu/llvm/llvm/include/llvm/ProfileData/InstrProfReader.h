//===- InstrProfReader.h - Instrumented profiling readers -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for reading profiling data for instrumentation
// based PGO and coverage.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_INSTRPROFREADER_H
#define LLVM_PROFILEDATA_INSTRPROFREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/Object/BuildID.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/InstrProfCorrelator.h"
#include "llvm/ProfileData/MemProf.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OnDiskHashTable.h"
#include "llvm/Support/SwapByteOrder.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

class InstrProfReader;

namespace vfs {
class FileSystem;
} // namespace vfs

/// A file format agnostic iterator over profiling data.
template <class record_type = NamedInstrProfRecord,
          class reader_type = InstrProfReader>
class InstrProfIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type = record_type;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type *;
  using reference = value_type &;

private:
  reader_type *Reader = nullptr;
  value_type Record;

  void increment() {
    if (Error E = Reader->readNextRecord(Record)) {
      // Handle errors in the reader.
      InstrProfError::take(std::move(E));
      *this = InstrProfIterator();
    }
  }

public:
  InstrProfIterator() = default;
  InstrProfIterator(reader_type *Reader) : Reader(Reader) { increment(); }

  InstrProfIterator &operator++() {
    increment();
    return *this;
  }
  bool operator==(const InstrProfIterator &RHS) const {
    return Reader == RHS.Reader;
  }
  bool operator!=(const InstrProfIterator &RHS) const {
    return Reader != RHS.Reader;
  }
  value_type &operator*() { return Record; }
  value_type *operator->() { return &Record; }
};

/// Base class and interface for reading profiling data of any known instrprof
/// format. Provides an iterator over NamedInstrProfRecords.
class InstrProfReader {
  instrprof_error LastError = instrprof_error::success;
  std::string LastErrorMsg;

public:
  InstrProfReader() = default;
  virtual ~InstrProfReader() = default;

  /// Read the header.  Required before reading first record.
  virtual Error readHeader() = 0;

  /// Read a single record.
  virtual Error readNextRecord(NamedInstrProfRecord &Record) = 0;

  /// Read a list of binary ids.
  virtual Error readBinaryIds(std::vector<llvm::object::BuildID> &BinaryIds) {
    return success();
  }

  /// Print binary ids.
  virtual Error printBinaryIds(raw_ostream &OS) { return success(); };

  /// Iterator over profile data.
  InstrProfIterator<> begin() { return InstrProfIterator<>(this); }
  InstrProfIterator<> end() { return InstrProfIterator<>(); }

  /// Return the profile version.
  virtual uint64_t getVersion() const = 0;

  virtual bool isIRLevelProfile() const = 0;

  virtual bool hasCSIRLevelProfile() const = 0;

  virtual bool instrEntryBBEnabled() const = 0;

  /// Return true if the profile has single byte counters representing coverage.
  virtual bool hasSingleByteCoverage() const = 0;

  /// Return true if the profile only instruments function entries.
  virtual bool functionEntryOnly() const = 0;

  /// Return true if profile includes a memory profile.
  virtual bool hasMemoryProfile() const = 0;

  /// Return true if this has a temporal profile.
  virtual bool hasTemporalProfile() const = 0;

  /// Returns a BitsetEnum describing the attributes of the profile. To check
  /// individual attributes prefer using the helpers above.
  virtual InstrProfKind getProfileKind() const = 0;

  /// Return the PGO symtab. There are three different readers:
  /// Raw, Text, and Indexed profile readers. The first two types
  /// of readers are used only by llvm-profdata tool, while the indexed
  /// profile reader is also used by llvm-cov tool and the compiler (
  /// backend or frontend). Since creating PGO symtab can create
  /// significant runtime and memory overhead (as it touches data
  /// for the whole program), InstrProfSymtab for the indexed profile
  /// reader should be created on demand and it is recommended to be
  /// only used for dumping purpose with llvm-proftool, not with the
  /// compiler.
  virtual InstrProfSymtab &getSymtab() = 0;

  /// Compute the sum of counts and return in Sum.
  void accumulateCounts(CountSumOrPercent &Sum, bool IsCS);

protected:
  std::unique_ptr<InstrProfSymtab> Symtab;
  /// A list of temporal profile traces.
  SmallVector<TemporalProfTraceTy> TemporalProfTraces;
  /// The total number of temporal profile traces seen.
  uint64_t TemporalProfTraceStreamSize = 0;

  /// Set the current error and return same.
  Error error(instrprof_error Err, const std::string &ErrMsg = "") {
    LastError = Err;
    LastErrorMsg = ErrMsg;
    if (Err == instrprof_error::success)
      return Error::success();
    return make_error<InstrProfError>(Err, ErrMsg);
  }

  Error error(Error &&E) {
    handleAllErrors(std::move(E), [&](const InstrProfError &IPE) {
      LastError = IPE.get();
      LastErrorMsg = IPE.getMessage();
    });
    return make_error<InstrProfError>(LastError, LastErrorMsg);
  }

  /// Clear the current error and return a successful one.
  Error success() { return error(instrprof_error::success); }

public:
  /// Return true if the reader has finished reading the profile data.
  bool isEOF() { return LastError == instrprof_error::eof; }

  /// Return true if the reader encountered an error reading profiling data.
  bool hasError() { return LastError != instrprof_error::success && !isEOF(); }

  /// Get the current error.
  Error getError() {
    if (hasError())
      return make_error<InstrProfError>(LastError, LastErrorMsg);
    return Error::success();
  }

  /// Factory method to create an appropriately typed reader for the given
  /// instrprof file.
  static Expected<std::unique_ptr<InstrProfReader>>
  create(const Twine &Path, vfs::FileSystem &FS,
         const InstrProfCorrelator *Correlator = nullptr,
         std::function<void(Error)> Warn = nullptr);

  static Expected<std::unique_ptr<InstrProfReader>>
  create(std::unique_ptr<MemoryBuffer> Buffer,
         const InstrProfCorrelator *Correlator = nullptr,
         std::function<void(Error)> Warn = nullptr);

  /// \param Weight for raw profiles use this as the temporal profile trace
  ///               weight
  /// \returns a list of temporal profile traces.
  virtual SmallVector<TemporalProfTraceTy> &
  getTemporalProfTraces(std::optional<uint64_t> Weight = {}) {
    // For non-raw profiles we ignore the input weight and instead use the
    // weights already in the traces.
    return TemporalProfTraces;
  }
  /// \returns the total number of temporal profile traces seen.
  uint64_t getTemporalProfTraceStreamSize() {
    return TemporalProfTraceStreamSize;
  }
};

/// Reader for the simple text based instrprof format.
///
/// This format is a simple text format that's suitable for test data. Records
/// are separated by one or more blank lines, and record fields are separated by
/// new lines.
///
/// Each record consists of a function name, a function hash, a number of
/// counters, and then each counter value, in that order.
class TextInstrProfReader : public InstrProfReader {
private:
  /// The profile data file contents.
  std::unique_ptr<MemoryBuffer> DataBuffer;
  /// Iterator over the profile data.
  line_iterator Line;
  /// The attributes of the current profile.
  InstrProfKind ProfileKind = InstrProfKind::Unknown;

  Error readValueProfileData(InstrProfRecord &Record);

  Error readTemporalProfTraceData();

public:
  TextInstrProfReader(std::unique_ptr<MemoryBuffer> DataBuffer_)
      : DataBuffer(std::move(DataBuffer_)), Line(*DataBuffer, true, '#') {}
  TextInstrProfReader(const TextInstrProfReader &) = delete;
  TextInstrProfReader &operator=(const TextInstrProfReader &) = delete;

  /// Return true if the given buffer is in text instrprof format.
  static bool hasFormat(const MemoryBuffer &Buffer);

  // Text format does not have version, so return 0.
  uint64_t getVersion() const override { return 0; }

  bool isIRLevelProfile() const override {
    return static_cast<bool>(ProfileKind & InstrProfKind::IRInstrumentation);
  }

  bool hasCSIRLevelProfile() const override {
    return static_cast<bool>(ProfileKind & InstrProfKind::ContextSensitive);
  }

  bool instrEntryBBEnabled() const override {
    return static_cast<bool>(ProfileKind &
                             InstrProfKind::FunctionEntryInstrumentation);
  }

  bool hasSingleByteCoverage() const override {
    return static_cast<bool>(ProfileKind & InstrProfKind::SingleByteCoverage);
  }

  bool functionEntryOnly() const override {
    return static_cast<bool>(ProfileKind & InstrProfKind::FunctionEntryOnly);
  }

  bool hasMemoryProfile() const override {
    // TODO: Add support for text format memory profiles.
    return false;
  }

  bool hasTemporalProfile() const override {
    return static_cast<bool>(ProfileKind & InstrProfKind::TemporalProfile);
  }

  InstrProfKind getProfileKind() const override { return ProfileKind; }

  /// Read the header.
  Error readHeader() override;

  /// Read a single record.
  Error readNextRecord(NamedInstrProfRecord &Record) override;

  InstrProfSymtab &getSymtab() override {
    assert(Symtab);
    return *Symtab;
  }
};

/// Reader for the raw instrprof binary format from runtime.
///
/// This format is a raw memory dump of the instrumentation-based profiling data
/// from the runtime.  It has no index.
///
/// Templated on the unsigned type whose size matches pointers on the platform
/// that wrote the profile.
template <class IntPtrT>
class RawInstrProfReader : public InstrProfReader {
private:
  /// The profile data file contents.
  std::unique_ptr<MemoryBuffer> DataBuffer;
  /// If available, this hold the ProfileData array used to correlate raw
  /// instrumentation data to their functions.
  const InstrProfCorrelatorImpl<IntPtrT> *Correlator;
  /// A list of timestamps paired with a function name reference.
  std::vector<std::pair<uint64_t, uint64_t>> TemporalProfTimestamps;
  bool ShouldSwapBytes;
  // The value of the version field of the raw profile data header. The lower 32
  // bits specifies the format version and the most significant 32 bits specify
  // the variant types of the profile.
  uint64_t Version;
  uint64_t CountersDelta;
  uint64_t BitmapDelta;
  uint64_t NamesDelta;
  const RawInstrProf::ProfileData<IntPtrT> *Data;
  const RawInstrProf::ProfileData<IntPtrT> *DataEnd;
  const RawInstrProf::VTableProfileData<IntPtrT> *VTableBegin = nullptr;
  const RawInstrProf::VTableProfileData<IntPtrT> *VTableEnd = nullptr;
  const char *CountersStart;
  const char *CountersEnd;
  const char *BitmapStart;
  const char *BitmapEnd;
  const char *NamesStart;
  const char *NamesEnd;
  const char *VNamesStart = nullptr;
  const char *VNamesEnd = nullptr;
  // After value profile is all read, this pointer points to
  // the header of next profile data (if exists)
  const uint8_t *ValueDataStart;
  uint32_t ValueKindLast;
  uint32_t CurValueDataSize;
  std::vector<llvm::object::BuildID> BinaryIds;

  std::function<void(Error)> Warn;

  /// Maxium counter value 2^56.
  static const uint64_t MaxCounterValue = (1ULL << 56);

public:
  RawInstrProfReader(std::unique_ptr<MemoryBuffer> DataBuffer,
                     const InstrProfCorrelator *Correlator,
                     std::function<void(Error)> Warn)
      : DataBuffer(std::move(DataBuffer)),
        Correlator(dyn_cast_or_null<const InstrProfCorrelatorImpl<IntPtrT>>(
            Correlator)),
        Warn(Warn) {}
  RawInstrProfReader(const RawInstrProfReader &) = delete;
  RawInstrProfReader &operator=(const RawInstrProfReader &) = delete;

  static bool hasFormat(const MemoryBuffer &DataBuffer);
  Error readHeader() override;
  Error readNextRecord(NamedInstrProfRecord &Record) override;
  Error readBinaryIds(std::vector<llvm::object::BuildID> &BinaryIds) override;
  Error printBinaryIds(raw_ostream &OS) override;

  uint64_t getVersion() const override { return Version; }

  bool isIRLevelProfile() const override {
    return (Version & VARIANT_MASK_IR_PROF) != 0;
  }

  bool hasCSIRLevelProfile() const override {
    return (Version & VARIANT_MASK_CSIR_PROF) != 0;
  }

  bool instrEntryBBEnabled() const override {
    return (Version & VARIANT_MASK_INSTR_ENTRY) != 0;
  }

  bool hasSingleByteCoverage() const override {
    return (Version & VARIANT_MASK_BYTE_COVERAGE) != 0;
  }

  bool functionEntryOnly() const override {
    return (Version & VARIANT_MASK_FUNCTION_ENTRY_ONLY) != 0;
  }

  bool hasMemoryProfile() const override {
    // Memory profiles have a separate raw format, so this should never be set.
    assert(!(Version & VARIANT_MASK_MEMPROF));
    return false;
  }

  bool hasTemporalProfile() const override {
    return (Version & VARIANT_MASK_TEMPORAL_PROF) != 0;
  }

  /// Returns a BitsetEnum describing the attributes of the raw instr profile.
  InstrProfKind getProfileKind() const override;

  InstrProfSymtab &getSymtab() override {
    assert(Symtab.get());
    return *Symtab.get();
  }

  SmallVector<TemporalProfTraceTy> &
  getTemporalProfTraces(std::optional<uint64_t> Weight = {}) override;

private:
  Error createSymtab(InstrProfSymtab &Symtab);
  Error readNextHeader(const char *CurrentPos);
  Error readHeader(const RawInstrProf::Header &Header);

  template <class IntT> IntT swap(IntT Int) const {
    return ShouldSwapBytes ? llvm::byteswap(Int) : Int;
  }

  llvm::endianness getDataEndianness() const {
    if (!ShouldSwapBytes)
      return llvm::endianness::native;
    if (llvm::endianness::native == llvm::endianness::little)
      return llvm::endianness::big;
    else
      return llvm::endianness::little;
  }

  inline uint8_t getNumPaddingBytes(uint64_t SizeInBytes) {
    return 7 & (sizeof(uint64_t) - SizeInBytes % sizeof(uint64_t));
  }

  Error readName(NamedInstrProfRecord &Record);
  Error readFuncHash(NamedInstrProfRecord &Record);
  Error readRawCounts(InstrProfRecord &Record);
  Error readRawBitmapBytes(InstrProfRecord &Record);
  Error readValueProfilingData(InstrProfRecord &Record);
  bool atEnd() const { return Data == DataEnd; }

  void advanceData() {
    // `CountersDelta` is a constant zero when using debug info correlation.
    if (!Correlator) {
      // The initial CountersDelta is the in-memory address difference between
      // the data and counts sections:
      // start(__llvm_prf_cnts) - start(__llvm_prf_data)
      // As we advance to the next record, we maintain the correct CountersDelta
      // with respect to the next record.
      CountersDelta -= sizeof(*Data);
      BitmapDelta -= sizeof(*Data);
    }
    Data++;
    ValueDataStart += CurValueDataSize;
  }

  const char *getNextHeaderPos() const {
      assert(atEnd());
      return (const char *)ValueDataStart;
  }

  StringRef getName(uint64_t NameRef) const {
    return Symtab->getFuncOrVarName(swap(NameRef));
  }

  int getCounterTypeSize() const {
    return hasSingleByteCoverage() ? sizeof(uint8_t) : sizeof(uint64_t);
  }
};

using RawInstrProfReader32 = RawInstrProfReader<uint32_t>;
using RawInstrProfReader64 = RawInstrProfReader<uint64_t>;

namespace IndexedInstrProf {

enum class HashT : uint32_t;

} // end namespace IndexedInstrProf

/// Trait for lookups into the on-disk hash table for the binary instrprof
/// format.
class InstrProfLookupTrait {
  std::vector<NamedInstrProfRecord> DataBuffer;
  IndexedInstrProf::HashT HashType;
  unsigned FormatVersion;
  // Endianness of the input value profile data.
  // It should be LE by default, but can be changed
  // for testing purpose.
  llvm::endianness ValueProfDataEndianness = llvm::endianness::little;

public:
  InstrProfLookupTrait(IndexedInstrProf::HashT HashType, unsigned FormatVersion)
      : HashType(HashType), FormatVersion(FormatVersion) {}

  using data_type = ArrayRef<NamedInstrProfRecord>;

  using internal_key_type = StringRef;
  using external_key_type = StringRef;
  using hash_value_type = uint64_t;
  using offset_type = uint64_t;

  static bool EqualKey(StringRef A, StringRef B) { return A == B; }
  static StringRef GetInternalKey(StringRef K) { return K; }
  static StringRef GetExternalKey(StringRef K) { return K; }

  hash_value_type ComputeHash(StringRef K);

  static std::pair<offset_type, offset_type>
  ReadKeyDataLength(const unsigned char *&D) {
    using namespace support;

    offset_type KeyLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    offset_type DataLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    return std::make_pair(KeyLen, DataLen);
  }

  StringRef ReadKey(const unsigned char *D, offset_type N) {
    return StringRef((const char *)D, N);
  }

  bool readValueProfilingData(const unsigned char *&D,
                              const unsigned char *const End);
  data_type ReadData(StringRef K, const unsigned char *D, offset_type N);

  // Used for testing purpose only.
  void setValueProfDataEndianness(llvm::endianness Endianness) {
    ValueProfDataEndianness = Endianness;
  }
};

struct InstrProfReaderIndexBase {
  virtual ~InstrProfReaderIndexBase() = default;

  // Read all the profile records with the same key pointed to the current
  // iterator.
  virtual Error getRecords(ArrayRef<NamedInstrProfRecord> &Data) = 0;

  // Read all the profile records with the key equal to FuncName
  virtual Error getRecords(StringRef FuncName,
                                     ArrayRef<NamedInstrProfRecord> &Data) = 0;
  virtual void advanceToNextKey() = 0;
  virtual bool atEnd() const = 0;
  virtual void setValueProfDataEndianness(llvm::endianness Endianness) = 0;
  virtual uint64_t getVersion() const = 0;
  virtual bool isIRLevelProfile() const = 0;
  virtual bool hasCSIRLevelProfile() const = 0;
  virtual bool instrEntryBBEnabled() const = 0;
  virtual bool hasSingleByteCoverage() const = 0;
  virtual bool functionEntryOnly() const = 0;
  virtual bool hasMemoryProfile() const = 0;
  virtual bool hasTemporalProfile() const = 0;
  virtual InstrProfKind getProfileKind() const = 0;
  virtual Error populateSymtab(InstrProfSymtab &) = 0;
};

using OnDiskHashTableImplV3 =
    OnDiskIterableChainedHashTable<InstrProfLookupTrait>;

using MemProfRecordHashTable =
    OnDiskIterableChainedHashTable<memprof::RecordLookupTrait>;
using MemProfFrameHashTable =
    OnDiskIterableChainedHashTable<memprof::FrameLookupTrait>;
using MemProfCallStackHashTable =
    OnDiskIterableChainedHashTable<memprof::CallStackLookupTrait>;

template <typename HashTableImpl>
class InstrProfReaderItaniumRemapper;

template <typename HashTableImpl>
class InstrProfReaderIndex : public InstrProfReaderIndexBase {
private:
  std::unique_ptr<HashTableImpl> HashTable;
  typename HashTableImpl::data_iterator RecordIterator;
  uint64_t FormatVersion;

  friend class InstrProfReaderItaniumRemapper<HashTableImpl>;

public:
  InstrProfReaderIndex(const unsigned char *Buckets,
                       const unsigned char *const Payload,
                       const unsigned char *const Base,
                       IndexedInstrProf::HashT HashType, uint64_t Version);
  ~InstrProfReaderIndex() override = default;

  Error getRecords(ArrayRef<NamedInstrProfRecord> &Data) override;
  Error getRecords(StringRef FuncName,
                   ArrayRef<NamedInstrProfRecord> &Data) override;
  void advanceToNextKey() override { RecordIterator++; }

  bool atEnd() const override {
    return RecordIterator == HashTable->data_end();
  }

  void setValueProfDataEndianness(llvm::endianness Endianness) override {
    HashTable->getInfoObj().setValueProfDataEndianness(Endianness);
  }

  uint64_t getVersion() const override { return GET_VERSION(FormatVersion); }

  bool isIRLevelProfile() const override {
    return (FormatVersion & VARIANT_MASK_IR_PROF) != 0;
  }

  bool hasCSIRLevelProfile() const override {
    return (FormatVersion & VARIANT_MASK_CSIR_PROF) != 0;
  }

  bool instrEntryBBEnabled() const override {
    return (FormatVersion & VARIANT_MASK_INSTR_ENTRY) != 0;
  }

  bool hasSingleByteCoverage() const override {
    return (FormatVersion & VARIANT_MASK_BYTE_COVERAGE) != 0;
  }

  bool functionEntryOnly() const override {
    return (FormatVersion & VARIANT_MASK_FUNCTION_ENTRY_ONLY) != 0;
  }

  bool hasMemoryProfile() const override {
    return (FormatVersion & VARIANT_MASK_MEMPROF) != 0;
  }

  bool hasTemporalProfile() const override {
    return (FormatVersion & VARIANT_MASK_TEMPORAL_PROF) != 0;
  }

  InstrProfKind getProfileKind() const override;

  Error populateSymtab(InstrProfSymtab &Symtab) override {
    // FIXME: the create method calls 'finalizeSymtab' and sorts a bunch of
    // arrays/maps. Since there are other data sources other than 'HashTable' to
    // populate a symtab, it might make sense to have something like this
    // 1. Let each data source populate Symtab and init the arrays/maps without
    // calling 'finalizeSymtab'
    // 2. Call 'finalizeSymtab' once to get all arrays/maps sorted if needed.
    return Symtab.create(HashTable->keys());
  }
};

/// Name matcher supporting fuzzy matching of symbol names to names in profiles.
class InstrProfReaderRemapper {
public:
  virtual ~InstrProfReaderRemapper() = default;
  virtual Error populateRemappings() { return Error::success(); }
  virtual Error getRecords(StringRef FuncName,
                           ArrayRef<NamedInstrProfRecord> &Data) = 0;
};

class IndexedMemProfReader {
private:
  /// The MemProf version.
  memprof::IndexedVersion Version = memprof::Version0;
  /// MemProf profile schema (if available).
  memprof::MemProfSchema Schema;
  /// MemProf record profile data on-disk indexed via llvm::md5(FunctionName).
  std::unique_ptr<MemProfRecordHashTable> MemProfRecordTable;
  /// MemProf frame profile data on-disk indexed via frame id.
  std::unique_ptr<MemProfFrameHashTable> MemProfFrameTable;
  /// MemProf call stack data on-disk indexed via call stack id.
  std::unique_ptr<MemProfCallStackHashTable> MemProfCallStackTable;
  /// The starting address of the frame array.
  const unsigned char *FrameBase = nullptr;
  /// The starting address of the call stack array.
  const unsigned char *CallStackBase = nullptr;

  Error deserializeV012(const unsigned char *Start, const unsigned char *Ptr,
                        uint64_t FirstWord);
  Error deserializeV3(const unsigned char *Start, const unsigned char *Ptr);

public:
  IndexedMemProfReader() = default;

  Error deserialize(const unsigned char *Start, uint64_t MemProfOffset);

  Expected<memprof::MemProfRecord>
  getMemProfRecord(const uint64_t FuncNameHash) const;
};

/// Reader for the indexed binary instrprof format.
class IndexedInstrProfReader : public InstrProfReader {
private:
  /// The profile data file contents.
  std::unique_ptr<MemoryBuffer> DataBuffer;
  /// The profile remapping file contents.
  std::unique_ptr<MemoryBuffer> RemappingBuffer;
  /// The index into the profile data.
  std::unique_ptr<InstrProfReaderIndexBase> Index;
  /// The profile remapping file contents.
  std::unique_ptr<InstrProfReaderRemapper> Remapper;
  /// Profile summary data.
  std::unique_ptr<ProfileSummary> Summary;
  /// Context sensitive profile summary data.
  std::unique_ptr<ProfileSummary> CS_Summary;
  IndexedMemProfReader MemProfReader;
  /// The compressed vtable names, to be used for symtab construction.
  /// A compiler that reads indexed profiles could construct symtab from module
  /// IR so it doesn't need the decompressed names.
  StringRef VTableName;
  /// A memory buffer holding binary ids.
  ArrayRef<uint8_t> BinaryIdsBuffer;

  // Index to the current record in the record array.
  unsigned RecordIndex = 0;

  // Read the profile summary. Return a pointer pointing to one byte past the
  // end of the summary data if it exists or the input \c Cur.
  // \c UseCS indicates whether to use the context-sensitive profile summary.
  const unsigned char *readSummary(IndexedInstrProf::ProfVersion Version,
                                   const unsigned char *Cur, bool UseCS);

public:
  IndexedInstrProfReader(
      std::unique_ptr<MemoryBuffer> DataBuffer,
      std::unique_ptr<MemoryBuffer> RemappingBuffer = nullptr)
      : DataBuffer(std::move(DataBuffer)),
        RemappingBuffer(std::move(RemappingBuffer)) {}
  IndexedInstrProfReader(const IndexedInstrProfReader &) = delete;
  IndexedInstrProfReader &operator=(const IndexedInstrProfReader &) = delete;

  /// Return the profile version.
  uint64_t getVersion() const override { return Index->getVersion(); }
  bool isIRLevelProfile() const override { return Index->isIRLevelProfile(); }
  bool hasCSIRLevelProfile() const override {
    return Index->hasCSIRLevelProfile();
  }

  bool instrEntryBBEnabled() const override {
    return Index->instrEntryBBEnabled();
  }

  bool hasSingleByteCoverage() const override {
    return Index->hasSingleByteCoverage();
  }

  bool functionEntryOnly() const override { return Index->functionEntryOnly(); }

  bool hasMemoryProfile() const override { return Index->hasMemoryProfile(); }

  bool hasTemporalProfile() const override {
    return Index->hasTemporalProfile();
  }

  /// Returns a BitsetEnum describing the attributes of the indexed instr
  /// profile.
  InstrProfKind getProfileKind() const override {
    return Index->getProfileKind();
  }

  /// Return true if the given buffer is in an indexed instrprof format.
  static bool hasFormat(const MemoryBuffer &DataBuffer);

  /// Read the file header.
  Error readHeader() override;
  /// Read a single record.
  Error readNextRecord(NamedInstrProfRecord &Record) override;

  /// Return the NamedInstrProfRecord associated with FuncName and FuncHash.
  /// When return a hash_mismatch error and MismatchedFuncSum is not nullptr,
  /// the sum of all counters in the mismatched function will be set to
  /// MismatchedFuncSum. If there are multiple instances of mismatched
  /// functions, MismatchedFuncSum returns the maximum. If \c FuncName is not
  /// found, try to lookup \c DeprecatedFuncName to handle profiles built by
  /// older compilers.
  Expected<InstrProfRecord>
  getInstrProfRecord(StringRef FuncName, uint64_t FuncHash,
                     StringRef DeprecatedFuncName = "",
                     uint64_t *MismatchedFuncSum = nullptr);

  /// Return the memprof record for the function identified by
  /// llvm::md5(Name).
  Expected<memprof::MemProfRecord> getMemProfRecord(uint64_t FuncNameHash) {
    return MemProfReader.getMemProfRecord(FuncNameHash);
  }

  /// Fill Counts with the profile data for the given function name.
  Error getFunctionCounts(StringRef FuncName, uint64_t FuncHash,
                          std::vector<uint64_t> &Counts);

  /// Fill Bitmap with the profile data for the given function name.
  Error getFunctionBitmap(StringRef FuncName, uint64_t FuncHash,
                          BitVector &Bitmap);

  /// Return the maximum of all known function counts.
  /// \c UseCS indicates whether to use the context-sensitive count.
  uint64_t getMaximumFunctionCount(bool UseCS) {
    if (UseCS) {
      assert(CS_Summary && "No context sensitive profile summary");
      return CS_Summary->getMaxFunctionCount();
    } else {
      assert(Summary && "No profile summary");
      return Summary->getMaxFunctionCount();
    }
  }

  /// Factory method to create an indexed reader.
  static Expected<std::unique_ptr<IndexedInstrProfReader>>
  create(const Twine &Path, vfs::FileSystem &FS,
         const Twine &RemappingPath = "");

  static Expected<std::unique_ptr<IndexedInstrProfReader>>
  create(std::unique_ptr<MemoryBuffer> Buffer,
         std::unique_ptr<MemoryBuffer> RemappingBuffer = nullptr);

  // Used for testing purpose only.
  void setValueProfDataEndianness(llvm::endianness Endianness) {
    Index->setValueProfDataEndianness(Endianness);
  }

  // See description in the base class. This interface is designed
  // to be used by llvm-profdata (for dumping). Avoid using this when
  // the client is the compiler.
  InstrProfSymtab &getSymtab() override;

  /// Return the profile summary.
  /// \c UseCS indicates whether to use the context-sensitive summary.
  ProfileSummary &getSummary(bool UseCS) {
    if (UseCS) {
      assert(CS_Summary && "No context sensitive summary");
      return *CS_Summary;
    } else {
      assert(Summary && "No profile summary");
      return *Summary;
    }
  }

  Error readBinaryIds(std::vector<llvm::object::BuildID> &BinaryIds) override;
  Error printBinaryIds(raw_ostream &OS) override;
};

} // end namespace llvm

#endif // LLVM_PROFILEDATA_INSTRPROFREADER_H

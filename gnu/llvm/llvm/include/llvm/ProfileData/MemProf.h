#ifndef LLVM_PROFILEDATA_MEMPROF_H_
#define LLVM_PROFILEDATA_MEMPROF_H_

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/ProfileData/MemProfData.inc"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"

#include <bitset>
#include <cstdint>
#include <optional>

namespace llvm {
namespace memprof {

struct MemProfRecord;

// The versions of the indexed MemProf format
enum IndexedVersion : uint64_t {
  // Version 0: This version didn't have a version field.
  Version0 = 0,
  // Version 1: Added a version field to the header.
  Version1 = 1,
  // Version 2: Added a call stack table.
  Version2 = 2,
  // Version 3: Added a radix tree for call stacks.  Switched to linear IDs for
  // frames and call stacks.
  Version3 = 3,
};

constexpr uint64_t MinimumSupportedVersion = Version0;
constexpr uint64_t MaximumSupportedVersion = Version3;

// Verify that the minimum and maximum satisfy the obvious constraint.
static_assert(MinimumSupportedVersion <= MaximumSupportedVersion);

enum class Meta : uint64_t {
  Start = 0,
#define MIBEntryDef(NameTag, Name, Type) NameTag,
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
  Size
};

using MemProfSchema = llvm::SmallVector<Meta, static_cast<int>(Meta::Size)>;

// Returns the full schema currently in use.
MemProfSchema getFullSchema();

// Returns the schema consisting of the fields used for hot cold memory hinting.
MemProfSchema getHotColdSchema();

// Holds the actual MemInfoBlock data with all fields. Contents may be read or
// written partially by providing an appropriate schema to the serialize and
// deserialize methods.
struct PortableMemInfoBlock {
  PortableMemInfoBlock() = default;
  explicit PortableMemInfoBlock(const MemInfoBlock &Block,
                                const MemProfSchema &IncomingSchema) {
    for (const Meta Id : IncomingSchema)
      Schema.set(llvm::to_underlying(Id));
#define MIBEntryDef(NameTag, Name, Type) Name = Block.Name;
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
  }

  PortableMemInfoBlock(const MemProfSchema &Schema, const unsigned char *Ptr) {
    deserialize(Schema, Ptr);
  }

  // Read the contents of \p Ptr based on the \p Schema to populate the
  // MemInfoBlock member.
  void deserialize(const MemProfSchema &IncomingSchema,
                   const unsigned char *Ptr) {
    using namespace support;

    Schema.reset();
    for (const Meta Id : IncomingSchema) {
      switch (Id) {
#define MIBEntryDef(NameTag, Name, Type)                                       \
  case Meta::Name: {                                                           \
    Name = endian::readNext<Type, llvm::endianness::little>(Ptr);              \
  } break;
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
      default:
        llvm_unreachable("Unknown meta type id, is the profile collected from "
                         "a newer version of the runtime?");
      }

      Schema.set(llvm::to_underlying(Id));
    }
  }

  // Write the contents of the MemInfoBlock based on the \p Schema provided to
  // the raw_ostream \p OS.
  void serialize(const MemProfSchema &Schema, raw_ostream &OS) const {
    using namespace support;

    endian::Writer LE(OS, llvm::endianness::little);
    for (const Meta Id : Schema) {
      switch (Id) {
#define MIBEntryDef(NameTag, Name, Type)                                       \
  case Meta::Name: {                                                           \
    LE.write<Type>(Name);                                                      \
  } break;
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
      default:
        llvm_unreachable("Unknown meta type id, invalid input?");
      }
    }
  }

  // Print out the contents of the MemInfoBlock in YAML format.
  void printYAML(raw_ostream &OS) const {
    OS << "      MemInfoBlock:\n";
#define MIBEntryDef(NameTag, Name, Type)                                       \
  OS << "        " << #Name << ": " << Name << "\n";
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
    if (AccessHistogramSize > 0) {
      OS << "        " << "AccessHistogramValues" << ":";
      for (uint32_t I = 0; I < AccessHistogramSize; ++I) {
        OS << " " << ((uint64_t *)AccessHistogram)[I];
      }
      OS << "\n";
    }
  }

  // Return the schema, only for unit tests.
  std::bitset<llvm::to_underlying(Meta::Size)> getSchema() const {
    return Schema;
  }

  // Define getters for each type which can be called by analyses.
#define MIBEntryDef(NameTag, Name, Type)                                       \
  Type get##Name() const {                                                     \
    assert(Schema[llvm::to_underlying(Meta::Name)]);                           \
    return Name;                                                               \
  }
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef

  void clear() { *this = PortableMemInfoBlock(); }

  bool operator==(const PortableMemInfoBlock &Other) const {
    if (Other.Schema != Schema)
      return false;

#define MIBEntryDef(NameTag, Name, Type)                                       \
  if (Schema[llvm::to_underlying(Meta::Name)] &&                               \
      Other.get##Name() != get##Name())                                        \
    return false;
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
    return true;
  }

  bool operator!=(const PortableMemInfoBlock &Other) const {
    return !operator==(Other);
  }

  static size_t serializedSize(const MemProfSchema &Schema) {
    size_t Result = 0;

    for (const Meta Id : Schema) {
      switch (Id) {
#define MIBEntryDef(NameTag, Name, Type)                                       \
  case Meta::Name: {                                                           \
    Result += sizeof(Type);                                                    \
  } break;
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
      default:
        llvm_unreachable("Unknown meta type id, invalid input?");
      }
    }

    return Result;
  }

private:
  // The set of available fields, indexed by Meta::Name.
  std::bitset<llvm::to_underlying(Meta::Size)> Schema;

#define MIBEntryDef(NameTag, Name, Type) Type Name = Type();
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
};

// A type representing the id generated by hashing the contents of the Frame.
using FrameId = uint64_t;
// A type representing the id to index into the frame array.
using LinearFrameId = uint32_t;
// Describes a call frame for a dynamic allocation context. The contents of
// the frame are populated by symbolizing the stack depot call frame from the
// compiler runtime.
struct Frame {
  // A uuid (uint64_t) identifying the function. It is obtained by
  // llvm::md5(FunctionName) which returns the lower 64 bits.
  GlobalValue::GUID Function;
  // The symbol name for the function. Only populated in the Frame by the reader
  // if requested during initialization. This field should not be serialized.
  std::unique_ptr<std::string> SymbolName;
  // The source line offset of the call from the beginning of parent function.
  uint32_t LineOffset;
  // The source column number of the call to help distinguish multiple calls
  // on the same line.
  uint32_t Column;
  // Whether the current frame is inlined.
  bool IsInlineFrame;

  Frame(const Frame &Other) {
    Function = Other.Function;
    SymbolName = Other.SymbolName
                     ? std::make_unique<std::string>(*Other.SymbolName)
                     : nullptr;
    LineOffset = Other.LineOffset;
    Column = Other.Column;
    IsInlineFrame = Other.IsInlineFrame;
  }

  Frame(GlobalValue::GUID Hash, uint32_t Off, uint32_t Col, bool Inline)
      : Function(Hash), LineOffset(Off), Column(Col), IsInlineFrame(Inline) {}

  bool operator==(const Frame &Other) const {
    // Ignore the SymbolName field to avoid a string compare. Comparing the
    // function hash serves the same purpose.
    return Other.Function == Function && Other.LineOffset == LineOffset &&
           Other.Column == Column && Other.IsInlineFrame == IsInlineFrame;
  }

  Frame &operator=(const Frame &Other) {
    Function = Other.Function;
    SymbolName = Other.SymbolName
                     ? std::make_unique<std::string>(*Other.SymbolName)
                     : nullptr;
    LineOffset = Other.LineOffset;
    Column = Other.Column;
    IsInlineFrame = Other.IsInlineFrame;
    return *this;
  }

  bool operator!=(const Frame &Other) const { return !operator==(Other); }

  bool hasSymbolName() const { return !!SymbolName; }

  StringRef getSymbolName() const {
    assert(hasSymbolName());
    return *SymbolName;
  }

  std::string getSymbolNameOr(StringRef Alt) const {
    return std::string(hasSymbolName() ? getSymbolName() : Alt);
  }

  // Write the contents of the frame to the ostream \p OS.
  void serialize(raw_ostream &OS) const {
    using namespace support;

    endian::Writer LE(OS, llvm::endianness::little);

    // If the type of the GlobalValue::GUID changes, then we need to update
    // the reader and the writer.
    static_assert(std::is_same<GlobalValue::GUID, uint64_t>::value,
                  "Expect GUID to be uint64_t.");
    LE.write<uint64_t>(Function);

    LE.write<uint32_t>(LineOffset);
    LE.write<uint32_t>(Column);
    LE.write<bool>(IsInlineFrame);
  }

  // Read a frame from char data which has been serialized as little endian.
  static Frame deserialize(const unsigned char *Ptr) {
    using namespace support;

    const uint64_t F =
        endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
    const uint32_t L =
        endian::readNext<uint32_t, llvm::endianness::little>(Ptr);
    const uint32_t C =
        endian::readNext<uint32_t, llvm::endianness::little>(Ptr);
    const bool I = endian::readNext<bool, llvm::endianness::little>(Ptr);
    return Frame(/*Function=*/F, /*LineOffset=*/L, /*Column=*/C,
                 /*IsInlineFrame=*/I);
  }

  // Returns the size of the frame information.
  static constexpr size_t serializedSize() {
    return sizeof(Frame::Function) + sizeof(Frame::LineOffset) +
           sizeof(Frame::Column) + sizeof(Frame::IsInlineFrame);
  }

  // Print the frame information in YAML format.
  void printYAML(raw_ostream &OS) const {
    OS << "      -\n"
       << "        Function: " << Function << "\n"
       << "        SymbolName: " << getSymbolNameOr("<None>") << "\n"
       << "        LineOffset: " << LineOffset << "\n"
       << "        Column: " << Column << "\n"
       << "        Inline: " << IsInlineFrame << "\n";
  }

  // Return a hash value based on the contents of the frame. Here we don't use
  // hashing from llvm ADT since we are going to persist the hash id, the hash
  // combine algorithm in ADT uses a new randomized seed each time.
  inline FrameId hash() const {
    auto HashCombine = [](auto Value, size_t Seed) {
      std::hash<decltype(Value)> Hasher;
      // The constant used below is the 64 bit representation of the fractional
      // part of the golden ratio. Used here for the randomness in their bit
      // pattern.
      return Hasher(Value) + 0x9e3779b97f4a7c15 + (Seed << 6) + (Seed >> 2);
    };

    size_t Result = 0;
    Result ^= HashCombine(Function, Result);
    Result ^= HashCombine(LineOffset, Result);
    Result ^= HashCombine(Column, Result);
    Result ^= HashCombine(IsInlineFrame, Result);
    return static_cast<FrameId>(Result);
  }
};

// A type representing the index into the table of call stacks.
using CallStackId = uint64_t;

// A type representing the index into the call stack array.
using LinearCallStackId = uint32_t;

// Holds allocation information in a space efficient format where frames are
// represented using unique identifiers.
struct IndexedAllocationInfo {
  // The dynamic calling context for the allocation in bottom-up (leaf-to-root)
  // order. Frame contents are stored out-of-line.
  // TODO: Remove once we fully transition to CSId.
  llvm::SmallVector<FrameId> CallStack;
  // Conceptually the same as above.  We are going to keep both CallStack and
  // CallStackId while we are transitioning from CallStack to CallStackId.
  CallStackId CSId = 0;
  // The statistics obtained from the runtime for the allocation.
  PortableMemInfoBlock Info;

  IndexedAllocationInfo() = default;
  IndexedAllocationInfo(ArrayRef<FrameId> CS, CallStackId CSId,
                        const MemInfoBlock &MB,
                        const MemProfSchema &Schema = getFullSchema())
      : CallStack(CS.begin(), CS.end()), CSId(CSId), Info(MB, Schema) {}

  // Returns the size in bytes when this allocation info struct is serialized.
  size_t serializedSize(const MemProfSchema &Schema,
                        IndexedVersion Version) const;

  bool operator==(const IndexedAllocationInfo &Other) const {
    if (Other.Info != Info)
      return false;

    if (Other.CSId != CSId)
      return false;
    return true;
  }

  bool operator!=(const IndexedAllocationInfo &Other) const {
    return !operator==(Other);
  }
};

// Holds allocation information with frame contents inline. The type should
// be used for temporary in-memory instances.
struct AllocationInfo {
  // Same as IndexedAllocationInfo::CallStack with the frame contents inline.
  std::vector<Frame> CallStack;
  // Same as IndexedAllocationInfo::Info;
  PortableMemInfoBlock Info;

  AllocationInfo() = default;
  AllocationInfo(
      const IndexedAllocationInfo &IndexedAI,
      llvm::function_ref<const Frame(const FrameId)> IdToFrameCallback) {
    for (const FrameId &Id : IndexedAI.CallStack) {
      CallStack.push_back(IdToFrameCallback(Id));
    }
    Info = IndexedAI.Info;
  }

  void printYAML(raw_ostream &OS) const {
    OS << "    -\n";
    OS << "      Callstack:\n";
    // TODO: Print out the frame on one line with to make it easier for deep
    // callstacks once we have a test to check valid YAML is generated.
    for (const Frame &F : CallStack) {
      F.printYAML(OS);
    }
    Info.printYAML(OS);
  }
};

// Holds the memprof profile information for a function. The internal
// representation stores frame ids for efficiency. This representation should
// be used in the profile conversion and manipulation tools.
struct IndexedMemProfRecord {
  // Memory allocation sites in this function for which we have memory
  // profiling data.
  llvm::SmallVector<IndexedAllocationInfo> AllocSites;
  // Holds call sites in this function which are part of some memory
  // allocation context. We store this as a list of locations, each with its
  // list of inline locations in bottom-up order i.e. from leaf to root. The
  // inline location list may include additional entries, users should pick
  // the last entry in the list with the same function GUID.
  llvm::SmallVector<llvm::SmallVector<FrameId>> CallSites;
  // Conceptually the same as above.  We are going to keep both CallSites and
  // CallSiteIds while we are transitioning from CallSites to CallSiteIds.
  llvm::SmallVector<CallStackId> CallSiteIds;

  void clear() {
    AllocSites.clear();
    CallSites.clear();
  }

  void merge(const IndexedMemProfRecord &Other) {
    // TODO: Filter out duplicates which may occur if multiple memprof
    // profiles are merged together using llvm-profdata.
    AllocSites.append(Other.AllocSites);
    CallSites.append(Other.CallSites);
  }

  size_t serializedSize(const MemProfSchema &Schema,
                        IndexedVersion Version) const;

  bool operator==(const IndexedMemProfRecord &Other) const {
    if (Other.AllocSites != AllocSites)
      return false;

    if (Other.CallSiteIds != CallSiteIds)
      return false;
    return true;
  }

  // Serializes the memprof records in \p Records to the ostream \p OS based
  // on the schema provided in \p Schema.
  void serialize(const MemProfSchema &Schema, raw_ostream &OS,
                 IndexedVersion Version,
                 llvm::DenseMap<CallStackId, LinearCallStackId>
                     *MemProfCallStackIndexes = nullptr) const;

  // Deserializes memprof records from the Buffer.
  static IndexedMemProfRecord deserialize(const MemProfSchema &Schema,
                                          const unsigned char *Buffer,
                                          IndexedVersion Version);

  // Convert IndexedMemProfRecord to MemProfRecord.  Callback is used to
  // translate CallStackId to call stacks with frames inline.
  MemProfRecord toMemProfRecord(
      llvm::function_ref<std::vector<Frame>(const CallStackId)> Callback) const;

  // Returns the GUID for the function name after canonicalization. For
  // memprof, we remove any .llvm suffix added by LTO. MemProfRecords are
  // mapped to functions using this GUID.
  static GlobalValue::GUID getGUID(const StringRef FunctionName);
};

// Holds the memprof profile information for a function. The internal
// representation stores frame contents inline. This representation should
// be used for small amount of temporary, in memory instances.
struct MemProfRecord {
  // Same as IndexedMemProfRecord::AllocSites with frame contents inline.
  llvm::SmallVector<AllocationInfo> AllocSites;
  // Same as IndexedMemProfRecord::CallSites with frame contents inline.
  llvm::SmallVector<std::vector<Frame>> CallSites;

  MemProfRecord() = default;
  MemProfRecord(
      const IndexedMemProfRecord &Record,
      llvm::function_ref<const Frame(const FrameId Id)> IdToFrameCallback) {
    for (const IndexedAllocationInfo &IndexedAI : Record.AllocSites) {
      AllocSites.emplace_back(IndexedAI, IdToFrameCallback);
    }
    for (const ArrayRef<FrameId> Site : Record.CallSites) {
      std::vector<Frame> Frames;
      for (const FrameId Id : Site) {
        Frames.push_back(IdToFrameCallback(Id));
      }
      CallSites.push_back(Frames);
    }
  }

  // Prints out the contents of the memprof record in YAML.
  void print(llvm::raw_ostream &OS) const {
    if (!AllocSites.empty()) {
      OS << "    AllocSites:\n";
      for (const AllocationInfo &N : AllocSites)
        N.printYAML(OS);
    }

    if (!CallSites.empty()) {
      OS << "    CallSites:\n";
      for (const std::vector<Frame> &Frames : CallSites) {
        for (const Frame &F : Frames) {
          OS << "    -\n";
          F.printYAML(OS);
        }
      }
    }
  }
};

// Reads a memprof schema from a buffer. All entries in the buffer are
// interpreted as uint64_t. The first entry in the buffer denotes the number of
// ids in the schema. Subsequent entries are integers which map to memprof::Meta
// enum class entries. After successfully reading the schema, the pointer is one
// byte past the schema contents.
Expected<MemProfSchema> readMemProfSchema(const unsigned char *&Buffer);

// Trait for reading IndexedMemProfRecord data from the on-disk hash table.
class RecordLookupTrait {
public:
  using data_type = const IndexedMemProfRecord &;
  using internal_key_type = uint64_t;
  using external_key_type = uint64_t;
  using hash_value_type = uint64_t;
  using offset_type = uint64_t;

  RecordLookupTrait() = delete;
  RecordLookupTrait(IndexedVersion V, const MemProfSchema &S)
      : Version(V), Schema(S) {}

  static bool EqualKey(uint64_t A, uint64_t B) { return A == B; }
  static uint64_t GetInternalKey(uint64_t K) { return K; }
  static uint64_t GetExternalKey(uint64_t K) { return K; }

  hash_value_type ComputeHash(uint64_t K) { return K; }

  static std::pair<offset_type, offset_type>
  ReadKeyDataLength(const unsigned char *&D) {
    using namespace support;

    offset_type KeyLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    offset_type DataLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    return std::make_pair(KeyLen, DataLen);
  }

  uint64_t ReadKey(const unsigned char *D, offset_type /*Unused*/) {
    using namespace support;
    return endian::readNext<external_key_type, llvm::endianness::little>(D);
  }

  data_type ReadData(uint64_t K, const unsigned char *D,
                     offset_type /*Unused*/) {
    Record = IndexedMemProfRecord::deserialize(Schema, D, Version);
    return Record;
  }

private:
  // Holds the MemProf version.
  IndexedVersion Version;
  // Holds the memprof schema used to deserialize records.
  MemProfSchema Schema;
  // Holds the records from one function deserialized from the indexed format.
  IndexedMemProfRecord Record;
};

// Trait for writing IndexedMemProfRecord data to the on-disk hash table.
class RecordWriterTrait {
public:
  using key_type = uint64_t;
  using key_type_ref = uint64_t;

  using data_type = IndexedMemProfRecord;
  using data_type_ref = IndexedMemProfRecord &;

  using hash_value_type = uint64_t;
  using offset_type = uint64_t;

private:
  // Pointer to the memprof schema to use for the generator.
  const MemProfSchema *Schema;
  // The MemProf version to use for the serialization.
  IndexedVersion Version;

  // Mappings from CallStackId to the indexes into the call stack array.
  llvm::DenseMap<CallStackId, LinearCallStackId> *MemProfCallStackIndexes;

public:
  // We do not support the default constructor, which does not set Version.
  RecordWriterTrait() = delete;
  RecordWriterTrait(
      const MemProfSchema *Schema, IndexedVersion V,
      llvm::DenseMap<CallStackId, LinearCallStackId> *MemProfCallStackIndexes)
      : Schema(Schema), Version(V),
        MemProfCallStackIndexes(MemProfCallStackIndexes) {}

  static hash_value_type ComputeHash(key_type_ref K) { return K; }

  std::pair<offset_type, offset_type>
  EmitKeyDataLength(raw_ostream &Out, key_type_ref K, data_type_ref V) {
    using namespace support;

    endian::Writer LE(Out, llvm::endianness::little);
    offset_type N = sizeof(K);
    LE.write<offset_type>(N);
    offset_type M = V.serializedSize(*Schema, Version);
    LE.write<offset_type>(M);
    return std::make_pair(N, M);
  }

  void EmitKey(raw_ostream &Out, key_type_ref K, offset_type /*Unused*/) {
    using namespace support;
    endian::Writer LE(Out, llvm::endianness::little);
    LE.write<uint64_t>(K);
  }

  void EmitData(raw_ostream &Out, key_type_ref /*Unused*/, data_type_ref V,
                offset_type /*Unused*/) {
    assert(Schema != nullptr && "MemProf schema is not initialized!");
    V.serialize(*Schema, Out, Version, MemProfCallStackIndexes);
    // Clear the IndexedMemProfRecord which results in clearing/freeing its
    // vectors of allocs and callsites. This is owned by the associated on-disk
    // hash table, but unused after this point. See also the comment added to
    // the client which constructs the on-disk hash table for this trait.
    V.clear();
  }
};

// Trait for writing frame mappings to the on-disk hash table.
class FrameWriterTrait {
public:
  using key_type = FrameId;
  using key_type_ref = FrameId;

  using data_type = Frame;
  using data_type_ref = Frame &;

  using hash_value_type = FrameId;
  using offset_type = uint64_t;

  static hash_value_type ComputeHash(key_type_ref K) { return K; }

  static std::pair<offset_type, offset_type>
  EmitKeyDataLength(raw_ostream &Out, key_type_ref K, data_type_ref V) {
    using namespace support;
    endian::Writer LE(Out, llvm::endianness::little);
    offset_type N = sizeof(K);
    LE.write<offset_type>(N);
    offset_type M = V.serializedSize();
    LE.write<offset_type>(M);
    return std::make_pair(N, M);
  }

  void EmitKey(raw_ostream &Out, key_type_ref K, offset_type /*Unused*/) {
    using namespace support;
    endian::Writer LE(Out, llvm::endianness::little);
    LE.write<key_type>(K);
  }

  void EmitData(raw_ostream &Out, key_type_ref /*Unused*/, data_type_ref V,
                offset_type /*Unused*/) {
    V.serialize(Out);
  }
};

// Trait for reading frame mappings from the on-disk hash table.
class FrameLookupTrait {
public:
  using data_type = const Frame;
  using internal_key_type = FrameId;
  using external_key_type = FrameId;
  using hash_value_type = FrameId;
  using offset_type = uint64_t;

  static bool EqualKey(internal_key_type A, internal_key_type B) {
    return A == B;
  }
  static uint64_t GetInternalKey(internal_key_type K) { return K; }
  static uint64_t GetExternalKey(external_key_type K) { return K; }

  hash_value_type ComputeHash(internal_key_type K) { return K; }

  static std::pair<offset_type, offset_type>
  ReadKeyDataLength(const unsigned char *&D) {
    using namespace support;

    offset_type KeyLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    offset_type DataLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    return std::make_pair(KeyLen, DataLen);
  }

  uint64_t ReadKey(const unsigned char *D, offset_type /*Unused*/) {
    using namespace support;
    return endian::readNext<external_key_type, llvm::endianness::little>(D);
  }

  data_type ReadData(uint64_t K, const unsigned char *D,
                     offset_type /*Unused*/) {
    return Frame::deserialize(D);
  }
};

// Trait for writing call stacks to the on-disk hash table.
class CallStackWriterTrait {
public:
  using key_type = CallStackId;
  using key_type_ref = CallStackId;

  using data_type = llvm::SmallVector<FrameId>;
  using data_type_ref = llvm::SmallVector<FrameId> &;

  using hash_value_type = CallStackId;
  using offset_type = uint64_t;

  static hash_value_type ComputeHash(key_type_ref K) { return K; }

  static std::pair<offset_type, offset_type>
  EmitKeyDataLength(raw_ostream &Out, key_type_ref K, data_type_ref V) {
    using namespace support;
    endian::Writer LE(Out, llvm::endianness::little);
    // We do not explicitly emit the key length because it is a constant.
    offset_type N = sizeof(K);
    offset_type M = sizeof(FrameId) * V.size();
    LE.write<offset_type>(M);
    return std::make_pair(N, M);
  }

  void EmitKey(raw_ostream &Out, key_type_ref K, offset_type /*Unused*/) {
    using namespace support;
    endian::Writer LE(Out, llvm::endianness::little);
    LE.write<key_type>(K);
  }

  void EmitData(raw_ostream &Out, key_type_ref /*Unused*/, data_type_ref V,
                offset_type /*Unused*/) {
    using namespace support;
    endian::Writer LE(Out, llvm::endianness::little);
    // Emit the frames.  We do not explicitly emit the length of the vector
    // because it can be inferred from the data length.
    for (FrameId F : V)
      LE.write<FrameId>(F);
  }
};

// Trait for reading call stack mappings from the on-disk hash table.
class CallStackLookupTrait {
public:
  using data_type = const llvm::SmallVector<FrameId>;
  using internal_key_type = CallStackId;
  using external_key_type = CallStackId;
  using hash_value_type = CallStackId;
  using offset_type = uint64_t;

  static bool EqualKey(internal_key_type A, internal_key_type B) {
    return A == B;
  }
  static uint64_t GetInternalKey(internal_key_type K) { return K; }
  static uint64_t GetExternalKey(external_key_type K) { return K; }

  hash_value_type ComputeHash(internal_key_type K) { return K; }

  static std::pair<offset_type, offset_type>
  ReadKeyDataLength(const unsigned char *&D) {
    using namespace support;

    // We do not explicitly read the key length because it is a constant.
    offset_type KeyLen = sizeof(external_key_type);
    offset_type DataLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    return std::make_pair(KeyLen, DataLen);
  }

  uint64_t ReadKey(const unsigned char *D, offset_type /*Unused*/) {
    using namespace support;
    return endian::readNext<external_key_type, llvm::endianness::little>(D);
  }

  data_type ReadData(uint64_t K, const unsigned char *D, offset_type Length) {
    using namespace support;
    llvm::SmallVector<FrameId> CS;
    // Derive the number of frames from the data length.
    uint64_t NumFrames = Length / sizeof(FrameId);
    assert(Length % sizeof(FrameId) == 0);
    CS.reserve(NumFrames);
    for (size_t I = 0; I != NumFrames; ++I) {
      FrameId F = endian::readNext<FrameId, llvm::endianness::little>(D);
      CS.push_back(F);
    }
    return CS;
  }
};

// Compute a CallStackId for a given call stack.
CallStackId hashCallStack(ArrayRef<FrameId> CS);

namespace detail {
// "Dereference" the iterator from DenseMap or OnDiskChainedHashTable.  We have
// to do so in one of two different ways depending on the type of the hash
// table.
template <typename value_type, typename IterTy>
value_type DerefIterator(IterTy Iter) {
  using deref_type = llvm::remove_cvref_t<decltype(*Iter)>;
  if constexpr (std::is_same_v<deref_type, value_type>)
    return *Iter;
  else
    return Iter->second;
}
} // namespace detail

// A function object that returns a frame for a given FrameId.
template <typename MapTy> struct FrameIdConverter {
  std::optional<FrameId> LastUnmappedId;
  MapTy &Map;

  FrameIdConverter() = delete;
  FrameIdConverter(MapTy &Map) : Map(Map) {}

  // Delete the copy constructor and copy assignment operator to avoid a
  // situation where a copy of FrameIdConverter gets an error in LastUnmappedId
  // while the original instance doesn't.
  FrameIdConverter(const FrameIdConverter &) = delete;
  FrameIdConverter &operator=(const FrameIdConverter &) = delete;

  Frame operator()(FrameId Id) {
    auto Iter = Map.find(Id);
    if (Iter == Map.end()) {
      LastUnmappedId = Id;
      return Frame(0, 0, 0, false);
    }
    return detail::DerefIterator<Frame>(Iter);
  }
};

// A function object that returns a call stack for a given CallStackId.
template <typename MapTy> struct CallStackIdConverter {
  std::optional<CallStackId> LastUnmappedId;
  MapTy &Map;
  llvm::function_ref<Frame(FrameId)> FrameIdToFrame;

  CallStackIdConverter() = delete;
  CallStackIdConverter(MapTy &Map,
                       llvm::function_ref<Frame(FrameId)> FrameIdToFrame)
      : Map(Map), FrameIdToFrame(FrameIdToFrame) {}

  // Delete the copy constructor and copy assignment operator to avoid a
  // situation where a copy of CallStackIdConverter gets an error in
  // LastUnmappedId while the original instance doesn't.
  CallStackIdConverter(const CallStackIdConverter &) = delete;
  CallStackIdConverter &operator=(const CallStackIdConverter &) = delete;

  std::vector<Frame> operator()(CallStackId CSId) {
    std::vector<Frame> Frames;
    auto CSIter = Map.find(CSId);
    if (CSIter == Map.end()) {
      LastUnmappedId = CSId;
    } else {
      llvm::SmallVector<FrameId> CS =
          detail::DerefIterator<llvm::SmallVector<FrameId>>(CSIter);
      Frames.reserve(CS.size());
      for (FrameId Id : CS)
        Frames.push_back(FrameIdToFrame(Id));
    }
    return Frames;
  }
};

// A function object that returns a Frame stored at a given index into the Frame
// array in the profile.
struct LinearFrameIdConverter {
  const unsigned char *FrameBase;

  LinearFrameIdConverter() = delete;
  LinearFrameIdConverter(const unsigned char *FrameBase)
      : FrameBase(FrameBase) {}

  Frame operator()(LinearFrameId LinearId) {
    uint64_t Offset = static_cast<uint64_t>(LinearId) * Frame::serializedSize();
    return Frame::deserialize(FrameBase + Offset);
  }
};

// A function object that returns a call stack stored at a given index into the
// call stack array in the profile.
struct LinearCallStackIdConverter {
  const unsigned char *CallStackBase;
  std::function<Frame(LinearFrameId)> FrameIdToFrame;

  LinearCallStackIdConverter() = delete;
  LinearCallStackIdConverter(const unsigned char *CallStackBase,
                             std::function<Frame(LinearFrameId)> FrameIdToFrame)
      : CallStackBase(CallStackBase), FrameIdToFrame(FrameIdToFrame) {}

  std::vector<Frame> operator()(LinearCallStackId LinearCSId) {
    std::vector<Frame> Frames;

    const unsigned char *Ptr =
        CallStackBase +
        static_cast<uint64_t>(LinearCSId) * sizeof(LinearFrameId);
    uint32_t NumFrames =
        support::endian::readNext<uint32_t, llvm::endianness::little>(Ptr);
    Frames.reserve(NumFrames);
    for (; NumFrames; --NumFrames) {
      LinearFrameId Elem =
          support::endian::read<LinearFrameId, llvm::endianness::little>(Ptr);
      // Follow a pointer to the parent, if any.  See comments below on
      // CallStackRadixTreeBuilder for the description of the radix tree format.
      if (static_cast<std::make_signed_t<LinearFrameId>>(Elem) < 0) {
        Ptr += (-Elem) * sizeof(LinearFrameId);
        Elem =
            support::endian::read<LinearFrameId, llvm::endianness::little>(Ptr);
      }
      // We shouldn't encounter another pointer.
      assert(static_cast<std::make_signed_t<LinearFrameId>>(Elem) >= 0);
      Frames.push_back(FrameIdToFrame(Elem));
      Ptr += sizeof(LinearFrameId);
    }

    return Frames;
  }
};

struct IndexedMemProfData {
  // A map to hold memprof data per function. The lower 64 bits obtained from
  // the md5 hash of the function name is used to index into the map.
  llvm::MapVector<GlobalValue::GUID, IndexedMemProfRecord> Records;

  // A map to hold frame id to frame mappings. The mappings are used to
  // convert IndexedMemProfRecord to MemProfRecords with frame information
  // inline.
  llvm::MapVector<FrameId, Frame> Frames;

  // A map to hold call stack id to call stacks.
  llvm::MapVector<CallStackId, llvm::SmallVector<FrameId>> CallStacks;
};

struct FrameStat {
  // The number of occurrences of a given FrameId.
  uint64_t Count = 0;
  // The sum of indexes where a given FrameId shows up.
  uint64_t PositionSum = 0;
};

// Compute a histogram of Frames in call stacks.
llvm::DenseMap<FrameId, FrameStat>
computeFrameHistogram(llvm::MapVector<CallStackId, llvm::SmallVector<FrameId>>
                          &MemProfCallStackData);

// Construct a radix tree of call stacks.
//
// A set of call stacks might look like:
//
// CallStackId 1:  f1 -> f2 -> f3
// CallStackId 2:  f1 -> f2 -> f4 -> f5
// CallStackId 3:  f1 -> f2 -> f4 -> f6
// CallStackId 4:  f7 -> f8 -> f9
//
// where each fn refers to a stack frame.
//
// Since we expect a lot of common prefixes, we can compress the call stacks
// into a radix tree like:
//
// CallStackId 1:  f1 -> f2 -> f3
//                       |
// CallStackId 2:        +---> f4 -> f5
//                             |
// CallStackId 3:              +---> f6
//
// CallStackId 4:  f7 -> f8 -> f9
//
// Now, we are interested in retrieving call stacks for a given CallStackId, so
// we just need a pointer from a given call stack to its parent.  For example,
// CallStackId 2 would point to CallStackId 1 as a parent.
//
// We serialize the radix tree above into a single array along with the length
// of each call stack and pointers to the parent call stacks.
//
// Index:              0  1  2  3  4  5  6  7  8  9 10 11 12 13 14
// Array:             L3 f9 f8 f7 L4 f6 J3 L4 f5 f4 J3 L3 f3 f2 f1
//                     ^           ^        ^           ^
//                     |           |        |           |
// CallStackId 4:  0 --+           |        |           |
// CallStackId 3:  4 --------------+        |           |
// CallStackId 2:  7 -----------------------+           |
// CallStackId 1: 11 -----------------------------------+
//
// - LN indicates the length of a call stack, encoded as ordinary integer N.
//
// - JN indicates a pointer to the parent, encoded as -N.
//
// The radix tree allows us to reconstruct call stacks in the leaf-to-root
// order as we scan the array from left ro right while following pointers to
// parents along the way.
//
// For example, if we are decoding CallStackId 2, we start a forward traversal
// at Index 7, noting the call stack length of 4 and obtaining f5 and f4.  When
// we see J3 at Index 10, we resume a forward traversal at Index 13 = 10 + 3,
// picking up f2 and f1.  We are done after collecting 4 frames as indicated at
// the beginning of the traversal.
//
// On-disk IndexedMemProfRecord will refer to call stacks by their indexes into
// the radix tree array, so we do not explicitly encode mappings like:
// "CallStackId 1 -> 11".
class CallStackRadixTreeBuilder {
  // The radix tree array.
  std::vector<LinearFrameId> RadixArray;

  // Mapping from CallStackIds to indexes into RadixArray.
  llvm::DenseMap<CallStackId, LinearCallStackId> CallStackPos;

  // In build, we partition a given call stack into two parts -- the prefix
  // that's common with the previously encoded call stack and the frames beyond
  // the common prefix -- the unique portion.  Then we want to find out where
  // the common prefix is stored in RadixArray so that we can link the unique
  // portion to the common prefix.  Indexes, declared below, helps with our
  // needs.  Intuitively, Indexes tells us where each of the previously encoded
  // call stack is stored in RadixArray.  More formally, Indexes satisfies:
  //
  //   RadixArray[Indexes[I]] == Prev[I]
  //
  // for every I, where Prev is the the call stack in the root-to-leaf order
  // previously encoded by build.  (Note that Prev, as passed to
  // encodeCallStack, is in the leaf-to-root order.)
  //
  // For example, if the call stack being encoded shares 5 frames at the root of
  // the call stack with the previously encoded call stack,
  // RadixArray[Indexes[0]] is the root frame of the common prefix.
  // RadixArray[Indexes[5 - 1]] is the last frame of the common prefix.
  std::vector<LinearCallStackId> Indexes;

  using CSIdPair = std::pair<CallStackId, llvm::SmallVector<FrameId>>;

  // Encode a call stack into RadixArray.  Return the starting index within
  // RadixArray.
  LinearCallStackId encodeCallStack(
      const llvm::SmallVector<FrameId> *CallStack,
      const llvm::SmallVector<FrameId> *Prev,
      const llvm::DenseMap<FrameId, LinearFrameId> &MemProfFrameIndexes);

public:
  CallStackRadixTreeBuilder() = default;

  // Build a radix tree array.
  void build(llvm::MapVector<CallStackId, llvm::SmallVector<FrameId>>
                 &&MemProfCallStackData,
             const llvm::DenseMap<FrameId, LinearFrameId> &MemProfFrameIndexes,
             llvm::DenseMap<FrameId, FrameStat> &FrameHistogram);

  const std::vector<LinearFrameId> &getRadixArray() const { return RadixArray; }

  llvm::DenseMap<CallStackId, LinearCallStackId> takeCallStackPos() {
    return std::move(CallStackPos);
  }
};

// Verify that each CallStackId is computed with hashCallStack.  This function
// is intended to help transition from CallStack to CSId in
// IndexedAllocationInfo.
void verifyIndexedMemProfRecord(const IndexedMemProfRecord &Record);

// Verify that each CallStackId is computed with hashCallStack.  This function
// is intended to help transition from CallStack to CSId in
// IndexedAllocationInfo.
void verifyFunctionProfileData(
    const llvm::MapVector<GlobalValue::GUID, IndexedMemProfRecord>
        &FunctionProfileData);
} // namespace memprof
} // namespace llvm

#endif // LLVM_PROFILEDATA_MEMPROF_H_

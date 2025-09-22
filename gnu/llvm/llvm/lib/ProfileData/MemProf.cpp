#include "llvm/ProfileData/MemProf.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/BLAKE3.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/HashBuilder.h"

namespace llvm {
namespace memprof {
MemProfSchema getFullSchema() {
  MemProfSchema List;
#define MIBEntryDef(NameTag, Name, Type) List.push_back(Meta::Name);
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
  return List;
}

MemProfSchema getHotColdSchema() {
  return {Meta::AllocCount, Meta::TotalSize, Meta::TotalLifetime,
          Meta::TotalLifetimeAccessDensity};
}

static size_t serializedSizeV0(const IndexedAllocationInfo &IAI,
                               const MemProfSchema &Schema) {
  size_t Size = 0;
  // The number of frames to serialize.
  Size += sizeof(uint64_t);
  // The callstack frame ids.
  Size += sizeof(FrameId) * IAI.CallStack.size();
  // The size of the payload.
  Size += PortableMemInfoBlock::serializedSize(Schema);
  return Size;
}

static size_t serializedSizeV2(const IndexedAllocationInfo &IAI,
                               const MemProfSchema &Schema) {
  size_t Size = 0;
  // The CallStackId
  Size += sizeof(CallStackId);
  // The size of the payload.
  Size += PortableMemInfoBlock::serializedSize(Schema);
  return Size;
}

static size_t serializedSizeV3(const IndexedAllocationInfo &IAI,
                               const MemProfSchema &Schema) {
  size_t Size = 0;
  // The linear call stack ID.
  Size += sizeof(LinearCallStackId);
  // The size of the payload.
  Size += PortableMemInfoBlock::serializedSize(Schema);
  return Size;
}

size_t IndexedAllocationInfo::serializedSize(const MemProfSchema &Schema,
                                             IndexedVersion Version) const {
  switch (Version) {
  case Version0:
  case Version1:
    return serializedSizeV0(*this, Schema);
  case Version2:
    return serializedSizeV2(*this, Schema);
  case Version3:
    return serializedSizeV3(*this, Schema);
  }
  llvm_unreachable("unsupported MemProf version");
}

static size_t serializedSizeV0(const IndexedMemProfRecord &Record,
                               const MemProfSchema &Schema) {
  // The number of alloc sites to serialize.
  size_t Result = sizeof(uint64_t);
  for (const IndexedAllocationInfo &N : Record.AllocSites)
    Result += N.serializedSize(Schema, Version0);

  // The number of callsites we have information for.
  Result += sizeof(uint64_t);
  for (const auto &Frames : Record.CallSites) {
    // The number of frame ids to serialize.
    Result += sizeof(uint64_t);
    Result += Frames.size() * sizeof(FrameId);
  }
  return Result;
}

static size_t serializedSizeV2(const IndexedMemProfRecord &Record,
                               const MemProfSchema &Schema) {
  // The number of alloc sites to serialize.
  size_t Result = sizeof(uint64_t);
  for (const IndexedAllocationInfo &N : Record.AllocSites)
    Result += N.serializedSize(Schema, Version2);

  // The number of callsites we have information for.
  Result += sizeof(uint64_t);
  // The CallStackId
  Result += Record.CallSiteIds.size() * sizeof(CallStackId);
  return Result;
}

static size_t serializedSizeV3(const IndexedMemProfRecord &Record,
                               const MemProfSchema &Schema) {
  // The number of alloc sites to serialize.
  size_t Result = sizeof(uint64_t);
  for (const IndexedAllocationInfo &N : Record.AllocSites)
    Result += N.serializedSize(Schema, Version3);

  // The number of callsites we have information for.
  Result += sizeof(uint64_t);
  // The linear call stack ID.
  Result += Record.CallSiteIds.size() * sizeof(LinearCallStackId);
  return Result;
}

size_t IndexedMemProfRecord::serializedSize(const MemProfSchema &Schema,
                                            IndexedVersion Version) const {
  switch (Version) {
  case Version0:
  case Version1:
    return serializedSizeV0(*this, Schema);
  case Version2:
    return serializedSizeV2(*this, Schema);
  case Version3:
    return serializedSizeV3(*this, Schema);
  }
  llvm_unreachable("unsupported MemProf version");
}

static void serializeV0(const IndexedMemProfRecord &Record,
                        const MemProfSchema &Schema, raw_ostream &OS) {
  using namespace support;

  endian::Writer LE(OS, llvm::endianness::little);

  LE.write<uint64_t>(Record.AllocSites.size());
  for (const IndexedAllocationInfo &N : Record.AllocSites) {
    LE.write<uint64_t>(N.CallStack.size());
    for (const FrameId &Id : N.CallStack)
      LE.write<FrameId>(Id);
    N.Info.serialize(Schema, OS);
  }

  // Related contexts.
  LE.write<uint64_t>(Record.CallSites.size());
  for (const auto &Frames : Record.CallSites) {
    LE.write<uint64_t>(Frames.size());
    for (const FrameId &Id : Frames)
      LE.write<FrameId>(Id);
  }
}

static void serializeV2(const IndexedMemProfRecord &Record,
                        const MemProfSchema &Schema, raw_ostream &OS) {
  using namespace support;

  endian::Writer LE(OS, llvm::endianness::little);

  LE.write<uint64_t>(Record.AllocSites.size());
  for (const IndexedAllocationInfo &N : Record.AllocSites) {
    LE.write<CallStackId>(N.CSId);
    N.Info.serialize(Schema, OS);
  }

  // Related contexts.
  LE.write<uint64_t>(Record.CallSiteIds.size());
  for (const auto &CSId : Record.CallSiteIds)
    LE.write<CallStackId>(CSId);
}

static void serializeV3(
    const IndexedMemProfRecord &Record, const MemProfSchema &Schema,
    raw_ostream &OS,
    llvm::DenseMap<CallStackId, LinearCallStackId> &MemProfCallStackIndexes) {
  using namespace support;

  endian::Writer LE(OS, llvm::endianness::little);

  LE.write<uint64_t>(Record.AllocSites.size());
  for (const IndexedAllocationInfo &N : Record.AllocSites) {
    assert(MemProfCallStackIndexes.contains(N.CSId));
    LE.write<LinearCallStackId>(MemProfCallStackIndexes[N.CSId]);
    N.Info.serialize(Schema, OS);
  }

  // Related contexts.
  LE.write<uint64_t>(Record.CallSiteIds.size());
  for (const auto &CSId : Record.CallSiteIds) {
    assert(MemProfCallStackIndexes.contains(CSId));
    LE.write<LinearCallStackId>(MemProfCallStackIndexes[CSId]);
  }
}

void IndexedMemProfRecord::serialize(
    const MemProfSchema &Schema, raw_ostream &OS, IndexedVersion Version,
    llvm::DenseMap<CallStackId, LinearCallStackId> *MemProfCallStackIndexes)
    const {
  switch (Version) {
  case Version0:
  case Version1:
    serializeV0(*this, Schema, OS);
    return;
  case Version2:
    serializeV2(*this, Schema, OS);
    return;
  case Version3:
    serializeV3(*this, Schema, OS, *MemProfCallStackIndexes);
    return;
  }
  llvm_unreachable("unsupported MemProf version");
}

static IndexedMemProfRecord deserializeV0(const MemProfSchema &Schema,
                                          const unsigned char *Ptr) {
  using namespace support;

  IndexedMemProfRecord Record;

  // Read the meminfo nodes.
  const uint64_t NumNodes =
      endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
  for (uint64_t I = 0; I < NumNodes; I++) {
    IndexedAllocationInfo Node;
    const uint64_t NumFrames =
        endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
    for (uint64_t J = 0; J < NumFrames; J++) {
      const FrameId Id =
          endian::readNext<FrameId, llvm::endianness::little>(Ptr);
      Node.CallStack.push_back(Id);
    }
    Node.CSId = hashCallStack(Node.CallStack);
    Node.Info.deserialize(Schema, Ptr);
    Ptr += PortableMemInfoBlock::serializedSize(Schema);
    Record.AllocSites.push_back(Node);
  }

  // Read the callsite information.
  const uint64_t NumCtxs =
      endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
  for (uint64_t J = 0; J < NumCtxs; J++) {
    const uint64_t NumFrames =
        endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
    llvm::SmallVector<FrameId> Frames;
    Frames.reserve(NumFrames);
    for (uint64_t K = 0; K < NumFrames; K++) {
      const FrameId Id =
          endian::readNext<FrameId, llvm::endianness::little>(Ptr);
      Frames.push_back(Id);
    }
    Record.CallSites.push_back(Frames);
    Record.CallSiteIds.push_back(hashCallStack(Frames));
  }

  return Record;
}

static IndexedMemProfRecord deserializeV2(const MemProfSchema &Schema,
                                          const unsigned char *Ptr) {
  using namespace support;

  IndexedMemProfRecord Record;

  // Read the meminfo nodes.
  const uint64_t NumNodes =
      endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
  Record.AllocSites.reserve(NumNodes);
  for (uint64_t I = 0; I < NumNodes; I++) {
    IndexedAllocationInfo Node;
    Node.CSId = endian::readNext<CallStackId, llvm::endianness::little>(Ptr);
    Node.Info.deserialize(Schema, Ptr);
    Ptr += PortableMemInfoBlock::serializedSize(Schema);
    Record.AllocSites.push_back(Node);
  }

  // Read the callsite information.
  const uint64_t NumCtxs =
      endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
  Record.CallSiteIds.reserve(NumCtxs);
  for (uint64_t J = 0; J < NumCtxs; J++) {
    CallStackId CSId =
        endian::readNext<CallStackId, llvm::endianness::little>(Ptr);
    Record.CallSiteIds.push_back(CSId);
  }

  return Record;
}

static IndexedMemProfRecord deserializeV3(const MemProfSchema &Schema,
                                          const unsigned char *Ptr) {
  using namespace support;

  IndexedMemProfRecord Record;

  // Read the meminfo nodes.
  const uint64_t NumNodes =
      endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
  Record.AllocSites.reserve(NumNodes);
  for (uint64_t I = 0; I < NumNodes; I++) {
    IndexedAllocationInfo Node;
    Node.CSId =
        endian::readNext<LinearCallStackId, llvm::endianness::little>(Ptr);
    Node.Info.deserialize(Schema, Ptr);
    Ptr += PortableMemInfoBlock::serializedSize(Schema);
    Record.AllocSites.push_back(Node);
  }

  // Read the callsite information.
  const uint64_t NumCtxs =
      endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
  Record.CallSiteIds.reserve(NumCtxs);
  for (uint64_t J = 0; J < NumCtxs; J++) {
    // We are storing LinearCallStackId in CallSiteIds, which is a vector of
    // CallStackId.  Assert that CallStackId is no smaller than
    // LinearCallStackId.
    static_assert(sizeof(LinearCallStackId) <= sizeof(CallStackId));
    LinearCallStackId CSId =
        endian::readNext<LinearCallStackId, llvm::endianness::little>(Ptr);
    Record.CallSiteIds.push_back(CSId);
  }

  return Record;
}

IndexedMemProfRecord
IndexedMemProfRecord::deserialize(const MemProfSchema &Schema,
                                  const unsigned char *Ptr,
                                  IndexedVersion Version) {
  switch (Version) {
  case Version0:
  case Version1:
    return deserializeV0(Schema, Ptr);
  case Version2:
    return deserializeV2(Schema, Ptr);
  case Version3:
    return deserializeV3(Schema, Ptr);
  }
  llvm_unreachable("unsupported MemProf version");
}

MemProfRecord IndexedMemProfRecord::toMemProfRecord(
    llvm::function_ref<std::vector<Frame>(const CallStackId)> Callback) const {
  MemProfRecord Record;

  Record.AllocSites.reserve(AllocSites.size());
  for (const IndexedAllocationInfo &IndexedAI : AllocSites) {
    AllocationInfo AI;
    AI.Info = IndexedAI.Info;
    AI.CallStack = Callback(IndexedAI.CSId);
    Record.AllocSites.push_back(std::move(AI));
  }

  Record.CallSites.reserve(CallSiteIds.size());
  for (CallStackId CSId : CallSiteIds)
    Record.CallSites.push_back(Callback(CSId));

  return Record;
}

GlobalValue::GUID IndexedMemProfRecord::getGUID(const StringRef FunctionName) {
  // Canonicalize the function name to drop suffixes such as ".llvm.". Note
  // we do not drop any ".__uniq." suffixes, as getCanonicalFnName does not drop
  // those by default. This is by design to differentiate internal linkage
  // functions during matching. By dropping the other suffixes we can then match
  // functions in the profile use phase prior to their addition. Note that this
  // applies to both instrumented and sampled function names.
  StringRef CanonicalName =
      sampleprof::FunctionSamples::getCanonicalFnName(FunctionName);

  // We use the function guid which we expect to be a uint64_t. At
  // this time, it is the lower 64 bits of the md5 of the canonical
  // function name.
  return Function::getGUID(CanonicalName);
}

Expected<MemProfSchema> readMemProfSchema(const unsigned char *&Buffer) {
  using namespace support;

  const unsigned char *Ptr = Buffer;
  const uint64_t NumSchemaIds =
      endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
  if (NumSchemaIds > static_cast<uint64_t>(Meta::Size)) {
    return make_error<InstrProfError>(instrprof_error::malformed,
                                      "memprof schema invalid");
  }

  MemProfSchema Result;
  for (size_t I = 0; I < NumSchemaIds; I++) {
    const uint64_t Tag =
        endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
    if (Tag >= static_cast<uint64_t>(Meta::Size)) {
      return make_error<InstrProfError>(instrprof_error::malformed,
                                        "memprof schema invalid");
    }
    Result.push_back(static_cast<Meta>(Tag));
  }
  // Advance the buffer to one past the schema if we succeeded.
  Buffer = Ptr;
  return Result;
}

CallStackId hashCallStack(ArrayRef<FrameId> CS) {
  llvm::HashBuilder<llvm::TruncatedBLAKE3<8>, llvm::endianness::little>
      HashBuilder;
  for (FrameId F : CS)
    HashBuilder.add(F);
  llvm::BLAKE3Result<8> Hash = HashBuilder.final();
  CallStackId CSId;
  std::memcpy(&CSId, Hash.data(), sizeof(Hash));
  return CSId;
}

// Encode a call stack into RadixArray.  Return the starting index within
// RadixArray.  For each call stack we encode, we emit two or three components
// into RadixArray.  If a given call stack doesn't have a common prefix relative
// to the previous one, we emit:
//
// - the frames in the given call stack in the root-to-leaf order
//
// - the length of the given call stack
//
// If a given call stack has a non-empty common prefix relative to the previous
// one, we emit:
//
// - the relative location of the common prefix, encoded as a negative number.
//
// - a portion of the given call stack that's beyond the common prefix
//
// - the length of the given call stack, including the length of the common
//   prefix.
//
// The resulting RadixArray requires a somewhat unintuitive backward traversal
// to reconstruct a call stack -- read the call stack length and scan backward
// while collecting frames in the leaf to root order.  build, the caller of this
// function, reverses RadixArray in place so that we can reconstruct a call
// stack as if we were deserializing an array in a typical way -- the call stack
// length followed by the frames in the leaf-to-root order except that we need
// to handle pointers to parents along the way.
//
// To quickly determine the location of the common prefix within RadixArray,
// Indexes caches the indexes of the previous call stack's frames within
// RadixArray.
LinearCallStackId CallStackRadixTreeBuilder::encodeCallStack(
    const llvm::SmallVector<FrameId> *CallStack,
    const llvm::SmallVector<FrameId> *Prev,
    const llvm::DenseMap<FrameId, LinearFrameId> &MemProfFrameIndexes) {
  // Compute the length of the common root prefix between Prev and CallStack.
  uint32_t CommonLen = 0;
  if (Prev) {
    auto Pos = std::mismatch(Prev->rbegin(), Prev->rend(), CallStack->rbegin(),
                             CallStack->rend());
    CommonLen = std::distance(CallStack->rbegin(), Pos.second);
  }

  // Drop the portion beyond CommonLen.
  assert(CommonLen <= Indexes.size());
  Indexes.resize(CommonLen);

  // Append a pointer to the parent.
  if (CommonLen) {
    uint32_t CurrentIndex = RadixArray.size();
    uint32_t ParentIndex = Indexes.back();
    // The offset to the parent must be negative because we are pointing to an
    // element we've already added to RadixArray.
    assert(ParentIndex < CurrentIndex);
    RadixArray.push_back(ParentIndex - CurrentIndex);
  }

  // Copy the part of the call stack beyond the common prefix to RadixArray.
  assert(CommonLen <= CallStack->size());
  for (FrameId F : llvm::drop_begin(llvm::reverse(*CallStack), CommonLen)) {
    // Remember the index of F in RadixArray.
    Indexes.push_back(RadixArray.size());
    RadixArray.push_back(MemProfFrameIndexes.find(F)->second);
  }
  assert(CallStack->size() == Indexes.size());

  // End with the call stack length.
  RadixArray.push_back(CallStack->size());

  // Return the index within RadixArray where we can start reconstructing a
  // given call stack from.
  return RadixArray.size() - 1;
}

void CallStackRadixTreeBuilder::build(
    llvm::MapVector<CallStackId, llvm::SmallVector<FrameId>>
        &&MemProfCallStackData,
    const llvm::DenseMap<FrameId, LinearFrameId> &MemProfFrameIndexes,
    llvm::DenseMap<FrameId, FrameStat> &FrameHistogram) {
  // Take the vector portion of MemProfCallStackData.  The vector is exactly
  // what we need to sort.  Also, we no longer need its lookup capability.
  llvm::SmallVector<CSIdPair, 0> CallStacks = MemProfCallStackData.takeVector();

  // Return early if we have no work to do.
  if (CallStacks.empty()) {
    RadixArray.clear();
    CallStackPos.clear();
    return;
  }

  // Sorting the list of call stacks in the dictionary order is sufficient to
  // maximize the length of the common prefix between two adjacent call stacks
  // and thus minimize the length of RadixArray.  However, we go one step
  // further and try to reduce the number of times we follow pointers to parents
  // during deserilization.  Consider a poorly encoded radix tree:
  //
  // CallStackId 1:  f1 -> f2 -> f3
  //                  |
  // CallStackId 2:   +--- f4 -> f5
  //                        |
  // CallStackId 3:         +--> f6
  //
  // Here, f2 and f4 appear once and twice, respectively, in the call stacks.
  // Once we encode CallStackId 1 into RadixArray, every other call stack with
  // common prefix f1 ends up pointing to CallStackId 1.  Since CallStackId 3
  // share "f1 f4" with CallStackId 2, CallStackId 3 needs to follow pointers to
  // parents twice.
  //
  // We try to alleviate the situation by sorting the list of call stacks by
  // comparing the popularity of frames rather than the integer values of
  // FrameIds.  In the example above, f4 is more popular than f2, so we sort the
  // call stacks and encode them as:
  //
  // CallStackId 2:  f1 -- f4 -> f5
  //                  |     |
  // CallStackId 3:   |     +--> f6
  //                  |
  // CallStackId 1:   +--> f2 -> f3
  //
  // Notice that CallStackId 3 follows a pointer to a parent only once.
  //
  // All this is a quick-n-dirty trick to reduce the number of jumps.  The
  // proper way would be to compute the weight of each radix tree node -- how
  // many call stacks use a given radix tree node, and encode a radix tree from
  // the heaviest node first.  We do not do so because that's a lot of work.
  llvm::sort(CallStacks, [&](const CSIdPair &L, const CSIdPair &R) {
    // Call stacks are stored from leaf to root.  Perform comparisons from the
    // root.
    return std::lexicographical_compare(
        L.second.rbegin(), L.second.rend(), R.second.rbegin(), R.second.rend(),
        [&](FrameId F1, FrameId F2) {
          uint64_t H1 = FrameHistogram[F1].Count;
          uint64_t H2 = FrameHistogram[F2].Count;
          // Popular frames should come later because we encode call stacks from
          // the last one in the list.
          if (H1 != H2)
            return H1 < H2;
          // For sort stability.
          return F1 < F2;
        });
  });

  // Reserve some reasonable amount of storage.
  RadixArray.clear();
  RadixArray.reserve(CallStacks.size() * 8);

  // Indexes will grow as long as the longest call stack.
  Indexes.clear();
  Indexes.reserve(512);

  // CallStackPos will grow to exactly CallStacks.size() entries.
  CallStackPos.clear();
  CallStackPos.reserve(CallStacks.size());

  // Compute the radix array.  We encode one call stack at a time, computing the
  // longest prefix that's shared with the previous call stack we encode.  For
  // each call stack we encode, we remember a mapping from CallStackId to its
  // position within RadixArray.
  //
  // As an optimization, we encode from the last call stack in CallStacks to
  // reduce the number of times we follow pointers to the parents.  Consider the
  // list of call stacks that has been sorted in the dictionary order:
  //
  // Call Stack 1: F1
  // Call Stack 2: F1 -> F2
  // Call Stack 3: F1 -> F2 -> F3
  //
  // If we traversed CallStacks in the forward order, we would end up with a
  // radix tree like:
  //
  // Call Stack 1:  F1
  //                |
  // Call Stack 2:  +---> F2
  //                      |
  // Call Stack 3:        +---> F3
  //
  // Notice that each call stack jumps to the previous one.  However, if we
  // traverse CallStacks in the reverse order, then Call Stack 3 has the
  // complete call stack encoded without any pointers.  Call Stack 1 and 2 point
  // to appropriate prefixes of Call Stack 3.
  const llvm::SmallVector<FrameId> *Prev = nullptr;
  for (const auto &[CSId, CallStack] : llvm::reverse(CallStacks)) {
    LinearCallStackId Pos =
        encodeCallStack(&CallStack, Prev, MemProfFrameIndexes);
    CallStackPos.insert({CSId, Pos});
    Prev = &CallStack;
  }

  // "RadixArray.size() - 1" below is problematic if RadixArray is empty.
  assert(!RadixArray.empty());

  // Reverse the radix array in place.  We do so mostly for intuitive
  // deserialization where we would read the length field and then the call
  // stack frames proper just like any other array deserialization, except
  // that we have occasional jumps to take advantage of prefixes.
  for (size_t I = 0, J = RadixArray.size() - 1; I < J; ++I, --J)
    std::swap(RadixArray[I], RadixArray[J]);

  // "Reverse" the indexes stored in CallStackPos.
  for (auto &[K, V] : CallStackPos)
    V = RadixArray.size() - 1 - V;
}

llvm::DenseMap<FrameId, FrameStat>
computeFrameHistogram(llvm::MapVector<CallStackId, llvm::SmallVector<FrameId>>
                          &MemProfCallStackData) {
  llvm::DenseMap<FrameId, FrameStat> Histogram;

  for (const auto &KV : MemProfCallStackData) {
    const auto &CS = KV.second;
    for (unsigned I = 0, E = CS.size(); I != E; ++I) {
      auto &S = Histogram[CS[I]];
      ++S.Count;
      S.PositionSum += I;
    }
  }
  return Histogram;
}

void verifyIndexedMemProfRecord(const IndexedMemProfRecord &Record) {
  for (const auto &AS : Record.AllocSites) {
    assert(AS.CSId == hashCallStack(AS.CallStack));
    (void)AS;
  }
}

void verifyFunctionProfileData(
    const llvm::MapVector<GlobalValue::GUID, IndexedMemProfRecord>
        &FunctionProfileData) {
  for (const auto &[GUID, Record] : FunctionProfileData) {
    (void)GUID;
    verifyIndexedMemProfRecord(Record);
  }
}

} // namespace memprof
} // namespace llvm

//===-- TypeStreamMerger.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/TypeStreamMerger.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/GlobalTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/MergingTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeIndexDiscovery.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecordHelpers.h"
#include "llvm/Support/Error.h"
#include <optional>

using namespace llvm;
using namespace llvm::codeview;

static inline size_t slotForIndex(TypeIndex Idx) {
  assert(!Idx.isSimple() && "simple type indices have no slots");
  return Idx.getIndex() - TypeIndex::FirstNonSimpleIndex;
}

namespace {

/// Implementation of CodeView type stream merging.
///
/// A CodeView type stream is a series of records that reference each other
/// through type indices. A type index is either "simple", meaning it is less
/// than 0x1000 and refers to a builtin type, or it is complex, meaning it
/// refers to a prior type record in the current stream. The type index of a
/// record is equal to the number of records before it in the stream plus
/// 0x1000.
///
/// Type records are only allowed to use type indices smaller than their own, so
/// a type stream is effectively a topologically sorted DAG. Cycles occurring in
/// the type graph of the source program are resolved with forward declarations
/// of composite types. This class implements the following type stream merging
/// algorithm, which relies on this DAG structure:
///
/// - Begin with a new empty stream, and a new empty hash table that maps from
///   type record contents to new type index.
/// - For each new type stream, maintain a map from source type index to
///   destination type index.
/// - For each record, copy it and rewrite its type indices to be valid in the
///   destination type stream.
/// - If the new type record is not already present in the destination stream
///   hash table, append it to the destination type stream, assign it the next
///   type index, and update the two hash tables.
/// - If the type record already exists in the destination stream, discard it
///   and update the type index map to forward the source type index to the
///   existing destination type index.
///
/// As an additional complication, type stream merging actually produces two
/// streams: an item (or IPI) stream and a type stream, as this is what is
/// actually stored in the final PDB. We choose which records go where by
/// looking at the record kind.
class TypeStreamMerger {
public:
  explicit TypeStreamMerger(SmallVectorImpl<TypeIndex> &SourceToDest)
      : IndexMap(SourceToDest) {
    // When dealing with precompiled headers objects, all data in SourceToDest
    // belongs to the precompiled headers object, and is assumed to be already
    // remapped to the target PDB. Any forthcoming type that will be merged in
    // might potentially back-reference this data. We also don't want to resolve
    // twice the types in the precompiled object.
    CurIndex += SourceToDest.size();
  }

  static const TypeIndex Untranslated;

  // Local hashing entry points
  Error mergeTypesAndIds(MergingTypeTableBuilder &DestIds,
                         MergingTypeTableBuilder &DestTypes,
                         const CVTypeArray &IdsAndTypes,
                         std::optional<PCHMergerInfo> &PCHInfo);
  Error mergeIdRecords(MergingTypeTableBuilder &Dest,
                       ArrayRef<TypeIndex> TypeSourceToDest,
                       const CVTypeArray &Ids);
  Error mergeTypeRecords(MergingTypeTableBuilder &Dest,
                         const CVTypeArray &Types);

  // Global hashing entry points
  Error mergeTypesAndIds(GlobalTypeTableBuilder &DestIds,
                         GlobalTypeTableBuilder &DestTypes,
                         const CVTypeArray &IdsAndTypes,
                         ArrayRef<GloballyHashedType> Hashes,
                         std::optional<PCHMergerInfo> &PCHInfo);
  Error mergeIdRecords(GlobalTypeTableBuilder &Dest,
                       ArrayRef<TypeIndex> TypeSourceToDest,
                       const CVTypeArray &Ids,
                       ArrayRef<GloballyHashedType> Hashes);
  Error mergeTypeRecords(GlobalTypeTableBuilder &Dest, const CVTypeArray &Types,
                         ArrayRef<GloballyHashedType> Hashes,
                         std::optional<PCHMergerInfo> &PCHInfo);

private:
  Error doit(const CVTypeArray &Types);

  Error remapAllTypes(const CVTypeArray &Types);

  Error remapType(const CVType &Type);

  void addMapping(TypeIndex Idx);

  inline bool remapTypeIndex(TypeIndex &Idx) {
    // If we're mapping a pure index stream, then IndexMap only contains
    // mappings from OldIdStream -> NewIdStream, in which case we will need to
    // use the special mapping from OldTypeStream -> NewTypeStream which was
    // computed externally.  Regardless, we use this special map if and only if
    // we are doing an id-only mapping.
    if (!hasTypeStream())
      return remapIndex(Idx, TypeLookup);

    assert(TypeLookup.empty());
    return remapIndex(Idx, IndexMap);
  }
  inline bool remapItemIndex(TypeIndex &Idx) {
    assert(hasIdStream());
    return remapIndex(Idx, IndexMap);
  }

  bool hasTypeStream() const {
    return (UseGlobalHashes) ? (!!DestGlobalTypeStream) : (!!DestTypeStream);
  }

  bool hasIdStream() const {
    return (UseGlobalHashes) ? (!!DestGlobalIdStream) : (!!DestIdStream);
  }

  ArrayRef<uint8_t> remapIndices(const CVType &OriginalType,
                                 MutableArrayRef<uint8_t> Storage);

  inline bool remapIndex(TypeIndex &Idx, ArrayRef<TypeIndex> Map) {
    if (LLVM_LIKELY(remapIndexSimple(Idx, Map)))
      return true;

    return remapIndexFallback(Idx, Map);
  }

  inline bool remapIndexSimple(TypeIndex &Idx, ArrayRef<TypeIndex> Map) const {
    // Simple types are unchanged.
    if (Idx.isSimple())
      return true;

    // Check if this type index refers to a record we've already translated
    // successfully. If it refers to a type later in the stream or a record we
    // had to defer, defer it until later pass.
    unsigned MapPos = slotForIndex(Idx);
    if (LLVM_UNLIKELY(MapPos >= Map.size() || Map[MapPos] == Untranslated))
      return false;

    Idx = Map[MapPos];
    return true;
  }

  bool remapIndexFallback(TypeIndex &Idx, ArrayRef<TypeIndex> Map);

  Error errorCorruptRecord() const {
    return llvm::make_error<CodeViewError>(cv_error_code::corrupt_record);
  }

  Expected<bool> shouldRemapType(const CVType &Type);

  std::optional<Error> LastError;

  bool UseGlobalHashes = false;

  bool IsSecondPass = false;

  unsigned NumBadIndices = 0;

  TypeIndex CurIndex{TypeIndex::FirstNonSimpleIndex};

  MergingTypeTableBuilder *DestIdStream = nullptr;
  MergingTypeTableBuilder *DestTypeStream = nullptr;

  GlobalTypeTableBuilder *DestGlobalIdStream = nullptr;
  GlobalTypeTableBuilder *DestGlobalTypeStream = nullptr;

  ArrayRef<GloballyHashedType> GlobalHashes;

  // If we're only mapping id records, this array contains the mapping for
  // type records.
  ArrayRef<TypeIndex> TypeLookup;

  /// Map from source type index to destination type index. Indexed by source
  /// type index minus 0x1000.
  SmallVectorImpl<TypeIndex> &IndexMap;

  /// Temporary storage that we use to copy a record's data while re-writing
  /// its type indices.
  SmallVector<uint8_t, 256> RemapStorage;

  std::optional<PCHMergerInfo> PCHInfo;
};

} // end anonymous namespace

const TypeIndex TypeStreamMerger::Untranslated(SimpleTypeKind::NotTranslated);

void TypeStreamMerger::addMapping(TypeIndex Idx) {
  if (!IsSecondPass) {
    assert(IndexMap.size() == slotForIndex(CurIndex) &&
           "visitKnownRecord should add one index map entry");
    IndexMap.push_back(Idx);
  } else {
    assert(slotForIndex(CurIndex) < IndexMap.size());
    IndexMap[slotForIndex(CurIndex)] = Idx;
  }
}

bool TypeStreamMerger::remapIndexFallback(TypeIndex &Idx,
                                          ArrayRef<TypeIndex> Map) {
  size_t MapPos = slotForIndex(Idx);

  // If this is the second pass and this index isn't in the map, then it points
  // outside the current type stream, and this is a corrupt record.
  if (IsSecondPass && MapPos >= Map.size()) {
    // FIXME: Print a more useful error. We can give the current record and the
    // index that we think its pointing to.
    if (LastError)
      LastError = joinErrors(std::move(*LastError), errorCorruptRecord());
    else
      LastError = errorCorruptRecord();
  }

  ++NumBadIndices;

  // This type index is invalid. Remap this to "not translated by cvpack",
  // and return failure.
  Idx = Untranslated;
  return false;
}

// Local hashing entry points
Error TypeStreamMerger::mergeTypeRecords(MergingTypeTableBuilder &Dest,
                                         const CVTypeArray &Types) {
  DestTypeStream = &Dest;
  UseGlobalHashes = false;

  return doit(Types);
}

Error TypeStreamMerger::mergeIdRecords(MergingTypeTableBuilder &Dest,
                                       ArrayRef<TypeIndex> TypeSourceToDest,
                                       const CVTypeArray &Ids) {
  DestIdStream = &Dest;
  TypeLookup = TypeSourceToDest;
  UseGlobalHashes = false;

  return doit(Ids);
}

Error TypeStreamMerger::mergeTypesAndIds(
    MergingTypeTableBuilder &DestIds, MergingTypeTableBuilder &DestTypes,
    const CVTypeArray &IdsAndTypes, std::optional<PCHMergerInfo> &PCHInfo) {
  DestIdStream = &DestIds;
  DestTypeStream = &DestTypes;
  UseGlobalHashes = false;
  auto Err = doit(IdsAndTypes);
  PCHInfo = this->PCHInfo;
  return Err;
}

// Global hashing entry points
Error TypeStreamMerger::mergeTypeRecords(
    GlobalTypeTableBuilder &Dest, const CVTypeArray &Types,
    ArrayRef<GloballyHashedType> Hashes,
    std::optional<PCHMergerInfo> &PCHInfo) {
  DestGlobalTypeStream = &Dest;
  UseGlobalHashes = true;
  GlobalHashes = Hashes;
  auto Err = doit(Types);
  PCHInfo = this->PCHInfo;
  return Err;
}

Error TypeStreamMerger::mergeIdRecords(GlobalTypeTableBuilder &Dest,
                                       ArrayRef<TypeIndex> TypeSourceToDest,
                                       const CVTypeArray &Ids,
                                       ArrayRef<GloballyHashedType> Hashes) {
  DestGlobalIdStream = &Dest;
  TypeLookup = TypeSourceToDest;
  UseGlobalHashes = true;
  GlobalHashes = Hashes;

  return doit(Ids);
}

Error TypeStreamMerger::mergeTypesAndIds(
    GlobalTypeTableBuilder &DestIds, GlobalTypeTableBuilder &DestTypes,
    const CVTypeArray &IdsAndTypes, ArrayRef<GloballyHashedType> Hashes,
    std::optional<PCHMergerInfo> &PCHInfo) {
  DestGlobalIdStream = &DestIds;
  DestGlobalTypeStream = &DestTypes;
  UseGlobalHashes = true;
  GlobalHashes = Hashes;
  auto Err = doit(IdsAndTypes);
  PCHInfo = this->PCHInfo;
  return Err;
}

Error TypeStreamMerger::doit(const CVTypeArray &Types) {
  if (auto EC = remapAllTypes(Types))
    return EC;

  // If we found bad indices but no other errors, try doing another pass and see
  // if we can resolve the indices that weren't in the map on the first pass.
  // This may require multiple passes, but we should always make progress. MASM
  // is the only known CodeView producer that makes type streams that aren't
  // topologically sorted. The standard library contains MASM-produced objects,
  // so this is important to handle correctly, but we don't have to be too
  // efficient. MASM type streams are usually very small.
  while (!LastError && NumBadIndices > 0) {
    unsigned BadIndicesRemaining = NumBadIndices;
    IsSecondPass = true;
    NumBadIndices = 0;
    CurIndex = TypeIndex(TypeIndex::FirstNonSimpleIndex);

    if (auto EC = remapAllTypes(Types))
      return EC;

    assert(NumBadIndices <= BadIndicesRemaining &&
           "second pass found more bad indices");
    if (!LastError && NumBadIndices == BadIndicesRemaining) {
      return llvm::make_error<CodeViewError>(
          cv_error_code::corrupt_record, "Input type graph contains cycles");
    }
  }

  if (LastError)
    return std::move(*LastError);
  return Error::success();
}

Error TypeStreamMerger::remapAllTypes(const CVTypeArray &Types) {
  BinaryStreamRef Stream = Types.getUnderlyingStream();
  ArrayRef<uint8_t> Buffer;
  cantFail(Stream.readBytes(0, Stream.getLength(), Buffer));

  return forEachCodeViewRecord<CVType>(
      Buffer, [this](const CVType &T) { return remapType(T); });
}

Error TypeStreamMerger::remapType(const CVType &Type) {
  auto R = shouldRemapType(Type);
  if (!R)
    return R.takeError();

  TypeIndex DestIdx = Untranslated;
  if (*R) {
    auto DoSerialize =
        [this, Type](MutableArrayRef<uint8_t> Storage) -> ArrayRef<uint8_t> {
      return remapIndices(Type, Storage);
    };
    unsigned AlignedSize = alignTo(Type.RecordData.size(), 4);

    if (LLVM_LIKELY(UseGlobalHashes)) {
      GlobalTypeTableBuilder &Dest =
          isIdRecord(Type.kind()) ? *DestGlobalIdStream : *DestGlobalTypeStream;
      GloballyHashedType H = GlobalHashes[CurIndex.toArrayIndex()];
      DestIdx = Dest.insertRecordAs(H, AlignedSize, DoSerialize);
    } else {
      MergingTypeTableBuilder &Dest =
          isIdRecord(Type.kind()) ? *DestIdStream : *DestTypeStream;

      RemapStorage.resize(AlignedSize);
      ArrayRef<uint8_t> Result = DoSerialize(RemapStorage);
      if (!Result.empty())
        DestIdx = Dest.insertRecordBytes(Result);
    }
  }
  addMapping(DestIdx);

  ++CurIndex;
  assert((IsSecondPass || IndexMap.size() == slotForIndex(CurIndex)) &&
         "visitKnownRecord should add one index map entry");
  return Error::success();
}

ArrayRef<uint8_t>
TypeStreamMerger::remapIndices(const CVType &OriginalType,
                               MutableArrayRef<uint8_t> Storage) {
  unsigned Align = OriginalType.RecordData.size() & 3;
  assert(Storage.size() == alignTo(OriginalType.RecordData.size(), 4) &&
         "The storage buffer size is not a multiple of 4 bytes which will "
         "cause misalignment in the output TPI stream!");

  SmallVector<TiReference, 4> Refs;
  discoverTypeIndices(OriginalType.RecordData, Refs);
  if (Refs.empty() && Align == 0)
    return OriginalType.RecordData;

  ::memcpy(Storage.data(), OriginalType.RecordData.data(),
           OriginalType.RecordData.size());

  uint8_t *DestContent = Storage.data() + sizeof(RecordPrefix);

  for (auto &Ref : Refs) {
    TypeIndex *DestTIs =
        reinterpret_cast<TypeIndex *>(DestContent + Ref.Offset);

    for (size_t I = 0; I < Ref.Count; ++I) {
      TypeIndex &TI = DestTIs[I];
      bool Success = (Ref.Kind == TiRefKind::IndexRef) ? remapItemIndex(TI)
                                                       : remapTypeIndex(TI);
      if (LLVM_UNLIKELY(!Success))
        return {};
    }
  }

  if (Align > 0) {
    RecordPrefix *StorageHeader =
        reinterpret_cast<RecordPrefix *>(Storage.data());
    StorageHeader->RecordLen += 4 - Align;

    DestContent = Storage.data() + OriginalType.RecordData.size();
    for (; Align < 4; ++Align)
      *DestContent++ = LF_PAD4 - Align;
  }
  return Storage;
}

Error llvm::codeview::mergeTypeRecords(MergingTypeTableBuilder &Dest,
                                       SmallVectorImpl<TypeIndex> &SourceToDest,
                                       const CVTypeArray &Types) {
  TypeStreamMerger M(SourceToDest);
  return M.mergeTypeRecords(Dest, Types);
}

Error llvm::codeview::mergeIdRecords(MergingTypeTableBuilder &Dest,
                                     ArrayRef<TypeIndex> TypeSourceToDest,
                                     SmallVectorImpl<TypeIndex> &SourceToDest,
                                     const CVTypeArray &Ids) {
  TypeStreamMerger M(SourceToDest);
  return M.mergeIdRecords(Dest, TypeSourceToDest, Ids);
}

Error llvm::codeview::mergeTypeAndIdRecords(
    MergingTypeTableBuilder &DestIds, MergingTypeTableBuilder &DestTypes,
    SmallVectorImpl<TypeIndex> &SourceToDest, const CVTypeArray &IdsAndTypes,
    std::optional<PCHMergerInfo> &PCHInfo) {
  TypeStreamMerger M(SourceToDest);
  return M.mergeTypesAndIds(DestIds, DestTypes, IdsAndTypes, PCHInfo);
}

Error llvm::codeview::mergeTypeAndIdRecords(
    GlobalTypeTableBuilder &DestIds, GlobalTypeTableBuilder &DestTypes,
    SmallVectorImpl<TypeIndex> &SourceToDest, const CVTypeArray &IdsAndTypes,
    ArrayRef<GloballyHashedType> Hashes,
    std::optional<PCHMergerInfo> &PCHInfo) {
  TypeStreamMerger M(SourceToDest);
  return M.mergeTypesAndIds(DestIds, DestTypes, IdsAndTypes, Hashes, PCHInfo);
}

Error llvm::codeview::mergeTypeRecords(GlobalTypeTableBuilder &Dest,
                                       SmallVectorImpl<TypeIndex> &SourceToDest,
                                       const CVTypeArray &Types,
                                       ArrayRef<GloballyHashedType> Hashes,
                                       std::optional<PCHMergerInfo> &PCHInfo) {
  TypeStreamMerger M(SourceToDest);
  return M.mergeTypeRecords(Dest, Types, Hashes, PCHInfo);
}

Error llvm::codeview::mergeIdRecords(GlobalTypeTableBuilder &Dest,
                                     ArrayRef<TypeIndex> Types,
                                     SmallVectorImpl<TypeIndex> &SourceToDest,
                                     const CVTypeArray &Ids,
                                     ArrayRef<GloballyHashedType> Hashes) {
  TypeStreamMerger M(SourceToDest);
  return M.mergeIdRecords(Dest, Types, Ids, Hashes);
}

Expected<bool> TypeStreamMerger::shouldRemapType(const CVType &Type) {
  // For object files containing precompiled types, we need to extract the
  // signature, through EndPrecompRecord. This is done here for performance
  // reasons, to avoid re-parsing the Types stream.
  if (Type.kind() == LF_ENDPRECOMP) {
    EndPrecompRecord EP;
    if (auto EC = TypeDeserializer::deserializeAs(const_cast<CVType &>(Type),
                                                  EP))
      return joinErrors(std::move(EC), errorCorruptRecord());
    // Only one record of this kind can appear in a OBJ.
    if (PCHInfo)
      return errorCorruptRecord();
    PCHInfo.emplace(PCHMergerInfo{EP.getSignature(), CurIndex.toArrayIndex()});
    return false;
  }
  return true;
}

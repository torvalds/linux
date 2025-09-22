//===--- APINotesReader.cpp - API Notes Reader ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the \c APINotesReader class that reads source
// API notes data providing additional information about source code as
// a separate input, such as the non-nil/nilable annotations for
// method parameters.
//
//===----------------------------------------------------------------------===//
#include "clang/APINotes/APINotesReader.h"
#include "APINotesFormat.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/OnDiskHashTable.h"

namespace clang {
namespace api_notes {
using namespace llvm::support;

namespace {
/// Deserialize a version tuple.
llvm::VersionTuple ReadVersionTuple(const uint8_t *&Data) {
  uint8_t NumVersions = (*Data++) & 0x03;

  unsigned Major = endian::readNext<uint32_t, llvm::endianness::little>(Data);
  if (NumVersions == 0)
    return llvm::VersionTuple(Major);

  unsigned Minor = endian::readNext<uint32_t, llvm::endianness::little>(Data);
  if (NumVersions == 1)
    return llvm::VersionTuple(Major, Minor);

  unsigned Subminor =
      endian::readNext<uint32_t, llvm::endianness::little>(Data);
  if (NumVersions == 2)
    return llvm::VersionTuple(Major, Minor, Subminor);

  unsigned Build = endian::readNext<uint32_t, llvm::endianness::little>(Data);
  return llvm::VersionTuple(Major, Minor, Subminor, Build);
}

/// An on-disk hash table whose data is versioned based on the Swift version.
template <typename Derived, typename KeyType, typename UnversionedDataType>
class VersionedTableInfo {
public:
  using internal_key_type = KeyType;
  using external_key_type = KeyType;
  using data_type =
      llvm::SmallVector<std::pair<llvm::VersionTuple, UnversionedDataType>, 1>;
  using hash_value_type = size_t;
  using offset_type = unsigned;

  internal_key_type GetInternalKey(external_key_type Key) { return Key; }

  external_key_type GetExternalKey(internal_key_type Key) { return Key; }

  static bool EqualKey(internal_key_type LHS, internal_key_type RHS) {
    return LHS == RHS;
  }

  static std::pair<unsigned, unsigned> ReadKeyDataLength(const uint8_t *&Data) {
    unsigned KeyLength =
        endian::readNext<uint16_t, llvm::endianness::little>(Data);
    unsigned DataLength =
        endian::readNext<uint16_t, llvm::endianness::little>(Data);
    return {KeyLength, DataLength};
  }

  static data_type ReadData(internal_key_type Key, const uint8_t *Data,
                            unsigned Length) {
    unsigned NumElements =
        endian::readNext<uint16_t, llvm::endianness::little>(Data);
    data_type Result;
    Result.reserve(NumElements);
    for (unsigned i = 0; i != NumElements; ++i) {
      auto version = ReadVersionTuple(Data);
      const auto *DataBefore = Data;
      (void)DataBefore;
      auto UnversionedData = Derived::readUnversioned(Key, Data);
      assert(Data != DataBefore &&
             "Unversioned data reader didn't move pointer");
      Result.push_back({version, UnversionedData});
    }
    return Result;
  }
};

/// Read serialized CommonEntityInfo.
void ReadCommonEntityInfo(const uint8_t *&Data, CommonEntityInfo &Info) {
  uint8_t UnavailableBits = *Data++;
  Info.Unavailable = (UnavailableBits >> 1) & 0x01;
  Info.UnavailableInSwift = UnavailableBits & 0x01;
  if ((UnavailableBits >> 2) & 0x01)
    Info.setSwiftPrivate(static_cast<bool>((UnavailableBits >> 3) & 0x01));

  unsigned MsgLength =
      endian::readNext<uint16_t, llvm::endianness::little>(Data);
  Info.UnavailableMsg =
      std::string(reinterpret_cast<const char *>(Data),
                  reinterpret_cast<const char *>(Data) + MsgLength);
  Data += MsgLength;

  unsigned SwiftNameLength =
      endian::readNext<uint16_t, llvm::endianness::little>(Data);
  Info.SwiftName =
      std::string(reinterpret_cast<const char *>(Data),
                  reinterpret_cast<const char *>(Data) + SwiftNameLength);
  Data += SwiftNameLength;
}

/// Read serialized CommonTypeInfo.
void ReadCommonTypeInfo(const uint8_t *&Data, CommonTypeInfo &Info) {
  ReadCommonEntityInfo(Data, Info);

  unsigned SwiftBridgeLength =
      endian::readNext<uint16_t, llvm::endianness::little>(Data);
  if (SwiftBridgeLength > 0) {
    Info.setSwiftBridge(std::string(reinterpret_cast<const char *>(Data),
                                    SwiftBridgeLength - 1));
    Data += SwiftBridgeLength - 1;
  }

  unsigned ErrorDomainLength =
      endian::readNext<uint16_t, llvm::endianness::little>(Data);
  if (ErrorDomainLength > 0) {
    Info.setNSErrorDomain(std::optional<std::string>(std::string(
        reinterpret_cast<const char *>(Data), ErrorDomainLength - 1)));
    Data += ErrorDomainLength - 1;
  }
}

/// Used to deserialize the on-disk identifier table.
class IdentifierTableInfo {
public:
  using internal_key_type = llvm::StringRef;
  using external_key_type = llvm::StringRef;
  using data_type = IdentifierID;
  using hash_value_type = uint32_t;
  using offset_type = unsigned;

  internal_key_type GetInternalKey(external_key_type Key) { return Key; }

  external_key_type GetExternalKey(internal_key_type Key) { return Key; }

  hash_value_type ComputeHash(internal_key_type Key) {
    return llvm::djbHash(Key);
  }

  static bool EqualKey(internal_key_type LHS, internal_key_type RHS) {
    return LHS == RHS;
  }

  static std::pair<unsigned, unsigned> ReadKeyDataLength(const uint8_t *&Data) {
    unsigned KeyLength =
        endian::readNext<uint16_t, llvm::endianness::little>(Data);
    unsigned DataLength =
        endian::readNext<uint16_t, llvm::endianness::little>(Data);
    return {KeyLength, DataLength};
  }

  static internal_key_type ReadKey(const uint8_t *Data, unsigned Length) {
    return llvm::StringRef(reinterpret_cast<const char *>(Data), Length);
  }

  static data_type ReadData(internal_key_type key, const uint8_t *Data,
                            unsigned Length) {
    return endian::readNext<uint32_t, llvm::endianness::little>(Data);
  }
};

/// Used to deserialize the on-disk table of Objective-C classes and C++
/// namespaces.
class ContextIDTableInfo {
public:
  using internal_key_type = ContextTableKey;
  using external_key_type = internal_key_type;
  using data_type = unsigned;
  using hash_value_type = size_t;
  using offset_type = unsigned;

  internal_key_type GetInternalKey(external_key_type Key) { return Key; }

  external_key_type GetExternalKey(internal_key_type Key) { return Key; }

  hash_value_type ComputeHash(internal_key_type Key) {
    return static_cast<size_t>(Key.hashValue());
  }

  static bool EqualKey(internal_key_type LHS, internal_key_type RHS) {
    return LHS == RHS;
  }

  static std::pair<unsigned, unsigned> ReadKeyDataLength(const uint8_t *&Data) {
    unsigned KeyLength =
        endian::readNext<uint16_t, llvm::endianness::little>(Data);
    unsigned DataLength =
        endian::readNext<uint16_t, llvm::endianness::little>(Data);
    return {KeyLength, DataLength};
  }

  static internal_key_type ReadKey(const uint8_t *Data, unsigned Length) {
    auto ParentCtxID =
        endian::readNext<uint32_t, llvm::endianness::little>(Data);
    auto ContextKind =
        endian::readNext<uint8_t, llvm::endianness::little>(Data);
    auto NameID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    return {ParentCtxID, ContextKind, NameID};
  }

  static data_type ReadData(internal_key_type Key, const uint8_t *Data,
                            unsigned Length) {
    return endian::readNext<uint32_t, llvm::endianness::little>(Data);
  }
};

/// Used to deserialize the on-disk Objective-C property table.
class ContextInfoTableInfo
    : public VersionedTableInfo<ContextInfoTableInfo, unsigned, ContextInfo> {
public:
  static internal_key_type ReadKey(const uint8_t *Data, unsigned Length) {
    return endian::readNext<uint32_t, llvm::endianness::little>(Data);
  }

  hash_value_type ComputeHash(internal_key_type Key) {
    return static_cast<size_t>(llvm::hash_value(Key));
  }

  static ContextInfo readUnversioned(internal_key_type Key,
                                     const uint8_t *&Data) {
    ContextInfo Info;
    ReadCommonTypeInfo(Data, Info);
    uint8_t Payload = *Data++;

    if (Payload & 0x01)
      Info.setHasDesignatedInits(true);
    Payload = Payload >> 1;

    if (Payload & 0x4)
      Info.setDefaultNullability(static_cast<NullabilityKind>(Payload & 0x03));
    Payload >>= 3;

    if (Payload & (1 << 1))
      Info.setSwiftObjCMembers(Payload & 1);
    Payload >>= 2;

    if (Payload & (1 << 1))
      Info.setSwiftImportAsNonGeneric(Payload & 1);

    return Info;
  }
};

/// Read serialized VariableInfo.
void ReadVariableInfo(const uint8_t *&Data, VariableInfo &Info) {
  ReadCommonEntityInfo(Data, Info);
  if (*Data++) {
    Info.setNullabilityAudited(static_cast<NullabilityKind>(*Data));
  }
  ++Data;

  auto TypeLen = endian::readNext<uint16_t, llvm::endianness::little>(Data);
  Info.setType(std::string(Data, Data + TypeLen));
  Data += TypeLen;
}

/// Used to deserialize the on-disk Objective-C property table.
class ObjCPropertyTableInfo
    : public VersionedTableInfo<ObjCPropertyTableInfo,
                                std::tuple<uint32_t, uint32_t, uint8_t>,
                                ObjCPropertyInfo> {
public:
  static internal_key_type ReadKey(const uint8_t *Data, unsigned Length) {
    auto ClassID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    auto NameID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    char IsInstance = endian::readNext<uint8_t, llvm::endianness::little>(Data);
    return {ClassID, NameID, IsInstance};
  }

  hash_value_type ComputeHash(internal_key_type Key) {
    return static_cast<size_t>(llvm::hash_value(Key));
  }

  static ObjCPropertyInfo readUnversioned(internal_key_type Key,
                                          const uint8_t *&Data) {
    ObjCPropertyInfo Info;
    ReadVariableInfo(Data, Info);
    uint8_t Flags = *Data++;
    if (Flags & (1 << 0))
      Info.setSwiftImportAsAccessors(Flags & (1 << 1));
    return Info;
  }
};

/// Read serialized ParamInfo.
void ReadParamInfo(const uint8_t *&Data, ParamInfo &Info) {
  ReadVariableInfo(Data, Info);

  uint8_t Payload = endian::readNext<uint8_t, llvm::endianness::little>(Data);
  if (auto RawConvention = Payload & 0x7) {
    auto Convention = static_cast<RetainCountConventionKind>(RawConvention - 1);
    Info.setRetainCountConvention(Convention);
  }
  Payload >>= 3;
  if (Payload & 0x01)
    Info.setNoEscape(Payload & 0x02);
  Payload >>= 2;
  assert(Payload == 0 && "Bad API notes");
}

/// Read serialized FunctionInfo.
void ReadFunctionInfo(const uint8_t *&Data, FunctionInfo &Info) {
  ReadCommonEntityInfo(Data, Info);

  uint8_t Payload = endian::readNext<uint8_t, llvm::endianness::little>(Data);
  if (auto RawConvention = Payload & 0x7) {
    auto Convention = static_cast<RetainCountConventionKind>(RawConvention - 1);
    Info.setRetainCountConvention(Convention);
  }
  Payload >>= 3;
  Info.NullabilityAudited = Payload & 0x1;
  Payload >>= 1;
  assert(Payload == 0 && "Bad API notes");

  Info.NumAdjustedNullable =
      endian::readNext<uint8_t, llvm::endianness::little>(Data);
  Info.NullabilityPayload =
      endian::readNext<uint64_t, llvm::endianness::little>(Data);

  unsigned NumParams =
      endian::readNext<uint16_t, llvm::endianness::little>(Data);
  while (NumParams > 0) {
    ParamInfo pi;
    ReadParamInfo(Data, pi);
    Info.Params.push_back(pi);
    --NumParams;
  }

  unsigned ResultTypeLen =
      endian::readNext<uint16_t, llvm::endianness::little>(Data);
  Info.ResultType = std::string(Data, Data + ResultTypeLen);
  Data += ResultTypeLen;
}

/// Used to deserialize the on-disk Objective-C method table.
class ObjCMethodTableInfo
    : public VersionedTableInfo<ObjCMethodTableInfo,
                                std::tuple<uint32_t, uint32_t, uint8_t>,
                                ObjCMethodInfo> {
public:
  static internal_key_type ReadKey(const uint8_t *Data, unsigned Length) {
    auto ClassID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    auto SelectorID =
        endian::readNext<uint32_t, llvm::endianness::little>(Data);
    auto IsInstance = endian::readNext<uint8_t, llvm::endianness::little>(Data);
    return {ClassID, SelectorID, IsInstance};
  }

  hash_value_type ComputeHash(internal_key_type Key) {
    return static_cast<size_t>(llvm::hash_value(Key));
  }

  static ObjCMethodInfo readUnversioned(internal_key_type Key,
                                        const uint8_t *&Data) {
    ObjCMethodInfo Info;
    uint8_t Payload = *Data++;
    Info.RequiredInit = Payload & 0x01;
    Payload >>= 1;
    Info.DesignatedInit = Payload & 0x01;
    Payload >>= 1;

    ReadFunctionInfo(Data, Info);
    return Info;
  }
};

/// Used to deserialize the on-disk Objective-C selector table.
class ObjCSelectorTableInfo {
public:
  using internal_key_type = StoredObjCSelector;
  using external_key_type = internal_key_type;
  using data_type = SelectorID;
  using hash_value_type = unsigned;
  using offset_type = unsigned;

  internal_key_type GetInternalKey(external_key_type Key) { return Key; }

  external_key_type GetExternalKey(internal_key_type Key) { return Key; }

  hash_value_type ComputeHash(internal_key_type Key) {
    return llvm::DenseMapInfo<StoredObjCSelector>::getHashValue(Key);
  }

  static bool EqualKey(internal_key_type LHS, internal_key_type RHS) {
    return llvm::DenseMapInfo<StoredObjCSelector>::isEqual(LHS, RHS);
  }

  static std::pair<unsigned, unsigned> ReadKeyDataLength(const uint8_t *&Data) {
    unsigned KeyLength =
        endian::readNext<uint16_t, llvm::endianness::little>(Data);
    unsigned DataLength =
        endian::readNext<uint16_t, llvm::endianness::little>(Data);
    return {KeyLength, DataLength};
  }

  static internal_key_type ReadKey(const uint8_t *Data, unsigned Length) {
    internal_key_type Key;
    Key.NumArgs = endian::readNext<uint16_t, llvm::endianness::little>(Data);
    unsigned NumIdents = (Length - sizeof(uint16_t)) / sizeof(uint32_t);
    for (unsigned i = 0; i != NumIdents; ++i) {
      Key.Identifiers.push_back(
          endian::readNext<uint32_t, llvm::endianness::little>(Data));
    }
    return Key;
  }

  static data_type ReadData(internal_key_type Key, const uint8_t *Data,
                            unsigned Length) {
    return endian::readNext<uint32_t, llvm::endianness::little>(Data);
  }
};

/// Used to deserialize the on-disk global variable table.
class GlobalVariableTableInfo
    : public VersionedTableInfo<GlobalVariableTableInfo, SingleDeclTableKey,
                                GlobalVariableInfo> {
public:
  static internal_key_type ReadKey(const uint8_t *Data, unsigned Length) {
    auto CtxID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    auto NameID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    return {CtxID, NameID};
  }

  hash_value_type ComputeHash(internal_key_type Key) {
    return static_cast<size_t>(Key.hashValue());
  }

  static GlobalVariableInfo readUnversioned(internal_key_type Key,
                                            const uint8_t *&Data) {
    GlobalVariableInfo Info;
    ReadVariableInfo(Data, Info);
    return Info;
  }
};

/// Used to deserialize the on-disk global function table.
class GlobalFunctionTableInfo
    : public VersionedTableInfo<GlobalFunctionTableInfo, SingleDeclTableKey,
                                GlobalFunctionInfo> {
public:
  static internal_key_type ReadKey(const uint8_t *Data, unsigned Length) {
    auto CtxID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    auto NameID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    return {CtxID, NameID};
  }

  hash_value_type ComputeHash(internal_key_type Key) {
    return static_cast<size_t>(Key.hashValue());
  }

  static GlobalFunctionInfo readUnversioned(internal_key_type Key,
                                            const uint8_t *&Data) {
    GlobalFunctionInfo Info;
    ReadFunctionInfo(Data, Info);
    return Info;
  }
};

/// Used to deserialize the on-disk C++ method table.
class CXXMethodTableInfo
    : public VersionedTableInfo<CXXMethodTableInfo, SingleDeclTableKey,
                                CXXMethodInfo> {
public:
  static internal_key_type ReadKey(const uint8_t *Data, unsigned Length) {
    auto CtxID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    auto NameID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    return {CtxID, NameID};
  }

  hash_value_type ComputeHash(internal_key_type Key) {
    return static_cast<size_t>(Key.hashValue());
  }

  static CXXMethodInfo readUnversioned(internal_key_type Key,
                                       const uint8_t *&Data) {
    CXXMethodInfo Info;
    ReadFunctionInfo(Data, Info);
    return Info;
  }
};

/// Used to deserialize the on-disk enumerator table.
class EnumConstantTableInfo
    : public VersionedTableInfo<EnumConstantTableInfo, uint32_t,
                                EnumConstantInfo> {
public:
  static internal_key_type ReadKey(const uint8_t *Data, unsigned Length) {
    auto NameID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    return NameID;
  }

  hash_value_type ComputeHash(internal_key_type Key) {
    return static_cast<size_t>(llvm::hash_value(Key));
  }

  static EnumConstantInfo readUnversioned(internal_key_type Key,
                                          const uint8_t *&Data) {
    EnumConstantInfo Info;
    ReadCommonEntityInfo(Data, Info);
    return Info;
  }
};

/// Used to deserialize the on-disk tag table.
class TagTableInfo
    : public VersionedTableInfo<TagTableInfo, SingleDeclTableKey, TagInfo> {
public:
  static internal_key_type ReadKey(const uint8_t *Data, unsigned Length) {
    auto CtxID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    auto NameID =
        endian::readNext<IdentifierID, llvm::endianness::little>(Data);
    return {CtxID, NameID};
  }

  hash_value_type ComputeHash(internal_key_type Key) {
    return static_cast<size_t>(Key.hashValue());
  }

  static TagInfo readUnversioned(internal_key_type Key, const uint8_t *&Data) {
    TagInfo Info;

    uint8_t Payload = *Data++;
    if (Payload & 1)
      Info.setFlagEnum(Payload & 2);
    Payload >>= 2;
    if (Payload > 0)
      Info.EnumExtensibility =
          static_cast<EnumExtensibilityKind>((Payload & 0x3) - 1);

    uint8_t Copyable =
        endian::readNext<uint8_t, llvm::endianness::little>(Data);
    if (Copyable == kSwiftNonCopyable)
      Info.setSwiftCopyable(std::optional(false));
    else if (Copyable == kSwiftCopyable)
      Info.setSwiftCopyable(std::optional(true));

    unsigned ImportAsLength =
        endian::readNext<uint16_t, llvm::endianness::little>(Data);
    if (ImportAsLength > 0) {
      Info.SwiftImportAs =
          std::string(reinterpret_cast<const char *>(Data), ImportAsLength - 1);
      Data += ImportAsLength - 1;
    }
    unsigned RetainOpLength =
        endian::readNext<uint16_t, llvm::endianness::little>(Data);
    if (RetainOpLength > 0) {
      Info.SwiftRetainOp =
          std::string(reinterpret_cast<const char *>(Data), RetainOpLength - 1);
      Data += RetainOpLength - 1;
    }
    unsigned ReleaseOpLength =
        endian::readNext<uint16_t, llvm::endianness::little>(Data);
    if (ReleaseOpLength > 0) {
      Info.SwiftReleaseOp = std::string(reinterpret_cast<const char *>(Data),
                                        ReleaseOpLength - 1);
      Data += ReleaseOpLength - 1;
    }

    ReadCommonTypeInfo(Data, Info);
    return Info;
  }
};

/// Used to deserialize the on-disk typedef table.
class TypedefTableInfo
    : public VersionedTableInfo<TypedefTableInfo, SingleDeclTableKey,
                                TypedefInfo> {
public:
  static internal_key_type ReadKey(const uint8_t *Data, unsigned Length) {
    auto CtxID = endian::readNext<uint32_t, llvm::endianness::little>(Data);
    auto nameID =
        endian::readNext<IdentifierID, llvm::endianness::little>(Data);
    return {CtxID, nameID};
  }

  hash_value_type ComputeHash(internal_key_type Key) {
    return static_cast<size_t>(Key.hashValue());
  }

  static TypedefInfo readUnversioned(internal_key_type Key,
                                     const uint8_t *&Data) {
    TypedefInfo Info;

    uint8_t Payload = *Data++;
    if (Payload > 0)
      Info.SwiftWrapper = static_cast<SwiftNewTypeKind>((Payload & 0x3) - 1);

    ReadCommonTypeInfo(Data, Info);
    return Info;
  }
};
} // end anonymous namespace

class APINotesReader::Implementation {
public:
  /// The input buffer for the API notes data.
  llvm::MemoryBuffer *InputBuffer;

  /// The Swift version to use for filtering.
  llvm::VersionTuple SwiftVersion;

  /// The name of the module that we read from the control block.
  std::string ModuleName;

  // The size and modification time of the source file from
  // which this API notes file was created, if known.
  std::optional<std::pair<off_t, time_t>> SourceFileSizeAndModTime;

  using SerializedIdentifierTable =
      llvm::OnDiskIterableChainedHashTable<IdentifierTableInfo>;

  /// The identifier table.
  std::unique_ptr<SerializedIdentifierTable> IdentifierTable;

  using SerializedContextIDTable =
      llvm::OnDiskIterableChainedHashTable<ContextIDTableInfo>;

  /// The Objective-C / C++ context ID table.
  std::unique_ptr<SerializedContextIDTable> ContextIDTable;

  using SerializedContextInfoTable =
      llvm::OnDiskIterableChainedHashTable<ContextInfoTableInfo>;

  /// The Objective-C context info table.
  std::unique_ptr<SerializedContextInfoTable> ContextInfoTable;

  using SerializedObjCPropertyTable =
      llvm::OnDiskIterableChainedHashTable<ObjCPropertyTableInfo>;

  /// The Objective-C property table.
  std::unique_ptr<SerializedObjCPropertyTable> ObjCPropertyTable;

  using SerializedObjCMethodTable =
      llvm::OnDiskIterableChainedHashTable<ObjCMethodTableInfo>;

  /// The Objective-C method table.
  std::unique_ptr<SerializedObjCMethodTable> ObjCMethodTable;

  using SerializedCXXMethodTable =
      llvm::OnDiskIterableChainedHashTable<CXXMethodTableInfo>;

  /// The C++ method table.
  std::unique_ptr<SerializedCXXMethodTable> CXXMethodTable;

  using SerializedObjCSelectorTable =
      llvm::OnDiskIterableChainedHashTable<ObjCSelectorTableInfo>;

  /// The Objective-C selector table.
  std::unique_ptr<SerializedObjCSelectorTable> ObjCSelectorTable;

  using SerializedGlobalVariableTable =
      llvm::OnDiskIterableChainedHashTable<GlobalVariableTableInfo>;

  /// The global variable table.
  std::unique_ptr<SerializedGlobalVariableTable> GlobalVariableTable;

  using SerializedGlobalFunctionTable =
      llvm::OnDiskIterableChainedHashTable<GlobalFunctionTableInfo>;

  /// The global function table.
  std::unique_ptr<SerializedGlobalFunctionTable> GlobalFunctionTable;

  using SerializedEnumConstantTable =
      llvm::OnDiskIterableChainedHashTable<EnumConstantTableInfo>;

  /// The enumerator table.
  std::unique_ptr<SerializedEnumConstantTable> EnumConstantTable;

  using SerializedTagTable = llvm::OnDiskIterableChainedHashTable<TagTableInfo>;

  /// The tag table.
  std::unique_ptr<SerializedTagTable> TagTable;

  using SerializedTypedefTable =
      llvm::OnDiskIterableChainedHashTable<TypedefTableInfo>;

  /// The typedef table.
  std::unique_ptr<SerializedTypedefTable> TypedefTable;

  /// Retrieve the identifier ID for the given string, or an empty
  /// optional if the string is unknown.
  std::optional<IdentifierID> getIdentifier(llvm::StringRef Str);

  /// Retrieve the selector ID for the given selector, or an empty
  /// optional if the string is unknown.
  std::optional<SelectorID> getSelector(ObjCSelectorRef Selector);

  bool readControlBlock(llvm::BitstreamCursor &Cursor,
                        llvm::SmallVectorImpl<uint64_t> &Scratch);
  bool readIdentifierBlock(llvm::BitstreamCursor &Cursor,
                           llvm::SmallVectorImpl<uint64_t> &Scratch);
  bool readContextBlock(llvm::BitstreamCursor &Cursor,
                        llvm::SmallVectorImpl<uint64_t> &Scratch);
  bool readObjCPropertyBlock(llvm::BitstreamCursor &Cursor,
                             llvm::SmallVectorImpl<uint64_t> &Scratch);
  bool readObjCMethodBlock(llvm::BitstreamCursor &Cursor,
                           llvm::SmallVectorImpl<uint64_t> &Scratch);
  bool readCXXMethodBlock(llvm::BitstreamCursor &Cursor,
                          llvm::SmallVectorImpl<uint64_t> &Scratch);
  bool readObjCSelectorBlock(llvm::BitstreamCursor &Cursor,
                             llvm::SmallVectorImpl<uint64_t> &Scratch);
  bool readGlobalVariableBlock(llvm::BitstreamCursor &Cursor,
                               llvm::SmallVectorImpl<uint64_t> &Scratch);
  bool readGlobalFunctionBlock(llvm::BitstreamCursor &Cursor,
                               llvm::SmallVectorImpl<uint64_t> &Scratch);
  bool readEnumConstantBlock(llvm::BitstreamCursor &Cursor,
                             llvm::SmallVectorImpl<uint64_t> &Scratch);
  bool readTagBlock(llvm::BitstreamCursor &Cursor,
                    llvm::SmallVectorImpl<uint64_t> &Scratch);
  bool readTypedefBlock(llvm::BitstreamCursor &Cursor,
                        llvm::SmallVectorImpl<uint64_t> &Scratch);
};

std::optional<IdentifierID>
APINotesReader::Implementation::getIdentifier(llvm::StringRef Str) {
  if (!IdentifierTable)
    return std::nullopt;

  if (Str.empty())
    return IdentifierID(0);

  auto Known = IdentifierTable->find(Str);
  if (Known == IdentifierTable->end())
    return std::nullopt;

  return *Known;
}

std::optional<SelectorID>
APINotesReader::Implementation::getSelector(ObjCSelectorRef Selector) {
  if (!ObjCSelectorTable || !IdentifierTable)
    return std::nullopt;

  // Translate the identifiers.
  StoredObjCSelector Key;
  Key.NumArgs = Selector.NumArgs;
  for (auto Ident : Selector.Identifiers) {
    if (auto IdentID = getIdentifier(Ident)) {
      Key.Identifiers.push_back(*IdentID);
    } else {
      return std::nullopt;
    }
  }

  auto Known = ObjCSelectorTable->find(Key);
  if (Known == ObjCSelectorTable->end())
    return std::nullopt;

  return *Known;
}

bool APINotesReader::Implementation::readControlBlock(
    llvm::BitstreamCursor &Cursor, llvm::SmallVectorImpl<uint64_t> &Scratch) {
  if (Cursor.EnterSubBlock(CONTROL_BLOCK_ID))
    return true;

  bool SawMetadata = false;

  llvm::Expected<llvm::BitstreamEntry> MaybeNext = Cursor.advance();
  if (!MaybeNext) {
    // FIXME this drops the error on the floor.
    consumeError(MaybeNext.takeError());
    return false;
  }
  llvm::BitstreamEntry Next = MaybeNext.get();

  while (Next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (Next.Kind == llvm::BitstreamEntry::Error)
      return true;

    if (Next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown metadata sub-block, possibly for use by a future version of the
      // API notes format.
      if (Cursor.SkipBlock())
        return true;

      MaybeNext = Cursor.advance();
      if (!MaybeNext) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeNext.takeError());
        return false;
      }
      Next = MaybeNext.get();
      continue;
    }

    Scratch.clear();
    llvm::StringRef BlobData;
    llvm::Expected<unsigned> MaybeKind =
        Cursor.readRecord(Next.ID, Scratch, &BlobData);
    if (!MaybeKind) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeKind.takeError());
      return false;
    }
    unsigned Kind = MaybeKind.get();

    switch (Kind) {
    case control_block::METADATA:
      // Already saw metadata.
      if (SawMetadata)
        return true;

      if (Scratch[0] != VERSION_MAJOR || Scratch[1] != VERSION_MINOR)
        return true;

      SawMetadata = true;
      break;

    case control_block::MODULE_NAME:
      ModuleName = BlobData.str();
      break;

    case control_block::MODULE_OPTIONS:
      break;

    case control_block::SOURCE_FILE:
      SourceFileSizeAndModTime = {Scratch[0], Scratch[1]};
      break;

    default:
      // Unknown metadata record, possibly for use by a future version of the
      // module format.
      break;
    }

    MaybeNext = Cursor.advance();
    if (!MaybeNext) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeNext.takeError());
      return false;
    }
    Next = MaybeNext.get();
  }

  return !SawMetadata;
}

bool APINotesReader::Implementation::readIdentifierBlock(
    llvm::BitstreamCursor &Cursor, llvm::SmallVectorImpl<uint64_t> &Scratch) {
  if (Cursor.EnterSubBlock(IDENTIFIER_BLOCK_ID))
    return true;

  llvm::Expected<llvm::BitstreamEntry> MaybeNext = Cursor.advance();
  if (!MaybeNext) {
    // FIXME this drops the error on the floor.
    consumeError(MaybeNext.takeError());
    return false;
  }
  llvm::BitstreamEntry Next = MaybeNext.get();

  while (Next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (Next.Kind == llvm::BitstreamEntry::Error)
      return true;

    if (Next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown sub-block, possibly for use by a future version of the
      // API notes format.
      if (Cursor.SkipBlock())
        return true;

      MaybeNext = Cursor.advance();
      if (!MaybeNext) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeNext.takeError());
        return false;
      }
      Next = MaybeNext.get();
      continue;
    }

    Scratch.clear();
    llvm::StringRef BlobData;
    llvm::Expected<unsigned> MaybeKind =
        Cursor.readRecord(Next.ID, Scratch, &BlobData);
    if (!MaybeKind) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeKind.takeError());
      return false;
    }
    unsigned Kind = MaybeKind.get();
    switch (Kind) {
    case identifier_block::IDENTIFIER_DATA: {
      // Already saw identifier table.
      if (IdentifierTable)
        return true;

      uint32_t tableOffset;
      identifier_block::IdentifierDataLayout::readRecord(Scratch, tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(BlobData.data());

      IdentifierTable.reset(SerializedIdentifierTable::Create(
          base + tableOffset, base + sizeof(uint32_t), base));
      break;
    }

    default:
      // Unknown record, possibly for use by a future version of the
      // module format.
      break;
    }

    MaybeNext = Cursor.advance();
    if (!MaybeNext) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeNext.takeError());
      return false;
    }
    Next = MaybeNext.get();
  }

  return false;
}

bool APINotesReader::Implementation::readContextBlock(
    llvm::BitstreamCursor &Cursor, llvm::SmallVectorImpl<uint64_t> &Scratch) {
  if (Cursor.EnterSubBlock(OBJC_CONTEXT_BLOCK_ID))
    return true;

  llvm::Expected<llvm::BitstreamEntry> MaybeNext = Cursor.advance();
  if (!MaybeNext) {
    // FIXME this drops the error on the floor.
    consumeError(MaybeNext.takeError());
    return false;
  }
  llvm::BitstreamEntry Next = MaybeNext.get();

  while (Next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (Next.Kind == llvm::BitstreamEntry::Error)
      return true;

    if (Next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown sub-block, possibly for use by a future version of the
      // API notes format.
      if (Cursor.SkipBlock())
        return true;

      MaybeNext = Cursor.advance();
      if (!MaybeNext) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeNext.takeError());
        return false;
      }
      Next = MaybeNext.get();
      continue;
    }

    Scratch.clear();
    llvm::StringRef BlobData;
    llvm::Expected<unsigned> MaybeKind =
        Cursor.readRecord(Next.ID, Scratch, &BlobData);
    if (!MaybeKind) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeKind.takeError());
      return false;
    }
    unsigned Kind = MaybeKind.get();
    switch (Kind) {
    case context_block::CONTEXT_ID_DATA: {
      // Already saw Objective-C / C++ context ID table.
      if (ContextIDTable)
        return true;

      uint32_t tableOffset;
      context_block::ContextIDLayout::readRecord(Scratch, tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(BlobData.data());

      ContextIDTable.reset(SerializedContextIDTable::Create(
          base + tableOffset, base + sizeof(uint32_t), base));
      break;
    }

    case context_block::CONTEXT_INFO_DATA: {
      // Already saw Objective-C / C++ context info table.
      if (ContextInfoTable)
        return true;

      uint32_t tableOffset;
      context_block::ContextInfoLayout::readRecord(Scratch, tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(BlobData.data());

      ContextInfoTable.reset(SerializedContextInfoTable::Create(
          base + tableOffset, base + sizeof(uint32_t), base));
      break;
    }

    default:
      // Unknown record, possibly for use by a future version of the
      // module format.
      break;
    }

    MaybeNext = Cursor.advance();
    if (!MaybeNext) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeNext.takeError());
      return false;
    }
    Next = MaybeNext.get();
  }

  return false;
}

bool APINotesReader::Implementation::readObjCPropertyBlock(
    llvm::BitstreamCursor &Cursor, llvm::SmallVectorImpl<uint64_t> &Scratch) {
  if (Cursor.EnterSubBlock(OBJC_PROPERTY_BLOCK_ID))
    return true;

  llvm::Expected<llvm::BitstreamEntry> MaybeNext = Cursor.advance();
  if (!MaybeNext) {
    // FIXME this drops the error on the floor.
    consumeError(MaybeNext.takeError());
    return false;
  }
  llvm::BitstreamEntry Next = MaybeNext.get();

  while (Next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (Next.Kind == llvm::BitstreamEntry::Error)
      return true;

    if (Next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown sub-block, possibly for use by a future version of the
      // API notes format.
      if (Cursor.SkipBlock())
        return true;

      MaybeNext = Cursor.advance();
      if (!MaybeNext) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeNext.takeError());
        return false;
      }
      Next = MaybeNext.get();
      continue;
    }

    Scratch.clear();
    llvm::StringRef BlobData;
    llvm::Expected<unsigned> MaybeKind =
        Cursor.readRecord(Next.ID, Scratch, &BlobData);
    if (!MaybeKind) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeKind.takeError());
      return false;
    }
    unsigned Kind = MaybeKind.get();
    switch (Kind) {
    case objc_property_block::OBJC_PROPERTY_DATA: {
      // Already saw Objective-C property table.
      if (ObjCPropertyTable)
        return true;

      uint32_t tableOffset;
      objc_property_block::ObjCPropertyDataLayout::readRecord(Scratch,
                                                              tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(BlobData.data());

      ObjCPropertyTable.reset(SerializedObjCPropertyTable::Create(
          base + tableOffset, base + sizeof(uint32_t), base));
      break;
    }

    default:
      // Unknown record, possibly for use by a future version of the
      // module format.
      break;
    }

    MaybeNext = Cursor.advance();
    if (!MaybeNext) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeNext.takeError());
      return false;
    }
    Next = MaybeNext.get();
  }

  return false;
}

bool APINotesReader::Implementation::readObjCMethodBlock(
    llvm::BitstreamCursor &Cursor, llvm::SmallVectorImpl<uint64_t> &Scratch) {
  if (Cursor.EnterSubBlock(OBJC_METHOD_BLOCK_ID))
    return true;

  llvm::Expected<llvm::BitstreamEntry> MaybeNext = Cursor.advance();
  if (!MaybeNext) {
    // FIXME this drops the error on the floor.
    consumeError(MaybeNext.takeError());
    return false;
  }
  llvm::BitstreamEntry Next = MaybeNext.get();
  while (Next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (Next.Kind == llvm::BitstreamEntry::Error)
      return true;

    if (Next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown sub-block, possibly for use by a future version of the
      // API notes format.
      if (Cursor.SkipBlock())
        return true;

      MaybeNext = Cursor.advance();
      if (!MaybeNext) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeNext.takeError());
        return false;
      }
      Next = MaybeNext.get();
      continue;
    }

    Scratch.clear();
    llvm::StringRef BlobData;
    llvm::Expected<unsigned> MaybeKind =
        Cursor.readRecord(Next.ID, Scratch, &BlobData);
    if (!MaybeKind) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeKind.takeError());
      return false;
    }
    unsigned Kind = MaybeKind.get();
    switch (Kind) {
    case objc_method_block::OBJC_METHOD_DATA: {
      // Already saw Objective-C method table.
      if (ObjCMethodTable)
        return true;

      uint32_t tableOffset;
      objc_method_block::ObjCMethodDataLayout::readRecord(Scratch, tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(BlobData.data());

      ObjCMethodTable.reset(SerializedObjCMethodTable::Create(
          base + tableOffset, base + sizeof(uint32_t), base));
      break;
    }

    default:
      // Unknown record, possibly for use by a future version of the
      // module format.
      break;
    }

    MaybeNext = Cursor.advance();
    if (!MaybeNext) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeNext.takeError());
      return false;
    }
    Next = MaybeNext.get();
  }

  return false;
}

bool APINotesReader::Implementation::readCXXMethodBlock(
    llvm::BitstreamCursor &Cursor, llvm::SmallVectorImpl<uint64_t> &Scratch) {
  if (Cursor.EnterSubBlock(CXX_METHOD_BLOCK_ID))
    return true;

  llvm::Expected<llvm::BitstreamEntry> MaybeNext = Cursor.advance();
  if (!MaybeNext) {
    // FIXME this drops the error on the floor.
    consumeError(MaybeNext.takeError());
    return false;
  }
  llvm::BitstreamEntry Next = MaybeNext.get();
  while (Next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (Next.Kind == llvm::BitstreamEntry::Error)
      return true;

    if (Next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown sub-block, possibly for use by a future version of the
      // API notes format.
      if (Cursor.SkipBlock())
        return true;

      MaybeNext = Cursor.advance();
      if (!MaybeNext) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeNext.takeError());
        return false;
      }
      Next = MaybeNext.get();
      continue;
    }

    Scratch.clear();
    llvm::StringRef BlobData;
    llvm::Expected<unsigned> MaybeKind =
        Cursor.readRecord(Next.ID, Scratch, &BlobData);
    if (!MaybeKind) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeKind.takeError());
      return false;
    }
    unsigned Kind = MaybeKind.get();
    switch (Kind) {
    case cxx_method_block::CXX_METHOD_DATA: {
      // Already saw C++ method table.
      if (CXXMethodTable)
        return true;

      uint32_t tableOffset;
      cxx_method_block::CXXMethodDataLayout::readRecord(Scratch, tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(BlobData.data());

      CXXMethodTable.reset(SerializedCXXMethodTable::Create(
          base + tableOffset, base + sizeof(uint32_t), base));
      break;
    }

    default:
      // Unknown record, possibly for use by a future version of the
      // module format.
      break;
    }

    MaybeNext = Cursor.advance();
    if (!MaybeNext) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeNext.takeError());
      return false;
    }
    Next = MaybeNext.get();
  }

  return false;
}

bool APINotesReader::Implementation::readObjCSelectorBlock(
    llvm::BitstreamCursor &Cursor, llvm::SmallVectorImpl<uint64_t> &Scratch) {
  if (Cursor.EnterSubBlock(OBJC_SELECTOR_BLOCK_ID))
    return true;

  llvm::Expected<llvm::BitstreamEntry> MaybeNext = Cursor.advance();
  if (!MaybeNext) {
    // FIXME this drops the error on the floor.
    consumeError(MaybeNext.takeError());
    return false;
  }
  llvm::BitstreamEntry Next = MaybeNext.get();
  while (Next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (Next.Kind == llvm::BitstreamEntry::Error)
      return true;

    if (Next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown sub-block, possibly for use by a future version of the
      // API notes format.
      if (Cursor.SkipBlock())
        return true;

      MaybeNext = Cursor.advance();
      if (!MaybeNext) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeNext.takeError());
        return false;
      }
      Next = MaybeNext.get();
      continue;
    }

    Scratch.clear();
    llvm::StringRef BlobData;
    llvm::Expected<unsigned> MaybeKind =
        Cursor.readRecord(Next.ID, Scratch, &BlobData);
    if (!MaybeKind) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeKind.takeError());
      return false;
    }
    unsigned Kind = MaybeKind.get();
    switch (Kind) {
    case objc_selector_block::OBJC_SELECTOR_DATA: {
      // Already saw Objective-C selector table.
      if (ObjCSelectorTable)
        return true;

      uint32_t tableOffset;
      objc_selector_block::ObjCSelectorDataLayout::readRecord(Scratch,
                                                              tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(BlobData.data());

      ObjCSelectorTable.reset(SerializedObjCSelectorTable::Create(
          base + tableOffset, base + sizeof(uint32_t), base));
      break;
    }

    default:
      // Unknown record, possibly for use by a future version of the
      // module format.
      break;
    }

    MaybeNext = Cursor.advance();
    if (!MaybeNext) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeNext.takeError());
      return false;
    }
    Next = MaybeNext.get();
  }

  return false;
}

bool APINotesReader::Implementation::readGlobalVariableBlock(
    llvm::BitstreamCursor &Cursor, llvm::SmallVectorImpl<uint64_t> &Scratch) {
  if (Cursor.EnterSubBlock(GLOBAL_VARIABLE_BLOCK_ID))
    return true;

  llvm::Expected<llvm::BitstreamEntry> MaybeNext = Cursor.advance();
  if (!MaybeNext) {
    // FIXME this drops the error on the floor.
    consumeError(MaybeNext.takeError());
    return false;
  }
  llvm::BitstreamEntry Next = MaybeNext.get();
  while (Next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (Next.Kind == llvm::BitstreamEntry::Error)
      return true;

    if (Next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown sub-block, possibly for use by a future version of the
      // API notes format.
      if (Cursor.SkipBlock())
        return true;

      MaybeNext = Cursor.advance();
      if (!MaybeNext) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeNext.takeError());
        return false;
      }
      Next = MaybeNext.get();
      continue;
    }

    Scratch.clear();
    llvm::StringRef BlobData;
    llvm::Expected<unsigned> MaybeKind =
        Cursor.readRecord(Next.ID, Scratch, &BlobData);
    if (!MaybeKind) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeKind.takeError());
      return false;
    }
    unsigned Kind = MaybeKind.get();
    switch (Kind) {
    case global_variable_block::GLOBAL_VARIABLE_DATA: {
      // Already saw global variable table.
      if (GlobalVariableTable)
        return true;

      uint32_t tableOffset;
      global_variable_block::GlobalVariableDataLayout::readRecord(Scratch,
                                                                  tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(BlobData.data());

      GlobalVariableTable.reset(SerializedGlobalVariableTable::Create(
          base + tableOffset, base + sizeof(uint32_t), base));
      break;
    }

    default:
      // Unknown record, possibly for use by a future version of the
      // module format.
      break;
    }

    MaybeNext = Cursor.advance();
    if (!MaybeNext) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeNext.takeError());
      return false;
    }
    Next = MaybeNext.get();
  }

  return false;
}

bool APINotesReader::Implementation::readGlobalFunctionBlock(
    llvm::BitstreamCursor &Cursor, llvm::SmallVectorImpl<uint64_t> &Scratch) {
  if (Cursor.EnterSubBlock(GLOBAL_FUNCTION_BLOCK_ID))
    return true;

  llvm::Expected<llvm::BitstreamEntry> MaybeNext = Cursor.advance();
  if (!MaybeNext) {
    // FIXME this drops the error on the floor.
    consumeError(MaybeNext.takeError());
    return false;
  }
  llvm::BitstreamEntry Next = MaybeNext.get();
  while (Next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (Next.Kind == llvm::BitstreamEntry::Error)
      return true;

    if (Next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown sub-block, possibly for use by a future version of the
      // API notes format.
      if (Cursor.SkipBlock())
        return true;

      MaybeNext = Cursor.advance();
      if (!MaybeNext) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeNext.takeError());
        return false;
      }
      Next = MaybeNext.get();
      continue;
    }

    Scratch.clear();
    llvm::StringRef BlobData;
    llvm::Expected<unsigned> MaybeKind =
        Cursor.readRecord(Next.ID, Scratch, &BlobData);
    if (!MaybeKind) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeKind.takeError());
      return false;
    }
    unsigned Kind = MaybeKind.get();
    switch (Kind) {
    case global_function_block::GLOBAL_FUNCTION_DATA: {
      // Already saw global function table.
      if (GlobalFunctionTable)
        return true;

      uint32_t tableOffset;
      global_function_block::GlobalFunctionDataLayout::readRecord(Scratch,
                                                                  tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(BlobData.data());

      GlobalFunctionTable.reset(SerializedGlobalFunctionTable::Create(
          base + tableOffset, base + sizeof(uint32_t), base));
      break;
    }

    default:
      // Unknown record, possibly for use by a future version of the
      // module format.
      break;
    }

    MaybeNext = Cursor.advance();
    if (!MaybeNext) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeNext.takeError());
      return false;
    }
    Next = MaybeNext.get();
  }

  return false;
}

bool APINotesReader::Implementation::readEnumConstantBlock(
    llvm::BitstreamCursor &Cursor, llvm::SmallVectorImpl<uint64_t> &Scratch) {
  if (Cursor.EnterSubBlock(ENUM_CONSTANT_BLOCK_ID))
    return true;

  llvm::Expected<llvm::BitstreamEntry> MaybeNext = Cursor.advance();
  if (!MaybeNext) {
    // FIXME this drops the error on the floor.
    consumeError(MaybeNext.takeError());
    return false;
  }
  llvm::BitstreamEntry Next = MaybeNext.get();
  while (Next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (Next.Kind == llvm::BitstreamEntry::Error)
      return true;

    if (Next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown sub-block, possibly for use by a future version of the
      // API notes format.
      if (Cursor.SkipBlock())
        return true;

      MaybeNext = Cursor.advance();
      if (!MaybeNext) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeNext.takeError());
        return false;
      }
      Next = MaybeNext.get();
      continue;
    }

    Scratch.clear();
    llvm::StringRef BlobData;
    llvm::Expected<unsigned> MaybeKind =
        Cursor.readRecord(Next.ID, Scratch, &BlobData);
    if (!MaybeKind) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeKind.takeError());
      return false;
    }
    unsigned Kind = MaybeKind.get();
    switch (Kind) {
    case enum_constant_block::ENUM_CONSTANT_DATA: {
      // Already saw enumerator table.
      if (EnumConstantTable)
        return true;

      uint32_t tableOffset;
      enum_constant_block::EnumConstantDataLayout::readRecord(Scratch,
                                                              tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(BlobData.data());

      EnumConstantTable.reset(SerializedEnumConstantTable::Create(
          base + tableOffset, base + sizeof(uint32_t), base));
      break;
    }

    default:
      // Unknown record, possibly for use by a future version of the
      // module format.
      break;
    }

    MaybeNext = Cursor.advance();
    if (!MaybeNext) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeNext.takeError());
      return false;
    }
    Next = MaybeNext.get();
  }

  return false;
}

bool APINotesReader::Implementation::readTagBlock(
    llvm::BitstreamCursor &Cursor, llvm::SmallVectorImpl<uint64_t> &Scratch) {
  if (Cursor.EnterSubBlock(TAG_BLOCK_ID))
    return true;

  llvm::Expected<llvm::BitstreamEntry> MaybeNext = Cursor.advance();
  if (!MaybeNext) {
    // FIXME this drops the error on the floor.
    consumeError(MaybeNext.takeError());
    return false;
  }
  llvm::BitstreamEntry Next = MaybeNext.get();
  while (Next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (Next.Kind == llvm::BitstreamEntry::Error)
      return true;

    if (Next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown sub-block, possibly for use by a future version of the
      // API notes format.
      if (Cursor.SkipBlock())
        return true;

      MaybeNext = Cursor.advance();
      if (!MaybeNext) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeNext.takeError());
        return false;
      }
      Next = MaybeNext.get();
      continue;
    }

    Scratch.clear();
    llvm::StringRef BlobData;
    llvm::Expected<unsigned> MaybeKind =
        Cursor.readRecord(Next.ID, Scratch, &BlobData);
    if (!MaybeKind) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeKind.takeError());
      return false;
    }
    unsigned Kind = MaybeKind.get();
    switch (Kind) {
    case tag_block::TAG_DATA: {
      // Already saw tag table.
      if (TagTable)
        return true;

      uint32_t tableOffset;
      tag_block::TagDataLayout::readRecord(Scratch, tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(BlobData.data());

      TagTable.reset(SerializedTagTable::Create(base + tableOffset,
                                                base + sizeof(uint32_t), base));
      break;
    }

    default:
      // Unknown record, possibly for use by a future version of the
      // module format.
      break;
    }

    MaybeNext = Cursor.advance();
    if (!MaybeNext) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeNext.takeError());
      return false;
    }
    Next = MaybeNext.get();
  }

  return false;
}

bool APINotesReader::Implementation::readTypedefBlock(
    llvm::BitstreamCursor &Cursor, llvm::SmallVectorImpl<uint64_t> &Scratch) {
  if (Cursor.EnterSubBlock(TYPEDEF_BLOCK_ID))
    return true;

  llvm::Expected<llvm::BitstreamEntry> MaybeNext = Cursor.advance();
  if (!MaybeNext) {
    // FIXME this drops the error on the floor.
    consumeError(MaybeNext.takeError());
    return false;
  }
  llvm::BitstreamEntry Next = MaybeNext.get();
  while (Next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (Next.Kind == llvm::BitstreamEntry::Error)
      return true;

    if (Next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown sub-block, possibly for use by a future version of the
      // API notes format.
      if (Cursor.SkipBlock())
        return true;

      MaybeNext = Cursor.advance();
      if (!MaybeNext) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeNext.takeError());
        return false;
      }
      Next = MaybeNext.get();
      continue;
    }

    Scratch.clear();
    llvm::StringRef BlobData;
    llvm::Expected<unsigned> MaybeKind =
        Cursor.readRecord(Next.ID, Scratch, &BlobData);
    if (!MaybeKind) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeKind.takeError());
      return false;
    }
    unsigned Kind = MaybeKind.get();
    switch (Kind) {
    case typedef_block::TYPEDEF_DATA: {
      // Already saw typedef table.
      if (TypedefTable)
        return true;

      uint32_t tableOffset;
      typedef_block::TypedefDataLayout::readRecord(Scratch, tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(BlobData.data());

      TypedefTable.reset(SerializedTypedefTable::Create(
          base + tableOffset, base + sizeof(uint32_t), base));
      break;
    }

    default:
      // Unknown record, possibly for use by a future version of the
      // module format.
      break;
    }

    MaybeNext = Cursor.advance();
    if (!MaybeNext) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeNext.takeError());
      return false;
    }
    Next = MaybeNext.get();
  }

  return false;
}

APINotesReader::APINotesReader(llvm::MemoryBuffer *InputBuffer,
                               llvm::VersionTuple SwiftVersion, bool &Failed)
    : Implementation(new class Implementation) {
  Failed = false;

  // Initialize the input buffer.
  Implementation->InputBuffer = InputBuffer;
  Implementation->SwiftVersion = SwiftVersion;
  llvm::BitstreamCursor Cursor(*Implementation->InputBuffer);

  // Validate signature.
  for (auto byte : API_NOTES_SIGNATURE) {
    if (Cursor.AtEndOfStream()) {
      Failed = true;
      return;
    }
    if (llvm::Expected<llvm::SimpleBitstreamCursor::word_t> maybeRead =
            Cursor.Read(8)) {
      if (maybeRead.get() != byte) {
        Failed = true;
        return;
      }
    } else {
      // FIXME this drops the error on the floor.
      consumeError(maybeRead.takeError());
      Failed = true;
      return;
    }
  }

  // Look at all of the blocks.
  bool HasValidControlBlock = false;
  llvm::SmallVector<uint64_t, 64> Scratch;
  while (!Cursor.AtEndOfStream()) {
    llvm::Expected<llvm::BitstreamEntry> MaybeTopLevelEntry = Cursor.advance();
    if (!MaybeTopLevelEntry) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeTopLevelEntry.takeError());
      Failed = true;
      return;
    }
    llvm::BitstreamEntry TopLevelEntry = MaybeTopLevelEntry.get();

    if (TopLevelEntry.Kind != llvm::BitstreamEntry::SubBlock)
      break;

    switch (TopLevelEntry.ID) {
    case llvm::bitc::BLOCKINFO_BLOCK_ID:
      if (!Cursor.ReadBlockInfoBlock()) {
        Failed = true;
        break;
      }
      break;

    case CONTROL_BLOCK_ID:
      // Only allow a single control block.
      if (HasValidControlBlock ||
          Implementation->readControlBlock(Cursor, Scratch)) {
        Failed = true;
        return;
      }

      HasValidControlBlock = true;
      break;

    case IDENTIFIER_BLOCK_ID:
      if (!HasValidControlBlock ||
          Implementation->readIdentifierBlock(Cursor, Scratch)) {
        Failed = true;
        return;
      }
      break;

    case OBJC_CONTEXT_BLOCK_ID:
      if (!HasValidControlBlock ||
          Implementation->readContextBlock(Cursor, Scratch)) {
        Failed = true;
        return;
      }

      break;

    case OBJC_PROPERTY_BLOCK_ID:
      if (!HasValidControlBlock ||
          Implementation->readObjCPropertyBlock(Cursor, Scratch)) {
        Failed = true;
        return;
      }
      break;

    case OBJC_METHOD_BLOCK_ID:
      if (!HasValidControlBlock ||
          Implementation->readObjCMethodBlock(Cursor, Scratch)) {
        Failed = true;
        return;
      }
      break;

    case CXX_METHOD_BLOCK_ID:
      if (!HasValidControlBlock ||
          Implementation->readCXXMethodBlock(Cursor, Scratch)) {
        Failed = true;
        return;
      }
      break;

    case OBJC_SELECTOR_BLOCK_ID:
      if (!HasValidControlBlock ||
          Implementation->readObjCSelectorBlock(Cursor, Scratch)) {
        Failed = true;
        return;
      }
      break;

    case GLOBAL_VARIABLE_BLOCK_ID:
      if (!HasValidControlBlock ||
          Implementation->readGlobalVariableBlock(Cursor, Scratch)) {
        Failed = true;
        return;
      }
      break;

    case GLOBAL_FUNCTION_BLOCK_ID:
      if (!HasValidControlBlock ||
          Implementation->readGlobalFunctionBlock(Cursor, Scratch)) {
        Failed = true;
        return;
      }
      break;

    case ENUM_CONSTANT_BLOCK_ID:
      if (!HasValidControlBlock ||
          Implementation->readEnumConstantBlock(Cursor, Scratch)) {
        Failed = true;
        return;
      }
      break;

    case TAG_BLOCK_ID:
      if (!HasValidControlBlock ||
          Implementation->readTagBlock(Cursor, Scratch)) {
        Failed = true;
        return;
      }
      break;

    case TYPEDEF_BLOCK_ID:
      if (!HasValidControlBlock ||
          Implementation->readTypedefBlock(Cursor, Scratch)) {
        Failed = true;
        return;
      }
      break;

    default:
      // Unknown top-level block, possibly for use by a future version of the
      // module format.
      if (Cursor.SkipBlock()) {
        Failed = true;
        return;
      }
      break;
    }
  }

  if (!Cursor.AtEndOfStream()) {
    Failed = true;
    return;
  }
}

APINotesReader::~APINotesReader() { delete Implementation->InputBuffer; }

std::unique_ptr<APINotesReader>
APINotesReader::Create(std::unique_ptr<llvm::MemoryBuffer> InputBuffer,
                       llvm::VersionTuple SwiftVersion) {
  bool Failed = false;
  std::unique_ptr<APINotesReader> Reader(
      new APINotesReader(InputBuffer.release(), SwiftVersion, Failed));
  if (Failed)
    return nullptr;

  return Reader;
}

template <typename T>
APINotesReader::VersionedInfo<T>::VersionedInfo(
    llvm::VersionTuple Version,
    llvm::SmallVector<std::pair<llvm::VersionTuple, T>, 1> R)
    : Results(std::move(R)) {

  assert(!Results.empty());
  assert(std::is_sorted(
      Results.begin(), Results.end(),
      [](const std::pair<llvm::VersionTuple, T> &left,
         const std::pair<llvm::VersionTuple, T> &right) -> bool {
        assert(left.first != right.first && "two entries for the same version");
        return left.first < right.first;
      }));

  Selected = std::nullopt;
  for (unsigned i = 0, n = Results.size(); i != n; ++i) {
    if (!Version.empty() && Results[i].first >= Version) {
      // If the current version is "4", then entries for 4 are better than
      // entries for 5, but both are valid. Because entries are sorted, we get
      // that behavior by picking the first match.
      Selected = i;
      break;
    }
  }

  // If we didn't find a match but we have an unversioned result, use the
  // unversioned result. This will always be the first entry because we encode
  // it as version 0.
  if (!Selected && Results[0].first.empty())
    Selected = 0;
}

auto APINotesReader::lookupObjCClassID(llvm::StringRef Name)
    -> std::optional<ContextID> {
  if (!Implementation->ContextIDTable)
    return std::nullopt;

  std::optional<IdentifierID> ClassID = Implementation->getIdentifier(Name);
  if (!ClassID)
    return std::nullopt;

  // ObjC classes can't be declared in C++ namespaces, so use -1 as the global
  // context.
  auto KnownID = Implementation->ContextIDTable->find(
      ContextTableKey(-1, (uint8_t)ContextKind::ObjCClass, *ClassID));
  if (KnownID == Implementation->ContextIDTable->end())
    return std::nullopt;

  return ContextID(*KnownID);
}

auto APINotesReader::lookupObjCClassInfo(llvm::StringRef Name)
    -> VersionedInfo<ContextInfo> {
  if (!Implementation->ContextInfoTable)
    return std::nullopt;

  std::optional<ContextID> CtxID = lookupObjCClassID(Name);
  if (!CtxID)
    return std::nullopt;

  auto KnownInfo = Implementation->ContextInfoTable->find(CtxID->Value);
  if (KnownInfo == Implementation->ContextInfoTable->end())
    return std::nullopt;

  return {Implementation->SwiftVersion, *KnownInfo};
}

auto APINotesReader::lookupObjCProtocolID(llvm::StringRef Name)
    -> std::optional<ContextID> {
  if (!Implementation->ContextIDTable)
    return std::nullopt;

  std::optional<IdentifierID> classID = Implementation->getIdentifier(Name);
  if (!classID)
    return std::nullopt;

  // ObjC classes can't be declared in C++ namespaces, so use -1 as the global
  // context.
  auto KnownID = Implementation->ContextIDTable->find(
      ContextTableKey(-1, (uint8_t)ContextKind::ObjCProtocol, *classID));
  if (KnownID == Implementation->ContextIDTable->end())
    return std::nullopt;

  return ContextID(*KnownID);
}

auto APINotesReader::lookupObjCProtocolInfo(llvm::StringRef Name)
    -> VersionedInfo<ContextInfo> {
  if (!Implementation->ContextInfoTable)
    return std::nullopt;

  std::optional<ContextID> CtxID = lookupObjCProtocolID(Name);
  if (!CtxID)
    return std::nullopt;

  auto KnownInfo = Implementation->ContextInfoTable->find(CtxID->Value);
  if (KnownInfo == Implementation->ContextInfoTable->end())
    return std::nullopt;

  return {Implementation->SwiftVersion, *KnownInfo};
}

auto APINotesReader::lookupObjCProperty(ContextID CtxID, llvm::StringRef Name,
                                        bool IsInstance)
    -> VersionedInfo<ObjCPropertyInfo> {
  if (!Implementation->ObjCPropertyTable)
    return std::nullopt;

  std::optional<IdentifierID> PropertyID = Implementation->getIdentifier(Name);
  if (!PropertyID)
    return std::nullopt;

  auto Known = Implementation->ObjCPropertyTable->find(
      std::make_tuple(CtxID.Value, *PropertyID, (char)IsInstance));
  if (Known == Implementation->ObjCPropertyTable->end())
    return std::nullopt;

  return {Implementation->SwiftVersion, *Known};
}

auto APINotesReader::lookupObjCMethod(ContextID CtxID, ObjCSelectorRef Selector,
                                      bool IsInstanceMethod)
    -> VersionedInfo<ObjCMethodInfo> {
  if (!Implementation->ObjCMethodTable)
    return std::nullopt;

  std::optional<SelectorID> SelID = Implementation->getSelector(Selector);
  if (!SelID)
    return std::nullopt;

  auto Known = Implementation->ObjCMethodTable->find(
      ObjCMethodTableInfo::internal_key_type{CtxID.Value, *SelID,
                                             IsInstanceMethod});
  if (Known == Implementation->ObjCMethodTable->end())
    return std::nullopt;

  return {Implementation->SwiftVersion, *Known};
}

auto APINotesReader::lookupCXXMethod(ContextID CtxID, llvm::StringRef Name)
    -> VersionedInfo<CXXMethodInfo> {
  if (!Implementation->CXXMethodTable)
    return std::nullopt;

  std::optional<IdentifierID> NameID = Implementation->getIdentifier(Name);
  if (!NameID)
    return std::nullopt;

  auto Known = Implementation->CXXMethodTable->find(
      SingleDeclTableKey(CtxID.Value, *NameID));
  if (Known == Implementation->CXXMethodTable->end())
    return std::nullopt;

  return {Implementation->SwiftVersion, *Known};
}

auto APINotesReader::lookupGlobalVariable(llvm::StringRef Name,
                                          std::optional<Context> Ctx)
    -> VersionedInfo<GlobalVariableInfo> {
  if (!Implementation->GlobalVariableTable)
    return std::nullopt;

  std::optional<IdentifierID> NameID = Implementation->getIdentifier(Name);
  if (!NameID)
    return std::nullopt;

  SingleDeclTableKey Key(Ctx, *NameID);

  auto Known = Implementation->GlobalVariableTable->find(Key);
  if (Known == Implementation->GlobalVariableTable->end())
    return std::nullopt;

  return {Implementation->SwiftVersion, *Known};
}

auto APINotesReader::lookupGlobalFunction(llvm::StringRef Name,
                                          std::optional<Context> Ctx)
    -> VersionedInfo<GlobalFunctionInfo> {
  if (!Implementation->GlobalFunctionTable)
    return std::nullopt;

  std::optional<IdentifierID> NameID = Implementation->getIdentifier(Name);
  if (!NameID)
    return std::nullopt;

  SingleDeclTableKey Key(Ctx, *NameID);

  auto Known = Implementation->GlobalFunctionTable->find(Key);
  if (Known == Implementation->GlobalFunctionTable->end())
    return std::nullopt;

  return {Implementation->SwiftVersion, *Known};
}

auto APINotesReader::lookupEnumConstant(llvm::StringRef Name)
    -> VersionedInfo<EnumConstantInfo> {
  if (!Implementation->EnumConstantTable)
    return std::nullopt;

  std::optional<IdentifierID> NameID = Implementation->getIdentifier(Name);
  if (!NameID)
    return std::nullopt;

  auto Known = Implementation->EnumConstantTable->find(*NameID);
  if (Known == Implementation->EnumConstantTable->end())
    return std::nullopt;

  return {Implementation->SwiftVersion, *Known};
}

auto APINotesReader::lookupTagID(llvm::StringRef Name,
                                 std::optional<Context> ParentCtx)
    -> std::optional<ContextID> {
  if (!Implementation->ContextIDTable)
    return std::nullopt;

  std::optional<IdentifierID> TagID = Implementation->getIdentifier(Name);
  if (!TagID)
    return std::nullopt;

  auto KnownID = Implementation->ContextIDTable->find(
      ContextTableKey(ParentCtx, ContextKind::Tag, *TagID));
  if (KnownID == Implementation->ContextIDTable->end())
    return std::nullopt;

  return ContextID(*KnownID);
}

auto APINotesReader::lookupTag(llvm::StringRef Name, std::optional<Context> Ctx)
    -> VersionedInfo<TagInfo> {
  if (!Implementation->TagTable)
    return std::nullopt;

  std::optional<IdentifierID> NameID = Implementation->getIdentifier(Name);
  if (!NameID)
    return std::nullopt;

  SingleDeclTableKey Key(Ctx, *NameID);

  auto Known = Implementation->TagTable->find(Key);
  if (Known == Implementation->TagTable->end())
    return std::nullopt;

  return {Implementation->SwiftVersion, *Known};
}

auto APINotesReader::lookupTypedef(llvm::StringRef Name,
                                   std::optional<Context> Ctx)
    -> VersionedInfo<TypedefInfo> {
  if (!Implementation->TypedefTable)
    return std::nullopt;

  std::optional<IdentifierID> NameID = Implementation->getIdentifier(Name);
  if (!NameID)
    return std::nullopt;

  SingleDeclTableKey Key(Ctx, *NameID);

  auto Known = Implementation->TypedefTable->find(Key);
  if (Known == Implementation->TypedefTable->end())
    return std::nullopt;

  return {Implementation->SwiftVersion, *Known};
}

auto APINotesReader::lookupNamespaceID(
    llvm::StringRef Name, std::optional<ContextID> ParentNamespaceID)
    -> std::optional<ContextID> {
  if (!Implementation->ContextIDTable)
    return std::nullopt;

  std::optional<IdentifierID> NamespaceID = Implementation->getIdentifier(Name);
  if (!NamespaceID)
    return std::nullopt;

  uint32_t RawParentNamespaceID =
      ParentNamespaceID ? ParentNamespaceID->Value : -1;
  auto KnownID = Implementation->ContextIDTable->find(
      {RawParentNamespaceID, (uint8_t)ContextKind::Namespace, *NamespaceID});
  if (KnownID == Implementation->ContextIDTable->end())
    return std::nullopt;

  return ContextID(*KnownID);
}

} // namespace api_notes
} // namespace clang

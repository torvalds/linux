//===-- APINotesWriter.cpp - API Notes Writer -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/APINotes/APINotesWriter.h"
#include "APINotesFormat.h"
#include "clang/APINotes/Types.h"
#include "clang/Basic/FileManager.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/OnDiskHashTable.h"
#include "llvm/Support/VersionTuple.h"

namespace clang {
namespace api_notes {
class APINotesWriter::Implementation {
  friend class APINotesWriter;

  template <typename T>
  using VersionedSmallVector =
      llvm::SmallVector<std::pair<llvm::VersionTuple, T>, 1>;

  std::string ModuleName;
  const FileEntry *SourceFile;

  /// Scratch space for bitstream writing.
  llvm::SmallVector<uint64_t, 64> Scratch;

  /// Mapping from strings to identifier IDs.
  llvm::StringMap<IdentifierID> IdentifierIDs;

  /// Information about contexts (Objective-C classes or protocols or C++
  /// namespaces).
  ///
  /// Indexed by the parent context ID, context kind and the identifier ID of
  /// this context and provides both the context ID and information describing
  /// the context within that module.
  llvm::DenseMap<ContextTableKey,
                 std::pair<unsigned, VersionedSmallVector<ContextInfo>>>
      Contexts;

  /// Information about parent contexts for each context.
  ///
  /// Indexed by context ID, provides the parent context ID.
  llvm::DenseMap<uint32_t, uint32_t> ParentContexts;

  /// Mapping from context IDs to the identifier ID holding the name.
  llvm::DenseMap<unsigned, unsigned> ContextNames;

  /// Information about Objective-C properties.
  ///
  /// Indexed by the context ID, property name, and whether this is an
  /// instance property.
  llvm::DenseMap<
      std::tuple<unsigned, unsigned, char>,
      llvm::SmallVector<std::pair<VersionTuple, ObjCPropertyInfo>, 1>>
      ObjCProperties;

  /// Information about Objective-C methods.
  ///
  /// Indexed by the context ID, selector ID, and Boolean (stored as a char)
  /// indicating whether this is a class or instance method.
  llvm::DenseMap<std::tuple<unsigned, unsigned, char>,
                 llvm::SmallVector<std::pair<VersionTuple, ObjCMethodInfo>, 1>>
      ObjCMethods;

  /// Information about C++ methods.
  ///
  /// Indexed by the context ID and name ID.
  llvm::DenseMap<SingleDeclTableKey,
                 llvm::SmallVector<std::pair<VersionTuple, CXXMethodInfo>, 1>>
      CXXMethods;

  /// Mapping from selectors to selector ID.
  llvm::DenseMap<StoredObjCSelector, SelectorID> SelectorIDs;

  /// Information about global variables.
  ///
  /// Indexed by the context ID, identifier ID.
  llvm::DenseMap<
      SingleDeclTableKey,
      llvm::SmallVector<std::pair<VersionTuple, GlobalVariableInfo>, 1>>
      GlobalVariables;

  /// Information about global functions.
  ///
  /// Indexed by the context ID, identifier ID.
  llvm::DenseMap<
      SingleDeclTableKey,
      llvm::SmallVector<std::pair<VersionTuple, GlobalFunctionInfo>, 1>>
      GlobalFunctions;

  /// Information about enumerators.
  ///
  /// Indexed by the identifier ID.
  llvm::DenseMap<
      unsigned, llvm::SmallVector<std::pair<VersionTuple, EnumConstantInfo>, 1>>
      EnumConstants;

  /// Information about tags.
  ///
  /// Indexed by the context ID, identifier ID.
  llvm::DenseMap<SingleDeclTableKey,
                 llvm::SmallVector<std::pair<VersionTuple, TagInfo>, 1>>
      Tags;

  /// Information about typedefs.
  ///
  /// Indexed by the context ID, identifier ID.
  llvm::DenseMap<SingleDeclTableKey,
                 llvm::SmallVector<std::pair<VersionTuple, TypedefInfo>, 1>>
      Typedefs;

  /// Retrieve the ID for the given identifier.
  IdentifierID getIdentifier(StringRef Identifier) {
    if (Identifier.empty())
      return 0;

    auto Known = IdentifierIDs.find(Identifier);
    if (Known != IdentifierIDs.end())
      return Known->second;

    // Add to the identifier table.
    Known = IdentifierIDs.insert({Identifier, IdentifierIDs.size() + 1}).first;
    return Known->second;
  }

  /// Retrieve the ID for the given selector.
  SelectorID getSelector(ObjCSelectorRef SelectorRef) {
    // Translate the selector reference into a stored selector.
    StoredObjCSelector Selector;
    Selector.NumArgs = SelectorRef.NumArgs;
    Selector.Identifiers.reserve(SelectorRef.Identifiers.size());
    for (auto piece : SelectorRef.Identifiers)
      Selector.Identifiers.push_back(getIdentifier(piece));

    // Look for the stored selector.
    auto Known = SelectorIDs.find(Selector);
    if (Known != SelectorIDs.end())
      return Known->second;

    // Add to the selector table.
    Known = SelectorIDs.insert({Selector, SelectorIDs.size()}).first;
    return Known->second;
  }

private:
  void writeBlockInfoBlock(llvm::BitstreamWriter &Stream);
  void writeControlBlock(llvm::BitstreamWriter &Stream);
  void writeIdentifierBlock(llvm::BitstreamWriter &Stream);
  void writeContextBlock(llvm::BitstreamWriter &Stream);
  void writeObjCPropertyBlock(llvm::BitstreamWriter &Stream);
  void writeObjCMethodBlock(llvm::BitstreamWriter &Stream);
  void writeCXXMethodBlock(llvm::BitstreamWriter &Stream);
  void writeObjCSelectorBlock(llvm::BitstreamWriter &Stream);
  void writeGlobalVariableBlock(llvm::BitstreamWriter &Stream);
  void writeGlobalFunctionBlock(llvm::BitstreamWriter &Stream);
  void writeEnumConstantBlock(llvm::BitstreamWriter &Stream);
  void writeTagBlock(llvm::BitstreamWriter &Stream);
  void writeTypedefBlock(llvm::BitstreamWriter &Stream);

public:
  Implementation(llvm::StringRef ModuleName, const FileEntry *SF)
      : ModuleName(std::string(ModuleName)), SourceFile(SF) {}

  void writeToStream(llvm::raw_ostream &OS);
};

void APINotesWriter::Implementation::writeToStream(llvm::raw_ostream &OS) {
  llvm::SmallVector<char, 0> Buffer;

  {
    llvm::BitstreamWriter Stream(Buffer);

    // Emit the signature.
    for (unsigned char Byte : API_NOTES_SIGNATURE)
      Stream.Emit(Byte, 8);

    // Emit the blocks.
    writeBlockInfoBlock(Stream);
    writeControlBlock(Stream);
    writeIdentifierBlock(Stream);
    writeContextBlock(Stream);
    writeObjCPropertyBlock(Stream);
    writeObjCMethodBlock(Stream);
    writeCXXMethodBlock(Stream);
    writeObjCSelectorBlock(Stream);
    writeGlobalVariableBlock(Stream);
    writeGlobalFunctionBlock(Stream);
    writeEnumConstantBlock(Stream);
    writeTagBlock(Stream);
    writeTypedefBlock(Stream);
  }

  OS.write(Buffer.data(), Buffer.size());
  OS.flush();
}

namespace {
/// Record the name of a block.
void emitBlockID(llvm::BitstreamWriter &Stream, unsigned ID,
                 llvm::StringRef Name) {
  Stream.EmitRecord(llvm::bitc::BLOCKINFO_CODE_SETBID,
                    llvm::ArrayRef<unsigned>{ID});

  // Emit the block name if present.
  if (Name.empty())
    return;
  Stream.EmitRecord(
      llvm::bitc::BLOCKINFO_CODE_BLOCKNAME,
      llvm::ArrayRef<unsigned char>(
          const_cast<unsigned char *>(
              reinterpret_cast<const unsigned char *>(Name.data())),
          Name.size()));
}

/// Record the name of a record within a block.
void emitRecordID(llvm::BitstreamWriter &Stream, unsigned ID,
                  llvm::StringRef Name) {
  assert(ID < 256 && "can't fit record ID in next to name");

  llvm::SmallVector<unsigned char, 64> Buffer;
  Buffer.resize(Name.size() + 1);
  Buffer[0] = ID;
  memcpy(Buffer.data() + 1, Name.data(), Name.size());

  Stream.EmitRecord(llvm::bitc::BLOCKINFO_CODE_SETRECORDNAME, Buffer);
}
} // namespace

void APINotesWriter::Implementation::writeBlockInfoBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII Scope(Stream, llvm::bitc::BLOCKINFO_BLOCK_ID, 2);

#define BLOCK(Block) emitBlockID(Stream, Block##_ID, #Block)
#define BLOCK_RECORD(NameSpace, Block)                                         \
  emitRecordID(Stream, NameSpace::Block, #Block)
  BLOCK(CONTROL_BLOCK);
  BLOCK_RECORD(control_block, METADATA);
  BLOCK_RECORD(control_block, MODULE_NAME);

  BLOCK(IDENTIFIER_BLOCK);
  BLOCK_RECORD(identifier_block, IDENTIFIER_DATA);

  BLOCK(OBJC_CONTEXT_BLOCK);
  BLOCK_RECORD(context_block, CONTEXT_ID_DATA);

  BLOCK(OBJC_PROPERTY_BLOCK);
  BLOCK_RECORD(objc_property_block, OBJC_PROPERTY_DATA);

  BLOCK(OBJC_METHOD_BLOCK);
  BLOCK_RECORD(objc_method_block, OBJC_METHOD_DATA);

  BLOCK(OBJC_SELECTOR_BLOCK);
  BLOCK_RECORD(objc_selector_block, OBJC_SELECTOR_DATA);

  BLOCK(GLOBAL_VARIABLE_BLOCK);
  BLOCK_RECORD(global_variable_block, GLOBAL_VARIABLE_DATA);

  BLOCK(GLOBAL_FUNCTION_BLOCK);
  BLOCK_RECORD(global_function_block, GLOBAL_FUNCTION_DATA);
#undef BLOCK_RECORD
#undef BLOCK
}

void APINotesWriter::Implementation::writeControlBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII Scope(Stream, CONTROL_BLOCK_ID, 3);

  control_block::MetadataLayout Metadata(Stream);
  Metadata.emit(Scratch, VERSION_MAJOR, VERSION_MINOR);

  control_block::ModuleNameLayout ModuleName(Stream);
  ModuleName.emit(Scratch, this->ModuleName);

  if (SourceFile) {
    control_block::SourceFileLayout SourceFile(Stream);
    SourceFile.emit(Scratch, this->SourceFile->getSize(),
                    this->SourceFile->getModificationTime());
  }
}

namespace {
/// Used to serialize the on-disk identifier table.
class IdentifierTableInfo {
public:
  using key_type = StringRef;
  using key_type_ref = key_type;
  using data_type = IdentifierID;
  using data_type_ref = const data_type &;
  using hash_value_type = uint32_t;
  using offset_type = unsigned;

  hash_value_type ComputeHash(key_type_ref Key) { return llvm::djbHash(Key); }

  std::pair<unsigned, unsigned>
  EmitKeyDataLength(raw_ostream &OS, key_type_ref Key, data_type_ref) {
    uint32_t KeyLength = Key.size();
    uint32_t DataLength = sizeof(uint32_t);

    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint16_t>(KeyLength);
    writer.write<uint16_t>(DataLength);
    return {KeyLength, DataLength};
  }

  void EmitKey(raw_ostream &OS, key_type_ref Key, unsigned) { OS << Key; }

  void EmitData(raw_ostream &OS, key_type_ref, data_type_ref Data, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint32_t>(Data);
  }
};
} // namespace

void APINotesWriter::Implementation::writeIdentifierBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII restoreBlock(Stream, IDENTIFIER_BLOCK_ID, 3);

  if (IdentifierIDs.empty())
    return;

  llvm::SmallString<4096> HashTableBlob;
  uint32_t Offset;
  {
    llvm::OnDiskChainedHashTableGenerator<IdentifierTableInfo> Generator;
    for (auto &II : IdentifierIDs)
      Generator.insert(II.first(), II.second);

    llvm::raw_svector_ostream BlobStream(HashTableBlob);
    // Make sure that no bucket is at offset 0
    llvm::support::endian::write<uint32_t>(BlobStream, 0,
                                           llvm::endianness::little);
    Offset = Generator.Emit(BlobStream);
  }

  identifier_block::IdentifierDataLayout IdentifierData(Stream);
  IdentifierData.emit(Scratch, Offset, HashTableBlob);
}

namespace {
/// Used to serialize the on-disk Objective-C context table.
class ContextIDTableInfo {
public:
  using key_type = ContextTableKey;
  using key_type_ref = key_type;
  using data_type = unsigned;
  using data_type_ref = const data_type &;
  using hash_value_type = size_t;
  using offset_type = unsigned;

  hash_value_type ComputeHash(key_type_ref Key) {
    return static_cast<size_t>(Key.hashValue());
  }

  std::pair<unsigned, unsigned> EmitKeyDataLength(raw_ostream &OS, key_type_ref,
                                                  data_type_ref) {
    uint32_t KeyLength = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);
    uint32_t DataLength = sizeof(uint32_t);

    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint16_t>(KeyLength);
    writer.write<uint16_t>(DataLength);
    return {KeyLength, DataLength};
  }

  void EmitKey(raw_ostream &OS, key_type_ref Key, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint32_t>(Key.parentContextID);
    writer.write<uint8_t>(Key.contextKind);
    writer.write<uint32_t>(Key.contextID);
  }

  void EmitData(raw_ostream &OS, key_type_ref, data_type_ref Data, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint32_t>(Data);
  }
};

/// Localized helper to make a type dependent, thwarting template argument
/// deduction.
template <typename T> struct MakeDependent { typedef T Type; };

/// Retrieve the serialized size of the given VersionTuple, for use in
/// on-disk hash tables.
unsigned getVersionTupleSize(const VersionTuple &VT) {
  unsigned size = sizeof(uint8_t) + /*major*/ sizeof(uint32_t);
  if (VT.getMinor())
    size += sizeof(uint32_t);
  if (VT.getSubminor())
    size += sizeof(uint32_t);
  if (VT.getBuild())
    size += sizeof(uint32_t);
  return size;
}

/// Determine the size of an array of versioned information,
template <typename T>
unsigned getVersionedInfoSize(
    const llvm::SmallVectorImpl<std::pair<llvm::VersionTuple, T>> &VI,
    llvm::function_ref<unsigned(const typename MakeDependent<T>::Type &)>
        getInfoSize) {
  unsigned result = sizeof(uint16_t); // # of elements
  for (const auto &E : VI) {
    result += getVersionTupleSize(E.first);
    result += getInfoSize(E.second);
  }
  return result;
}

/// Emit a serialized representation of a version tuple.
void emitVersionTuple(raw_ostream &OS, const VersionTuple &VT) {
  llvm::support::endian::Writer writer(OS, llvm::endianness::little);

  // First byte contains the number of components beyond the 'major' component.
  uint8_t descriptor;
  if (VT.getBuild())
    descriptor = 3;
  else if (VT.getSubminor())
    descriptor = 2;
  else if (VT.getMinor())
    descriptor = 1;
  else
    descriptor = 0;
  writer.write<uint8_t>(descriptor);

  // Write the components.
  writer.write<uint32_t>(VT.getMajor());
  if (auto minor = VT.getMinor())
    writer.write<uint32_t>(*minor);
  if (auto subminor = VT.getSubminor())
    writer.write<uint32_t>(*subminor);
  if (auto build = VT.getBuild())
    writer.write<uint32_t>(*build);
}

/// Emit versioned information.
template <typename T>
void emitVersionedInfo(
    raw_ostream &OS, llvm::SmallVectorImpl<std::pair<VersionTuple, T>> &VI,
    llvm::function_ref<void(raw_ostream &,
                            const typename MakeDependent<T>::Type &)>
        emitInfo) {
  std::sort(VI.begin(), VI.end(),
            [](const std::pair<VersionTuple, T> &LHS,
               const std::pair<VersionTuple, T> &RHS) -> bool {
              assert((&LHS == &RHS || LHS.first != RHS.first) &&
                     "two entries for the same version");
              return LHS.first < RHS.first;
            });

  llvm::support::endian::Writer writer(OS, llvm::endianness::little);
  writer.write<uint16_t>(VI.size());
  for (const auto &E : VI) {
    emitVersionTuple(OS, E.first);
    emitInfo(OS, E.second);
  }
}

/// On-disk hash table info key base for handling versioned data.
template <typename Derived, typename KeyType, typename UnversionedDataType>
class VersionedTableInfo {
  Derived &asDerived() { return *static_cast<Derived *>(this); }

  const Derived &asDerived() const {
    return *static_cast<const Derived *>(this);
  }

public:
  using key_type = KeyType;
  using key_type_ref = key_type;
  using data_type =
      llvm::SmallVector<std::pair<llvm::VersionTuple, UnversionedDataType>, 1>;
  using data_type_ref = data_type &;
  using hash_value_type = size_t;
  using offset_type = unsigned;

  std::pair<unsigned, unsigned>
  EmitKeyDataLength(raw_ostream &OS, key_type_ref Key, data_type_ref Data) {
    uint32_t KeyLength = asDerived().getKeyLength(Key);
    uint32_t DataLength =
        getVersionedInfoSize(Data, [this](const UnversionedDataType &UI) {
          return asDerived().getUnversionedInfoSize(UI);
        });

    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint16_t>(KeyLength);
    writer.write<uint16_t>(DataLength);
    return {KeyLength, DataLength};
  }

  void EmitData(raw_ostream &OS, key_type_ref, data_type_ref Data, unsigned) {
    emitVersionedInfo(
        OS, Data, [this](llvm::raw_ostream &OS, const UnversionedDataType &UI) {
          asDerived().emitUnversionedInfo(OS, UI);
        });
  }
};

/// Emit a serialized representation of the common entity information.
void emitCommonEntityInfo(raw_ostream &OS, const CommonEntityInfo &CEI) {
  llvm::support::endian::Writer writer(OS, llvm::endianness::little);

  uint8_t payload = 0;
  if (auto swiftPrivate = CEI.isSwiftPrivate()) {
    payload |= 0x01;
    if (*swiftPrivate)
      payload |= 0x02;
  }
  payload <<= 1;
  payload |= CEI.Unavailable;
  payload <<= 1;
  payload |= CEI.UnavailableInSwift;

  writer.write<uint8_t>(payload);

  writer.write<uint16_t>(CEI.UnavailableMsg.size());
  OS.write(CEI.UnavailableMsg.c_str(), CEI.UnavailableMsg.size());

  writer.write<uint16_t>(CEI.SwiftName.size());
  OS.write(CEI.SwiftName.c_str(), CEI.SwiftName.size());
}

/// Retrieve the serialized size of the given CommonEntityInfo, for use in
/// on-disk hash tables.
unsigned getCommonEntityInfoSize(const CommonEntityInfo &CEI) {
  return 5 + CEI.UnavailableMsg.size() + CEI.SwiftName.size();
}

// Retrieve the serialized size of the given CommonTypeInfo, for use
// in on-disk hash tables.
unsigned getCommonTypeInfoSize(const CommonTypeInfo &CTI) {
  return 2 + (CTI.getSwiftBridge() ? CTI.getSwiftBridge()->size() : 0) + 2 +
         (CTI.getNSErrorDomain() ? CTI.getNSErrorDomain()->size() : 0) +
         getCommonEntityInfoSize(CTI);
}

/// Emit a serialized representation of the common type information.
void emitCommonTypeInfo(raw_ostream &OS, const CommonTypeInfo &CTI) {
  emitCommonEntityInfo(OS, CTI);

  llvm::support::endian::Writer writer(OS, llvm::endianness::little);
  if (auto swiftBridge = CTI.getSwiftBridge()) {
    writer.write<uint16_t>(swiftBridge->size() + 1);
    OS.write(swiftBridge->c_str(), swiftBridge->size());
  } else {
    writer.write<uint16_t>(0);
  }
  if (auto nsErrorDomain = CTI.getNSErrorDomain()) {
    writer.write<uint16_t>(nsErrorDomain->size() + 1);
    OS.write(nsErrorDomain->c_str(), CTI.getNSErrorDomain()->size());
  } else {
    writer.write<uint16_t>(0);
  }
}

/// Used to serialize the on-disk Objective-C property table.
class ContextInfoTableInfo
    : public VersionedTableInfo<ContextInfoTableInfo, unsigned, ContextInfo> {
public:
  unsigned getKeyLength(key_type_ref) { return sizeof(uint32_t); }

  void EmitKey(raw_ostream &OS, key_type_ref Key, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint32_t>(Key);
  }

  hash_value_type ComputeHash(key_type_ref Key) {
    return static_cast<size_t>(llvm::hash_value(Key));
  }

  unsigned getUnversionedInfoSize(const ContextInfo &OCI) {
    return getCommonTypeInfoSize(OCI) + 1;
  }

  void emitUnversionedInfo(raw_ostream &OS, const ContextInfo &OCI) {
    emitCommonTypeInfo(OS, OCI);

    uint8_t payload = 0;
    if (auto swiftImportAsNonGeneric = OCI.getSwiftImportAsNonGeneric())
      payload |= (0x01 << 1) | (uint8_t)swiftImportAsNonGeneric.value();
    payload <<= 2;
    if (auto swiftObjCMembers = OCI.getSwiftObjCMembers())
      payload |= (0x01 << 1) | (uint8_t)swiftObjCMembers.value();
    payload <<= 3;
    if (auto nullable = OCI.getDefaultNullability())
      payload |= (0x01 << 2) | static_cast<uint8_t>(*nullable);
    payload = (payload << 1) | (OCI.hasDesignatedInits() ? 1 : 0);

    OS << payload;
  }
};
} // namespace

void APINotesWriter::Implementation::writeContextBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII restoreBlock(Stream, OBJC_CONTEXT_BLOCK_ID, 3);

  if (Contexts.empty())
    return;

  {
    llvm::SmallString<4096> HashTableBlob;
    uint32_t Offset;
    {
      llvm::OnDiskChainedHashTableGenerator<ContextIDTableInfo> Generator;
      for (auto &OC : Contexts)
        Generator.insert(OC.first, OC.second.first);

      llvm::raw_svector_ostream BlobStream(HashTableBlob);
      // Make sure that no bucket is at offset 0
      llvm::support::endian::write<uint32_t>(BlobStream, 0,
                                             llvm::endianness::little);
      Offset = Generator.Emit(BlobStream);
    }

    context_block::ContextIDLayout ContextID(Stream);
    ContextID.emit(Scratch, Offset, HashTableBlob);
  }

  {
    llvm::SmallString<4096> HashTableBlob;
    uint32_t Offset;
    {
      llvm::OnDiskChainedHashTableGenerator<ContextInfoTableInfo> Generator;
      for (auto &OC : Contexts)
        Generator.insert(OC.second.first, OC.second.second);

      llvm::raw_svector_ostream BlobStream(HashTableBlob);
      // Make sure that no bucket is at offset 0
      llvm::support::endian::write<uint32_t>(BlobStream, 0,
                                             llvm::endianness::little);
      Offset = Generator.Emit(BlobStream);
    }

    context_block::ContextInfoLayout ContextInfo(Stream);
    ContextInfo.emit(Scratch, Offset, HashTableBlob);
  }
}

namespace {
/// Retrieve the serialized size of the given VariableInfo, for use in
/// on-disk hash tables.
unsigned getVariableInfoSize(const VariableInfo &VI) {
  return 2 + getCommonEntityInfoSize(VI) + 2 + VI.getType().size();
}

/// Emit a serialized representation of the variable information.
void emitVariableInfo(raw_ostream &OS, const VariableInfo &VI) {
  emitCommonEntityInfo(OS, VI);

  uint8_t bytes[2] = {0, 0};
  if (auto nullable = VI.getNullability()) {
    bytes[0] = 1;
    bytes[1] = static_cast<uint8_t>(*nullable);
  } else {
    // Nothing to do.
  }

  OS.write(reinterpret_cast<const char *>(bytes), 2);

  llvm::support::endian::Writer writer(OS, llvm::endianness::little);
  writer.write<uint16_t>(VI.getType().size());
  OS.write(VI.getType().data(), VI.getType().size());
}

/// Used to serialize the on-disk Objective-C property table.
class ObjCPropertyTableInfo
    : public VersionedTableInfo<ObjCPropertyTableInfo,
                                std::tuple<unsigned, unsigned, char>,
                                ObjCPropertyInfo> {
public:
  unsigned getKeyLength(key_type_ref) {
    return sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t);
  }

  void EmitKey(raw_ostream &OS, key_type_ref Key, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint32_t>(std::get<0>(Key));
    writer.write<uint32_t>(std::get<1>(Key));
    writer.write<uint8_t>(std::get<2>(Key));
  }

  hash_value_type ComputeHash(key_type_ref Key) {
    return static_cast<size_t>(llvm::hash_value(Key));
  }

  unsigned getUnversionedInfoSize(const ObjCPropertyInfo &OPI) {
    return getVariableInfoSize(OPI) + 1;
  }

  void emitUnversionedInfo(raw_ostream &OS, const ObjCPropertyInfo &OPI) {
    emitVariableInfo(OS, OPI);

    uint8_t flags = 0;
    if (auto value = OPI.getSwiftImportAsAccessors()) {
      flags |= 1 << 0;
      flags |= value.value() << 1;
    }
    OS << flags;
  }
};
} // namespace

void APINotesWriter::Implementation::writeObjCPropertyBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII Scope(Stream, OBJC_PROPERTY_BLOCK_ID, 3);

  if (ObjCProperties.empty())
    return;

  {
    llvm::SmallString<4096> HashTableBlob;
    uint32_t Offset;
    {
      llvm::OnDiskChainedHashTableGenerator<ObjCPropertyTableInfo> Generator;
      for (auto &OP : ObjCProperties)
        Generator.insert(OP.first, OP.second);

      llvm::raw_svector_ostream BlobStream(HashTableBlob);
      // Make sure that no bucket is at offset 0
      llvm::support::endian::write<uint32_t>(BlobStream, 0,
                                             llvm::endianness::little);
      Offset = Generator.Emit(BlobStream);
    }

    objc_property_block::ObjCPropertyDataLayout ObjCPropertyData(Stream);
    ObjCPropertyData.emit(Scratch, Offset, HashTableBlob);
  }
}

namespace {
unsigned getFunctionInfoSize(const FunctionInfo &);
void emitFunctionInfo(llvm::raw_ostream &, const FunctionInfo &);

/// Used to serialize the on-disk Objective-C method table.
class ObjCMethodTableInfo
    : public VersionedTableInfo<ObjCMethodTableInfo,
                                std::tuple<unsigned, unsigned, char>,
                                ObjCMethodInfo> {
public:
  unsigned getKeyLength(key_type_ref) {
    return sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t);
  }

  void EmitKey(raw_ostream &OS, key_type_ref Key, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint32_t>(std::get<0>(Key));
    writer.write<uint32_t>(std::get<1>(Key));
    writer.write<uint8_t>(std::get<2>(Key));
  }

  hash_value_type ComputeHash(key_type_ref key) {
    return static_cast<size_t>(llvm::hash_value(key));
  }

  unsigned getUnversionedInfoSize(const ObjCMethodInfo &OMI) {
    return getFunctionInfoSize(OMI) + 1;
  }

  void emitUnversionedInfo(raw_ostream &OS, const ObjCMethodInfo &OMI) {
    uint8_t flags = 0;
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    flags = (flags << 1) | OMI.DesignatedInit;
    flags = (flags << 1) | OMI.RequiredInit;
    writer.write<uint8_t>(flags);

    emitFunctionInfo(OS, OMI);
  }
};

/// Used to serialize the on-disk C++ method table.
class CXXMethodTableInfo
    : public VersionedTableInfo<CXXMethodTableInfo, SingleDeclTableKey,
                                CXXMethodInfo> {
public:
  unsigned getKeyLength(key_type_ref) {
    return sizeof(uint32_t) + sizeof(uint32_t);
  }

  void EmitKey(raw_ostream &OS, key_type_ref Key, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint32_t>(Key.parentContextID);
    writer.write<uint32_t>(Key.nameID);
  }

  hash_value_type ComputeHash(key_type_ref key) {
    return static_cast<size_t>(key.hashValue());
  }

  unsigned getUnversionedInfoSize(const CXXMethodInfo &OMI) {
    return getFunctionInfoSize(OMI);
  }

  void emitUnversionedInfo(raw_ostream &OS, const CXXMethodInfo &OMI) {
    emitFunctionInfo(OS, OMI);
  }
};
} // namespace

void APINotesWriter::Implementation::writeObjCMethodBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII Scope(Stream, OBJC_METHOD_BLOCK_ID, 3);

  if (ObjCMethods.empty())
    return;

  {
    llvm::SmallString<4096> HashTableBlob;
    uint32_t Offset;
    {
      llvm::OnDiskChainedHashTableGenerator<ObjCMethodTableInfo> Generator;
      for (auto &OM : ObjCMethods)
        Generator.insert(OM.first, OM.second);

      llvm::raw_svector_ostream BlobStream(HashTableBlob);
      // Make sure that no bucket is at offset 0
      llvm::support::endian::write<uint32_t>(BlobStream, 0,
                                             llvm::endianness::little);
      Offset = Generator.Emit(BlobStream);
    }

    objc_method_block::ObjCMethodDataLayout ObjCMethodData(Stream);
    ObjCMethodData.emit(Scratch, Offset, HashTableBlob);
  }
}

void APINotesWriter::Implementation::writeCXXMethodBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII Scope(Stream, CXX_METHOD_BLOCK_ID, 3);

  if (CXXMethods.empty())
    return;

  {
    llvm::SmallString<4096> HashTableBlob;
    uint32_t Offset;
    {
      llvm::OnDiskChainedHashTableGenerator<CXXMethodTableInfo> Generator;
      for (auto &MD : CXXMethods)
        Generator.insert(MD.first, MD.second);

      llvm::raw_svector_ostream BlobStream(HashTableBlob);
      // Make sure that no bucket is at offset 0
      llvm::support::endian::write<uint32_t>(BlobStream, 0,
                                             llvm::endianness::little);
      Offset = Generator.Emit(BlobStream);
    }

    cxx_method_block::CXXMethodDataLayout CXXMethodData(Stream);
    CXXMethodData.emit(Scratch, Offset, HashTableBlob);
  }
}

namespace {
/// Used to serialize the on-disk Objective-C selector table.
class ObjCSelectorTableInfo {
public:
  using key_type = StoredObjCSelector;
  using key_type_ref = const key_type &;
  using data_type = SelectorID;
  using data_type_ref = data_type;
  using hash_value_type = unsigned;
  using offset_type = unsigned;

  hash_value_type ComputeHash(key_type_ref Key) {
    return llvm::DenseMapInfo<StoredObjCSelector>::getHashValue(Key);
  }

  std::pair<unsigned, unsigned>
  EmitKeyDataLength(raw_ostream &OS, key_type_ref Key, data_type_ref) {
    uint32_t KeyLength =
        sizeof(uint16_t) + sizeof(uint32_t) * Key.Identifiers.size();
    uint32_t DataLength = sizeof(uint32_t);

    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint16_t>(KeyLength);
    writer.write<uint16_t>(DataLength);
    return {KeyLength, DataLength};
  }

  void EmitKey(raw_ostream &OS, key_type_ref Key, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint16_t>(Key.NumArgs);
    for (auto Identifier : Key.Identifiers)
      writer.write<uint32_t>(Identifier);
  }

  void EmitData(raw_ostream &OS, key_type_ref, data_type_ref Data, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint32_t>(Data);
  }
};
} // namespace

void APINotesWriter::Implementation::writeObjCSelectorBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII Scope(Stream, OBJC_SELECTOR_BLOCK_ID, 3);

  if (SelectorIDs.empty())
    return;

  {
    llvm::SmallString<4096> HashTableBlob;
    uint32_t Offset;
    {
      llvm::OnDiskChainedHashTableGenerator<ObjCSelectorTableInfo> Generator;
      for (auto &S : SelectorIDs)
        Generator.insert(S.first, S.second);

      llvm::raw_svector_ostream BlobStream(HashTableBlob);
      // Make sure that no bucket is at offset 0
      llvm::support::endian::write<uint32_t>(BlobStream, 0,
                                             llvm::endianness::little);
      Offset = Generator.Emit(BlobStream);
    }

    objc_selector_block::ObjCSelectorDataLayout ObjCSelectorData(Stream);
    ObjCSelectorData.emit(Scratch, Offset, HashTableBlob);
  }
}

namespace {
/// Used to serialize the on-disk global variable table.
class GlobalVariableTableInfo
    : public VersionedTableInfo<GlobalVariableTableInfo, SingleDeclTableKey,
                                GlobalVariableInfo> {
public:
  unsigned getKeyLength(key_type_ref) {
    return sizeof(uint32_t) + sizeof(uint32_t);
  }

  void EmitKey(raw_ostream &OS, key_type_ref Key, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint32_t>(Key.parentContextID);
    writer.write<uint32_t>(Key.nameID);
  }

  hash_value_type ComputeHash(key_type_ref Key) {
    return static_cast<size_t>(Key.hashValue());
  }

  unsigned getUnversionedInfoSize(const GlobalVariableInfo &GVI) {
    return getVariableInfoSize(GVI);
  }

  void emitUnversionedInfo(raw_ostream &OS, const GlobalVariableInfo &GVI) {
    emitVariableInfo(OS, GVI);
  }
};
} // namespace

void APINotesWriter::Implementation::writeGlobalVariableBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII Scope(Stream, GLOBAL_VARIABLE_BLOCK_ID, 3);

  if (GlobalVariables.empty())
    return;

  {
    llvm::SmallString<4096> HashTableBlob;
    uint32_t Offset;
    {
      llvm::OnDiskChainedHashTableGenerator<GlobalVariableTableInfo> Generator;
      for (auto &GV : GlobalVariables)
        Generator.insert(GV.first, GV.second);

      llvm::raw_svector_ostream BlobStream(HashTableBlob);
      // Make sure that no bucket is at offset 0
      llvm::support::endian::write<uint32_t>(BlobStream, 0,
                                             llvm::endianness::little);
      Offset = Generator.Emit(BlobStream);
    }

    global_variable_block::GlobalVariableDataLayout GlobalVariableData(Stream);
    GlobalVariableData.emit(Scratch, Offset, HashTableBlob);
  }
}

namespace {
unsigned getParamInfoSize(const ParamInfo &PI) {
  return getVariableInfoSize(PI) + 1;
}

void emitParamInfo(raw_ostream &OS, const ParamInfo &PI) {
  emitVariableInfo(OS, PI);

  uint8_t flags = 0;
  if (auto noescape = PI.isNoEscape()) {
    flags |= 0x01;
    if (*noescape)
      flags |= 0x02;
  }
  flags <<= 3;
  if (auto RCC = PI.getRetainCountConvention())
    flags |= static_cast<uint8_t>(RCC.value()) + 1;

  llvm::support::endian::Writer writer(OS, llvm::endianness::little);
  writer.write<uint8_t>(flags);
}

/// Retrieve the serialized size of the given FunctionInfo, for use in on-disk
/// hash tables.
unsigned getFunctionInfoSize(const FunctionInfo &FI) {
  unsigned size = getCommonEntityInfoSize(FI) + 2 + sizeof(uint64_t);
  size += sizeof(uint16_t);
  for (const auto &P : FI.Params)
    size += getParamInfoSize(P);
  size += sizeof(uint16_t) + FI.ResultType.size();
  return size;
}

/// Emit a serialized representation of the function information.
void emitFunctionInfo(raw_ostream &OS, const FunctionInfo &FI) {
  emitCommonEntityInfo(OS, FI);

  uint8_t flags = 0;
  flags |= FI.NullabilityAudited;
  flags <<= 3;
  if (auto RCC = FI.getRetainCountConvention())
    flags |= static_cast<uint8_t>(RCC.value()) + 1;

  llvm::support::endian::Writer writer(OS, llvm::endianness::little);

  writer.write<uint8_t>(flags);
  writer.write<uint8_t>(FI.NumAdjustedNullable);
  writer.write<uint64_t>(FI.NullabilityPayload);

  writer.write<uint16_t>(FI.Params.size());
  for (const auto &PI : FI.Params)
    emitParamInfo(OS, PI);

  writer.write<uint16_t>(FI.ResultType.size());
  writer.write(ArrayRef<char>{FI.ResultType.data(), FI.ResultType.size()});
}

/// Used to serialize the on-disk global function table.
class GlobalFunctionTableInfo
    : public VersionedTableInfo<GlobalFunctionTableInfo, SingleDeclTableKey,
                                GlobalFunctionInfo> {
public:
  unsigned getKeyLength(key_type_ref) {
    return sizeof(uint32_t) + sizeof(uint32_t);
  }

  void EmitKey(raw_ostream &OS, key_type_ref Key, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint32_t>(Key.parentContextID);
    writer.write<uint32_t>(Key.nameID);
  }

  hash_value_type ComputeHash(key_type_ref Key) {
    return static_cast<size_t>(Key.hashValue());
  }

  unsigned getUnversionedInfoSize(const GlobalFunctionInfo &GFI) {
    return getFunctionInfoSize(GFI);
  }

  void emitUnversionedInfo(raw_ostream &OS, const GlobalFunctionInfo &GFI) {
    emitFunctionInfo(OS, GFI);
  }
};
} // namespace

void APINotesWriter::Implementation::writeGlobalFunctionBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII Scope(Stream, GLOBAL_FUNCTION_BLOCK_ID, 3);

  if (GlobalFunctions.empty())
    return;

  {
    llvm::SmallString<4096> HashTableBlob;
    uint32_t Offset;
    {
      llvm::OnDiskChainedHashTableGenerator<GlobalFunctionTableInfo> Generator;
      for (auto &F : GlobalFunctions)
        Generator.insert(F.first, F.second);

      llvm::raw_svector_ostream BlobStream(HashTableBlob);
      // Make sure that no bucket is at offset 0
      llvm::support::endian::write<uint32_t>(BlobStream, 0,
                                             llvm::endianness::little);
      Offset = Generator.Emit(BlobStream);
    }

    global_function_block::GlobalFunctionDataLayout GlobalFunctionData(Stream);
    GlobalFunctionData.emit(Scratch, Offset, HashTableBlob);
  }
}

namespace {
/// Used to serialize the on-disk global enum constant.
class EnumConstantTableInfo
    : public VersionedTableInfo<EnumConstantTableInfo, unsigned,
                                EnumConstantInfo> {
public:
  unsigned getKeyLength(key_type_ref) { return sizeof(uint32_t); }

  void EmitKey(raw_ostream &OS, key_type_ref Key, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint32_t>(Key);
  }

  hash_value_type ComputeHash(key_type_ref Key) {
    return static_cast<size_t>(llvm::hash_value(Key));
  }

  unsigned getUnversionedInfoSize(const EnumConstantInfo &ECI) {
    return getCommonEntityInfoSize(ECI);
  }

  void emitUnversionedInfo(raw_ostream &OS, const EnumConstantInfo &ECI) {
    emitCommonEntityInfo(OS, ECI);
  }
};
} // namespace

void APINotesWriter::Implementation::writeEnumConstantBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII Scope(Stream, ENUM_CONSTANT_BLOCK_ID, 3);

  if (EnumConstants.empty())
    return;

  {
    llvm::SmallString<4096> HashTableBlob;
    uint32_t Offset;
    {
      llvm::OnDiskChainedHashTableGenerator<EnumConstantTableInfo> Generator;
      for (auto &EC : EnumConstants)
        Generator.insert(EC.first, EC.second);

      llvm::raw_svector_ostream BlobStream(HashTableBlob);
      // Make sure that no bucket is at offset 0
      llvm::support::endian::write<uint32_t>(BlobStream, 0,
                                             llvm::endianness::little);
      Offset = Generator.Emit(BlobStream);
    }

    enum_constant_block::EnumConstantDataLayout EnumConstantData(Stream);
    EnumConstantData.emit(Scratch, Offset, HashTableBlob);
  }
}

namespace {
template <typename Derived, typename UnversionedDataType>
class CommonTypeTableInfo
    : public VersionedTableInfo<Derived, SingleDeclTableKey,
                                UnversionedDataType> {
public:
  using key_type_ref = typename CommonTypeTableInfo::key_type_ref;
  using hash_value_type = typename CommonTypeTableInfo::hash_value_type;

  unsigned getKeyLength(key_type_ref) {
    return sizeof(uint32_t) + sizeof(IdentifierID);
  }

  void EmitKey(raw_ostream &OS, key_type_ref Key, unsigned) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);
    writer.write<uint32_t>(Key.parentContextID);
    writer.write<IdentifierID>(Key.nameID);
  }

  hash_value_type ComputeHash(key_type_ref Key) {
    return static_cast<size_t>(Key.hashValue());
  }

  unsigned getUnversionedInfoSize(const UnversionedDataType &UDT) {
    return getCommonTypeInfoSize(UDT);
  }

  void emitUnversionedInfo(raw_ostream &OS, const UnversionedDataType &UDT) {
    emitCommonTypeInfo(OS, UDT);
  }
};

/// Used to serialize the on-disk tag table.
class TagTableInfo : public CommonTypeTableInfo<TagTableInfo, TagInfo> {
public:
  unsigned getUnversionedInfoSize(const TagInfo &TI) {
    return 2 + (TI.SwiftImportAs ? TI.SwiftImportAs->size() : 0) +
           2 + (TI.SwiftRetainOp ? TI.SwiftRetainOp->size() : 0) +
           2 + (TI.SwiftReleaseOp ? TI.SwiftReleaseOp->size() : 0) +
           2 + getCommonTypeInfoSize(TI);
  }

  void emitUnversionedInfo(raw_ostream &OS, const TagInfo &TI) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);

    uint8_t Flags = 0;
    if (auto extensibility = TI.EnumExtensibility) {
      Flags |= static_cast<uint8_t>(extensibility.value()) + 1;
      assert((Flags < (1 << 2)) && "must fit in two bits");
    }

    Flags <<= 2;
    if (auto value = TI.isFlagEnum())
      Flags |= (value.value() << 1 | 1 << 0);

    writer.write<uint8_t>(Flags);

    if (auto Copyable = TI.isSwiftCopyable())
      writer.write<uint8_t>(*Copyable ? kSwiftCopyable : kSwiftNonCopyable);
    else
      writer.write<uint8_t>(0);

    if (auto ImportAs = TI.SwiftImportAs) {
      writer.write<uint16_t>(ImportAs->size() + 1);
      OS.write(ImportAs->c_str(), ImportAs->size());
    } else {
      writer.write<uint16_t>(0);
    }
    if (auto RetainOp = TI.SwiftRetainOp) {
      writer.write<uint16_t>(RetainOp->size() + 1);
      OS.write(RetainOp->c_str(), RetainOp->size());
    } else {
      writer.write<uint16_t>(0);
    }
    if (auto ReleaseOp = TI.SwiftReleaseOp) {
      writer.write<uint16_t>(ReleaseOp->size() + 1);
      OS.write(ReleaseOp->c_str(), ReleaseOp->size());
    } else {
      writer.write<uint16_t>(0);
    }

    emitCommonTypeInfo(OS, TI);
  }
};
} // namespace

void APINotesWriter::Implementation::writeTagBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII Scope(Stream, TAG_BLOCK_ID, 3);

  if (Tags.empty())
    return;

  {
    llvm::SmallString<4096> HashTableBlob;
    uint32_t Offset;
    {
      llvm::OnDiskChainedHashTableGenerator<TagTableInfo> Generator;
      for (auto &T : Tags)
        Generator.insert(T.first, T.second);

      llvm::raw_svector_ostream BlobStream(HashTableBlob);
      // Make sure that no bucket is at offset 0
      llvm::support::endian::write<uint32_t>(BlobStream, 0,
                                             llvm::endianness::little);
      Offset = Generator.Emit(BlobStream);
    }

    tag_block::TagDataLayout TagData(Stream);
    TagData.emit(Scratch, Offset, HashTableBlob);
  }
}

namespace {
/// Used to serialize the on-disk typedef table.
class TypedefTableInfo
    : public CommonTypeTableInfo<TypedefTableInfo, TypedefInfo> {
public:
  unsigned getUnversionedInfoSize(const TypedefInfo &TI) {
    return 1 + getCommonTypeInfoSize(TI);
  }

  void emitUnversionedInfo(raw_ostream &OS, const TypedefInfo &TI) {
    llvm::support::endian::Writer writer(OS, llvm::endianness::little);

    uint8_t Flags = 0;
    if (auto swiftWrapper = TI.SwiftWrapper)
      Flags |= static_cast<uint8_t>(*swiftWrapper) + 1;

    writer.write<uint8_t>(Flags);

    emitCommonTypeInfo(OS, TI);
  }
};
} // namespace

void APINotesWriter::Implementation::writeTypedefBlock(
    llvm::BitstreamWriter &Stream) {
  llvm::BCBlockRAII Scope(Stream, TYPEDEF_BLOCK_ID, 3);

  if (Typedefs.empty())
    return;

  {
    llvm::SmallString<4096> HashTableBlob;
    uint32_t Offset;
    {
      llvm::OnDiskChainedHashTableGenerator<TypedefTableInfo> Generator;
      for (auto &T : Typedefs)
        Generator.insert(T.first, T.second);

      llvm::raw_svector_ostream BlobStream(HashTableBlob);
      // Make sure that no bucket is at offset 0
      llvm::support::endian::write<uint32_t>(BlobStream, 0,
                                             llvm::endianness::little);
      Offset = Generator.Emit(BlobStream);
    }

    typedef_block::TypedefDataLayout TypedefData(Stream);
    TypedefData.emit(Scratch, Offset, HashTableBlob);
  }
}

// APINotesWriter

APINotesWriter::APINotesWriter(llvm::StringRef ModuleName, const FileEntry *SF)
    : Implementation(new class Implementation(ModuleName, SF)) {}

APINotesWriter::~APINotesWriter() = default;

void APINotesWriter::writeToStream(llvm::raw_ostream &OS) {
  Implementation->writeToStream(OS);
}

ContextID APINotesWriter::addContext(std::optional<ContextID> ParentCtxID,
                                     llvm::StringRef Name, ContextKind Kind,
                                     const ContextInfo &Info,
                                     llvm::VersionTuple SwiftVersion) {
  IdentifierID NameID = Implementation->getIdentifier(Name);

  uint32_t RawParentCtxID = ParentCtxID ? ParentCtxID->Value : -1;
  ContextTableKey Key(RawParentCtxID, static_cast<uint8_t>(Kind), NameID);
  auto Known = Implementation->Contexts.find(Key);
  if (Known == Implementation->Contexts.end()) {
    unsigned NextID = Implementation->Contexts.size() + 1;

    Implementation::VersionedSmallVector<ContextInfo> EmptyVersionedInfo;
    Known = Implementation->Contexts
                .insert(std::make_pair(
                    Key, std::make_pair(NextID, EmptyVersionedInfo)))
                .first;

    Implementation->ContextNames[NextID] = NameID;
    Implementation->ParentContexts[NextID] = RawParentCtxID;
  }

  // Add this version information.
  auto &VersionedVec = Known->second.second;
  bool Found = false;
  for (auto &Versioned : VersionedVec) {
    if (Versioned.first == SwiftVersion) {
      Versioned.second |= Info;
      Found = true;
      break;
    }
  }

  if (!Found)
    VersionedVec.push_back({SwiftVersion, Info});

  return ContextID(Known->second.first);
}

void APINotesWriter::addObjCProperty(ContextID CtxID, StringRef Name,
                                     bool IsInstanceProperty,
                                     const ObjCPropertyInfo &Info,
                                     VersionTuple SwiftVersion) {
  IdentifierID NameID = Implementation->getIdentifier(Name);
  Implementation
      ->ObjCProperties[std::make_tuple(CtxID.Value, NameID, IsInstanceProperty)]
      .push_back({SwiftVersion, Info});
}

void APINotesWriter::addObjCMethod(ContextID CtxID, ObjCSelectorRef Selector,
                                   bool IsInstanceMethod,
                                   const ObjCMethodInfo &Info,
                                   VersionTuple SwiftVersion) {
  SelectorID SelID = Implementation->getSelector(Selector);
  auto Key = std::tuple<unsigned, unsigned, char>{CtxID.Value, SelID,
                                                  IsInstanceMethod};
  Implementation->ObjCMethods[Key].push_back({SwiftVersion, Info});

  // If this method is a designated initializer, update the class to note that
  // it has designated initializers.
  if (Info.DesignatedInit) {
    assert(Implementation->ParentContexts.contains(CtxID.Value));
    uint32_t ParentCtxID = Implementation->ParentContexts[CtxID.Value];
    ContextTableKey CtxKey(ParentCtxID,
                           static_cast<uint8_t>(ContextKind::ObjCClass),
                           Implementation->ContextNames[CtxID.Value]);
    assert(Implementation->Contexts.contains(CtxKey));
    auto &VersionedVec = Implementation->Contexts[CtxKey].second;
    bool Found = false;
    for (auto &Versioned : VersionedVec) {
      if (Versioned.first == SwiftVersion) {
        Versioned.second.setHasDesignatedInits(true);
        Found = true;
        break;
      }
    }

    if (!Found) {
      VersionedVec.push_back({SwiftVersion, ContextInfo()});
      VersionedVec.back().second.setHasDesignatedInits(true);
    }
  }
}

void APINotesWriter::addCXXMethod(ContextID CtxID, llvm::StringRef Name,
                                  const CXXMethodInfo &Info,
                                  VersionTuple SwiftVersion) {
  IdentifierID NameID = Implementation->getIdentifier(Name);
  SingleDeclTableKey Key(CtxID.Value, NameID);
  Implementation->CXXMethods[Key].push_back({SwiftVersion, Info});
}

void APINotesWriter::addGlobalVariable(std::optional<Context> Ctx,
                                       llvm::StringRef Name,
                                       const GlobalVariableInfo &Info,
                                       VersionTuple SwiftVersion) {
  IdentifierID VariableID = Implementation->getIdentifier(Name);
  SingleDeclTableKey Key(Ctx, VariableID);
  Implementation->GlobalVariables[Key].push_back({SwiftVersion, Info});
}

void APINotesWriter::addGlobalFunction(std::optional<Context> Ctx,
                                       llvm::StringRef Name,
                                       const GlobalFunctionInfo &Info,
                                       VersionTuple SwiftVersion) {
  IdentifierID NameID = Implementation->getIdentifier(Name);
  SingleDeclTableKey Key(Ctx, NameID);
  Implementation->GlobalFunctions[Key].push_back({SwiftVersion, Info});
}

void APINotesWriter::addEnumConstant(llvm::StringRef Name,
                                     const EnumConstantInfo &Info,
                                     VersionTuple SwiftVersion) {
  IdentifierID EnumConstantID = Implementation->getIdentifier(Name);
  Implementation->EnumConstants[EnumConstantID].push_back({SwiftVersion, Info});
}

void APINotesWriter::addTag(std::optional<Context> Ctx, llvm::StringRef Name,
                            const TagInfo &Info, VersionTuple SwiftVersion) {
  IdentifierID TagID = Implementation->getIdentifier(Name);
  SingleDeclTableKey Key(Ctx, TagID);
  Implementation->Tags[Key].push_back({SwiftVersion, Info});
}

void APINotesWriter::addTypedef(std::optional<Context> Ctx,
                                llvm::StringRef Name, const TypedefInfo &Info,
                                VersionTuple SwiftVersion) {
  IdentifierID TypedefID = Implementation->getIdentifier(Name);
  SingleDeclTableKey Key(Ctx, TypedefID);
  Implementation->Typedefs[Key].push_back({SwiftVersion, Info});
}
} // namespace api_notes
} // namespace clang

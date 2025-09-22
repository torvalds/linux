//===- TypeRecordMapping.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/TypeRecordMapping.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/CodeViewRecordIO.h"
#include "llvm/DebugInfo/CodeView/EnumTables.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/ScopedPrinter.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::codeview;

namespace {

#define error(X)                                                               \
  do {                                                                         \
    if (auto EC = X)                                                           \
      return EC;                                                               \
  } while (false)

static const EnumEntry<TypeLeafKind> LeafTypeNames[] = {
#define CV_TYPE(enum, val) {#enum, enum},
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
};

static StringRef getLeafTypeName(TypeLeafKind LT) {
  switch (LT) {
#define TYPE_RECORD(ename, value, name)                                        \
  case ename:                                                                  \
    return #name;
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
  default:
    break;
  }
  return "UnknownLeaf";
}

template <typename T>
static bool compEnumNames(const EnumEntry<T> &lhs, const EnumEntry<T> &rhs) {
  return lhs.Name < rhs.Name;
}

template <typename T, typename TFlag>
static std::string getFlagNames(CodeViewRecordIO &IO, T Value,
                                ArrayRef<EnumEntry<TFlag>> Flags) {
  if (!IO.isStreaming())
    return std::string("");
  typedef EnumEntry<TFlag> FlagEntry;
  typedef SmallVector<FlagEntry, 10> FlagVector;
  FlagVector SetFlags;
  for (const auto &Flag : Flags) {
    if (Flag.Value == 0)
      continue;
    if ((Value & Flag.Value) == Flag.Value) {
      SetFlags.push_back(Flag);
    }
  }

  llvm::sort(SetFlags, &compEnumNames<TFlag>);

  std::string FlagLabel;
  bool FirstOcc = true;
  for (const auto &Flag : SetFlags) {
    if (FirstOcc)
      FirstOcc = false;
    else
      FlagLabel += (" | ");

    FlagLabel += (Flag.Name.str() + " (0x" + utohexstr(Flag.Value) + ")");
  }

  if (!FlagLabel.empty()) {
    std::string LabelWithBraces(" ( ");
    LabelWithBraces += FlagLabel + " )";
    return LabelWithBraces;
  } else
    return FlagLabel;
}

template <typename T, typename TEnum>
static StringRef getEnumName(CodeViewRecordIO &IO, T Value,
                             ArrayRef<EnumEntry<TEnum>> EnumValues) {
  if (!IO.isStreaming())
    return "";
  StringRef Name;
  for (const auto &EnumItem : EnumValues) {
    if (EnumItem.Value == Value) {
      Name = EnumItem.Name;
      break;
    }
  }

  return Name;
}

static std::string getMemberAttributes(CodeViewRecordIO &IO,
                                       MemberAccess Access, MethodKind Kind,
                                       MethodOptions Options) {
  if (!IO.isStreaming())
    return "";
  std::string AccessSpecifier = std::string(
      getEnumName(IO, uint8_t(Access), ArrayRef(getMemberAccessNames())));
  std::string MemberAttrs(AccessSpecifier);
  if (Kind != MethodKind::Vanilla) {
    std::string MethodKind = std::string(
        getEnumName(IO, unsigned(Kind), ArrayRef(getMemberKindNames())));
    MemberAttrs += ", " + MethodKind;
  }
  if (Options != MethodOptions::None) {
    std::string MethodOptions =
        getFlagNames(IO, unsigned(Options), ArrayRef(getMethodOptionNames()));
    MemberAttrs += ", " + MethodOptions;
  }
  return MemberAttrs;
}

struct MapOneMethodRecord {
  explicit MapOneMethodRecord(bool IsFromOverloadList)
      : IsFromOverloadList(IsFromOverloadList) {}

  Error operator()(CodeViewRecordIO &IO, OneMethodRecord &Method) const {
    std::string Attrs = getMemberAttributes(
        IO, Method.getAccess(), Method.getMethodKind(), Method.getOptions());
    error(IO.mapInteger(Method.Attrs.Attrs, "Attrs: " + Attrs));
    if (IsFromOverloadList) {
      uint16_t Padding = 0;
      error(IO.mapInteger(Padding));
    }
    error(IO.mapInteger(Method.Type, "Type"));
    if (Method.isIntroducingVirtual()) {
      error(IO.mapInteger(Method.VFTableOffset, "VFTableOffset"));
    } else if (IO.isReading())
      Method.VFTableOffset = -1;

    if (!IsFromOverloadList)
      error(IO.mapStringZ(Method.Name, "Name"));

    return Error::success();
  }

private:
  bool IsFromOverloadList;
};
} // namespace

// Computes a string representation of a hash of the specified name, suitable
// for use when emitting CodeView type names.
static void computeHashString(StringRef Name,
                              SmallString<32> &StringifiedHash) {
  llvm::MD5 Hash;
  llvm::MD5::MD5Result Result;
  Hash.update(Name);
  Hash.final(Result);
  Hash.stringifyResult(Result, StringifiedHash);
}

static Error mapNameAndUniqueName(CodeViewRecordIO &IO, StringRef &Name,
                                  StringRef &UniqueName, bool HasUniqueName) {
  if (IO.isWriting()) {
    // Try to be smart about what we write here.  We can't write anything too
    // large, so if we're going to go over the limit, replace lengthy names with
    // a stringified hash value.
    size_t BytesLeft = IO.maxFieldLength();
    if (HasUniqueName) {
      size_t BytesNeeded = Name.size() + UniqueName.size() + 2;
      if (BytesNeeded > BytesLeft) {
        // The minimum space required for emitting hashes of both names.
        assert(BytesLeft >= 70);

        // Replace the entire unique name with a hash of the unique name.
        SmallString<32> Hash;
        computeHashString(UniqueName, Hash);
        std::string UniqueB = Twine("??@" + Hash + "@").str();
        assert(UniqueB.size() == 36);

        // Truncate the name if necessary and append a hash of the name.
        // The name length, hash included, is limited to 4096 bytes.
        const size_t MaxTakeN = 4096;
        size_t TakeN = std::min(MaxTakeN, BytesLeft - UniqueB.size() - 2) - 32;
        computeHashString(Name, Hash);
        std::string NameB = (Name.take_front(TakeN) + Hash).str();

        StringRef N = NameB;
        StringRef U = UniqueB;
        error(IO.mapStringZ(N));
        error(IO.mapStringZ(U));
      } else {
        error(IO.mapStringZ(Name));
        error(IO.mapStringZ(UniqueName));
      }
    } else {
      // Cap the length of the string at however many bytes we have available,
      // plus one for the required null terminator.
      auto N = StringRef(Name).take_front(BytesLeft - 1);
      error(IO.mapStringZ(N));
    }
  } else {
    // Reading & Streaming mode come after writing mode is executed for each
    // record. Truncating large names are done during writing, so its not
    // necessary to do it while reading or streaming.
    error(IO.mapStringZ(Name, "Name"));
    if (HasUniqueName)
      error(IO.mapStringZ(UniqueName, "LinkageName"));
  }

  return Error::success();
}

Error TypeRecordMapping::visitTypeBegin(CVType &CVR) {
  assert(!TypeKind && "Already in a type mapping!");
  assert(!MemberKind && "Already in a member mapping!");

  // FieldList and MethodList records can be any length because they can be
  // split with continuation records.  All other record types cannot be
  // longer than the maximum record length.
  std::optional<uint32_t> MaxLen;
  if (CVR.kind() != TypeLeafKind::LF_FIELDLIST &&
      CVR.kind() != TypeLeafKind::LF_METHODLIST)
    MaxLen = MaxRecordLength - sizeof(RecordPrefix);
  error(IO.beginRecord(MaxLen));
  TypeKind = CVR.kind();

  if (IO.isStreaming()) {
    auto RecordKind = CVR.kind();
    uint16_t RecordLen = CVR.length() - 2;
    std::string RecordKindName = std::string(
        getEnumName(IO, unsigned(RecordKind), ArrayRef(LeafTypeNames)));
    error(IO.mapInteger(RecordLen, "Record length"));
    error(IO.mapEnum(RecordKind, "Record kind: " + RecordKindName));
  }
  return Error::success();
}

Error TypeRecordMapping::visitTypeBegin(CVType &CVR, TypeIndex Index) {
  if (IO.isStreaming())
    IO.emitRawComment(" " + getLeafTypeName(CVR.kind()) + " (0x" +
                      utohexstr(Index.getIndex()) + ")");
  return visitTypeBegin(CVR);
}

Error TypeRecordMapping::visitTypeEnd(CVType &Record) {
  assert(TypeKind && "Not in a type mapping!");
  assert(!MemberKind && "Still in a member mapping!");

  error(IO.endRecord());

  TypeKind.reset();
  return Error::success();
}

Error TypeRecordMapping::visitMemberBegin(CVMemberRecord &Record) {
  assert(TypeKind && "Not in a type mapping!");
  assert(!MemberKind && "Already in a member mapping!");

  // The largest possible subrecord is one in which there is a record prefix,
  // followed by the subrecord, followed by a continuation, and that entire
  // sequence spawns `MaxRecordLength` bytes.  So the record's length is
  // calculated as follows.

  constexpr uint32_t ContinuationLength = 8;
  error(IO.beginRecord(MaxRecordLength - sizeof(RecordPrefix) -
                       ContinuationLength));

  MemberKind = Record.Kind;
  if (IO.isStreaming()) {
    std::string MemberKindName = std::string(getLeafTypeName(Record.Kind));
    MemberKindName +=
        " ( " +
        (getEnumName(IO, unsigned(Record.Kind), ArrayRef(LeafTypeNames)))
            .str() +
        " )";
    error(IO.mapEnum(Record.Kind, "Member kind: " + MemberKindName));
  }
  return Error::success();
}

Error TypeRecordMapping::visitMemberEnd(CVMemberRecord &Record) {
  assert(TypeKind && "Not in a type mapping!");
  assert(MemberKind && "Not in a member mapping!");

  if (IO.isReading()) {
    if (auto EC = IO.skipPadding())
      return EC;
  }

  MemberKind.reset();
  error(IO.endRecord());
  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, ModifierRecord &Record) {
  std::string ModifierNames =
      getFlagNames(IO, static_cast<uint16_t>(Record.Modifiers),
                   ArrayRef(getTypeModifierNames()));
  error(IO.mapInteger(Record.ModifiedType, "ModifiedType"));
  error(IO.mapEnum(Record.Modifiers, "Modifiers" + ModifierNames));
  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          ProcedureRecord &Record) {
  std::string CallingConvName = std::string(getEnumName(
      IO, uint8_t(Record.CallConv), ArrayRef(getCallingConventions())));
  std::string FuncOptionNames =
      getFlagNames(IO, static_cast<uint16_t>(Record.Options),
                   ArrayRef(getFunctionOptionEnum()));
  error(IO.mapInteger(Record.ReturnType, "ReturnType"));
  error(IO.mapEnum(Record.CallConv, "CallingConvention: " + CallingConvName));
  error(IO.mapEnum(Record.Options, "FunctionOptions" + FuncOptionNames));
  error(IO.mapInteger(Record.ParameterCount, "NumParameters"));
  error(IO.mapInteger(Record.ArgumentList, "ArgListType"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          MemberFunctionRecord &Record) {
  std::string CallingConvName = std::string(getEnumName(
      IO, uint8_t(Record.CallConv), ArrayRef(getCallingConventions())));
  std::string FuncOptionNames =
      getFlagNames(IO, static_cast<uint16_t>(Record.Options),
                   ArrayRef(getFunctionOptionEnum()));
  error(IO.mapInteger(Record.ReturnType, "ReturnType"));
  error(IO.mapInteger(Record.ClassType, "ClassType"));
  error(IO.mapInteger(Record.ThisType, "ThisType"));
  error(IO.mapEnum(Record.CallConv, "CallingConvention: " + CallingConvName));
  error(IO.mapEnum(Record.Options, "FunctionOptions" + FuncOptionNames));
  error(IO.mapInteger(Record.ParameterCount, "NumParameters"));
  error(IO.mapInteger(Record.ArgumentList, "ArgListType"));
  error(IO.mapInteger(Record.ThisPointerAdjustment, "ThisAdjustment"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, ArgListRecord &Record) {
  error(IO.mapVectorN<uint32_t>(
      Record.ArgIndices,
      [](CodeViewRecordIO &IO, TypeIndex &N) {
        return IO.mapInteger(N, "Argument");
      },
      "NumArgs"));
  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          StringListRecord &Record) {
  error(IO.mapVectorN<uint32_t>(
      Record.StringIndices,
      [](CodeViewRecordIO &IO, TypeIndex &N) {
        return IO.mapInteger(N, "Strings");
      },
      "NumStrings"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, PointerRecord &Record) {

  SmallString<128> Attr("Attrs: ");

  if (IO.isStreaming()) {
    std::string PtrType = std::string(getEnumName(
        IO, unsigned(Record.getPointerKind()), ArrayRef(getPtrKindNames())));
    Attr += "[ Type: " + PtrType;

    std::string PtrMode = std::string(getEnumName(
        IO, unsigned(Record.getMode()), ArrayRef(getPtrModeNames())));
    Attr += ", Mode: " + PtrMode;

    auto PtrSizeOf = Record.getSize();
    Attr += ", SizeOf: " + itostr(PtrSizeOf);

    if (Record.isFlat())
      Attr += ", isFlat";
    if (Record.isConst())
      Attr += ", isConst";
    if (Record.isVolatile())
      Attr += ", isVolatile";
    if (Record.isUnaligned())
      Attr += ", isUnaligned";
    if (Record.isRestrict())
      Attr += ", isRestricted";
    if (Record.isLValueReferenceThisPtr())
      Attr += ", isThisPtr&";
    if (Record.isRValueReferenceThisPtr())
      Attr += ", isThisPtr&&";
    Attr += " ]";
  }

  error(IO.mapInteger(Record.ReferentType, "PointeeType"));
  error(IO.mapInteger(Record.Attrs, Attr));

  if (Record.isPointerToMember()) {
    if (IO.isReading())
      Record.MemberInfo.emplace();

    MemberPointerInfo &M = *Record.MemberInfo;
    error(IO.mapInteger(M.ContainingType, "ClassType"));
    std::string PtrMemberGetRepresentation = std::string(getEnumName(
        IO, uint16_t(M.Representation), ArrayRef(getPtrMemberRepNames())));
    error(IO.mapEnum(M.Representation,
                     "Representation: " + PtrMemberGetRepresentation));
  }

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, ArrayRecord &Record) {
  error(IO.mapInteger(Record.ElementType, "ElementType"));
  error(IO.mapInteger(Record.IndexType, "IndexType"));
  error(IO.mapEncodedInteger(Record.Size, "SizeOf"));
  error(IO.mapStringZ(Record.Name, "Name"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, ClassRecord &Record) {
  assert((CVR.kind() == TypeLeafKind::LF_STRUCTURE) ||
         (CVR.kind() == TypeLeafKind::LF_CLASS) ||
         (CVR.kind() == TypeLeafKind::LF_INTERFACE));

  std::string PropertiesNames =
      getFlagNames(IO, static_cast<uint16_t>(Record.Options),
                   ArrayRef(getClassOptionNames()));
  error(IO.mapInteger(Record.MemberCount, "MemberCount"));
  error(IO.mapEnum(Record.Options, "Properties" + PropertiesNames));
  error(IO.mapInteger(Record.FieldList, "FieldList"));
  error(IO.mapInteger(Record.DerivationList, "DerivedFrom"));
  error(IO.mapInteger(Record.VTableShape, "VShape"));
  error(IO.mapEncodedInteger(Record.Size, "SizeOf"));
  error(mapNameAndUniqueName(IO, Record.Name, Record.UniqueName,
                             Record.hasUniqueName()));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, UnionRecord &Record) {
  std::string PropertiesNames =
      getFlagNames(IO, static_cast<uint16_t>(Record.Options),
                   ArrayRef(getClassOptionNames()));
  error(IO.mapInteger(Record.MemberCount, "MemberCount"));
  error(IO.mapEnum(Record.Options, "Properties" + PropertiesNames));
  error(IO.mapInteger(Record.FieldList, "FieldList"));
  error(IO.mapEncodedInteger(Record.Size, "SizeOf"));
  error(mapNameAndUniqueName(IO, Record.Name, Record.UniqueName,
                             Record.hasUniqueName()));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, EnumRecord &Record) {
  std::string PropertiesNames =
      getFlagNames(IO, static_cast<uint16_t>(Record.Options),
                   ArrayRef(getClassOptionNames()));
  error(IO.mapInteger(Record.MemberCount, "NumEnumerators"));
  error(IO.mapEnum(Record.Options, "Properties" + PropertiesNames));
  error(IO.mapInteger(Record.UnderlyingType, "UnderlyingType"));
  error(IO.mapInteger(Record.FieldList, "FieldListType"));
  error(mapNameAndUniqueName(IO, Record.Name, Record.UniqueName,
                             Record.hasUniqueName()));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, BitFieldRecord &Record) {
  error(IO.mapInteger(Record.Type, "Type"));
  error(IO.mapInteger(Record.BitSize, "BitSize"));
  error(IO.mapInteger(Record.BitOffset, "BitOffset"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          VFTableShapeRecord &Record) {
  uint16_t Size;
  if (!IO.isReading()) {
    ArrayRef<VFTableSlotKind> Slots = Record.getSlots();
    Size = Slots.size();
    error(IO.mapInteger(Size, "VFEntryCount"));

    for (size_t SlotIndex = 0; SlotIndex < Slots.size(); SlotIndex += 2) {
      uint8_t Byte = static_cast<uint8_t>(Slots[SlotIndex]) << 4;
      if ((SlotIndex + 1) < Slots.size()) {
        Byte |= static_cast<uint8_t>(Slots[SlotIndex + 1]);
      }
      error(IO.mapInteger(Byte));
    }
  } else {
    error(IO.mapInteger(Size));
    for (uint16_t I = 0; I < Size; I += 2) {
      uint8_t Byte;
      error(IO.mapInteger(Byte));
      Record.Slots.push_back(static_cast<VFTableSlotKind>(Byte & 0xF));
      if ((I + 1) < Size)
        Record.Slots.push_back(static_cast<VFTableSlotKind>(Byte >> 4));
    }
  }

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, VFTableRecord &Record) {
  error(IO.mapInteger(Record.CompleteClass, "CompleteClass"));
  error(IO.mapInteger(Record.OverriddenVFTable, "OverriddenVFTable"));
  error(IO.mapInteger(Record.VFPtrOffset, "VFPtrOffset"));
  uint32_t NamesLen = 0;
  if (!IO.isReading()) {
    for (auto Name : Record.MethodNames)
      NamesLen += Name.size() + 1;
  }
  error(IO.mapInteger(NamesLen));
  error(IO.mapVectorTail(
      Record.MethodNames,
      [](CodeViewRecordIO &IO, StringRef &S) {
        return IO.mapStringZ(S, "MethodName");
      },
      "VFTableName"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, StringIdRecord &Record) {
  error(IO.mapInteger(Record.Id, "Id"));
  error(IO.mapStringZ(Record.String, "StringData"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          UdtSourceLineRecord &Record) {
  error(IO.mapInteger(Record.UDT, "UDT"));
  error(IO.mapInteger(Record.SourceFile, "SourceFile"));
  error(IO.mapInteger(Record.LineNumber, "LineNumber"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          UdtModSourceLineRecord &Record) {
  error(IO.mapInteger(Record.UDT, "UDT"));
  error(IO.mapInteger(Record.SourceFile, "SourceFile"));
  error(IO.mapInteger(Record.LineNumber, "LineNumber"));
  error(IO.mapInteger(Record.Module, "Module"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, FuncIdRecord &Record) {
  error(IO.mapInteger(Record.ParentScope, "ParentScope"));
  error(IO.mapInteger(Record.FunctionType, "FunctionType"));
  error(IO.mapStringZ(Record.Name, "Name"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          MemberFuncIdRecord &Record) {
  error(IO.mapInteger(Record.ClassType, "ClassType"));
  error(IO.mapInteger(Record.FunctionType, "FunctionType"));
  error(IO.mapStringZ(Record.Name, "Name"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          BuildInfoRecord &Record) {
  error(IO.mapVectorN<uint16_t>(
      Record.ArgIndices,
      [](CodeViewRecordIO &IO, TypeIndex &N) {
        return IO.mapInteger(N, "Argument");
      },
      "NumArgs"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          MethodOverloadListRecord &Record) {
  // TODO: Split the list into multiple records if it's longer than 64KB, using
  // a subrecord of TypeRecordKind::Index to chain the records together.
  error(IO.mapVectorTail(Record.Methods, MapOneMethodRecord(true), "Method"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          FieldListRecord &Record) {
  if (IO.isStreaming()) {
    if (auto EC = codeview::visitMemberRecordStream(Record.Data, *this))
      return EC;
  } else
    error(IO.mapByteVectorTail(Record.Data));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          TypeServer2Record &Record) {
  error(IO.mapGuid(Record.Guid, "Guid"));
  error(IO.mapInteger(Record.Age, "Age"));
  error(IO.mapStringZ(Record.Name, "Name"));
  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, LabelRecord &Record) {
  std::string ModeName = std::string(
      getEnumName(IO, uint16_t(Record.Mode), ArrayRef(getLabelTypeEnum())));
  error(IO.mapEnum(Record.Mode, "Mode: " + ModeName));
  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          BaseClassRecord &Record) {
  std::string Attrs = getMemberAttributes(
      IO, Record.getAccess(), MethodKind::Vanilla, MethodOptions::None);
  error(IO.mapInteger(Record.Attrs.Attrs, "Attrs: " + Attrs));
  error(IO.mapInteger(Record.Type, "BaseType"));
  error(IO.mapEncodedInteger(Record.Offset, "BaseOffset"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          EnumeratorRecord &Record) {
  std::string Attrs = getMemberAttributes(
      IO, Record.getAccess(), MethodKind::Vanilla, MethodOptions::None);
  error(IO.mapInteger(Record.Attrs.Attrs, "Attrs: " + Attrs));

  // FIXME: Handle full APInt such as __int128.
  error(IO.mapEncodedInteger(Record.Value, "EnumValue"));
  error(IO.mapStringZ(Record.Name, "Name"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          DataMemberRecord &Record) {
  std::string Attrs = getMemberAttributes(
      IO, Record.getAccess(), MethodKind::Vanilla, MethodOptions::None);
  error(IO.mapInteger(Record.Attrs.Attrs, "Attrs: " + Attrs));
  error(IO.mapInteger(Record.Type, "Type"));
  error(IO.mapEncodedInteger(Record.FieldOffset, "FieldOffset"));
  error(IO.mapStringZ(Record.Name, "Name"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          OverloadedMethodRecord &Record) {
  error(IO.mapInteger(Record.NumOverloads, "MethodCount"));
  error(IO.mapInteger(Record.MethodList, "MethodListIndex"));
  error(IO.mapStringZ(Record.Name, "Name"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          OneMethodRecord &Record) {
  const bool IsFromOverloadList = (TypeKind == LF_METHODLIST);
  MapOneMethodRecord Mapper(IsFromOverloadList);
  return Mapper(IO, Record);
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          NestedTypeRecord &Record) {
  uint16_t Padding = 0;
  error(IO.mapInteger(Padding, "Padding"));
  error(IO.mapInteger(Record.Type, "Type"));
  error(IO.mapStringZ(Record.Name, "Name"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          StaticDataMemberRecord &Record) {

  std::string Attrs = getMemberAttributes(
      IO, Record.getAccess(), MethodKind::Vanilla, MethodOptions::None);
  error(IO.mapInteger(Record.Attrs.Attrs, "Attrs: " + Attrs));
  error(IO.mapInteger(Record.Type, "Type"));
  error(IO.mapStringZ(Record.Name, "Name"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          VirtualBaseClassRecord &Record) {

  std::string Attrs = getMemberAttributes(
      IO, Record.getAccess(), MethodKind::Vanilla, MethodOptions::None);
  error(IO.mapInteger(Record.Attrs.Attrs, "Attrs: " + Attrs));
  error(IO.mapInteger(Record.BaseType, "BaseType"));
  error(IO.mapInteger(Record.VBPtrType, "VBPtrType"));
  error(IO.mapEncodedInteger(Record.VBPtrOffset, "VBPtrOffset"));
  error(IO.mapEncodedInteger(Record.VTableIndex, "VBTableIndex"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          VFPtrRecord &Record) {
  uint16_t Padding = 0;
  error(IO.mapInteger(Padding, "Padding"));
  error(IO.mapInteger(Record.Type, "Type"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          ListContinuationRecord &Record) {
  uint16_t Padding = 0;
  error(IO.mapInteger(Padding, "Padding"));
  error(IO.mapInteger(Record.ContinuationIndex, "ContinuationIndex"));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          PrecompRecord &Precomp) {
  error(IO.mapInteger(Precomp.StartTypeIndex, "StartIndex"));
  error(IO.mapInteger(Precomp.TypesCount, "Count"));
  error(IO.mapInteger(Precomp.Signature, "Signature"));
  error(IO.mapStringZ(Precomp.PrecompFilePath, "PrecompFile"));
  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          EndPrecompRecord &EndPrecomp) {
  error(IO.mapInteger(EndPrecomp.Signature, "Signature"));
  return Error::success();
}

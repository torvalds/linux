//===- BTFDebug.cpp - BTF Generator ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing BTF debug info.
//
//===----------------------------------------------------------------------===//

#include "BTFDebug.h"
#include "BPF.h"
#include "BPFCORE.h"
#include "MCTargetDesc/BPFMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include <optional>

using namespace llvm;

static const char *BTFKindStr[] = {
#define HANDLE_BTF_KIND(ID, NAME) "BTF_KIND_" #NAME,
#include "llvm/DebugInfo/BTF/BTF.def"
};

/// Emit a BTF common type.
void BTFTypeBase::emitType(MCStreamer &OS) {
  OS.AddComment(std::string(BTFKindStr[Kind]) + "(id = " + std::to_string(Id) +
                ")");
  OS.emitInt32(BTFType.NameOff);
  OS.AddComment("0x" + Twine::utohexstr(BTFType.Info));
  OS.emitInt32(BTFType.Info);
  OS.emitInt32(BTFType.Size);
}

BTFTypeDerived::BTFTypeDerived(const DIDerivedType *DTy, unsigned Tag,
                               bool NeedsFixup)
    : DTy(DTy), NeedsFixup(NeedsFixup), Name(DTy->getName()) {
  switch (Tag) {
  case dwarf::DW_TAG_pointer_type:
    Kind = BTF::BTF_KIND_PTR;
    break;
  case dwarf::DW_TAG_const_type:
    Kind = BTF::BTF_KIND_CONST;
    break;
  case dwarf::DW_TAG_volatile_type:
    Kind = BTF::BTF_KIND_VOLATILE;
    break;
  case dwarf::DW_TAG_typedef:
    Kind = BTF::BTF_KIND_TYPEDEF;
    break;
  case dwarf::DW_TAG_restrict_type:
    Kind = BTF::BTF_KIND_RESTRICT;
    break;
  default:
    llvm_unreachable("Unknown DIDerivedType Tag");
  }
  BTFType.Info = Kind << 24;
}

/// Used by DW_TAG_pointer_type only.
BTFTypeDerived::BTFTypeDerived(unsigned NextTypeId, unsigned Tag,
                               StringRef Name)
    : DTy(nullptr), NeedsFixup(false), Name(Name) {
  Kind = BTF::BTF_KIND_PTR;
  BTFType.Info = Kind << 24;
  BTFType.Type = NextTypeId;
}

void BTFTypeDerived::completeType(BTFDebug &BDebug) {
  if (IsCompleted)
    return;
  IsCompleted = true;

  BTFType.NameOff = BDebug.addString(Name);

  if (NeedsFixup || !DTy)
    return;

  // The base type for PTR/CONST/VOLATILE could be void.
  const DIType *ResolvedType = DTy->getBaseType();
  if (!ResolvedType) {
    assert((Kind == BTF::BTF_KIND_PTR || Kind == BTF::BTF_KIND_CONST ||
            Kind == BTF::BTF_KIND_VOLATILE) &&
           "Invalid null basetype");
    BTFType.Type = 0;
  } else {
    BTFType.Type = BDebug.getTypeId(ResolvedType);
  }
}

void BTFTypeDerived::emitType(MCStreamer &OS) { BTFTypeBase::emitType(OS); }

void BTFTypeDerived::setPointeeType(uint32_t PointeeType) {
  BTFType.Type = PointeeType;
}

/// Represent a struct/union forward declaration.
BTFTypeFwd::BTFTypeFwd(StringRef Name, bool IsUnion) : Name(Name) {
  Kind = BTF::BTF_KIND_FWD;
  BTFType.Info = IsUnion << 31 | Kind << 24;
  BTFType.Type = 0;
}

void BTFTypeFwd::completeType(BTFDebug &BDebug) {
  if (IsCompleted)
    return;
  IsCompleted = true;

  BTFType.NameOff = BDebug.addString(Name);
}

void BTFTypeFwd::emitType(MCStreamer &OS) { BTFTypeBase::emitType(OS); }

BTFTypeInt::BTFTypeInt(uint32_t Encoding, uint32_t SizeInBits,
                       uint32_t OffsetInBits, StringRef TypeName)
    : Name(TypeName) {
  // Translate IR int encoding to BTF int encoding.
  uint8_t BTFEncoding;
  switch (Encoding) {
  case dwarf::DW_ATE_boolean:
    BTFEncoding = BTF::INT_BOOL;
    break;
  case dwarf::DW_ATE_signed:
  case dwarf::DW_ATE_signed_char:
    BTFEncoding = BTF::INT_SIGNED;
    break;
  case dwarf::DW_ATE_unsigned:
  case dwarf::DW_ATE_unsigned_char:
    BTFEncoding = 0;
    break;
  default:
    llvm_unreachable("Unknown BTFTypeInt Encoding");
  }

  Kind = BTF::BTF_KIND_INT;
  BTFType.Info = Kind << 24;
  BTFType.Size = roundupToBytes(SizeInBits);
  IntVal = (BTFEncoding << 24) | OffsetInBits << 16 | SizeInBits;
}

void BTFTypeInt::completeType(BTFDebug &BDebug) {
  if (IsCompleted)
    return;
  IsCompleted = true;

  BTFType.NameOff = BDebug.addString(Name);
}

void BTFTypeInt::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  OS.AddComment("0x" + Twine::utohexstr(IntVal));
  OS.emitInt32(IntVal);
}

BTFTypeEnum::BTFTypeEnum(const DICompositeType *ETy, uint32_t VLen,
    bool IsSigned) : ETy(ETy) {
  Kind = BTF::BTF_KIND_ENUM;
  BTFType.Info = IsSigned << 31 | Kind << 24 | VLen;
  BTFType.Size = roundupToBytes(ETy->getSizeInBits());
}

void BTFTypeEnum::completeType(BTFDebug &BDebug) {
  if (IsCompleted)
    return;
  IsCompleted = true;

  BTFType.NameOff = BDebug.addString(ETy->getName());

  DINodeArray Elements = ETy->getElements();
  for (const auto Element : Elements) {
    const auto *Enum = cast<DIEnumerator>(Element);

    struct BTF::BTFEnum BTFEnum;
    BTFEnum.NameOff = BDebug.addString(Enum->getName());
    // BTF enum value is 32bit, enforce it.
    uint32_t Value;
    if (Enum->isUnsigned())
      Value = static_cast<uint32_t>(Enum->getValue().getZExtValue());
    else
      Value = static_cast<uint32_t>(Enum->getValue().getSExtValue());
    BTFEnum.Val = Value;
    EnumValues.push_back(BTFEnum);
  }
}

void BTFTypeEnum::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  for (const auto &Enum : EnumValues) {
    OS.emitInt32(Enum.NameOff);
    OS.emitInt32(Enum.Val);
  }
}

BTFTypeEnum64::BTFTypeEnum64(const DICompositeType *ETy, uint32_t VLen,
    bool IsSigned) : ETy(ETy) {
  Kind = BTF::BTF_KIND_ENUM64;
  BTFType.Info = IsSigned << 31 | Kind << 24 | VLen;
  BTFType.Size = roundupToBytes(ETy->getSizeInBits());
}

void BTFTypeEnum64::completeType(BTFDebug &BDebug) {
  if (IsCompleted)
    return;
  IsCompleted = true;

  BTFType.NameOff = BDebug.addString(ETy->getName());

  DINodeArray Elements = ETy->getElements();
  for (const auto Element : Elements) {
    const auto *Enum = cast<DIEnumerator>(Element);

    struct BTF::BTFEnum64 BTFEnum;
    BTFEnum.NameOff = BDebug.addString(Enum->getName());
    uint64_t Value;
    if (Enum->isUnsigned())
      Value = static_cast<uint64_t>(Enum->getValue().getZExtValue());
    else
      Value = static_cast<uint64_t>(Enum->getValue().getSExtValue());
    BTFEnum.Val_Lo32 = Value;
    BTFEnum.Val_Hi32 = Value >> 32;
    EnumValues.push_back(BTFEnum);
  }
}

void BTFTypeEnum64::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  for (const auto &Enum : EnumValues) {
    OS.emitInt32(Enum.NameOff);
    OS.AddComment("0x" + Twine::utohexstr(Enum.Val_Lo32));
    OS.emitInt32(Enum.Val_Lo32);
    OS.AddComment("0x" + Twine::utohexstr(Enum.Val_Hi32));
    OS.emitInt32(Enum.Val_Hi32);
  }
}

BTFTypeArray::BTFTypeArray(uint32_t ElemTypeId, uint32_t NumElems) {
  Kind = BTF::BTF_KIND_ARRAY;
  BTFType.NameOff = 0;
  BTFType.Info = Kind << 24;
  BTFType.Size = 0;

  ArrayInfo.ElemType = ElemTypeId;
  ArrayInfo.Nelems = NumElems;
}

/// Represent a BTF array.
void BTFTypeArray::completeType(BTFDebug &BDebug) {
  if (IsCompleted)
    return;
  IsCompleted = true;

  // The IR does not really have a type for the index.
  // A special type for array index should have been
  // created during initial type traversal. Just
  // retrieve that type id.
  ArrayInfo.IndexType = BDebug.getArrayIndexTypeId();
}

void BTFTypeArray::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  OS.emitInt32(ArrayInfo.ElemType);
  OS.emitInt32(ArrayInfo.IndexType);
  OS.emitInt32(ArrayInfo.Nelems);
}

/// Represent either a struct or a union.
BTFTypeStruct::BTFTypeStruct(const DICompositeType *STy, bool IsStruct,
                             bool HasBitField, uint32_t Vlen)
    : STy(STy), HasBitField(HasBitField) {
  Kind = IsStruct ? BTF::BTF_KIND_STRUCT : BTF::BTF_KIND_UNION;
  BTFType.Size = roundupToBytes(STy->getSizeInBits());
  BTFType.Info = (HasBitField << 31) | (Kind << 24) | Vlen;
}

void BTFTypeStruct::completeType(BTFDebug &BDebug) {
  if (IsCompleted)
    return;
  IsCompleted = true;

  BTFType.NameOff = BDebug.addString(STy->getName());

  // Add struct/union members.
  const DINodeArray Elements = STy->getElements();
  for (const auto *Element : Elements) {
    struct BTF::BTFMember BTFMember;
    const auto *DDTy = cast<DIDerivedType>(Element);

    BTFMember.NameOff = BDebug.addString(DDTy->getName());
    if (HasBitField) {
      uint8_t BitFieldSize = DDTy->isBitField() ? DDTy->getSizeInBits() : 0;
      BTFMember.Offset = BitFieldSize << 24 | DDTy->getOffsetInBits();
    } else {
      BTFMember.Offset = DDTy->getOffsetInBits();
    }
    const auto *BaseTy = DDTy->getBaseType();
    BTFMember.Type = BDebug.getTypeId(BaseTy);
    Members.push_back(BTFMember);
  }
}

void BTFTypeStruct::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  for (const auto &Member : Members) {
    OS.emitInt32(Member.NameOff);
    OS.emitInt32(Member.Type);
    OS.AddComment("0x" + Twine::utohexstr(Member.Offset));
    OS.emitInt32(Member.Offset);
  }
}

std::string BTFTypeStruct::getName() { return std::string(STy->getName()); }

/// The Func kind represents both subprogram and pointee of function
/// pointers. If the FuncName is empty, it represents a pointee of function
/// pointer. Otherwise, it represents a subprogram. The func arg names
/// are empty for pointee of function pointer case, and are valid names
/// for subprogram.
BTFTypeFuncProto::BTFTypeFuncProto(
    const DISubroutineType *STy, uint32_t VLen,
    const std::unordered_map<uint32_t, StringRef> &FuncArgNames)
    : STy(STy), FuncArgNames(FuncArgNames) {
  Kind = BTF::BTF_KIND_FUNC_PROTO;
  BTFType.Info = (Kind << 24) | VLen;
}

void BTFTypeFuncProto::completeType(BTFDebug &BDebug) {
  if (IsCompleted)
    return;
  IsCompleted = true;

  DITypeRefArray Elements = STy->getTypeArray();
  auto RetType = Elements[0];
  BTFType.Type = RetType ? BDebug.getTypeId(RetType) : 0;
  BTFType.NameOff = 0;

  // For null parameter which is typically the last one
  // to represent the vararg, encode the NameOff/Type to be 0.
  for (unsigned I = 1, N = Elements.size(); I < N; ++I) {
    struct BTF::BTFParam Param;
    auto Element = Elements[I];
    if (Element) {
      Param.NameOff = BDebug.addString(FuncArgNames[I]);
      Param.Type = BDebug.getTypeId(Element);
    } else {
      Param.NameOff = 0;
      Param.Type = 0;
    }
    Parameters.push_back(Param);
  }
}

void BTFTypeFuncProto::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  for (const auto &Param : Parameters) {
    OS.emitInt32(Param.NameOff);
    OS.emitInt32(Param.Type);
  }
}

BTFTypeFunc::BTFTypeFunc(StringRef FuncName, uint32_t ProtoTypeId,
    uint32_t Scope)
    : Name(FuncName) {
  Kind = BTF::BTF_KIND_FUNC;
  BTFType.Info = (Kind << 24) | Scope;
  BTFType.Type = ProtoTypeId;
}

void BTFTypeFunc::completeType(BTFDebug &BDebug) {
  if (IsCompleted)
    return;
  IsCompleted = true;

  BTFType.NameOff = BDebug.addString(Name);
}

void BTFTypeFunc::emitType(MCStreamer &OS) { BTFTypeBase::emitType(OS); }

BTFKindVar::BTFKindVar(StringRef VarName, uint32_t TypeId, uint32_t VarInfo)
    : Name(VarName) {
  Kind = BTF::BTF_KIND_VAR;
  BTFType.Info = Kind << 24;
  BTFType.Type = TypeId;
  Info = VarInfo;
}

void BTFKindVar::completeType(BTFDebug &BDebug) {
  BTFType.NameOff = BDebug.addString(Name);
}

void BTFKindVar::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  OS.emitInt32(Info);
}

BTFKindDataSec::BTFKindDataSec(AsmPrinter *AsmPrt, std::string SecName)
    : Asm(AsmPrt), Name(SecName) {
  Kind = BTF::BTF_KIND_DATASEC;
  BTFType.Info = Kind << 24;
  BTFType.Size = 0;
}

void BTFKindDataSec::completeType(BTFDebug &BDebug) {
  BTFType.NameOff = BDebug.addString(Name);
  BTFType.Info |= Vars.size();
}

void BTFKindDataSec::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);

  for (const auto &V : Vars) {
    OS.emitInt32(std::get<0>(V));
    Asm->emitLabelReference(std::get<1>(V), 4);
    OS.emitInt32(std::get<2>(V));
  }
}

BTFTypeFloat::BTFTypeFloat(uint32_t SizeInBits, StringRef TypeName)
    : Name(TypeName) {
  Kind = BTF::BTF_KIND_FLOAT;
  BTFType.Info = Kind << 24;
  BTFType.Size = roundupToBytes(SizeInBits);
}

void BTFTypeFloat::completeType(BTFDebug &BDebug) {
  if (IsCompleted)
    return;
  IsCompleted = true;

  BTFType.NameOff = BDebug.addString(Name);
}

BTFTypeDeclTag::BTFTypeDeclTag(uint32_t BaseTypeId, int ComponentIdx,
                               StringRef Tag)
    : Tag(Tag) {
  Kind = BTF::BTF_KIND_DECL_TAG;
  BTFType.Info = Kind << 24;
  BTFType.Type = BaseTypeId;
  Info = ComponentIdx;
}

void BTFTypeDeclTag::completeType(BTFDebug &BDebug) {
  if (IsCompleted)
    return;
  IsCompleted = true;

  BTFType.NameOff = BDebug.addString(Tag);
}

void BTFTypeDeclTag::emitType(MCStreamer &OS) {
  BTFTypeBase::emitType(OS);
  OS.emitInt32(Info);
}

BTFTypeTypeTag::BTFTypeTypeTag(uint32_t NextTypeId, StringRef Tag)
    : DTy(nullptr), Tag(Tag) {
  Kind = BTF::BTF_KIND_TYPE_TAG;
  BTFType.Info = Kind << 24;
  BTFType.Type = NextTypeId;
}

BTFTypeTypeTag::BTFTypeTypeTag(const DIDerivedType *DTy, StringRef Tag)
    : DTy(DTy), Tag(Tag) {
  Kind = BTF::BTF_KIND_TYPE_TAG;
  BTFType.Info = Kind << 24;
}

void BTFTypeTypeTag::completeType(BTFDebug &BDebug) {
  if (IsCompleted)
    return;
  IsCompleted = true;
  BTFType.NameOff = BDebug.addString(Tag);
  if (DTy) {
    const DIType *ResolvedType = DTy->getBaseType();
    if (!ResolvedType)
      BTFType.Type = 0;
    else
      BTFType.Type = BDebug.getTypeId(ResolvedType);
  }
}

uint32_t BTFStringTable::addString(StringRef S) {
  // Check whether the string already exists.
  for (auto &OffsetM : OffsetToIdMap) {
    if (Table[OffsetM.second] == S)
      return OffsetM.first;
  }
  // Not find, add to the string table.
  uint32_t Offset = Size;
  OffsetToIdMap[Offset] = Table.size();
  Table.push_back(std::string(S));
  Size += S.size() + 1;
  return Offset;
}

BTFDebug::BTFDebug(AsmPrinter *AP)
    : DebugHandlerBase(AP), OS(*Asm->OutStreamer), SkipInstruction(false),
      LineInfoGenerated(false), SecNameOff(0), ArrayIndexTypeId(0),
      MapDefNotCollected(true) {
  addString("\0");
}

uint32_t BTFDebug::addType(std::unique_ptr<BTFTypeBase> TypeEntry,
                           const DIType *Ty) {
  TypeEntry->setId(TypeEntries.size() + 1);
  uint32_t Id = TypeEntry->getId();
  DIToIdMap[Ty] = Id;
  TypeEntries.push_back(std::move(TypeEntry));
  return Id;
}

uint32_t BTFDebug::addType(std::unique_ptr<BTFTypeBase> TypeEntry) {
  TypeEntry->setId(TypeEntries.size() + 1);
  uint32_t Id = TypeEntry->getId();
  TypeEntries.push_back(std::move(TypeEntry));
  return Id;
}

void BTFDebug::visitBasicType(const DIBasicType *BTy, uint32_t &TypeId) {
  // Only int and binary floating point types are supported in BTF.
  uint32_t Encoding = BTy->getEncoding();
  std::unique_ptr<BTFTypeBase> TypeEntry;
  switch (Encoding) {
  case dwarf::DW_ATE_boolean:
  case dwarf::DW_ATE_signed:
  case dwarf::DW_ATE_signed_char:
  case dwarf::DW_ATE_unsigned:
  case dwarf::DW_ATE_unsigned_char:
    // Create a BTF type instance for this DIBasicType and put it into
    // DIToIdMap for cross-type reference check.
    TypeEntry = std::make_unique<BTFTypeInt>(
        Encoding, BTy->getSizeInBits(), BTy->getOffsetInBits(), BTy->getName());
    break;
  case dwarf::DW_ATE_float:
    TypeEntry =
        std::make_unique<BTFTypeFloat>(BTy->getSizeInBits(), BTy->getName());
    break;
  default:
    return;
  }

  TypeId = addType(std::move(TypeEntry), BTy);
}

/// Handle subprogram or subroutine types.
void BTFDebug::visitSubroutineType(
    const DISubroutineType *STy, bool ForSubprog,
    const std::unordered_map<uint32_t, StringRef> &FuncArgNames,
    uint32_t &TypeId) {
  DITypeRefArray Elements = STy->getTypeArray();
  uint32_t VLen = Elements.size() - 1;
  if (VLen > BTF::MAX_VLEN)
    return;

  // Subprogram has a valid non-zero-length name, and the pointee of
  // a function pointer has an empty name. The subprogram type will
  // not be added to DIToIdMap as it should not be referenced by
  // any other types.
  auto TypeEntry = std::make_unique<BTFTypeFuncProto>(STy, VLen, FuncArgNames);
  if (ForSubprog)
    TypeId = addType(std::move(TypeEntry)); // For subprogram
  else
    TypeId = addType(std::move(TypeEntry), STy); // For func ptr

  // Visit return type and func arg types.
  for (const auto Element : Elements) {
    visitTypeEntry(Element);
  }
}

void BTFDebug::processDeclAnnotations(DINodeArray Annotations,
                                      uint32_t BaseTypeId,
                                      int ComponentIdx) {
  if (!Annotations)
     return;

  for (const Metadata *Annotation : Annotations->operands()) {
    const MDNode *MD = cast<MDNode>(Annotation);
    const MDString *Name = cast<MDString>(MD->getOperand(0));
    if (Name->getString() != "btf_decl_tag")
      continue;

    const MDString *Value = cast<MDString>(MD->getOperand(1));
    auto TypeEntry = std::make_unique<BTFTypeDeclTag>(BaseTypeId, ComponentIdx,
                                                      Value->getString());
    addType(std::move(TypeEntry));
  }
}

uint32_t BTFDebug::processDISubprogram(const DISubprogram *SP,
                                       uint32_t ProtoTypeId, uint8_t Scope) {
  auto FuncTypeEntry =
      std::make_unique<BTFTypeFunc>(SP->getName(), ProtoTypeId, Scope);
  uint32_t FuncId = addType(std::move(FuncTypeEntry));

  // Process argument annotations.
  for (const DINode *DN : SP->getRetainedNodes()) {
    if (const auto *DV = dyn_cast<DILocalVariable>(DN)) {
      uint32_t Arg = DV->getArg();
      if (Arg)
        processDeclAnnotations(DV->getAnnotations(), FuncId, Arg - 1);
    }
  }
  processDeclAnnotations(SP->getAnnotations(), FuncId, -1);

  return FuncId;
}

/// Generate btf_type_tag chains.
int BTFDebug::genBTFTypeTags(const DIDerivedType *DTy, int BaseTypeId) {
  SmallVector<const MDString *, 4> MDStrs;
  DINodeArray Annots = DTy->getAnnotations();
  if (Annots) {
    // For type with "int __tag1 __tag2 *p", the MDStrs will have
    // content: [__tag1, __tag2].
    for (const Metadata *Annotations : Annots->operands()) {
      const MDNode *MD = cast<MDNode>(Annotations);
      const MDString *Name = cast<MDString>(MD->getOperand(0));
      if (Name->getString() != "btf_type_tag")
        continue;
      MDStrs.push_back(cast<MDString>(MD->getOperand(1)));
    }
  }

  if (MDStrs.size() == 0)
    return -1;

  // With MDStrs [__tag1, __tag2], the output type chain looks like
  //   PTR -> __tag2 -> __tag1 -> BaseType
  // In the below, we construct BTF types with the order of __tag1, __tag2
  // and PTR.
  unsigned TmpTypeId;
  std::unique_ptr<BTFTypeTypeTag> TypeEntry;
  if (BaseTypeId >= 0)
    TypeEntry =
        std::make_unique<BTFTypeTypeTag>(BaseTypeId, MDStrs[0]->getString());
  else
    TypeEntry = std::make_unique<BTFTypeTypeTag>(DTy, MDStrs[0]->getString());
  TmpTypeId = addType(std::move(TypeEntry));

  for (unsigned I = 1; I < MDStrs.size(); I++) {
    const MDString *Value = MDStrs[I];
    TypeEntry = std::make_unique<BTFTypeTypeTag>(TmpTypeId, Value->getString());
    TmpTypeId = addType(std::move(TypeEntry));
  }
  return TmpTypeId;
}

/// Handle structure/union types.
void BTFDebug::visitStructType(const DICompositeType *CTy, bool IsStruct,
                               uint32_t &TypeId) {
  const DINodeArray Elements = CTy->getElements();
  uint32_t VLen = Elements.size();
  if (VLen > BTF::MAX_VLEN)
    return;

  // Check whether we have any bitfield members or not
  bool HasBitField = false;
  for (const auto *Element : Elements) {
    auto E = cast<DIDerivedType>(Element);
    if (E->isBitField()) {
      HasBitField = true;
      break;
    }
  }

  auto TypeEntry =
      std::make_unique<BTFTypeStruct>(CTy, IsStruct, HasBitField, VLen);
  StructTypes.push_back(TypeEntry.get());
  TypeId = addType(std::move(TypeEntry), CTy);

  // Check struct/union annotations
  processDeclAnnotations(CTy->getAnnotations(), TypeId, -1);

  // Visit all struct members.
  int FieldNo = 0;
  for (const auto *Element : Elements) {
    const auto Elem = cast<DIDerivedType>(Element);
    visitTypeEntry(Elem);
    processDeclAnnotations(Elem->getAnnotations(), TypeId, FieldNo);
    FieldNo++;
  }
}

void BTFDebug::visitArrayType(const DICompositeType *CTy, uint32_t &TypeId) {
  // Visit array element type.
  uint32_t ElemTypeId;
  const DIType *ElemType = CTy->getBaseType();
  visitTypeEntry(ElemType, ElemTypeId, false, false);

  // Visit array dimensions.
  DINodeArray Elements = CTy->getElements();
  for (int I = Elements.size() - 1; I >= 0; --I) {
    if (auto *Element = dyn_cast_or_null<DINode>(Elements[I]))
      if (Element->getTag() == dwarf::DW_TAG_subrange_type) {
        const DISubrange *SR = cast<DISubrange>(Element);
        auto *CI = SR->getCount().dyn_cast<ConstantInt *>();
        int64_t Count = CI->getSExtValue();

        // For struct s { int b; char c[]; }, the c[] will be represented
        // as an array with Count = -1.
        auto TypeEntry =
            std::make_unique<BTFTypeArray>(ElemTypeId,
                Count >= 0 ? Count : 0);
        if (I == 0)
          ElemTypeId = addType(std::move(TypeEntry), CTy);
        else
          ElemTypeId = addType(std::move(TypeEntry));
      }
  }

  // The array TypeId is the type id of the outermost dimension.
  TypeId = ElemTypeId;

  // The IR does not have a type for array index while BTF wants one.
  // So create an array index type if there is none.
  if (!ArrayIndexTypeId) {
    auto TypeEntry = std::make_unique<BTFTypeInt>(dwarf::DW_ATE_unsigned, 32,
                                                   0, "__ARRAY_SIZE_TYPE__");
    ArrayIndexTypeId = addType(std::move(TypeEntry));
  }
}

void BTFDebug::visitEnumType(const DICompositeType *CTy, uint32_t &TypeId) {
  DINodeArray Elements = CTy->getElements();
  uint32_t VLen = Elements.size();
  if (VLen > BTF::MAX_VLEN)
    return;

  bool IsSigned = false;
  unsigned NumBits = 32;
  // No BaseType implies forward declaration in which case a
  // BTFTypeEnum with Vlen = 0 is emitted.
  if (CTy->getBaseType() != nullptr) {
    const auto *BTy = cast<DIBasicType>(CTy->getBaseType());
    IsSigned = BTy->getEncoding() == dwarf::DW_ATE_signed ||
               BTy->getEncoding() == dwarf::DW_ATE_signed_char;
    NumBits = BTy->getSizeInBits();
  }

  if (NumBits <= 32) {
    auto TypeEntry = std::make_unique<BTFTypeEnum>(CTy, VLen, IsSigned);
    TypeId = addType(std::move(TypeEntry), CTy);
  } else {
    assert(NumBits == 64);
    auto TypeEntry = std::make_unique<BTFTypeEnum64>(CTy, VLen, IsSigned);
    TypeId = addType(std::move(TypeEntry), CTy);
  }
  // No need to visit base type as BTF does not encode it.
}

/// Handle structure/union forward declarations.
void BTFDebug::visitFwdDeclType(const DICompositeType *CTy, bool IsUnion,
                                uint32_t &TypeId) {
  auto TypeEntry = std::make_unique<BTFTypeFwd>(CTy->getName(), IsUnion);
  TypeId = addType(std::move(TypeEntry), CTy);
}

/// Handle structure, union, array and enumeration types.
void BTFDebug::visitCompositeType(const DICompositeType *CTy,
                                  uint32_t &TypeId) {
  auto Tag = CTy->getTag();
  if (Tag == dwarf::DW_TAG_structure_type || Tag == dwarf::DW_TAG_union_type) {
    // Handle forward declaration differently as it does not have members.
    if (CTy->isForwardDecl())
      visitFwdDeclType(CTy, Tag == dwarf::DW_TAG_union_type, TypeId);
    else
      visitStructType(CTy, Tag == dwarf::DW_TAG_structure_type, TypeId);
  } else if (Tag == dwarf::DW_TAG_array_type)
    visitArrayType(CTy, TypeId);
  else if (Tag == dwarf::DW_TAG_enumeration_type)
    visitEnumType(CTy, TypeId);
}

bool BTFDebug::IsForwardDeclCandidate(const DIType *Base) {
  if (const auto *CTy = dyn_cast<DICompositeType>(Base)) {
    auto CTag = CTy->getTag();
    if ((CTag == dwarf::DW_TAG_structure_type ||
         CTag == dwarf::DW_TAG_union_type) &&
        !CTy->getName().empty() && !CTy->isForwardDecl())
      return true;
  }
  return false;
}

/// Handle pointer, typedef, const, volatile, restrict and member types.
void BTFDebug::visitDerivedType(const DIDerivedType *DTy, uint32_t &TypeId,
                                bool CheckPointer, bool SeenPointer) {
  unsigned Tag = DTy->getTag();

  /// Try to avoid chasing pointees, esp. structure pointees which may
  /// unnecessary bring in a lot of types.
  if (CheckPointer && !SeenPointer) {
    SeenPointer = Tag == dwarf::DW_TAG_pointer_type;
  }

  if (CheckPointer && SeenPointer) {
    const DIType *Base = DTy->getBaseType();
    if (Base) {
      if (IsForwardDeclCandidate(Base)) {
        /// Find a candidate, generate a fixup. Later on the struct/union
        /// pointee type will be replaced with either a real type or
        /// a forward declaration.
        auto TypeEntry = std::make_unique<BTFTypeDerived>(DTy, Tag, true);
        auto &Fixup = FixupDerivedTypes[cast<DICompositeType>(Base)];
        Fixup.push_back(std::make_pair(DTy, TypeEntry.get()));
        TypeId = addType(std::move(TypeEntry), DTy);
        return;
      }
    }
  }

  if (Tag == dwarf::DW_TAG_pointer_type) {
    int TmpTypeId = genBTFTypeTags(DTy, -1);
    if (TmpTypeId >= 0) {
      auto TypeDEntry =
          std::make_unique<BTFTypeDerived>(TmpTypeId, Tag, DTy->getName());
      TypeId = addType(std::move(TypeDEntry), DTy);
    } else {
      auto TypeEntry = std::make_unique<BTFTypeDerived>(DTy, Tag, false);
      TypeId = addType(std::move(TypeEntry), DTy);
    }
  } else if (Tag == dwarf::DW_TAG_typedef || Tag == dwarf::DW_TAG_const_type ||
             Tag == dwarf::DW_TAG_volatile_type ||
             Tag == dwarf::DW_TAG_restrict_type) {
    auto TypeEntry = std::make_unique<BTFTypeDerived>(DTy, Tag, false);
    TypeId = addType(std::move(TypeEntry), DTy);
    if (Tag == dwarf::DW_TAG_typedef)
      processDeclAnnotations(DTy->getAnnotations(), TypeId, -1);
  } else if (Tag != dwarf::DW_TAG_member) {
    return;
  }

  // Visit base type of pointer, typedef, const, volatile, restrict or
  // struct/union member.
  uint32_t TempTypeId = 0;
  if (Tag == dwarf::DW_TAG_member)
    visitTypeEntry(DTy->getBaseType(), TempTypeId, true, false);
  else
    visitTypeEntry(DTy->getBaseType(), TempTypeId, CheckPointer, SeenPointer);
}

/// Visit a type entry. CheckPointer is true if the type has
/// one of its predecessors as one struct/union member. SeenPointer
/// is true if CheckPointer is true and one of its predecessors
/// is a pointer. The goal of CheckPointer and SeenPointer is to
/// do pruning for struct/union types so some of these types
/// will not be emitted in BTF and rather forward declarations
/// will be generated.
void BTFDebug::visitTypeEntry(const DIType *Ty, uint32_t &TypeId,
                              bool CheckPointer, bool SeenPointer) {
  if (!Ty || DIToIdMap.find(Ty) != DIToIdMap.end()) {
    TypeId = DIToIdMap[Ty];

    // To handle the case like the following:
    //    struct t;
    //    typedef struct t _t;
    //    struct s1 { _t *c; };
    //    int test1(struct s1 *arg) { ... }
    //
    //    struct t { int a; int b; };
    //    struct s2 { _t c; }
    //    int test2(struct s2 *arg) { ... }
    //
    // During traversing test1() argument, "_t" is recorded
    // in DIToIdMap and a forward declaration fixup is created
    // for "struct t" to avoid pointee type traversal.
    //
    // During traversing test2() argument, even if we see "_t" is
    // already defined, we should keep moving to eventually
    // bring in types for "struct t". Otherwise, the "struct s2"
    // definition won't be correct.
    //
    // In the above, we have following debuginfo:
    //  {ptr, struct_member} ->  typedef -> struct
    // and BTF type for 'typedef' is generated while 'struct' may
    // be in FixUp. But let us generalize the above to handle
    //  {different types} -> [various derived types]+ -> another type.
    // For example,
    //  {func_param, struct_member} -> const -> ptr -> volatile -> struct
    // We will traverse const/ptr/volatile which already have corresponding
    // BTF types and generate type for 'struct' which might be in Fixup
    // state.
    if (Ty && (!CheckPointer || !SeenPointer)) {
      if (const auto *DTy = dyn_cast<DIDerivedType>(Ty)) {
        while (DTy) {
          const DIType *BaseTy = DTy->getBaseType();
          if (!BaseTy)
            break;

          if (DIToIdMap.find(BaseTy) != DIToIdMap.end()) {
            DTy = dyn_cast<DIDerivedType>(BaseTy);
          } else {
            if (CheckPointer && DTy->getTag() == dwarf::DW_TAG_pointer_type) {
              SeenPointer = true;
              if (IsForwardDeclCandidate(BaseTy))
                break;
            }
            uint32_t TmpTypeId;
            visitTypeEntry(BaseTy, TmpTypeId, CheckPointer, SeenPointer);
            break;
          }
        }
      }
    }

    return;
  }

  if (const auto *BTy = dyn_cast<DIBasicType>(Ty))
    visitBasicType(BTy, TypeId);
  else if (const auto *STy = dyn_cast<DISubroutineType>(Ty))
    visitSubroutineType(STy, false, std::unordered_map<uint32_t, StringRef>(),
                        TypeId);
  else if (const auto *CTy = dyn_cast<DICompositeType>(Ty))
    visitCompositeType(CTy, TypeId);
  else if (const auto *DTy = dyn_cast<DIDerivedType>(Ty))
    visitDerivedType(DTy, TypeId, CheckPointer, SeenPointer);
  else
    llvm_unreachable("Unknown DIType");
}

void BTFDebug::visitTypeEntry(const DIType *Ty) {
  uint32_t TypeId;
  visitTypeEntry(Ty, TypeId, false, false);
}

void BTFDebug::visitMapDefType(const DIType *Ty, uint32_t &TypeId) {
  if (!Ty || DIToIdMap.find(Ty) != DIToIdMap.end()) {
    TypeId = DIToIdMap[Ty];
    return;
  }

  // MapDef type may be a struct type or a non-pointer derived type
  const DIType *OrigTy = Ty;
  while (auto *DTy = dyn_cast<DIDerivedType>(Ty)) {
    auto Tag = DTy->getTag();
    if (Tag != dwarf::DW_TAG_typedef && Tag != dwarf::DW_TAG_const_type &&
        Tag != dwarf::DW_TAG_volatile_type &&
        Tag != dwarf::DW_TAG_restrict_type)
      break;
    Ty = DTy->getBaseType();
  }

  const auto *CTy = dyn_cast<DICompositeType>(Ty);
  if (!CTy)
    return;

  auto Tag = CTy->getTag();
  if (Tag != dwarf::DW_TAG_structure_type || CTy->isForwardDecl())
    return;

  // Visit all struct members to ensure pointee type is visited
  const DINodeArray Elements = CTy->getElements();
  for (const auto *Element : Elements) {
    const auto *MemberType = cast<DIDerivedType>(Element);
    visitTypeEntry(MemberType->getBaseType());
  }

  // Visit this type, struct or a const/typedef/volatile/restrict type
  visitTypeEntry(OrigTy, TypeId, false, false);
}

/// Read file contents from the actual file or from the source
std::string BTFDebug::populateFileContent(const DIFile *File) {
  std::string FileName;

  if (!File->getFilename().starts_with("/") && File->getDirectory().size())
    FileName = File->getDirectory().str() + "/" + File->getFilename().str();
  else
    FileName = std::string(File->getFilename());

  // No need to populate the contends if it has been populated!
  if (FileContent.contains(FileName))
    return FileName;

  std::vector<std::string> Content;
  std::string Line;
  Content.push_back(Line); // Line 0 for empty string

  std::unique_ptr<MemoryBuffer> Buf;
  auto Source = File->getSource();
  if (Source)
    Buf = MemoryBuffer::getMemBufferCopy(*Source);
  else if (ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
               MemoryBuffer::getFile(FileName))
    Buf = std::move(*BufOrErr);
  if (Buf)
    for (line_iterator I(*Buf, false), E; I != E; ++I)
      Content.push_back(std::string(*I));

  FileContent[FileName] = Content;
  return FileName;
}

void BTFDebug::constructLineInfo(MCSymbol *Label, const DIFile *File,
                                 uint32_t Line, uint32_t Column) {
  std::string FileName = populateFileContent(File);
  BTFLineInfo LineInfo;

  LineInfo.Label = Label;
  LineInfo.FileNameOff = addString(FileName);
  // If file content is not available, let LineOff = 0.
  if (Line < FileContent[FileName].size())
    LineInfo.LineOff = addString(FileContent[FileName][Line]);
  else
    LineInfo.LineOff = 0;
  LineInfo.LineNum = Line;
  LineInfo.ColumnNum = Column;
  LineInfoTable[SecNameOff].push_back(LineInfo);
}

void BTFDebug::emitCommonHeader() {
  OS.AddComment("0x" + Twine::utohexstr(BTF::MAGIC));
  OS.emitIntValue(BTF::MAGIC, 2);
  OS.emitInt8(BTF::VERSION);
  OS.emitInt8(0);
}

void BTFDebug::emitBTFSection() {
  // Do not emit section if no types and only "" string.
  if (!TypeEntries.size() && StringTable.getSize() == 1)
    return;

  MCContext &Ctx = OS.getContext();
  MCSectionELF *Sec = Ctx.getELFSection(".BTF", ELF::SHT_PROGBITS, 0);
  Sec->setAlignment(Align(4));
  OS.switchSection(Sec);

  // Emit header.
  emitCommonHeader();
  OS.emitInt32(BTF::HeaderSize);

  uint32_t TypeLen = 0, StrLen;
  for (const auto &TypeEntry : TypeEntries)
    TypeLen += TypeEntry->getSize();
  StrLen = StringTable.getSize();

  OS.emitInt32(0);
  OS.emitInt32(TypeLen);
  OS.emitInt32(TypeLen);
  OS.emitInt32(StrLen);

  // Emit type table.
  for (const auto &TypeEntry : TypeEntries)
    TypeEntry->emitType(OS);

  // Emit string table.
  uint32_t StringOffset = 0;
  for (const auto &S : StringTable.getTable()) {
    OS.AddComment("string offset=" + std::to_string(StringOffset));
    OS.emitBytes(S);
    OS.emitBytes(StringRef("\0", 1));
    StringOffset += S.size() + 1;
  }
}

void BTFDebug::emitBTFExtSection() {
  // Do not emit section if empty FuncInfoTable and LineInfoTable
  // and FieldRelocTable.
  if (!FuncInfoTable.size() && !LineInfoTable.size() &&
      !FieldRelocTable.size())
    return;

  MCContext &Ctx = OS.getContext();
  MCSectionELF *Sec = Ctx.getELFSection(".BTF.ext", ELF::SHT_PROGBITS, 0);
  Sec->setAlignment(Align(4));
  OS.switchSection(Sec);

  // Emit header.
  emitCommonHeader();
  OS.emitInt32(BTF::ExtHeaderSize);

  // Account for FuncInfo/LineInfo record size as well.
  uint32_t FuncLen = 4, LineLen = 4;
  // Do not account for optional FieldReloc.
  uint32_t FieldRelocLen = 0;
  for (const auto &FuncSec : FuncInfoTable) {
    FuncLen += BTF::SecFuncInfoSize;
    FuncLen += FuncSec.second.size() * BTF::BPFFuncInfoSize;
  }
  for (const auto &LineSec : LineInfoTable) {
    LineLen += BTF::SecLineInfoSize;
    LineLen += LineSec.second.size() * BTF::BPFLineInfoSize;
  }
  for (const auto &FieldRelocSec : FieldRelocTable) {
    FieldRelocLen += BTF::SecFieldRelocSize;
    FieldRelocLen += FieldRelocSec.second.size() * BTF::BPFFieldRelocSize;
  }

  if (FieldRelocLen)
    FieldRelocLen += 4;

  OS.emitInt32(0);
  OS.emitInt32(FuncLen);
  OS.emitInt32(FuncLen);
  OS.emitInt32(LineLen);
  OS.emitInt32(FuncLen + LineLen);
  OS.emitInt32(FieldRelocLen);

  // Emit func_info table.
  OS.AddComment("FuncInfo");
  OS.emitInt32(BTF::BPFFuncInfoSize);
  for (const auto &FuncSec : FuncInfoTable) {
    OS.AddComment("FuncInfo section string offset=" +
                  std::to_string(FuncSec.first));
    OS.emitInt32(FuncSec.first);
    OS.emitInt32(FuncSec.second.size());
    for (const auto &FuncInfo : FuncSec.second) {
      Asm->emitLabelReference(FuncInfo.Label, 4);
      OS.emitInt32(FuncInfo.TypeId);
    }
  }

  // Emit line_info table.
  OS.AddComment("LineInfo");
  OS.emitInt32(BTF::BPFLineInfoSize);
  for (const auto &LineSec : LineInfoTable) {
    OS.AddComment("LineInfo section string offset=" +
                  std::to_string(LineSec.first));
    OS.emitInt32(LineSec.first);
    OS.emitInt32(LineSec.second.size());
    for (const auto &LineInfo : LineSec.second) {
      Asm->emitLabelReference(LineInfo.Label, 4);
      OS.emitInt32(LineInfo.FileNameOff);
      OS.emitInt32(LineInfo.LineOff);
      OS.AddComment("Line " + std::to_string(LineInfo.LineNum) + " Col " +
                    std::to_string(LineInfo.ColumnNum));
      OS.emitInt32(LineInfo.LineNum << 10 | LineInfo.ColumnNum);
    }
  }

  // Emit field reloc table.
  if (FieldRelocLen) {
    OS.AddComment("FieldReloc");
    OS.emitInt32(BTF::BPFFieldRelocSize);
    for (const auto &FieldRelocSec : FieldRelocTable) {
      OS.AddComment("Field reloc section string offset=" +
                    std::to_string(FieldRelocSec.first));
      OS.emitInt32(FieldRelocSec.first);
      OS.emitInt32(FieldRelocSec.second.size());
      for (const auto &FieldRelocInfo : FieldRelocSec.second) {
        Asm->emitLabelReference(FieldRelocInfo.Label, 4);
        OS.emitInt32(FieldRelocInfo.TypeID);
        OS.emitInt32(FieldRelocInfo.OffsetNameOff);
        OS.emitInt32(FieldRelocInfo.RelocKind);
      }
    }
  }
}

void BTFDebug::beginFunctionImpl(const MachineFunction *MF) {
  auto *SP = MF->getFunction().getSubprogram();
  auto *Unit = SP->getUnit();

  if (Unit->getEmissionKind() == DICompileUnit::NoDebug) {
    SkipInstruction = true;
    return;
  }
  SkipInstruction = false;

  // Collect MapDef types. Map definition needs to collect
  // pointee types. Do it first. Otherwise, for the following
  // case:
  //    struct m { ...};
  //    struct t {
  //      struct m *key;
  //    };
  //    foo(struct t *arg);
  //
  //    struct mapdef {
  //      ...
  //      struct m *key;
  //      ...
  //    } __attribute__((section(".maps"))) hash_map;
  //
  // If subroutine foo is traversed first, a type chain
  // "ptr->struct m(fwd)" will be created and later on
  // when traversing mapdef, since "ptr->struct m" exists,
  // the traversal of "struct m" will be omitted.
  if (MapDefNotCollected) {
    processGlobals(true);
    MapDefNotCollected = false;
  }

  // Collect all types locally referenced in this function.
  // Use RetainedNodes so we can collect all argument names
  // even if the argument is not used.
  std::unordered_map<uint32_t, StringRef> FuncArgNames;
  for (const DINode *DN : SP->getRetainedNodes()) {
    if (const auto *DV = dyn_cast<DILocalVariable>(DN)) {
      // Collect function arguments for subprogram func type.
      uint32_t Arg = DV->getArg();
      if (Arg) {
        visitTypeEntry(DV->getType());
        FuncArgNames[Arg] = DV->getName();
      }
    }
  }

  // Construct subprogram func proto type.
  uint32_t ProtoTypeId;
  visitSubroutineType(SP->getType(), true, FuncArgNames, ProtoTypeId);

  // Construct subprogram func type
  uint8_t Scope = SP->isLocalToUnit() ? BTF::FUNC_STATIC : BTF::FUNC_GLOBAL;
  uint32_t FuncTypeId = processDISubprogram(SP, ProtoTypeId, Scope);

  for (const auto &TypeEntry : TypeEntries)
    TypeEntry->completeType(*this);

  // Construct funcinfo and the first lineinfo for the function.
  MCSymbol *FuncLabel = Asm->getFunctionBegin();
  BTFFuncInfo FuncInfo;
  FuncInfo.Label = FuncLabel;
  FuncInfo.TypeId = FuncTypeId;
  if (FuncLabel->isInSection()) {
    MCSection &Section = FuncLabel->getSection();
    const MCSectionELF *SectionELF = dyn_cast<MCSectionELF>(&Section);
    assert(SectionELF && "Null section for Function Label");
    SecNameOff = addString(SectionELF->getName());
  } else {
    SecNameOff = addString(".text");
  }
  FuncInfoTable[SecNameOff].push_back(FuncInfo);
}

void BTFDebug::endFunctionImpl(const MachineFunction *MF) {
  SkipInstruction = false;
  LineInfoGenerated = false;
  SecNameOff = 0;
}

/// On-demand populate types as requested from abstract member
/// accessing or preserve debuginfo type.
unsigned BTFDebug::populateType(const DIType *Ty) {
  unsigned Id;
  visitTypeEntry(Ty, Id, false, false);
  for (const auto &TypeEntry : TypeEntries)
    TypeEntry->completeType(*this);
  return Id;
}

/// Generate a struct member field relocation.
void BTFDebug::generatePatchImmReloc(const MCSymbol *ORSym, uint32_t RootId,
                                     const GlobalVariable *GVar, bool IsAma) {
  BTFFieldReloc FieldReloc;
  FieldReloc.Label = ORSym;
  FieldReloc.TypeID = RootId;

  StringRef AccessPattern = GVar->getName();
  size_t FirstDollar = AccessPattern.find_first_of('$');
  if (IsAma) {
    size_t FirstColon = AccessPattern.find_first_of(':');
    size_t SecondColon = AccessPattern.find_first_of(':', FirstColon + 1);
    StringRef IndexPattern = AccessPattern.substr(FirstDollar + 1);
    StringRef RelocKindStr = AccessPattern.substr(FirstColon + 1,
        SecondColon - FirstColon);
    StringRef PatchImmStr = AccessPattern.substr(SecondColon + 1,
        FirstDollar - SecondColon);

    FieldReloc.OffsetNameOff = addString(IndexPattern);
    FieldReloc.RelocKind = std::stoull(std::string(RelocKindStr));
    PatchImms[GVar] = std::make_pair(std::stoll(std::string(PatchImmStr)),
                                     FieldReloc.RelocKind);
  } else {
    StringRef RelocStr = AccessPattern.substr(FirstDollar + 1);
    FieldReloc.OffsetNameOff = addString("0");
    FieldReloc.RelocKind = std::stoull(std::string(RelocStr));
    PatchImms[GVar] = std::make_pair(RootId, FieldReloc.RelocKind);
  }
  FieldRelocTable[SecNameOff].push_back(FieldReloc);
}

void BTFDebug::processGlobalValue(const MachineOperand &MO) {
  // check whether this is a candidate or not
  if (MO.isGlobal()) {
    const GlobalValue *GVal = MO.getGlobal();
    auto *GVar = dyn_cast<GlobalVariable>(GVal);
    if (!GVar) {
      // Not a global variable. Maybe an extern function reference.
      processFuncPrototypes(dyn_cast<Function>(GVal));
      return;
    }

    if (!GVar->hasAttribute(BPFCoreSharedInfo::AmaAttr) &&
        !GVar->hasAttribute(BPFCoreSharedInfo::TypeIdAttr))
      return;

    MCSymbol *ORSym = OS.getContext().createTempSymbol();
    OS.emitLabel(ORSym);

    MDNode *MDN = GVar->getMetadata(LLVMContext::MD_preserve_access_index);
    uint32_t RootId = populateType(dyn_cast<DIType>(MDN));
    generatePatchImmReloc(ORSym, RootId, GVar,
                          GVar->hasAttribute(BPFCoreSharedInfo::AmaAttr));
  }
}

void BTFDebug::beginInstruction(const MachineInstr *MI) {
  DebugHandlerBase::beginInstruction(MI);

  if (SkipInstruction || MI->isMetaInstruction() ||
      MI->getFlag(MachineInstr::FrameSetup))
    return;

  if (MI->isInlineAsm()) {
    // Count the number of register definitions to find the asm string.
    unsigned NumDefs = 0;
    while (true) {
      const MachineOperand &MO = MI->getOperand(NumDefs);
      if (MO.isReg() && MO.isDef()) {
        ++NumDefs;
        continue;
      }
      // Skip this inline asm instruction if the asmstr is empty.
      const char *AsmStr = MO.getSymbolName();
      if (AsmStr[0] == 0)
        return;
      break;
    }
  }

  if (MI->getOpcode() == BPF::LD_imm64) {
    // If the insn is "r2 = LD_imm64 @<an AmaAttr global>",
    // add this insn into the .BTF.ext FieldReloc subsection.
    // Relocation looks like:
    //  . SecName:
    //    . InstOffset
    //    . TypeID
    //    . OffSetNameOff
    //    . RelocType
    // Later, the insn is replaced with "r2 = <offset>"
    // where "<offset>" equals to the offset based on current
    // type definitions.
    //
    // If the insn is "r2 = LD_imm64 @<an TypeIdAttr global>",
    // The LD_imm64 result will be replaced with a btf type id.
    processGlobalValue(MI->getOperand(1));
  } else if (MI->getOpcode() == BPF::CORE_LD64 ||
             MI->getOpcode() == BPF::CORE_LD32 ||
             MI->getOpcode() == BPF::CORE_ST ||
             MI->getOpcode() == BPF::CORE_SHIFT) {
    // relocation insn is a load, store or shift insn.
    processGlobalValue(MI->getOperand(3));
  } else if (MI->getOpcode() == BPF::JAL) {
    // check extern function references
    const MachineOperand &MO = MI->getOperand(0);
    if (MO.isGlobal()) {
      processFuncPrototypes(dyn_cast<Function>(MO.getGlobal()));
    }
  }

  if (!CurMI) // no debug info
    return;

  // Skip this instruction if no DebugLoc, the DebugLoc
  // is the same as the previous instruction or Line is 0.
  const DebugLoc &DL = MI->getDebugLoc();
  if (!DL || PrevInstLoc == DL || DL.getLine() == 0) {
    // This instruction will be skipped, no LineInfo has
    // been generated, construct one based on function signature.
    if (LineInfoGenerated == false) {
      auto *S = MI->getMF()->getFunction().getSubprogram();
      if (!S)
        return;
      MCSymbol *FuncLabel = Asm->getFunctionBegin();
      constructLineInfo(FuncLabel, S->getFile(), S->getLine(), 0);
      LineInfoGenerated = true;
    }

    return;
  }

  // Create a temporary label to remember the insn for lineinfo.
  MCSymbol *LineSym = OS.getContext().createTempSymbol();
  OS.emitLabel(LineSym);

  // Construct the lineinfo.
  constructLineInfo(LineSym, DL->getFile(), DL.getLine(), DL.getCol());

  LineInfoGenerated = true;
  PrevInstLoc = DL;
}

void BTFDebug::processGlobals(bool ProcessingMapDef) {
  // Collect all types referenced by globals.
  const Module *M = MMI->getModule();
  for (const GlobalVariable &Global : M->globals()) {
    // Decide the section name.
    StringRef SecName;
    std::optional<SectionKind> GVKind;

    if (!Global.isDeclarationForLinker())
      GVKind = TargetLoweringObjectFile::getKindForGlobal(&Global, Asm->TM);

    if (Global.isDeclarationForLinker())
      SecName = Global.hasSection() ? Global.getSection() : "";
    else if (GVKind->isCommon())
      SecName = ".bss";
    else {
      TargetLoweringObjectFile *TLOF = Asm->TM.getObjFileLowering();
      MCSection *Sec = TLOF->SectionForGlobal(&Global, Asm->TM);
      SecName = Sec->getName();
    }

    if (ProcessingMapDef != SecName.starts_with(".maps"))
      continue;

    // Create a .rodata datasec if the global variable is an initialized
    // constant with private linkage and if it won't be in .rodata.str<#>
    // and .rodata.cst<#> sections.
    if (SecName == ".rodata" && Global.hasPrivateLinkage() &&
        DataSecEntries.find(std::string(SecName)) == DataSecEntries.end()) {
      // skip .rodata.str<#> and .rodata.cst<#> sections
      if (!GVKind->isMergeableCString() && !GVKind->isMergeableConst()) {
        DataSecEntries[std::string(SecName)] =
            std::make_unique<BTFKindDataSec>(Asm, std::string(SecName));
      }
    }

    SmallVector<DIGlobalVariableExpression *, 1> GVs;
    Global.getDebugInfo(GVs);

    // No type information, mostly internal, skip it.
    if (GVs.size() == 0)
      continue;

    uint32_t GVTypeId = 0;
    DIGlobalVariable *DIGlobal = nullptr;
    for (auto *GVE : GVs) {
      DIGlobal = GVE->getVariable();
      if (SecName.starts_with(".maps"))
        visitMapDefType(DIGlobal->getType(), GVTypeId);
      else
        visitTypeEntry(DIGlobal->getType(), GVTypeId, false, false);
      break;
    }

    // Only support the following globals:
    //  . static variables
    //  . non-static weak or non-weak global variables
    //  . weak or non-weak extern global variables
    // Whether DataSec is readonly or not can be found from corresponding ELF
    // section flags. Whether a BTF_KIND_VAR is a weak symbol or not
    // can be found from the corresponding ELF symbol table.
    auto Linkage = Global.getLinkage();
    if (Linkage != GlobalValue::InternalLinkage &&
        Linkage != GlobalValue::ExternalLinkage &&
        Linkage != GlobalValue::WeakAnyLinkage &&
        Linkage != GlobalValue::WeakODRLinkage &&
        Linkage != GlobalValue::ExternalWeakLinkage)
      continue;

    uint32_t GVarInfo;
    if (Linkage == GlobalValue::InternalLinkage) {
      GVarInfo = BTF::VAR_STATIC;
    } else if (Global.hasInitializer()) {
      GVarInfo = BTF::VAR_GLOBAL_ALLOCATED;
    } else {
      GVarInfo = BTF::VAR_GLOBAL_EXTERNAL;
    }

    auto VarEntry =
        std::make_unique<BTFKindVar>(Global.getName(), GVTypeId, GVarInfo);
    uint32_t VarId = addType(std::move(VarEntry));

    processDeclAnnotations(DIGlobal->getAnnotations(), VarId, -1);

    // An empty SecName means an extern variable without section attribute.
    if (SecName.empty())
      continue;

    // Find or create a DataSec
    if (DataSecEntries.find(std::string(SecName)) == DataSecEntries.end()) {
      DataSecEntries[std::string(SecName)] =
          std::make_unique<BTFKindDataSec>(Asm, std::string(SecName));
    }

    // Calculate symbol size
    const DataLayout &DL = Global.getDataLayout();
    uint32_t Size = DL.getTypeAllocSize(Global.getValueType());

    DataSecEntries[std::string(SecName)]->addDataSecEntry(VarId,
        Asm->getSymbol(&Global), Size);

    if (Global.hasInitializer())
      processGlobalInitializer(Global.getInitializer());
  }
}

/// Process global variable initializer in pursuit for function
/// pointers. Add discovered (extern) functions to BTF. Some (extern)
/// functions might have been missed otherwise. Every symbol needs BTF
/// info when linking with bpftool. Primary use case: "static"
/// initialization of BPF maps.
///
/// struct {
///   __uint(type, BPF_MAP_TYPE_PROG_ARRAY);
///   ...
/// } prog_map SEC(".maps") = { .values = { extern_func } };
///
void BTFDebug::processGlobalInitializer(const Constant *C) {
  if (auto *Fn = dyn_cast<Function>(C))
    processFuncPrototypes(Fn);
  if (auto *CA = dyn_cast<ConstantAggregate>(C)) {
    for (unsigned I = 0, N = CA->getNumOperands(); I < N; ++I)
      processGlobalInitializer(CA->getOperand(I));
  }
}

/// Emit proper patchable instructions.
bool BTFDebug::InstLower(const MachineInstr *MI, MCInst &OutMI) {
  if (MI->getOpcode() == BPF::LD_imm64) {
    const MachineOperand &MO = MI->getOperand(1);
    if (MO.isGlobal()) {
      const GlobalValue *GVal = MO.getGlobal();
      auto *GVar = dyn_cast<GlobalVariable>(GVal);
      if (GVar) {
        // Emit "mov ri, <imm>"
        int64_t Imm;
        uint32_t Reloc;
        if (GVar->hasAttribute(BPFCoreSharedInfo::AmaAttr) ||
            GVar->hasAttribute(BPFCoreSharedInfo::TypeIdAttr)) {
          Imm = PatchImms[GVar].first;
          Reloc = PatchImms[GVar].second;
        } else {
          return false;
        }

        if (Reloc == BTF::ENUM_VALUE_EXISTENCE || Reloc == BTF::ENUM_VALUE ||
            Reloc == BTF::BTF_TYPE_ID_LOCAL || Reloc == BTF::BTF_TYPE_ID_REMOTE)
          OutMI.setOpcode(BPF::LD_imm64);
        else
          OutMI.setOpcode(BPF::MOV_ri);
        OutMI.addOperand(MCOperand::createReg(MI->getOperand(0).getReg()));
        OutMI.addOperand(MCOperand::createImm(Imm));
        return true;
      }
    }
  } else if (MI->getOpcode() == BPF::CORE_LD64 ||
             MI->getOpcode() == BPF::CORE_LD32 ||
             MI->getOpcode() == BPF::CORE_ST ||
             MI->getOpcode() == BPF::CORE_SHIFT) {
    const MachineOperand &MO = MI->getOperand(3);
    if (MO.isGlobal()) {
      const GlobalValue *GVal = MO.getGlobal();
      auto *GVar = dyn_cast<GlobalVariable>(GVal);
      if (GVar && GVar->hasAttribute(BPFCoreSharedInfo::AmaAttr)) {
        uint32_t Imm = PatchImms[GVar].first;
        OutMI.setOpcode(MI->getOperand(1).getImm());
        if (MI->getOperand(0).isImm())
          OutMI.addOperand(MCOperand::createImm(MI->getOperand(0).getImm()));
        else
          OutMI.addOperand(MCOperand::createReg(MI->getOperand(0).getReg()));
        OutMI.addOperand(MCOperand::createReg(MI->getOperand(2).getReg()));
        OutMI.addOperand(MCOperand::createImm(Imm));
        return true;
      }
    }
  }
  return false;
}

void BTFDebug::processFuncPrototypes(const Function *F) {
  if (!F)
    return;

  const DISubprogram *SP = F->getSubprogram();
  if (!SP || SP->isDefinition())
    return;

  // Do not emit again if already emitted.
  if (!ProtoFunctions.insert(F).second)
    return;

  uint32_t ProtoTypeId;
  const std::unordered_map<uint32_t, StringRef> FuncArgNames;
  visitSubroutineType(SP->getType(), false, FuncArgNames, ProtoTypeId);
  uint32_t FuncId = processDISubprogram(SP, ProtoTypeId, BTF::FUNC_EXTERN);

  if (F->hasSection()) {
    StringRef SecName = F->getSection();

    if (DataSecEntries.find(std::string(SecName)) == DataSecEntries.end()) {
      DataSecEntries[std::string(SecName)] =
          std::make_unique<BTFKindDataSec>(Asm, std::string(SecName));
    }

    // We really don't know func size, set it to 0.
    DataSecEntries[std::string(SecName)]->addDataSecEntry(FuncId,
        Asm->getSymbol(F), 0);
  }
}

void BTFDebug::endModule() {
  // Collect MapDef globals if not collected yet.
  if (MapDefNotCollected) {
    processGlobals(true);
    MapDefNotCollected = false;
  }

  // Collect global types/variables except MapDef globals.
  processGlobals(false);

  for (auto &DataSec : DataSecEntries)
    addType(std::move(DataSec.second));

  // Fixups
  for (auto &Fixup : FixupDerivedTypes) {
    const DICompositeType *CTy = Fixup.first;
    StringRef TypeName = CTy->getName();
    bool IsUnion = CTy->getTag() == dwarf::DW_TAG_union_type;

    // Search through struct types
    uint32_t StructTypeId = 0;
    for (const auto &StructType : StructTypes) {
      if (StructType->getName() == TypeName) {
        StructTypeId = StructType->getId();
        break;
      }
    }

    if (StructTypeId == 0) {
      auto FwdTypeEntry = std::make_unique<BTFTypeFwd>(TypeName, IsUnion);
      StructTypeId = addType(std::move(FwdTypeEntry));
    }

    for (auto &TypeInfo : Fixup.second) {
      const DIDerivedType *DTy = TypeInfo.first;
      BTFTypeDerived *BDType = TypeInfo.second;

      int TmpTypeId = genBTFTypeTags(DTy, StructTypeId);
      if (TmpTypeId >= 0)
        BDType->setPointeeType(TmpTypeId);
      else
        BDType->setPointeeType(StructTypeId);
    }
  }

  // Complete BTF type cross refereences.
  for (const auto &TypeEntry : TypeEntries)
    TypeEntry->completeType(*this);

  // Emit BTF sections.
  emitBTFSection();
  emitBTFExtSection();
}

//===- RecordName.cpp ----------------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/RecordName.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolRecordMapping.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::codeview;

namespace {
class TypeNameComputer : public TypeVisitorCallbacks {
  /// The type collection.  Used to calculate names of nested types.
  TypeCollection &Types;
  TypeIndex CurrentTypeIndex = TypeIndex::None();

  /// Name of the current type. Only valid before visitTypeEnd.
  SmallString<256> Name;

public:
  explicit TypeNameComputer(TypeCollection &Types) : Types(Types) {}

  StringRef name() const { return Name; }

  /// Paired begin/end actions for all types. Receives all record data,
  /// including the fixed-length record prefix.
  Error visitTypeBegin(CVType &Record) override;
  Error visitTypeBegin(CVType &Record, TypeIndex Index) override;
  Error visitTypeEnd(CVType &Record) override;

#define TYPE_RECORD(EnumName, EnumVal, Name)                                   \
  Error visitKnownRecord(CVType &CVR, Name##Record &Record) override;
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#define MEMBER_RECORD(EnumName, EnumVal, Name)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
};
} // namespace

Error TypeNameComputer::visitTypeBegin(CVType &Record) {
  llvm_unreachable("Must call visitTypeBegin with a TypeIndex!");
  return Error::success();
}

Error TypeNameComputer::visitTypeBegin(CVType &Record, TypeIndex Index) {
  // Reset Name to the empty string. If the visitor sets it, we know it.
  Name = "";
  CurrentTypeIndex = Index;
  return Error::success();
}

Error TypeNameComputer::visitTypeEnd(CVType &CVR) { return Error::success(); }

Error TypeNameComputer::visitKnownRecord(CVType &CVR,
                                         FieldListRecord &FieldList) {
  Name = "<field list>";
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVRecord<TypeLeafKind> &CVR,
                                         StringIdRecord &String) {
  Name = String.getString();
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, ArgListRecord &Args) {
  auto Indices = Args.getIndices();
  uint32_t Size = Indices.size();
  Name = "(";
  for (uint32_t I = 0; I < Size; ++I) {
    if (Indices[I] < CurrentTypeIndex)
      Name.append(Types.getTypeName(Indices[I]));
    else
      Name.append("<unknown 0x" + utohexstr(Indices[I].getIndex()) + ">");
    if (I + 1 != Size)
      Name.append(", ");
  }
  Name.push_back(')');
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR,
                                         StringListRecord &Strings) {
  auto Indices = Strings.getIndices();
  uint32_t Size = Indices.size();
  Name = "\"";
  for (uint32_t I = 0; I < Size; ++I) {
    Name.append(Types.getTypeName(Indices[I]));
    if (I + 1 != Size)
      Name.append("\" \"");
  }
  Name.push_back('\"');
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, ClassRecord &Class) {
  Name = Class.getName();
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, UnionRecord &Union) {
  Name = Union.getName();
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, EnumRecord &Enum) {
  Name = Enum.getName();
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, ArrayRecord &AT) {
  Name = AT.getName();
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, VFTableRecord &VFT) {
  Name = VFT.getName();
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, MemberFuncIdRecord &Id) {
  Name = Id.getName();
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, ProcedureRecord &Proc) {
  StringRef Ret = Types.getTypeName(Proc.getReturnType());
  StringRef Params = Types.getTypeName(Proc.getArgumentList());
  Name = formatv("{0} {1}", Ret, Params).sstr<256>();
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR,
                                         MemberFunctionRecord &MF) {
  StringRef Ret = Types.getTypeName(MF.getReturnType());
  StringRef Class = Types.getTypeName(MF.getClassType());
  StringRef Params = Types.getTypeName(MF.getArgumentList());
  Name = formatv("{0} {1}::{2}", Ret, Class, Params).sstr<256>();
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, FuncIdRecord &Func) {
  Name = Func.getName();
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, TypeServer2Record &TS) {
  Name = TS.getName();
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, PointerRecord &Ptr) {

  if (Ptr.isPointerToMember()) {
    const MemberPointerInfo &MI = Ptr.getMemberInfo();

    StringRef Pointee = Types.getTypeName(Ptr.getReferentType());
    StringRef Class = Types.getTypeName(MI.getContainingType());
    Name = formatv("{0} {1}::*", Pointee, Class);
  } else {
    Name.append(Types.getTypeName(Ptr.getReferentType()));

    if (Ptr.getMode() == PointerMode::LValueReference)
      Name.append("&");
    else if (Ptr.getMode() == PointerMode::RValueReference)
      Name.append("&&");
    else if (Ptr.getMode() == PointerMode::Pointer)
      Name.append("*");

    // Qualifiers in pointer records apply to the pointer, not the pointee, so
    // they go on the right.
    if (Ptr.isConst())
      Name.append(" const");
    if (Ptr.isVolatile())
      Name.append(" volatile");
    if (Ptr.isUnaligned())
      Name.append(" __unaligned");
    if (Ptr.isRestrict())
      Name.append(" __restrict");
  }
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, ModifierRecord &Mod) {
  uint16_t Mods = static_cast<uint16_t>(Mod.getModifiers());

  if (Mods & uint16_t(ModifierOptions::Const))
    Name.append("const ");
  if (Mods & uint16_t(ModifierOptions::Volatile))
    Name.append("volatile ");
  if (Mods & uint16_t(ModifierOptions::Unaligned))
    Name.append("__unaligned ");
  Name.append(Types.getTypeName(Mod.getModifiedType()));
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR,
                                         VFTableShapeRecord &Shape) {
  Name = formatv("<vftable {0} methods>", Shape.getEntryCount());
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(
    CVType &CVR, UdtModSourceLineRecord &ModSourceLine) {
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR,
                                         UdtSourceLineRecord &SourceLine) {
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, BitFieldRecord &BF) {
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR,
                                         MethodOverloadListRecord &Overloads) {
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, BuildInfoRecord &BI) {
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR, LabelRecord &R) {
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR,
                                         PrecompRecord &Precomp) {
  return Error::success();
}

Error TypeNameComputer::visitKnownRecord(CVType &CVR,
                                         EndPrecompRecord &EndPrecomp) {
  return Error::success();
}

std::string llvm::codeview::computeTypeName(TypeCollection &Types,
                                            TypeIndex Index) {
  TypeNameComputer Computer(Types);
  CVType Record = Types.getType(Index);
  if (auto EC = visitTypeRecord(Record, Index, Computer)) {
    consumeError(std::move(EC));
    return "<unknown UDT>";
  }
  return std::string(Computer.name());
}

static int getSymbolNameOffset(CVSymbol Sym) {
  switch (Sym.kind()) {
  // See ProcSym
  case SymbolKind::S_GPROC32:
  case SymbolKind::S_LPROC32:
  case SymbolKind::S_GPROC32_ID:
  case SymbolKind::S_LPROC32_ID:
  case SymbolKind::S_LPROC32_DPC:
  case SymbolKind::S_LPROC32_DPC_ID:
    return 35;
  // See Thunk32Sym
  case SymbolKind::S_THUNK32:
    return 21;
  // See SectionSym
  case SymbolKind::S_SECTION:
    return 16;
  // See CoffGroupSym
  case SymbolKind::S_COFFGROUP:
    return 14;
  // See PublicSym32, FileStaticSym, RegRelativeSym, DataSym, ThreadLocalDataSym
  case SymbolKind::S_PUB32:
  case SymbolKind::S_FILESTATIC:
  case SymbolKind::S_REGREL32:
  case SymbolKind::S_GDATA32:
  case SymbolKind::S_LDATA32:
  case SymbolKind::S_LMANDATA:
  case SymbolKind::S_GMANDATA:
  case SymbolKind::S_LTHREAD32:
  case SymbolKind::S_GTHREAD32:
  case SymbolKind::S_PROCREF:
  case SymbolKind::S_LPROCREF:
    return 10;
  // See RegisterSym and LocalSym
  case SymbolKind::S_REGISTER:
  case SymbolKind::S_LOCAL:
    return 6;
  // See BlockSym
  case SymbolKind::S_BLOCK32:
    return 18;
  // See LabelSym
  case SymbolKind::S_LABEL32:
    return 7;
  // See ObjNameSym, ExportSym, and UDTSym
  case SymbolKind::S_OBJNAME:
  case SymbolKind::S_EXPORT:
  case SymbolKind::S_UDT:
    return 4;
  // See BPRelativeSym
  case SymbolKind::S_BPREL32:
    return 8;
  // See UsingNamespaceSym
  case SymbolKind::S_UNAMESPACE:
    return 0;
  default:
    return -1;
  }
}

StringRef llvm::codeview::getSymbolName(CVSymbol Sym) {
  if (Sym.kind() == SymbolKind::S_CONSTANT) {
    // S_CONSTANT is preceded by an APSInt, which has a variable length.  So we
    // have to do a full deserialization.
    BinaryStreamReader Reader(Sym.content(), llvm::endianness::little);
    // The container doesn't matter for single records.
    SymbolRecordMapping Mapping(Reader, CodeViewContainer::ObjectFile);
    ConstantSym Const(SymbolKind::S_CONSTANT);
    cantFail(Mapping.visitSymbolBegin(Sym));
    cantFail(Mapping.visitKnownRecord(Sym, Const));
    cantFail(Mapping.visitSymbolEnd(Sym));
    return Const.Name;
  }

  int Offset = getSymbolNameOffset(Sym);
  if (Offset == -1)
    return StringRef();

  StringRef StringData = toStringRef(Sym.content()).drop_front(Offset);
  return StringData.split('\0').first;
}

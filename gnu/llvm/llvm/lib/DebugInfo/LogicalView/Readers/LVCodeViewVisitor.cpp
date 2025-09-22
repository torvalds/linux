//===-- LVCodeViewVisitor.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVCodeViewVisitor class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Readers/LVCodeViewVisitor.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/DebugInfo/CodeView/EnumTables.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/SymbolRecordHelpers.h"
#include "llvm/DebugInfo/CodeView/TypeRecordHelpers.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbackPipeline.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSymbol.h"
#include "llvm/DebugInfo/LogicalView/Core/LVType.h"
#include "llvm/DebugInfo/LogicalView/Readers/LVCodeViewReader.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/InputFile.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTable.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/PDB.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::object;
using namespace llvm::pdb;
using namespace llvm::logicalview;

#define DEBUG_TYPE "CodeViewUtilities"

namespace llvm {
namespace logicalview {

static TypeIndex getTrueType(TypeIndex &TI) {
  // Dealing with a MSVC generated PDB, we encountered a type index with the
  // value of: 0x0280xxxx where xxxx=0000.
  //
  // There is some documentation about type indices:
  // https://llvm.org/docs/PDB/TpiStream.html
  //
  // A type index is a 32-bit integer that uniquely identifies a type inside
  // of an object file’s .debug$T section or a PDB file’s TPI or IPI stream.
  // The value of the type index for the first type record from the TPI stream
  // is given by the TypeIndexBegin member of the TPI Stream Header although
  // in practice this value is always equal to 0x1000 (4096).
  //
  // Any type index with a high bit set is considered to come from the IPI
  // stream, although this appears to be more of a hack, and LLVM does not
  // generate type indices of this nature. They can, however, be observed in
  // Microsoft PDBs occasionally, so one should be prepared to handle them.
  // Note that having the high bit set is not a necessary condition to
  // determine whether a type index comes from the IPI stream, it is only
  // sufficient.
  LLVM_DEBUG(
      { dbgs() << "Index before: " << HexNumber(TI.getIndex()) << "\n"; });
  TI.setIndex(TI.getIndex() & 0x0000ffff);
  LLVM_DEBUG(
      { dbgs() << "Index after: " << HexNumber(TI.getIndex()) << "\n"; });
  return TI;
}

static const EnumEntry<TypeLeafKind> LeafTypeNames[] = {
#define CV_TYPE(enum, val) {#enum, enum},
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
};

// Return the type name pointed by the type index. It uses the kind to query
// the associated name for the record type.
static StringRef getRecordName(LazyRandomTypeCollection &Types, TypeIndex TI) {
  if (TI.isSimple())
    return {};

  StringRef RecordName;
  CVType CVReference = Types.getType(TI);
  auto GetName = [&](auto Record) {
    if (Error Err = TypeDeserializer::deserializeAs(
            const_cast<CVType &>(CVReference), Record))
      consumeError(std::move(Err));
    else
      RecordName = Record.getName();
  };

  TypeRecordKind RK = static_cast<TypeRecordKind>(CVReference.kind());
  if (RK == TypeRecordKind::Class || RK == TypeRecordKind::Struct)
    GetName(ClassRecord(RK));
  else if (RK == TypeRecordKind::Union)
    GetName(UnionRecord(RK));
  else if (RK == TypeRecordKind::Enum)
    GetName(EnumRecord(RK));

  return RecordName;
}

} // namespace logicalview
} // namespace llvm

#undef DEBUG_TYPE
#define DEBUG_TYPE "CodeViewDataVisitor"

namespace llvm {
namespace logicalview {

// Keeps the type indexes with line information.
using LVLineRecords = std::vector<TypeIndex>;

namespace {

class LVTypeRecords {
  LVShared *Shared = nullptr;

  // Logical elements associated to their CodeView Type Index.
  using RecordEntry = std::pair<TypeLeafKind, LVElement *>;
  using RecordTable = std::map<TypeIndex, RecordEntry>;
  RecordTable RecordFromTypes;
  RecordTable RecordFromIds;

  using NameTable = std::map<StringRef, TypeIndex>;
  NameTable NameFromTypes;
  NameTable NameFromIds;

public:
  LVTypeRecords(LVShared *Shared) : Shared(Shared) {}

  void add(uint32_t StreamIdx, TypeIndex TI, TypeLeafKind Kind,
           LVElement *Element = nullptr);
  void add(uint32_t StreamIdx, TypeIndex TI, StringRef Name);
  LVElement *find(uint32_t StreamIdx, TypeIndex TI, bool Create = true);
  TypeIndex find(uint32_t StreamIdx, StringRef Name);
};

class LVForwardReferences {
  // Forward reference and its definitions (Name as key).
  using ForwardEntry = std::pair<TypeIndex, TypeIndex>;
  using ForwardTypeNames = std::map<StringRef, ForwardEntry>;
  ForwardTypeNames ForwardTypesNames;

  // Forward reference and its definition (TypeIndex as key).
  using ForwardType = std::map<TypeIndex, TypeIndex>;
  ForwardType ForwardTypes;

  // Forward types and its references.
  void add(TypeIndex TIForward, TypeIndex TIReference) {
    ForwardTypes.emplace(TIForward, TIReference);
  }

  void add(StringRef Name, TypeIndex TIForward) {
    if (ForwardTypesNames.find(Name) == ForwardTypesNames.end()) {
      ForwardTypesNames.emplace(
          std::piecewise_construct, std::forward_as_tuple(Name),
          std::forward_as_tuple(TIForward, TypeIndex::None()));
    } else {
      // Update a recorded definition with its reference.
      ForwardTypesNames[Name].first = TIForward;
      add(TIForward, ForwardTypesNames[Name].second);
    }
  }

  // Update a previously recorded forward reference with its definition.
  void update(StringRef Name, TypeIndex TIReference) {
    if (ForwardTypesNames.find(Name) != ForwardTypesNames.end()) {
      // Update the recorded forward reference with its definition.
      ForwardTypesNames[Name].second = TIReference;
      add(ForwardTypesNames[Name].first, TIReference);
    } else {
      // We have not seen the forward reference. Insert the definition.
      ForwardTypesNames.emplace(
          std::piecewise_construct, std::forward_as_tuple(Name),
          std::forward_as_tuple(TypeIndex::None(), TIReference));
    }
  }

public:
  LVForwardReferences() = default;

  void record(bool IsForwardRef, StringRef Name, TypeIndex TI) {
    // We are expecting for the forward references to be first. But that
    // is not always the case. A name must be recorded regardless of the
    // order in which the forward reference appears.
    (IsForwardRef) ? add(Name, TI) : update(Name, TI);
  }

  TypeIndex find(TypeIndex TIForward) {
    return (ForwardTypes.find(TIForward) != ForwardTypes.end())
               ? ForwardTypes[TIForward]
               : TypeIndex::None();
  }

  TypeIndex find(StringRef Name) {
    return (ForwardTypesNames.find(Name) != ForwardTypesNames.end())
               ? ForwardTypesNames[Name].second
               : TypeIndex::None();
  }

  // If the given TI corresponds to a reference, return the reference.
  // Otherwise return the given TI.
  TypeIndex remap(TypeIndex TI) {
    TypeIndex Forward = find(TI);
    return Forward.isNoneType() ? TI : Forward;
  }
};

// Namespace deduction.
class LVNamespaceDeduction {
  LVShared *Shared = nullptr;

  using Names = std::map<StringRef, LVScope *>;
  Names NamespaceNames;

  using LookupSet = std::set<StringRef>;
  LookupSet DeducedScopes;
  LookupSet UnresolvedScopes;
  LookupSet IdentifiedNamespaces;

  void add(StringRef Name, LVScope *Namespace) {
    if (NamespaceNames.find(Name) == NamespaceNames.end())
      NamespaceNames.emplace(Name, Namespace);
  }

public:
  LVNamespaceDeduction(LVShared *Shared) : Shared(Shared) {}

  void init();
  void add(StringRef String);
  LVScope *get(LVStringRefs Components);
  LVScope *get(StringRef Name, bool CheckScope = true);

  // Find the logical namespace for the 'Name' component.
  LVScope *find(StringRef Name) {
    LVScope *Namespace = (NamespaceNames.find(Name) != NamespaceNames.end())
                             ? NamespaceNames[Name]
                             : nullptr;
    return Namespace;
  }

  // For the given lexical components, return a tuple with the first entry
  // being the outermost namespace and the second entry being the first
  // non-namespace.
  LVLexicalIndex find(LVStringRefs Components) {
    if (Components.empty())
      return {};

    LVStringRefs::size_type FirstNamespace = 0;
    LVStringRefs::size_type FirstNonNamespace;
    for (LVStringRefs::size_type Index = 0; Index < Components.size();
         ++Index) {
      FirstNonNamespace = Index;
      LookupSet::iterator Iter = IdentifiedNamespaces.find(Components[Index]);
      if (Iter == IdentifiedNamespaces.end())
        // The component is not a namespace name.
        break;
    }
    return std::make_tuple(FirstNamespace, FirstNonNamespace);
  }
};

// Strings.
class LVStringRecords {
  using StringEntry = std::tuple<uint32_t, std::string, LVScopeCompileUnit *>;
  using StringIds = std::map<TypeIndex, StringEntry>;
  StringIds Strings;

public:
  LVStringRecords() = default;

  void add(TypeIndex TI, StringRef String) {
    static uint32_t Index = 0;
    if (Strings.find(TI) == Strings.end())
      Strings.emplace(
          std::piecewise_construct, std::forward_as_tuple(TI),
          std::forward_as_tuple(++Index, std::string(String), nullptr));
  }

  StringRef find(TypeIndex TI) {
    StringIds::iterator Iter = Strings.find(TI);
    return Iter != Strings.end() ? std::get<1>(Iter->second) : StringRef{};
  }

  uint32_t findIndex(TypeIndex TI) {
    StringIds::iterator Iter = Strings.find(TI);
    return Iter != Strings.end() ? std::get<0>(Iter->second) : 0;
  }

  // Move strings representing the filenames to the compile unit.
  void addFilenames();
  void addFilenames(LVScopeCompileUnit *Scope);
};
} // namespace

using LVTypeKinds = std::set<TypeLeafKind>;
using LVSymbolKinds = std::set<SymbolKind>;

// The following data keeps forward information, type records, names for
// namespace deduction, strings records, line records.
// It is shared by the type visitor, symbol visitor and logical visitor and
// it is independent from the CodeViewReader.
struct LVShared {
  LVCodeViewReader *Reader;
  LVLogicalVisitor *Visitor;
  LVForwardReferences ForwardReferences;
  LVLineRecords LineRecords;
  LVNamespaceDeduction NamespaceDeduction;
  LVStringRecords StringRecords;
  LVTypeRecords TypeRecords;

  // In order to determine which types and/or symbols records should be handled
  // by the reader, we record record kinds seen by the type and symbol visitors.
  // At the end of the scopes creation, the '--internal=tag' option will allow
  // to print the unique record ids collected.
  LVTypeKinds TypeKinds;
  LVSymbolKinds SymbolKinds;

  LVShared(LVCodeViewReader *Reader, LVLogicalVisitor *Visitor)
      : Reader(Reader), Visitor(Visitor), NamespaceDeduction(this),
        TypeRecords(this) {}
  ~LVShared() = default;
};
} // namespace logicalview
} // namespace llvm

void LVTypeRecords::add(uint32_t StreamIdx, TypeIndex TI, TypeLeafKind Kind,
                        LVElement *Element) {
  RecordTable &Target =
      (StreamIdx == StreamTPI) ? RecordFromTypes : RecordFromIds;
  Target.emplace(std::piecewise_construct, std::forward_as_tuple(TI),
                 std::forward_as_tuple(Kind, Element));
}

void LVTypeRecords::add(uint32_t StreamIdx, TypeIndex TI, StringRef Name) {
  NameTable &Target = (StreamIdx == StreamTPI) ? NameFromTypes : NameFromIds;
  Target.emplace(Name, TI);
}

LVElement *LVTypeRecords::find(uint32_t StreamIdx, TypeIndex TI, bool Create) {
  RecordTable &Target =
      (StreamIdx == StreamTPI) ? RecordFromTypes : RecordFromIds;

  LVElement *Element = nullptr;
  RecordTable::iterator Iter = Target.find(TI);
  if (Iter != Target.end()) {
    Element = Iter->second.second;
    if (Element || !Create)
      return Element;

    // Create the logical element if not found.
    Element = Shared->Visitor->createElement(Iter->second.first);
    if (Element) {
      Element->setOffset(TI.getIndex());
      Element->setOffsetFromTypeIndex();
      Target[TI].second = Element;
    }
  }
  return Element;
}

TypeIndex LVTypeRecords::find(uint32_t StreamIdx, StringRef Name) {
  NameTable &Target = (StreamIdx == StreamTPI) ? NameFromTypes : NameFromIds;
  NameTable::iterator Iter = Target.find(Name);
  return Iter != Target.end() ? Iter->second : TypeIndex::None();
}

void LVStringRecords::addFilenames() {
  for (StringIds::const_reference Entry : Strings) {
    StringRef Name = std::get<1>(Entry.second);
    LVScopeCompileUnit *Scope = std::get<2>(Entry.second);
    Scope->addFilename(transformPath(Name));
  }
  Strings.clear();
}

void LVStringRecords::addFilenames(LVScopeCompileUnit *Scope) {
  for (StringIds::reference Entry : Strings)
    if (!std::get<2>(Entry.second))
      std::get<2>(Entry.second) = Scope;
}

void LVNamespaceDeduction::add(StringRef String) {
  StringRef InnerComponent;
  StringRef OuterComponent;
  std::tie(OuterComponent, InnerComponent) = getInnerComponent(String);
  DeducedScopes.insert(InnerComponent);
  if (OuterComponent.size())
    UnresolvedScopes.insert(OuterComponent);
}

void LVNamespaceDeduction::init() {
  // We have 2 sets of names:
  // - deduced scopes (class, structure, union and enum) and
  // - unresolved scopes, that can represent namespaces or any deduced.
  // Before creating the namespaces, we have to traverse the unresolved
  // and remove any references to already deduced scopes.
  LVStringRefs Components;
  for (const StringRef &Unresolved : UnresolvedScopes) {
    Components = getAllLexicalComponents(Unresolved);
    for (const StringRef &Component : Components) {
      LookupSet::iterator Iter = DeducedScopes.find(Component);
      if (Iter == DeducedScopes.end())
        IdentifiedNamespaces.insert(Component);
    }
  }

  LLVM_DEBUG({
    auto Print = [&](LookupSet &Container, const char *Title) {
      auto Header = [&]() {
        dbgs() << formatv("\n{0}\n", fmt_repeat('=', 72));
        dbgs() << formatv("{0}\n", Title);
        dbgs() << formatv("{0}\n", fmt_repeat('=', 72));
      };
      Header();
      for (const StringRef &Item : Container)
        dbgs() << formatv("'{0}'\n", Item.str().c_str());
    };

    Print(DeducedScopes, "Deducted Scopes");
    Print(UnresolvedScopes, "Unresolved Scopes");
    Print(IdentifiedNamespaces, "Namespaces");
  });
}

LVScope *LVNamespaceDeduction::get(LVStringRefs Components) {
  LLVM_DEBUG({
    for (const StringRef &Component : Components)
      dbgs() << formatv("'{0}'\n", Component.str().c_str());
  });

  if (Components.empty())
    return nullptr;

  // Update the namespaces relationship.
  LVScope *Namespace = nullptr;
  LVScope *Parent = Shared->Reader->getCompileUnit();
  for (const StringRef &Component : Components) {
    // Check if we have seen the namespace.
    Namespace = find(Component);
    if (!Namespace) {
      // We have identified namespaces that are generated by MSVC. Mark them
      // as 'system' so they will be excluded from the logical view.
      Namespace = Shared->Reader->createScopeNamespace();
      Namespace->setTag(dwarf::DW_TAG_namespace);
      Namespace->setName(Component);
      Parent->addElement(Namespace);
      getReader().isSystemEntry(Namespace);
      add(Component, Namespace);
    }
    Parent = Namespace;
  }
  return Parent;
}

LVScope *LVNamespaceDeduction::get(StringRef ScopedName, bool CheckScope) {
  LVStringRefs Components = getAllLexicalComponents(ScopedName);
  if (CheckScope)
    llvm::erase_if(Components, [&](StringRef Component) {
      LookupSet::iterator Iter = IdentifiedNamespaces.find(Component);
      return Iter == IdentifiedNamespaces.end();
    });

  LLVM_DEBUG(
      { dbgs() << formatv("ScopedName: '{0}'\n", ScopedName.str().c_str()); });

  return get(Components);
}

#undef DEBUG_TYPE
#define DEBUG_TYPE "CodeViewTypeVisitor"

//===----------------------------------------------------------------------===//
// TypeRecord traversal.
//===----------------------------------------------------------------------===//
void LVTypeVisitor::printTypeIndex(StringRef FieldName, TypeIndex TI,
                                   uint32_t StreamIdx) const {
  codeview::printTypeIndex(W, FieldName, TI,
                           StreamIdx == StreamTPI ? Types : Ids);
}

Error LVTypeVisitor::visitTypeBegin(CVType &Record) {
  return visitTypeBegin(Record, TypeIndex::fromArrayIndex(Types.size()));
}

Error LVTypeVisitor::visitTypeBegin(CVType &Record, TypeIndex TI) {
  LLVM_DEBUG({
    W.getOStream() << formatTypeLeafKind(Record.kind());
    W.getOStream() << " (" << HexNumber(TI.getIndex()) << ")\n";
  });

  if (options().getInternalTag())
    Shared->TypeKinds.insert(Record.kind());

  // The collected type records, will be use to create the logical elements
  // during the symbols traversal when a type is referenced.
  CurrentTypeIndex = TI;
  Shared->TypeRecords.add(StreamIdx, TI, Record.kind());
  return Error::success();
}

Error LVTypeVisitor::visitUnknownType(CVType &Record) {
  LLVM_DEBUG({ W.printNumber("Length", uint32_t(Record.content().size())); });
  return Error::success();
}

Error LVTypeVisitor::visitMemberBegin(CVMemberRecord &Record) {
  LLVM_DEBUG({
    W.startLine() << formatTypeLeafKind(Record.Kind);
    W.getOStream() << " {\n";
    W.indent();
  });
  return Error::success();
}

Error LVTypeVisitor::visitMemberEnd(CVMemberRecord &Record) {
  LLVM_DEBUG({
    W.unindent();
    W.startLine() << "}\n";
  });
  return Error::success();
}

Error LVTypeVisitor::visitUnknownMember(CVMemberRecord &Record) {
  LLVM_DEBUG({ W.printHex("UnknownMember", unsigned(Record.Kind)); });
  return Error::success();
}

// LF_BUILDINFO (TPI)/(IPI)
Error LVTypeVisitor::visitKnownRecord(CVType &Record, BuildInfoRecord &Args) {
  // All the args are references into the TPI/IPI stream.
  LLVM_DEBUG({
    W.printNumber("NumArgs", static_cast<uint32_t>(Args.getArgs().size()));
    ListScope Arguments(W, "Arguments");
    for (TypeIndex Arg : Args.getArgs())
      printTypeIndex("ArgType", Arg, StreamIPI);
  });

  // Only add the strings that hold information about filenames. They will be
  // used to complete the line/file information for the logical elements.
  // There are other strings holding information about namespaces.
  TypeIndex TI;
  StringRef String;

  // Absolute CWD path
  TI = Args.getArgs()[BuildInfoRecord::BuildInfoArg::CurrentDirectory];
  String = Ids.getTypeName(TI);
  if (!String.empty())
    Shared->StringRecords.add(TI, String);

  // Get the compile unit name.
  TI = Args.getArgs()[BuildInfoRecord::BuildInfoArg::SourceFile];
  String = Ids.getTypeName(TI);
  if (!String.empty())
    Shared->StringRecords.add(TI, String);
  LogicalVisitor->setCompileUnitName(std::string(String));

  return Error::success();
}

// LF_CLASS, LF_STRUCTURE, LF_INTERFACE (TPI)
Error LVTypeVisitor::visitKnownRecord(CVType &Record, ClassRecord &Class) {
  LLVM_DEBUG({
    printTypeIndex("TypeIndex", CurrentTypeIndex, StreamTPI);
    printTypeIndex("FieldListType", Class.getFieldList(), StreamTPI);
    W.printString("Name", Class.getName());
  });

  // Collect class name for scope deduction.
  Shared->NamespaceDeduction.add(Class.getName());
  Shared->ForwardReferences.record(Class.isForwardRef(), Class.getName(),
                                   CurrentTypeIndex);

  // Collect class name for contained scopes deduction.
  Shared->TypeRecords.add(StreamIdx, CurrentTypeIndex, Class.getName());
  return Error::success();
}

// LF_ENUM (TPI)
Error LVTypeVisitor::visitKnownRecord(CVType &Record, EnumRecord &Enum) {
  LLVM_DEBUG({
    printTypeIndex("TypeIndex", CurrentTypeIndex, StreamTPI);
    printTypeIndex("FieldListType", Enum.getFieldList(), StreamTPI);
    W.printString("Name", Enum.getName());
  });

  // Collect enum name for scope deduction.
  Shared->NamespaceDeduction.add(Enum.getName());
  return Error::success();
}

// LF_FUNC_ID (TPI)/(IPI)
Error LVTypeVisitor::visitKnownRecord(CVType &Record, FuncIdRecord &Func) {
  LLVM_DEBUG({
    printTypeIndex("TypeIndex", CurrentTypeIndex, StreamTPI);
    printTypeIndex("Type", Func.getFunctionType(), StreamTPI);
    printTypeIndex("Parent", Func.getParentScope(), StreamTPI);
    W.printString("Name", Func.getName());
  });

  // Collect function name for scope deduction.
  Shared->NamespaceDeduction.add(Func.getName());
  return Error::success();
}

// LF_PROCEDURE (TPI)
Error LVTypeVisitor::visitKnownRecord(CVType &Record, ProcedureRecord &Proc) {
  LLVM_DEBUG({
    printTypeIndex("TypeIndex", CurrentTypeIndex, StreamTPI);
    printTypeIndex("ReturnType", Proc.getReturnType(), StreamTPI);
    W.printNumber("NumParameters", Proc.getParameterCount());
    printTypeIndex("ArgListType", Proc.getArgumentList(), StreamTPI);
  });

  // Collect procedure information as they can be referenced by typedefs.
  Shared->TypeRecords.add(StreamTPI, CurrentTypeIndex, {});
  return Error::success();
}

// LF_STRING_ID (TPI)/(IPI)
Error LVTypeVisitor::visitKnownRecord(CVType &Record, StringIdRecord &String) {
  // No additional references are needed.
  LLVM_DEBUG({
    printTypeIndex("Id", String.getId(), StreamIPI);
    W.printString("StringData", String.getString());
  });
  return Error::success();
}

// LF_UDT_SRC_LINE (TPI)/(IPI)
Error LVTypeVisitor::visitKnownRecord(CVType &Record,
                                      UdtSourceLineRecord &Line) {
  // UDT and SourceFile are references into the TPI/IPI stream.
  LLVM_DEBUG({
    printTypeIndex("UDT", Line.getUDT(), StreamIPI);
    printTypeIndex("SourceFile", Line.getSourceFile(), StreamIPI);
    W.printNumber("LineNumber", Line.getLineNumber());
  });

  Shared->LineRecords.push_back(CurrentTypeIndex);
  return Error::success();
}

// LF_UNION (TPI)
Error LVTypeVisitor::visitKnownRecord(CVType &Record, UnionRecord &Union) {
  LLVM_DEBUG({
    W.printNumber("MemberCount", Union.getMemberCount());
    printTypeIndex("FieldList", Union.getFieldList(), StreamTPI);
    W.printNumber("SizeOf", Union.getSize());
    W.printString("Name", Union.getName());
    if (Union.hasUniqueName())
      W.printString("UniqueName", Union.getUniqueName());
  });

  // Collect union name for scope deduction.
  Shared->NamespaceDeduction.add(Union.getName());
  Shared->ForwardReferences.record(Union.isForwardRef(), Union.getName(),
                                   CurrentTypeIndex);

  // Collect class name for contained scopes deduction.
  Shared->TypeRecords.add(StreamIdx, CurrentTypeIndex, Union.getName());
  return Error::success();
}

#undef DEBUG_TYPE
#define DEBUG_TYPE "CodeViewSymbolVisitor"

//===----------------------------------------------------------------------===//
// SymbolRecord traversal.
//===----------------------------------------------------------------------===//
void LVSymbolVisitorDelegate::printRelocatedField(StringRef Label,
                                                  uint32_t RelocOffset,
                                                  uint32_t Offset,
                                                  StringRef *RelocSym) {
  Reader->printRelocatedField(Label, CoffSection, RelocOffset, Offset,
                              RelocSym);
}

void LVSymbolVisitorDelegate::getLinkageName(uint32_t RelocOffset,
                                             uint32_t Offset,
                                             StringRef *RelocSym) {
  Reader->getLinkageName(CoffSection, RelocOffset, Offset, RelocSym);
}

StringRef
LVSymbolVisitorDelegate::getFileNameForFileOffset(uint32_t FileOffset) {
  Expected<StringRef> Name = Reader->getFileNameForFileOffset(FileOffset);
  if (!Name) {
    consumeError(Name.takeError());
    return {};
  }
  return *Name;
}

DebugStringTableSubsectionRef LVSymbolVisitorDelegate::getStringTable() {
  return Reader->CVStringTable;
}

void LVSymbolVisitor::printLocalVariableAddrRange(
    const LocalVariableAddrRange &Range, uint32_t RelocationOffset) {
  DictScope S(W, "LocalVariableAddrRange");
  if (ObjDelegate)
    ObjDelegate->printRelocatedField("OffsetStart", RelocationOffset,
                                     Range.OffsetStart);
  W.printHex("ISectStart", Range.ISectStart);
  W.printHex("Range", Range.Range);
}

void LVSymbolVisitor::printLocalVariableAddrGap(
    ArrayRef<LocalVariableAddrGap> Gaps) {
  for (const LocalVariableAddrGap &Gap : Gaps) {
    ListScope S(W, "LocalVariableAddrGap");
    W.printHex("GapStartOffset", Gap.GapStartOffset);
    W.printHex("Range", Gap.Range);
  }
}

void LVSymbolVisitor::printTypeIndex(StringRef FieldName, TypeIndex TI) const {
  codeview::printTypeIndex(W, FieldName, TI, Types);
}

Error LVSymbolVisitor::visitSymbolBegin(CVSymbol &Record) {
  return visitSymbolBegin(Record, 0);
}

Error LVSymbolVisitor::visitSymbolBegin(CVSymbol &Record, uint32_t Offset) {
  SymbolKind Kind = Record.kind();
  LLVM_DEBUG({
    W.printNumber("Offset", Offset);
    W.printEnum("Begin Kind", unsigned(Kind), getSymbolTypeNames());
  });

  if (options().getInternalTag())
    Shared->SymbolKinds.insert(Kind);

  LogicalVisitor->CurrentElement = LogicalVisitor->createElement(Kind);
  if (!LogicalVisitor->CurrentElement) {
    LLVM_DEBUG({
        // We have an unsupported Symbol or Type Record.
        // W.printEnum("Kind ignored", unsigned(Kind), getSymbolTypeNames());
    });
    return Error::success();
  }

  // Offset carried by the traversal routines when dealing with streams.
  CurrentOffset = Offset;
  IsCompileUnit = false;
  if (!LogicalVisitor->CurrentElement->getOffsetFromTypeIndex())
    LogicalVisitor->CurrentElement->setOffset(Offset);
  if (symbolOpensScope(Kind) || (IsCompileUnit = symbolIsCompileUnit(Kind))) {
    assert(LogicalVisitor->CurrentScope && "Invalid scope!");
    LogicalVisitor->addElement(LogicalVisitor->CurrentScope, IsCompileUnit);
  } else {
    if (LogicalVisitor->CurrentSymbol)
      LogicalVisitor->addElement(LogicalVisitor->CurrentSymbol);
    if (LogicalVisitor->CurrentType)
      LogicalVisitor->addElement(LogicalVisitor->CurrentType);
  }

  return Error::success();
}

Error LVSymbolVisitor::visitSymbolEnd(CVSymbol &Record) {
  SymbolKind Kind = Record.kind();
  LLVM_DEBUG(
      { W.printEnum("End Kind", unsigned(Kind), getSymbolTypeNames()); });

  if (symbolEndsScope(Kind)) {
    LogicalVisitor->popScope();
  }

  return Error::success();
}

Error LVSymbolVisitor::visitUnknownSymbol(CVSymbol &Record) {
  LLVM_DEBUG({ W.printNumber("Length", Record.length()); });
  return Error::success();
}

// S_BLOCK32
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record, BlockSym &Block) {
  LLVM_DEBUG({
    W.printHex("CodeSize", Block.CodeSize);
    W.printHex("Segment", Block.Segment);
    W.printString("BlockName", Block.Name);
  });

  if (LVScope *Scope = LogicalVisitor->CurrentScope) {
    StringRef LinkageName;
    if (ObjDelegate)
      ObjDelegate->getLinkageName(Block.getRelocationOffset(), Block.CodeOffset,
                                  &LinkageName);
    Scope->setLinkageName(LinkageName);

    if (options().getGeneralCollectRanges()) {
      // Record converted segment::offset addressing for this scope.
      LVAddress Addendum = Reader->getSymbolTableAddress(LinkageName);
      LVAddress LowPC =
          Reader->linearAddress(Block.Segment, Block.CodeOffset, Addendum);
      LVAddress HighPC = LowPC + Block.CodeSize - 1;
      Scope->addObject(LowPC, HighPC);
    }
  }

  return Error::success();
}

// S_BPREL32
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record,
                                        BPRelativeSym &Local) {
  LLVM_DEBUG({
    printTypeIndex("Type", Local.Type);
    W.printNumber("Offset", Local.Offset);
    W.printString("VarName", Local.Name);
  });

  if (LVSymbol *Symbol = LogicalVisitor->CurrentSymbol) {
    Symbol->setName(Local.Name);
    // From the MS_Symbol_Type.pdf documentation (S_BPREL32):
    // This symbol specifies symbols that are allocated on the stack for a
    // procedure. For C and C++, these include the actual function parameters
    // and the local non-static variables of functions.
    // However, the offset for 'this' comes as a negative value.

    // Symbol was created as 'variable'; determine its real kind.
    Symbol->resetIsVariable();

    if (Local.Name == "this") {
      Symbol->setIsParameter();
      Symbol->setIsArtificial();
    } else {
      // Determine symbol kind.
      bool(Local.Offset > 0) ? Symbol->setIsParameter()
                             : Symbol->setIsVariable();
    }

    // Update correct debug information tag.
    if (Symbol->getIsParameter())
      Symbol->setTag(dwarf::DW_TAG_formal_parameter);

    LVElement *Element = LogicalVisitor->getElement(StreamTPI, Local.Type);
    if (Element && Element->getIsScoped()) {
      // We have a local type. Find its parent function.
      LVScope *Parent = Symbol->getFunctionParent();
      // The element representing the type has been already finalized. If
      // the type is an aggregate type, its members have been already added.
      // As the type is local, its level will be changed.

      // FIXME: Currently the algorithm used to scope lambda functions is
      // incorrect. Before we allocate the type at this scope, check if is
      // already allocated in other scope.
      if (!Element->getParentScope()) {
        Parent->addElement(Element);
        Element->updateLevel(Parent);
      }
    }
    Symbol->setType(Element);
  }

  return Error::success();
}

// S_REGREL32
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record,
                                        RegRelativeSym &Local) {
  LLVM_DEBUG({
    printTypeIndex("Type", Local.Type);
    W.printNumber("Offset", Local.Offset);
    W.printString("VarName", Local.Name);
  });

  if (LVSymbol *Symbol = LogicalVisitor->CurrentSymbol) {
    Symbol->setName(Local.Name);

    // Symbol was created as 'variable'; determine its real kind.
    Symbol->resetIsVariable();

    // Check for the 'this' symbol.
    if (Local.Name == "this") {
      Symbol->setIsArtificial();
      Symbol->setIsParameter();
    } else {
      // Determine symbol kind.
      determineSymbolKind(Symbol, Local.Register);
    }

    // Update correct debug information tag.
    if (Symbol->getIsParameter())
      Symbol->setTag(dwarf::DW_TAG_formal_parameter);

    LVElement *Element = LogicalVisitor->getElement(StreamTPI, Local.Type);
    if (Element && Element->getIsScoped()) {
      // We have a local type. Find its parent function.
      LVScope *Parent = Symbol->getFunctionParent();
      // The element representing the type has been already finalized. If
      // the type is an aggregate type, its members have been already added.
      // As the type is local, its level will be changed.

      // FIXME: Currently the algorithm used to scope lambda functions is
      // incorrect. Before we allocate the type at this scope, check if is
      // already allocated in other scope.
      if (!Element->getParentScope()) {
        Parent->addElement(Element);
        Element->updateLevel(Parent);
      }
    }
    Symbol->setType(Element);
  }

  return Error::success();
}

// S_BUILDINFO
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &CVR,
                                        BuildInfoSym &BuildInfo) {
  LLVM_DEBUG({ printTypeIndex("BuildId", BuildInfo.BuildId); });

  CVType CVBuildType = Ids.getType(BuildInfo.BuildId);
  if (Error Err = LogicalVisitor->finishVisitation(
          CVBuildType, BuildInfo.BuildId, Reader->getCompileUnit()))
    return Err;

  return Error::success();
}

// S_COMPILE2
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record,
                                        Compile2Sym &Compile2) {
  LLVM_DEBUG({
    W.printEnum("Language", uint8_t(Compile2.getLanguage()),
                getSourceLanguageNames());
    W.printFlags("Flags", uint32_t(Compile2.getFlags()),
                 getCompileSym3FlagNames());
    W.printEnum("Machine", unsigned(Compile2.Machine), getCPUTypeNames());
    W.printString("VersionName", Compile2.Version);
  });

  // MSVC generates the following sequence for a CodeView module:
  //   S_OBJNAME    --> Set 'CurrentObjectName'.
  //   S_COMPILE2   --> Set the compile unit name using 'CurrentObjectName'.
  //   ...
  //   S_BUILDINFO  --> Extract the source name.
  //
  // Clang generates the following sequence for a CodeView module:
  //   S_COMPILE2   --> Set the compile unit name to empty string.
  //   ...
  //   S_BUILDINFO  --> Extract the source name.
  //
  // For both toolchains, update the compile unit name from S_BUILDINFO.
  if (LVScope *Scope = LogicalVisitor->CurrentScope) {
    // The name of the CU, was extracted from the 'BuildInfo' subsection.
    Reader->setCompileUnitCPUType(Compile2.Machine);
    Scope->setName(CurrentObjectName);
    if (options().getAttributeProducer())
      Scope->setProducer(Compile2.Version);
    getReader().isSystemEntry(Scope, CurrentObjectName);

    // The line records in CodeView are recorded per Module ID. Update
    // the relationship between the current CU and the Module ID.
    Reader->addModule(Scope);

    // Updated the collected strings with their associated compile unit.
    Shared->StringRecords.addFilenames(Reader->getCompileUnit());
  }

  // Clear any previous ObjectName.
  CurrentObjectName = "";
  return Error::success();
}

// S_COMPILE3
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record,
                                        Compile3Sym &Compile3) {
  LLVM_DEBUG({
    W.printEnum("Language", uint8_t(Compile3.getLanguage()),
                getSourceLanguageNames());
    W.printFlags("Flags", uint32_t(Compile3.getFlags()),
                 getCompileSym3FlagNames());
    W.printEnum("Machine", unsigned(Compile3.Machine), getCPUTypeNames());
    W.printString("VersionName", Compile3.Version);
  });

  // MSVC generates the following sequence for a CodeView module:
  //   S_OBJNAME    --> Set 'CurrentObjectName'.
  //   S_COMPILE3   --> Set the compile unit name using 'CurrentObjectName'.
  //   ...
  //   S_BUILDINFO  --> Extract the source name.
  //
  // Clang generates the following sequence for a CodeView module:
  //   S_COMPILE3   --> Set the compile unit name to empty string.
  //   ...
  //   S_BUILDINFO  --> Extract the source name.
  //
  // For both toolchains, update the compile unit name from S_BUILDINFO.
  if (LVScope *Scope = LogicalVisitor->CurrentScope) {
    // The name of the CU, was extracted from the 'BuildInfo' subsection.
    Reader->setCompileUnitCPUType(Compile3.Machine);
    Scope->setName(CurrentObjectName);
    if (options().getAttributeProducer())
      Scope->setProducer(Compile3.Version);
    getReader().isSystemEntry(Scope, CurrentObjectName);

    // The line records in CodeView are recorded per Module ID. Update
    // the relationship between the current CU and the Module ID.
    Reader->addModule(Scope);

    // Updated the collected strings with their associated compile unit.
    Shared->StringRecords.addFilenames(Reader->getCompileUnit());
  }

  // Clear any previous ObjectName.
  CurrentObjectName = "";
  return Error::success();
}

// S_CONSTANT, S_MANCONSTANT
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record,
                                        ConstantSym &Constant) {
  LLVM_DEBUG({
    printTypeIndex("Type", Constant.Type);
    W.printNumber("Value", Constant.Value);
    W.printString("Name", Constant.Name);
  });

  if (LVSymbol *Symbol = LogicalVisitor->CurrentSymbol) {
    Symbol->setName(Constant.Name);
    Symbol->setType(LogicalVisitor->getElement(StreamTPI, Constant.Type));
    Symbol->resetIncludeInPrint();
  }

  return Error::success();
}

// S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE
Error LVSymbolVisitor::visitKnownRecord(
    CVSymbol &Record,
    DefRangeFramePointerRelFullScopeSym &DefRangeFramePointerRelFullScope) {
  // DefRanges don't have types, just registers and code offsets.
  LLVM_DEBUG({
    if (LocalSymbol)
      W.getOStream() << formatv("Symbol: {0}, ", LocalSymbol->getName());

    W.printNumber("Offset", DefRangeFramePointerRelFullScope.Offset);
  });

  if (LVSymbol *Symbol = LocalSymbol) {
    Symbol->setHasCodeViewLocation();
    LocalSymbol = nullptr;

    // Add location debug location. Operands: [Offset, 0].
    dwarf::Attribute Attr =
        dwarf::Attribute(SymbolKind::S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE);

    uint64_t Operand1 = DefRangeFramePointerRelFullScope.Offset;
    Symbol->addLocation(Attr, 0, 0, 0, 0);
    Symbol->addLocationOperands(LVSmall(Attr), {Operand1});
  }

  return Error::success();
}

// S_DEFRANGE_FRAMEPOINTER_REL
Error LVSymbolVisitor::visitKnownRecord(
    CVSymbol &Record, DefRangeFramePointerRelSym &DefRangeFramePointerRel) {
  // DefRanges don't have types, just registers and code offsets.
  LLVM_DEBUG({
    if (LocalSymbol)
      W.getOStream() << formatv("Symbol: {0}, ", LocalSymbol->getName());

    W.printNumber("Offset", DefRangeFramePointerRel.Hdr.Offset);
    printLocalVariableAddrRange(DefRangeFramePointerRel.Range,
                                DefRangeFramePointerRel.getRelocationOffset());
    printLocalVariableAddrGap(DefRangeFramePointerRel.Gaps);
  });

  // We are expecting the following sequence:
  //   128 | S_LOCAL [size = 20] `ParamBar`
  //         ...
  //   148 | S_DEFRANGE_FRAMEPOINTER_REL [size = 16]
  if (LVSymbol *Symbol = LocalSymbol) {
    Symbol->setHasCodeViewLocation();
    LocalSymbol = nullptr;

    // Add location debug location. Operands: [Offset, 0].
    dwarf::Attribute Attr =
        dwarf::Attribute(SymbolKind::S_DEFRANGE_FRAMEPOINTER_REL);
    uint64_t Operand1 = DefRangeFramePointerRel.Hdr.Offset;

    LocalVariableAddrRange Range = DefRangeFramePointerRel.Range;
    LVAddress Address =
        Reader->linearAddress(Range.ISectStart, Range.OffsetStart);

    Symbol->addLocation(Attr, Address, Address + Range.Range, 0, 0);
    Symbol->addLocationOperands(LVSmall(Attr), {Operand1});
  }

  return Error::success();
}

// S_DEFRANGE_REGISTER_REL
Error LVSymbolVisitor::visitKnownRecord(
    CVSymbol &Record, DefRangeRegisterRelSym &DefRangeRegisterRel) {
  // DefRanges don't have types, just registers and code offsets.
  LLVM_DEBUG({
    if (LocalSymbol)
      W.getOStream() << formatv("Symbol: {0}, ", LocalSymbol->getName());

    W.printBoolean("HasSpilledUDTMember",
                   DefRangeRegisterRel.hasSpilledUDTMember());
    W.printNumber("OffsetInParent", DefRangeRegisterRel.offsetInParent());
    W.printNumber("BasePointerOffset",
                  DefRangeRegisterRel.Hdr.BasePointerOffset);
    printLocalVariableAddrRange(DefRangeRegisterRel.Range,
                                DefRangeRegisterRel.getRelocationOffset());
    printLocalVariableAddrGap(DefRangeRegisterRel.Gaps);
  });

  if (LVSymbol *Symbol = LocalSymbol) {
    Symbol->setHasCodeViewLocation();
    LocalSymbol = nullptr;

    // Add location debug location. Operands: [Register, Offset].
    dwarf::Attribute Attr =
        dwarf::Attribute(SymbolKind::S_DEFRANGE_REGISTER_REL);
    uint64_t Operand1 = DefRangeRegisterRel.Hdr.Register;
    uint64_t Operand2 = DefRangeRegisterRel.Hdr.BasePointerOffset;

    LocalVariableAddrRange Range = DefRangeRegisterRel.Range;
    LVAddress Address =
        Reader->linearAddress(Range.ISectStart, Range.OffsetStart);

    Symbol->addLocation(Attr, Address, Address + Range.Range, 0, 0);
    Symbol->addLocationOperands(LVSmall(Attr), {Operand1, Operand2});
  }

  return Error::success();
}

// S_DEFRANGE_REGISTER
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record,
                                        DefRangeRegisterSym &DefRangeRegister) {
  // DefRanges don't have types, just registers and code offsets.
  LLVM_DEBUG({
    if (LocalSymbol)
      W.getOStream() << formatv("Symbol: {0}, ", LocalSymbol->getName());

    W.printEnum("Register", uint16_t(DefRangeRegister.Hdr.Register),
                getRegisterNames(Reader->getCompileUnitCPUType()));
    W.printNumber("MayHaveNoName", DefRangeRegister.Hdr.MayHaveNoName);
    printLocalVariableAddrRange(DefRangeRegister.Range,
                                DefRangeRegister.getRelocationOffset());
    printLocalVariableAddrGap(DefRangeRegister.Gaps);
  });

  if (LVSymbol *Symbol = LocalSymbol) {
    Symbol->setHasCodeViewLocation();
    LocalSymbol = nullptr;

    // Add location debug location. Operands: [Register, 0].
    dwarf::Attribute Attr = dwarf::Attribute(SymbolKind::S_DEFRANGE_REGISTER);
    uint64_t Operand1 = DefRangeRegister.Hdr.Register;

    LocalVariableAddrRange Range = DefRangeRegister.Range;
    LVAddress Address =
        Reader->linearAddress(Range.ISectStart, Range.OffsetStart);

    Symbol->addLocation(Attr, Address, Address + Range.Range, 0, 0);
    Symbol->addLocationOperands(LVSmall(Attr), {Operand1});
  }

  return Error::success();
}

// S_DEFRANGE_SUBFIELD_REGISTER
Error LVSymbolVisitor::visitKnownRecord(
    CVSymbol &Record, DefRangeSubfieldRegisterSym &DefRangeSubfieldRegister) {
  // DefRanges don't have types, just registers and code offsets.
  LLVM_DEBUG({
    if (LocalSymbol)
      W.getOStream() << formatv("Symbol: {0}, ", LocalSymbol->getName());

    W.printEnum("Register", uint16_t(DefRangeSubfieldRegister.Hdr.Register),
                getRegisterNames(Reader->getCompileUnitCPUType()));
    W.printNumber("MayHaveNoName", DefRangeSubfieldRegister.Hdr.MayHaveNoName);
    W.printNumber("OffsetInParent",
                  DefRangeSubfieldRegister.Hdr.OffsetInParent);
    printLocalVariableAddrRange(DefRangeSubfieldRegister.Range,
                                DefRangeSubfieldRegister.getRelocationOffset());
    printLocalVariableAddrGap(DefRangeSubfieldRegister.Gaps);
  });

  if (LVSymbol *Symbol = LocalSymbol) {
    Symbol->setHasCodeViewLocation();
    LocalSymbol = nullptr;

    // Add location debug location.  Operands: [Register, 0].
    dwarf::Attribute Attr =
        dwarf::Attribute(SymbolKind::S_DEFRANGE_SUBFIELD_REGISTER);
    uint64_t Operand1 = DefRangeSubfieldRegister.Hdr.Register;

    LocalVariableAddrRange Range = DefRangeSubfieldRegister.Range;
    LVAddress Address =
        Reader->linearAddress(Range.ISectStart, Range.OffsetStart);

    Symbol->addLocation(Attr, Address, Address + Range.Range, 0, 0);
    Symbol->addLocationOperands(LVSmall(Attr), {Operand1});
  }

  return Error::success();
}

// S_DEFRANGE_SUBFIELD
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record,
                                        DefRangeSubfieldSym &DefRangeSubfield) {
  // DefRanges don't have types, just registers and code offsets.
  LLVM_DEBUG({
    if (LocalSymbol)
      W.getOStream() << formatv("Symbol: {0}, ", LocalSymbol->getName());

    if (ObjDelegate) {
      DebugStringTableSubsectionRef Strings = ObjDelegate->getStringTable();
      auto ExpectedProgram = Strings.getString(DefRangeSubfield.Program);
      if (!ExpectedProgram) {
        consumeError(ExpectedProgram.takeError());
        return llvm::make_error<CodeViewError>(
            "String table offset outside of bounds of String Table!");
      }
      W.printString("Program", *ExpectedProgram);
    }
    W.printNumber("OffsetInParent", DefRangeSubfield.OffsetInParent);
    printLocalVariableAddrRange(DefRangeSubfield.Range,
                                DefRangeSubfield.getRelocationOffset());
    printLocalVariableAddrGap(DefRangeSubfield.Gaps);
  });

  if (LVSymbol *Symbol = LocalSymbol) {
    Symbol->setHasCodeViewLocation();
    LocalSymbol = nullptr;

    // Add location debug location. Operands: [Program, 0].
    dwarf::Attribute Attr = dwarf::Attribute(SymbolKind::S_DEFRANGE_SUBFIELD);
    uint64_t Operand1 = DefRangeSubfield.Program;

    LocalVariableAddrRange Range = DefRangeSubfield.Range;
    LVAddress Address =
        Reader->linearAddress(Range.ISectStart, Range.OffsetStart);

    Symbol->addLocation(Attr, Address, Address + Range.Range, 0, 0);
    Symbol->addLocationOperands(LVSmall(Attr), {Operand1, /*Operand2=*/0});
  }

  return Error::success();
}

// S_DEFRANGE
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record,
                                        DefRangeSym &DefRange) {
  // DefRanges don't have types, just registers and code offsets.
  LLVM_DEBUG({
    if (LocalSymbol)
      W.getOStream() << formatv("Symbol: {0}, ", LocalSymbol->getName());

    if (ObjDelegate) {
      DebugStringTableSubsectionRef Strings = ObjDelegate->getStringTable();
      auto ExpectedProgram = Strings.getString(DefRange.Program);
      if (!ExpectedProgram) {
        consumeError(ExpectedProgram.takeError());
        return llvm::make_error<CodeViewError>(
            "String table offset outside of bounds of String Table!");
      }
      W.printString("Program", *ExpectedProgram);
    }
    printLocalVariableAddrRange(DefRange.Range, DefRange.getRelocationOffset());
    printLocalVariableAddrGap(DefRange.Gaps);
  });

  if (LVSymbol *Symbol = LocalSymbol) {
    Symbol->setHasCodeViewLocation();
    LocalSymbol = nullptr;

    // Add location debug location. Operands: [Program, 0].
    dwarf::Attribute Attr = dwarf::Attribute(SymbolKind::S_DEFRANGE);
    uint64_t Operand1 = DefRange.Program;

    LocalVariableAddrRange Range = DefRange.Range;
    LVAddress Address =
        Reader->linearAddress(Range.ISectStart, Range.OffsetStart);

    Symbol->addLocation(Attr, Address, Address + Range.Range, 0, 0);
    Symbol->addLocationOperands(LVSmall(Attr), {Operand1, /*Operand2=*/0});
  }

  return Error::success();
}

// S_FRAMEPROC
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record,
                                        FrameProcSym &FrameProc) {
  if (LVScope *Function = LogicalVisitor->getReaderScope()) {
    // S_FRAMEPROC contains extra information for the function described
    // by any of the previous generated records:
    // S_GPROC32, S_LPROC32, S_LPROC32_ID, S_GPROC32_ID.

    // The generated sequence is:
    //   S_GPROC32_ID ...
    //   S_FRAMEPROC ...

    // Collect additional inline flags for the current scope function.
    FrameProcedureOptions Flags = FrameProc.Flags;
    if (FrameProcedureOptions::MarkedInline ==
        (Flags & FrameProcedureOptions::MarkedInline))
      Function->setInlineCode(dwarf::DW_INL_declared_inlined);
    if (FrameProcedureOptions::Inlined ==
        (Flags & FrameProcedureOptions::Inlined))
      Function->setInlineCode(dwarf::DW_INL_inlined);

    // To determine the symbol kind for any symbol declared in that function,
    // we can access the S_FRAMEPROC for the parent scope function. It contains
    // information about the local fp and param fp registers and compare with
    // the register in the S_REGREL32 to get a match.
    codeview::CPUType CPU = Reader->getCompileUnitCPUType();
    LocalFrameRegister = FrameProc.getLocalFramePtrReg(CPU);
    ParamFrameRegister = FrameProc.getParamFramePtrReg(CPU);
  }

  return Error::success();
}

// S_GDATA32, S_LDATA32, S_LMANDATA, S_GMANDATA
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record, DataSym &Data) {
  LLVM_DEBUG({
    printTypeIndex("Type", Data.Type);
    W.printString("DisplayName", Data.Name);
  });

  if (LVSymbol *Symbol = LogicalVisitor->CurrentSymbol) {
    StringRef LinkageName;
    if (ObjDelegate)
      ObjDelegate->getLinkageName(Data.getRelocationOffset(), Data.DataOffset,
                                  &LinkageName);

    Symbol->setName(Data.Name);
    Symbol->setLinkageName(LinkageName);

    // The MSVC generates local data as initialization for aggregates. It
    // contains the address for an initialization function.
    // The symbols contains the '$initializer$' pattern. Allow them only if
    // the '--internal=system' option is given.
    //   0 | S_LDATA32 `Struct$initializer$`
    //       type = 0x1040 (void ()*)
    if (getReader().isSystemEntry(Symbol) && !options().getAttributeSystem()) {
      Symbol->resetIncludeInPrint();
      return Error::success();
    }

    if (LVScope *Namespace = Shared->NamespaceDeduction.get(Data.Name)) {
      // The variable is already at different scope. In order to reflect
      // the correct parent, move it to the namespace.
      if (Symbol->getParentScope()->removeElement(Symbol))
        Namespace->addElement(Symbol);
    }

    Symbol->setType(LogicalVisitor->getElement(StreamTPI, Data.Type));
    if (Record.kind() == SymbolKind::S_GDATA32)
      Symbol->setIsExternal();
  }

  return Error::success();
}

// S_INLINESITE
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record,
                                        InlineSiteSym &InlineSite) {
  LLVM_DEBUG({ printTypeIndex("Inlinee", InlineSite.Inlinee); });

  if (LVScope *InlinedFunction = LogicalVisitor->CurrentScope) {
    LVScope *AbstractFunction = Reader->createScopeFunction();
    AbstractFunction->setIsSubprogram();
    AbstractFunction->setTag(dwarf::DW_TAG_subprogram);
    AbstractFunction->setInlineCode(dwarf::DW_INL_inlined);
    AbstractFunction->setIsInlinedAbstract();
    InlinedFunction->setReference(AbstractFunction);

    LogicalVisitor->startProcessArgumentList();
    // 'Inlinee' is a Type ID.
    CVType CVFunctionType = Ids.getType(InlineSite.Inlinee);
    if (Error Err = LogicalVisitor->finishVisitation(
            CVFunctionType, InlineSite.Inlinee, AbstractFunction))
      return Err;
    LogicalVisitor->stopProcessArgumentList();

    // For inlined functions set the linkage name to be the same as
    // the name. It used to find their lines and ranges.
    StringRef Name = AbstractFunction->getName();
    InlinedFunction->setName(Name);
    InlinedFunction->setLinkageName(Name);

    // Process annotation bytes to calculate code and line offsets.
    if (Error Err = LogicalVisitor->inlineSiteAnnotation(
            AbstractFunction, InlinedFunction, InlineSite))
      return Err;
  }

  return Error::success();
}

// S_LOCAL
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record, LocalSym &Local) {
  LLVM_DEBUG({
    printTypeIndex("Type", Local.Type);
    W.printFlags("Flags", uint16_t(Local.Flags), getLocalFlagNames());
    W.printString("VarName", Local.Name);
  });

  if (LVSymbol *Symbol = LogicalVisitor->CurrentSymbol) {
    Symbol->setName(Local.Name);

    // Symbol was created as 'variable'; determine its real kind.
    Symbol->resetIsVariable();

    // Be sure the 'this' symbol is marked as 'compiler generated'.
    if (bool(Local.Flags & LocalSymFlags::IsCompilerGenerated) ||
        Local.Name == "this") {
      Symbol->setIsArtificial();
      Symbol->setIsParameter();
    } else {
      bool(Local.Flags & LocalSymFlags::IsParameter) ? Symbol->setIsParameter()
                                                     : Symbol->setIsVariable();
    }

    // Update correct debug information tag.
    if (Symbol->getIsParameter())
      Symbol->setTag(dwarf::DW_TAG_formal_parameter);

    LVElement *Element = LogicalVisitor->getElement(StreamTPI, Local.Type);
    if (Element && Element->getIsScoped()) {
      // We have a local type. Find its parent function.
      LVScope *Parent = Symbol->getFunctionParent();
      // The element representing the type has been already finalized. If
      // the type is an aggregate type, its members have been already added.
      // As the type is local, its level will be changed.
      Parent->addElement(Element);
      Element->updateLevel(Parent);
    }
    Symbol->setType(Element);

    // The CodeView records (S_DEFFRAME_*) describing debug location for
    // this symbol, do not have any direct reference to it. Those records
    // are emitted after this symbol. Record the current symbol.
    LocalSymbol = Symbol;
  }

  return Error::success();
}

// S_OBJNAME
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record, ObjNameSym &ObjName) {
  LLVM_DEBUG({
    W.printHex("Signature", ObjName.Signature);
    W.printString("ObjectName", ObjName.Name);
  });

  CurrentObjectName = ObjName.Name;
  return Error::success();
}

// S_GPROC32, S_LPROC32, S_LPROC32_ID, S_GPROC32_ID
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record, ProcSym &Proc) {
  if (InFunctionScope)
    return llvm::make_error<CodeViewError>("Visiting a ProcSym while inside "
                                           "function scope!");

  InFunctionScope = true;

  LLVM_DEBUG({
    printTypeIndex("FunctionType", Proc.FunctionType);
    W.printHex("Segment", Proc.Segment);
    W.printFlags("Flags", static_cast<uint8_t>(Proc.Flags),
                 getProcSymFlagNames());
    W.printString("DisplayName", Proc.Name);
  });

  // Clang and Microsoft generated different debug information records:
  // For functions definitions:
  // Clang:     S_GPROC32 -> LF_FUNC_ID -> LF_PROCEDURE
  // Microsoft: S_GPROC32 ->               LF_PROCEDURE

  // For member function definition:
  // Clang:     S_GPROC32 -> LF_MFUNC_ID -> LF_MFUNCTION
  // Microsoft: S_GPROC32 ->                LF_MFUNCTION
  // In order to support both sequences, if we found LF_FUNCTION_ID, just
  // get the TypeIndex for LF_PROCEDURE.

  // For the given test case, we have the sequence:
  // namespace NSP_local {
  //   void foo_local() {
  //   }
  // }
  //
  // 0x1000 | LF_STRING_ID String: NSP_local
  // 0x1002 | LF_PROCEDURE
  //          return type = 0x0003 (void), # args = 0, param list = 0x1001
  //          calling conv = cdecl, options = None
  // 0x1003 | LF_FUNC_ID
  //          name = foo_local, type = 0x1002, parent scope = 0x1000
  //      0 | S_GPROC32_ID `NSP_local::foo_local`
  //          type = `0x1003 (foo_local)`
  // 0x1004 | LF_STRING_ID String: suite
  // 0x1005 | LF_STRING_ID String: suite_local.cpp
  //
  // The LF_STRING_ID can hold different information:
  // 0x1000 - The enclosing namespace.
  // 0x1004 - The compile unit directory name.
  // 0x1005 - The compile unit name.
  //
  // Before deducting its scope, we need to evaluate its type and create any
  // associated namespaces.
  if (LVScope *Function = LogicalVisitor->CurrentScope) {
    StringRef LinkageName;
    if (ObjDelegate)
      ObjDelegate->getLinkageName(Proc.getRelocationOffset(), Proc.CodeOffset,
                                  &LinkageName);

    // The line table can be accessed using the linkage name.
    Reader->addToSymbolTable(LinkageName, Function);
    Function->setName(Proc.Name);
    Function->setLinkageName(LinkageName);

    if (options().getGeneralCollectRanges()) {
      // Record converted segment::offset addressing for this scope.
      LVAddress Addendum = Reader->getSymbolTableAddress(LinkageName);
      LVAddress LowPC =
          Reader->linearAddress(Proc.Segment, Proc.CodeOffset, Addendum);
      LVAddress HighPC = LowPC + Proc.CodeSize - 1;
      Function->addObject(LowPC, HighPC);

      // If the scope is a function, add it to the public names.
      if ((options().getAttributePublics() || options().getPrintAnyLine()) &&
          !Function->getIsInlinedFunction())
        Reader->getCompileUnit()->addPublicName(Function, LowPC, HighPC);
    }

    if (Function->getIsSystem() && !options().getAttributeSystem()) {
      Function->resetIncludeInPrint();
      return Error::success();
    }

    TypeIndex TIFunctionType = Proc.FunctionType;
    if (TIFunctionType.isSimple())
      Function->setType(LogicalVisitor->getElement(StreamTPI, TIFunctionType));
    else {
      // We have to detect the correct stream, using the lexical parent
      // name, as there is not other obvious way to get the stream.
      //   Normal function: LF_FUNC_ID (TPI)/(IPI)
      //                    LF_PROCEDURE (TPI)
      //   Lambda function: LF_MFUNCTION (TPI)
      //   Member function: LF_MFUNC_ID (TPI)/(IPI)

      StringRef OuterComponent;
      std::tie(OuterComponent, std::ignore) = getInnerComponent(Proc.Name);
      TypeIndex TI = Shared->ForwardReferences.find(OuterComponent);

      std::optional<CVType> CVFunctionType;
      auto GetRecordType = [&]() -> bool {
        CVFunctionType = Ids.tryGetType(TIFunctionType);
        if (!CVFunctionType)
          return false;

        if (TI.isNoneType())
          // Normal function.
          if (CVFunctionType->kind() == LF_FUNC_ID)
            return true;

        // Member function.
        return (CVFunctionType->kind() == LF_MFUNC_ID);
      };

      // We can have a LF_FUNC_ID, LF_PROCEDURE or LF_MFUNCTION.
      if (!GetRecordType()) {
        CVFunctionType = Types.tryGetType(TIFunctionType);
        if (!CVFunctionType)
          return llvm::make_error<CodeViewError>("Invalid type index");
      }

      if (Error Err = LogicalVisitor->finishVisitation(
              *CVFunctionType, TIFunctionType, Function))
        return Err;
    }

    if (Record.kind() == SymbolKind::S_GPROC32 ||
        Record.kind() == SymbolKind::S_GPROC32_ID)
      Function->setIsExternal();

    // We don't have a way to see if the symbol is compiler generated. Use
    // the linkage name, to detect `scalar deleting destructor' functions.
    std::string DemangledSymbol = demangle(LinkageName);
    if (DemangledSymbol.find("scalar deleting dtor") != std::string::npos) {
      Function->setIsArtificial();
    } else {
      // Clang generates global ctor and dtor names containing the substrings:
      // 'dynamic initializer for' and 'dynamic atexit destructor for'.
      if (DemangledSymbol.find("dynamic atexit destructor for") !=
          std::string::npos)
        Function->setIsArtificial();
    }
  }

  return Error::success();
}

// S_END
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record,
                                        ScopeEndSym &ScopeEnd) {
  InFunctionScope = false;
  return Error::success();
}

// S_THUNK32
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record, Thunk32Sym &Thunk) {
  if (InFunctionScope)
    return llvm::make_error<CodeViewError>("Visiting a Thunk32Sym while inside "
                                           "function scope!");

  InFunctionScope = true;

  LLVM_DEBUG({
    W.printHex("Segment", Thunk.Segment);
    W.printString("Name", Thunk.Name);
  });

  if (LVScope *Function = LogicalVisitor->CurrentScope)
    Function->setName(Thunk.Name);

  return Error::success();
}

// S_UDT, S_COBOLUDT
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record, UDTSym &UDT) {
  LLVM_DEBUG({
    printTypeIndex("Type", UDT.Type);
    W.printString("UDTName", UDT.Name);
  });

  if (LVType *Type = LogicalVisitor->CurrentType) {
    if (LVScope *Namespace = Shared->NamespaceDeduction.get(UDT.Name)) {
      if (Type->getParentScope()->removeElement(Type))
        Namespace->addElement(Type);
    }

    Type->setName(UDT.Name);

    // We have to determine if the typedef is a real C/C++ definition or is
    // the S_UDT record that describe all the user defined types.
    //      0 | S_UDT `Name` original type = 0x1009
    // 0x1009 | LF_STRUCTURE `Name`
    // Ignore type definitions for RTTI types:
    // _s__RTTIBaseClassArray, _s__RTTIBaseClassDescriptor,
    // _s__RTTICompleteObjectLocator, _s__RTTIClassHierarchyDescriptor.
    if (getReader().isSystemEntry(Type))
      Type->resetIncludeInPrint();
    else {
      StringRef RecordName = getRecordName(Types, UDT.Type);
      if (UDT.Name == RecordName)
        Type->resetIncludeInPrint();
      Type->setType(LogicalVisitor->getElement(StreamTPI, UDT.Type));
    }
  }

  return Error::success();
}

// S_UNAMESPACE
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record,
                                        UsingNamespaceSym &UN) {
  LLVM_DEBUG({ W.printString("Namespace", UN.Name); });
  return Error::success();
}

// S_ARMSWITCHTABLE
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &CVR,
                                        JumpTableSym &JumpTable) {
  LLVM_DEBUG({
    W.printHex("BaseOffset", JumpTable.BaseOffset);
    W.printNumber("BaseSegment", JumpTable.BaseSegment);
    W.printFlags("SwitchType", static_cast<uint16_t>(JumpTable.SwitchType),
                 getJumpTableEntrySizeNames());
    W.printHex("BranchOffset", JumpTable.BranchOffset);
    W.printHex("TableOffset", JumpTable.TableOffset);
    W.printNumber("BranchSegment", JumpTable.BranchSegment);
    W.printNumber("TableSegment", JumpTable.TableSegment);
    W.printNumber("EntriesCount", JumpTable.EntriesCount);
  });
  return Error::success();
}

// S_CALLERS, S_CALLEES, S_INLINEES
Error LVSymbolVisitor::visitKnownRecord(CVSymbol &Record, CallerSym &Caller) {
  LLVM_DEBUG({
    llvm::StringRef FieldName;
    switch (Caller.getKind()) {
    case SymbolRecordKind::CallerSym:
      FieldName = "Callee";
      break;
    case SymbolRecordKind::CalleeSym:
      FieldName = "Caller";
      break;
    case SymbolRecordKind::InlineesSym:
      FieldName = "Inlinee";
      break;
    default:
      return llvm::make_error<CodeViewError>(
          "Unknown CV Record type for a CallerSym object!");
    }
    for (auto FuncID : Caller.Indices) {
      printTypeIndex(FieldName, FuncID);
    }
  });
  return Error::success();
}

#undef DEBUG_TYPE
#define DEBUG_TYPE "CodeViewLogicalVisitor"

//===----------------------------------------------------------------------===//
// Logical visitor.
//===----------------------------------------------------------------------===//
LVLogicalVisitor::LVLogicalVisitor(LVCodeViewReader *Reader, ScopedPrinter &W,
                                   InputFile &Input)
    : Reader(Reader), W(W), Input(Input) {
  // The LogicalVisitor connects the CodeViewReader with the visitors that
  // traverse the types, symbols, etc. Do any initialization that is needed.
  Shared = std::make_shared<LVShared>(Reader, this);
}

void LVLogicalVisitor::printTypeIndex(StringRef FieldName, TypeIndex TI,
                                      uint32_t StreamIdx) {
  codeview::printTypeIndex(W, FieldName, TI,
                           StreamIdx == StreamTPI ? types() : ids());
}

void LVLogicalVisitor::printTypeBegin(CVType &Record, TypeIndex TI,
                                      LVElement *Element, uint32_t StreamIdx) {
  W.getOStream() << "\n";
  W.startLine() << formatTypeLeafKind(Record.kind());
  W.getOStream() << " (" << HexNumber(TI.getIndex()) << ")";
  W.getOStream() << " {\n";
  W.indent();
  W.printEnum("TypeLeafKind", unsigned(Record.kind()), ArrayRef(LeafTypeNames));
  printTypeIndex("TI", TI, StreamIdx);
  W.startLine() << "Element: " << HexNumber(Element->getOffset()) << " "
                << Element->getName() << "\n";
}

void LVLogicalVisitor::printTypeEnd(CVType &Record) {
  W.unindent();
  W.startLine() << "}\n";
}

void LVLogicalVisitor::printMemberBegin(CVMemberRecord &Record, TypeIndex TI,
                                        LVElement *Element,
                                        uint32_t StreamIdx) {
  W.getOStream() << "\n";
  W.startLine() << formatTypeLeafKind(Record.Kind);
  W.getOStream() << " (" << HexNumber(TI.getIndex()) << ")";
  W.getOStream() << " {\n";
  W.indent();
  W.printEnum("TypeLeafKind", unsigned(Record.Kind), ArrayRef(LeafTypeNames));
  printTypeIndex("TI", TI, StreamIdx);
  W.startLine() << "Element: " << HexNumber(Element->getOffset()) << " "
                << Element->getName() << "\n";
}

void LVLogicalVisitor::printMemberEnd(CVMemberRecord &Record) {
  W.unindent();
  W.startLine() << "}\n";
}

Error LVLogicalVisitor::visitUnknownType(CVType &Record, TypeIndex TI) {
  LLVM_DEBUG({
    printTypeIndex("\nTI", TI, StreamTPI);
    W.printNumber("Length", uint32_t(Record.content().size()));
  });
  return Error::success();
}

// LF_ARGLIST (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, ArgListRecord &Args,
                                         TypeIndex TI, LVElement *Element) {
  ArrayRef<TypeIndex> Indices = Args.getIndices();
  uint32_t Size = Indices.size();
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    W.printNumber("NumArgs", Size);
    ListScope Arguments(W, "Arguments");
    for (uint32_t I = 0; I < Size; ++I)
      printTypeIndex("ArgType", Indices[I], StreamTPI);
    printTypeEnd(Record);
  });

  LVScope *Function = static_cast<LVScope *>(Element);
  for (uint32_t Index = 0; Index < Size; ++Index) {
    TypeIndex ParameterType = Indices[Index];
    createParameter(ParameterType, StringRef(), Function);
  }

  return Error::success();
}

// LF_ARRAY (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, ArrayRecord &AT,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("ElementType", AT.getElementType(), StreamTPI);
    printTypeIndex("IndexType", AT.getIndexType(), StreamTPI);
    W.printNumber("SizeOf", AT.getSize());
    W.printString("Name", AT.getName());
    printTypeEnd(Record);
  });

  if (Element->getIsFinalized())
    return Error::success();
  Element->setIsFinalized();

  LVScopeArray *Array = static_cast<LVScopeArray *>(Element);
  if (!Array)
    return Error::success();

  Reader->getCompileUnit()->addElement(Array);
  TypeIndex TIElementType = AT.getElementType();

  LVType *PrevSubrange = nullptr;
  LazyRandomTypeCollection &Types = types();

  // As the logical view is modeled on DWARF, for each dimension we have to
  // create a DW_TAG_subrange_type, with dimension size.
  // The subrange type can be: unsigned __int32 or unsigned __int64.
  auto AddSubrangeType = [&](ArrayRecord &AR) {
    LVType *Subrange = Reader->createTypeSubrange();
    Subrange->setTag(dwarf::DW_TAG_subrange_type);
    Subrange->setType(getElement(StreamTPI, AR.getIndexType()));
    Subrange->setCount(AR.getSize());
    Subrange->setOffset(
        TIElementType.isSimple()
            ? (uint32_t)(TypeLeafKind)TIElementType.getSimpleKind()
            : TIElementType.getIndex());
    Array->addElement(Subrange);

    if (PrevSubrange)
      if (int64_t Count = Subrange->getCount())
        PrevSubrange->setCount(PrevSubrange->getCount() / Count);
    PrevSubrange = Subrange;
  };

  // Preserve the original TypeIndex; it would be updated in the case of:
  // - The array type contains qualifiers.
  // - In multidimensional arrays, the last LF_ARRAY entry contains the type.
  TypeIndex TIArrayType;

  // For each dimension in the array, there is a LF_ARRAY entry. The last
  // entry contains the array type, which can be a LF_MODIFIER in the case
  // of the type being modified by a qualifier (const, etc).
  ArrayRecord AR(AT);
  CVType CVEntry = Record;
  while (CVEntry.kind() == LF_ARRAY) {
    // Create the subrange information, required by the logical view. Once
    // the array has been processed, the dimension sizes will updated, as
    // the sizes are a progression. For instance:
    // sizeof(int) = 4
    // int Array[2];        Sizes:  8          Dim: 8  /  4 -> [2]
    // int Array[2][3];     Sizes: 24, 12      Dim: 24 / 12 -> [2]
    //                                         Dim: 12 /  4 ->    [3]
    // int Array[2][3][4];  sizes: 96, 48, 16  Dim: 96 / 48 -> [2]
    //                                         Dim: 48 / 16 ->    [3]
    //                                         Dim: 16 /  4 ->       [4]
    AddSubrangeType(AR);
    TIArrayType = TIElementType;

    // The current ElementType can be a modifier, in which case we need to
    // get the type being modified.
    // If TypeIndex is not a simple type, check if we have a qualified type.
    if (!TIElementType.isSimple()) {
      CVType CVElementType = Types.getType(TIElementType);
      if (CVElementType.kind() == LF_MODIFIER) {
        LVElement *QualifiedType =
            Shared->TypeRecords.find(StreamTPI, TIElementType);
        if (Error Err =
                finishVisitation(CVElementType, TIElementType, QualifiedType))
          return Err;
        // Get the TypeIndex of the type that the LF_MODIFIER modifies.
        TIElementType = getModifiedType(CVElementType);
      }
    }
    // Ends the traversal, as we have reached a simple type (int, char, etc).
    if (TIElementType.isSimple())
      break;

    // Read next dimension linked entry, if any.
    CVEntry = Types.getType(TIElementType);
    if (Error Err = TypeDeserializer::deserializeAs(
            const_cast<CVType &>(CVEntry), AR)) {
      consumeError(std::move(Err));
      break;
    }
    TIElementType = AR.getElementType();
    // NOTE: The typeindex has a value of: 0x0280.0000
    getTrueType(TIElementType);
  }

  Array->setName(AT.getName());
  TIArrayType = Shared->ForwardReferences.remap(TIArrayType);
  Array->setType(getElement(StreamTPI, TIArrayType));

  if (PrevSubrange)
    // In the case of an aggregate type (class, struct, union, interface),
    // get the aggregate size. As the original record is pointing to its
    // reference, we have to update it.
    if (uint64_t Size =
            isAggregate(CVEntry)
                ? getSizeInBytesForTypeRecord(Types.getType(TIArrayType))
                : getSizeInBytesForTypeIndex(TIElementType))
      PrevSubrange->setCount(PrevSubrange->getCount() / Size);

  return Error::success();
}

// LF_BITFIELD (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, BitFieldRecord &BF,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("Type", TI, StreamTPI);
    W.printNumber("BitSize", BF.getBitSize());
    W.printNumber("BitOffset", BF.getBitOffset());
    printTypeEnd(Record);
  });

  Element->setType(getElement(StreamTPI, BF.getType()));
  Element->setBitSize(BF.getBitSize());
  return Error::success();
}

// LF_BUILDINFO (TPI)/(IPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, BuildInfoRecord &BI,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamIPI);
    W.printNumber("NumArgs", static_cast<uint32_t>(BI.getArgs().size()));
    ListScope Arguments(W, "Arguments");
    for (TypeIndex Arg : BI.getArgs())
      printTypeIndex("ArgType", Arg, StreamIPI);
    printTypeEnd(Record);
  });

  // The given 'Element' refers to the current compilation unit.
  // All the args are references into the TPI/IPI stream.
  TypeIndex TIName = BI.getArgs()[BuildInfoRecord::BuildInfoArg::SourceFile];
  std::string Name = std::string(ids().getTypeName(TIName));

  // There are cases where LF_BUILDINFO fields are empty.
  if (!Name.empty())
    Element->setName(Name);

  return Error::success();
}

// LF_CLASS, LF_STRUCTURE, LF_INTERFACE (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, ClassRecord &Class,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    W.printNumber("MemberCount", Class.getMemberCount());
    printTypeIndex("FieldList", Class.getFieldList(), StreamTPI);
    printTypeIndex("DerivedFrom", Class.getDerivationList(), StreamTPI);
    printTypeIndex("VShape", Class.getVTableShape(), StreamTPI);
    W.printNumber("SizeOf", Class.getSize());
    W.printString("Name", Class.getName());
    if (Class.hasUniqueName())
      W.printString("UniqueName", Class.getUniqueName());
    printTypeEnd(Record);
  });

  if (Element->getIsFinalized())
    return Error::success();
  Element->setIsFinalized();

  LVScopeAggregate *Scope = static_cast<LVScopeAggregate *>(Element);
  if (!Scope)
    return Error::success();

  Scope->setName(Class.getName());
  if (Class.hasUniqueName())
    Scope->setLinkageName(Class.getUniqueName());

  if (Class.isNested()) {
    Scope->setIsNested();
    createParents(Class.getName(), Scope);
  }

  if (Class.isScoped())
    Scope->setIsScoped();

  // Nested types will be added to their parents at creation. The forward
  // references are only processed to finish the referenced element creation.
  if (!(Class.isNested() || Class.isScoped())) {
    if (LVScope *Namespace = Shared->NamespaceDeduction.get(Class.getName()))
      Namespace->addElement(Scope);
    else
      Reader->getCompileUnit()->addElement(Scope);
  }

  LazyRandomTypeCollection &Types = types();
  TypeIndex TIFieldList = Class.getFieldList();
  if (TIFieldList.isNoneType()) {
    TypeIndex ForwardType = Shared->ForwardReferences.find(Class.getName());
    if (!ForwardType.isNoneType()) {
      CVType CVReference = Types.getType(ForwardType);
      TypeRecordKind RK = static_cast<TypeRecordKind>(CVReference.kind());
      ClassRecord ReferenceRecord(RK);
      if (Error Err = TypeDeserializer::deserializeAs(
              const_cast<CVType &>(CVReference), ReferenceRecord))
        return Err;
      TIFieldList = ReferenceRecord.getFieldList();
    }
  }

  if (!TIFieldList.isNoneType()) {
    // Pass down the TypeIndex 'TI' for the aggregate containing the field list.
    CVType CVFieldList = Types.getType(TIFieldList);
    if (Error Err = finishVisitation(CVFieldList, TI, Scope))
      return Err;
  }

  return Error::success();
}

// LF_ENUM (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, EnumRecord &Enum,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    W.printNumber("NumEnumerators", Enum.getMemberCount());
    printTypeIndex("UnderlyingType", Enum.getUnderlyingType(), StreamTPI);
    printTypeIndex("FieldListType", Enum.getFieldList(), StreamTPI);
    W.printString("Name", Enum.getName());
    printTypeEnd(Record);
  });

  LVScopeEnumeration *Scope = static_cast<LVScopeEnumeration *>(Element);
  if (!Scope)
    return Error::success();

  if (Scope->getIsFinalized())
    return Error::success();
  Scope->setIsFinalized();

  // Set the name, as in the case of nested, it would determine the relation
  // to any potential parent, via the LF_NESTTYPE record.
  Scope->setName(Enum.getName());
  if (Enum.hasUniqueName())
    Scope->setLinkageName(Enum.getUniqueName());

  Scope->setType(getElement(StreamTPI, Enum.getUnderlyingType()));

  if (Enum.isNested()) {
    Scope->setIsNested();
    createParents(Enum.getName(), Scope);
  }

  if (Enum.isScoped()) {
    Scope->setIsScoped();
    Scope->setIsEnumClass();
  }

  // Nested types will be added to their parents at creation.
  if (!(Enum.isNested() || Enum.isScoped())) {
    if (LVScope *Namespace = Shared->NamespaceDeduction.get(Enum.getName()))
      Namespace->addElement(Scope);
    else
      Reader->getCompileUnit()->addElement(Scope);
  }

  TypeIndex TIFieldList = Enum.getFieldList();
  if (!TIFieldList.isNoneType()) {
    LazyRandomTypeCollection &Types = types();
    CVType CVFieldList = Types.getType(TIFieldList);
    if (Error Err = finishVisitation(CVFieldList, TIFieldList, Scope))
      return Err;
  }

  return Error::success();
}

// LF_FIELDLIST (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record,
                                         FieldListRecord &FieldList,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    printTypeEnd(Record);
  });

  if (Error Err = visitFieldListMemberStream(TI, Element, FieldList.Data))
    return Err;

  return Error::success();
}

// LF_FUNC_ID (TPI)/(IPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, FuncIdRecord &Func,
                                         TypeIndex TI, LVElement *Element) {
  // ParentScope and FunctionType are references into the TPI stream.
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamIPI);
    printTypeIndex("ParentScope", Func.getParentScope(), StreamTPI);
    printTypeIndex("FunctionType", Func.getFunctionType(), StreamTPI);
    W.printString("Name", Func.getName());
    printTypeEnd(Record);
  });

  // The TypeIndex (LF_PROCEDURE) returned by 'getFunctionType' is the
  // function propotype, we need to use the function definition.
  if (LVScope *FunctionDcl = static_cast<LVScope *>(Element)) {
    // For inlined functions, the inlined instance has been already processed
    // (all its information is contained in the Symbols section).
    // 'Element' points to the created 'abstract' (out-of-line) function.
    // Use the parent scope information to allocate it to the correct scope.
    LazyRandomTypeCollection &Types = types();
    TypeIndex TIParent = Func.getParentScope();
    if (FunctionDcl->getIsInlinedAbstract()) {
      FunctionDcl->setName(Func.getName());
      if (TIParent.isNoneType())
        Reader->getCompileUnit()->addElement(FunctionDcl);
    }

    if (!TIParent.isNoneType()) {
      CVType CVParentScope = ids().getType(TIParent);
      if (Error Err = finishVisitation(CVParentScope, TIParent, FunctionDcl))
        return Err;
    }

    TypeIndex TIFunctionType = Func.getFunctionType();
    CVType CVFunctionType = Types.getType(TIFunctionType);
    if (Error Err =
            finishVisitation(CVFunctionType, TIFunctionType, FunctionDcl))
      return Err;

    FunctionDcl->setIsFinalized();
  }

  return Error::success();
}

// LF_LABEL (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, LabelRecord &LR,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    printTypeEnd(Record);
  });
  return Error::success();
}

// LF_MFUNC_ID (TPI)/(IPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, MemberFuncIdRecord &Id,
                                         TypeIndex TI, LVElement *Element) {
  // ClassType and FunctionType are references into the TPI stream.
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamIPI);
    printTypeIndex("ClassType", Id.getClassType(), StreamTPI);
    printTypeIndex("FunctionType", Id.getFunctionType(), StreamTPI);
    W.printString("Name", Id.getName());
    printTypeEnd(Record);
  });

  LVScope *FunctionDcl = static_cast<LVScope *>(Element);
  if (FunctionDcl->getIsInlinedAbstract()) {
    // For inlined functions, the inlined instance has been already processed
    // (all its information is contained in the Symbols section).
    // 'Element' points to the created 'abstract' (out-of-line) function.
    // Use the parent scope information to allocate it to the correct scope.
    if (LVScope *Class = static_cast<LVScope *>(
            Shared->TypeRecords.find(StreamTPI, Id.getClassType())))
      Class->addElement(FunctionDcl);
  }

  TypeIndex TIFunctionType = Id.getFunctionType();
  CVType CVFunction = types().getType(TIFunctionType);
  if (Error Err = finishVisitation(CVFunction, TIFunctionType, Element))
    return Err;

  return Error::success();
}

// LF_MFUNCTION (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record,
                                         MemberFunctionRecord &MF, TypeIndex TI,
                                         LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("ReturnType", MF.getReturnType(), StreamTPI);
    printTypeIndex("ClassType", MF.getClassType(), StreamTPI);
    printTypeIndex("ThisType", MF.getThisType(), StreamTPI);
    W.printNumber("NumParameters", MF.getParameterCount());
    printTypeIndex("ArgListType", MF.getArgumentList(), StreamTPI);
    W.printNumber("ThisAdjustment", MF.getThisPointerAdjustment());
    printTypeEnd(Record);
  });

  if (LVScope *MemberFunction = static_cast<LVScope *>(Element)) {
    LVElement *Class = getElement(StreamTPI, MF.getClassType());

    MemberFunction->setIsFinalized();
    MemberFunction->setType(getElement(StreamTPI, MF.getReturnType()));
    MemberFunction->setOffset(TI.getIndex());
    MemberFunction->setOffsetFromTypeIndex();

    if (ProcessArgumentList) {
      ProcessArgumentList = false;

      if (!MemberFunction->getIsStatic()) {
        LVElement *ThisPointer = getElement(StreamTPI, MF.getThisType());
        // When creating the 'this' pointer, check if it points to a reference.
        ThisPointer->setType(Class);
        LVSymbol *This =
            createParameter(ThisPointer, StringRef(), MemberFunction);
        This->setIsArtificial();
      }

      // Create formal parameters.
      LazyRandomTypeCollection &Types = types();
      CVType CVArguments = Types.getType(MF.getArgumentList());
      if (Error Err = finishVisitation(CVArguments, MF.getArgumentList(),
                                       MemberFunction))
        return Err;
    }
  }

  return Error::success();
}

// LF_METHODLIST (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record,
                                         MethodOverloadListRecord &Overloads,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    printTypeEnd(Record);
  });

  for (OneMethodRecord &Method : Overloads.Methods) {
    CVMemberRecord Record;
    Record.Kind = LF_METHOD;
    Method.Name = OverloadedMethodName;
    if (Error Err = visitKnownMember(Record, Method, TI, Element))
      return Err;
  }

  return Error::success();
}

// LF_MODIFIER (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, ModifierRecord &Mod,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("ModifiedType", Mod.getModifiedType(), StreamTPI);
    printTypeEnd(Record);
  });

  // Create the modified type, which will be attached to the type(s) that
  // contains the modifiers.
  LVElement *ModifiedType = getElement(StreamTPI, Mod.getModifiedType());

  // At this point the types recording the qualifiers do not have a
  // scope parent. They must be assigned to the current compile unit.
  LVScopeCompileUnit *CompileUnit = Reader->getCompileUnit();

  // The incoming element does not have a defined kind. Use the given
  // modifiers to complete its type. A type can have more than one modifier;
  // in that case, we have to create an extra type to have the other modifier.
  LVType *LastLink = static_cast<LVType *>(Element);
  if (!LastLink->getParentScope())
    CompileUnit->addElement(LastLink);

  bool SeenModifier = false;
  uint16_t Mods = static_cast<uint16_t>(Mod.getModifiers());
  if (Mods & uint16_t(ModifierOptions::Const)) {
    SeenModifier = true;
    LastLink->setTag(dwarf::DW_TAG_const_type);
    LastLink->setIsConst();
    LastLink->setName("const");
  }
  if (Mods & uint16_t(ModifierOptions::Volatile)) {
    if (SeenModifier) {
      LVType *Volatile = Reader->createType();
      Volatile->setIsModifier();
      LastLink->setType(Volatile);
      LastLink = Volatile;
      CompileUnit->addElement(LastLink);
    }
    LastLink->setTag(dwarf::DW_TAG_volatile_type);
    LastLink->setIsVolatile();
    LastLink->setName("volatile");
  }
  if (Mods & uint16_t(ModifierOptions::Unaligned)) {
    if (SeenModifier) {
      LVType *Unaligned = Reader->createType();
      Unaligned->setIsModifier();
      LastLink->setType(Unaligned);
      LastLink = Unaligned;
      CompileUnit->addElement(LastLink);
    }
    LastLink->setTag(dwarf::DW_TAG_unaligned);
    LastLink->setIsUnaligned();
    LastLink->setName("unaligned");
  }

  LastLink->setType(ModifiedType);
  return Error::success();
}

// LF_POINTER (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, PointerRecord &Ptr,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("PointeeType", Ptr.getReferentType(), StreamTPI);
    W.printNumber("IsFlat", Ptr.isFlat());
    W.printNumber("IsConst", Ptr.isConst());
    W.printNumber("IsVolatile", Ptr.isVolatile());
    W.printNumber("IsUnaligned", Ptr.isUnaligned());
    W.printNumber("IsRestrict", Ptr.isRestrict());
    W.printNumber("IsThisPtr&", Ptr.isLValueReferenceThisPtr());
    W.printNumber("IsThisPtr&&", Ptr.isRValueReferenceThisPtr());
    W.printNumber("SizeOf", Ptr.getSize());

    if (Ptr.isPointerToMember()) {
      const MemberPointerInfo &MI = Ptr.getMemberInfo();
      printTypeIndex("ClassType", MI.getContainingType(), StreamTPI);
    }
    printTypeEnd(Record);
  });

  // Find the pointed-to type.
  LVType *Pointer = static_cast<LVType *>(Element);
  LVElement *Pointee = nullptr;

  PointerMode Mode = Ptr.getMode();
  Pointee = Ptr.isPointerToMember()
                ? Shared->TypeRecords.find(StreamTPI, Ptr.getReferentType())
                : getElement(StreamTPI, Ptr.getReferentType());

  // At this point the types recording the qualifiers do not have a
  // scope parent. They must be assigned to the current compile unit.
  LVScopeCompileUnit *CompileUnit = Reader->getCompileUnit();

  // Order for the different modifiers:
  // <restrict> <pointer, Reference, ValueReference> <const, volatile>
  // Const and volatile already processed.
  bool SeenModifier = false;
  LVType *LastLink = Pointer;
  if (!LastLink->getParentScope())
    CompileUnit->addElement(LastLink);

  if (Ptr.isRestrict()) {
    SeenModifier = true;
    LVType *Restrict = Reader->createType();
    Restrict->setTag(dwarf::DW_TAG_restrict_type);
    Restrict->setIsRestrict();
    Restrict->setName("restrict");
    LastLink->setType(Restrict);
    LastLink = Restrict;
    CompileUnit->addElement(LastLink);
  }
  if (Mode == PointerMode::LValueReference) {
    if (SeenModifier) {
      LVType *LReference = Reader->createType();
      LReference->setIsModifier();
      LastLink->setType(LReference);
      LastLink = LReference;
      CompileUnit->addElement(LastLink);
    }
    LastLink->setTag(dwarf::DW_TAG_reference_type);
    LastLink->setIsReference();
    LastLink->setName("&");
  }
  if (Mode == PointerMode::RValueReference) {
    if (SeenModifier) {
      LVType *RReference = Reader->createType();
      RReference->setIsModifier();
      LastLink->setType(RReference);
      LastLink = RReference;
      CompileUnit->addElement(LastLink);
    }
    LastLink->setTag(dwarf::DW_TAG_rvalue_reference_type);
    LastLink->setIsRvalueReference();
    LastLink->setName("&&");
  }

  // When creating the pointer, check if it points to a reference.
  LastLink->setType(Pointee);
  return Error::success();
}

// LF_PROCEDURE (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, ProcedureRecord &Proc,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("ReturnType", Proc.getReturnType(), StreamTPI);
    W.printNumber("NumParameters", Proc.getParameterCount());
    printTypeIndex("ArgListType", Proc.getArgumentList(), StreamTPI);
    printTypeEnd(Record);
  });

  // There is no need to traverse the argument list, as the CodeView format
  // declares the parameters as a 'S_LOCAL' symbol tagged as parameter.
  // Only process parameters when dealing with inline functions.
  if (LVScope *FunctionDcl = static_cast<LVScope *>(Element)) {
    FunctionDcl->setType(getElement(StreamTPI, Proc.getReturnType()));

    if (ProcessArgumentList) {
      ProcessArgumentList = false;
      // Create formal parameters.
      LazyRandomTypeCollection &Types = types();
      CVType CVArguments = Types.getType(Proc.getArgumentList());
      if (Error Err = finishVisitation(CVArguments, Proc.getArgumentList(),
                                       FunctionDcl))
        return Err;
    }
  }

  return Error::success();
}

// LF_UNION (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, UnionRecord &Union,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    W.printNumber("MemberCount", Union.getMemberCount());
    printTypeIndex("FieldList", Union.getFieldList(), StreamTPI);
    W.printNumber("SizeOf", Union.getSize());
    W.printString("Name", Union.getName());
    if (Union.hasUniqueName())
      W.printString("UniqueName", Union.getUniqueName());
    printTypeEnd(Record);
  });

  LVScopeAggregate *Scope = static_cast<LVScopeAggregate *>(Element);
  if (!Scope)
    return Error::success();

  if (Scope->getIsFinalized())
    return Error::success();
  Scope->setIsFinalized();

  Scope->setName(Union.getName());
  if (Union.hasUniqueName())
    Scope->setLinkageName(Union.getUniqueName());

  if (Union.isNested()) {
    Scope->setIsNested();
    createParents(Union.getName(), Scope);
  } else {
    if (LVScope *Namespace = Shared->NamespaceDeduction.get(Union.getName()))
      Namespace->addElement(Scope);
    else
      Reader->getCompileUnit()->addElement(Scope);
  }

  if (!Union.getFieldList().isNoneType()) {
    LazyRandomTypeCollection &Types = types();
    // Pass down the TypeIndex 'TI' for the aggregate containing the field list.
    CVType CVFieldList = Types.getType(Union.getFieldList());
    if (Error Err = finishVisitation(CVFieldList, TI, Scope))
      return Err;
  }

  return Error::success();
}

// LF_TYPESERVER2 (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, TypeServer2Record &TS,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    W.printString("Guid", formatv("{0}", TS.getGuid()).str());
    W.printNumber("Age", TS.getAge());
    W.printString("Name", TS.getName());
    printTypeEnd(Record);
  });
  return Error::success();
}

// LF_VFTABLE (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, VFTableRecord &VFT,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("CompleteClass", VFT.getCompleteClass(), StreamTPI);
    printTypeIndex("OverriddenVFTable", VFT.getOverriddenVTable(), StreamTPI);
    W.printHex("VFPtrOffset", VFT.getVFPtrOffset());
    W.printString("VFTableName", VFT.getName());
    for (const StringRef &N : VFT.getMethodNames())
      W.printString("MethodName", N);
    printTypeEnd(Record);
  });
  return Error::success();
}

// LF_VTSHAPE (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record,
                                         VFTableShapeRecord &Shape,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    W.printNumber("VFEntryCount", Shape.getEntryCount());
    printTypeEnd(Record);
  });
  return Error::success();
}

// LF_SUBSTR_LIST (TPI)/(IPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record,
                                         StringListRecord &Strings,
                                         TypeIndex TI, LVElement *Element) {
  // All the indices are references into the TPI/IPI stream.
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamIPI);
    ArrayRef<TypeIndex> Indices = Strings.getIndices();
    uint32_t Size = Indices.size();
    W.printNumber("NumStrings", Size);
    ListScope Arguments(W, "Strings");
    for (uint32_t I = 0; I < Size; ++I)
      printTypeIndex("String", Indices[I], StreamIPI);
    printTypeEnd(Record);
  });
  return Error::success();
}

// LF_STRING_ID (TPI)/(IPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, StringIdRecord &String,
                                         TypeIndex TI, LVElement *Element) {
  // All args are references into the TPI/IPI stream.
  LLVM_DEBUG({
    printTypeIndex("\nTI", TI, StreamIPI);
    printTypeIndex("Id", String.getId(), StreamIPI);
    W.printString("StringData", String.getString());
  });

  if (LVScope *Namespace = Shared->NamespaceDeduction.get(
          String.getString(), /*CheckScope=*/false)) {
    // The function is already at different scope. In order to reflect
    // the correct parent, move it to the namespace.
    if (LVScope *Scope = Element->getParentScope())
      Scope->removeElement(Element);
    Namespace->addElement(Element);
  }

  return Error::success();
}

// LF_UDT_SRC_LINE (TPI)/(IPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record,
                                         UdtSourceLineRecord &SourceLine,
                                         TypeIndex TI, LVElement *Element) {
  // All args are references into the TPI/IPI stream.
  LLVM_DEBUG({
    printTypeIndex("\nTI", TI, StreamIPI);
    printTypeIndex("UDT", SourceLine.getUDT(), StreamIPI);
    printTypeIndex("SourceFile", SourceLine.getSourceFile(), StreamIPI);
    W.printNumber("LineNumber", SourceLine.getLineNumber());
  });
  return Error::success();
}

// LF_UDT_MOD_SRC_LINE (TPI)/(IPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record,
                                         UdtModSourceLineRecord &ModSourceLine,
                                         TypeIndex TI, LVElement *Element) {
  // All args are references into the TPI/IPI stream.
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamIPI);
    printTypeIndex("\nTI", TI, StreamIPI);
    printTypeIndex("UDT", ModSourceLine.getUDT(), StreamIPI);
    printTypeIndex("SourceFile", ModSourceLine.getSourceFile(), StreamIPI);
    W.printNumber("LineNumber", ModSourceLine.getLineNumber());
    W.printNumber("Module", ModSourceLine.getModule());
    printTypeEnd(Record);
  });
  return Error::success();
}

// LF_PRECOMP (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record, PrecompRecord &Precomp,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    W.printHex("StartIndex", Precomp.getStartTypeIndex());
    W.printHex("Count", Precomp.getTypesCount());
    W.printHex("Signature", Precomp.getSignature());
    W.printString("PrecompFile", Precomp.getPrecompFilePath());
    printTypeEnd(Record);
  });
  return Error::success();
}

// LF_ENDPRECOMP (TPI)
Error LVLogicalVisitor::visitKnownRecord(CVType &Record,
                                         EndPrecompRecord &EndPrecomp,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printTypeBegin(Record, TI, Element, StreamTPI);
    W.printHex("Signature", EndPrecomp.getSignature());
    printTypeEnd(Record);
  });
  return Error::success();
}

Error LVLogicalVisitor::visitUnknownMember(CVMemberRecord &Record,
                                           TypeIndex TI) {
  LLVM_DEBUG({ W.printHex("UnknownMember", unsigned(Record.Kind)); });
  return Error::success();
}

// LF_BCLASS, LF_BINTERFACE
Error LVLogicalVisitor::visitKnownMember(CVMemberRecord &Record,
                                         BaseClassRecord &Base, TypeIndex TI,
                                         LVElement *Element) {
  LLVM_DEBUG({
    printMemberBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("BaseType", Base.getBaseType(), StreamTPI);
    W.printHex("BaseOffset", Base.getBaseOffset());
    printMemberEnd(Record);
  });

  createElement(Record.Kind);
  if (LVSymbol *Symbol = CurrentSymbol) {
    LVElement *BaseClass = getElement(StreamTPI, Base.getBaseType());
    Symbol->setName(BaseClass->getName());
    Symbol->setType(BaseClass);
    Symbol->setAccessibilityCode(Base.getAccess());
    static_cast<LVScope *>(Element)->addElement(Symbol);
  }

  return Error::success();
}

// LF_MEMBER
Error LVLogicalVisitor::visitKnownMember(CVMemberRecord &Record,
                                         DataMemberRecord &Field, TypeIndex TI,
                                         LVElement *Element) {
  LLVM_DEBUG({
    printMemberBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("Type", Field.getType(), StreamTPI);
    W.printHex("FieldOffset", Field.getFieldOffset());
    W.printString("Name", Field.getName());
    printMemberEnd(Record);
  });

  // Create the data member.
  createDataMember(Record, static_cast<LVScope *>(Element), Field.getName(),
                   Field.getType(), Field.getAccess());
  return Error::success();
}

// LF_ENUMERATE
Error LVLogicalVisitor::visitKnownMember(CVMemberRecord &Record,
                                         EnumeratorRecord &Enum, TypeIndex TI,
                                         LVElement *Element) {
  LLVM_DEBUG({
    printMemberBegin(Record, TI, Element, StreamTPI);
    W.printNumber("EnumValue", Enum.getValue());
    W.printString("Name", Enum.getName());
    printMemberEnd(Record);
  });

  createElement(Record.Kind);
  if (LVType *Type = CurrentType) {
    Type->setName(Enum.getName());
    SmallString<16> Value;
    Enum.getValue().toString(Value, 16, true, true);
    Type->setValue(Value);
    static_cast<LVScope *>(Element)->addElement(CurrentType);
  }

  return Error::success();
}

// LF_INDEX
Error LVLogicalVisitor::visitKnownMember(CVMemberRecord &Record,
                                         ListContinuationRecord &Cont,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printMemberBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("ContinuationIndex", Cont.getContinuationIndex(), StreamTPI);
    printMemberEnd(Record);
  });
  return Error::success();
}

// LF_NESTTYPE
Error LVLogicalVisitor::visitKnownMember(CVMemberRecord &Record,
                                         NestedTypeRecord &Nested, TypeIndex TI,
                                         LVElement *Element) {
  LLVM_DEBUG({
    printMemberBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("Type", Nested.getNestedType(), StreamTPI);
    W.printString("Name", Nested.getName());
    printMemberEnd(Record);
  });

  if (LVElement *Typedef = createElement(SymbolKind::S_UDT)) {
    Typedef->setName(Nested.getName());
    LVElement *NestedType = getElement(StreamTPI, Nested.getNestedType());
    Typedef->setType(NestedType);
    LVScope *Scope = static_cast<LVScope *>(Element);
    Scope->addElement(Typedef);

    if (NestedType && NestedType->getIsNested()) {
      // 'Element' is an aggregate type that may contains this nested type
      // definition. Used their scoped names, to decide on their relationship.
      StringRef RecordName = getRecordName(types(), TI);

      StringRef NestedTypeName = NestedType->getName();
      if (NestedTypeName.size() && RecordName.size()) {
        StringRef OuterComponent;
        std::tie(OuterComponent, std::ignore) =
            getInnerComponent(NestedTypeName);
        // We have an already created nested type. Add it to the current scope
        // and update all its children if any.
        if (OuterComponent.size() && OuterComponent == RecordName) {
          if (!NestedType->getIsScopedAlready()) {
            Scope->addElement(NestedType);
            NestedType->setIsScopedAlready();
            NestedType->updateLevel(Scope);
          }
          Typedef->resetIncludeInPrint();
        }
      }
    }
  }

  return Error::success();
}

// LF_ONEMETHOD
Error LVLogicalVisitor::visitKnownMember(CVMemberRecord &Record,
                                         OneMethodRecord &Method, TypeIndex TI,
                                         LVElement *Element) {
  LLVM_DEBUG({
    printMemberBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("Type", Method.getType(), StreamTPI);
    // If virtual, then read the vftable offset.
    if (Method.isIntroducingVirtual())
      W.printHex("VFTableOffset", Method.getVFTableOffset());
    W.printString("Name", Method.getName());
    printMemberEnd(Record);
  });

  // All the LF_ONEMETHOD objects share the same type description.
  // We have to create a scope object for each one and get the required
  // information from the LF_MFUNCTION object.
  ProcessArgumentList = true;
  if (LVElement *MemberFunction = createElement(TypeLeafKind::LF_ONEMETHOD)) {
    MemberFunction->setIsFinalized();
    static_cast<LVScope *>(Element)->addElement(MemberFunction);

    MemberFunction->setName(Method.getName());
    MemberFunction->setAccessibilityCode(Method.getAccess());

    MethodKind Kind = Method.getMethodKind();
    if (Kind == MethodKind::Static)
      MemberFunction->setIsStatic();
    MemberFunction->setVirtualityCode(Kind);

    MethodOptions Flags = Method.Attrs.getFlags();
    if (MethodOptions::CompilerGenerated ==
        (Flags & MethodOptions::CompilerGenerated))
      MemberFunction->setIsArtificial();

    LazyRandomTypeCollection &Types = types();
    CVType CVMethodType = Types.getType(Method.getType());
    if (Error Err =
            finishVisitation(CVMethodType, Method.getType(), MemberFunction))
      return Err;
  }
  ProcessArgumentList = false;

  return Error::success();
}

// LF_METHOD
Error LVLogicalVisitor::visitKnownMember(CVMemberRecord &Record,
                                         OverloadedMethodRecord &Method,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printMemberBegin(Record, TI, Element, StreamTPI);
    W.printHex("MethodCount", Method.getNumOverloads());
    printTypeIndex("MethodListIndex", Method.getMethodList(), StreamTPI);
    W.printString("Name", Method.getName());
    printMemberEnd(Record);
  });

  // Record the overloaded method name, which will be used during the
  // traversal of the method list.
  LazyRandomTypeCollection &Types = types();
  OverloadedMethodName = Method.getName();
  CVType CVMethods = Types.getType(Method.getMethodList());
  if (Error Err = finishVisitation(CVMethods, Method.getMethodList(), Element))
    return Err;

  return Error::success();
}

// LF_STMEMBER
Error LVLogicalVisitor::visitKnownMember(CVMemberRecord &Record,
                                         StaticDataMemberRecord &Field,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printMemberBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("Type", Field.getType(), StreamTPI);
    W.printString("Name", Field.getName());
    printMemberEnd(Record);
  });

  // Create the data member.
  createDataMember(Record, static_cast<LVScope *>(Element), Field.getName(),
                   Field.getType(), Field.getAccess());
  return Error::success();
}

// LF_VFUNCTAB
Error LVLogicalVisitor::visitKnownMember(CVMemberRecord &Record,
                                         VFPtrRecord &VFTable, TypeIndex TI,
                                         LVElement *Element) {
  LLVM_DEBUG({
    printMemberBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("Type", VFTable.getType(), StreamTPI);
    printMemberEnd(Record);
  });
  return Error::success();
}

// LF_VBCLASS, LF_IVBCLASS
Error LVLogicalVisitor::visitKnownMember(CVMemberRecord &Record,
                                         VirtualBaseClassRecord &Base,
                                         TypeIndex TI, LVElement *Element) {
  LLVM_DEBUG({
    printMemberBegin(Record, TI, Element, StreamTPI);
    printTypeIndex("BaseType", Base.getBaseType(), StreamTPI);
    printTypeIndex("VBPtrType", Base.getVBPtrType(), StreamTPI);
    W.printHex("VBPtrOffset", Base.getVBPtrOffset());
    W.printHex("VBTableIndex", Base.getVTableIndex());
    printMemberEnd(Record);
  });

  createElement(Record.Kind);
  if (LVSymbol *Symbol = CurrentSymbol) {
    LVElement *BaseClass = getElement(StreamTPI, Base.getBaseType());
    Symbol->setName(BaseClass->getName());
    Symbol->setType(BaseClass);
    Symbol->setAccessibilityCode(Base.getAccess());
    Symbol->setVirtualityCode(MethodKind::Virtual);
    static_cast<LVScope *>(Element)->addElement(Symbol);
  }

  return Error::success();
}

Error LVLogicalVisitor::visitMemberRecord(CVMemberRecord &Record,
                                          TypeVisitorCallbacks &Callbacks,
                                          TypeIndex TI, LVElement *Element) {
  if (Error Err = Callbacks.visitMemberBegin(Record))
    return Err;

  switch (Record.Kind) {
  default:
    if (Error Err = Callbacks.visitUnknownMember(Record))
      return Err;
    break;
#define MEMBER_RECORD(EnumName, EnumVal, Name)                                 \
  case EnumName: {                                                             \
    if (Error Err =                                                            \
            visitKnownMember<Name##Record>(Record, Callbacks, TI, Element))    \
      return Err;                                                              \
    break;                                                                     \
  }
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)                \
  MEMBER_RECORD(EnumVal, EnumVal, AliasName)
#define TYPE_RECORD(EnumName, EnumVal, Name)
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
  }

  if (Error Err = Callbacks.visitMemberEnd(Record))
    return Err;

  return Error::success();
}

Error LVLogicalVisitor::finishVisitation(CVType &Record, TypeIndex TI,
                                         LVElement *Element) {
  switch (Record.kind()) {
  default:
    if (Error Err = visitUnknownType(Record, TI))
      return Err;
    break;
#define TYPE_RECORD(EnumName, EnumVal, Name)                                   \
  case EnumName: {                                                             \
    if (Error Err = visitKnownRecord<Name##Record>(Record, TI, Element))       \
      return Err;                                                              \
    break;                                                                     \
  }
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)                  \
  TYPE_RECORD(EnumVal, EnumVal, AliasName)
#define MEMBER_RECORD(EnumName, EnumVal, Name)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
  }

  return Error::success();
}

// Customized version of 'FieldListVisitHelper'.
Error LVLogicalVisitor::visitFieldListMemberStream(
    TypeIndex TI, LVElement *Element, ArrayRef<uint8_t> FieldList) {
  BinaryByteStream Stream(FieldList, llvm::endianness::little);
  BinaryStreamReader Reader(Stream);
  FieldListDeserializer Deserializer(Reader);
  TypeVisitorCallbackPipeline Pipeline;
  Pipeline.addCallbackToPipeline(Deserializer);

  TypeLeafKind Leaf;
  while (!Reader.empty()) {
    if (Error Err = Reader.readEnum(Leaf))
      return Err;

    CVMemberRecord Record;
    Record.Kind = Leaf;
    if (Error Err = visitMemberRecord(Record, Pipeline, TI, Element))
      return Err;
  }

  return Error::success();
}

void LVLogicalVisitor::addElement(LVScope *Scope, bool IsCompileUnit) {
  // The CodeView specifications does not treat S_COMPILE2 and S_COMPILE3
  // as symbols that open a scope. The CodeView reader, treat them in a
  // similar way as DWARF. As there is no a symbole S_END to close the
  // compile unit, we need to check for the next compile unit.
  if (IsCompileUnit) {
    if (!ScopeStack.empty())
      popScope();
    InCompileUnitScope = true;
  }

  pushScope(Scope);
  ReaderParent->addElement(Scope);
}

void LVLogicalVisitor::addElement(LVSymbol *Symbol) {
  ReaderScope->addElement(Symbol);
}

void LVLogicalVisitor::addElement(LVType *Type) {
  ReaderScope->addElement(Type);
}

LVElement *LVLogicalVisitor::createElement(TypeLeafKind Kind) {
  CurrentScope = nullptr;
  CurrentSymbol = nullptr;
  CurrentType = nullptr;

  if (Kind < TypeIndex::FirstNonSimpleIndex) {
    CurrentType = Reader->createType();
    CurrentType->setIsBase();
    CurrentType->setTag(dwarf::DW_TAG_base_type);
    if (options().getAttributeBase())
      CurrentType->setIncludeInPrint();
    return CurrentType;
  }

  switch (Kind) {
  // Types.
  case TypeLeafKind::LF_ENUMERATE:
    CurrentType = Reader->createTypeEnumerator();
    CurrentType->setTag(dwarf::DW_TAG_enumerator);
    return CurrentType;
  case TypeLeafKind::LF_MODIFIER:
    CurrentType = Reader->createType();
    CurrentType->setIsModifier();
    return CurrentType;
  case TypeLeafKind::LF_POINTER:
    CurrentType = Reader->createType();
    CurrentType->setIsPointer();
    CurrentType->setName("*");
    CurrentType->setTag(dwarf::DW_TAG_pointer_type);
    return CurrentType;

    // Symbols.
  case TypeLeafKind::LF_BCLASS:
  case TypeLeafKind::LF_IVBCLASS:
  case TypeLeafKind::LF_VBCLASS:
    CurrentSymbol = Reader->createSymbol();
    CurrentSymbol->setTag(dwarf::DW_TAG_inheritance);
    CurrentSymbol->setIsInheritance();
    return CurrentSymbol;
  case TypeLeafKind::LF_MEMBER:
  case TypeLeafKind::LF_STMEMBER:
    CurrentSymbol = Reader->createSymbol();
    CurrentSymbol->setIsMember();
    CurrentSymbol->setTag(dwarf::DW_TAG_member);
    return CurrentSymbol;

  // Scopes.
  case TypeLeafKind::LF_ARRAY:
    CurrentScope = Reader->createScopeArray();
    CurrentScope->setTag(dwarf::DW_TAG_array_type);
    return CurrentScope;
  case TypeLeafKind::LF_CLASS:
    CurrentScope = Reader->createScopeAggregate();
    CurrentScope->setTag(dwarf::DW_TAG_class_type);
    CurrentScope->setIsClass();
    return CurrentScope;
  case TypeLeafKind::LF_ENUM:
    CurrentScope = Reader->createScopeEnumeration();
    CurrentScope->setTag(dwarf::DW_TAG_enumeration_type);
    return CurrentScope;
  case TypeLeafKind::LF_METHOD:
  case TypeLeafKind::LF_ONEMETHOD:
  case TypeLeafKind::LF_PROCEDURE:
    CurrentScope = Reader->createScopeFunction();
    CurrentScope->setIsSubprogram();
    CurrentScope->setTag(dwarf::DW_TAG_subprogram);
    return CurrentScope;
  case TypeLeafKind::LF_STRUCTURE:
    CurrentScope = Reader->createScopeAggregate();
    CurrentScope->setIsStructure();
    CurrentScope->setTag(dwarf::DW_TAG_structure_type);
    return CurrentScope;
  case TypeLeafKind::LF_UNION:
    CurrentScope = Reader->createScopeAggregate();
    CurrentScope->setIsUnion();
    CurrentScope->setTag(dwarf::DW_TAG_union_type);
    return CurrentScope;
  default:
    // If '--internal=tag' and '--print=warning' are specified in the command
    // line, we record and print each seen 'TypeLeafKind'.
    break;
  }
  return nullptr;
}

LVElement *LVLogicalVisitor::createElement(SymbolKind Kind) {
  CurrentScope = nullptr;
  CurrentSymbol = nullptr;
  CurrentType = nullptr;
  switch (Kind) {
  // Types.
  case SymbolKind::S_UDT:
    CurrentType = Reader->createTypeDefinition();
    CurrentType->setTag(dwarf::DW_TAG_typedef);
    return CurrentType;

  // Symbols.
  case SymbolKind::S_CONSTANT:
    CurrentSymbol = Reader->createSymbol();
    CurrentSymbol->setIsConstant();
    CurrentSymbol->setTag(dwarf::DW_TAG_constant);
    return CurrentSymbol;

  case SymbolKind::S_BPREL32:
  case SymbolKind::S_REGREL32:
  case SymbolKind::S_GDATA32:
  case SymbolKind::S_LDATA32:
  case SymbolKind::S_LOCAL:
    // During the symbol traversal more information is available to
    // determine if the symbol is a parameter or a variable. At this
    // stage mark it as variable.
    CurrentSymbol = Reader->createSymbol();
    CurrentSymbol->setIsVariable();
    CurrentSymbol->setTag(dwarf::DW_TAG_variable);
    return CurrentSymbol;

  // Scopes.
  case SymbolKind::S_BLOCK32:
    CurrentScope = Reader->createScope();
    CurrentScope->setIsLexicalBlock();
    CurrentScope->setTag(dwarf::DW_TAG_lexical_block);
    return CurrentScope;
  case SymbolKind::S_COMPILE2:
  case SymbolKind::S_COMPILE3:
    CurrentScope = Reader->createScopeCompileUnit();
    CurrentScope->setTag(dwarf::DW_TAG_compile_unit);
    Reader->setCompileUnit(static_cast<LVScopeCompileUnit *>(CurrentScope));
    return CurrentScope;
  case SymbolKind::S_INLINESITE:
  case SymbolKind::S_INLINESITE2:
    CurrentScope = Reader->createScopeFunctionInlined();
    CurrentScope->setIsInlinedFunction();
    CurrentScope->setTag(dwarf::DW_TAG_inlined_subroutine);
    return CurrentScope;
  case SymbolKind::S_LPROC32:
  case SymbolKind::S_GPROC32:
  case SymbolKind::S_LPROC32_ID:
  case SymbolKind::S_GPROC32_ID:
  case SymbolKind::S_SEPCODE:
  case SymbolKind::S_THUNK32:
    CurrentScope = Reader->createScopeFunction();
    CurrentScope->setIsSubprogram();
    CurrentScope->setTag(dwarf::DW_TAG_subprogram);
    return CurrentScope;
  default:
    // If '--internal=tag' and '--print=warning' are specified in the command
    // line, we record and print each seen 'SymbolKind'.
    break;
  }
  return nullptr;
}

LVElement *LVLogicalVisitor::createElement(TypeIndex TI, TypeLeafKind Kind) {
  LVElement *Element = Shared->TypeRecords.find(StreamTPI, TI);
  if (!Element) {
    // We are dealing with a base type or pointer to a base type, which are
    // not included explicitly in the CodeView format.
    if (Kind < TypeIndex::FirstNonSimpleIndex) {
      Element = createElement(Kind);
      Element->setIsFinalized();
      Shared->TypeRecords.add(StreamTPI, (TypeIndex)Kind, Kind, Element);
      Element->setOffset(Kind);
      return Element;
    }
    // We are dealing with a pointer to a base type.
    if (TI.getIndex() < TypeIndex::FirstNonSimpleIndex) {
      Element = createElement(Kind);
      Shared->TypeRecords.add(StreamTPI, TI, Kind, Element);
      Element->setOffset(TI.getIndex());
      Element->setOffsetFromTypeIndex();
      return Element;
    }

    W.printString("** Not implemented. **");
    printTypeIndex("TypeIndex", TI, StreamTPI);
    W.printString("TypeLeafKind", formatTypeLeafKind(Kind));
    return nullptr;
  }

  Element->setOffset(TI.getIndex());
  Element->setOffsetFromTypeIndex();
  return Element;
}

void LVLogicalVisitor::createDataMember(CVMemberRecord &Record, LVScope *Parent,
                                        StringRef Name, TypeIndex TI,
                                        MemberAccess Access) {
  LLVM_DEBUG({
    printTypeIndex("TypeIndex", TI, StreamTPI);
    W.printString("TypeName", Name);
  });

  createElement(Record.Kind);
  if (LVSymbol *Symbol = CurrentSymbol) {
    Symbol->setName(Name);
    if (TI.isNoneType() || TI.isSimple())
      Symbol->setType(getElement(StreamTPI, TI));
    else {
      LazyRandomTypeCollection &Types = types();
      CVType CVMemberType = Types.getType(TI);
      if (CVMemberType.kind() == LF_BITFIELD) {
        if (Error Err = finishVisitation(CVMemberType, TI, Symbol)) {
          consumeError(std::move(Err));
          return;
        }
      } else
        Symbol->setType(getElement(StreamTPI, TI));
    }
    Symbol->setAccessibilityCode(Access);
    Parent->addElement(Symbol);
  }
}

LVSymbol *LVLogicalVisitor::createParameter(LVElement *Element, StringRef Name,
                                            LVScope *Parent) {
  LVSymbol *Parameter = Reader->createSymbol();
  Parent->addElement(Parameter);
  Parameter->setIsParameter();
  Parameter->setTag(dwarf::DW_TAG_formal_parameter);
  Parameter->setName(Name);
  Parameter->setType(Element);
  return Parameter;
}

LVSymbol *LVLogicalVisitor::createParameter(TypeIndex TI, StringRef Name,
                                            LVScope *Parent) {
  return createParameter(getElement(StreamTPI, TI), Name, Parent);
}

LVType *LVLogicalVisitor::createBaseType(TypeIndex TI, StringRef TypeName) {
  TypeLeafKind SimpleKind = (TypeLeafKind)TI.getSimpleKind();
  TypeIndex TIR = (TypeIndex)SimpleKind;
  LLVM_DEBUG({
    printTypeIndex("TypeIndex", TIR, StreamTPI);
    W.printString("TypeName", TypeName);
  });

  if (LVElement *Element = Shared->TypeRecords.find(StreamTPI, TIR))
    return static_cast<LVType *>(Element);

  if (createElement(TIR, SimpleKind)) {
    CurrentType->setName(TypeName);
    Reader->getCompileUnit()->addElement(CurrentType);
  }
  return CurrentType;
}

LVType *LVLogicalVisitor::createPointerType(TypeIndex TI, StringRef TypeName) {
  LLVM_DEBUG({
    printTypeIndex("TypeIndex", TI, StreamTPI);
    W.printString("TypeName", TypeName);
  });

  if (LVElement *Element = Shared->TypeRecords.find(StreamTPI, TI))
    return static_cast<LVType *>(Element);

  LVType *Pointee = createBaseType(TI, TypeName.drop_back(1));
  if (createElement(TI, TypeLeafKind::LF_POINTER)) {
    CurrentType->setIsFinalized();
    CurrentType->setType(Pointee);
    Reader->getCompileUnit()->addElement(CurrentType);
  }
  return CurrentType;
}

void LVLogicalVisitor::createParents(StringRef ScopedName, LVElement *Element) {
  // For the given test case:
  //
  // struct S { enum E { ... }; };
  // S::E V;
  //
  //      0 | S_LOCAL `V`
  //          type=0x1004 (S::E), flags = none
  // 0x1004 | LF_ENUM  `S::E`
  //          options: has unique name | is nested
  // 0x1009 | LF_STRUCTURE `S`
  //          options: contains nested class
  //
  // When the local 'V' is processed, its type 'E' is created. But There is
  // no direct reference to its parent 'S'. We use the scoped name for 'E',
  // to create its parents.

  // The input scoped name must have at least parent and nested names.
  // Drop the last element name, as it corresponds to the nested type.
  LVStringRefs Components = getAllLexicalComponents(ScopedName);
  if (Components.size() < 2)
    return;
  Components.pop_back();

  LVStringRefs::size_type FirstNamespace;
  LVStringRefs::size_type FirstAggregate;
  std::tie(FirstNamespace, FirstAggregate) =
      Shared->NamespaceDeduction.find(Components);

  LLVM_DEBUG({
    W.printString("First Namespace", Components[FirstNamespace]);
    W.printString("First NonNamespace", Components[FirstAggregate]);
  });

  // Create any referenced namespaces.
  if (FirstNamespace < FirstAggregate) {
    Shared->NamespaceDeduction.get(
        LVStringRefs(Components.begin() + FirstNamespace,
                     Components.begin() + FirstAggregate));
  }

  // Traverse the enclosing scopes (aggregates) and create them. In the
  // case of nested empty aggregates, MSVC does not emit a full record
  // description. It emits only the reference record.
  LVScope *Aggregate = nullptr;
  TypeIndex TIAggregate;
  std::string AggregateName = getScopedName(
      LVStringRefs(Components.begin(), Components.begin() + FirstAggregate));

  // This traversal is executed at least once.
  for (LVStringRefs::size_type Index = FirstAggregate;
       Index < Components.size(); ++Index) {
    AggregateName = getScopedName(LVStringRefs(Components.begin() + Index,
                                               Components.begin() + Index + 1),
                                  AggregateName);
    TIAggregate = Shared->ForwardReferences.remap(
        Shared->TypeRecords.find(StreamTPI, AggregateName));
    Aggregate =
        TIAggregate.isNoneType()
            ? nullptr
            : static_cast<LVScope *>(getElement(StreamTPI, TIAggregate));
  }

  // Workaround for cases where LF_NESTTYPE is missing for nested templates.
  // If we manage to get parent information from the scoped name, we can add
  // the nested type without relying on the LF_NESTTYPE.
  if (Aggregate && !Element->getIsScopedAlready()) {
    Aggregate->addElement(Element);
    Element->setIsScopedAlready();
  }
}

LVElement *LVLogicalVisitor::getElement(uint32_t StreamIdx, TypeIndex TI,
                                        LVScope *Parent) {
  LLVM_DEBUG({ printTypeIndex("TypeIndex", TI, StreamTPI); });
  TI = Shared->ForwardReferences.remap(TI);
  LLVM_DEBUG({ printTypeIndex("TypeIndex Remap", TI, StreamTPI); });

  LVElement *Element = Shared->TypeRecords.find(StreamIdx, TI);
  if (!Element) {
    if (TI.isNoneType() || TI.isSimple()) {
      StringRef TypeName = TypeIndex::simpleTypeName(TI);
      // If the name ends with "*", create 2 logical types: a pointer and a
      // pointee type. TypeIndex is composed of a SympleTypeMode byte followed
      // by a SimpleTypeKind byte. The logical pointer will be identified by
      // the full TypeIndex value and the pointee by the SimpleTypeKind.
      return (TypeName.back() == '*') ? createPointerType(TI, TypeName)
                                      : createBaseType(TI, TypeName);
    }

    LLVM_DEBUG({ W.printHex("TypeIndex not implemented: ", TI.getIndex()); });
    return nullptr;
  }

  // The element has been finalized.
  if (Element->getIsFinalized())
    return Element;

  // Add the element in case of a given parent.
  if (Parent)
    Parent->addElement(Element);

  // Check for a composite type.
  LazyRandomTypeCollection &Types = types();
  CVType CVRecord = Types.getType(TI);
  if (Error Err = finishVisitation(CVRecord, TI, Element)) {
    consumeError(std::move(Err));
    return nullptr;
  }
  Element->setIsFinalized();
  return Element;
}

void LVLogicalVisitor::processLines() {
  // Traverse the collected LF_UDT_SRC_LINE records and add the source line
  // information to the logical elements.
  for (const TypeIndex &Entry : Shared->LineRecords) {
    CVType CVRecord = ids().getType(Entry);
    UdtSourceLineRecord Line;
    if (Error Err = TypeDeserializer::deserializeAs(
            const_cast<CVType &>(CVRecord), Line))
      consumeError(std::move(Err));
    else {
      LLVM_DEBUG({
        printTypeIndex("UDT", Line.getUDT(), StreamIPI);
        printTypeIndex("SourceFile", Line.getSourceFile(), StreamIPI);
        W.printNumber("LineNumber", Line.getLineNumber());
      });

      // The TypeIndex returned by 'getUDT()' must point to an already
      // created logical element. If no logical element is found, it means
      // the LF_UDT_SRC_LINE is associated with a system TypeIndex.
      if (LVElement *Element = Shared->TypeRecords.find(
              StreamTPI, Line.getUDT(), /*Create=*/false)) {
        Element->setLineNumber(Line.getLineNumber());
        Element->setFilenameIndex(
            Shared->StringRecords.findIndex(Line.getSourceFile()));
      }
    }
  }
}

void LVLogicalVisitor::processNamespaces() {
  // Create namespaces.
  Shared->NamespaceDeduction.init();
}

void LVLogicalVisitor::processFiles() { Shared->StringRecords.addFilenames(); }

void LVLogicalVisitor::printRecords(raw_ostream &OS) const {
  if (!options().getInternalTag())
    return;

  unsigned Count = 0;
  auto PrintItem = [&](StringRef Name) {
    auto NewLine = [&]() {
      if (++Count == 4) {
        Count = 0;
        OS << "\n";
      }
    };
    OS << format("%20s", Name.str().c_str());
    NewLine();
  };

  OS << "\nTypes:\n";
  for (const TypeLeafKind &Kind : Shared->TypeKinds)
    PrintItem(formatTypeLeafKind(Kind));
  Shared->TypeKinds.clear();

  Count = 0;
  OS << "\nSymbols:\n";
  for (const SymbolKind &Kind : Shared->SymbolKinds)
    PrintItem(LVCodeViewReader::getSymbolKindName(Kind));
  Shared->SymbolKinds.clear();

  OS << "\n";
}

Error LVLogicalVisitor::inlineSiteAnnotation(LVScope *AbstractFunction,
                                             LVScope *InlinedFunction,
                                             InlineSiteSym &InlineSite) {
  // Get the parent scope to update the address ranges of the nested
  // scope representing the inlined function.
  LVAddress ParentLowPC = 0;
  LVScope *Parent = InlinedFunction->getParentScope();
  if (const LVLocations *Locations = Parent->getRanges()) {
    if (!Locations->empty())
      ParentLowPC = (*Locations->begin())->getLowerAddress();
  }

  // For the given inlinesite, get the initial line number and its
  // source filename. Update the logical scope representing it.
  uint32_t LineNumber = 0;
  StringRef Filename;
  LVInlineeInfo::iterator Iter = InlineeInfo.find(InlineSite.Inlinee);
  if (Iter != InlineeInfo.end()) {
    LineNumber = Iter->second.first;
    Filename = Iter->second.second;
    AbstractFunction->setLineNumber(LineNumber);
    // TODO: This part needs additional work in order to set properly the
    // correct filename in order to detect changes between filenames.
    // AbstractFunction->setFilename(Filename);
  }

  LLVM_DEBUG({
    dbgs() << "inlineSiteAnnotation\n"
           << "Abstract: " << AbstractFunction->getName() << "\n"
           << "Inlined: " << InlinedFunction->getName() << "\n"
           << "Parent: " << Parent->getName() << "\n"
           << "Low PC: " << hexValue(ParentLowPC) << "\n";
  });

  // Get the source lines if requested by command line option.
  if (!options().getPrintLines())
    return Error::success();

  // Limitation: Currently we don't track changes in the FileOffset. The
  // side effects are the caller that it is unable to differentiate the
  // source filename for the inlined code.
  uint64_t CodeOffset = ParentLowPC;
  int32_t LineOffset = LineNumber;
  uint32_t FileOffset = 0;

  auto UpdateClose = [&]() { LLVM_DEBUG({ dbgs() << ("\n"); }); };
  auto UpdateCodeOffset = [&](uint32_t Delta) {
    CodeOffset += Delta;
    LLVM_DEBUG({
      dbgs() << formatv(" code 0x{0} (+0x{1})", utohexstr(CodeOffset),
                        utohexstr(Delta));
    });
  };
  auto UpdateLineOffset = [&](int32_t Delta) {
    LineOffset += Delta;
    LLVM_DEBUG({
      char Sign = Delta > 0 ? '+' : '-';
      dbgs() << formatv(" line {0} ({1}{2})", LineOffset, Sign,
                        std::abs(Delta));
    });
  };
  auto UpdateFileOffset = [&](int32_t Offset) {
    FileOffset = Offset;
    LLVM_DEBUG({ dbgs() << formatv(" file {0}", FileOffset); });
  };

  LVLines InlineeLines;
  auto CreateLine = [&]() {
    // Create the logical line record.
    LVLineDebug *Line = Reader->createLineDebug();
    Line->setAddress(CodeOffset);
    Line->setLineNumber(LineOffset);
    // TODO: This part needs additional work in order to set properly the
    // correct filename in order to detect changes between filenames.
    // Line->setFilename(Filename);
    InlineeLines.push_back(Line);
  };

  bool SeenLowAddress = false;
  bool SeenHighAddress = false;
  uint64_t LowPC = 0;
  uint64_t HighPC = 0;

  for (auto &Annot : InlineSite.annotations()) {
    LLVM_DEBUG({
      dbgs() << formatv("  {0}",
                        fmt_align(toHex(Annot.Bytes), AlignStyle::Left, 9));
    });

    // Use the opcode to interpret the integer values.
    switch (Annot.OpCode) {
    case BinaryAnnotationsOpCode::ChangeCodeOffset:
    case BinaryAnnotationsOpCode::CodeOffset:
    case BinaryAnnotationsOpCode::ChangeCodeLength:
      UpdateCodeOffset(Annot.U1);
      UpdateClose();
      if (Annot.OpCode == BinaryAnnotationsOpCode::ChangeCodeOffset) {
        CreateLine();
        LowPC = CodeOffset;
        SeenLowAddress = true;
        break;
      }
      if (Annot.OpCode == BinaryAnnotationsOpCode::ChangeCodeLength) {
        HighPC = CodeOffset - 1;
        SeenHighAddress = true;
      }
      break;
    case BinaryAnnotationsOpCode::ChangeCodeLengthAndCodeOffset:
      UpdateCodeOffset(Annot.U2);
      UpdateClose();
      break;
    case BinaryAnnotationsOpCode::ChangeLineOffset:
    case BinaryAnnotationsOpCode::ChangeCodeOffsetAndLineOffset:
      UpdateCodeOffset(Annot.U1);
      UpdateLineOffset(Annot.S1);
      UpdateClose();
      if (Annot.OpCode ==
          BinaryAnnotationsOpCode::ChangeCodeOffsetAndLineOffset)
        CreateLine();
      break;
    case BinaryAnnotationsOpCode::ChangeFile:
      UpdateFileOffset(Annot.U1);
      UpdateClose();
      break;
    default:
      break;
    }
    if (SeenLowAddress && SeenHighAddress) {
      SeenLowAddress = false;
      SeenHighAddress = false;
      InlinedFunction->addObject(LowPC, HighPC);
    }
  }

  Reader->addInlineeLines(InlinedFunction, InlineeLines);
  UpdateClose();

  return Error::success();
}

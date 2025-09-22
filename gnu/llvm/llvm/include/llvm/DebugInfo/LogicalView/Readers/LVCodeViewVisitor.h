//===-- LVCodeViewVisitor.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVCodeViewVisitor class, which is used to describe a
// debug information (CodeView) visitor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_READERS_CODEVIEWVISITOR_H
#define LLVM_DEBUGINFO_LOGICALVIEW_READERS_CODEVIEWVISITOR_H

#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/CodeView/SymbolDumpDelegate.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorCallbacks.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include "llvm/DebugInfo/LogicalView/Readers/LVBinaryReader.h"
#include "llvm/DebugInfo/PDB/Native/InputFile.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include <stack>
#include <utility>

namespace llvm {
namespace logicalview {

using namespace llvm::codeview;

class LVCodeViewReader;
class LVLogicalVisitor;
struct LVShared;

class LVTypeVisitor final : public TypeVisitorCallbacks {
  ScopedPrinter &W;
  LVLogicalVisitor *LogicalVisitor;
  LazyRandomTypeCollection &Types;
  LazyRandomTypeCollection &Ids;
  uint32_t StreamIdx;
  LVShared *Shared = nullptr;

  // In a PDB, a type index may refer to a type (TPI) or an item ID (IPI).
  // In a COFF or PDB (/Z7), the type index always refer to a type (TPI).
  // When creating logical elements, we must access the correct element
  // table, while searching for a type index.
  bool HasIds = false;

  // Current type index during the types traversal.
  TypeIndex CurrentTypeIndex = TypeIndex::None();

  void printTypeIndex(StringRef FieldName, TypeIndex TI,
                      uint32_t StreamIdx) const;

public:
  LVTypeVisitor(ScopedPrinter &W, LVLogicalVisitor *LogicalVisitor,
                LazyRandomTypeCollection &Types, LazyRandomTypeCollection &Ids,
                uint32_t StreamIdx, LVShared *Shared)
      : TypeVisitorCallbacks(), W(W), LogicalVisitor(LogicalVisitor),
        Types(Types), Ids(Ids), StreamIdx(StreamIdx), Shared(Shared) {
    HasIds = &Types != &Ids;
  }

  Error visitTypeBegin(CVType &Record) override;
  Error visitTypeBegin(CVType &Record, TypeIndex TI) override;
  Error visitMemberBegin(CVMemberRecord &Record) override;
  Error visitMemberEnd(CVMemberRecord &Record) override;
  Error visitUnknownMember(CVMemberRecord &Record) override;

  Error visitKnownRecord(CVType &Record, BuildInfoRecord &Args) override;
  Error visitKnownRecord(CVType &Record, ClassRecord &Class) override;
  Error visitKnownRecord(CVType &Record, EnumRecord &Enum) override;
  Error visitKnownRecord(CVType &Record, FuncIdRecord &Func) override;
  Error visitKnownRecord(CVType &Record, ProcedureRecord &Proc) override;
  Error visitKnownRecord(CVType &Record, StringIdRecord &String) override;
  Error visitKnownRecord(CVType &Record, UdtSourceLineRecord &Line) override;
  Error visitKnownRecord(CVType &Record, UnionRecord &Union) override;
  Error visitUnknownType(CVType &Record) override;
};

class LVSymbolVisitorDelegate final : public SymbolVisitorDelegate {
  LVCodeViewReader *Reader;
  const llvm::object::coff_section *CoffSection;
  StringRef SectionContents;

public:
  LVSymbolVisitorDelegate(LVCodeViewReader *Reader,
                          const llvm::object::SectionRef &Section,
                          const llvm::object::COFFObjectFile *Obj,
                          StringRef SectionContents)
      : Reader(Reader), SectionContents(SectionContents) {
    CoffSection = Obj->getCOFFSection(Section);
  }

  uint32_t getRecordOffset(BinaryStreamReader Reader) override {
    ArrayRef<uint8_t> Data;
    if (Error Err = Reader.readLongestContiguousChunk(Data)) {
      llvm::consumeError(std::move(Err));
      return 0;
    }
    return Data.data() - SectionContents.bytes_begin();
  }

  void printRelocatedField(StringRef Label, uint32_t RelocOffset,
                           uint32_t Offset, StringRef *RelocSym = nullptr);

  void getLinkageName(uint32_t RelocOffset, uint32_t Offset,
                      StringRef *RelocSym = nullptr);

  StringRef getFileNameForFileOffset(uint32_t FileOffset) override;
  DebugStringTableSubsectionRef getStringTable() override;
};

class LVElement;
class LVScope;
class LVSymbol;
class LVType;

// Visitor for CodeView symbol streams found in COFF object files and PDB files.
class LVSymbolVisitor final : public SymbolVisitorCallbacks {
  LVCodeViewReader *Reader;
  ScopedPrinter &W;
  LVLogicalVisitor *LogicalVisitor;
  LazyRandomTypeCollection &Types;
  LazyRandomTypeCollection &Ids;
  LVSymbolVisitorDelegate *ObjDelegate;
  LVShared *Shared;

  // Symbol offset when processing PDB streams.
  uint32_t CurrentOffset = 0;
  // Current object name collected from S_OBJNAME.
  StringRef CurrentObjectName;
  // Last symbol processed by S_LOCAL.
  LVSymbol *LocalSymbol = nullptr;

  bool HasIds;
  bool InFunctionScope = false;
  bool IsCompileUnit = false;

  // Register for the locals and parameters symbols in the current frame.
  RegisterId LocalFrameRegister = RegisterId::NONE;
  RegisterId ParamFrameRegister = RegisterId::NONE;

  void printLocalVariableAddrRange(const LocalVariableAddrRange &Range,
                                   uint32_t RelocationOffset);
  void printLocalVariableAddrGap(ArrayRef<LocalVariableAddrGap> Gaps);
  void printTypeIndex(StringRef FieldName, TypeIndex TI) const;

  // Return true if this symbol is a Compile Unit.
  bool symbolIsCompileUnit(SymbolKind Kind) {
    switch (Kind) {
    case SymbolKind::S_COMPILE2:
    case SymbolKind::S_COMPILE3:
      return true;
    default:
      return false;
    }
  }

  // Determine symbol kind (local or parameter).
  void determineSymbolKind(LVSymbol *Symbol, RegisterId Register) {
    if (Register == LocalFrameRegister) {
      Symbol->setIsVariable();
      return;
    }
    if (Register == ParamFrameRegister) {
      Symbol->setIsParameter();
      return;
    }
    // Assume is a variable.
    Symbol->setIsVariable();
  }

public:
  LVSymbolVisitor(LVCodeViewReader *Reader, ScopedPrinter &W,
                  LVLogicalVisitor *LogicalVisitor,
                  LazyRandomTypeCollection &Types,
                  LazyRandomTypeCollection &Ids,
                  LVSymbolVisitorDelegate *ObjDelegate, LVShared *Shared)
      : Reader(Reader), W(W), LogicalVisitor(LogicalVisitor), Types(Types),
        Ids(Ids), ObjDelegate(ObjDelegate), Shared(Shared) {
    HasIds = &Types != &Ids;
  }

  Error visitSymbolBegin(CVSymbol &Record) override;
  Error visitSymbolBegin(CVSymbol &Record, uint32_t Offset) override;
  Error visitSymbolEnd(CVSymbol &Record) override;
  Error visitUnknownSymbol(CVSymbol &Record) override;

  Error visitKnownRecord(CVSymbol &Record, BlockSym &Block) override;
  Error visitKnownRecord(CVSymbol &Record, BPRelativeSym &Local) override;
  Error visitKnownRecord(CVSymbol &Record, BuildInfoSym &BuildInfo) override;
  Error visitKnownRecord(CVSymbol &Record, Compile2Sym &Compile2) override;
  Error visitKnownRecord(CVSymbol &Record, Compile3Sym &Compile3) override;
  Error visitKnownRecord(CVSymbol &Record, ConstantSym &Constant) override;
  Error visitKnownRecord(CVSymbol &Record, DataSym &Data) override;
  Error visitKnownRecord(CVSymbol &Record,
                         DefRangeFramePointerRelFullScopeSym
                             &DefRangeFramePointerRelFullScope) override;
  Error visitKnownRecord(
      CVSymbol &Record,
      DefRangeFramePointerRelSym &DefRangeFramePointerRel) override;
  Error visitKnownRecord(CVSymbol &Record,
                         DefRangeRegisterRelSym &DefRangeRegisterRel) override;
  Error visitKnownRecord(CVSymbol &Record,
                         DefRangeRegisterSym &DefRangeRegister) override;
  Error visitKnownRecord(
      CVSymbol &Record,
      DefRangeSubfieldRegisterSym &DefRangeSubfieldRegister) override;
  Error visitKnownRecord(CVSymbol &Record,
                         DefRangeSubfieldSym &DefRangeSubfield) override;
  Error visitKnownRecord(CVSymbol &Record, DefRangeSym &DefRange) override;
  Error visitKnownRecord(CVSymbol &Record, FrameProcSym &FrameProc) override;
  Error visitKnownRecord(CVSymbol &Record, InlineSiteSym &InlineSite) override;
  Error visitKnownRecord(CVSymbol &Record, LocalSym &Local) override;
  Error visitKnownRecord(CVSymbol &Record, ObjNameSym &ObjName) override;
  Error visitKnownRecord(CVSymbol &Record, ProcSym &Proc) override;
  Error visitKnownRecord(CVSymbol &Record, RegRelativeSym &Local) override;
  Error visitKnownRecord(CVSymbol &Record, ScopeEndSym &ScopeEnd) override;
  Error visitKnownRecord(CVSymbol &Record, Thunk32Sym &Thunk) override;
  Error visitKnownRecord(CVSymbol &Record, UDTSym &UDT) override;
  Error visitKnownRecord(CVSymbol &Record, UsingNamespaceSym &UN) override;
  Error visitKnownRecord(CVSymbol &Record, JumpTableSym &JumpTable) override;
  Error visitKnownRecord(CVSymbol &Record, CallerSym &Caller) override;
};

// Visitor for CodeView types and symbols to populate elements.
class LVLogicalVisitor final {
  LVCodeViewReader *Reader;
  ScopedPrinter &W;

  // Encapsulates access to the input file and any dependent type server,
  // including any precompiled header object.
  llvm::pdb::InputFile &Input;
  std::shared_ptr<llvm::pdb::InputFile> TypeServer = nullptr;
  std::shared_ptr<LazyRandomTypeCollection> PrecompHeader = nullptr;

  std::shared_ptr<LVShared> Shared;

  // Object files have only one type stream that contains both types and ids.
  // Precompiled header objects don't contain an IPI stream. Use the TPI.
  LazyRandomTypeCollection &types() {
    return TypeServer ? TypeServer->types()
                      : (PrecompHeader ? *PrecompHeader : Input.types());
  }
  LazyRandomTypeCollection &ids() {
    return TypeServer ? TypeServer->ids()
                      : (PrecompHeader ? *PrecompHeader : Input.ids());
  }

  using LVScopeStack = std::stack<LVScope *>;
  LVScopeStack ScopeStack;
  LVScope *ReaderParent = nullptr;
  LVScope *ReaderScope = nullptr;
  bool InCompileUnitScope = false;

  // Allow processing of argument list.
  bool ProcessArgumentList = false;
  StringRef OverloadedMethodName;
  std::string CompileUnitName;

  // Inlined functions source information.
  using LVInlineeEntry = std::pair<uint32_t, StringRef>;
  using LVInlineeInfo = std::map<TypeIndex, LVInlineeEntry>;
  LVInlineeInfo InlineeInfo;

  Error visitFieldListMemberStream(TypeIndex TI, LVElement *Element,
                                   ArrayRef<uint8_t> FieldList);

  LVType *createBaseType(TypeIndex TI, StringRef TypeName);
  LVType *createPointerType(TypeIndex TI, StringRef TypeName);
  LVSymbol *createParameter(TypeIndex TI, StringRef Name, LVScope *Parent);
  LVSymbol *createParameter(LVElement *Element, StringRef Name,
                            LVScope *Parent);
  void createDataMember(CVMemberRecord &Record, LVScope *Parent, StringRef Name,
                        TypeIndex Type, MemberAccess Access);
  void createParents(StringRef ScopedName, LVElement *Element);

public:
  LVLogicalVisitor(LVCodeViewReader *Reader, ScopedPrinter &W,
                   llvm::pdb::InputFile &Input);

  // Current elements during the processing of a RecordType or RecordSymbol.
  // They are shared with the SymbolVisitor.
  LVElement *CurrentElement = nullptr;
  LVScope *CurrentScope = nullptr;
  LVSymbol *CurrentSymbol = nullptr;
  LVType *CurrentType = nullptr;

  // Input source in the case of type server or precompiled header.
  void setInput(std::shared_ptr<llvm::pdb::InputFile> TypeServer) {
    this->TypeServer = TypeServer;
  }
  void setInput(std::shared_ptr<LazyRandomTypeCollection> PrecompHeader) {
    this->PrecompHeader = PrecompHeader;
  }

  void addInlineeInfo(TypeIndex TI, uint32_t LineNumber, StringRef Filename) {
    InlineeInfo.emplace(std::piecewise_construct, std::forward_as_tuple(TI),
                        std::forward_as_tuple(LineNumber, Filename));
  }

  void printTypeIndex(StringRef FieldName, TypeIndex TI, uint32_t StreamIdx);
  void printMemberAttributes(MemberAttributes Attrs);
  void printMemberAttributes(MemberAccess Access, MethodKind Kind,
                             MethodOptions Options);

  LVElement *createElement(TypeLeafKind Kind);
  LVElement *createElement(SymbolKind Kind);
  LVElement *createElement(TypeIndex TI, TypeLeafKind Kind);

  // Break down the annotation byte code and calculate code and line offsets.
  Error inlineSiteAnnotation(LVScope *AbstractFunction,
                             LVScope *InlinedFunction,
                             InlineSiteSym &InlineSite);

  void pushScope(LVScope *Scope) {
    ScopeStack.push(ReaderParent);
    ReaderParent = ReaderScope;
    ReaderScope = Scope;
  }
  void popScope() {
    ReaderScope = ReaderParent;
    ReaderParent = ScopeStack.top();
    ScopeStack.pop();
  }
  void closeScope() {
    if (InCompileUnitScope) {
      InCompileUnitScope = false;
      popScope();
    }
  }
  void setRoot(LVScope *Root) { ReaderScope = Root; }

  void addElement(LVScope *Scope, bool IsCompileUnit);
  void addElement(LVSymbol *Symbol);
  void addElement(LVType *Type);

  std::string getCompileUnitName() { return CompileUnitName; }
  void setCompileUnitName(std::string Name) {
    CompileUnitName = std::move(Name);
  }

  LVElement *getElement(uint32_t StreamIdx, TypeIndex TI,
                        LVScope *Parent = nullptr);
  LVShared *getShared() { return Shared.get(); }

  LVScope *getReaderScope() const { return ReaderScope; }

  void printTypeBegin(CVType &Record, TypeIndex TI, LVElement *Element,
                      uint32_t StreamIdx);
  void printTypeEnd(CVType &Record);
  void printMemberBegin(CVMemberRecord &Record, TypeIndex TI,
                        LVElement *Element, uint32_t StreamIdx);
  void printMemberEnd(CVMemberRecord &Record);

  void startProcessArgumentList() { ProcessArgumentList = true; }
  void stopProcessArgumentList() { ProcessArgumentList = false; }

  void processFiles();
  void processLines();
  void processNamespaces();

  void printRecords(raw_ostream &OS) const;

  Error visitUnknownType(CVType &Record, TypeIndex TI);
  Error visitKnownRecord(CVType &Record, ArgListRecord &Args, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, ArrayRecord &AT, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, BitFieldRecord &BF, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, BuildInfoRecord &BI, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, ClassRecord &Class, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, EnumRecord &Enum, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, FieldListRecord &FieldList,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownRecord(CVType &Record, FuncIdRecord &Func, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, LabelRecord &LR, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, ModifierRecord &Mod, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, MemberFuncIdRecord &Id, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, MemberFunctionRecord &MF, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, MethodOverloadListRecord &Overloads,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownRecord(CVType &Record, PointerRecord &Ptr, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, ProcedureRecord &Proc, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, UnionRecord &Union, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, TypeServer2Record &TS, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, VFTableRecord &VFT, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, VFTableShapeRecord &Shape,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownRecord(CVType &Record, StringListRecord &Strings,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownRecord(CVType &Record, StringIdRecord &String, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, UdtSourceLineRecord &SourceLine,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownRecord(CVType &Record, UdtModSourceLineRecord &ModSourceLine,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownRecord(CVType &Record, PrecompRecord &Precomp, TypeIndex TI,
                         LVElement *Element);
  Error visitKnownRecord(CVType &Record, EndPrecompRecord &EndPrecomp,
                         TypeIndex TI, LVElement *Element);

  Error visitUnknownMember(CVMemberRecord &Record, TypeIndex TI);
  Error visitKnownMember(CVMemberRecord &Record, BaseClassRecord &Base,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownMember(CVMemberRecord &Record, DataMemberRecord &Field,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownMember(CVMemberRecord &Record, EnumeratorRecord &Enum,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownMember(CVMemberRecord &Record, ListContinuationRecord &Cont,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownMember(CVMemberRecord &Record, NestedTypeRecord &Nested,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownMember(CVMemberRecord &Record, OneMethodRecord &Method,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownMember(CVMemberRecord &Record, OverloadedMethodRecord &Method,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownMember(CVMemberRecord &Record, StaticDataMemberRecord &Field,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownMember(CVMemberRecord &Record, VFPtrRecord &VFTable,
                         TypeIndex TI, LVElement *Element);
  Error visitKnownMember(CVMemberRecord &Record, VirtualBaseClassRecord &Base,
                         TypeIndex TI, LVElement *Element);

  template <typename T>
  Error visitKnownMember(CVMemberRecord &Record,
                         TypeVisitorCallbacks &Callbacks, TypeIndex TI,
                         LVElement *Element) {
    TypeRecordKind RK = static_cast<TypeRecordKind>(Record.Kind);
    T KnownRecord(RK);
    if (Error Err = Callbacks.visitKnownMember(Record, KnownRecord))
      return Err;
    if (Error Err = visitKnownMember(Record, KnownRecord, TI, Element))
      return Err;
    return Error::success();
  }

  template <typename T>
  Error visitKnownRecord(CVType &Record, TypeIndex TI, LVElement *Element) {
    TypeRecordKind RK = static_cast<TypeRecordKind>(Record.kind());
    T KnownRecord(RK);
    if (Error Err = TypeDeserializer::deserializeAs(
            const_cast<CVType &>(Record), KnownRecord))
      return Err;
    if (Error Err = visitKnownRecord(Record, KnownRecord, TI, Element))
      return Err;
    return Error::success();
  }

  Error visitMemberRecord(CVMemberRecord &Record,
                          TypeVisitorCallbacks &Callbacks, TypeIndex TI,
                          LVElement *Element);
  Error finishVisitation(CVType &Record, TypeIndex TI, LVElement *Element);
};

} // namespace logicalview
} // namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_READERS_CODEVIEWVISITOR_H

//===- llvm/lib/CodeGen/AsmPrinter/CodeViewDebug.h --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing Microsoft CodeView debug info.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_CODEVIEWDEBUG_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_CODEVIEWDEBUG_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/DbgEntityHistoryCalculator.h"
#include "llvm/CodeGen/DebugHandlerBase.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/GlobalTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace llvm {

struct ClassInfo;
class StringRef;
class AsmPrinter;
class Function;
class GlobalVariable;
class MCSectionCOFF;
class MCStreamer;
class MCSymbol;
class MachineFunction;

/// Collects and handles line tables information in a CodeView format.
class LLVM_LIBRARY_VISIBILITY CodeViewDebug : public DebugHandlerBase {
public:
  struct LocalVarDef {
    /// Indicates that variable data is stored in memory relative to the
    /// specified register.
    int InMemory : 1;

    /// Offset of variable data in memory.
    int DataOffset : 31;

    /// Non-zero if this is a piece of an aggregate.
    uint16_t IsSubfield : 1;

    /// Offset into aggregate.
    uint16_t StructOffset : 15;

    /// Register containing the data or the register base of the memory
    /// location containing the data.
    uint16_t CVRegister;

    uint64_t static toOpaqueValue(const LocalVarDef DR) {
      uint64_t Val = 0;
      std::memcpy(&Val, &DR, sizeof(Val));
      return Val;
    }

    LocalVarDef static createFromOpaqueValue(uint64_t Val) {
      LocalVarDef DR;
      std::memcpy(&DR, &Val, sizeof(Val));
      return DR;
    }
  };

  static_assert(sizeof(uint64_t) == sizeof(LocalVarDef));

private:
  MCStreamer &OS;
  BumpPtrAllocator Allocator;
  codeview::GlobalTypeTableBuilder TypeTable;

  /// Whether to emit type record hashes into .debug$H.
  bool EmitDebugGlobalHashes = false;

  /// The codeview CPU type used by the translation unit.
  codeview::CPUType TheCPU;

  static LocalVarDef createDefRangeMem(uint16_t CVRegister, int Offset);

  /// Similar to DbgVariable in DwarfDebug, but not dwarf-specific.
  struct LocalVariable {
    const DILocalVariable *DIVar = nullptr;
    MapVector<LocalVarDef,
              SmallVector<std::pair<const MCSymbol *, const MCSymbol *>, 1>>
        DefRanges;
    bool UseReferenceType = false;
    std::optional<APSInt> ConstantValue;
  };

  struct CVGlobalVariable {
    const DIGlobalVariable *DIGV;
    PointerUnion<const GlobalVariable *, const DIExpression *> GVInfo;
  };

  struct InlineSite {
    SmallVector<LocalVariable, 1> InlinedLocals;
    SmallVector<const DILocation *, 1> ChildSites;
    const DISubprogram *Inlinee = nullptr;

    /// The ID of the inline site or function used with .cv_loc. Not a type
    /// index.
    unsigned SiteFuncId = 0;
  };

  // Combines information from DILexicalBlock and LexicalScope.
  struct LexicalBlock {
    SmallVector<LocalVariable, 1> Locals;
    SmallVector<CVGlobalVariable, 1> Globals;
    SmallVector<LexicalBlock *, 1> Children;
    const MCSymbol *Begin;
    const MCSymbol *End;
    StringRef Name;
  };

  struct JumpTableInfo {
    codeview::JumpTableEntrySize EntrySize;
    const MCSymbol *Base;
    uint64_t BaseOffset;
    const MCSymbol *Branch;
    const MCSymbol *Table;
    size_t TableSize;
  };

  // For each function, store a vector of labels to its instructions, as well as
  // to the end of the function.
  struct FunctionInfo {
    FunctionInfo() = default;

    // Uncopyable.
    FunctionInfo(const FunctionInfo &FI) = delete;

    /// Map from inlined call site to inlined instructions and child inlined
    /// call sites. Listed in program order.
    std::unordered_map<const DILocation *, InlineSite> InlineSites;

    /// Ordered list of top-level inlined call sites.
    SmallVector<const DILocation *, 1> ChildSites;

    /// Set of all functions directly inlined into this one.
    SmallSet<codeview::TypeIndex, 1> Inlinees;

    SmallVector<LocalVariable, 1> Locals;
    SmallVector<CVGlobalVariable, 1> Globals;

    std::unordered_map<const DILexicalBlockBase*, LexicalBlock> LexicalBlocks;

    // Lexical blocks containing local variables.
    SmallVector<LexicalBlock *, 1> ChildBlocks;

    std::vector<std::pair<MCSymbol *, MDNode *>> Annotations;
    std::vector<std::tuple<const MCSymbol *, const MCSymbol *, const DIType *>>
        HeapAllocSites;

    std::vector<JumpTableInfo> JumpTables;

    const MCSymbol *Begin = nullptr;
    const MCSymbol *End = nullptr;
    unsigned FuncId = 0;
    unsigned LastFileId = 0;

    /// Number of bytes allocated in the prologue for all local stack objects.
    unsigned FrameSize = 0;

    /// Number of bytes of parameters on the stack.
    unsigned ParamSize = 0;

    /// Number of bytes pushed to save CSRs.
    unsigned CSRSize = 0;

    /// Adjustment to apply on x86 when using the VFRAME frame pointer.
    int OffsetAdjustment = 0;

    /// Two-bit value indicating which register is the designated frame pointer
    /// register for local variables. Included in S_FRAMEPROC.
    codeview::EncodedFramePtrReg EncodedLocalFramePtrReg =
        codeview::EncodedFramePtrReg::None;

    /// Two-bit value indicating which register is the designated frame pointer
    /// register for stack parameters. Included in S_FRAMEPROC.
    codeview::EncodedFramePtrReg EncodedParamFramePtrReg =
        codeview::EncodedFramePtrReg::None;

    codeview::FrameProcedureOptions FrameProcOpts;

    bool HasStackRealignment = false;

    bool HaveLineInfo = false;

    bool HasFramePointer = false;
  };
  FunctionInfo *CurFn = nullptr;

  codeview::SourceLanguage CurrentSourceLanguage =
      codeview::SourceLanguage::Masm;

  // This map records the constant offset in DIExpression of the
  // DIGlobalVariableExpression referencing the DIGlobalVariable.
  DenseMap<const DIGlobalVariable *, uint64_t> CVGlobalVariableOffsets;

  // Map used to separate variables according to the lexical scope they belong
  // in.  This is populated by recordLocalVariable() before
  // collectLexicalBlocks() separates the variables between the FunctionInfo
  // and LexicalBlocks.
  DenseMap<const LexicalScope *, SmallVector<LocalVariable, 1>> ScopeVariables;

  // Map to separate global variables according to the lexical scope they
  // belong in. A null local scope represents the global scope.
  typedef SmallVector<CVGlobalVariable, 1> GlobalVariableList;
  DenseMap<const DIScope*, std::unique_ptr<GlobalVariableList> > ScopeGlobals;

  // Array of global variables which  need to be emitted into a COMDAT section.
  SmallVector<CVGlobalVariable, 1> ComdatVariables;

  // Array of non-COMDAT global variables.
  SmallVector<CVGlobalVariable, 1> GlobalVariables;

  /// List of static const data members to be emitted as S_CONSTANTs.
  SmallVector<const DIDerivedType *, 4> StaticConstMembers;

  /// The set of comdat .debug$S sections that we've seen so far. Each section
  /// must start with a magic version number that must only be emitted once.
  /// This set tracks which sections we've already opened.
  DenseSet<MCSectionCOFF *> ComdatDebugSections;

  /// Switch to the appropriate .debug$S section for GVSym. If GVSym, the symbol
  /// of an emitted global value, is in a comdat COFF section, this will switch
  /// to a new .debug$S section in that comdat. This method ensures that the
  /// section starts with the magic version number on first use. If GVSym is
  /// null, uses the main .debug$S section.
  void switchToDebugSectionForSymbol(const MCSymbol *GVSym);

  /// The next available function index for use with our .cv_* directives. Not
  /// to be confused with type indices for LF_FUNC_ID records.
  unsigned NextFuncId = 0;

  InlineSite &getInlineSite(const DILocation *InlinedAt,
                            const DISubprogram *Inlinee);

  codeview::TypeIndex getFuncIdForSubprogram(const DISubprogram *SP);

  void calculateRanges(LocalVariable &Var,
                       const DbgValueHistoryMap::Entries &Entries);

  /// Remember some debug info about each function. Keep it in a stable order to
  /// emit at the end of the TU.
  MapVector<const Function *, std::unique_ptr<FunctionInfo>> FnDebugInfo;

  /// Map from full file path to .cv_file id. Full paths are built from DIFiles
  /// and are stored in FileToFilepathMap;
  DenseMap<StringRef, unsigned> FileIdMap;

  /// All inlined subprograms in the order they should be emitted.
  SmallSetVector<const DISubprogram *, 4> InlinedSubprograms;

  /// Map from a pair of DI metadata nodes and its DI type (or scope) that can
  /// be nullptr, to CodeView type indices. Primarily indexed by
  /// {DIType*, DIType*} and {DISubprogram*, DIType*}.
  ///
  /// The second entry in the key is needed for methods as DISubroutineType
  /// representing static method type are shared with non-method function type.
  DenseMap<std::pair<const DINode *, const DIType *>, codeview::TypeIndex>
      TypeIndices;

  /// Map from DICompositeType* to complete type index. Non-record types are
  /// always looked up in the normal TypeIndices map.
  DenseMap<const DICompositeType *, codeview::TypeIndex> CompleteTypeIndices;

  /// Complete record types to emit after all active type lowerings are
  /// finished.
  SmallVector<const DICompositeType *, 4> DeferredCompleteTypes;

  /// Number of type lowering frames active on the stack.
  unsigned TypeEmissionLevel = 0;

  codeview::TypeIndex VBPType;

  const DISubprogram *CurrentSubprogram = nullptr;

  // The UDTs we have seen while processing types; each entry is a pair of type
  // index and type name.
  std::vector<std::pair<std::string, const DIType *>> LocalUDTs;
  std::vector<std::pair<std::string, const DIType *>> GlobalUDTs;

  using FileToFilepathMapTy = std::map<const DIFile *, std::string>;
  FileToFilepathMapTy FileToFilepathMap;

  StringRef getFullFilepath(const DIFile *File);

  unsigned maybeRecordFile(const DIFile *F);

  void maybeRecordLocation(const DebugLoc &DL, const MachineFunction *MF);

  void clear();

  void setCurrentSubprogram(const DISubprogram *SP) {
    CurrentSubprogram = SP;
    LocalUDTs.clear();
  }

  /// Emit the magic version number at the start of a CodeView type or symbol
  /// section. Appears at the front of every .debug$S or .debug$T or .debug$P
  /// section.
  void emitCodeViewMagicVersion();

  void emitTypeInformation();

  void emitTypeGlobalHashes();

  void emitObjName();

  void emitCompilerInformation();

  void emitBuildInfo();

  void emitInlineeLinesSubsection();

  void emitDebugInfoForThunk(const Function *GV,
                             FunctionInfo &FI,
                             const MCSymbol *Fn);

  void emitDebugInfoForFunction(const Function *GV, FunctionInfo &FI);

  void emitDebugInfoForRetainedTypes();

  void emitDebugInfoForUDTs(
      const std::vector<std::pair<std::string, const DIType *>> &UDTs);

  void collectDebugInfoForGlobals();
  void emitDebugInfoForGlobals();
  void emitGlobalVariableList(ArrayRef<CVGlobalVariable> Globals);
  void emitConstantSymbolRecord(const DIType *DTy, APSInt &Value,
                                const std::string &QualifiedName);
  void emitDebugInfoForGlobal(const CVGlobalVariable &CVGV);
  void emitStaticConstMemberList();

  /// Opens a subsection of the given kind in a .debug$S codeview section.
  /// Returns an end label for use with endCVSubsection when the subsection is
  /// finished.
  MCSymbol *beginCVSubsection(codeview::DebugSubsectionKind Kind);
  void endCVSubsection(MCSymbol *EndLabel);

  /// Opens a symbol record of the given kind. Returns an end label for use with
  /// endSymbolRecord.
  MCSymbol *beginSymbolRecord(codeview::SymbolKind Kind);
  void endSymbolRecord(MCSymbol *SymEnd);

  /// Emits an S_END, S_INLINESITE_END, or S_PROC_ID_END record. These records
  /// are empty, so we emit them with a simpler assembly sequence that doesn't
  /// involve labels.
  void emitEndSymbolRecord(codeview::SymbolKind EndKind);

  void emitInlinedCallSite(const FunctionInfo &FI, const DILocation *InlinedAt,
                           const InlineSite &Site);

  void emitInlinees(const SmallSet<codeview::TypeIndex, 1> &Inlinees);

  using InlinedEntity = DbgValueHistoryMap::InlinedEntity;

  void collectGlobalVariableInfo();
  void collectVariableInfo(const DISubprogram *SP);

  void collectVariableInfoFromMFTable(DenseSet<InlinedEntity> &Processed);

  // Construct the lexical block tree for a routine, pruning emptpy lexical
  // scopes, and populate it with local variables.
  void collectLexicalBlockInfo(SmallVectorImpl<LexicalScope *> &Scopes,
                               SmallVectorImpl<LexicalBlock *> &Blocks,
                               SmallVectorImpl<LocalVariable> &Locals,
                               SmallVectorImpl<CVGlobalVariable> &Globals);
  void collectLexicalBlockInfo(LexicalScope &Scope,
                               SmallVectorImpl<LexicalBlock *> &ParentBlocks,
                               SmallVectorImpl<LocalVariable> &ParentLocals,
                               SmallVectorImpl<CVGlobalVariable> &ParentGlobals);

  /// Records information about a local variable in the appropriate scope. In
  /// particular, locals from inlined code live inside the inlining site.
  void recordLocalVariable(LocalVariable &&Var, const LexicalScope *LS);

  /// Emits local variables in the appropriate order.
  void emitLocalVariableList(const FunctionInfo &FI,
                             ArrayRef<LocalVariable> Locals);

  /// Emits an S_LOCAL record and its associated defined ranges.
  void emitLocalVariable(const FunctionInfo &FI, const LocalVariable &Var);

  /// Emits a sequence of lexical block scopes and their children.
  void emitLexicalBlockList(ArrayRef<LexicalBlock *> Blocks,
                            const FunctionInfo& FI);

  /// Emit a lexical block scope and its children.
  void emitLexicalBlock(const LexicalBlock &Block, const FunctionInfo& FI);

  /// Translates the DIType to codeview if necessary and returns a type index
  /// for it.
  codeview::TypeIndex getTypeIndex(const DIType *Ty,
                                   const DIType *ClassTy = nullptr);

  codeview::TypeIndex
  getTypeIndexForThisPtr(const DIDerivedType *PtrTy,
                         const DISubroutineType *SubroutineTy);

  codeview::TypeIndex getTypeIndexForReferenceTo(const DIType *Ty);

  codeview::TypeIndex getMemberFunctionType(const DISubprogram *SP,
                                            const DICompositeType *Class);

  codeview::TypeIndex getScopeIndex(const DIScope *Scope);

  codeview::TypeIndex getVBPTypeIndex();

  void addToUDTs(const DIType *Ty);

  void addUDTSrcLine(const DIType *Ty, codeview::TypeIndex TI);

  codeview::TypeIndex lowerType(const DIType *Ty, const DIType *ClassTy);
  codeview::TypeIndex lowerTypeAlias(const DIDerivedType *Ty);
  codeview::TypeIndex lowerTypeArray(const DICompositeType *Ty);
  codeview::TypeIndex lowerTypeString(const DIStringType *Ty);
  codeview::TypeIndex lowerTypeBasic(const DIBasicType *Ty);
  codeview::TypeIndex lowerTypePointer(
      const DIDerivedType *Ty,
      codeview::PointerOptions PO = codeview::PointerOptions::None);
  codeview::TypeIndex lowerTypeMemberPointer(
      const DIDerivedType *Ty,
      codeview::PointerOptions PO = codeview::PointerOptions::None);
  codeview::TypeIndex lowerTypeModifier(const DIDerivedType *Ty);
  codeview::TypeIndex lowerTypeFunction(const DISubroutineType *Ty);
  codeview::TypeIndex lowerTypeVFTableShape(const DIDerivedType *Ty);
  codeview::TypeIndex lowerTypeMemberFunction(
      const DISubroutineType *Ty, const DIType *ClassTy, int ThisAdjustment,
      bool IsStaticMethod,
      codeview::FunctionOptions FO = codeview::FunctionOptions::None);
  codeview::TypeIndex lowerTypeEnum(const DICompositeType *Ty);
  codeview::TypeIndex lowerTypeClass(const DICompositeType *Ty);
  codeview::TypeIndex lowerTypeUnion(const DICompositeType *Ty);

  /// Symbol records should point to complete types, but type records should
  /// always point to incomplete types to avoid cycles in the type graph. Only
  /// use this entry point when generating symbol records. The complete and
  /// incomplete type indices only differ for record types. All other types use
  /// the same index.
  codeview::TypeIndex getCompleteTypeIndex(const DIType *Ty);

  codeview::TypeIndex lowerCompleteTypeClass(const DICompositeType *Ty);
  codeview::TypeIndex lowerCompleteTypeUnion(const DICompositeType *Ty);

  struct TypeLoweringScope;

  void emitDeferredCompleteTypes();

  void collectMemberInfo(ClassInfo &Info, const DIDerivedType *DDTy);
  ClassInfo collectClassInfo(const DICompositeType *Ty);

  /// Common record member lowering functionality for record types, which are
  /// structs, classes, and unions. Returns the field list index and the member
  /// count.
  std::tuple<codeview::TypeIndex, codeview::TypeIndex, unsigned, bool>
  lowerRecordFieldList(const DICompositeType *Ty);

  /// Inserts {{Node, ClassTy}, TI} into TypeIndices and checks for duplicates.
  codeview::TypeIndex recordTypeIndexForDINode(const DINode *Node,
                                               codeview::TypeIndex TI,
                                               const DIType *ClassTy = nullptr);

  /// Collect the names of parent scopes, innermost to outermost. Return the
  /// innermost subprogram scope if present. Ensure that parent type scopes are
  /// inserted into the type table.
  const DISubprogram *
  collectParentScopeNames(const DIScope *Scope,
                          SmallVectorImpl<StringRef> &ParentScopeNames);
  std::string getFullyQualifiedName(const DIScope *Scope, StringRef Name);
  std::string getFullyQualifiedName(const DIScope *Scope);

  unsigned getPointerSizeInBytes();

  void discoverJumpTableBranches(const MachineFunction *MF, bool isThumb);
  void collectDebugInfoForJumpTables(const MachineFunction *MF, bool isThumb);
  void emitDebugInfoForJumpTables(const FunctionInfo &FI);

protected:
  /// Gather pre-function debug information.
  void beginFunctionImpl(const MachineFunction *MF) override;

  /// Gather post-function debug information.
  void endFunctionImpl(const MachineFunction *) override;

  /// Check if the current module is in Fortran.
  bool moduleIsInFortran() {
    return CurrentSourceLanguage == codeview::SourceLanguage::Fortran;
  }

public:
  CodeViewDebug(AsmPrinter *AP);

  void beginModule(Module *M) override;

  /// Emit the COFF section that holds the line table information.
  void endModule() override;

  /// Process beginning of an instruction.
  void beginInstruction(const MachineInstr *MI) override;
};

template <> struct DenseMapInfo<CodeViewDebug::LocalVarDef> {

  static inline CodeViewDebug::LocalVarDef getEmptyKey() {
    return CodeViewDebug::LocalVarDef::createFromOpaqueValue(~0ULL);
  }

  static inline CodeViewDebug::LocalVarDef getTombstoneKey() {
    return CodeViewDebug::LocalVarDef::createFromOpaqueValue(~0ULL - 1ULL);
  }

  static unsigned getHashValue(const CodeViewDebug::LocalVarDef &DR) {
    return CodeViewDebug::LocalVarDef::toOpaqueValue(DR) * 37ULL;
  }

  static bool isEqual(const CodeViewDebug::LocalVarDef &LHS,
                      const CodeViewDebug::LocalVarDef &RHS) {
    return CodeViewDebug::LocalVarDef::toOpaqueValue(LHS) ==
           CodeViewDebug::LocalVarDef::toOpaqueValue(RHS);
  }
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_ASMPRINTER_CODEVIEWDEBUG_H

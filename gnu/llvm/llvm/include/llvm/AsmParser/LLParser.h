//===-- LLParser.h - Parser Class -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the parser class for .ll files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_LLPARSER_H
#define LLVM_ASMPARSER_LLPARSER_H

#include "LLLexer.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/AsmParser/NumberedValues.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Support/ModRef.h"
#include <map>
#include <optional>

namespace llvm {
  class Module;
  class ConstantRange;
  class FunctionType;
  class GlobalObject;
  class SMDiagnostic;
  class SMLoc;
  class SourceMgr;
  class Type;
  struct MaybeAlign;
  class Function;
  class Value;
  class BasicBlock;
  class Instruction;
  class Constant;
  class GlobalValue;
  class Comdat;
  class MDString;
  class MDNode;
  struct SlotMapping;

  /// ValID - Represents a reference of a definition of some sort with no type.
  /// There are several cases where we have to parse the value but where the
  /// type can depend on later context.  This may either be a numeric reference
  /// or a symbolic (%var) reference.  This is just a discriminated union.
  struct ValID {
    enum {
      t_LocalID,             // ID in UIntVal.
      t_GlobalID,            // ID in UIntVal.
      t_LocalName,           // Name in StrVal.
      t_GlobalName,          // Name in StrVal.
      t_APSInt,              // Value in APSIntVal.
      t_APFloat,             // Value in APFloatVal.
      t_Null,                // No value.
      t_Undef,               // No value.
      t_Zero,                // No value.
      t_None,                // No value.
      t_Poison,              // No value.
      t_EmptyArray,          // No value:  []
      t_Constant,            // Value in ConstantVal.
      t_ConstantSplat,       // Value in ConstantVal.
      t_InlineAsm,           // Value in FTy/StrVal/StrVal2/UIntVal.
      t_ConstantStruct,      // Value in ConstantStructElts.
      t_PackedConstantStruct // Value in ConstantStructElts.
    } Kind = t_LocalID;

    LLLexer::LocTy Loc;
    unsigned UIntVal;
    FunctionType *FTy = nullptr;
    std::string StrVal, StrVal2;
    APSInt APSIntVal;
    APFloat APFloatVal{0.0};
    Constant *ConstantVal;
    std::unique_ptr<Constant *[]> ConstantStructElts;
    bool NoCFI = false;

    ValID() = default;
    ValID(const ValID &RHS)
        : Kind(RHS.Kind), Loc(RHS.Loc), UIntVal(RHS.UIntVal), FTy(RHS.FTy),
          StrVal(RHS.StrVal), StrVal2(RHS.StrVal2), APSIntVal(RHS.APSIntVal),
          APFloatVal(RHS.APFloatVal), ConstantVal(RHS.ConstantVal),
          NoCFI(RHS.NoCFI) {
      assert(!RHS.ConstantStructElts);
    }

    bool operator<(const ValID &RHS) const {
      assert(Kind == RHS.Kind && "Comparing ValIDs of different kinds");
      if (Kind == t_LocalID || Kind == t_GlobalID)
        return UIntVal < RHS.UIntVal;
      assert((Kind == t_LocalName || Kind == t_GlobalName ||
              Kind == t_ConstantStruct || Kind == t_PackedConstantStruct) &&
             "Ordering not defined for this ValID kind yet");
      return StrVal < RHS.StrVal;
    }
  };

  class LLParser {
  public:
    typedef LLLexer::LocTy LocTy;
  private:
    LLVMContext &Context;
    // Lexer to determine whether to use opaque pointers or not.
    LLLexer OPLex;
    LLLexer Lex;
    // Module being parsed, null if we are only parsing summary index.
    Module *M;
    // Summary index being parsed, null if we are only parsing Module.
    ModuleSummaryIndex *Index;
    SlotMapping *Slots;

    SmallVector<Instruction*, 64> InstsWithTBAATag;

    /// DIAssignID metadata does not support temporary RAUW so we cannot use
    /// the normal metadata forward reference resolution method. Instead,
    /// non-temporary DIAssignID are attached to instructions (recorded here)
    /// then replaced later.
    DenseMap<MDNode *, SmallVector<Instruction *, 2>> TempDIAssignIDAttachments;

    // Type resolution handling data structures.  The location is set when we
    // have processed a use of the type but not a definition yet.
    StringMap<std::pair<Type*, LocTy> > NamedTypes;
    std::map<unsigned, std::pair<Type*, LocTy> > NumberedTypes;

    std::map<unsigned, TrackingMDNodeRef> NumberedMetadata;
    std::map<unsigned, std::pair<TempMDTuple, LocTy>> ForwardRefMDNodes;

    // Global Value reference information.
    std::map<std::string, std::pair<GlobalValue*, LocTy> > ForwardRefVals;
    std::map<unsigned, std::pair<GlobalValue*, LocTy> > ForwardRefValIDs;
    NumberedValues<GlobalValue *> NumberedVals;

    // Comdat forward reference information.
    std::map<std::string, LocTy> ForwardRefComdats;

    // References to blockaddress.  The key is the function ValID, the value is
    // a list of references to blocks in that function.
    std::map<ValID, std::map<ValID, GlobalValue *>> ForwardRefBlockAddresses;
    class PerFunctionState;
    /// Reference to per-function state to allow basic blocks to be
    /// forward-referenced by blockaddress instructions within the same
    /// function.
    PerFunctionState *BlockAddressPFS;

    // References to dso_local_equivalent. The key is the global's ValID, the
    // value is a placeholder value that will be replaced. Note there are two
    // maps for tracking ValIDs that are GlobalNames and ValIDs that are
    // GlobalIDs. These are needed because "operator<" doesn't discriminate
    // between the two.
    std::map<ValID, GlobalValue *> ForwardRefDSOLocalEquivalentNames;
    std::map<ValID, GlobalValue *> ForwardRefDSOLocalEquivalentIDs;

    // Attribute builder reference information.
    std::map<Value*, std::vector<unsigned> > ForwardRefAttrGroups;
    std::map<unsigned, AttrBuilder> NumberedAttrBuilders;

    // Summary global value reference information.
    std::map<unsigned, std::vector<std::pair<ValueInfo *, LocTy>>>
        ForwardRefValueInfos;
    std::map<unsigned, std::vector<std::pair<AliasSummary *, LocTy>>>
        ForwardRefAliasees;
    std::vector<ValueInfo> NumberedValueInfos;

    // Summary type id reference information.
    std::map<unsigned, std::vector<std::pair<GlobalValue::GUID *, LocTy>>>
        ForwardRefTypeIds;

    // Map of module ID to path.
    std::map<unsigned, StringRef> ModuleIdMap;

    /// Only the llvm-as tool may set this to false to bypass
    /// UpgradeDebuginfo so it can generate broken bitcode.
    bool UpgradeDebugInfo;

    bool SeenNewDbgInfoFormat = false;
    bool SeenOldDbgInfoFormat = false;

    std::string SourceFileName;

  public:
    LLParser(StringRef F, SourceMgr &SM, SMDiagnostic &Err, Module *M,
             ModuleSummaryIndex *Index, LLVMContext &Context,
             SlotMapping *Slots = nullptr)
        : Context(Context), OPLex(F, SM, Err, Context),
          Lex(F, SM, Err, Context), M(M), Index(Index), Slots(Slots),
          BlockAddressPFS(nullptr) {}
    bool Run(
        bool UpgradeDebugInfo,
        DataLayoutCallbackTy DataLayoutCallback = [](StringRef, StringRef) {
          return std::nullopt;
        });

    bool parseStandaloneConstantValue(Constant *&C, const SlotMapping *Slots);

    bool parseTypeAtBeginning(Type *&Ty, unsigned &Read,
                              const SlotMapping *Slots);

    bool parseDIExpressionBodyAtBeginning(MDNode *&Result, unsigned &Read,
                                          const SlotMapping *Slots);

    LLVMContext &getContext() { return Context; }

  private:
    bool error(LocTy L, const Twine &Msg) const { return Lex.Error(L, Msg); }
    bool tokError(const Twine &Msg) const { return error(Lex.getLoc(), Msg); }

    bool checkValueID(LocTy L, StringRef Kind, StringRef Prefix,
                      unsigned NextID, unsigned ID) const;

    /// Restore the internal name and slot mappings using the mappings that
    /// were created at an earlier parsing stage.
    void restoreParsingState(const SlotMapping *Slots);

    /// getGlobalVal - Get a value with the specified name or ID, creating a
    /// forward reference record if needed.  This can return null if the value
    /// exists but does not have the right type.
    GlobalValue *getGlobalVal(const std::string &N, Type *Ty, LocTy Loc);
    GlobalValue *getGlobalVal(unsigned ID, Type *Ty, LocTy Loc);

    /// Get a Comdat with the specified name, creating a forward reference
    /// record if needed.
    Comdat *getComdat(const std::string &Name, LocTy Loc);

    // Helper Routines.
    bool parseToken(lltok::Kind T, const char *ErrMsg);
    bool EatIfPresent(lltok::Kind T) {
      if (Lex.getKind() != T) return false;
      Lex.Lex();
      return true;
    }

    FastMathFlags EatFastMathFlagsIfPresent() {
      FastMathFlags FMF;
      while (true)
        switch (Lex.getKind()) {
        case lltok::kw_fast: FMF.setFast();            Lex.Lex(); continue;
        case lltok::kw_nnan: FMF.setNoNaNs();          Lex.Lex(); continue;
        case lltok::kw_ninf: FMF.setNoInfs();          Lex.Lex(); continue;
        case lltok::kw_nsz:  FMF.setNoSignedZeros();   Lex.Lex(); continue;
        case lltok::kw_arcp: FMF.setAllowReciprocal(); Lex.Lex(); continue;
        case lltok::kw_contract:
          FMF.setAllowContract(true);
          Lex.Lex();
          continue;
        case lltok::kw_reassoc: FMF.setAllowReassoc(); Lex.Lex(); continue;
        case lltok::kw_afn:     FMF.setApproxFunc();   Lex.Lex(); continue;
        default: return FMF;
        }
      return FMF;
    }

    bool parseOptionalToken(lltok::Kind T, bool &Present,
                            LocTy *Loc = nullptr) {
      if (Lex.getKind() != T) {
        Present = false;
      } else {
        if (Loc)
          *Loc = Lex.getLoc();
        Lex.Lex();
        Present = true;
      }
      return false;
    }
    bool parseStringConstant(std::string &Result);
    bool parseUInt32(unsigned &Val);
    bool parseUInt32(unsigned &Val, LocTy &Loc) {
      Loc = Lex.getLoc();
      return parseUInt32(Val);
    }
    bool parseUInt64(uint64_t &Val);
    bool parseUInt64(uint64_t &Val, LocTy &Loc) {
      Loc = Lex.getLoc();
      return parseUInt64(Val);
    }
    bool parseFlag(unsigned &Val);

    bool parseStringAttribute(AttrBuilder &B);

    bool parseTLSModel(GlobalVariable::ThreadLocalMode &TLM);
    bool parseOptionalThreadLocal(GlobalVariable::ThreadLocalMode &TLM);
    bool parseOptionalUnnamedAddr(GlobalVariable::UnnamedAddr &UnnamedAddr);
    bool parseOptionalAddrSpace(unsigned &AddrSpace, unsigned DefaultAS = 0);
    bool parseOptionalProgramAddrSpace(unsigned &AddrSpace) {
      return parseOptionalAddrSpace(
          AddrSpace, M->getDataLayout().getProgramAddressSpace());
    };
    bool parseEnumAttribute(Attribute::AttrKind Attr, AttrBuilder &B,
                            bool InAttrGroup);
    bool parseOptionalParamOrReturnAttrs(AttrBuilder &B, bool IsParam);
    bool parseOptionalParamAttrs(AttrBuilder &B) {
      return parseOptionalParamOrReturnAttrs(B, true);
    }
    bool parseOptionalReturnAttrs(AttrBuilder &B) {
      return parseOptionalParamOrReturnAttrs(B, false);
    }
    bool parseOptionalLinkage(unsigned &Res, bool &HasLinkage,
                              unsigned &Visibility, unsigned &DLLStorageClass,
                              bool &DSOLocal);
    void parseOptionalDSOLocal(bool &DSOLocal);
    void parseOptionalVisibility(unsigned &Res);
    bool parseOptionalImportType(lltok::Kind Kind,
                                 GlobalValueSummary::ImportKind &Res);
    void parseOptionalDLLStorageClass(unsigned &Res);
    bool parseOptionalCallingConv(unsigned &CC);
    bool parseOptionalAlignment(MaybeAlign &Alignment,
                                bool AllowParens = false);
    bool parseOptionalCodeModel(CodeModel::Model &model);
    bool parseOptionalDerefAttrBytes(lltok::Kind AttrKind, uint64_t &Bytes);
    bool parseOptionalUWTableKind(UWTableKind &Kind);
    bool parseAllocKind(AllocFnKind &Kind);
    std::optional<MemoryEffects> parseMemoryAttr();
    unsigned parseNoFPClassAttr();
    bool parseScopeAndOrdering(bool IsAtomic, SyncScope::ID &SSID,
                               AtomicOrdering &Ordering);
    bool parseScope(SyncScope::ID &SSID);
    bool parseOrdering(AtomicOrdering &Ordering);
    bool parseOptionalStackAlignment(unsigned &Alignment);
    bool parseOptionalCommaAlign(MaybeAlign &Alignment, bool &AteExtraComma);
    bool parseOptionalCommaAddrSpace(unsigned &AddrSpace, LocTy &Loc,
                                     bool &AteExtraComma);
    bool parseAllocSizeArguments(unsigned &BaseSizeArg,
                                 std::optional<unsigned> &HowManyArg);
    bool parseVScaleRangeArguments(unsigned &MinValue, unsigned &MaxValue);
    bool parseIndexList(SmallVectorImpl<unsigned> &Indices,
                        bool &AteExtraComma);
    bool parseIndexList(SmallVectorImpl<unsigned> &Indices) {
      bool AteExtraComma;
      if (parseIndexList(Indices, AteExtraComma))
        return true;
      if (AteExtraComma)
        return tokError("expected index");
      return false;
    }

    // Top-Level Entities
    bool parseTopLevelEntities();
    void dropUnknownMetadataReferences();
    bool validateEndOfModule(bool UpgradeDebugInfo);
    bool validateEndOfIndex();
    bool parseTargetDefinitions(DataLayoutCallbackTy DataLayoutCallback);
    bool parseTargetDefinition(std::string &TentativeDLStr, LocTy &DLStrLoc);
    bool parseModuleAsm();
    bool parseSourceFileName();
    bool parseUnnamedType();
    bool parseNamedType();
    bool parseDeclare();
    bool parseDefine();

    bool parseGlobalType(bool &IsConstant);
    bool parseUnnamedGlobal();
    bool parseNamedGlobal();
    bool parseGlobal(const std::string &Name, unsigned NameID, LocTy NameLoc,
                     unsigned Linkage, bool HasLinkage, unsigned Visibility,
                     unsigned DLLStorageClass, bool DSOLocal,
                     GlobalVariable::ThreadLocalMode TLM,
                     GlobalVariable::UnnamedAddr UnnamedAddr);
    bool parseAliasOrIFunc(const std::string &Name, unsigned NameID,
                           LocTy NameLoc, unsigned L, unsigned Visibility,
                           unsigned DLLStorageClass, bool DSOLocal,
                           GlobalVariable::ThreadLocalMode TLM,
                           GlobalVariable::UnnamedAddr UnnamedAddr);
    bool parseComdat();
    bool parseStandaloneMetadata();
    bool parseNamedMetadata();
    bool parseMDString(MDString *&Result);
    bool parseMDNodeID(MDNode *&Result);
    bool parseUnnamedAttrGrp();
    bool parseFnAttributeValuePairs(AttrBuilder &B,
                                    std::vector<unsigned> &FwdRefAttrGrps,
                                    bool inAttrGrp, LocTy &BuiltinLoc);
    bool parseRangeAttr(AttrBuilder &B);
    bool parseInitializesAttr(AttrBuilder &B);
    bool parseRequiredTypeAttr(AttrBuilder &B, lltok::Kind AttrToken,
                               Attribute::AttrKind AttrKind);

    // Module Summary Index Parsing.
    bool skipModuleSummaryEntry();
    bool parseSummaryEntry();
    bool parseModuleEntry(unsigned ID);
    bool parseModuleReference(StringRef &ModulePath);
    bool parseGVReference(ValueInfo &VI, unsigned &GVId);
    bool parseSummaryIndexFlags();
    bool parseBlockCount();
    bool parseGVEntry(unsigned ID);
    bool parseFunctionSummary(std::string Name, GlobalValue::GUID, unsigned ID);
    bool parseVariableSummary(std::string Name, GlobalValue::GUID, unsigned ID);
    bool parseAliasSummary(std::string Name, GlobalValue::GUID, unsigned ID);
    bool parseGVFlags(GlobalValueSummary::GVFlags &GVFlags);
    bool parseGVarFlags(GlobalVarSummary::GVarFlags &GVarFlags);
    bool parseOptionalFFlags(FunctionSummary::FFlags &FFlags);
    bool parseOptionalCalls(std::vector<FunctionSummary::EdgeTy> &Calls);
    bool parseHotness(CalleeInfo::HotnessType &Hotness);
    bool parseOptionalTypeIdInfo(FunctionSummary::TypeIdInfo &TypeIdInfo);
    bool parseTypeTests(std::vector<GlobalValue::GUID> &TypeTests);
    bool parseVFuncIdList(lltok::Kind Kind,
                          std::vector<FunctionSummary::VFuncId> &VFuncIdList);
    bool parseConstVCallList(
        lltok::Kind Kind,
        std::vector<FunctionSummary::ConstVCall> &ConstVCallList);
    using IdToIndexMapType =
        std::map<unsigned, std::vector<std::pair<unsigned, LocTy>>>;
    bool parseConstVCall(FunctionSummary::ConstVCall &ConstVCall,
                         IdToIndexMapType &IdToIndexMap, unsigned Index);
    bool parseVFuncId(FunctionSummary::VFuncId &VFuncId,
                      IdToIndexMapType &IdToIndexMap, unsigned Index);
    bool parseOptionalVTableFuncs(VTableFuncList &VTableFuncs);
    bool parseOptionalParamAccesses(
        std::vector<FunctionSummary::ParamAccess> &Params);
    bool parseParamNo(uint64_t &ParamNo);
    using IdLocListType = std::vector<std::pair<unsigned, LocTy>>;
    bool parseParamAccess(FunctionSummary::ParamAccess &Param,
                          IdLocListType &IdLocList);
    bool parseParamAccessCall(FunctionSummary::ParamAccess::Call &Call,
                              IdLocListType &IdLocList);
    bool parseParamAccessOffset(ConstantRange &Range);
    bool parseOptionalRefs(std::vector<ValueInfo> &Refs);
    bool parseTypeIdEntry(unsigned ID);
    bool parseTypeIdSummary(TypeIdSummary &TIS);
    bool parseTypeIdCompatibleVtableEntry(unsigned ID);
    bool parseTypeTestResolution(TypeTestResolution &TTRes);
    bool parseOptionalWpdResolutions(
        std::map<uint64_t, WholeProgramDevirtResolution> &WPDResMap);
    bool parseWpdRes(WholeProgramDevirtResolution &WPDRes);
    bool parseOptionalResByArg(
        std::map<std::vector<uint64_t>, WholeProgramDevirtResolution::ByArg>
            &ResByArg);
    bool parseArgs(std::vector<uint64_t> &Args);
    bool addGlobalValueToIndex(std::string Name, GlobalValue::GUID,
                               GlobalValue::LinkageTypes Linkage, unsigned ID,
                               std::unique_ptr<GlobalValueSummary> Summary,
                               LocTy Loc);
    bool parseOptionalAllocs(std::vector<AllocInfo> &Allocs);
    bool parseMemProfs(std::vector<MIBInfo> &MIBs);
    bool parseAllocType(uint8_t &AllocType);
    bool parseOptionalCallsites(std::vector<CallsiteInfo> &Callsites);

    // Type Parsing.
    bool parseType(Type *&Result, const Twine &Msg, bool AllowVoid = false);
    bool parseType(Type *&Result, bool AllowVoid = false) {
      return parseType(Result, "expected type", AllowVoid);
    }
    bool parseType(Type *&Result, const Twine &Msg, LocTy &Loc,
                   bool AllowVoid = false) {
      Loc = Lex.getLoc();
      return parseType(Result, Msg, AllowVoid);
    }
    bool parseType(Type *&Result, LocTy &Loc, bool AllowVoid = false) {
      Loc = Lex.getLoc();
      return parseType(Result, AllowVoid);
    }
    bool parseAnonStructType(Type *&Result, bool Packed);
    bool parseStructBody(SmallVectorImpl<Type *> &Body);
    bool parseStructDefinition(SMLoc TypeLoc, StringRef Name,
                               std::pair<Type *, LocTy> &Entry,
                               Type *&ResultTy);

    bool parseArrayVectorType(Type *&Result, bool IsVector);
    bool parseFunctionType(Type *&Result);
    bool parseTargetExtType(Type *&Result);

    // Function Semantic Analysis.
    class PerFunctionState {
      LLParser &P;
      Function &F;
      std::map<std::string, std::pair<Value*, LocTy> > ForwardRefVals;
      std::map<unsigned, std::pair<Value*, LocTy> > ForwardRefValIDs;
      NumberedValues<Value *> NumberedVals;

      /// FunctionNumber - If this is an unnamed function, this is the slot
      /// number of it, otherwise it is -1.
      int FunctionNumber;

    public:
      PerFunctionState(LLParser &p, Function &f, int functionNumber,
                       ArrayRef<unsigned> UnnamedArgNums);
      ~PerFunctionState();

      Function &getFunction() const { return F; }

      bool finishFunction();

      /// GetVal - Get a value with the specified name or ID, creating a
      /// forward reference record if needed.  This can return null if the value
      /// exists but does not have the right type.
      Value *getVal(const std::string &Name, Type *Ty, LocTy Loc);
      Value *getVal(unsigned ID, Type *Ty, LocTy Loc);

      /// setInstName - After an instruction is parsed and inserted into its
      /// basic block, this installs its name.
      bool setInstName(int NameID, const std::string &NameStr, LocTy NameLoc,
                       Instruction *Inst);

      /// GetBB - Get a basic block with the specified name or ID, creating a
      /// forward reference record if needed.  This can return null if the value
      /// is not a BasicBlock.
      BasicBlock *getBB(const std::string &Name, LocTy Loc);
      BasicBlock *getBB(unsigned ID, LocTy Loc);

      /// DefineBB - Define the specified basic block, which is either named or
      /// unnamed.  If there is an error, this returns null otherwise it returns
      /// the block being defined.
      BasicBlock *defineBB(const std::string &Name, int NameID, LocTy Loc);

      bool resolveForwardRefBlockAddresses();
    };

    bool convertValIDToValue(Type *Ty, ValID &ID, Value *&V,
                             PerFunctionState *PFS);

    Value *checkValidVariableType(LocTy Loc, const Twine &Name, Type *Ty,
                                  Value *Val);

    bool parseConstantValue(Type *Ty, Constant *&C);
    bool parseValue(Type *Ty, Value *&V, PerFunctionState *PFS);
    bool parseValue(Type *Ty, Value *&V, PerFunctionState &PFS) {
      return parseValue(Ty, V, &PFS);
    }

    bool parseValue(Type *Ty, Value *&V, LocTy &Loc, PerFunctionState &PFS) {
      Loc = Lex.getLoc();
      return parseValue(Ty, V, &PFS);
    }

    bool parseTypeAndValue(Value *&V, PerFunctionState *PFS);
    bool parseTypeAndValue(Value *&V, PerFunctionState &PFS) {
      return parseTypeAndValue(V, &PFS);
    }
    bool parseTypeAndValue(Value *&V, LocTy &Loc, PerFunctionState &PFS) {
      Loc = Lex.getLoc();
      return parseTypeAndValue(V, PFS);
    }
    bool parseTypeAndBasicBlock(BasicBlock *&BB, LocTy &Loc,
                                PerFunctionState &PFS);
    bool parseTypeAndBasicBlock(BasicBlock *&BB, PerFunctionState &PFS) {
      LocTy Loc;
      return parseTypeAndBasicBlock(BB, Loc, PFS);
    }

    struct ParamInfo {
      LocTy Loc;
      Value *V;
      AttributeSet Attrs;
      ParamInfo(LocTy loc, Value *v, AttributeSet attrs)
          : Loc(loc), V(v), Attrs(attrs) {}
    };
    bool parseParameterList(SmallVectorImpl<ParamInfo> &ArgList,
                            PerFunctionState &PFS, bool IsMustTailCall = false,
                            bool InVarArgsFunc = false);

    bool
    parseOptionalOperandBundles(SmallVectorImpl<OperandBundleDef> &BundleList,
                                PerFunctionState &PFS);

    bool parseExceptionArgs(SmallVectorImpl<Value *> &Args,
                            PerFunctionState &PFS);

    bool resolveFunctionType(Type *RetType,
                             const SmallVector<ParamInfo, 16> &ArgList,
                             FunctionType *&FuncTy);

    // Constant Parsing.
    bool parseValID(ValID &ID, PerFunctionState *PFS,
                    Type *ExpectedTy = nullptr);
    bool parseGlobalValue(Type *Ty, Constant *&C);
    bool parseGlobalTypeAndValue(Constant *&V);
    bool parseGlobalValueVector(SmallVectorImpl<Constant *> &Elts);
    bool parseOptionalComdat(StringRef GlobalName, Comdat *&C);
    bool parseSanitizer(GlobalVariable *GV);
    bool parseMetadataAsValue(Value *&V, PerFunctionState &PFS);
    bool parseValueAsMetadata(Metadata *&MD, const Twine &TypeMsg,
                              PerFunctionState *PFS);
    bool parseDIArgList(Metadata *&MD, PerFunctionState *PFS);
    bool parseMetadata(Metadata *&MD, PerFunctionState *PFS);
    bool parseMDTuple(MDNode *&MD, bool IsDistinct = false);
    bool parseMDNode(MDNode *&N);
    bool parseMDNodeTail(MDNode *&N);
    bool parseMDNodeVector(SmallVectorImpl<Metadata *> &Elts);
    bool parseMetadataAttachment(unsigned &Kind, MDNode *&MD);
    bool parseDebugRecord(DbgRecord *&DR, PerFunctionState &PFS);
    bool parseInstructionMetadata(Instruction &Inst);
    bool parseGlobalObjectMetadataAttachment(GlobalObject &GO);
    bool parseOptionalFunctionMetadata(Function &F);

    template <class FieldTy>
    bool parseMDField(LocTy Loc, StringRef Name, FieldTy &Result);
    template <class FieldTy> bool parseMDField(StringRef Name, FieldTy &Result);
    template <class ParserTy> bool parseMDFieldsImplBody(ParserTy ParseField);
    template <class ParserTy>
    bool parseMDFieldsImpl(ParserTy ParseField, LocTy &ClosingLoc);
    bool parseSpecializedMDNode(MDNode *&N, bool IsDistinct = false);
    bool parseDIExpressionBody(MDNode *&Result, bool IsDistinct);

#define HANDLE_SPECIALIZED_MDNODE_LEAF(CLASS)                                  \
  bool parse##CLASS(MDNode *&Result, bool IsDistinct);
#include "llvm/IR/Metadata.def"

    // Function Parsing.
    struct ArgInfo {
      LocTy Loc;
      Type *Ty;
      AttributeSet Attrs;
      std::string Name;
      ArgInfo(LocTy L, Type *ty, AttributeSet Attr, const std::string &N)
          : Loc(L), Ty(ty), Attrs(Attr), Name(N) {}
    };
    bool parseArgumentList(SmallVectorImpl<ArgInfo> &ArgList,
                           SmallVectorImpl<unsigned> &UnnamedArgNums,
                           bool &IsVarArg);
    bool parseFunctionHeader(Function *&Fn, bool IsDefine,
                             unsigned &FunctionNumber,
                             SmallVectorImpl<unsigned> &UnnamedArgNums);
    bool parseFunctionBody(Function &Fn, unsigned FunctionNumber,
                           ArrayRef<unsigned> UnnamedArgNums);
    bool parseBasicBlock(PerFunctionState &PFS);

    enum TailCallType { TCT_None, TCT_Tail, TCT_MustTail };

    // Instruction Parsing.  Each instruction parsing routine can return with a
    // normal result, an error result, or return having eaten an extra comma.
    enum InstResult { InstNormal = 0, InstError = 1, InstExtraComma = 2 };
    int parseInstruction(Instruction *&Inst, BasicBlock *BB,
                         PerFunctionState &PFS);
    bool parseCmpPredicate(unsigned &P, unsigned Opc);

    bool parseRet(Instruction *&Inst, BasicBlock *BB, PerFunctionState &PFS);
    bool parseBr(Instruction *&Inst, PerFunctionState &PFS);
    bool parseSwitch(Instruction *&Inst, PerFunctionState &PFS);
    bool parseIndirectBr(Instruction *&Inst, PerFunctionState &PFS);
    bool parseInvoke(Instruction *&Inst, PerFunctionState &PFS);
    bool parseResume(Instruction *&Inst, PerFunctionState &PFS);
    bool parseCleanupRet(Instruction *&Inst, PerFunctionState &PFS);
    bool parseCatchRet(Instruction *&Inst, PerFunctionState &PFS);
    bool parseCatchSwitch(Instruction *&Inst, PerFunctionState &PFS);
    bool parseCatchPad(Instruction *&Inst, PerFunctionState &PFS);
    bool parseCleanupPad(Instruction *&Inst, PerFunctionState &PFS);
    bool parseCallBr(Instruction *&Inst, PerFunctionState &PFS);

    bool parseUnaryOp(Instruction *&Inst, PerFunctionState &PFS, unsigned Opc,
                      bool IsFP);
    bool parseArithmetic(Instruction *&Inst, PerFunctionState &PFS,
                         unsigned Opc, bool IsFP);
    bool parseLogical(Instruction *&Inst, PerFunctionState &PFS, unsigned Opc);
    bool parseCompare(Instruction *&Inst, PerFunctionState &PFS, unsigned Opc);
    bool parseCast(Instruction *&Inst, PerFunctionState &PFS, unsigned Opc);
    bool parseSelect(Instruction *&Inst, PerFunctionState &PFS);
    bool parseVAArg(Instruction *&Inst, PerFunctionState &PFS);
    bool parseExtractElement(Instruction *&Inst, PerFunctionState &PFS);
    bool parseInsertElement(Instruction *&Inst, PerFunctionState &PFS);
    bool parseShuffleVector(Instruction *&Inst, PerFunctionState &PFS);
    int parsePHI(Instruction *&Inst, PerFunctionState &PFS);
    bool parseLandingPad(Instruction *&Inst, PerFunctionState &PFS);
    bool parseCall(Instruction *&Inst, PerFunctionState &PFS,
                   CallInst::TailCallKind TCK);
    int parseAlloc(Instruction *&Inst, PerFunctionState &PFS);
    int parseLoad(Instruction *&Inst, PerFunctionState &PFS);
    int parseStore(Instruction *&Inst, PerFunctionState &PFS);
    int parseCmpXchg(Instruction *&Inst, PerFunctionState &PFS);
    int parseAtomicRMW(Instruction *&Inst, PerFunctionState &PFS);
    int parseFence(Instruction *&Inst, PerFunctionState &PFS);
    int parseGetElementPtr(Instruction *&Inst, PerFunctionState &PFS);
    int parseExtractValue(Instruction *&Inst, PerFunctionState &PFS);
    int parseInsertValue(Instruction *&Inst, PerFunctionState &PFS);
    bool parseFreeze(Instruction *&I, PerFunctionState &PFS);

    // Use-list order directives.
    bool parseUseListOrder(PerFunctionState *PFS = nullptr);
    bool parseUseListOrderBB();
    bool parseUseListOrderIndexes(SmallVectorImpl<unsigned> &Indexes);
    bool sortUseListOrder(Value *V, ArrayRef<unsigned> Indexes, SMLoc Loc);
  };
} // End llvm namespace

#endif

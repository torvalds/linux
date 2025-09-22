//===-- LLParser.cpp - Parser Class ---------------------------------------===//
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

#include "llvm/AsmParser/LLParser.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/AsmParser/LLToken.h"
#include "llvm/AsmParser/SlotMapping.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/ConstantRangeList.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <optional>
#include <vector>

using namespace llvm;

static cl::opt<bool> AllowIncompleteIR(
    "allow-incomplete-ir", cl::init(false), cl::Hidden,
    cl::desc(
        "Allow incomplete IR on a best effort basis (references to unknown "
        "metadata will be dropped)"));

extern llvm::cl::opt<bool> UseNewDbgInfoFormat;
extern cl::opt<cl::boolOrDefault> PreserveInputDbgFormat;
extern bool WriteNewDbgInfoFormatToBitcode;
extern cl::opt<bool> WriteNewDbgInfoFormat;

static std::string getTypeString(Type *T) {
  std::string Result;
  raw_string_ostream Tmp(Result);
  Tmp << *T;
  return Tmp.str();
}

/// Run: module ::= toplevelentity*
bool LLParser::Run(bool UpgradeDebugInfo,
                   DataLayoutCallbackTy DataLayoutCallback) {
  // Prime the lexer.
  Lex.Lex();

  if (Context.shouldDiscardValueNames())
    return error(
        Lex.getLoc(),
        "Can't read textual IR with a Context that discards named Values");

  if (M) {
    if (parseTargetDefinitions(DataLayoutCallback))
      return true;
  }

  return parseTopLevelEntities() || validateEndOfModule(UpgradeDebugInfo) ||
         validateEndOfIndex();
}

bool LLParser::parseStandaloneConstantValue(Constant *&C,
                                            const SlotMapping *Slots) {
  restoreParsingState(Slots);
  Lex.Lex();

  Type *Ty = nullptr;
  if (parseType(Ty) || parseConstantValue(Ty, C))
    return true;
  if (Lex.getKind() != lltok::Eof)
    return error(Lex.getLoc(), "expected end of string");
  return false;
}

bool LLParser::parseTypeAtBeginning(Type *&Ty, unsigned &Read,
                                    const SlotMapping *Slots) {
  restoreParsingState(Slots);
  Lex.Lex();

  Read = 0;
  SMLoc Start = Lex.getLoc();
  Ty = nullptr;
  if (parseType(Ty))
    return true;
  SMLoc End = Lex.getLoc();
  Read = End.getPointer() - Start.getPointer();

  return false;
}

bool LLParser::parseDIExpressionBodyAtBeginning(MDNode *&Result, unsigned &Read,
                                                const SlotMapping *Slots) {
  restoreParsingState(Slots);
  Lex.Lex();

  Read = 0;
  SMLoc Start = Lex.getLoc();
  Result = nullptr;
  bool Status = parseDIExpressionBody(Result, /*IsDistinct=*/false);
  SMLoc End = Lex.getLoc();
  Read = End.getPointer() - Start.getPointer();

  return Status;
}

void LLParser::restoreParsingState(const SlotMapping *Slots) {
  if (!Slots)
    return;
  NumberedVals = Slots->GlobalValues;
  NumberedMetadata = Slots->MetadataNodes;
  for (const auto &I : Slots->NamedTypes)
    NamedTypes.insert(
        std::make_pair(I.getKey(), std::make_pair(I.second, LocTy())));
  for (const auto &I : Slots->Types)
    NumberedTypes.insert(
        std::make_pair(I.first, std::make_pair(I.second, LocTy())));
}

static void dropIntrinsicWithUnknownMetadataArgument(IntrinsicInst *II) {
  // White-list intrinsics that are safe to drop.
  if (!isa<DbgInfoIntrinsic>(II) &&
      II->getIntrinsicID() != Intrinsic::experimental_noalias_scope_decl)
    return;

  SmallVector<MetadataAsValue *> MVs;
  for (Value *V : II->args())
    if (auto *MV = dyn_cast<MetadataAsValue>(V))
      if (auto *MD = dyn_cast<MDNode>(MV->getMetadata()))
        if (MD->isTemporary())
          MVs.push_back(MV);

  if (!MVs.empty()) {
    assert(II->use_empty() && "Cannot have uses");
    II->eraseFromParent();

    // Also remove no longer used MetadataAsValue wrappers.
    for (MetadataAsValue *MV : MVs)
      if (MV->use_empty())
        delete MV;
  }
}

void LLParser::dropUnknownMetadataReferences() {
  auto Pred = [](unsigned MDKind, MDNode *Node) { return Node->isTemporary(); };
  for (Function &F : *M) {
    F.eraseMetadataIf(Pred);
    for (Instruction &I : make_early_inc_range(instructions(F))) {
      I.eraseMetadataIf(Pred);

      if (auto *II = dyn_cast<IntrinsicInst>(&I))
        dropIntrinsicWithUnknownMetadataArgument(II);
    }
  }

  for (GlobalVariable &GV : M->globals())
    GV.eraseMetadataIf(Pred);

  for (const auto &[ID, Info] : make_early_inc_range(ForwardRefMDNodes)) {
    // Check whether there is only a single use left, which would be in our
    // own NumberedMetadata.
    if (Info.first->getNumTemporaryUses() == 1) {
      NumberedMetadata.erase(ID);
      ForwardRefMDNodes.erase(ID);
    }
  }
}

/// validateEndOfModule - Do final validity and basic correctness checks at the
/// end of the module.
bool LLParser::validateEndOfModule(bool UpgradeDebugInfo) {
  if (!M)
    return false;

  // We should have already returned an error if we observed both intrinsics and
  // records in this IR.
  assert(!(SeenNewDbgInfoFormat && SeenOldDbgInfoFormat) &&
         "Mixed debug intrinsics/records seen without a parsing error?");
  if (PreserveInputDbgFormat == cl::boolOrDefault::BOU_TRUE) {
    UseNewDbgInfoFormat = SeenNewDbgInfoFormat;
    WriteNewDbgInfoFormatToBitcode = SeenNewDbgInfoFormat;
    WriteNewDbgInfoFormat = SeenNewDbgInfoFormat;
    M->setNewDbgInfoFormatFlag(SeenNewDbgInfoFormat);
  }

  // Handle any function attribute group forward references.
  for (const auto &RAG : ForwardRefAttrGroups) {
    Value *V = RAG.first;
    const std::vector<unsigned> &Attrs = RAG.second;
    AttrBuilder B(Context);

    for (const auto &Attr : Attrs) {
      auto R = NumberedAttrBuilders.find(Attr);
      if (R != NumberedAttrBuilders.end())
        B.merge(R->second);
    }

    if (Function *Fn = dyn_cast<Function>(V)) {
      AttributeList AS = Fn->getAttributes();
      AttrBuilder FnAttrs(M->getContext(), AS.getFnAttrs());
      AS = AS.removeFnAttributes(Context);

      FnAttrs.merge(B);

      // If the alignment was parsed as an attribute, move to the alignment
      // field.
      if (MaybeAlign A = FnAttrs.getAlignment()) {
        Fn->setAlignment(*A);
        FnAttrs.removeAttribute(Attribute::Alignment);
      }

      AS = AS.addFnAttributes(Context, FnAttrs);
      Fn->setAttributes(AS);
    } else if (CallInst *CI = dyn_cast<CallInst>(V)) {
      AttributeList AS = CI->getAttributes();
      AttrBuilder FnAttrs(M->getContext(), AS.getFnAttrs());
      AS = AS.removeFnAttributes(Context);
      FnAttrs.merge(B);
      AS = AS.addFnAttributes(Context, FnAttrs);
      CI->setAttributes(AS);
    } else if (InvokeInst *II = dyn_cast<InvokeInst>(V)) {
      AttributeList AS = II->getAttributes();
      AttrBuilder FnAttrs(M->getContext(), AS.getFnAttrs());
      AS = AS.removeFnAttributes(Context);
      FnAttrs.merge(B);
      AS = AS.addFnAttributes(Context, FnAttrs);
      II->setAttributes(AS);
    } else if (CallBrInst *CBI = dyn_cast<CallBrInst>(V)) {
      AttributeList AS = CBI->getAttributes();
      AttrBuilder FnAttrs(M->getContext(), AS.getFnAttrs());
      AS = AS.removeFnAttributes(Context);
      FnAttrs.merge(B);
      AS = AS.addFnAttributes(Context, FnAttrs);
      CBI->setAttributes(AS);
    } else if (auto *GV = dyn_cast<GlobalVariable>(V)) {
      AttrBuilder Attrs(M->getContext(), GV->getAttributes());
      Attrs.merge(B);
      GV->setAttributes(AttributeSet::get(Context,Attrs));
    } else {
      llvm_unreachable("invalid object with forward attribute group reference");
    }
  }

  // If there are entries in ForwardRefBlockAddresses at this point, the
  // function was never defined.
  if (!ForwardRefBlockAddresses.empty())
    return error(ForwardRefBlockAddresses.begin()->first.Loc,
                 "expected function name in blockaddress");

  auto ResolveForwardRefDSOLocalEquivalents = [&](const ValID &GVRef,
                                                  GlobalValue *FwdRef) {
    GlobalValue *GV = nullptr;
    if (GVRef.Kind == ValID::t_GlobalName) {
      GV = M->getNamedValue(GVRef.StrVal);
    } else {
      GV = NumberedVals.get(GVRef.UIntVal);
    }

    if (!GV)
      return error(GVRef.Loc, "unknown function '" + GVRef.StrVal +
                                  "' referenced by dso_local_equivalent");

    if (!GV->getValueType()->isFunctionTy())
      return error(GVRef.Loc,
                   "expected a function, alias to function, or ifunc "
                   "in dso_local_equivalent");

    auto *Equiv = DSOLocalEquivalent::get(GV);
    FwdRef->replaceAllUsesWith(Equiv);
    FwdRef->eraseFromParent();
    return false;
  };

  // If there are entries in ForwardRefDSOLocalEquivalentIDs/Names at this
  // point, they are references after the function was defined.  Resolve those
  // now.
  for (auto &Iter : ForwardRefDSOLocalEquivalentIDs) {
    if (ResolveForwardRefDSOLocalEquivalents(Iter.first, Iter.second))
      return true;
  }
  for (auto &Iter : ForwardRefDSOLocalEquivalentNames) {
    if (ResolveForwardRefDSOLocalEquivalents(Iter.first, Iter.second))
      return true;
  }
  ForwardRefDSOLocalEquivalentIDs.clear();
  ForwardRefDSOLocalEquivalentNames.clear();

  for (const auto &NT : NumberedTypes)
    if (NT.second.second.isValid())
      return error(NT.second.second,
                   "use of undefined type '%" + Twine(NT.first) + "'");

  for (StringMap<std::pair<Type*, LocTy> >::iterator I =
       NamedTypes.begin(), E = NamedTypes.end(); I != E; ++I)
    if (I->second.second.isValid())
      return error(I->second.second,
                   "use of undefined type named '" + I->getKey() + "'");

  if (!ForwardRefComdats.empty())
    return error(ForwardRefComdats.begin()->second,
                 "use of undefined comdat '$" +
                     ForwardRefComdats.begin()->first + "'");

  for (const auto &[Name, Info] : make_early_inc_range(ForwardRefVals)) {
    if (StringRef(Name).starts_with("llvm.")) {
      Intrinsic::ID IID = Function::lookupIntrinsicID(Name);
      if (IID == Intrinsic::not_intrinsic)
        // Don't do anything for unknown intrinsics.
        continue;

      // Automatically create declarations for intrinsics. Intrinsics can only
      // be called directly, so the call function type directly determines the
      // declaration function type.
      //
      // Additionally, automatically add the required mangling suffix to the
      // intrinsic name. This means that we may replace a single forward
      // declaration with multiple functions here.
      for (Use &U : make_early_inc_range(Info.first->uses())) {
        auto *CB = dyn_cast<CallBase>(U.getUser());
        if (!CB || !CB->isCallee(&U))
          return error(Info.second, "intrinsic can only be used as callee");

        SmallVector<Type *> OverloadTys;
        if (!Intrinsic::getIntrinsicSignature(IID, CB->getFunctionType(),
                                              OverloadTys))
          return error(Info.second, "invalid intrinsic signature");

        U.set(Intrinsic::getDeclaration(M, IID, OverloadTys));
      }

      Info.first->eraseFromParent();
      ForwardRefVals.erase(Name);
      continue;
    }

    // If incomplete IR is allowed, also add declarations for
    // non-intrinsics.
    if (!AllowIncompleteIR)
      continue;

    auto GetCommonFunctionType = [](Value *V) -> FunctionType * {
      FunctionType *FTy = nullptr;
      for (Use &U : V->uses()) {
        auto *CB = dyn_cast<CallBase>(U.getUser());
        if (!CB || !CB->isCallee(&U) || (FTy && FTy != CB->getFunctionType()))
          return nullptr;
        FTy = CB->getFunctionType();
      }
      return FTy;
    };

    // First check whether this global is only used in calls with the same
    // type, in which case we'll insert a function. Otherwise, fall back to
    // using a dummy i8 type.
    Type *Ty = GetCommonFunctionType(Info.first);
    if (!Ty)
      Ty = Type::getInt8Ty(Context);

    GlobalValue *GV;
    if (auto *FTy = dyn_cast<FunctionType>(Ty))
      GV = Function::Create(FTy, GlobalValue::ExternalLinkage, Name, M);
    else
      GV = new GlobalVariable(*M, Ty, /*isConstant*/ false,
                              GlobalValue::ExternalLinkage,
                              /*Initializer*/ nullptr, Name);
    Info.first->replaceAllUsesWith(GV);
    Info.first->eraseFromParent();
    ForwardRefVals.erase(Name);
  }

  if (!ForwardRefVals.empty())
    return error(ForwardRefVals.begin()->second.second,
                 "use of undefined value '@" + ForwardRefVals.begin()->first +
                     "'");

  if (!ForwardRefValIDs.empty())
    return error(ForwardRefValIDs.begin()->second.second,
                 "use of undefined value '@" +
                     Twine(ForwardRefValIDs.begin()->first) + "'");

  if (AllowIncompleteIR && !ForwardRefMDNodes.empty())
    dropUnknownMetadataReferences();

  if (!ForwardRefMDNodes.empty())
    return error(ForwardRefMDNodes.begin()->second.second,
                 "use of undefined metadata '!" +
                     Twine(ForwardRefMDNodes.begin()->first) + "'");

  // Resolve metadata cycles.
  for (auto &N : NumberedMetadata) {
    if (N.second && !N.second->isResolved())
      N.second->resolveCycles();
  }

  for (auto *Inst : InstsWithTBAATag) {
    MDNode *MD = Inst->getMetadata(LLVMContext::MD_tbaa);
    // With incomplete IR, the tbaa metadata may have been dropped.
    if (!AllowIncompleteIR)
      assert(MD && "UpgradeInstWithTBAATag should have a TBAA tag");
    if (MD) {
      auto *UpgradedMD = UpgradeTBAANode(*MD);
      if (MD != UpgradedMD)
        Inst->setMetadata(LLVMContext::MD_tbaa, UpgradedMD);
    }
  }

  // Look for intrinsic functions and CallInst that need to be upgraded.  We use
  // make_early_inc_range here because we may remove some functions.
  for (Function &F : llvm::make_early_inc_range(*M))
    UpgradeCallsToIntrinsic(&F);

  if (UpgradeDebugInfo)
    llvm::UpgradeDebugInfo(*M);

  UpgradeModuleFlags(*M);
  UpgradeSectionAttributes(*M);

  if (PreserveInputDbgFormat != cl::boolOrDefault::BOU_TRUE)
    M->setIsNewDbgInfoFormat(UseNewDbgInfoFormat);

  if (!Slots)
    return false;
  // Initialize the slot mapping.
  // Because by this point we've parsed and validated everything, we can "steal"
  // the mapping from LLParser as it doesn't need it anymore.
  Slots->GlobalValues = std::move(NumberedVals);
  Slots->MetadataNodes = std::move(NumberedMetadata);
  for (const auto &I : NamedTypes)
    Slots->NamedTypes.insert(std::make_pair(I.getKey(), I.second.first));
  for (const auto &I : NumberedTypes)
    Slots->Types.insert(std::make_pair(I.first, I.second.first));

  return false;
}

/// Do final validity and basic correctness checks at the end of the index.
bool LLParser::validateEndOfIndex() {
  if (!Index)
    return false;

  if (!ForwardRefValueInfos.empty())
    return error(ForwardRefValueInfos.begin()->second.front().second,
                 "use of undefined summary '^" +
                     Twine(ForwardRefValueInfos.begin()->first) + "'");

  if (!ForwardRefAliasees.empty())
    return error(ForwardRefAliasees.begin()->second.front().second,
                 "use of undefined summary '^" +
                     Twine(ForwardRefAliasees.begin()->first) + "'");

  if (!ForwardRefTypeIds.empty())
    return error(ForwardRefTypeIds.begin()->second.front().second,
                 "use of undefined type id summary '^" +
                     Twine(ForwardRefTypeIds.begin()->first) + "'");

  return false;
}

//===----------------------------------------------------------------------===//
// Top-Level Entities
//===----------------------------------------------------------------------===//

bool LLParser::parseTargetDefinitions(DataLayoutCallbackTy DataLayoutCallback) {
  // Delay parsing of the data layout string until the target triple is known.
  // Then, pass both the the target triple and the tentative data layout string
  // to DataLayoutCallback, allowing to override the DL string.
  // This enables importing modules with invalid DL strings.
  std::string TentativeDLStr = M->getDataLayoutStr();
  LocTy DLStrLoc;

  bool Done = false;
  while (!Done) {
    switch (Lex.getKind()) {
    case lltok::kw_target:
      if (parseTargetDefinition(TentativeDLStr, DLStrLoc))
        return true;
      break;
    case lltok::kw_source_filename:
      if (parseSourceFileName())
        return true;
      break;
    default:
      Done = true;
    }
  }
  // Run the override callback to potentially change the data layout string, and
  // parse the data layout string.
  if (auto LayoutOverride =
          DataLayoutCallback(M->getTargetTriple(), TentativeDLStr)) {
    TentativeDLStr = *LayoutOverride;
    DLStrLoc = {};
  }
  Expected<DataLayout> MaybeDL = DataLayout::parse(TentativeDLStr);
  if (!MaybeDL)
    return error(DLStrLoc, toString(MaybeDL.takeError()));
  M->setDataLayout(MaybeDL.get());
  return false;
}

bool LLParser::parseTopLevelEntities() {
  // If there is no Module, then parse just the summary index entries.
  if (!M) {
    while (true) {
      switch (Lex.getKind()) {
      case lltok::Eof:
        return false;
      case lltok::SummaryID:
        if (parseSummaryEntry())
          return true;
        break;
      case lltok::kw_source_filename:
        if (parseSourceFileName())
          return true;
        break;
      default:
        // Skip everything else
        Lex.Lex();
      }
    }
  }
  while (true) {
    switch (Lex.getKind()) {
    default:
      return tokError("expected top-level entity");
    case lltok::Eof: return false;
    case lltok::kw_declare:
      if (parseDeclare())
        return true;
      break;
    case lltok::kw_define:
      if (parseDefine())
        return true;
      break;
    case lltok::kw_module:
      if (parseModuleAsm())
        return true;
      break;
    case lltok::LocalVarID:
      if (parseUnnamedType())
        return true;
      break;
    case lltok::LocalVar:
      if (parseNamedType())
        return true;
      break;
    case lltok::GlobalID:
      if (parseUnnamedGlobal())
        return true;
      break;
    case lltok::GlobalVar:
      if (parseNamedGlobal())
        return true;
      break;
    case lltok::ComdatVar:  if (parseComdat()) return true; break;
    case lltok::exclaim:
      if (parseStandaloneMetadata())
        return true;
      break;
    case lltok::SummaryID:
      if (parseSummaryEntry())
        return true;
      break;
    case lltok::MetadataVar:
      if (parseNamedMetadata())
        return true;
      break;
    case lltok::kw_attributes:
      if (parseUnnamedAttrGrp())
        return true;
      break;
    case lltok::kw_uselistorder:
      if (parseUseListOrder())
        return true;
      break;
    case lltok::kw_uselistorder_bb:
      if (parseUseListOrderBB())
        return true;
      break;
    }
  }
}

/// toplevelentity
///   ::= 'module' 'asm' STRINGCONSTANT
bool LLParser::parseModuleAsm() {
  assert(Lex.getKind() == lltok::kw_module);
  Lex.Lex();

  std::string AsmStr;
  if (parseToken(lltok::kw_asm, "expected 'module asm'") ||
      parseStringConstant(AsmStr))
    return true;

  M->appendModuleInlineAsm(AsmStr);
  return false;
}

/// toplevelentity
///   ::= 'target' 'triple' '=' STRINGCONSTANT
///   ::= 'target' 'datalayout' '=' STRINGCONSTANT
bool LLParser::parseTargetDefinition(std::string &TentativeDLStr,
                                     LocTy &DLStrLoc) {
  assert(Lex.getKind() == lltok::kw_target);
  std::string Str;
  switch (Lex.Lex()) {
  default:
    return tokError("unknown target property");
  case lltok::kw_triple:
    Lex.Lex();
    if (parseToken(lltok::equal, "expected '=' after target triple") ||
        parseStringConstant(Str))
      return true;
    M->setTargetTriple(Str);
    return false;
  case lltok::kw_datalayout:
    Lex.Lex();
    if (parseToken(lltok::equal, "expected '=' after target datalayout"))
      return true;
    DLStrLoc = Lex.getLoc();
    if (parseStringConstant(TentativeDLStr))
      return true;
    return false;
  }
}

/// toplevelentity
///   ::= 'source_filename' '=' STRINGCONSTANT
bool LLParser::parseSourceFileName() {
  assert(Lex.getKind() == lltok::kw_source_filename);
  Lex.Lex();
  if (parseToken(lltok::equal, "expected '=' after source_filename") ||
      parseStringConstant(SourceFileName))
    return true;
  if (M)
    M->setSourceFileName(SourceFileName);
  return false;
}

/// parseUnnamedType:
///   ::= LocalVarID '=' 'type' type
bool LLParser::parseUnnamedType() {
  LocTy TypeLoc = Lex.getLoc();
  unsigned TypeID = Lex.getUIntVal();
  Lex.Lex(); // eat LocalVarID;

  if (parseToken(lltok::equal, "expected '=' after name") ||
      parseToken(lltok::kw_type, "expected 'type' after '='"))
    return true;

  Type *Result = nullptr;
  if (parseStructDefinition(TypeLoc, "", NumberedTypes[TypeID], Result))
    return true;

  if (!isa<StructType>(Result)) {
    std::pair<Type*, LocTy> &Entry = NumberedTypes[TypeID];
    if (Entry.first)
      return error(TypeLoc, "non-struct types may not be recursive");
    Entry.first = Result;
    Entry.second = SMLoc();
  }

  return false;
}

/// toplevelentity
///   ::= LocalVar '=' 'type' type
bool LLParser::parseNamedType() {
  std::string Name = Lex.getStrVal();
  LocTy NameLoc = Lex.getLoc();
  Lex.Lex();  // eat LocalVar.

  if (parseToken(lltok::equal, "expected '=' after name") ||
      parseToken(lltok::kw_type, "expected 'type' after name"))
    return true;

  Type *Result = nullptr;
  if (parseStructDefinition(NameLoc, Name, NamedTypes[Name], Result))
    return true;

  if (!isa<StructType>(Result)) {
    std::pair<Type*, LocTy> &Entry = NamedTypes[Name];
    if (Entry.first)
      return error(NameLoc, "non-struct types may not be recursive");
    Entry.first = Result;
    Entry.second = SMLoc();
  }

  return false;
}

/// toplevelentity
///   ::= 'declare' FunctionHeader
bool LLParser::parseDeclare() {
  assert(Lex.getKind() == lltok::kw_declare);
  Lex.Lex();

  std::vector<std::pair<unsigned, MDNode *>> MDs;
  while (Lex.getKind() == lltok::MetadataVar) {
    unsigned MDK;
    MDNode *N;
    if (parseMetadataAttachment(MDK, N))
      return true;
    MDs.push_back({MDK, N});
  }

  Function *F;
  unsigned FunctionNumber = -1;
  SmallVector<unsigned> UnnamedArgNums;
  if (parseFunctionHeader(F, false, FunctionNumber, UnnamedArgNums))
    return true;
  for (auto &MD : MDs)
    F->addMetadata(MD.first, *MD.second);
  return false;
}

/// toplevelentity
///   ::= 'define' FunctionHeader (!dbg !56)* '{' ...
bool LLParser::parseDefine() {
  assert(Lex.getKind() == lltok::kw_define);
  Lex.Lex();

  Function *F;
  unsigned FunctionNumber = -1;
  SmallVector<unsigned> UnnamedArgNums;
  return parseFunctionHeader(F, true, FunctionNumber, UnnamedArgNums) ||
         parseOptionalFunctionMetadata(*F) ||
         parseFunctionBody(*F, FunctionNumber, UnnamedArgNums);
}

/// parseGlobalType
///   ::= 'constant'
///   ::= 'global'
bool LLParser::parseGlobalType(bool &IsConstant) {
  if (Lex.getKind() == lltok::kw_constant)
    IsConstant = true;
  else if (Lex.getKind() == lltok::kw_global)
    IsConstant = false;
  else {
    IsConstant = false;
    return tokError("expected 'global' or 'constant'");
  }
  Lex.Lex();
  return false;
}

bool LLParser::parseOptionalUnnamedAddr(
    GlobalVariable::UnnamedAddr &UnnamedAddr) {
  if (EatIfPresent(lltok::kw_unnamed_addr))
    UnnamedAddr = GlobalValue::UnnamedAddr::Global;
  else if (EatIfPresent(lltok::kw_local_unnamed_addr))
    UnnamedAddr = GlobalValue::UnnamedAddr::Local;
  else
    UnnamedAddr = GlobalValue::UnnamedAddr::None;
  return false;
}

/// parseUnnamedGlobal:
///   OptionalVisibility (ALIAS | IFUNC) ...
///   OptionalLinkage OptionalPreemptionSpecifier OptionalVisibility
///   OptionalDLLStorageClass
///                                                     ...   -> global variable
///   GlobalID '=' OptionalVisibility (ALIAS | IFUNC) ...
///   GlobalID '=' OptionalLinkage OptionalPreemptionSpecifier
///   OptionalVisibility
///                OptionalDLLStorageClass
///                                                     ...   -> global variable
bool LLParser::parseUnnamedGlobal() {
  unsigned VarID;
  std::string Name;
  LocTy NameLoc = Lex.getLoc();

  // Handle the GlobalID form.
  if (Lex.getKind() == lltok::GlobalID) {
    VarID = Lex.getUIntVal();
    if (checkValueID(NameLoc, "global", "@", NumberedVals.getNext(), VarID))
      return true;

    Lex.Lex(); // eat GlobalID;
    if (parseToken(lltok::equal, "expected '=' after name"))
      return true;
  } else {
    VarID = NumberedVals.getNext();
  }

  bool HasLinkage;
  unsigned Linkage, Visibility, DLLStorageClass;
  bool DSOLocal;
  GlobalVariable::ThreadLocalMode TLM;
  GlobalVariable::UnnamedAddr UnnamedAddr;
  if (parseOptionalLinkage(Linkage, HasLinkage, Visibility, DLLStorageClass,
                           DSOLocal) ||
      parseOptionalThreadLocal(TLM) || parseOptionalUnnamedAddr(UnnamedAddr))
    return true;

  switch (Lex.getKind()) {
  default:
    return parseGlobal(Name, VarID, NameLoc, Linkage, HasLinkage, Visibility,
                       DLLStorageClass, DSOLocal, TLM, UnnamedAddr);
  case lltok::kw_alias:
  case lltok::kw_ifunc:
    return parseAliasOrIFunc(Name, VarID, NameLoc, Linkage, Visibility,
                             DLLStorageClass, DSOLocal, TLM, UnnamedAddr);
  }
}

/// parseNamedGlobal:
///   GlobalVar '=' OptionalVisibility (ALIAS | IFUNC) ...
///   GlobalVar '=' OptionalLinkage OptionalPreemptionSpecifier
///                 OptionalVisibility OptionalDLLStorageClass
///                                                     ...   -> global variable
bool LLParser::parseNamedGlobal() {
  assert(Lex.getKind() == lltok::GlobalVar);
  LocTy NameLoc = Lex.getLoc();
  std::string Name = Lex.getStrVal();
  Lex.Lex();

  bool HasLinkage;
  unsigned Linkage, Visibility, DLLStorageClass;
  bool DSOLocal;
  GlobalVariable::ThreadLocalMode TLM;
  GlobalVariable::UnnamedAddr UnnamedAddr;
  if (parseToken(lltok::equal, "expected '=' in global variable") ||
      parseOptionalLinkage(Linkage, HasLinkage, Visibility, DLLStorageClass,
                           DSOLocal) ||
      parseOptionalThreadLocal(TLM) || parseOptionalUnnamedAddr(UnnamedAddr))
    return true;

  switch (Lex.getKind()) {
  default:
    return parseGlobal(Name, -1, NameLoc, Linkage, HasLinkage, Visibility,
                       DLLStorageClass, DSOLocal, TLM, UnnamedAddr);
  case lltok::kw_alias:
  case lltok::kw_ifunc:
    return parseAliasOrIFunc(Name, -1, NameLoc, Linkage, Visibility,
                             DLLStorageClass, DSOLocal, TLM, UnnamedAddr);
  }
}

bool LLParser::parseComdat() {
  assert(Lex.getKind() == lltok::ComdatVar);
  std::string Name = Lex.getStrVal();
  LocTy NameLoc = Lex.getLoc();
  Lex.Lex();

  if (parseToken(lltok::equal, "expected '=' here"))
    return true;

  if (parseToken(lltok::kw_comdat, "expected comdat keyword"))
    return tokError("expected comdat type");

  Comdat::SelectionKind SK;
  switch (Lex.getKind()) {
  default:
    return tokError("unknown selection kind");
  case lltok::kw_any:
    SK = Comdat::Any;
    break;
  case lltok::kw_exactmatch:
    SK = Comdat::ExactMatch;
    break;
  case lltok::kw_largest:
    SK = Comdat::Largest;
    break;
  case lltok::kw_nodeduplicate:
    SK = Comdat::NoDeduplicate;
    break;
  case lltok::kw_samesize:
    SK = Comdat::SameSize;
    break;
  }
  Lex.Lex();

  // See if the comdat was forward referenced, if so, use the comdat.
  Module::ComdatSymTabType &ComdatSymTab = M->getComdatSymbolTable();
  Module::ComdatSymTabType::iterator I = ComdatSymTab.find(Name);
  if (I != ComdatSymTab.end() && !ForwardRefComdats.erase(Name))
    return error(NameLoc, "redefinition of comdat '$" + Name + "'");

  Comdat *C;
  if (I != ComdatSymTab.end())
    C = &I->second;
  else
    C = M->getOrInsertComdat(Name);
  C->setSelectionKind(SK);

  return false;
}

// MDString:
//   ::= '!' STRINGCONSTANT
bool LLParser::parseMDString(MDString *&Result) {
  std::string Str;
  if (parseStringConstant(Str))
    return true;
  Result = MDString::get(Context, Str);
  return false;
}

// MDNode:
//   ::= '!' MDNodeNumber
bool LLParser::parseMDNodeID(MDNode *&Result) {
  // !{ ..., !42, ... }
  LocTy IDLoc = Lex.getLoc();
  unsigned MID = 0;
  if (parseUInt32(MID))
    return true;

  // If not a forward reference, just return it now.
  if (NumberedMetadata.count(MID)) {
    Result = NumberedMetadata[MID];
    return false;
  }

  // Otherwise, create MDNode forward reference.
  auto &FwdRef = ForwardRefMDNodes[MID];
  FwdRef = std::make_pair(MDTuple::getTemporary(Context, std::nullopt), IDLoc);

  Result = FwdRef.first.get();
  NumberedMetadata[MID].reset(Result);
  return false;
}

/// parseNamedMetadata:
///   !foo = !{ !1, !2 }
bool LLParser::parseNamedMetadata() {
  assert(Lex.getKind() == lltok::MetadataVar);
  std::string Name = Lex.getStrVal();
  Lex.Lex();

  if (parseToken(lltok::equal, "expected '=' here") ||
      parseToken(lltok::exclaim, "Expected '!' here") ||
      parseToken(lltok::lbrace, "Expected '{' here"))
    return true;

  NamedMDNode *NMD = M->getOrInsertNamedMetadata(Name);
  if (Lex.getKind() != lltok::rbrace)
    do {
      MDNode *N = nullptr;
      // parse DIExpressions inline as a special case. They are still MDNodes,
      // so they can still appear in named metadata. Remove this logic if they
      // become plain Metadata.
      if (Lex.getKind() == lltok::MetadataVar &&
          Lex.getStrVal() == "DIExpression") {
        if (parseDIExpression(N, /*IsDistinct=*/false))
          return true;
        // DIArgLists should only appear inline in a function, as they may
        // contain LocalAsMetadata arguments which require a function context.
      } else if (Lex.getKind() == lltok::MetadataVar &&
                 Lex.getStrVal() == "DIArgList") {
        return tokError("found DIArgList outside of function");
      } else if (parseToken(lltok::exclaim, "Expected '!' here") ||
                 parseMDNodeID(N)) {
        return true;
      }
      NMD->addOperand(N);
    } while (EatIfPresent(lltok::comma));

  return parseToken(lltok::rbrace, "expected end of metadata node");
}

/// parseStandaloneMetadata:
///   !42 = !{...}
bool LLParser::parseStandaloneMetadata() {
  assert(Lex.getKind() == lltok::exclaim);
  Lex.Lex();
  unsigned MetadataID = 0;

  MDNode *Init;
  if (parseUInt32(MetadataID) || parseToken(lltok::equal, "expected '=' here"))
    return true;

  // Detect common error, from old metadata syntax.
  if (Lex.getKind() == lltok::Type)
    return tokError("unexpected type in metadata definition");

  bool IsDistinct = EatIfPresent(lltok::kw_distinct);
  if (Lex.getKind() == lltok::MetadataVar) {
    if (parseSpecializedMDNode(Init, IsDistinct))
      return true;
  } else if (parseToken(lltok::exclaim, "Expected '!' here") ||
             parseMDTuple(Init, IsDistinct))
    return true;

  // See if this was forward referenced, if so, handle it.
  auto FI = ForwardRefMDNodes.find(MetadataID);
  if (FI != ForwardRefMDNodes.end()) {
    auto *ToReplace = FI->second.first.get();
    // DIAssignID has its own special forward-reference "replacement" for
    // attachments (the temporary attachments are never actually attached).
    if (isa<DIAssignID>(Init)) {
      for (auto *Inst : TempDIAssignIDAttachments[ToReplace]) {
        assert(!Inst->getMetadata(LLVMContext::MD_DIAssignID) &&
               "Inst unexpectedly already has DIAssignID attachment");
        Inst->setMetadata(LLVMContext::MD_DIAssignID, Init);
      }
    }

    ToReplace->replaceAllUsesWith(Init);
    ForwardRefMDNodes.erase(FI);

    assert(NumberedMetadata[MetadataID] == Init && "Tracking VH didn't work");
  } else {
    if (NumberedMetadata.count(MetadataID))
      return tokError("Metadata id is already used");
    NumberedMetadata[MetadataID].reset(Init);
  }

  return false;
}

// Skips a single module summary entry.
bool LLParser::skipModuleSummaryEntry() {
  // Each module summary entry consists of a tag for the entry
  // type, followed by a colon, then the fields which may be surrounded by
  // nested sets of parentheses. The "tag:" looks like a Label. Once parsing
  // support is in place we will look for the tokens corresponding to the
  // expected tags.
  if (Lex.getKind() != lltok::kw_gv && Lex.getKind() != lltok::kw_module &&
      Lex.getKind() != lltok::kw_typeid && Lex.getKind() != lltok::kw_flags &&
      Lex.getKind() != lltok::kw_blockcount)
    return tokError(
        "Expected 'gv', 'module', 'typeid', 'flags' or 'blockcount' at the "
        "start of summary entry");
  if (Lex.getKind() == lltok::kw_flags)
    return parseSummaryIndexFlags();
  if (Lex.getKind() == lltok::kw_blockcount)
    return parseBlockCount();
  Lex.Lex();
  if (parseToken(lltok::colon, "expected ':' at start of summary entry") ||
      parseToken(lltok::lparen, "expected '(' at start of summary entry"))
    return true;
  // Now walk through the parenthesized entry, until the number of open
  // parentheses goes back down to 0 (the first '(' was parsed above).
  unsigned NumOpenParen = 1;
  do {
    switch (Lex.getKind()) {
    case lltok::lparen:
      NumOpenParen++;
      break;
    case lltok::rparen:
      NumOpenParen--;
      break;
    case lltok::Eof:
      return tokError("found end of file while parsing summary entry");
    default:
      // Skip everything in between parentheses.
      break;
    }
    Lex.Lex();
  } while (NumOpenParen > 0);
  return false;
}

/// SummaryEntry
///   ::= SummaryID '=' GVEntry | ModuleEntry | TypeIdEntry
bool LLParser::parseSummaryEntry() {
  assert(Lex.getKind() == lltok::SummaryID);
  unsigned SummaryID = Lex.getUIntVal();

  // For summary entries, colons should be treated as distinct tokens,
  // not an indication of the end of a label token.
  Lex.setIgnoreColonInIdentifiers(true);

  Lex.Lex();
  if (parseToken(lltok::equal, "expected '=' here"))
    return true;

  // If we don't have an index object, skip the summary entry.
  if (!Index)
    return skipModuleSummaryEntry();

  bool result = false;
  switch (Lex.getKind()) {
  case lltok::kw_gv:
    result = parseGVEntry(SummaryID);
    break;
  case lltok::kw_module:
    result = parseModuleEntry(SummaryID);
    break;
  case lltok::kw_typeid:
    result = parseTypeIdEntry(SummaryID);
    break;
  case lltok::kw_typeidCompatibleVTable:
    result = parseTypeIdCompatibleVtableEntry(SummaryID);
    break;
  case lltok::kw_flags:
    result = parseSummaryIndexFlags();
    break;
  case lltok::kw_blockcount:
    result = parseBlockCount();
    break;
  default:
    result = error(Lex.getLoc(), "unexpected summary kind");
    break;
  }
  Lex.setIgnoreColonInIdentifiers(false);
  return result;
}

static bool isValidVisibilityForLinkage(unsigned V, unsigned L) {
  return !GlobalValue::isLocalLinkage((GlobalValue::LinkageTypes)L) ||
         (GlobalValue::VisibilityTypes)V == GlobalValue::DefaultVisibility;
}
static bool isValidDLLStorageClassForLinkage(unsigned S, unsigned L) {
  return !GlobalValue::isLocalLinkage((GlobalValue::LinkageTypes)L) ||
         (GlobalValue::DLLStorageClassTypes)S == GlobalValue::DefaultStorageClass;
}

// If there was an explicit dso_local, update GV. In the absence of an explicit
// dso_local we keep the default value.
static void maybeSetDSOLocal(bool DSOLocal, GlobalValue &GV) {
  if (DSOLocal)
    GV.setDSOLocal(true);
}

/// parseAliasOrIFunc:
///   ::= GlobalVar '=' OptionalLinkage OptionalPreemptionSpecifier
///                     OptionalVisibility OptionalDLLStorageClass
///                     OptionalThreadLocal OptionalUnnamedAddr
///                     'alias|ifunc' AliaseeOrResolver SymbolAttrs*
///
/// AliaseeOrResolver
///   ::= TypeAndValue
///
/// SymbolAttrs
///   ::= ',' 'partition' StringConstant
///
/// Everything through OptionalUnnamedAddr has already been parsed.
///
bool LLParser::parseAliasOrIFunc(const std::string &Name, unsigned NameID,
                                 LocTy NameLoc, unsigned L, unsigned Visibility,
                                 unsigned DLLStorageClass, bool DSOLocal,
                                 GlobalVariable::ThreadLocalMode TLM,
                                 GlobalVariable::UnnamedAddr UnnamedAddr) {
  bool IsAlias;
  if (Lex.getKind() == lltok::kw_alias)
    IsAlias = true;
  else if (Lex.getKind() == lltok::kw_ifunc)
    IsAlias = false;
  else
    llvm_unreachable("Not an alias or ifunc!");
  Lex.Lex();

  GlobalValue::LinkageTypes Linkage = (GlobalValue::LinkageTypes) L;

  if(IsAlias && !GlobalAlias::isValidLinkage(Linkage))
    return error(NameLoc, "invalid linkage type for alias");

  if (!isValidVisibilityForLinkage(Visibility, L))
    return error(NameLoc,
                 "symbol with local linkage must have default visibility");

  if (!isValidDLLStorageClassForLinkage(DLLStorageClass, L))
    return error(NameLoc,
                 "symbol with local linkage cannot have a DLL storage class");

  Type *Ty;
  LocTy ExplicitTypeLoc = Lex.getLoc();
  if (parseType(Ty) ||
      parseToken(lltok::comma, "expected comma after alias or ifunc's type"))
    return true;

  Constant *Aliasee;
  LocTy AliaseeLoc = Lex.getLoc();
  if (Lex.getKind() != lltok::kw_bitcast &&
      Lex.getKind() != lltok::kw_getelementptr &&
      Lex.getKind() != lltok::kw_addrspacecast &&
      Lex.getKind() != lltok::kw_inttoptr) {
    if (parseGlobalTypeAndValue(Aliasee))
      return true;
  } else {
    // The bitcast dest type is not present, it is implied by the dest type.
    ValID ID;
    if (parseValID(ID, /*PFS=*/nullptr))
      return true;
    if (ID.Kind != ValID::t_Constant)
      return error(AliaseeLoc, "invalid aliasee");
    Aliasee = ID.ConstantVal;
  }

  Type *AliaseeType = Aliasee->getType();
  auto *PTy = dyn_cast<PointerType>(AliaseeType);
  if (!PTy)
    return error(AliaseeLoc, "An alias or ifunc must have pointer type");
  unsigned AddrSpace = PTy->getAddressSpace();

  GlobalValue *GVal = nullptr;

  // See if the alias was forward referenced, if so, prepare to replace the
  // forward reference.
  if (!Name.empty()) {
    auto I = ForwardRefVals.find(Name);
    if (I != ForwardRefVals.end()) {
      GVal = I->second.first;
      ForwardRefVals.erase(Name);
    } else if (M->getNamedValue(Name)) {
      return error(NameLoc, "redefinition of global '@" + Name + "'");
    }
  } else {
    auto I = ForwardRefValIDs.find(NameID);
    if (I != ForwardRefValIDs.end()) {
      GVal = I->second.first;
      ForwardRefValIDs.erase(I);
    }
  }

  // Okay, create the alias/ifunc but do not insert it into the module yet.
  std::unique_ptr<GlobalAlias> GA;
  std::unique_ptr<GlobalIFunc> GI;
  GlobalValue *GV;
  if (IsAlias) {
    GA.reset(GlobalAlias::create(Ty, AddrSpace,
                                 (GlobalValue::LinkageTypes)Linkage, Name,
                                 Aliasee, /*Parent*/ nullptr));
    GV = GA.get();
  } else {
    GI.reset(GlobalIFunc::create(Ty, AddrSpace,
                                 (GlobalValue::LinkageTypes)Linkage, Name,
                                 Aliasee, /*Parent*/ nullptr));
    GV = GI.get();
  }
  GV->setThreadLocalMode(TLM);
  GV->setVisibility((GlobalValue::VisibilityTypes)Visibility);
  GV->setDLLStorageClass((GlobalValue::DLLStorageClassTypes)DLLStorageClass);
  GV->setUnnamedAddr(UnnamedAddr);
  maybeSetDSOLocal(DSOLocal, *GV);

  // At this point we've parsed everything except for the IndirectSymbolAttrs.
  // Now parse them if there are any.
  while (Lex.getKind() == lltok::comma) {
    Lex.Lex();

    if (Lex.getKind() == lltok::kw_partition) {
      Lex.Lex();
      GV->setPartition(Lex.getStrVal());
      if (parseToken(lltok::StringConstant, "expected partition string"))
        return true;
    } else {
      return tokError("unknown alias or ifunc property!");
    }
  }

  if (Name.empty())
    NumberedVals.add(NameID, GV);

  if (GVal) {
    // Verify that types agree.
    if (GVal->getType() != GV->getType())
      return error(
          ExplicitTypeLoc,
          "forward reference and definition of alias have different types");

    // If they agree, just RAUW the old value with the alias and remove the
    // forward ref info.
    GVal->replaceAllUsesWith(GV);
    GVal->eraseFromParent();
  }

  // Insert into the module, we know its name won't collide now.
  if (IsAlias)
    M->insertAlias(GA.release());
  else
    M->insertIFunc(GI.release());
  assert(GV->getName() == Name && "Should not be a name conflict!");

  return false;
}

static bool isSanitizer(lltok::Kind Kind) {
  switch (Kind) {
  case lltok::kw_no_sanitize_address:
  case lltok::kw_no_sanitize_hwaddress:
  case lltok::kw_sanitize_memtag:
  case lltok::kw_sanitize_address_dyninit:
    return true;
  default:
    return false;
  }
}

bool LLParser::parseSanitizer(GlobalVariable *GV) {
  using SanitizerMetadata = GlobalValue::SanitizerMetadata;
  SanitizerMetadata Meta;
  if (GV->hasSanitizerMetadata())
    Meta = GV->getSanitizerMetadata();

  switch (Lex.getKind()) {
  case lltok::kw_no_sanitize_address:
    Meta.NoAddress = true;
    break;
  case lltok::kw_no_sanitize_hwaddress:
    Meta.NoHWAddress = true;
    break;
  case lltok::kw_sanitize_memtag:
    Meta.Memtag = true;
    break;
  case lltok::kw_sanitize_address_dyninit:
    Meta.IsDynInit = true;
    break;
  default:
    return tokError("non-sanitizer token passed to LLParser::parseSanitizer()");
  }
  GV->setSanitizerMetadata(Meta);
  Lex.Lex();
  return false;
}

/// parseGlobal
///   ::= GlobalVar '=' OptionalLinkage OptionalPreemptionSpecifier
///       OptionalVisibility OptionalDLLStorageClass
///       OptionalThreadLocal OptionalUnnamedAddr OptionalAddrSpace
///       OptionalExternallyInitialized GlobalType Type Const OptionalAttrs
///   ::= OptionalLinkage OptionalPreemptionSpecifier OptionalVisibility
///       OptionalDLLStorageClass OptionalThreadLocal OptionalUnnamedAddr
///       OptionalAddrSpace OptionalExternallyInitialized GlobalType Type
///       Const OptionalAttrs
///
/// Everything up to and including OptionalUnnamedAddr has been parsed
/// already.
///
bool LLParser::parseGlobal(const std::string &Name, unsigned NameID,
                           LocTy NameLoc, unsigned Linkage, bool HasLinkage,
                           unsigned Visibility, unsigned DLLStorageClass,
                           bool DSOLocal, GlobalVariable::ThreadLocalMode TLM,
                           GlobalVariable::UnnamedAddr UnnamedAddr) {
  if (!isValidVisibilityForLinkage(Visibility, Linkage))
    return error(NameLoc,
                 "symbol with local linkage must have default visibility");

  if (!isValidDLLStorageClassForLinkage(DLLStorageClass, Linkage))
    return error(NameLoc,
                 "symbol with local linkage cannot have a DLL storage class");

  unsigned AddrSpace;
  bool IsConstant, IsExternallyInitialized;
  LocTy IsExternallyInitializedLoc;
  LocTy TyLoc;

  Type *Ty = nullptr;
  if (parseOptionalAddrSpace(AddrSpace) ||
      parseOptionalToken(lltok::kw_externally_initialized,
                         IsExternallyInitialized,
                         &IsExternallyInitializedLoc) ||
      parseGlobalType(IsConstant) || parseType(Ty, TyLoc))
    return true;

  // If the linkage is specified and is external, then no initializer is
  // present.
  Constant *Init = nullptr;
  if (!HasLinkage ||
      !GlobalValue::isValidDeclarationLinkage(
          (GlobalValue::LinkageTypes)Linkage)) {
    if (parseGlobalValue(Ty, Init))
      return true;
  }

  if (Ty->isFunctionTy() || !PointerType::isValidElementType(Ty))
    return error(TyLoc, "invalid type for global variable");

  GlobalValue *GVal = nullptr;

  // See if the global was forward referenced, if so, use the global.
  if (!Name.empty()) {
    auto I = ForwardRefVals.find(Name);
    if (I != ForwardRefVals.end()) {
      GVal = I->second.first;
      ForwardRefVals.erase(I);
    } else if (M->getNamedValue(Name)) {
      return error(NameLoc, "redefinition of global '@" + Name + "'");
    }
  } else {
    // Handle @"", where a name is syntactically specified, but semantically
    // missing.
    if (NameID == (unsigned)-1)
      NameID = NumberedVals.getNext();

    auto I = ForwardRefValIDs.find(NameID);
    if (I != ForwardRefValIDs.end()) {
      GVal = I->second.first;
      ForwardRefValIDs.erase(I);
    }
  }

  GlobalVariable *GV = new GlobalVariable(
      *M, Ty, false, GlobalValue::ExternalLinkage, nullptr, Name, nullptr,
      GlobalVariable::NotThreadLocal, AddrSpace);

  if (Name.empty())
    NumberedVals.add(NameID, GV);

  // Set the parsed properties on the global.
  if (Init)
    GV->setInitializer(Init);
  GV->setConstant(IsConstant);
  GV->setLinkage((GlobalValue::LinkageTypes)Linkage);
  maybeSetDSOLocal(DSOLocal, *GV);
  GV->setVisibility((GlobalValue::VisibilityTypes)Visibility);
  GV->setDLLStorageClass((GlobalValue::DLLStorageClassTypes)DLLStorageClass);
  GV->setExternallyInitialized(IsExternallyInitialized);
  GV->setThreadLocalMode(TLM);
  GV->setUnnamedAddr(UnnamedAddr);

  if (GVal) {
    if (GVal->getAddressSpace() != AddrSpace)
      return error(
          TyLoc,
          "forward reference and definition of global have different types");

    GVal->replaceAllUsesWith(GV);
    GVal->eraseFromParent();
  }

  // parse attributes on the global.
  while (Lex.getKind() == lltok::comma) {
    Lex.Lex();

    if (Lex.getKind() == lltok::kw_section) {
      Lex.Lex();
      GV->setSection(Lex.getStrVal());
      if (parseToken(lltok::StringConstant, "expected global section string"))
        return true;
    } else if (Lex.getKind() == lltok::kw_partition) {
      Lex.Lex();
      GV->setPartition(Lex.getStrVal());
      if (parseToken(lltok::StringConstant, "expected partition string"))
        return true;
    } else if (Lex.getKind() == lltok::kw_align) {
      MaybeAlign Alignment;
      if (parseOptionalAlignment(Alignment))
        return true;
      if (Alignment)
        GV->setAlignment(*Alignment);
    } else if (Lex.getKind() == lltok::kw_code_model) {
      CodeModel::Model CodeModel;
      if (parseOptionalCodeModel(CodeModel))
        return true;
      GV->setCodeModel(CodeModel);
    } else if (Lex.getKind() == lltok::MetadataVar) {
      if (parseGlobalObjectMetadataAttachment(*GV))
        return true;
    } else if (isSanitizer(Lex.getKind())) {
      if (parseSanitizer(GV))
        return true;
    } else {
      Comdat *C;
      if (parseOptionalComdat(Name, C))
        return true;
      if (C)
        GV->setComdat(C);
      else
        return tokError("unknown global variable property!");
    }
  }

  AttrBuilder Attrs(M->getContext());
  LocTy BuiltinLoc;
  std::vector<unsigned> FwdRefAttrGrps;
  if (parseFnAttributeValuePairs(Attrs, FwdRefAttrGrps, false, BuiltinLoc))
    return true;
  if (Attrs.hasAttributes() || !FwdRefAttrGrps.empty()) {
    GV->setAttributes(AttributeSet::get(Context, Attrs));
    ForwardRefAttrGroups[GV] = FwdRefAttrGrps;
  }

  return false;
}

/// parseUnnamedAttrGrp
///   ::= 'attributes' AttrGrpID '=' '{' AttrValPair+ '}'
bool LLParser::parseUnnamedAttrGrp() {
  assert(Lex.getKind() == lltok::kw_attributes);
  LocTy AttrGrpLoc = Lex.getLoc();
  Lex.Lex();

  if (Lex.getKind() != lltok::AttrGrpID)
    return tokError("expected attribute group id");

  unsigned VarID = Lex.getUIntVal();
  std::vector<unsigned> unused;
  LocTy BuiltinLoc;
  Lex.Lex();

  if (parseToken(lltok::equal, "expected '=' here") ||
      parseToken(lltok::lbrace, "expected '{' here"))
    return true;

  auto R = NumberedAttrBuilders.find(VarID);
  if (R == NumberedAttrBuilders.end())
    R = NumberedAttrBuilders.emplace(VarID, AttrBuilder(M->getContext())).first;

  if (parseFnAttributeValuePairs(R->second, unused, true, BuiltinLoc) ||
      parseToken(lltok::rbrace, "expected end of attribute group"))
    return true;

  if (!R->second.hasAttributes())
    return error(AttrGrpLoc, "attribute group has no attributes");

  return false;
}

static Attribute::AttrKind tokenToAttribute(lltok::Kind Kind) {
  switch (Kind) {
#define GET_ATTR_NAMES
#define ATTRIBUTE_ENUM(ENUM_NAME, DISPLAY_NAME) \
  case lltok::kw_##DISPLAY_NAME: \
    return Attribute::ENUM_NAME;
#include "llvm/IR/Attributes.inc"
  default:
    return Attribute::None;
  }
}

bool LLParser::parseEnumAttribute(Attribute::AttrKind Attr, AttrBuilder &B,
                                  bool InAttrGroup) {
  if (Attribute::isTypeAttrKind(Attr))
    return parseRequiredTypeAttr(B, Lex.getKind(), Attr);

  switch (Attr) {
  case Attribute::Alignment: {
    MaybeAlign Alignment;
    if (InAttrGroup) {
      uint32_t Value = 0;
      Lex.Lex();
      if (parseToken(lltok::equal, "expected '=' here") || parseUInt32(Value))
        return true;
      Alignment = Align(Value);
    } else {
      if (parseOptionalAlignment(Alignment, true))
        return true;
    }
    B.addAlignmentAttr(Alignment);
    return false;
  }
  case Attribute::StackAlignment: {
    unsigned Alignment;
    if (InAttrGroup) {
      Lex.Lex();
      if (parseToken(lltok::equal, "expected '=' here") ||
          parseUInt32(Alignment))
        return true;
    } else {
      if (parseOptionalStackAlignment(Alignment))
        return true;
    }
    B.addStackAlignmentAttr(Alignment);
    return false;
  }
  case Attribute::AllocSize: {
    unsigned ElemSizeArg;
    std::optional<unsigned> NumElemsArg;
    if (parseAllocSizeArguments(ElemSizeArg, NumElemsArg))
      return true;
    B.addAllocSizeAttr(ElemSizeArg, NumElemsArg);
    return false;
  }
  case Attribute::VScaleRange: {
    unsigned MinValue, MaxValue;
    if (parseVScaleRangeArguments(MinValue, MaxValue))
      return true;
    B.addVScaleRangeAttr(MinValue,
                         MaxValue > 0 ? MaxValue : std::optional<unsigned>());
    return false;
  }
  case Attribute::Dereferenceable: {
    uint64_t Bytes;
    if (parseOptionalDerefAttrBytes(lltok::kw_dereferenceable, Bytes))
      return true;
    B.addDereferenceableAttr(Bytes);
    return false;
  }
  case Attribute::DereferenceableOrNull: {
    uint64_t Bytes;
    if (parseOptionalDerefAttrBytes(lltok::kw_dereferenceable_or_null, Bytes))
      return true;
    B.addDereferenceableOrNullAttr(Bytes);
    return false;
  }
  case Attribute::UWTable: {
    UWTableKind Kind;
    if (parseOptionalUWTableKind(Kind))
      return true;
    B.addUWTableAttr(Kind);
    return false;
  }
  case Attribute::AllocKind: {
    AllocFnKind Kind = AllocFnKind::Unknown;
    if (parseAllocKind(Kind))
      return true;
    B.addAllocKindAttr(Kind);
    return false;
  }
  case Attribute::Memory: {
    std::optional<MemoryEffects> ME = parseMemoryAttr();
    if (!ME)
      return true;
    B.addMemoryAttr(*ME);
    return false;
  }
  case Attribute::NoFPClass: {
    if (FPClassTest NoFPClass =
            static_cast<FPClassTest>(parseNoFPClassAttr())) {
      B.addNoFPClassAttr(NoFPClass);
      return false;
    }

    return true;
  }
  case Attribute::Range:
    return parseRangeAttr(B);
  case Attribute::Initializes:
    return parseInitializesAttr(B);
  default:
    B.addAttribute(Attr);
    Lex.Lex();
    return false;
  }
}

static bool upgradeMemoryAttr(MemoryEffects &ME, lltok::Kind Kind) {
  switch (Kind) {
  case lltok::kw_readnone:
    ME &= MemoryEffects::none();
    return true;
  case lltok::kw_readonly:
    ME &= MemoryEffects::readOnly();
    return true;
  case lltok::kw_writeonly:
    ME &= MemoryEffects::writeOnly();
    return true;
  case lltok::kw_argmemonly:
    ME &= MemoryEffects::argMemOnly();
    return true;
  case lltok::kw_inaccessiblememonly:
    ME &= MemoryEffects::inaccessibleMemOnly();
    return true;
  case lltok::kw_inaccessiblemem_or_argmemonly:
    ME &= MemoryEffects::inaccessibleOrArgMemOnly();
    return true;
  default:
    return false;
  }
}

/// parseFnAttributeValuePairs
///   ::= <attr> | <attr> '=' <value>
bool LLParser::parseFnAttributeValuePairs(AttrBuilder &B,
                                          std::vector<unsigned> &FwdRefAttrGrps,
                                          bool InAttrGrp, LocTy &BuiltinLoc) {
  bool HaveError = false;

  B.clear();

  MemoryEffects ME = MemoryEffects::unknown();
  while (true) {
    lltok::Kind Token = Lex.getKind();
    if (Token == lltok::rbrace)
      break; // Finished.

    if (Token == lltok::StringConstant) {
      if (parseStringAttribute(B))
        return true;
      continue;
    }

    if (Token == lltok::AttrGrpID) {
      // Allow a function to reference an attribute group:
      //
      //   define void @foo() #1 { ... }
      if (InAttrGrp) {
        HaveError |= error(
            Lex.getLoc(),
            "cannot have an attribute group reference in an attribute group");
      } else {
        // Save the reference to the attribute group. We'll fill it in later.
        FwdRefAttrGrps.push_back(Lex.getUIntVal());
      }
      Lex.Lex();
      continue;
    }

    SMLoc Loc = Lex.getLoc();
    if (Token == lltok::kw_builtin)
      BuiltinLoc = Loc;

    if (upgradeMemoryAttr(ME, Token)) {
      Lex.Lex();
      continue;
    }

    Attribute::AttrKind Attr = tokenToAttribute(Token);
    if (Attr == Attribute::None) {
      if (!InAttrGrp)
        break;
      return error(Lex.getLoc(), "unterminated attribute group");
    }

    if (parseEnumAttribute(Attr, B, InAttrGrp))
      return true;

    // As a hack, we allow function alignment to be initially parsed as an
    // attribute on a function declaration/definition or added to an attribute
    // group and later moved to the alignment field.
    if (!Attribute::canUseAsFnAttr(Attr) && Attr != Attribute::Alignment)
      HaveError |= error(Loc, "this attribute does not apply to functions");
  }

  if (ME != MemoryEffects::unknown())
    B.addMemoryAttr(ME);
  return HaveError;
}

//===----------------------------------------------------------------------===//
// GlobalValue Reference/Resolution Routines.
//===----------------------------------------------------------------------===//

static inline GlobalValue *createGlobalFwdRef(Module *M, PointerType *PTy) {
  // The used global type does not matter. We will later RAUW it with a
  // global/function of the correct type.
  return new GlobalVariable(*M, Type::getInt8Ty(M->getContext()), false,
                            GlobalValue::ExternalWeakLinkage, nullptr, "",
                            nullptr, GlobalVariable::NotThreadLocal,
                            PTy->getAddressSpace());
}

Value *LLParser::checkValidVariableType(LocTy Loc, const Twine &Name, Type *Ty,
                                        Value *Val) {
  Type *ValTy = Val->getType();
  if (ValTy == Ty)
    return Val;
  if (Ty->isLabelTy())
    error(Loc, "'" + Name + "' is not a basic block");
  else
    error(Loc, "'" + Name + "' defined with type '" +
                   getTypeString(Val->getType()) + "' but expected '" +
                   getTypeString(Ty) + "'");
  return nullptr;
}

/// getGlobalVal - Get a value with the specified name or ID, creating a
/// forward reference record if needed.  This can return null if the value
/// exists but does not have the right type.
GlobalValue *LLParser::getGlobalVal(const std::string &Name, Type *Ty,
                                    LocTy Loc) {
  PointerType *PTy = dyn_cast<PointerType>(Ty);
  if (!PTy) {
    error(Loc, "global variable reference must have pointer type");
    return nullptr;
  }

  // Look this name up in the normal function symbol table.
  GlobalValue *Val =
    cast_or_null<GlobalValue>(M->getValueSymbolTable().lookup(Name));

  // If this is a forward reference for the value, see if we already created a
  // forward ref record.
  if (!Val) {
    auto I = ForwardRefVals.find(Name);
    if (I != ForwardRefVals.end())
      Val = I->second.first;
  }

  // If we have the value in the symbol table or fwd-ref table, return it.
  if (Val)
    return cast_or_null<GlobalValue>(
        checkValidVariableType(Loc, "@" + Name, Ty, Val));

  // Otherwise, create a new forward reference for this value and remember it.
  GlobalValue *FwdVal = createGlobalFwdRef(M, PTy);
  ForwardRefVals[Name] = std::make_pair(FwdVal, Loc);
  return FwdVal;
}

GlobalValue *LLParser::getGlobalVal(unsigned ID, Type *Ty, LocTy Loc) {
  PointerType *PTy = dyn_cast<PointerType>(Ty);
  if (!PTy) {
    error(Loc, "global variable reference must have pointer type");
    return nullptr;
  }

  GlobalValue *Val = NumberedVals.get(ID);

  // If this is a forward reference for the value, see if we already created a
  // forward ref record.
  if (!Val) {
    auto I = ForwardRefValIDs.find(ID);
    if (I != ForwardRefValIDs.end())
      Val = I->second.first;
  }

  // If we have the value in the symbol table or fwd-ref table, return it.
  if (Val)
    return cast_or_null<GlobalValue>(
        checkValidVariableType(Loc, "@" + Twine(ID), Ty, Val));

  // Otherwise, create a new forward reference for this value and remember it.
  GlobalValue *FwdVal = createGlobalFwdRef(M, PTy);
  ForwardRefValIDs[ID] = std::make_pair(FwdVal, Loc);
  return FwdVal;
}

//===----------------------------------------------------------------------===//
// Comdat Reference/Resolution Routines.
//===----------------------------------------------------------------------===//

Comdat *LLParser::getComdat(const std::string &Name, LocTy Loc) {
  // Look this name up in the comdat symbol table.
  Module::ComdatSymTabType &ComdatSymTab = M->getComdatSymbolTable();
  Module::ComdatSymTabType::iterator I = ComdatSymTab.find(Name);
  if (I != ComdatSymTab.end())
    return &I->second;

  // Otherwise, create a new forward reference for this value and remember it.
  Comdat *C = M->getOrInsertComdat(Name);
  ForwardRefComdats[Name] = Loc;
  return C;
}

//===----------------------------------------------------------------------===//
// Helper Routines.
//===----------------------------------------------------------------------===//

/// parseToken - If the current token has the specified kind, eat it and return
/// success.  Otherwise, emit the specified error and return failure.
bool LLParser::parseToken(lltok::Kind T, const char *ErrMsg) {
  if (Lex.getKind() != T)
    return tokError(ErrMsg);
  Lex.Lex();
  return false;
}

/// parseStringConstant
///   ::= StringConstant
bool LLParser::parseStringConstant(std::string &Result) {
  if (Lex.getKind() != lltok::StringConstant)
    return tokError("expected string constant");
  Result = Lex.getStrVal();
  Lex.Lex();
  return false;
}

/// parseUInt32
///   ::= uint32
bool LLParser::parseUInt32(uint32_t &Val) {
  if (Lex.getKind() != lltok::APSInt || Lex.getAPSIntVal().isSigned())
    return tokError("expected integer");
  uint64_t Val64 = Lex.getAPSIntVal().getLimitedValue(0xFFFFFFFFULL+1);
  if (Val64 != unsigned(Val64))
    return tokError("expected 32-bit integer (too large)");
  Val = Val64;
  Lex.Lex();
  return false;
}

/// parseUInt64
///   ::= uint64
bool LLParser::parseUInt64(uint64_t &Val) {
  if (Lex.getKind() != lltok::APSInt || Lex.getAPSIntVal().isSigned())
    return tokError("expected integer");
  Val = Lex.getAPSIntVal().getLimitedValue();
  Lex.Lex();
  return false;
}

/// parseTLSModel
///   := 'localdynamic'
///   := 'initialexec'
///   := 'localexec'
bool LLParser::parseTLSModel(GlobalVariable::ThreadLocalMode &TLM) {
  switch (Lex.getKind()) {
    default:
      return tokError("expected localdynamic, initialexec or localexec");
    case lltok::kw_localdynamic:
      TLM = GlobalVariable::LocalDynamicTLSModel;
      break;
    case lltok::kw_initialexec:
      TLM = GlobalVariable::InitialExecTLSModel;
      break;
    case lltok::kw_localexec:
      TLM = GlobalVariable::LocalExecTLSModel;
      break;
  }

  Lex.Lex();
  return false;
}

/// parseOptionalThreadLocal
///   := /*empty*/
///   := 'thread_local'
///   := 'thread_local' '(' tlsmodel ')'
bool LLParser::parseOptionalThreadLocal(GlobalVariable::ThreadLocalMode &TLM) {
  TLM = GlobalVariable::NotThreadLocal;
  if (!EatIfPresent(lltok::kw_thread_local))
    return false;

  TLM = GlobalVariable::GeneralDynamicTLSModel;
  if (Lex.getKind() == lltok::lparen) {
    Lex.Lex();
    return parseTLSModel(TLM) ||
           parseToken(lltok::rparen, "expected ')' after thread local model");
  }
  return false;
}

/// parseOptionalAddrSpace
///   := /*empty*/
///   := 'addrspace' '(' uint32 ')'
bool LLParser::parseOptionalAddrSpace(unsigned &AddrSpace, unsigned DefaultAS) {
  AddrSpace = DefaultAS;
  if (!EatIfPresent(lltok::kw_addrspace))
    return false;

  auto ParseAddrspaceValue = [&](unsigned &AddrSpace) -> bool {
    if (Lex.getKind() == lltok::StringConstant) {
      auto AddrSpaceStr = Lex.getStrVal();
      if (AddrSpaceStr == "A") {
        AddrSpace = M->getDataLayout().getAllocaAddrSpace();
      } else if (AddrSpaceStr == "G") {
        AddrSpace = M->getDataLayout().getDefaultGlobalsAddressSpace();
      } else if (AddrSpaceStr == "P") {
        AddrSpace = M->getDataLayout().getProgramAddressSpace();
      } else {
        return tokError("invalid symbolic addrspace '" + AddrSpaceStr + "'");
      }
      Lex.Lex();
      return false;
    }
    if (Lex.getKind() != lltok::APSInt)
      return tokError("expected integer or string constant");
    SMLoc Loc = Lex.getLoc();
    if (parseUInt32(AddrSpace))
      return true;
    if (!isUInt<24>(AddrSpace))
      return error(Loc, "invalid address space, must be a 24-bit integer");
    return false;
  };

  return parseToken(lltok::lparen, "expected '(' in address space") ||
         ParseAddrspaceValue(AddrSpace) ||
         parseToken(lltok::rparen, "expected ')' in address space");
}

/// parseStringAttribute
///   := StringConstant
///   := StringConstant '=' StringConstant
bool LLParser::parseStringAttribute(AttrBuilder &B) {
  std::string Attr = Lex.getStrVal();
  Lex.Lex();
  std::string Val;
  if (EatIfPresent(lltok::equal) && parseStringConstant(Val))
    return true;
  B.addAttribute(Attr, Val);
  return false;
}

/// Parse a potentially empty list of parameter or return attributes.
bool LLParser::parseOptionalParamOrReturnAttrs(AttrBuilder &B, bool IsParam) {
  bool HaveError = false;

  B.clear();

  while (true) {
    lltok::Kind Token = Lex.getKind();
    if (Token == lltok::StringConstant) {
      if (parseStringAttribute(B))
        return true;
      continue;
    }

    SMLoc Loc = Lex.getLoc();
    Attribute::AttrKind Attr = tokenToAttribute(Token);
    if (Attr == Attribute::None)
      return HaveError;

    if (parseEnumAttribute(Attr, B, /* InAttrGroup */ false))
      return true;

    if (IsParam && !Attribute::canUseAsParamAttr(Attr))
      HaveError |= error(Loc, "this attribute does not apply to parameters");
    if (!IsParam && !Attribute::canUseAsRetAttr(Attr))
      HaveError |= error(Loc, "this attribute does not apply to return values");
  }
}

static unsigned parseOptionalLinkageAux(lltok::Kind Kind, bool &HasLinkage) {
  HasLinkage = true;
  switch (Kind) {
  default:
    HasLinkage = false;
    return GlobalValue::ExternalLinkage;
  case lltok::kw_private:
    return GlobalValue::PrivateLinkage;
  case lltok::kw_internal:
    return GlobalValue::InternalLinkage;
  case lltok::kw_weak:
    return GlobalValue::WeakAnyLinkage;
  case lltok::kw_weak_odr:
    return GlobalValue::WeakODRLinkage;
  case lltok::kw_linkonce:
    return GlobalValue::LinkOnceAnyLinkage;
  case lltok::kw_linkonce_odr:
    return GlobalValue::LinkOnceODRLinkage;
  case lltok::kw_available_externally:
    return GlobalValue::AvailableExternallyLinkage;
  case lltok::kw_appending:
    return GlobalValue::AppendingLinkage;
  case lltok::kw_common:
    return GlobalValue::CommonLinkage;
  case lltok::kw_extern_weak:
    return GlobalValue::ExternalWeakLinkage;
  case lltok::kw_external:
    return GlobalValue::ExternalLinkage;
  }
}

/// parseOptionalLinkage
///   ::= /*empty*/
///   ::= 'private'
///   ::= 'internal'
///   ::= 'weak'
///   ::= 'weak_odr'
///   ::= 'linkonce'
///   ::= 'linkonce_odr'
///   ::= 'available_externally'
///   ::= 'appending'
///   ::= 'common'
///   ::= 'extern_weak'
///   ::= 'external'
bool LLParser::parseOptionalLinkage(unsigned &Res, bool &HasLinkage,
                                    unsigned &Visibility,
                                    unsigned &DLLStorageClass, bool &DSOLocal) {
  Res = parseOptionalLinkageAux(Lex.getKind(), HasLinkage);
  if (HasLinkage)
    Lex.Lex();
  parseOptionalDSOLocal(DSOLocal);
  parseOptionalVisibility(Visibility);
  parseOptionalDLLStorageClass(DLLStorageClass);

  if (DSOLocal && DLLStorageClass == GlobalValue::DLLImportStorageClass) {
    return error(Lex.getLoc(), "dso_location and DLL-StorageClass mismatch");
  }

  return false;
}

void LLParser::parseOptionalDSOLocal(bool &DSOLocal) {
  switch (Lex.getKind()) {
  default:
    DSOLocal = false;
    break;
  case lltok::kw_dso_local:
    DSOLocal = true;
    Lex.Lex();
    break;
  case lltok::kw_dso_preemptable:
    DSOLocal = false;
    Lex.Lex();
    break;
  }
}

/// parseOptionalVisibility
///   ::= /*empty*/
///   ::= 'default'
///   ::= 'hidden'
///   ::= 'protected'
///
void LLParser::parseOptionalVisibility(unsigned &Res) {
  switch (Lex.getKind()) {
  default:
    Res = GlobalValue::DefaultVisibility;
    return;
  case lltok::kw_default:
    Res = GlobalValue::DefaultVisibility;
    break;
  case lltok::kw_hidden:
    Res = GlobalValue::HiddenVisibility;
    break;
  case lltok::kw_protected:
    Res = GlobalValue::ProtectedVisibility;
    break;
  }
  Lex.Lex();
}

bool LLParser::parseOptionalImportType(lltok::Kind Kind,
                                       GlobalValueSummary::ImportKind &Res) {
  switch (Kind) {
  default:
    return tokError("unknown import kind. Expect definition or declaration.");
  case lltok::kw_definition:
    Res = GlobalValueSummary::Definition;
    return false;
  case lltok::kw_declaration:
    Res = GlobalValueSummary::Declaration;
    return false;
  }
}

/// parseOptionalDLLStorageClass
///   ::= /*empty*/
///   ::= 'dllimport'
///   ::= 'dllexport'
///
void LLParser::parseOptionalDLLStorageClass(unsigned &Res) {
  switch (Lex.getKind()) {
  default:
    Res = GlobalValue::DefaultStorageClass;
    return;
  case lltok::kw_dllimport:
    Res = GlobalValue::DLLImportStorageClass;
    break;
  case lltok::kw_dllexport:
    Res = GlobalValue::DLLExportStorageClass;
    break;
  }
  Lex.Lex();
}

/// parseOptionalCallingConv
///   ::= /*empty*/
///   ::= 'ccc'
///   ::= 'fastcc'
///   ::= 'intel_ocl_bicc'
///   ::= 'coldcc'
///   ::= 'cfguard_checkcc'
///   ::= 'x86_stdcallcc'
///   ::= 'x86_fastcallcc'
///   ::= 'x86_thiscallcc'
///   ::= 'x86_vectorcallcc'
///   ::= 'arm_apcscc'
///   ::= 'arm_aapcscc'
///   ::= 'arm_aapcs_vfpcc'
///   ::= 'aarch64_vector_pcs'
///   ::= 'aarch64_sve_vector_pcs'
///   ::= 'aarch64_sme_preservemost_from_x0'
///   ::= 'aarch64_sme_preservemost_from_x1'
///   ::= 'aarch64_sme_preservemost_from_x2'
///   ::= 'msp430_intrcc'
///   ::= 'avr_intrcc'
///   ::= 'avr_signalcc'
///   ::= 'ptx_kernel'
///   ::= 'ptx_device'
///   ::= 'spir_func'
///   ::= 'spir_kernel'
///   ::= 'x86_64_sysvcc'
///   ::= 'win64cc'
///   ::= 'anyregcc'
///   ::= 'preserve_mostcc'
///   ::= 'preserve_allcc'
///   ::= 'preserve_nonecc'
///   ::= 'ghccc'
///   ::= 'swiftcc'
///   ::= 'swifttailcc'
///   ::= 'x86_intrcc'
///   ::= 'hhvmcc'
///   ::= 'hhvm_ccc'
///   ::= 'cxx_fast_tlscc'
///   ::= 'amdgpu_vs'
///   ::= 'amdgpu_ls'
///   ::= 'amdgpu_hs'
///   ::= 'amdgpu_es'
///   ::= 'amdgpu_gs'
///   ::= 'amdgpu_ps'
///   ::= 'amdgpu_cs'
///   ::= 'amdgpu_cs_chain'
///   ::= 'amdgpu_cs_chain_preserve'
///   ::= 'amdgpu_kernel'
///   ::= 'tailcc'
///   ::= 'm68k_rtdcc'
///   ::= 'graalcc'
///   ::= 'riscv_vector_cc'
///   ::= 'cc' UINT
///
bool LLParser::parseOptionalCallingConv(unsigned &CC) {
  switch (Lex.getKind()) {
  default:                       CC = CallingConv::C; return false;
  case lltok::kw_ccc:            CC = CallingConv::C; break;
  case lltok::kw_fastcc:         CC = CallingConv::Fast; break;
  case lltok::kw_coldcc:         CC = CallingConv::Cold; break;
  case lltok::kw_cfguard_checkcc: CC = CallingConv::CFGuard_Check; break;
  case lltok::kw_x86_stdcallcc:  CC = CallingConv::X86_StdCall; break;
  case lltok::kw_x86_fastcallcc: CC = CallingConv::X86_FastCall; break;
  case lltok::kw_x86_regcallcc:  CC = CallingConv::X86_RegCall; break;
  case lltok::kw_x86_thiscallcc: CC = CallingConv::X86_ThisCall; break;
  case lltok::kw_x86_vectorcallcc:CC = CallingConv::X86_VectorCall; break;
  case lltok::kw_arm_apcscc:     CC = CallingConv::ARM_APCS; break;
  case lltok::kw_arm_aapcscc:    CC = CallingConv::ARM_AAPCS; break;
  case lltok::kw_arm_aapcs_vfpcc:CC = CallingConv::ARM_AAPCS_VFP; break;
  case lltok::kw_aarch64_vector_pcs:CC = CallingConv::AArch64_VectorCall; break;
  case lltok::kw_aarch64_sve_vector_pcs:
    CC = CallingConv::AArch64_SVE_VectorCall;
    break;
  case lltok::kw_aarch64_sme_preservemost_from_x0:
    CC = CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0;
    break;
  case lltok::kw_aarch64_sme_preservemost_from_x1:
    CC = CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X1;
    break;
  case lltok::kw_aarch64_sme_preservemost_from_x2:
    CC = CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2;
    break;
  case lltok::kw_msp430_intrcc:  CC = CallingConv::MSP430_INTR; break;
  case lltok::kw_avr_intrcc:     CC = CallingConv::AVR_INTR; break;
  case lltok::kw_avr_signalcc:   CC = CallingConv::AVR_SIGNAL; break;
  case lltok::kw_ptx_kernel:     CC = CallingConv::PTX_Kernel; break;
  case lltok::kw_ptx_device:     CC = CallingConv::PTX_Device; break;
  case lltok::kw_spir_kernel:    CC = CallingConv::SPIR_KERNEL; break;
  case lltok::kw_spir_func:      CC = CallingConv::SPIR_FUNC; break;
  case lltok::kw_intel_ocl_bicc: CC = CallingConv::Intel_OCL_BI; break;
  case lltok::kw_x86_64_sysvcc:  CC = CallingConv::X86_64_SysV; break;
  case lltok::kw_win64cc:        CC = CallingConv::Win64; break;
  case lltok::kw_anyregcc:       CC = CallingConv::AnyReg; break;
  case lltok::kw_preserve_mostcc:CC = CallingConv::PreserveMost; break;
  case lltok::kw_preserve_allcc: CC = CallingConv::PreserveAll; break;
  case lltok::kw_preserve_nonecc:CC = CallingConv::PreserveNone; break;
  case lltok::kw_ghccc:          CC = CallingConv::GHC; break;
  case lltok::kw_swiftcc:        CC = CallingConv::Swift; break;
  case lltok::kw_swifttailcc:    CC = CallingConv::SwiftTail; break;
  case lltok::kw_x86_intrcc:     CC = CallingConv::X86_INTR; break;
  case lltok::kw_hhvmcc:
    CC = CallingConv::DUMMY_HHVM;
    break;
  case lltok::kw_hhvm_ccc:
    CC = CallingConv::DUMMY_HHVM_C;
    break;
  case lltok::kw_cxx_fast_tlscc: CC = CallingConv::CXX_FAST_TLS; break;
  case lltok::kw_amdgpu_vs:      CC = CallingConv::AMDGPU_VS; break;
  case lltok::kw_amdgpu_gfx:     CC = CallingConv::AMDGPU_Gfx; break;
  case lltok::kw_amdgpu_ls:      CC = CallingConv::AMDGPU_LS; break;
  case lltok::kw_amdgpu_hs:      CC = CallingConv::AMDGPU_HS; break;
  case lltok::kw_amdgpu_es:      CC = CallingConv::AMDGPU_ES; break;
  case lltok::kw_amdgpu_gs:      CC = CallingConv::AMDGPU_GS; break;
  case lltok::kw_amdgpu_ps:      CC = CallingConv::AMDGPU_PS; break;
  case lltok::kw_amdgpu_cs:      CC = CallingConv::AMDGPU_CS; break;
  case lltok::kw_amdgpu_cs_chain:
    CC = CallingConv::AMDGPU_CS_Chain;
    break;
  case lltok::kw_amdgpu_cs_chain_preserve:
    CC = CallingConv::AMDGPU_CS_ChainPreserve;
    break;
  case lltok::kw_amdgpu_kernel:  CC = CallingConv::AMDGPU_KERNEL; break;
  case lltok::kw_tailcc:         CC = CallingConv::Tail; break;
  case lltok::kw_m68k_rtdcc:     CC = CallingConv::M68k_RTD; break;
  case lltok::kw_graalcc:        CC = CallingConv::GRAAL; break;
  case lltok::kw_riscv_vector_cc:
    CC = CallingConv::RISCV_VectorCall;
    break;
  case lltok::kw_cc: {
      Lex.Lex();
      return parseUInt32(CC);
    }
  }

  Lex.Lex();
  return false;
}

/// parseMetadataAttachment
///   ::= !dbg !42
bool LLParser::parseMetadataAttachment(unsigned &Kind, MDNode *&MD) {
  assert(Lex.getKind() == lltok::MetadataVar && "Expected metadata attachment");

  std::string Name = Lex.getStrVal();
  Kind = M->getMDKindID(Name);
  Lex.Lex();

  return parseMDNode(MD);
}

/// parseInstructionMetadata
///   ::= !dbg !42 (',' !dbg !57)*
bool LLParser::parseInstructionMetadata(Instruction &Inst) {
  do {
    if (Lex.getKind() != lltok::MetadataVar)
      return tokError("expected metadata after comma");

    unsigned MDK;
    MDNode *N;
    if (parseMetadataAttachment(MDK, N))
      return true;

    if (MDK == LLVMContext::MD_DIAssignID)
      TempDIAssignIDAttachments[N].push_back(&Inst);
    else
      Inst.setMetadata(MDK, N);

    if (MDK == LLVMContext::MD_tbaa)
      InstsWithTBAATag.push_back(&Inst);

    // If this is the end of the list, we're done.
  } while (EatIfPresent(lltok::comma));
  return false;
}

/// parseGlobalObjectMetadataAttachment
///   ::= !dbg !57
bool LLParser::parseGlobalObjectMetadataAttachment(GlobalObject &GO) {
  unsigned MDK;
  MDNode *N;
  if (parseMetadataAttachment(MDK, N))
    return true;

  GO.addMetadata(MDK, *N);
  return false;
}

/// parseOptionalFunctionMetadata
///   ::= (!dbg !57)*
bool LLParser::parseOptionalFunctionMetadata(Function &F) {
  while (Lex.getKind() == lltok::MetadataVar)
    if (parseGlobalObjectMetadataAttachment(F))
      return true;
  return false;
}

/// parseOptionalAlignment
///   ::= /* empty */
///   ::= 'align' 4
bool LLParser::parseOptionalAlignment(MaybeAlign &Alignment, bool AllowParens) {
  Alignment = std::nullopt;
  if (!EatIfPresent(lltok::kw_align))
    return false;
  LocTy AlignLoc = Lex.getLoc();
  uint64_t Value = 0;

  LocTy ParenLoc = Lex.getLoc();
  bool HaveParens = false;
  if (AllowParens) {
    if (EatIfPresent(lltok::lparen))
      HaveParens = true;
  }

  if (parseUInt64(Value))
    return true;

  if (HaveParens && !EatIfPresent(lltok::rparen))
    return error(ParenLoc, "expected ')'");

  if (!isPowerOf2_64(Value))
    return error(AlignLoc, "alignment is not a power of two");
  if (Value > Value::MaximumAlignment)
    return error(AlignLoc, "huge alignments are not supported yet");
  Alignment = Align(Value);
  return false;
}

/// parseOptionalCodeModel
///   ::= /* empty */
///   ::= 'code_model' "large"
bool LLParser::parseOptionalCodeModel(CodeModel::Model &model) {
  Lex.Lex();
  auto StrVal = Lex.getStrVal();
  auto ErrMsg = "expected global code model string";
  if (StrVal == "tiny")
    model = CodeModel::Tiny;
  else if (StrVal == "small")
    model = CodeModel::Small;
  else if (StrVal == "kernel")
    model = CodeModel::Kernel;
  else if (StrVal == "medium")
    model = CodeModel::Medium;
  else if (StrVal == "large")
    model = CodeModel::Large;
  else
    return tokError(ErrMsg);
  if (parseToken(lltok::StringConstant, ErrMsg))
    return true;
  return false;
}

/// parseOptionalDerefAttrBytes
///   ::= /* empty */
///   ::= AttrKind '(' 4 ')'
///
/// where AttrKind is either 'dereferenceable' or 'dereferenceable_or_null'.
bool LLParser::parseOptionalDerefAttrBytes(lltok::Kind AttrKind,
                                           uint64_t &Bytes) {
  assert((AttrKind == lltok::kw_dereferenceable ||
          AttrKind == lltok::kw_dereferenceable_or_null) &&
         "contract!");

  Bytes = 0;
  if (!EatIfPresent(AttrKind))
    return false;
  LocTy ParenLoc = Lex.getLoc();
  if (!EatIfPresent(lltok::lparen))
    return error(ParenLoc, "expected '('");
  LocTy DerefLoc = Lex.getLoc();
  if (parseUInt64(Bytes))
    return true;
  ParenLoc = Lex.getLoc();
  if (!EatIfPresent(lltok::rparen))
    return error(ParenLoc, "expected ')'");
  if (!Bytes)
    return error(DerefLoc, "dereferenceable bytes must be non-zero");
  return false;
}

bool LLParser::parseOptionalUWTableKind(UWTableKind &Kind) {
  Lex.Lex();
  Kind = UWTableKind::Default;
  if (!EatIfPresent(lltok::lparen))
    return false;
  LocTy KindLoc = Lex.getLoc();
  if (Lex.getKind() == lltok::kw_sync)
    Kind = UWTableKind::Sync;
  else if (Lex.getKind() == lltok::kw_async)
    Kind = UWTableKind::Async;
  else
    return error(KindLoc, "expected unwind table kind");
  Lex.Lex();
  return parseToken(lltok::rparen, "expected ')'");
}

bool LLParser::parseAllocKind(AllocFnKind &Kind) {
  Lex.Lex();
  LocTy ParenLoc = Lex.getLoc();
  if (!EatIfPresent(lltok::lparen))
    return error(ParenLoc, "expected '('");
  LocTy KindLoc = Lex.getLoc();
  std::string Arg;
  if (parseStringConstant(Arg))
    return error(KindLoc, "expected allockind value");
  for (StringRef A : llvm::split(Arg, ",")) {
    if (A == "alloc") {
      Kind |= AllocFnKind::Alloc;
    } else if (A == "realloc") {
      Kind |= AllocFnKind::Realloc;
    } else if (A == "free") {
      Kind |= AllocFnKind::Free;
    } else if (A == "uninitialized") {
      Kind |= AllocFnKind::Uninitialized;
    } else if (A == "zeroed") {
      Kind |= AllocFnKind::Zeroed;
    } else if (A == "aligned") {
      Kind |= AllocFnKind::Aligned;
    } else {
      return error(KindLoc, Twine("unknown allockind ") + A);
    }
  }
  ParenLoc = Lex.getLoc();
  if (!EatIfPresent(lltok::rparen))
    return error(ParenLoc, "expected ')'");
  if (Kind == AllocFnKind::Unknown)
    return error(KindLoc, "expected allockind value");
  return false;
}

static std::optional<MemoryEffects::Location> keywordToLoc(lltok::Kind Tok) {
  switch (Tok) {
  case lltok::kw_argmem:
    return IRMemLocation::ArgMem;
  case lltok::kw_inaccessiblemem:
    return IRMemLocation::InaccessibleMem;
  default:
    return std::nullopt;
  }
}

static std::optional<ModRefInfo> keywordToModRef(lltok::Kind Tok) {
  switch (Tok) {
  case lltok::kw_none:
    return ModRefInfo::NoModRef;
  case lltok::kw_read:
    return ModRefInfo::Ref;
  case lltok::kw_write:
    return ModRefInfo::Mod;
  case lltok::kw_readwrite:
    return ModRefInfo::ModRef;
  default:
    return std::nullopt;
  }
}

std::optional<MemoryEffects> LLParser::parseMemoryAttr() {
  MemoryEffects ME = MemoryEffects::none();

  // We use syntax like memory(argmem: read), so the colon should not be
  // interpreted as a label terminator.
  Lex.setIgnoreColonInIdentifiers(true);
  auto _ = make_scope_exit([&] { Lex.setIgnoreColonInIdentifiers(false); });

  Lex.Lex();
  if (!EatIfPresent(lltok::lparen)) {
    tokError("expected '('");
    return std::nullopt;
  }

  bool SeenLoc = false;
  do {
    std::optional<IRMemLocation> Loc = keywordToLoc(Lex.getKind());
    if (Loc) {
      Lex.Lex();
      if (!EatIfPresent(lltok::colon)) {
        tokError("expected ':' after location");
        return std::nullopt;
      }
    }

    std::optional<ModRefInfo> MR = keywordToModRef(Lex.getKind());
    if (!MR) {
      if (!Loc)
        tokError("expected memory location (argmem, inaccessiblemem) "
                 "or access kind (none, read, write, readwrite)");
      else
        tokError("expected access kind (none, read, write, readwrite)");
      return std::nullopt;
    }

    Lex.Lex();
    if (Loc) {
      SeenLoc = true;
      ME = ME.getWithModRef(*Loc, *MR);
    } else {
      if (SeenLoc) {
        tokError("default access kind must be specified first");
        return std::nullopt;
      }
      ME = MemoryEffects(*MR);
    }

    if (EatIfPresent(lltok::rparen))
      return ME;
  } while (EatIfPresent(lltok::comma));

  tokError("unterminated memory attribute");
  return std::nullopt;
}

static unsigned keywordToFPClassTest(lltok::Kind Tok) {
  switch (Tok) {
  case lltok::kw_all:
    return fcAllFlags;
  case lltok::kw_nan:
    return fcNan;
  case lltok::kw_snan:
    return fcSNan;
  case lltok::kw_qnan:
    return fcQNan;
  case lltok::kw_inf:
    return fcInf;
  case lltok::kw_ninf:
    return fcNegInf;
  case lltok::kw_pinf:
    return fcPosInf;
  case lltok::kw_norm:
    return fcNormal;
  case lltok::kw_nnorm:
    return fcNegNormal;
  case lltok::kw_pnorm:
    return fcPosNormal;
  case lltok::kw_sub:
    return fcSubnormal;
  case lltok::kw_nsub:
    return fcNegSubnormal;
  case lltok::kw_psub:
    return fcPosSubnormal;
  case lltok::kw_zero:
    return fcZero;
  case lltok::kw_nzero:
    return fcNegZero;
  case lltok::kw_pzero:
    return fcPosZero;
  default:
    return 0;
  }
}

unsigned LLParser::parseNoFPClassAttr() {
  unsigned Mask = fcNone;

  Lex.Lex();
  if (!EatIfPresent(lltok::lparen)) {
    tokError("expected '('");
    return 0;
  }

  do {
    uint64_t Value = 0;
    unsigned TestMask = keywordToFPClassTest(Lex.getKind());
    if (TestMask != 0) {
      Mask |= TestMask;
      // TODO: Disallow overlapping masks to avoid copy paste errors
    } else if (Mask == 0 && Lex.getKind() == lltok::APSInt &&
               !parseUInt64(Value)) {
      if (Value == 0 || (Value & ~static_cast<unsigned>(fcAllFlags)) != 0) {
        error(Lex.getLoc(), "invalid mask value for 'nofpclass'");
        return 0;
      }

      if (!EatIfPresent(lltok::rparen)) {
        error(Lex.getLoc(), "expected ')'");
        return 0;
      }

      return Value;
    } else {
      error(Lex.getLoc(), "expected nofpclass test mask");
      return 0;
    }

    Lex.Lex();
    if (EatIfPresent(lltok::rparen))
      return Mask;
  } while (1);

  llvm_unreachable("unterminated nofpclass attribute");
}

/// parseOptionalCommaAlign
///   ::=
///   ::= ',' align 4
///
/// This returns with AteExtraComma set to true if it ate an excess comma at the
/// end.
bool LLParser::parseOptionalCommaAlign(MaybeAlign &Alignment,
                                       bool &AteExtraComma) {
  AteExtraComma = false;
  while (EatIfPresent(lltok::comma)) {
    // Metadata at the end is an early exit.
    if (Lex.getKind() == lltok::MetadataVar) {
      AteExtraComma = true;
      return false;
    }

    if (Lex.getKind() != lltok::kw_align)
      return error(Lex.getLoc(), "expected metadata or 'align'");

    if (parseOptionalAlignment(Alignment))
      return true;
  }

  return false;
}

/// parseOptionalCommaAddrSpace
///   ::=
///   ::= ',' addrspace(1)
///
/// This returns with AteExtraComma set to true if it ate an excess comma at the
/// end.
bool LLParser::parseOptionalCommaAddrSpace(unsigned &AddrSpace, LocTy &Loc,
                                           bool &AteExtraComma) {
  AteExtraComma = false;
  while (EatIfPresent(lltok::comma)) {
    // Metadata at the end is an early exit.
    if (Lex.getKind() == lltok::MetadataVar) {
      AteExtraComma = true;
      return false;
    }

    Loc = Lex.getLoc();
    if (Lex.getKind() != lltok::kw_addrspace)
      return error(Lex.getLoc(), "expected metadata or 'addrspace'");

    if (parseOptionalAddrSpace(AddrSpace))
      return true;
  }

  return false;
}

bool LLParser::parseAllocSizeArguments(unsigned &BaseSizeArg,
                                       std::optional<unsigned> &HowManyArg) {
  Lex.Lex();

  auto StartParen = Lex.getLoc();
  if (!EatIfPresent(lltok::lparen))
    return error(StartParen, "expected '('");

  if (parseUInt32(BaseSizeArg))
    return true;

  if (EatIfPresent(lltok::comma)) {
    auto HowManyAt = Lex.getLoc();
    unsigned HowMany;
    if (parseUInt32(HowMany))
      return true;
    if (HowMany == BaseSizeArg)
      return error(HowManyAt,
                   "'allocsize' indices can't refer to the same parameter");
    HowManyArg = HowMany;
  } else
    HowManyArg = std::nullopt;

  auto EndParen = Lex.getLoc();
  if (!EatIfPresent(lltok::rparen))
    return error(EndParen, "expected ')'");
  return false;
}

bool LLParser::parseVScaleRangeArguments(unsigned &MinValue,
                                         unsigned &MaxValue) {
  Lex.Lex();

  auto StartParen = Lex.getLoc();
  if (!EatIfPresent(lltok::lparen))
    return error(StartParen, "expected '('");

  if (parseUInt32(MinValue))
    return true;

  if (EatIfPresent(lltok::comma)) {
    if (parseUInt32(MaxValue))
      return true;
  } else
    MaxValue = MinValue;

  auto EndParen = Lex.getLoc();
  if (!EatIfPresent(lltok::rparen))
    return error(EndParen, "expected ')'");
  return false;
}

/// parseScopeAndOrdering
///   if isAtomic: ::= SyncScope? AtomicOrdering
///   else: ::=
///
/// This sets Scope and Ordering to the parsed values.
bool LLParser::parseScopeAndOrdering(bool IsAtomic, SyncScope::ID &SSID,
                                     AtomicOrdering &Ordering) {
  if (!IsAtomic)
    return false;

  return parseScope(SSID) || parseOrdering(Ordering);
}

/// parseScope
///   ::= syncscope("singlethread" | "<target scope>")?
///
/// This sets synchronization scope ID to the ID of the parsed value.
bool LLParser::parseScope(SyncScope::ID &SSID) {
  SSID = SyncScope::System;
  if (EatIfPresent(lltok::kw_syncscope)) {
    auto StartParenAt = Lex.getLoc();
    if (!EatIfPresent(lltok::lparen))
      return error(StartParenAt, "Expected '(' in syncscope");

    std::string SSN;
    auto SSNAt = Lex.getLoc();
    if (parseStringConstant(SSN))
      return error(SSNAt, "Expected synchronization scope name");

    auto EndParenAt = Lex.getLoc();
    if (!EatIfPresent(lltok::rparen))
      return error(EndParenAt, "Expected ')' in syncscope");

    SSID = Context.getOrInsertSyncScopeID(SSN);
  }

  return false;
}

/// parseOrdering
///   ::= AtomicOrdering
///
/// This sets Ordering to the parsed value.
bool LLParser::parseOrdering(AtomicOrdering &Ordering) {
  switch (Lex.getKind()) {
  default:
    return tokError("Expected ordering on atomic instruction");
  case lltok::kw_unordered: Ordering = AtomicOrdering::Unordered; break;
  case lltok::kw_monotonic: Ordering = AtomicOrdering::Monotonic; break;
  // Not specified yet:
  // case lltok::kw_consume: Ordering = AtomicOrdering::Consume; break;
  case lltok::kw_acquire: Ordering = AtomicOrdering::Acquire; break;
  case lltok::kw_release: Ordering = AtomicOrdering::Release; break;
  case lltok::kw_acq_rel: Ordering = AtomicOrdering::AcquireRelease; break;
  case lltok::kw_seq_cst:
    Ordering = AtomicOrdering::SequentiallyConsistent;
    break;
  }
  Lex.Lex();
  return false;
}

/// parseOptionalStackAlignment
///   ::= /* empty */
///   ::= 'alignstack' '(' 4 ')'
bool LLParser::parseOptionalStackAlignment(unsigned &Alignment) {
  Alignment = 0;
  if (!EatIfPresent(lltok::kw_alignstack))
    return false;
  LocTy ParenLoc = Lex.getLoc();
  if (!EatIfPresent(lltok::lparen))
    return error(ParenLoc, "expected '('");
  LocTy AlignLoc = Lex.getLoc();
  if (parseUInt32(Alignment))
    return true;
  ParenLoc = Lex.getLoc();
  if (!EatIfPresent(lltok::rparen))
    return error(ParenLoc, "expected ')'");
  if (!isPowerOf2_32(Alignment))
    return error(AlignLoc, "stack alignment is not a power of two");
  return false;
}

/// parseIndexList - This parses the index list for an insert/extractvalue
/// instruction.  This sets AteExtraComma in the case where we eat an extra
/// comma at the end of the line and find that it is followed by metadata.
/// Clients that don't allow metadata can call the version of this function that
/// only takes one argument.
///
/// parseIndexList
///    ::=  (',' uint32)+
///
bool LLParser::parseIndexList(SmallVectorImpl<unsigned> &Indices,
                              bool &AteExtraComma) {
  AteExtraComma = false;

  if (Lex.getKind() != lltok::comma)
    return tokError("expected ',' as start of index list");

  while (EatIfPresent(lltok::comma)) {
    if (Lex.getKind() == lltok::MetadataVar) {
      if (Indices.empty())
        return tokError("expected index");
      AteExtraComma = true;
      return false;
    }
    unsigned Idx = 0;
    if (parseUInt32(Idx))
      return true;
    Indices.push_back(Idx);
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Type Parsing.
//===----------------------------------------------------------------------===//

/// parseType - parse a type.
bool LLParser::parseType(Type *&Result, const Twine &Msg, bool AllowVoid) {
  SMLoc TypeLoc = Lex.getLoc();
  switch (Lex.getKind()) {
  default:
    return tokError(Msg);
  case lltok::Type:
    // Type ::= 'float' | 'void' (etc)
    Result = Lex.getTyVal();
    Lex.Lex();

    // Handle "ptr" opaque pointer type.
    //
    // Type ::= ptr ('addrspace' '(' uint32 ')')?
    if (Result->isPointerTy()) {
      unsigned AddrSpace;
      if (parseOptionalAddrSpace(AddrSpace))
        return true;
      Result = PointerType::get(getContext(), AddrSpace);

      // Give a nice error for 'ptr*'.
      if (Lex.getKind() == lltok::star)
        return tokError("ptr* is invalid - use ptr instead");

      // Fall through to parsing the type suffixes only if this 'ptr' is a
      // function return. Otherwise, return success, implicitly rejecting other
      // suffixes.
      if (Lex.getKind() != lltok::lparen)
        return false;
    }
    break;
  case lltok::kw_target: {
    // Type ::= TargetExtType
    if (parseTargetExtType(Result))
      return true;
    break;
  }
  case lltok::lbrace:
    // Type ::= StructType
    if (parseAnonStructType(Result, false))
      return true;
    break;
  case lltok::lsquare:
    // Type ::= '[' ... ']'
    Lex.Lex(); // eat the lsquare.
    if (parseArrayVectorType(Result, false))
      return true;
    break;
  case lltok::less: // Either vector or packed struct.
    // Type ::= '<' ... '>'
    Lex.Lex();
    if (Lex.getKind() == lltok::lbrace) {
      if (parseAnonStructType(Result, true) ||
          parseToken(lltok::greater, "expected '>' at end of packed struct"))
        return true;
    } else if (parseArrayVectorType(Result, true))
      return true;
    break;
  case lltok::LocalVar: {
    // Type ::= %foo
    std::pair<Type*, LocTy> &Entry = NamedTypes[Lex.getStrVal()];

    // If the type hasn't been defined yet, create a forward definition and
    // remember where that forward def'n was seen (in case it never is defined).
    if (!Entry.first) {
      Entry.first = StructType::create(Context, Lex.getStrVal());
      Entry.second = Lex.getLoc();
    }
    Result = Entry.first;
    Lex.Lex();
    break;
  }

  case lltok::LocalVarID: {
    // Type ::= %4
    std::pair<Type*, LocTy> &Entry = NumberedTypes[Lex.getUIntVal()];

    // If the type hasn't been defined yet, create a forward definition and
    // remember where that forward def'n was seen (in case it never is defined).
    if (!Entry.first) {
      Entry.first = StructType::create(Context);
      Entry.second = Lex.getLoc();
    }
    Result = Entry.first;
    Lex.Lex();
    break;
  }
  }

  // parse the type suffixes.
  while (true) {
    switch (Lex.getKind()) {
    // End of type.
    default:
      if (!AllowVoid && Result->isVoidTy())
        return error(TypeLoc, "void type only allowed for function results");
      return false;

    // Type ::= Type '*'
    case lltok::star:
      if (Result->isLabelTy())
        return tokError("basic block pointers are invalid");
      if (Result->isVoidTy())
        return tokError("pointers to void are invalid - use i8* instead");
      if (!PointerType::isValidElementType(Result))
        return tokError("pointer to this type is invalid");
      Result = PointerType::getUnqual(Result);
      Lex.Lex();
      break;

    // Type ::= Type 'addrspace' '(' uint32 ')' '*'
    case lltok::kw_addrspace: {
      if (Result->isLabelTy())
        return tokError("basic block pointers are invalid");
      if (Result->isVoidTy())
        return tokError("pointers to void are invalid; use i8* instead");
      if (!PointerType::isValidElementType(Result))
        return tokError("pointer to this type is invalid");
      unsigned AddrSpace;
      if (parseOptionalAddrSpace(AddrSpace) ||
          parseToken(lltok::star, "expected '*' in address space"))
        return true;

      Result = PointerType::get(Result, AddrSpace);
      break;
    }

    /// Types '(' ArgTypeListI ')' OptFuncAttrs
    case lltok::lparen:
      if (parseFunctionType(Result))
        return true;
      break;
    }
  }
}

/// parseParameterList
///    ::= '(' ')'
///    ::= '(' Arg (',' Arg)* ')'
///  Arg
///    ::= Type OptionalAttributes Value OptionalAttributes
bool LLParser::parseParameterList(SmallVectorImpl<ParamInfo> &ArgList,
                                  PerFunctionState &PFS, bool IsMustTailCall,
                                  bool InVarArgsFunc) {
  if (parseToken(lltok::lparen, "expected '(' in call"))
    return true;

  while (Lex.getKind() != lltok::rparen) {
    // If this isn't the first argument, we need a comma.
    if (!ArgList.empty() &&
        parseToken(lltok::comma, "expected ',' in argument list"))
      return true;

    // parse an ellipsis if this is a musttail call in a variadic function.
    if (Lex.getKind() == lltok::dotdotdot) {
      const char *Msg = "unexpected ellipsis in argument list for ";
      if (!IsMustTailCall)
        return tokError(Twine(Msg) + "non-musttail call");
      if (!InVarArgsFunc)
        return tokError(Twine(Msg) + "musttail call in non-varargs function");
      Lex.Lex();  // Lex the '...', it is purely for readability.
      return parseToken(lltok::rparen, "expected ')' at end of argument list");
    }

    // parse the argument.
    LocTy ArgLoc;
    Type *ArgTy = nullptr;
    Value *V;
    if (parseType(ArgTy, ArgLoc))
      return true;

    AttrBuilder ArgAttrs(M->getContext());

    if (ArgTy->isMetadataTy()) {
      if (parseMetadataAsValue(V, PFS))
        return true;
    } else {
      // Otherwise, handle normal operands.
      if (parseOptionalParamAttrs(ArgAttrs) || parseValue(ArgTy, V, PFS))
        return true;
    }
    ArgList.push_back(ParamInfo(
        ArgLoc, V, AttributeSet::get(V->getContext(), ArgAttrs)));
  }

  if (IsMustTailCall && InVarArgsFunc)
    return tokError("expected '...' at end of argument list for musttail call "
                    "in varargs function");

  Lex.Lex();  // Lex the ')'.
  return false;
}

/// parseRequiredTypeAttr
///   ::= attrname(<ty>)
bool LLParser::parseRequiredTypeAttr(AttrBuilder &B, lltok::Kind AttrToken,
                                     Attribute::AttrKind AttrKind) {
  Type *Ty = nullptr;
  if (!EatIfPresent(AttrToken))
    return true;
  if (!EatIfPresent(lltok::lparen))
    return error(Lex.getLoc(), "expected '('");
  if (parseType(Ty))
    return true;
  if (!EatIfPresent(lltok::rparen))
    return error(Lex.getLoc(), "expected ')'");

  B.addTypeAttr(AttrKind, Ty);
  return false;
}

/// parseRangeAttr
///   ::= range(<ty> <n>,<n>)
bool LLParser::parseRangeAttr(AttrBuilder &B) {
  Lex.Lex();

  APInt Lower;
  APInt Upper;
  Type *Ty = nullptr;
  LocTy TyLoc;

  auto ParseAPSInt = [&](unsigned BitWidth, APInt &Val) {
    if (Lex.getKind() != lltok::APSInt)
      return tokError("expected integer");
    if (Lex.getAPSIntVal().getBitWidth() > BitWidth)
      return tokError(
          "integer is too large for the bit width of specified type");
    Val = Lex.getAPSIntVal().extend(BitWidth);
    Lex.Lex();
    return false;
  };

  if (parseToken(lltok::lparen, "expected '('") || parseType(Ty, TyLoc))
    return true;
  if (!Ty->isIntegerTy())
    return error(TyLoc, "the range must have integer type!");

  unsigned BitWidth = Ty->getPrimitiveSizeInBits();

  if (ParseAPSInt(BitWidth, Lower) ||
      parseToken(lltok::comma, "expected ','") || ParseAPSInt(BitWidth, Upper))
    return true;
  if (Lower == Upper)
    return tokError("the range should not represent the full or empty set!");

  if (parseToken(lltok::rparen, "expected ')'"))
    return true;

  B.addRangeAttr(ConstantRange(Lower, Upper));
  return false;
}

/// parseInitializesAttr
///   ::= initializes((Lo1,Hi1),(Lo2,Hi2),...)
bool LLParser::parseInitializesAttr(AttrBuilder &B) {
  Lex.Lex();

  auto ParseAPSInt = [&](APInt &Val) {
    if (Lex.getKind() != lltok::APSInt)
      return tokError("expected integer");
    Val = Lex.getAPSIntVal().extend(64);
    Lex.Lex();
    return false;
  };

  if (parseToken(lltok::lparen, "expected '('"))
    return true;

  SmallVector<ConstantRange, 2> RangeList;
  // Parse each constant range.
  do {
    APInt Lower, Upper;
    if (parseToken(lltok::lparen, "expected '('"))
      return true;

    if (ParseAPSInt(Lower) || parseToken(lltok::comma, "expected ','") ||
        ParseAPSInt(Upper))
      return true;

    if (Lower == Upper)
      return tokError("the range should not represent the full or empty set!");

    if (parseToken(lltok::rparen, "expected ')'"))
      return true;

    RangeList.push_back(ConstantRange(Lower, Upper));
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')'"))
    return true;

  auto CRLOrNull = ConstantRangeList::getConstantRangeList(RangeList);
  if (!CRLOrNull.has_value())
    return tokError("Invalid (unordered or overlapping) range list");
  B.addInitializesAttr(*CRLOrNull);
  return false;
}

/// parseOptionalOperandBundles
///    ::= /*empty*/
///    ::= '[' OperandBundle [, OperandBundle ]* ']'
///
/// OperandBundle
///    ::= bundle-tag '(' ')'
///    ::= bundle-tag '(' Type Value [, Type Value ]* ')'
///
/// bundle-tag ::= String Constant
bool LLParser::parseOptionalOperandBundles(
    SmallVectorImpl<OperandBundleDef> &BundleList, PerFunctionState &PFS) {
  LocTy BeginLoc = Lex.getLoc();
  if (!EatIfPresent(lltok::lsquare))
    return false;

  while (Lex.getKind() != lltok::rsquare) {
    // If this isn't the first operand bundle, we need a comma.
    if (!BundleList.empty() &&
        parseToken(lltok::comma, "expected ',' in input list"))
      return true;

    std::string Tag;
    if (parseStringConstant(Tag))
      return true;

    if (parseToken(lltok::lparen, "expected '(' in operand bundle"))
      return true;

    std::vector<Value *> Inputs;
    while (Lex.getKind() != lltok::rparen) {
      // If this isn't the first input, we need a comma.
      if (!Inputs.empty() &&
          parseToken(lltok::comma, "expected ',' in input list"))
        return true;

      Type *Ty = nullptr;
      Value *Input = nullptr;
      if (parseType(Ty) || parseValue(Ty, Input, PFS))
        return true;
      Inputs.push_back(Input);
    }

    BundleList.emplace_back(std::move(Tag), std::move(Inputs));

    Lex.Lex(); // Lex the ')'.
  }

  if (BundleList.empty())
    return error(BeginLoc, "operand bundle set must not be empty");

  Lex.Lex(); // Lex the ']'.
  return false;
}

bool LLParser::checkValueID(LocTy Loc, StringRef Kind, StringRef Prefix,
                            unsigned NextID, unsigned ID) const {
  if (ID < NextID)
    return error(Loc, Kind + " expected to be numbered '" + Prefix +
                          Twine(NextID) + "' or greater");

  return false;
}

/// parseArgumentList - parse the argument list for a function type or function
/// prototype.
///   ::= '(' ArgTypeListI ')'
/// ArgTypeListI
///   ::= /*empty*/
///   ::= '...'
///   ::= ArgTypeList ',' '...'
///   ::= ArgType (',' ArgType)*
///
bool LLParser::parseArgumentList(SmallVectorImpl<ArgInfo> &ArgList,
                                 SmallVectorImpl<unsigned> &UnnamedArgNums,
                                 bool &IsVarArg) {
  unsigned CurValID = 0;
  IsVarArg = false;
  assert(Lex.getKind() == lltok::lparen);
  Lex.Lex(); // eat the (.

  if (Lex.getKind() != lltok::rparen) {
    do {
      // Handle ... at end of arg list.
      if (EatIfPresent(lltok::dotdotdot)) {
        IsVarArg = true;
        break;
      }

      // Otherwise must be an argument type.
      LocTy TypeLoc = Lex.getLoc();
      Type *ArgTy = nullptr;
      AttrBuilder Attrs(M->getContext());
      if (parseType(ArgTy) || parseOptionalParamAttrs(Attrs))
        return true;

      if (ArgTy->isVoidTy())
        return error(TypeLoc, "argument can not have void type");

      std::string Name;
      if (Lex.getKind() == lltok::LocalVar) {
        Name = Lex.getStrVal();
        Lex.Lex();
      } else {
        unsigned ArgID;
        if (Lex.getKind() == lltok::LocalVarID) {
          ArgID = Lex.getUIntVal();
          if (checkValueID(TypeLoc, "argument", "%", CurValID, ArgID))
            return true;
          Lex.Lex();
        } else {
          ArgID = CurValID;
        }
        UnnamedArgNums.push_back(ArgID);
        CurValID = ArgID + 1;
      }

      if (!ArgTy->isFirstClassType())
        return error(TypeLoc, "invalid type for function argument");

      ArgList.emplace_back(TypeLoc, ArgTy,
                           AttributeSet::get(ArgTy->getContext(), Attrs),
                           std::move(Name));
    } while (EatIfPresent(lltok::comma));
  }

  return parseToken(lltok::rparen, "expected ')' at end of argument list");
}

/// parseFunctionType
///  ::= Type ArgumentList OptionalAttrs
bool LLParser::parseFunctionType(Type *&Result) {
  assert(Lex.getKind() == lltok::lparen);

  if (!FunctionType::isValidReturnType(Result))
    return tokError("invalid function return type");

  SmallVector<ArgInfo, 8> ArgList;
  bool IsVarArg;
  SmallVector<unsigned> UnnamedArgNums;
  if (parseArgumentList(ArgList, UnnamedArgNums, IsVarArg))
    return true;

  // Reject names on the arguments lists.
  for (const ArgInfo &Arg : ArgList) {
    if (!Arg.Name.empty())
      return error(Arg.Loc, "argument name invalid in function type");
    if (Arg.Attrs.hasAttributes())
      return error(Arg.Loc, "argument attributes invalid in function type");
  }

  SmallVector<Type*, 16> ArgListTy;
  for (const ArgInfo &Arg : ArgList)
    ArgListTy.push_back(Arg.Ty);

  Result = FunctionType::get(Result, ArgListTy, IsVarArg);
  return false;
}

/// parseAnonStructType - parse an anonymous struct type, which is inlined into
/// other structs.
bool LLParser::parseAnonStructType(Type *&Result, bool Packed) {
  SmallVector<Type*, 8> Elts;
  if (parseStructBody(Elts))
    return true;

  Result = StructType::get(Context, Elts, Packed);
  return false;
}

/// parseStructDefinition - parse a struct in a 'type' definition.
bool LLParser::parseStructDefinition(SMLoc TypeLoc, StringRef Name,
                                     std::pair<Type *, LocTy> &Entry,
                                     Type *&ResultTy) {
  // If the type was already defined, diagnose the redefinition.
  if (Entry.first && !Entry.second.isValid())
    return error(TypeLoc, "redefinition of type");

  // If we have opaque, just return without filling in the definition for the
  // struct.  This counts as a definition as far as the .ll file goes.
  if (EatIfPresent(lltok::kw_opaque)) {
    // This type is being defined, so clear the location to indicate this.
    Entry.second = SMLoc();

    // If this type number has never been uttered, create it.
    if (!Entry.first)
      Entry.first = StructType::create(Context, Name);
    ResultTy = Entry.first;
    return false;
  }

  // If the type starts with '<', then it is either a packed struct or a vector.
  bool isPacked = EatIfPresent(lltok::less);

  // If we don't have a struct, then we have a random type alias, which we
  // accept for compatibility with old files.  These types are not allowed to be
  // forward referenced and not allowed to be recursive.
  if (Lex.getKind() != lltok::lbrace) {
    if (Entry.first)
      return error(TypeLoc, "forward references to non-struct type");

    ResultTy = nullptr;
    if (isPacked)
      return parseArrayVectorType(ResultTy, true);
    return parseType(ResultTy);
  }

  // This type is being defined, so clear the location to indicate this.
  Entry.second = SMLoc();

  // If this type number has never been uttered, create it.
  if (!Entry.first)
    Entry.first = StructType::create(Context, Name);

  StructType *STy = cast<StructType>(Entry.first);

  SmallVector<Type*, 8> Body;
  if (parseStructBody(Body) ||
      (isPacked && parseToken(lltok::greater, "expected '>' in packed struct")))
    return true;

  STy->setBody(Body, isPacked);
  ResultTy = STy;
  return false;
}

/// parseStructType: Handles packed and unpacked types.  </> parsed elsewhere.
///   StructType
///     ::= '{' '}'
///     ::= '{' Type (',' Type)* '}'
///     ::= '<' '{' '}' '>'
///     ::= '<' '{' Type (',' Type)* '}' '>'
bool LLParser::parseStructBody(SmallVectorImpl<Type *> &Body) {
  assert(Lex.getKind() == lltok::lbrace);
  Lex.Lex(); // Consume the '{'

  // Handle the empty struct.
  if (EatIfPresent(lltok::rbrace))
    return false;

  LocTy EltTyLoc = Lex.getLoc();
  Type *Ty = nullptr;
  if (parseType(Ty))
    return true;
  Body.push_back(Ty);

  if (!StructType::isValidElementType(Ty))
    return error(EltTyLoc, "invalid element type for struct");

  while (EatIfPresent(lltok::comma)) {
    EltTyLoc = Lex.getLoc();
    if (parseType(Ty))
      return true;

    if (!StructType::isValidElementType(Ty))
      return error(EltTyLoc, "invalid element type for struct");

    Body.push_back(Ty);
  }

  return parseToken(lltok::rbrace, "expected '}' at end of struct");
}

/// parseArrayVectorType - parse an array or vector type, assuming the first
/// token has already been consumed.
///   Type
///     ::= '[' APSINTVAL 'x' Types ']'
///     ::= '<' APSINTVAL 'x' Types '>'
///     ::= '<' 'vscale' 'x' APSINTVAL 'x' Types '>'
bool LLParser::parseArrayVectorType(Type *&Result, bool IsVector) {
  bool Scalable = false;

  if (IsVector && Lex.getKind() == lltok::kw_vscale) {
    Lex.Lex(); // consume the 'vscale'
    if (parseToken(lltok::kw_x, "expected 'x' after vscale"))
      return true;

    Scalable = true;
  }

  if (Lex.getKind() != lltok::APSInt || Lex.getAPSIntVal().isSigned() ||
      Lex.getAPSIntVal().getBitWidth() > 64)
    return tokError("expected number in address space");

  LocTy SizeLoc = Lex.getLoc();
  uint64_t Size = Lex.getAPSIntVal().getZExtValue();
  Lex.Lex();

  if (parseToken(lltok::kw_x, "expected 'x' after element count"))
    return true;

  LocTy TypeLoc = Lex.getLoc();
  Type *EltTy = nullptr;
  if (parseType(EltTy))
    return true;

  if (parseToken(IsVector ? lltok::greater : lltok::rsquare,
                 "expected end of sequential type"))
    return true;

  if (IsVector) {
    if (Size == 0)
      return error(SizeLoc, "zero element vector is illegal");
    if ((unsigned)Size != Size)
      return error(SizeLoc, "size too large for vector");
    if (!VectorType::isValidElementType(EltTy))
      return error(TypeLoc, "invalid vector element type");
    Result = VectorType::get(EltTy, unsigned(Size), Scalable);
  } else {
    if (!ArrayType::isValidElementType(EltTy))
      return error(TypeLoc, "invalid array element type");
    Result = ArrayType::get(EltTy, Size);
  }
  return false;
}

/// parseTargetExtType - handle target extension type syntax
///   TargetExtType
///     ::= 'target' '(' STRINGCONSTANT TargetExtTypeParams TargetExtIntParams ')'
///
///   TargetExtTypeParams
///     ::= /*empty*/
///     ::= ',' Type TargetExtTypeParams
///
///   TargetExtIntParams
///     ::= /*empty*/
///     ::= ',' uint32 TargetExtIntParams
bool LLParser::parseTargetExtType(Type *&Result) {
  Lex.Lex(); // Eat the 'target' keyword.

  // Get the mandatory type name.
  std::string TypeName;
  if (parseToken(lltok::lparen, "expected '(' in target extension type") ||
      parseStringConstant(TypeName))
    return true;

  // Parse all of the integer and type parameters at the same time; the use of
  // SeenInt will allow us to catch cases where type parameters follow integer
  // parameters.
  SmallVector<Type *> TypeParams;
  SmallVector<unsigned> IntParams;
  bool SeenInt = false;
  while (Lex.getKind() == lltok::comma) {
    Lex.Lex(); // Eat the comma.

    if (Lex.getKind() == lltok::APSInt) {
      SeenInt = true;
      unsigned IntVal;
      if (parseUInt32(IntVal))
        return true;
      IntParams.push_back(IntVal);
    } else if (SeenInt) {
      // The only other kind of parameter we support is type parameters, which
      // must precede the integer parameters. This is therefore an error.
      return tokError("expected uint32 param");
    } else {
      Type *TypeParam;
      if (parseType(TypeParam, /*AllowVoid=*/true))
        return true;
      TypeParams.push_back(TypeParam);
    }
  }

  if (parseToken(lltok::rparen, "expected ')' in target extension type"))
    return true;

  Result = TargetExtType::get(Context, TypeName, TypeParams, IntParams);
  return false;
}

//===----------------------------------------------------------------------===//
// Function Semantic Analysis.
//===----------------------------------------------------------------------===//

LLParser::PerFunctionState::PerFunctionState(LLParser &p, Function &f,
                                             int functionNumber,
                                             ArrayRef<unsigned> UnnamedArgNums)
  : P(p), F(f), FunctionNumber(functionNumber) {

  // Insert unnamed arguments into the NumberedVals list.
  auto It = UnnamedArgNums.begin();
  for (Argument &A : F.args()) {
    if (!A.hasName()) {
      unsigned ArgNum = *It++;
      NumberedVals.add(ArgNum, &A);
    }
  }
}

LLParser::PerFunctionState::~PerFunctionState() {
  // If there were any forward referenced non-basicblock values, delete them.

  for (const auto &P : ForwardRefVals) {
    if (isa<BasicBlock>(P.second.first))
      continue;
    P.second.first->replaceAllUsesWith(
        PoisonValue::get(P.second.first->getType()));
    P.second.first->deleteValue();
  }

  for (const auto &P : ForwardRefValIDs) {
    if (isa<BasicBlock>(P.second.first))
      continue;
    P.second.first->replaceAllUsesWith(
        PoisonValue::get(P.second.first->getType()));
    P.second.first->deleteValue();
  }
}

bool LLParser::PerFunctionState::finishFunction() {
  if (!ForwardRefVals.empty())
    return P.error(ForwardRefVals.begin()->second.second,
                   "use of undefined value '%" + ForwardRefVals.begin()->first +
                       "'");
  if (!ForwardRefValIDs.empty())
    return P.error(ForwardRefValIDs.begin()->second.second,
                   "use of undefined value '%" +
                       Twine(ForwardRefValIDs.begin()->first) + "'");
  return false;
}

/// getVal - Get a value with the specified name or ID, creating a
/// forward reference record if needed.  This can return null if the value
/// exists but does not have the right type.
Value *LLParser::PerFunctionState::getVal(const std::string &Name, Type *Ty,
                                          LocTy Loc) {
  // Look this name up in the normal function symbol table.
  Value *Val = F.getValueSymbolTable()->lookup(Name);

  // If this is a forward reference for the value, see if we already created a
  // forward ref record.
  if (!Val) {
    auto I = ForwardRefVals.find(Name);
    if (I != ForwardRefVals.end())
      Val = I->second.first;
  }

  // If we have the value in the symbol table or fwd-ref table, return it.
  if (Val)
    return P.checkValidVariableType(Loc, "%" + Name, Ty, Val);

  // Don't make placeholders with invalid type.
  if (!Ty->isFirstClassType()) {
    P.error(Loc, "invalid use of a non-first-class type");
    return nullptr;
  }

  // Otherwise, create a new forward reference for this value and remember it.
  Value *FwdVal;
  if (Ty->isLabelTy()) {
    FwdVal = BasicBlock::Create(F.getContext(), Name, &F);
  } else {
    FwdVal = new Argument(Ty, Name);
  }
  if (FwdVal->getName() != Name) {
    P.error(Loc, "name is too long which can result in name collisions, "
                 "consider making the name shorter or "
                 "increasing -non-global-value-max-name-size");
    return nullptr;
  }

  ForwardRefVals[Name] = std::make_pair(FwdVal, Loc);
  return FwdVal;
}

Value *LLParser::PerFunctionState::getVal(unsigned ID, Type *Ty, LocTy Loc) {
  // Look this name up in the normal function symbol table.
  Value *Val = NumberedVals.get(ID);

  // If this is a forward reference for the value, see if we already created a
  // forward ref record.
  if (!Val) {
    auto I = ForwardRefValIDs.find(ID);
    if (I != ForwardRefValIDs.end())
      Val = I->second.first;
  }

  // If we have the value in the symbol table or fwd-ref table, return it.
  if (Val)
    return P.checkValidVariableType(Loc, "%" + Twine(ID), Ty, Val);

  if (!Ty->isFirstClassType()) {
    P.error(Loc, "invalid use of a non-first-class type");
    return nullptr;
  }

  // Otherwise, create a new forward reference for this value and remember it.
  Value *FwdVal;
  if (Ty->isLabelTy()) {
    FwdVal = BasicBlock::Create(F.getContext(), "", &F);
  } else {
    FwdVal = new Argument(Ty);
  }

  ForwardRefValIDs[ID] = std::make_pair(FwdVal, Loc);
  return FwdVal;
}

/// setInstName - After an instruction is parsed and inserted into its
/// basic block, this installs its name.
bool LLParser::PerFunctionState::setInstName(int NameID,
                                             const std::string &NameStr,
                                             LocTy NameLoc, Instruction *Inst) {
  // If this instruction has void type, it cannot have a name or ID specified.
  if (Inst->getType()->isVoidTy()) {
    if (NameID != -1 || !NameStr.empty())
      return P.error(NameLoc, "instructions returning void cannot have a name");
    return false;
  }

  // If this was a numbered instruction, verify that the instruction is the
  // expected value and resolve any forward references.
  if (NameStr.empty()) {
    // If neither a name nor an ID was specified, just use the next ID.
    if (NameID == -1)
      NameID = NumberedVals.getNext();

    if (P.checkValueID(NameLoc, "instruction", "%", NumberedVals.getNext(),
                       NameID))
      return true;

    auto FI = ForwardRefValIDs.find(NameID);
    if (FI != ForwardRefValIDs.end()) {
      Value *Sentinel = FI->second.first;
      if (Sentinel->getType() != Inst->getType())
        return P.error(NameLoc, "instruction forward referenced with type '" +
                                    getTypeString(FI->second.first->getType()) +
                                    "'");

      Sentinel->replaceAllUsesWith(Inst);
      Sentinel->deleteValue();
      ForwardRefValIDs.erase(FI);
    }

    NumberedVals.add(NameID, Inst);
    return false;
  }

  // Otherwise, the instruction had a name.  Resolve forward refs and set it.
  auto FI = ForwardRefVals.find(NameStr);
  if (FI != ForwardRefVals.end()) {
    Value *Sentinel = FI->second.first;
    if (Sentinel->getType() != Inst->getType())
      return P.error(NameLoc, "instruction forward referenced with type '" +
                                  getTypeString(FI->second.first->getType()) +
                                  "'");

    Sentinel->replaceAllUsesWith(Inst);
    Sentinel->deleteValue();
    ForwardRefVals.erase(FI);
  }

  // Set the name on the instruction.
  Inst->setName(NameStr);

  if (Inst->getName() != NameStr)
    return P.error(NameLoc, "multiple definition of local value named '" +
                                NameStr + "'");
  return false;
}

/// getBB - Get a basic block with the specified name or ID, creating a
/// forward reference record if needed.
BasicBlock *LLParser::PerFunctionState::getBB(const std::string &Name,
                                              LocTy Loc) {
  return dyn_cast_or_null<BasicBlock>(
      getVal(Name, Type::getLabelTy(F.getContext()), Loc));
}

BasicBlock *LLParser::PerFunctionState::getBB(unsigned ID, LocTy Loc) {
  return dyn_cast_or_null<BasicBlock>(
      getVal(ID, Type::getLabelTy(F.getContext()), Loc));
}

/// defineBB - Define the specified basic block, which is either named or
/// unnamed.  If there is an error, this returns null otherwise it returns
/// the block being defined.
BasicBlock *LLParser::PerFunctionState::defineBB(const std::string &Name,
                                                 int NameID, LocTy Loc) {
  BasicBlock *BB;
  if (Name.empty()) {
    if (NameID != -1) {
      if (P.checkValueID(Loc, "label", "", NumberedVals.getNext(), NameID))
        return nullptr;
    } else {
      NameID = NumberedVals.getNext();
    }
    BB = getBB(NameID, Loc);
    if (!BB) {
      P.error(Loc, "unable to create block numbered '" + Twine(NameID) + "'");
      return nullptr;
    }
  } else {
    BB = getBB(Name, Loc);
    if (!BB) {
      P.error(Loc, "unable to create block named '" + Name + "'");
      return nullptr;
    }
  }

  // Move the block to the end of the function.  Forward ref'd blocks are
  // inserted wherever they happen to be referenced.
  F.splice(F.end(), &F, BB->getIterator());

  // Remove the block from forward ref sets.
  if (Name.empty()) {
    ForwardRefValIDs.erase(NameID);
    NumberedVals.add(NameID, BB);
  } else {
    // BB forward references are already in the function symbol table.
    ForwardRefVals.erase(Name);
  }

  return BB;
}

//===----------------------------------------------------------------------===//
// Constants.
//===----------------------------------------------------------------------===//

/// parseValID - parse an abstract value that doesn't necessarily have a
/// type implied.  For example, if we parse "4" we don't know what integer type
/// it has.  The value will later be combined with its type and checked for
/// basic correctness.  PFS is used to convert function-local operands of
/// metadata (since metadata operands are not just parsed here but also
/// converted to values). PFS can be null when we are not parsing metadata
/// values inside a function.
bool LLParser::parseValID(ValID &ID, PerFunctionState *PFS, Type *ExpectedTy) {
  ID.Loc = Lex.getLoc();
  switch (Lex.getKind()) {
  default:
    return tokError("expected value token");
  case lltok::GlobalID:  // @42
    ID.UIntVal = Lex.getUIntVal();
    ID.Kind = ValID::t_GlobalID;
    break;
  case lltok::GlobalVar:  // @foo
    ID.StrVal = Lex.getStrVal();
    ID.Kind = ValID::t_GlobalName;
    break;
  case lltok::LocalVarID:  // %42
    ID.UIntVal = Lex.getUIntVal();
    ID.Kind = ValID::t_LocalID;
    break;
  case lltok::LocalVar:  // %foo
    ID.StrVal = Lex.getStrVal();
    ID.Kind = ValID::t_LocalName;
    break;
  case lltok::APSInt:
    ID.APSIntVal = Lex.getAPSIntVal();
    ID.Kind = ValID::t_APSInt;
    break;
  case lltok::APFloat:
    ID.APFloatVal = Lex.getAPFloatVal();
    ID.Kind = ValID::t_APFloat;
    break;
  case lltok::kw_true:
    ID.ConstantVal = ConstantInt::getTrue(Context);
    ID.Kind = ValID::t_Constant;
    break;
  case lltok::kw_false:
    ID.ConstantVal = ConstantInt::getFalse(Context);
    ID.Kind = ValID::t_Constant;
    break;
  case lltok::kw_null: ID.Kind = ValID::t_Null; break;
  case lltok::kw_undef: ID.Kind = ValID::t_Undef; break;
  case lltok::kw_poison: ID.Kind = ValID::t_Poison; break;
  case lltok::kw_zeroinitializer: ID.Kind = ValID::t_Zero; break;
  case lltok::kw_none: ID.Kind = ValID::t_None; break;

  case lltok::lbrace: {
    // ValID ::= '{' ConstVector '}'
    Lex.Lex();
    SmallVector<Constant*, 16> Elts;
    if (parseGlobalValueVector(Elts) ||
        parseToken(lltok::rbrace, "expected end of struct constant"))
      return true;

    ID.ConstantStructElts = std::make_unique<Constant *[]>(Elts.size());
    ID.UIntVal = Elts.size();
    memcpy(ID.ConstantStructElts.get(), Elts.data(),
           Elts.size() * sizeof(Elts[0]));
    ID.Kind = ValID::t_ConstantStruct;
    return false;
  }
  case lltok::less: {
    // ValID ::= '<' ConstVector '>'         --> Vector.
    // ValID ::= '<' '{' ConstVector '}' '>' --> Packed Struct.
    Lex.Lex();
    bool isPackedStruct = EatIfPresent(lltok::lbrace);

    SmallVector<Constant*, 16> Elts;
    LocTy FirstEltLoc = Lex.getLoc();
    if (parseGlobalValueVector(Elts) ||
        (isPackedStruct &&
         parseToken(lltok::rbrace, "expected end of packed struct")) ||
        parseToken(lltok::greater, "expected end of constant"))
      return true;

    if (isPackedStruct) {
      ID.ConstantStructElts = std::make_unique<Constant *[]>(Elts.size());
      memcpy(ID.ConstantStructElts.get(), Elts.data(),
             Elts.size() * sizeof(Elts[0]));
      ID.UIntVal = Elts.size();
      ID.Kind = ValID::t_PackedConstantStruct;
      return false;
    }

    if (Elts.empty())
      return error(ID.Loc, "constant vector must not be empty");

    if (!Elts[0]->getType()->isIntegerTy() &&
        !Elts[0]->getType()->isFloatingPointTy() &&
        !Elts[0]->getType()->isPointerTy())
      return error(
          FirstEltLoc,
          "vector elements must have integer, pointer or floating point type");

    // Verify that all the vector elements have the same type.
    for (unsigned i = 1, e = Elts.size(); i != e; ++i)
      if (Elts[i]->getType() != Elts[0]->getType())
        return error(FirstEltLoc, "vector element #" + Twine(i) +
                                      " is not of type '" +
                                      getTypeString(Elts[0]->getType()));

    ID.ConstantVal = ConstantVector::get(Elts);
    ID.Kind = ValID::t_Constant;
    return false;
  }
  case lltok::lsquare: {   // Array Constant
    Lex.Lex();
    SmallVector<Constant*, 16> Elts;
    LocTy FirstEltLoc = Lex.getLoc();
    if (parseGlobalValueVector(Elts) ||
        parseToken(lltok::rsquare, "expected end of array constant"))
      return true;

    // Handle empty element.
    if (Elts.empty()) {
      // Use undef instead of an array because it's inconvenient to determine
      // the element type at this point, there being no elements to examine.
      ID.Kind = ValID::t_EmptyArray;
      return false;
    }

    if (!Elts[0]->getType()->isFirstClassType())
      return error(FirstEltLoc, "invalid array element type: " +
                                    getTypeString(Elts[0]->getType()));

    ArrayType *ATy = ArrayType::get(Elts[0]->getType(), Elts.size());

    // Verify all elements are correct type!
    for (unsigned i = 0, e = Elts.size(); i != e; ++i) {
      if (Elts[i]->getType() != Elts[0]->getType())
        return error(FirstEltLoc, "array element #" + Twine(i) +
                                      " is not of type '" +
                                      getTypeString(Elts[0]->getType()));
    }

    ID.ConstantVal = ConstantArray::get(ATy, Elts);
    ID.Kind = ValID::t_Constant;
    return false;
  }
  case lltok::kw_c:  // c "foo"
    Lex.Lex();
    ID.ConstantVal = ConstantDataArray::getString(Context, Lex.getStrVal(),
                                                  false);
    if (parseToken(lltok::StringConstant, "expected string"))
      return true;
    ID.Kind = ValID::t_Constant;
    return false;

  case lltok::kw_asm: {
    // ValID ::= 'asm' SideEffect? AlignStack? IntelDialect? STRINGCONSTANT ','
    //             STRINGCONSTANT
    bool HasSideEffect, AlignStack, AsmDialect, CanThrow;
    Lex.Lex();
    if (parseOptionalToken(lltok::kw_sideeffect, HasSideEffect) ||
        parseOptionalToken(lltok::kw_alignstack, AlignStack) ||
        parseOptionalToken(lltok::kw_inteldialect, AsmDialect) ||
        parseOptionalToken(lltok::kw_unwind, CanThrow) ||
        parseStringConstant(ID.StrVal) ||
        parseToken(lltok::comma, "expected comma in inline asm expression") ||
        parseToken(lltok::StringConstant, "expected constraint string"))
      return true;
    ID.StrVal2 = Lex.getStrVal();
    ID.UIntVal = unsigned(HasSideEffect) | (unsigned(AlignStack) << 1) |
                 (unsigned(AsmDialect) << 2) | (unsigned(CanThrow) << 3);
    ID.Kind = ValID::t_InlineAsm;
    return false;
  }

  case lltok::kw_blockaddress: {
    // ValID ::= 'blockaddress' '(' @foo ',' %bar ')'
    Lex.Lex();

    ValID Fn, Label;

    if (parseToken(lltok::lparen, "expected '(' in block address expression") ||
        parseValID(Fn, PFS) ||
        parseToken(lltok::comma,
                   "expected comma in block address expression") ||
        parseValID(Label, PFS) ||
        parseToken(lltok::rparen, "expected ')' in block address expression"))
      return true;

    if (Fn.Kind != ValID::t_GlobalID && Fn.Kind != ValID::t_GlobalName)
      return error(Fn.Loc, "expected function name in blockaddress");
    if (Label.Kind != ValID::t_LocalID && Label.Kind != ValID::t_LocalName)
      return error(Label.Loc, "expected basic block name in blockaddress");

    // Try to find the function (but skip it if it's forward-referenced).
    GlobalValue *GV = nullptr;
    if (Fn.Kind == ValID::t_GlobalID) {
      GV = NumberedVals.get(Fn.UIntVal);
    } else if (!ForwardRefVals.count(Fn.StrVal)) {
      GV = M->getNamedValue(Fn.StrVal);
    }
    Function *F = nullptr;
    if (GV) {
      // Confirm that it's actually a function with a definition.
      if (!isa<Function>(GV))
        return error(Fn.Loc, "expected function name in blockaddress");
      F = cast<Function>(GV);
      if (F->isDeclaration())
        return error(Fn.Loc, "cannot take blockaddress inside a declaration");
    }

    if (!F) {
      // Make a global variable as a placeholder for this reference.
      GlobalValue *&FwdRef =
          ForwardRefBlockAddresses.insert(std::make_pair(
                                              std::move(Fn),
                                              std::map<ValID, GlobalValue *>()))
              .first->second.insert(std::make_pair(std::move(Label), nullptr))
              .first->second;
      if (!FwdRef) {
        unsigned FwdDeclAS;
        if (ExpectedTy) {
          // If we know the type that the blockaddress is being assigned to,
          // we can use the address space of that type.
          if (!ExpectedTy->isPointerTy())
            return error(ID.Loc,
                         "type of blockaddress must be a pointer and not '" +
                             getTypeString(ExpectedTy) + "'");
          FwdDeclAS = ExpectedTy->getPointerAddressSpace();
        } else if (PFS) {
          // Otherwise, we default the address space of the current function.
          FwdDeclAS = PFS->getFunction().getAddressSpace();
        } else {
          llvm_unreachable("Unknown address space for blockaddress");
        }
        FwdRef = new GlobalVariable(
            *M, Type::getInt8Ty(Context), false, GlobalValue::InternalLinkage,
            nullptr, "", nullptr, GlobalValue::NotThreadLocal, FwdDeclAS);
      }

      ID.ConstantVal = FwdRef;
      ID.Kind = ValID::t_Constant;
      return false;
    }

    // We found the function; now find the basic block.  Don't use PFS, since we
    // might be inside a constant expression.
    BasicBlock *BB;
    if (BlockAddressPFS && F == &BlockAddressPFS->getFunction()) {
      if (Label.Kind == ValID::t_LocalID)
        BB = BlockAddressPFS->getBB(Label.UIntVal, Label.Loc);
      else
        BB = BlockAddressPFS->getBB(Label.StrVal, Label.Loc);
      if (!BB)
        return error(Label.Loc, "referenced value is not a basic block");
    } else {
      if (Label.Kind == ValID::t_LocalID)
        return error(Label.Loc, "cannot take address of numeric label after "
                                "the function is defined");
      BB = dyn_cast_or_null<BasicBlock>(
          F->getValueSymbolTable()->lookup(Label.StrVal));
      if (!BB)
        return error(Label.Loc, "referenced value is not a basic block");
    }

    ID.ConstantVal = BlockAddress::get(F, BB);
    ID.Kind = ValID::t_Constant;
    return false;
  }

  case lltok::kw_dso_local_equivalent: {
    // ValID ::= 'dso_local_equivalent' @foo
    Lex.Lex();

    ValID Fn;

    if (parseValID(Fn, PFS))
      return true;

    if (Fn.Kind != ValID::t_GlobalID && Fn.Kind != ValID::t_GlobalName)
      return error(Fn.Loc,
                   "expected global value name in dso_local_equivalent");

    // Try to find the function (but skip it if it's forward-referenced).
    GlobalValue *GV = nullptr;
    if (Fn.Kind == ValID::t_GlobalID) {
      GV = NumberedVals.get(Fn.UIntVal);
    } else if (!ForwardRefVals.count(Fn.StrVal)) {
      GV = M->getNamedValue(Fn.StrVal);
    }

    if (!GV) {
      // Make a placeholder global variable as a placeholder for this reference.
      auto &FwdRefMap = (Fn.Kind == ValID::t_GlobalID)
                            ? ForwardRefDSOLocalEquivalentIDs
                            : ForwardRefDSOLocalEquivalentNames;
      GlobalValue *&FwdRef = FwdRefMap.try_emplace(Fn, nullptr).first->second;
      if (!FwdRef) {
        FwdRef = new GlobalVariable(*M, Type::getInt8Ty(Context), false,
                                    GlobalValue::InternalLinkage, nullptr, "",
                                    nullptr, GlobalValue::NotThreadLocal);
      }

      ID.ConstantVal = FwdRef;
      ID.Kind = ValID::t_Constant;
      return false;
    }

    if (!GV->getValueType()->isFunctionTy())
      return error(Fn.Loc, "expected a function, alias to function, or ifunc "
                           "in dso_local_equivalent");

    ID.ConstantVal = DSOLocalEquivalent::get(GV);
    ID.Kind = ValID::t_Constant;
    return false;
  }

  case lltok::kw_no_cfi: {
    // ValID ::= 'no_cfi' @foo
    Lex.Lex();

    if (parseValID(ID, PFS))
      return true;

    if (ID.Kind != ValID::t_GlobalID && ID.Kind != ValID::t_GlobalName)
      return error(ID.Loc, "expected global value name in no_cfi");

    ID.NoCFI = true;
    return false;
  }
  case lltok::kw_ptrauth: {
    // ValID ::= 'ptrauth' '(' ptr @foo ',' i32 <key>
    //                         (',' i64 <disc> (',' ptr addrdisc)? )? ')'
    Lex.Lex();

    Constant *Ptr, *Key;
    Constant *Disc = nullptr, *AddrDisc = nullptr;

    if (parseToken(lltok::lparen,
                   "expected '(' in constant ptrauth expression") ||
        parseGlobalTypeAndValue(Ptr) ||
        parseToken(lltok::comma,
                   "expected comma in constant ptrauth expression") ||
        parseGlobalTypeAndValue(Key))
      return true;
    // If present, parse the optional disc/addrdisc.
    if (EatIfPresent(lltok::comma))
      if (parseGlobalTypeAndValue(Disc) ||
          (EatIfPresent(lltok::comma) && parseGlobalTypeAndValue(AddrDisc)))
        return true;
    if (parseToken(lltok::rparen,
                   "expected ')' in constant ptrauth expression"))
      return true;

    if (!Ptr->getType()->isPointerTy())
      return error(ID.Loc, "constant ptrauth base pointer must be a pointer");

    auto *KeyC = dyn_cast<ConstantInt>(Key);
    if (!KeyC || KeyC->getBitWidth() != 32)
      return error(ID.Loc, "constant ptrauth key must be i32 constant");

    ConstantInt *DiscC = nullptr;
    if (Disc) {
      DiscC = dyn_cast<ConstantInt>(Disc);
      if (!DiscC || DiscC->getBitWidth() != 64)
        return error(
            ID.Loc,
            "constant ptrauth integer discriminator must be i64 constant");
    } else {
      DiscC = ConstantInt::get(Type::getInt64Ty(Context), 0);
    }

    if (AddrDisc) {
      if (!AddrDisc->getType()->isPointerTy())
        return error(
            ID.Loc, "constant ptrauth address discriminator must be a pointer");
    } else {
      AddrDisc = ConstantPointerNull::get(PointerType::get(Context, 0));
    }

    ID.ConstantVal = ConstantPtrAuth::get(Ptr, KeyC, DiscC, AddrDisc);
    ID.Kind = ValID::t_Constant;
    return false;
  }

  case lltok::kw_trunc:
  case lltok::kw_bitcast:
  case lltok::kw_addrspacecast:
  case lltok::kw_inttoptr:
  case lltok::kw_ptrtoint: {
    unsigned Opc = Lex.getUIntVal();
    Type *DestTy = nullptr;
    Constant *SrcVal;
    Lex.Lex();
    if (parseToken(lltok::lparen, "expected '(' after constantexpr cast") ||
        parseGlobalTypeAndValue(SrcVal) ||
        parseToken(lltok::kw_to, "expected 'to' in constantexpr cast") ||
        parseType(DestTy) ||
        parseToken(lltok::rparen, "expected ')' at end of constantexpr cast"))
      return true;
    if (!CastInst::castIsValid((Instruction::CastOps)Opc, SrcVal, DestTy))
      return error(ID.Loc, "invalid cast opcode for cast from '" +
                               getTypeString(SrcVal->getType()) + "' to '" +
                               getTypeString(DestTy) + "'");
    ID.ConstantVal = ConstantExpr::getCast((Instruction::CastOps)Opc,
                                                 SrcVal, DestTy);
    ID.Kind = ValID::t_Constant;
    return false;
  }
  case lltok::kw_extractvalue:
    return error(ID.Loc, "extractvalue constexprs are no longer supported");
  case lltok::kw_insertvalue:
    return error(ID.Loc, "insertvalue constexprs are no longer supported");
  case lltok::kw_udiv:
    return error(ID.Loc, "udiv constexprs are no longer supported");
  case lltok::kw_sdiv:
    return error(ID.Loc, "sdiv constexprs are no longer supported");
  case lltok::kw_urem:
    return error(ID.Loc, "urem constexprs are no longer supported");
  case lltok::kw_srem:
    return error(ID.Loc, "srem constexprs are no longer supported");
  case lltok::kw_fadd:
    return error(ID.Loc, "fadd constexprs are no longer supported");
  case lltok::kw_fsub:
    return error(ID.Loc, "fsub constexprs are no longer supported");
  case lltok::kw_fmul:
    return error(ID.Loc, "fmul constexprs are no longer supported");
  case lltok::kw_fdiv:
    return error(ID.Loc, "fdiv constexprs are no longer supported");
  case lltok::kw_frem:
    return error(ID.Loc, "frem constexprs are no longer supported");
  case lltok::kw_and:
    return error(ID.Loc, "and constexprs are no longer supported");
  case lltok::kw_or:
    return error(ID.Loc, "or constexprs are no longer supported");
  case lltok::kw_lshr:
    return error(ID.Loc, "lshr constexprs are no longer supported");
  case lltok::kw_ashr:
    return error(ID.Loc, "ashr constexprs are no longer supported");
  case lltok::kw_shl:
    return error(ID.Loc, "shl constexprs are no longer supported");
  case lltok::kw_fneg:
    return error(ID.Loc, "fneg constexprs are no longer supported");
  case lltok::kw_select:
    return error(ID.Loc, "select constexprs are no longer supported");
  case lltok::kw_zext:
    return error(ID.Loc, "zext constexprs are no longer supported");
  case lltok::kw_sext:
    return error(ID.Loc, "sext constexprs are no longer supported");
  case lltok::kw_fptrunc:
    return error(ID.Loc, "fptrunc constexprs are no longer supported");
  case lltok::kw_fpext:
    return error(ID.Loc, "fpext constexprs are no longer supported");
  case lltok::kw_uitofp:
    return error(ID.Loc, "uitofp constexprs are no longer supported");
  case lltok::kw_sitofp:
    return error(ID.Loc, "sitofp constexprs are no longer supported");
  case lltok::kw_fptoui:
    return error(ID.Loc, "fptoui constexprs are no longer supported");
  case lltok::kw_fptosi:
    return error(ID.Loc, "fptosi constexprs are no longer supported");
  case lltok::kw_icmp:
    return error(ID.Loc, "icmp constexprs are no longer supported");
  case lltok::kw_fcmp:
    return error(ID.Loc, "fcmp constexprs are no longer supported");

  // Binary Operators.
  case lltok::kw_add:
  case lltok::kw_sub:
  case lltok::kw_mul:
  case lltok::kw_xor: {
    bool NUW = false;
    bool NSW = false;
    unsigned Opc = Lex.getUIntVal();
    Constant *Val0, *Val1;
    Lex.Lex();
    if (Opc == Instruction::Add || Opc == Instruction::Sub ||
        Opc == Instruction::Mul) {
      if (EatIfPresent(lltok::kw_nuw))
        NUW = true;
      if (EatIfPresent(lltok::kw_nsw)) {
        NSW = true;
        if (EatIfPresent(lltok::kw_nuw))
          NUW = true;
      }
    }
    if (parseToken(lltok::lparen, "expected '(' in binary constantexpr") ||
        parseGlobalTypeAndValue(Val0) ||
        parseToken(lltok::comma, "expected comma in binary constantexpr") ||
        parseGlobalTypeAndValue(Val1) ||
        parseToken(lltok::rparen, "expected ')' in binary constantexpr"))
      return true;
    if (Val0->getType() != Val1->getType())
      return error(ID.Loc, "operands of constexpr must have same type");
    // Check that the type is valid for the operator.
    if (!Val0->getType()->isIntOrIntVectorTy())
      return error(ID.Loc,
                   "constexpr requires integer or integer vector operands");
    unsigned Flags = 0;
    if (NUW)   Flags |= OverflowingBinaryOperator::NoUnsignedWrap;
    if (NSW)   Flags |= OverflowingBinaryOperator::NoSignedWrap;
    ID.ConstantVal = ConstantExpr::get(Opc, Val0, Val1, Flags);
    ID.Kind = ValID::t_Constant;
    return false;
  }

  case lltok::kw_splat: {
    Lex.Lex();
    if (parseToken(lltok::lparen, "expected '(' after vector splat"))
      return true;
    Constant *C;
    if (parseGlobalTypeAndValue(C))
      return true;
    if (parseToken(lltok::rparen, "expected ')' at end of vector splat"))
      return true;

    ID.ConstantVal = C;
    ID.Kind = ValID::t_ConstantSplat;
    return false;
  }

  case lltok::kw_getelementptr:
  case lltok::kw_shufflevector:
  case lltok::kw_insertelement:
  case lltok::kw_extractelement: {
    unsigned Opc = Lex.getUIntVal();
    SmallVector<Constant*, 16> Elts;
    GEPNoWrapFlags NW;
    bool HasInRange = false;
    APSInt InRangeStart;
    APSInt InRangeEnd;
    Type *Ty;
    Lex.Lex();

    if (Opc == Instruction::GetElementPtr) {
      while (true) {
        if (EatIfPresent(lltok::kw_inbounds))
          NW |= GEPNoWrapFlags::inBounds();
        else if (EatIfPresent(lltok::kw_nusw))
          NW |= GEPNoWrapFlags::noUnsignedSignedWrap();
        else if (EatIfPresent(lltok::kw_nuw))
          NW |= GEPNoWrapFlags::noUnsignedWrap();
        else
          break;
      }

      if (EatIfPresent(lltok::kw_inrange)) {
        if (parseToken(lltok::lparen, "expected '('"))
          return true;
        if (Lex.getKind() != lltok::APSInt)
          return tokError("expected integer");
        InRangeStart = Lex.getAPSIntVal();
        Lex.Lex();
        if (parseToken(lltok::comma, "expected ','"))
          return true;
        if (Lex.getKind() != lltok::APSInt)
          return tokError("expected integer");
        InRangeEnd = Lex.getAPSIntVal();
        Lex.Lex();
        if (parseToken(lltok::rparen, "expected ')'"))
          return true;
        HasInRange = true;
      }
    }

    if (parseToken(lltok::lparen, "expected '(' in constantexpr"))
      return true;

    if (Opc == Instruction::GetElementPtr) {
      if (parseType(Ty) ||
          parseToken(lltok::comma, "expected comma after getelementptr's type"))
        return true;
    }

    if (parseGlobalValueVector(Elts) ||
        parseToken(lltok::rparen, "expected ')' in constantexpr"))
      return true;

    if (Opc == Instruction::GetElementPtr) {
      if (Elts.size() == 0 ||
          !Elts[0]->getType()->isPtrOrPtrVectorTy())
        return error(ID.Loc, "base of getelementptr must be a pointer");

      Type *BaseType = Elts[0]->getType();
      std::optional<ConstantRange> InRange;
      if (HasInRange) {
        unsigned IndexWidth =
            M->getDataLayout().getIndexTypeSizeInBits(BaseType);
        InRangeStart = InRangeStart.extOrTrunc(IndexWidth);
        InRangeEnd = InRangeEnd.extOrTrunc(IndexWidth);
        if (InRangeStart.sge(InRangeEnd))
          return error(ID.Loc, "expected end to be larger than start");
        InRange = ConstantRange::getNonEmpty(InRangeStart, InRangeEnd);
      }

      unsigned GEPWidth =
          BaseType->isVectorTy()
              ? cast<FixedVectorType>(BaseType)->getNumElements()
              : 0;

      ArrayRef<Constant *> Indices(Elts.begin() + 1, Elts.end());
      for (Constant *Val : Indices) {
        Type *ValTy = Val->getType();
        if (!ValTy->isIntOrIntVectorTy())
          return error(ID.Loc, "getelementptr index must be an integer");
        if (auto *ValVTy = dyn_cast<VectorType>(ValTy)) {
          unsigned ValNumEl = cast<FixedVectorType>(ValVTy)->getNumElements();
          if (GEPWidth && (ValNumEl != GEPWidth))
            return error(
                ID.Loc,
                "getelementptr vector index has a wrong number of elements");
          // GEPWidth may have been unknown because the base is a scalar,
          // but it is known now.
          GEPWidth = ValNumEl;
        }
      }

      SmallPtrSet<Type*, 4> Visited;
      if (!Indices.empty() && !Ty->isSized(&Visited))
        return error(ID.Loc, "base element of getelementptr must be sized");

      if (!GetElementPtrInst::getIndexedType(Ty, Indices))
        return error(ID.Loc, "invalid getelementptr indices");

      ID.ConstantVal =
          ConstantExpr::getGetElementPtr(Ty, Elts[0], Indices, NW, InRange);
    } else if (Opc == Instruction::ShuffleVector) {
      if (Elts.size() != 3)
        return error(ID.Loc, "expected three operands to shufflevector");
      if (!ShuffleVectorInst::isValidOperands(Elts[0], Elts[1], Elts[2]))
        return error(ID.Loc, "invalid operands to shufflevector");
      SmallVector<int, 16> Mask;
      ShuffleVectorInst::getShuffleMask(cast<Constant>(Elts[2]), Mask);
      ID.ConstantVal = ConstantExpr::getShuffleVector(Elts[0], Elts[1], Mask);
    } else if (Opc == Instruction::ExtractElement) {
      if (Elts.size() != 2)
        return error(ID.Loc, "expected two operands to extractelement");
      if (!ExtractElementInst::isValidOperands(Elts[0], Elts[1]))
        return error(ID.Loc, "invalid extractelement operands");
      ID.ConstantVal = ConstantExpr::getExtractElement(Elts[0], Elts[1]);
    } else {
      assert(Opc == Instruction::InsertElement && "Unknown opcode");
      if (Elts.size() != 3)
        return error(ID.Loc, "expected three operands to insertelement");
      if (!InsertElementInst::isValidOperands(Elts[0], Elts[1], Elts[2]))
        return error(ID.Loc, "invalid insertelement operands");
      ID.ConstantVal =
                 ConstantExpr::getInsertElement(Elts[0], Elts[1],Elts[2]);
    }

    ID.Kind = ValID::t_Constant;
    return false;
  }
  }

  Lex.Lex();
  return false;
}

/// parseGlobalValue - parse a global value with the specified type.
bool LLParser::parseGlobalValue(Type *Ty, Constant *&C) {
  C = nullptr;
  ValID ID;
  Value *V = nullptr;
  bool Parsed = parseValID(ID, /*PFS=*/nullptr, Ty) ||
                convertValIDToValue(Ty, ID, V, nullptr);
  if (V && !(C = dyn_cast<Constant>(V)))
    return error(ID.Loc, "global values must be constants");
  return Parsed;
}

bool LLParser::parseGlobalTypeAndValue(Constant *&V) {
  Type *Ty = nullptr;
  return parseType(Ty) || parseGlobalValue(Ty, V);
}

bool LLParser::parseOptionalComdat(StringRef GlobalName, Comdat *&C) {
  C = nullptr;

  LocTy KwLoc = Lex.getLoc();
  if (!EatIfPresent(lltok::kw_comdat))
    return false;

  if (EatIfPresent(lltok::lparen)) {
    if (Lex.getKind() != lltok::ComdatVar)
      return tokError("expected comdat variable");
    C = getComdat(Lex.getStrVal(), Lex.getLoc());
    Lex.Lex();
    if (parseToken(lltok::rparen, "expected ')' after comdat var"))
      return true;
  } else {
    if (GlobalName.empty())
      return tokError("comdat cannot be unnamed");
    C = getComdat(std::string(GlobalName), KwLoc);
  }

  return false;
}

/// parseGlobalValueVector
///   ::= /*empty*/
///   ::= TypeAndValue (',' TypeAndValue)*
bool LLParser::parseGlobalValueVector(SmallVectorImpl<Constant *> &Elts) {
  // Empty list.
  if (Lex.getKind() == lltok::rbrace ||
      Lex.getKind() == lltok::rsquare ||
      Lex.getKind() == lltok::greater ||
      Lex.getKind() == lltok::rparen)
    return false;

  do {
    // Let the caller deal with inrange.
    if (Lex.getKind() == lltok::kw_inrange)
      return false;

    Constant *C;
    if (parseGlobalTypeAndValue(C))
      return true;
    Elts.push_back(C);
  } while (EatIfPresent(lltok::comma));

  return false;
}

bool LLParser::parseMDTuple(MDNode *&MD, bool IsDistinct) {
  SmallVector<Metadata *, 16> Elts;
  if (parseMDNodeVector(Elts))
    return true;

  MD = (IsDistinct ? MDTuple::getDistinct : MDTuple::get)(Context, Elts);
  return false;
}

/// MDNode:
///  ::= !{ ... }
///  ::= !7
///  ::= !DILocation(...)
bool LLParser::parseMDNode(MDNode *&N) {
  if (Lex.getKind() == lltok::MetadataVar)
    return parseSpecializedMDNode(N);

  return parseToken(lltok::exclaim, "expected '!' here") || parseMDNodeTail(N);
}

bool LLParser::parseMDNodeTail(MDNode *&N) {
  // !{ ... }
  if (Lex.getKind() == lltok::lbrace)
    return parseMDTuple(N);

  // !42
  return parseMDNodeID(N);
}

namespace {

/// Structure to represent an optional metadata field.
template <class FieldTy> struct MDFieldImpl {
  typedef MDFieldImpl ImplTy;
  FieldTy Val;
  bool Seen;

  void assign(FieldTy Val) {
    Seen = true;
    this->Val = std::move(Val);
  }

  explicit MDFieldImpl(FieldTy Default)
      : Val(std::move(Default)), Seen(false) {}
};

/// Structure to represent an optional metadata field that
/// can be of either type (A or B) and encapsulates the
/// MD<typeofA>Field and MD<typeofB>Field structs, so not
/// to reimplement the specifics for representing each Field.
template <class FieldTypeA, class FieldTypeB> struct MDEitherFieldImpl {
  typedef MDEitherFieldImpl<FieldTypeA, FieldTypeB> ImplTy;
  FieldTypeA A;
  FieldTypeB B;
  bool Seen;

  enum {
    IsInvalid = 0,
    IsTypeA = 1,
    IsTypeB = 2
  } WhatIs;

  void assign(FieldTypeA A) {
    Seen = true;
    this->A = std::move(A);
    WhatIs = IsTypeA;
  }

  void assign(FieldTypeB B) {
    Seen = true;
    this->B = std::move(B);
    WhatIs = IsTypeB;
  }

  explicit MDEitherFieldImpl(FieldTypeA DefaultA, FieldTypeB DefaultB)
      : A(std::move(DefaultA)), B(std::move(DefaultB)), Seen(false),
        WhatIs(IsInvalid) {}
};

struct MDUnsignedField : public MDFieldImpl<uint64_t> {
  uint64_t Max;

  MDUnsignedField(uint64_t Default = 0, uint64_t Max = UINT64_MAX)
      : ImplTy(Default), Max(Max) {}
};

struct LineField : public MDUnsignedField {
  LineField() : MDUnsignedField(0, UINT32_MAX) {}
};

struct ColumnField : public MDUnsignedField {
  ColumnField() : MDUnsignedField(0, UINT16_MAX) {}
};

struct DwarfTagField : public MDUnsignedField {
  DwarfTagField() : MDUnsignedField(0, dwarf::DW_TAG_hi_user) {}
  DwarfTagField(dwarf::Tag DefaultTag)
      : MDUnsignedField(DefaultTag, dwarf::DW_TAG_hi_user) {}
};

struct DwarfMacinfoTypeField : public MDUnsignedField {
  DwarfMacinfoTypeField() : MDUnsignedField(0, dwarf::DW_MACINFO_vendor_ext) {}
  DwarfMacinfoTypeField(dwarf::MacinfoRecordType DefaultType)
    : MDUnsignedField(DefaultType, dwarf::DW_MACINFO_vendor_ext) {}
};

struct DwarfAttEncodingField : public MDUnsignedField {
  DwarfAttEncodingField() : MDUnsignedField(0, dwarf::DW_ATE_hi_user) {}
};

struct DwarfVirtualityField : public MDUnsignedField {
  DwarfVirtualityField() : MDUnsignedField(0, dwarf::DW_VIRTUALITY_max) {}
};

struct DwarfLangField : public MDUnsignedField {
  DwarfLangField() : MDUnsignedField(0, dwarf::DW_LANG_hi_user) {}
};

struct DwarfCCField : public MDUnsignedField {
  DwarfCCField() : MDUnsignedField(0, dwarf::DW_CC_hi_user) {}
};

struct EmissionKindField : public MDUnsignedField {
  EmissionKindField() : MDUnsignedField(0, DICompileUnit::LastEmissionKind) {}
};

struct NameTableKindField : public MDUnsignedField {
  NameTableKindField()
      : MDUnsignedField(
            0, (unsigned)
                   DICompileUnit::DebugNameTableKind::LastDebugNameTableKind) {}
};

struct DIFlagField : public MDFieldImpl<DINode::DIFlags> {
  DIFlagField() : MDFieldImpl(DINode::FlagZero) {}
};

struct DISPFlagField : public MDFieldImpl<DISubprogram::DISPFlags> {
  DISPFlagField() : MDFieldImpl(DISubprogram::SPFlagZero) {}
};

struct MDAPSIntField : public MDFieldImpl<APSInt> {
  MDAPSIntField() : ImplTy(APSInt()) {}
};

struct MDSignedField : public MDFieldImpl<int64_t> {
  int64_t Min = INT64_MIN;
  int64_t Max = INT64_MAX;

  MDSignedField(int64_t Default = 0)
      : ImplTy(Default) {}
  MDSignedField(int64_t Default, int64_t Min, int64_t Max)
      : ImplTy(Default), Min(Min), Max(Max) {}
};

struct MDBoolField : public MDFieldImpl<bool> {
  MDBoolField(bool Default = false) : ImplTy(Default) {}
};

struct MDField : public MDFieldImpl<Metadata *> {
  bool AllowNull;

  MDField(bool AllowNull = true) : ImplTy(nullptr), AllowNull(AllowNull) {}
};

struct MDStringField : public MDFieldImpl<MDString *> {
  bool AllowEmpty;
  MDStringField(bool AllowEmpty = true)
      : ImplTy(nullptr), AllowEmpty(AllowEmpty) {}
};

struct MDFieldList : public MDFieldImpl<SmallVector<Metadata *, 4>> {
  MDFieldList() : ImplTy(SmallVector<Metadata *, 4>()) {}
};

struct ChecksumKindField : public MDFieldImpl<DIFile::ChecksumKind> {
  ChecksumKindField(DIFile::ChecksumKind CSKind) : ImplTy(CSKind) {}
};

struct MDSignedOrMDField : MDEitherFieldImpl<MDSignedField, MDField> {
  MDSignedOrMDField(int64_t Default = 0, bool AllowNull = true)
      : ImplTy(MDSignedField(Default), MDField(AllowNull)) {}

  MDSignedOrMDField(int64_t Default, int64_t Min, int64_t Max,
                    bool AllowNull = true)
      : ImplTy(MDSignedField(Default, Min, Max), MDField(AllowNull)) {}

  bool isMDSignedField() const { return WhatIs == IsTypeA; }
  bool isMDField() const { return WhatIs == IsTypeB; }
  int64_t getMDSignedValue() const {
    assert(isMDSignedField() && "Wrong field type");
    return A.Val;
  }
  Metadata *getMDFieldValue() const {
    assert(isMDField() && "Wrong field type");
    return B.Val;
  }
};

} // end anonymous namespace

namespace llvm {

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, MDAPSIntField &Result) {
  if (Lex.getKind() != lltok::APSInt)
    return tokError("expected integer");

  Result.assign(Lex.getAPSIntVal());
  Lex.Lex();
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name,
                            MDUnsignedField &Result) {
  if (Lex.getKind() != lltok::APSInt || Lex.getAPSIntVal().isSigned())
    return tokError("expected unsigned integer");

  auto &U = Lex.getAPSIntVal();
  if (U.ugt(Result.Max))
    return tokError("value for '" + Name + "' too large, limit is " +
                    Twine(Result.Max));
  Result.assign(U.getZExtValue());
  assert(Result.Val <= Result.Max && "Expected value in range");
  Lex.Lex();
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, LineField &Result) {
  return parseMDField(Loc, Name, static_cast<MDUnsignedField &>(Result));
}
template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, ColumnField &Result) {
  return parseMDField(Loc, Name, static_cast<MDUnsignedField &>(Result));
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, DwarfTagField &Result) {
  if (Lex.getKind() == lltok::APSInt)
    return parseMDField(Loc, Name, static_cast<MDUnsignedField &>(Result));

  if (Lex.getKind() != lltok::DwarfTag)
    return tokError("expected DWARF tag");

  unsigned Tag = dwarf::getTag(Lex.getStrVal());
  if (Tag == dwarf::DW_TAG_invalid)
    return tokError("invalid DWARF tag" + Twine(" '") + Lex.getStrVal() + "'");
  assert(Tag <= Result.Max && "Expected valid DWARF tag");

  Result.assign(Tag);
  Lex.Lex();
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name,
                            DwarfMacinfoTypeField &Result) {
  if (Lex.getKind() == lltok::APSInt)
    return parseMDField(Loc, Name, static_cast<MDUnsignedField &>(Result));

  if (Lex.getKind() != lltok::DwarfMacinfo)
    return tokError("expected DWARF macinfo type");

  unsigned Macinfo = dwarf::getMacinfo(Lex.getStrVal());
  if (Macinfo == dwarf::DW_MACINFO_invalid)
    return tokError("invalid DWARF macinfo type" + Twine(" '") +
                    Lex.getStrVal() + "'");
  assert(Macinfo <= Result.Max && "Expected valid DWARF macinfo type");

  Result.assign(Macinfo);
  Lex.Lex();
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name,
                            DwarfVirtualityField &Result) {
  if (Lex.getKind() == lltok::APSInt)
    return parseMDField(Loc, Name, static_cast<MDUnsignedField &>(Result));

  if (Lex.getKind() != lltok::DwarfVirtuality)
    return tokError("expected DWARF virtuality code");

  unsigned Virtuality = dwarf::getVirtuality(Lex.getStrVal());
  if (Virtuality == dwarf::DW_VIRTUALITY_invalid)
    return tokError("invalid DWARF virtuality code" + Twine(" '") +
                    Lex.getStrVal() + "'");
  assert(Virtuality <= Result.Max && "Expected valid DWARF virtuality code");
  Result.assign(Virtuality);
  Lex.Lex();
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, DwarfLangField &Result) {
  if (Lex.getKind() == lltok::APSInt)
    return parseMDField(Loc, Name, static_cast<MDUnsignedField &>(Result));

  if (Lex.getKind() != lltok::DwarfLang)
    return tokError("expected DWARF language");

  unsigned Lang = dwarf::getLanguage(Lex.getStrVal());
  if (!Lang)
    return tokError("invalid DWARF language" + Twine(" '") + Lex.getStrVal() +
                    "'");
  assert(Lang <= Result.Max && "Expected valid DWARF language");
  Result.assign(Lang);
  Lex.Lex();
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, DwarfCCField &Result) {
  if (Lex.getKind() == lltok::APSInt)
    return parseMDField(Loc, Name, static_cast<MDUnsignedField &>(Result));

  if (Lex.getKind() != lltok::DwarfCC)
    return tokError("expected DWARF calling convention");

  unsigned CC = dwarf::getCallingConvention(Lex.getStrVal());
  if (!CC)
    return tokError("invalid DWARF calling convention" + Twine(" '") +
                    Lex.getStrVal() + "'");
  assert(CC <= Result.Max && "Expected valid DWARF calling convention");
  Result.assign(CC);
  Lex.Lex();
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name,
                            EmissionKindField &Result) {
  if (Lex.getKind() == lltok::APSInt)
    return parseMDField(Loc, Name, static_cast<MDUnsignedField &>(Result));

  if (Lex.getKind() != lltok::EmissionKind)
    return tokError("expected emission kind");

  auto Kind = DICompileUnit::getEmissionKind(Lex.getStrVal());
  if (!Kind)
    return tokError("invalid emission kind" + Twine(" '") + Lex.getStrVal() +
                    "'");
  assert(*Kind <= Result.Max && "Expected valid emission kind");
  Result.assign(*Kind);
  Lex.Lex();
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name,
                            NameTableKindField &Result) {
  if (Lex.getKind() == lltok::APSInt)
    return parseMDField(Loc, Name, static_cast<MDUnsignedField &>(Result));

  if (Lex.getKind() != lltok::NameTableKind)
    return tokError("expected nameTable kind");

  auto Kind = DICompileUnit::getNameTableKind(Lex.getStrVal());
  if (!Kind)
    return tokError("invalid nameTable kind" + Twine(" '") + Lex.getStrVal() +
                    "'");
  assert(((unsigned)*Kind) <= Result.Max && "Expected valid nameTable kind");
  Result.assign((unsigned)*Kind);
  Lex.Lex();
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name,
                            DwarfAttEncodingField &Result) {
  if (Lex.getKind() == lltok::APSInt)
    return parseMDField(Loc, Name, static_cast<MDUnsignedField &>(Result));

  if (Lex.getKind() != lltok::DwarfAttEncoding)
    return tokError("expected DWARF type attribute encoding");

  unsigned Encoding = dwarf::getAttributeEncoding(Lex.getStrVal());
  if (!Encoding)
    return tokError("invalid DWARF type attribute encoding" + Twine(" '") +
                    Lex.getStrVal() + "'");
  assert(Encoding <= Result.Max && "Expected valid DWARF language");
  Result.assign(Encoding);
  Lex.Lex();
  return false;
}

/// DIFlagField
///  ::= uint32
///  ::= DIFlagVector
///  ::= DIFlagVector '|' DIFlagFwdDecl '|' uint32 '|' DIFlagPublic
template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, DIFlagField &Result) {

  // parser for a single flag.
  auto parseFlag = [&](DINode::DIFlags &Val) {
    if (Lex.getKind() == lltok::APSInt && !Lex.getAPSIntVal().isSigned()) {
      uint32_t TempVal = static_cast<uint32_t>(Val);
      bool Res = parseUInt32(TempVal);
      Val = static_cast<DINode::DIFlags>(TempVal);
      return Res;
    }

    if (Lex.getKind() != lltok::DIFlag)
      return tokError("expected debug info flag");

    Val = DINode::getFlag(Lex.getStrVal());
    if (!Val)
      return tokError(Twine("invalid debug info flag '") + Lex.getStrVal() +
                      "'");
    Lex.Lex();
    return false;
  };

  // parse the flags and combine them together.
  DINode::DIFlags Combined = DINode::FlagZero;
  do {
    DINode::DIFlags Val;
    if (parseFlag(Val))
      return true;
    Combined |= Val;
  } while (EatIfPresent(lltok::bar));

  Result.assign(Combined);
  return false;
}

/// DISPFlagField
///  ::= uint32
///  ::= DISPFlagVector
///  ::= DISPFlagVector '|' DISPFlag* '|' uint32
template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, DISPFlagField &Result) {

  // parser for a single flag.
  auto parseFlag = [&](DISubprogram::DISPFlags &Val) {
    if (Lex.getKind() == lltok::APSInt && !Lex.getAPSIntVal().isSigned()) {
      uint32_t TempVal = static_cast<uint32_t>(Val);
      bool Res = parseUInt32(TempVal);
      Val = static_cast<DISubprogram::DISPFlags>(TempVal);
      return Res;
    }

    if (Lex.getKind() != lltok::DISPFlag)
      return tokError("expected debug info flag");

    Val = DISubprogram::getFlag(Lex.getStrVal());
    if (!Val)
      return tokError(Twine("invalid subprogram debug info flag '") +
                      Lex.getStrVal() + "'");
    Lex.Lex();
    return false;
  };

  // parse the flags and combine them together.
  DISubprogram::DISPFlags Combined = DISubprogram::SPFlagZero;
  do {
    DISubprogram::DISPFlags Val;
    if (parseFlag(Val))
      return true;
    Combined |= Val;
  } while (EatIfPresent(lltok::bar));

  Result.assign(Combined);
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, MDSignedField &Result) {
  if (Lex.getKind() != lltok::APSInt)
    return tokError("expected signed integer");

  auto &S = Lex.getAPSIntVal();
  if (S < Result.Min)
    return tokError("value for '" + Name + "' too small, limit is " +
                    Twine(Result.Min));
  if (S > Result.Max)
    return tokError("value for '" + Name + "' too large, limit is " +
                    Twine(Result.Max));
  Result.assign(S.getExtValue());
  assert(Result.Val >= Result.Min && "Expected value in range");
  assert(Result.Val <= Result.Max && "Expected value in range");
  Lex.Lex();
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, MDBoolField &Result) {
  switch (Lex.getKind()) {
  default:
    return tokError("expected 'true' or 'false'");
  case lltok::kw_true:
    Result.assign(true);
    break;
  case lltok::kw_false:
    Result.assign(false);
    break;
  }
  Lex.Lex();
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, MDField &Result) {
  if (Lex.getKind() == lltok::kw_null) {
    if (!Result.AllowNull)
      return tokError("'" + Name + "' cannot be null");
    Lex.Lex();
    Result.assign(nullptr);
    return false;
  }

  Metadata *MD;
  if (parseMetadata(MD, nullptr))
    return true;

  Result.assign(MD);
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name,
                            MDSignedOrMDField &Result) {
  // Try to parse a signed int.
  if (Lex.getKind() == lltok::APSInt) {
    MDSignedField Res = Result.A;
    if (!parseMDField(Loc, Name, Res)) {
      Result.assign(Res);
      return false;
    }
    return true;
  }

  // Otherwise, try to parse as an MDField.
  MDField Res = Result.B;
  if (!parseMDField(Loc, Name, Res)) {
    Result.assign(Res);
    return false;
  }

  return true;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, MDStringField &Result) {
  LocTy ValueLoc = Lex.getLoc();
  std::string S;
  if (parseStringConstant(S))
    return true;

  if (!Result.AllowEmpty && S.empty())
    return error(ValueLoc, "'" + Name + "' cannot be empty");

  Result.assign(S.empty() ? nullptr : MDString::get(Context, S));
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name, MDFieldList &Result) {
  SmallVector<Metadata *, 4> MDs;
  if (parseMDNodeVector(MDs))
    return true;

  Result.assign(std::move(MDs));
  return false;
}

template <>
bool LLParser::parseMDField(LocTy Loc, StringRef Name,
                            ChecksumKindField &Result) {
  std::optional<DIFile::ChecksumKind> CSKind =
      DIFile::getChecksumKind(Lex.getStrVal());

  if (Lex.getKind() != lltok::ChecksumKind || !CSKind)
    return tokError("invalid checksum kind" + Twine(" '") + Lex.getStrVal() +
                    "'");

  Result.assign(*CSKind);
  Lex.Lex();
  return false;
}

} // end namespace llvm

template <class ParserTy>
bool LLParser::parseMDFieldsImplBody(ParserTy ParseField) {
  do {
    if (Lex.getKind() != lltok::LabelStr)
      return tokError("expected field label here");

    if (ParseField())
      return true;
  } while (EatIfPresent(lltok::comma));

  return false;
}

template <class ParserTy>
bool LLParser::parseMDFieldsImpl(ParserTy ParseField, LocTy &ClosingLoc) {
  assert(Lex.getKind() == lltok::MetadataVar && "Expected metadata type name");
  Lex.Lex();

  if (parseToken(lltok::lparen, "expected '(' here"))
    return true;
  if (Lex.getKind() != lltok::rparen)
    if (parseMDFieldsImplBody(ParseField))
      return true;

  ClosingLoc = Lex.getLoc();
  return parseToken(lltok::rparen, "expected ')' here");
}

template <class FieldTy>
bool LLParser::parseMDField(StringRef Name, FieldTy &Result) {
  if (Result.Seen)
    return tokError("field '" + Name + "' cannot be specified more than once");

  LocTy Loc = Lex.getLoc();
  Lex.Lex();
  return parseMDField(Loc, Name, Result);
}

bool LLParser::parseSpecializedMDNode(MDNode *&N, bool IsDistinct) {
  assert(Lex.getKind() == lltok::MetadataVar && "Expected metadata type name");

#define HANDLE_SPECIALIZED_MDNODE_LEAF(CLASS)                                  \
  if (Lex.getStrVal() == #CLASS)                                               \
    return parse##CLASS(N, IsDistinct);
#include "llvm/IR/Metadata.def"

  return tokError("expected metadata type");
}

#define DECLARE_FIELD(NAME, TYPE, INIT) TYPE NAME INIT
#define NOP_FIELD(NAME, TYPE, INIT)
#define REQUIRE_FIELD(NAME, TYPE, INIT)                                        \
  if (!NAME.Seen)                                                              \
    return error(ClosingLoc, "missing required field '" #NAME "'");
#define PARSE_MD_FIELD(NAME, TYPE, DEFAULT)                                    \
  if (Lex.getStrVal() == #NAME)                                                \
    return parseMDField(#NAME, NAME);
#define PARSE_MD_FIELDS()                                                      \
  VISIT_MD_FIELDS(DECLARE_FIELD, DECLARE_FIELD)                                \
  do {                                                                         \
    LocTy ClosingLoc;                                                          \
    if (parseMDFieldsImpl(                                                     \
            [&]() -> bool {                                                    \
              VISIT_MD_FIELDS(PARSE_MD_FIELD, PARSE_MD_FIELD)                  \
              return tokError(Twine("invalid field '") + Lex.getStrVal() +     \
                              "'");                                            \
            },                                                                 \
            ClosingLoc))                                                       \
      return true;                                                             \
    VISIT_MD_FIELDS(NOP_FIELD, REQUIRE_FIELD)                                  \
  } while (false)
#define GET_OR_DISTINCT(CLASS, ARGS)                                           \
  (IsDistinct ? CLASS::getDistinct ARGS : CLASS::get ARGS)

/// parseDILocationFields:
///   ::= !DILocation(line: 43, column: 8, scope: !5, inlinedAt: !6,
///   isImplicitCode: true)
bool LLParser::parseDILocation(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  OPTIONAL(line, LineField, );                                                 \
  OPTIONAL(column, ColumnField, );                                             \
  REQUIRED(scope, MDField, (/* AllowNull */ false));                           \
  OPTIONAL(inlinedAt, MDField, );                                              \
  OPTIONAL(isImplicitCode, MDBoolField, (false));
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result =
      GET_OR_DISTINCT(DILocation, (Context, line.Val, column.Val, scope.Val,
                                   inlinedAt.Val, isImplicitCode.Val));
  return false;
}

/// parseDIAssignID:
///   ::= distinct !DIAssignID()
bool LLParser::parseDIAssignID(MDNode *&Result, bool IsDistinct) {
  if (!IsDistinct)
    return Lex.Error("missing 'distinct', required for !DIAssignID()");

  Lex.Lex();

  // Now eat the parens.
  if (parseToken(lltok::lparen, "expected '(' here"))
    return true;
  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  Result = DIAssignID::getDistinct(Context);
  return false;
}

/// parseGenericDINode:
///   ::= !GenericDINode(tag: 15, header: "...", operands: {...})
bool LLParser::parseGenericDINode(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(tag, DwarfTagField, );                                              \
  OPTIONAL(header, MDStringField, );                                           \
  OPTIONAL(operands, MDFieldList, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(GenericDINode,
                           (Context, tag.Val, header.Val, operands.Val));
  return false;
}

/// parseDISubrange:
///   ::= !DISubrange(count: 30, lowerBound: 2)
///   ::= !DISubrange(count: !node, lowerBound: 2)
///   ::= !DISubrange(lowerBound: !node1, upperBound: !node2, stride: !node3)
bool LLParser::parseDISubrange(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  OPTIONAL(count, MDSignedOrMDField, (-1, -1, INT64_MAX, false));              \
  OPTIONAL(lowerBound, MDSignedOrMDField, );                                   \
  OPTIONAL(upperBound, MDSignedOrMDField, );                                   \
  OPTIONAL(stride, MDSignedOrMDField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Metadata *Count = nullptr;
  Metadata *LowerBound = nullptr;
  Metadata *UpperBound = nullptr;
  Metadata *Stride = nullptr;

  auto convToMetadata = [&](MDSignedOrMDField Bound) -> Metadata * {
    if (Bound.isMDSignedField())
      return ConstantAsMetadata::get(ConstantInt::getSigned(
          Type::getInt64Ty(Context), Bound.getMDSignedValue()));
    if (Bound.isMDField())
      return Bound.getMDFieldValue();
    return nullptr;
  };

  Count = convToMetadata(count);
  LowerBound = convToMetadata(lowerBound);
  UpperBound = convToMetadata(upperBound);
  Stride = convToMetadata(stride);

  Result = GET_OR_DISTINCT(DISubrange,
                           (Context, Count, LowerBound, UpperBound, Stride));

  return false;
}

/// parseDIGenericSubrange:
///   ::= !DIGenericSubrange(lowerBound: !node1, upperBound: !node2, stride:
///   !node3)
bool LLParser::parseDIGenericSubrange(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  OPTIONAL(count, MDSignedOrMDField, );                                        \
  OPTIONAL(lowerBound, MDSignedOrMDField, );                                   \
  OPTIONAL(upperBound, MDSignedOrMDField, );                                   \
  OPTIONAL(stride, MDSignedOrMDField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  auto ConvToMetadata = [&](MDSignedOrMDField Bound) -> Metadata * {
    if (Bound.isMDSignedField())
      return DIExpression::get(
          Context, {dwarf::DW_OP_consts,
                    static_cast<uint64_t>(Bound.getMDSignedValue())});
    if (Bound.isMDField())
      return Bound.getMDFieldValue();
    return nullptr;
  };

  Metadata *Count = ConvToMetadata(count);
  Metadata *LowerBound = ConvToMetadata(lowerBound);
  Metadata *UpperBound = ConvToMetadata(upperBound);
  Metadata *Stride = ConvToMetadata(stride);

  Result = GET_OR_DISTINCT(DIGenericSubrange,
                           (Context, Count, LowerBound, UpperBound, Stride));

  return false;
}

/// parseDIEnumerator:
///   ::= !DIEnumerator(value: 30, isUnsigned: true, name: "SomeKind")
bool LLParser::parseDIEnumerator(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(name, MDStringField, );                                             \
  REQUIRED(value, MDAPSIntField, );                                            \
  OPTIONAL(isUnsigned, MDBoolField, (false));
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  if (isUnsigned.Val && value.Val.isNegative())
    return tokError("unsigned enumerator with negative value");

  APSInt Value(value.Val);
  // Add a leading zero so that unsigned values with the msb set are not
  // mistaken for negative values when used for signed enumerators.
  if (!isUnsigned.Val && value.Val.isUnsigned() && value.Val.isSignBitSet())
    Value = Value.zext(Value.getBitWidth() + 1);

  Result =
      GET_OR_DISTINCT(DIEnumerator, (Context, Value, isUnsigned.Val, name.Val));

  return false;
}

/// parseDIBasicType:
///   ::= !DIBasicType(tag: DW_TAG_base_type, name: "int", size: 32, align: 32,
///                    encoding: DW_ATE_encoding, flags: 0)
bool LLParser::parseDIBasicType(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  OPTIONAL(tag, DwarfTagField, (dwarf::DW_TAG_base_type));                     \
  OPTIONAL(name, MDStringField, );                                             \
  OPTIONAL(size, MDUnsignedField, (0, UINT64_MAX));                            \
  OPTIONAL(align, MDUnsignedField, (0, UINT32_MAX));                           \
  OPTIONAL(encoding, DwarfAttEncodingField, );                                 \
  OPTIONAL(flags, DIFlagField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DIBasicType, (Context, tag.Val, name.Val, size.Val,
                                         align.Val, encoding.Val, flags.Val));
  return false;
}

/// parseDIStringType:
///   ::= !DIStringType(name: "character(4)", size: 32, align: 32)
bool LLParser::parseDIStringType(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  OPTIONAL(tag, DwarfTagField, (dwarf::DW_TAG_string_type));                   \
  OPTIONAL(name, MDStringField, );                                             \
  OPTIONAL(stringLength, MDField, );                                           \
  OPTIONAL(stringLengthExpression, MDField, );                                 \
  OPTIONAL(stringLocationExpression, MDField, );                               \
  OPTIONAL(size, MDUnsignedField, (0, UINT64_MAX));                            \
  OPTIONAL(align, MDUnsignedField, (0, UINT32_MAX));                           \
  OPTIONAL(encoding, DwarfAttEncodingField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(
      DIStringType,
      (Context, tag.Val, name.Val, stringLength.Val, stringLengthExpression.Val,
       stringLocationExpression.Val, size.Val, align.Val, encoding.Val));
  return false;
}

/// parseDIDerivedType:
///   ::= !DIDerivedType(tag: DW_TAG_pointer_type, name: "int", file: !0,
///                      line: 7, scope: !1, baseType: !2, size: 32,
///                      align: 32, offset: 0, flags: 0, extraData: !3,
///                      dwarfAddressSpace: 3, ptrAuthKey: 1,
///                      ptrAuthIsAddressDiscriminated: true,
///                      ptrAuthExtraDiscriminator: 0x1234,
///                      ptrAuthIsaPointer: 1, ptrAuthAuthenticatesNullValues:1
///                      )
bool LLParser::parseDIDerivedType(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(tag, DwarfTagField, );                                              \
  OPTIONAL(name, MDStringField, );                                             \
  OPTIONAL(file, MDField, );                                                   \
  OPTIONAL(line, LineField, );                                                 \
  OPTIONAL(scope, MDField, );                                                  \
  REQUIRED(baseType, MDField, );                                               \
  OPTIONAL(size, MDUnsignedField, (0, UINT64_MAX));                            \
  OPTIONAL(align, MDUnsignedField, (0, UINT32_MAX));                           \
  OPTIONAL(offset, MDUnsignedField, (0, UINT64_MAX));                          \
  OPTIONAL(flags, DIFlagField, );                                              \
  OPTIONAL(extraData, MDField, );                                              \
  OPTIONAL(dwarfAddressSpace, MDUnsignedField, (UINT32_MAX, UINT32_MAX));      \
  OPTIONAL(annotations, MDField, );                                            \
  OPTIONAL(ptrAuthKey, MDUnsignedField, (0, 7));                               \
  OPTIONAL(ptrAuthIsAddressDiscriminated, MDBoolField, );                      \
  OPTIONAL(ptrAuthExtraDiscriminator, MDUnsignedField, (0, 0xffff));           \
  OPTIONAL(ptrAuthIsaPointer, MDBoolField, );                                  \
  OPTIONAL(ptrAuthAuthenticatesNullValues, MDBoolField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  std::optional<unsigned> DWARFAddressSpace;
  if (dwarfAddressSpace.Val != UINT32_MAX)
    DWARFAddressSpace = dwarfAddressSpace.Val;
  std::optional<DIDerivedType::PtrAuthData> PtrAuthData;
  if (ptrAuthKey.Val)
    PtrAuthData.emplace(
        (unsigned)ptrAuthKey.Val, ptrAuthIsAddressDiscriminated.Val,
        (unsigned)ptrAuthExtraDiscriminator.Val, ptrAuthIsaPointer.Val,
        ptrAuthAuthenticatesNullValues.Val);

  Result = GET_OR_DISTINCT(DIDerivedType,
                           (Context, tag.Val, name.Val, file.Val, line.Val,
                            scope.Val, baseType.Val, size.Val, align.Val,
                            offset.Val, DWARFAddressSpace, PtrAuthData,
                            flags.Val, extraData.Val, annotations.Val));
  return false;
}

bool LLParser::parseDICompositeType(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(tag, DwarfTagField, );                                              \
  OPTIONAL(name, MDStringField, );                                             \
  OPTIONAL(file, MDField, );                                                   \
  OPTIONAL(line, LineField, );                                                 \
  OPTIONAL(scope, MDField, );                                                  \
  OPTIONAL(baseType, MDField, );                                               \
  OPTIONAL(size, MDUnsignedField, (0, UINT64_MAX));                            \
  OPTIONAL(align, MDUnsignedField, (0, UINT32_MAX));                           \
  OPTIONAL(offset, MDUnsignedField, (0, UINT64_MAX));                          \
  OPTIONAL(flags, DIFlagField, );                                              \
  OPTIONAL(elements, MDField, );                                               \
  OPTIONAL(runtimeLang, DwarfLangField, );                                     \
  OPTIONAL(vtableHolder, MDField, );                                           \
  OPTIONAL(templateParams, MDField, );                                         \
  OPTIONAL(identifier, MDStringField, );                                       \
  OPTIONAL(discriminator, MDField, );                                          \
  OPTIONAL(dataLocation, MDField, );                                           \
  OPTIONAL(associated, MDField, );                                             \
  OPTIONAL(allocated, MDField, );                                              \
  OPTIONAL(rank, MDSignedOrMDField, );                                         \
  OPTIONAL(annotations, MDField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Metadata *Rank = nullptr;
  if (rank.isMDSignedField())
    Rank = ConstantAsMetadata::get(ConstantInt::getSigned(
        Type::getInt64Ty(Context), rank.getMDSignedValue()));
  else if (rank.isMDField())
    Rank = rank.getMDFieldValue();

  // If this has an identifier try to build an ODR type.
  if (identifier.Val)
    if (auto *CT = DICompositeType::buildODRType(
            Context, *identifier.Val, tag.Val, name.Val, file.Val, line.Val,
            scope.Val, baseType.Val, size.Val, align.Val, offset.Val, flags.Val,
            elements.Val, runtimeLang.Val, vtableHolder.Val, templateParams.Val,
            discriminator.Val, dataLocation.Val, associated.Val, allocated.Val,
            Rank, annotations.Val)) {
      Result = CT;
      return false;
    }

  // Create a new node, and save it in the context if it belongs in the type
  // map.
  Result = GET_OR_DISTINCT(
      DICompositeType,
      (Context, tag.Val, name.Val, file.Val, line.Val, scope.Val, baseType.Val,
       size.Val, align.Val, offset.Val, flags.Val, elements.Val,
       runtimeLang.Val, vtableHolder.Val, templateParams.Val, identifier.Val,
       discriminator.Val, dataLocation.Val, associated.Val, allocated.Val, Rank,
       annotations.Val));
  return false;
}

bool LLParser::parseDISubroutineType(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  OPTIONAL(flags, DIFlagField, );                                              \
  OPTIONAL(cc, DwarfCCField, );                                                \
  REQUIRED(types, MDField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DISubroutineType,
                           (Context, flags.Val, cc.Val, types.Val));
  return false;
}

/// parseDIFileType:
///   ::= !DIFileType(filename: "path/to/file", directory: "/path/to/dir",
///                   checksumkind: CSK_MD5,
///                   checksum: "000102030405060708090a0b0c0d0e0f",
///                   source: "source file contents")
bool LLParser::parseDIFile(MDNode *&Result, bool IsDistinct) {
  // The default constructed value for checksumkind is required, but will never
  // be used, as the parser checks if the field was actually Seen before using
  // the Val.
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(filename, MDStringField, );                                         \
  REQUIRED(directory, MDStringField, );                                        \
  OPTIONAL(checksumkind, ChecksumKindField, (DIFile::CSK_MD5));                \
  OPTIONAL(checksum, MDStringField, );                                         \
  OPTIONAL(source, MDStringField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  std::optional<DIFile::ChecksumInfo<MDString *>> OptChecksum;
  if (checksumkind.Seen && checksum.Seen)
    OptChecksum.emplace(checksumkind.Val, checksum.Val);
  else if (checksumkind.Seen || checksum.Seen)
    return Lex.Error("'checksumkind' and 'checksum' must be provided together");

  MDString *Source = nullptr;
  if (source.Seen)
    Source = source.Val;
  Result = GET_OR_DISTINCT(
      DIFile, (Context, filename.Val, directory.Val, OptChecksum, Source));
  return false;
}

/// parseDICompileUnit:
///   ::= !DICompileUnit(language: DW_LANG_C99, file: !0, producer: "clang",
///                      isOptimized: true, flags: "-O2", runtimeVersion: 1,
///                      splitDebugFilename: "abc.debug",
///                      emissionKind: FullDebug, enums: !1, retainedTypes: !2,
///                      globals: !4, imports: !5, macros: !6, dwoId: 0x0abcd,
///                      sysroot: "/", sdk: "MacOSX.sdk")
bool LLParser::parseDICompileUnit(MDNode *&Result, bool IsDistinct) {
  if (!IsDistinct)
    return Lex.Error("missing 'distinct', required for !DICompileUnit");

#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(language, DwarfLangField, );                                        \
  REQUIRED(file, MDField, (/* AllowNull */ false));                            \
  OPTIONAL(producer, MDStringField, );                                         \
  OPTIONAL(isOptimized, MDBoolField, );                                        \
  OPTIONAL(flags, MDStringField, );                                            \
  OPTIONAL(runtimeVersion, MDUnsignedField, (0, UINT32_MAX));                  \
  OPTIONAL(splitDebugFilename, MDStringField, );                               \
  OPTIONAL(emissionKind, EmissionKindField, );                                 \
  OPTIONAL(enums, MDField, );                                                  \
  OPTIONAL(retainedTypes, MDField, );                                          \
  OPTIONAL(globals, MDField, );                                                \
  OPTIONAL(imports, MDField, );                                                \
  OPTIONAL(macros, MDField, );                                                 \
  OPTIONAL(dwoId, MDUnsignedField, );                                          \
  OPTIONAL(splitDebugInlining, MDBoolField, = true);                           \
  OPTIONAL(debugInfoForProfiling, MDBoolField, = false);                       \
  OPTIONAL(nameTableKind, NameTableKindField, );                               \
  OPTIONAL(rangesBaseAddress, MDBoolField, = false);                           \
  OPTIONAL(sysroot, MDStringField, );                                          \
  OPTIONAL(sdk, MDStringField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = DICompileUnit::getDistinct(
      Context, language.Val, file.Val, producer.Val, isOptimized.Val, flags.Val,
      runtimeVersion.Val, splitDebugFilename.Val, emissionKind.Val, enums.Val,
      retainedTypes.Val, globals.Val, imports.Val, macros.Val, dwoId.Val,
      splitDebugInlining.Val, debugInfoForProfiling.Val, nameTableKind.Val,
      rangesBaseAddress.Val, sysroot.Val, sdk.Val);
  return false;
}

/// parseDISubprogram:
///   ::= !DISubprogram(scope: !0, name: "foo", linkageName: "_Zfoo",
///                     file: !1, line: 7, type: !2, isLocal: false,
///                     isDefinition: true, scopeLine: 8, containingType: !3,
///                     virtuality: DW_VIRTUALTIY_pure_virtual,
///                     virtualIndex: 10, thisAdjustment: 4, flags: 11,
///                     spFlags: 10, isOptimized: false, templateParams: !4,
///                     declaration: !5, retainedNodes: !6, thrownTypes: !7,
///                     annotations: !8)
bool LLParser::parseDISubprogram(MDNode *&Result, bool IsDistinct) {
  auto Loc = Lex.getLoc();
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  OPTIONAL(scope, MDField, );                                                  \
  OPTIONAL(name, MDStringField, );                                             \
  OPTIONAL(linkageName, MDStringField, );                                      \
  OPTIONAL(file, MDField, );                                                   \
  OPTIONAL(line, LineField, );                                                 \
  OPTIONAL(type, MDField, );                                                   \
  OPTIONAL(isLocal, MDBoolField, );                                            \
  OPTIONAL(isDefinition, MDBoolField, (true));                                 \
  OPTIONAL(scopeLine, LineField, );                                            \
  OPTIONAL(containingType, MDField, );                                         \
  OPTIONAL(virtuality, DwarfVirtualityField, );                                \
  OPTIONAL(virtualIndex, MDUnsignedField, (0, UINT32_MAX));                    \
  OPTIONAL(thisAdjustment, MDSignedField, (0, INT32_MIN, INT32_MAX));          \
  OPTIONAL(flags, DIFlagField, );                                              \
  OPTIONAL(spFlags, DISPFlagField, );                                          \
  OPTIONAL(isOptimized, MDBoolField, );                                        \
  OPTIONAL(unit, MDField, );                                                   \
  OPTIONAL(templateParams, MDField, );                                         \
  OPTIONAL(declaration, MDField, );                                            \
  OPTIONAL(retainedNodes, MDField, );                                          \
  OPTIONAL(thrownTypes, MDField, );                                            \
  OPTIONAL(annotations, MDField, );                                            \
  OPTIONAL(targetFuncName, MDStringField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  // An explicit spFlags field takes precedence over individual fields in
  // older IR versions.
  DISubprogram::DISPFlags SPFlags =
      spFlags.Seen ? spFlags.Val
                   : DISubprogram::toSPFlags(isLocal.Val, isDefinition.Val,
                                             isOptimized.Val, virtuality.Val);
  if ((SPFlags & DISubprogram::SPFlagDefinition) && !IsDistinct)
    return Lex.Error(
        Loc,
        "missing 'distinct', required for !DISubprogram that is a Definition");
  Result = GET_OR_DISTINCT(
      DISubprogram,
      (Context, scope.Val, name.Val, linkageName.Val, file.Val, line.Val,
       type.Val, scopeLine.Val, containingType.Val, virtualIndex.Val,
       thisAdjustment.Val, flags.Val, SPFlags, unit.Val, templateParams.Val,
       declaration.Val, retainedNodes.Val, thrownTypes.Val, annotations.Val,
       targetFuncName.Val));
  return false;
}

/// parseDILexicalBlock:
///   ::= !DILexicalBlock(scope: !0, file: !2, line: 7, column: 9)
bool LLParser::parseDILexicalBlock(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(scope, MDField, (/* AllowNull */ false));                           \
  OPTIONAL(file, MDField, );                                                   \
  OPTIONAL(line, LineField, );                                                 \
  OPTIONAL(column, ColumnField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(
      DILexicalBlock, (Context, scope.Val, file.Val, line.Val, column.Val));
  return false;
}

/// parseDILexicalBlockFile:
///   ::= !DILexicalBlockFile(scope: !0, file: !2, discriminator: 9)
bool LLParser::parseDILexicalBlockFile(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(scope, MDField, (/* AllowNull */ false));                           \
  OPTIONAL(file, MDField, );                                                   \
  REQUIRED(discriminator, MDUnsignedField, (0, UINT32_MAX));
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DILexicalBlockFile,
                           (Context, scope.Val, file.Val, discriminator.Val));
  return false;
}

/// parseDICommonBlock:
///   ::= !DICommonBlock(scope: !0, file: !2, name: "COMMON name", line: 9)
bool LLParser::parseDICommonBlock(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(scope, MDField, );                                                  \
  OPTIONAL(declaration, MDField, );                                            \
  OPTIONAL(name, MDStringField, );                                             \
  OPTIONAL(file, MDField, );                                                   \
  OPTIONAL(line, LineField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DICommonBlock,
                           (Context, scope.Val, declaration.Val, name.Val,
                            file.Val, line.Val));
  return false;
}

/// parseDINamespace:
///   ::= !DINamespace(scope: !0, file: !2, name: "SomeNamespace", line: 9)
bool LLParser::parseDINamespace(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(scope, MDField, );                                                  \
  OPTIONAL(name, MDStringField, );                                             \
  OPTIONAL(exportSymbols, MDBoolField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DINamespace,
                           (Context, scope.Val, name.Val, exportSymbols.Val));
  return false;
}

/// parseDIMacro:
///   ::= !DIMacro(macinfo: type, line: 9, name: "SomeMacro", value:
///   "SomeValue")
bool LLParser::parseDIMacro(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(type, DwarfMacinfoTypeField, );                                     \
  OPTIONAL(line, LineField, );                                                 \
  REQUIRED(name, MDStringField, );                                             \
  OPTIONAL(value, MDStringField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DIMacro,
                           (Context, type.Val, line.Val, name.Val, value.Val));
  return false;
}

/// parseDIMacroFile:
///   ::= !DIMacroFile(line: 9, file: !2, nodes: !3)
bool LLParser::parseDIMacroFile(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  OPTIONAL(type, DwarfMacinfoTypeField, (dwarf::DW_MACINFO_start_file));       \
  OPTIONAL(line, LineField, );                                                 \
  REQUIRED(file, MDField, );                                                   \
  OPTIONAL(nodes, MDField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DIMacroFile,
                           (Context, type.Val, line.Val, file.Val, nodes.Val));
  return false;
}

/// parseDIModule:
///   ::= !DIModule(scope: !0, name: "SomeModule", configMacros:
///   "-DNDEBUG", includePath: "/usr/include", apinotes: "module.apinotes",
///   file: !1, line: 4, isDecl: false)
bool LLParser::parseDIModule(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(scope, MDField, );                                                  \
  REQUIRED(name, MDStringField, );                                             \
  OPTIONAL(configMacros, MDStringField, );                                     \
  OPTIONAL(includePath, MDStringField, );                                      \
  OPTIONAL(apinotes, MDStringField, );                                         \
  OPTIONAL(file, MDField, );                                                   \
  OPTIONAL(line, LineField, );                                                 \
  OPTIONAL(isDecl, MDBoolField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DIModule, (Context, file.Val, scope.Val, name.Val,
                                      configMacros.Val, includePath.Val,
                                      apinotes.Val, line.Val, isDecl.Val));
  return false;
}

/// parseDITemplateTypeParameter:
///   ::= !DITemplateTypeParameter(name: "Ty", type: !1, defaulted: false)
bool LLParser::parseDITemplateTypeParameter(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  OPTIONAL(name, MDStringField, );                                             \
  REQUIRED(type, MDField, );                                                   \
  OPTIONAL(defaulted, MDBoolField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DITemplateTypeParameter,
                           (Context, name.Val, type.Val, defaulted.Val));
  return false;
}

/// parseDITemplateValueParameter:
///   ::= !DITemplateValueParameter(tag: DW_TAG_template_value_parameter,
///                                 name: "V", type: !1, defaulted: false,
///                                 value: i32 7)
bool LLParser::parseDITemplateValueParameter(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  OPTIONAL(tag, DwarfTagField, (dwarf::DW_TAG_template_value_parameter));      \
  OPTIONAL(name, MDStringField, );                                             \
  OPTIONAL(type, MDField, );                                                   \
  OPTIONAL(defaulted, MDBoolField, );                                          \
  REQUIRED(value, MDField, );

  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(
      DITemplateValueParameter,
      (Context, tag.Val, name.Val, type.Val, defaulted.Val, value.Val));
  return false;
}

/// parseDIGlobalVariable:
///   ::= !DIGlobalVariable(scope: !0, name: "foo", linkageName: "foo",
///                         file: !1, line: 7, type: !2, isLocal: false,
///                         isDefinition: true, templateParams: !3,
///                         declaration: !4, align: 8)
bool LLParser::parseDIGlobalVariable(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  OPTIONAL(name, MDStringField, (/* AllowEmpty */ false));                     \
  OPTIONAL(scope, MDField, );                                                  \
  OPTIONAL(linkageName, MDStringField, );                                      \
  OPTIONAL(file, MDField, );                                                   \
  OPTIONAL(line, LineField, );                                                 \
  OPTIONAL(type, MDField, );                                                   \
  OPTIONAL(isLocal, MDBoolField, );                                            \
  OPTIONAL(isDefinition, MDBoolField, (true));                                 \
  OPTIONAL(templateParams, MDField, );                                         \
  OPTIONAL(declaration, MDField, );                                            \
  OPTIONAL(align, MDUnsignedField, (0, UINT32_MAX));                           \
  OPTIONAL(annotations, MDField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result =
      GET_OR_DISTINCT(DIGlobalVariable,
                      (Context, scope.Val, name.Val, linkageName.Val, file.Val,
                       line.Val, type.Val, isLocal.Val, isDefinition.Val,
                       declaration.Val, templateParams.Val, align.Val,
                       annotations.Val));
  return false;
}

/// parseDILocalVariable:
///   ::= !DILocalVariable(arg: 7, scope: !0, name: "foo",
///                        file: !1, line: 7, type: !2, arg: 2, flags: 7,
///                        align: 8)
///   ::= !DILocalVariable(scope: !0, name: "foo",
///                        file: !1, line: 7, type: !2, arg: 2, flags: 7,
///                        align: 8)
bool LLParser::parseDILocalVariable(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(scope, MDField, (/* AllowNull */ false));                           \
  OPTIONAL(name, MDStringField, );                                             \
  OPTIONAL(arg, MDUnsignedField, (0, UINT16_MAX));                             \
  OPTIONAL(file, MDField, );                                                   \
  OPTIONAL(line, LineField, );                                                 \
  OPTIONAL(type, MDField, );                                                   \
  OPTIONAL(flags, DIFlagField, );                                              \
  OPTIONAL(align, MDUnsignedField, (0, UINT32_MAX));                           \
  OPTIONAL(annotations, MDField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DILocalVariable,
                           (Context, scope.Val, name.Val, file.Val, line.Val,
                            type.Val, arg.Val, flags.Val, align.Val,
                            annotations.Val));
  return false;
}

/// parseDILabel:
///   ::= !DILabel(scope: !0, name: "foo", file: !1, line: 7)
bool LLParser::parseDILabel(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(scope, MDField, (/* AllowNull */ false));                           \
  REQUIRED(name, MDStringField, );                                             \
  REQUIRED(file, MDField, );                                                   \
  REQUIRED(line, LineField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DILabel,
                           (Context, scope.Val, name.Val, file.Val, line.Val));
  return false;
}

/// parseDIExpressionBody:
///   ::= (0, 7, -1)
bool LLParser::parseDIExpressionBody(MDNode *&Result, bool IsDistinct) {
  if (parseToken(lltok::lparen, "expected '(' here"))
    return true;

  SmallVector<uint64_t, 8> Elements;
  if (Lex.getKind() != lltok::rparen)
    do {
      if (Lex.getKind() == lltok::DwarfOp) {
        if (unsigned Op = dwarf::getOperationEncoding(Lex.getStrVal())) {
          Lex.Lex();
          Elements.push_back(Op);
          continue;
        }
        return tokError(Twine("invalid DWARF op '") + Lex.getStrVal() + "'");
      }

      if (Lex.getKind() == lltok::DwarfAttEncoding) {
        if (unsigned Op = dwarf::getAttributeEncoding(Lex.getStrVal())) {
          Lex.Lex();
          Elements.push_back(Op);
          continue;
        }
        return tokError(Twine("invalid DWARF attribute encoding '") +
                        Lex.getStrVal() + "'");
      }

      if (Lex.getKind() != lltok::APSInt || Lex.getAPSIntVal().isSigned())
        return tokError("expected unsigned integer");

      auto &U = Lex.getAPSIntVal();
      if (U.ugt(UINT64_MAX))
        return tokError("element too large, limit is " + Twine(UINT64_MAX));
      Elements.push_back(U.getZExtValue());
      Lex.Lex();
    } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  Result = GET_OR_DISTINCT(DIExpression, (Context, Elements));
  return false;
}

/// parseDIExpression:
///   ::= !DIExpression(0, 7, -1)
bool LLParser::parseDIExpression(MDNode *&Result, bool IsDistinct) {
  assert(Lex.getKind() == lltok::MetadataVar && "Expected metadata type name");
  assert(Lex.getStrVal() == "DIExpression" && "Expected '!DIExpression'");
  Lex.Lex();

  return parseDIExpressionBody(Result, IsDistinct);
}

/// ParseDIArgList:
///   ::= !DIArgList(i32 7, i64 %0)
bool LLParser::parseDIArgList(Metadata *&MD, PerFunctionState *PFS) {
  assert(PFS && "Expected valid function state");
  assert(Lex.getKind() == lltok::MetadataVar && "Expected metadata type name");
  Lex.Lex();

  if (parseToken(lltok::lparen, "expected '(' here"))
    return true;

  SmallVector<ValueAsMetadata *, 4> Args;
  if (Lex.getKind() != lltok::rparen)
    do {
      Metadata *MD;
      if (parseValueAsMetadata(MD, "expected value-as-metadata operand", PFS))
        return true;
      Args.push_back(dyn_cast<ValueAsMetadata>(MD));
    } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  MD = DIArgList::get(Context, Args);
  return false;
}

/// parseDIGlobalVariableExpression:
///   ::= !DIGlobalVariableExpression(var: !0, expr: !1)
bool LLParser::parseDIGlobalVariableExpression(MDNode *&Result,
                                               bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(var, MDField, );                                                    \
  REQUIRED(expr, MDField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result =
      GET_OR_DISTINCT(DIGlobalVariableExpression, (Context, var.Val, expr.Val));
  return false;
}

/// parseDIObjCProperty:
///   ::= !DIObjCProperty(name: "foo", file: !1, line: 7, setter: "setFoo",
///                       getter: "getFoo", attributes: 7, type: !2)
bool LLParser::parseDIObjCProperty(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  OPTIONAL(name, MDStringField, );                                             \
  OPTIONAL(file, MDField, );                                                   \
  OPTIONAL(line, LineField, );                                                 \
  OPTIONAL(setter, MDStringField, );                                           \
  OPTIONAL(getter, MDStringField, );                                           \
  OPTIONAL(attributes, MDUnsignedField, (0, UINT32_MAX));                      \
  OPTIONAL(type, MDField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DIObjCProperty,
                           (Context, name.Val, file.Val, line.Val, setter.Val,
                            getter.Val, attributes.Val, type.Val));
  return false;
}

/// parseDIImportedEntity:
///   ::= !DIImportedEntity(tag: DW_TAG_imported_module, scope: !0, entity: !1,
///                         line: 7, name: "foo", elements: !2)
bool LLParser::parseDIImportedEntity(MDNode *&Result, bool IsDistinct) {
#define VISIT_MD_FIELDS(OPTIONAL, REQUIRED)                                    \
  REQUIRED(tag, DwarfTagField, );                                              \
  REQUIRED(scope, MDField, );                                                  \
  OPTIONAL(entity, MDField, );                                                 \
  OPTIONAL(file, MDField, );                                                   \
  OPTIONAL(line, LineField, );                                                 \
  OPTIONAL(name, MDStringField, );                                             \
  OPTIONAL(elements, MDField, );
  PARSE_MD_FIELDS();
#undef VISIT_MD_FIELDS

  Result = GET_OR_DISTINCT(DIImportedEntity,
                           (Context, tag.Val, scope.Val, entity.Val, file.Val,
                            line.Val, name.Val, elements.Val));
  return false;
}

#undef PARSE_MD_FIELD
#undef NOP_FIELD
#undef REQUIRE_FIELD
#undef DECLARE_FIELD

/// parseMetadataAsValue
///  ::= metadata i32 %local
///  ::= metadata i32 @global
///  ::= metadata i32 7
///  ::= metadata !0
///  ::= metadata !{...}
///  ::= metadata !"string"
bool LLParser::parseMetadataAsValue(Value *&V, PerFunctionState &PFS) {
  // Note: the type 'metadata' has already been parsed.
  Metadata *MD;
  if (parseMetadata(MD, &PFS))
    return true;

  V = MetadataAsValue::get(Context, MD);
  return false;
}

/// parseValueAsMetadata
///  ::= i32 %local
///  ::= i32 @global
///  ::= i32 7
bool LLParser::parseValueAsMetadata(Metadata *&MD, const Twine &TypeMsg,
                                    PerFunctionState *PFS) {
  Type *Ty;
  LocTy Loc;
  if (parseType(Ty, TypeMsg, Loc))
    return true;
  if (Ty->isMetadataTy())
    return error(Loc, "invalid metadata-value-metadata roundtrip");

  Value *V;
  if (parseValue(Ty, V, PFS))
    return true;

  MD = ValueAsMetadata::get(V);
  return false;
}

/// parseMetadata
///  ::= i32 %local
///  ::= i32 @global
///  ::= i32 7
///  ::= !42
///  ::= !{...}
///  ::= !"string"
///  ::= !DILocation(...)
bool LLParser::parseMetadata(Metadata *&MD, PerFunctionState *PFS) {
  if (Lex.getKind() == lltok::MetadataVar) {
    // DIArgLists are a special case, as they are a list of ValueAsMetadata and
    // so parsing this requires a Function State.
    if (Lex.getStrVal() == "DIArgList") {
      Metadata *AL;
      if (parseDIArgList(AL, PFS))
        return true;
      MD = AL;
      return false;
    }
    MDNode *N;
    if (parseSpecializedMDNode(N)) {
      return true;
    }
    MD = N;
    return false;
  }

  // ValueAsMetadata:
  // <type> <value>
  if (Lex.getKind() != lltok::exclaim)
    return parseValueAsMetadata(MD, "expected metadata operand", PFS);

  // '!'.
  assert(Lex.getKind() == lltok::exclaim && "Expected '!' here");
  Lex.Lex();

  // MDString:
  //   ::= '!' STRINGCONSTANT
  if (Lex.getKind() == lltok::StringConstant) {
    MDString *S;
    if (parseMDString(S))
      return true;
    MD = S;
    return false;
  }

  // MDNode:
  // !{ ... }
  // !7
  MDNode *N;
  if (parseMDNodeTail(N))
    return true;
  MD = N;
  return false;
}

//===----------------------------------------------------------------------===//
// Function Parsing.
//===----------------------------------------------------------------------===//

bool LLParser::convertValIDToValue(Type *Ty, ValID &ID, Value *&V,
                                   PerFunctionState *PFS) {
  if (Ty->isFunctionTy())
    return error(ID.Loc, "functions are not values, refer to them as pointers");

  switch (ID.Kind) {
  case ValID::t_LocalID:
    if (!PFS)
      return error(ID.Loc, "invalid use of function-local name");
    V = PFS->getVal(ID.UIntVal, Ty, ID.Loc);
    return V == nullptr;
  case ValID::t_LocalName:
    if (!PFS)
      return error(ID.Loc, "invalid use of function-local name");
    V = PFS->getVal(ID.StrVal, Ty, ID.Loc);
    return V == nullptr;
  case ValID::t_InlineAsm: {
    if (!ID.FTy)
      return error(ID.Loc, "invalid type for inline asm constraint string");
    if (Error Err = InlineAsm::verify(ID.FTy, ID.StrVal2))
      return error(ID.Loc, toString(std::move(Err)));
    V = InlineAsm::get(
        ID.FTy, ID.StrVal, ID.StrVal2, ID.UIntVal & 1, (ID.UIntVal >> 1) & 1,
        InlineAsm::AsmDialect((ID.UIntVal >> 2) & 1), (ID.UIntVal >> 3) & 1);
    return false;
  }
  case ValID::t_GlobalName:
    V = getGlobalVal(ID.StrVal, Ty, ID.Loc);
    if (V && ID.NoCFI)
      V = NoCFIValue::get(cast<GlobalValue>(V));
    return V == nullptr;
  case ValID::t_GlobalID:
    V = getGlobalVal(ID.UIntVal, Ty, ID.Loc);
    if (V && ID.NoCFI)
      V = NoCFIValue::get(cast<GlobalValue>(V));
    return V == nullptr;
  case ValID::t_APSInt:
    if (!Ty->isIntegerTy())
      return error(ID.Loc, "integer constant must have integer type");
    ID.APSIntVal = ID.APSIntVal.extOrTrunc(Ty->getPrimitiveSizeInBits());
    V = ConstantInt::get(Context, ID.APSIntVal);
    return false;
  case ValID::t_APFloat:
    if (!Ty->isFloatingPointTy() ||
        !ConstantFP::isValueValidForType(Ty, ID.APFloatVal))
      return error(ID.Loc, "floating point constant invalid for type");

    // The lexer has no type info, so builds all half, bfloat, float, and double
    // FP constants as double.  Fix this here.  Long double does not need this.
    if (&ID.APFloatVal.getSemantics() == &APFloat::IEEEdouble()) {
      // Check for signaling before potentially converting and losing that info.
      bool IsSNAN = ID.APFloatVal.isSignaling();
      bool Ignored;
      if (Ty->isHalfTy())
        ID.APFloatVal.convert(APFloat::IEEEhalf(), APFloat::rmNearestTiesToEven,
                              &Ignored);
      else if (Ty->isBFloatTy())
        ID.APFloatVal.convert(APFloat::BFloat(), APFloat::rmNearestTiesToEven,
                              &Ignored);
      else if (Ty->isFloatTy())
        ID.APFloatVal.convert(APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven,
                              &Ignored);
      if (IsSNAN) {
        // The convert call above may quiet an SNaN, so manufacture another
        // SNaN. The bitcast works because the payload (significand) parameter
        // is truncated to fit.
        APInt Payload = ID.APFloatVal.bitcastToAPInt();
        ID.APFloatVal = APFloat::getSNaN(ID.APFloatVal.getSemantics(),
                                         ID.APFloatVal.isNegative(), &Payload);
      }
    }
    V = ConstantFP::get(Context, ID.APFloatVal);

    if (V->getType() != Ty)
      return error(ID.Loc, "floating point constant does not have type '" +
                               getTypeString(Ty) + "'");

    return false;
  case ValID::t_Null:
    if (!Ty->isPointerTy())
      return error(ID.Loc, "null must be a pointer type");
    V = ConstantPointerNull::get(cast<PointerType>(Ty));
    return false;
  case ValID::t_Undef:
    // FIXME: LabelTy should not be a first-class type.
    if (!Ty->isFirstClassType() || Ty->isLabelTy())
      return error(ID.Loc, "invalid type for undef constant");
    V = UndefValue::get(Ty);
    return false;
  case ValID::t_EmptyArray:
    if (!Ty->isArrayTy() || cast<ArrayType>(Ty)->getNumElements() != 0)
      return error(ID.Loc, "invalid empty array initializer");
    V = UndefValue::get(Ty);
    return false;
  case ValID::t_Zero:
    // FIXME: LabelTy should not be a first-class type.
    if (!Ty->isFirstClassType() || Ty->isLabelTy())
      return error(ID.Loc, "invalid type for null constant");
    if (auto *TETy = dyn_cast<TargetExtType>(Ty))
      if (!TETy->hasProperty(TargetExtType::HasZeroInit))
        return error(ID.Loc, "invalid type for null constant");
    V = Constant::getNullValue(Ty);
    return false;
  case ValID::t_None:
    if (!Ty->isTokenTy())
      return error(ID.Loc, "invalid type for none constant");
    V = Constant::getNullValue(Ty);
    return false;
  case ValID::t_Poison:
    // FIXME: LabelTy should not be a first-class type.
    if (!Ty->isFirstClassType() || Ty->isLabelTy())
      return error(ID.Loc, "invalid type for poison constant");
    V = PoisonValue::get(Ty);
    return false;
  case ValID::t_Constant:
    if (ID.ConstantVal->getType() != Ty)
      return error(ID.Loc, "constant expression type mismatch: got type '" +
                               getTypeString(ID.ConstantVal->getType()) +
                               "' but expected '" + getTypeString(Ty) + "'");
    V = ID.ConstantVal;
    return false;
  case ValID::t_ConstantSplat:
    if (!Ty->isVectorTy())
      return error(ID.Loc, "vector constant must have vector type");
    if (ID.ConstantVal->getType() != Ty->getScalarType())
      return error(ID.Loc, "constant expression type mismatch: got type '" +
                               getTypeString(ID.ConstantVal->getType()) +
                               "' but expected '" +
                               getTypeString(Ty->getScalarType()) + "'");
    V = ConstantVector::getSplat(cast<VectorType>(Ty)->getElementCount(),
                                 ID.ConstantVal);
    return false;
  case ValID::t_ConstantStruct:
  case ValID::t_PackedConstantStruct:
    if (StructType *ST = dyn_cast<StructType>(Ty)) {
      if (ST->getNumElements() != ID.UIntVal)
        return error(ID.Loc,
                     "initializer with struct type has wrong # elements");
      if (ST->isPacked() != (ID.Kind == ValID::t_PackedConstantStruct))
        return error(ID.Loc, "packed'ness of initializer and type don't match");

      // Verify that the elements are compatible with the structtype.
      for (unsigned i = 0, e = ID.UIntVal; i != e; ++i)
        if (ID.ConstantStructElts[i]->getType() != ST->getElementType(i))
          return error(
              ID.Loc,
              "element " + Twine(i) +
                  " of struct initializer doesn't match struct element type");

      V = ConstantStruct::get(
          ST, ArrayRef(ID.ConstantStructElts.get(), ID.UIntVal));
    } else
      return error(ID.Loc, "constant expression type mismatch");
    return false;
  }
  llvm_unreachable("Invalid ValID");
}

bool LLParser::parseConstantValue(Type *Ty, Constant *&C) {
  C = nullptr;
  ValID ID;
  auto Loc = Lex.getLoc();
  if (parseValID(ID, /*PFS=*/nullptr))
    return true;
  switch (ID.Kind) {
  case ValID::t_APSInt:
  case ValID::t_APFloat:
  case ValID::t_Undef:
  case ValID::t_Constant:
  case ValID::t_ConstantSplat:
  case ValID::t_ConstantStruct:
  case ValID::t_PackedConstantStruct: {
    Value *V;
    if (convertValIDToValue(Ty, ID, V, /*PFS=*/nullptr))
      return true;
    assert(isa<Constant>(V) && "Expected a constant value");
    C = cast<Constant>(V);
    return false;
  }
  case ValID::t_Null:
    C = Constant::getNullValue(Ty);
    return false;
  default:
    return error(Loc, "expected a constant value");
  }
}

bool LLParser::parseValue(Type *Ty, Value *&V, PerFunctionState *PFS) {
  V = nullptr;
  ValID ID;
  return parseValID(ID, PFS, Ty) ||
         convertValIDToValue(Ty, ID, V, PFS);
}

bool LLParser::parseTypeAndValue(Value *&V, PerFunctionState *PFS) {
  Type *Ty = nullptr;
  return parseType(Ty) || parseValue(Ty, V, PFS);
}

bool LLParser::parseTypeAndBasicBlock(BasicBlock *&BB, LocTy &Loc,
                                      PerFunctionState &PFS) {
  Value *V;
  Loc = Lex.getLoc();
  if (parseTypeAndValue(V, PFS))
    return true;
  if (!isa<BasicBlock>(V))
    return error(Loc, "expected a basic block");
  BB = cast<BasicBlock>(V);
  return false;
}

bool isOldDbgFormatIntrinsic(StringRef Name) {
  // Exit early for the common (non-debug-intrinsic) case.
  // We can make this the only check when we begin supporting all "llvm.dbg"
  // intrinsics in the new debug info format.
  if (!Name.starts_with("llvm.dbg."))
    return false;
  Intrinsic::ID FnID = Function::lookupIntrinsicID(Name);
  return FnID == Intrinsic::dbg_declare || FnID == Intrinsic::dbg_value ||
         FnID == Intrinsic::dbg_assign;
}

/// FunctionHeader
///   ::= OptionalLinkage OptionalPreemptionSpecifier OptionalVisibility
///       OptionalCallingConv OptRetAttrs OptUnnamedAddr Type GlobalName
///       '(' ArgList ')' OptAddrSpace OptFuncAttrs OptSection OptionalAlign
///       OptGC OptionalPrefix OptionalPrologue OptPersonalityFn
bool LLParser::parseFunctionHeader(Function *&Fn, bool IsDefine,
                                   unsigned &FunctionNumber,
                                   SmallVectorImpl<unsigned> &UnnamedArgNums) {
  // parse the linkage.
  LocTy LinkageLoc = Lex.getLoc();
  unsigned Linkage;
  unsigned Visibility;
  unsigned DLLStorageClass;
  bool DSOLocal;
  AttrBuilder RetAttrs(M->getContext());
  unsigned CC;
  bool HasLinkage;
  Type *RetType = nullptr;
  LocTy RetTypeLoc = Lex.getLoc();
  if (parseOptionalLinkage(Linkage, HasLinkage, Visibility, DLLStorageClass,
                           DSOLocal) ||
      parseOptionalCallingConv(CC) || parseOptionalReturnAttrs(RetAttrs) ||
      parseType(RetType, RetTypeLoc, true /*void allowed*/))
    return true;

  // Verify that the linkage is ok.
  switch ((GlobalValue::LinkageTypes)Linkage) {
  case GlobalValue::ExternalLinkage:
    break; // always ok.
  case GlobalValue::ExternalWeakLinkage:
    if (IsDefine)
      return error(LinkageLoc, "invalid linkage for function definition");
    break;
  case GlobalValue::PrivateLinkage:
  case GlobalValue::InternalLinkage:
  case GlobalValue::AvailableExternallyLinkage:
  case GlobalValue::LinkOnceAnyLinkage:
  case GlobalValue::LinkOnceODRLinkage:
  case GlobalValue::WeakAnyLinkage:
  case GlobalValue::WeakODRLinkage:
    if (!IsDefine)
      return error(LinkageLoc, "invalid linkage for function declaration");
    break;
  case GlobalValue::AppendingLinkage:
  case GlobalValue::CommonLinkage:
    return error(LinkageLoc, "invalid function linkage type");
  }

  if (!isValidVisibilityForLinkage(Visibility, Linkage))
    return error(LinkageLoc,
                 "symbol with local linkage must have default visibility");

  if (!isValidDLLStorageClassForLinkage(DLLStorageClass, Linkage))
    return error(LinkageLoc,
                 "symbol with local linkage cannot have a DLL storage class");

  if (!FunctionType::isValidReturnType(RetType))
    return error(RetTypeLoc, "invalid function return type");

  LocTy NameLoc = Lex.getLoc();

  std::string FunctionName;
  if (Lex.getKind() == lltok::GlobalVar) {
    FunctionName = Lex.getStrVal();
  } else if (Lex.getKind() == lltok::GlobalID) {     // @42 is ok.
    FunctionNumber = Lex.getUIntVal();
    if (checkValueID(NameLoc, "function", "@", NumberedVals.getNext(),
                     FunctionNumber))
      return true;
  } else {
    return tokError("expected function name");
  }

  Lex.Lex();

  if (Lex.getKind() != lltok::lparen)
    return tokError("expected '(' in function argument list");

  SmallVector<ArgInfo, 8> ArgList;
  bool IsVarArg;
  AttrBuilder FuncAttrs(M->getContext());
  std::vector<unsigned> FwdRefAttrGrps;
  LocTy BuiltinLoc;
  std::string Section;
  std::string Partition;
  MaybeAlign Alignment;
  std::string GC;
  GlobalValue::UnnamedAddr UnnamedAddr = GlobalValue::UnnamedAddr::None;
  unsigned AddrSpace = 0;
  Constant *Prefix = nullptr;
  Constant *Prologue = nullptr;
  Constant *PersonalityFn = nullptr;
  Comdat *C;

  if (parseArgumentList(ArgList, UnnamedArgNums, IsVarArg) ||
      parseOptionalUnnamedAddr(UnnamedAddr) ||
      parseOptionalProgramAddrSpace(AddrSpace) ||
      parseFnAttributeValuePairs(FuncAttrs, FwdRefAttrGrps, false,
                                 BuiltinLoc) ||
      (EatIfPresent(lltok::kw_section) && parseStringConstant(Section)) ||
      (EatIfPresent(lltok::kw_partition) && parseStringConstant(Partition)) ||
      parseOptionalComdat(FunctionName, C) ||
      parseOptionalAlignment(Alignment) ||
      (EatIfPresent(lltok::kw_gc) && parseStringConstant(GC)) ||
      (EatIfPresent(lltok::kw_prefix) && parseGlobalTypeAndValue(Prefix)) ||
      (EatIfPresent(lltok::kw_prologue) && parseGlobalTypeAndValue(Prologue)) ||
      (EatIfPresent(lltok::kw_personality) &&
       parseGlobalTypeAndValue(PersonalityFn)))
    return true;

  if (FuncAttrs.contains(Attribute::Builtin))
    return error(BuiltinLoc, "'builtin' attribute not valid on function");

  // If the alignment was parsed as an attribute, move to the alignment field.
  if (MaybeAlign A = FuncAttrs.getAlignment()) {
    Alignment = A;
    FuncAttrs.removeAttribute(Attribute::Alignment);
  }

  // Okay, if we got here, the function is syntactically valid.  Convert types
  // and do semantic checks.
  std::vector<Type*> ParamTypeList;
  SmallVector<AttributeSet, 8> Attrs;

  for (const ArgInfo &Arg : ArgList) {
    ParamTypeList.push_back(Arg.Ty);
    Attrs.push_back(Arg.Attrs);
  }

  AttributeList PAL =
      AttributeList::get(Context, AttributeSet::get(Context, FuncAttrs),
                         AttributeSet::get(Context, RetAttrs), Attrs);

  if (PAL.hasParamAttr(0, Attribute::StructRet) && !RetType->isVoidTy())
    return error(RetTypeLoc, "functions with 'sret' argument must return void");

  FunctionType *FT = FunctionType::get(RetType, ParamTypeList, IsVarArg);
  PointerType *PFT = PointerType::get(FT, AddrSpace);

  Fn = nullptr;
  GlobalValue *FwdFn = nullptr;
  if (!FunctionName.empty()) {
    // If this was a definition of a forward reference, remove the definition
    // from the forward reference table and fill in the forward ref.
    auto FRVI = ForwardRefVals.find(FunctionName);
    if (FRVI != ForwardRefVals.end()) {
      FwdFn = FRVI->second.first;
      if (FwdFn->getType() != PFT)
        return error(FRVI->second.second,
                     "invalid forward reference to "
                     "function '" +
                         FunctionName +
                         "' with wrong type: "
                         "expected '" +
                         getTypeString(PFT) + "' but was '" +
                         getTypeString(FwdFn->getType()) + "'");
      ForwardRefVals.erase(FRVI);
    } else if ((Fn = M->getFunction(FunctionName))) {
      // Reject redefinitions.
      return error(NameLoc,
                   "invalid redefinition of function '" + FunctionName + "'");
    } else if (M->getNamedValue(FunctionName)) {
      return error(NameLoc, "redefinition of function '@" + FunctionName + "'");
    }

  } else {
    // Handle @"", where a name is syntactically specified, but semantically
    // missing.
    if (FunctionNumber == (unsigned)-1)
      FunctionNumber = NumberedVals.getNext();

    // If this is a definition of a forward referenced function, make sure the
    // types agree.
    auto I = ForwardRefValIDs.find(FunctionNumber);
    if (I != ForwardRefValIDs.end()) {
      FwdFn = I->second.first;
      if (FwdFn->getType() != PFT)
        return error(NameLoc, "type of definition and forward reference of '@" +
                                  Twine(FunctionNumber) +
                                  "' disagree: "
                                  "expected '" +
                                  getTypeString(PFT) + "' but was '" +
                                  getTypeString(FwdFn->getType()) + "'");
      ForwardRefValIDs.erase(I);
    }
  }

  Fn = Function::Create(FT, GlobalValue::ExternalLinkage, AddrSpace,
                        FunctionName, M);

  assert(Fn->getAddressSpace() == AddrSpace && "Created function in wrong AS");

  if (FunctionName.empty())
    NumberedVals.add(FunctionNumber, Fn);

  Fn->setLinkage((GlobalValue::LinkageTypes)Linkage);
  maybeSetDSOLocal(DSOLocal, *Fn);
  Fn->setVisibility((GlobalValue::VisibilityTypes)Visibility);
  Fn->setDLLStorageClass((GlobalValue::DLLStorageClassTypes)DLLStorageClass);
  Fn->setCallingConv(CC);
  Fn->setAttributes(PAL);
  Fn->setUnnamedAddr(UnnamedAddr);
  if (Alignment)
    Fn->setAlignment(*Alignment);
  Fn->setSection(Section);
  Fn->setPartition(Partition);
  Fn->setComdat(C);
  Fn->setPersonalityFn(PersonalityFn);
  if (!GC.empty()) Fn->setGC(GC);
  Fn->setPrefixData(Prefix);
  Fn->setPrologueData(Prologue);
  ForwardRefAttrGroups[Fn] = FwdRefAttrGrps;

  // Add all of the arguments we parsed to the function.
  Function::arg_iterator ArgIt = Fn->arg_begin();
  for (unsigned i = 0, e = ArgList.size(); i != e; ++i, ++ArgIt) {
    // If the argument has a name, insert it into the argument symbol table.
    if (ArgList[i].Name.empty()) continue;

    // Set the name, if it conflicted, it will be auto-renamed.
    ArgIt->setName(ArgList[i].Name);

    if (ArgIt->getName() != ArgList[i].Name)
      return error(ArgList[i].Loc,
                   "redefinition of argument '%" + ArgList[i].Name + "'");
  }

  if (FwdFn) {
    FwdFn->replaceAllUsesWith(Fn);
    FwdFn->eraseFromParent();
  }

  if (IsDefine)
    return false;

  // Check the declaration has no block address forward references.
  ValID ID;
  if (FunctionName.empty()) {
    ID.Kind = ValID::t_GlobalID;
    ID.UIntVal = FunctionNumber;
  } else {
    ID.Kind = ValID::t_GlobalName;
    ID.StrVal = FunctionName;
  }
  auto Blocks = ForwardRefBlockAddresses.find(ID);
  if (Blocks != ForwardRefBlockAddresses.end())
    return error(Blocks->first.Loc,
                 "cannot take blockaddress inside a declaration");
  return false;
}

bool LLParser::PerFunctionState::resolveForwardRefBlockAddresses() {
  ValID ID;
  if (FunctionNumber == -1) {
    ID.Kind = ValID::t_GlobalName;
    ID.StrVal = std::string(F.getName());
  } else {
    ID.Kind = ValID::t_GlobalID;
    ID.UIntVal = FunctionNumber;
  }

  auto Blocks = P.ForwardRefBlockAddresses.find(ID);
  if (Blocks == P.ForwardRefBlockAddresses.end())
    return false;

  for (const auto &I : Blocks->second) {
    const ValID &BBID = I.first;
    GlobalValue *GV = I.second;

    assert((BBID.Kind == ValID::t_LocalID || BBID.Kind == ValID::t_LocalName) &&
           "Expected local id or name");
    BasicBlock *BB;
    if (BBID.Kind == ValID::t_LocalName)
      BB = getBB(BBID.StrVal, BBID.Loc);
    else
      BB = getBB(BBID.UIntVal, BBID.Loc);
    if (!BB)
      return P.error(BBID.Loc, "referenced value is not a basic block");

    Value *ResolvedVal = BlockAddress::get(&F, BB);
    ResolvedVal = P.checkValidVariableType(BBID.Loc, BBID.StrVal, GV->getType(),
                                           ResolvedVal);
    if (!ResolvedVal)
      return true;
    GV->replaceAllUsesWith(ResolvedVal);
    GV->eraseFromParent();
  }

  P.ForwardRefBlockAddresses.erase(Blocks);
  return false;
}

/// parseFunctionBody
///   ::= '{' BasicBlock+ UseListOrderDirective* '}'
bool LLParser::parseFunctionBody(Function &Fn, unsigned FunctionNumber,
                                 ArrayRef<unsigned> UnnamedArgNums) {
  if (Lex.getKind() != lltok::lbrace)
    return tokError("expected '{' in function body");
  Lex.Lex();  // eat the {.

  PerFunctionState PFS(*this, Fn, FunctionNumber, UnnamedArgNums);

  // Resolve block addresses and allow basic blocks to be forward-declared
  // within this function.
  if (PFS.resolveForwardRefBlockAddresses())
    return true;
  SaveAndRestore ScopeExit(BlockAddressPFS, &PFS);

  // We need at least one basic block.
  if (Lex.getKind() == lltok::rbrace || Lex.getKind() == lltok::kw_uselistorder)
    return tokError("function body requires at least one basic block");

  while (Lex.getKind() != lltok::rbrace &&
         Lex.getKind() != lltok::kw_uselistorder)
    if (parseBasicBlock(PFS))
      return true;

  while (Lex.getKind() != lltok::rbrace)
    if (parseUseListOrder(&PFS))
      return true;

  // Eat the }.
  Lex.Lex();

  // Verify function is ok.
  return PFS.finishFunction();
}

/// parseBasicBlock
///   ::= (LabelStr|LabelID)? Instruction*
bool LLParser::parseBasicBlock(PerFunctionState &PFS) {
  // If this basic block starts out with a name, remember it.
  std::string Name;
  int NameID = -1;
  LocTy NameLoc = Lex.getLoc();
  if (Lex.getKind() == lltok::LabelStr) {
    Name = Lex.getStrVal();
    Lex.Lex();
  } else if (Lex.getKind() == lltok::LabelID) {
    NameID = Lex.getUIntVal();
    Lex.Lex();
  }

  BasicBlock *BB = PFS.defineBB(Name, NameID, NameLoc);
  if (!BB)
    return true;

  std::string NameStr;

  // Parse the instructions and debug values in this block until we get a
  // terminator.
  Instruction *Inst;
  auto DeleteDbgRecord = [](DbgRecord *DR) { DR->deleteRecord(); };
  using DbgRecordPtr = std::unique_ptr<DbgRecord, decltype(DeleteDbgRecord)>;
  SmallVector<DbgRecordPtr> TrailingDbgRecord;
  do {
    // Handle debug records first - there should always be an instruction
    // following the debug records, i.e. they cannot appear after the block
    // terminator.
    while (Lex.getKind() == lltok::hash) {
      if (SeenOldDbgInfoFormat)
        return error(Lex.getLoc(), "debug record should not appear in a module "
                                   "containing debug info intrinsics");
      if (!SeenNewDbgInfoFormat)
        M->setNewDbgInfoFormatFlag(true);
      SeenNewDbgInfoFormat = true;
      Lex.Lex();

      DbgRecord *DR;
      if (parseDebugRecord(DR, PFS))
        return true;
      TrailingDbgRecord.emplace_back(DR, DeleteDbgRecord);
    }

    // This instruction may have three possibilities for a name: a) none
    // specified, b) name specified "%foo =", c) number specified: "%4 =".
    LocTy NameLoc = Lex.getLoc();
    int NameID = -1;
    NameStr = "";

    if (Lex.getKind() == lltok::LocalVarID) {
      NameID = Lex.getUIntVal();
      Lex.Lex();
      if (parseToken(lltok::equal, "expected '=' after instruction id"))
        return true;
    } else if (Lex.getKind() == lltok::LocalVar) {
      NameStr = Lex.getStrVal();
      Lex.Lex();
      if (parseToken(lltok::equal, "expected '=' after instruction name"))
        return true;
    }

    switch (parseInstruction(Inst, BB, PFS)) {
    default:
      llvm_unreachable("Unknown parseInstruction result!");
    case InstError: return true;
    case InstNormal:
      Inst->insertInto(BB, BB->end());

      // With a normal result, we check to see if the instruction is followed by
      // a comma and metadata.
      if (EatIfPresent(lltok::comma))
        if (parseInstructionMetadata(*Inst))
          return true;
      break;
    case InstExtraComma:
      Inst->insertInto(BB, BB->end());

      // If the instruction parser ate an extra comma at the end of it, it
      // *must* be followed by metadata.
      if (parseInstructionMetadata(*Inst))
        return true;
      break;
    }

    // Set the name on the instruction.
    if (PFS.setInstName(NameID, NameStr, NameLoc, Inst))
      return true;

    // Attach any preceding debug values to this instruction.
    for (DbgRecordPtr &DR : TrailingDbgRecord)
      BB->insertDbgRecordBefore(DR.release(), Inst->getIterator());
    TrailingDbgRecord.clear();
  } while (!Inst->isTerminator());

  assert(TrailingDbgRecord.empty() &&
         "All debug values should have been attached to an instruction.");

  return false;
}

/// parseDebugRecord
///   ::= #dbg_label '(' MDNode ')'
///   ::= #dbg_type '(' Metadata ',' MDNode ',' Metadata ','
///                 (MDNode ',' Metadata ',' Metadata ',')? MDNode ')'
bool LLParser::parseDebugRecord(DbgRecord *&DR, PerFunctionState &PFS) {
  using RecordKind = DbgRecord::Kind;
  using LocType = DbgVariableRecord::LocationType;
  LocTy DVRLoc = Lex.getLoc();
  if (Lex.getKind() != lltok::DbgRecordType)
    return error(DVRLoc, "expected debug record type here");
  RecordKind RecordType = StringSwitch<RecordKind>(Lex.getStrVal())
                              .Case("declare", RecordKind::ValueKind)
                              .Case("value", RecordKind::ValueKind)
                              .Case("assign", RecordKind::ValueKind)
                              .Case("label", RecordKind::LabelKind);

  // Parsing labels is trivial; parse here and early exit, otherwise go into the
  // full DbgVariableRecord processing stage.
  if (RecordType == RecordKind::LabelKind) {
    Lex.Lex();
    if (parseToken(lltok::lparen, "Expected '(' here"))
      return true;
    MDNode *Label;
    if (parseMDNode(Label))
      return true;
    if (parseToken(lltok::comma, "Expected ',' here"))
      return true;
    MDNode *DbgLoc;
    if (parseMDNode(DbgLoc))
      return true;
    if (parseToken(lltok::rparen, "Expected ')' here"))
      return true;
    DR = DbgLabelRecord::createUnresolvedDbgLabelRecord(Label, DbgLoc);
    return false;
  }

  LocType ValueType = StringSwitch<LocType>(Lex.getStrVal())
                          .Case("declare", LocType::Declare)
                          .Case("value", LocType::Value)
                          .Case("assign", LocType::Assign);

  Lex.Lex();
  if (parseToken(lltok::lparen, "Expected '(' here"))
    return true;

  // Parse Value field.
  Metadata *ValLocMD;
  if (parseMetadata(ValLocMD, &PFS))
    return true;
  if (parseToken(lltok::comma, "Expected ',' here"))
    return true;

  // Parse Variable field.
  MDNode *Variable;
  if (parseMDNode(Variable))
    return true;
  if (parseToken(lltok::comma, "Expected ',' here"))
    return true;

  // Parse Expression field.
  MDNode *Expression;
  if (parseMDNode(Expression))
    return true;
  if (parseToken(lltok::comma, "Expected ',' here"))
    return true;

  // Parse additional fields for #dbg_assign.
  MDNode *AssignID = nullptr;
  Metadata *AddressLocation = nullptr;
  MDNode *AddressExpression = nullptr;
  if (ValueType == LocType::Assign) {
    // Parse DIAssignID.
    if (parseMDNode(AssignID))
      return true;
    if (parseToken(lltok::comma, "Expected ',' here"))
      return true;

    // Parse address ValueAsMetadata.
    if (parseMetadata(AddressLocation, &PFS))
      return true;
    if (parseToken(lltok::comma, "Expected ',' here"))
      return true;

    // Parse address DIExpression.
    if (parseMDNode(AddressExpression))
      return true;
    if (parseToken(lltok::comma, "Expected ',' here"))
      return true;
  }

  /// Parse DILocation.
  MDNode *DebugLoc;
  if (parseMDNode(DebugLoc))
    return true;

  if (parseToken(lltok::rparen, "Expected ')' here"))
    return true;
  DR = DbgVariableRecord::createUnresolvedDbgVariableRecord(
      ValueType, ValLocMD, Variable, Expression, AssignID, AddressLocation,
      AddressExpression, DebugLoc);
  return false;
}
//===----------------------------------------------------------------------===//
// Instruction Parsing.
//===----------------------------------------------------------------------===//

/// parseInstruction - parse one of the many different instructions.
///
int LLParser::parseInstruction(Instruction *&Inst, BasicBlock *BB,
                               PerFunctionState &PFS) {
  lltok::Kind Token = Lex.getKind();
  if (Token == lltok::Eof)
    return tokError("found end of file when expecting more instructions");
  LocTy Loc = Lex.getLoc();
  unsigned KeywordVal = Lex.getUIntVal();
  Lex.Lex();  // Eat the keyword.

  switch (Token) {
  default:
    return error(Loc, "expected instruction opcode");
  // Terminator Instructions.
  case lltok::kw_unreachable: Inst = new UnreachableInst(Context); return false;
  case lltok::kw_ret:
    return parseRet(Inst, BB, PFS);
  case lltok::kw_br:
    return parseBr(Inst, PFS);
  case lltok::kw_switch:
    return parseSwitch(Inst, PFS);
  case lltok::kw_indirectbr:
    return parseIndirectBr(Inst, PFS);
  case lltok::kw_invoke:
    return parseInvoke(Inst, PFS);
  case lltok::kw_resume:
    return parseResume(Inst, PFS);
  case lltok::kw_cleanupret:
    return parseCleanupRet(Inst, PFS);
  case lltok::kw_catchret:
    return parseCatchRet(Inst, PFS);
  case lltok::kw_catchswitch:
    return parseCatchSwitch(Inst, PFS);
  case lltok::kw_catchpad:
    return parseCatchPad(Inst, PFS);
  case lltok::kw_cleanuppad:
    return parseCleanupPad(Inst, PFS);
  case lltok::kw_callbr:
    return parseCallBr(Inst, PFS);
  // Unary Operators.
  case lltok::kw_fneg: {
    FastMathFlags FMF = EatFastMathFlagsIfPresent();
    int Res = parseUnaryOp(Inst, PFS, KeywordVal, /*IsFP*/ true);
    if (Res != 0)
      return Res;
    if (FMF.any())
      Inst->setFastMathFlags(FMF);
    return false;
  }
  // Binary Operators.
  case lltok::kw_add:
  case lltok::kw_sub:
  case lltok::kw_mul:
  case lltok::kw_shl: {
    bool NUW = EatIfPresent(lltok::kw_nuw);
    bool NSW = EatIfPresent(lltok::kw_nsw);
    if (!NUW) NUW = EatIfPresent(lltok::kw_nuw);

    if (parseArithmetic(Inst, PFS, KeywordVal, /*IsFP*/ false))
      return true;

    if (NUW) cast<BinaryOperator>(Inst)->setHasNoUnsignedWrap(true);
    if (NSW) cast<BinaryOperator>(Inst)->setHasNoSignedWrap(true);
    return false;
  }
  case lltok::kw_fadd:
  case lltok::kw_fsub:
  case lltok::kw_fmul:
  case lltok::kw_fdiv:
  case lltok::kw_frem: {
    FastMathFlags FMF = EatFastMathFlagsIfPresent();
    int Res = parseArithmetic(Inst, PFS, KeywordVal, /*IsFP*/ true);
    if (Res != 0)
      return Res;
    if (FMF.any())
      Inst->setFastMathFlags(FMF);
    return 0;
  }

  case lltok::kw_sdiv:
  case lltok::kw_udiv:
  case lltok::kw_lshr:
  case lltok::kw_ashr: {
    bool Exact = EatIfPresent(lltok::kw_exact);

    if (parseArithmetic(Inst, PFS, KeywordVal, /*IsFP*/ false))
      return true;
    if (Exact) cast<BinaryOperator>(Inst)->setIsExact(true);
    return false;
  }

  case lltok::kw_urem:
  case lltok::kw_srem:
    return parseArithmetic(Inst, PFS, KeywordVal,
                           /*IsFP*/ false);
  case lltok::kw_or: {
    bool Disjoint = EatIfPresent(lltok::kw_disjoint);
    if (parseLogical(Inst, PFS, KeywordVal))
      return true;
    if (Disjoint)
      cast<PossiblyDisjointInst>(Inst)->setIsDisjoint(true);
    return false;
  }
  case lltok::kw_and:
  case lltok::kw_xor:
    return parseLogical(Inst, PFS, KeywordVal);
  case lltok::kw_icmp:
    return parseCompare(Inst, PFS, KeywordVal);
  case lltok::kw_fcmp: {
    FastMathFlags FMF = EatFastMathFlagsIfPresent();
    int Res = parseCompare(Inst, PFS, KeywordVal);
    if (Res != 0)
      return Res;
    if (FMF.any())
      Inst->setFastMathFlags(FMF);
    return 0;
  }

  // Casts.
  case lltok::kw_uitofp:
  case lltok::kw_zext: {
    bool NonNeg = EatIfPresent(lltok::kw_nneg);
    bool Res = parseCast(Inst, PFS, KeywordVal);
    if (Res != 0)
      return Res;
    if (NonNeg)
      Inst->setNonNeg();
    return 0;
  }
  case lltok::kw_trunc: {
    bool NUW = EatIfPresent(lltok::kw_nuw);
    bool NSW = EatIfPresent(lltok::kw_nsw);
    if (!NUW)
      NUW = EatIfPresent(lltok::kw_nuw);
    if (parseCast(Inst, PFS, KeywordVal))
      return true;
    if (NUW)
      cast<TruncInst>(Inst)->setHasNoUnsignedWrap(true);
    if (NSW)
      cast<TruncInst>(Inst)->setHasNoSignedWrap(true);
    return false;
  }
  case lltok::kw_sext:
  case lltok::kw_fptrunc:
  case lltok::kw_fpext:
  case lltok::kw_bitcast:
  case lltok::kw_addrspacecast:
  case lltok::kw_sitofp:
  case lltok::kw_fptoui:
  case lltok::kw_fptosi:
  case lltok::kw_inttoptr:
  case lltok::kw_ptrtoint:
    return parseCast(Inst, PFS, KeywordVal);
  // Other.
  case lltok::kw_select: {
    FastMathFlags FMF = EatFastMathFlagsIfPresent();
    int Res = parseSelect(Inst, PFS);
    if (Res != 0)
      return Res;
    if (FMF.any()) {
      if (!isa<FPMathOperator>(Inst))
        return error(Loc, "fast-math-flags specified for select without "
                          "floating-point scalar or vector return type");
      Inst->setFastMathFlags(FMF);
    }
    return 0;
  }
  case lltok::kw_va_arg:
    return parseVAArg(Inst, PFS);
  case lltok::kw_extractelement:
    return parseExtractElement(Inst, PFS);
  case lltok::kw_insertelement:
    return parseInsertElement(Inst, PFS);
  case lltok::kw_shufflevector:
    return parseShuffleVector(Inst, PFS);
  case lltok::kw_phi: {
    FastMathFlags FMF = EatFastMathFlagsIfPresent();
    int Res = parsePHI(Inst, PFS);
    if (Res != 0)
      return Res;
    if (FMF.any()) {
      if (!isa<FPMathOperator>(Inst))
        return error(Loc, "fast-math-flags specified for phi without "
                          "floating-point scalar or vector return type");
      Inst->setFastMathFlags(FMF);
    }
    return 0;
  }
  case lltok::kw_landingpad:
    return parseLandingPad(Inst, PFS);
  case lltok::kw_freeze:
    return parseFreeze(Inst, PFS);
  // Call.
  case lltok::kw_call:
    return parseCall(Inst, PFS, CallInst::TCK_None);
  case lltok::kw_tail:
    return parseCall(Inst, PFS, CallInst::TCK_Tail);
  case lltok::kw_musttail:
    return parseCall(Inst, PFS, CallInst::TCK_MustTail);
  case lltok::kw_notail:
    return parseCall(Inst, PFS, CallInst::TCK_NoTail);
  // Memory.
  case lltok::kw_alloca:
    return parseAlloc(Inst, PFS);
  case lltok::kw_load:
    return parseLoad(Inst, PFS);
  case lltok::kw_store:
    return parseStore(Inst, PFS);
  case lltok::kw_cmpxchg:
    return parseCmpXchg(Inst, PFS);
  case lltok::kw_atomicrmw:
    return parseAtomicRMW(Inst, PFS);
  case lltok::kw_fence:
    return parseFence(Inst, PFS);
  case lltok::kw_getelementptr:
    return parseGetElementPtr(Inst, PFS);
  case lltok::kw_extractvalue:
    return parseExtractValue(Inst, PFS);
  case lltok::kw_insertvalue:
    return parseInsertValue(Inst, PFS);
  }
}

/// parseCmpPredicate - parse an integer or fp predicate, based on Kind.
bool LLParser::parseCmpPredicate(unsigned &P, unsigned Opc) {
  if (Opc == Instruction::FCmp) {
    switch (Lex.getKind()) {
    default:
      return tokError("expected fcmp predicate (e.g. 'oeq')");
    case lltok::kw_oeq: P = CmpInst::FCMP_OEQ; break;
    case lltok::kw_one: P = CmpInst::FCMP_ONE; break;
    case lltok::kw_olt: P = CmpInst::FCMP_OLT; break;
    case lltok::kw_ogt: P = CmpInst::FCMP_OGT; break;
    case lltok::kw_ole: P = CmpInst::FCMP_OLE; break;
    case lltok::kw_oge: P = CmpInst::FCMP_OGE; break;
    case lltok::kw_ord: P = CmpInst::FCMP_ORD; break;
    case lltok::kw_uno: P = CmpInst::FCMP_UNO; break;
    case lltok::kw_ueq: P = CmpInst::FCMP_UEQ; break;
    case lltok::kw_une: P = CmpInst::FCMP_UNE; break;
    case lltok::kw_ult: P = CmpInst::FCMP_ULT; break;
    case lltok::kw_ugt: P = CmpInst::FCMP_UGT; break;
    case lltok::kw_ule: P = CmpInst::FCMP_ULE; break;
    case lltok::kw_uge: P = CmpInst::FCMP_UGE; break;
    case lltok::kw_true: P = CmpInst::FCMP_TRUE; break;
    case lltok::kw_false: P = CmpInst::FCMP_FALSE; break;
    }
  } else {
    switch (Lex.getKind()) {
    default:
      return tokError("expected icmp predicate (e.g. 'eq')");
    case lltok::kw_eq:  P = CmpInst::ICMP_EQ; break;
    case lltok::kw_ne:  P = CmpInst::ICMP_NE; break;
    case lltok::kw_slt: P = CmpInst::ICMP_SLT; break;
    case lltok::kw_sgt: P = CmpInst::ICMP_SGT; break;
    case lltok::kw_sle: P = CmpInst::ICMP_SLE; break;
    case lltok::kw_sge: P = CmpInst::ICMP_SGE; break;
    case lltok::kw_ult: P = CmpInst::ICMP_ULT; break;
    case lltok::kw_ugt: P = CmpInst::ICMP_UGT; break;
    case lltok::kw_ule: P = CmpInst::ICMP_ULE; break;
    case lltok::kw_uge: P = CmpInst::ICMP_UGE; break;
    }
  }
  Lex.Lex();
  return false;
}

//===----------------------------------------------------------------------===//
// Terminator Instructions.
//===----------------------------------------------------------------------===//

/// parseRet - parse a return instruction.
///   ::= 'ret' void (',' !dbg, !1)*
///   ::= 'ret' TypeAndValue (',' !dbg, !1)*
bool LLParser::parseRet(Instruction *&Inst, BasicBlock *BB,
                        PerFunctionState &PFS) {
  SMLoc TypeLoc = Lex.getLoc();
  Type *Ty = nullptr;
  if (parseType(Ty, true /*void allowed*/))
    return true;

  Type *ResType = PFS.getFunction().getReturnType();

  if (Ty->isVoidTy()) {
    if (!ResType->isVoidTy())
      return error(TypeLoc, "value doesn't match function result type '" +
                                getTypeString(ResType) + "'");

    Inst = ReturnInst::Create(Context);
    return false;
  }

  Value *RV;
  if (parseValue(Ty, RV, PFS))
    return true;

  if (ResType != RV->getType())
    return error(TypeLoc, "value doesn't match function result type '" +
                              getTypeString(ResType) + "'");

  Inst = ReturnInst::Create(Context, RV);
  return false;
}

/// parseBr
///   ::= 'br' TypeAndValue
///   ::= 'br' TypeAndValue ',' TypeAndValue ',' TypeAndValue
bool LLParser::parseBr(Instruction *&Inst, PerFunctionState &PFS) {
  LocTy Loc, Loc2;
  Value *Op0;
  BasicBlock *Op1, *Op2;
  if (parseTypeAndValue(Op0, Loc, PFS))
    return true;

  if (BasicBlock *BB = dyn_cast<BasicBlock>(Op0)) {
    Inst = BranchInst::Create(BB);
    return false;
  }

  if (Op0->getType() != Type::getInt1Ty(Context))
    return error(Loc, "branch condition must have 'i1' type");

  if (parseToken(lltok::comma, "expected ',' after branch condition") ||
      parseTypeAndBasicBlock(Op1, Loc, PFS) ||
      parseToken(lltok::comma, "expected ',' after true destination") ||
      parseTypeAndBasicBlock(Op2, Loc2, PFS))
    return true;

  Inst = BranchInst::Create(Op1, Op2, Op0);
  return false;
}

/// parseSwitch
///  Instruction
///    ::= 'switch' TypeAndValue ',' TypeAndValue '[' JumpTable ']'
///  JumpTable
///    ::= (TypeAndValue ',' TypeAndValue)*
bool LLParser::parseSwitch(Instruction *&Inst, PerFunctionState &PFS) {
  LocTy CondLoc, BBLoc;
  Value *Cond;
  BasicBlock *DefaultBB;
  if (parseTypeAndValue(Cond, CondLoc, PFS) ||
      parseToken(lltok::comma, "expected ',' after switch condition") ||
      parseTypeAndBasicBlock(DefaultBB, BBLoc, PFS) ||
      parseToken(lltok::lsquare, "expected '[' with switch table"))
    return true;

  if (!Cond->getType()->isIntegerTy())
    return error(CondLoc, "switch condition must have integer type");

  // parse the jump table pairs.
  SmallPtrSet<Value*, 32> SeenCases;
  SmallVector<std::pair<ConstantInt*, BasicBlock*>, 32> Table;
  while (Lex.getKind() != lltok::rsquare) {
    Value *Constant;
    BasicBlock *DestBB;

    if (parseTypeAndValue(Constant, CondLoc, PFS) ||
        parseToken(lltok::comma, "expected ',' after case value") ||
        parseTypeAndBasicBlock(DestBB, PFS))
      return true;

    if (!SeenCases.insert(Constant).second)
      return error(CondLoc, "duplicate case value in switch");
    if (!isa<ConstantInt>(Constant))
      return error(CondLoc, "case value is not a constant integer");

    Table.push_back(std::make_pair(cast<ConstantInt>(Constant), DestBB));
  }

  Lex.Lex();  // Eat the ']'.

  SwitchInst *SI = SwitchInst::Create(Cond, DefaultBB, Table.size());
  for (unsigned i = 0, e = Table.size(); i != e; ++i)
    SI->addCase(Table[i].first, Table[i].second);
  Inst = SI;
  return false;
}

/// parseIndirectBr
///  Instruction
///    ::= 'indirectbr' TypeAndValue ',' '[' LabelList ']'
bool LLParser::parseIndirectBr(Instruction *&Inst, PerFunctionState &PFS) {
  LocTy AddrLoc;
  Value *Address;
  if (parseTypeAndValue(Address, AddrLoc, PFS) ||
      parseToken(lltok::comma, "expected ',' after indirectbr address") ||
      parseToken(lltok::lsquare, "expected '[' with indirectbr"))
    return true;

  if (!Address->getType()->isPointerTy())
    return error(AddrLoc, "indirectbr address must have pointer type");

  // parse the destination list.
  SmallVector<BasicBlock*, 16> DestList;

  if (Lex.getKind() != lltok::rsquare) {
    BasicBlock *DestBB;
    if (parseTypeAndBasicBlock(DestBB, PFS))
      return true;
    DestList.push_back(DestBB);

    while (EatIfPresent(lltok::comma)) {
      if (parseTypeAndBasicBlock(DestBB, PFS))
        return true;
      DestList.push_back(DestBB);
    }
  }

  if (parseToken(lltok::rsquare, "expected ']' at end of block list"))
    return true;

  IndirectBrInst *IBI = IndirectBrInst::Create(Address, DestList.size());
  for (BasicBlock *Dest : DestList)
    IBI->addDestination(Dest);
  Inst = IBI;
  return false;
}

// If RetType is a non-function pointer type, then this is the short syntax
// for the call, which means that RetType is just the return type.  Infer the
// rest of the function argument types from the arguments that are present.
bool LLParser::resolveFunctionType(Type *RetType,
                                   const SmallVector<ParamInfo, 16> &ArgList,
                                   FunctionType *&FuncTy) {
  FuncTy = dyn_cast<FunctionType>(RetType);
  if (!FuncTy) {
    // Pull out the types of all of the arguments...
    std::vector<Type*> ParamTypes;
    for (const ParamInfo &Arg : ArgList)
      ParamTypes.push_back(Arg.V->getType());

    if (!FunctionType::isValidReturnType(RetType))
      return true;

    FuncTy = FunctionType::get(RetType, ParamTypes, false);
  }
  return false;
}

/// parseInvoke
///   ::= 'invoke' OptionalCallingConv OptionalAttrs Type Value ParamList
///       OptionalAttrs 'to' TypeAndValue 'unwind' TypeAndValue
bool LLParser::parseInvoke(Instruction *&Inst, PerFunctionState &PFS) {
  LocTy CallLoc = Lex.getLoc();
  AttrBuilder RetAttrs(M->getContext()), FnAttrs(M->getContext());
  std::vector<unsigned> FwdRefAttrGrps;
  LocTy NoBuiltinLoc;
  unsigned CC;
  unsigned InvokeAddrSpace;
  Type *RetType = nullptr;
  LocTy RetTypeLoc;
  ValID CalleeID;
  SmallVector<ParamInfo, 16> ArgList;
  SmallVector<OperandBundleDef, 2> BundleList;

  BasicBlock *NormalBB, *UnwindBB;
  if (parseOptionalCallingConv(CC) || parseOptionalReturnAttrs(RetAttrs) ||
      parseOptionalProgramAddrSpace(InvokeAddrSpace) ||
      parseType(RetType, RetTypeLoc, true /*void allowed*/) ||
      parseValID(CalleeID, &PFS) || parseParameterList(ArgList, PFS) ||
      parseFnAttributeValuePairs(FnAttrs, FwdRefAttrGrps, false,
                                 NoBuiltinLoc) ||
      parseOptionalOperandBundles(BundleList, PFS) ||
      parseToken(lltok::kw_to, "expected 'to' in invoke") ||
      parseTypeAndBasicBlock(NormalBB, PFS) ||
      parseToken(lltok::kw_unwind, "expected 'unwind' in invoke") ||
      parseTypeAndBasicBlock(UnwindBB, PFS))
    return true;

  // If RetType is a non-function pointer type, then this is the short syntax
  // for the call, which means that RetType is just the return type.  Infer the
  // rest of the function argument types from the arguments that are present.
  FunctionType *Ty;
  if (resolveFunctionType(RetType, ArgList, Ty))
    return error(RetTypeLoc, "Invalid result type for LLVM function");

  CalleeID.FTy = Ty;

  // Look up the callee.
  Value *Callee;
  if (convertValIDToValue(PointerType::get(Ty, InvokeAddrSpace), CalleeID,
                          Callee, &PFS))
    return true;

  // Set up the Attribute for the function.
  SmallVector<Value *, 8> Args;
  SmallVector<AttributeSet, 8> ArgAttrs;

  // Loop through FunctionType's arguments and ensure they are specified
  // correctly.  Also, gather any parameter attributes.
  FunctionType::param_iterator I = Ty->param_begin();
  FunctionType::param_iterator E = Ty->param_end();
  for (const ParamInfo &Arg : ArgList) {
    Type *ExpectedTy = nullptr;
    if (I != E) {
      ExpectedTy = *I++;
    } else if (!Ty->isVarArg()) {
      return error(Arg.Loc, "too many arguments specified");
    }

    if (ExpectedTy && ExpectedTy != Arg.V->getType())
      return error(Arg.Loc, "argument is not of expected type '" +
                                getTypeString(ExpectedTy) + "'");
    Args.push_back(Arg.V);
    ArgAttrs.push_back(Arg.Attrs);
  }

  if (I != E)
    return error(CallLoc, "not enough parameters specified for call");

  // Finish off the Attribute and check them
  AttributeList PAL =
      AttributeList::get(Context, AttributeSet::get(Context, FnAttrs),
                         AttributeSet::get(Context, RetAttrs), ArgAttrs);

  InvokeInst *II =
      InvokeInst::Create(Ty, Callee, NormalBB, UnwindBB, Args, BundleList);
  II->setCallingConv(CC);
  II->setAttributes(PAL);
  ForwardRefAttrGroups[II] = FwdRefAttrGrps;
  Inst = II;
  return false;
}

/// parseResume
///   ::= 'resume' TypeAndValue
bool LLParser::parseResume(Instruction *&Inst, PerFunctionState &PFS) {
  Value *Exn; LocTy ExnLoc;
  if (parseTypeAndValue(Exn, ExnLoc, PFS))
    return true;

  ResumeInst *RI = ResumeInst::Create(Exn);
  Inst = RI;
  return false;
}

bool LLParser::parseExceptionArgs(SmallVectorImpl<Value *> &Args,
                                  PerFunctionState &PFS) {
  if (parseToken(lltok::lsquare, "expected '[' in catchpad/cleanuppad"))
    return true;

  while (Lex.getKind() != lltok::rsquare) {
    // If this isn't the first argument, we need a comma.
    if (!Args.empty() &&
        parseToken(lltok::comma, "expected ',' in argument list"))
      return true;

    // parse the argument.
    LocTy ArgLoc;
    Type *ArgTy = nullptr;
    if (parseType(ArgTy, ArgLoc))
      return true;

    Value *V;
    if (ArgTy->isMetadataTy()) {
      if (parseMetadataAsValue(V, PFS))
        return true;
    } else {
      if (parseValue(ArgTy, V, PFS))
        return true;
    }
    Args.push_back(V);
  }

  Lex.Lex();  // Lex the ']'.
  return false;
}

/// parseCleanupRet
///   ::= 'cleanupret' from Value unwind ('to' 'caller' | TypeAndValue)
bool LLParser::parseCleanupRet(Instruction *&Inst, PerFunctionState &PFS) {
  Value *CleanupPad = nullptr;

  if (parseToken(lltok::kw_from, "expected 'from' after cleanupret"))
    return true;

  if (parseValue(Type::getTokenTy(Context), CleanupPad, PFS))
    return true;

  if (parseToken(lltok::kw_unwind, "expected 'unwind' in cleanupret"))
    return true;

  BasicBlock *UnwindBB = nullptr;
  if (Lex.getKind() == lltok::kw_to) {
    Lex.Lex();
    if (parseToken(lltok::kw_caller, "expected 'caller' in cleanupret"))
      return true;
  } else {
    if (parseTypeAndBasicBlock(UnwindBB, PFS)) {
      return true;
    }
  }

  Inst = CleanupReturnInst::Create(CleanupPad, UnwindBB);
  return false;
}

/// parseCatchRet
///   ::= 'catchret' from Parent Value 'to' TypeAndValue
bool LLParser::parseCatchRet(Instruction *&Inst, PerFunctionState &PFS) {
  Value *CatchPad = nullptr;

  if (parseToken(lltok::kw_from, "expected 'from' after catchret"))
    return true;

  if (parseValue(Type::getTokenTy(Context), CatchPad, PFS))
    return true;

  BasicBlock *BB;
  if (parseToken(lltok::kw_to, "expected 'to' in catchret") ||
      parseTypeAndBasicBlock(BB, PFS))
    return true;

  Inst = CatchReturnInst::Create(CatchPad, BB);
  return false;
}

/// parseCatchSwitch
///   ::= 'catchswitch' within Parent
bool LLParser::parseCatchSwitch(Instruction *&Inst, PerFunctionState &PFS) {
  Value *ParentPad;

  if (parseToken(lltok::kw_within, "expected 'within' after catchswitch"))
    return true;

  if (Lex.getKind() != lltok::kw_none && Lex.getKind() != lltok::LocalVar &&
      Lex.getKind() != lltok::LocalVarID)
    return tokError("expected scope value for catchswitch");

  if (parseValue(Type::getTokenTy(Context), ParentPad, PFS))
    return true;

  if (parseToken(lltok::lsquare, "expected '[' with catchswitch labels"))
    return true;

  SmallVector<BasicBlock *, 32> Table;
  do {
    BasicBlock *DestBB;
    if (parseTypeAndBasicBlock(DestBB, PFS))
      return true;
    Table.push_back(DestBB);
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rsquare, "expected ']' after catchswitch labels"))
    return true;

  if (parseToken(lltok::kw_unwind, "expected 'unwind' after catchswitch scope"))
    return true;

  BasicBlock *UnwindBB = nullptr;
  if (EatIfPresent(lltok::kw_to)) {
    if (parseToken(lltok::kw_caller, "expected 'caller' in catchswitch"))
      return true;
  } else {
    if (parseTypeAndBasicBlock(UnwindBB, PFS))
      return true;
  }

  auto *CatchSwitch =
      CatchSwitchInst::Create(ParentPad, UnwindBB, Table.size());
  for (BasicBlock *DestBB : Table)
    CatchSwitch->addHandler(DestBB);
  Inst = CatchSwitch;
  return false;
}

/// parseCatchPad
///   ::= 'catchpad' ParamList 'to' TypeAndValue 'unwind' TypeAndValue
bool LLParser::parseCatchPad(Instruction *&Inst, PerFunctionState &PFS) {
  Value *CatchSwitch = nullptr;

  if (parseToken(lltok::kw_within, "expected 'within' after catchpad"))
    return true;

  if (Lex.getKind() != lltok::LocalVar && Lex.getKind() != lltok::LocalVarID)
    return tokError("expected scope value for catchpad");

  if (parseValue(Type::getTokenTy(Context), CatchSwitch, PFS))
    return true;

  SmallVector<Value *, 8> Args;
  if (parseExceptionArgs(Args, PFS))
    return true;

  Inst = CatchPadInst::Create(CatchSwitch, Args);
  return false;
}

/// parseCleanupPad
///   ::= 'cleanuppad' within Parent ParamList
bool LLParser::parseCleanupPad(Instruction *&Inst, PerFunctionState &PFS) {
  Value *ParentPad = nullptr;

  if (parseToken(lltok::kw_within, "expected 'within' after cleanuppad"))
    return true;

  if (Lex.getKind() != lltok::kw_none && Lex.getKind() != lltok::LocalVar &&
      Lex.getKind() != lltok::LocalVarID)
    return tokError("expected scope value for cleanuppad");

  if (parseValue(Type::getTokenTy(Context), ParentPad, PFS))
    return true;

  SmallVector<Value *, 8> Args;
  if (parseExceptionArgs(Args, PFS))
    return true;

  Inst = CleanupPadInst::Create(ParentPad, Args);
  return false;
}

//===----------------------------------------------------------------------===//
// Unary Operators.
//===----------------------------------------------------------------------===//

/// parseUnaryOp
///  ::= UnaryOp TypeAndValue ',' Value
///
/// If IsFP is false, then any integer operand is allowed, if it is true, any fp
/// operand is allowed.
bool LLParser::parseUnaryOp(Instruction *&Inst, PerFunctionState &PFS,
                            unsigned Opc, bool IsFP) {
  LocTy Loc; Value *LHS;
  if (parseTypeAndValue(LHS, Loc, PFS))
    return true;

  bool Valid = IsFP ? LHS->getType()->isFPOrFPVectorTy()
                    : LHS->getType()->isIntOrIntVectorTy();

  if (!Valid)
    return error(Loc, "invalid operand type for instruction");

  Inst = UnaryOperator::Create((Instruction::UnaryOps)Opc, LHS);
  return false;
}

/// parseCallBr
///   ::= 'callbr' OptionalCallingConv OptionalAttrs Type Value ParamList
///       OptionalAttrs OptionalOperandBundles 'to' TypeAndValue
///       '[' LabelList ']'
bool LLParser::parseCallBr(Instruction *&Inst, PerFunctionState &PFS) {
  LocTy CallLoc = Lex.getLoc();
  AttrBuilder RetAttrs(M->getContext()), FnAttrs(M->getContext());
  std::vector<unsigned> FwdRefAttrGrps;
  LocTy NoBuiltinLoc;
  unsigned CC;
  Type *RetType = nullptr;
  LocTy RetTypeLoc;
  ValID CalleeID;
  SmallVector<ParamInfo, 16> ArgList;
  SmallVector<OperandBundleDef, 2> BundleList;

  BasicBlock *DefaultDest;
  if (parseOptionalCallingConv(CC) || parseOptionalReturnAttrs(RetAttrs) ||
      parseType(RetType, RetTypeLoc, true /*void allowed*/) ||
      parseValID(CalleeID, &PFS) || parseParameterList(ArgList, PFS) ||
      parseFnAttributeValuePairs(FnAttrs, FwdRefAttrGrps, false,
                                 NoBuiltinLoc) ||
      parseOptionalOperandBundles(BundleList, PFS) ||
      parseToken(lltok::kw_to, "expected 'to' in callbr") ||
      parseTypeAndBasicBlock(DefaultDest, PFS) ||
      parseToken(lltok::lsquare, "expected '[' in callbr"))
    return true;

  // parse the destination list.
  SmallVector<BasicBlock *, 16> IndirectDests;

  if (Lex.getKind() != lltok::rsquare) {
    BasicBlock *DestBB;
    if (parseTypeAndBasicBlock(DestBB, PFS))
      return true;
    IndirectDests.push_back(DestBB);

    while (EatIfPresent(lltok::comma)) {
      if (parseTypeAndBasicBlock(DestBB, PFS))
        return true;
      IndirectDests.push_back(DestBB);
    }
  }

  if (parseToken(lltok::rsquare, "expected ']' at end of block list"))
    return true;

  // If RetType is a non-function pointer type, then this is the short syntax
  // for the call, which means that RetType is just the return type.  Infer the
  // rest of the function argument types from the arguments that are present.
  FunctionType *Ty;
  if (resolveFunctionType(RetType, ArgList, Ty))
    return error(RetTypeLoc, "Invalid result type for LLVM function");

  CalleeID.FTy = Ty;

  // Look up the callee.
  Value *Callee;
  if (convertValIDToValue(PointerType::getUnqual(Ty), CalleeID, Callee, &PFS))
    return true;

  // Set up the Attribute for the function.
  SmallVector<Value *, 8> Args;
  SmallVector<AttributeSet, 8> ArgAttrs;

  // Loop through FunctionType's arguments and ensure they are specified
  // correctly.  Also, gather any parameter attributes.
  FunctionType::param_iterator I = Ty->param_begin();
  FunctionType::param_iterator E = Ty->param_end();
  for (const ParamInfo &Arg : ArgList) {
    Type *ExpectedTy = nullptr;
    if (I != E) {
      ExpectedTy = *I++;
    } else if (!Ty->isVarArg()) {
      return error(Arg.Loc, "too many arguments specified");
    }

    if (ExpectedTy && ExpectedTy != Arg.V->getType())
      return error(Arg.Loc, "argument is not of expected type '" +
                                getTypeString(ExpectedTy) + "'");
    Args.push_back(Arg.V);
    ArgAttrs.push_back(Arg.Attrs);
  }

  if (I != E)
    return error(CallLoc, "not enough parameters specified for call");

  // Finish off the Attribute and check them
  AttributeList PAL =
      AttributeList::get(Context, AttributeSet::get(Context, FnAttrs),
                         AttributeSet::get(Context, RetAttrs), ArgAttrs);

  CallBrInst *CBI =
      CallBrInst::Create(Ty, Callee, DefaultDest, IndirectDests, Args,
                         BundleList);
  CBI->setCallingConv(CC);
  CBI->setAttributes(PAL);
  ForwardRefAttrGroups[CBI] = FwdRefAttrGrps;
  Inst = CBI;
  return false;
}

//===----------------------------------------------------------------------===//
// Binary Operators.
//===----------------------------------------------------------------------===//

/// parseArithmetic
///  ::= ArithmeticOps TypeAndValue ',' Value
///
/// If IsFP is false, then any integer operand is allowed, if it is true, any fp
/// operand is allowed.
bool LLParser::parseArithmetic(Instruction *&Inst, PerFunctionState &PFS,
                               unsigned Opc, bool IsFP) {
  LocTy Loc; Value *LHS, *RHS;
  if (parseTypeAndValue(LHS, Loc, PFS) ||
      parseToken(lltok::comma, "expected ',' in arithmetic operation") ||
      parseValue(LHS->getType(), RHS, PFS))
    return true;

  bool Valid = IsFP ? LHS->getType()->isFPOrFPVectorTy()
                    : LHS->getType()->isIntOrIntVectorTy();

  if (!Valid)
    return error(Loc, "invalid operand type for instruction");

  Inst = BinaryOperator::Create((Instruction::BinaryOps)Opc, LHS, RHS);
  return false;
}

/// parseLogical
///  ::= ArithmeticOps TypeAndValue ',' Value {
bool LLParser::parseLogical(Instruction *&Inst, PerFunctionState &PFS,
                            unsigned Opc) {
  LocTy Loc; Value *LHS, *RHS;
  if (parseTypeAndValue(LHS, Loc, PFS) ||
      parseToken(lltok::comma, "expected ',' in logical operation") ||
      parseValue(LHS->getType(), RHS, PFS))
    return true;

  if (!LHS->getType()->isIntOrIntVectorTy())
    return error(Loc,
                 "instruction requires integer or integer vector operands");

  Inst = BinaryOperator::Create((Instruction::BinaryOps)Opc, LHS, RHS);
  return false;
}

/// parseCompare
///  ::= 'icmp' IPredicates TypeAndValue ',' Value
///  ::= 'fcmp' FPredicates TypeAndValue ',' Value
bool LLParser::parseCompare(Instruction *&Inst, PerFunctionState &PFS,
                            unsigned Opc) {
  // parse the integer/fp comparison predicate.
  LocTy Loc;
  unsigned Pred;
  Value *LHS, *RHS;
  if (parseCmpPredicate(Pred, Opc) || parseTypeAndValue(LHS, Loc, PFS) ||
      parseToken(lltok::comma, "expected ',' after compare value") ||
      parseValue(LHS->getType(), RHS, PFS))
    return true;

  if (Opc == Instruction::FCmp) {
    if (!LHS->getType()->isFPOrFPVectorTy())
      return error(Loc, "fcmp requires floating point operands");
    Inst = new FCmpInst(CmpInst::Predicate(Pred), LHS, RHS);
  } else {
    assert(Opc == Instruction::ICmp && "Unknown opcode for CmpInst!");
    if (!LHS->getType()->isIntOrIntVectorTy() &&
        !LHS->getType()->isPtrOrPtrVectorTy())
      return error(Loc, "icmp requires integer operands");
    Inst = new ICmpInst(CmpInst::Predicate(Pred), LHS, RHS);
  }
  return false;
}

//===----------------------------------------------------------------------===//
// Other Instructions.
//===----------------------------------------------------------------------===//

/// parseCast
///   ::= CastOpc TypeAndValue 'to' Type
bool LLParser::parseCast(Instruction *&Inst, PerFunctionState &PFS,
                         unsigned Opc) {
  LocTy Loc;
  Value *Op;
  Type *DestTy = nullptr;
  if (parseTypeAndValue(Op, Loc, PFS) ||
      parseToken(lltok::kw_to, "expected 'to' after cast value") ||
      parseType(DestTy))
    return true;

  if (!CastInst::castIsValid((Instruction::CastOps)Opc, Op, DestTy)) {
    CastInst::castIsValid((Instruction::CastOps)Opc, Op, DestTy);
    return error(Loc, "invalid cast opcode for cast from '" +
                          getTypeString(Op->getType()) + "' to '" +
                          getTypeString(DestTy) + "'");
  }
  Inst = CastInst::Create((Instruction::CastOps)Opc, Op, DestTy);
  return false;
}

/// parseSelect
///   ::= 'select' TypeAndValue ',' TypeAndValue ',' TypeAndValue
bool LLParser::parseSelect(Instruction *&Inst, PerFunctionState &PFS) {
  LocTy Loc;
  Value *Op0, *Op1, *Op2;
  if (parseTypeAndValue(Op0, Loc, PFS) ||
      parseToken(lltok::comma, "expected ',' after select condition") ||
      parseTypeAndValue(Op1, PFS) ||
      parseToken(lltok::comma, "expected ',' after select value") ||
      parseTypeAndValue(Op2, PFS))
    return true;

  if (const char *Reason = SelectInst::areInvalidOperands(Op0, Op1, Op2))
    return error(Loc, Reason);

  Inst = SelectInst::Create(Op0, Op1, Op2);
  return false;
}

/// parseVAArg
///   ::= 'va_arg' TypeAndValue ',' Type
bool LLParser::parseVAArg(Instruction *&Inst, PerFunctionState &PFS) {
  Value *Op;
  Type *EltTy = nullptr;
  LocTy TypeLoc;
  if (parseTypeAndValue(Op, PFS) ||
      parseToken(lltok::comma, "expected ',' after vaarg operand") ||
      parseType(EltTy, TypeLoc))
    return true;

  if (!EltTy->isFirstClassType())
    return error(TypeLoc, "va_arg requires operand with first class type");

  Inst = new VAArgInst(Op, EltTy);
  return false;
}

/// parseExtractElement
///   ::= 'extractelement' TypeAndValue ',' TypeAndValue
bool LLParser::parseExtractElement(Instruction *&Inst, PerFunctionState &PFS) {
  LocTy Loc;
  Value *Op0, *Op1;
  if (parseTypeAndValue(Op0, Loc, PFS) ||
      parseToken(lltok::comma, "expected ',' after extract value") ||
      parseTypeAndValue(Op1, PFS))
    return true;

  if (!ExtractElementInst::isValidOperands(Op0, Op1))
    return error(Loc, "invalid extractelement operands");

  Inst = ExtractElementInst::Create(Op0, Op1);
  return false;
}

/// parseInsertElement
///   ::= 'insertelement' TypeAndValue ',' TypeAndValue ',' TypeAndValue
bool LLParser::parseInsertElement(Instruction *&Inst, PerFunctionState &PFS) {
  LocTy Loc;
  Value *Op0, *Op1, *Op2;
  if (parseTypeAndValue(Op0, Loc, PFS) ||
      parseToken(lltok::comma, "expected ',' after insertelement value") ||
      parseTypeAndValue(Op1, PFS) ||
      parseToken(lltok::comma, "expected ',' after insertelement value") ||
      parseTypeAndValue(Op2, PFS))
    return true;

  if (!InsertElementInst::isValidOperands(Op0, Op1, Op2))
    return error(Loc, "invalid insertelement operands");

  Inst = InsertElementInst::Create(Op0, Op1, Op2);
  return false;
}

/// parseShuffleVector
///   ::= 'shufflevector' TypeAndValue ',' TypeAndValue ',' TypeAndValue
bool LLParser::parseShuffleVector(Instruction *&Inst, PerFunctionState &PFS) {
  LocTy Loc;
  Value *Op0, *Op1, *Op2;
  if (parseTypeAndValue(Op0, Loc, PFS) ||
      parseToken(lltok::comma, "expected ',' after shuffle mask") ||
      parseTypeAndValue(Op1, PFS) ||
      parseToken(lltok::comma, "expected ',' after shuffle value") ||
      parseTypeAndValue(Op2, PFS))
    return true;

  if (!ShuffleVectorInst::isValidOperands(Op0, Op1, Op2))
    return error(Loc, "invalid shufflevector operands");

  Inst = new ShuffleVectorInst(Op0, Op1, Op2);
  return false;
}

/// parsePHI
///   ::= 'phi' Type '[' Value ',' Value ']' (',' '[' Value ',' Value ']')*
int LLParser::parsePHI(Instruction *&Inst, PerFunctionState &PFS) {
  Type *Ty = nullptr;  LocTy TypeLoc;
  Value *Op0, *Op1;

  if (parseType(Ty, TypeLoc))
    return true;

  if (!Ty->isFirstClassType())
    return error(TypeLoc, "phi node must have first class type");

  bool First = true;
  bool AteExtraComma = false;
  SmallVector<std::pair<Value*, BasicBlock*>, 16> PHIVals;

  while (true) {
    if (First) {
      if (Lex.getKind() != lltok::lsquare)
        break;
      First = false;
    } else if (!EatIfPresent(lltok::comma))
      break;

    if (Lex.getKind() == lltok::MetadataVar) {
      AteExtraComma = true;
      break;
    }

    if (parseToken(lltok::lsquare, "expected '[' in phi value list") ||
        parseValue(Ty, Op0, PFS) ||
        parseToken(lltok::comma, "expected ',' after insertelement value") ||
        parseValue(Type::getLabelTy(Context), Op1, PFS) ||
        parseToken(lltok::rsquare, "expected ']' in phi value list"))
      return true;

    PHIVals.push_back(std::make_pair(Op0, cast<BasicBlock>(Op1)));
  }

  PHINode *PN = PHINode::Create(Ty, PHIVals.size());
  for (unsigned i = 0, e = PHIVals.size(); i != e; ++i)
    PN->addIncoming(PHIVals[i].first, PHIVals[i].second);
  Inst = PN;
  return AteExtraComma ? InstExtraComma : InstNormal;
}

/// parseLandingPad
///   ::= 'landingpad' Type 'personality' TypeAndValue 'cleanup'? Clause+
/// Clause
///   ::= 'catch' TypeAndValue
///   ::= 'filter'
///   ::= 'filter' TypeAndValue ( ',' TypeAndValue )*
bool LLParser::parseLandingPad(Instruction *&Inst, PerFunctionState &PFS) {
  Type *Ty = nullptr; LocTy TyLoc;

  if (parseType(Ty, TyLoc))
    return true;

  std::unique_ptr<LandingPadInst> LP(LandingPadInst::Create(Ty, 0));
  LP->setCleanup(EatIfPresent(lltok::kw_cleanup));

  while (Lex.getKind() == lltok::kw_catch || Lex.getKind() == lltok::kw_filter){
    LandingPadInst::ClauseType CT;
    if (EatIfPresent(lltok::kw_catch))
      CT = LandingPadInst::Catch;
    else if (EatIfPresent(lltok::kw_filter))
      CT = LandingPadInst::Filter;
    else
      return tokError("expected 'catch' or 'filter' clause type");

    Value *V;
    LocTy VLoc;
    if (parseTypeAndValue(V, VLoc, PFS))
      return true;

    // A 'catch' type expects a non-array constant. A filter clause expects an
    // array constant.
    if (CT == LandingPadInst::Catch) {
      if (isa<ArrayType>(V->getType()))
        error(VLoc, "'catch' clause has an invalid type");
    } else {
      if (!isa<ArrayType>(V->getType()))
        error(VLoc, "'filter' clause has an invalid type");
    }

    Constant *CV = dyn_cast<Constant>(V);
    if (!CV)
      return error(VLoc, "clause argument must be a constant");
    LP->addClause(CV);
  }

  Inst = LP.release();
  return false;
}

/// parseFreeze
///   ::= 'freeze' Type Value
bool LLParser::parseFreeze(Instruction *&Inst, PerFunctionState &PFS) {
  LocTy Loc;
  Value *Op;
  if (parseTypeAndValue(Op, Loc, PFS))
    return true;

  Inst = new FreezeInst(Op);
  return false;
}

/// parseCall
///   ::= 'call' OptionalFastMathFlags OptionalCallingConv
///           OptionalAttrs Type Value ParameterList OptionalAttrs
///   ::= 'tail' 'call' OptionalFastMathFlags OptionalCallingConv
///           OptionalAttrs Type Value ParameterList OptionalAttrs
///   ::= 'musttail' 'call' OptionalFastMathFlags OptionalCallingConv
///           OptionalAttrs Type Value ParameterList OptionalAttrs
///   ::= 'notail' 'call'  OptionalFastMathFlags OptionalCallingConv
///           OptionalAttrs Type Value ParameterList OptionalAttrs
bool LLParser::parseCall(Instruction *&Inst, PerFunctionState &PFS,
                         CallInst::TailCallKind TCK) {
  AttrBuilder RetAttrs(M->getContext()), FnAttrs(M->getContext());
  std::vector<unsigned> FwdRefAttrGrps;
  LocTy BuiltinLoc;
  unsigned CallAddrSpace;
  unsigned CC;
  Type *RetType = nullptr;
  LocTy RetTypeLoc;
  ValID CalleeID;
  SmallVector<ParamInfo, 16> ArgList;
  SmallVector<OperandBundleDef, 2> BundleList;
  LocTy CallLoc = Lex.getLoc();

  if (TCK != CallInst::TCK_None &&
      parseToken(lltok::kw_call,
                 "expected 'tail call', 'musttail call', or 'notail call'"))
    return true;

  FastMathFlags FMF = EatFastMathFlagsIfPresent();

  if (parseOptionalCallingConv(CC) || parseOptionalReturnAttrs(RetAttrs) ||
      parseOptionalProgramAddrSpace(CallAddrSpace) ||
      parseType(RetType, RetTypeLoc, true /*void allowed*/) ||
      parseValID(CalleeID, &PFS) ||
      parseParameterList(ArgList, PFS, TCK == CallInst::TCK_MustTail,
                         PFS.getFunction().isVarArg()) ||
      parseFnAttributeValuePairs(FnAttrs, FwdRefAttrGrps, false, BuiltinLoc) ||
      parseOptionalOperandBundles(BundleList, PFS))
    return true;

  // If RetType is a non-function pointer type, then this is the short syntax
  // for the call, which means that RetType is just the return type.  Infer the
  // rest of the function argument types from the arguments that are present.
  FunctionType *Ty;
  if (resolveFunctionType(RetType, ArgList, Ty))
    return error(RetTypeLoc, "Invalid result type for LLVM function");

  CalleeID.FTy = Ty;

  // Look up the callee.
  Value *Callee;
  if (convertValIDToValue(PointerType::get(Ty, CallAddrSpace), CalleeID, Callee,
                          &PFS))
    return true;

  // Set up the Attribute for the function.
  SmallVector<AttributeSet, 8> Attrs;

  SmallVector<Value*, 8> Args;

  // Loop through FunctionType's arguments and ensure they are specified
  // correctly.  Also, gather any parameter attributes.
  FunctionType::param_iterator I = Ty->param_begin();
  FunctionType::param_iterator E = Ty->param_end();
  for (const ParamInfo &Arg : ArgList) {
    Type *ExpectedTy = nullptr;
    if (I != E) {
      ExpectedTy = *I++;
    } else if (!Ty->isVarArg()) {
      return error(Arg.Loc, "too many arguments specified");
    }

    if (ExpectedTy && ExpectedTy != Arg.V->getType())
      return error(Arg.Loc, "argument is not of expected type '" +
                                getTypeString(ExpectedTy) + "'");
    Args.push_back(Arg.V);
    Attrs.push_back(Arg.Attrs);
  }

  if (I != E)
    return error(CallLoc, "not enough parameters specified for call");

  // Finish off the Attribute and check them
  AttributeList PAL =
      AttributeList::get(Context, AttributeSet::get(Context, FnAttrs),
                         AttributeSet::get(Context, RetAttrs), Attrs);

  CallInst *CI = CallInst::Create(Ty, Callee, Args, BundleList);
  CI->setTailCallKind(TCK);
  CI->setCallingConv(CC);
  if (FMF.any()) {
    if (!isa<FPMathOperator>(CI)) {
      CI->deleteValue();
      return error(CallLoc, "fast-math-flags specified for call without "
                            "floating-point scalar or vector return type");
    }
    CI->setFastMathFlags(FMF);
  }

  if (CalleeID.Kind == ValID::t_GlobalName &&
      isOldDbgFormatIntrinsic(CalleeID.StrVal)) {
    if (SeenNewDbgInfoFormat) {
      CI->deleteValue();
      return error(CallLoc, "llvm.dbg intrinsic should not appear in a module "
                            "using non-intrinsic debug info");
    }
    if (!SeenOldDbgInfoFormat)
      M->setNewDbgInfoFormatFlag(false);
    SeenOldDbgInfoFormat = true;
  }
  CI->setAttributes(PAL);
  ForwardRefAttrGroups[CI] = FwdRefAttrGrps;
  Inst = CI;
  return false;
}

//===----------------------------------------------------------------------===//
// Memory Instructions.
//===----------------------------------------------------------------------===//

/// parseAlloc
///   ::= 'alloca' 'inalloca'? 'swifterror'? Type (',' TypeAndValue)?
///       (',' 'align' i32)? (',', 'addrspace(n))?
int LLParser::parseAlloc(Instruction *&Inst, PerFunctionState &PFS) {
  Value *Size = nullptr;
  LocTy SizeLoc, TyLoc, ASLoc;
  MaybeAlign Alignment;
  unsigned AddrSpace = 0;
  Type *Ty = nullptr;

  bool IsInAlloca = EatIfPresent(lltok::kw_inalloca);
  bool IsSwiftError = EatIfPresent(lltok::kw_swifterror);

  if (parseType(Ty, TyLoc))
    return true;

  if (Ty->isFunctionTy() || !PointerType::isValidElementType(Ty))
    return error(TyLoc, "invalid type for alloca");

  bool AteExtraComma = false;
  if (EatIfPresent(lltok::comma)) {
    if (Lex.getKind() == lltok::kw_align) {
      if (parseOptionalAlignment(Alignment))
        return true;
      if (parseOptionalCommaAddrSpace(AddrSpace, ASLoc, AteExtraComma))
        return true;
    } else if (Lex.getKind() == lltok::kw_addrspace) {
      ASLoc = Lex.getLoc();
      if (parseOptionalAddrSpace(AddrSpace))
        return true;
    } else if (Lex.getKind() == lltok::MetadataVar) {
      AteExtraComma = true;
    } else {
      if (parseTypeAndValue(Size, SizeLoc, PFS))
        return true;
      if (EatIfPresent(lltok::comma)) {
        if (Lex.getKind() == lltok::kw_align) {
          if (parseOptionalAlignment(Alignment))
            return true;
          if (parseOptionalCommaAddrSpace(AddrSpace, ASLoc, AteExtraComma))
            return true;
        } else if (Lex.getKind() == lltok::kw_addrspace) {
          ASLoc = Lex.getLoc();
          if (parseOptionalAddrSpace(AddrSpace))
            return true;
        } else if (Lex.getKind() == lltok::MetadataVar) {
          AteExtraComma = true;
        }
      }
    }
  }

  if (Size && !Size->getType()->isIntegerTy())
    return error(SizeLoc, "element count must have integer type");

  SmallPtrSet<Type *, 4> Visited;
  if (!Alignment && !Ty->isSized(&Visited))
    return error(TyLoc, "Cannot allocate unsized type");
  if (!Alignment)
    Alignment = M->getDataLayout().getPrefTypeAlign(Ty);
  AllocaInst *AI = new AllocaInst(Ty, AddrSpace, Size, *Alignment);
  AI->setUsedWithInAlloca(IsInAlloca);
  AI->setSwiftError(IsSwiftError);
  Inst = AI;
  return AteExtraComma ? InstExtraComma : InstNormal;
}

/// parseLoad
///   ::= 'load' 'volatile'? TypeAndValue (',' 'align' i32)?
///   ::= 'load' 'atomic' 'volatile'? TypeAndValue
///       'singlethread'? AtomicOrdering (',' 'align' i32)?
int LLParser::parseLoad(Instruction *&Inst, PerFunctionState &PFS) {
  Value *Val; LocTy Loc;
  MaybeAlign Alignment;
  bool AteExtraComma = false;
  bool isAtomic = false;
  AtomicOrdering Ordering = AtomicOrdering::NotAtomic;
  SyncScope::ID SSID = SyncScope::System;

  if (Lex.getKind() == lltok::kw_atomic) {
    isAtomic = true;
    Lex.Lex();
  }

  bool isVolatile = false;
  if (Lex.getKind() == lltok::kw_volatile) {
    isVolatile = true;
    Lex.Lex();
  }

  Type *Ty;
  LocTy ExplicitTypeLoc = Lex.getLoc();
  if (parseType(Ty) ||
      parseToken(lltok::comma, "expected comma after load's type") ||
      parseTypeAndValue(Val, Loc, PFS) ||
      parseScopeAndOrdering(isAtomic, SSID, Ordering) ||
      parseOptionalCommaAlign(Alignment, AteExtraComma))
    return true;

  if (!Val->getType()->isPointerTy() || !Ty->isFirstClassType())
    return error(Loc, "load operand must be a pointer to a first class type");
  if (isAtomic && !Alignment)
    return error(Loc, "atomic load must have explicit non-zero alignment");
  if (Ordering == AtomicOrdering::Release ||
      Ordering == AtomicOrdering::AcquireRelease)
    return error(Loc, "atomic load cannot use Release ordering");

  SmallPtrSet<Type *, 4> Visited;
  if (!Alignment && !Ty->isSized(&Visited))
    return error(ExplicitTypeLoc, "loading unsized types is not allowed");
  if (!Alignment)
    Alignment = M->getDataLayout().getABITypeAlign(Ty);
  Inst = new LoadInst(Ty, Val, "", isVolatile, *Alignment, Ordering, SSID);
  return AteExtraComma ? InstExtraComma : InstNormal;
}

/// parseStore

///   ::= 'store' 'volatile'? TypeAndValue ',' TypeAndValue (',' 'align' i32)?
///   ::= 'store' 'atomic' 'volatile'? TypeAndValue ',' TypeAndValue
///       'singlethread'? AtomicOrdering (',' 'align' i32)?
int LLParser::parseStore(Instruction *&Inst, PerFunctionState &PFS) {
  Value *Val, *Ptr; LocTy Loc, PtrLoc;
  MaybeAlign Alignment;
  bool AteExtraComma = false;
  bool isAtomic = false;
  AtomicOrdering Ordering = AtomicOrdering::NotAtomic;
  SyncScope::ID SSID = SyncScope::System;

  if (Lex.getKind() == lltok::kw_atomic) {
    isAtomic = true;
    Lex.Lex();
  }

  bool isVolatile = false;
  if (Lex.getKind() == lltok::kw_volatile) {
    isVolatile = true;
    Lex.Lex();
  }

  if (parseTypeAndValue(Val, Loc, PFS) ||
      parseToken(lltok::comma, "expected ',' after store operand") ||
      parseTypeAndValue(Ptr, PtrLoc, PFS) ||
      parseScopeAndOrdering(isAtomic, SSID, Ordering) ||
      parseOptionalCommaAlign(Alignment, AteExtraComma))
    return true;

  if (!Ptr->getType()->isPointerTy())
    return error(PtrLoc, "store operand must be a pointer");
  if (!Val->getType()->isFirstClassType())
    return error(Loc, "store operand must be a first class value");
  if (isAtomic && !Alignment)
    return error(Loc, "atomic store must have explicit non-zero alignment");
  if (Ordering == AtomicOrdering::Acquire ||
      Ordering == AtomicOrdering::AcquireRelease)
    return error(Loc, "atomic store cannot use Acquire ordering");
  SmallPtrSet<Type *, 4> Visited;
  if (!Alignment && !Val->getType()->isSized(&Visited))
    return error(Loc, "storing unsized types is not allowed");
  if (!Alignment)
    Alignment = M->getDataLayout().getABITypeAlign(Val->getType());

  Inst = new StoreInst(Val, Ptr, isVolatile, *Alignment, Ordering, SSID);
  return AteExtraComma ? InstExtraComma : InstNormal;
}

/// parseCmpXchg
///   ::= 'cmpxchg' 'weak'? 'volatile'? TypeAndValue ',' TypeAndValue ','
///       TypeAndValue 'singlethread'? AtomicOrdering AtomicOrdering ','
///       'Align'?
int LLParser::parseCmpXchg(Instruction *&Inst, PerFunctionState &PFS) {
  Value *Ptr, *Cmp, *New; LocTy PtrLoc, CmpLoc, NewLoc;
  bool AteExtraComma = false;
  AtomicOrdering SuccessOrdering = AtomicOrdering::NotAtomic;
  AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic;
  SyncScope::ID SSID = SyncScope::System;
  bool isVolatile = false;
  bool isWeak = false;
  MaybeAlign Alignment;

  if (EatIfPresent(lltok::kw_weak))
    isWeak = true;

  if (EatIfPresent(lltok::kw_volatile))
    isVolatile = true;

  if (parseTypeAndValue(Ptr, PtrLoc, PFS) ||
      parseToken(lltok::comma, "expected ',' after cmpxchg address") ||
      parseTypeAndValue(Cmp, CmpLoc, PFS) ||
      parseToken(lltok::comma, "expected ',' after cmpxchg cmp operand") ||
      parseTypeAndValue(New, NewLoc, PFS) ||
      parseScopeAndOrdering(true /*Always atomic*/, SSID, SuccessOrdering) ||
      parseOrdering(FailureOrdering) ||
      parseOptionalCommaAlign(Alignment, AteExtraComma))
    return true;

  if (!AtomicCmpXchgInst::isValidSuccessOrdering(SuccessOrdering))
    return tokError("invalid cmpxchg success ordering");
  if (!AtomicCmpXchgInst::isValidFailureOrdering(FailureOrdering))
    return tokError("invalid cmpxchg failure ordering");
  if (!Ptr->getType()->isPointerTy())
    return error(PtrLoc, "cmpxchg operand must be a pointer");
  if (Cmp->getType() != New->getType())
    return error(NewLoc, "compare value and new value type do not match");
  if (!New->getType()->isFirstClassType())
    return error(NewLoc, "cmpxchg operand must be a first class value");

  const Align DefaultAlignment(
      PFS.getFunction().getDataLayout().getTypeStoreSize(
          Cmp->getType()));

  AtomicCmpXchgInst *CXI =
      new AtomicCmpXchgInst(Ptr, Cmp, New, Alignment.value_or(DefaultAlignment),
                            SuccessOrdering, FailureOrdering, SSID);
  CXI->setVolatile(isVolatile);
  CXI->setWeak(isWeak);

  Inst = CXI;
  return AteExtraComma ? InstExtraComma : InstNormal;
}

/// parseAtomicRMW
///   ::= 'atomicrmw' 'volatile'? BinOp TypeAndValue ',' TypeAndValue
///       'singlethread'? AtomicOrdering
int LLParser::parseAtomicRMW(Instruction *&Inst, PerFunctionState &PFS) {
  Value *Ptr, *Val; LocTy PtrLoc, ValLoc;
  bool AteExtraComma = false;
  AtomicOrdering Ordering = AtomicOrdering::NotAtomic;
  SyncScope::ID SSID = SyncScope::System;
  bool isVolatile = false;
  bool IsFP = false;
  AtomicRMWInst::BinOp Operation;
  MaybeAlign Alignment;

  if (EatIfPresent(lltok::kw_volatile))
    isVolatile = true;

  switch (Lex.getKind()) {
  default:
    return tokError("expected binary operation in atomicrmw");
  case lltok::kw_xchg: Operation = AtomicRMWInst::Xchg; break;
  case lltok::kw_add: Operation = AtomicRMWInst::Add; break;
  case lltok::kw_sub: Operation = AtomicRMWInst::Sub; break;
  case lltok::kw_and: Operation = AtomicRMWInst::And; break;
  case lltok::kw_nand: Operation = AtomicRMWInst::Nand; break;
  case lltok::kw_or: Operation = AtomicRMWInst::Or; break;
  case lltok::kw_xor: Operation = AtomicRMWInst::Xor; break;
  case lltok::kw_max: Operation = AtomicRMWInst::Max; break;
  case lltok::kw_min: Operation = AtomicRMWInst::Min; break;
  case lltok::kw_umax: Operation = AtomicRMWInst::UMax; break;
  case lltok::kw_umin: Operation = AtomicRMWInst::UMin; break;
  case lltok::kw_uinc_wrap:
    Operation = AtomicRMWInst::UIncWrap;
    break;
  case lltok::kw_udec_wrap:
    Operation = AtomicRMWInst::UDecWrap;
    break;
  case lltok::kw_fadd:
    Operation = AtomicRMWInst::FAdd;
    IsFP = true;
    break;
  case lltok::kw_fsub:
    Operation = AtomicRMWInst::FSub;
    IsFP = true;
    break;
  case lltok::kw_fmax:
    Operation = AtomicRMWInst::FMax;
    IsFP = true;
    break;
  case lltok::kw_fmin:
    Operation = AtomicRMWInst::FMin;
    IsFP = true;
    break;
  }
  Lex.Lex();  // Eat the operation.

  if (parseTypeAndValue(Ptr, PtrLoc, PFS) ||
      parseToken(lltok::comma, "expected ',' after atomicrmw address") ||
      parseTypeAndValue(Val, ValLoc, PFS) ||
      parseScopeAndOrdering(true /*Always atomic*/, SSID, Ordering) ||
      parseOptionalCommaAlign(Alignment, AteExtraComma))
    return true;

  if (Ordering == AtomicOrdering::Unordered)
    return tokError("atomicrmw cannot be unordered");
  if (!Ptr->getType()->isPointerTy())
    return error(PtrLoc, "atomicrmw operand must be a pointer");
  if (Val->getType()->isScalableTy())
    return error(ValLoc, "atomicrmw operand may not be scalable");

  if (Operation == AtomicRMWInst::Xchg) {
    if (!Val->getType()->isIntegerTy() &&
        !Val->getType()->isFloatingPointTy() &&
        !Val->getType()->isPointerTy()) {
      return error(
          ValLoc,
          "atomicrmw " + AtomicRMWInst::getOperationName(Operation) +
              " operand must be an integer, floating point, or pointer type");
    }
  } else if (IsFP) {
    if (!Val->getType()->isFPOrFPVectorTy()) {
      return error(ValLoc, "atomicrmw " +
                               AtomicRMWInst::getOperationName(Operation) +
                               " operand must be a floating point type");
    }
  } else {
    if (!Val->getType()->isIntegerTy()) {
      return error(ValLoc, "atomicrmw " +
                               AtomicRMWInst::getOperationName(Operation) +
                               " operand must be an integer");
    }
  }

  unsigned Size =
      PFS.getFunction().getDataLayout().getTypeStoreSizeInBits(
          Val->getType());
  if (Size < 8 || (Size & (Size - 1)))
    return error(ValLoc, "atomicrmw operand must be power-of-two byte-sized"
                         " integer");
  const Align DefaultAlignment(
      PFS.getFunction().getDataLayout().getTypeStoreSize(
          Val->getType()));
  AtomicRMWInst *RMWI =
      new AtomicRMWInst(Operation, Ptr, Val,
                        Alignment.value_or(DefaultAlignment), Ordering, SSID);
  RMWI->setVolatile(isVolatile);
  Inst = RMWI;
  return AteExtraComma ? InstExtraComma : InstNormal;
}

/// parseFence
///   ::= 'fence' 'singlethread'? AtomicOrdering
int LLParser::parseFence(Instruction *&Inst, PerFunctionState &PFS) {
  AtomicOrdering Ordering = AtomicOrdering::NotAtomic;
  SyncScope::ID SSID = SyncScope::System;
  if (parseScopeAndOrdering(true /*Always atomic*/, SSID, Ordering))
    return true;

  if (Ordering == AtomicOrdering::Unordered)
    return tokError("fence cannot be unordered");
  if (Ordering == AtomicOrdering::Monotonic)
    return tokError("fence cannot be monotonic");

  Inst = new FenceInst(Context, Ordering, SSID);
  return InstNormal;
}

/// parseGetElementPtr
///   ::= 'getelementptr' 'inbounds'? TypeAndValue (',' TypeAndValue)*
int LLParser::parseGetElementPtr(Instruction *&Inst, PerFunctionState &PFS) {
  Value *Ptr = nullptr;
  Value *Val = nullptr;
  LocTy Loc, EltLoc;
  GEPNoWrapFlags NW;

  while (true) {
    if (EatIfPresent(lltok::kw_inbounds))
      NW |= GEPNoWrapFlags::inBounds();
    else if (EatIfPresent(lltok::kw_nusw))
      NW |= GEPNoWrapFlags::noUnsignedSignedWrap();
    else if (EatIfPresent(lltok::kw_nuw))
      NW |= GEPNoWrapFlags::noUnsignedWrap();
    else
      break;
  }

  Type *Ty = nullptr;
  if (parseType(Ty) ||
      parseToken(lltok::comma, "expected comma after getelementptr's type") ||
      parseTypeAndValue(Ptr, Loc, PFS))
    return true;

  Type *BaseType = Ptr->getType();
  PointerType *BasePointerType = dyn_cast<PointerType>(BaseType->getScalarType());
  if (!BasePointerType)
    return error(Loc, "base of getelementptr must be a pointer");

  SmallVector<Value*, 16> Indices;
  bool AteExtraComma = false;
  // GEP returns a vector of pointers if at least one of parameters is a vector.
  // All vector parameters should have the same vector width.
  ElementCount GEPWidth = BaseType->isVectorTy()
                              ? cast<VectorType>(BaseType)->getElementCount()
                              : ElementCount::getFixed(0);

  while (EatIfPresent(lltok::comma)) {
    if (Lex.getKind() == lltok::MetadataVar) {
      AteExtraComma = true;
      break;
    }
    if (parseTypeAndValue(Val, EltLoc, PFS))
      return true;
    if (!Val->getType()->isIntOrIntVectorTy())
      return error(EltLoc, "getelementptr index must be an integer");

    if (auto *ValVTy = dyn_cast<VectorType>(Val->getType())) {
      ElementCount ValNumEl = ValVTy->getElementCount();
      if (GEPWidth != ElementCount::getFixed(0) && GEPWidth != ValNumEl)
        return error(
            EltLoc,
            "getelementptr vector index has a wrong number of elements");
      GEPWidth = ValNumEl;
    }
    Indices.push_back(Val);
  }

  SmallPtrSet<Type*, 4> Visited;
  if (!Indices.empty() && !Ty->isSized(&Visited))
    return error(Loc, "base element of getelementptr must be sized");

  auto *STy = dyn_cast<StructType>(Ty);
  if (STy && STy->containsScalableVectorType())
    return error(Loc, "getelementptr cannot target structure that contains "
                      "scalable vector type");

  if (!GetElementPtrInst::getIndexedType(Ty, Indices))
    return error(Loc, "invalid getelementptr indices");
  GetElementPtrInst *GEP = GetElementPtrInst::Create(Ty, Ptr, Indices);
  Inst = GEP;
  GEP->setNoWrapFlags(NW);
  return AteExtraComma ? InstExtraComma : InstNormal;
}

/// parseExtractValue
///   ::= 'extractvalue' TypeAndValue (',' uint32)+
int LLParser::parseExtractValue(Instruction *&Inst, PerFunctionState &PFS) {
  Value *Val; LocTy Loc;
  SmallVector<unsigned, 4> Indices;
  bool AteExtraComma;
  if (parseTypeAndValue(Val, Loc, PFS) ||
      parseIndexList(Indices, AteExtraComma))
    return true;

  if (!Val->getType()->isAggregateType())
    return error(Loc, "extractvalue operand must be aggregate type");

  if (!ExtractValueInst::getIndexedType(Val->getType(), Indices))
    return error(Loc, "invalid indices for extractvalue");
  Inst = ExtractValueInst::Create(Val, Indices);
  return AteExtraComma ? InstExtraComma : InstNormal;
}

/// parseInsertValue
///   ::= 'insertvalue' TypeAndValue ',' TypeAndValue (',' uint32)+
int LLParser::parseInsertValue(Instruction *&Inst, PerFunctionState &PFS) {
  Value *Val0, *Val1; LocTy Loc0, Loc1;
  SmallVector<unsigned, 4> Indices;
  bool AteExtraComma;
  if (parseTypeAndValue(Val0, Loc0, PFS) ||
      parseToken(lltok::comma, "expected comma after insertvalue operand") ||
      parseTypeAndValue(Val1, Loc1, PFS) ||
      parseIndexList(Indices, AteExtraComma))
    return true;

  if (!Val0->getType()->isAggregateType())
    return error(Loc0, "insertvalue operand must be aggregate type");

  Type *IndexedType = ExtractValueInst::getIndexedType(Val0->getType(), Indices);
  if (!IndexedType)
    return error(Loc0, "invalid indices for insertvalue");
  if (IndexedType != Val1->getType())
    return error(Loc1, "insertvalue operand and field disagree in type: '" +
                           getTypeString(Val1->getType()) + "' instead of '" +
                           getTypeString(IndexedType) + "'");
  Inst = InsertValueInst::Create(Val0, Val1, Indices);
  return AteExtraComma ? InstExtraComma : InstNormal;
}

//===----------------------------------------------------------------------===//
// Embedded metadata.
//===----------------------------------------------------------------------===//

/// parseMDNodeVector
///   ::= { Element (',' Element)* }
/// Element
///   ::= 'null' | Metadata
bool LLParser::parseMDNodeVector(SmallVectorImpl<Metadata *> &Elts) {
  if (parseToken(lltok::lbrace, "expected '{' here"))
    return true;

  // Check for an empty list.
  if (EatIfPresent(lltok::rbrace))
    return false;

  do {
    if (EatIfPresent(lltok::kw_null)) {
      Elts.push_back(nullptr);
      continue;
    }

    Metadata *MD;
    if (parseMetadata(MD, nullptr))
      return true;
    Elts.push_back(MD);
  } while (EatIfPresent(lltok::comma));

  return parseToken(lltok::rbrace, "expected end of metadata node");
}

//===----------------------------------------------------------------------===//
// Use-list order directives.
//===----------------------------------------------------------------------===//
bool LLParser::sortUseListOrder(Value *V, ArrayRef<unsigned> Indexes,
                                SMLoc Loc) {
  if (V->use_empty())
    return error(Loc, "value has no uses");

  unsigned NumUses = 0;
  SmallDenseMap<const Use *, unsigned, 16> Order;
  for (const Use &U : V->uses()) {
    if (++NumUses > Indexes.size())
      break;
    Order[&U] = Indexes[NumUses - 1];
  }
  if (NumUses < 2)
    return error(Loc, "value only has one use");
  if (Order.size() != Indexes.size() || NumUses > Indexes.size())
    return error(Loc,
                 "wrong number of indexes, expected " + Twine(V->getNumUses()));

  V->sortUseList([&](const Use &L, const Use &R) {
    return Order.lookup(&L) < Order.lookup(&R);
  });
  return false;
}

/// parseUseListOrderIndexes
///   ::= '{' uint32 (',' uint32)+ '}'
bool LLParser::parseUseListOrderIndexes(SmallVectorImpl<unsigned> &Indexes) {
  SMLoc Loc = Lex.getLoc();
  if (parseToken(lltok::lbrace, "expected '{' here"))
    return true;
  if (Lex.getKind() == lltok::rbrace)
    return Lex.Error("expected non-empty list of uselistorder indexes");

  // Use Offset, Max, and IsOrdered to check consistency of indexes.  The
  // indexes should be distinct numbers in the range [0, size-1], and should
  // not be in order.
  unsigned Offset = 0;
  unsigned Max = 0;
  bool IsOrdered = true;
  assert(Indexes.empty() && "Expected empty order vector");
  do {
    unsigned Index;
    if (parseUInt32(Index))
      return true;

    // Update consistency checks.
    Offset += Index - Indexes.size();
    Max = std::max(Max, Index);
    IsOrdered &= Index == Indexes.size();

    Indexes.push_back(Index);
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rbrace, "expected '}' here"))
    return true;

  if (Indexes.size() < 2)
    return error(Loc, "expected >= 2 uselistorder indexes");
  if (Offset != 0 || Max >= Indexes.size())
    return error(Loc,
                 "expected distinct uselistorder indexes in range [0, size)");
  if (IsOrdered)
    return error(Loc, "expected uselistorder indexes to change the order");

  return false;
}

/// parseUseListOrder
///   ::= 'uselistorder' Type Value ',' UseListOrderIndexes
bool LLParser::parseUseListOrder(PerFunctionState *PFS) {
  SMLoc Loc = Lex.getLoc();
  if (parseToken(lltok::kw_uselistorder, "expected uselistorder directive"))
    return true;

  Value *V;
  SmallVector<unsigned, 16> Indexes;
  if (parseTypeAndValue(V, PFS) ||
      parseToken(lltok::comma, "expected comma in uselistorder directive") ||
      parseUseListOrderIndexes(Indexes))
    return true;

  return sortUseListOrder(V, Indexes, Loc);
}

/// parseUseListOrderBB
///   ::= 'uselistorder_bb' @foo ',' %bar ',' UseListOrderIndexes
bool LLParser::parseUseListOrderBB() {
  assert(Lex.getKind() == lltok::kw_uselistorder_bb);
  SMLoc Loc = Lex.getLoc();
  Lex.Lex();

  ValID Fn, Label;
  SmallVector<unsigned, 16> Indexes;
  if (parseValID(Fn, /*PFS=*/nullptr) ||
      parseToken(lltok::comma, "expected comma in uselistorder_bb directive") ||
      parseValID(Label, /*PFS=*/nullptr) ||
      parseToken(lltok::comma, "expected comma in uselistorder_bb directive") ||
      parseUseListOrderIndexes(Indexes))
    return true;

  // Check the function.
  GlobalValue *GV;
  if (Fn.Kind == ValID::t_GlobalName)
    GV = M->getNamedValue(Fn.StrVal);
  else if (Fn.Kind == ValID::t_GlobalID)
    GV = NumberedVals.get(Fn.UIntVal);
  else
    return error(Fn.Loc, "expected function name in uselistorder_bb");
  if (!GV)
    return error(Fn.Loc,
                 "invalid function forward reference in uselistorder_bb");
  auto *F = dyn_cast<Function>(GV);
  if (!F)
    return error(Fn.Loc, "expected function name in uselistorder_bb");
  if (F->isDeclaration())
    return error(Fn.Loc, "invalid declaration in uselistorder_bb");

  // Check the basic block.
  if (Label.Kind == ValID::t_LocalID)
    return error(Label.Loc, "invalid numeric label in uselistorder_bb");
  if (Label.Kind != ValID::t_LocalName)
    return error(Label.Loc, "expected basic block name in uselistorder_bb");
  Value *V = F->getValueSymbolTable()->lookup(Label.StrVal);
  if (!V)
    return error(Label.Loc, "invalid basic block in uselistorder_bb");
  if (!isa<BasicBlock>(V))
    return error(Label.Loc, "expected basic block in uselistorder_bb");

  return sortUseListOrder(V, Indexes, Loc);
}

/// ModuleEntry
///   ::= 'module' ':' '(' 'path' ':' STRINGCONSTANT ',' 'hash' ':' Hash ')'
/// Hash ::= '(' UInt32 ',' UInt32 ',' UInt32 ',' UInt32 ',' UInt32 ')'
bool LLParser::parseModuleEntry(unsigned ID) {
  assert(Lex.getKind() == lltok::kw_module);
  Lex.Lex();

  std::string Path;
  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here") ||
      parseToken(lltok::kw_path, "expected 'path' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseStringConstant(Path) ||
      parseToken(lltok::comma, "expected ',' here") ||
      parseToken(lltok::kw_hash, "expected 'hash' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;

  ModuleHash Hash;
  if (parseUInt32(Hash[0]) || parseToken(lltok::comma, "expected ',' here") ||
      parseUInt32(Hash[1]) || parseToken(lltok::comma, "expected ',' here") ||
      parseUInt32(Hash[2]) || parseToken(lltok::comma, "expected ',' here") ||
      parseUInt32(Hash[3]) || parseToken(lltok::comma, "expected ',' here") ||
      parseUInt32(Hash[4]))
    return true;

  if (parseToken(lltok::rparen, "expected ')' here") ||
      parseToken(lltok::rparen, "expected ')' here"))
    return true;

  auto ModuleEntry = Index->addModule(Path, Hash);
  ModuleIdMap[ID] = ModuleEntry->first();

  return false;
}

/// TypeIdEntry
///   ::= 'typeid' ':' '(' 'name' ':' STRINGCONSTANT ',' TypeIdSummary ')'
bool LLParser::parseTypeIdEntry(unsigned ID) {
  assert(Lex.getKind() == lltok::kw_typeid);
  Lex.Lex();

  std::string Name;
  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here") ||
      parseToken(lltok::kw_name, "expected 'name' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseStringConstant(Name))
    return true;

  TypeIdSummary &TIS = Index->getOrInsertTypeIdSummary(Name);
  if (parseToken(lltok::comma, "expected ',' here") ||
      parseTypeIdSummary(TIS) || parseToken(lltok::rparen, "expected ')' here"))
    return true;

  // Check if this ID was forward referenced, and if so, update the
  // corresponding GUIDs.
  auto FwdRefTIDs = ForwardRefTypeIds.find(ID);
  if (FwdRefTIDs != ForwardRefTypeIds.end()) {
    for (auto TIDRef : FwdRefTIDs->second) {
      assert(!*TIDRef.first &&
             "Forward referenced type id GUID expected to be 0");
      *TIDRef.first = GlobalValue::getGUID(Name);
    }
    ForwardRefTypeIds.erase(FwdRefTIDs);
  }

  return false;
}

/// TypeIdSummary
///   ::= 'summary' ':' '(' TypeTestResolution [',' OptionalWpdResolutions]? ')'
bool LLParser::parseTypeIdSummary(TypeIdSummary &TIS) {
  if (parseToken(lltok::kw_summary, "expected 'summary' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here") ||
      parseTypeTestResolution(TIS.TTRes))
    return true;

  if (EatIfPresent(lltok::comma)) {
    // Expect optional wpdResolutions field
    if (parseOptionalWpdResolutions(TIS.WPDRes))
      return true;
  }

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  return false;
}

static ValueInfo EmptyVI =
    ValueInfo(false, (GlobalValueSummaryMapTy::value_type *)-8);

/// TypeIdCompatibleVtableEntry
///   ::= 'typeidCompatibleVTable' ':' '(' 'name' ':' STRINGCONSTANT ','
///   TypeIdCompatibleVtableInfo
///   ')'
bool LLParser::parseTypeIdCompatibleVtableEntry(unsigned ID) {
  assert(Lex.getKind() == lltok::kw_typeidCompatibleVTable);
  Lex.Lex();

  std::string Name;
  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here") ||
      parseToken(lltok::kw_name, "expected 'name' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseStringConstant(Name))
    return true;

  TypeIdCompatibleVtableInfo &TI =
      Index->getOrInsertTypeIdCompatibleVtableSummary(Name);
  if (parseToken(lltok::comma, "expected ',' here") ||
      parseToken(lltok::kw_summary, "expected 'summary' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;

  IdToIndexMapType IdToIndexMap;
  // parse each call edge
  do {
    uint64_t Offset;
    if (parseToken(lltok::lparen, "expected '(' here") ||
        parseToken(lltok::kw_offset, "expected 'offset' here") ||
        parseToken(lltok::colon, "expected ':' here") || parseUInt64(Offset) ||
        parseToken(lltok::comma, "expected ',' here"))
      return true;

    LocTy Loc = Lex.getLoc();
    unsigned GVId;
    ValueInfo VI;
    if (parseGVReference(VI, GVId))
      return true;

    // Keep track of the TypeIdCompatibleVtableInfo array index needing a
    // forward reference. We will save the location of the ValueInfo needing an
    // update, but can only do so once the std::vector is finalized.
    if (VI == EmptyVI)
      IdToIndexMap[GVId].push_back(std::make_pair(TI.size(), Loc));
    TI.push_back({Offset, VI});

    if (parseToken(lltok::rparen, "expected ')' in call"))
      return true;
  } while (EatIfPresent(lltok::comma));

  // Now that the TI vector is finalized, it is safe to save the locations
  // of any forward GV references that need updating later.
  for (auto I : IdToIndexMap) {
    auto &Infos = ForwardRefValueInfos[I.first];
    for (auto P : I.second) {
      assert(TI[P.first].VTableVI == EmptyVI &&
             "Forward referenced ValueInfo expected to be empty");
      Infos.emplace_back(&TI[P.first].VTableVI, P.second);
    }
  }

  if (parseToken(lltok::rparen, "expected ')' here") ||
      parseToken(lltok::rparen, "expected ')' here"))
    return true;

  // Check if this ID was forward referenced, and if so, update the
  // corresponding GUIDs.
  auto FwdRefTIDs = ForwardRefTypeIds.find(ID);
  if (FwdRefTIDs != ForwardRefTypeIds.end()) {
    for (auto TIDRef : FwdRefTIDs->second) {
      assert(!*TIDRef.first &&
             "Forward referenced type id GUID expected to be 0");
      *TIDRef.first = GlobalValue::getGUID(Name);
    }
    ForwardRefTypeIds.erase(FwdRefTIDs);
  }

  return false;
}

/// TypeTestResolution
///   ::= 'typeTestRes' ':' '(' 'kind' ':'
///         ( 'unsat' | 'byteArray' | 'inline' | 'single' | 'allOnes' ) ','
///         'sizeM1BitWidth' ':' SizeM1BitWidth [',' 'alignLog2' ':' UInt64]?
///         [',' 'sizeM1' ':' UInt64]? [',' 'bitMask' ':' UInt8]?
///         [',' 'inlinesBits' ':' UInt64]? ')'
bool LLParser::parseTypeTestResolution(TypeTestResolution &TTRes) {
  if (parseToken(lltok::kw_typeTestRes, "expected 'typeTestRes' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here") ||
      parseToken(lltok::kw_kind, "expected 'kind' here") ||
      parseToken(lltok::colon, "expected ':' here"))
    return true;

  switch (Lex.getKind()) {
  case lltok::kw_unknown:
    TTRes.TheKind = TypeTestResolution::Unknown;
    break;
  case lltok::kw_unsat:
    TTRes.TheKind = TypeTestResolution::Unsat;
    break;
  case lltok::kw_byteArray:
    TTRes.TheKind = TypeTestResolution::ByteArray;
    break;
  case lltok::kw_inline:
    TTRes.TheKind = TypeTestResolution::Inline;
    break;
  case lltok::kw_single:
    TTRes.TheKind = TypeTestResolution::Single;
    break;
  case lltok::kw_allOnes:
    TTRes.TheKind = TypeTestResolution::AllOnes;
    break;
  default:
    return error(Lex.getLoc(), "unexpected TypeTestResolution kind");
  }
  Lex.Lex();

  if (parseToken(lltok::comma, "expected ',' here") ||
      parseToken(lltok::kw_sizeM1BitWidth, "expected 'sizeM1BitWidth' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseUInt32(TTRes.SizeM1BitWidth))
    return true;

  // parse optional fields
  while (EatIfPresent(lltok::comma)) {
    switch (Lex.getKind()) {
    case lltok::kw_alignLog2:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") ||
          parseUInt64(TTRes.AlignLog2))
        return true;
      break;
    case lltok::kw_sizeM1:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseUInt64(TTRes.SizeM1))
        return true;
      break;
    case lltok::kw_bitMask: {
      unsigned Val;
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseUInt32(Val))
        return true;
      assert(Val <= 0xff);
      TTRes.BitMask = (uint8_t)Val;
      break;
    }
    case lltok::kw_inlineBits:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") ||
          parseUInt64(TTRes.InlineBits))
        return true;
      break;
    default:
      return error(Lex.getLoc(), "expected optional TypeTestResolution field");
    }
  }

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  return false;
}

/// OptionalWpdResolutions
///   ::= 'wpsResolutions' ':' '(' WpdResolution [',' WpdResolution]* ')'
/// WpdResolution ::= '(' 'offset' ':' UInt64 ',' WpdRes ')'
bool LLParser::parseOptionalWpdResolutions(
    std::map<uint64_t, WholeProgramDevirtResolution> &WPDResMap) {
  if (parseToken(lltok::kw_wpdResolutions, "expected 'wpdResolutions' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;

  do {
    uint64_t Offset;
    WholeProgramDevirtResolution WPDRes;
    if (parseToken(lltok::lparen, "expected '(' here") ||
        parseToken(lltok::kw_offset, "expected 'offset' here") ||
        parseToken(lltok::colon, "expected ':' here") || parseUInt64(Offset) ||
        parseToken(lltok::comma, "expected ',' here") || parseWpdRes(WPDRes) ||
        parseToken(lltok::rparen, "expected ')' here"))
      return true;
    WPDResMap[Offset] = WPDRes;
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  return false;
}

/// WpdRes
///   ::= 'wpdRes' ':' '(' 'kind' ':' 'indir'
///         [',' OptionalResByArg]? ')'
///   ::= 'wpdRes' ':' '(' 'kind' ':' 'singleImpl'
///         ',' 'singleImplName' ':' STRINGCONSTANT ','
///         [',' OptionalResByArg]? ')'
///   ::= 'wpdRes' ':' '(' 'kind' ':' 'branchFunnel'
///         [',' OptionalResByArg]? ')'
bool LLParser::parseWpdRes(WholeProgramDevirtResolution &WPDRes) {
  if (parseToken(lltok::kw_wpdRes, "expected 'wpdRes' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here") ||
      parseToken(lltok::kw_kind, "expected 'kind' here") ||
      parseToken(lltok::colon, "expected ':' here"))
    return true;

  switch (Lex.getKind()) {
  case lltok::kw_indir:
    WPDRes.TheKind = WholeProgramDevirtResolution::Indir;
    break;
  case lltok::kw_singleImpl:
    WPDRes.TheKind = WholeProgramDevirtResolution::SingleImpl;
    break;
  case lltok::kw_branchFunnel:
    WPDRes.TheKind = WholeProgramDevirtResolution::BranchFunnel;
    break;
  default:
    return error(Lex.getLoc(), "unexpected WholeProgramDevirtResolution kind");
  }
  Lex.Lex();

  // parse optional fields
  while (EatIfPresent(lltok::comma)) {
    switch (Lex.getKind()) {
    case lltok::kw_singleImplName:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':' here") ||
          parseStringConstant(WPDRes.SingleImplName))
        return true;
      break;
    case lltok::kw_resByArg:
      if (parseOptionalResByArg(WPDRes.ResByArg))
        return true;
      break;
    default:
      return error(Lex.getLoc(),
                   "expected optional WholeProgramDevirtResolution field");
    }
  }

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  return false;
}

/// OptionalResByArg
///   ::= 'wpdRes' ':' '(' ResByArg[, ResByArg]* ')'
/// ResByArg ::= Args ',' 'byArg' ':' '(' 'kind' ':'
///                ( 'indir' | 'uniformRetVal' | 'UniqueRetVal' |
///                  'virtualConstProp' )
///                [',' 'info' ':' UInt64]? [',' 'byte' ':' UInt32]?
///                [',' 'bit' ':' UInt32]? ')'
bool LLParser::parseOptionalResByArg(
    std::map<std::vector<uint64_t>, WholeProgramDevirtResolution::ByArg>
        &ResByArg) {
  if (parseToken(lltok::kw_resByArg, "expected 'resByArg' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;

  do {
    std::vector<uint64_t> Args;
    if (parseArgs(Args) || parseToken(lltok::comma, "expected ',' here") ||
        parseToken(lltok::kw_byArg, "expected 'byArg here") ||
        parseToken(lltok::colon, "expected ':' here") ||
        parseToken(lltok::lparen, "expected '(' here") ||
        parseToken(lltok::kw_kind, "expected 'kind' here") ||
        parseToken(lltok::colon, "expected ':' here"))
      return true;

    WholeProgramDevirtResolution::ByArg ByArg;
    switch (Lex.getKind()) {
    case lltok::kw_indir:
      ByArg.TheKind = WholeProgramDevirtResolution::ByArg::Indir;
      break;
    case lltok::kw_uniformRetVal:
      ByArg.TheKind = WholeProgramDevirtResolution::ByArg::UniformRetVal;
      break;
    case lltok::kw_uniqueRetVal:
      ByArg.TheKind = WholeProgramDevirtResolution::ByArg::UniqueRetVal;
      break;
    case lltok::kw_virtualConstProp:
      ByArg.TheKind = WholeProgramDevirtResolution::ByArg::VirtualConstProp;
      break;
    default:
      return error(Lex.getLoc(),
                   "unexpected WholeProgramDevirtResolution::ByArg kind");
    }
    Lex.Lex();

    // parse optional fields
    while (EatIfPresent(lltok::comma)) {
      switch (Lex.getKind()) {
      case lltok::kw_info:
        Lex.Lex();
        if (parseToken(lltok::colon, "expected ':' here") ||
            parseUInt64(ByArg.Info))
          return true;
        break;
      case lltok::kw_byte:
        Lex.Lex();
        if (parseToken(lltok::colon, "expected ':' here") ||
            parseUInt32(ByArg.Byte))
          return true;
        break;
      case lltok::kw_bit:
        Lex.Lex();
        if (parseToken(lltok::colon, "expected ':' here") ||
            parseUInt32(ByArg.Bit))
          return true;
        break;
      default:
        return error(Lex.getLoc(),
                     "expected optional whole program devirt field");
      }
    }

    if (parseToken(lltok::rparen, "expected ')' here"))
      return true;

    ResByArg[Args] = ByArg;
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  return false;
}

/// OptionalResByArg
///   ::= 'args' ':' '(' UInt64[, UInt64]* ')'
bool LLParser::parseArgs(std::vector<uint64_t> &Args) {
  if (parseToken(lltok::kw_args, "expected 'args' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;

  do {
    uint64_t Val;
    if (parseUInt64(Val))
      return true;
    Args.push_back(Val);
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  return false;
}

static const auto FwdVIRef = (GlobalValueSummaryMapTy::value_type *)-8;

static void resolveFwdRef(ValueInfo *Fwd, ValueInfo &Resolved) {
  bool ReadOnly = Fwd->isReadOnly();
  bool WriteOnly = Fwd->isWriteOnly();
  assert(!(ReadOnly && WriteOnly));
  *Fwd = Resolved;
  if (ReadOnly)
    Fwd->setReadOnly();
  if (WriteOnly)
    Fwd->setWriteOnly();
}

/// Stores the given Name/GUID and associated summary into the Index.
/// Also updates any forward references to the associated entry ID.
bool LLParser::addGlobalValueToIndex(
    std::string Name, GlobalValue::GUID GUID, GlobalValue::LinkageTypes Linkage,
    unsigned ID, std::unique_ptr<GlobalValueSummary> Summary, LocTy Loc) {
  // First create the ValueInfo utilizing the Name or GUID.
  ValueInfo VI;
  if (GUID != 0) {
    assert(Name.empty());
    VI = Index->getOrInsertValueInfo(GUID);
  } else {
    assert(!Name.empty());
    if (M) {
      auto *GV = M->getNamedValue(Name);
      if (!GV)
        return error(Loc, "Reference to undefined global \"" + Name + "\"");

      VI = Index->getOrInsertValueInfo(GV);
    } else {
      assert(
          (!GlobalValue::isLocalLinkage(Linkage) || !SourceFileName.empty()) &&
          "Need a source_filename to compute GUID for local");
      GUID = GlobalValue::getGUID(
          GlobalValue::getGlobalIdentifier(Name, Linkage, SourceFileName));
      VI = Index->getOrInsertValueInfo(GUID, Index->saveString(Name));
    }
  }

  // Resolve forward references from calls/refs
  auto FwdRefVIs = ForwardRefValueInfos.find(ID);
  if (FwdRefVIs != ForwardRefValueInfos.end()) {
    for (auto VIRef : FwdRefVIs->second) {
      assert(VIRef.first->getRef() == FwdVIRef &&
             "Forward referenced ValueInfo expected to be empty");
      resolveFwdRef(VIRef.first, VI);
    }
    ForwardRefValueInfos.erase(FwdRefVIs);
  }

  // Resolve forward references from aliases
  auto FwdRefAliasees = ForwardRefAliasees.find(ID);
  if (FwdRefAliasees != ForwardRefAliasees.end()) {
    for (auto AliaseeRef : FwdRefAliasees->second) {
      assert(!AliaseeRef.first->hasAliasee() &&
             "Forward referencing alias already has aliasee");
      assert(Summary && "Aliasee must be a definition");
      AliaseeRef.first->setAliasee(VI, Summary.get());
    }
    ForwardRefAliasees.erase(FwdRefAliasees);
  }

  // Add the summary if one was provided.
  if (Summary)
    Index->addGlobalValueSummary(VI, std::move(Summary));

  // Save the associated ValueInfo for use in later references by ID.
  if (ID == NumberedValueInfos.size())
    NumberedValueInfos.push_back(VI);
  else {
    // Handle non-continuous numbers (to make test simplification easier).
    if (ID > NumberedValueInfos.size())
      NumberedValueInfos.resize(ID + 1);
    NumberedValueInfos[ID] = VI;
  }

  return false;
}

/// parseSummaryIndexFlags
///   ::= 'flags' ':' UInt64
bool LLParser::parseSummaryIndexFlags() {
  assert(Lex.getKind() == lltok::kw_flags);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' here"))
    return true;
  uint64_t Flags;
  if (parseUInt64(Flags))
    return true;
  if (Index)
    Index->setFlags(Flags);
  return false;
}

/// parseBlockCount
///   ::= 'blockcount' ':' UInt64
bool LLParser::parseBlockCount() {
  assert(Lex.getKind() == lltok::kw_blockcount);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' here"))
    return true;
  uint64_t BlockCount;
  if (parseUInt64(BlockCount))
    return true;
  if (Index)
    Index->setBlockCount(BlockCount);
  return false;
}

/// parseGVEntry
///   ::= 'gv' ':' '(' ('name' ':' STRINGCONSTANT | 'guid' ':' UInt64)
///         [',' 'summaries' ':' Summary[',' Summary]* ]? ')'
/// Summary ::= '(' (FunctionSummary | VariableSummary | AliasSummary) ')'
bool LLParser::parseGVEntry(unsigned ID) {
  assert(Lex.getKind() == lltok::kw_gv);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;

  LocTy Loc = Lex.getLoc();
  std::string Name;
  GlobalValue::GUID GUID = 0;
  switch (Lex.getKind()) {
  case lltok::kw_name:
    Lex.Lex();
    if (parseToken(lltok::colon, "expected ':' here") ||
        parseStringConstant(Name))
      return true;
    // Can't create GUID/ValueInfo until we have the linkage.
    break;
  case lltok::kw_guid:
    Lex.Lex();
    if (parseToken(lltok::colon, "expected ':' here") || parseUInt64(GUID))
      return true;
    break;
  default:
    return error(Lex.getLoc(), "expected name or guid tag");
  }

  if (!EatIfPresent(lltok::comma)) {
    // No summaries. Wrap up.
    if (parseToken(lltok::rparen, "expected ')' here"))
      return true;
    // This was created for a call to an external or indirect target.
    // A GUID with no summary came from a VALUE_GUID record, dummy GUID
    // created for indirect calls with VP. A Name with no GUID came from
    // an external definition. We pass ExternalLinkage since that is only
    // used when the GUID must be computed from Name, and in that case
    // the symbol must have external linkage.
    return addGlobalValueToIndex(Name, GUID, GlobalValue::ExternalLinkage, ID,
                                 nullptr, Loc);
  }

  // Have a list of summaries
  if (parseToken(lltok::kw_summaries, "expected 'summaries' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;
  do {
    switch (Lex.getKind()) {
    case lltok::kw_function:
      if (parseFunctionSummary(Name, GUID, ID))
        return true;
      break;
    case lltok::kw_variable:
      if (parseVariableSummary(Name, GUID, ID))
        return true;
      break;
    case lltok::kw_alias:
      if (parseAliasSummary(Name, GUID, ID))
        return true;
      break;
    default:
      return error(Lex.getLoc(), "expected summary type");
    }
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' here") ||
      parseToken(lltok::rparen, "expected ')' here"))
    return true;

  return false;
}

/// FunctionSummary
///   ::= 'function' ':' '(' 'module' ':' ModuleReference ',' GVFlags
///         ',' 'insts' ':' UInt32 [',' OptionalFFlags]? [',' OptionalCalls]?
///         [',' OptionalTypeIdInfo]? [',' OptionalParamAccesses]?
///         [',' OptionalRefs]? ')'
bool LLParser::parseFunctionSummary(std::string Name, GlobalValue::GUID GUID,
                                    unsigned ID) {
  LocTy Loc = Lex.getLoc();
  assert(Lex.getKind() == lltok::kw_function);
  Lex.Lex();

  StringRef ModulePath;
  GlobalValueSummary::GVFlags GVFlags = GlobalValueSummary::GVFlags(
      GlobalValue::ExternalLinkage, GlobalValue::DefaultVisibility,
      /*NotEligibleToImport=*/false,
      /*Live=*/false, /*IsLocal=*/false, /*CanAutoHide=*/false,
      GlobalValueSummary::Definition);
  unsigned InstCount;
  std::vector<FunctionSummary::EdgeTy> Calls;
  FunctionSummary::TypeIdInfo TypeIdInfo;
  std::vector<FunctionSummary::ParamAccess> ParamAccesses;
  std::vector<ValueInfo> Refs;
  std::vector<CallsiteInfo> Callsites;
  std::vector<AllocInfo> Allocs;
  // Default is all-zeros (conservative values).
  FunctionSummary::FFlags FFlags = {};
  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here") ||
      parseModuleReference(ModulePath) ||
      parseToken(lltok::comma, "expected ',' here") || parseGVFlags(GVFlags) ||
      parseToken(lltok::comma, "expected ',' here") ||
      parseToken(lltok::kw_insts, "expected 'insts' here") ||
      parseToken(lltok::colon, "expected ':' here") || parseUInt32(InstCount))
    return true;

  // parse optional fields
  while (EatIfPresent(lltok::comma)) {
    switch (Lex.getKind()) {
    case lltok::kw_funcFlags:
      if (parseOptionalFFlags(FFlags))
        return true;
      break;
    case lltok::kw_calls:
      if (parseOptionalCalls(Calls))
        return true;
      break;
    case lltok::kw_typeIdInfo:
      if (parseOptionalTypeIdInfo(TypeIdInfo))
        return true;
      break;
    case lltok::kw_refs:
      if (parseOptionalRefs(Refs))
        return true;
      break;
    case lltok::kw_params:
      if (parseOptionalParamAccesses(ParamAccesses))
        return true;
      break;
    case lltok::kw_allocs:
      if (parseOptionalAllocs(Allocs))
        return true;
      break;
    case lltok::kw_callsites:
      if (parseOptionalCallsites(Callsites))
        return true;
      break;
    default:
      return error(Lex.getLoc(), "expected optional function summary field");
    }
  }

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  auto FS = std::make_unique<FunctionSummary>(
      GVFlags, InstCount, FFlags, /*EntryCount=*/0, std::move(Refs),
      std::move(Calls), std::move(TypeIdInfo.TypeTests),
      std::move(TypeIdInfo.TypeTestAssumeVCalls),
      std::move(TypeIdInfo.TypeCheckedLoadVCalls),
      std::move(TypeIdInfo.TypeTestAssumeConstVCalls),
      std::move(TypeIdInfo.TypeCheckedLoadConstVCalls),
      std::move(ParamAccesses), std::move(Callsites), std::move(Allocs));

  FS->setModulePath(ModulePath);

  return addGlobalValueToIndex(Name, GUID,
                               (GlobalValue::LinkageTypes)GVFlags.Linkage, ID,
                               std::move(FS), Loc);
}

/// VariableSummary
///   ::= 'variable' ':' '(' 'module' ':' ModuleReference ',' GVFlags
///         [',' OptionalRefs]? ')'
bool LLParser::parseVariableSummary(std::string Name, GlobalValue::GUID GUID,
                                    unsigned ID) {
  LocTy Loc = Lex.getLoc();
  assert(Lex.getKind() == lltok::kw_variable);
  Lex.Lex();

  StringRef ModulePath;
  GlobalValueSummary::GVFlags GVFlags = GlobalValueSummary::GVFlags(
      GlobalValue::ExternalLinkage, GlobalValue::DefaultVisibility,
      /*NotEligibleToImport=*/false,
      /*Live=*/false, /*IsLocal=*/false, /*CanAutoHide=*/false,
      GlobalValueSummary::Definition);
  GlobalVarSummary::GVarFlags GVarFlags(/*ReadOnly*/ false,
                                        /* WriteOnly */ false,
                                        /* Constant */ false,
                                        GlobalObject::VCallVisibilityPublic);
  std::vector<ValueInfo> Refs;
  VTableFuncList VTableFuncs;
  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here") ||
      parseModuleReference(ModulePath) ||
      parseToken(lltok::comma, "expected ',' here") || parseGVFlags(GVFlags) ||
      parseToken(lltok::comma, "expected ',' here") ||
      parseGVarFlags(GVarFlags))
    return true;

  // parse optional fields
  while (EatIfPresent(lltok::comma)) {
    switch (Lex.getKind()) {
    case lltok::kw_vTableFuncs:
      if (parseOptionalVTableFuncs(VTableFuncs))
        return true;
      break;
    case lltok::kw_refs:
      if (parseOptionalRefs(Refs))
        return true;
      break;
    default:
      return error(Lex.getLoc(), "expected optional variable summary field");
    }
  }

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  auto GS =
      std::make_unique<GlobalVarSummary>(GVFlags, GVarFlags, std::move(Refs));

  GS->setModulePath(ModulePath);
  GS->setVTableFuncs(std::move(VTableFuncs));

  return addGlobalValueToIndex(Name, GUID,
                               (GlobalValue::LinkageTypes)GVFlags.Linkage, ID,
                               std::move(GS), Loc);
}

/// AliasSummary
///   ::= 'alias' ':' '(' 'module' ':' ModuleReference ',' GVFlags ','
///         'aliasee' ':' GVReference ')'
bool LLParser::parseAliasSummary(std::string Name, GlobalValue::GUID GUID,
                                 unsigned ID) {
  assert(Lex.getKind() == lltok::kw_alias);
  LocTy Loc = Lex.getLoc();
  Lex.Lex();

  StringRef ModulePath;
  GlobalValueSummary::GVFlags GVFlags = GlobalValueSummary::GVFlags(
      GlobalValue::ExternalLinkage, GlobalValue::DefaultVisibility,
      /*NotEligibleToImport=*/false,
      /*Live=*/false, /*IsLocal=*/false, /*CanAutoHide=*/false,
      GlobalValueSummary::Definition);
  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here") ||
      parseModuleReference(ModulePath) ||
      parseToken(lltok::comma, "expected ',' here") || parseGVFlags(GVFlags) ||
      parseToken(lltok::comma, "expected ',' here") ||
      parseToken(lltok::kw_aliasee, "expected 'aliasee' here") ||
      parseToken(lltok::colon, "expected ':' here"))
    return true;

  ValueInfo AliaseeVI;
  unsigned GVId;
  if (parseGVReference(AliaseeVI, GVId))
    return true;

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  auto AS = std::make_unique<AliasSummary>(GVFlags);

  AS->setModulePath(ModulePath);

  // Record forward reference if the aliasee is not parsed yet.
  if (AliaseeVI.getRef() == FwdVIRef) {
    ForwardRefAliasees[GVId].emplace_back(AS.get(), Loc);
  } else {
    auto Summary = Index->findSummaryInModule(AliaseeVI, ModulePath);
    assert(Summary && "Aliasee must be a definition");
    AS->setAliasee(AliaseeVI, Summary);
  }

  return addGlobalValueToIndex(Name, GUID,
                               (GlobalValue::LinkageTypes)GVFlags.Linkage, ID,
                               std::move(AS), Loc);
}

/// Flag
///   ::= [0|1]
bool LLParser::parseFlag(unsigned &Val) {
  if (Lex.getKind() != lltok::APSInt || Lex.getAPSIntVal().isSigned())
    return tokError("expected integer");
  Val = (unsigned)Lex.getAPSIntVal().getBoolValue();
  Lex.Lex();
  return false;
}

/// OptionalFFlags
///   := 'funcFlags' ':' '(' ['readNone' ':' Flag]?
///        [',' 'readOnly' ':' Flag]? [',' 'noRecurse' ':' Flag]?
///        [',' 'returnDoesNotAlias' ':' Flag]? ')'
///        [',' 'noInline' ':' Flag]? ')'
///        [',' 'alwaysInline' ':' Flag]? ')'
///        [',' 'noUnwind' ':' Flag]? ')'
///        [',' 'mayThrow' ':' Flag]? ')'
///        [',' 'hasUnknownCall' ':' Flag]? ')'
///        [',' 'mustBeUnreachable' ':' Flag]? ')'

bool LLParser::parseOptionalFFlags(FunctionSummary::FFlags &FFlags) {
  assert(Lex.getKind() == lltok::kw_funcFlags);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' in funcFlags") ||
      parseToken(lltok::lparen, "expected '(' in funcFlags"))
    return true;

  do {
    unsigned Val = 0;
    switch (Lex.getKind()) {
    case lltok::kw_readNone:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Val))
        return true;
      FFlags.ReadNone = Val;
      break;
    case lltok::kw_readOnly:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Val))
        return true;
      FFlags.ReadOnly = Val;
      break;
    case lltok::kw_noRecurse:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Val))
        return true;
      FFlags.NoRecurse = Val;
      break;
    case lltok::kw_returnDoesNotAlias:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Val))
        return true;
      FFlags.ReturnDoesNotAlias = Val;
      break;
    case lltok::kw_noInline:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Val))
        return true;
      FFlags.NoInline = Val;
      break;
    case lltok::kw_alwaysInline:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Val))
        return true;
      FFlags.AlwaysInline = Val;
      break;
    case lltok::kw_noUnwind:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Val))
        return true;
      FFlags.NoUnwind = Val;
      break;
    case lltok::kw_mayThrow:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Val))
        return true;
      FFlags.MayThrow = Val;
      break;
    case lltok::kw_hasUnknownCall:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Val))
        return true;
      FFlags.HasUnknownCall = Val;
      break;
    case lltok::kw_mustBeUnreachable:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Val))
        return true;
      FFlags.MustBeUnreachable = Val;
      break;
    default:
      return error(Lex.getLoc(), "expected function flag type");
    }
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' in funcFlags"))
    return true;

  return false;
}

/// OptionalCalls
///   := 'calls' ':' '(' Call [',' Call]* ')'
/// Call ::= '(' 'callee' ':' GVReference
///            [( ',' 'hotness' ':' Hotness | ',' 'relbf' ':' UInt32 )]?
///            [ ',' 'tail' ]? ')'
bool LLParser::parseOptionalCalls(std::vector<FunctionSummary::EdgeTy> &Calls) {
  assert(Lex.getKind() == lltok::kw_calls);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' in calls") ||
      parseToken(lltok::lparen, "expected '(' in calls"))
    return true;

  IdToIndexMapType IdToIndexMap;
  // parse each call edge
  do {
    ValueInfo VI;
    if (parseToken(lltok::lparen, "expected '(' in call") ||
        parseToken(lltok::kw_callee, "expected 'callee' in call") ||
        parseToken(lltok::colon, "expected ':'"))
      return true;

    LocTy Loc = Lex.getLoc();
    unsigned GVId;
    if (parseGVReference(VI, GVId))
      return true;

    CalleeInfo::HotnessType Hotness = CalleeInfo::HotnessType::Unknown;
    unsigned RelBF = 0;
    unsigned HasTailCall = false;

    // parse optional fields
    while (EatIfPresent(lltok::comma)) {
      switch (Lex.getKind()) {
      case lltok::kw_hotness:
        Lex.Lex();
        if (parseToken(lltok::colon, "expected ':'") || parseHotness(Hotness))
          return true;
        break;
      case lltok::kw_relbf:
        Lex.Lex();
        if (parseToken(lltok::colon, "expected ':'") || parseUInt32(RelBF))
          return true;
        break;
      case lltok::kw_tail:
        Lex.Lex();
        if (parseToken(lltok::colon, "expected ':'") || parseFlag(HasTailCall))
          return true;
        break;
      default:
        return error(Lex.getLoc(), "expected hotness, relbf, or tail");
      }
    }
    if (Hotness != CalleeInfo::HotnessType::Unknown && RelBF > 0)
      return tokError("Expected only one of hotness or relbf");
    // Keep track of the Call array index needing a forward reference.
    // We will save the location of the ValueInfo needing an update, but
    // can only do so once the std::vector is finalized.
    if (VI.getRef() == FwdVIRef)
      IdToIndexMap[GVId].push_back(std::make_pair(Calls.size(), Loc));
    Calls.push_back(
        FunctionSummary::EdgeTy{VI, CalleeInfo(Hotness, HasTailCall, RelBF)});

    if (parseToken(lltok::rparen, "expected ')' in call"))
      return true;
  } while (EatIfPresent(lltok::comma));

  // Now that the Calls vector is finalized, it is safe to save the locations
  // of any forward GV references that need updating later.
  for (auto I : IdToIndexMap) {
    auto &Infos = ForwardRefValueInfos[I.first];
    for (auto P : I.second) {
      assert(Calls[P.first].first.getRef() == FwdVIRef &&
             "Forward referenced ValueInfo expected to be empty");
      Infos.emplace_back(&Calls[P.first].first, P.second);
    }
  }

  if (parseToken(lltok::rparen, "expected ')' in calls"))
    return true;

  return false;
}

/// Hotness
///   := ('unknown'|'cold'|'none'|'hot'|'critical')
bool LLParser::parseHotness(CalleeInfo::HotnessType &Hotness) {
  switch (Lex.getKind()) {
  case lltok::kw_unknown:
    Hotness = CalleeInfo::HotnessType::Unknown;
    break;
  case lltok::kw_cold:
    Hotness = CalleeInfo::HotnessType::Cold;
    break;
  case lltok::kw_none:
    Hotness = CalleeInfo::HotnessType::None;
    break;
  case lltok::kw_hot:
    Hotness = CalleeInfo::HotnessType::Hot;
    break;
  case lltok::kw_critical:
    Hotness = CalleeInfo::HotnessType::Critical;
    break;
  default:
    return error(Lex.getLoc(), "invalid call edge hotness");
  }
  Lex.Lex();
  return false;
}

/// OptionalVTableFuncs
///   := 'vTableFuncs' ':' '(' VTableFunc [',' VTableFunc]* ')'
/// VTableFunc ::= '(' 'virtFunc' ':' GVReference ',' 'offset' ':' UInt64 ')'
bool LLParser::parseOptionalVTableFuncs(VTableFuncList &VTableFuncs) {
  assert(Lex.getKind() == lltok::kw_vTableFuncs);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' in vTableFuncs") ||
      parseToken(lltok::lparen, "expected '(' in vTableFuncs"))
    return true;

  IdToIndexMapType IdToIndexMap;
  // parse each virtual function pair
  do {
    ValueInfo VI;
    if (parseToken(lltok::lparen, "expected '(' in vTableFunc") ||
        parseToken(lltok::kw_virtFunc, "expected 'callee' in vTableFunc") ||
        parseToken(lltok::colon, "expected ':'"))
      return true;

    LocTy Loc = Lex.getLoc();
    unsigned GVId;
    if (parseGVReference(VI, GVId))
      return true;

    uint64_t Offset;
    if (parseToken(lltok::comma, "expected comma") ||
        parseToken(lltok::kw_offset, "expected offset") ||
        parseToken(lltok::colon, "expected ':'") || parseUInt64(Offset))
      return true;

    // Keep track of the VTableFuncs array index needing a forward reference.
    // We will save the location of the ValueInfo needing an update, but
    // can only do so once the std::vector is finalized.
    if (VI == EmptyVI)
      IdToIndexMap[GVId].push_back(std::make_pair(VTableFuncs.size(), Loc));
    VTableFuncs.push_back({VI, Offset});

    if (parseToken(lltok::rparen, "expected ')' in vTableFunc"))
      return true;
  } while (EatIfPresent(lltok::comma));

  // Now that the VTableFuncs vector is finalized, it is safe to save the
  // locations of any forward GV references that need updating later.
  for (auto I : IdToIndexMap) {
    auto &Infos = ForwardRefValueInfos[I.first];
    for (auto P : I.second) {
      assert(VTableFuncs[P.first].FuncVI == EmptyVI &&
             "Forward referenced ValueInfo expected to be empty");
      Infos.emplace_back(&VTableFuncs[P.first].FuncVI, P.second);
    }
  }

  if (parseToken(lltok::rparen, "expected ')' in vTableFuncs"))
    return true;

  return false;
}

/// ParamNo := 'param' ':' UInt64
bool LLParser::parseParamNo(uint64_t &ParamNo) {
  if (parseToken(lltok::kw_param, "expected 'param' here") ||
      parseToken(lltok::colon, "expected ':' here") || parseUInt64(ParamNo))
    return true;
  return false;
}

/// ParamAccessOffset := 'offset' ':' '[' APSINTVAL ',' APSINTVAL ']'
bool LLParser::parseParamAccessOffset(ConstantRange &Range) {
  APSInt Lower;
  APSInt Upper;
  auto ParseAPSInt = [&](APSInt &Val) {
    if (Lex.getKind() != lltok::APSInt)
      return tokError("expected integer");
    Val = Lex.getAPSIntVal();
    Val = Val.extOrTrunc(FunctionSummary::ParamAccess::RangeWidth);
    Val.setIsSigned(true);
    Lex.Lex();
    return false;
  };
  if (parseToken(lltok::kw_offset, "expected 'offset' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lsquare, "expected '[' here") || ParseAPSInt(Lower) ||
      parseToken(lltok::comma, "expected ',' here") || ParseAPSInt(Upper) ||
      parseToken(lltok::rsquare, "expected ']' here"))
    return true;

  ++Upper;
  Range =
      (Lower == Upper && !Lower.isMaxValue())
          ? ConstantRange::getEmpty(FunctionSummary::ParamAccess::RangeWidth)
          : ConstantRange(Lower, Upper);

  return false;
}

/// ParamAccessCall
///   := '(' 'callee' ':' GVReference ',' ParamNo ',' ParamAccessOffset ')'
bool LLParser::parseParamAccessCall(FunctionSummary::ParamAccess::Call &Call,
                                    IdLocListType &IdLocList) {
  if (parseToken(lltok::lparen, "expected '(' here") ||
      parseToken(lltok::kw_callee, "expected 'callee' here") ||
      parseToken(lltok::colon, "expected ':' here"))
    return true;

  unsigned GVId;
  ValueInfo VI;
  LocTy Loc = Lex.getLoc();
  if (parseGVReference(VI, GVId))
    return true;

  Call.Callee = VI;
  IdLocList.emplace_back(GVId, Loc);

  if (parseToken(lltok::comma, "expected ',' here") ||
      parseParamNo(Call.ParamNo) ||
      parseToken(lltok::comma, "expected ',' here") ||
      parseParamAccessOffset(Call.Offsets))
    return true;

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  return false;
}

/// ParamAccess
///   := '(' ParamNo ',' ParamAccessOffset [',' OptionalParamAccessCalls]? ')'
/// OptionalParamAccessCalls := '(' Call [',' Call]* ')'
bool LLParser::parseParamAccess(FunctionSummary::ParamAccess &Param,
                                IdLocListType &IdLocList) {
  if (parseToken(lltok::lparen, "expected '(' here") ||
      parseParamNo(Param.ParamNo) ||
      parseToken(lltok::comma, "expected ',' here") ||
      parseParamAccessOffset(Param.Use))
    return true;

  if (EatIfPresent(lltok::comma)) {
    if (parseToken(lltok::kw_calls, "expected 'calls' here") ||
        parseToken(lltok::colon, "expected ':' here") ||
        parseToken(lltok::lparen, "expected '(' here"))
      return true;
    do {
      FunctionSummary::ParamAccess::Call Call;
      if (parseParamAccessCall(Call, IdLocList))
        return true;
      Param.Calls.push_back(Call);
    } while (EatIfPresent(lltok::comma));

    if (parseToken(lltok::rparen, "expected ')' here"))
      return true;
  }

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  return false;
}

/// OptionalParamAccesses
///   := 'params' ':' '(' ParamAccess [',' ParamAccess]* ')'
bool LLParser::parseOptionalParamAccesses(
    std::vector<FunctionSummary::ParamAccess> &Params) {
  assert(Lex.getKind() == lltok::kw_params);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;

  IdLocListType VContexts;
  size_t CallsNum = 0;
  do {
    FunctionSummary::ParamAccess ParamAccess;
    if (parseParamAccess(ParamAccess, VContexts))
      return true;
    CallsNum += ParamAccess.Calls.size();
    assert(VContexts.size() == CallsNum);
    (void)CallsNum;
    Params.emplace_back(std::move(ParamAccess));
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  // Now that the Params is finalized, it is safe to save the locations
  // of any forward GV references that need updating later.
  IdLocListType::const_iterator ItContext = VContexts.begin();
  for (auto &PA : Params) {
    for (auto &C : PA.Calls) {
      if (C.Callee.getRef() == FwdVIRef)
        ForwardRefValueInfos[ItContext->first].emplace_back(&C.Callee,
                                                            ItContext->second);
      ++ItContext;
    }
  }
  assert(ItContext == VContexts.end());

  return false;
}

/// OptionalRefs
///   := 'refs' ':' '(' GVReference [',' GVReference]* ')'
bool LLParser::parseOptionalRefs(std::vector<ValueInfo> &Refs) {
  assert(Lex.getKind() == lltok::kw_refs);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' in refs") ||
      parseToken(lltok::lparen, "expected '(' in refs"))
    return true;

  struct ValueContext {
    ValueInfo VI;
    unsigned GVId;
    LocTy Loc;
  };
  std::vector<ValueContext> VContexts;
  // parse each ref edge
  do {
    ValueContext VC;
    VC.Loc = Lex.getLoc();
    if (parseGVReference(VC.VI, VC.GVId))
      return true;
    VContexts.push_back(VC);
  } while (EatIfPresent(lltok::comma));

  // Sort value contexts so that ones with writeonly
  // and readonly ValueInfo  are at the end of VContexts vector.
  // See FunctionSummary::specialRefCounts()
  llvm::sort(VContexts, [](const ValueContext &VC1, const ValueContext &VC2) {
    return VC1.VI.getAccessSpecifier() < VC2.VI.getAccessSpecifier();
  });

  IdToIndexMapType IdToIndexMap;
  for (auto &VC : VContexts) {
    // Keep track of the Refs array index needing a forward reference.
    // We will save the location of the ValueInfo needing an update, but
    // can only do so once the std::vector is finalized.
    if (VC.VI.getRef() == FwdVIRef)
      IdToIndexMap[VC.GVId].push_back(std::make_pair(Refs.size(), VC.Loc));
    Refs.push_back(VC.VI);
  }

  // Now that the Refs vector is finalized, it is safe to save the locations
  // of any forward GV references that need updating later.
  for (auto I : IdToIndexMap) {
    auto &Infos = ForwardRefValueInfos[I.first];
    for (auto P : I.second) {
      assert(Refs[P.first].getRef() == FwdVIRef &&
             "Forward referenced ValueInfo expected to be empty");
      Infos.emplace_back(&Refs[P.first], P.second);
    }
  }

  if (parseToken(lltok::rparen, "expected ')' in refs"))
    return true;

  return false;
}

/// OptionalTypeIdInfo
///   := 'typeidinfo' ':' '(' [',' TypeTests]? [',' TypeTestAssumeVCalls]?
///         [',' TypeCheckedLoadVCalls]?  [',' TypeTestAssumeConstVCalls]?
///         [',' TypeCheckedLoadConstVCalls]? ')'
bool LLParser::parseOptionalTypeIdInfo(
    FunctionSummary::TypeIdInfo &TypeIdInfo) {
  assert(Lex.getKind() == lltok::kw_typeIdInfo);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' in typeIdInfo"))
    return true;

  do {
    switch (Lex.getKind()) {
    case lltok::kw_typeTests:
      if (parseTypeTests(TypeIdInfo.TypeTests))
        return true;
      break;
    case lltok::kw_typeTestAssumeVCalls:
      if (parseVFuncIdList(lltok::kw_typeTestAssumeVCalls,
                           TypeIdInfo.TypeTestAssumeVCalls))
        return true;
      break;
    case lltok::kw_typeCheckedLoadVCalls:
      if (parseVFuncIdList(lltok::kw_typeCheckedLoadVCalls,
                           TypeIdInfo.TypeCheckedLoadVCalls))
        return true;
      break;
    case lltok::kw_typeTestAssumeConstVCalls:
      if (parseConstVCallList(lltok::kw_typeTestAssumeConstVCalls,
                              TypeIdInfo.TypeTestAssumeConstVCalls))
        return true;
      break;
    case lltok::kw_typeCheckedLoadConstVCalls:
      if (parseConstVCallList(lltok::kw_typeCheckedLoadConstVCalls,
                              TypeIdInfo.TypeCheckedLoadConstVCalls))
        return true;
      break;
    default:
      return error(Lex.getLoc(), "invalid typeIdInfo list type");
    }
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' in typeIdInfo"))
    return true;

  return false;
}

/// TypeTests
///   ::= 'typeTests' ':' '(' (SummaryID | UInt64)
///         [',' (SummaryID | UInt64)]* ')'
bool LLParser::parseTypeTests(std::vector<GlobalValue::GUID> &TypeTests) {
  assert(Lex.getKind() == lltok::kw_typeTests);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' in typeIdInfo"))
    return true;

  IdToIndexMapType IdToIndexMap;
  do {
    GlobalValue::GUID GUID = 0;
    if (Lex.getKind() == lltok::SummaryID) {
      unsigned ID = Lex.getUIntVal();
      LocTy Loc = Lex.getLoc();
      // Keep track of the TypeTests array index needing a forward reference.
      // We will save the location of the GUID needing an update, but
      // can only do so once the std::vector is finalized.
      IdToIndexMap[ID].push_back(std::make_pair(TypeTests.size(), Loc));
      Lex.Lex();
    } else if (parseUInt64(GUID))
      return true;
    TypeTests.push_back(GUID);
  } while (EatIfPresent(lltok::comma));

  // Now that the TypeTests vector is finalized, it is safe to save the
  // locations of any forward GV references that need updating later.
  for (auto I : IdToIndexMap) {
    auto &Ids = ForwardRefTypeIds[I.first];
    for (auto P : I.second) {
      assert(TypeTests[P.first] == 0 &&
             "Forward referenced type id GUID expected to be 0");
      Ids.emplace_back(&TypeTests[P.first], P.second);
    }
  }

  if (parseToken(lltok::rparen, "expected ')' in typeIdInfo"))
    return true;

  return false;
}

/// VFuncIdList
///   ::= Kind ':' '(' VFuncId [',' VFuncId]* ')'
bool LLParser::parseVFuncIdList(
    lltok::Kind Kind, std::vector<FunctionSummary::VFuncId> &VFuncIdList) {
  assert(Lex.getKind() == Kind);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;

  IdToIndexMapType IdToIndexMap;
  do {
    FunctionSummary::VFuncId VFuncId;
    if (parseVFuncId(VFuncId, IdToIndexMap, VFuncIdList.size()))
      return true;
    VFuncIdList.push_back(VFuncId);
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  // Now that the VFuncIdList vector is finalized, it is safe to save the
  // locations of any forward GV references that need updating later.
  for (auto I : IdToIndexMap) {
    auto &Ids = ForwardRefTypeIds[I.first];
    for (auto P : I.second) {
      assert(VFuncIdList[P.first].GUID == 0 &&
             "Forward referenced type id GUID expected to be 0");
      Ids.emplace_back(&VFuncIdList[P.first].GUID, P.second);
    }
  }

  return false;
}

/// ConstVCallList
///   ::= Kind ':' '(' ConstVCall [',' ConstVCall]* ')'
bool LLParser::parseConstVCallList(
    lltok::Kind Kind,
    std::vector<FunctionSummary::ConstVCall> &ConstVCallList) {
  assert(Lex.getKind() == Kind);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;

  IdToIndexMapType IdToIndexMap;
  do {
    FunctionSummary::ConstVCall ConstVCall;
    if (parseConstVCall(ConstVCall, IdToIndexMap, ConstVCallList.size()))
      return true;
    ConstVCallList.push_back(ConstVCall);
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  // Now that the ConstVCallList vector is finalized, it is safe to save the
  // locations of any forward GV references that need updating later.
  for (auto I : IdToIndexMap) {
    auto &Ids = ForwardRefTypeIds[I.first];
    for (auto P : I.second) {
      assert(ConstVCallList[P.first].VFunc.GUID == 0 &&
             "Forward referenced type id GUID expected to be 0");
      Ids.emplace_back(&ConstVCallList[P.first].VFunc.GUID, P.second);
    }
  }

  return false;
}

/// ConstVCall
///   ::= '(' VFuncId ',' Args ')'
bool LLParser::parseConstVCall(FunctionSummary::ConstVCall &ConstVCall,
                               IdToIndexMapType &IdToIndexMap, unsigned Index) {
  if (parseToken(lltok::lparen, "expected '(' here") ||
      parseVFuncId(ConstVCall.VFunc, IdToIndexMap, Index))
    return true;

  if (EatIfPresent(lltok::comma))
    if (parseArgs(ConstVCall.Args))
      return true;

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  return false;
}

/// VFuncId
///   ::= 'vFuncId' ':' '(' (SummaryID | 'guid' ':' UInt64) ','
///         'offset' ':' UInt64 ')'
bool LLParser::parseVFuncId(FunctionSummary::VFuncId &VFuncId,
                            IdToIndexMapType &IdToIndexMap, unsigned Index) {
  assert(Lex.getKind() == lltok::kw_vFuncId);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;

  if (Lex.getKind() == lltok::SummaryID) {
    VFuncId.GUID = 0;
    unsigned ID = Lex.getUIntVal();
    LocTy Loc = Lex.getLoc();
    // Keep track of the array index needing a forward reference.
    // We will save the location of the GUID needing an update, but
    // can only do so once the caller's std::vector is finalized.
    IdToIndexMap[ID].push_back(std::make_pair(Index, Loc));
    Lex.Lex();
  } else if (parseToken(lltok::kw_guid, "expected 'guid' here") ||
             parseToken(lltok::colon, "expected ':' here") ||
             parseUInt64(VFuncId.GUID))
    return true;

  if (parseToken(lltok::comma, "expected ',' here") ||
      parseToken(lltok::kw_offset, "expected 'offset' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseUInt64(VFuncId.Offset) ||
      parseToken(lltok::rparen, "expected ')' here"))
    return true;

  return false;
}

/// GVFlags
///   ::= 'flags' ':' '(' 'linkage' ':' OptionalLinkageAux ','
///         'visibility' ':' Flag 'notEligibleToImport' ':' Flag ','
///         'live' ':' Flag ',' 'dsoLocal' ':' Flag ','
///         'canAutoHide' ':' Flag ',' ')'
bool LLParser::parseGVFlags(GlobalValueSummary::GVFlags &GVFlags) {
  assert(Lex.getKind() == lltok::kw_flags);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;

  do {
    unsigned Flag = 0;
    switch (Lex.getKind()) {
    case lltok::kw_linkage:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'"))
        return true;
      bool HasLinkage;
      GVFlags.Linkage = parseOptionalLinkageAux(Lex.getKind(), HasLinkage);
      assert(HasLinkage && "Linkage not optional in summary entry");
      Lex.Lex();
      break;
    case lltok::kw_visibility:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'"))
        return true;
      parseOptionalVisibility(Flag);
      GVFlags.Visibility = Flag;
      break;
    case lltok::kw_notEligibleToImport:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Flag))
        return true;
      GVFlags.NotEligibleToImport = Flag;
      break;
    case lltok::kw_live:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Flag))
        return true;
      GVFlags.Live = Flag;
      break;
    case lltok::kw_dsoLocal:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Flag))
        return true;
      GVFlags.DSOLocal = Flag;
      break;
    case lltok::kw_canAutoHide:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'") || parseFlag(Flag))
        return true;
      GVFlags.CanAutoHide = Flag;
      break;
    case lltok::kw_importType:
      Lex.Lex();
      if (parseToken(lltok::colon, "expected ':'"))
        return true;
      GlobalValueSummary::ImportKind IK;
      if (parseOptionalImportType(Lex.getKind(), IK))
        return true;
      GVFlags.ImportType = static_cast<unsigned>(IK);
      Lex.Lex();
      break;
    default:
      return error(Lex.getLoc(), "expected gv flag type");
    }
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' here"))
    return true;

  return false;
}

/// GVarFlags
///   ::= 'varFlags' ':' '(' 'readonly' ':' Flag
///                      ',' 'writeonly' ':' Flag
///                      ',' 'constant' ':' Flag ')'
bool LLParser::parseGVarFlags(GlobalVarSummary::GVarFlags &GVarFlags) {
  assert(Lex.getKind() == lltok::kw_varFlags);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::lparen, "expected '(' here"))
    return true;

  auto ParseRest = [this](unsigned int &Val) {
    Lex.Lex();
    if (parseToken(lltok::colon, "expected ':'"))
      return true;
    return parseFlag(Val);
  };

  do {
    unsigned Flag = 0;
    switch (Lex.getKind()) {
    case lltok::kw_readonly:
      if (ParseRest(Flag))
        return true;
      GVarFlags.MaybeReadOnly = Flag;
      break;
    case lltok::kw_writeonly:
      if (ParseRest(Flag))
        return true;
      GVarFlags.MaybeWriteOnly = Flag;
      break;
    case lltok::kw_constant:
      if (ParseRest(Flag))
        return true;
      GVarFlags.Constant = Flag;
      break;
    case lltok::kw_vcall_visibility:
      if (ParseRest(Flag))
        return true;
      GVarFlags.VCallVisibility = Flag;
      break;
    default:
      return error(Lex.getLoc(), "expected gvar flag type");
    }
  } while (EatIfPresent(lltok::comma));
  return parseToken(lltok::rparen, "expected ')' here");
}

/// ModuleReference
///   ::= 'module' ':' UInt
bool LLParser::parseModuleReference(StringRef &ModulePath) {
  // parse module id.
  if (parseToken(lltok::kw_module, "expected 'module' here") ||
      parseToken(lltok::colon, "expected ':' here") ||
      parseToken(lltok::SummaryID, "expected module ID"))
    return true;

  unsigned ModuleID = Lex.getUIntVal();
  auto I = ModuleIdMap.find(ModuleID);
  // We should have already parsed all module IDs
  assert(I != ModuleIdMap.end());
  ModulePath = I->second;
  return false;
}

/// GVReference
///   ::= SummaryID
bool LLParser::parseGVReference(ValueInfo &VI, unsigned &GVId) {
  bool WriteOnly = false, ReadOnly = EatIfPresent(lltok::kw_readonly);
  if (!ReadOnly)
    WriteOnly = EatIfPresent(lltok::kw_writeonly);
  if (parseToken(lltok::SummaryID, "expected GV ID"))
    return true;

  GVId = Lex.getUIntVal();
  // Check if we already have a VI for this GV
  if (GVId < NumberedValueInfos.size() && NumberedValueInfos[GVId]) {
    assert(NumberedValueInfos[GVId].getRef() != FwdVIRef);
    VI = NumberedValueInfos[GVId];
  } else
    // We will create a forward reference to the stored location.
    VI = ValueInfo(false, FwdVIRef);

  if (ReadOnly)
    VI.setReadOnly();
  if (WriteOnly)
    VI.setWriteOnly();
  return false;
}

/// OptionalAllocs
///   := 'allocs' ':' '(' Alloc [',' Alloc]* ')'
/// Alloc ::= '(' 'versions' ':' '(' Version [',' Version]* ')'
///              ',' MemProfs ')'
/// Version ::= UInt32
bool LLParser::parseOptionalAllocs(std::vector<AllocInfo> &Allocs) {
  assert(Lex.getKind() == lltok::kw_allocs);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' in allocs") ||
      parseToken(lltok::lparen, "expected '(' in allocs"))
    return true;

  // parse each alloc
  do {
    if (parseToken(lltok::lparen, "expected '(' in alloc") ||
        parseToken(lltok::kw_versions, "expected 'versions' in alloc") ||
        parseToken(lltok::colon, "expected ':'") ||
        parseToken(lltok::lparen, "expected '(' in versions"))
      return true;

    SmallVector<uint8_t> Versions;
    do {
      uint8_t V = 0;
      if (parseAllocType(V))
        return true;
      Versions.push_back(V);
    } while (EatIfPresent(lltok::comma));

    if (parseToken(lltok::rparen, "expected ')' in versions") ||
        parseToken(lltok::comma, "expected ',' in alloc"))
      return true;

    std::vector<MIBInfo> MIBs;
    if (parseMemProfs(MIBs))
      return true;

    Allocs.push_back({Versions, MIBs});

    if (parseToken(lltok::rparen, "expected ')' in alloc"))
      return true;
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' in allocs"))
    return true;

  return false;
}

/// MemProfs
///   := 'memProf' ':' '(' MemProf [',' MemProf]* ')'
/// MemProf ::= '(' 'type' ':' AllocType
///              ',' 'stackIds' ':' '(' StackId [',' StackId]* ')' ')'
/// StackId ::= UInt64
bool LLParser::parseMemProfs(std::vector<MIBInfo> &MIBs) {
  assert(Lex.getKind() == lltok::kw_memProf);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' in memprof") ||
      parseToken(lltok::lparen, "expected '(' in memprof"))
    return true;

  // parse each MIB
  do {
    if (parseToken(lltok::lparen, "expected '(' in memprof") ||
        parseToken(lltok::kw_type, "expected 'type' in memprof") ||
        parseToken(lltok::colon, "expected ':'"))
      return true;

    uint8_t AllocType;
    if (parseAllocType(AllocType))
      return true;

    if (parseToken(lltok::comma, "expected ',' in memprof") ||
        parseToken(lltok::kw_stackIds, "expected 'stackIds' in memprof") ||
        parseToken(lltok::colon, "expected ':'") ||
        parseToken(lltok::lparen, "expected '(' in stackIds"))
      return true;

    SmallVector<unsigned> StackIdIndices;
    do {
      uint64_t StackId = 0;
      if (parseUInt64(StackId))
        return true;
      StackIdIndices.push_back(Index->addOrGetStackIdIndex(StackId));
    } while (EatIfPresent(lltok::comma));

    if (parseToken(lltok::rparen, "expected ')' in stackIds"))
      return true;

    MIBs.push_back({(AllocationType)AllocType, StackIdIndices});

    if (parseToken(lltok::rparen, "expected ')' in memprof"))
      return true;
  } while (EatIfPresent(lltok::comma));

  if (parseToken(lltok::rparen, "expected ')' in memprof"))
    return true;

  return false;
}

/// AllocType
///   := ('none'|'notcold'|'cold'|'hot')
bool LLParser::parseAllocType(uint8_t &AllocType) {
  switch (Lex.getKind()) {
  case lltok::kw_none:
    AllocType = (uint8_t)AllocationType::None;
    break;
  case lltok::kw_notcold:
    AllocType = (uint8_t)AllocationType::NotCold;
    break;
  case lltok::kw_cold:
    AllocType = (uint8_t)AllocationType::Cold;
    break;
  case lltok::kw_hot:
    AllocType = (uint8_t)AllocationType::Hot;
    break;
  default:
    return error(Lex.getLoc(), "invalid alloc type");
  }
  Lex.Lex();
  return false;
}

/// OptionalCallsites
///   := 'callsites' ':' '(' Callsite [',' Callsite]* ')'
/// Callsite ::= '(' 'callee' ':' GVReference
///              ',' 'clones' ':' '(' Version [',' Version]* ')'
///              ',' 'stackIds' ':' '(' StackId [',' StackId]* ')' ')'
/// Version ::= UInt32
/// StackId ::= UInt64
bool LLParser::parseOptionalCallsites(std::vector<CallsiteInfo> &Callsites) {
  assert(Lex.getKind() == lltok::kw_callsites);
  Lex.Lex();

  if (parseToken(lltok::colon, "expected ':' in callsites") ||
      parseToken(lltok::lparen, "expected '(' in callsites"))
    return true;

  IdToIndexMapType IdToIndexMap;
  // parse each callsite
  do {
    if (parseToken(lltok::lparen, "expected '(' in callsite") ||
        parseToken(lltok::kw_callee, "expected 'callee' in callsite") ||
        parseToken(lltok::colon, "expected ':'"))
      return true;

    ValueInfo VI;
    unsigned GVId = 0;
    LocTy Loc = Lex.getLoc();
    if (!EatIfPresent(lltok::kw_null)) {
      if (parseGVReference(VI, GVId))
        return true;
    }

    if (parseToken(lltok::comma, "expected ',' in callsite") ||
        parseToken(lltok::kw_clones, "expected 'clones' in callsite") ||
        parseToken(lltok::colon, "expected ':'") ||
        parseToken(lltok::lparen, "expected '(' in clones"))
      return true;

    SmallVector<unsigned> Clones;
    do {
      unsigned V = 0;
      if (parseUInt32(V))
        return true;
      Clones.push_back(V);
    } while (EatIfPresent(lltok::comma));

    if (parseToken(lltok::rparen, "expected ')' in clones") ||
        parseToken(lltok::comma, "expected ',' in callsite") ||
        parseToken(lltok::kw_stackIds, "expected 'stackIds' in callsite") ||
        parseToken(lltok::colon, "expected ':'") ||
        parseToken(lltok::lparen, "expected '(' in stackIds"))
      return true;

    SmallVector<unsigned> StackIdIndices;
    do {
      uint64_t StackId = 0;
      if (parseUInt64(StackId))
        return true;
      StackIdIndices.push_back(Index->addOrGetStackIdIndex(StackId));
    } while (EatIfPresent(lltok::comma));

    if (parseToken(lltok::rparen, "expected ')' in stackIds"))
      return true;

    // Keep track of the Callsites array index needing a forward reference.
    // We will save the location of the ValueInfo needing an update, but
    // can only do so once the SmallVector is finalized.
    if (VI.getRef() == FwdVIRef)
      IdToIndexMap[GVId].push_back(std::make_pair(Callsites.size(), Loc));
    Callsites.push_back({VI, Clones, StackIdIndices});

    if (parseToken(lltok::rparen, "expected ')' in callsite"))
      return true;
  } while (EatIfPresent(lltok::comma));

  // Now that the Callsites vector is finalized, it is safe to save the
  // locations of any forward GV references that need updating later.
  for (auto I : IdToIndexMap) {
    auto &Infos = ForwardRefValueInfos[I.first];
    for (auto P : I.second) {
      assert(Callsites[P.first].Callee.getRef() == FwdVIRef &&
             "Forward referenced ValueInfo expected to be empty");
      Infos.emplace_back(&Callsites[P.first].Callee, P.second);
    }
  }

  if (parseToken(lltok::rparen, "expected ')' in callsites"))
    return true;

  return false;
}

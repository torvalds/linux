//===- ThinLTOBitcodeWriter.cpp - Bitcode writing pass for ThinLTO --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/ThinLTOBitcodeWriter.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TypeMetadataUtils.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Object/ModuleSymbolTable.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/Transforms/IPO/LowerTypeTests.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
using namespace llvm;

namespace {

// Determine if a promotion alias should be created for a symbol name.
static bool allowPromotionAlias(const std::string &Name) {
  // Promotion aliases are used only in inline assembly. It's safe to
  // simply skip unusual names. Subset of MCAsmInfo::isAcceptableChar()
  // and MCAsmInfoXCOFF::isAcceptableChar().
  for (const char &C : Name) {
    if (isAlnum(C) || C == '_' || C == '.')
      continue;
    return false;
  }
  return true;
}

// Promote each local-linkage entity defined by ExportM and used by ImportM by
// changing visibility and appending the given ModuleId.
void promoteInternals(Module &ExportM, Module &ImportM, StringRef ModuleId,
                      SetVector<GlobalValue *> &PromoteExtra) {
  DenseMap<const Comdat *, Comdat *> RenamedComdats;
  for (auto &ExportGV : ExportM.global_values()) {
    if (!ExportGV.hasLocalLinkage())
      continue;

    auto Name = ExportGV.getName();
    GlobalValue *ImportGV = nullptr;
    if (!PromoteExtra.count(&ExportGV)) {
      ImportGV = ImportM.getNamedValue(Name);
      if (!ImportGV)
        continue;
      ImportGV->removeDeadConstantUsers();
      if (ImportGV->use_empty()) {
        ImportGV->eraseFromParent();
        continue;
      }
    }

    std::string OldName = Name.str();
    std::string NewName = (Name + ModuleId).str();

    if (const auto *C = ExportGV.getComdat())
      if (C->getName() == Name)
        RenamedComdats.try_emplace(C, ExportM.getOrInsertComdat(NewName));

    ExportGV.setName(NewName);
    ExportGV.setLinkage(GlobalValue::ExternalLinkage);
    ExportGV.setVisibility(GlobalValue::HiddenVisibility);

    if (ImportGV) {
      ImportGV->setName(NewName);
      ImportGV->setVisibility(GlobalValue::HiddenVisibility);
    }

    if (isa<Function>(&ExportGV) && allowPromotionAlias(OldName)) {
      // Create a local alias with the original name to avoid breaking
      // references from inline assembly.
      std::string Alias =
          ".lto_set_conditional " + OldName + "," + NewName + "\n";
      ExportM.appendModuleInlineAsm(Alias);
    }
  }

  if (!RenamedComdats.empty())
    for (auto &GO : ExportM.global_objects())
      if (auto *C = GO.getComdat()) {
        auto Replacement = RenamedComdats.find(C);
        if (Replacement != RenamedComdats.end())
          GO.setComdat(Replacement->second);
      }
}

// Promote all internal (i.e. distinct) type ids used by the module by replacing
// them with external type ids formed using the module id.
//
// Note that this needs to be done before we clone the module because each clone
// will receive its own set of distinct metadata nodes.
void promoteTypeIds(Module &M, StringRef ModuleId) {
  DenseMap<Metadata *, Metadata *> LocalToGlobal;
  auto ExternalizeTypeId = [&](CallInst *CI, unsigned ArgNo) {
    Metadata *MD =
        cast<MetadataAsValue>(CI->getArgOperand(ArgNo))->getMetadata();

    if (isa<MDNode>(MD) && cast<MDNode>(MD)->isDistinct()) {
      Metadata *&GlobalMD = LocalToGlobal[MD];
      if (!GlobalMD) {
        std::string NewName = (Twine(LocalToGlobal.size()) + ModuleId).str();
        GlobalMD = MDString::get(M.getContext(), NewName);
      }

      CI->setArgOperand(ArgNo,
                        MetadataAsValue::get(M.getContext(), GlobalMD));
    }
  };

  if (Function *TypeTestFunc =
          M.getFunction(Intrinsic::getName(Intrinsic::type_test))) {
    for (const Use &U : TypeTestFunc->uses()) {
      auto CI = cast<CallInst>(U.getUser());
      ExternalizeTypeId(CI, 1);
    }
  }

  if (Function *PublicTypeTestFunc =
          M.getFunction(Intrinsic::getName(Intrinsic::public_type_test))) {
    for (const Use &U : PublicTypeTestFunc->uses()) {
      auto CI = cast<CallInst>(U.getUser());
      ExternalizeTypeId(CI, 1);
    }
  }

  if (Function *TypeCheckedLoadFunc =
          M.getFunction(Intrinsic::getName(Intrinsic::type_checked_load))) {
    for (const Use &U : TypeCheckedLoadFunc->uses()) {
      auto CI = cast<CallInst>(U.getUser());
      ExternalizeTypeId(CI, 2);
    }
  }

  if (Function *TypeCheckedLoadRelativeFunc = M.getFunction(
          Intrinsic::getName(Intrinsic::type_checked_load_relative))) {
    for (const Use &U : TypeCheckedLoadRelativeFunc->uses()) {
      auto CI = cast<CallInst>(U.getUser());
      ExternalizeTypeId(CI, 2);
    }
  }

  for (GlobalObject &GO : M.global_objects()) {
    SmallVector<MDNode *, 1> MDs;
    GO.getMetadata(LLVMContext::MD_type, MDs);

    GO.eraseMetadata(LLVMContext::MD_type);
    for (auto *MD : MDs) {
      auto I = LocalToGlobal.find(MD->getOperand(1));
      if (I == LocalToGlobal.end()) {
        GO.addMetadata(LLVMContext::MD_type, *MD);
        continue;
      }
      GO.addMetadata(
          LLVMContext::MD_type,
          *MDNode::get(M.getContext(), {MD->getOperand(0), I->second}));
    }
  }
}

// Drop unused globals, and drop type information from function declarations.
// FIXME: If we made functions typeless then there would be no need to do this.
void simplifyExternals(Module &M) {
  FunctionType *EmptyFT =
      FunctionType::get(Type::getVoidTy(M.getContext()), false);

  for (Function &F : llvm::make_early_inc_range(M)) {
    if (F.isDeclaration() && F.use_empty()) {
      F.eraseFromParent();
      continue;
    }

    if (!F.isDeclaration() || F.getFunctionType() == EmptyFT ||
        // Changing the type of an intrinsic may invalidate the IR.
        F.getName().starts_with("llvm."))
      continue;

    Function *NewF =
        Function::Create(EmptyFT, GlobalValue::ExternalLinkage,
                         F.getAddressSpace(), "", &M);
    NewF->copyAttributesFrom(&F);
    // Only copy function attribtues.
    NewF->setAttributes(AttributeList::get(M.getContext(),
                                           AttributeList::FunctionIndex,
                                           F.getAttributes().getFnAttrs()));
    NewF->takeName(&F);
    F.replaceAllUsesWith(NewF);
    F.eraseFromParent();
  }

  for (GlobalIFunc &I : llvm::make_early_inc_range(M.ifuncs())) {
    if (I.use_empty())
      I.eraseFromParent();
    else
      assert(I.getResolverFunction() && "ifunc misses its resolver function");
  }

  for (GlobalVariable &GV : llvm::make_early_inc_range(M.globals())) {
    if (GV.isDeclaration() && GV.use_empty()) {
      GV.eraseFromParent();
      continue;
    }
  }
}

static void
filterModule(Module *M,
             function_ref<bool(const GlobalValue *)> ShouldKeepDefinition) {
  std::vector<GlobalValue *> V;
  for (GlobalValue &GV : M->global_values())
    if (!ShouldKeepDefinition(&GV))
      V.push_back(&GV);

  for (GlobalValue *GV : V)
    if (!convertToDeclaration(*GV))
      GV->eraseFromParent();
}

void forEachVirtualFunction(Constant *C, function_ref<void(Function *)> Fn) {
  if (auto *F = dyn_cast<Function>(C))
    return Fn(F);
  if (isa<GlobalValue>(C))
    return;
  for (Value *Op : C->operands())
    forEachVirtualFunction(cast<Constant>(Op), Fn);
}

// Clone any @llvm[.compiler].used over to the new module and append
// values whose defs were cloned into that module.
static void cloneUsedGlobalVariables(const Module &SrcM, Module &DestM,
                                     bool CompilerUsed) {
  SmallVector<GlobalValue *, 4> Used, NewUsed;
  // First collect those in the llvm[.compiler].used set.
  collectUsedGlobalVariables(SrcM, Used, CompilerUsed);
  // Next build a set of the equivalent values defined in DestM.
  for (auto *V : Used) {
    auto *GV = DestM.getNamedValue(V->getName());
    if (GV && !GV->isDeclaration())
      NewUsed.push_back(GV);
  }
  // Finally, add them to a llvm[.compiler].used variable in DestM.
  if (CompilerUsed)
    appendToCompilerUsed(DestM, NewUsed);
  else
    appendToUsed(DestM, NewUsed);
}

#ifndef NDEBUG
static bool enableUnifiedLTO(Module &M) {
  bool UnifiedLTO = false;
  if (auto *MD =
          mdconst::extract_or_null<ConstantInt>(M.getModuleFlag("UnifiedLTO")))
    UnifiedLTO = MD->getZExtValue();
  return UnifiedLTO;
}
#endif

// If it's possible to split M into regular and thin LTO parts, do so and write
// a multi-module bitcode file with the two parts to OS. Otherwise, write only a
// regular LTO bitcode file to OS.
void splitAndWriteThinLTOBitcode(
    raw_ostream &OS, raw_ostream *ThinLinkOS,
    function_ref<AAResults &(Function &)> AARGetter, Module &M) {
  std::string ModuleId = getUniqueModuleId(&M);
  if (ModuleId.empty()) {
    assert(!enableUnifiedLTO(M));
    // We couldn't generate a module ID for this module, write it out as a
    // regular LTO module with an index for summary-based dead stripping.
    ProfileSummaryInfo PSI(M);
    M.addModuleFlag(Module::Error, "ThinLTO", uint32_t(0));
    ModuleSummaryIndex Index = buildModuleSummaryIndex(M, nullptr, &PSI);
    WriteBitcodeToFile(M, OS, /*ShouldPreserveUseListOrder=*/false, &Index,
                       /*UnifiedLTO=*/false);

    if (ThinLinkOS)
      // We don't have a ThinLTO part, but still write the module to the
      // ThinLinkOS if requested so that the expected output file is produced.
      WriteBitcodeToFile(M, *ThinLinkOS, /*ShouldPreserveUseListOrder=*/false,
                         &Index, /*UnifiedLTO=*/false);

    return;
  }

  promoteTypeIds(M, ModuleId);

  // Returns whether a global or its associated global has attached type
  // metadata. The former may participate in CFI or whole-program
  // devirtualization, so they need to appear in the merged module instead of
  // the thin LTO module. Similarly, globals that are associated with globals
  // with type metadata need to appear in the merged module because they will
  // reference the global's section directly.
  auto HasTypeMetadata = [](const GlobalObject *GO) {
    if (MDNode *MD = GO->getMetadata(LLVMContext::MD_associated))
      if (auto *AssocVM = dyn_cast_or_null<ValueAsMetadata>(MD->getOperand(0)))
        if (auto *AssocGO = dyn_cast<GlobalObject>(AssocVM->getValue()))
          if (AssocGO->hasMetadata(LLVMContext::MD_type))
            return true;
    return GO->hasMetadata(LLVMContext::MD_type);
  };

  // Collect the set of virtual functions that are eligible for virtual constant
  // propagation. Each eligible function must not access memory, must return
  // an integer of width <=64 bits, must take at least one argument, must not
  // use its first argument (assumed to be "this") and all arguments other than
  // the first one must be of <=64 bit integer type.
  //
  // Note that we test whether this copy of the function is readnone, rather
  // than testing function attributes, which must hold for any copy of the
  // function, even a less optimized version substituted at link time. This is
  // sound because the virtual constant propagation optimizations effectively
  // inline all implementations of the virtual function into each call site,
  // rather than using function attributes to perform local optimization.
  DenseSet<const Function *> EligibleVirtualFns;
  // If any member of a comdat lives in MergedM, put all members of that
  // comdat in MergedM to keep the comdat together.
  DenseSet<const Comdat *> MergedMComdats;
  for (GlobalVariable &GV : M.globals())
    if (!GV.isDeclaration() && HasTypeMetadata(&GV)) {
      if (const auto *C = GV.getComdat())
        MergedMComdats.insert(C);
      forEachVirtualFunction(GV.getInitializer(), [&](Function *F) {
        auto *RT = dyn_cast<IntegerType>(F->getReturnType());
        if (!RT || RT->getBitWidth() > 64 || F->arg_empty() ||
            !F->arg_begin()->use_empty())
          return;
        for (auto &Arg : drop_begin(F->args())) {
          auto *ArgT = dyn_cast<IntegerType>(Arg.getType());
          if (!ArgT || ArgT->getBitWidth() > 64)
            return;
        }
        if (!F->isDeclaration() &&
            computeFunctionBodyMemoryAccess(*F, AARGetter(*F))
                .doesNotAccessMemory())
          EligibleVirtualFns.insert(F);
      });
    }

  ValueToValueMapTy VMap;
  std::unique_ptr<Module> MergedM(
      CloneModule(M, VMap, [&](const GlobalValue *GV) -> bool {
        if (const auto *C = GV->getComdat())
          if (MergedMComdats.count(C))
            return true;
        if (auto *F = dyn_cast<Function>(GV))
          return EligibleVirtualFns.count(F);
        if (auto *GVar =
                dyn_cast_or_null<GlobalVariable>(GV->getAliaseeObject()))
          return HasTypeMetadata(GVar);
        return false;
      }));
  StripDebugInfo(*MergedM);
  MergedM->setModuleInlineAsm("");

  // Clone any llvm.*used globals to ensure the included values are
  // not deleted.
  cloneUsedGlobalVariables(M, *MergedM, /*CompilerUsed*/ false);
  cloneUsedGlobalVariables(M, *MergedM, /*CompilerUsed*/ true);

  for (Function &F : *MergedM)
    if (!F.isDeclaration()) {
      // Reset the linkage of all functions eligible for virtual constant
      // propagation. The canonical definitions live in the thin LTO module so
      // that they can be imported.
      F.setLinkage(GlobalValue::AvailableExternallyLinkage);
      F.setComdat(nullptr);
    }

  SetVector<GlobalValue *> CfiFunctions;
  for (auto &F : M)
    if ((!F.hasLocalLinkage() || F.hasAddressTaken()) && HasTypeMetadata(&F))
      CfiFunctions.insert(&F);

  // Remove all globals with type metadata, globals with comdats that live in
  // MergedM, and aliases pointing to such globals from the thin LTO module.
  filterModule(&M, [&](const GlobalValue *GV) {
    if (auto *GVar = dyn_cast_or_null<GlobalVariable>(GV->getAliaseeObject()))
      if (HasTypeMetadata(GVar))
        return false;
    if (const auto *C = GV->getComdat())
      if (MergedMComdats.count(C))
        return false;
    return true;
  });

  promoteInternals(*MergedM, M, ModuleId, CfiFunctions);
  promoteInternals(M, *MergedM, ModuleId, CfiFunctions);

  auto &Ctx = MergedM->getContext();
  SmallVector<MDNode *, 8> CfiFunctionMDs;
  for (auto *V : CfiFunctions) {
    Function &F = *cast<Function>(V);
    SmallVector<MDNode *, 2> Types;
    F.getMetadata(LLVMContext::MD_type, Types);

    SmallVector<Metadata *, 4> Elts;
    Elts.push_back(MDString::get(Ctx, F.getName()));
    CfiFunctionLinkage Linkage;
    if (lowertypetests::isJumpTableCanonical(&F))
      Linkage = CFL_Definition;
    else if (F.hasExternalWeakLinkage())
      Linkage = CFL_WeakDeclaration;
    else
      Linkage = CFL_Declaration;
    Elts.push_back(ConstantAsMetadata::get(
        llvm::ConstantInt::get(Type::getInt8Ty(Ctx), Linkage)));
    append_range(Elts, Types);
    CfiFunctionMDs.push_back(MDTuple::get(Ctx, Elts));
  }

  if(!CfiFunctionMDs.empty()) {
    NamedMDNode *NMD = MergedM->getOrInsertNamedMetadata("cfi.functions");
    for (auto *MD : CfiFunctionMDs)
      NMD->addOperand(MD);
  }

  SmallVector<MDNode *, 8> FunctionAliases;
  for (auto &A : M.aliases()) {
    if (!isa<Function>(A.getAliasee()))
      continue;

    auto *F = cast<Function>(A.getAliasee());

    Metadata *Elts[] = {
        MDString::get(Ctx, A.getName()),
        MDString::get(Ctx, F->getName()),
        ConstantAsMetadata::get(
            ConstantInt::get(Type::getInt8Ty(Ctx), A.getVisibility())),
        ConstantAsMetadata::get(
            ConstantInt::get(Type::getInt8Ty(Ctx), A.isWeakForLinker())),
    };

    FunctionAliases.push_back(MDTuple::get(Ctx, Elts));
  }

  if (!FunctionAliases.empty()) {
    NamedMDNode *NMD = MergedM->getOrInsertNamedMetadata("aliases");
    for (auto *MD : FunctionAliases)
      NMD->addOperand(MD);
  }

  SmallVector<MDNode *, 8> Symvers;
  ModuleSymbolTable::CollectAsmSymvers(M, [&](StringRef Name, StringRef Alias) {
    Function *F = M.getFunction(Name);
    if (!F || F->use_empty())
      return;

    Symvers.push_back(MDTuple::get(
        Ctx, {MDString::get(Ctx, Name), MDString::get(Ctx, Alias)}));
  });

  if (!Symvers.empty()) {
    NamedMDNode *NMD = MergedM->getOrInsertNamedMetadata("symvers");
    for (auto *MD : Symvers)
      NMD->addOperand(MD);
  }

  simplifyExternals(*MergedM);

  // FIXME: Try to re-use BSI and PFI from the original module here.
  ProfileSummaryInfo PSI(M);
  ModuleSummaryIndex Index = buildModuleSummaryIndex(M, nullptr, &PSI);

  // Mark the merged module as requiring full LTO. We still want an index for
  // it though, so that it can participate in summary-based dead stripping.
  MergedM->addModuleFlag(Module::Error, "ThinLTO", uint32_t(0));
  ModuleSummaryIndex MergedMIndex =
      buildModuleSummaryIndex(*MergedM, nullptr, &PSI);

  SmallVector<char, 0> Buffer;

  BitcodeWriter W(Buffer);
  // Save the module hash produced for the full bitcode, which will
  // be used in the backends, and use that in the minimized bitcode
  // produced for the full link.
  ModuleHash ModHash = {{0}};
  W.writeModule(M, /*ShouldPreserveUseListOrder=*/false, &Index,
                /*GenerateHash=*/true, &ModHash);
  W.writeModule(*MergedM, /*ShouldPreserveUseListOrder=*/false, &MergedMIndex);
  W.writeSymtab();
  W.writeStrtab();
  OS << Buffer;

  // If a minimized bitcode module was requested for the thin link, only
  // the information that is needed by thin link will be written in the
  // given OS (the merged module will be written as usual).
  if (ThinLinkOS) {
    Buffer.clear();
    BitcodeWriter W2(Buffer);
    StripDebugInfo(M);
    W2.writeThinLinkBitcode(M, Index, ModHash);
    W2.writeModule(*MergedM, /*ShouldPreserveUseListOrder=*/false,
                   &MergedMIndex);
    W2.writeSymtab();
    W2.writeStrtab();
    *ThinLinkOS << Buffer;
  }
}

// Check if the LTO Unit splitting has been enabled.
bool enableSplitLTOUnit(Module &M) {
  bool EnableSplitLTOUnit = false;
  if (auto *MD = mdconst::extract_or_null<ConstantInt>(
          M.getModuleFlag("EnableSplitLTOUnit")))
    EnableSplitLTOUnit = MD->getZExtValue();
  return EnableSplitLTOUnit;
}

// Returns whether this module needs to be split because it uses type metadata.
bool hasTypeMetadata(Module &M) {
  for (auto &GO : M.global_objects()) {
    if (GO.hasMetadata(LLVMContext::MD_type))
      return true;
  }
  return false;
}

bool writeThinLTOBitcode(raw_ostream &OS, raw_ostream *ThinLinkOS,
                         function_ref<AAResults &(Function &)> AARGetter,
                         Module &M, const ModuleSummaryIndex *Index) {
  std::unique_ptr<ModuleSummaryIndex> NewIndex = nullptr;
  // See if this module has any type metadata. If so, we try to split it
  // or at least promote type ids to enable WPD.
  if (hasTypeMetadata(M)) {
    if (enableSplitLTOUnit(M)) {
      splitAndWriteThinLTOBitcode(OS, ThinLinkOS, AARGetter, M);
      return true;
    }
    // Promote type ids as needed for index-based WPD.
    std::string ModuleId = getUniqueModuleId(&M);
    if (!ModuleId.empty()) {
      promoteTypeIds(M, ModuleId);
      // Need to rebuild the index so that it contains type metadata
      // for the newly promoted type ids.
      // FIXME: Probably should not bother building the index at all
      // in the caller of writeThinLTOBitcode (which does so via the
      // ModuleSummaryIndexAnalysis pass), since we have to rebuild it
      // anyway whenever there is type metadata (here or in
      // splitAndWriteThinLTOBitcode). Just always build it once via the
      // buildModuleSummaryIndex when Module(s) are ready.
      ProfileSummaryInfo PSI(M);
      NewIndex = std::make_unique<ModuleSummaryIndex>(
          buildModuleSummaryIndex(M, nullptr, &PSI));
      Index = NewIndex.get();
    }
  }

  // Write it out as an unsplit ThinLTO module.

  // Save the module hash produced for the full bitcode, which will
  // be used in the backends, and use that in the minimized bitcode
  // produced for the full link.
  ModuleHash ModHash = {{0}};
  WriteBitcodeToFile(M, OS, /*ShouldPreserveUseListOrder=*/false, Index,
                     /*GenerateHash=*/true, &ModHash);
  // If a minimized bitcode module was requested for the thin link, only
  // the information that is needed by thin link will be written in the
  // given OS.
  if (ThinLinkOS && Index)
    writeThinLinkBitcodeToFile(M, *ThinLinkOS, *Index, ModHash);
  return false;
}

} // anonymous namespace
extern bool WriteNewDbgInfoFormatToBitcode;
PreservedAnalyses
llvm::ThinLTOBitcodeWriterPass::run(Module &M, ModuleAnalysisManager &AM) {
  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  ScopedDbgInfoFormatSetter FormatSetter(M, M.IsNewDbgInfoFormat &&
                                                WriteNewDbgInfoFormatToBitcode);
  if (M.IsNewDbgInfoFormat)
    M.removeDebugIntrinsicDeclarations();

  bool Changed = writeThinLTOBitcode(
      OS, ThinLinkOS,
      [&FAM](Function &F) -> AAResults & {
        return FAM.getResult<AAManager>(F);
      },
      M, &AM.getResult<ModuleSummaryIndexAnalysis>(M));

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

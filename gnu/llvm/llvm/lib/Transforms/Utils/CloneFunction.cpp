//===- CloneFunction.cpp - Clone a function into another function ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CloneFunctionInto interface, which is used as the
// low-level function cloner.  This is used by the CloneFunction and function
// inliner to do the dirty work of copying the body of a function around.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <map>
#include <optional>
using namespace llvm;

#define DEBUG_TYPE "clone-function"

/// See comments in Cloning.h.
BasicBlock *llvm::CloneBasicBlock(const BasicBlock *BB, ValueToValueMapTy &VMap,
                                  const Twine &NameSuffix, Function *F,
                                  ClonedCodeInfo *CodeInfo,
                                  DebugInfoFinder *DIFinder) {
  BasicBlock *NewBB = BasicBlock::Create(BB->getContext(), "", F);
  NewBB->IsNewDbgInfoFormat = BB->IsNewDbgInfoFormat;
  if (BB->hasName())
    NewBB->setName(BB->getName() + NameSuffix);

  bool hasCalls = false, hasDynamicAllocas = false, hasMemProfMetadata = false;
  Module *TheModule = F ? F->getParent() : nullptr;

  // Loop over all instructions, and copy them over.
  for (const Instruction &I : *BB) {
    if (DIFinder && TheModule)
      DIFinder->processInstruction(*TheModule, I);

    Instruction *NewInst = I.clone();
    if (I.hasName())
      NewInst->setName(I.getName() + NameSuffix);

    NewInst->insertBefore(*NewBB, NewBB->end());
    NewInst->cloneDebugInfoFrom(&I);

    VMap[&I] = NewInst; // Add instruction map to value.

    if (isa<CallInst>(I) && !I.isDebugOrPseudoInst()) {
      hasCalls = true;
      hasMemProfMetadata |= I.hasMetadata(LLVMContext::MD_memprof);
    }
    if (const AllocaInst *AI = dyn_cast<AllocaInst>(&I)) {
      if (!AI->isStaticAlloca()) {
        hasDynamicAllocas = true;
      }
    }
  }

  if (CodeInfo) {
    CodeInfo->ContainsCalls |= hasCalls;
    CodeInfo->ContainsMemProfMetadata |= hasMemProfMetadata;
    CodeInfo->ContainsDynamicAllocas |= hasDynamicAllocas;
  }
  return NewBB;
}

// Clone OldFunc into NewFunc, transforming the old arguments into references to
// VMap values.
//
void llvm::CloneFunctionInto(Function *NewFunc, const Function *OldFunc,
                             ValueToValueMapTy &VMap,
                             CloneFunctionChangeType Changes,
                             SmallVectorImpl<ReturnInst *> &Returns,
                             const char *NameSuffix, ClonedCodeInfo *CodeInfo,
                             ValueMapTypeRemapper *TypeMapper,
                             ValueMaterializer *Materializer) {
  NewFunc->setIsNewDbgInfoFormat(OldFunc->IsNewDbgInfoFormat);
  assert(NameSuffix && "NameSuffix cannot be null!");

#ifndef NDEBUG
  for (const Argument &I : OldFunc->args())
    assert(VMap.count(&I) && "No mapping from source argument specified!");
#endif

  bool ModuleLevelChanges = Changes > CloneFunctionChangeType::LocalChangesOnly;

  // Copy all attributes other than those stored in the AttributeList.  We need
  // to remap the parameter indices of the AttributeList.
  AttributeList NewAttrs = NewFunc->getAttributes();
  NewFunc->copyAttributesFrom(OldFunc);
  NewFunc->setAttributes(NewAttrs);

  const RemapFlags FuncGlobalRefFlags =
      ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges;

  // Fix up the personality function that got copied over.
  if (OldFunc->hasPersonalityFn())
    NewFunc->setPersonalityFn(MapValue(OldFunc->getPersonalityFn(), VMap,
                                       FuncGlobalRefFlags, TypeMapper,
                                       Materializer));

  if (OldFunc->hasPrefixData()) {
    NewFunc->setPrefixData(MapValue(OldFunc->getPrefixData(), VMap,
                                    FuncGlobalRefFlags, TypeMapper,
                                    Materializer));
  }

  if (OldFunc->hasPrologueData()) {
    NewFunc->setPrologueData(MapValue(OldFunc->getPrologueData(), VMap,
                                      FuncGlobalRefFlags, TypeMapper,
                                      Materializer));
  }

  SmallVector<AttributeSet, 4> NewArgAttrs(NewFunc->arg_size());
  AttributeList OldAttrs = OldFunc->getAttributes();

  // Clone any argument attributes that are present in the VMap.
  for (const Argument &OldArg : OldFunc->args()) {
    if (Argument *NewArg = dyn_cast<Argument>(VMap[&OldArg])) {
      NewArgAttrs[NewArg->getArgNo()] =
          OldAttrs.getParamAttrs(OldArg.getArgNo());
    }
  }

  NewFunc->setAttributes(
      AttributeList::get(NewFunc->getContext(), OldAttrs.getFnAttrs(),
                         OldAttrs.getRetAttrs(), NewArgAttrs));

  // Everything else beyond this point deals with function instructions,
  // so if we are dealing with a function declaration, we're done.
  if (OldFunc->isDeclaration())
    return;

  // When we remap instructions within the same module, we want to avoid
  // duplicating inlined DISubprograms, so record all subprograms we find as we
  // duplicate instructions and then freeze them in the MD map. We also record
  // information about dbg.value and dbg.declare to avoid duplicating the
  // types.
  std::optional<DebugInfoFinder> DIFinder;

  // Track the subprogram attachment that needs to be cloned to fine-tune the
  // mapping within the same module.
  DISubprogram *SPClonedWithinModule = nullptr;
  if (Changes < CloneFunctionChangeType::DifferentModule) {
    assert((NewFunc->getParent() == nullptr ||
            NewFunc->getParent() == OldFunc->getParent()) &&
           "Expected NewFunc to have the same parent, or no parent");

    // Need to find subprograms, types, and compile units.
    DIFinder.emplace();

    SPClonedWithinModule = OldFunc->getSubprogram();
    if (SPClonedWithinModule)
      DIFinder->processSubprogram(SPClonedWithinModule);
  } else {
    assert((NewFunc->getParent() == nullptr ||
            NewFunc->getParent() != OldFunc->getParent()) &&
           "Expected NewFunc to have different parents, or no parent");

    if (Changes == CloneFunctionChangeType::DifferentModule) {
      assert(NewFunc->getParent() &&
             "Need parent of new function to maintain debug info invariants");

      // Need to find all the compile units.
      DIFinder.emplace();
    }
  }

  // Loop over all of the basic blocks in the function, cloning them as
  // appropriate.  Note that we save BE this way in order to handle cloning of
  // recursive functions into themselves.
  for (const BasicBlock &BB : *OldFunc) {

    // Create a new basic block and copy instructions into it!
    BasicBlock *CBB = CloneBasicBlock(&BB, VMap, NameSuffix, NewFunc, CodeInfo,
                                      DIFinder ? &*DIFinder : nullptr);

    // Add basic block mapping.
    VMap[&BB] = CBB;

    // It is only legal to clone a function if a block address within that
    // function is never referenced outside of the function.  Given that, we
    // want to map block addresses from the old function to block addresses in
    // the clone. (This is different from the generic ValueMapper
    // implementation, which generates an invalid blockaddress when
    // cloning a function.)
    if (BB.hasAddressTaken()) {
      Constant *OldBBAddr = BlockAddress::get(const_cast<Function *>(OldFunc),
                                              const_cast<BasicBlock *>(&BB));
      VMap[OldBBAddr] = BlockAddress::get(NewFunc, CBB);
    }

    // Note return instructions for the caller.
    if (ReturnInst *RI = dyn_cast<ReturnInst>(CBB->getTerminator()))
      Returns.push_back(RI);
  }

  if (Changes < CloneFunctionChangeType::DifferentModule &&
      DIFinder->subprogram_count() > 0) {
    // Turn on module-level changes, since we need to clone (some of) the
    // debug info metadata.
    //
    // FIXME: Metadata effectively owned by a function should be made
    // local, and only that local metadata should be cloned.
    ModuleLevelChanges = true;

    auto mapToSelfIfNew = [&VMap](MDNode *N) {
      // Avoid clobbering an existing mapping.
      (void)VMap.MD().try_emplace(N, N);
    };

    // Avoid cloning types, compile units, and (other) subprograms.
    SmallPtrSet<const DISubprogram *, 16> MappedToSelfSPs;
    for (DISubprogram *ISP : DIFinder->subprograms()) {
      if (ISP != SPClonedWithinModule) {
        mapToSelfIfNew(ISP);
        MappedToSelfSPs.insert(ISP);
      }
    }

    // If a subprogram isn't going to be cloned skip its lexical blocks as well.
    for (DIScope *S : DIFinder->scopes()) {
      auto *LScope = dyn_cast<DILocalScope>(S);
      if (LScope && MappedToSelfSPs.count(LScope->getSubprogram()))
        mapToSelfIfNew(S);
    }

    for (DICompileUnit *CU : DIFinder->compile_units())
      mapToSelfIfNew(CU);

    for (DIType *Type : DIFinder->types())
      mapToSelfIfNew(Type);
  } else {
    assert(!SPClonedWithinModule &&
           "Subprogram should be in DIFinder->subprogram_count()...");
  }

  const auto RemapFlag = ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges;
  // Duplicate the metadata that is attached to the cloned function.
  // Subprograms/CUs/types that were already mapped to themselves won't be
  // duplicated.
  SmallVector<std::pair<unsigned, MDNode *>, 1> MDs;
  OldFunc->getAllMetadata(MDs);
  for (auto MD : MDs) {
    NewFunc->addMetadata(MD.first, *MapMetadata(MD.second, VMap, RemapFlag,
                                                TypeMapper, Materializer));
  }

  // Loop over all of the instructions in the new function, fixing up operand
  // references as we go. This uses VMap to do all the hard work.
  for (Function::iterator
           BB = cast<BasicBlock>(VMap[&OldFunc->front()])->getIterator(),
           BE = NewFunc->end();
       BB != BE; ++BB)
    // Loop over all instructions, fixing each one as we find it, and any
    // attached debug-info records.
    for (Instruction &II : *BB) {
      RemapInstruction(&II, VMap, RemapFlag, TypeMapper, Materializer);
      RemapDbgRecordRange(II.getModule(), II.getDbgRecordRange(), VMap,
                          RemapFlag, TypeMapper, Materializer);
    }

  // Only update !llvm.dbg.cu for DifferentModule (not CloneModule). In the
  // same module, the compile unit will already be listed (or not). When
  // cloning a module, CloneModule() will handle creating the named metadata.
  if (Changes != CloneFunctionChangeType::DifferentModule)
    return;

  // Update !llvm.dbg.cu with compile units added to the new module if this
  // function is being cloned in isolation.
  //
  // FIXME: This is making global / module-level changes, which doesn't seem
  // like the right encapsulation  Consider dropping the requirement to update
  // !llvm.dbg.cu (either obsoleting the node, or restricting it to
  // non-discardable compile units) instead of discovering compile units by
  // visiting the metadata attached to global values, which would allow this
  // code to be deleted. Alternatively, perhaps give responsibility for this
  // update to CloneFunctionInto's callers.
  auto *NewModule = NewFunc->getParent();
  auto *NMD = NewModule->getOrInsertNamedMetadata("llvm.dbg.cu");
  // Avoid multiple insertions of the same DICompileUnit to NMD.
  SmallPtrSet<const void *, 8> Visited;
  for (auto *Operand : NMD->operands())
    Visited.insert(Operand);
  for (auto *Unit : DIFinder->compile_units()) {
    MDNode *MappedUnit =
        MapMetadata(Unit, VMap, RF_None, TypeMapper, Materializer);
    if (Visited.insert(MappedUnit).second)
      NMD->addOperand(MappedUnit);
  }
}

/// Return a copy of the specified function and add it to that function's
/// module.  Also, any references specified in the VMap are changed to refer to
/// their mapped value instead of the original one.  If any of the arguments to
/// the function are in the VMap, the arguments are deleted from the resultant
/// function.  The VMap is updated to include mappings from all of the
/// instructions and basicblocks in the function from their old to new values.
///
Function *llvm::CloneFunction(Function *F, ValueToValueMapTy &VMap,
                              ClonedCodeInfo *CodeInfo) {
  std::vector<Type *> ArgTypes;

  // The user might be deleting arguments to the function by specifying them in
  // the VMap.  If so, we need to not add the arguments to the arg ty vector
  //
  for (const Argument &I : F->args())
    if (VMap.count(&I) == 0) // Haven't mapped the argument to anything yet?
      ArgTypes.push_back(I.getType());

  // Create a new function type...
  FunctionType *FTy =
      FunctionType::get(F->getFunctionType()->getReturnType(), ArgTypes,
                        F->getFunctionType()->isVarArg());

  // Create the new function...
  Function *NewF = Function::Create(FTy, F->getLinkage(), F->getAddressSpace(),
                                    F->getName(), F->getParent());
  NewF->setIsNewDbgInfoFormat(F->IsNewDbgInfoFormat);

  // Loop over the arguments, copying the names of the mapped arguments over...
  Function::arg_iterator DestI = NewF->arg_begin();
  for (const Argument &I : F->args())
    if (VMap.count(&I) == 0) {     // Is this argument preserved?
      DestI->setName(I.getName()); // Copy the name over...
      VMap[&I] = &*DestI++;        // Add mapping to VMap
    }

  SmallVector<ReturnInst *, 8> Returns; // Ignore returns cloned.
  CloneFunctionInto(NewF, F, VMap, CloneFunctionChangeType::LocalChangesOnly,
                    Returns, "", CodeInfo);

  return NewF;
}

namespace {
/// This is a private class used to implement CloneAndPruneFunctionInto.
struct PruningFunctionCloner {
  Function *NewFunc;
  const Function *OldFunc;
  ValueToValueMapTy &VMap;
  bool ModuleLevelChanges;
  const char *NameSuffix;
  ClonedCodeInfo *CodeInfo;
  bool HostFuncIsStrictFP;

  Instruction *cloneInstruction(BasicBlock::const_iterator II);

public:
  PruningFunctionCloner(Function *newFunc, const Function *oldFunc,
                        ValueToValueMapTy &valueMap, bool moduleLevelChanges,
                        const char *nameSuffix, ClonedCodeInfo *codeInfo)
      : NewFunc(newFunc), OldFunc(oldFunc), VMap(valueMap),
        ModuleLevelChanges(moduleLevelChanges), NameSuffix(nameSuffix),
        CodeInfo(codeInfo) {
    HostFuncIsStrictFP =
        newFunc->getAttributes().hasFnAttr(Attribute::StrictFP);
  }

  /// The specified block is found to be reachable, clone it and
  /// anything that it can reach.
  void CloneBlock(const BasicBlock *BB, BasicBlock::const_iterator StartingInst,
                  std::vector<const BasicBlock *> &ToClone);
};
} // namespace

Instruction *
PruningFunctionCloner::cloneInstruction(BasicBlock::const_iterator II) {
  const Instruction &OldInst = *II;
  Instruction *NewInst = nullptr;
  if (HostFuncIsStrictFP) {
    Intrinsic::ID CIID = getConstrainedIntrinsicID(OldInst);
    if (CIID != Intrinsic::not_intrinsic) {
      // Instead of cloning the instruction, a call to constrained intrinsic
      // should be created.
      // Assume the first arguments of constrained intrinsics are the same as
      // the operands of original instruction.

      // Determine overloaded types of the intrinsic.
      SmallVector<Type *, 2> TParams;
      SmallVector<Intrinsic::IITDescriptor, 8> Descriptor;
      getIntrinsicInfoTableEntries(CIID, Descriptor);
      for (unsigned I = 0, E = Descriptor.size(); I != E; ++I) {
        Intrinsic::IITDescriptor Operand = Descriptor[I];
        switch (Operand.Kind) {
        case Intrinsic::IITDescriptor::Argument:
          if (Operand.getArgumentKind() !=
              Intrinsic::IITDescriptor::AK_MatchType) {
            if (I == 0)
              TParams.push_back(OldInst.getType());
            else
              TParams.push_back(OldInst.getOperand(I - 1)->getType());
          }
          break;
        case Intrinsic::IITDescriptor::SameVecWidthArgument:
          ++I;
          break;
        default:
          break;
        }
      }

      // Create intrinsic call.
      LLVMContext &Ctx = NewFunc->getContext();
      Function *IFn =
          Intrinsic::getDeclaration(NewFunc->getParent(), CIID, TParams);
      SmallVector<Value *, 4> Args;
      unsigned NumOperands = OldInst.getNumOperands();
      if (isa<CallInst>(OldInst))
        --NumOperands;
      for (unsigned I = 0; I < NumOperands; ++I) {
        Value *Op = OldInst.getOperand(I);
        Args.push_back(Op);
      }
      if (const auto *CmpI = dyn_cast<FCmpInst>(&OldInst)) {
        FCmpInst::Predicate Pred = CmpI->getPredicate();
        StringRef PredName = FCmpInst::getPredicateName(Pred);
        Args.push_back(MetadataAsValue::get(Ctx, MDString::get(Ctx, PredName)));
      }

      // The last arguments of a constrained intrinsic are metadata that
      // represent rounding mode (absents in some intrinsics) and exception
      // behavior. The inlined function uses default settings.
      if (Intrinsic::hasConstrainedFPRoundingModeOperand(CIID))
        Args.push_back(
            MetadataAsValue::get(Ctx, MDString::get(Ctx, "round.tonearest")));
      Args.push_back(
          MetadataAsValue::get(Ctx, MDString::get(Ctx, "fpexcept.ignore")));

      NewInst = CallInst::Create(IFn, Args, OldInst.getName() + ".strict");
    }
  }
  if (!NewInst)
    NewInst = II->clone();
  return NewInst;
}

/// The specified block is found to be reachable, clone it and
/// anything that it can reach.
void PruningFunctionCloner::CloneBlock(
    const BasicBlock *BB, BasicBlock::const_iterator StartingInst,
    std::vector<const BasicBlock *> &ToClone) {
  WeakTrackingVH &BBEntry = VMap[BB];

  // Have we already cloned this block?
  if (BBEntry)
    return;

  // Nope, clone it now.
  BasicBlock *NewBB;
  Twine NewName(BB->hasName() ? Twine(BB->getName()) + NameSuffix : "");
  BBEntry = NewBB = BasicBlock::Create(BB->getContext(), NewName, NewFunc);
  NewBB->IsNewDbgInfoFormat = BB->IsNewDbgInfoFormat;

  // It is only legal to clone a function if a block address within that
  // function is never referenced outside of the function.  Given that, we
  // want to map block addresses from the old function to block addresses in
  // the clone. (This is different from the generic ValueMapper
  // implementation, which generates an invalid blockaddress when
  // cloning a function.)
  //
  // Note that we don't need to fix the mapping for unreachable blocks;
  // the default mapping there is safe.
  if (BB->hasAddressTaken()) {
    Constant *OldBBAddr = BlockAddress::get(const_cast<Function *>(OldFunc),
                                            const_cast<BasicBlock *>(BB));
    VMap[OldBBAddr] = BlockAddress::get(NewFunc, NewBB);
  }

  bool hasCalls = false, hasDynamicAllocas = false, hasStaticAllocas = false;
  bool hasMemProfMetadata = false;

  // Keep a cursor pointing at the last place we cloned debug-info records from.
  BasicBlock::const_iterator DbgCursor = StartingInst;
  auto CloneDbgRecordsToHere =
      [NewBB, &DbgCursor](Instruction *NewInst, BasicBlock::const_iterator II) {
        if (!NewBB->IsNewDbgInfoFormat)
          return;

        // Clone debug-info records onto this instruction. Iterate through any
        // source-instructions we've cloned and then subsequently optimised
        // away, so that their debug-info doesn't go missing.
        for (; DbgCursor != II; ++DbgCursor)
          NewInst->cloneDebugInfoFrom(&*DbgCursor, std::nullopt, false);
        NewInst->cloneDebugInfoFrom(&*II);
        DbgCursor = std::next(II);
      };

  // Loop over all instructions, and copy them over, DCE'ing as we go.  This
  // loop doesn't include the terminator.
  for (BasicBlock::const_iterator II = StartingInst, IE = --BB->end(); II != IE;
       ++II) {

    Instruction *NewInst = cloneInstruction(II);
    NewInst->insertInto(NewBB, NewBB->end());

    if (HostFuncIsStrictFP) {
      // All function calls in the inlined function must get 'strictfp'
      // attribute to prevent undesirable optimizations.
      if (auto *Call = dyn_cast<CallInst>(NewInst))
        Call->addFnAttr(Attribute::StrictFP);
    }

    // Eagerly remap operands to the newly cloned instruction, except for PHI
    // nodes for which we defer processing until we update the CFG. Also defer
    // debug intrinsic processing because they may contain use-before-defs.
    if (!isa<PHINode>(NewInst) && !isa<DbgVariableIntrinsic>(NewInst)) {
      RemapInstruction(NewInst, VMap,
                       ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges);

      // Eagerly constant fold the newly cloned instruction. If successful, add
      // a mapping to the new value. Non-constant operands may be incomplete at
      // this stage, thus instruction simplification is performed after
      // processing phi-nodes.
      if (Value *V = ConstantFoldInstruction(
              NewInst, BB->getDataLayout())) {
        if (isInstructionTriviallyDead(NewInst)) {
          VMap[&*II] = V;
          NewInst->eraseFromParent();
          continue;
        }
      }
    }

    if (II->hasName())
      NewInst->setName(II->getName() + NameSuffix);
    VMap[&*II] = NewInst; // Add instruction map to value.
    if (isa<CallInst>(II) && !II->isDebugOrPseudoInst()) {
      hasCalls = true;
      hasMemProfMetadata |= II->hasMetadata(LLVMContext::MD_memprof);
    }

    CloneDbgRecordsToHere(NewInst, II);

    if (CodeInfo) {
      CodeInfo->OrigVMap[&*II] = NewInst;
      if (auto *CB = dyn_cast<CallBase>(&*II))
        if (CB->hasOperandBundles())
          CodeInfo->OperandBundleCallSites.push_back(NewInst);
    }

    if (const AllocaInst *AI = dyn_cast<AllocaInst>(II)) {
      if (isa<ConstantInt>(AI->getArraySize()))
        hasStaticAllocas = true;
      else
        hasDynamicAllocas = true;
    }
  }

  // Finally, clone over the terminator.
  const Instruction *OldTI = BB->getTerminator();
  bool TerminatorDone = false;
  if (const BranchInst *BI = dyn_cast<BranchInst>(OldTI)) {
    if (BI->isConditional()) {
      // If the condition was a known constant in the callee...
      ConstantInt *Cond = dyn_cast<ConstantInt>(BI->getCondition());
      // Or is a known constant in the caller...
      if (!Cond) {
        Value *V = VMap.lookup(BI->getCondition());
        Cond = dyn_cast_or_null<ConstantInt>(V);
      }

      // Constant fold to uncond branch!
      if (Cond) {
        BasicBlock *Dest = BI->getSuccessor(!Cond->getZExtValue());
        VMap[OldTI] = BranchInst::Create(Dest, NewBB);
        ToClone.push_back(Dest);
        TerminatorDone = true;
      }
    }
  } else if (const SwitchInst *SI = dyn_cast<SwitchInst>(OldTI)) {
    // If switching on a value known constant in the caller.
    ConstantInt *Cond = dyn_cast<ConstantInt>(SI->getCondition());
    if (!Cond) { // Or known constant after constant prop in the callee...
      Value *V = VMap.lookup(SI->getCondition());
      Cond = dyn_cast_or_null<ConstantInt>(V);
    }
    if (Cond) { // Constant fold to uncond branch!
      SwitchInst::ConstCaseHandle Case = *SI->findCaseValue(Cond);
      BasicBlock *Dest = const_cast<BasicBlock *>(Case.getCaseSuccessor());
      VMap[OldTI] = BranchInst::Create(Dest, NewBB);
      ToClone.push_back(Dest);
      TerminatorDone = true;
    }
  }

  if (!TerminatorDone) {
    Instruction *NewInst = OldTI->clone();
    if (OldTI->hasName())
      NewInst->setName(OldTI->getName() + NameSuffix);
    NewInst->insertInto(NewBB, NewBB->end());

    CloneDbgRecordsToHere(NewInst, OldTI->getIterator());

    VMap[OldTI] = NewInst; // Add instruction map to value.

    if (CodeInfo) {
      CodeInfo->OrigVMap[OldTI] = NewInst;
      if (auto *CB = dyn_cast<CallBase>(OldTI))
        if (CB->hasOperandBundles())
          CodeInfo->OperandBundleCallSites.push_back(NewInst);
    }

    // Recursively clone any reachable successor blocks.
    append_range(ToClone, successors(BB->getTerminator()));
  } else {
    // If we didn't create a new terminator, clone DbgVariableRecords from the
    // old terminator onto the new terminator.
    Instruction *NewInst = NewBB->getTerminator();
    assert(NewInst);

    CloneDbgRecordsToHere(NewInst, OldTI->getIterator());
  }

  if (CodeInfo) {
    CodeInfo->ContainsCalls |= hasCalls;
    CodeInfo->ContainsMemProfMetadata |= hasMemProfMetadata;
    CodeInfo->ContainsDynamicAllocas |= hasDynamicAllocas;
    CodeInfo->ContainsDynamicAllocas |=
        hasStaticAllocas && BB != &BB->getParent()->front();
  }
}

/// This works like CloneAndPruneFunctionInto, except that it does not clone the
/// entire function. Instead it starts at an instruction provided by the caller
/// and copies (and prunes) only the code reachable from that instruction.
void llvm::CloneAndPruneIntoFromInst(Function *NewFunc, const Function *OldFunc,
                                     const Instruction *StartingInst,
                                     ValueToValueMapTy &VMap,
                                     bool ModuleLevelChanges,
                                     SmallVectorImpl<ReturnInst *> &Returns,
                                     const char *NameSuffix,
                                     ClonedCodeInfo *CodeInfo) {
  assert(NameSuffix && "NameSuffix cannot be null!");

  ValueMapTypeRemapper *TypeMapper = nullptr;
  ValueMaterializer *Materializer = nullptr;

#ifndef NDEBUG
  // If the cloning starts at the beginning of the function, verify that
  // the function arguments are mapped.
  if (!StartingInst)
    for (const Argument &II : OldFunc->args())
      assert(VMap.count(&II) && "No mapping from source argument specified!");
#endif

  PruningFunctionCloner PFC(NewFunc, OldFunc, VMap, ModuleLevelChanges,
                            NameSuffix, CodeInfo);
  const BasicBlock *StartingBB;
  if (StartingInst)
    StartingBB = StartingInst->getParent();
  else {
    StartingBB = &OldFunc->getEntryBlock();
    StartingInst = &StartingBB->front();
  }

  // Collect debug intrinsics for remapping later.
  SmallVector<const DbgVariableIntrinsic *, 8> DbgIntrinsics;
  for (const auto &BB : *OldFunc) {
    for (const auto &I : BB) {
      if (const auto *DVI = dyn_cast<DbgVariableIntrinsic>(&I))
        DbgIntrinsics.push_back(DVI);
    }
  }

  // Clone the entry block, and anything recursively reachable from it.
  std::vector<const BasicBlock *> CloneWorklist;
  PFC.CloneBlock(StartingBB, StartingInst->getIterator(), CloneWorklist);
  while (!CloneWorklist.empty()) {
    const BasicBlock *BB = CloneWorklist.back();
    CloneWorklist.pop_back();
    PFC.CloneBlock(BB, BB->begin(), CloneWorklist);
  }

  // Loop over all of the basic blocks in the old function.  If the block was
  // reachable, we have cloned it and the old block is now in the value map:
  // insert it into the new function in the right order.  If not, ignore it.
  //
  // Defer PHI resolution until rest of function is resolved.
  SmallVector<const PHINode *, 16> PHIToResolve;
  for (const BasicBlock &BI : *OldFunc) {
    Value *V = VMap.lookup(&BI);
    BasicBlock *NewBB = cast_or_null<BasicBlock>(V);
    if (!NewBB)
      continue; // Dead block.

    // Move the new block to preserve the order in the original function.
    NewBB->moveBefore(NewFunc->end());

    // Handle PHI nodes specially, as we have to remove references to dead
    // blocks.
    for (const PHINode &PN : BI.phis()) {
      // PHI nodes may have been remapped to non-PHI nodes by the caller or
      // during the cloning process.
      if (isa<PHINode>(VMap[&PN]))
        PHIToResolve.push_back(&PN);
      else
        break;
    }

    // Finally, remap the terminator instructions, as those can't be remapped
    // until all BBs are mapped.
    RemapInstruction(NewBB->getTerminator(), VMap,
                     ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges,
                     TypeMapper, Materializer);
  }

  // Defer PHI resolution until rest of function is resolved, PHI resolution
  // requires the CFG to be up-to-date.
  for (unsigned phino = 0, e = PHIToResolve.size(); phino != e;) {
    const PHINode *OPN = PHIToResolve[phino];
    unsigned NumPreds = OPN->getNumIncomingValues();
    const BasicBlock *OldBB = OPN->getParent();
    BasicBlock *NewBB = cast<BasicBlock>(VMap[OldBB]);

    // Map operands for blocks that are live and remove operands for blocks
    // that are dead.
    for (; phino != PHIToResolve.size() &&
           PHIToResolve[phino]->getParent() == OldBB;
         ++phino) {
      OPN = PHIToResolve[phino];
      PHINode *PN = cast<PHINode>(VMap[OPN]);
      for (unsigned pred = 0, e = NumPreds; pred != e; ++pred) {
        Value *V = VMap.lookup(PN->getIncomingBlock(pred));
        if (BasicBlock *MappedBlock = cast_or_null<BasicBlock>(V)) {
          Value *InVal =
              MapValue(PN->getIncomingValue(pred), VMap,
                       ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges);
          assert(InVal && "Unknown input value?");
          PN->setIncomingValue(pred, InVal);
          PN->setIncomingBlock(pred, MappedBlock);
        } else {
          PN->removeIncomingValue(pred, false);
          --pred; // Revisit the next entry.
          --e;
        }
      }
    }

    // The loop above has removed PHI entries for those blocks that are dead
    // and has updated others.  However, if a block is live (i.e. copied over)
    // but its terminator has been changed to not go to this block, then our
    // phi nodes will have invalid entries.  Update the PHI nodes in this
    // case.
    PHINode *PN = cast<PHINode>(NewBB->begin());
    NumPreds = pred_size(NewBB);
    if (NumPreds != PN->getNumIncomingValues()) {
      assert(NumPreds < PN->getNumIncomingValues());
      // Count how many times each predecessor comes to this block.
      std::map<BasicBlock *, unsigned> PredCount;
      for (BasicBlock *Pred : predecessors(NewBB))
        --PredCount[Pred];

      // Figure out how many entries to remove from each PHI.
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
        ++PredCount[PN->getIncomingBlock(i)];

      // At this point, the excess predecessor entries are positive in the
      // map.  Loop over all of the PHIs and remove excess predecessor
      // entries.
      BasicBlock::iterator I = NewBB->begin();
      for (; (PN = dyn_cast<PHINode>(I)); ++I) {
        for (const auto &PCI : PredCount) {
          BasicBlock *Pred = PCI.first;
          for (unsigned NumToRemove = PCI.second; NumToRemove; --NumToRemove)
            PN->removeIncomingValue(Pred, false);
        }
      }
    }

    // If the loops above have made these phi nodes have 0 or 1 operand,
    // replace them with poison or the input value.  We must do this for
    // correctness, because 0-operand phis are not valid.
    PN = cast<PHINode>(NewBB->begin());
    if (PN->getNumIncomingValues() == 0) {
      BasicBlock::iterator I = NewBB->begin();
      BasicBlock::const_iterator OldI = OldBB->begin();
      while ((PN = dyn_cast<PHINode>(I++))) {
        Value *NV = PoisonValue::get(PN->getType());
        PN->replaceAllUsesWith(NV);
        assert(VMap[&*OldI] == PN && "VMap mismatch");
        VMap[&*OldI] = NV;
        PN->eraseFromParent();
        ++OldI;
      }
    }
  }

  // Drop all incompatible return attributes that cannot be applied to NewFunc
  // during cloning, so as to allow instruction simplification to reason on the
  // old state of the function. The original attributes are restored later.
  AttributeMask IncompatibleAttrs =
      AttributeFuncs::typeIncompatible(OldFunc->getReturnType());
  AttributeList Attrs = NewFunc->getAttributes();
  NewFunc->removeRetAttrs(IncompatibleAttrs);

  // As phi-nodes have been now remapped, allow incremental simplification of
  // newly-cloned instructions.
  const DataLayout &DL = NewFunc->getDataLayout();
  for (const auto &BB : *OldFunc) {
    for (const auto &I : BB) {
      auto *NewI = dyn_cast_or_null<Instruction>(VMap.lookup(&I));
      if (!NewI)
        continue;

      if (Value *V = simplifyInstruction(NewI, DL)) {
        NewI->replaceAllUsesWith(V);

        if (isInstructionTriviallyDead(NewI)) {
          NewI->eraseFromParent();
        } else {
          // Did not erase it? Restore the new instruction into VMap previously
          // dropped by `ValueIsRAUWd`.
          VMap[&I] = NewI;
        }
      }
    }
  }

  // Restore attributes.
  NewFunc->setAttributes(Attrs);

  // Remap debug intrinsic operands now that all values have been mapped.
  // Doing this now (late) preserves use-before-defs in debug intrinsics. If
  // we didn't do this, ValueAsMetadata(use-before-def) operands would be
  // replaced by empty metadata. This would signal later cleanup passes to
  // remove the debug intrinsics, potentially causing incorrect locations.
  for (const auto *DVI : DbgIntrinsics) {
    if (DbgVariableIntrinsic *NewDVI =
            cast_or_null<DbgVariableIntrinsic>(VMap.lookup(DVI)))
      RemapInstruction(NewDVI, VMap,
                       ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges,
                       TypeMapper, Materializer);
  }

  // Do the same for DbgVariableRecords, touching all the instructions in the
  // cloned range of blocks.
  Function::iterator Begin = cast<BasicBlock>(VMap[StartingBB])->getIterator();
  for (BasicBlock &BB : make_range(Begin, NewFunc->end())) {
    for (Instruction &I : BB) {
      RemapDbgRecordRange(I.getModule(), I.getDbgRecordRange(), VMap,
                          ModuleLevelChanges ? RF_None
                                             : RF_NoModuleLevelChanges,
                          TypeMapper, Materializer);
    }
  }

  // Simplify conditional branches and switches with a constant operand. We try
  // to prune these out when cloning, but if the simplification required
  // looking through PHI nodes, those are only available after forming the full
  // basic block. That may leave some here, and we still want to prune the dead
  // code as early as possible.
  for (BasicBlock &BB : make_range(Begin, NewFunc->end()))
    ConstantFoldTerminator(&BB);

  // Some blocks may have become unreachable as a result. Find and delete them.
  {
    SmallPtrSet<BasicBlock *, 16> ReachableBlocks;
    SmallVector<BasicBlock *, 16> Worklist;
    Worklist.push_back(&*Begin);
    while (!Worklist.empty()) {
      BasicBlock *BB = Worklist.pop_back_val();
      if (ReachableBlocks.insert(BB).second)
        append_range(Worklist, successors(BB));
    }

    SmallVector<BasicBlock *, 16> UnreachableBlocks;
    for (BasicBlock &BB : make_range(Begin, NewFunc->end()))
      if (!ReachableBlocks.contains(&BB))
        UnreachableBlocks.push_back(&BB);
    DeleteDeadBlocks(UnreachableBlocks);
  }

  // Now that the inlined function body has been fully constructed, go through
  // and zap unconditional fall-through branches. This happens all the time when
  // specializing code: code specialization turns conditional branches into
  // uncond branches, and this code folds them.
  Function::iterator I = Begin;
  while (I != NewFunc->end()) {
    BranchInst *BI = dyn_cast<BranchInst>(I->getTerminator());
    if (!BI || BI->isConditional()) {
      ++I;
      continue;
    }

    BasicBlock *Dest = BI->getSuccessor(0);
    if (!Dest->getSinglePredecessor()) {
      ++I;
      continue;
    }

    // We shouldn't be able to get single-entry PHI nodes here, as instsimplify
    // above should have zapped all of them..
    assert(!isa<PHINode>(Dest->begin()));

    // We know all single-entry PHI nodes in the inlined function have been
    // removed, so we just need to splice the blocks.
    BI->eraseFromParent();

    // Make all PHI nodes that referred to Dest now refer to I as their source.
    Dest->replaceAllUsesWith(&*I);

    // Move all the instructions in the succ to the pred.
    I->splice(I->end(), Dest);

    // Remove the dest block.
    Dest->eraseFromParent();

    // Do not increment I, iteratively merge all things this block branches to.
  }

  // Make a final pass over the basic blocks from the old function to gather
  // any return instructions which survived folding. We have to do this here
  // because we can iteratively remove and merge returns above.
  for (Function::iterator I = cast<BasicBlock>(VMap[StartingBB])->getIterator(),
                          E = NewFunc->end();
       I != E; ++I)
    if (ReturnInst *RI = dyn_cast<ReturnInst>(I->getTerminator()))
      Returns.push_back(RI);
}

/// This works exactly like CloneFunctionInto,
/// except that it does some simple constant prop and DCE on the fly.  The
/// effect of this is to copy significantly less code in cases where (for
/// example) a function call with constant arguments is inlined, and those
/// constant arguments cause a significant amount of code in the callee to be
/// dead.  Since this doesn't produce an exact copy of the input, it can't be
/// used for things like CloneFunction or CloneModule.
void llvm::CloneAndPruneFunctionInto(
    Function *NewFunc, const Function *OldFunc, ValueToValueMapTy &VMap,
    bool ModuleLevelChanges, SmallVectorImpl<ReturnInst *> &Returns,
    const char *NameSuffix, ClonedCodeInfo *CodeInfo) {
  CloneAndPruneIntoFromInst(NewFunc, OldFunc, &OldFunc->front().front(), VMap,
                            ModuleLevelChanges, Returns, NameSuffix, CodeInfo);
}

/// Remaps instructions in \p Blocks using the mapping in \p VMap.
void llvm::remapInstructionsInBlocks(ArrayRef<BasicBlock *> Blocks,
                                     ValueToValueMapTy &VMap) {
  // Rewrite the code to refer to itself.
  for (auto *BB : Blocks) {
    for (auto &Inst : *BB) {
      RemapDbgRecordRange(Inst.getModule(), Inst.getDbgRecordRange(), VMap,
                          RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
      RemapInstruction(&Inst, VMap,
                       RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
    }
  }
}

/// Clones a loop \p OrigLoop.  Returns the loop and the blocks in \p
/// Blocks.
///
/// Updates LoopInfo and DominatorTree assuming the loop is dominated by block
/// \p LoopDomBB.  Insert the new blocks before block specified in \p Before.
Loop *llvm::cloneLoopWithPreheader(BasicBlock *Before, BasicBlock *LoopDomBB,
                                   Loop *OrigLoop, ValueToValueMapTy &VMap,
                                   const Twine &NameSuffix, LoopInfo *LI,
                                   DominatorTree *DT,
                                   SmallVectorImpl<BasicBlock *> &Blocks) {
  Function *F = OrigLoop->getHeader()->getParent();
  Loop *ParentLoop = OrigLoop->getParentLoop();
  DenseMap<Loop *, Loop *> LMap;

  Loop *NewLoop = LI->AllocateLoop();
  LMap[OrigLoop] = NewLoop;
  if (ParentLoop)
    ParentLoop->addChildLoop(NewLoop);
  else
    LI->addTopLevelLoop(NewLoop);

  BasicBlock *OrigPH = OrigLoop->getLoopPreheader();
  assert(OrigPH && "No preheader");
  BasicBlock *NewPH = CloneBasicBlock(OrigPH, VMap, NameSuffix, F);
  // To rename the loop PHIs.
  VMap[OrigPH] = NewPH;
  Blocks.push_back(NewPH);

  // Update LoopInfo.
  if (ParentLoop)
    ParentLoop->addBasicBlockToLoop(NewPH, *LI);

  // Update DominatorTree.
  DT->addNewBlock(NewPH, LoopDomBB);

  for (Loop *CurLoop : OrigLoop->getLoopsInPreorder()) {
    Loop *&NewLoop = LMap[CurLoop];
    if (!NewLoop) {
      NewLoop = LI->AllocateLoop();

      // Establish the parent/child relationship.
      Loop *OrigParent = CurLoop->getParentLoop();
      assert(OrigParent && "Could not find the original parent loop");
      Loop *NewParentLoop = LMap[OrigParent];
      assert(NewParentLoop && "Could not find the new parent loop");

      NewParentLoop->addChildLoop(NewLoop);
    }
  }

  for (BasicBlock *BB : OrigLoop->getBlocks()) {
    Loop *CurLoop = LI->getLoopFor(BB);
    Loop *&NewLoop = LMap[CurLoop];
    assert(NewLoop && "Expecting new loop to be allocated");

    BasicBlock *NewBB = CloneBasicBlock(BB, VMap, NameSuffix, F);
    VMap[BB] = NewBB;

    // Update LoopInfo.
    NewLoop->addBasicBlockToLoop(NewBB, *LI);

    // Add DominatorTree node. After seeing all blocks, update to correct
    // IDom.
    DT->addNewBlock(NewBB, NewPH);

    Blocks.push_back(NewBB);
  }

  for (BasicBlock *BB : OrigLoop->getBlocks()) {
    // Update loop headers.
    Loop *CurLoop = LI->getLoopFor(BB);
    if (BB == CurLoop->getHeader())
      LMap[CurLoop]->moveToHeader(cast<BasicBlock>(VMap[BB]));

    // Update DominatorTree.
    BasicBlock *IDomBB = DT->getNode(BB)->getIDom()->getBlock();
    DT->changeImmediateDominator(cast<BasicBlock>(VMap[BB]),
                                 cast<BasicBlock>(VMap[IDomBB]));
  }

  // Move them physically from the end of the block list.
  F->splice(Before->getIterator(), F, NewPH->getIterator());
  F->splice(Before->getIterator(), F, NewLoop->getHeader()->getIterator(),
            F->end());

  return NewLoop;
}

/// Duplicate non-Phi instructions from the beginning of block up to
/// StopAt instruction into a split block between BB and its predecessor.
BasicBlock *llvm::DuplicateInstructionsInSplitBetween(
    BasicBlock *BB, BasicBlock *PredBB, Instruction *StopAt,
    ValueToValueMapTy &ValueMapping, DomTreeUpdater &DTU) {

  assert(count(successors(PredBB), BB) == 1 &&
         "There must be a single edge between PredBB and BB!");
  // We are going to have to map operands from the original BB block to the new
  // copy of the block 'NewBB'.  If there are PHI nodes in BB, evaluate them to
  // account for entry from PredBB.
  BasicBlock::iterator BI = BB->begin();
  for (; PHINode *PN = dyn_cast<PHINode>(BI); ++BI)
    ValueMapping[PN] = PN->getIncomingValueForBlock(PredBB);

  BasicBlock *NewBB = SplitEdge(PredBB, BB);
  NewBB->setName(PredBB->getName() + ".split");
  Instruction *NewTerm = NewBB->getTerminator();

  // FIXME: SplitEdge does not yet take a DTU, so we include the split edge
  //        in the update set here.
  DTU.applyUpdates({{DominatorTree::Delete, PredBB, BB},
                    {DominatorTree::Insert, PredBB, NewBB},
                    {DominatorTree::Insert, NewBB, BB}});

  // Clone the non-phi instructions of BB into NewBB, keeping track of the
  // mapping and using it to remap operands in the cloned instructions.
  // Stop once we see the terminator too. This covers the case where BB's
  // terminator gets replaced and StopAt == BB's terminator.
  for (; StopAt != &*BI && BB->getTerminator() != &*BI; ++BI) {
    Instruction *New = BI->clone();
    New->setName(BI->getName());
    New->insertBefore(NewTerm);
    New->cloneDebugInfoFrom(&*BI);
    ValueMapping[&*BI] = New;

    // Remap operands to patch up intra-block references.
    for (unsigned i = 0, e = New->getNumOperands(); i != e; ++i)
      if (Instruction *Inst = dyn_cast<Instruction>(New->getOperand(i))) {
        auto I = ValueMapping.find(Inst);
        if (I != ValueMapping.end())
          New->setOperand(i, I->second);
      }

    // Remap debug variable operands.
    remapDebugVariable(ValueMapping, New);
  }

  return NewBB;
}

void llvm::cloneNoAliasScopes(ArrayRef<MDNode *> NoAliasDeclScopes,
                              DenseMap<MDNode *, MDNode *> &ClonedScopes,
                              StringRef Ext, LLVMContext &Context) {
  MDBuilder MDB(Context);

  for (auto *ScopeList : NoAliasDeclScopes) {
    for (const auto &MDOperand : ScopeList->operands()) {
      if (MDNode *MD = dyn_cast<MDNode>(MDOperand)) {
        AliasScopeNode SNANode(MD);

        std::string Name;
        auto ScopeName = SNANode.getName();
        if (!ScopeName.empty())
          Name = (Twine(ScopeName) + ":" + Ext).str();
        else
          Name = std::string(Ext);

        MDNode *NewScope = MDB.createAnonymousAliasScope(
            const_cast<MDNode *>(SNANode.getDomain()), Name);
        ClonedScopes.insert(std::make_pair(MD, NewScope));
      }
    }
  }
}

void llvm::adaptNoAliasScopes(Instruction *I,
                              const DenseMap<MDNode *, MDNode *> &ClonedScopes,
                              LLVMContext &Context) {
  auto CloneScopeList = [&](const MDNode *ScopeList) -> MDNode * {
    bool NeedsReplacement = false;
    SmallVector<Metadata *, 8> NewScopeList;
    for (const auto &MDOp : ScopeList->operands()) {
      if (MDNode *MD = dyn_cast<MDNode>(MDOp)) {
        if (auto *NewMD = ClonedScopes.lookup(MD)) {
          NewScopeList.push_back(NewMD);
          NeedsReplacement = true;
          continue;
        }
        NewScopeList.push_back(MD);
      }
    }
    if (NeedsReplacement)
      return MDNode::get(Context, NewScopeList);
    return nullptr;
  };

  if (auto *Decl = dyn_cast<NoAliasScopeDeclInst>(I))
    if (auto *NewScopeList = CloneScopeList(Decl->getScopeList()))
      Decl->setScopeList(NewScopeList);

  auto replaceWhenNeeded = [&](unsigned MD_ID) {
    if (const MDNode *CSNoAlias = I->getMetadata(MD_ID))
      if (auto *NewScopeList = CloneScopeList(CSNoAlias))
        I->setMetadata(MD_ID, NewScopeList);
  };
  replaceWhenNeeded(LLVMContext::MD_noalias);
  replaceWhenNeeded(LLVMContext::MD_alias_scope);
}

void llvm::cloneAndAdaptNoAliasScopes(ArrayRef<MDNode *> NoAliasDeclScopes,
                                      ArrayRef<BasicBlock *> NewBlocks,
                                      LLVMContext &Context, StringRef Ext) {
  if (NoAliasDeclScopes.empty())
    return;

  DenseMap<MDNode *, MDNode *> ClonedScopes;
  LLVM_DEBUG(dbgs() << "cloneAndAdaptNoAliasScopes: cloning "
                    << NoAliasDeclScopes.size() << " node(s)\n");

  cloneNoAliasScopes(NoAliasDeclScopes, ClonedScopes, Ext, Context);
  // Identify instructions using metadata that needs adaptation
  for (BasicBlock *NewBlock : NewBlocks)
    for (Instruction &I : *NewBlock)
      adaptNoAliasScopes(&I, ClonedScopes, Context);
}

void llvm::cloneAndAdaptNoAliasScopes(ArrayRef<MDNode *> NoAliasDeclScopes,
                                      Instruction *IStart, Instruction *IEnd,
                                      LLVMContext &Context, StringRef Ext) {
  if (NoAliasDeclScopes.empty())
    return;

  DenseMap<MDNode *, MDNode *> ClonedScopes;
  LLVM_DEBUG(dbgs() << "cloneAndAdaptNoAliasScopes: cloning "
                    << NoAliasDeclScopes.size() << " node(s)\n");

  cloneNoAliasScopes(NoAliasDeclScopes, ClonedScopes, Ext, Context);
  // Identify instructions using metadata that needs adaptation
  assert(IStart->getParent() == IEnd->getParent() && "different basic block ?");
  auto ItStart = IStart->getIterator();
  auto ItEnd = IEnd->getIterator();
  ++ItEnd; // IEnd is included, increment ItEnd to get the end of the range
  for (auto &I : llvm::make_range(ItStart, ItEnd))
    adaptNoAliasScopes(&I, ClonedScopes, Context);
}

void llvm::identifyNoAliasScopesToClone(
    ArrayRef<BasicBlock *> BBs, SmallVectorImpl<MDNode *> &NoAliasDeclScopes) {
  for (BasicBlock *BB : BBs)
    for (Instruction &I : *BB)
      if (auto *Decl = dyn_cast<NoAliasScopeDeclInst>(&I))
        NoAliasDeclScopes.push_back(Decl->getScopeList());
}

void llvm::identifyNoAliasScopesToClone(
    BasicBlock::iterator Start, BasicBlock::iterator End,
    SmallVectorImpl<MDNode *> &NoAliasDeclScopes) {
  for (Instruction &I : make_range(Start, End))
    if (auto *Decl = dyn_cast<NoAliasScopeDeclInst>(&I))
      NoAliasDeclScopes.push_back(Decl->getScopeList());
}

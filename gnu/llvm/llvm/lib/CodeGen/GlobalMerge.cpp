//===- GlobalMerge.cpp - Internal globals merging -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass merges globals with internal linkage into one. This way all the
// globals which were merged into a biggest one can be addressed using offsets
// from the same base pointer (no need for separate base pointer for each of the
// global). Such a transformation can significantly reduce the register pressure
// when many globals are involved.
//
// For example, consider the code which touches several global variables at
// once:
//
// static int foo[N], bar[N], baz[N];
//
// for (i = 0; i < N; ++i) {
//    foo[i] = bar[i] * baz[i];
// }
//
//  On ARM the addresses of 3 arrays should be kept in the registers, thus
//  this code has quite large register pressure (loop body):
//
//  ldr     r1, [r5], #4
//  ldr     r2, [r6], #4
//  mul     r1, r2, r1
//  str     r1, [r0], #4
//
//  Pass converts the code to something like:
//
//  static struct {
//    int foo[N];
//    int bar[N];
//    int baz[N];
//  } merged;
//
//  for (i = 0; i < N; ++i) {
//    merged.foo[i] = merged.bar[i] * merged.baz[i];
//  }
//
//  and in ARM code this becomes:
//
//  ldr     r0, [r5, #40]
//  ldr     r1, [r5, #80]
//  mul     r0, r1, r0
//  str     r0, [r5], #4
//
//  note that we saved 2 registers here almostly "for free".
//
// However, merging globals can have tradeoffs:
// - it confuses debuggers, tools, and users
// - it makes linker optimizations less useful (order files, LOHs, ...)
// - it forces usage of indexed addressing (which isn't necessarily "free")
// - it can increase register pressure when the uses are disparate enough.
//
// We use heuristics to discover the best global grouping we can (cf cl::opts).
//
// ===---------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalMerge.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "global-merge"

// FIXME: This is only useful as a last-resort way to disable the pass.
static cl::opt<bool>
EnableGlobalMerge("enable-global-merge", cl::Hidden,
                  cl::desc("Enable the global merge pass"),
                  cl::init(true));

static cl::opt<unsigned>
GlobalMergeMaxOffset("global-merge-max-offset", cl::Hidden,
                     cl::desc("Set maximum offset for global merge pass"),
                     cl::init(0));

static cl::opt<bool> GlobalMergeGroupByUse(
    "global-merge-group-by-use", cl::Hidden,
    cl::desc("Improve global merge pass to look at uses"), cl::init(true));

static cl::opt<bool> GlobalMergeIgnoreSingleUse(
    "global-merge-ignore-single-use", cl::Hidden,
    cl::desc("Improve global merge pass to ignore globals only used alone"),
    cl::init(true));

static cl::opt<bool>
EnableGlobalMergeOnConst("global-merge-on-const", cl::Hidden,
                         cl::desc("Enable global merge pass on constants"),
                         cl::init(false));

// FIXME: this could be a transitional option, and we probably need to remove
// it if only we are sure this optimization could always benefit all targets.
static cl::opt<cl::boolOrDefault>
EnableGlobalMergeOnExternal("global-merge-on-external", cl::Hidden,
     cl::desc("Enable global merge pass on external linkage"));

static cl::opt<unsigned>
    GlobalMergeMinDataSize("global-merge-min-data-size",
                           cl::desc("The minimum size in bytes of each global "
                                    "that should considered in merging."),
                           cl::init(0), cl::Hidden);

STATISTIC(NumMerged, "Number of globals merged");

namespace {

class GlobalMergeImpl {
  const TargetMachine *TM = nullptr;
  GlobalMergeOptions Opt;
  bool IsMachO = false;

private:
  bool doMerge(SmallVectorImpl<GlobalVariable *> &Globals, Module &M,
               bool isConst, unsigned AddrSpace) const;

  /// Merge everything in \p Globals for which the corresponding bit
  /// in \p GlobalSet is set.
  bool doMerge(const SmallVectorImpl<GlobalVariable *> &Globals,
               const BitVector &GlobalSet, Module &M, bool isConst,
               unsigned AddrSpace) const;

  /// Check if the given variable has been identified as must keep
  /// \pre setMustKeepGlobalVariables must have been called on the Module that
  ///      contains GV
  bool isMustKeepGlobalVariable(const GlobalVariable *GV) const {
    return MustKeepGlobalVariables.count(GV);
  }

  /// Collect every variables marked as "used" or used in a landing pad
  /// instruction for this Module.
  void setMustKeepGlobalVariables(Module &M);

  /// Collect every variables marked as "used"
  void collectUsedGlobalVariables(Module &M, StringRef Name);

  /// Keep track of the GlobalVariable that must not be merged away
  SmallSetVector<const GlobalVariable *, 16> MustKeepGlobalVariables;

public:
  GlobalMergeImpl(const TargetMachine *TM, GlobalMergeOptions Opt)
      : TM(TM), Opt(Opt) {}
  bool run(Module &M);
};

class GlobalMerge : public FunctionPass {
  const TargetMachine *TM = nullptr;
  GlobalMergeOptions Opt;

public:
  static char ID; // Pass identification, replacement for typeid.

  explicit GlobalMerge() : FunctionPass(ID) {
    Opt.MaxOffset = GlobalMergeMaxOffset;
    initializeGlobalMergePass(*PassRegistry::getPassRegistry());
  }

  explicit GlobalMerge(const TargetMachine *TM, unsigned MaximalOffset,
                       bool OnlyOptimizeForSize, bool MergeExternalGlobals)
      : FunctionPass(ID), TM(TM) {
    Opt.MaxOffset = MaximalOffset;
    Opt.SizeOnly = OnlyOptimizeForSize;
    Opt.MergeExternal = MergeExternalGlobals;
    initializeGlobalMergePass(*PassRegistry::getPassRegistry());
  }

  bool doInitialization(Module &M) override {
    auto GetSmallDataLimit = [](Module &M) -> std::optional<uint64_t> {
      Metadata *SDL = M.getModuleFlag("SmallDataLimit");
      if (!SDL)
        return std::nullopt;
      return mdconst::extract<ConstantInt>(SDL)->getZExtValue();
    };
    if (GlobalMergeMinDataSize.getNumOccurrences())
      Opt.MinSize = GlobalMergeMinDataSize;
    else if (auto SDL = GetSmallDataLimit(M); SDL && *SDL > 0)
      Opt.MinSize = *SDL + 1;
    else
      Opt.MinSize = 0;

    GlobalMergeImpl P(TM, Opt);
    return P.run(M);
  }
  bool runOnFunction(Function &F) override { return false; }

  StringRef getPassName() const override { return "Merge internal globals"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    FunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

PreservedAnalyses GlobalMergePass::run(Module &M, ModuleAnalysisManager &) {
  GlobalMergeImpl P(TM, Options);
  bool Changed = P.run(M);
  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

char GlobalMerge::ID = 0;

INITIALIZE_PASS(GlobalMerge, DEBUG_TYPE, "Merge global variables", false, false)

bool GlobalMergeImpl::doMerge(SmallVectorImpl<GlobalVariable *> &Globals,
                              Module &M, bool isConst,
                              unsigned AddrSpace) const {
  auto &DL = M.getDataLayout();
  // FIXME: Find better heuristics
  llvm::stable_sort(
      Globals, [&DL](const GlobalVariable *GV1, const GlobalVariable *GV2) {
        // We don't support scalable global variables.
        return DL.getTypeAllocSize(GV1->getValueType()).getFixedValue() <
               DL.getTypeAllocSize(GV2->getValueType()).getFixedValue();
      });

  // If we want to just blindly group all globals together, do so.
  if (!GlobalMergeGroupByUse) {
    BitVector AllGlobals(Globals.size());
    AllGlobals.set();
    return doMerge(Globals, AllGlobals, M, isConst, AddrSpace);
  }

  // If we want to be smarter, look at all uses of each global, to try to
  // discover all sets of globals used together, and how many times each of
  // these sets occurred.
  //
  // Keep this reasonably efficient, by having an append-only list of all sets
  // discovered so far (UsedGlobalSet), and mapping each "together-ness" unit of
  // code (currently, a Function) to the set of globals seen so far that are
  // used together in that unit (GlobalUsesByFunction).
  //
  // When we look at the Nth global, we know that any new set is either:
  // - the singleton set {N}, containing this global only, or
  // - the union of {N} and a previously-discovered set, containing some
  //   combination of the previous N-1 globals.
  // Using that knowledge, when looking at the Nth global, we can keep:
  // - a reference to the singleton set {N} (CurGVOnlySetIdx)
  // - a list mapping each previous set to its union with {N} (EncounteredUGS),
  //   if it actually occurs.

  // We keep track of the sets of globals used together "close enough".
  struct UsedGlobalSet {
    BitVector Globals;
    unsigned UsageCount = 1;

    UsedGlobalSet(size_t Size) : Globals(Size) {}
  };

  // Each set is unique in UsedGlobalSets.
  std::vector<UsedGlobalSet> UsedGlobalSets;

  // Avoid repeating the create-global-set pattern.
  auto CreateGlobalSet = [&]() -> UsedGlobalSet & {
    UsedGlobalSets.emplace_back(Globals.size());
    return UsedGlobalSets.back();
  };

  // The first set is the empty set.
  CreateGlobalSet().UsageCount = 0;

  // We define "close enough" to be "in the same function".
  // FIXME: Grouping uses by function is way too aggressive, so we should have
  // a better metric for distance between uses.
  // The obvious alternative would be to group by BasicBlock, but that's in
  // turn too conservative..
  // Anything in between wouldn't be trivial to compute, so just stick with
  // per-function grouping.

  // The value type is an index into UsedGlobalSets.
  // The default (0) conveniently points to the empty set.
  DenseMap<Function *, size_t /*UsedGlobalSetIdx*/> GlobalUsesByFunction;

  // Now, look at each merge-eligible global in turn.

  // Keep track of the sets we already encountered to which we added the
  // current global.
  // Each element matches the same-index element in UsedGlobalSets.
  // This lets us efficiently tell whether a set has already been expanded to
  // include the current global.
  std::vector<size_t> EncounteredUGS;

  for (size_t GI = 0, GE = Globals.size(); GI != GE; ++GI) {
    GlobalVariable *GV = Globals[GI];

    // Reset the encountered sets for this global and grow it in case we created
    // new sets for the previous global.
    EncounteredUGS.assign(UsedGlobalSets.size(), 0);

    // We might need to create a set that only consists of the current global.
    // Keep track of its index into UsedGlobalSets.
    size_t CurGVOnlySetIdx = 0;

    // For each global, look at all its Uses.
    for (auto &U : GV->uses()) {
      // This Use might be a ConstantExpr.  We're interested in Instruction
      // users, so look through ConstantExpr...
      Use *UI, *UE;
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(U.getUser())) {
        if (CE->use_empty())
          continue;
        UI = &*CE->use_begin();
        UE = nullptr;
      } else if (isa<Instruction>(U.getUser())) {
        UI = &U;
        UE = UI->getNext();
      } else {
        continue;
      }

      // ...to iterate on all the instruction users of the global.
      // Note that we iterate on Uses and not on Users to be able to getNext().
      for (; UI != UE; UI = UI->getNext()) {
        Instruction *I = dyn_cast<Instruction>(UI->getUser());
        if (!I)
          continue;

        Function *ParentFn = I->getParent()->getParent();

        // If we're only optimizing for size, ignore non-minsize functions.
        if (Opt.SizeOnly && !ParentFn->hasMinSize())
          continue;

        size_t UGSIdx = GlobalUsesByFunction[ParentFn];

        // If this is the first global the basic block uses, map it to the set
        // consisting of this global only.
        if (!UGSIdx) {
          // If that set doesn't exist yet, create it.
          if (!CurGVOnlySetIdx) {
            CurGVOnlySetIdx = UsedGlobalSets.size();
            CreateGlobalSet().Globals.set(GI);
          } else {
            ++UsedGlobalSets[CurGVOnlySetIdx].UsageCount;
          }

          GlobalUsesByFunction[ParentFn] = CurGVOnlySetIdx;
          continue;
        }

        // If we already encountered this BB, just increment the counter.
        if (UsedGlobalSets[UGSIdx].Globals.test(GI)) {
          ++UsedGlobalSets[UGSIdx].UsageCount;
          continue;
        }

        // If not, the previous set wasn't actually used in this function.
        --UsedGlobalSets[UGSIdx].UsageCount;

        // If we already expanded the previous set to include this global, just
        // reuse that expanded set.
        if (size_t ExpandedIdx = EncounteredUGS[UGSIdx]) {
          ++UsedGlobalSets[ExpandedIdx].UsageCount;
          GlobalUsesByFunction[ParentFn] = ExpandedIdx;
          continue;
        }

        // If not, create a new set consisting of the union of the previous set
        // and this global.  Mark it as encountered, so we can reuse it later.
        GlobalUsesByFunction[ParentFn] = EncounteredUGS[UGSIdx] =
            UsedGlobalSets.size();

        UsedGlobalSet &NewUGS = CreateGlobalSet();
        NewUGS.Globals.set(GI);
        NewUGS.Globals |= UsedGlobalSets[UGSIdx].Globals;
      }
    }
  }

  // Now we found a bunch of sets of globals used together.  We accumulated
  // the number of times we encountered the sets (i.e., the number of blocks
  // that use that exact set of globals).
  //
  // Multiply that by the size of the set to give us a crude profitability
  // metric.
  llvm::stable_sort(UsedGlobalSets,
                    [](const UsedGlobalSet &UGS1, const UsedGlobalSet &UGS2) {
                      return UGS1.Globals.count() * UGS1.UsageCount <
                             UGS2.Globals.count() * UGS2.UsageCount;
                    });

  // We can choose to merge all globals together, but ignore globals never used
  // with another global.  This catches the obviously non-profitable cases of
  // having a single global, but is aggressive enough for any other case.
  if (GlobalMergeIgnoreSingleUse) {
    BitVector AllGlobals(Globals.size());
    for (const UsedGlobalSet &UGS : llvm::reverse(UsedGlobalSets)) {
      if (UGS.UsageCount == 0)
        continue;
      if (UGS.Globals.count() > 1)
        AllGlobals |= UGS.Globals;
    }
    return doMerge(Globals, AllGlobals, M, isConst, AddrSpace);
  }

  // Starting from the sets with the best (=biggest) profitability, find a
  // good combination.
  // The ideal (and expensive) solution can only be found by trying all
  // combinations, looking for the one with the best profitability.
  // Don't be smart about it, and just pick the first compatible combination,
  // starting with the sets with the best profitability.
  BitVector PickedGlobals(Globals.size());
  bool Changed = false;

  for (const UsedGlobalSet &UGS : llvm::reverse(UsedGlobalSets)) {
    if (UGS.UsageCount == 0)
      continue;
    if (PickedGlobals.anyCommon(UGS.Globals))
      continue;
    PickedGlobals |= UGS.Globals;
    // If the set only contains one global, there's no point in merging.
    // Ignore the global for inclusion in other sets though, so keep it in
    // PickedGlobals.
    if (UGS.Globals.count() < 2)
      continue;
    Changed |= doMerge(Globals, UGS.Globals, M, isConst, AddrSpace);
  }

  return Changed;
}

bool GlobalMergeImpl::doMerge(const SmallVectorImpl<GlobalVariable *> &Globals,
                              const BitVector &GlobalSet, Module &M,
                              bool isConst, unsigned AddrSpace) const {
  assert(Globals.size() > 1);

  Type *Int32Ty = Type::getInt32Ty(M.getContext());
  Type *Int8Ty = Type::getInt8Ty(M.getContext());
  auto &DL = M.getDataLayout();

  LLVM_DEBUG(dbgs() << " Trying to merge set, starts with #"
                    << GlobalSet.find_first() << "\n");

  bool Changed = false;
  ssize_t i = GlobalSet.find_first();
  while (i != -1) {
    ssize_t j = 0;
    uint64_t MergedSize = 0;
    std::vector<Type*> Tys;
    std::vector<Constant*> Inits;
    std::vector<unsigned> StructIdxs;

    bool HasExternal = false;
    StringRef FirstExternalName;
    Align MaxAlign;
    unsigned CurIdx = 0;
    for (j = i; j != -1; j = GlobalSet.find_next(j)) {
      Type *Ty = Globals[j]->getValueType();

      // Make sure we use the same alignment AsmPrinter would use.
      Align Alignment = DL.getPreferredAlign(Globals[j]);
      unsigned Padding = alignTo(MergedSize, Alignment) - MergedSize;
      MergedSize += Padding;
      MergedSize += DL.getTypeAllocSize(Ty);
      if (MergedSize > Opt.MaxOffset) {
        break;
      }
      if (Padding) {
        Tys.push_back(ArrayType::get(Int8Ty, Padding));
        Inits.push_back(ConstantAggregateZero::get(Tys.back()));
        ++CurIdx;
      }
      Tys.push_back(Ty);
      Inits.push_back(Globals[j]->getInitializer());
      StructIdxs.push_back(CurIdx++);

      MaxAlign = std::max(MaxAlign, Alignment);

      if (Globals[j]->hasExternalLinkage() && !HasExternal) {
        HasExternal = true;
        FirstExternalName = Globals[j]->getName();
      }
    }

    // Exit early if there is only one global to merge.
    if (Tys.size() < 2) {
      i = j;
      continue;
    }

    // If merged variables doesn't have external linkage, we needn't to expose
    // the symbol after merging.
    GlobalValue::LinkageTypes Linkage = HasExternal
                                            ? GlobalValue::ExternalLinkage
                                            : GlobalValue::InternalLinkage;
    // Use a packed struct so we can control alignment.
    StructType *MergedTy = StructType::get(M.getContext(), Tys, true);
    Constant *MergedInit = ConstantStruct::get(MergedTy, Inits);

    // On Darwin external linkage needs to be preserved, otherwise
    // dsymutil cannot preserve the debug info for the merged
    // variables.  If they have external linkage, use the symbol name
    // of the first variable merged as the suffix of global symbol
    // name.  This avoids a link-time naming conflict for the
    // _MergedGlobals symbols.
    Twine MergedName =
        (IsMachO && HasExternal)
            ? "_MergedGlobals_" + FirstExternalName
            : "_MergedGlobals";
    auto MergedLinkage = IsMachO ? Linkage : GlobalValue::PrivateLinkage;
    auto *MergedGV = new GlobalVariable(
        M, MergedTy, isConst, MergedLinkage, MergedInit, MergedName, nullptr,
        GlobalVariable::NotThreadLocal, AddrSpace);

    MergedGV->setAlignment(MaxAlign);
    MergedGV->setSection(Globals[i]->getSection());

    const StructLayout *MergedLayout = DL.getStructLayout(MergedTy);
    for (ssize_t k = i, idx = 0; k != j; k = GlobalSet.find_next(k), ++idx) {
      GlobalValue::LinkageTypes Linkage = Globals[k]->getLinkage();
      std::string Name(Globals[k]->getName());
      GlobalValue::VisibilityTypes Visibility = Globals[k]->getVisibility();
      GlobalValue::DLLStorageClassTypes DLLStorage =
          Globals[k]->getDLLStorageClass();

      // Copy metadata while adjusting any debug info metadata by the original
      // global's offset within the merged global.
      MergedGV->copyMetadata(Globals[k],
                             MergedLayout->getElementOffset(StructIdxs[idx]));

      Constant *Idx[2] = {
          ConstantInt::get(Int32Ty, 0),
          ConstantInt::get(Int32Ty, StructIdxs[idx]),
      };
      Constant *GEP =
          ConstantExpr::getInBoundsGetElementPtr(MergedTy, MergedGV, Idx);
      Globals[k]->replaceAllUsesWith(GEP);
      Globals[k]->eraseFromParent();

      // When the linkage is not internal we must emit an alias for the original
      // variable name as it may be accessed from another object. On non-Mach-O
      // we can also emit an alias for internal linkage as it's safe to do so.
      // It's not safe on Mach-O as the alias (and thus the portion of the
      // MergedGlobals variable) may be dead stripped at link time.
      if (Linkage != GlobalValue::InternalLinkage || !IsMachO) {
        GlobalAlias *GA = GlobalAlias::create(Tys[StructIdxs[idx]], AddrSpace,
                                              Linkage, Name, GEP, &M);
        GA->setVisibility(Visibility);
        GA->setDLLStorageClass(DLLStorage);
      }

      NumMerged++;
    }
    Changed = true;
    i = j;
  }

  return Changed;
}

void GlobalMergeImpl::collectUsedGlobalVariables(Module &M, StringRef Name) {
  // Extract global variables from llvm.used array
  const GlobalVariable *GV = M.getGlobalVariable(Name);
  if (!GV || !GV->hasInitializer()) return;

  // Should be an array of 'i8*'.
  const ConstantArray *InitList = cast<ConstantArray>(GV->getInitializer());

  for (unsigned i = 0, e = InitList->getNumOperands(); i != e; ++i)
    if (const GlobalVariable *G =
        dyn_cast<GlobalVariable>(InitList->getOperand(i)->stripPointerCasts()))
      MustKeepGlobalVariables.insert(G);
}

void GlobalMergeImpl::setMustKeepGlobalVariables(Module &M) {
  collectUsedGlobalVariables(M, "llvm.used");
  collectUsedGlobalVariables(M, "llvm.compiler.used");

  for (Function &F : M) {
    for (BasicBlock &BB : F) {
      Instruction *Pad = BB.getFirstNonPHI();
      if (!Pad->isEHPad())
        continue;

      // Keep globals used by landingpads and catchpads.
      for (const Use &U : Pad->operands()) {
        if (const GlobalVariable *GV =
                dyn_cast<GlobalVariable>(U->stripPointerCasts()))
          MustKeepGlobalVariables.insert(GV);
        else if (const ConstantArray *CA = dyn_cast<ConstantArray>(U->stripPointerCasts())) {
          for (const Use &Elt : CA->operands()) {
            if (const GlobalVariable *GV =
                    dyn_cast<GlobalVariable>(Elt->stripPointerCasts()))
              MustKeepGlobalVariables.insert(GV);
          }
        }
      }
    }
  }
}

bool GlobalMergeImpl::run(Module &M) {
  if (!EnableGlobalMerge)
    return false;

  IsMachO = Triple(M.getTargetTriple()).isOSBinFormatMachO();

  auto &DL = M.getDataLayout();
  MapVector<std::pair<unsigned, StringRef>, SmallVector<GlobalVariable *, 0>>
      Globals, ConstGlobals, BSSGlobals;
  bool Changed = false;
  setMustKeepGlobalVariables(M);

  LLVM_DEBUG({
      dbgs() << "Number of GV that must be kept:  " <<
                MustKeepGlobalVariables.size() << "\n";
      for (const GlobalVariable *KeptGV : MustKeepGlobalVariables)
        dbgs() << "Kept: " << *KeptGV << "\n";
  });
  // Grab all non-const globals.
  for (auto &GV : M.globals()) {
    // Merge is safe for "normal" internal or external globals only
    if (GV.isDeclaration() || GV.isThreadLocal() || GV.hasImplicitSection())
      continue;

    // It's not safe to merge globals that may be preempted
    if (TM && !TM->shouldAssumeDSOLocal(&GV))
      continue;

    if (!(Opt.MergeExternal && GV.hasExternalLinkage()) &&
        !GV.hasInternalLinkage())
      continue;

    PointerType *PT = dyn_cast<PointerType>(GV.getType());
    assert(PT && "Global variable is not a pointer!");

    unsigned AddressSpace = PT->getAddressSpace();
    StringRef Section = GV.getSection();

    // Ignore all 'special' globals.
    if (GV.getName().starts_with("llvm.") || GV.getName().starts_with(".llvm."))
      continue;

    // Ignore all "required" globals:
    if (isMustKeepGlobalVariable(&GV))
      continue;

    // Don't merge tagged globals, as each global should have its own unique
    // memory tag at runtime. TODO(hctim): This can be relaxed: constant globals
    // with compatible alignment and the same contents may be merged as long as
    // the globals occupy the same number of tag granules (i.e. `size_a / 16 ==
    // size_b / 16`).
    if (GV.isTagged())
      continue;

    Type *Ty = GV.getValueType();
    TypeSize AllocSize = DL.getTypeAllocSize(Ty);
    if (AllocSize < Opt.MaxOffset && AllocSize >= Opt.MinSize) {
      if (TM &&
          TargetLoweringObjectFile::getKindForGlobal(&GV, *TM).isBSS())
        BSSGlobals[{AddressSpace, Section}].push_back(&GV);
      else if (GV.isConstant())
        ConstGlobals[{AddressSpace, Section}].push_back(&GV);
      else
        Globals[{AddressSpace, Section}].push_back(&GV);
    }
  }

  for (auto &P : Globals)
    if (P.second.size() > 1)
      Changed |= doMerge(P.second, M, false, P.first.first);

  for (auto &P : BSSGlobals)
    if (P.second.size() > 1)
      Changed |= doMerge(P.second, M, false, P.first.first);

  if (EnableGlobalMergeOnConst)
    for (auto &P : ConstGlobals)
      if (P.second.size() > 1)
        Changed |= doMerge(P.second, M, true, P.first.first);

  return Changed;
}

Pass *llvm::createGlobalMergePass(const TargetMachine *TM, unsigned Offset,
                                  bool OnlyOptimizeForSize,
                                  bool MergeExternalByDefault) {
  bool MergeExternal = (EnableGlobalMergeOnExternal == cl::BOU_UNSET) ?
    MergeExternalByDefault : (EnableGlobalMergeOnExternal == cl::BOU_TRUE);
  return new GlobalMerge(TM, Offset, OnlyOptimizeForSize, MergeExternal);
}

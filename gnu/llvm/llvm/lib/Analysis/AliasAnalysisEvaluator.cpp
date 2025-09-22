//===- AliasAnalysisEvaluator.cpp - Alias Analysis Accuracy Evaluator -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/AliasAnalysisEvaluator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

static cl::opt<bool> PrintAll("print-all-alias-modref-info", cl::ReallyHidden);

static cl::opt<bool> PrintNoAlias("print-no-aliases", cl::ReallyHidden);
static cl::opt<bool> PrintMayAlias("print-may-aliases", cl::ReallyHidden);
static cl::opt<bool> PrintPartialAlias("print-partial-aliases", cl::ReallyHidden);
static cl::opt<bool> PrintMustAlias("print-must-aliases", cl::ReallyHidden);

static cl::opt<bool> PrintNoModRef("print-no-modref", cl::ReallyHidden);
static cl::opt<bool> PrintRef("print-ref", cl::ReallyHidden);
static cl::opt<bool> PrintMod("print-mod", cl::ReallyHidden);
static cl::opt<bool> PrintModRef("print-modref", cl::ReallyHidden);

static cl::opt<bool> EvalAAMD("evaluate-aa-metadata", cl::ReallyHidden);

static void PrintResults(AliasResult AR, bool P,
                         std::pair<const Value *, Type *> Loc1,
                         std::pair<const Value *, Type *> Loc2,
                         const Module *M) {
  if (PrintAll || P) {
    Type *Ty1 = Loc1.second, *Ty2 = Loc2.second;
    unsigned AS1 = Loc1.first->getType()->getPointerAddressSpace();
    unsigned AS2 = Loc2.first->getType()->getPointerAddressSpace();
    std::string o1, o2;
    {
      raw_string_ostream os1(o1), os2(o2);
      Loc1.first->printAsOperand(os1, false, M);
      Loc2.first->printAsOperand(os2, false, M);
    }

    if (o2 < o1) {
      std::swap(o1, o2);
      std::swap(Ty1, Ty2);
      std::swap(AS1, AS2);
      // Change offset sign for the local AR, for printing only.
      AR.swap();
    }
    errs() << "  " << AR << ":\t";
    Ty1->print(errs(), false, /* NoDetails */ true);
    if (AS1 != 0)
      errs() << " addrspace(" << AS1 << ")";
    errs() << "* " << o1 << ", ";
    Ty2->print(errs(), false, /* NoDetails */ true);
    if (AS2 != 0)
      errs() << " addrspace(" << AS2 << ")";
    errs() << "* " << o2 << "\n";
  }
}

static inline void PrintModRefResults(
    const char *Msg, bool P, Instruction *I,
    std::pair<const Value *, Type *> Loc, Module *M) {
  if (PrintAll || P) {
    errs() << "  " << Msg << ":  Ptr: ";
    Loc.second->print(errs(), false, /* NoDetails */ true);
    errs() << "* ";
    Loc.first->printAsOperand(errs(), false, M);
    errs() << "\t<->" << *I << '\n';
  }
}

static inline void PrintModRefResults(const char *Msg, bool P, CallBase *CallA,
                                      CallBase *CallB, Module *M) {
  if (PrintAll || P) {
    errs() << "  " << Msg << ": " << *CallA << " <-> " << *CallB << '\n';
  }
}

static inline void PrintLoadStoreResults(AliasResult AR, bool P,
                                         const Value *V1, const Value *V2,
                                         const Module *M) {
  if (PrintAll || P) {
    errs() << "  " << AR << ": " << *V1 << " <-> " << *V2 << '\n';
  }
}

PreservedAnalyses AAEvaluator::run(Function &F, FunctionAnalysisManager &AM) {
  runInternal(F, AM.getResult<AAManager>(F));
  return PreservedAnalyses::all();
}

void AAEvaluator::runInternal(Function &F, AAResults &AA) {
  const DataLayout &DL = F.getDataLayout();

  ++FunctionCount;

  SetVector<std::pair<const Value *, Type *>> Pointers;
  SmallSetVector<CallBase *, 16> Calls;
  SetVector<Value *> Loads;
  SetVector<Value *> Stores;

  for (Instruction &Inst : instructions(F)) {
    if (auto *LI = dyn_cast<LoadInst>(&Inst)) {
      Pointers.insert({LI->getPointerOperand(), LI->getType()});
      Loads.insert(LI);
    } else if (auto *SI = dyn_cast<StoreInst>(&Inst)) {
      Pointers.insert({SI->getPointerOperand(),
                       SI->getValueOperand()->getType()});
      Stores.insert(SI);
    } else if (auto *CB = dyn_cast<CallBase>(&Inst))
      Calls.insert(CB);
  }

  if (PrintAll || PrintNoAlias || PrintMayAlias || PrintPartialAlias ||
      PrintMustAlias || PrintNoModRef || PrintMod || PrintRef || PrintModRef)
    errs() << "Function: " << F.getName() << ": " << Pointers.size()
           << " pointers, " << Calls.size() << " call sites\n";

  // iterate over the worklist, and run the full (n^2)/2 disambiguations
  for (auto I1 = Pointers.begin(), E = Pointers.end(); I1 != E; ++I1) {
    LocationSize Size1 = LocationSize::precise(DL.getTypeStoreSize(I1->second));
    for (auto I2 = Pointers.begin(); I2 != I1; ++I2) {
      LocationSize Size2 =
          LocationSize::precise(DL.getTypeStoreSize(I2->second));
      AliasResult AR = AA.alias(I1->first, Size1, I2->first, Size2);
      switch (AR) {
      case AliasResult::NoAlias:
        PrintResults(AR, PrintNoAlias, *I1, *I2, F.getParent());
        ++NoAliasCount;
        break;
      case AliasResult::MayAlias:
        PrintResults(AR, PrintMayAlias, *I1, *I2, F.getParent());
        ++MayAliasCount;
        break;
      case AliasResult::PartialAlias:
        PrintResults(AR, PrintPartialAlias, *I1, *I2, F.getParent());
        ++PartialAliasCount;
        break;
      case AliasResult::MustAlias:
        PrintResults(AR, PrintMustAlias, *I1, *I2, F.getParent());
        ++MustAliasCount;
        break;
      }
    }
  }

  if (EvalAAMD) {
    // iterate over all pairs of load, store
    for (Value *Load : Loads) {
      for (Value *Store : Stores) {
        AliasResult AR = AA.alias(MemoryLocation::get(cast<LoadInst>(Load)),
                                  MemoryLocation::get(cast<StoreInst>(Store)));
        switch (AR) {
        case AliasResult::NoAlias:
          PrintLoadStoreResults(AR, PrintNoAlias, Load, Store, F.getParent());
          ++NoAliasCount;
          break;
        case AliasResult::MayAlias:
          PrintLoadStoreResults(AR, PrintMayAlias, Load, Store, F.getParent());
          ++MayAliasCount;
          break;
        case AliasResult::PartialAlias:
          PrintLoadStoreResults(AR, PrintPartialAlias, Load, Store, F.getParent());
          ++PartialAliasCount;
          break;
        case AliasResult::MustAlias:
          PrintLoadStoreResults(AR, PrintMustAlias, Load, Store, F.getParent());
          ++MustAliasCount;
          break;
        }
      }
    }

    // iterate over all pairs of store, store
    for (SetVector<Value *>::iterator I1 = Stores.begin(), E = Stores.end();
         I1 != E; ++I1) {
      for (SetVector<Value *>::iterator I2 = Stores.begin(); I2 != I1; ++I2) {
        AliasResult AR = AA.alias(MemoryLocation::get(cast<StoreInst>(*I1)),
                                  MemoryLocation::get(cast<StoreInst>(*I2)));
        switch (AR) {
        case AliasResult::NoAlias:
          PrintLoadStoreResults(AR, PrintNoAlias, *I1, *I2, F.getParent());
          ++NoAliasCount;
          break;
        case AliasResult::MayAlias:
          PrintLoadStoreResults(AR, PrintMayAlias, *I1, *I2, F.getParent());
          ++MayAliasCount;
          break;
        case AliasResult::PartialAlias:
          PrintLoadStoreResults(AR, PrintPartialAlias, *I1, *I2, F.getParent());
          ++PartialAliasCount;
          break;
        case AliasResult::MustAlias:
          PrintLoadStoreResults(AR, PrintMustAlias, *I1, *I2, F.getParent());
          ++MustAliasCount;
          break;
        }
      }
    }
  }

  // Mod/ref alias analysis: compare all pairs of calls and values
  for (CallBase *Call : Calls) {
    for (const auto &Pointer : Pointers) {
      LocationSize Size =
          LocationSize::precise(DL.getTypeStoreSize(Pointer.second));
      switch (AA.getModRefInfo(Call, Pointer.first, Size)) {
      case ModRefInfo::NoModRef:
        PrintModRefResults("NoModRef", PrintNoModRef, Call, Pointer,
                           F.getParent());
        ++NoModRefCount;
        break;
      case ModRefInfo::Mod:
        PrintModRefResults("Just Mod", PrintMod, Call, Pointer, F.getParent());
        ++ModCount;
        break;
      case ModRefInfo::Ref:
        PrintModRefResults("Just Ref", PrintRef, Call, Pointer, F.getParent());
        ++RefCount;
        break;
      case ModRefInfo::ModRef:
        PrintModRefResults("Both ModRef", PrintModRef, Call, Pointer,
                           F.getParent());
        ++ModRefCount;
        break;
      }
    }
  }

  // Mod/ref alias analysis: compare all pairs of calls
  for (CallBase *CallA : Calls) {
    for (CallBase *CallB : Calls) {
      if (CallA == CallB)
        continue;
      switch (AA.getModRefInfo(CallA, CallB)) {
      case ModRefInfo::NoModRef:
        PrintModRefResults("NoModRef", PrintNoModRef, CallA, CallB,
                           F.getParent());
        ++NoModRefCount;
        break;
      case ModRefInfo::Mod:
        PrintModRefResults("Just Mod", PrintMod, CallA, CallB, F.getParent());
        ++ModCount;
        break;
      case ModRefInfo::Ref:
        PrintModRefResults("Just Ref", PrintRef, CallA, CallB, F.getParent());
        ++RefCount;
        break;
      case ModRefInfo::ModRef:
        PrintModRefResults("Both ModRef", PrintModRef, CallA, CallB,
                           F.getParent());
        ++ModRefCount;
        break;
      }
    }
  }
}

static void PrintPercent(int64_t Num, int64_t Sum) {
  errs() << "(" << Num * 100LL / Sum << "." << ((Num * 1000LL / Sum) % 10)
         << "%)\n";
}

AAEvaluator::~AAEvaluator() {
  if (FunctionCount == 0)
    return;

  int64_t AliasSum =
      NoAliasCount + MayAliasCount + PartialAliasCount + MustAliasCount;
  errs() << "===== Alias Analysis Evaluator Report =====\n";
  if (AliasSum == 0) {
    errs() << "  Alias Analysis Evaluator Summary: No pointers!\n";
  } else {
    errs() << "  " << AliasSum << " Total Alias Queries Performed\n";
    errs() << "  " << NoAliasCount << " no alias responses ";
    PrintPercent(NoAliasCount, AliasSum);
    errs() << "  " << MayAliasCount << " may alias responses ";
    PrintPercent(MayAliasCount, AliasSum);
    errs() << "  " << PartialAliasCount << " partial alias responses ";
    PrintPercent(PartialAliasCount, AliasSum);
    errs() << "  " << MustAliasCount << " must alias responses ";
    PrintPercent(MustAliasCount, AliasSum);
    errs() << "  Alias Analysis Evaluator Pointer Alias Summary: "
           << NoAliasCount * 100 / AliasSum << "%/"
           << MayAliasCount * 100 / AliasSum << "%/"
           << PartialAliasCount * 100 / AliasSum << "%/"
           << MustAliasCount * 100 / AliasSum << "%\n";
  }

  // Display the summary for mod/ref analysis
  int64_t ModRefSum = NoModRefCount + RefCount + ModCount + ModRefCount;
  if (ModRefSum == 0) {
    errs() << "  Alias Analysis Mod/Ref Evaluator Summary: no "
              "mod/ref!\n";
  } else {
    errs() << "  " << ModRefSum << " Total ModRef Queries Performed\n";
    errs() << "  " << NoModRefCount << " no mod/ref responses ";
    PrintPercent(NoModRefCount, ModRefSum);
    errs() << "  " << ModCount << " mod responses ";
    PrintPercent(ModCount, ModRefSum);
    errs() << "  " << RefCount << " ref responses ";
    PrintPercent(RefCount, ModRefSum);
    errs() << "  " << ModRefCount << " mod & ref responses ";
    PrintPercent(ModRefCount, ModRefSum);
    errs() << "  Alias Analysis Evaluator Mod/Ref Summary: "
           << NoModRefCount * 100 / ModRefSum << "%/"
           << ModCount * 100 / ModRefSum << "%/" << RefCount * 100 / ModRefSum
           << "%/" << ModRefCount * 100 / ModRefSum << "%\n";
  }
}

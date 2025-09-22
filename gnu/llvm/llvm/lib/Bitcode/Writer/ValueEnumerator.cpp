//===- ValueEnumerator.cpp - Number values and types for bitcode writer ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ValueEnumerator class.
//
//===----------------------------------------------------------------------===//

#include "ValueEnumerator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <tuple>

using namespace llvm;

namespace {

struct OrderMap {
  DenseMap<const Value *, std::pair<unsigned, bool>> IDs;
  unsigned LastGlobalValueID = 0;

  OrderMap() = default;

  bool isGlobalValue(unsigned ID) const {
    return ID <= LastGlobalValueID;
  }

  unsigned size() const { return IDs.size(); }
  std::pair<unsigned, bool> &operator[](const Value *V) { return IDs[V]; }

  std::pair<unsigned, bool> lookup(const Value *V) const {
    return IDs.lookup(V);
  }

  void index(const Value *V) {
    // Explicitly sequence get-size and insert-value operations to avoid UB.
    unsigned ID = IDs.size() + 1;
    IDs[V].first = ID;
  }
};

} // end anonymous namespace

static void orderValue(const Value *V, OrderMap &OM) {
  if (OM.lookup(V).first)
    return;

  if (const Constant *C = dyn_cast<Constant>(V)) {
    if (C->getNumOperands()) {
      for (const Value *Op : C->operands())
        if (!isa<BasicBlock>(Op) && !isa<GlobalValue>(Op))
          orderValue(Op, OM);
      if (auto *CE = dyn_cast<ConstantExpr>(C))
        if (CE->getOpcode() == Instruction::ShuffleVector)
          orderValue(CE->getShuffleMaskForBitcode(), OM);
    }
  }

  // Note: we cannot cache this lookup above, since inserting into the map
  // changes the map's size, and thus affects the other IDs.
  OM.index(V);
}

static OrderMap orderModule(const Module &M) {
  // This needs to match the order used by ValueEnumerator::ValueEnumerator()
  // and ValueEnumerator::incorporateFunction().
  OrderMap OM;

  // Initializers of GlobalValues are processed in
  // BitcodeReader::ResolveGlobalAndAliasInits().  Match the order there rather
  // than ValueEnumerator, and match the code in predictValueUseListOrderImpl()
  // by giving IDs in reverse order.
  //
  // Since GlobalValues never reference each other directly (just through
  // initializers), their relative IDs only matter for determining order of
  // uses in their initializers.
  for (const GlobalVariable &G : reverse(M.globals()))
    orderValue(&G, OM);
  for (const GlobalAlias &A : reverse(M.aliases()))
    orderValue(&A, OM);
  for (const GlobalIFunc &I : reverse(M.ifuncs()))
    orderValue(&I, OM);
  for (const Function &F : reverse(M))
    orderValue(&F, OM);
  OM.LastGlobalValueID = OM.size();

  auto orderConstantValue = [&OM](const Value *V) {
    if (isa<Constant>(V) || isa<InlineAsm>(V))
      orderValue(V, OM);
  };

  for (const Function &F : M) {
    if (F.isDeclaration())
      continue;
    // Here we need to match the union of ValueEnumerator::incorporateFunction()
    // and WriteFunction().  Basic blocks are implicitly declared before
    // anything else (by declaring their size).
    for (const BasicBlock &BB : F)
      orderValue(&BB, OM);

    // Metadata used by instructions is decoded before the actual instructions,
    // so visit any constants used by it beforehand.
    for (const BasicBlock &BB : F)
      for (const Instruction &I : BB) {
        auto OrderConstantFromMetadata = [&](Metadata *MD) {
          if (const auto *VAM = dyn_cast<ValueAsMetadata>(MD)) {
            orderConstantValue(VAM->getValue());
          } else if (const auto *AL = dyn_cast<DIArgList>(MD)) {
            for (const auto *VAM : AL->getArgs())
              orderConstantValue(VAM->getValue());
          }
        };

        for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange())) {
          OrderConstantFromMetadata(DVR.getRawLocation());
          if (DVR.isDbgAssign())
            OrderConstantFromMetadata(DVR.getRawAddress());
        }

        for (const Value *V : I.operands()) {
          if (const auto *MAV = dyn_cast<MetadataAsValue>(V))
            OrderConstantFromMetadata(MAV->getMetadata());
        }
      }

    for (const Argument &A : F.args())
      orderValue(&A, OM);
    for (const BasicBlock &BB : F)
      for (const Instruction &I : BB) {
        for (const Value *Op : I.operands())
          orderConstantValue(Op);
        if (auto *SVI = dyn_cast<ShuffleVectorInst>(&I))
          orderValue(SVI->getShuffleMaskForBitcode(), OM);
        orderValue(&I, OM);
      }
  }
  return OM;
}

static void predictValueUseListOrderImpl(const Value *V, const Function *F,
                                         unsigned ID, const OrderMap &OM,
                                         UseListOrderStack &Stack) {
  // Predict use-list order for this one.
  using Entry = std::pair<const Use *, unsigned>;
  SmallVector<Entry, 64> List;
  for (const Use &U : V->uses())
    // Check if this user will be serialized.
    if (OM.lookup(U.getUser()).first)
      List.push_back(std::make_pair(&U, List.size()));

  if (List.size() < 2)
    // We may have lost some users.
    return;

  bool IsGlobalValue = OM.isGlobalValue(ID);
  llvm::sort(List, [&](const Entry &L, const Entry &R) {
    const Use *LU = L.first;
    const Use *RU = R.first;
    if (LU == RU)
      return false;

    auto LID = OM.lookup(LU->getUser()).first;
    auto RID = OM.lookup(RU->getUser()).first;

    // If ID is 4, then expect: 7 6 5 1 2 3.
    if (LID < RID) {
      if (RID <= ID)
        if (!IsGlobalValue) // GlobalValue uses don't get reversed.
          return true;
      return false;
    }
    if (RID < LID) {
      if (LID <= ID)
        if (!IsGlobalValue) // GlobalValue uses don't get reversed.
          return false;
      return true;
    }

    // LID and RID are equal, so we have different operands of the same user.
    // Assume operands are added in order for all instructions.
    if (LID <= ID)
      if (!IsGlobalValue) // GlobalValue uses don't get reversed.
        return LU->getOperandNo() < RU->getOperandNo();
    return LU->getOperandNo() > RU->getOperandNo();
  });

  if (llvm::is_sorted(List, llvm::less_second()))
    // Order is already correct.
    return;

  // Store the shuffle.
  Stack.emplace_back(V, F, List.size());
  assert(List.size() == Stack.back().Shuffle.size() && "Wrong size");
  for (size_t I = 0, E = List.size(); I != E; ++I)
    Stack.back().Shuffle[I] = List[I].second;
}

static void predictValueUseListOrder(const Value *V, const Function *F,
                                     OrderMap &OM, UseListOrderStack &Stack) {
  auto &IDPair = OM[V];
  assert(IDPair.first && "Unmapped value");
  if (IDPair.second)
    // Already predicted.
    return;

  // Do the actual prediction.
  IDPair.second = true;
  if (!V->use_empty() && std::next(V->use_begin()) != V->use_end())
    predictValueUseListOrderImpl(V, F, IDPair.first, OM, Stack);

  // Recursive descent into constants.
  if (const Constant *C = dyn_cast<Constant>(V)) {
    if (C->getNumOperands()) { // Visit GlobalValues.
      for (const Value *Op : C->operands())
        if (isa<Constant>(Op)) // Visit GlobalValues.
          predictValueUseListOrder(Op, F, OM, Stack);
      if (auto *CE = dyn_cast<ConstantExpr>(C))
        if (CE->getOpcode() == Instruction::ShuffleVector)
          predictValueUseListOrder(CE->getShuffleMaskForBitcode(), F, OM,
                                   Stack);
    }
  }
}

static UseListOrderStack predictUseListOrder(const Module &M) {
  OrderMap OM = orderModule(M);

  // Use-list orders need to be serialized after all the users have been added
  // to a value, or else the shuffles will be incomplete.  Store them per
  // function in a stack.
  //
  // Aside from function order, the order of values doesn't matter much here.
  UseListOrderStack Stack;

  // We want to visit the functions backward now so we can list function-local
  // constants in the last Function they're used in.  Module-level constants
  // have already been visited above.
  for (const Function &F : llvm::reverse(M)) {
    auto PredictValueOrderFromMetadata = [&](Metadata *MD) {
      if (const auto *VAM = dyn_cast<ValueAsMetadata>(MD)) {
        predictValueUseListOrder(VAM->getValue(), &F, OM, Stack);
      } else if (const auto *AL = dyn_cast<DIArgList>(MD)) {
        for (const auto *VAM : AL->getArgs())
          predictValueUseListOrder(VAM->getValue(), &F, OM, Stack);
      }
    };
    if (F.isDeclaration())
      continue;
    for (const BasicBlock &BB : F)
      predictValueUseListOrder(&BB, &F, OM, Stack);
    for (const Argument &A : F.args())
      predictValueUseListOrder(&A, &F, OM, Stack);
    for (const BasicBlock &BB : F) {
      for (const Instruction &I : BB) {
        for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange())) {
          PredictValueOrderFromMetadata(DVR.getRawLocation());
          if (DVR.isDbgAssign())
            PredictValueOrderFromMetadata(DVR.getRawAddress());
        }
        for (const Value *Op : I.operands()) {
          if (isa<Constant>(*Op) || isa<InlineAsm>(*Op)) // Visit GlobalValues.
            predictValueUseListOrder(Op, &F, OM, Stack);
          if (const auto *MAV = dyn_cast<MetadataAsValue>(Op))
            PredictValueOrderFromMetadata(MAV->getMetadata());
        }
        if (auto *SVI = dyn_cast<ShuffleVectorInst>(&I))
          predictValueUseListOrder(SVI->getShuffleMaskForBitcode(), &F, OM,
                                   Stack);
        predictValueUseListOrder(&I, &F, OM, Stack);
      }
    }
  }

  // Visit globals last, since the module-level use-list block will be seen
  // before the function bodies are processed.
  for (const GlobalVariable &G : M.globals())
    predictValueUseListOrder(&G, nullptr, OM, Stack);
  for (const Function &F : M)
    predictValueUseListOrder(&F, nullptr, OM, Stack);
  for (const GlobalAlias &A : M.aliases())
    predictValueUseListOrder(&A, nullptr, OM, Stack);
  for (const GlobalIFunc &I : M.ifuncs())
    predictValueUseListOrder(&I, nullptr, OM, Stack);
  for (const GlobalVariable &G : M.globals())
    if (G.hasInitializer())
      predictValueUseListOrder(G.getInitializer(), nullptr, OM, Stack);
  for (const GlobalAlias &A : M.aliases())
    predictValueUseListOrder(A.getAliasee(), nullptr, OM, Stack);
  for (const GlobalIFunc &I : M.ifuncs())
    predictValueUseListOrder(I.getResolver(), nullptr, OM, Stack);
  for (const Function &F : M) {
    for (const Use &U : F.operands())
      predictValueUseListOrder(U.get(), nullptr, OM, Stack);
  }

  return Stack;
}

static bool isIntOrIntVectorValue(const std::pair<const Value*, unsigned> &V) {
  return V.first->getType()->isIntOrIntVectorTy();
}

ValueEnumerator::ValueEnumerator(const Module &M,
                                 bool ShouldPreserveUseListOrder)
    : ShouldPreserveUseListOrder(ShouldPreserveUseListOrder) {
  if (ShouldPreserveUseListOrder)
    UseListOrders = predictUseListOrder(M);

  // Enumerate the global variables.
  for (const GlobalVariable &GV : M.globals()) {
    EnumerateValue(&GV);
    EnumerateType(GV.getValueType());
  }

  // Enumerate the functions.
  for (const Function & F : M) {
    EnumerateValue(&F);
    EnumerateType(F.getValueType());
    EnumerateAttributes(F.getAttributes());
  }

  // Enumerate the aliases.
  for (const GlobalAlias &GA : M.aliases()) {
    EnumerateValue(&GA);
    EnumerateType(GA.getValueType());
  }

  // Enumerate the ifuncs.
  for (const GlobalIFunc &GIF : M.ifuncs()) {
    EnumerateValue(&GIF);
    EnumerateType(GIF.getValueType());
  }

  // Remember what is the cutoff between globalvalue's and other constants.
  unsigned FirstConstant = Values.size();

  // Enumerate the global variable initializers and attributes.
  for (const GlobalVariable &GV : M.globals()) {
    if (GV.hasInitializer())
      EnumerateValue(GV.getInitializer());
    if (GV.hasAttributes())
      EnumerateAttributes(GV.getAttributesAsList(AttributeList::FunctionIndex));
  }

  // Enumerate the aliasees.
  for (const GlobalAlias &GA : M.aliases())
    EnumerateValue(GA.getAliasee());

  // Enumerate the ifunc resolvers.
  for (const GlobalIFunc &GIF : M.ifuncs())
    EnumerateValue(GIF.getResolver());

  // Enumerate any optional Function data.
  for (const Function &F : M)
    for (const Use &U : F.operands())
      EnumerateValue(U.get());

  // Enumerate the metadata type.
  //
  // TODO: Move this to ValueEnumerator::EnumerateOperandType() once bitcode
  // only encodes the metadata type when it's used as a value.
  EnumerateType(Type::getMetadataTy(M.getContext()));

  // Insert constants and metadata that are named at module level into the slot
  // pool so that the module symbol table can refer to them...
  EnumerateValueSymbolTable(M.getValueSymbolTable());
  EnumerateNamedMetadata(M);

  SmallVector<std::pair<unsigned, MDNode *>, 8> MDs;
  for (const GlobalVariable &GV : M.globals()) {
    MDs.clear();
    GV.getAllMetadata(MDs);
    for (const auto &I : MDs)
      // FIXME: Pass GV to EnumerateMetadata and arrange for the bitcode writer
      // to write metadata to the global variable's own metadata block
      // (PR28134).
      EnumerateMetadata(nullptr, I.second);
  }

  // Enumerate types used by function bodies and argument lists.
  for (const Function &F : M) {
    for (const Argument &A : F.args())
      EnumerateType(A.getType());

    // Enumerate metadata attached to this function.
    MDs.clear();
    F.getAllMetadata(MDs);
    for (const auto &I : MDs)
      EnumerateMetadata(F.isDeclaration() ? nullptr : &F, I.second);

    for (const BasicBlock &BB : F)
      for (const Instruction &I : BB) {
        // Local metadata is enumerated during function-incorporation, but
        // any ConstantAsMetadata arguments in a DIArgList should be examined
        // now.
        auto EnumerateNonLocalValuesFromMetadata = [&](Metadata *MD) {
          assert(MD && "Metadata unexpectedly null");
          if (const auto *AL = dyn_cast<DIArgList>(MD)) {
            for (const auto *VAM : AL->getArgs()) {
              if (isa<ConstantAsMetadata>(VAM))
                EnumerateMetadata(&F, VAM);
            }
            return;
          }

          if (!isa<LocalAsMetadata>(MD))
            EnumerateMetadata(&F, MD);
        };

        for (DbgRecord &DR : I.getDbgRecordRange()) {
          if (DbgLabelRecord *DLR = dyn_cast<DbgLabelRecord>(&DR)) {
            EnumerateMetadata(&F, DLR->getLabel());
            EnumerateMetadata(&F, &*DLR->getDebugLoc());
            continue;
          }
          // Enumerate non-local location metadata.
          DbgVariableRecord &DVR = cast<DbgVariableRecord>(DR);
          EnumerateNonLocalValuesFromMetadata(DVR.getRawLocation());
          EnumerateMetadata(&F, DVR.getExpression());
          EnumerateMetadata(&F, DVR.getVariable());
          EnumerateMetadata(&F, &*DVR.getDebugLoc());
          if (DVR.isDbgAssign()) {
            EnumerateNonLocalValuesFromMetadata(DVR.getRawAddress());
            EnumerateMetadata(&F, DVR.getAssignID());
            EnumerateMetadata(&F, DVR.getAddressExpression());
          }
        }
        for (const Use &Op : I.operands()) {
          auto *MD = dyn_cast<MetadataAsValue>(&Op);
          if (!MD) {
            EnumerateOperandType(Op);
            continue;
          }

          EnumerateNonLocalValuesFromMetadata(MD->getMetadata());
        }
        if (auto *SVI = dyn_cast<ShuffleVectorInst>(&I))
          EnumerateType(SVI->getShuffleMaskForBitcode()->getType());
        if (auto *GEP = dyn_cast<GetElementPtrInst>(&I))
          EnumerateType(GEP->getSourceElementType());
        if (auto *AI = dyn_cast<AllocaInst>(&I))
          EnumerateType(AI->getAllocatedType());
        EnumerateType(I.getType());
        if (const auto *Call = dyn_cast<CallBase>(&I)) {
          EnumerateAttributes(Call->getAttributes());
          EnumerateType(Call->getFunctionType());
        }

        // Enumerate metadata attached with this instruction.
        MDs.clear();
        I.getAllMetadataOtherThanDebugLoc(MDs);
        for (unsigned i = 0, e = MDs.size(); i != e; ++i)
          EnumerateMetadata(&F, MDs[i].second);

        // Don't enumerate the location directly -- it has a special record
        // type -- but enumerate its operands.
        if (DILocation *L = I.getDebugLoc())
          for (const Metadata *Op : L->operands())
            EnumerateMetadata(&F, Op);
      }
  }

  // Optimize constant ordering.
  OptimizeConstants(FirstConstant, Values.size());

  // Organize metadata ordering.
  organizeMetadata();
}

unsigned ValueEnumerator::getInstructionID(const Instruction *Inst) const {
  InstructionMapType::const_iterator I = InstructionMap.find(Inst);
  assert(I != InstructionMap.end() && "Instruction is not mapped!");
  return I->second;
}

unsigned ValueEnumerator::getComdatID(const Comdat *C) const {
  unsigned ComdatID = Comdats.idFor(C);
  assert(ComdatID && "Comdat not found!");
  return ComdatID;
}

void ValueEnumerator::setInstructionID(const Instruction *I) {
  InstructionMap[I] = InstructionCount++;
}

unsigned ValueEnumerator::getValueID(const Value *V) const {
  if (auto *MD = dyn_cast<MetadataAsValue>(V))
    return getMetadataID(MD->getMetadata());

  ValueMapType::const_iterator I = ValueMap.find(V);
  assert(I != ValueMap.end() && "Value not in slotcalculator!");
  return I->second-1;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void ValueEnumerator::dump() const {
  print(dbgs(), ValueMap, "Default");
  dbgs() << '\n';
  print(dbgs(), MetadataMap, "MetaData");
  dbgs() << '\n';
}
#endif

void ValueEnumerator::print(raw_ostream &OS, const ValueMapType &Map,
                            const char *Name) const {
  OS << "Map Name: " << Name << "\n";
  OS << "Size: " << Map.size() << "\n";
  for (const auto &I : Map) {
    const Value *V = I.first;
    if (V->hasName())
      OS << "Value: " << V->getName();
    else
      OS << "Value: [null]\n";
    V->print(errs());
    errs() << '\n';

    OS << " Uses(" << V->getNumUses() << "):";
    for (const Use &U : V->uses()) {
      if (&U != &*V->use_begin())
        OS << ",";
      if(U->hasName())
        OS << " " << U->getName();
      else
        OS << " [null]";

    }
    OS <<  "\n\n";
  }
}

void ValueEnumerator::print(raw_ostream &OS, const MetadataMapType &Map,
                            const char *Name) const {
  OS << "Map Name: " << Name << "\n";
  OS << "Size: " << Map.size() << "\n";
  for (const auto &I : Map) {
    const Metadata *MD = I.first;
    OS << "Metadata: slot = " << I.second.ID << "\n";
    OS << "Metadata: function = " << I.second.F << "\n";
    MD->print(OS);
    OS << "\n";
  }
}

/// OptimizeConstants - Reorder constant pool for denser encoding.
void ValueEnumerator::OptimizeConstants(unsigned CstStart, unsigned CstEnd) {
  if (CstStart == CstEnd || CstStart+1 == CstEnd) return;

  if (ShouldPreserveUseListOrder)
    // Optimizing constants makes the use-list order difficult to predict.
    // Disable it for now when trying to preserve the order.
    return;

  std::stable_sort(Values.begin() + CstStart, Values.begin() + CstEnd,
                   [this](const std::pair<const Value *, unsigned> &LHS,
                          const std::pair<const Value *, unsigned> &RHS) {
    // Sort by plane.
    if (LHS.first->getType() != RHS.first->getType())
      return getTypeID(LHS.first->getType()) < getTypeID(RHS.first->getType());
    // Then by frequency.
    return LHS.second > RHS.second;
  });

  // Ensure that integer and vector of integer constants are at the start of the
  // constant pool.  This is important so that GEP structure indices come before
  // gep constant exprs.
  std::stable_partition(Values.begin() + CstStart, Values.begin() + CstEnd,
                        isIntOrIntVectorValue);

  // Rebuild the modified portion of ValueMap.
  for (; CstStart != CstEnd; ++CstStart)
    ValueMap[Values[CstStart].first] = CstStart+1;
}

/// EnumerateValueSymbolTable - Insert all of the values in the specified symbol
/// table into the values table.
void ValueEnumerator::EnumerateValueSymbolTable(const ValueSymbolTable &VST) {
  for (ValueSymbolTable::const_iterator VI = VST.begin(), VE = VST.end();
       VI != VE; ++VI)
    EnumerateValue(VI->getValue());
}

/// Insert all of the values referenced by named metadata in the specified
/// module.
void ValueEnumerator::EnumerateNamedMetadata(const Module &M) {
  for (const auto &I : M.named_metadata())
    EnumerateNamedMDNode(&I);
}

void ValueEnumerator::EnumerateNamedMDNode(const NamedMDNode *MD) {
  for (const MDNode *N : MD->operands())
    EnumerateMetadata(nullptr, N);
}

unsigned ValueEnumerator::getMetadataFunctionID(const Function *F) const {
  return F ? getValueID(F) + 1 : 0;
}

void ValueEnumerator::EnumerateMetadata(const Function *F, const Metadata *MD) {
  EnumerateMetadata(getMetadataFunctionID(F), MD);
}

void ValueEnumerator::EnumerateFunctionLocalMetadata(
    const Function &F, const LocalAsMetadata *Local) {
  EnumerateFunctionLocalMetadata(getMetadataFunctionID(&F), Local);
}

void ValueEnumerator::EnumerateFunctionLocalListMetadata(
    const Function &F, const DIArgList *ArgList) {
  EnumerateFunctionLocalListMetadata(getMetadataFunctionID(&F), ArgList);
}

void ValueEnumerator::dropFunctionFromMetadata(
    MetadataMapType::value_type &FirstMD) {
  SmallVector<const MDNode *, 64> Worklist;
  auto push = [&Worklist](MetadataMapType::value_type &MD) {
    auto &Entry = MD.second;

    // Nothing to do if this metadata isn't tagged.
    if (!Entry.F)
      return;

    // Drop the function tag.
    Entry.F = 0;

    // If this is has an ID and is an MDNode, then its operands have entries as
    // well.  We need to drop the function from them too.
    if (Entry.ID)
      if (auto *N = dyn_cast<MDNode>(MD.first))
        Worklist.push_back(N);
  };
  push(FirstMD);
  while (!Worklist.empty())
    for (const Metadata *Op : Worklist.pop_back_val()->operands()) {
      if (!Op)
        continue;
      auto MD = MetadataMap.find(Op);
      if (MD != MetadataMap.end())
        push(*MD);
    }
}

void ValueEnumerator::EnumerateMetadata(unsigned F, const Metadata *MD) {
  // It's vital for reader efficiency that uniqued subgraphs are done in
  // post-order; it's expensive when their operands have forward references.
  // If a distinct node is referenced from a uniqued node, it'll be delayed
  // until the uniqued subgraph has been completely traversed.
  SmallVector<const MDNode *, 32> DelayedDistinctNodes;

  // Start by enumerating MD, and then work through its transitive operands in
  // post-order.  This requires a depth-first search.
  SmallVector<std::pair<const MDNode *, MDNode::op_iterator>, 32> Worklist;
  if (const MDNode *N = enumerateMetadataImpl(F, MD))
    Worklist.push_back(std::make_pair(N, N->op_begin()));

  while (!Worklist.empty()) {
    const MDNode *N = Worklist.back().first;

    // Enumerate operands until we hit a new node.  We need to traverse these
    // nodes' operands before visiting the rest of N's operands.
    MDNode::op_iterator I = std::find_if(
        Worklist.back().second, N->op_end(),
        [&](const Metadata *MD) { return enumerateMetadataImpl(F, MD); });
    if (I != N->op_end()) {
      auto *Op = cast<MDNode>(*I);
      Worklist.back().second = ++I;

      // Delay traversing Op if it's a distinct node and N is uniqued.
      if (Op->isDistinct() && !N->isDistinct())
        DelayedDistinctNodes.push_back(Op);
      else
        Worklist.push_back(std::make_pair(Op, Op->op_begin()));
      continue;
    }

    // All the operands have been visited.  Now assign an ID.
    Worklist.pop_back();
    MDs.push_back(N);
    MetadataMap[N].ID = MDs.size();

    // Flush out any delayed distinct nodes; these are all the distinct nodes
    // that are leaves in last uniqued subgraph.
    if (Worklist.empty() || Worklist.back().first->isDistinct()) {
      for (const MDNode *N : DelayedDistinctNodes)
        Worklist.push_back(std::make_pair(N, N->op_begin()));
      DelayedDistinctNodes.clear();
    }
  }
}

const MDNode *ValueEnumerator::enumerateMetadataImpl(unsigned F, const Metadata *MD) {
  if (!MD)
    return nullptr;

  assert(
      (isa<MDNode>(MD) || isa<MDString>(MD) || isa<ConstantAsMetadata>(MD)) &&
      "Invalid metadata kind");

  auto Insertion = MetadataMap.insert(std::make_pair(MD, MDIndex(F)));
  MDIndex &Entry = Insertion.first->second;
  if (!Insertion.second) {
    // Already mapped.  If F doesn't match the function tag, drop it.
    if (Entry.hasDifferentFunction(F))
      dropFunctionFromMetadata(*Insertion.first);
    return nullptr;
  }

  // Don't assign IDs to metadata nodes.
  if (auto *N = dyn_cast<MDNode>(MD))
    return N;

  // Save the metadata.
  MDs.push_back(MD);
  Entry.ID = MDs.size();

  // Enumerate the constant, if any.
  if (auto *C = dyn_cast<ConstantAsMetadata>(MD))
    EnumerateValue(C->getValue());

  return nullptr;
}

/// EnumerateFunctionLocalMetadata - Incorporate function-local metadata
/// information reachable from the metadata.
void ValueEnumerator::EnumerateFunctionLocalMetadata(
    unsigned F, const LocalAsMetadata *Local) {
  assert(F && "Expected a function");

  // Check to see if it's already in!
  MDIndex &Index = MetadataMap[Local];
  if (Index.ID) {
    assert(Index.F == F && "Expected the same function");
    return;
  }

  MDs.push_back(Local);
  Index.F = F;
  Index.ID = MDs.size();

  EnumerateValue(Local->getValue());
}

/// EnumerateFunctionLocalListMetadata - Incorporate function-local metadata
/// information reachable from the metadata.
void ValueEnumerator::EnumerateFunctionLocalListMetadata(
    unsigned F, const DIArgList *ArgList) {
  assert(F && "Expected a function");

  // Check to see if it's already in!
  MDIndex &Index = MetadataMap[ArgList];
  if (Index.ID) {
    assert(Index.F == F && "Expected the same function");
    return;
  }

  for (ValueAsMetadata *VAM : ArgList->getArgs()) {
    if (isa<LocalAsMetadata>(VAM)) {
      assert(MetadataMap.count(VAM) &&
             "LocalAsMetadata should be enumerated before DIArgList");
      assert(MetadataMap[VAM].F == F &&
             "Expected LocalAsMetadata in the same function");
    } else {
      assert(isa<ConstantAsMetadata>(VAM) &&
             "Expected LocalAsMetadata or ConstantAsMetadata");
      assert(ValueMap.count(VAM->getValue()) &&
             "Constant should be enumerated beforeDIArgList");
      EnumerateMetadata(F, VAM);
    }
  }

  MDs.push_back(ArgList);
  Index.F = F;
  Index.ID = MDs.size();
}

static unsigned getMetadataTypeOrder(const Metadata *MD) {
  // Strings are emitted in bulk and must come first.
  if (isa<MDString>(MD))
    return 0;

  // ConstantAsMetadata doesn't reference anything.  We may as well shuffle it
  // to the front since we can detect it.
  auto *N = dyn_cast<MDNode>(MD);
  if (!N)
    return 1;

  // The reader is fast forward references for distinct node operands, but slow
  // when uniqued operands are unresolved.
  return N->isDistinct() ? 2 : 3;
}

void ValueEnumerator::organizeMetadata() {
  assert(MetadataMap.size() == MDs.size() &&
         "Metadata map and vector out of sync");

  if (MDs.empty())
    return;

  // Copy out the index information from MetadataMap in order to choose a new
  // order.
  SmallVector<MDIndex, 64> Order;
  Order.reserve(MetadataMap.size());
  for (const Metadata *MD : MDs)
    Order.push_back(MetadataMap.lookup(MD));

  // Partition:
  //   - by function, then
  //   - by isa<MDString>
  // and then sort by the original/current ID.  Since the IDs are guaranteed to
  // be unique, the result of llvm::sort will be deterministic.  There's no need
  // for std::stable_sort.
  llvm::sort(Order, [this](MDIndex LHS, MDIndex RHS) {
    return std::make_tuple(LHS.F, getMetadataTypeOrder(LHS.get(MDs)), LHS.ID) <
           std::make_tuple(RHS.F, getMetadataTypeOrder(RHS.get(MDs)), RHS.ID);
  });

  // Rebuild MDs, index the metadata ranges for each function in FunctionMDs,
  // and fix up MetadataMap.
  std::vector<const Metadata *> OldMDs;
  MDs.swap(OldMDs);
  MDs.reserve(OldMDs.size());
  for (unsigned I = 0, E = Order.size(); I != E && !Order[I].F; ++I) {
    auto *MD = Order[I].get(OldMDs);
    MDs.push_back(MD);
    MetadataMap[MD].ID = I + 1;
    if (isa<MDString>(MD))
      ++NumMDStrings;
  }

  // Return early if there's nothing for the functions.
  if (MDs.size() == Order.size())
    return;

  // Build the function metadata ranges.
  MDRange R;
  FunctionMDs.reserve(OldMDs.size());
  unsigned PrevF = 0;
  for (unsigned I = MDs.size(), E = Order.size(), ID = MDs.size(); I != E;
       ++I) {
    unsigned F = Order[I].F;
    if (!PrevF) {
      PrevF = F;
    } else if (PrevF != F) {
      R.Last = FunctionMDs.size();
      std::swap(R, FunctionMDInfo[PrevF]);
      R.First = FunctionMDs.size();

      ID = MDs.size();
      PrevF = F;
    }

    auto *MD = Order[I].get(OldMDs);
    FunctionMDs.push_back(MD);
    MetadataMap[MD].ID = ++ID;
    if (isa<MDString>(MD))
      ++R.NumStrings;
  }
  R.Last = FunctionMDs.size();
  FunctionMDInfo[PrevF] = R;
}

void ValueEnumerator::incorporateFunctionMetadata(const Function &F) {
  NumModuleMDs = MDs.size();

  auto R = FunctionMDInfo.lookup(getValueID(&F) + 1);
  NumMDStrings = R.NumStrings;
  MDs.insert(MDs.end(), FunctionMDs.begin() + R.First,
             FunctionMDs.begin() + R.Last);
}

void ValueEnumerator::EnumerateValue(const Value *V) {
  assert(!V->getType()->isVoidTy() && "Can't insert void values!");
  assert(!isa<MetadataAsValue>(V) && "EnumerateValue doesn't handle Metadata!");

  // Check to see if it's already in!
  unsigned &ValueID = ValueMap[V];
  if (ValueID) {
    // Increment use count.
    Values[ValueID-1].second++;
    return;
  }

  if (auto *GO = dyn_cast<GlobalObject>(V))
    if (const Comdat *C = GO->getComdat())
      Comdats.insert(C);

  // Enumerate the type of this value.
  EnumerateType(V->getType());

  if (const Constant *C = dyn_cast<Constant>(V)) {
    if (isa<GlobalValue>(C)) {
      // Initializers for globals are handled explicitly elsewhere.
    } else if (C->getNumOperands()) {
      // If a constant has operands, enumerate them.  This makes sure that if a
      // constant has uses (for example an array of const ints), that they are
      // inserted also.

      // We prefer to enumerate them with values before we enumerate the user
      // itself.  This makes it more likely that we can avoid forward references
      // in the reader.  We know that there can be no cycles in the constants
      // graph that don't go through a global variable.
      for (const Use &U : C->operands())
        if (!isa<BasicBlock>(U)) // Don't enumerate BB operand to BlockAddress.
          EnumerateValue(U);
      if (auto *CE = dyn_cast<ConstantExpr>(C)) {
        if (CE->getOpcode() == Instruction::ShuffleVector)
          EnumerateValue(CE->getShuffleMaskForBitcode());
        if (auto *GEP = dyn_cast<GEPOperator>(CE))
          EnumerateType(GEP->getSourceElementType());
      }

      // Finally, add the value.  Doing this could make the ValueID reference be
      // dangling, don't reuse it.
      Values.push_back(std::make_pair(V, 1U));
      ValueMap[V] = Values.size();
      return;
    }
  }

  // Add the value.
  Values.push_back(std::make_pair(V, 1U));
  ValueID = Values.size();
}


void ValueEnumerator::EnumerateType(Type *Ty) {
  unsigned *TypeID = &TypeMap[Ty];

  // We've already seen this type.
  if (*TypeID)
    return;

  // If it is a non-anonymous struct, mark the type as being visited so that we
  // don't recursively visit it.  This is safe because we allow forward
  // references of these in the bitcode reader.
  if (StructType *STy = dyn_cast<StructType>(Ty))
    if (!STy->isLiteral())
      *TypeID = ~0U;

  // Enumerate all of the subtypes before we enumerate this type.  This ensures
  // that the type will be enumerated in an order that can be directly built.
  for (Type *SubTy : Ty->subtypes())
    EnumerateType(SubTy);

  // Refresh the TypeID pointer in case the table rehashed.
  TypeID = &TypeMap[Ty];

  // Check to see if we got the pointer another way.  This can happen when
  // enumerating recursive types that hit the base case deeper than they start.
  //
  // If this is actually a struct that we are treating as forward ref'able,
  // then emit the definition now that all of its contents are available.
  if (*TypeID && *TypeID != ~0U)
    return;

  // Add this type now that its contents are all happily enumerated.
  Types.push_back(Ty);

  *TypeID = Types.size();
}

// Enumerate the types for the specified value.  If the value is a constant,
// walk through it, enumerating the types of the constant.
void ValueEnumerator::EnumerateOperandType(const Value *V) {
  EnumerateType(V->getType());

  assert(!isa<MetadataAsValue>(V) && "Unexpected metadata operand");

  const Constant *C = dyn_cast<Constant>(V);
  if (!C)
    return;

  // If this constant is already enumerated, ignore it, we know its type must
  // be enumerated.
  if (ValueMap.count(C))
    return;

  // This constant may have operands, make sure to enumerate the types in
  // them.
  for (const Value *Op : C->operands()) {
    // Don't enumerate basic blocks here, this happens as operands to
    // blockaddress.
    if (isa<BasicBlock>(Op))
      continue;

    EnumerateOperandType(Op);
  }
  if (auto *CE = dyn_cast<ConstantExpr>(C)) {
    if (CE->getOpcode() == Instruction::ShuffleVector)
      EnumerateOperandType(CE->getShuffleMaskForBitcode());
    if (CE->getOpcode() == Instruction::GetElementPtr)
      EnumerateType(cast<GEPOperator>(CE)->getSourceElementType());
  }
}

void ValueEnumerator::EnumerateAttributes(AttributeList PAL) {
  if (PAL.isEmpty()) return;  // null is always 0.

  // Do a lookup.
  unsigned &Entry = AttributeListMap[PAL];
  if (Entry == 0) {
    // Never saw this before, add it.
    AttributeLists.push_back(PAL);
    Entry = AttributeLists.size();
  }

  // Do lookups for all attribute groups.
  for (unsigned i : PAL.indexes()) {
    AttributeSet AS = PAL.getAttributes(i);
    if (!AS.hasAttributes())
      continue;
    IndexAndAttrSet Pair = {i, AS};
    unsigned &Entry = AttributeGroupMap[Pair];
    if (Entry == 0) {
      AttributeGroups.push_back(Pair);
      Entry = AttributeGroups.size();

      for (Attribute Attr : AS) {
        if (Attr.isTypeAttribute())
          EnumerateType(Attr.getValueAsType());
      }
    }
  }
}

void ValueEnumerator::incorporateFunction(const Function &F) {
  InstructionCount = 0;
  NumModuleValues = Values.size();

  // Add global metadata to the function block.  This doesn't include
  // LocalAsMetadata.
  incorporateFunctionMetadata(F);

  // Adding function arguments to the value table.
  for (const auto &I : F.args()) {
    EnumerateValue(&I);
    if (I.hasAttribute(Attribute::ByVal))
      EnumerateType(I.getParamByValType());
    else if (I.hasAttribute(Attribute::StructRet))
      EnumerateType(I.getParamStructRetType());
    else if (I.hasAttribute(Attribute::ByRef))
      EnumerateType(I.getParamByRefType());
  }
  FirstFuncConstantID = Values.size();

  // Add all function-level constants to the value table.
  for (const BasicBlock &BB : F) {
    for (const Instruction &I : BB) {
      for (const Use &OI : I.operands()) {
        if ((isa<Constant>(OI) && !isa<GlobalValue>(OI)) || isa<InlineAsm>(OI))
          EnumerateValue(OI);
      }
      if (auto *SVI = dyn_cast<ShuffleVectorInst>(&I))
        EnumerateValue(SVI->getShuffleMaskForBitcode());
    }
    BasicBlocks.push_back(&BB);
    ValueMap[&BB] = BasicBlocks.size();
  }

  // Optimize the constant layout.
  OptimizeConstants(FirstFuncConstantID, Values.size());

  // Add the function's parameter attributes so they are available for use in
  // the function's instruction.
  EnumerateAttributes(F.getAttributes());

  FirstInstID = Values.size();

  SmallVector<LocalAsMetadata *, 8> FnLocalMDVector;
  SmallVector<DIArgList *, 8> ArgListMDVector;

  auto AddFnLocalMetadata = [&](Metadata *MD) {
    if (!MD)
      return;
    if (auto *Local = dyn_cast<LocalAsMetadata>(MD)) {
      // Enumerate metadata after the instructions they might refer to.
      FnLocalMDVector.push_back(Local);
    } else if (auto *ArgList = dyn_cast<DIArgList>(MD)) {
      ArgListMDVector.push_back(ArgList);
      for (ValueAsMetadata *VMD : ArgList->getArgs()) {
        if (auto *Local = dyn_cast<LocalAsMetadata>(VMD)) {
          // Enumerate metadata after the instructions they might refer
          // to.
          FnLocalMDVector.push_back(Local);
        }
      }
    }
  };

  // Add all of the instructions.
  for (const BasicBlock &BB : F) {
    for (const Instruction &I : BB) {
      for (const Use &OI : I.operands()) {
        if (auto *MD = dyn_cast<MetadataAsValue>(&OI))
          AddFnLocalMetadata(MD->getMetadata());
      }
      /// RemoveDIs: Add non-instruction function-local metadata uses.
      for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange())) {
        assert(DVR.getRawLocation() &&
               "DbgVariableRecord location unexpectedly null");
        AddFnLocalMetadata(DVR.getRawLocation());
        if (DVR.isDbgAssign()) {
          assert(DVR.getRawAddress() &&
                 "DbgVariableRecord location unexpectedly null");
          AddFnLocalMetadata(DVR.getRawAddress());
        }
      }
      if (!I.getType()->isVoidTy())
        EnumerateValue(&I);
    }
  }

  // Add all of the function-local metadata.
  for (const LocalAsMetadata *Local : FnLocalMDVector) {
    // At this point, every local values have been incorporated, we shouldn't
    // have a metadata operand that references a value that hasn't been seen.
    assert(ValueMap.count(Local->getValue()) &&
           "Missing value for metadata operand");
    EnumerateFunctionLocalMetadata(F, Local);
  }
  // DIArgList entries must come after function-local metadata, as it is not
  // possible to forward-reference them.
  for (const DIArgList *ArgList : ArgListMDVector)
    EnumerateFunctionLocalListMetadata(F, ArgList);
}

void ValueEnumerator::purgeFunction() {
  /// Remove purged values from the ValueMap.
  for (const auto &V : llvm::drop_begin(Values, NumModuleValues))
    ValueMap.erase(V.first);
  for (const Metadata *MD : llvm::drop_begin(MDs, NumModuleMDs))
    MetadataMap.erase(MD);
  for (const BasicBlock *BB : BasicBlocks)
    ValueMap.erase(BB);

  Values.resize(NumModuleValues);
  MDs.resize(NumModuleMDs);
  BasicBlocks.clear();
  NumMDStrings = 0;
}

static void IncorporateFunctionInfoGlobalBBIDs(const Function *F,
                                 DenseMap<const BasicBlock*, unsigned> &IDMap) {
  unsigned Counter = 0;
  for (const BasicBlock &BB : *F)
    IDMap[&BB] = ++Counter;
}

/// getGlobalBasicBlockID - This returns the function-specific ID for the
/// specified basic block.  This is relatively expensive information, so it
/// should only be used by rare constructs such as address-of-label.
unsigned ValueEnumerator::getGlobalBasicBlockID(const BasicBlock *BB) const {
  unsigned &Idx = GlobalBasicBlockIDs[BB];
  if (Idx != 0)
    return Idx-1;

  IncorporateFunctionInfoGlobalBBIDs(BB->getParent(), GlobalBasicBlockIDs);
  return getGlobalBasicBlockID(BB);
}

uint64_t ValueEnumerator::computeBitsRequiredForTypeIndices() const {
  return Log2_32_Ceil(getTypes().size() + 1);
}

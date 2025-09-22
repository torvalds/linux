//===-- SPIRVRegularizer.cpp - regularize IR for SPIR-V ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements regularization of LLVM IR for SPIR-V. The prototype of
// the pass was taken from SPIRV-LLVM translator.
//
//===----------------------------------------------------------------------===//

#include "SPIRV.h"
#include "SPIRVTargetMachine.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <list>

#define DEBUG_TYPE "spirv-regularizer"

using namespace llvm;

namespace llvm {
void initializeSPIRVRegularizerPass(PassRegistry &);
}

namespace {
struct SPIRVRegularizer : public FunctionPass, InstVisitor<SPIRVRegularizer> {
  DenseMap<Function *, Function *> Old2NewFuncs;

public:
  static char ID;
  SPIRVRegularizer() : FunctionPass(ID) {
    initializeSPIRVRegularizerPass(*PassRegistry::getPassRegistry());
  }
  bool runOnFunction(Function &F) override;
  StringRef getPassName() const override { return "SPIR-V Regularizer"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    FunctionPass::getAnalysisUsage(AU);
  }
  void visitCallInst(CallInst &CI);

private:
  void visitCallScalToVec(CallInst *CI, StringRef MangledName,
                          StringRef DemangledName);
  void runLowerConstExpr(Function &F);
};
} // namespace

char SPIRVRegularizer::ID = 0;

INITIALIZE_PASS(SPIRVRegularizer, DEBUG_TYPE, "SPIR-V Regularizer", false,
                false)

// Since SPIR-V cannot represent constant expression, constant expressions
// in LLVM IR need to be lowered to instructions. For each function,
// the constant expressions used by instructions of the function are replaced
// by instructions placed in the entry block since it dominates all other BBs.
// Each constant expression only needs to be lowered once in each function
// and all uses of it by instructions in that function are replaced by
// one instruction.
// TODO: remove redundant instructions for common subexpression.
void SPIRVRegularizer::runLowerConstExpr(Function &F) {
  LLVMContext &Ctx = F.getContext();
  std::list<Instruction *> WorkList;
  for (auto &II : instructions(F))
    WorkList.push_back(&II);

  auto FBegin = F.begin();
  while (!WorkList.empty()) {
    Instruction *II = WorkList.front();

    auto LowerOp = [&II, &FBegin, &F](Value *V) -> Value * {
      if (isa<Function>(V))
        return V;
      auto *CE = cast<ConstantExpr>(V);
      LLVM_DEBUG(dbgs() << "[lowerConstantExpressions] " << *CE);
      auto ReplInst = CE->getAsInstruction();
      auto InsPoint = II->getParent() == &*FBegin ? II : &FBegin->back();
      ReplInst->insertBefore(InsPoint);
      LLVM_DEBUG(dbgs() << " -> " << *ReplInst << '\n');
      std::vector<Instruction *> Users;
      // Do not replace use during iteration of use. Do it in another loop.
      for (auto U : CE->users()) {
        LLVM_DEBUG(dbgs() << "[lowerConstantExpressions] Use: " << *U << '\n');
        auto InstUser = dyn_cast<Instruction>(U);
        // Only replace users in scope of current function.
        if (InstUser && InstUser->getParent()->getParent() == &F)
          Users.push_back(InstUser);
      }
      for (auto &User : Users) {
        if (ReplInst->getParent() == User->getParent() &&
            User->comesBefore(ReplInst))
          ReplInst->moveBefore(User);
        User->replaceUsesOfWith(CE, ReplInst);
      }
      return ReplInst;
    };

    WorkList.pop_front();
    auto LowerConstantVec = [&II, &LowerOp, &WorkList,
                             &Ctx](ConstantVector *Vec,
                                   unsigned NumOfOp) -> Value * {
      if (std::all_of(Vec->op_begin(), Vec->op_end(), [](Value *V) {
            return isa<ConstantExpr>(V) || isa<Function>(V);
          })) {
        // Expand a vector of constexprs and construct it back with
        // series of insertelement instructions.
        std::list<Value *> OpList;
        std::transform(Vec->op_begin(), Vec->op_end(),
                       std::back_inserter(OpList),
                       [LowerOp](Value *V) { return LowerOp(V); });
        Value *Repl = nullptr;
        unsigned Idx = 0;
        auto *PhiII = dyn_cast<PHINode>(II);
        Instruction *InsPoint =
            PhiII ? &PhiII->getIncomingBlock(NumOfOp)->back() : II;
        std::list<Instruction *> ReplList;
        for (auto V : OpList) {
          if (auto *Inst = dyn_cast<Instruction>(V))
            ReplList.push_back(Inst);
          Repl = InsertElementInst::Create(
              (Repl ? Repl : PoisonValue::get(Vec->getType())), V,
              ConstantInt::get(Type::getInt32Ty(Ctx), Idx++), "", InsPoint);
        }
        WorkList.splice(WorkList.begin(), ReplList);
        return Repl;
      }
      return nullptr;
    };
    for (unsigned OI = 0, OE = II->getNumOperands(); OI != OE; ++OI) {
      auto *Op = II->getOperand(OI);
      if (auto *Vec = dyn_cast<ConstantVector>(Op)) {
        Value *ReplInst = LowerConstantVec(Vec, OI);
        if (ReplInst)
          II->replaceUsesOfWith(Op, ReplInst);
      } else if (auto CE = dyn_cast<ConstantExpr>(Op)) {
        WorkList.push_front(cast<Instruction>(LowerOp(CE)));
      } else if (auto MDAsVal = dyn_cast<MetadataAsValue>(Op)) {
        auto ConstMD = dyn_cast<ConstantAsMetadata>(MDAsVal->getMetadata());
        if (!ConstMD)
          continue;
        Constant *C = ConstMD->getValue();
        Value *ReplInst = nullptr;
        if (auto *Vec = dyn_cast<ConstantVector>(C))
          ReplInst = LowerConstantVec(Vec, OI);
        if (auto *CE = dyn_cast<ConstantExpr>(C))
          ReplInst = LowerOp(CE);
        if (!ReplInst)
          continue;
        Metadata *RepMD = ValueAsMetadata::get(ReplInst);
        Value *RepMDVal = MetadataAsValue::get(Ctx, RepMD);
        II->setOperand(OI, RepMDVal);
        WorkList.push_front(cast<Instruction>(ReplInst));
      }
    }
  }
}

// It fixes calls to OCL builtins that accept vector arguments and one of them
// is actually a scalar splat.
void SPIRVRegularizer::visitCallInst(CallInst &CI) {
  auto F = CI.getCalledFunction();
  if (!F)
    return;

  auto MangledName = F->getName();
  char *NameStr = itaniumDemangle(F->getName().data());
  if (!NameStr)
    return;
  StringRef DemangledName(NameStr);

  // TODO: add support for other builtins.
  if (DemangledName.starts_with("fmin") || DemangledName.starts_with("fmax") ||
      DemangledName.starts_with("min") || DemangledName.starts_with("max"))
    visitCallScalToVec(&CI, MangledName, DemangledName);
  free(NameStr);
}

void SPIRVRegularizer::visitCallScalToVec(CallInst *CI, StringRef MangledName,
                                          StringRef DemangledName) {
  // Check if all arguments have the same type - it's simple case.
  auto Uniform = true;
  Type *Arg0Ty = CI->getOperand(0)->getType();
  auto IsArg0Vector = isa<VectorType>(Arg0Ty);
  for (unsigned I = 1, E = CI->arg_size(); Uniform && (I != E); ++I)
    Uniform = isa<VectorType>(CI->getOperand(I)->getType()) == IsArg0Vector;
  if (Uniform)
    return;

  auto *OldF = CI->getCalledFunction();
  Function *NewF = nullptr;
  if (!Old2NewFuncs.count(OldF)) {
    AttributeList Attrs = CI->getCalledFunction()->getAttributes();
    SmallVector<Type *, 2> ArgTypes = {OldF->getArg(0)->getType(), Arg0Ty};
    auto *NewFTy =
        FunctionType::get(OldF->getReturnType(), ArgTypes, OldF->isVarArg());
    NewF = Function::Create(NewFTy, OldF->getLinkage(), OldF->getName(),
                            *OldF->getParent());
    ValueToValueMapTy VMap;
    auto NewFArgIt = NewF->arg_begin();
    for (auto &Arg : OldF->args()) {
      auto ArgName = Arg.getName();
      NewFArgIt->setName(ArgName);
      VMap[&Arg] = &(*NewFArgIt++);
    }
    SmallVector<ReturnInst *, 8> Returns;
    CloneFunctionInto(NewF, OldF, VMap,
                      CloneFunctionChangeType::LocalChangesOnly, Returns);
    NewF->setAttributes(Attrs);
    Old2NewFuncs[OldF] = NewF;
  } else {
    NewF = Old2NewFuncs[OldF];
  }
  assert(NewF);

  // This produces an instruction sequence that implements a splat of
  // CI->getOperand(1) to a vector Arg0Ty. However, we use InsertElementInst
  // and ShuffleVectorInst to generate the same code as the SPIR-V translator.
  // For instance (transcoding/OpMin.ll), this call
  //   call spir_func <2 x i32> @_Z3minDv2_ii(<2 x i32> <i32 1, i32 10>, i32 5)
  // is translated to
  //    %8 = OpUndef %v2uint
  //   %14 = OpConstantComposite %v2uint %uint_1 %uint_10
  //   ...
  //   %10 = OpCompositeInsert %v2uint %uint_5 %8 0
  //   %11 = OpVectorShuffle %v2uint %10 %8 0 0
  // %call = OpExtInst %v2uint %1 s_min %14 %11
  auto ConstInt = ConstantInt::get(IntegerType::get(CI->getContext(), 32), 0);
  PoisonValue *PVal = PoisonValue::get(Arg0Ty);
  Instruction *Inst =
      InsertElementInst::Create(PVal, CI->getOperand(1), ConstInt, "", CI);
  ElementCount VecElemCount = cast<VectorType>(Arg0Ty)->getElementCount();
  Constant *ConstVec = ConstantVector::getSplat(VecElemCount, ConstInt);
  Value *NewVec = new ShuffleVectorInst(Inst, PVal, ConstVec, "", CI);
  CI->setOperand(1, NewVec);
  CI->replaceUsesOfWith(OldF, NewF);
  CI->mutateFunctionType(NewF->getFunctionType());
}

bool SPIRVRegularizer::runOnFunction(Function &F) {
  runLowerConstExpr(F);
  visit(F);
  for (auto &OldNew : Old2NewFuncs) {
    Function *OldF = OldNew.first;
    Function *NewF = OldNew.second;
    NewF->takeName(OldF);
    OldF->eraseFromParent();
  }
  return true;
}

FunctionPass *llvm::createSPIRVRegularizerPass() {
  return new SPIRVRegularizer();
}

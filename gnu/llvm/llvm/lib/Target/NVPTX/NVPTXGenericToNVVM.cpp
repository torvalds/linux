//===-- GenericToNVVM.cpp - Convert generic module to NVVM module - C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Convert generic global variables into either .global or .const access based
// on the variable's "constant" qualifier.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/NVPTXBaseInfo.h"
#include "NVPTX.h"
#include "NVPTXUtilities.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

using namespace llvm;

namespace llvm {
void initializeGenericToNVVMLegacyPassPass(PassRegistry &);
}

namespace {
class GenericToNVVM {
public:
  bool runOnModule(Module &M);

private:
  Value *remapConstant(Module *M, Function *F, Constant *C,
                       IRBuilder<> &Builder);
  Value *remapConstantVectorOrConstantAggregate(Module *M, Function *F,
                                                Constant *C,
                                                IRBuilder<> &Builder);
  Value *remapConstantExpr(Module *M, Function *F, ConstantExpr *C,
                           IRBuilder<> &Builder);

  typedef ValueMap<GlobalVariable *, GlobalVariable *> GVMapTy;
  typedef ValueMap<Constant *, Value *> ConstantToValueMapTy;
  GVMapTy GVMap;
  ConstantToValueMapTy ConstantToValueMap;
};
} // end namespace

bool GenericToNVVM::runOnModule(Module &M) {
  // Create a clone of each global variable that has the default address space.
  // The clone is created with the global address space  specifier, and the pair
  // of original global variable and its clone is placed in the GVMap for later
  // use.

  for (GlobalVariable &GV : llvm::make_early_inc_range(M.globals())) {
    if (GV.getType()->getAddressSpace() == llvm::ADDRESS_SPACE_GENERIC &&
        !llvm::isTexture(GV) && !llvm::isSurface(GV) && !llvm::isSampler(GV) &&
        !GV.getName().starts_with("llvm.")) {
      GlobalVariable *NewGV = new GlobalVariable(
          M, GV.getValueType(), GV.isConstant(), GV.getLinkage(),
          GV.hasInitializer() ? GV.getInitializer() : nullptr, "", &GV,
          GV.getThreadLocalMode(), llvm::ADDRESS_SPACE_GLOBAL);
      NewGV->copyAttributesFrom(&GV);
      NewGV->copyMetadata(&GV, /*Offset=*/0);
      GVMap[&GV] = NewGV;
    }
  }

  // Return immediately, if every global variable has a specific address space
  // specifier.
  if (GVMap.empty()) {
    return false;
  }

  // Walk through the instructions in function defitinions, and replace any use
  // of original global variables in GVMap with a use of the corresponding
  // copies in GVMap.  If necessary, promote constants to instructions.
  for (Function &F : M) {
    if (F.isDeclaration()) {
      continue;
    }
    IRBuilder<> Builder(F.getEntryBlock().getFirstNonPHIOrDbg());
    for (BasicBlock &BB : F) {
      for (Instruction &II : BB) {
        for (unsigned i = 0, e = II.getNumOperands(); i < e; ++i) {
          Value *Operand = II.getOperand(i);
          if (isa<Constant>(Operand)) {
            II.setOperand(
                i, remapConstant(&M, &F, cast<Constant>(Operand), Builder));
          }
        }
      }
    }
    ConstantToValueMap.clear();
  }

  // Copy GVMap over to a standard value map.
  ValueToValueMapTy VM;
  for (auto I = GVMap.begin(), E = GVMap.end(); I != E; ++I)
    VM[I->first] = I->second;

  // Walk through the global variable  initializers, and replace any use of
  // original global variables in GVMap with a use of the corresponding copies
  // in GVMap.  The copies need to be bitcast to the original global variable
  // types, as we cannot use cvta in global variable initializers.
  for (GVMapTy::iterator I = GVMap.begin(), E = GVMap.end(); I != E;) {
    GlobalVariable *GV = I->first;
    GlobalVariable *NewGV = I->second;

    // Remove GV from the map so that it can be RAUWed.  Note that
    // DenseMap::erase() won't invalidate any iterators but this one.
    auto Next = std::next(I);
    GVMap.erase(I);
    I = Next;

    Constant *BitCastNewGV = ConstantExpr::getPointerCast(NewGV, GV->getType());
    // At this point, the remaining uses of GV should be found only in global
    // variable initializers, as other uses have been already been removed
    // while walking through the instructions in function definitions.
    GV->replaceAllUsesWith(BitCastNewGV);
    std::string Name = std::string(GV->getName());
    GV->eraseFromParent();
    NewGV->setName(Name);
  }
  assert(GVMap.empty() && "Expected it to be empty by now");

  return true;
}

Value *GenericToNVVM::remapConstant(Module *M, Function *F, Constant *C,
                                    IRBuilder<> &Builder) {
  // If the constant C has been converted already in the given function  F, just
  // return the converted value.
  ConstantToValueMapTy::iterator CTII = ConstantToValueMap.find(C);
  if (CTII != ConstantToValueMap.end()) {
    return CTII->second;
  }

  Value *NewValue = C;
  if (isa<GlobalVariable>(C)) {
    // If the constant C is a global variable and is found in GVMap, substitute
    //
    //   addrspacecast GVMap[C] to addrspace(0)
    //
    // for our use of C.
    GVMapTy::iterator I = GVMap.find(cast<GlobalVariable>(C));
    if (I != GVMap.end()) {
      GlobalVariable *GV = I->second;
      NewValue = Builder.CreateAddrSpaceCast(
          GV,
          PointerType::get(GV->getValueType(), llvm::ADDRESS_SPACE_GENERIC));
    }
  } else if (isa<ConstantAggregate>(C)) {
    // If any element in the constant vector or aggregate C is or uses a global
    // variable in GVMap, the constant C needs to be reconstructed, using a set
    // of instructions.
    NewValue = remapConstantVectorOrConstantAggregate(M, F, C, Builder);
  } else if (isa<ConstantExpr>(C)) {
    // If any operand in the constant expression C is or uses a global variable
    // in GVMap, the constant expression C needs to be reconstructed, using a
    // set of instructions.
    NewValue = remapConstantExpr(M, F, cast<ConstantExpr>(C), Builder);
  }

  ConstantToValueMap[C] = NewValue;
  return NewValue;
}

Value *GenericToNVVM::remapConstantVectorOrConstantAggregate(
    Module *M, Function *F, Constant *C, IRBuilder<> &Builder) {
  bool OperandChanged = false;
  SmallVector<Value *, 4> NewOperands;
  unsigned NumOperands = C->getNumOperands();

  // Check if any element is or uses a global variable in  GVMap, and thus
  // converted to another value.
  for (unsigned i = 0; i < NumOperands; ++i) {
    Value *Operand = C->getOperand(i);
    Value *NewOperand = remapConstant(M, F, cast<Constant>(Operand), Builder);
    OperandChanged |= Operand != NewOperand;
    NewOperands.push_back(NewOperand);
  }

  // If none of the elements has been modified, return C as it is.
  if (!OperandChanged) {
    return C;
  }

  // If any of the elements has been  modified, construct the equivalent
  // vector or aggregate value with a set instructions and the converted
  // elements.
  Value *NewValue = PoisonValue::get(C->getType());
  if (isa<ConstantVector>(C)) {
    for (unsigned i = 0; i < NumOperands; ++i) {
      Value *Idx = ConstantInt::get(Type::getInt32Ty(M->getContext()), i);
      NewValue = Builder.CreateInsertElement(NewValue, NewOperands[i], Idx);
    }
  } else {
    for (unsigned i = 0; i < NumOperands; ++i) {
      NewValue =
          Builder.CreateInsertValue(NewValue, NewOperands[i], ArrayRef(i));
    }
  }

  return NewValue;
}

Value *GenericToNVVM::remapConstantExpr(Module *M, Function *F, ConstantExpr *C,
                                        IRBuilder<> &Builder) {
  bool OperandChanged = false;
  SmallVector<Value *, 4> NewOperands;
  unsigned NumOperands = C->getNumOperands();

  // Check if any operand is or uses a global variable in  GVMap, and thus
  // converted to another value.
  for (unsigned i = 0; i < NumOperands; ++i) {
    Value *Operand = C->getOperand(i);
    Value *NewOperand = remapConstant(M, F, cast<Constant>(Operand), Builder);
    OperandChanged |= Operand != NewOperand;
    NewOperands.push_back(NewOperand);
  }

  // If none of the operands has been modified, return C as it is.
  if (!OperandChanged) {
    return C;
  }

  // If any of the operands has been modified, construct the instruction with
  // the converted operands.
  unsigned Opcode = C->getOpcode();
  switch (Opcode) {
  case Instruction::ExtractElement:
    // ExtractElementConstantExpr
    return Builder.CreateExtractElement(NewOperands[0], NewOperands[1]);
  case Instruction::InsertElement:
    // InsertElementConstantExpr
    return Builder.CreateInsertElement(NewOperands[0], NewOperands[1],
                                       NewOperands[2]);
  case Instruction::ShuffleVector:
    // ShuffleVector
    return Builder.CreateShuffleVector(NewOperands[0], NewOperands[1],
                                       NewOperands[2]);
  case Instruction::GetElementPtr:
    // GetElementPtrConstantExpr
    return Builder.CreateGEP(cast<GEPOperator>(C)->getSourceElementType(),
                             NewOperands[0],
                             ArrayRef(&NewOperands[1], NumOperands - 1), "",
                             cast<GEPOperator>(C)->isInBounds());
  case Instruction::Select:
    // SelectConstantExpr
    return Builder.CreateSelect(NewOperands[0], NewOperands[1], NewOperands[2]);
  default:
    // BinaryConstantExpr
    if (Instruction::isBinaryOp(Opcode)) {
      return Builder.CreateBinOp(Instruction::BinaryOps(C->getOpcode()),
                                 NewOperands[0], NewOperands[1]);
    }
    // UnaryConstantExpr
    if (Instruction::isCast(Opcode)) {
      return Builder.CreateCast(Instruction::CastOps(C->getOpcode()),
                                NewOperands[0], C->getType());
    }
    llvm_unreachable("GenericToNVVM encountered an unsupported ConstantExpr");
  }
}

namespace {
class GenericToNVVMLegacyPass : public ModulePass {
public:
  static char ID;

  GenericToNVVMLegacyPass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;
};
} // namespace

char GenericToNVVMLegacyPass::ID = 0;

ModulePass *llvm::createGenericToNVVMLegacyPass() {
  return new GenericToNVVMLegacyPass();
}

INITIALIZE_PASS(
    GenericToNVVMLegacyPass, "generic-to-nvvm",
    "Ensure that the global variables are in the global address space", false,
    false)

bool GenericToNVVMLegacyPass::runOnModule(Module &M) {
  return GenericToNVVM().runOnModule(M);
}

PreservedAnalyses GenericToNVVMPass::run(Module &M, ModuleAnalysisManager &AM) {
  return GenericToNVVM().runOnModule(M) ? PreservedAnalyses::none()
                                        : PreservedAnalyses::all();
}

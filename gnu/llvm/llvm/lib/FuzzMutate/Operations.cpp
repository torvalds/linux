//===-- Operations.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/FuzzMutate/Operations.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;
using namespace fuzzerop;

void llvm::describeFuzzerIntOps(std::vector<fuzzerop::OpDescriptor> &Ops) {
  Ops.push_back(binOpDescriptor(1, Instruction::Add));
  Ops.push_back(binOpDescriptor(1, Instruction::Sub));
  Ops.push_back(binOpDescriptor(1, Instruction::Mul));
  Ops.push_back(binOpDescriptor(1, Instruction::SDiv));
  Ops.push_back(binOpDescriptor(1, Instruction::UDiv));
  Ops.push_back(binOpDescriptor(1, Instruction::SRem));
  Ops.push_back(binOpDescriptor(1, Instruction::URem));
  Ops.push_back(binOpDescriptor(1, Instruction::Shl));
  Ops.push_back(binOpDescriptor(1, Instruction::LShr));
  Ops.push_back(binOpDescriptor(1, Instruction::AShr));
  Ops.push_back(binOpDescriptor(1, Instruction::And));
  Ops.push_back(binOpDescriptor(1, Instruction::Or));
  Ops.push_back(binOpDescriptor(1, Instruction::Xor));

  Ops.push_back(cmpOpDescriptor(1, Instruction::ICmp, CmpInst::ICMP_EQ));
  Ops.push_back(cmpOpDescriptor(1, Instruction::ICmp, CmpInst::ICMP_NE));
  Ops.push_back(cmpOpDescriptor(1, Instruction::ICmp, CmpInst::ICMP_UGT));
  Ops.push_back(cmpOpDescriptor(1, Instruction::ICmp, CmpInst::ICMP_UGE));
  Ops.push_back(cmpOpDescriptor(1, Instruction::ICmp, CmpInst::ICMP_ULT));
  Ops.push_back(cmpOpDescriptor(1, Instruction::ICmp, CmpInst::ICMP_ULE));
  Ops.push_back(cmpOpDescriptor(1, Instruction::ICmp, CmpInst::ICMP_SGT));
  Ops.push_back(cmpOpDescriptor(1, Instruction::ICmp, CmpInst::ICMP_SGE));
  Ops.push_back(cmpOpDescriptor(1, Instruction::ICmp, CmpInst::ICMP_SLT));
  Ops.push_back(cmpOpDescriptor(1, Instruction::ICmp, CmpInst::ICMP_SLE));
}

void llvm::describeFuzzerFloatOps(std::vector<fuzzerop::OpDescriptor> &Ops) {
  Ops.push_back(binOpDescriptor(1, Instruction::FAdd));
  Ops.push_back(binOpDescriptor(1, Instruction::FSub));
  Ops.push_back(binOpDescriptor(1, Instruction::FMul));
  Ops.push_back(binOpDescriptor(1, Instruction::FDiv));
  Ops.push_back(binOpDescriptor(1, Instruction::FRem));

  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_FALSE));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_OEQ));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_OGT));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_OGE));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_OLT));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_OLE));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_ONE));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_ORD));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_UNO));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_UEQ));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_UGT));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_UGE));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_ULT));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_ULE));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_UNE));
  Ops.push_back(cmpOpDescriptor(1, Instruction::FCmp, CmpInst::FCMP_TRUE));
}

void llvm::describeFuzzerUnaryOperations(
    std::vector<fuzzerop::OpDescriptor> &Ops) {
  Ops.push_back(fnegDescriptor(1));
}

void llvm::describeFuzzerControlFlowOps(
    std::vector<fuzzerop::OpDescriptor> &Ops) {
  Ops.push_back(splitBlockDescriptor(1));
}

void llvm::describeFuzzerOtherOps(std::vector<fuzzerop::OpDescriptor> &Ops) {
  Ops.push_back(selectDescriptor(1));
}

void llvm::describeFuzzerPointerOps(std::vector<fuzzerop::OpDescriptor> &Ops) {
  Ops.push_back(gepDescriptor(1));
}

void llvm::describeFuzzerAggregateOps(
    std::vector<fuzzerop::OpDescriptor> &Ops) {
  Ops.push_back(extractValueDescriptor(1));
  Ops.push_back(insertValueDescriptor(1));
}

void llvm::describeFuzzerVectorOps(std::vector<fuzzerop::OpDescriptor> &Ops) {
  Ops.push_back(extractElementDescriptor(1));
  Ops.push_back(insertElementDescriptor(1));
  Ops.push_back(shuffleVectorDescriptor(1));
}

OpDescriptor llvm::fuzzerop::selectDescriptor(unsigned Weight) {
  auto buildOp = [](ArrayRef<Value *> Srcs, Instruction *Inst) {
    return SelectInst::Create(Srcs[0], Srcs[1], Srcs[2], "S", Inst);
  };
  return {Weight,
          {boolOrVecBoolType(), matchFirstLengthWAnyType(), matchSecondType()},
          buildOp};
}

OpDescriptor llvm::fuzzerop::fnegDescriptor(unsigned Weight) {
  auto buildOp = [](ArrayRef<Value *> Srcs, Instruction *Inst) {
    return UnaryOperator::Create(Instruction::FNeg, Srcs[0], "F", Inst);
  };
  return {Weight, {anyFloatOrVecFloatType()}, buildOp};
}

OpDescriptor llvm::fuzzerop::binOpDescriptor(unsigned Weight,
                                             Instruction::BinaryOps Op) {
  auto buildOp = [Op](ArrayRef<Value *> Srcs, Instruction *Inst) {
    return BinaryOperator::Create(Op, Srcs[0], Srcs[1], "B", Inst);
  };
  switch (Op) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::SDiv:
  case Instruction::UDiv:
  case Instruction::SRem:
  case Instruction::URem:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    return {Weight, {anyIntOrVecIntType(), matchFirstType()}, buildOp};
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::FRem:
    return {Weight, {anyFloatOrVecFloatType(), matchFirstType()}, buildOp};
  case Instruction::BinaryOpsEnd:
    llvm_unreachable("Value out of range of enum");
  }
  llvm_unreachable("Covered switch");
}

OpDescriptor llvm::fuzzerop::cmpOpDescriptor(unsigned Weight,
                                             Instruction::OtherOps CmpOp,
                                             CmpInst::Predicate Pred) {
  auto buildOp = [CmpOp, Pred](ArrayRef<Value *> Srcs, Instruction *Inst) {
    return CmpInst::Create(CmpOp, Pred, Srcs[0], Srcs[1], "C", Inst);
  };

  switch (CmpOp) {
  case Instruction::ICmp:
    return {Weight, {anyIntOrVecIntType(), matchFirstType()}, buildOp};
  case Instruction::FCmp:
    return {Weight, {anyFloatOrVecFloatType(), matchFirstType()}, buildOp};
  default:
    llvm_unreachable("CmpOp must be ICmp or FCmp");
  }
}

OpDescriptor llvm::fuzzerop::splitBlockDescriptor(unsigned Weight) {
  auto buildSplitBlock = [](ArrayRef<Value *> Srcs, Instruction *Inst) {
    BasicBlock *Block = Inst->getParent();
    BasicBlock *Next = Block->splitBasicBlock(Inst, "BB");

    // If it was an exception handling block, we are done.
    if (Block->isEHPad())
      return nullptr;

    // Loop back on this block by replacing the unconditional forward branch
    // with a conditional with a backedge.
    if (Block != &Block->getParent()->getEntryBlock()) {
      BranchInst::Create(Block, Next, Srcs[0], Block->getTerminator());
      Block->getTerminator()->eraseFromParent();

      // We need values for each phi in the block. Since there isn't a good way
      // to do a variable number of input values currently, we just fill them
      // with undef.
      for (PHINode &PHI : Block->phis())
        PHI.addIncoming(UndefValue::get(PHI.getType()), Block);
    }
    return nullptr;
  };
  SourcePred isInt1Ty{[](ArrayRef<Value *>, const Value *V) {
                        return V->getType()->isIntegerTy(1);
                      },
                      std::nullopt};
  return {Weight, {isInt1Ty}, buildSplitBlock};
}

OpDescriptor llvm::fuzzerop::gepDescriptor(unsigned Weight) {
  auto buildGEP = [](ArrayRef<Value *> Srcs, Instruction *Inst) {
    // TODO: It would be better to generate a random type here, rather than
    // generating a random value and picking its type.
    Type *Ty = Srcs[1]->getType();
    auto Indices = ArrayRef(Srcs).drop_front(2);
    return GetElementPtrInst::Create(Ty, Srcs[0], Indices, "G", Inst);
  };
  // TODO: Handle aggregates and vectors
  // TODO: Support multiple indices.
  // TODO: Try to avoid meaningless accesses.
  SourcePred sizedType(
      [](ArrayRef<Value *>, const Value *V) { return V->getType()->isSized(); },
      std::nullopt);
  return {Weight, {sizedPtrType(), sizedType, anyIntType()}, buildGEP};
}

static uint64_t getAggregateNumElements(Type *T) {
  assert(T->isAggregateType() && "Not a struct or array");
  if (isa<StructType>(T))
    return T->getStructNumElements();
  return T->getArrayNumElements();
}

static SourcePred validExtractValueIndex() {
  auto Pred = [](ArrayRef<Value *> Cur, const Value *V) {
    if (auto *CI = dyn_cast<ConstantInt>(V))
      if (!CI->uge(getAggregateNumElements(Cur[0]->getType())))
        return true;
    return false;
  };
  auto Make = [](ArrayRef<Value *> Cur, ArrayRef<Type *> Ts) {
    std::vector<Constant *> Result;
    auto *Int32Ty = Type::getInt32Ty(Cur[0]->getContext());
    uint64_t N = getAggregateNumElements(Cur[0]->getType());
    // Create indices at the start, end, and middle, but avoid dups.
    Result.push_back(ConstantInt::get(Int32Ty, 0));
    if (N > 1)
      Result.push_back(ConstantInt::get(Int32Ty, N - 1));
    if (N > 2)
      Result.push_back(ConstantInt::get(Int32Ty, N / 2));
    return Result;
  };
  return {Pred, Make};
}

OpDescriptor llvm::fuzzerop::extractValueDescriptor(unsigned Weight) {
  auto buildExtract = [](ArrayRef<Value *> Srcs, Instruction *Inst) {
    // TODO: It's pretty inefficient to shuffle this all through constants.
    unsigned Idx = cast<ConstantInt>(Srcs[1])->getZExtValue();
    return ExtractValueInst::Create(Srcs[0], {Idx}, "E", Inst);
  };
  // TODO: Should we handle multiple indices?
  return {Weight, {anyAggregateType(), validExtractValueIndex()}, buildExtract};
}

static SourcePred matchScalarInAggregate() {
  auto Pred = [](ArrayRef<Value *> Cur, const Value *V) {
    if (auto *ArrayT = dyn_cast<ArrayType>(Cur[0]->getType()))
      return V->getType() == ArrayT->getElementType();

    auto *STy = cast<StructType>(Cur[0]->getType());
    for (int I = 0, E = STy->getNumElements(); I < E; ++I)
      if (STy->getTypeAtIndex(I) == V->getType())
        return true;
    return false;
  };
  auto Make = [](ArrayRef<Value *> Cur, ArrayRef<Type *>) {
    if (auto *ArrayT = dyn_cast<ArrayType>(Cur[0]->getType()))
      return makeConstantsWithType(ArrayT->getElementType());

    std::vector<Constant *> Result;
    auto *STy = cast<StructType>(Cur[0]->getType());
    for (int I = 0, E = STy->getNumElements(); I < E; ++I)
      makeConstantsWithType(STy->getTypeAtIndex(I), Result);
    return Result;
  };
  return {Pred, Make};
}

static SourcePred validInsertValueIndex() {
  auto Pred = [](ArrayRef<Value *> Cur, const Value *V) {
    if (auto *CI = dyn_cast<ConstantInt>(V))
      if (CI->getBitWidth() == 32) {
        Type *Indexed = ExtractValueInst::getIndexedType(Cur[0]->getType(),
                                                         CI->getZExtValue());
        return Indexed == Cur[1]->getType();
      }
    return false;
  };
  auto Make = [](ArrayRef<Value *> Cur, ArrayRef<Type *> Ts) {
    std::vector<Constant *> Result;
    auto *Int32Ty = Type::getInt32Ty(Cur[0]->getContext());
    auto *BaseTy = Cur[0]->getType();
    int I = 0;
    while (Type *Indexed = ExtractValueInst::getIndexedType(BaseTy, I)) {
      if (Indexed == Cur[1]->getType())
        Result.push_back(ConstantInt::get(Int32Ty, I));
      ++I;
    }
    return Result;
  };
  return {Pred, Make};
}

OpDescriptor llvm::fuzzerop::insertValueDescriptor(unsigned Weight) {
  auto buildInsert = [](ArrayRef<Value *> Srcs, Instruction *Inst) {
    // TODO: It's pretty inefficient to shuffle this all through constants.
    unsigned Idx = cast<ConstantInt>(Srcs[2])->getZExtValue();
    return InsertValueInst::Create(Srcs[0], Srcs[1], {Idx}, "I", Inst);
  };
  return {
      Weight,
      {anyAggregateType(), matchScalarInAggregate(), validInsertValueIndex()},
      buildInsert};
}

OpDescriptor llvm::fuzzerop::extractElementDescriptor(unsigned Weight) {
  auto buildExtract = [](ArrayRef<Value *> Srcs, Instruction *Inst) {
    return ExtractElementInst::Create(Srcs[0], Srcs[1], "E", Inst);
  };
  // TODO: Try to avoid undefined accesses.
  return {Weight, {anyVectorType(), anyIntType()}, buildExtract};
}

OpDescriptor llvm::fuzzerop::insertElementDescriptor(unsigned Weight) {
  auto buildInsert = [](ArrayRef<Value *> Srcs, Instruction *Inst) {
    return InsertElementInst::Create(Srcs[0], Srcs[1], Srcs[2], "I", Inst);
  };
  // TODO: Try to avoid undefined accesses.
  return {Weight,
          {anyVectorType(), matchScalarOfFirstType(), anyIntType()},
          buildInsert};
}

static SourcePred validShuffleVectorIndex() {
  auto Pred = [](ArrayRef<Value *> Cur, const Value *V) {
    return ShuffleVectorInst::isValidOperands(Cur[0], Cur[1], V);
  };
  auto Make = [](ArrayRef<Value *> Cur, ArrayRef<Type *> Ts) {
    auto *FirstTy = cast<VectorType>(Cur[0]->getType());
    auto *Int32Ty = Type::getInt32Ty(Cur[0]->getContext());
    // TODO: It's straighforward to make up reasonable values, but listing them
    // exhaustively would be insane. Come up with a couple of sensible ones.
    return std::vector<Constant *>{
        UndefValue::get(VectorType::get(Int32Ty, FirstTy->getElementCount()))};
  };
  return {Pred, Make};
}

OpDescriptor llvm::fuzzerop::shuffleVectorDescriptor(unsigned Weight) {
  auto buildShuffle = [](ArrayRef<Value *> Srcs, Instruction *Inst) {
    return new ShuffleVectorInst(Srcs[0], Srcs[1], Srcs[2], "S", Inst);
  };
  return {Weight,
          {anyVectorType(), matchFirstType(), validShuffleVectorIndex()},
          buildShuffle};
}

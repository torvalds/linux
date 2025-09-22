//===-- RandomIRBuilder.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/FuzzMutate/RandomIRBuilder.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/FuzzMutate/OpDescriptor.h"
#include "llvm/FuzzMutate/Random.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace fuzzerop;

/// Return a vector of Blocks that dominates this block, excluding current
/// block.
static std::vector<BasicBlock *> getDominators(BasicBlock *BB) {
  std::vector<BasicBlock *> ret;
  DominatorTree DT(*BB->getParent());
  DomTreeNode *Node = DT.getNode(BB);
  // It's possible that an orphan block is not in the dom tree. In that case we
  // just return nothing.
  if (!Node)
    return ret;
  Node = Node->getIDom();
  while (Node && Node->getBlock()) {
    ret.push_back(Node->getBlock());
    // Get parent block.
    Node = Node->getIDom();
  }
  return ret;
}

/// Return a vector of Blocks that is dominated by this block, excluding current
/// block
static std::vector<BasicBlock *> getDominatees(BasicBlock *BB) {
  DominatorTree DT(*BB->getParent());
  std::vector<BasicBlock *> ret;
  DomTreeNode *Parent = DT.getNode(BB);
  // It's possible that an orphan block is not in the dom tree. In that case we
  // just return nothing.
  if (!Parent)
    return ret;
  for (DomTreeNode *Child : Parent->children())
    ret.push_back(Child->getBlock());
  uint64_t Idx = 0;
  while (Idx < ret.size()) {
    DomTreeNode *Node = DT[ret[Idx]];
    Idx++;
    for (DomTreeNode *Child : Node->children())
      ret.push_back(Child->getBlock());
  }
  return ret;
}

AllocaInst *RandomIRBuilder::createStackMemory(Function *F, Type *Ty,
                                               Value *Init) {
  /// TODO: For all Allocas, maybe allocate an array.
  BasicBlock *EntryBB = &F->getEntryBlock();
  DataLayout DL(F->getParent());
  AllocaInst *Alloca = new AllocaInst(Ty, DL.getAllocaAddrSpace(), "A",
                                      &*EntryBB->getFirstInsertionPt());
  if (Init)
    new StoreInst(Init, Alloca, Alloca->getNextNode());
  return Alloca;
}

std::pair<GlobalVariable *, bool>
RandomIRBuilder::findOrCreateGlobalVariable(Module *M, ArrayRef<Value *> Srcs,
                                            fuzzerop::SourcePred Pred) {
  auto MatchesPred = [&Srcs, &Pred](GlobalVariable *GV) {
    // Can't directly compare GV's type, as it would be a pointer to the actual
    // type.
    return Pred.matches(Srcs, UndefValue::get(GV->getValueType()));
  };
  bool DidCreate = false;
  SmallVector<GlobalVariable *, 4> GlobalVars;
  for (GlobalVariable &GV : M->globals()) {
    GlobalVars.push_back(&GV);
  }
  auto RS = makeSampler(Rand, make_filter_range(GlobalVars, MatchesPred));
  RS.sample(nullptr, 1);
  GlobalVariable *GV = RS.getSelection();
  if (!GV) {
    DidCreate = true;
    using LinkageTypes = GlobalVariable::LinkageTypes;
    auto TRS = makeSampler<Constant *>(Rand);
    TRS.sample(Pred.generate(Srcs, KnownTypes));
    Constant *Init = TRS.getSelection();
    Type *Ty = Init->getType();
    GV = new GlobalVariable(*M, Ty, false, LinkageTypes::ExternalLinkage, Init,
                            "G", nullptr,
                            GlobalValue::ThreadLocalMode::NotThreadLocal,
                            M->getDataLayout().getDefaultGlobalsAddressSpace());
  }
  return {GV, DidCreate};
}

Value *RandomIRBuilder::findOrCreateSource(BasicBlock &BB,
                                           ArrayRef<Instruction *> Insts) {
  return findOrCreateSource(BB, Insts, {}, anyType());
}

Value *RandomIRBuilder::findOrCreateSource(BasicBlock &BB,
                                           ArrayRef<Instruction *> Insts,
                                           ArrayRef<Value *> Srcs,
                                           SourcePred Pred,
                                           bool allowConstant) {
  auto MatchesPred = [&Srcs, &Pred](Value *V) { return Pred.matches(Srcs, V); };
  SmallVector<uint64_t, 8> SrcTys;
  for (uint64_t i = 0; i < EndOfValueSource; i++)
    SrcTys.push_back(i);
  std::shuffle(SrcTys.begin(), SrcTys.end(), Rand);
  for (uint64_t SrcTy : SrcTys) {
    switch (SrcTy) {
    case SrcFromInstInCurBlock: {
      auto RS = makeSampler(Rand, make_filter_range(Insts, MatchesPred));
      if (!RS.isEmpty()) {
        return RS.getSelection();
      }
      break;
    }
    case FunctionArgument: {
      Function *F = BB.getParent();
      SmallVector<Argument *, 8> Args;
      for (uint64_t i = 0; i < F->arg_size(); i++) {
        Args.push_back(F->getArg(i));
      }
      auto RS = makeSampler(Rand, make_filter_range(Args, MatchesPred));
      if (!RS.isEmpty()) {
        return RS.getSelection();
      }
      break;
    }
    case InstInDominator: {
      auto Dominators = getDominators(&BB);
      std::shuffle(Dominators.begin(), Dominators.end(), Rand);
      for (BasicBlock *Dom : Dominators) {
        SmallVector<Instruction *, 16> Instructions;
        for (Instruction &I : *Dom) {
          Instructions.push_back(&I);
        }
        auto RS =
            makeSampler(Rand, make_filter_range(Instructions, MatchesPred));
        // Also consider choosing no source, meaning we want a new one.
        if (!RS.isEmpty()) {
          return RS.getSelection();
        }
      }
      break;
    }
    case SrcFromGlobalVariable: {
      Module *M = BB.getParent()->getParent();
      auto [GV, DidCreate] = findOrCreateGlobalVariable(M, Srcs, Pred);
      Type *Ty = GV->getValueType();
      LoadInst *LoadGV = nullptr;
      if (BB.getTerminator()) {
        LoadGV = new LoadInst(Ty, GV, "LGV", &*BB.getFirstInsertionPt());
      } else {
        LoadGV = new LoadInst(Ty, GV, "LGV", &BB);
      }
      // Because we might be generating new values, we have to check if it
      // matches again.
      if (DidCreate) {
        if (Pred.matches(Srcs, LoadGV)) {
          return LoadGV;
        }
        LoadGV->eraseFromParent();
        // If no one is using this GlobalVariable, delete it too.
        if (GV->use_empty()) {
          GV->eraseFromParent();
        }
      }
      break;
    }
    case NewConstOrStack: {
      return newSource(BB, Insts, Srcs, Pred, allowConstant);
    }
    default:
    case EndOfValueSource: {
      llvm_unreachable("EndOfValueSource executed");
    }
    }
  }
  llvm_unreachable("Can't find a source");
}

Value *RandomIRBuilder::newSource(BasicBlock &BB, ArrayRef<Instruction *> Insts,
                                  ArrayRef<Value *> Srcs, SourcePred Pred,
                                  bool allowConstant) {
  // Generate some constants to choose from.
  auto RS = makeSampler<Value *>(Rand);
  RS.sample(Pred.generate(Srcs, KnownTypes));

  // If we can find a pointer to load from, use it half the time.
  Value *Ptr = findPointer(BB, Insts);
  if (Ptr) {
    // Create load from the chosen pointer
    auto IP = BB.getFirstInsertionPt();
    if (auto *I = dyn_cast<Instruction>(Ptr)) {
      IP = ++I->getIterator();
      assert(IP != BB.end() && "guaranteed by the findPointer");
    }
    // Pick the type independently.
    Type *AccessTy = RS.getSelection()->getType();
    auto *NewLoad = new LoadInst(AccessTy, Ptr, "L", &*IP);

    // Only sample this load if it really matches the descriptor
    if (Pred.matches(Srcs, NewLoad))
      RS.sample(NewLoad, RS.totalWeight());
    else
      NewLoad->eraseFromParent();
  }

  Value *newSrc = RS.getSelection();
  // Generate a stack alloca and store the constant to it if constant is not
  // allowed, our hope is that later mutations can generate some values and
  // store to this placeholder.
  if (!allowConstant && isa<Constant>(newSrc)) {
    Type *Ty = newSrc->getType();
    Function *F = BB.getParent();
    AllocaInst *Alloca = createStackMemory(F, Ty, newSrc);
    if (BB.getTerminator()) {
      newSrc = new LoadInst(Ty, Alloca, /*ArrLen,*/ "L", BB.getTerminator());
    } else {
      newSrc = new LoadInst(Ty, Alloca, /*ArrLen,*/ "L", &BB);
    }
  }
  return newSrc;
}

static bool isCompatibleReplacement(const Instruction *I, const Use &Operand,
                                    const Value *Replacement) {
  unsigned int OperandNo = Operand.getOperandNo();
  if (Operand->getType() != Replacement->getType())
    return false;
  switch (I->getOpcode()) {
  case Instruction::GetElementPtr:
  case Instruction::ExtractElement:
  case Instruction::ExtractValue:
    // TODO: We could potentially validate these, but for now just leave indices
    // alone.
    if (OperandNo >= 1)
      return false;
    break;
  case Instruction::InsertValue:
  case Instruction::InsertElement:
  case Instruction::ShuffleVector:
    if (OperandNo >= 2)
      return false;
    break;
  // For Br/Switch, we only try to modify the 1st Operand (condition).
  // Modify other operands, like switch case may accidently change case from
  // ConstantInt to a register, which is illegal.
  case Instruction::Switch:
  case Instruction::Br:
    if (OperandNo >= 1)
      return false;
    break;
  case Instruction::Call:
  case Instruction::Invoke:
  case Instruction::CallBr: {
    const Function *Callee = cast<CallBase>(I)->getCalledFunction();
    // If it's an indirect call, give up.
    if (!Callee)
      return false;
    // If callee is not an intrinsic, operand 0 is the function to be called.
    // Since we cannot assume that the replacement is a function pointer,
    // we give up.
    if (!Callee->getIntrinsicID() && OperandNo == 0)
      return false;
    return !Callee->hasParamAttribute(OperandNo, Attribute::ImmArg);
  }
  default:
    break;
  }
  return true;
}

Instruction *RandomIRBuilder::connectToSink(BasicBlock &BB,
                                            ArrayRef<Instruction *> Insts,
                                            Value *V) {
  SmallVector<uint64_t, 8> SinkTys;
  for (uint64_t i = 0; i < EndOfValueSink; i++)
    SinkTys.push_back(i);
  std::shuffle(SinkTys.begin(), SinkTys.end(), Rand);
  auto findSinkAndConnect =
      [this, V](ArrayRef<Instruction *> Instructions) -> Instruction * {
    auto RS = makeSampler<Use *>(Rand);
    for (auto &I : Instructions) {
      for (Use &U : I->operands())
        if (isCompatibleReplacement(I, U, V))
          RS.sample(&U, 1);
    }
    if (!RS.isEmpty()) {
      Use *Sink = RS.getSelection();
      User *U = Sink->getUser();
      unsigned OpNo = Sink->getOperandNo();
      U->setOperand(OpNo, V);
      return cast<Instruction>(U);
    }
    return nullptr;
  };
  Instruction *Sink = nullptr;
  for (uint64_t SinkTy : SinkTys) {
    switch (SinkTy) {
    case SinkToInstInCurBlock:
      Sink = findSinkAndConnect(Insts);
      if (Sink)
        return Sink;
      break;
    case PointersInDominator: {
      auto Dominators = getDominators(&BB);
      std::shuffle(Dominators.begin(), Dominators.end(), Rand);
      for (BasicBlock *Dom : Dominators) {
        for (Instruction &I : *Dom) {
          if (isa<PointerType>(I.getType()))
            return new StoreInst(V, &I, Insts.back());
        }
      }
      break;
    }
    case InstInDominatee: {
      auto Dominatees = getDominatees(&BB);
      std::shuffle(Dominatees.begin(), Dominatees.end(), Rand);
      for (BasicBlock *Dominee : Dominatees) {
        std::vector<Instruction *> Instructions;
        for (Instruction &I : *Dominee)
          Instructions.push_back(&I);
        Sink = findSinkAndConnect(Instructions);
        if (Sink) {
          return Sink;
        }
      }
      break;
    }
    case NewStore:
      /// TODO: allocate a new stack memory.
      return newSink(BB, Insts, V);
    case SinkToGlobalVariable: {
      Module *M = BB.getParent()->getParent();
      auto [GV, DidCreate] =
          findOrCreateGlobalVariable(M, {}, fuzzerop::onlyType(V->getType()));
      return new StoreInst(V, GV, Insts.back());
    }
    case EndOfValueSink:
    default:
      llvm_unreachable("EndOfValueSink executed");
    }
  }
  llvm_unreachable("Can't find a sink");
}

Instruction *RandomIRBuilder::newSink(BasicBlock &BB,
                                      ArrayRef<Instruction *> Insts, Value *V) {
  Value *Ptr = findPointer(BB, Insts);
  if (!Ptr) {
    if (uniform(Rand, 0, 1)) {
      Type *Ty = V->getType();
      Ptr = createStackMemory(BB.getParent(), Ty, UndefValue::get(Ty));
    } else {
      Ptr = UndefValue::get(PointerType::get(V->getType(), 0));
    }
  }

  return new StoreInst(V, Ptr, Insts.back());
}

Value *RandomIRBuilder::findPointer(BasicBlock &BB,
                                    ArrayRef<Instruction *> Insts) {
  auto IsMatchingPtr = [](Instruction *Inst) {
    // Invoke instructions sometimes produce valid pointers but currently
    // we can't insert loads or stores from them
    if (Inst->isTerminator())
      return false;

    return Inst->getType()->isPointerTy();
  };
  if (auto RS = makeSampler(Rand, make_filter_range(Insts, IsMatchingPtr)))
    return RS.getSelection();
  return nullptr;
}

Type *RandomIRBuilder::randomType() {
  uint64_t TyIdx = uniform<uint64_t>(Rand, 0, KnownTypes.size() - 1);
  return KnownTypes[TyIdx];
}

Function *RandomIRBuilder::createFunctionDeclaration(Module &M,
                                                     uint64_t ArgNum) {
  Type *RetType = randomType();

  SmallVector<Type *, 2> Args;
  for (uint64_t i = 0; i < ArgNum; i++) {
    Args.push_back(randomType());
  }

  Function *F = Function::Create(FunctionType::get(RetType, Args,
                                                   /*isVarArg=*/false),
                                 GlobalValue::ExternalLinkage, "f", &M);
  return F;
}
Function *RandomIRBuilder::createFunctionDeclaration(Module &M) {
  return createFunctionDeclaration(
      M, uniform<uint64_t>(Rand, MinArgNum, MaxArgNum));
}

Function *RandomIRBuilder::createFunctionDefinition(Module &M,
                                                    uint64_t ArgNum) {
  Function *F = this->createFunctionDeclaration(M, ArgNum);

  // TODO: Some arguments and a return value would probably be more
  // interesting.
  LLVMContext &Context = M.getContext();
  DataLayout DL(&M);
  BasicBlock *BB = BasicBlock::Create(Context, "BB", F);
  Type *RetTy = F->getReturnType();
  if (RetTy != Type::getVoidTy(Context)) {
    Instruction *RetAlloca =
        new AllocaInst(RetTy, DL.getAllocaAddrSpace(), "RP", BB);
    Instruction *RetLoad = new LoadInst(RetTy, RetAlloca, "", BB);
    ReturnInst::Create(Context, RetLoad, BB);
  } else {
    ReturnInst::Create(Context, BB);
  }

  return F;
}
Function *RandomIRBuilder::createFunctionDefinition(Module &M) {
  return createFunctionDefinition(
      M, uniform<uint64_t>(Rand, MinArgNum, MaxArgNum));
}

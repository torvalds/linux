//===-- IRMutator.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/FuzzMutate/IRMutator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/FuzzMutate/Operations.h"
#include "llvm/FuzzMutate/Random.h"
#include "llvm/FuzzMutate/RandomIRBuilder.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <map>
#include <optional>

using namespace llvm;

void IRMutationStrategy::mutate(Module &M, RandomIRBuilder &IB) {
  auto RS = makeSampler<Function *>(IB.Rand);
  for (Function &F : M)
    if (!F.isDeclaration())
      RS.sample(&F, /*Weight=*/1);

  while (RS.totalWeight() < IB.MinFunctionNum) {
    Function *F = IB.createFunctionDefinition(M);
    RS.sample(F, /*Weight=*/1);
  }
  mutate(*RS.getSelection(), IB);
}

void IRMutationStrategy::mutate(Function &F, RandomIRBuilder &IB) {
  auto Range = make_filter_range(make_pointer_range(F),
                                 [](BasicBlock *BB) { return !BB->isEHPad(); });

  mutate(*makeSampler(IB.Rand, Range).getSelection(), IB);
}

void IRMutationStrategy::mutate(BasicBlock &BB, RandomIRBuilder &IB) {
  mutate(*makeSampler(IB.Rand, make_pointer_range(BB)).getSelection(), IB);
}

size_t llvm::IRMutator::getModuleSize(const Module &M) {
  return M.getInstructionCount() + M.size() + M.global_size() + M.alias_size();
}

void IRMutator::mutateModule(Module &M, int Seed, size_t MaxSize) {
  std::vector<Type *> Types;
  for (const auto &Getter : AllowedTypes)
    Types.push_back(Getter(M.getContext()));
  RandomIRBuilder IB(Seed, Types);

  size_t CurSize = IRMutator::getModuleSize(M);
  auto RS = makeSampler<IRMutationStrategy *>(IB.Rand);
  for (const auto &Strategy : Strategies)
    RS.sample(Strategy.get(),
              Strategy->getWeight(CurSize, MaxSize, RS.totalWeight()));
  if (RS.totalWeight() == 0)
    return;
  auto Strategy = RS.getSelection();

  Strategy->mutate(M, IB);
}

static void eliminateDeadCode(Function &F) {
  FunctionPassManager FPM;
  FPM.addPass(DCEPass());
  FunctionAnalysisManager FAM;
  FAM.registerPass([&] { return TargetLibraryAnalysis(); });
  FAM.registerPass([&] { return PassInstrumentationAnalysis(); });
  FPM.run(F, FAM);
}

void InjectorIRStrategy::mutate(Function &F, RandomIRBuilder &IB) {
  IRMutationStrategy::mutate(F, IB);
  eliminateDeadCode(F);
}

std::vector<fuzzerop::OpDescriptor> InjectorIRStrategy::getDefaultOps() {
  std::vector<fuzzerop::OpDescriptor> Ops;
  describeFuzzerIntOps(Ops);
  describeFuzzerFloatOps(Ops);
  describeFuzzerControlFlowOps(Ops);
  describeFuzzerPointerOps(Ops);
  describeFuzzerAggregateOps(Ops);
  describeFuzzerVectorOps(Ops);
  return Ops;
}

std::optional<fuzzerop::OpDescriptor>
InjectorIRStrategy::chooseOperation(Value *Src, RandomIRBuilder &IB) {
  auto OpMatchesPred = [&Src](fuzzerop::OpDescriptor &Op) {
    return Op.SourcePreds[0].matches({}, Src);
  };
  auto RS = makeSampler(IB.Rand, make_filter_range(Operations, OpMatchesPred));
  if (RS.isEmpty())
    return std::nullopt;
  return *RS;
}

static inline iterator_range<BasicBlock::iterator>
getInsertionRange(BasicBlock &BB) {
  auto End = BB.getTerminatingMustTailCall() ? std::prev(BB.end()) : BB.end();
  return make_range(BB.getFirstInsertionPt(), End);
}

void InjectorIRStrategy::mutate(BasicBlock &BB, RandomIRBuilder &IB) {
  SmallVector<Instruction *, 32> Insts;
  for (Instruction &I : getInsertionRange(BB))
    Insts.push_back(&I);
  if (Insts.size() < 1)
    return;

  // Choose an insertion point for our new instruction.
  size_t IP = uniform<size_t>(IB.Rand, 0, Insts.size() - 1);

  auto InstsBefore = ArrayRef(Insts).slice(0, IP);
  auto InstsAfter = ArrayRef(Insts).slice(IP);

  // Choose a source, which will be used to constrain the operation selection.
  SmallVector<Value *, 2> Srcs;
  Srcs.push_back(IB.findOrCreateSource(BB, InstsBefore));

  // Choose an operation that's constrained to be valid for the type of the
  // source, collect any other sources it needs, and then build it.
  auto OpDesc = chooseOperation(Srcs[0], IB);
  // Bail if no operation was found
  if (!OpDesc)
    return;

  for (const auto &Pred : ArrayRef(OpDesc->SourcePreds).slice(1))
    Srcs.push_back(IB.findOrCreateSource(BB, InstsBefore, Srcs, Pred));

  if (Value *Op = OpDesc->BuilderFunc(Srcs, Insts[IP])) {
    // Find a sink and wire up the results of the operation.
    IB.connectToSink(BB, InstsAfter, Op);
  }
}

uint64_t InstDeleterIRStrategy::getWeight(size_t CurrentSize, size_t MaxSize,
                                          uint64_t CurrentWeight) {
  // If we have less than 200 bytes, panic and try to always delete.
  if (CurrentSize > MaxSize - 200)
    return CurrentWeight ? CurrentWeight * 100 : 1;
  // Draw a line starting from when we only have 1k left and increasing linearly
  // to double the current weight.
  int64_t Line = (-2 * static_cast<int64_t>(CurrentWeight)) *
                 (static_cast<int64_t>(MaxSize) -
                  static_cast<int64_t>(CurrentSize) - 1000) /
                 1000;
  // Clamp negative weights to zero.
  if (Line < 0)
    return 0;
  return Line;
}

void InstDeleterIRStrategy::mutate(Function &F, RandomIRBuilder &IB) {
  auto RS = makeSampler<Instruction *>(IB.Rand);
  for (Instruction &Inst : instructions(F)) {
    // TODO: We can't handle these instructions.
    if (Inst.isTerminator() || Inst.isEHPad() || Inst.isSwiftError() ||
        isa<PHINode>(Inst))
      continue;

    RS.sample(&Inst, /*Weight=*/1);
  }
  if (RS.isEmpty())
    return;

  // Delete the instruction.
  mutate(*RS.getSelection(), IB);
  // Clean up any dead code that's left over after removing the instruction.
  eliminateDeadCode(F);
}

void InstDeleterIRStrategy::mutate(Instruction &Inst, RandomIRBuilder &IB) {
  assert(!Inst.isTerminator() && "Deleting terminators invalidates CFG");

  if (Inst.getType()->isVoidTy()) {
    // Instructions with void type (ie, store) have no uses to worry about. Just
    // erase it and move on.
    Inst.eraseFromParent();
    return;
  }

  // Otherwise we need to find some other value with the right type to keep the
  // users happy.
  auto Pred = fuzzerop::onlyType(Inst.getType());
  auto RS = makeSampler<Value *>(IB.Rand);
  SmallVector<Instruction *, 32> InstsBefore;
  BasicBlock *BB = Inst.getParent();
  for (auto I = BB->getFirstInsertionPt(), E = Inst.getIterator(); I != E;
       ++I) {
    if (Pred.matches({}, &*I))
      RS.sample(&*I, /*Weight=*/1);
    InstsBefore.push_back(&*I);
  }
  if (!RS)
    RS.sample(IB.newSource(*BB, InstsBefore, {}, Pred), /*Weight=*/1);

  Inst.replaceAllUsesWith(RS.getSelection());
  Inst.eraseFromParent();
}

void InstModificationIRStrategy::mutate(Instruction &Inst,
                                        RandomIRBuilder &IB) {
  SmallVector<std::function<void()>, 8> Modifications;
  CmpInst *CI = nullptr;
  GetElementPtrInst *GEP = nullptr;
  switch (Inst.getOpcode()) {
  default:
    break;
  // Add nsw, nuw flag
  case Instruction::Add:
  case Instruction::Mul:
  case Instruction::Sub:
  case Instruction::Shl:
    Modifications.push_back(
        [&Inst]() { Inst.setHasNoSignedWrap(!Inst.hasNoSignedWrap()); });
    Modifications.push_back(
        [&Inst]() { Inst.setHasNoUnsignedWrap(!Inst.hasNoUnsignedWrap()); });
    break;
  case Instruction::ICmp:
    CI = cast<ICmpInst>(&Inst);
    for (unsigned p = CmpInst::FIRST_ICMP_PREDICATE;
         p <= CmpInst::LAST_ICMP_PREDICATE; p++) {
      Modifications.push_back(
          [CI, p]() { CI->setPredicate(static_cast<CmpInst::Predicate>(p)); });
    }
    break;
  // Add inbound flag.
  case Instruction::GetElementPtr:
    GEP = cast<GetElementPtrInst>(&Inst);
    Modifications.push_back(
        [GEP]() { GEP->setIsInBounds(!GEP->isInBounds()); });
    break;
  // Add exact flag.
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::LShr:
  case Instruction::AShr:
    Modifications.push_back([&Inst] { Inst.setIsExact(!Inst.isExact()); });
    break;

  case Instruction::FCmp:
    CI = cast<FCmpInst>(&Inst);
    for (unsigned p = CmpInst::FIRST_FCMP_PREDICATE;
         p <= CmpInst::LAST_FCMP_PREDICATE; p++) {
      Modifications.push_back(
          [CI, p]() { CI->setPredicate(static_cast<CmpInst::Predicate>(p)); });
    }
    break;
  }

  // Add fast math flag if possible.
  if (isa<FPMathOperator>(&Inst)) {
    // Try setting everything unless they are already on.
    Modifications.push_back(
        [&Inst] { Inst.setFast(!Inst.getFastMathFlags().all()); });
    // Try unsetting everything unless they are already off.
    Modifications.push_back(
        [&Inst] { Inst.setFast(!Inst.getFastMathFlags().none()); });
    // Individual setting by flipping the bit
    Modifications.push_back(
        [&Inst] { Inst.setHasAllowReassoc(!Inst.hasAllowReassoc()); });
    Modifications.push_back([&Inst] { Inst.setHasNoNaNs(!Inst.hasNoNaNs()); });
    Modifications.push_back([&Inst] { Inst.setHasNoInfs(!Inst.hasNoInfs()); });
    Modifications.push_back(
        [&Inst] { Inst.setHasNoSignedZeros(!Inst.hasNoSignedZeros()); });
    Modifications.push_back(
        [&Inst] { Inst.setHasAllowReciprocal(!Inst.hasAllowReciprocal()); });
    Modifications.push_back(
        [&Inst] { Inst.setHasAllowContract(!Inst.hasAllowContract()); });
    Modifications.push_back(
        [&Inst] { Inst.setHasApproxFunc(!Inst.hasApproxFunc()); });
  }

  // Randomly switch operands of instructions
  std::pair<int, int> NoneItem({-1, -1}), ShuffleItems(NoneItem);
  switch (Inst.getOpcode()) {
  case Instruction::SDiv:
  case Instruction::UDiv:
  case Instruction::SRem:
  case Instruction::URem:
  case Instruction::FDiv:
  case Instruction::FRem: {
    // Verify that the after shuffle the second operand is not
    // constant 0.
    Value *Operand = Inst.getOperand(0);
    if (Constant *C = dyn_cast<Constant>(Operand)) {
      if (!C->isZeroValue()) {
        ShuffleItems = {0, 1};
      }
    }
    break;
  }
  case Instruction::Select:
    ShuffleItems = {1, 2};
    break;
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
  case Instruction::ICmp:
  case Instruction::FCmp:
  case Instruction::ShuffleVector:
    ShuffleItems = {0, 1};
    break;
  }
  if (ShuffleItems != NoneItem) {
    Modifications.push_back([&Inst, &ShuffleItems]() {
      Value *Op0 = Inst.getOperand(ShuffleItems.first);
      Inst.setOperand(ShuffleItems.first, Inst.getOperand(ShuffleItems.second));
      Inst.setOperand(ShuffleItems.second, Op0);
    });
  }

  auto RS = makeSampler(IB.Rand, Modifications);
  if (RS)
    RS.getSelection()();
}

/// Return a case value that is not already taken to make sure we don't have two
/// cases with same value.
static uint64_t getUniqueCaseValue(SmallSet<uint64_t, 4> &CasesTaken,
                                   uint64_t MaxValue, RandomIRBuilder &IB) {
  uint64_t tmp;
  do {
    tmp = uniform<uint64_t>(IB.Rand, 0, MaxValue);
  } while (CasesTaken.count(tmp) != 0);
  CasesTaken.insert(tmp);
  return tmp;
}

void InsertFunctionStrategy::mutate(BasicBlock &BB, RandomIRBuilder &IB) {
  Module *M = BB.getParent()->getParent();
  // If nullptr is selected, we will create a new function declaration.
  SmallVector<Function *, 32> Functions({nullptr});
  for (Function &F : M->functions()) {
    Functions.push_back(&F);
  }

  auto RS = makeSampler(IB.Rand, Functions);
  Function *F = RS.getSelection();
  // Some functions accept metadata type or token type as arguments.
  // We don't call those functions for now.
  // For example, `@llvm.dbg.declare(metadata, metadata, metadata)`
  // https://llvm.org/docs/SourceLevelDebugging.html#llvm-dbg-declare
  auto IsUnsupportedTy = [](Type *T) {
    return T->isMetadataTy() || T->isTokenTy();
  };
  if (!F || IsUnsupportedTy(F->getReturnType()) ||
      any_of(F->getFunctionType()->params(), IsUnsupportedTy)) {
    F = IB.createFunctionDeclaration(*M);
  }

  FunctionType *FTy = F->getFunctionType();
  SmallVector<fuzzerop::SourcePred, 2> SourcePreds;
  if (!F->arg_empty()) {
    for (Type *ArgTy : FTy->params()) {
      SourcePreds.push_back(fuzzerop::onlyType(ArgTy));
    }
  }
  bool isRetVoid = (F->getReturnType() == Type::getVoidTy(M->getContext()));
  auto BuilderFunc = [FTy, F, isRetVoid](ArrayRef<Value *> Srcs,
                                         Instruction *Inst) {
    StringRef Name = isRetVoid ? nullptr : "C";
    CallInst *Call = CallInst::Create(FTy, F, Srcs, Name, Inst);
    // Don't return this call inst if it return void as it can't be sinked.
    return isRetVoid ? nullptr : Call;
  };

  SmallVector<Instruction *, 32> Insts;
  for (Instruction &I : getInsertionRange(BB))
    Insts.push_back(&I);
  if (Insts.size() < 1)
    return;

  // Choose an insertion point for our new call instruction.
  uint64_t IP = uniform<uint64_t>(IB.Rand, 0, Insts.size() - 1);

  auto InstsBefore = ArrayRef(Insts).slice(0, IP);
  auto InstsAfter = ArrayRef(Insts).slice(IP);

  // Choose a source, which will be used to constrain the operation selection.
  SmallVector<Value *, 2> Srcs;

  for (const auto &Pred : ArrayRef(SourcePreds)) {
    Srcs.push_back(IB.findOrCreateSource(BB, InstsBefore, Srcs, Pred));
  }

  if (Value *Op = BuilderFunc(Srcs, Insts[IP])) {
    // Find a sink and wire up the results of the operation.
    IB.connectToSink(BB, InstsAfter, Op);
  }
}

void InsertCFGStrategy::mutate(BasicBlock &BB, RandomIRBuilder &IB) {
  SmallVector<Instruction *, 32> Insts;
  for (Instruction &I : getInsertionRange(BB))
    Insts.push_back(&I);
  if (Insts.size() < 1)
    return;

  // Choose a point where we split the block.
  uint64_t IP = uniform<uint64_t>(IB.Rand, 0, Insts.size() - 1);
  auto InstsBeforeSplit = ArrayRef(Insts).slice(0, IP);

  // `Sink` inherits Blocks' terminator, `Source` will have a BranchInst
  // directly jumps to `Sink`. Here, we have to create a new terminator for
  // `Source`.
  BasicBlock *Block = Insts[IP]->getParent();
  BasicBlock *Source = Block;
  BasicBlock *Sink = Block->splitBasicBlock(Insts[IP], "BB");

  Function *F = BB.getParent();
  LLVMContext &C = F->getParent()->getContext();
  // A coin decides if it is branch or switch
  if (uniform<uint64_t>(IB.Rand, 0, 1)) {
    // Branch
    BasicBlock *IfTrue = BasicBlock::Create(C, "T", F);
    BasicBlock *IfFalse = BasicBlock::Create(C, "F", F);
    Value *Cond =
        IB.findOrCreateSource(*Source, InstsBeforeSplit, {},
                              fuzzerop::onlyType(Type::getInt1Ty(C)), false);
    BranchInst *Branch = BranchInst::Create(IfTrue, IfFalse, Cond);
    // Remove the old terminator.
    ReplaceInstWithInst(Source->getTerminator(), Branch);
    // Connect these blocks to `Sink`
    connectBlocksToSink({IfTrue, IfFalse}, Sink, IB);
  } else {
    // Switch
    // Determine Integer type, it IS possible we use a boolean to switch.
    auto RS =
        makeSampler(IB.Rand, make_filter_range(IB.KnownTypes, [](Type *Ty) {
                      return Ty->isIntegerTy();
                    }));
    assert(RS && "There is no integer type in all allowed types, is the "
                 "setting correct?");
    Type *Ty = RS.getSelection();
    IntegerType *IntTy = cast<IntegerType>(Ty);

    uint64_t BitSize = IntTy->getBitWidth();
    uint64_t MaxCaseVal =
        (BitSize >= 64) ? (uint64_t)-1 : ((uint64_t)1 << BitSize) - 1;
    // Create Switch inst in Block
    Value *Cond = IB.findOrCreateSource(*Source, InstsBeforeSplit, {},
                                        fuzzerop::onlyType(IntTy), false);
    BasicBlock *DefaultBlock = BasicBlock::Create(C, "SW_D", F);
    uint64_t NumCases = uniform<uint64_t>(IB.Rand, 1, MaxNumCases);
    NumCases = (NumCases > MaxCaseVal) ? MaxCaseVal + 1 : NumCases;
    SwitchInst *Switch = SwitchInst::Create(Cond, DefaultBlock, NumCases);
    // Remove the old terminator.
    ReplaceInstWithInst(Source->getTerminator(), Switch);

    // Create blocks, for each block assign a case value.
    SmallVector<BasicBlock *, 4> Blocks({DefaultBlock});
    SmallSet<uint64_t, 4> CasesTaken;
    for (uint64_t i = 0; i < NumCases; i++) {
      uint64_t CaseVal = getUniqueCaseValue(CasesTaken, MaxCaseVal, IB);
      BasicBlock *CaseBlock = BasicBlock::Create(C, "SW_C", F);
      ConstantInt *OnValue = ConstantInt::get(IntTy, CaseVal);
      Switch->addCase(OnValue, CaseBlock);
      Blocks.push_back(CaseBlock);
    }

    // Connect these blocks to `Sink`
    connectBlocksToSink(Blocks, Sink, IB);
  }
}

/// The caller has to guarantee that these blocks are "empty", i.e. it doesn't
/// even have terminator.
void InsertCFGStrategy::connectBlocksToSink(ArrayRef<BasicBlock *> Blocks,
                                            BasicBlock *Sink,
                                            RandomIRBuilder &IB) {
  uint64_t DirectSinkIdx = uniform<uint64_t>(IB.Rand, 0, Blocks.size() - 1);
  for (uint64_t i = 0; i < Blocks.size(); i++) {
    // We have at least one block that directly goes to sink.
    CFGToSink ToSink = (i == DirectSinkIdx)
                           ? CFGToSink::DirectSink
                           : static_cast<CFGToSink>(uniform<uint64_t>(
                                 IB.Rand, 0, CFGToSink::EndOfCFGToLink - 1));
    BasicBlock *BB = Blocks[i];
    Function *F = BB->getParent();
    LLVMContext &C = F->getParent()->getContext();
    switch (ToSink) {
    case CFGToSink::Return: {
      Type *RetTy = F->getReturnType();
      Value *RetValue = nullptr;
      if (!RetTy->isVoidTy())
        RetValue =
            IB.findOrCreateSource(*BB, {}, {}, fuzzerop::onlyType(RetTy));
      ReturnInst::Create(C, RetValue, BB);
      break;
    }
    case CFGToSink::DirectSink: {
      BranchInst::Create(Sink, BB);
      break;
    }
    case CFGToSink::SinkOrSelfLoop: {
      SmallVector<BasicBlock *, 2> Branches({Sink, BB});
      // A coin decides which block is true branch.
      uint64_t coin = uniform<uint64_t>(IB.Rand, 0, 1);
      Value *Cond = IB.findOrCreateSource(
          *BB, {}, {}, fuzzerop::onlyType(Type::getInt1Ty(C)), false);
      BranchInst::Create(Branches[coin], Branches[1 - coin], Cond, BB);
      break;
    }
    case CFGToSink::EndOfCFGToLink:
      llvm_unreachable("EndOfCFGToLink executed, something's wrong.");
    }
  }
}

void InsertPHIStrategy::mutate(BasicBlock &BB, RandomIRBuilder &IB) {
  // Can't insert PHI node to entry node.
  if (&BB == &BB.getParent()->getEntryBlock())
    return;
  Type *Ty = IB.randomType();
  PHINode *PHI = PHINode::Create(Ty, llvm::pred_size(&BB), "", &BB.front());

  // Use a map to make sure the same incoming basic block has the same value.
  DenseMap<BasicBlock *, Value *> IncomingValues;
  for (BasicBlock *Pred : predecessors(&BB)) {
    Value *Src = IncomingValues[Pred];
    // If `Pred` is not in the map yet, we'll get a nullptr.
    if (!Src) {
      SmallVector<Instruction *, 32> Insts;
      for (auto I = Pred->begin(); I != Pred->end(); ++I)
        Insts.push_back(&*I);
      // There is no need to inform IB what previously used values are if we are
      // using `onlyType`
      Src = IB.findOrCreateSource(*Pred, Insts, {}, fuzzerop::onlyType(Ty));
      IncomingValues[Pred] = Src;
    }
    PHI->addIncoming(Src, Pred);
  }
  SmallVector<Instruction *, 32> InstsAfter;
  for (Instruction &I : getInsertionRange(BB))
    InstsAfter.push_back(&I);
  IB.connectToSink(BB, InstsAfter, PHI);
}

void SinkInstructionStrategy::mutate(Function &F, RandomIRBuilder &IB) {
  for (BasicBlock &BB : F) {
    this->mutate(BB, IB);
  }
}
void SinkInstructionStrategy::mutate(BasicBlock &BB, RandomIRBuilder &IB) {
  SmallVector<Instruction *, 32> Insts;
  for (Instruction &I : getInsertionRange(BB))
    Insts.push_back(&I);
  if (Insts.size() < 1)
    return;
  // Choose an Instruction to mutate.
  uint64_t Idx = uniform<uint64_t>(IB.Rand, 0, Insts.size() - 1);
  Instruction *Inst = Insts[Idx];
  // `Idx + 1` so we don't sink to ourselves.
  auto InstsAfter = ArrayRef(Insts).slice(Idx + 1);
  Type *Ty = Inst->getType();
  // Don't sink terminators, void function calls, token, etc.
  if (!Ty->isVoidTy() && !Ty->isTokenTy())
    // Find a new sink and wire up the results of the operation.
    IB.connectToSink(BB, InstsAfter, Inst);
}

void ShuffleBlockStrategy::mutate(BasicBlock &BB, RandomIRBuilder &IB) {
  // A deterministic alternative to SmallPtrSet with the same lookup
  // performance.
  std::map<size_t, Instruction *> AliveInsts;
  std::map<Instruction *, size_t> AliveInstsLookup;
  size_t InsertIdx = 0;
  for (auto &I : make_early_inc_range(make_range(
           BB.getFirstInsertionPt(), BB.getTerminator()->getIterator()))) {
    // First gather all instructions that can be shuffled. Don't take
    // terminator.
    AliveInsts.insert({InsertIdx, &I});
    AliveInstsLookup.insert({&I, InsertIdx++});
    // Then remove these instructions from the block
    I.removeFromParent();
  }

  // Shuffle these instructions using topological sort.
  // Returns false if all current instruction's dependencies in this block have
  // been shuffled. If so, this instruction can be shuffled too.
  auto hasAliveParent = [&AliveInsts, &AliveInstsLookup](size_t Index) {
    for (Value *O : AliveInsts[Index]->operands()) {
      Instruction *P = dyn_cast<Instruction>(O);
      if (P && AliveInstsLookup.count(P))
        return true;
    }
    return false;
  };
  // Get all alive instructions that depend on the current instruction.
  // Takes Instruction* instead of index because the instruction is already
  // shuffled.
  auto getAliveChildren = [&AliveInstsLookup](Instruction *I) {
    SmallSetVector<size_t, 8> Children;
    for (Value *U : I->users()) {
      Instruction *P = dyn_cast<Instruction>(U);
      if (P && AliveInstsLookup.count(P))
        Children.insert(AliveInstsLookup[P]);
    }
    return Children;
  };
  SmallSet<size_t, 8> RootIndices;
  SmallVector<Instruction *, 8> Insts;
  for (const auto &[Index, Inst] : AliveInsts) {
    if (!hasAliveParent(Index))
      RootIndices.insert(Index);
  }
  // Topological sort by randomly selecting a node without a parent, or root.
  while (!RootIndices.empty()) {
    auto RS = makeSampler<size_t>(IB.Rand);
    for (size_t RootIdx : RootIndices)
      RS.sample(RootIdx, 1);
    size_t RootIdx = RS.getSelection();

    RootIndices.erase(RootIdx);
    Instruction *Root = AliveInsts[RootIdx];
    AliveInsts.erase(RootIdx);
    AliveInstsLookup.erase(Root);
    Insts.push_back(Root);

    for (size_t Child : getAliveChildren(Root)) {
      if (!hasAliveParent(Child)) {
        RootIndices.insert(Child);
      }
    }
  }

  Instruction *Terminator = BB.getTerminator();
  // Then put instructions back.
  for (Instruction *I : Insts) {
    I->insertBefore(Terminator);
  }
}

std::unique_ptr<Module> llvm::parseModule(const uint8_t *Data, size_t Size,
                                          LLVMContext &Context) {

  if (Size <= 1)
    // We get bogus data given an empty corpus - just create a new module.
    return std::make_unique<Module>("M", Context);

  auto Buffer = MemoryBuffer::getMemBuffer(
      StringRef(reinterpret_cast<const char *>(Data), Size), "Fuzzer input",
      /*RequiresNullTerminator=*/false);

  SMDiagnostic Err;
  auto M = parseBitcodeFile(Buffer->getMemBufferRef(), Context);
  if (Error E = M.takeError()) {
    errs() << toString(std::move(E)) << "\n";
    return nullptr;
  }
  return std::move(M.get());
}

size_t llvm::writeModule(const Module &M, uint8_t *Dest, size_t MaxSize) {
  std::string Buf;
  {
    raw_string_ostream OS(Buf);
    WriteBitcodeToFile(M, OS);
  }
  if (Buf.size() > MaxSize)
    return 0;
  memcpy(Dest, Buf.data(), Buf.size());
  return Buf.size();
}

std::unique_ptr<Module> llvm::parseAndVerify(const uint8_t *Data, size_t Size,
                                             LLVMContext &Context) {
  auto M = parseModule(Data, Size, Context);
  if (!M || verifyModule(*M, &errs()))
    return nullptr;

  return M;
}

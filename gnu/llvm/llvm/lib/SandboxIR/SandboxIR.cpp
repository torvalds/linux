//===- SandboxIR.cpp - A transactional overlay IR on top of LLVM IR -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/SandboxIR/SandboxIR.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"
#include <sstream>

using namespace llvm::sandboxir;

Value *Use::get() const { return Ctx->getValue(LLVMUse->get()); }

void Use::set(Value *V) { LLVMUse->set(V->Val); }

unsigned Use::getOperandNo() const { return Usr->getUseOperandNo(*this); }

#ifndef NDEBUG
void Use::dump(raw_ostream &OS) const {
  Value *Def = nullptr;
  if (LLVMUse == nullptr)
    OS << "<null> LLVM Use! ";
  else
    Def = Ctx->getValue(LLVMUse->get());
  OS << "Def:  ";
  if (Def == nullptr)
    OS << "NULL";
  else
    OS << *Def;
  OS << "\n";

  OS << "User: ";
  if (Usr == nullptr)
    OS << "NULL";
  else
    OS << *Usr;
  OS << "\n";

  OS << "OperandNo: ";
  if (Usr == nullptr)
    OS << "N/A";
  else
    OS << getOperandNo();
  OS << "\n";
}

void Use::dump() const { dump(dbgs()); }
#endif // NDEBUG

Use OperandUseIterator::operator*() const { return Use; }

OperandUseIterator &OperandUseIterator::operator++() {
  assert(Use.LLVMUse != nullptr && "Already at end!");
  User *User = Use.getUser();
  Use = User->getOperandUseInternal(Use.getOperandNo() + 1, /*Verify=*/false);
  return *this;
}

UserUseIterator &UserUseIterator::operator++() {
  // Get the corresponding llvm::Use, get the next in the list, and update the
  // sandboxir::Use.
  llvm::Use *&LLVMUse = Use.LLVMUse;
  assert(LLVMUse != nullptr && "Already at end!");
  LLVMUse = LLVMUse->getNext();
  if (LLVMUse == nullptr) {
    Use.Usr = nullptr;
    return *this;
  }
  auto *Ctx = Use.Ctx;
  auto *LLVMUser = LLVMUse->getUser();
  Use.Usr = cast_or_null<sandboxir::User>(Ctx->getValue(LLVMUser));
  return *this;
}

Value::Value(ClassID SubclassID, llvm::Value *Val, Context &Ctx)
    : SubclassID(SubclassID), Val(Val), Ctx(Ctx) {
#ifndef NDEBUG
  UID = Ctx.getNumValues();
#endif
}

Value::use_iterator Value::use_begin() {
  llvm::Use *LLVMUse = nullptr;
  if (Val->use_begin() != Val->use_end())
    LLVMUse = &*Val->use_begin();
  User *User = LLVMUse != nullptr ? cast_or_null<sandboxir::User>(Ctx.getValue(
                                        Val->use_begin()->getUser()))
                                  : nullptr;
  return use_iterator(Use(LLVMUse, User, Ctx));
}

Value::user_iterator Value::user_begin() {
  auto UseBegin = Val->use_begin();
  auto UseEnd = Val->use_end();
  bool AtEnd = UseBegin == UseEnd;
  llvm::Use *LLVMUse = AtEnd ? nullptr : &*UseBegin;
  User *User =
      AtEnd ? nullptr
            : cast_or_null<sandboxir::User>(Ctx.getValue(&*LLVMUse->getUser()));
  return user_iterator(Use(LLVMUse, User, Ctx), UseToUser());
}

unsigned Value::getNumUses() const { return range_size(Val->users()); }

void Value::replaceUsesWithIf(
    Value *OtherV, llvm::function_ref<bool(const Use &)> ShouldReplace) {
  assert(getType() == OtherV->getType() && "Can't replace with different type");
  llvm::Value *OtherVal = OtherV->Val;
  // We are delegating RUWIf to LLVM IR's RUWIf.
  Val->replaceUsesWithIf(
      OtherVal, [&ShouldReplace, this](llvm::Use &LLVMUse) -> bool {
        User *DstU = cast_or_null<User>(Ctx.getValue(LLVMUse.getUser()));
        if (DstU == nullptr)
          return false;
        Use UseToReplace(&LLVMUse, DstU, Ctx);
        if (!ShouldReplace(UseToReplace))
          return false;
        auto &Tracker = Ctx.getTracker();
        if (Tracker.isTracking())
          Tracker.track(std::make_unique<UseSet>(UseToReplace, Tracker));
        return true;
      });
}

void Value::replaceAllUsesWith(Value *Other) {
  assert(getType() == Other->getType() &&
         "Replacing with Value of different type!");
  auto &Tracker = Ctx.getTracker();
  if (Tracker.isTracking()) {
    for (auto Use : uses())
      Tracker.track(std::make_unique<UseSet>(Use, Tracker));
  }
  // We are delegating RAUW to LLVM IR's RAUW.
  Val->replaceAllUsesWith(Other->Val);
}

#ifndef NDEBUG
std::string Value::getUid() const {
  std::stringstream SS;
  SS << "SB" << UID << ".";
  return SS.str();
}

void Value::dumpCommonHeader(raw_ostream &OS) const {
  OS << getUid() << " " << getSubclassIDStr(SubclassID) << " ";
}

void Value::dumpCommonFooter(raw_ostream &OS) const {
  OS.indent(2) << "Val: ";
  if (Val)
    OS << *Val;
  else
    OS << "NULL";
  OS << "\n";
}

void Value::dumpCommonPrefix(raw_ostream &OS) const {
  if (Val)
    OS << *Val;
  else
    OS << "NULL ";
}

void Value::dumpCommonSuffix(raw_ostream &OS) const {
  OS << " ; " << getUid() << " (" << getSubclassIDStr(SubclassID) << ")";
}

void Value::printAsOperandCommon(raw_ostream &OS) const {
  if (Val)
    Val->printAsOperand(OS);
  else
    OS << "NULL ";
}

void Argument::printAsOperand(raw_ostream &OS) const {
  printAsOperandCommon(OS);
}
void Argument::dump(raw_ostream &OS) const {
  dumpCommonPrefix(OS);
  dumpCommonSuffix(OS);
}
void Argument::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif // NDEBUG

Use User::getOperandUseDefault(unsigned OpIdx, bool Verify) const {
  assert((!Verify || OpIdx < getNumOperands()) && "Out of bounds!");
  assert(isa<llvm::User>(Val) && "Non-users have no operands!");
  llvm::Use *LLVMUse;
  if (OpIdx != getNumOperands())
    LLVMUse = &cast<llvm::User>(Val)->getOperandUse(OpIdx);
  else
    LLVMUse = cast<llvm::User>(Val)->op_end();
  return Use(LLVMUse, const_cast<User *>(this), Ctx);
}

#ifndef NDEBUG
void User::verifyUserOfLLVMUse(const llvm::Use &Use) const {
  assert(Ctx.getValue(Use.getUser()) == this &&
         "Use not found in this SBUser's operands!");
}
#endif

bool User::classof(const Value *From) {
  switch (From->getSubclassID()) {
#define DEF_VALUE(ID, CLASS)
#define DEF_USER(ID, CLASS)                                                    \
  case ClassID::ID:                                                            \
    return true;
#define DEF_INSTR(ID, OPC, CLASS)                                              \
  case ClassID::ID:                                                            \
    return true;
#include "llvm/SandboxIR/SandboxIRValues.def"
  default:
    return false;
  }
}

void User::setOperand(unsigned OperandIdx, Value *Operand) {
  assert(isa<llvm::User>(Val) && "No operands!");
  auto &Tracker = Ctx.getTracker();
  if (Tracker.isTracking())
    Tracker.track(std::make_unique<UseSet>(getOperandUse(OperandIdx), Tracker));
  // We are delegating to llvm::User::setOperand().
  cast<llvm::User>(Val)->setOperand(OperandIdx, Operand->Val);
}

bool User::replaceUsesOfWith(Value *FromV, Value *ToV) {
  auto &Tracker = Ctx.getTracker();
  if (Tracker.isTracking()) {
    for (auto OpIdx : seq<unsigned>(0, getNumOperands())) {
      auto Use = getOperandUse(OpIdx);
      if (Use.get() == FromV)
        Tracker.track(std::make_unique<UseSet>(Use, Tracker));
    }
  }
  // We are delegating RUOW to LLVM IR's RUOW.
  return cast<llvm::User>(Val)->replaceUsesOfWith(FromV->Val, ToV->Val);
}

#ifndef NDEBUG
void User::dumpCommonHeader(raw_ostream &OS) const {
  Value::dumpCommonHeader(OS);
  // TODO: This is incomplete
}
#endif // NDEBUG

BBIterator &BBIterator::operator++() {
  auto ItE = BB->end();
  assert(It != ItE && "Already at end!");
  ++It;
  if (It == ItE)
    return *this;
  Instruction &NextI = *cast<sandboxir::Instruction>(Ctx->getValue(&*It));
  unsigned Num = NextI.getNumOfIRInstrs();
  assert(Num > 0 && "Bad getNumOfIRInstrs()");
  It = std::next(It, Num - 1);
  return *this;
}

BBIterator &BBIterator::operator--() {
  assert(It != BB->begin() && "Already at begin!");
  if (It == BB->end()) {
    --It;
    return *this;
  }
  Instruction &CurrI = **this;
  unsigned Num = CurrI.getNumOfIRInstrs();
  assert(Num > 0 && "Bad getNumOfIRInstrs()");
  assert(std::prev(It, Num - 1) != BB->begin() && "Already at begin!");
  It = std::prev(It, Num);
  return *this;
}

const char *Instruction::getOpcodeName(Opcode Opc) {
  switch (Opc) {
#define DEF_VALUE(ID, CLASS)
#define DEF_USER(ID, CLASS)
#define OP(OPC)                                                                \
  case Opcode::OPC:                                                            \
    return #OPC;
#define DEF_INSTR(ID, OPC, CLASS) OPC
#include "llvm/SandboxIR/SandboxIRValues.def"
  }
  llvm_unreachable("Unknown Opcode");
}

llvm::Instruction *Instruction::getTopmostLLVMInstruction() const {
  Instruction *Prev = getPrevNode();
  if (Prev == nullptr) {
    // If at top of the BB, return the first BB instruction.
    return &*cast<llvm::BasicBlock>(getParent()->Val)->begin();
  }
  // Else get the Previous sandbox IR instruction's bottom IR instruction and
  // return its successor.
  llvm::Instruction *PrevBotI = cast<llvm::Instruction>(Prev->Val);
  return PrevBotI->getNextNode();
}

BBIterator Instruction::getIterator() const {
  auto *I = cast<llvm::Instruction>(Val);
  return BasicBlock::iterator(I->getParent(), I->getIterator(), &Ctx);
}

Instruction *Instruction::getNextNode() const {
  assert(getParent() != nullptr && "Detached!");
  assert(getIterator() != getParent()->end() && "Already at end!");
  // `Val` is the bottom-most LLVM IR instruction. Get the next in the chain,
  // and get the corresponding sandboxir Instruction that maps to it. This works
  // even for SandboxIR Instructions that map to more than one LLVM Instruction.
  auto *LLVMI = cast<llvm::Instruction>(Val);
  assert(LLVMI->getParent() != nullptr && "LLVM IR instr is detached!");
  auto *NextLLVMI = LLVMI->getNextNode();
  auto *NextI = cast_or_null<Instruction>(Ctx.getValue(NextLLVMI));
  if (NextI == nullptr)
    return nullptr;
  return NextI;
}

Instruction *Instruction::getPrevNode() const {
  assert(getParent() != nullptr && "Detached!");
  auto It = getIterator();
  if (It != getParent()->begin())
    return std::prev(getIterator()).get();
  return nullptr;
}

void Instruction::removeFromParent() {
  auto &Tracker = Ctx.getTracker();
  if (Tracker.isTracking())
    Tracker.track(std::make_unique<RemoveFromParent>(this, Tracker));

  // Detach all the LLVM IR instructions from their parent BB.
  for (llvm::Instruction *I : getLLVMInstrs())
    I->removeFromParent();
}

void Instruction::eraseFromParent() {
  assert(users().empty() && "Still connected to users, can't erase!");
  std::unique_ptr<Value> Detached = Ctx.detach(this);
  auto LLVMInstrs = getLLVMInstrs();

  auto &Tracker = Ctx.getTracker();
  if (Tracker.isTracking()) {
    Tracker.track(
        std::make_unique<EraseFromParent>(std::move(Detached), Tracker));
    // We don't actually delete the IR instruction, because then it would be
    // impossible to bring it back from the dead at the same memory location.
    // Instead we remove it from its BB and track its current location.
    for (llvm::Instruction *I : LLVMInstrs)
      I->removeFromParent();
    // TODO: Multi-instructions need special treatment because some of the
    // references are internal to the instruction.
    for (llvm::Instruction *I : LLVMInstrs)
      I->dropAllReferences();
  } else {
    // Erase in reverse to avoid erasing nstructions with attached uses.
    for (llvm::Instruction *I : reverse(LLVMInstrs))
      I->eraseFromParent();
  }
}

void Instruction::moveBefore(BasicBlock &BB, const BBIterator &WhereIt) {
  if (std::next(getIterator()) == WhereIt)
    // Destination is same as origin, nothing to do.
    return;

  auto &Tracker = Ctx.getTracker();
  if (Tracker.isTracking())
    Tracker.track(std::make_unique<MoveInstr>(this, Tracker));

  auto *LLVMBB = cast<llvm::BasicBlock>(BB.Val);
  llvm::BasicBlock::iterator It;
  if (WhereIt == BB.end()) {
    It = LLVMBB->end();
  } else {
    Instruction *WhereI = &*WhereIt;
    It = WhereI->getTopmostLLVMInstruction()->getIterator();
  }
  // TODO: Move this to the verifier of sandboxir::Instruction.
  assert(is_sorted(getLLVMInstrs(),
                   [](auto *I1, auto *I2) { return I1->comesBefore(I2); }) &&
         "Expected program order!");
  // Do the actual move in LLVM IR.
  for (auto *I : getLLVMInstrs())
    I->moveBefore(*LLVMBB, It);
}

void Instruction::insertBefore(Instruction *BeforeI) {
  llvm::Instruction *BeforeTopI = BeforeI->getTopmostLLVMInstruction();
  // TODO: Move this to the verifier of sandboxir::Instruction.
  assert(is_sorted(getLLVMInstrs(),
                   [](auto *I1, auto *I2) { return I1->comesBefore(I2); }) &&
         "Expected program order!");
  // Insert the LLVM IR Instructions in program order.
  for (llvm::Instruction *I : getLLVMInstrs())
    I->insertBefore(BeforeTopI);
}

void Instruction::insertAfter(Instruction *AfterI) {
  insertInto(AfterI->getParent(), std::next(AfterI->getIterator()));
}

void Instruction::insertInto(BasicBlock *BB, const BBIterator &WhereIt) {
  llvm::BasicBlock *LLVMBB = cast<llvm::BasicBlock>(BB->Val);
  llvm::Instruction *LLVMBeforeI;
  llvm::BasicBlock::iterator LLVMBeforeIt;
  if (WhereIt != BB->end()) {
    Instruction *BeforeI = &*WhereIt;
    LLVMBeforeI = BeforeI->getTopmostLLVMInstruction();
    LLVMBeforeIt = LLVMBeforeI->getIterator();
  } else {
    LLVMBeforeI = nullptr;
    LLVMBeforeIt = LLVMBB->end();
  }
  // Insert the LLVM IR Instructions in program order.
  for (llvm::Instruction *I : getLLVMInstrs())
    I->insertInto(LLVMBB, LLVMBeforeIt);
}

BasicBlock *Instruction::getParent() const {
  // Get the LLVM IR Instruction that this maps to, get its parent, and get the
  // corresponding sandboxir::BasicBlock by looking it up in sandboxir::Context.
  auto *BB = cast<llvm::Instruction>(Val)->getParent();
  if (BB == nullptr)
    return nullptr;
  return cast<BasicBlock>(Ctx.getValue(BB));
}

bool Instruction::classof(const sandboxir::Value *From) {
  switch (From->getSubclassID()) {
#define DEF_INSTR(ID, OPC, CLASS)                                              \
  case ClassID::ID:                                                            \
    return true;
#include "llvm/SandboxIR/SandboxIRValues.def"
  default:
    return false;
  }
}

#ifndef NDEBUG
void Instruction::dump(raw_ostream &OS) const {
  OS << "Unimplemented! Please override dump().";
}
void Instruction::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif // NDEBUG

Value *SelectInst::createCommon(Value *Cond, Value *True, Value *False,
                                const Twine &Name, IRBuilder<> &Builder,
                                Context &Ctx) {
  llvm::Value *NewV =
      Builder.CreateSelect(Cond->Val, True->Val, False->Val, Name);
  if (auto *NewSI = dyn_cast<llvm::SelectInst>(NewV))
    return Ctx.createSelectInst(NewSI);
  assert(isa<llvm::Constant>(NewV) && "Expected constant");
  return Ctx.getOrCreateConstant(cast<llvm::Constant>(NewV));
}

Value *SelectInst::create(Value *Cond, Value *True, Value *False,
                          Instruction *InsertBefore, Context &Ctx,
                          const Twine &Name) {
  llvm::Instruction *BeforeIR = InsertBefore->getTopmostLLVMInstruction();
  auto &Builder = Ctx.getLLVMIRBuilder();
  Builder.SetInsertPoint(BeforeIR);
  return createCommon(Cond, True, False, Name, Builder, Ctx);
}

Value *SelectInst::create(Value *Cond, Value *True, Value *False,
                          BasicBlock *InsertAtEnd, Context &Ctx,
                          const Twine &Name) {
  auto *IRInsertAtEnd = cast<llvm::BasicBlock>(InsertAtEnd->Val);
  auto &Builder = Ctx.getLLVMIRBuilder();
  Builder.SetInsertPoint(IRInsertAtEnd);
  return createCommon(Cond, True, False, Name, Builder, Ctx);
}

bool SelectInst::classof(const Value *From) {
  return From->getSubclassID() == ClassID::Select;
}

#ifndef NDEBUG
void SelectInst::dump(raw_ostream &OS) const {
  dumpCommonPrefix(OS);
  dumpCommonSuffix(OS);
}

void SelectInst::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif // NDEBUG

LoadInst *LoadInst::create(Type *Ty, Value *Ptr, MaybeAlign Align,
                           Instruction *InsertBefore, Context &Ctx,
                           const Twine &Name) {
  llvm::Instruction *BeforeIR = InsertBefore->getTopmostLLVMInstruction();
  auto &Builder = Ctx.getLLVMIRBuilder();
  Builder.SetInsertPoint(BeforeIR);
  auto *NewLI = Builder.CreateAlignedLoad(Ty, Ptr->Val, Align,
                                          /*isVolatile=*/false, Name);
  auto *NewSBI = Ctx.createLoadInst(NewLI);
  return NewSBI;
}

LoadInst *LoadInst::create(Type *Ty, Value *Ptr, MaybeAlign Align,
                           BasicBlock *InsertAtEnd, Context &Ctx,
                           const Twine &Name) {
  auto &Builder = Ctx.getLLVMIRBuilder();
  Builder.SetInsertPoint(cast<llvm::BasicBlock>(InsertAtEnd->Val));
  auto *NewLI = Builder.CreateAlignedLoad(Ty, Ptr->Val, Align,
                                          /*isVolatile=*/false, Name);
  auto *NewSBI = Ctx.createLoadInst(NewLI);
  return NewSBI;
}

bool LoadInst::classof(const Value *From) {
  return From->getSubclassID() == ClassID::Load;
}

Value *LoadInst::getPointerOperand() const {
  return Ctx.getValue(cast<llvm::LoadInst>(Val)->getPointerOperand());
}

#ifndef NDEBUG
void LoadInst::dump(raw_ostream &OS) const {
  dumpCommonPrefix(OS);
  dumpCommonSuffix(OS);
}

void LoadInst::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif // NDEBUG
StoreInst *StoreInst::create(Value *V, Value *Ptr, MaybeAlign Align,
                             Instruction *InsertBefore, Context &Ctx) {
  llvm::Instruction *BeforeIR = InsertBefore->getTopmostLLVMInstruction();
  auto &Builder = Ctx.getLLVMIRBuilder();
  Builder.SetInsertPoint(BeforeIR);
  auto *NewSI =
      Builder.CreateAlignedStore(V->Val, Ptr->Val, Align, /*isVolatile=*/false);
  auto *NewSBI = Ctx.createStoreInst(NewSI);
  return NewSBI;
}
StoreInst *StoreInst::create(Value *V, Value *Ptr, MaybeAlign Align,
                             BasicBlock *InsertAtEnd, Context &Ctx) {
  auto *InsertAtEndIR = cast<llvm::BasicBlock>(InsertAtEnd->Val);
  auto &Builder = Ctx.getLLVMIRBuilder();
  Builder.SetInsertPoint(InsertAtEndIR);
  auto *NewSI =
      Builder.CreateAlignedStore(V->Val, Ptr->Val, Align, /*isVolatile=*/false);
  auto *NewSBI = Ctx.createStoreInst(NewSI);
  return NewSBI;
}

bool StoreInst::classof(const Value *From) {
  return From->getSubclassID() == ClassID::Store;
}

Value *StoreInst::getValueOperand() const {
  return Ctx.getValue(cast<llvm::StoreInst>(Val)->getValueOperand());
}

Value *StoreInst::getPointerOperand() const {
  return Ctx.getValue(cast<llvm::StoreInst>(Val)->getPointerOperand());
}

#ifndef NDEBUG
void StoreInst::dump(raw_ostream &OS) const {
  dumpCommonPrefix(OS);
  dumpCommonSuffix(OS);
}

void StoreInst::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif // NDEBUG

ReturnInst *ReturnInst::createCommon(Value *RetVal, IRBuilder<> &Builder,
                                     Context &Ctx) {
  llvm::ReturnInst *NewRI;
  if (RetVal != nullptr)
    NewRI = Builder.CreateRet(RetVal->Val);
  else
    NewRI = Builder.CreateRetVoid();
  return Ctx.createReturnInst(NewRI);
}

ReturnInst *ReturnInst::create(Value *RetVal, Instruction *InsertBefore,
                               Context &Ctx) {
  llvm::Instruction *BeforeIR = InsertBefore->getTopmostLLVMInstruction();
  auto &Builder = Ctx.getLLVMIRBuilder();
  Builder.SetInsertPoint(BeforeIR);
  return createCommon(RetVal, Builder, Ctx);
}

ReturnInst *ReturnInst::create(Value *RetVal, BasicBlock *InsertAtEnd,
                               Context &Ctx) {
  auto &Builder = Ctx.getLLVMIRBuilder();
  Builder.SetInsertPoint(cast<llvm::BasicBlock>(InsertAtEnd->Val));
  return createCommon(RetVal, Builder, Ctx);
}

Value *ReturnInst::getReturnValue() const {
  auto *LLVMRetVal = cast<llvm::ReturnInst>(Val)->getReturnValue();
  return LLVMRetVal != nullptr ? Ctx.getValue(LLVMRetVal) : nullptr;
}

#ifndef NDEBUG
void ReturnInst::dump(raw_ostream &OS) const {
  dumpCommonPrefix(OS);
  dumpCommonSuffix(OS);
}

void ReturnInst::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}

void OpaqueInst::dump(raw_ostream &OS) const {
  dumpCommonPrefix(OS);
  dumpCommonSuffix(OS);
}

void OpaqueInst::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif // NDEBUG

Constant *Constant::createInt(Type *Ty, uint64_t V, Context &Ctx,
                              bool IsSigned) {
  llvm::Constant *LLVMC = llvm::ConstantInt::get(Ty, V, IsSigned);
  return Ctx.getOrCreateConstant(LLVMC);
}

#ifndef NDEBUG
void Constant::dump(raw_ostream &OS) const {
  dumpCommonPrefix(OS);
  dumpCommonSuffix(OS);
}

void Constant::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}

void Function::dumpNameAndArgs(raw_ostream &OS) const {
  auto *F = cast<llvm::Function>(Val);
  OS << *F->getReturnType() << " @" << F->getName() << "(";
  interleave(
      F->args(),
      [this, &OS](const llvm::Argument &LLVMArg) {
        auto *SBArg = cast_or_null<Argument>(Ctx.getValue(&LLVMArg));
        if (SBArg == nullptr)
          OS << "NULL";
        else
          SBArg->printAsOperand(OS);
      },
      [&] { OS << ", "; });
  OS << ")";
}
void Function::dump(raw_ostream &OS) const {
  dumpNameAndArgs(OS);
  OS << " {\n";
  auto *LLVMF = cast<llvm::Function>(Val);
  interleave(
      *LLVMF,
      [this, &OS](const llvm::BasicBlock &LLVMBB) {
        auto *BB = cast_or_null<BasicBlock>(Ctx.getValue(&LLVMBB));
        if (BB == nullptr)
          OS << "NULL";
        else
          OS << *BB;
      },
      [&OS] { OS << "\n"; });
  OS << "}\n";
}
void Function::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif // NDEBUG

BasicBlock::iterator::pointer
BasicBlock::iterator::getInstr(llvm::BasicBlock::iterator It) const {
  return cast_or_null<Instruction>(Ctx->getValue(&*It));
}

std::unique_ptr<Value> Context::detachLLVMValue(llvm::Value *V) {
  std::unique_ptr<Value> Erased;
  auto It = LLVMValueToValueMap.find(V);
  if (It != LLVMValueToValueMap.end()) {
    auto *Val = It->second.release();
    Erased = std::unique_ptr<Value>(Val);
    LLVMValueToValueMap.erase(It);
  }
  return Erased;
}

std::unique_ptr<Value> Context::detach(Value *V) {
  assert(V->getSubclassID() != Value::ClassID::Constant &&
         "Can't detach a constant!");
  assert(V->getSubclassID() != Value::ClassID::User && "Can't detach a user!");
  return detachLLVMValue(V->Val);
}

Value *Context::registerValue(std::unique_ptr<Value> &&VPtr) {
  assert(VPtr->getSubclassID() != Value::ClassID::User &&
         "Can't register a user!");
  Value *V = VPtr.get();
  [[maybe_unused]] auto Pair =
      LLVMValueToValueMap.insert({VPtr->Val, std::move(VPtr)});
  assert(Pair.second && "Already exists!");
  return V;
}

Value *Context::getOrCreateValueInternal(llvm::Value *LLVMV, llvm::User *U) {
  auto Pair = LLVMValueToValueMap.insert({LLVMV, nullptr});
  auto It = Pair.first;
  if (!Pair.second)
    return It->second.get();

  if (auto *C = dyn_cast<llvm::Constant>(LLVMV)) {
    It->second = std::unique_ptr<Constant>(new Constant(C, *this));
    auto *NewC = It->second.get();
    for (llvm::Value *COp : C->operands())
      getOrCreateValueInternal(COp, C);
    return NewC;
  }
  if (auto *Arg = dyn_cast<llvm::Argument>(LLVMV)) {
    It->second = std::unique_ptr<Argument>(new Argument(Arg, *this));
    return It->second.get();
  }
  if (auto *BB = dyn_cast<llvm::BasicBlock>(LLVMV)) {
    assert(isa<BlockAddress>(U) &&
           "This won't create a SBBB, don't call this function directly!");
    if (auto *SBBB = getValue(BB))
      return SBBB;
    return nullptr;
  }
  assert(isa<llvm::Instruction>(LLVMV) && "Expected Instruction");

  switch (cast<llvm::Instruction>(LLVMV)->getOpcode()) {
  case llvm::Instruction::Select: {
    auto *LLVMSel = cast<llvm::SelectInst>(LLVMV);
    It->second = std::unique_ptr<SelectInst>(new SelectInst(LLVMSel, *this));
    return It->second.get();
  }
  case llvm::Instruction::Load: {
    auto *LLVMLd = cast<llvm::LoadInst>(LLVMV);
    It->second = std::unique_ptr<LoadInst>(new LoadInst(LLVMLd, *this));
    return It->second.get();
  }
  case llvm::Instruction::Store: {
    auto *LLVMSt = cast<llvm::StoreInst>(LLVMV);
    It->second = std::unique_ptr<StoreInst>(new StoreInst(LLVMSt, *this));
    return It->second.get();
  }
  case llvm::Instruction::Ret: {
    auto *LLVMRet = cast<llvm::ReturnInst>(LLVMV);
    It->second = std::unique_ptr<ReturnInst>(new ReturnInst(LLVMRet, *this));
    return It->second.get();
  }
  default:
    break;
  }

  It->second = std::unique_ptr<OpaqueInst>(
      new OpaqueInst(cast<llvm::Instruction>(LLVMV), *this));
  return It->second.get();
}

BasicBlock *Context::createBasicBlock(llvm::BasicBlock *LLVMBB) {
  assert(getValue(LLVMBB) == nullptr && "Already exists!");
  auto NewBBPtr = std::unique_ptr<BasicBlock>(new BasicBlock(LLVMBB, *this));
  auto *BB = cast<BasicBlock>(registerValue(std::move(NewBBPtr)));
  // Create SandboxIR for BB's body.
  BB->buildBasicBlockFromLLVMIR(LLVMBB);
  return BB;
}

SelectInst *Context::createSelectInst(llvm::SelectInst *SI) {
  auto NewPtr = std::unique_ptr<SelectInst>(new SelectInst(SI, *this));
  return cast<SelectInst>(registerValue(std::move(NewPtr)));
}

LoadInst *Context::createLoadInst(llvm::LoadInst *LI) {
  auto NewPtr = std::unique_ptr<LoadInst>(new LoadInst(LI, *this));
  return cast<LoadInst>(registerValue(std::move(NewPtr)));
}

StoreInst *Context::createStoreInst(llvm::StoreInst *SI) {
  auto NewPtr = std::unique_ptr<StoreInst>(new StoreInst(SI, *this));
  return cast<StoreInst>(registerValue(std::move(NewPtr)));
}

ReturnInst *Context::createReturnInst(llvm::ReturnInst *I) {
  auto NewPtr = std::unique_ptr<ReturnInst>(new ReturnInst(I, *this));
  return cast<ReturnInst>(registerValue(std::move(NewPtr)));
}

Value *Context::getValue(llvm::Value *V) const {
  auto It = LLVMValueToValueMap.find(V);
  if (It != LLVMValueToValueMap.end())
    return It->second.get();
  return nullptr;
}

Function *Context::createFunction(llvm::Function *F) {
  assert(getValue(F) == nullptr && "Already exists!");
  auto NewFPtr = std::unique_ptr<Function>(new Function(F, *this));
  // Create arguments.
  for (auto &Arg : F->args())
    getOrCreateArgument(&Arg);
  // Create BBs.
  for (auto &BB : *F)
    createBasicBlock(&BB);
  auto *SBF = cast<Function>(registerValue(std::move(NewFPtr)));
  return SBF;
}

Function *BasicBlock::getParent() const {
  auto *BB = cast<llvm::BasicBlock>(Val);
  auto *F = BB->getParent();
  if (F == nullptr)
    // Detached
    return nullptr;
  return cast_or_null<Function>(Ctx.getValue(F));
}

void BasicBlock::buildBasicBlockFromLLVMIR(llvm::BasicBlock *LLVMBB) {
  for (llvm::Instruction &IRef : reverse(*LLVMBB)) {
    llvm::Instruction *I = &IRef;
    Ctx.getOrCreateValue(I);
    for (auto [OpIdx, Op] : enumerate(I->operands())) {
      // Skip instruction's label operands
      if (isa<llvm::BasicBlock>(Op))
        continue;
      // Skip metadata
      if (isa<llvm::MetadataAsValue>(Op))
        continue;
      // Skip asm
      if (isa<llvm::InlineAsm>(Op))
        continue;
      Ctx.getOrCreateValue(Op);
    }
  }
#if !defined(NDEBUG) && defined(SBVEC_EXPENSIVE_CHECKS)
  verify();
#endif
}

BasicBlock::iterator BasicBlock::begin() const {
  llvm::BasicBlock *BB = cast<llvm::BasicBlock>(Val);
  llvm::BasicBlock::iterator It = BB->begin();
  if (!BB->empty()) {
    auto *V = Ctx.getValue(&*BB->begin());
    assert(V != nullptr && "No SandboxIR for BB->begin()!");
    auto *I = cast<Instruction>(V);
    unsigned Num = I->getNumOfIRInstrs();
    assert(Num >= 1u && "Bad getNumOfIRInstrs()");
    It = std::next(It, Num - 1);
  }
  return iterator(BB, It, &Ctx);
}

Instruction *BasicBlock::getTerminator() const {
  auto *TerminatorV =
      Ctx.getValue(cast<llvm::BasicBlock>(Val)->getTerminator());
  return cast_or_null<Instruction>(TerminatorV);
}

Instruction &BasicBlock::front() const {
  auto *BB = cast<llvm::BasicBlock>(Val);
  assert(!BB->empty() && "Empty block!");
  auto *SBI = cast<Instruction>(getContext().getValue(&*BB->begin()));
  assert(SBI != nullptr && "Expected Instr!");
  return *SBI;
}

Instruction &BasicBlock::back() const {
  auto *BB = cast<llvm::BasicBlock>(Val);
  assert(!BB->empty() && "Empty block!");
  auto *SBI = cast<Instruction>(getContext().getValue(&*BB->rbegin()));
  assert(SBI != nullptr && "Expected Instr!");
  return *SBI;
}

#ifndef NDEBUG
void BasicBlock::dump(raw_ostream &OS) const {
  llvm::BasicBlock *BB = cast<llvm::BasicBlock>(Val);
  const auto &Name = BB->getName();
  OS << Name;
  if (!Name.empty())
    OS << ":\n";
  // If there are Instructions in the BB that are not mapped to SandboxIR, then
  // use a crash-proof dump.
  if (any_of(*BB, [this](llvm::Instruction &I) {
        return Ctx.getValue(&I) == nullptr;
      })) {
    OS << "<Crash-proof mode!>\n";
    DenseSet<Instruction *> Visited;
    for (llvm::Instruction &IRef : *BB) {
      Value *SBV = Ctx.getValue(&IRef);
      if (SBV == nullptr)
        OS << IRef << " *** No SandboxIR ***\n";
      else {
        auto *SBI = dyn_cast<Instruction>(SBV);
        if (SBI == nullptr) {
          OS << IRef << " *** Not a SBInstruction!!! ***\n";
        } else {
          if (Visited.insert(SBI).second)
            OS << *SBI << "\n";
        }
      }
    }
  } else {
    for (auto &SBI : *this) {
      SBI.dump(OS);
      OS << "\n";
    }
  }
}
void BasicBlock::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif // NDEBUG

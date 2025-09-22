//===- Tracker.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/SandboxIR/Tracker.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/SandboxIR/SandboxIR.h"
#include <sstream>

using namespace llvm::sandboxir;

IRChangeBase::IRChangeBase(Tracker &Parent) : Parent(Parent) {
#ifndef NDEBUG
  assert(!Parent.InMiddleOfCreatingChange &&
         "We are in the middle of creating another change!");
  if (Parent.isTracking())
    Parent.InMiddleOfCreatingChange = true;
#endif // NDEBUG
}

#ifndef NDEBUG
unsigned IRChangeBase::getIdx() const {
  auto It =
      find_if(Parent.Changes, [this](auto &Ptr) { return Ptr.get() == this; });
  return It - Parent.Changes.begin();
}

void UseSet::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif // NDEBUG

Tracker::~Tracker() {
  assert(Changes.empty() && "You must accept or revert changes!");
}

EraseFromParent::EraseFromParent(std::unique_ptr<sandboxir::Value> &&ErasedIPtr,
                                 Tracker &Tracker)
    : IRChangeBase(Tracker), ErasedIPtr(std::move(ErasedIPtr)) {
  auto *I = cast<Instruction>(this->ErasedIPtr.get());
  auto LLVMInstrs = I->getLLVMInstrs();
  // Iterate in reverse program order.
  for (auto *LLVMI : reverse(LLVMInstrs)) {
    SmallVector<llvm::Value *> Operands;
    Operands.reserve(LLVMI->getNumOperands());
    for (auto [OpNum, Use] : enumerate(LLVMI->operands()))
      Operands.push_back(Use.get());
    InstrData.push_back({Operands, LLVMI});
  }
  assert(is_sorted(InstrData,
                   [](const auto &D0, const auto &D1) {
                     return D0.LLVMI->comesBefore(D1.LLVMI);
                   }) &&
         "Expected reverse program order!");
  auto *BotLLVMI = cast<llvm::Instruction>(I->Val);
  if (BotLLVMI->getNextNode() != nullptr)
    NextLLVMIOrBB = BotLLVMI->getNextNode();
  else
    NextLLVMIOrBB = BotLLVMI->getParent();
}

void EraseFromParent::accept() {
  for (const auto &IData : InstrData)
    IData.LLVMI->deleteValue();
}

void EraseFromParent::revert() {
  // Place the bottom-most instruction first.
  auto [Operands, BotLLVMI] = InstrData[0];
  if (auto *NextLLVMI = NextLLVMIOrBB.dyn_cast<llvm::Instruction *>()) {
    BotLLVMI->insertBefore(NextLLVMI);
  } else {
    auto *LLVMBB = NextLLVMIOrBB.get<llvm::BasicBlock *>();
    BotLLVMI->insertInto(LLVMBB, LLVMBB->end());
  }
  for (auto [OpNum, Op] : enumerate(Operands))
    BotLLVMI->setOperand(OpNum, Op);

  // Go over the rest of the instructions and stack them on top.
  for (auto [Operands, LLVMI] : drop_begin(InstrData)) {
    LLVMI->insertBefore(BotLLVMI);
    for (auto [OpNum, Op] : enumerate(Operands))
      LLVMI->setOperand(OpNum, Op);
    BotLLVMI = LLVMI;
  }
  Parent.getContext().registerValue(std::move(ErasedIPtr));
}

#ifndef NDEBUG
void EraseFromParent::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif // NDEBUG

RemoveFromParent::RemoveFromParent(Instruction *RemovedI, Tracker &Tracker)
    : IRChangeBase(Tracker), RemovedI(RemovedI) {
  if (auto *NextI = RemovedI->getNextNode())
    NextInstrOrBB = NextI;
  else
    NextInstrOrBB = RemovedI->getParent();
}

void RemoveFromParent::revert() {
  if (auto *NextI = NextInstrOrBB.dyn_cast<Instruction *>()) {
    RemovedI->insertBefore(NextI);
  } else {
    auto *BB = NextInstrOrBB.get<BasicBlock *>();
    RemovedI->insertInto(BB, BB->end());
  }
}

#ifndef NDEBUG
void RemoveFromParent::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif

MoveInstr::MoveInstr(Instruction *MovedI, Tracker &Tracker)
    : IRChangeBase(Tracker), MovedI(MovedI) {
  if (auto *NextI = MovedI->getNextNode())
    NextInstrOrBB = NextI;
  else
    NextInstrOrBB = MovedI->getParent();
}

void MoveInstr::revert() {
  if (auto *NextI = NextInstrOrBB.dyn_cast<Instruction *>()) {
    MovedI->moveBefore(NextI);
  } else {
    auto *BB = NextInstrOrBB.get<BasicBlock *>();
    MovedI->moveBefore(*BB, BB->end());
  }
}

#ifndef NDEBUG
void MoveInstr::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif

void Tracker::track(std::unique_ptr<IRChangeBase> &&Change) {
  assert(State == TrackerState::Record && "The tracker should be tracking!");
  Changes.push_back(std::move(Change));

#ifndef NDEBUG
  InMiddleOfCreatingChange = false;
#endif
}

void Tracker::save() { State = TrackerState::Record; }

void Tracker::revert() {
  assert(State == TrackerState::Record && "Forgot to save()!");
  State = TrackerState::Disabled;
  for (auto &Change : reverse(Changes))
    Change->revert();
  Changes.clear();
}

void Tracker::accept() {
  assert(State == TrackerState::Record && "Forgot to save()!");
  State = TrackerState::Disabled;
  for (auto &Change : Changes)
    Change->accept();
  Changes.clear();
}

#ifndef NDEBUG
void Tracker::dump(raw_ostream &OS) const {
  for (const auto &ChangePtr : Changes) {
    ChangePtr->dump(OS);
    OS << "\n";
  }
}
void Tracker::dump() const {
  dump(dbgs());
  dbgs() << "\n";
}
#endif // NDEBUG

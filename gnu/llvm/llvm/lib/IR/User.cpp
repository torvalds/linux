//===-- User.cpp - Implement the User class -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/User.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IntrinsicInst.h"

namespace llvm {
class BasicBlock;

//===----------------------------------------------------------------------===//
//                                 User Class
//===----------------------------------------------------------------------===//

bool User::replaceUsesOfWith(Value *From, Value *To) {
  bool Changed = false;
  if (From == To) return Changed;   // Duh what?

  assert((!isa<Constant>(this) || isa<GlobalValue>(this)) &&
         "Cannot call User::replaceUsesOfWith on a constant!");

  for (unsigned i = 0, E = getNumOperands(); i != E; ++i)
    if (getOperand(i) == From) {  // Is This operand is pointing to oldval?
      // The side effects of this setOperand call include linking to
      // "To", adding "this" to the uses list of To, and
      // most importantly, removing "this" from the use list of "From".
      setOperand(i, To);
      Changed = true;
    }
  if (auto DVI = dyn_cast_or_null<DbgVariableIntrinsic>(this)) {
    if (is_contained(DVI->location_ops(), From)) {
      DVI->replaceVariableLocationOp(From, To);
      Changed = true;
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
//                         User allocHungoffUses Implementation
//===----------------------------------------------------------------------===//

void User::allocHungoffUses(unsigned N, bool IsPhi) {
  assert(HasHungOffUses && "alloc must have hung off uses");

  static_assert(alignof(Use) >= alignof(BasicBlock *),
                "Alignment is insufficient for 'hung-off-uses' pieces");

  // Allocate the array of Uses
  size_t size = N * sizeof(Use);
  if (IsPhi)
    size += N * sizeof(BasicBlock *);
  Use *Begin = static_cast<Use*>(::operator new(size));
  Use *End = Begin + N;
  setOperandList(Begin);
  for (; Begin != End; Begin++)
    new (Begin) Use(this);
}

void User::growHungoffUses(unsigned NewNumUses, bool IsPhi) {
  assert(HasHungOffUses && "realloc must have hung off uses");

  unsigned OldNumUses = getNumOperands();

  // We don't support shrinking the number of uses.  We wouldn't have enough
  // space to copy the old uses in to the new space.
  assert(NewNumUses > OldNumUses && "realloc must grow num uses");

  Use *OldOps = getOperandList();
  allocHungoffUses(NewNumUses, IsPhi);
  Use *NewOps = getOperandList();

  // Now copy from the old operands list to the new one.
  std::copy(OldOps, OldOps + OldNumUses, NewOps);

  // If this is a Phi, then we need to copy the BB pointers too.
  if (IsPhi) {
    auto *OldPtr = reinterpret_cast<char *>(OldOps + OldNumUses);
    auto *NewPtr = reinterpret_cast<char *>(NewOps + NewNumUses);
    std::copy(OldPtr, OldPtr + (OldNumUses * sizeof(BasicBlock *)), NewPtr);
  }
  Use::zap(OldOps, OldOps + OldNumUses, true);
}


// This is a private struct used by `User` to track the co-allocated descriptor
// section.
struct DescriptorInfo {
  intptr_t SizeInBytes;
};

ArrayRef<const uint8_t> User::getDescriptor() const {
  auto MutableARef = const_cast<User *>(this)->getDescriptor();
  return {MutableARef.begin(), MutableARef.end()};
}

MutableArrayRef<uint8_t> User::getDescriptor() {
  assert(HasDescriptor && "Don't call otherwise!");
  assert(!HasHungOffUses && "Invariant!");

  auto *DI = reinterpret_cast<DescriptorInfo *>(getIntrusiveOperands()) - 1;
  assert(DI->SizeInBytes != 0 && "Should not have had a descriptor otherwise!");

  return MutableArrayRef<uint8_t>(
      reinterpret_cast<uint8_t *>(DI) - DI->SizeInBytes, DI->SizeInBytes);
}

bool User::isDroppable() const {
  return isa<AssumeInst>(this) || isa<PseudoProbeInst>(this);
}

//===----------------------------------------------------------------------===//
//                         User operator new Implementations
//===----------------------------------------------------------------------===//

void *User::allocateFixedOperandUser(size_t Size, unsigned Us,
                                     unsigned DescBytes) {
  assert(Us < (1u << NumUserOperandsBits) && "Too many operands");

  static_assert(sizeof(DescriptorInfo) % sizeof(void *) == 0, "Required below");

  unsigned DescBytesToAllocate =
      DescBytes == 0 ? 0 : (DescBytes + sizeof(DescriptorInfo));
  assert(DescBytesToAllocate % sizeof(void *) == 0 &&
         "We need this to satisfy alignment constraints for Uses");

  uint8_t *Storage = static_cast<uint8_t *>(
      ::operator new(Size + sizeof(Use) * Us + DescBytesToAllocate));
  Use *Start = reinterpret_cast<Use *>(Storage + DescBytesToAllocate);
  Use *End = Start + Us;
  User *Obj = reinterpret_cast<User*>(End);
  Obj->NumUserOperands = Us;
  Obj->HasHungOffUses = false;
  Obj->HasDescriptor = DescBytes != 0;
  for (; Start != End; Start++)
    new (Start) Use(Obj);

  if (DescBytes != 0) {
    auto *DescInfo = reinterpret_cast<DescriptorInfo *>(Storage + DescBytes);
    DescInfo->SizeInBytes = DescBytes;
  }

  return Obj;
}

void *User::operator new(size_t Size, unsigned Us) {
  return allocateFixedOperandUser(Size, Us, 0);
}

void *User::operator new(size_t Size, unsigned Us, unsigned DescBytes) {
  return allocateFixedOperandUser(Size, Us, DescBytes);
}

void *User::operator new(size_t Size) {
  // Allocate space for a single Use*
  void *Storage = ::operator new(Size + sizeof(Use *));
  Use **HungOffOperandList = static_cast<Use **>(Storage);
  User *Obj = reinterpret_cast<User *>(HungOffOperandList + 1);
  Obj->NumUserOperands = 0;
  Obj->HasHungOffUses = true;
  Obj->HasDescriptor = false;
  *HungOffOperandList = nullptr;
  return Obj;
}

//===----------------------------------------------------------------------===//
//                         User operator delete Implementation
//===----------------------------------------------------------------------===//

// Repress memory sanitization, due to use-after-destroy by operator
// delete. Bug report 24578 identifies this issue.
LLVM_NO_SANITIZE_MEMORY_ATTRIBUTE void User::operator delete(void *Usr) {
  // Hung off uses use a single Use* before the User, while other subclasses
  // use a Use[] allocated prior to the user.
  User *Obj = static_cast<User *>(Usr);
  if (Obj->HasHungOffUses) {
    assert(!Obj->HasDescriptor && "not supported!");

    Use **HungOffOperandList = static_cast<Use **>(Usr) - 1;
    // drop the hung off uses.
    Use::zap(*HungOffOperandList, *HungOffOperandList + Obj->NumUserOperands,
             /* Delete */ true);
    ::operator delete(HungOffOperandList);
  } else if (Obj->HasDescriptor) {
    Use *UseBegin = static_cast<Use *>(Usr) - Obj->NumUserOperands;
    Use::zap(UseBegin, UseBegin + Obj->NumUserOperands, /* Delete */ false);

    auto *DI = reinterpret_cast<DescriptorInfo *>(UseBegin) - 1;
    uint8_t *Storage = reinterpret_cast<uint8_t *>(DI) - DI->SizeInBytes;
    ::operator delete(Storage);
  } else {
    Use *Storage = static_cast<Use *>(Usr) - Obj->NumUserOperands;
    Use::zap(Storage, Storage + Obj->NumUserOperands,
             /* Delete */ false);
    ::operator delete(Storage);
  }
}

} // namespace llvm

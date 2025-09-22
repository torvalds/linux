//===- ValueList.cpp - Internal BitcodeReader implementation --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ValueList.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstddef>

using namespace llvm;

Error BitcodeReaderValueList::assignValue(unsigned Idx, Value *V,
                                          unsigned TypeID) {
  if (Idx == size()) {
    push_back(V, TypeID);
    return Error::success();
  }

  if (Idx >= size())
    resize(Idx + 1);

  auto &Old = ValuePtrs[Idx];
  if (!Old.first) {
    Old.first = V;
    Old.second = TypeID;
    return Error::success();
  }

  assert(!isa<Constant>(&*Old.first) && "Shouldn't update constant");
  // If there was a forward reference to this value, replace it.
  Value *PrevVal = Old.first;
  if (PrevVal->getType() != V->getType())
    return createStringError(
        std::errc::illegal_byte_sequence,
        "Assigned value does not match type of forward declaration");
  Old.first->replaceAllUsesWith(V);
  PrevVal->deleteValue();
  return Error::success();
}

Value *BitcodeReaderValueList::getValueFwdRef(unsigned Idx, Type *Ty,
                                              unsigned TyID,
                                              BasicBlock *ConstExprInsertBB) {
  // Bail out for a clearly invalid value.
  if (Idx >= RefsUpperBound)
    return nullptr;

  if (Idx >= size())
    resize(Idx + 1);

  if (Value *V = ValuePtrs[Idx].first) {
    // If the types don't match, it's invalid.
    if (Ty && Ty != V->getType())
      return nullptr;

    Expected<Value *> MaybeV = MaterializeValueFn(Idx, ConstExprInsertBB);
    if (!MaybeV) {
      // TODO: We might want to propagate the precise error message here.
      consumeError(MaybeV.takeError());
      return nullptr;
    }
    return MaybeV.get();
  }

  // No type specified, must be invalid reference.
  if (!Ty)
    return nullptr;

  // Create and return a placeholder, which will later be RAUW'd.
  Value *V = new Argument(Ty);
  ValuePtrs[Idx] = {V, TyID};
  return V;
}

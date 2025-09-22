//===-- Value.cpp -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines support functions for the `Value` type.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/FlowSensitive/Value.h"
#include "clang/Analysis/FlowSensitive/DebugSupport.h"
#include "llvm/Support/Casting.h"

namespace clang {
namespace dataflow {

static bool areEquivalentIndirectionValues(const Value &Val1,
                                           const Value &Val2) {
  if (auto *IndVal1 = dyn_cast<PointerValue>(&Val1)) {
    auto *IndVal2 = cast<PointerValue>(&Val2);
    return &IndVal1->getPointeeLoc() == &IndVal2->getPointeeLoc();
  }
  return false;
}

bool areEquivalentValues(const Value &Val1, const Value &Val2) {
  if (&Val1 == &Val2)
    return true;
  if (Val1.getKind() != Val2.getKind())
    return false;
  // If values are distinct and have properties, we don't consider them equal,
  // leaving equality up to the user model.
  if (!Val1.properties().empty() || !Val2.properties().empty())
    return false;
  if (isa<TopBoolValue>(&Val1))
    return true;
  return areEquivalentIndirectionValues(Val1, Val2);
}

raw_ostream &operator<<(raw_ostream &OS, const Value &Val) {
  switch (Val.getKind()) {
  case Value::Kind::Integer:
    return OS << "Integer(@" << &Val << ")";
  case Value::Kind::Pointer:
    return OS << "Pointer(" << &cast<PointerValue>(Val).getPointeeLoc() << ")";
  case Value::Kind::TopBool:
    return OS << "TopBool(" << cast<TopBoolValue>(Val).getAtom() << ")";
  case Value::Kind::AtomicBool:
    return OS << "AtomicBool(" << cast<AtomicBoolValue>(Val).getAtom() << ")";
  case Value::Kind::FormulaBool:
    return OS << "FormulaBool(" << cast<FormulaBoolValue>(Val).formula() << ")";
  }
  llvm_unreachable("Unknown clang::dataflow::Value::Kind enum");
}

} // namespace dataflow
} // namespace clang

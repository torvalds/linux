//===- Formula.cpp ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/FlowSensitive/Formula.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <type_traits>

namespace clang::dataflow {

const Formula &Formula::create(llvm::BumpPtrAllocator &Alloc, Kind K,
                               ArrayRef<const Formula *> Operands,
                               unsigned Value) {
  assert(Operands.size() == numOperands(K));
  if (Value != 0) // Currently, formulas have values or operands, not both.
    assert(numOperands(K) == 0);
  void *Mem = Alloc.Allocate(sizeof(Formula) +
                                 Operands.size() * sizeof(Operands.front()),
                             alignof(Formula));
  Formula *Result = new (Mem) Formula();
  Result->FormulaKind = K;
  Result->Value = Value;
  // Operands are stored as `const Formula *`s after the formula itself.
  // We don't need to construct an object as pointers are trivial types.
  // Formula is alignas(const Formula *), so alignment is satisfied.
  llvm::copy(Operands, reinterpret_cast<const Formula **>(Result + 1));
  return *Result;
}

static llvm::StringLiteral sigil(Formula::Kind K) {
  switch (K) {
  case Formula::AtomRef:
  case Formula::Literal:
    return "";
  case Formula::Not:
    return "!";
  case Formula::And:
    return " & ";
  case Formula::Or:
    return " | ";
  case Formula::Implies:
    return " => ";
  case Formula::Equal:
    return " = ";
  }
  llvm_unreachable("unhandled formula kind");
}

void Formula::print(llvm::raw_ostream &OS, const AtomNames *Names) const {
  if (Names && kind() == AtomRef)
    if (auto It = Names->find(getAtom()); It != Names->end()) {
      OS << It->second;
      return;
    }

  switch (numOperands(kind())) {
  case 0:
    switch (kind()) {
    case AtomRef:
      OS << getAtom();
      break;
    case Literal:
      OS << (literal() ? "true" : "false");
      break;
    default:
      llvm_unreachable("unhandled formula kind");
    }
    break;
  case 1:
    OS << sigil(kind());
    operands()[0]->print(OS, Names);
    break;
  case 2:
    OS << '(';
    operands()[0]->print(OS, Names);
    OS << sigil(kind());
    operands()[1]->print(OS, Names);
    OS << ')';
    break;
  default:
    llvm_unreachable("unhandled formula arity");
  }
}

} // namespace clang::dataflow
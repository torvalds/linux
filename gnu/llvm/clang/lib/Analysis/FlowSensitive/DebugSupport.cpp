//===- DebugSupport.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines functions which generate more readable forms of data
//  structures used in the dataflow analyses, for debugging purposes.
//
//===----------------------------------------------------------------------===//

#include <utility>

#include "clang/Analysis/FlowSensitive/DebugSupport.h"
#include "clang/Analysis/FlowSensitive/Solver.h"
#include "clang/Analysis/FlowSensitive/Value.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"

namespace clang {
namespace dataflow {

llvm::StringRef debugString(Value::Kind Kind) {
  switch (Kind) {
  case Value::Kind::Integer:
    return "Integer";
  case Value::Kind::Pointer:
    return "Pointer";
  case Value::Kind::AtomicBool:
    return "AtomicBool";
  case Value::Kind::TopBool:
    return "TopBool";
  case Value::Kind::FormulaBool:
    return "FormulaBool";
  }
  llvm_unreachable("Unhandled value kind");
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                              Solver::Result::Assignment Assignment) {
  switch (Assignment) {
  case Solver::Result::Assignment::AssignedFalse:
    return OS << "False";
  case Solver::Result::Assignment::AssignedTrue:
    return OS << "True";
  }
  llvm_unreachable("Booleans can only be assigned true/false");
}

llvm::StringRef debugString(Solver::Result::Status Status) {
  switch (Status) {
  case Solver::Result::Status::Satisfiable:
    return "Satisfiable";
  case Solver::Result::Status::Unsatisfiable:
    return "Unsatisfiable";
  case Solver::Result::Status::TimedOut:
    return "TimedOut";
  }
  llvm_unreachable("Unhandled SAT check result status");
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const Solver::Result &R) {
  OS << debugString(R.getStatus()) << "\n";
  if (auto Solution = R.getSolution()) {
    std::vector<std::pair<Atom, Solver::Result::Assignment>> Sorted = {
        Solution->begin(), Solution->end()};
    llvm::sort(Sorted);
    for (const auto &Entry : Sorted)
      OS << Entry.first << " = " << Entry.second << "\n";
  }
  return OS;
}

} // namespace dataflow
} // namespace clang

//===- ConstraintSytem.cpp - A system of linear constraints. ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ConstraintSystem.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Debug.h"

#include <string>

using namespace llvm;

#define DEBUG_TYPE "constraint-system"

bool ConstraintSystem::eliminateUsingFM() {
  // Implementation of Fourier–Motzkin elimination, with some tricks from the
  // paper Pugh, William. "The Omega test: a fast and practical integer
  // programming algorithm for dependence
  //  analysis."
  // Supercomputing'91: Proceedings of the 1991 ACM/
  // IEEE conference on Supercomputing. IEEE, 1991.
  assert(!Constraints.empty() &&
         "should only be called for non-empty constraint systems");

  unsigned LastIdx = NumVariables - 1;

  // First, either remove the variable in place if it is 0 or add the row to
  // RemainingRows and remove it from the system.
  SmallVector<SmallVector<Entry, 8>, 4> RemainingRows;
  for (unsigned R1 = 0; R1 < Constraints.size();) {
    SmallVector<Entry, 8> &Row1 = Constraints[R1];
    if (getLastCoefficient(Row1, LastIdx) == 0) {
      if (Row1.size() > 0 && Row1.back().Id == LastIdx)
        Row1.pop_back();
      R1++;
    } else {
      std::swap(Constraints[R1], Constraints.back());
      RemainingRows.push_back(std::move(Constraints.back()));
      Constraints.pop_back();
    }
  }

  // Process rows where the variable is != 0.
  unsigned NumRemainingConstraints = RemainingRows.size();
  for (unsigned R1 = 0; R1 < NumRemainingConstraints; R1++) {
    // FIXME do not use copy
    for (unsigned R2 = R1 + 1; R2 < NumRemainingConstraints; R2++) {
      if (R1 == R2)
        continue;

      int64_t UpperLast = getLastCoefficient(RemainingRows[R2], LastIdx);
      int64_t LowerLast = getLastCoefficient(RemainingRows[R1], LastIdx);
      assert(
          UpperLast != 0 && LowerLast != 0 &&
          "RemainingRows should only contain rows where the variable is != 0");

      if ((LowerLast < 0 && UpperLast < 0) || (LowerLast > 0 && UpperLast > 0))
        continue;

      unsigned LowerR = R1;
      unsigned UpperR = R2;
      if (UpperLast < 0) {
        std::swap(LowerR, UpperR);
        std::swap(LowerLast, UpperLast);
      }

      SmallVector<Entry, 8> NR;
      unsigned IdxUpper = 0;
      unsigned IdxLower = 0;
      auto &LowerRow = RemainingRows[LowerR];
      auto &UpperRow = RemainingRows[UpperR];
      while (true) {
        if (IdxUpper >= UpperRow.size() || IdxLower >= LowerRow.size())
          break;
        int64_t M1, M2, N;
        int64_t UpperV = 0;
        int64_t LowerV = 0;
        uint16_t CurrentId = std::numeric_limits<uint16_t>::max();
        if (IdxUpper < UpperRow.size()) {
          CurrentId = std::min(UpperRow[IdxUpper].Id, CurrentId);
        }
        if (IdxLower < LowerRow.size()) {
          CurrentId = std::min(LowerRow[IdxLower].Id, CurrentId);
        }

        if (IdxUpper < UpperRow.size() && UpperRow[IdxUpper].Id == CurrentId) {
          UpperV = UpperRow[IdxUpper].Coefficient;
          IdxUpper++;
        }

        if (MulOverflow(UpperV, -1 * LowerLast, M1))
          return false;
        if (IdxLower < LowerRow.size() && LowerRow[IdxLower].Id == CurrentId) {
          LowerV = LowerRow[IdxLower].Coefficient;
          IdxLower++;
        }

        if (MulOverflow(LowerV, UpperLast, M2))
          return false;
        if (AddOverflow(M1, M2, N))
          return false;
        if (N == 0)
          continue;
        NR.emplace_back(N, CurrentId);
      }
      if (NR.empty())
        continue;
      Constraints.push_back(std::move(NR));
      // Give up if the new system gets too big.
      if (Constraints.size() > 500)
        return false;
    }
  }
  NumVariables -= 1;

  return true;
}

bool ConstraintSystem::mayHaveSolutionImpl() {
  while (!Constraints.empty() && NumVariables > 1) {
    if (!eliminateUsingFM())
      return true;
  }

  if (Constraints.empty() || NumVariables > 1)
    return true;

  return all_of(Constraints, [](auto &R) {
    if (R.empty())
      return true;
    if (R[0].Id == 0)
      return R[0].Coefficient >= 0;
    return true;
  });
}

SmallVector<std::string> ConstraintSystem::getVarNamesList() const {
  SmallVector<std::string> Names(Value2Index.size(), "");
#ifndef NDEBUG
  for (auto &[V, Index] : Value2Index) {
    std::string OperandName;
    if (V->getName().empty())
      OperandName = V->getNameOrAsOperand();
    else
      OperandName = std::string("%") + V->getName().str();
    Names[Index - 1] = OperandName;
  }
#endif
  return Names;
}

void ConstraintSystem::dump() const {
#ifndef NDEBUG
  if (Constraints.empty())
    return;
  SmallVector<std::string> Names = getVarNamesList();
  for (const auto &Row : Constraints) {
    SmallVector<std::string, 16> Parts;
    for (const Entry &E : Row) {
      if (E.Id >= NumVariables)
        break;
      if (E.Id == 0)
        continue;
      std::string Coefficient;
      if (E.Coefficient != 1)
        Coefficient = std::to_string(E.Coefficient) + " * ";
      Parts.push_back(Coefficient + Names[E.Id - 1]);
    }
    // assert(!Parts.empty() && "need to have at least some parts");
    int64_t ConstPart = 0;
    if (Row[0].Id == 0)
      ConstPart = Row[0].Coefficient;
    LLVM_DEBUG(dbgs() << join(Parts, std::string(" + "))
                      << " <= " << std::to_string(ConstPart) << "\n");
  }
#endif
}

bool ConstraintSystem::mayHaveSolution() {
  LLVM_DEBUG(dbgs() << "---\n");
  LLVM_DEBUG(dump());
  bool HasSolution = mayHaveSolutionImpl();
  LLVM_DEBUG(dbgs() << (HasSolution ? "sat" : "unsat") << "\n");
  return HasSolution;
}

bool ConstraintSystem::isConditionImplied(SmallVector<int64_t, 8> R) const {
  // If all variable coefficients are 0, we have 'C >= 0'. If the constant is >=
  // 0, R is always true, regardless of the system.
  if (all_of(ArrayRef(R).drop_front(1), [](int64_t C) { return C == 0; }))
    return R[0] >= 0;

  // If there is no solution with the negation of R added to the system, the
  // condition must hold based on the existing constraints.
  R = ConstraintSystem::negate(R);
  if (R.empty())
    return false;

  auto NewSystem = *this;
  NewSystem.addVariableRow(R);
  return !NewSystem.mayHaveSolution();
}

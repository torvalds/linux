//===- TaggedUnionModeling.h -------------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_TAGGEDUNIONMODELING_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_TAGGEDUNIONMODELING_H

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/ADT/FoldingSet.h"
#include <numeric>

namespace clang::ento::tagged_union_modeling {

// The implementation of all these functions can be found in the file
// StdVariantChecker.cpp under the same directory as this file.

bool isCopyConstructorCall(const CallEvent &Call);
bool isCopyAssignmentCall(const CallEvent &Call);
bool isMoveAssignmentCall(const CallEvent &Call);
bool isMoveConstructorCall(const CallEvent &Call);
bool isStdType(const Type *Type, const std::string &TypeName);
bool isStdVariant(const Type *Type);

// When invalidating regions, we also have to follow that by invalidating the
// corresponding custom data in the program state.
template <class TypeMap>
ProgramStateRef
removeInformationStoredForDeadInstances(const CallEvent &Call,
                                        ProgramStateRef State,
                                        ArrayRef<const MemRegion *> Regions) {
  // If we do not know anything about the call we shall not continue.
  // If the call is happens within a system header it is implementation detail.
  // We should not take it into consideration.
  if (Call.isInSystemHeader())
    return State;

  for (const MemRegion *Region : Regions)
    State = State->remove<TypeMap>(Region);

  return State;
}

template <class TypeMap>
void handleConstructorAndAssignment(const CallEvent &Call, CheckerContext &C,
                                    SVal ThisSVal) {
  ProgramStateRef State = Call.getState();

  if (!State)
    return;

  auto ArgSVal = Call.getArgSVal(0);
  const auto *ThisRegion = ThisSVal.getAsRegion();
  const auto *ArgMemRegion = ArgSVal.getAsRegion();

  // Make changes to the state according to type of constructor/assignment
  bool IsCopy = isCopyConstructorCall(Call) || isCopyAssignmentCall(Call);
  bool IsMove = isMoveConstructorCall(Call) || isMoveAssignmentCall(Call);
  // First we handle copy and move operations
  if (IsCopy || IsMove) {
    const QualType *OtherQType = State->get<TypeMap>(ArgMemRegion);

    // If the argument of a copy constructor or assignment is unknown then
    // we will not know the argument of the copied to object.
    if (!OtherQType) {
      State = State->remove<TypeMap>(ThisRegion);
    } else {
      // When move semantics is used we can only know that the moved from
      // object must be in a destructible state. Other usage of the object
      // than destruction is undefined.
      if (IsMove)
        State = State->remove<TypeMap>(ArgMemRegion);

      State = State->set<TypeMap>(ThisRegion, *OtherQType);
    }
  } else {
    // Value constructor
    auto ArgQType = ArgSVal.getType(C.getASTContext());
    const Type *ArgTypePtr = ArgQType.getTypePtr();

    QualType WoPointer = ArgTypePtr->getPointeeType();
    State = State->set<TypeMap>(ThisRegion, WoPointer);
  }

  C.addTransition(State);
}

} // namespace clang::ento::tagged_union_modeling

#endif // LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_TAGGEDUNIONMODELING_H

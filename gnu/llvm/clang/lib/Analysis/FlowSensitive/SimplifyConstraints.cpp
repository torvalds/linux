//===-- SimplifyConstraints.cpp ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/FlowSensitive/SimplifyConstraints.h"
#include "llvm/ADT/EquivalenceClasses.h"

namespace clang {
namespace dataflow {

// Substitutes all occurrences of a given atom in `F` by a given formula and
// returns the resulting formula.
static const Formula &
substitute(const Formula &F,
           const llvm::DenseMap<Atom, const Formula *> &Substitutions,
           Arena &arena) {
  switch (F.kind()) {
  case Formula::AtomRef:
    if (auto iter = Substitutions.find(F.getAtom());
        iter != Substitutions.end())
      return *iter->second;
    return F;
  case Formula::Literal:
    return F;
  case Formula::Not:
    return arena.makeNot(substitute(*F.operands()[0], Substitutions, arena));
  case Formula::And:
    return arena.makeAnd(substitute(*F.operands()[0], Substitutions, arena),
                         substitute(*F.operands()[1], Substitutions, arena));
  case Formula::Or:
    return arena.makeOr(substitute(*F.operands()[0], Substitutions, arena),
                        substitute(*F.operands()[1], Substitutions, arena));
  case Formula::Implies:
    return arena.makeImplies(
        substitute(*F.operands()[0], Substitutions, arena),
        substitute(*F.operands()[1], Substitutions, arena));
  case Formula::Equal:
    return arena.makeEquals(substitute(*F.operands()[0], Substitutions, arena),
                            substitute(*F.operands()[1], Substitutions, arena));
  }
  llvm_unreachable("Unknown formula kind");
}

// Returns the result of replacing atoms in `Atoms` with the leader of their
// equivalence class in `EquivalentAtoms`.
// Atoms that don't have an equivalence class in `EquivalentAtoms` are inserted
// into it as single-member equivalence classes.
static llvm::DenseSet<Atom>
projectToLeaders(const llvm::DenseSet<Atom> &Atoms,
                 llvm::EquivalenceClasses<Atom> &EquivalentAtoms) {
  llvm::DenseSet<Atom> Result;

  for (Atom Atom : Atoms)
    Result.insert(EquivalentAtoms.getOrInsertLeaderValue(Atom));

  return Result;
}

// Returns the atoms in the equivalence class for the leader identified by
// `LeaderIt`.
static llvm::SmallVector<Atom>
atomsInEquivalenceClass(const llvm::EquivalenceClasses<Atom> &EquivalentAtoms,
                        llvm::EquivalenceClasses<Atom>::iterator LeaderIt) {
  llvm::SmallVector<Atom> Result;
  for (auto MemberIt = EquivalentAtoms.member_begin(LeaderIt);
       MemberIt != EquivalentAtoms.member_end(); ++MemberIt)
    Result.push_back(*MemberIt);
  return Result;
}

void simplifyConstraints(llvm::SetVector<const Formula *> &Constraints,
                         Arena &arena, SimplifyConstraintsInfo *Info) {
  auto contradiction = [&]() {
    Constraints.clear();
    Constraints.insert(&arena.makeLiteral(false));
  };

  llvm::EquivalenceClasses<Atom> EquivalentAtoms;
  llvm::DenseSet<Atom> TrueAtoms;
  llvm::DenseSet<Atom> FalseAtoms;

  while (true) {
    for (const auto *Constraint : Constraints) {
      switch (Constraint->kind()) {
      case Formula::AtomRef:
        TrueAtoms.insert(Constraint->getAtom());
        break;
      case Formula::Not:
        if (Constraint->operands()[0]->kind() == Formula::AtomRef)
          FalseAtoms.insert(Constraint->operands()[0]->getAtom());
        break;
      case Formula::Equal: {
        ArrayRef<const Formula *> operands = Constraint->operands();
        if (operands[0]->kind() == Formula::AtomRef &&
            operands[1]->kind() == Formula::AtomRef) {
          EquivalentAtoms.unionSets(operands[0]->getAtom(),
                                    operands[1]->getAtom());
        }
        break;
      }
      default:
        break;
      }
    }

    TrueAtoms = projectToLeaders(TrueAtoms, EquivalentAtoms);
    FalseAtoms = projectToLeaders(FalseAtoms, EquivalentAtoms);

    llvm::DenseMap<Atom, const Formula *> Substitutions;
    for (auto It = EquivalentAtoms.begin(); It != EquivalentAtoms.end(); ++It) {
      Atom TheAtom = It->getData();
      Atom Leader = EquivalentAtoms.getLeaderValue(TheAtom);
      if (TrueAtoms.contains(Leader)) {
        if (FalseAtoms.contains(Leader)) {
          contradiction();
          return;
        }
        Substitutions.insert({TheAtom, &arena.makeLiteral(true)});
      } else if (FalseAtoms.contains(Leader)) {
        Substitutions.insert({TheAtom, &arena.makeLiteral(false)});
      } else if (TheAtom != Leader) {
        Substitutions.insert({TheAtom, &arena.makeAtomRef(Leader)});
      }
    }

    llvm::SetVector<const Formula *> NewConstraints;
    for (const auto *Constraint : Constraints) {
      const Formula &NewConstraint =
          substitute(*Constraint, Substitutions, arena);
      if (NewConstraint.isLiteral(true))
        continue;
      if (NewConstraint.isLiteral(false)) {
        contradiction();
        return;
      }
      if (NewConstraint.kind() == Formula::And) {
        NewConstraints.insert(NewConstraint.operands()[0]);
        NewConstraints.insert(NewConstraint.operands()[1]);
        continue;
      }
      NewConstraints.insert(&NewConstraint);
    }

    if (NewConstraints == Constraints)
      break;
    Constraints = std::move(NewConstraints);
  }

  if (Info) {
    for (auto It = EquivalentAtoms.begin(), End = EquivalentAtoms.end();
         It != End; ++It) {
      if (!It->isLeader())
        continue;
      Atom At = *EquivalentAtoms.findLeader(It);
      if (TrueAtoms.contains(At) || FalseAtoms.contains(At))
        continue;
      llvm::SmallVector<Atom> Atoms =
          atomsInEquivalenceClass(EquivalentAtoms, It);
      if (Atoms.size() == 1)
        continue;
      std::sort(Atoms.begin(), Atoms.end());
      Info->EquivalentAtoms.push_back(std::move(Atoms));
    }
    for (Atom At : TrueAtoms)
      Info->TrueAtoms.append(atomsInEquivalenceClass(
          EquivalentAtoms, EquivalentAtoms.findValue(At)));
    std::sort(Info->TrueAtoms.begin(), Info->TrueAtoms.end());
    for (Atom At : FalseAtoms)
      Info->FalseAtoms.append(atomsInEquivalenceClass(
          EquivalentAtoms, EquivalentAtoms.findValue(At)));
    std::sort(Info->FalseAtoms.begin(), Info->FalseAtoms.end());
  }
}

} // namespace dataflow
} // namespace clang

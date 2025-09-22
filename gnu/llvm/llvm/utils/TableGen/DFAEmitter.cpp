//===- DFAEmitter.cpp - Finite state automaton emitter --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class can produce a generic deterministic finite state automaton (DFA),
// given a set of possible states and transitions.
//
// The input transitions can be nondeterministic - this class will produce the
// deterministic equivalent state machine.
//
// The generated code can run the DFA and produce an accepted / not accepted
// state and also produce, given a sequence of transitions that results in an
// accepted state, the sequence of intermediate states. This is useful if the
// initial automaton was nondeterministic - it allows mapping back from the DFA
// to the NFA.
//
//===----------------------------------------------------------------------===//

#include "DFAEmitter.h"
#include "Basic/SequenceToOffsetTable.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/UniqueVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cassert>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

#define DEBUG_TYPE "dfa-emitter"

using namespace llvm;

//===----------------------------------------------------------------------===//
// DfaEmitter implementation. This is independent of the GenAutomaton backend.
//===----------------------------------------------------------------------===//

void DfaEmitter::addTransition(state_type From, state_type To, action_type A) {
  Actions.insert(A);
  NfaStates.insert(From);
  NfaStates.insert(To);
  NfaTransitions[{From, A}].push_back(To);
  ++NumNfaTransitions;
}

void DfaEmitter::visitDfaState(const DfaState &DS) {
  // For every possible action...
  auto FromId = DfaStates.idFor(DS);
  for (action_type A : Actions) {
    DfaState NewStates;
    DfaTransitionInfo TI;
    // For every represented state, word pair in the original NFA...
    for (state_type FromState : DS) {
      // If this action is possible from this state add the transitioned-to
      // states to NewStates.
      auto I = NfaTransitions.find({FromState, A});
      if (I == NfaTransitions.end())
        continue;
      for (state_type &ToState : I->second) {
        NewStates.push_back(ToState);
        TI.emplace_back(FromState, ToState);
      }
    }
    if (NewStates.empty())
      continue;
    // Sort and unique.
    sort(NewStates);
    NewStates.erase(llvm::unique(NewStates), NewStates.end());
    sort(TI);
    TI.erase(llvm::unique(TI), TI.end());
    unsigned ToId = DfaStates.insert(NewStates);
    DfaTransitions.emplace(std::pair(FromId, A), std::pair(ToId, TI));
  }
}

void DfaEmitter::constructDfa() {
  DfaState Initial(1, /*NFA initial state=*/0);
  DfaStates.insert(Initial);

  // Note that UniqueVector starts indices at 1, not zero.
  unsigned DfaStateId = 1;
  while (DfaStateId <= DfaStates.size()) {
    DfaState S = DfaStates[DfaStateId];
    visitDfaState(S);
    DfaStateId++;
  }
}

void DfaEmitter::emit(StringRef Name, raw_ostream &OS) {
  constructDfa();

  OS << "// Input NFA has " << NfaStates.size() << " states with "
     << NumNfaTransitions << " transitions.\n";
  OS << "// Generated DFA has " << DfaStates.size() << " states with "
     << DfaTransitions.size() << " transitions.\n\n";

  // Implementation note: We don't bake a simple std::pair<> here as it requires
  // significantly more effort to parse. A simple test with a large array of
  // struct-pairs (N=100000) took clang-10 6s to parse. The same array of
  // std::pair<uint64_t, uint64_t> took 242s. Instead we allow the user to
  // define the pair type.
  //
  // FIXME: It may make sense to emit these as ULEB sequences instead of
  // pairs of uint64_t.
  OS << "// A zero-terminated sequence of NFA state transitions. Every DFA\n";
  OS << "// transition implies a set of NFA transitions. These are referred\n";
  OS << "// to by index in " << Name << "Transitions[].\n";

  SequenceToOffsetTable<DfaTransitionInfo> Table;
  std::map<DfaTransitionInfo, unsigned> EmittedIndices;
  for (auto &T : DfaTransitions)
    Table.add(T.second.second);
  Table.layout();
  OS << "const std::array<NfaStatePair, " << Table.size() << "> " << Name
     << "TransitionInfo = {{\n";
  Table.emit(
      OS,
      [](raw_ostream &OS, std::pair<uint64_t, uint64_t> P) {
        OS << "{" << P.first << ", " << P.second << "}";
      },
      "{0ULL, 0ULL}");

  OS << "}};\n\n";

  OS << "// A transition in the generated " << Name << " DFA.\n";
  OS << "struct " << Name << "Transition {\n";
  OS << "  unsigned FromDfaState; // The transitioned-from DFA state.\n";
  OS << "  ";
  printActionType(OS);
  OS << " Action;       // The input symbol that causes this transition.\n";
  OS << "  unsigned ToDfaState;   // The transitioned-to DFA state.\n";
  OS << "  unsigned InfoIdx;      // Start index into " << Name
     << "TransitionInfo.\n";
  OS << "};\n\n";

  OS << "// A table of DFA transitions, ordered by {FromDfaState, Action}.\n";
  OS << "// The initial state is 1, not zero.\n";
  OS << "const std::array<" << Name << "Transition, " << DfaTransitions.size()
     << "> " << Name << "Transitions = {{\n";
  for (auto &KV : DfaTransitions) {
    dfa_state_type From = KV.first.first;
    dfa_state_type To = KV.second.first;
    action_type A = KV.first.second;
    unsigned InfoIdx = Table.get(KV.second.second);
    OS << "  {" << From << ", ";
    printActionValue(A, OS);
    OS << ", " << To << ", " << InfoIdx << "},\n";
  }
  OS << "\n}};\n\n";
}

void DfaEmitter::printActionType(raw_ostream &OS) { OS << "uint64_t"; }

void DfaEmitter::printActionValue(action_type A, raw_ostream &OS) { OS << A; }

//===----------------------------------------------------------------------===//
// AutomatonEmitter implementation
//===----------------------------------------------------------------------===//

namespace {

using Action = std::variant<Record *, unsigned, std::string>;
using ActionTuple = std::vector<Action>;
class Automaton;

class Transition {
  uint64_t NewState;
  // The tuple of actions that causes this transition.
  ActionTuple Actions;
  // The types of the actions; this is the same across all transitions.
  SmallVector<std::string, 4> Types;

public:
  Transition(Record *R, Automaton *Parent);
  const ActionTuple &getActions() { return Actions; }
  SmallVector<std::string, 4> getTypes() { return Types; }

  bool canTransitionFrom(uint64_t State);
  uint64_t transitionFrom(uint64_t State);
};

class Automaton {
  RecordKeeper &Records;
  Record *R;
  std::vector<Transition> Transitions;
  /// All possible action tuples, uniqued.
  UniqueVector<ActionTuple> Actions;
  /// The fields within each Transition object to find the action symbols.
  std::vector<StringRef> ActionSymbolFields;

public:
  Automaton(RecordKeeper &Records, Record *R);
  void emit(raw_ostream &OS);

  ArrayRef<StringRef> getActionSymbolFields() { return ActionSymbolFields; }
  /// If the type of action A has been overridden (there exists a field
  /// "TypeOf_A") return that, otherwise return the empty string.
  StringRef getActionSymbolType(StringRef A);
};

class AutomatonEmitter {
  RecordKeeper &Records;

public:
  AutomatonEmitter(RecordKeeper &R) : Records(R) {}
  void run(raw_ostream &OS);
};

/// A DfaEmitter implementation that can print our variant action type.
class CustomDfaEmitter : public DfaEmitter {
  const UniqueVector<ActionTuple> &Actions;
  std::string TypeName;

public:
  CustomDfaEmitter(const UniqueVector<ActionTuple> &Actions, StringRef TypeName)
      : Actions(Actions), TypeName(TypeName) {}

  void printActionType(raw_ostream &OS) override;
  void printActionValue(action_type A, raw_ostream &OS) override;
};
} // namespace

void AutomatonEmitter::run(raw_ostream &OS) {
  for (Record *R : Records.getAllDerivedDefinitions("GenericAutomaton")) {
    Automaton A(Records, R);
    OS << "#ifdef GET_" << R->getName() << "_DECL\n";
    A.emit(OS);
    OS << "#endif  // GET_" << R->getName() << "_DECL\n";
  }
}

Automaton::Automaton(RecordKeeper &Records, Record *R)
    : Records(Records), R(R) {
  LLVM_DEBUG(dbgs() << "Emitting automaton for " << R->getName() << "\n");
  ActionSymbolFields = R->getValueAsListOfStrings("SymbolFields");
}

void Automaton::emit(raw_ostream &OS) {
  StringRef TransitionClass = R->getValueAsString("TransitionClass");
  for (Record *T : Records.getAllDerivedDefinitions(TransitionClass)) {
    assert(T->isSubClassOf("Transition"));
    Transitions.emplace_back(T, this);
    Actions.insert(Transitions.back().getActions());
  }

  LLVM_DEBUG(dbgs() << "  Action alphabet cardinality: " << Actions.size()
                    << "\n");
  LLVM_DEBUG(dbgs() << "  Each state has " << Transitions.size()
                    << " potential transitions.\n");

  StringRef Name = R->getName();

  CustomDfaEmitter Emitter(Actions, std::string(Name) + "Action");
  // Starting from the initial state, build up a list of possible states and
  // transitions.
  std::deque<uint64_t> Worklist(1, 0);
  std::set<uint64_t> SeenStates;
  unsigned NumTransitions = 0;
  SeenStates.insert(Worklist.front());
  while (!Worklist.empty()) {
    uint64_t State = Worklist.front();
    Worklist.pop_front();
    for (Transition &T : Transitions) {
      if (!T.canTransitionFrom(State))
        continue;
      uint64_t NewState = T.transitionFrom(State);
      if (SeenStates.emplace(NewState).second)
        Worklist.emplace_back(NewState);
      ++NumTransitions;
      Emitter.addTransition(State, NewState, Actions.idFor(T.getActions()));
    }
  }
  LLVM_DEBUG(dbgs() << "  NFA automaton has " << SeenStates.size()
                    << " states with " << NumTransitions << " transitions.\n");
  (void)NumTransitions;

  const auto &ActionTypes = Transitions.back().getTypes();
  OS << "// The type of an action in the " << Name << " automaton.\n";
  if (ActionTypes.size() == 1) {
    OS << "using " << Name << "Action = " << ActionTypes[0] << ";\n";
  } else {
    OS << "using " << Name << "Action = std::tuple<" << join(ActionTypes, ", ")
       << ">;\n";
  }
  OS << "\n";

  Emitter.emit(Name, OS);
}

StringRef Automaton::getActionSymbolType(StringRef A) {
  Twine Ty = "TypeOf_" + A;
  if (!R->getValue(Ty.str()))
    return "";
  return R->getValueAsString(Ty.str());
}

Transition::Transition(Record *R, Automaton *Parent) {
  BitsInit *NewStateInit = R->getValueAsBitsInit("NewState");
  NewState = 0;
  assert(NewStateInit->getNumBits() <= sizeof(uint64_t) * 8 &&
         "State cannot be represented in 64 bits!");
  for (unsigned I = 0; I < NewStateInit->getNumBits(); ++I) {
    if (auto *Bit = dyn_cast<BitInit>(NewStateInit->getBit(I))) {
      if (Bit->getValue())
        NewState |= 1ULL << I;
    }
  }

  for (StringRef A : Parent->getActionSymbolFields()) {
    RecordVal *SymbolV = R->getValue(A);
    if (auto *Ty = dyn_cast<RecordRecTy>(SymbolV->getType())) {
      Actions.emplace_back(R->getValueAsDef(A));
      Types.emplace_back(Ty->getAsString());
    } else if (isa<IntRecTy>(SymbolV->getType())) {
      Actions.emplace_back(static_cast<unsigned>(R->getValueAsInt(A)));
      Types.emplace_back("unsigned");
    } else if (isa<StringRecTy>(SymbolV->getType())) {
      Actions.emplace_back(std::string(R->getValueAsString(A)));
      Types.emplace_back("std::string");
    } else {
      report_fatal_error("Unhandled symbol type!");
    }

    StringRef TypeOverride = Parent->getActionSymbolType(A);
    if (!TypeOverride.empty())
      Types.back() = std::string(TypeOverride);
  }
}

bool Transition::canTransitionFrom(uint64_t State) {
  if ((State & NewState) == 0)
    // The bits we want to set are not set;
    return true;
  return false;
}

uint64_t Transition::transitionFrom(uint64_t State) { return State | NewState; }

void CustomDfaEmitter::printActionType(raw_ostream &OS) { OS << TypeName; }

void CustomDfaEmitter::printActionValue(action_type A, raw_ostream &OS) {
  const ActionTuple &AT = Actions[A];
  if (AT.size() > 1)
    OS << "std::tuple(";
  ListSeparator LS;
  for (const auto &SingleAction : AT) {
    OS << LS;
    if (const auto *R = std::get_if<Record *>(&SingleAction))
      OS << (*R)->getName();
    else if (const auto *S = std::get_if<std::string>(&SingleAction))
      OS << '"' << *S << '"';
    else
      OS << std::get<unsigned>(SingleAction);
  }
  if (AT.size() > 1)
    OS << ")";
}

static TableGen::Emitter::OptClass<AutomatonEmitter>
    X("gen-automata", "Generate generic automata");

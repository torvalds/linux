//===--------------------- DfaEmitter.h -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Defines a generic automaton builder. This takes a set of transitions and
// states that represent a nondeterministic finite state automaton (NFA) and
// emits a determinized DFA in a form that include/llvm/Support/Automaton.h can
// drive.
//
// See file llvm/TableGen/Automaton.td for the TableGen API definition.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_DFAEMITTER_H
#define LLVM_UTILS_TABLEGEN_DFAEMITTER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/UniqueVector.h"
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace llvm {

class raw_ostream;
class StringRef;

/// Construct a deterministic finite state automaton from possible
/// nondeterministic state and transition data.
///
/// The state type is a 64-bit unsigned integer. The generated automaton is
/// invariant to the sparsity of the state representation - its size is only
/// a function of the cardinality of the set of states.
///
/// The inputs to this emitter are considered to define a nondeterministic
/// finite state automaton (NFA). This is then converted to a DFA during
/// emission. The emitted tables can be used to by
/// include/llvm/Support/Automaton.h.
class DfaEmitter {
public:
  // The type of an NFA state. The initial state is always zero.
  using state_type = uint64_t;
  // The type of an action.
  using action_type = uint64_t;

  DfaEmitter() = default;
  virtual ~DfaEmitter() = default;

  void addTransition(state_type From, state_type To, action_type A);
  void emit(StringRef Name, raw_ostream &OS);

protected:
  /// Emit the C++ type of an action to OS.
  virtual void printActionType(raw_ostream &OS);
  /// Emit the C++ value of an action A to OS.
  virtual void printActionValue(action_type A, raw_ostream &OS);

private:
  /// The state type of deterministic states. These are only used internally to
  /// this class. This is an ID into the DfaStates UniqueVector.
  using dfa_state_type = unsigned;

  /// The actual representation of a DFA state, which is a union of one or more
  /// NFA states.
  using DfaState = SmallVector<state_type, 4>;

  /// A DFA transition consists of a set of NFA states transitioning to a
  /// new set of NFA states. The DfaTransitionInfo tracks, for every
  /// transitioned-from NFA state, a set of valid transitioned-to states.
  ///
  /// Emission of this transition relation allows algorithmic determination of
  /// the possible candidate NFA paths taken under a given input sequence to
  /// reach a given DFA state.
  using DfaTransitionInfo = SmallVector<std::pair<state_type, state_type>, 4>;

  /// The set of all possible actions.
  std::set<action_type> Actions;

  /// The set of nondeterministic transitions. A state-action pair can
  /// transition to multiple target states.
  std::map<std::pair<state_type, action_type>, std::vector<state_type>>
      NfaTransitions;
  std::set<state_type> NfaStates;
  unsigned NumNfaTransitions = 0;

  /// The set of deterministic states. DfaStates.getId(DfaState) returns an ID,
  /// which is dfa_state_type. Note that because UniqueVector reserves state
  /// zero, the initial DFA state is always 1.
  UniqueVector<DfaState> DfaStates;
  /// The set of deterministic transitions. A state-action pair has only a
  /// single target state.
  std::map<std::pair<dfa_state_type, action_type>,
           std::pair<dfa_state_type, DfaTransitionInfo>>
      DfaTransitions;

  /// Visit all NFA states and construct the DFA.
  void constructDfa();
  /// Visit a single DFA state and construct all possible transitions to new DFA
  /// states.
  void visitDfaState(const DfaState &DS);
};

} // namespace llvm

#endif

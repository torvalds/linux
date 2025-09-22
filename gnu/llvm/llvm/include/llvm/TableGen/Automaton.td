//===- Automaton.td ----------------------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the key top-level classes needed to produce a reasonably
// generic finite-state automaton.
//
//===----------------------------------------------------------------------===//

// Define a record inheriting from GenericAutomaton to generate a reasonably
// generic finite-state automaton over a set of actions and states.
//
// This automaton is defined by:
//   1) a state space (explicit, always bits<32>).
//   2) a set of input symbols (actions, explicit) and
//   3) a transition function from state + action -> state.
//
// A theoretical automaton is defined by <Q, S, d, q0, F>:
//   Q: A set of possible states.
//   S: (sigma) The input alphabet.
//   d: (delta) The transition function f(q in Q, s in S) -> q' in Q.
//   F: The set of final (accepting) states.
//
// Because generating all possible states is tedious, we instead define the
// transition function only and crawl all reachable states starting from the
// initial state with all inputs under all transitions until termination.
//
// We define F = S, that is, all valid states are accepting.
//
// To ensure the generation of the automaton terminates, the state transitions
// are defined as a lattice (meaning every transitioned-to state is more
// specific than the transitioned-from state, for some definition of specificity).
// Concretely a transition may set one or more bits in the state that were
// previously zero to one. If any bit was not zero, the transition is invalid.
//
// Instead of defining all possible states (which would be cumbersome), the user
// provides a set of possible Transitions from state A, consuming an input
// symbol A to state B. The Transition object transforms state A to state B and
// acts as a predicate. This means the state space can be discovered by crawling
// all the possible transitions until none are valid.
//
// This automaton is considered to be nondeterministic, meaning that multiple
// transitions can occur from any (state, action) pair. The generated automaton
// is determinized, meaning that is executes in O(k) time where k is the input
// sequence length.
//
// In addition to a generated automaton that determines if a sequence of inputs
// is accepted or not, a table is emitted that allows determining a plausible
// sequence of states traversed to accept that input.
class GenericAutomaton {
  // Name of a class that inherits from Transition. All records inheriting from
  // this class will be considered when constructing the automaton.
  string TransitionClass;

  // Names of fields within TransitionClass that define the action symbol. This
  // defines the action as an N-tuple.
  //
  // Each symbol field can be of class, int, string or code type.
  //   If the type of a field is a class, the Record's name is used verbatim
  //     in C++ and the class name is used as the C++ type name.
  //   If the type of a field is a string, code or int, that is also used
  //     verbatim in C++.
  //
  // To override the C++ type name for field F, define a field called TypeOf_F.
  // This should be a string that will be used verbatim in C++.
  //
  // As an example, to define a 2-tuple with an enum and a string, one might:
  //   def MyTransition : Transition {
  //     MyEnum S1;
  //     int S2;
  //   }
  //   def MyAutomaton : GenericAutomaton }{
  //     let TransitionClass = "Transition";
  //     let SymbolFields = ["S1", "S2"];
  //     let TypeOf_S1 = "MyEnumInCxxKind";
  //   }
  list<string> SymbolFields;
}

// All transitions inherit from Transition.
class Transition {
  // A transition S' = T(S) is valid if, for every set bit in NewState, the
  // corresponding bit in S is clear. That is:
  //   def T(S):
  //     S' = S | NewState
  //     return S' if S' != S else Failure
  //
  // The automaton generator uses this property to crawl the set of possible
  // transitions from a starting state of 0b0.
  bits<32> NewState;
}

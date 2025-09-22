//===-- llvm/ADT/CombinationGenerator.h ------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Combination generator.
///
/// Example: given input {{0, 1}, {2}, {3, 4}} it will produce the following
/// combinations: {0, 2, 3}, {0, 2, 4}, {1, 2, 3}, {1, 2, 4}.
///
/// It is useful to think of input as vector-of-vectors, where the
/// outer vector is the variable space, and inner vector is choice space.
/// The number of choices for each variable can be different.
///
/// As for implementation, it is useful to think of this as a weird number,
/// where each digit (==variable) may have different base (==number of choices).
/// Thus modelling of 'produce next combination' is exactly analogous to the
/// incrementing of an number - increment lowest digit (pick next choice for the
/// variable), and if it wrapped to the beginning then increment next digit.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_COMBINATIONGENERATOR_H
#define LLVM_ADT_COMBINATIONGENERATOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <cstring>

namespace llvm {

template <typename choice_type, typename choices_storage_type,
          int variable_smallsize>
class CombinationGenerator {
  template <typename T> struct WrappingIterator {
    using value_type = T;

    const ArrayRef<value_type> Range;
    typename decltype(Range)::const_iterator Position;

    // Rewind the tape, placing the position to again point at the beginning.
    void rewind() { Position = Range.begin(); }

    // Advance position forward, possibly wrapping to the beginning.
    // Returns whether the wrap happened.
    bool advance() {
      ++Position;
      bool Wrapped = Position == Range.end();
      if (Wrapped)
        rewind();
      return Wrapped;
    }

    // Get the value at which we are currently pointing.
    const value_type &operator*() const { return *Position; }

    WrappingIterator(ArrayRef<value_type> Range_) : Range(Range_) {
      assert(!Range.empty() && "The range must not be empty.");
      rewind();
    }
  };

  const ArrayRef<choices_storage_type> VariablesChoices;

  void performGeneration(
      const function_ref<bool(ArrayRef<choice_type>)> Callback) const {
    SmallVector<WrappingIterator<choice_type>, variable_smallsize>
        VariablesState;

    // 'increment' of the whole VariablesState is defined identically to the
    // increment of a number: starting from the least significant element,
    // increment it, and if it wrapped, then propagate that carry by also
    // incrementing next (more significant) element.
    auto IncrementState =
        [](MutableArrayRef<WrappingIterator<choice_type>> VariablesState)
        -> bool {
      for (WrappingIterator<choice_type> &Variable :
           llvm::reverse(VariablesState)) {
        bool Wrapped = Variable.advance();
        if (!Wrapped)
          return false; // There you go, next combination is ready.
        // We have carry - increment more significant variable next..
      }
      return true; // MSB variable wrapped, no more unique combinations.
    };

    // Initialize the per-variable state to refer to the possible choices for
    // that variable.
    VariablesState.reserve(VariablesChoices.size());
    for (ArrayRef<choice_type> VC : VariablesChoices)
      VariablesState.emplace_back(VC);

    // Temporary buffer to store each combination before performing Callback.
    SmallVector<choice_type, variable_smallsize> CurrentCombination;
    CurrentCombination.resize(VariablesState.size());

    while (true) {
      // Gather the currently-selected variable choices into a vector.
      for (auto I : llvm::zip(VariablesState, CurrentCombination))
        std::get<1>(I) = *std::get<0>(I);
      // And pass the new combination into callback, as intended.
      if (/*Abort=*/Callback(CurrentCombination))
        return;
      // And tick the state to next combination, which will be unique.
      if (IncrementState(VariablesState))
        return; // All combinations produced.
    }
  };

public:
  CombinationGenerator(ArrayRef<choices_storage_type> VariablesChoices_)
      : VariablesChoices(VariablesChoices_) {
#ifndef NDEBUG
    assert(!VariablesChoices.empty() && "There should be some variables.");
    llvm::for_each(VariablesChoices, [](ArrayRef<choice_type> VariableChoices) {
      assert(!VariableChoices.empty() &&
             "There must always be some choice, at least a placeholder one.");
    });
#endif
  }

  // How many combinations can we produce, max?
  // This is at most how many times the callback will be called.
  size_t numCombinations() const {
    size_t NumVariants = 1;
    for (ArrayRef<choice_type> VariableChoices : VariablesChoices)
      NumVariants *= VariableChoices.size();
    assert(NumVariants >= 1 &&
           "We should always end up producing at least one combination");
    return NumVariants;
  }

  // Actually perform exhaustive combination generation.
  // Each result will be passed into the callback.
  void generate(const function_ref<bool(ArrayRef<choice_type>)> Callback) {
    performGeneration(Callback);
  }
};

} // namespace llvm

#endif

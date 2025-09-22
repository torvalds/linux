//===- ValueProfileCollector.h - determine what to value profile ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a utility class, ValueProfileCollector, that is used to
// determine what kind of llvm::Value's are worth value-profiling, at which
// point in the program, and which instruction holds the Value Profile metadata.
// Currently, the only users of this utility is the PGOInstrumentation[Gen|Use]
// passes.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_PROFILE_GEN_ANALYSIS_H
#define LLVM_ANALYSIS_PROFILE_GEN_ANALYSIS_H

#include "llvm/ProfileData/InstrProf.h"
#include <memory>
#include <vector>

namespace llvm {

class Function;
class Instruction;
class TargetLibraryInfo;
class Value;

/// Utility analysis that determines what values are worth profiling.
/// The actual logic is inside the ValueProfileCollectorImpl, whose job is to
/// populate the Candidates vector.
///
/// Value profiling an expression means to track the values that this expression
/// takes at runtime and the frequency of each value.
/// It is important to distinguish between two sets of value profiles for a
/// particular expression:
///  1) The set of values at the point of evaluation.
///  2) The set of values at the point of use.
/// In some cases, the two sets are identical, but it's not unusual for the two
/// to differ.
///
/// To elaborate more, consider this C code, and focus on the expression `nn`:
///  void foo(int nn, bool b) {
///    if (b)  memcpy(x, y, nn);
///  }
/// The point of evaluation can be as early as the start of the function, and
/// let's say the value profile for `nn` is:
///     total=100; (value,freq) set = {(8,10), (32,50)}
/// The point of use is right before we call memcpy, and since we execute the
/// memcpy conditionally, the value profile of `nn` can be:
///     total=15; (value,freq) set = {(8,10), (4,5)}
///
/// For this reason, a plugin is responsible for computing the insertion point
/// for each value to be profiled. The `CandidateInfo` structure encapsulates
/// all the information needed for each value profile site.
class ValueProfileCollector {
public:
  struct CandidateInfo {
    Value *V;                   // The value to profile.
    Instruction *InsertPt;      // Insert the VP lib call before this instr.
    Instruction *AnnotatedInst; // Where metadata is attached.
  };

  ValueProfileCollector(Function &Fn, TargetLibraryInfo &TLI);
  ValueProfileCollector(ValueProfileCollector &&) = delete;
  ValueProfileCollector &operator=(ValueProfileCollector &&) = delete;

  ValueProfileCollector(const ValueProfileCollector &) = delete;
  ValueProfileCollector &operator=(const ValueProfileCollector &) = delete;
  ~ValueProfileCollector();

  /// returns a list of value profiling candidates of the given kind
  std::vector<CandidateInfo> get(InstrProfValueKind Kind) const;

private:
  class ValueProfileCollectorImpl;
  std::unique_ptr<ValueProfileCollectorImpl> PImpl;
};

} // namespace llvm

#endif

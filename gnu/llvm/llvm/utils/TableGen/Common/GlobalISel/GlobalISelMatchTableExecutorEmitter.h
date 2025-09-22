//===- GlobalISelMatchTableExecutorEmitter.h ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains common code related to emitting
/// GIMatchTableExecutor-derived classes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_GLOBALISELMATCHTABLEEXECUTOREMITTER_H
#define LLVM_UTILS_TABLEGEN_GLOBALISELMATCHTABLEEXECUTOREMITTER_H

#include "Common/SubtargetFeatureInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include <functional>

namespace llvm {
class CodeGenTarget;

namespace gi {
class RuleMatcher;
class LLTCodeGen;
class MatchTable;
} // namespace gi

/// Abstract base class for TableGen backends that emit a
/// `GIMatchTableExecutor`-derived class.
class GlobalISelMatchTableExecutorEmitter {
  /// Emits logic to check features required by \p Rules using the

  /// SubtargetFeatures map.
  void emitSubtargetFeatureBitsetImpl(raw_ostream &OS,
                                      ArrayRef<gi::RuleMatcher> Rules);

  /// Emits an enum + an array that stores references to
  /// \p ComplexOperandMatchers.
  void emitComplexPredicates(raw_ostream &OS,
                             ArrayRef<Record *> ComplexOperandMatchers);

  /// Emits an enum + an array that stores references to
  /// \p CustomOperandRenderers.
  void emitCustomOperandRenderers(raw_ostream &OS,
                                  ArrayRef<StringRef> CustomOperandRenderers);

  /// Emits an enum + an array to reference \p TypeObjects (LLTs) in the match
  /// table.
  void emitTypeObjects(raw_ostream &OS, ArrayRef<gi::LLTCodeGen> TypeObjects);

  /// Emits the getMatchTable function which contains all of the match table's
  /// opcodes.
  void emitMatchTable(raw_ostream &OS, const gi::MatchTable &Table);

  /// Helper function to emit `test` functions for the executor. This emits both
  /// an enum to reference predicates in the MatchTable, and a function to
  /// switch over the enum & execute the predicate's C++ code.
  ///
  /// \tparam PredicateObject An object representing a predicate to emit.
  /// \param OS Output stream
  /// \param TypeIdentifier Identifier used for the type of the predicate,
  ///        e.g. `MI` for MachineInstrs.
  /// \param ArgType Full type of the argument, e.g. `const MachineInstr &`
  /// \param ArgName Name of the argument, e.g. `MI` for MachineInstrs.
  /// \param AdditionalArgs Optional additional argument declarations.
  /// \param AdditionalDeclarations Optional declarations to write at the start
  ///        of the function, before switching over the predicates enum.
  /// \param Predicates Predicates to emit.
  /// \param GetPredEnumName Returns an enum name for a given predicate.
  /// \param GetPredCode Returns the C++ code of a given predicate.
  /// \param Comment Optional comment for the enum declaration.
  template <typename PredicateObject>
  void emitCxxPredicateFns(
      raw_ostream &OS, StringRef TypeIdentifier, StringRef ArgType,
      StringRef ArgName, StringRef AdditionalArgs,
      StringRef AdditionalDeclarations, ArrayRef<PredicateObject> Predicates,
      std::function<StringRef(PredicateObject)> GetPredEnumName,
      std::function<StringRef(PredicateObject)> GetPredCode,
      StringRef Comment) {
    if (!Comment.empty())
      OS << "// " << Comment << "\n";
    if (!Predicates.empty()) {
      OS << "enum {\n";
      StringRef EnumeratorSeparator = " = GICXXPred_Invalid + 1,\n";
      for (const auto &Pred : Predicates) {
        OS << "  GICXXPred_" << TypeIdentifier << "_Predicate_"
           << GetPredEnumName(Pred) << EnumeratorSeparator;
        EnumeratorSeparator = ",\n";
      }
      OS << "};\n";
    }

    OS << "bool " << getClassName() << "::test" << ArgName << "Predicate_"
       << TypeIdentifier << "(unsigned PredicateID, " << ArgType << " "
       << ArgName << AdditionalArgs << ") const {\n"
       << AdditionalDeclarations;
    if (!AdditionalDeclarations.empty())
      OS << "\n";
    if (!Predicates.empty()) {
      OS << "  switch (PredicateID) {\n";
      for (const auto &Pred : Predicates) {
        // Ensure all code is indented.
        const auto Code = join(split(GetPredCode(Pred).str(), "\n"), "\n    ");
        OS << "  case GICXXPred_" << TypeIdentifier << "_Predicate_"
           << GetPredEnumName(Pred) << ": {\n"
           << "    " << Code << "\n";
        if (!StringRef(Code).ltrim().starts_with("return")) {
          OS << "    llvm_unreachable(\"" << GetPredEnumName(Pred)
             << " should have returned\");\n";
        }
        OS << "  }\n";
      }
      OS << "  }\n";
    }
    OS << "  llvm_unreachable(\"Unknown predicate\");\n"
       << "  return false;\n"
       << "}\n";
  }

protected:
  /// Emits `testMIPredicate_MI`.
  /// \tparam PredicateObject An object representing a predicate to emit.
  /// \param OS Output stream
  /// \param AdditionalDecls Additional C++ variable declarations.
  /// \param Predicates Predicates to emit.
  /// \param GetPredEnumName Returns an enum name for a given predicate.
  /// \param GetPredCode Returns the C++ code of a given predicate.
  /// \param Comment Optional comment for the enum declaration.
  template <typename PredicateObject>
  void emitMIPredicateFnsImpl(
      raw_ostream &OS, StringRef AdditionalDecls,
      ArrayRef<PredicateObject> Predicates,
      std::function<StringRef(PredicateObject)> GetPredEnumName,
      std::function<StringRef(PredicateObject)> GetPredCode,
      StringRef Comment = "") {
    return emitCxxPredicateFns(
        OS, "MI", "const MachineInstr &", "MI", ", const MatcherState &State",
        AdditionalDecls, Predicates, GetPredEnumName, GetPredCode, Comment);
  }

  /// Helper function to emit the following executor functions:
  ///   * testImmPredicate_I64      (TypeIdentifier=I64)
  ///   * testImmPredicate_APInt    (TypeIdentifier=APInt)
  ///   * testImmPredicate_APFloat  (TypeIdentifier=APFloat)
  ///
  /// \tparam PredicateObject An object representing a predicate to emit.
  /// \param OS Output stream
  /// \param TypeIdentifier Identifier used for the type of the predicate
  /// \param ArgType Full type of the argument
  /// \param Predicates Predicates to emit.
  /// \param GetPredEnumName Returns an enum name for a given predicate.
  /// \param GetPredCode Returns the C++ code of a given predicate.
  /// \param Comment Optional comment for the enum declaration.
  template <typename PredicateObject>
  void emitImmPredicateFnsImpl(
      raw_ostream &OS, StringRef TypeIdentifier, StringRef ArgType,
      ArrayRef<PredicateObject> Predicates,
      std::function<StringRef(PredicateObject)> GetPredEnumName,
      std::function<StringRef(PredicateObject)> GetPredCode,
      StringRef Comment = "") {
    return emitCxxPredicateFns(OS, TypeIdentifier, ArgType, "Imm", "", "",
                               Predicates, GetPredEnumName, GetPredCode,
                               Comment);
  }

  GlobalISelMatchTableExecutorEmitter() = default;

public:
  virtual ~GlobalISelMatchTableExecutorEmitter() = default;

  virtual const CodeGenTarget &getTarget() const = 0;

  /// \returns the name of the class being emitted including any prefixes, e.g.
  /// `AMDGPUInstructionSelector`.
  virtual StringRef getClassName() const = 0;

  /// Emit additional content in emitExecutorImpl
  virtual void emitAdditionalImpl(raw_ostream &OS) {}

  /// Emit additional content in emitTemporariesInit.
  virtual void emitAdditionalTemporariesInit(raw_ostream &OS) {}

  /// Emit the `testMIPredicate_MI` function.
  /// Note: `emitMIPredicateFnsImpl` can be used to do most of the work.
  virtual void emitMIPredicateFns(raw_ostream &OS) = 0;

  /// Emit the `testImmPredicate_I64` function.
  /// Note: `emitImmPredicateFnsImpl` can be used to do most of the work.
  virtual void emitI64ImmPredicateFns(raw_ostream &OS) = 0;

  /// Emit the `testImmPredicate_APFloat` function.
  /// Note: `emitImmPredicateFnsImpl` can be used to do most of the work.
  virtual void emitAPFloatImmPredicateFns(raw_ostream &OS) = 0;

  /// Emit the `testImmPredicate_APInt` function.
  /// Note: `emitImmPredicateFnsImpl` can be used to do most of the work.
  virtual void emitAPIntImmPredicateFns(raw_ostream &OS) = 0;
  virtual void emitTestSimplePredicate(raw_ostream &OS) = 0;
  virtual void emitRunCustomAction(raw_ostream &OS) = 0;

  void emitExecutorImpl(raw_ostream &OS, const gi::MatchTable &Table,
                        ArrayRef<gi::LLTCodeGen> TypeObjects,
                        ArrayRef<gi::RuleMatcher> Rules,
                        ArrayRef<Record *> ComplexOperandMatchers,
                        ArrayRef<StringRef> CustomOperandRenderers,
                        StringRef IfDefName);
  void emitPredicateBitset(raw_ostream &OS, StringRef IfDefName);
  void emitTemporariesDecl(raw_ostream &OS, StringRef IfDefName);
  void emitTemporariesInit(raw_ostream &OS, unsigned MaxTemporaries,
                           StringRef IfDefName);
  void emitPredicatesDecl(raw_ostream &OS, StringRef IfDefName);
  void emitPredicatesInit(raw_ostream &OS, StringRef IfDefName);

  // Map of predicates to their subtarget features.
  SubtargetFeatureInfoMap SubtargetFeatures;

  std::map<std::string, unsigned> HwModes;
};
} // namespace llvm

#endif

//===-- DataflowAnalysisContext.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a DataflowAnalysisContext class that owns objects that
//  encompass the state of a program and stores context that is used during
//  dataflow analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_DATAFLOWANALYSISCONTEXT_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_DATAFLOWANALYSISCONTEXT_H

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/TypeOrdering.h"
#include "clang/Analysis/FlowSensitive/ASTOps.h"
#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/Arena.h"
#include "clang/Analysis/FlowSensitive/Solver.h"
#include "clang/Analysis/FlowSensitive/StorageLocation.h"
#include "clang/Analysis/FlowSensitive/Value.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <memory>
#include <optional>

namespace clang {
namespace dataflow {
class Logger;

struct ContextSensitiveOptions {
  /// The maximum depth to analyze. A value of zero is equivalent to disabling
  /// context-sensitive analysis entirely.
  unsigned Depth = 2;
};

/// Owns objects that encompass the state of a program and stores context that
/// is used during dataflow analysis.
class DataflowAnalysisContext {
public:
  struct Options {
    /// Options for analyzing function bodies when present in the translation
    /// unit, or empty to disable context-sensitive analysis. Note that this is
    /// fundamentally limited: some constructs, such as recursion, are
    /// explicitly unsupported.
    std::optional<ContextSensitiveOptions> ContextSensitiveOpts;

    /// If provided, analysis details will be recorded here.
    /// (This is always non-null within an AnalysisContext, the framework
    /// provides a fallback no-op logger).
    Logger *Log = nullptr;
  };

  /// Constructs a dataflow analysis context.
  ///
  /// Requirements:
  ///
  ///  `S` must not be null.
  DataflowAnalysisContext(std::unique_ptr<Solver> S,
                          Options Opts = Options{
                              /*ContextSensitiveOpts=*/std::nullopt,
                              /*Logger=*/nullptr})
      : DataflowAnalysisContext(*S, std::move(S), Opts) {}

  /// Constructs a dataflow analysis context.
  ///
  /// Requirements:
  ///
  ///  `S` must outlive the `DataflowAnalysisContext`.
  DataflowAnalysisContext(Solver &S, Options Opts = Options{
                                         /*ContextSensitiveOpts=*/std::nullopt,
                                         /*Logger=*/nullptr})
      : DataflowAnalysisContext(S, nullptr, Opts) {}

  ~DataflowAnalysisContext();

  /// Sets a callback that returns the names and types of the synthetic fields
  /// to add to a `RecordStorageLocation` of a given type.
  /// Typically, this is called from the constructor of a `DataflowAnalysis`
  ///
  /// The field types returned by the callback may not have reference type.
  ///
  /// To maintain the invariant that all `RecordStorageLocation`s of a given
  /// type have the same fields:
  /// *  The callback must always return the same result for a given type
  /// *  `setSyntheticFieldCallback()` must be called before any
  //     `RecordStorageLocation`s are created.
  void setSyntheticFieldCallback(
      std::function<llvm::StringMap<QualType>(QualType)> CB) {
    assert(!RecordStorageLocationCreated);
    SyntheticFieldCallback = CB;
  }

  /// Returns a new storage location appropriate for `Type`.
  ///
  /// A null `Type` is interpreted as the pointee type of `std::nullptr_t`.
  StorageLocation &createStorageLocation(QualType Type);

  /// Creates a `RecordStorageLocation` for the given type and with the given
  /// fields.
  ///
  /// Requirements:
  ///
  ///  `FieldLocs` must contain exactly the fields returned by
  ///  `getModeledFields(Type)`.
  ///  `SyntheticFields` must contain exactly the fields returned by
  ///  `getSyntheticFields(Type)`.
  RecordStorageLocation &createRecordStorageLocation(
      QualType Type, RecordStorageLocation::FieldToLoc FieldLocs,
      RecordStorageLocation::SyntheticFieldMap SyntheticFields);

  /// Returns a stable storage location for `D`.
  StorageLocation &getStableStorageLocation(const ValueDecl &D);

  /// Returns a stable storage location for `E`.
  StorageLocation &getStableStorageLocation(const Expr &E);

  /// Returns a pointer value that represents a null pointer. Calls with
  /// `PointeeType` that are canonically equivalent will return the same result.
  /// A null `PointeeType` can be used for the pointee of `std::nullptr_t`.
  PointerValue &getOrCreateNullPointerValue(QualType PointeeType);

  /// Adds `Constraint` to current and future flow conditions in this context.
  ///
  /// Invariants must contain only flow-insensitive information, i.e. facts that
  /// are true on all paths through the program.
  /// Information can be added eagerly (when analysis begins), or lazily (e.g.
  /// when values are first used). The analysis must be careful that the same
  /// information is added regardless of which order blocks are analyzed in.
  void addInvariant(const Formula &Constraint);

  /// Adds `Constraint` to the flow condition identified by `Token`.
  void addFlowConditionConstraint(Atom Token, const Formula &Constraint);

  /// Creates a new flow condition with the same constraints as the flow
  /// condition identified by `Token` and returns its token.
  Atom forkFlowCondition(Atom Token);

  /// Creates a new flow condition that represents the disjunction of the flow
  /// conditions identified by `FirstToken` and `SecondToken`, and returns its
  /// token.
  Atom joinFlowConditions(Atom FirstToken, Atom SecondToken);

  /// Returns true if the constraints of the flow condition identified by
  /// `Token` imply that `F` is true.
  /// Returns false if the flow condition does not imply `F` or if the solver
  /// times out.
  bool flowConditionImplies(Atom Token, const Formula &F);

  /// Returns true if the constraints of the flow condition identified by
  /// `Token` still allow `F` to be true.
  /// Returns false if the flow condition implies that `F` is false or if the
  /// solver times out.
  bool flowConditionAllows(Atom Token, const Formula &F);

  /// Returns true if `Val1` is equivalent to `Val2`.
  /// Note: This function doesn't take into account constraints on `Val1` and
  /// `Val2` imposed by the flow condition.
  bool equivalentFormulas(const Formula &Val1, const Formula &Val2);

  LLVM_DUMP_METHOD void dumpFlowCondition(Atom Token,
                                          llvm::raw_ostream &OS = llvm::dbgs());

  /// Returns the `AdornedCFG` registered for `F`, if any. Otherwise,
  /// returns null.
  const AdornedCFG *getAdornedCFG(const FunctionDecl *F);

  const Options &getOptions() { return Opts; }

  Arena &arena() { return *A; }

  /// Returns the outcome of satisfiability checking on `Constraints`.
  ///
  /// Flow conditions are not incorporated, so they may need to be manually
  /// included in `Constraints` to provide contextually-accurate results, e.g.
  /// if any definitions or relationships of the values in `Constraints` have
  /// been stored in flow conditions.
  Solver::Result querySolver(llvm::SetVector<const Formula *> Constraints);

  /// Returns the fields of `Type`, limited to the set of fields modeled by this
  /// context.
  FieldSet getModeledFields(QualType Type);

  /// Returns the names and types of the synthetic fields for the given record
  /// type.
  llvm::StringMap<QualType> getSyntheticFields(QualType Type) {
    assert(Type->isRecordType());
    if (SyntheticFieldCallback) {
      llvm::StringMap<QualType> Result = SyntheticFieldCallback(Type);
      // Synthetic fields are not allowed to have reference type.
      assert([&Result] {
        for (const auto &Entry : Result)
          if (Entry.getValue()->isReferenceType())
            return false;
        return true;
      }());
      return Result;
    }
    return {};
  }

private:
  friend class Environment;

  struct NullableQualTypeDenseMapInfo : private llvm::DenseMapInfo<QualType> {
    static QualType getEmptyKey() {
      // Allow a NULL `QualType` by using a different value as the empty key.
      return QualType::getFromOpaquePtr(reinterpret_cast<Type *>(1));
    }

    using DenseMapInfo::getHashValue;
    using DenseMapInfo::getTombstoneKey;
    using DenseMapInfo::isEqual;
  };

  /// `S` is the solver to use. `OwnedSolver` may be:
  /// *  Null (in which case `S` is non-onwed and must outlive this object), or
  /// *  Non-null (in which case it must refer to `S`, and the
  ///    `DataflowAnalysisContext will take ownership of `OwnedSolver`).
  DataflowAnalysisContext(Solver &S, std::unique_ptr<Solver> &&OwnedSolver,
                          Options Opts);

  // Extends the set of modeled field declarations.
  void addModeledFields(const FieldSet &Fields);

  /// Adds all constraints of the flow condition identified by `Token` and all
  /// of its transitive dependencies to `Constraints`.
  void
  addTransitiveFlowConditionConstraints(Atom Token,
                                        llvm::SetVector<const Formula *> &Out);

  /// Returns true if the solver is able to prove that there is a satisfying
  /// assignment for `Constraints`.
  bool isSatisfiable(llvm::SetVector<const Formula *> Constraints) {
    return querySolver(std::move(Constraints)).getStatus() ==
           Solver::Result::Status::Satisfiable;
  }

  /// Returns true if the solver is able to prove that there is no satisfying
  /// assignment for `Constraints`
  bool isUnsatisfiable(llvm::SetVector<const Formula *> Constraints) {
    return querySolver(std::move(Constraints)).getStatus() ==
           Solver::Result::Status::Unsatisfiable;
  }

  Solver &S;
  std::unique_ptr<Solver> OwnedSolver;
  std::unique_ptr<Arena> A;

  // Maps from program declarations and statements to storage locations that are
  // assigned to them. These assignments are global (aggregated across all basic
  // blocks) and are used to produce stable storage locations when the same
  // basic blocks are evaluated multiple times. The storage locations that are
  // in scope for a particular basic block are stored in `Environment`.
  llvm::DenseMap<const ValueDecl *, StorageLocation *> DeclToLoc;
  llvm::DenseMap<const Expr *, StorageLocation *> ExprToLoc;

  // Null pointer values, keyed by the canonical pointee type.
  //
  // FIXME: The pointer values are indexed by the pointee types which are
  // required to initialize the `PointeeLoc` field in `PointerValue`. Consider
  // creating a type-independent `NullPointerValue` without a `PointeeLoc`
  // field.
  llvm::DenseMap<QualType, PointerValue *, NullableQualTypeDenseMapInfo>
      NullPointerVals;

  Options Opts;

  // Flow conditions are tracked symbolically: each unique flow condition is
  // associated with a fresh symbolic variable (token), bound to the clause that
  // defines the flow condition. Conceptually, each binding corresponds to an
  // "iff" of the form `FC <=> (C1 ^ C2 ^ ...)` where `FC` is a flow condition
  // token (an atomic boolean) and `Ci`s are the set of constraints in the flow
  // flow condition clause. The set of constraints (C1 ^ C2 ^ ...) are stored in
  // the `FlowConditionConstraints` map, keyed by the token of the flow
  // condition.
  //
  // Flow conditions depend on other flow conditions if they are created using
  // `forkFlowCondition` or `joinFlowConditions`. The graph of flow condition
  // dependencies is stored in the `FlowConditionDeps` map.
  llvm::DenseMap<Atom, llvm::DenseSet<Atom>> FlowConditionDeps;
  llvm::DenseMap<Atom, const Formula *> FlowConditionConstraints;
  const Formula *Invariant = nullptr;

  llvm::DenseMap<const FunctionDecl *, AdornedCFG> FunctionContexts;

  // Fields modeled by environments covered by this context.
  FieldSet ModeledFields;

  std::unique_ptr<Logger> LogOwner; // If created via flags.

  std::function<llvm::StringMap<QualType>(QualType)> SyntheticFieldCallback;

  /// Has any `RecordStorageLocation` been created yet?
  bool RecordStorageLocationCreated = false;
};

} // namespace dataflow
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_DATAFLOWANALYSISCONTEXT_H

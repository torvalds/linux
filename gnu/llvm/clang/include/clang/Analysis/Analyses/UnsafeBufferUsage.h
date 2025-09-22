//===- UnsafeBufferUsage.h - Replace pointers with modern C++ ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines an analysis that aids replacing buffer accesses through
//  raw pointers with safer C++ abstractions such as containers and views/spans.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_UNSAFEBUFFERUSAGE_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_UNSAFEBUFFERUSAGE_H

#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/Support/Debug.h"

namespace clang {

using VarGrpTy = std::vector<const VarDecl *>;
using VarGrpRef = ArrayRef<const VarDecl *>;

class VariableGroupsManager {
public:
  VariableGroupsManager() = default;
  virtual ~VariableGroupsManager() = default;
  /// Returns the set of variables (including `Var`) that need to be fixed
  /// together in one step.
  ///
  /// `Var` must be a variable that needs fix (so it must be in a group).
  /// `HasParm` is an optional argument that will be set to true if the set of
  /// variables, where `Var` is in, contains parameters.
  virtual VarGrpRef getGroupOfVar(const VarDecl *Var,
                                  bool *HasParm = nullptr) const =0;

  /// Returns the non-empty group of variables that include parameters of the
  /// analyzing function, if such a group exists.  An empty group, otherwise.
  virtual VarGrpRef getGroupOfParms() const =0;
};

// FixitStrategy is a map from variables to the way we plan to emit fixes for
// these variables. It is figured out gradually by trying different fixes
// for different variables depending on gadgets in which these variables
// participate.
class FixitStrategy {
public:
  enum class Kind {
    Wontfix,  // We don't plan to emit a fixit for this variable.
    Span,     // We recommend replacing the variable with std::span.
    Iterator, // We recommend replacing the variable with std::span::iterator.
    Array,    // We recommend replacing the variable with std::array.
    Vector    // We recommend replacing the variable with std::vector.
  };

private:
  using MapTy = llvm::DenseMap<const VarDecl *, Kind>;

  MapTy Map;

public:
  FixitStrategy() = default;
  FixitStrategy(const FixitStrategy &) = delete; // Let's avoid copies.
  FixitStrategy &operator=(const FixitStrategy &) = delete;
  FixitStrategy(FixitStrategy &&) = default;
  FixitStrategy &operator=(FixitStrategy &&) = default;

  void set(const VarDecl *VD, Kind K) { Map[VD] = K; }

  Kind lookup(const VarDecl *VD) const {
    auto I = Map.find(VD);
    if (I == Map.end())
      return Kind::Wontfix;

    return I->second;
  }
};

/// The interface that lets the caller handle unsafe buffer usage analysis
/// results by overriding this class's handle... methods.
class UnsafeBufferUsageHandler {
#ifndef NDEBUG
public:
  // A self-debugging facility that you can use to notify the user when
  // suggestions or fixits are incomplete.
  // Uses std::function to avoid computing the message when it won't
  // actually be displayed.
  using DebugNote = std::pair<SourceLocation, std::string>;
  using DebugNoteList = std::vector<DebugNote>;
  using DebugNoteByVar = std::map<const VarDecl *, DebugNoteList>;
  DebugNoteByVar DebugNotesByVar;
#endif

public:
  UnsafeBufferUsageHandler() = default;
  virtual ~UnsafeBufferUsageHandler() = default;

  /// This analyses produces large fixits that are organized into lists
  /// of primitive fixits (individual insertions/removals/replacements).
  using FixItList = llvm::SmallVectorImpl<FixItHint>;

  /// Invoked when an unsafe operation over raw pointers is found.
  virtual void handleUnsafeOperation(const Stmt *Operation,
                                     bool IsRelatedToDecl, ASTContext &Ctx) = 0;

  /// Invoked when an unsafe operation with a std container is found.
  virtual void handleUnsafeOperationInContainer(const Stmt *Operation,
                                                bool IsRelatedToDecl,
                                                ASTContext &Ctx) = 0;

  /// Invoked when a fix is suggested against a variable. This function groups
  /// all variables that must be fixed together (i.e their types must be changed
  /// to the same target type to prevent type mismatches) into a single fixit.
  ///
  /// `D` is the declaration of the callable under analysis that owns `Variable`
  /// and all of its group mates.
  virtual void
  handleUnsafeVariableGroup(const VarDecl *Variable,
                            const VariableGroupsManager &VarGrpMgr,
                            FixItList &&Fixes, const Decl *D,
                            const FixitStrategy &VarTargetTypes) = 0;

#ifndef NDEBUG
public:
  bool areDebugNotesRequested() {
    DEBUG_WITH_TYPE("SafeBuffers", return true);
    return false;
  }

  void addDebugNoteForVar(const VarDecl *VD, SourceLocation Loc,
                          std::string Text) {
    if (areDebugNotesRequested())
      DebugNotesByVar[VD].push_back(std::make_pair(Loc, Text));
  }

  void clearDebugNotes() {
    if (areDebugNotesRequested())
      DebugNotesByVar.clear();
  }
#endif

public:
  /// \return true iff buffer safety is opt-out at `Loc`; false otherwise.
  virtual bool isSafeBufferOptOut(const SourceLocation &Loc) const = 0;

  /// \return true iff unsafe uses in containers should NOT be reported at
  /// `Loc`; false otherwise.
  virtual bool
  ignoreUnsafeBufferInContainer(const SourceLocation &Loc) const = 0;

  virtual std::string
  getUnsafeBufferUsageAttributeTextAt(SourceLocation Loc,
                                      StringRef WSSuffix = "") const = 0;
};

// This function invokes the analysis and allows the caller to react to it
// through the handler class.
void checkUnsafeBufferUsage(const Decl *D, UnsafeBufferUsageHandler &Handler,
                            bool EmitSuggestions);

namespace internal {
// Tests if any two `FixItHint`s in `FixIts` conflict.  Two `FixItHint`s
// conflict if they have overlapping source ranges.
bool anyConflict(const llvm::SmallVectorImpl<FixItHint> &FixIts,
                 const SourceManager &SM);
} // namespace internal
} // end namespace clang

#endif /* LLVM_CLANG_ANALYSIS_ANALYSES_UNSAFEBUFFERUSAGE_H */

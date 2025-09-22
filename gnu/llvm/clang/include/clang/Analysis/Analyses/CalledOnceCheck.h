//===- CalledOnceCheck.h - Check 'called once' parameters -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a check for function-like parameters that should be
//  called exactly one time.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_CALLEDONCECHECK_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_CALLEDONCECHECK_H

namespace clang {

class AnalysisDeclContext;
class BlockDecl;
class CFG;
class Decl;
class Expr;
class ParmVarDecl;
class Stmt;

/// Classification of situations when parameter is not called on every path.
/// \enum IfThen -- then branch of the if statement has no call.
/// \enum IfElse -- else branch of the if statement has no call.
/// \enum Switch -- one of the switch cases doesn't have a call.
/// \enum SwitchSkipped -- there is no call if none of the cases applies.
/// \enum LoopEntered -- no call when the loop is entered.
/// \enum LoopSkipped -- no call when the loop is not entered.
/// \enum FallbackReason -- fallback case when we were not able to figure out
/// the reason.
enum class NeverCalledReason {
  IfThen,
  IfElse,
  Switch,
  SwitchSkipped,
  LoopEntered,
  LoopSkipped,
  FallbackReason,
  LARGEST_VALUE = FallbackReason
};

class CalledOnceCheckHandler {
public:
  CalledOnceCheckHandler() = default;
  virtual ~CalledOnceCheckHandler() = default;

  /// Called when parameter is called twice.
  /// \param Parameter -- parameter that should be called once.
  /// \param Call -- call to report the warning.
  /// \param PrevCall -- previous call.
  /// \param IsCompletionHandler -- true, if parameter is a completion handler.
  /// \param Poised -- true, if the second call is guaranteed to happen after
  /// the first call.
  virtual void handleDoubleCall(const ParmVarDecl *Parameter, const Expr *Call,
                                const Expr *PrevCall, bool IsCompletionHandler,
                                bool Poised) {}

  /// Called when parameter is not called at all.
  /// \param Parameter -- parameter that should be called once.
  /// \param IsCompletionHandler -- true, if parameter is a completion handler.
  virtual void handleNeverCalled(const ParmVarDecl *Parameter,
                                 bool IsCompletionHandler) {}

  /// Called when captured parameter is not called at all.
  /// \param Parameter -- parameter that should be called once.
  /// \param Where -- declaration that captures \p Parameter
  /// \param IsCompletionHandler -- true, if parameter is a completion handler.
  virtual void handleCapturedNeverCalled(const ParmVarDecl *Parameter,
                                         const Decl *Where,
                                         bool IsCompletionHandler) {}

  /// Called when parameter is not called on one of the paths.
  /// Usually we try to find a statement that is the least common ancestor of
  /// the path containing the call and not containing the call.  This helps us
  /// to pinpoint a bad path for the user.
  /// \param Parameter -- parameter that should be called once.
  /// \param Function -- function declaration where the problem occurred.
  /// \param Where -- the least common ancestor statement.
  /// \param Reason -- a reason describing the path without a call.
  /// \param IsCalledDirectly -- true, if parameter actually gets called on
  /// the other path.  It is opposed to be used in some other way (added to some
  /// collection, passed as a parameter, etc.).
  /// \param IsCompletionHandler -- true, if parameter is a completion handler.
  virtual void handleNeverCalled(const ParmVarDecl *Parameter,
                                 const Decl *Function, const Stmt *Where,
                                 NeverCalledReason Reason,
                                 bool IsCalledDirectly,
                                 bool IsCompletionHandler) {}

  /// Called when the block is guaranteed to be called exactly once.
  /// It means that we can be stricter with what we report on that block.
  /// \param Block -- block declaration that is known to be called exactly once.
  virtual void
  handleBlockThatIsGuaranteedToBeCalledOnce(const BlockDecl *Block) {}

  /// Called when the block has no guarantees about how many times it can get
  /// called.
  /// It means that we should be more lenient with reporting warnings in it.
  /// \param Block -- block declaration in question.
  virtual void handleBlockWithNoGuarantees(const BlockDecl *Block) {}
};

/// Check given CFG for 'called once' parameter violations.
///
/// It traverses the function and tracks how such parameters are used.
/// It detects two main violations:
///   * parameter is called twice
///   * parameter is not called
///
/// \param AC -- context.
/// \param Handler -- a handler for found violations.
/// \param CheckConventionalParameters -- true, if we want to check parameters
/// not explicitly marked as 'called once', but having the same requirements
/// according to conventions.
void checkCalledOnceParameters(AnalysisDeclContext &AC,
                               CalledOnceCheckHandler &Handler,
                               bool CheckConventionalParameters);

} // end namespace clang

#endif /* LLVM_CLANG_ANALYSIS_ANALYSES_CALLEDONCECHECK_H */

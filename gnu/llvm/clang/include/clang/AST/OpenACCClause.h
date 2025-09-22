//===- OpenACCClause.h - Classes for OpenACC clauses ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file defines OpenACC AST classes for clauses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_OPENACCCLAUSE_H
#define LLVM_CLANG_AST_OPENACCCLAUSE_H
#include "clang/AST/ASTContext.h"
#include "clang/AST/StmtIterator.h"
#include "clang/Basic/OpenACCKinds.h"

#include <utility>

namespace clang {
/// This is the base type for all OpenACC Clauses.
class OpenACCClause {
  OpenACCClauseKind Kind;
  SourceRange Location;

protected:
  OpenACCClause(OpenACCClauseKind K, SourceLocation BeginLoc,
                SourceLocation EndLoc)
      : Kind(K), Location(BeginLoc, EndLoc) {
    assert(!BeginLoc.isInvalid() && !EndLoc.isInvalid() &&
           "Begin and end location must be valid for OpenACCClause");
      }

public:
  OpenACCClauseKind getClauseKind() const { return Kind; }
  SourceLocation getBeginLoc() const { return Location.getBegin(); }
  SourceLocation getEndLoc() const { return Location.getEnd(); }

  static bool classof(const OpenACCClause *) { return false; }

  using child_iterator = StmtIterator;
  using const_child_iterator = ConstStmtIterator;
  using child_range = llvm::iterator_range<child_iterator>;
  using const_child_range = llvm::iterator_range<const_child_iterator>;

  child_range children();
  const_child_range children() const {
    auto Children = const_cast<OpenACCClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  virtual ~OpenACCClause() = default;
};

// Represents the 'auto' clause.
class OpenACCAutoClause : public OpenACCClause {
protected:
  OpenACCAutoClause(SourceLocation BeginLoc, SourceLocation EndLoc)
      : OpenACCClause(OpenACCClauseKind::Auto, BeginLoc, EndLoc) {}

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Auto;
  }

  static OpenACCAutoClause *
  Create(const ASTContext &Ctx, SourceLocation BeginLoc, SourceLocation EndLoc);

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

// Represents the 'independent' clause.
class OpenACCIndependentClause : public OpenACCClause {
protected:
  OpenACCIndependentClause(SourceLocation BeginLoc, SourceLocation EndLoc)
      : OpenACCClause(OpenACCClauseKind::Independent, BeginLoc, EndLoc) {}

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Independent;
  }

  static OpenACCIndependentClause *
  Create(const ASTContext &Ctx, SourceLocation BeginLoc, SourceLocation EndLoc);

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};
// Represents the 'seq' clause.
class OpenACCSeqClause : public OpenACCClause {
protected:
  OpenACCSeqClause(SourceLocation BeginLoc, SourceLocation EndLoc)
      : OpenACCClause(OpenACCClauseKind::Seq, BeginLoc, EndLoc) {}

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Seq;
  }

  static OpenACCSeqClause *
  Create(const ASTContext &Ctx, SourceLocation BeginLoc, SourceLocation EndLoc);

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

// Not yet implemented, but the type name is necessary for 'seq' diagnostics, so
// this provides a basic, do-nothing implementation. We still need to add this
// type to the visitors/etc, as well as get it to take its proper arguments.
class OpenACCGangClause : public OpenACCClause {
protected:
  OpenACCGangClause(SourceLocation BeginLoc, SourceLocation EndLoc)
      : OpenACCClause(OpenACCClauseKind::Gang, BeginLoc, EndLoc) {
    llvm_unreachable("Not yet implemented");
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Gang;
  }

  static OpenACCGangClause *
  Create(const ASTContext &Ctx, SourceLocation BeginLoc, SourceLocation EndLoc);

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

// Not yet implemented, but the type name is necessary for 'seq' diagnostics, so
// this provides a basic, do-nothing implementation. We still need to add this
// type to the visitors/etc, as well as get it to take its proper arguments.
class OpenACCVectorClause : public OpenACCClause {
protected:
  OpenACCVectorClause(SourceLocation BeginLoc, SourceLocation EndLoc)
      : OpenACCClause(OpenACCClauseKind::Vector, BeginLoc, EndLoc) {
    llvm_unreachable("Not yet implemented");
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Gang;
  }

  static OpenACCVectorClause *
  Create(const ASTContext &Ctx, SourceLocation BeginLoc, SourceLocation EndLoc);

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

// Not yet implemented, but the type name is necessary for 'seq' diagnostics, so
// this provides a basic, do-nothing implementation. We still need to add this
// type to the visitors/etc, as well as get it to take its proper arguments.
class OpenACCWorkerClause : public OpenACCClause {
protected:
  OpenACCWorkerClause(SourceLocation BeginLoc, SourceLocation EndLoc)
      : OpenACCClause(OpenACCClauseKind::Gang, BeginLoc, EndLoc) {
    llvm_unreachable("Not yet implemented");
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Gang;
  }

  static OpenACCWorkerClause *
  Create(const ASTContext &Ctx, SourceLocation BeginLoc, SourceLocation EndLoc);

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

/// Represents a clause that has a list of parameters.
class OpenACCClauseWithParams : public OpenACCClause {
  /// Location of the '('.
  SourceLocation LParenLoc;

protected:
  OpenACCClauseWithParams(OpenACCClauseKind K, SourceLocation BeginLoc,
                          SourceLocation LParenLoc, SourceLocation EndLoc)
      : OpenACCClause(K, BeginLoc, EndLoc), LParenLoc(LParenLoc) {}

public:
  static bool classof(const OpenACCClause *C);

  SourceLocation getLParenLoc() const { return LParenLoc; }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

using DeviceTypeArgument = std::pair<IdentifierInfo *, SourceLocation>;
/// A 'device_type' or 'dtype' clause, takes a list of either an 'asterisk' or
/// an identifier. The 'asterisk' means 'the rest'.
class OpenACCDeviceTypeClause final
    : public OpenACCClauseWithParams,
      public llvm::TrailingObjects<OpenACCDeviceTypeClause,
                                   DeviceTypeArgument> {
  // Data stored in trailing objects as IdentifierInfo* /SourceLocation pairs. A
  // nullptr IdentifierInfo* represents an asterisk.
  unsigned NumArchs;
  OpenACCDeviceTypeClause(OpenACCClauseKind K, SourceLocation BeginLoc,
                          SourceLocation LParenLoc,
                          ArrayRef<DeviceTypeArgument> Archs,
                          SourceLocation EndLoc)
      : OpenACCClauseWithParams(K, BeginLoc, LParenLoc, EndLoc),
        NumArchs(Archs.size()) {
    assert(
        (K == OpenACCClauseKind::DeviceType || K == OpenACCClauseKind::DType) &&
        "Invalid clause kind for device-type");

    assert(!llvm::any_of(Archs, [](const DeviceTypeArgument &Arg) {
      return Arg.second.isInvalid();
    }) && "Invalid SourceLocation for an argument");

    assert(
        (Archs.size() == 1 || !llvm::any_of(Archs,
                                            [](const DeviceTypeArgument &Arg) {
                                              return Arg.first == nullptr;
                                            })) &&
        "Only a single asterisk version is permitted, and must be the "
        "only one");

    std::uninitialized_copy(Archs.begin(), Archs.end(),
                            getTrailingObjects<DeviceTypeArgument>());
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::DType ||
           C->getClauseKind() == OpenACCClauseKind::DeviceType;
  }
  bool hasAsterisk() const {
    return getArchitectures().size() > 0 &&
           getArchitectures()[0].first == nullptr;
  }

  ArrayRef<DeviceTypeArgument> getArchitectures() const {
    return ArrayRef<DeviceTypeArgument>(
        getTrailingObjects<DeviceTypeArgument>(), NumArchs);
  }

  static OpenACCDeviceTypeClause *
  Create(const ASTContext &C, OpenACCClauseKind K, SourceLocation BeginLoc,
         SourceLocation LParenLoc, ArrayRef<DeviceTypeArgument> Archs,
         SourceLocation EndLoc);
};

/// A 'default' clause, has the optional 'none' or 'present' argument.
class OpenACCDefaultClause : public OpenACCClauseWithParams {
  friend class ASTReaderStmt;
  friend class ASTWriterStmt;

  OpenACCDefaultClauseKind DefaultClauseKind;

protected:
  OpenACCDefaultClause(OpenACCDefaultClauseKind K, SourceLocation BeginLoc,
                       SourceLocation LParenLoc, SourceLocation EndLoc)
      : OpenACCClauseWithParams(OpenACCClauseKind::Default, BeginLoc, LParenLoc,
                                EndLoc),
        DefaultClauseKind(K) {
    assert((DefaultClauseKind == OpenACCDefaultClauseKind::None ||
            DefaultClauseKind == OpenACCDefaultClauseKind::Present) &&
           "Invalid Clause Kind");
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Default;
  }
  OpenACCDefaultClauseKind getDefaultClauseKind() const {
    return DefaultClauseKind;
  }

  static OpenACCDefaultClause *Create(const ASTContext &C,
                                      OpenACCDefaultClauseKind K,
                                      SourceLocation BeginLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);
};

/// Represents one of the handful of classes that has an optional/required
/// 'condition' expression as an argument.
class OpenACCClauseWithCondition : public OpenACCClauseWithParams {
  Expr *ConditionExpr = nullptr;

protected:
  OpenACCClauseWithCondition(OpenACCClauseKind K, SourceLocation BeginLoc,
                             SourceLocation LParenLoc, Expr *ConditionExpr,
                             SourceLocation EndLoc)
      : OpenACCClauseWithParams(K, BeginLoc, LParenLoc, EndLoc),
        ConditionExpr(ConditionExpr) {}

public:
  static bool classof(const OpenACCClause *C);

  bool hasConditionExpr() const { return ConditionExpr; }
  const Expr *getConditionExpr() const { return ConditionExpr; }
  Expr *getConditionExpr() { return ConditionExpr; }

  child_range children() {
    if (ConditionExpr)
      return child_range(reinterpret_cast<Stmt **>(&ConditionExpr),
                         reinterpret_cast<Stmt **>(&ConditionExpr + 1));
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    if (ConditionExpr)
      return const_child_range(
          reinterpret_cast<Stmt *const *>(&ConditionExpr),
          reinterpret_cast<Stmt *const *>(&ConditionExpr + 1));
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

/// An 'if' clause, which has a required condition expression.
class OpenACCIfClause : public OpenACCClauseWithCondition {
protected:
  OpenACCIfClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                  Expr *ConditionExpr, SourceLocation EndLoc);

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::If;
  }
  static OpenACCIfClause *Create(const ASTContext &C, SourceLocation BeginLoc,
                                 SourceLocation LParenLoc, Expr *ConditionExpr,
                                 SourceLocation EndLoc);
};

/// A 'self' clause, which has an optional condition expression.
class OpenACCSelfClause : public OpenACCClauseWithCondition {
  OpenACCSelfClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                    Expr *ConditionExpr, SourceLocation EndLoc);

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Self;
  }
  static OpenACCSelfClause *Create(const ASTContext &C, SourceLocation BeginLoc,
                                   SourceLocation LParenLoc,
                                   Expr *ConditionExpr, SourceLocation EndLoc);
};

/// Represents a clause that has one or more expressions associated with it.
class OpenACCClauseWithExprs : public OpenACCClauseWithParams {
  MutableArrayRef<Expr *> Exprs;

protected:
  OpenACCClauseWithExprs(OpenACCClauseKind K, SourceLocation BeginLoc,
                         SourceLocation LParenLoc, SourceLocation EndLoc)
      : OpenACCClauseWithParams(K, BeginLoc, LParenLoc, EndLoc) {}

  /// Used only for initialization, the leaf class can initialize this to
  /// trailing storage.
  void setExprs(MutableArrayRef<Expr *> NewExprs) {
    assert(Exprs.empty() && "Cannot change Exprs list");
    Exprs = NewExprs;
  }

  /// Gets the entire list of expressions, but leave it to the
  /// individual clauses to expose this how they'd like.
  llvm::ArrayRef<Expr *> getExprs() const { return Exprs; }

public:
  static bool classof(const OpenACCClause *C);
  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(Exprs.begin()),
                       reinterpret_cast<Stmt **>(Exprs.end()));
  }

  const_child_range children() const {
    child_range Children =
        const_cast<OpenACCClauseWithExprs *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }
};

// Represents the 'devnum' and expressions lists for the 'wait' clause.
class OpenACCWaitClause final
    : public OpenACCClauseWithExprs,
      public llvm::TrailingObjects<OpenACCWaitClause, Expr *> {
  SourceLocation QueuesLoc;
  OpenACCWaitClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                    Expr *DevNumExpr, SourceLocation QueuesLoc,
                    ArrayRef<Expr *> QueueIdExprs, SourceLocation EndLoc)
      : OpenACCClauseWithExprs(OpenACCClauseKind::Wait, BeginLoc, LParenLoc,
                               EndLoc),
        QueuesLoc(QueuesLoc) {
    // The first element of the trailing storage is always the devnum expr,
    // whether it is used or not.
    std::uninitialized_copy(&DevNumExpr, &DevNumExpr + 1,
                            getTrailingObjects<Expr *>());
    std::uninitialized_copy(QueueIdExprs.begin(), QueueIdExprs.end(),
                            getTrailingObjects<Expr *>() + 1);
    setExprs(
        MutableArrayRef(getTrailingObjects<Expr *>(), QueueIdExprs.size() + 1));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Wait;
  }
  static OpenACCWaitClause *Create(const ASTContext &C, SourceLocation BeginLoc,
                                   SourceLocation LParenLoc, Expr *DevNumExpr,
                                   SourceLocation QueuesLoc,
                                   ArrayRef<Expr *> QueueIdExprs,
                                   SourceLocation EndLoc);

  bool hasQueuesTag() const { return !QueuesLoc.isInvalid(); }
  SourceLocation getQueuesLoc() const { return QueuesLoc; }
  bool hasDevNumExpr() const { return getExprs()[0]; }
  Expr *getDevNumExpr() const { return getExprs()[0]; }
  llvm::ArrayRef<Expr *> getQueueIdExprs() {
    return OpenACCClauseWithExprs::getExprs().drop_front();
  }
  llvm::ArrayRef<Expr *> getQueueIdExprs() const {
    return OpenACCClauseWithExprs::getExprs().drop_front();
  }
};

class OpenACCNumGangsClause final
    : public OpenACCClauseWithExprs,
      public llvm::TrailingObjects<OpenACCNumGangsClause, Expr *> {

  OpenACCNumGangsClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                        ArrayRef<Expr *> IntExprs, SourceLocation EndLoc)
      : OpenACCClauseWithExprs(OpenACCClauseKind::NumGangs, BeginLoc, LParenLoc,
                               EndLoc) {
    std::uninitialized_copy(IntExprs.begin(), IntExprs.end(),
                            getTrailingObjects<Expr *>());
    setExprs(MutableArrayRef(getTrailingObjects<Expr *>(), IntExprs.size()));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::NumGangs;
  }
  static OpenACCNumGangsClause *
  Create(const ASTContext &C, SourceLocation BeginLoc, SourceLocation LParenLoc,
         ArrayRef<Expr *> IntExprs, SourceLocation EndLoc);

  llvm::ArrayRef<Expr *> getIntExprs() {
    return OpenACCClauseWithExprs::getExprs();
  }

  llvm::ArrayRef<Expr *> getIntExprs() const {
    return OpenACCClauseWithExprs::getExprs();
  }
};

/// Represents one of a handful of clauses that have a single integer
/// expression.
class OpenACCClauseWithSingleIntExpr : public OpenACCClauseWithExprs {
  Expr *IntExpr;

protected:
  OpenACCClauseWithSingleIntExpr(OpenACCClauseKind K, SourceLocation BeginLoc,
                                 SourceLocation LParenLoc, Expr *IntExpr,
                                 SourceLocation EndLoc)
      : OpenACCClauseWithExprs(K, BeginLoc, LParenLoc, EndLoc),
        IntExpr(IntExpr) {
    if (IntExpr)
      setExprs(MutableArrayRef<Expr *>{&this->IntExpr, 1});
  }

public:
  static bool classof(const OpenACCClause *C);
  bool hasIntExpr() const { return !getExprs().empty(); }
  const Expr *getIntExpr() const {
    return hasIntExpr() ? getExprs()[0] : nullptr;
  }

  Expr *getIntExpr() { return hasIntExpr() ? getExprs()[0] : nullptr; };
};

class OpenACCNumWorkersClause : public OpenACCClauseWithSingleIntExpr {
  OpenACCNumWorkersClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                          Expr *IntExpr, SourceLocation EndLoc);

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::NumWorkers;
  }
  static OpenACCNumWorkersClause *Create(const ASTContext &C,
                                         SourceLocation BeginLoc,
                                         SourceLocation LParenLoc,
                                         Expr *IntExpr, SourceLocation EndLoc);
};

class OpenACCVectorLengthClause : public OpenACCClauseWithSingleIntExpr {
  OpenACCVectorLengthClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                            Expr *IntExpr, SourceLocation EndLoc);

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::VectorLength;
  }
  static OpenACCVectorLengthClause *
  Create(const ASTContext &C, SourceLocation BeginLoc, SourceLocation LParenLoc,
         Expr *IntExpr, SourceLocation EndLoc);
};

class OpenACCAsyncClause : public OpenACCClauseWithSingleIntExpr {
  OpenACCAsyncClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                     Expr *IntExpr, SourceLocation EndLoc);

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Async;
  }
  static OpenACCAsyncClause *Create(const ASTContext &C,
                                    SourceLocation BeginLoc,
                                    SourceLocation LParenLoc, Expr *IntExpr,
                                    SourceLocation EndLoc);
};

/// Represents a clause with one or more 'var' objects, represented as an expr,
/// as its arguments. Var-list is expected to be stored in trailing storage.
/// For now, we're just storing the original expression in its entirety, unlike
/// OMP which has to do a bunch of work to create a private.
class OpenACCClauseWithVarList : public OpenACCClauseWithExprs {
protected:
  OpenACCClauseWithVarList(OpenACCClauseKind K, SourceLocation BeginLoc,
                           SourceLocation LParenLoc, SourceLocation EndLoc)
      : OpenACCClauseWithExprs(K, BeginLoc, LParenLoc, EndLoc) {}

public:
  static bool classof(const OpenACCClause *C);
  ArrayRef<Expr *> getVarList() { return getExprs(); }
  ArrayRef<Expr *> getVarList() const { return getExprs(); }
};

class OpenACCPrivateClause final
    : public OpenACCClauseWithVarList,
      public llvm::TrailingObjects<OpenACCPrivateClause, Expr *> {

  OpenACCPrivateClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                       ArrayRef<Expr *> VarList, SourceLocation EndLoc)
      : OpenACCClauseWithVarList(OpenACCClauseKind::Private, BeginLoc,
                                 LParenLoc, EndLoc) {
    std::uninitialized_copy(VarList.begin(), VarList.end(),
                            getTrailingObjects<Expr *>());
    setExprs(MutableArrayRef(getTrailingObjects<Expr *>(), VarList.size()));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Private;
  }
  static OpenACCPrivateClause *
  Create(const ASTContext &C, SourceLocation BeginLoc, SourceLocation LParenLoc,
         ArrayRef<Expr *> VarList, SourceLocation EndLoc);
};

class OpenACCFirstPrivateClause final
    : public OpenACCClauseWithVarList,
      public llvm::TrailingObjects<OpenACCFirstPrivateClause, Expr *> {

  OpenACCFirstPrivateClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                            ArrayRef<Expr *> VarList, SourceLocation EndLoc)
      : OpenACCClauseWithVarList(OpenACCClauseKind::FirstPrivate, BeginLoc,
                                 LParenLoc, EndLoc) {
    std::uninitialized_copy(VarList.begin(), VarList.end(),
                            getTrailingObjects<Expr *>());
    setExprs(MutableArrayRef(getTrailingObjects<Expr *>(), VarList.size()));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::FirstPrivate;
  }
  static OpenACCFirstPrivateClause *
  Create(const ASTContext &C, SourceLocation BeginLoc, SourceLocation LParenLoc,
         ArrayRef<Expr *> VarList, SourceLocation EndLoc);
};

class OpenACCDevicePtrClause final
    : public OpenACCClauseWithVarList,
      public llvm::TrailingObjects<OpenACCDevicePtrClause, Expr *> {

  OpenACCDevicePtrClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                         ArrayRef<Expr *> VarList, SourceLocation EndLoc)
      : OpenACCClauseWithVarList(OpenACCClauseKind::DevicePtr, BeginLoc,
                                 LParenLoc, EndLoc) {
    std::uninitialized_copy(VarList.begin(), VarList.end(),
                            getTrailingObjects<Expr *>());
    setExprs(MutableArrayRef(getTrailingObjects<Expr *>(), VarList.size()));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::DevicePtr;
  }
  static OpenACCDevicePtrClause *
  Create(const ASTContext &C, SourceLocation BeginLoc, SourceLocation LParenLoc,
         ArrayRef<Expr *> VarList, SourceLocation EndLoc);
};

class OpenACCAttachClause final
    : public OpenACCClauseWithVarList,
      public llvm::TrailingObjects<OpenACCAttachClause, Expr *> {

  OpenACCAttachClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                      ArrayRef<Expr *> VarList, SourceLocation EndLoc)
      : OpenACCClauseWithVarList(OpenACCClauseKind::Attach, BeginLoc, LParenLoc,
                                 EndLoc) {
    std::uninitialized_copy(VarList.begin(), VarList.end(),
                            getTrailingObjects<Expr *>());
    setExprs(MutableArrayRef(getTrailingObjects<Expr *>(), VarList.size()));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Attach;
  }
  static OpenACCAttachClause *
  Create(const ASTContext &C, SourceLocation BeginLoc, SourceLocation LParenLoc,
         ArrayRef<Expr *> VarList, SourceLocation EndLoc);
};

class OpenACCNoCreateClause final
    : public OpenACCClauseWithVarList,
      public llvm::TrailingObjects<OpenACCNoCreateClause, Expr *> {

  OpenACCNoCreateClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                        ArrayRef<Expr *> VarList, SourceLocation EndLoc)
      : OpenACCClauseWithVarList(OpenACCClauseKind::NoCreate, BeginLoc,
                                 LParenLoc, EndLoc) {
    std::uninitialized_copy(VarList.begin(), VarList.end(),
                            getTrailingObjects<Expr *>());
    setExprs(MutableArrayRef(getTrailingObjects<Expr *>(), VarList.size()));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::NoCreate;
  }
  static OpenACCNoCreateClause *
  Create(const ASTContext &C, SourceLocation BeginLoc, SourceLocation LParenLoc,
         ArrayRef<Expr *> VarList, SourceLocation EndLoc);
};

class OpenACCPresentClause final
    : public OpenACCClauseWithVarList,
      public llvm::TrailingObjects<OpenACCPresentClause, Expr *> {

  OpenACCPresentClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                       ArrayRef<Expr *> VarList, SourceLocation EndLoc)
      : OpenACCClauseWithVarList(OpenACCClauseKind::Present, BeginLoc,
                                 LParenLoc, EndLoc) {
    std::uninitialized_copy(VarList.begin(), VarList.end(),
                            getTrailingObjects<Expr *>());
    setExprs(MutableArrayRef(getTrailingObjects<Expr *>(), VarList.size()));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Present;
  }
  static OpenACCPresentClause *
  Create(const ASTContext &C, SourceLocation BeginLoc, SourceLocation LParenLoc,
         ArrayRef<Expr *> VarList, SourceLocation EndLoc);
};

class OpenACCCopyClause final
    : public OpenACCClauseWithVarList,
      public llvm::TrailingObjects<OpenACCCopyClause, Expr *> {

  OpenACCCopyClause(OpenACCClauseKind Spelling, SourceLocation BeginLoc,
                    SourceLocation LParenLoc, ArrayRef<Expr *> VarList,
                    SourceLocation EndLoc)
      : OpenACCClauseWithVarList(Spelling, BeginLoc, LParenLoc, EndLoc) {
    assert((Spelling == OpenACCClauseKind::Copy ||
            Spelling == OpenACCClauseKind::PCopy ||
            Spelling == OpenACCClauseKind::PresentOrCopy) &&
           "Invalid clause kind for copy-clause");
    std::uninitialized_copy(VarList.begin(), VarList.end(),
                            getTrailingObjects<Expr *>());
    setExprs(MutableArrayRef(getTrailingObjects<Expr *>(), VarList.size()));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Copy ||
           C->getClauseKind() == OpenACCClauseKind::PCopy ||
           C->getClauseKind() == OpenACCClauseKind::PresentOrCopy;
  }
  static OpenACCCopyClause *
  Create(const ASTContext &C, OpenACCClauseKind Spelling,
         SourceLocation BeginLoc, SourceLocation LParenLoc,
         ArrayRef<Expr *> VarList, SourceLocation EndLoc);
};

class OpenACCCopyInClause final
    : public OpenACCClauseWithVarList,
      public llvm::TrailingObjects<OpenACCCopyInClause, Expr *> {
  bool IsReadOnly;

  OpenACCCopyInClause(OpenACCClauseKind Spelling, SourceLocation BeginLoc,
                      SourceLocation LParenLoc, bool IsReadOnly,
                      ArrayRef<Expr *> VarList, SourceLocation EndLoc)
      : OpenACCClauseWithVarList(Spelling, BeginLoc, LParenLoc, EndLoc),
        IsReadOnly(IsReadOnly) {
    assert((Spelling == OpenACCClauseKind::CopyIn ||
            Spelling == OpenACCClauseKind::PCopyIn ||
            Spelling == OpenACCClauseKind::PresentOrCopyIn) &&
           "Invalid clause kind for copyin-clause");
    std::uninitialized_copy(VarList.begin(), VarList.end(),
                            getTrailingObjects<Expr *>());
    setExprs(MutableArrayRef(getTrailingObjects<Expr *>(), VarList.size()));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::CopyIn ||
           C->getClauseKind() == OpenACCClauseKind::PCopyIn ||
           C->getClauseKind() == OpenACCClauseKind::PresentOrCopyIn;
  }
  bool isReadOnly() const { return IsReadOnly; }
  static OpenACCCopyInClause *
  Create(const ASTContext &C, OpenACCClauseKind Spelling,
         SourceLocation BeginLoc, SourceLocation LParenLoc, bool IsReadOnly,
         ArrayRef<Expr *> VarList, SourceLocation EndLoc);
};

class OpenACCCopyOutClause final
    : public OpenACCClauseWithVarList,
      public llvm::TrailingObjects<OpenACCCopyOutClause, Expr *> {
  bool IsZero;

  OpenACCCopyOutClause(OpenACCClauseKind Spelling, SourceLocation BeginLoc,
                       SourceLocation LParenLoc, bool IsZero,
                       ArrayRef<Expr *> VarList, SourceLocation EndLoc)
      : OpenACCClauseWithVarList(Spelling, BeginLoc, LParenLoc, EndLoc),
        IsZero(IsZero) {
    assert((Spelling == OpenACCClauseKind::CopyOut ||
            Spelling == OpenACCClauseKind::PCopyOut ||
            Spelling == OpenACCClauseKind::PresentOrCopyOut) &&
           "Invalid clause kind for copyout-clause");
    std::uninitialized_copy(VarList.begin(), VarList.end(),
                            getTrailingObjects<Expr *>());
    setExprs(MutableArrayRef(getTrailingObjects<Expr *>(), VarList.size()));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::CopyOut ||
           C->getClauseKind() == OpenACCClauseKind::PCopyOut ||
           C->getClauseKind() == OpenACCClauseKind::PresentOrCopyOut;
  }
  bool isZero() const { return IsZero; }
  static OpenACCCopyOutClause *
  Create(const ASTContext &C, OpenACCClauseKind Spelling,
         SourceLocation BeginLoc, SourceLocation LParenLoc, bool IsZero,
         ArrayRef<Expr *> VarList, SourceLocation EndLoc);
};

class OpenACCCreateClause final
    : public OpenACCClauseWithVarList,
      public llvm::TrailingObjects<OpenACCCreateClause, Expr *> {
  bool IsZero;

  OpenACCCreateClause(OpenACCClauseKind Spelling, SourceLocation BeginLoc,
                      SourceLocation LParenLoc, bool IsZero,
                      ArrayRef<Expr *> VarList, SourceLocation EndLoc)
      : OpenACCClauseWithVarList(Spelling, BeginLoc, LParenLoc, EndLoc),
        IsZero(IsZero) {
    assert((Spelling == OpenACCClauseKind::Create ||
            Spelling == OpenACCClauseKind::PCreate ||
            Spelling == OpenACCClauseKind::PresentOrCreate) &&
           "Invalid clause kind for create-clause");
    std::uninitialized_copy(VarList.begin(), VarList.end(),
                            getTrailingObjects<Expr *>());
    setExprs(MutableArrayRef(getTrailingObjects<Expr *>(), VarList.size()));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Create ||
           C->getClauseKind() == OpenACCClauseKind::PCreate ||
           C->getClauseKind() == OpenACCClauseKind::PresentOrCreate;
  }
  bool isZero() const { return IsZero; }
  static OpenACCCreateClause *
  Create(const ASTContext &C, OpenACCClauseKind Spelling,
         SourceLocation BeginLoc, SourceLocation LParenLoc, bool IsZero,
         ArrayRef<Expr *> VarList, SourceLocation EndLoc);
};

class OpenACCReductionClause final
    : public OpenACCClauseWithVarList,
      public llvm::TrailingObjects<OpenACCReductionClause, Expr *> {
  OpenACCReductionOperator Op;

  OpenACCReductionClause(SourceLocation BeginLoc, SourceLocation LParenLoc,
                         OpenACCReductionOperator Operator,
                         ArrayRef<Expr *> VarList, SourceLocation EndLoc)
      : OpenACCClauseWithVarList(OpenACCClauseKind::Reduction, BeginLoc,
                                 LParenLoc, EndLoc),
        Op(Operator) {
    std::uninitialized_copy(VarList.begin(), VarList.end(),
                            getTrailingObjects<Expr *>());
    setExprs(MutableArrayRef(getTrailingObjects<Expr *>(), VarList.size()));
  }

public:
  static bool classof(const OpenACCClause *C) {
    return C->getClauseKind() == OpenACCClauseKind::Reduction;
  }

  static OpenACCReductionClause *
  Create(const ASTContext &C, SourceLocation BeginLoc, SourceLocation LParenLoc,
         OpenACCReductionOperator Operator, ArrayRef<Expr *> VarList,
         SourceLocation EndLoc);

  OpenACCReductionOperator getReductionOp() const { return Op; }
};

template <class Impl> class OpenACCClauseVisitor {
  Impl &getDerived() { return static_cast<Impl &>(*this); }

public:
  void VisitClauseList(ArrayRef<const OpenACCClause *> List) {
    for (const OpenACCClause *Clause : List)
      Visit(Clause);
  }

  void Visit(const OpenACCClause *C) {
    if (!C)
      return;

    switch (C->getClauseKind()) {
#define VISIT_CLAUSE(CLAUSE_NAME)                                              \
  case OpenACCClauseKind::CLAUSE_NAME:                                         \
    Visit##CLAUSE_NAME##Clause(*cast<OpenACC##CLAUSE_NAME##Clause>(C));        \
    return;
#define CLAUSE_ALIAS(ALIAS_NAME, CLAUSE_NAME, DEPRECATED)                      \
  case OpenACCClauseKind::ALIAS_NAME:                                          \
    Visit##CLAUSE_NAME##Clause(*cast<OpenACC##CLAUSE_NAME##Clause>(C));        \
    return;
#include "clang/Basic/OpenACCClauses.def"

    default:
      llvm_unreachable("Clause visitor not yet implemented");
    }
    llvm_unreachable("Invalid Clause kind");
  }

#define VISIT_CLAUSE(CLAUSE_NAME)                                              \
  void Visit##CLAUSE_NAME##Clause(                                             \
      const OpenACC##CLAUSE_NAME##Clause &Clause) {                            \
    return getDerived().Visit##CLAUSE_NAME##Clause(Clause);                    \
  }

#include "clang/Basic/OpenACCClauses.def"
};

class OpenACCClausePrinter final
    : public OpenACCClauseVisitor<OpenACCClausePrinter> {
  raw_ostream &OS;
  const PrintingPolicy &Policy;

  void printExpr(const Expr *E);

public:
  void VisitClauseList(ArrayRef<const OpenACCClause *> List) {
    for (const OpenACCClause *Clause : List) {
      Visit(Clause);

      if (Clause != List.back())
        OS << ' ';
    }
  }
  OpenACCClausePrinter(raw_ostream &OS, const PrintingPolicy &Policy)
      : OS(OS), Policy(Policy) {}

#define VISIT_CLAUSE(CLAUSE_NAME)                                              \
  void Visit##CLAUSE_NAME##Clause(const OpenACC##CLAUSE_NAME##Clause &Clause);
#include "clang/Basic/OpenACCClauses.def"
};

} // namespace clang

#endif // LLVM_CLANG_AST_OPENACCCLAUSE_H

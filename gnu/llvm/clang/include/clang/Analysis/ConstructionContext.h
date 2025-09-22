//===- ConstructionContext.h - CFG constructor information ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ConstructionContext class and its sub-classes,
// which represent various different ways of constructing C++ objects
// with the additional information the users may want to know about
// the constructor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_CONSTRUCTIONCONTEXT_H
#define LLVM_CLANG_ANALYSIS_CONSTRUCTIONCONTEXT_H

#include "clang/Analysis/Support/BumpVector.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"

namespace clang {

/// Represents a single point (AST node) in the program that requires attention
/// during construction of an object. ConstructionContext would be represented
/// as a list of such items.
class ConstructionContextItem {
public:
  enum ItemKind {
    VariableKind,
    NewAllocatorKind,
    ReturnKind,
    MaterializationKind,
    TemporaryDestructorKind,
    ElidedDestructorKind,
    ElidableConstructorKind,
    ArgumentKind,
    LambdaCaptureKind,
    STATEMENT_WITH_INDEX_KIND_BEGIN = ArgumentKind,
    STATEMENT_WITH_INDEX_KIND_END = LambdaCaptureKind,
    STATEMENT_KIND_BEGIN = VariableKind,
    STATEMENT_KIND_END = LambdaCaptureKind,
    InitializerKind,
    INITIALIZER_KIND_BEGIN = InitializerKind,
    INITIALIZER_KIND_END = InitializerKind
  };

  LLVM_DUMP_METHOD static StringRef getKindAsString(ItemKind K) {
    switch (K) {
      case VariableKind:            return "construct into local variable";
      case NewAllocatorKind:        return "construct into new-allocator";
      case ReturnKind:              return "construct into return address";
      case MaterializationKind:     return "materialize temporary";
      case TemporaryDestructorKind: return "destroy temporary";
      case ElidedDestructorKind:    return "elide destructor";
      case ElidableConstructorKind: return "elide constructor";
      case ArgumentKind:            return "construct into argument";
      case LambdaCaptureKind:
        return "construct into lambda captured variable";
      case InitializerKind:         return "construct into member variable";
    };
    llvm_unreachable("Unknown ItemKind");
  }

private:
  const void *const Data;
  const ItemKind Kind;
  const unsigned Index = 0;

  bool hasStatement() const {
    return Kind >= STATEMENT_KIND_BEGIN &&
           Kind <= STATEMENT_KIND_END;
  }

  bool hasIndex() const {
    return Kind >= STATEMENT_WITH_INDEX_KIND_BEGIN &&
           Kind <= STATEMENT_WITH_INDEX_KIND_END;
  }

  bool hasInitializer() const {
    return Kind >= INITIALIZER_KIND_BEGIN &&
           Kind <= INITIALIZER_KIND_END;
  }

public:
  // ConstructionContextItem should be simple enough so that it was easy to
  // re-construct it from the AST node it captures. For that reason we provide
  // simple implicit conversions from all sorts of supported AST nodes.
  ConstructionContextItem(const DeclStmt *DS)
      : Data(DS), Kind(VariableKind) {}

  ConstructionContextItem(const CXXNewExpr *NE)
      : Data(NE), Kind(NewAllocatorKind) {}

  ConstructionContextItem(const ReturnStmt *RS)
      : Data(RS), Kind(ReturnKind) {}

  ConstructionContextItem(const MaterializeTemporaryExpr *MTE)
      : Data(MTE), Kind(MaterializationKind) {}

  ConstructionContextItem(const CXXBindTemporaryExpr *BTE,
                          bool IsElided = false)
      : Data(BTE),
        Kind(IsElided ? ElidedDestructorKind : TemporaryDestructorKind) {}

  ConstructionContextItem(const CXXConstructExpr *CE)
      : Data(CE), Kind(ElidableConstructorKind) {}

  ConstructionContextItem(const CallExpr *CE, unsigned Index)
      : Data(CE), Kind(ArgumentKind), Index(Index) {}

  ConstructionContextItem(const CXXConstructExpr *CE, unsigned Index)
      : Data(CE), Kind(ArgumentKind), Index(Index) {}

  ConstructionContextItem(const CXXInheritedCtorInitExpr *CE, unsigned Index)
      : Data(CE), Kind(ArgumentKind), Index(Index) {}

  ConstructionContextItem(const ObjCMessageExpr *ME, unsigned Index)
      : Data(ME), Kind(ArgumentKind), Index(Index) {}

  // A polymorphic version of the previous calls with dynamic type check.
  ConstructionContextItem(const Expr *E, unsigned Index)
      : Data(E), Kind(ArgumentKind), Index(Index) {
    assert(isa<CallExpr>(E) || isa<CXXConstructExpr>(E) ||
           isa<CXXDeleteExpr>(E) || isa<CXXInheritedCtorInitExpr>(E) ||
           isa<ObjCMessageExpr>(E));
  }

  ConstructionContextItem(const CXXCtorInitializer *Init)
      : Data(Init), Kind(InitializerKind), Index(0) {}

  ConstructionContextItem(const LambdaExpr *LE, unsigned Index)
      : Data(LE), Kind(LambdaCaptureKind), Index(Index) {}

  ItemKind getKind() const { return Kind; }

  LLVM_DUMP_METHOD StringRef getKindAsString() const {
    return getKindAsString(getKind());
  }

  /// The construction site - the statement that triggered the construction
  /// for one of its parts. For instance, stack variable declaration statement
  /// triggers construction of itself or its elements if it's an array,
  /// new-expression triggers construction of the newly allocated object(s).
  const Stmt *getStmt() const {
    assert(hasStatement());
    return static_cast<const Stmt *>(Data);
  }

  const Stmt *getStmtOrNull() const {
    return hasStatement() ? getStmt() : nullptr;
  }

  /// The construction site is not necessarily a statement. It may also be a
  /// CXXCtorInitializer, which means that a member variable is being
  /// constructed during initialization of the object that contains it.
  const CXXCtorInitializer *getCXXCtorInitializer() const {
    assert(hasInitializer());
    return static_cast<const CXXCtorInitializer *>(Data);
  }

  /// If a single trigger statement triggers multiple constructors, they are
  /// usually being enumerated. This covers function argument constructors
  /// triggered by a call-expression and items in an initializer list triggered
  /// by an init-list-expression.
  unsigned getIndex() const {
    // This is a fairly specific request. Let's make sure the user knows
    // what he's doing.
    assert(hasIndex());
    return Index;
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddPointer(Data);
    ID.AddInteger(Kind);
    ID.AddInteger(Index);
  }

  bool operator==(const ConstructionContextItem &Other) const {
    // For most kinds the Index comparison is trivially true, but
    // checking kind separately doesn't seem to be less expensive
    // than checking Index. Same in operator<().
    return std::make_tuple(Data, Kind, Index) ==
           std::make_tuple(Other.Data, Other.Kind, Other.Index);
  }

  bool operator<(const ConstructionContextItem &Other) const {
    return std::make_tuple(Data, Kind, Index) <
           std::make_tuple(Other.Data, Other.Kind, Other.Index);
  }
};

/// Construction context can be seen as a linked list of multiple layers.
/// Sometimes a single trigger is not enough to describe the construction
/// site. That's what causing us to have a chain of "partial" construction
/// context layers. Some examples:
/// - A constructor within in an aggregate initializer list within a variable
///   would have a construction context of the initializer list with
///   the parent construction context of a variable.
/// - A constructor for a temporary that needs to be both destroyed
///   and materialized into an elidable copy constructor would have a
///   construction context of a CXXBindTemporaryExpr with the parent
///   construction context of a MaterializeTemproraryExpr.
/// Not all of these are currently supported.
/// Layers are created gradually while traversing the AST, and layers that
/// represent the outmost AST nodes are built first, while the node that
/// immediately contains the constructor would be built last and capture the
/// previous layers as its parents. Construction context captures the last layer
/// (which has links to the previous layers) and classifies the seemingly
/// arbitrary chain of layers into one of the possible ways of constructing
/// an object in C++ for user-friendly experience.
class ConstructionContextLayer {
  const ConstructionContextLayer *Parent = nullptr;
  ConstructionContextItem Item;

  ConstructionContextLayer(ConstructionContextItem Item,
                           const ConstructionContextLayer *Parent)
      : Parent(Parent), Item(Item) {}

public:
  static const ConstructionContextLayer *
  create(BumpVectorContext &C, const ConstructionContextItem &Item,
         const ConstructionContextLayer *Parent = nullptr);

  const ConstructionContextItem &getItem() const { return Item; }
  const ConstructionContextLayer *getParent() const { return Parent; }
  bool isLast() const { return !Parent; }

  /// See if Other is a proper initial segment of this construction context
  /// in terms of the parent chain - i.e. a few first parents coincide and
  /// then the other context terminates but our context goes further - i.e.,
  /// we are providing the same context that the other context provides,
  /// and a bit more above that.
  bool isStrictlyMoreSpecificThan(const ConstructionContextLayer *Other) const;
};


/// ConstructionContext's subclasses describe different ways of constructing
/// an object in C++. The context re-captures the essential parent AST nodes
/// of the CXXConstructExpr it is assigned to and presents these nodes
/// through easy-to-understand accessor methods.
class ConstructionContext {
public:
  enum Kind {
    SimpleVariableKind,
    CXX17ElidedCopyVariableKind,
    VARIABLE_BEGIN = SimpleVariableKind,
    VARIABLE_END = CXX17ElidedCopyVariableKind,
    SimpleConstructorInitializerKind,
    CXX17ElidedCopyConstructorInitializerKind,
    INITIALIZER_BEGIN = SimpleConstructorInitializerKind,
    INITIALIZER_END = CXX17ElidedCopyConstructorInitializerKind,
    NewAllocatedObjectKind,
    SimpleTemporaryObjectKind,
    ElidedTemporaryObjectKind,
    TEMPORARY_BEGIN = SimpleTemporaryObjectKind,
    TEMPORARY_END = ElidedTemporaryObjectKind,
    SimpleReturnedValueKind,
    CXX17ElidedCopyReturnedValueKind,
    RETURNED_VALUE_BEGIN = SimpleReturnedValueKind,
    RETURNED_VALUE_END = CXX17ElidedCopyReturnedValueKind,
    ArgumentKind,
    LambdaCaptureKind
  };

protected:
  Kind K;

  // Do not make public! These need to only be constructed
  // via createFromLayers().
  explicit ConstructionContext(Kind K) : K(K) {}

private:
  // A helper function for constructing an instance into a bump vector context.
  template <typename T, typename... ArgTypes>
  static T *create(BumpVectorContext &C, ArgTypes... Args) {
    auto *CC = C.getAllocator().Allocate<T>();
    return new (CC) T(Args...);
  }

  // A sub-routine of createFromLayers() that deals with temporary objects
  // that need to be materialized. The BTE argument is for the situation when
  // the object also needs to be bound for destruction.
  static const ConstructionContext *createMaterializedTemporaryFromLayers(
      BumpVectorContext &C, const MaterializeTemporaryExpr *MTE,
      const CXXBindTemporaryExpr *BTE,
      const ConstructionContextLayer *ParentLayer);

  // A sub-routine of createFromLayers() that deals with temporary objects
  // that need to be bound for destruction. Automatically finds out if the
  // object also needs to be materialized and delegates to
  // createMaterializedTemporaryFromLayers() if necessary.
  static const ConstructionContext *
  createBoundTemporaryFromLayers(
      BumpVectorContext &C, const CXXBindTemporaryExpr *BTE,
      const ConstructionContextLayer *ParentLayer);

public:
  /// Consume the construction context layer, together with its parent layers,
  /// and wrap it up into a complete construction context. May return null
  /// if layers do not form any supported construction context.
  static const ConstructionContext *
  createFromLayers(BumpVectorContext &C,
                   const ConstructionContextLayer *TopLayer);

  Kind getKind() const { return K; }

  virtual const ArrayInitLoopExpr *getArrayInitLoop() const { return nullptr; }

  // Only declared to silence -Wnon-virtual-dtor warnings.
  virtual ~ConstructionContext() = default;
};

/// An abstract base class for local variable constructors.
class VariableConstructionContext : public ConstructionContext {
  const DeclStmt *DS;

protected:
  VariableConstructionContext(ConstructionContext::Kind K, const DeclStmt *DS)
      : ConstructionContext(K), DS(DS) {
    assert(classof(this));
    assert(DS);
  }

public:
  const DeclStmt *getDeclStmt() const { return DS; }

  const ArrayInitLoopExpr *getArrayInitLoop() const override {
    const auto *Var = cast<VarDecl>(DS->getSingleDecl());

    return dyn_cast<ArrayInitLoopExpr>(Var->getInit());
  }

  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() >= VARIABLE_BEGIN &&
           CC->getKind() <= VARIABLE_END;
  }
};

/// Represents construction into a simple local variable, eg. T var(123);.
/// If a variable has an initializer, eg. T var = makeT();, then the final
/// elidable copy-constructor from makeT() into var would also be a simple
/// variable constructor handled by this class.
class SimpleVariableConstructionContext : public VariableConstructionContext {
  friend class ConstructionContext; // Allows to create<>() itself.

  explicit SimpleVariableConstructionContext(const DeclStmt *DS)
      : VariableConstructionContext(ConstructionContext::SimpleVariableKind,
                                    DS) {}

public:
  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() == SimpleVariableKind;
  }
};

/// Represents construction into a simple variable with an initializer syntax,
/// with a single constructor, eg. T var = makeT();. Such construction context
/// may only appear in C++17 because previously it was split into a temporary
/// object constructor and an elidable simple variable copy-constructor and
/// we were producing separate construction contexts for these constructors.
/// In C++17 we have a single construction context that combines both.
/// Note that if the object has trivial destructor, then this code is
/// indistinguishable from a simple variable constructor on the AST level;
/// in this case we provide a simple variable construction context.
class CXX17ElidedCopyVariableConstructionContext
    : public VariableConstructionContext {
  const CXXBindTemporaryExpr *BTE;

  friend class ConstructionContext; // Allows to create<>() itself.

  explicit CXX17ElidedCopyVariableConstructionContext(
      const DeclStmt *DS, const CXXBindTemporaryExpr *BTE)
      : VariableConstructionContext(CXX17ElidedCopyVariableKind, DS), BTE(BTE) {
    assert(BTE);
  }

public:
  const CXXBindTemporaryExpr *getCXXBindTemporaryExpr() const { return BTE; }

  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() == CXX17ElidedCopyVariableKind;
  }
};

// An abstract base class for constructor-initializer-based constructors.
class ConstructorInitializerConstructionContext : public ConstructionContext {
  const CXXCtorInitializer *I;

protected:
  explicit ConstructorInitializerConstructionContext(
      ConstructionContext::Kind K, const CXXCtorInitializer *I)
      : ConstructionContext(K), I(I) {
    assert(classof(this));
    assert(I);
  }

public:
  const CXXCtorInitializer *getCXXCtorInitializer() const { return I; }

  const ArrayInitLoopExpr *getArrayInitLoop() const override {
    return dyn_cast<ArrayInitLoopExpr>(I->getInit());
  }

  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() >= INITIALIZER_BEGIN &&
           CC->getKind() <= INITIALIZER_END;
  }
};

/// Represents construction into a field or a base class within a bigger object
/// via a constructor initializer, eg. T(): field(123) { ... }.
class SimpleConstructorInitializerConstructionContext
    : public ConstructorInitializerConstructionContext {
  friend class ConstructionContext; // Allows to create<>() itself.

  explicit SimpleConstructorInitializerConstructionContext(
      const CXXCtorInitializer *I)
      : ConstructorInitializerConstructionContext(
            ConstructionContext::SimpleConstructorInitializerKind, I) {}

public:
  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() == SimpleConstructorInitializerKind;
  }
};

/// Represents construction into a field or a base class within a bigger object
/// via a constructor initializer, with a single constructor, eg.
/// T(): field(Field(123)) { ... }. Such construction context may only appear
/// in C++17 because previously it was split into a temporary object constructor
/// and an elidable simple constructor-initializer copy-constructor and we were
/// producing separate construction contexts for these constructors. In C++17
/// we have a single construction context that combines both. Note that if the
/// object has trivial destructor, then this code is indistinguishable from
/// a simple constructor-initializer constructor on the AST level; in this case
/// we provide a simple constructor-initializer construction context.
class CXX17ElidedCopyConstructorInitializerConstructionContext
    : public ConstructorInitializerConstructionContext {
  const CXXBindTemporaryExpr *BTE;

  friend class ConstructionContext; // Allows to create<>() itself.

  explicit CXX17ElidedCopyConstructorInitializerConstructionContext(
      const CXXCtorInitializer *I, const CXXBindTemporaryExpr *BTE)
      : ConstructorInitializerConstructionContext(
            CXX17ElidedCopyConstructorInitializerKind, I),
        BTE(BTE) {
    assert(BTE);
  }

public:
  const CXXBindTemporaryExpr *getCXXBindTemporaryExpr() const { return BTE; }

  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() == CXX17ElidedCopyConstructorInitializerKind;
  }
};

/// Represents immediate initialization of memory allocated by operator new,
/// eg. new T(123);.
class NewAllocatedObjectConstructionContext : public ConstructionContext {
  const CXXNewExpr *NE;

  friend class ConstructionContext; // Allows to create<>() itself.

  explicit NewAllocatedObjectConstructionContext(const CXXNewExpr *NE)
      : ConstructionContext(ConstructionContext::NewAllocatedObjectKind),
        NE(NE) {
    assert(NE);
  }

public:
  const CXXNewExpr *getCXXNewExpr() const { return NE; }

  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() == NewAllocatedObjectKind;
  }
};

/// Represents a temporary object, eg. T(123), that does not immediately cross
/// function boundaries "by value"; constructors that construct function
/// value-type arguments or values that are immediately returned from the
/// function that returns a value receive separate construction context kinds.
class TemporaryObjectConstructionContext : public ConstructionContext {
  const CXXBindTemporaryExpr *BTE;
  const MaterializeTemporaryExpr *MTE;

protected:
  explicit TemporaryObjectConstructionContext(
      ConstructionContext::Kind K, const CXXBindTemporaryExpr *BTE,
      const MaterializeTemporaryExpr *MTE)
      : ConstructionContext(K), BTE(BTE), MTE(MTE) {
    // Both BTE and MTE can be null here, all combinations possible.
    // Even though for now at least one should be non-null, we simply haven't
    // implemented the other case yet (this would be a temporary in the middle
    // of nowhere that doesn't have a non-trivial destructor).
  }

public:
  /// CXXBindTemporaryExpr here is non-null as long as the temporary has
  /// a non-trivial destructor.
  const CXXBindTemporaryExpr *getCXXBindTemporaryExpr() const {
    return BTE;
  }

  /// MaterializeTemporaryExpr is non-null as long as the temporary is actually
  /// used after construction, eg. by binding to a reference (lifetime
  /// extension), accessing a field, calling a method, or passing it into
  /// a function (an elidable copy or move constructor would be a common
  /// example) by reference.
  const MaterializeTemporaryExpr *getMaterializedTemporaryExpr() const {
    return MTE;
  }

  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() >= TEMPORARY_BEGIN && CC->getKind() <= TEMPORARY_END;
  }
};

/// Represents a temporary object that is not constructed for the purpose of
/// being immediately copied/moved by an elidable copy/move-constructor.
/// This includes temporary objects "in the middle of nowhere" like T(123) and
/// lifetime-extended temporaries.
class SimpleTemporaryObjectConstructionContext
    : public TemporaryObjectConstructionContext {
  friend class ConstructionContext; // Allows to create<>() itself.

  explicit SimpleTemporaryObjectConstructionContext(
      const CXXBindTemporaryExpr *BTE, const MaterializeTemporaryExpr *MTE)
      : TemporaryObjectConstructionContext(
            ConstructionContext::SimpleTemporaryObjectKind, BTE, MTE) {}

public:
  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() == SimpleTemporaryObjectKind;
  }
};

/// Represents a temporary object that is constructed for the sole purpose
/// of being immediately copied by an elidable copy/move constructor.
/// For example, T t = T(123); includes a temporary T(123) that is immediately
/// copied to variable t. In such cases the elidable copy can (but not
/// necessarily should) be omitted ("elided") according to the rules of the
/// language; the constructor would then construct variable t directly.
/// This construction context contains information of the elidable constructor
/// and its respective construction context.
class ElidedTemporaryObjectConstructionContext
    : public TemporaryObjectConstructionContext {
  const CXXConstructExpr *ElidedCE;
  const ConstructionContext *ElidedCC;

  friend class ConstructionContext; // Allows to create<>() itself.

  explicit ElidedTemporaryObjectConstructionContext(
      const CXXBindTemporaryExpr *BTE, const MaterializeTemporaryExpr *MTE,
      const CXXConstructExpr *ElidedCE, const ConstructionContext *ElidedCC)
      : TemporaryObjectConstructionContext(
            ConstructionContext::ElidedTemporaryObjectKind, BTE, MTE),
        ElidedCE(ElidedCE), ElidedCC(ElidedCC) {
    // Elided constructor and its context should be either both specified
    // or both unspecified. In the former case, the constructor must be
    // elidable.
    assert(ElidedCE && ElidedCE->isElidable() && ElidedCC);
  }

public:
  const CXXConstructExpr *getConstructorAfterElision() const {
    return ElidedCE;
  }

  const ConstructionContext *getConstructionContextAfterElision() const {
    return ElidedCC;
  }

  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() == ElidedTemporaryObjectKind;
  }
};

class ReturnedValueConstructionContext : public ConstructionContext {
  const ReturnStmt *RS;

protected:
  explicit ReturnedValueConstructionContext(ConstructionContext::Kind K,
                                            const ReturnStmt *RS)
      : ConstructionContext(K), RS(RS) {
    assert(classof(this));
    assert(RS);
  }

public:
  const ReturnStmt *getReturnStmt() const { return RS; }

  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() >= RETURNED_VALUE_BEGIN &&
           CC->getKind() <= RETURNED_VALUE_END;
  }
};

/// Represents a temporary object that is being immediately returned from a
/// function by value, eg. return t; or return T(123);. In this case there is
/// always going to be a constructor at the return site. However, the usual
/// temporary-related bureaucracy (CXXBindTemporaryExpr,
/// MaterializeTemporaryExpr) is normally located in the caller function's AST.
class SimpleReturnedValueConstructionContext
    : public ReturnedValueConstructionContext {
  friend class ConstructionContext; // Allows to create<>() itself.

  explicit SimpleReturnedValueConstructionContext(const ReturnStmt *RS)
      : ReturnedValueConstructionContext(
            ConstructionContext::SimpleReturnedValueKind, RS) {}

public:
  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() == SimpleReturnedValueKind;
  }
};

/// Represents a temporary object that is being immediately returned from a
/// function by value, eg. return t; or return T(123); in C++17.
/// In C++17 there is not going to be an elidable copy constructor at the
/// return site.  However, the usual temporary-related bureaucracy (CXXBindTemporaryExpr,
/// MaterializeTemporaryExpr) is normally located in the caller function's AST.
/// Note that if the object has trivial destructor, then this code is
/// indistinguishable from a simple returned value constructor on the AST level;
/// in this case we provide a simple returned value construction context.
class CXX17ElidedCopyReturnedValueConstructionContext
    : public ReturnedValueConstructionContext {
  const CXXBindTemporaryExpr *BTE;

  friend class ConstructionContext; // Allows to create<>() itself.

  explicit CXX17ElidedCopyReturnedValueConstructionContext(
      const ReturnStmt *RS, const CXXBindTemporaryExpr *BTE)
      : ReturnedValueConstructionContext(
            ConstructionContext::CXX17ElidedCopyReturnedValueKind, RS),
        BTE(BTE) {
    assert(BTE);
  }

public:
  const CXXBindTemporaryExpr *getCXXBindTemporaryExpr() const { return BTE; }

  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() == CXX17ElidedCopyReturnedValueKind;
  }
};

class ArgumentConstructionContext : public ConstructionContext {
  // The call of which the context is an argument.
  const Expr *CE;

  // Which argument we're constructing. Note that when numbering between
  // arguments and parameters is inconsistent (eg., operator calls),
  // this is the index of the argument, not of the parameter.
  unsigned Index;

  // Whether the object needs to be destroyed.
  const CXXBindTemporaryExpr *BTE;

  friend class ConstructionContext; // Allows to create<>() itself.

  explicit ArgumentConstructionContext(const Expr *CE, unsigned Index,
                                       const CXXBindTemporaryExpr *BTE)
      : ConstructionContext(ArgumentKind), CE(CE),
        Index(Index), BTE(BTE) {
    assert(isa<CallExpr>(CE) || isa<CXXConstructExpr>(CE) ||
           isa<ObjCMessageExpr>(CE));
    // BTE is optional.
  }

public:
  const Expr *getCallLikeExpr() const { return CE; }
  unsigned getIndex() const { return Index; }
  const CXXBindTemporaryExpr *getCXXBindTemporaryExpr() const { return BTE; }

  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() == ArgumentKind;
  }
};

class LambdaCaptureConstructionContext : public ConstructionContext {
  // The lambda of which the initializer we capture.
  const LambdaExpr *LE;

  // Index of the captured element in the captured list.
  unsigned Index;

  friend class ConstructionContext; // Allows to create<>() itself.

  explicit LambdaCaptureConstructionContext(const LambdaExpr *LE,
                                            unsigned Index)
      : ConstructionContext(LambdaCaptureKind), LE(LE), Index(Index) {}

public:
  const LambdaExpr *getLambdaExpr() const { return LE; }
  unsigned getIndex() const { return Index; }

  const Expr *getInitializer() const {
    return *(LE->capture_init_begin() + Index);
  }

  const FieldDecl *getFieldDecl() const {
    auto It = LE->getLambdaClass()->field_begin();
    std::advance(It, Index);
    return *It;
  }

  const ArrayInitLoopExpr *getArrayInitLoop() const override {
    return dyn_cast_or_null<ArrayInitLoopExpr>(getInitializer());
  }

  static bool classof(const ConstructionContext *CC) {
    return CC->getKind() == LambdaCaptureKind;
  }
};

} // end namespace clang

#endif // LLVM_CLANG_ANALYSIS_CONSTRUCTIONCONTEXT_H

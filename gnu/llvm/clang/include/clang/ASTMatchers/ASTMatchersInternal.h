//===- ASTMatchersInternal.h - Structural query framework -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Implements the base layer of the matcher framework.
//
//  Matchers are methods that return a Matcher<T> which provides a method
//  Matches(...) which is a predicate on an AST node. The Matches method's
//  parameters define the context of the match, which allows matchers to recurse
//  or store the current node as bound to a specific string, so that it can be
//  retrieved later.
//
//  In general, matchers have two parts:
//  1. A function Matcher<T> MatcherName(<arguments>) which returns a Matcher<T>
//     based on the arguments and optionally on template type deduction based
//     on the arguments. Matcher<T>s form an implicit reverse hierarchy
//     to clang's AST class hierarchy, meaning that you can use a Matcher<Base>
//     everywhere a Matcher<Derived> is required.
//  2. An implementation of a class derived from MatcherInterface<T>.
//
//  The matcher functions are defined in ASTMatchers.h. To make it possible
//  to implement both the matcher function and the implementation of the matcher
//  interface in one place, ASTMatcherMacros.h defines macros that allow
//  implementing a matcher in a single place.
//
//  This file contains the base classes needed to construct the actual matchers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ASTMATCHERS_ASTMATCHERSINTERNAL_H
#define LLVM_CLANG_ASTMATCHERS_ASTMATCHERSINTERNAL_H

#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Regex.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace clang {

class ASTContext;

namespace ast_matchers {

class BoundNodes;

namespace internal {

/// A type-list implementation.
///
/// A "linked list" of types, accessible by using the ::head and ::tail
/// typedefs.
template <typename... Ts> struct TypeList {}; // Empty sentinel type list.

template <typename T1, typename... Ts> struct TypeList<T1, Ts...> {
  /// The first type on the list.
  using head = T1;

  /// A sublist with the tail. ie everything but the head.
  ///
  /// This type is used to do recursion. TypeList<>/EmptyTypeList indicates the
  /// end of the list.
  using tail = TypeList<Ts...>;
};

/// The empty type list.
using EmptyTypeList = TypeList<>;

/// Helper meta-function to determine if some type \c T is present or
///   a parent type in the list.
template <typename AnyTypeList, typename T> struct TypeListContainsSuperOf {
  static const bool value =
      std::is_base_of<typename AnyTypeList::head, T>::value ||
      TypeListContainsSuperOf<typename AnyTypeList::tail, T>::value;
};
template <typename T> struct TypeListContainsSuperOf<EmptyTypeList, T> {
  static const bool value = false;
};

/// Variadic function object.
///
/// Most of the functions below that use VariadicFunction could be implemented
/// using plain C++11 variadic functions, but the function object allows us to
/// capture it on the dynamic matcher registry.
template <typename ResultT, typename ArgT,
          ResultT (*Func)(ArrayRef<const ArgT *>)>
struct VariadicFunction {
  ResultT operator()() const { return Func(std::nullopt); }

  template <typename... ArgsT>
  ResultT operator()(const ArgT &Arg1, const ArgsT &... Args) const {
    return Execute(Arg1, static_cast<const ArgT &>(Args)...);
  }

  // We also allow calls with an already created array, in case the caller
  // already had it.
  ResultT operator()(ArrayRef<ArgT> Args) const {
    return Func(llvm::to_vector<8>(llvm::make_pointer_range(Args)));
  }

private:
  // Trampoline function to allow for implicit conversions to take place
  // before we make the array.
  template <typename... ArgsT> ResultT Execute(const ArgsT &... Args) const {
    const ArgT *const ArgsArray[] = {&Args...};
    return Func(ArrayRef<const ArgT *>(ArgsArray, sizeof...(ArgsT)));
  }
};

/// Unifies obtaining the underlying type of a regular node through
/// `getType` and a TypedefNameDecl node through `getUnderlyingType`.
inline QualType getUnderlyingType(const Expr &Node) { return Node.getType(); }

inline QualType getUnderlyingType(const ValueDecl &Node) {
  return Node.getType();
}
inline QualType getUnderlyingType(const TypedefNameDecl &Node) {
  return Node.getUnderlyingType();
}
inline QualType getUnderlyingType(const FriendDecl &Node) {
  if (const TypeSourceInfo *TSI = Node.getFriendType())
    return TSI->getType();
  return QualType();
}
inline QualType getUnderlyingType(const CXXBaseSpecifier &Node) {
  return Node.getType();
}

/// Unifies obtaining a `TypeSourceInfo` from different node types.
template <typename T,
          std::enable_if_t<TypeListContainsSuperOf<
              TypeList<CXXBaseSpecifier, CXXCtorInitializer,
                       CXXTemporaryObjectExpr, CXXUnresolvedConstructExpr,
                       CompoundLiteralExpr, DeclaratorDecl, ObjCPropertyDecl,
                       TemplateArgumentLoc, TypedefNameDecl>,
              T>::value> * = nullptr>
inline TypeSourceInfo *GetTypeSourceInfo(const T &Node) {
  return Node.getTypeSourceInfo();
}
template <typename T,
          std::enable_if_t<TypeListContainsSuperOf<
              TypeList<CXXFunctionalCastExpr, ExplicitCastExpr>, T>::value> * =
              nullptr>
inline TypeSourceInfo *GetTypeSourceInfo(const T &Node) {
  return Node.getTypeInfoAsWritten();
}
inline TypeSourceInfo *GetTypeSourceInfo(const BlockDecl &Node) {
  return Node.getSignatureAsWritten();
}
inline TypeSourceInfo *GetTypeSourceInfo(const CXXNewExpr &Node) {
  return Node.getAllocatedTypeSourceInfo();
}

/// Unifies obtaining the FunctionProtoType pointer from both
/// FunctionProtoType and FunctionDecl nodes..
inline const FunctionProtoType *
getFunctionProtoType(const FunctionProtoType &Node) {
  return &Node;
}

inline const FunctionProtoType *getFunctionProtoType(const FunctionDecl &Node) {
  return Node.getType()->getAs<FunctionProtoType>();
}

/// Unifies obtaining the access specifier from Decl and CXXBaseSpecifier nodes.
inline clang::AccessSpecifier getAccessSpecifier(const Decl &Node) {
  return Node.getAccess();
}

inline clang::AccessSpecifier getAccessSpecifier(const CXXBaseSpecifier &Node) {
  return Node.getAccessSpecifier();
}

/// Internal version of BoundNodes. Holds all the bound nodes.
class BoundNodesMap {
public:
  /// Adds \c Node to the map with key \c ID.
  ///
  /// The node's base type should be in NodeBaseType or it will be unaccessible.
  void addNode(StringRef ID, const DynTypedNode &DynNode) {
    NodeMap[std::string(ID)] = DynNode;
  }

  /// Returns the AST node bound to \c ID.
  ///
  /// Returns NULL if there was no node bound to \c ID or if there is a node but
  /// it cannot be converted to the specified type.
  template <typename T>
  const T *getNodeAs(StringRef ID) const {
    IDToNodeMap::const_iterator It = NodeMap.find(ID);
    if (It == NodeMap.end()) {
      return nullptr;
    }
    return It->second.get<T>();
  }

  DynTypedNode getNode(StringRef ID) const {
    IDToNodeMap::const_iterator It = NodeMap.find(ID);
    if (It == NodeMap.end()) {
      return DynTypedNode();
    }
    return It->second;
  }

  /// Imposes an order on BoundNodesMaps.
  bool operator<(const BoundNodesMap &Other) const {
    return NodeMap < Other.NodeMap;
  }

  /// A map from IDs to the bound nodes.
  ///
  /// Note that we're using std::map here, as for memoization:
  /// - we need a comparison operator
  /// - we need an assignment operator
  using IDToNodeMap = std::map<std::string, DynTypedNode, std::less<>>;

  const IDToNodeMap &getMap() const {
    return NodeMap;
  }

  /// Returns \c true if this \c BoundNodesMap can be compared, i.e. all
  /// stored nodes have memoization data.
  bool isComparable() const {
    for (const auto &IDAndNode : NodeMap) {
      if (!IDAndNode.second.getMemoizationData())
        return false;
    }
    return true;
  }

private:
  IDToNodeMap NodeMap;
};

/// Creates BoundNodesTree objects.
///
/// The tree builder is used during the matching process to insert the bound
/// nodes from the Id matcher.
class BoundNodesTreeBuilder {
public:
  /// A visitor interface to visit all BoundNodes results for a
  /// BoundNodesTree.
  class Visitor {
  public:
    virtual ~Visitor() = default;

    /// Called multiple times during a single call to VisitMatches(...).
    ///
    /// 'BoundNodesView' contains the bound nodes for a single match.
    virtual void visitMatch(const BoundNodes& BoundNodesView) = 0;
  };

  /// Add a binding from an id to a node.
  void setBinding(StringRef Id, const DynTypedNode &DynNode) {
    if (Bindings.empty())
      Bindings.emplace_back();
    for (BoundNodesMap &Binding : Bindings)
      Binding.addNode(Id, DynNode);
  }

  /// Adds a branch in the tree.
  void addMatch(const BoundNodesTreeBuilder &Bindings);

  /// Visits all matches that this BoundNodesTree represents.
  ///
  /// The ownership of 'ResultVisitor' remains at the caller.
  void visitMatches(Visitor* ResultVisitor);

  template <typename ExcludePredicate>
  bool removeBindings(const ExcludePredicate &Predicate) {
    llvm::erase_if(Bindings, Predicate);
    return !Bindings.empty();
  }

  /// Imposes an order on BoundNodesTreeBuilders.
  bool operator<(const BoundNodesTreeBuilder &Other) const {
    return Bindings < Other.Bindings;
  }

  /// Returns \c true if this \c BoundNodesTreeBuilder can be compared,
  /// i.e. all stored node maps have memoization data.
  bool isComparable() const {
    for (const BoundNodesMap &NodesMap : Bindings) {
      if (!NodesMap.isComparable())
        return false;
    }
    return true;
  }

private:
  SmallVector<BoundNodesMap, 1> Bindings;
};

class ASTMatchFinder;

/// Generic interface for all matchers.
///
/// Used by the implementation of Matcher<T> and DynTypedMatcher.
/// In general, implement MatcherInterface<T> or SingleNodeMatcherInterface<T>
/// instead.
class DynMatcherInterface
    : public llvm::ThreadSafeRefCountedBase<DynMatcherInterface> {
public:
  virtual ~DynMatcherInterface() = default;

  /// Returns true if \p DynNode can be matched.
  ///
  /// May bind \p DynNode to an ID via \p Builder, or recurse into
  /// the AST via \p Finder.
  virtual bool dynMatches(const DynTypedNode &DynNode, ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const = 0;

  virtual std::optional<clang::TraversalKind> TraversalKind() const {
    return std::nullopt;
  }
};

/// Generic interface for matchers on an AST node of type T.
///
/// Implement this if your matcher may need to inspect the children or
/// descendants of the node or bind matched nodes to names. If you are
/// writing a simple matcher that only inspects properties of the
/// current node and doesn't care about its children or descendants,
/// implement SingleNodeMatcherInterface instead.
template <typename T>
class MatcherInterface : public DynMatcherInterface {
public:
  /// Returns true if 'Node' can be matched.
  ///
  /// May bind 'Node' to an ID via 'Builder', or recurse into
  /// the AST via 'Finder'.
  virtual bool matches(const T &Node,
                       ASTMatchFinder *Finder,
                       BoundNodesTreeBuilder *Builder) const = 0;

  bool dynMatches(const DynTypedNode &DynNode, ASTMatchFinder *Finder,
                  BoundNodesTreeBuilder *Builder) const override {
    return matches(DynNode.getUnchecked<T>(), Finder, Builder);
  }
};

/// Interface for matchers that only evaluate properties on a single
/// node.
template <typename T>
class SingleNodeMatcherInterface : public MatcherInterface<T> {
public:
  /// Returns true if the matcher matches the provided node.
  ///
  /// A subclass must implement this instead of Matches().
  virtual bool matchesNode(const T &Node) const = 0;

private:
  /// Implements MatcherInterface::Matches.
  bool matches(const T &Node,
               ASTMatchFinder * /* Finder */,
               BoundNodesTreeBuilder * /*  Builder */) const override {
    return matchesNode(Node);
  }
};

template <typename> class Matcher;

/// Matcher that works on a \c DynTypedNode.
///
/// It is constructed from a \c Matcher<T> object and redirects most calls to
/// underlying matcher.
/// It checks whether the \c DynTypedNode is convertible into the type of the
/// underlying matcher and then do the actual match on the actual node, or
/// return false if it is not convertible.
class DynTypedMatcher {
public:
  /// Takes ownership of the provided implementation pointer.
  template <typename T>
  DynTypedMatcher(MatcherInterface<T> *Implementation)
      : SupportedKind(ASTNodeKind::getFromNodeKind<T>()),
        RestrictKind(SupportedKind), Implementation(Implementation) {}

  /// Construct from a variadic function.
  enum VariadicOperator {
    /// Matches nodes for which all provided matchers match.
    VO_AllOf,

    /// Matches nodes for which at least one of the provided matchers
    /// matches.
    VO_AnyOf,

    /// Matches nodes for which at least one of the provided matchers
    /// matches, but doesn't stop at the first match.
    VO_EachOf,

    /// Matches any node but executes all inner matchers to find result
    /// bindings.
    VO_Optionally,

    /// Matches nodes that do not match the provided matcher.
    ///
    /// Uses the variadic matcher interface, but fails if
    /// InnerMatchers.size() != 1.
    VO_UnaryNot
  };

  static DynTypedMatcher
  constructVariadic(VariadicOperator Op, ASTNodeKind SupportedKind,
                    std::vector<DynTypedMatcher> InnerMatchers);

  static DynTypedMatcher
  constructRestrictedWrapper(const DynTypedMatcher &InnerMatcher,
                             ASTNodeKind RestrictKind);

  /// Get a "true" matcher for \p NodeKind.
  ///
  /// It only checks that the node is of the right kind.
  static DynTypedMatcher trueMatcher(ASTNodeKind NodeKind);

  void setAllowBind(bool AB) { AllowBind = AB; }

  /// Check whether this matcher could ever match a node of kind \p Kind.
  /// \return \c false if this matcher will never match such a node. Otherwise,
  /// return \c true.
  bool canMatchNodesOfKind(ASTNodeKind Kind) const;

  /// Return a matcher that points to the same implementation, but
  ///   restricts the node types for \p Kind.
  DynTypedMatcher dynCastTo(const ASTNodeKind Kind) const;

  /// Return a matcher that points to the same implementation, but sets the
  ///   traversal kind.
  ///
  /// If the traversal kind is already set, then \c TK overrides it.
  DynTypedMatcher withTraversalKind(TraversalKind TK);

  /// Returns true if the matcher matches the given \c DynNode.
  bool matches(const DynTypedNode &DynNode, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const;

  /// Same as matches(), but skips the kind check.
  ///
  /// It is faster, but the caller must ensure the node is valid for the
  /// kind of this matcher.
  bool matchesNoKindCheck(const DynTypedNode &DynNode, ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const;

  /// Bind the specified \p ID to the matcher.
  /// \return A new matcher with the \p ID bound to it if this matcher supports
  ///   binding. Otherwise, returns an empty \c std::optional<>.
  std::optional<DynTypedMatcher> tryBind(StringRef ID) const;

  /// Returns a unique \p ID for the matcher.
  ///
  /// Casting a Matcher<T> to Matcher<U> creates a matcher that has the
  /// same \c Implementation pointer, but different \c RestrictKind. We need to
  /// include both in the ID to make it unique.
  ///
  /// \c MatcherIDType supports operator< and provides strict weak ordering.
  using MatcherIDType = std::pair<ASTNodeKind, uint64_t>;
  MatcherIDType getID() const {
    /// FIXME: Document the requirements this imposes on matcher
    /// implementations (no new() implementation_ during a Matches()).
    return std::make_pair(RestrictKind,
                          reinterpret_cast<uint64_t>(Implementation.get()));
  }

  /// Returns the type this matcher works on.
  ///
  /// \c matches() will always return false unless the node passed is of this
  /// or a derived type.
  ASTNodeKind getSupportedKind() const { return SupportedKind; }

  /// Returns \c true if the passed \c DynTypedMatcher can be converted
  ///   to a \c Matcher<T>.
  ///
  /// This method verifies that the underlying matcher in \c Other can process
  /// nodes of types T.
  template <typename T> bool canConvertTo() const {
    return canConvertTo(ASTNodeKind::getFromNodeKind<T>());
  }
  bool canConvertTo(ASTNodeKind To) const;

  /// Construct a \c Matcher<T> interface around the dynamic matcher.
  ///
  /// This method asserts that \c canConvertTo() is \c true. Callers
  /// should call \c canConvertTo() first to make sure that \c this is
  /// compatible with T.
  template <typename T> Matcher<T> convertTo() const {
    assert(canConvertTo<T>());
    return unconditionalConvertTo<T>();
  }

  /// Same as \c convertTo(), but does not check that the underlying
  ///   matcher can handle a value of T.
  ///
  /// If it is not compatible, then this matcher will never match anything.
  template <typename T> Matcher<T> unconditionalConvertTo() const;

  /// Returns the \c TraversalKind respected by calls to `match()`, if any.
  ///
  /// Most matchers will not have a traversal kind set, instead relying on the
  /// surrounding context. For those, \c std::nullopt is returned.
  std::optional<clang::TraversalKind> getTraversalKind() const {
    return Implementation->TraversalKind();
  }

private:
  DynTypedMatcher(ASTNodeKind SupportedKind, ASTNodeKind RestrictKind,
                  IntrusiveRefCntPtr<DynMatcherInterface> Implementation)
      : SupportedKind(SupportedKind), RestrictKind(RestrictKind),
        Implementation(std::move(Implementation)) {}

  bool AllowBind = false;
  ASTNodeKind SupportedKind;

  /// A potentially stricter node kind.
  ///
  /// It allows to perform implicit and dynamic cast of matchers without
  /// needing to change \c Implementation.
  ASTNodeKind RestrictKind;
  IntrusiveRefCntPtr<DynMatcherInterface> Implementation;
};

/// Wrapper of a MatcherInterface<T> *that allows copying.
///
/// A Matcher<Base> can be used anywhere a Matcher<Derived> is
/// required. This establishes an is-a relationship which is reverse
/// to the AST hierarchy. In other words, Matcher<T> is contravariant
/// with respect to T. The relationship is built via a type conversion
/// operator rather than a type hierarchy to be able to templatize the
/// type hierarchy instead of spelling it out.
template <typename T>
class Matcher {
public:
  /// Takes ownership of the provided implementation pointer.
  explicit Matcher(MatcherInterface<T> *Implementation)
      : Implementation(Implementation) {}

  /// Implicitly converts \c Other to a Matcher<T>.
  ///
  /// Requires \c T to be derived from \c From.
  template <typename From>
  Matcher(const Matcher<From> &Other,
          std::enable_if_t<std::is_base_of<From, T>::value &&
                           !std::is_same<From, T>::value> * = nullptr)
      : Implementation(restrictMatcher(Other.Implementation)) {
    assert(Implementation.getSupportedKind().isSame(
        ASTNodeKind::getFromNodeKind<T>()));
  }

  /// Implicitly converts \c Matcher<Type> to \c Matcher<QualType>.
  ///
  /// The resulting matcher is not strict, i.e. ignores qualifiers.
  template <typename TypeT>
  Matcher(const Matcher<TypeT> &Other,
          std::enable_if_t<std::is_same<T, QualType>::value &&
                           std::is_same<TypeT, Type>::value> * = nullptr)
      : Implementation(new TypeToQualType<TypeT>(Other)) {}

  /// Convert \c this into a \c Matcher<T> by applying dyn_cast<> to the
  /// argument.
  /// \c To must be a base class of \c T.
  template <typename To> Matcher<To> dynCastTo() const & {
    static_assert(std::is_base_of<To, T>::value, "Invalid dynCast call.");
    return Matcher<To>(Implementation);
  }

  template <typename To> Matcher<To> dynCastTo() && {
    static_assert(std::is_base_of<To, T>::value, "Invalid dynCast call.");
    return Matcher<To>(std::move(Implementation));
  }

  /// Forwards the call to the underlying MatcherInterface<T> pointer.
  bool matches(const T &Node,
               ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const {
    return Implementation.matches(DynTypedNode::create(Node), Finder, Builder);
  }

  /// Returns an ID that uniquely identifies the matcher.
  DynTypedMatcher::MatcherIDType getID() const {
    return Implementation.getID();
  }

  /// Extract the dynamic matcher.
  ///
  /// The returned matcher keeps the same restrictions as \c this and remembers
  /// that it is meant to support nodes of type \c T.
  operator DynTypedMatcher() const & { return Implementation; }

  operator DynTypedMatcher() && { return std::move(Implementation); }

  /// Allows the conversion of a \c Matcher<Type> to a \c
  /// Matcher<QualType>.
  ///
  /// Depending on the constructor argument, the matcher is either strict, i.e.
  /// does only matches in the absence of qualifiers, or not, i.e. simply
  /// ignores any qualifiers.
  template <typename TypeT>
  class TypeToQualType : public MatcherInterface<QualType> {
    const DynTypedMatcher InnerMatcher;

  public:
    TypeToQualType(const Matcher<TypeT> &InnerMatcher)
        : InnerMatcher(InnerMatcher) {}

    bool matches(const QualType &Node, ASTMatchFinder *Finder,
                 BoundNodesTreeBuilder *Builder) const override {
      if (Node.isNull())
        return false;
      return this->InnerMatcher.matches(DynTypedNode::create(*Node), Finder,
                                        Builder);
    }

    std::optional<clang::TraversalKind> TraversalKind() const override {
      return this->InnerMatcher.getTraversalKind();
    }
  };

private:
  // For Matcher<T> <=> Matcher<U> conversions.
  template <typename U> friend class Matcher;

  // For DynTypedMatcher::unconditionalConvertTo<T>.
  friend class DynTypedMatcher;

  static DynTypedMatcher restrictMatcher(const DynTypedMatcher &Other) {
    return Other.dynCastTo(ASTNodeKind::getFromNodeKind<T>());
  }

  explicit Matcher(const DynTypedMatcher &Implementation)
      : Implementation(restrictMatcher(Implementation)) {
    assert(this->Implementation.getSupportedKind().isSame(
        ASTNodeKind::getFromNodeKind<T>()));
  }

  DynTypedMatcher Implementation;
};  // class Matcher

/// A convenient helper for creating a Matcher<T> without specifying
/// the template type argument.
template <typename T>
inline Matcher<T> makeMatcher(MatcherInterface<T> *Implementation) {
  return Matcher<T>(Implementation);
}

/// Interface that allows matchers to traverse the AST.
/// FIXME: Find a better name.
///
/// This provides three entry methods for each base node type in the AST:
/// - \c matchesChildOf:
///   Matches a matcher on every child node of the given node. Returns true
///   if at least one child node could be matched.
/// - \c matchesDescendantOf:
///   Matches a matcher on all descendant nodes of the given node. Returns true
///   if at least one descendant matched.
/// - \c matchesAncestorOf:
///   Matches a matcher on all ancestors of the given node. Returns true if
///   at least one ancestor matched.
///
/// FIXME: Currently we only allow Stmt and Decl nodes to start a traversal.
/// In the future, we want to implement this for all nodes for which it makes
/// sense. In the case of matchesAncestorOf, we'll want to implement it for
/// all nodes, as all nodes have ancestors.
class ASTMatchFinder {
public:
  /// Defines how bindings are processed on recursive matches.
  enum BindKind {
    /// Stop at the first match and only bind the first match.
    BK_First,

    /// Create results for all combinations of bindings that match.
    BK_All
  };

  /// Defines which ancestors are considered for a match.
  enum AncestorMatchMode {
    /// All ancestors.
    AMM_All,

    /// Direct parent only.
    AMM_ParentOnly
  };

  virtual ~ASTMatchFinder() = default;

  /// Returns true if the given C++ class is directly or indirectly derived
  /// from a base type matching \c base.
  ///
  /// A class is not considered to be derived from itself.
  virtual bool classIsDerivedFrom(const CXXRecordDecl *Declaration,
                                  const Matcher<NamedDecl> &Base,
                                  BoundNodesTreeBuilder *Builder,
                                  bool Directly) = 0;

  /// Returns true if the given Objective-C class is directly or indirectly
  /// derived from a base class matching \c base.
  ///
  /// A class is not considered to be derived from itself.
  virtual bool objcClassIsDerivedFrom(const ObjCInterfaceDecl *Declaration,
                                      const Matcher<NamedDecl> &Base,
                                      BoundNodesTreeBuilder *Builder,
                                      bool Directly) = 0;

  template <typename T>
  bool matchesChildOf(const T &Node, const DynTypedMatcher &Matcher,
                      BoundNodesTreeBuilder *Builder, BindKind Bind) {
    static_assert(std::is_base_of<Decl, T>::value ||
                      std::is_base_of<Stmt, T>::value ||
                      std::is_base_of<NestedNameSpecifier, T>::value ||
                      std::is_base_of<NestedNameSpecifierLoc, T>::value ||
                      std::is_base_of<TypeLoc, T>::value ||
                      std::is_base_of<QualType, T>::value ||
                      std::is_base_of<Attr, T>::value,
                  "unsupported type for recursive matching");
    return matchesChildOf(DynTypedNode::create(Node), getASTContext(), Matcher,
                          Builder, Bind);
  }

  template <typename T>
  bool matchesDescendantOf(const T &Node, const DynTypedMatcher &Matcher,
                           BoundNodesTreeBuilder *Builder, BindKind Bind) {
    static_assert(std::is_base_of<Decl, T>::value ||
                      std::is_base_of<Stmt, T>::value ||
                      std::is_base_of<NestedNameSpecifier, T>::value ||
                      std::is_base_of<NestedNameSpecifierLoc, T>::value ||
                      std::is_base_of<TypeLoc, T>::value ||
                      std::is_base_of<QualType, T>::value ||
                      std::is_base_of<Attr, T>::value,
                  "unsupported type for recursive matching");
    return matchesDescendantOf(DynTypedNode::create(Node), getASTContext(),
                               Matcher, Builder, Bind);
  }

  // FIXME: Implement support for BindKind.
  template <typename T>
  bool matchesAncestorOf(const T &Node, const DynTypedMatcher &Matcher,
                         BoundNodesTreeBuilder *Builder,
                         AncestorMatchMode MatchMode) {
    static_assert(std::is_base_of<Decl, T>::value ||
                      std::is_base_of<NestedNameSpecifierLoc, T>::value ||
                      std::is_base_of<Stmt, T>::value ||
                      std::is_base_of<TypeLoc, T>::value ||
                      std::is_base_of<Attr, T>::value,
                  "type not allowed for recursive matching");
    return matchesAncestorOf(DynTypedNode::create(Node), getASTContext(),
                             Matcher, Builder, MatchMode);
  }

  virtual ASTContext &getASTContext() const = 0;

  virtual bool IsMatchingInASTNodeNotSpelledInSource() const = 0;

  virtual bool IsMatchingInASTNodeNotAsIs() const = 0;

  bool isTraversalIgnoringImplicitNodes() const;

protected:
  virtual bool matchesChildOf(const DynTypedNode &Node, ASTContext &Ctx,
                              const DynTypedMatcher &Matcher,
                              BoundNodesTreeBuilder *Builder,
                              BindKind Bind) = 0;

  virtual bool matchesDescendantOf(const DynTypedNode &Node, ASTContext &Ctx,
                                   const DynTypedMatcher &Matcher,
                                   BoundNodesTreeBuilder *Builder,
                                   BindKind Bind) = 0;

  virtual bool matchesAncestorOf(const DynTypedNode &Node, ASTContext &Ctx,
                                 const DynTypedMatcher &Matcher,
                                 BoundNodesTreeBuilder *Builder,
                                 AncestorMatchMode MatchMode) = 0;
private:
  friend struct ASTChildrenNotSpelledInSourceScope;
  virtual bool isMatchingChildrenNotSpelledInSource() const = 0;
  virtual void setMatchingChildrenNotSpelledInSource(bool Set) = 0;
};

struct ASTChildrenNotSpelledInSourceScope {
  ASTChildrenNotSpelledInSourceScope(ASTMatchFinder *V, bool B)
      : MV(V), MB(V->isMatchingChildrenNotSpelledInSource()) {
    V->setMatchingChildrenNotSpelledInSource(B);
  }
  ~ASTChildrenNotSpelledInSourceScope() {
    MV->setMatchingChildrenNotSpelledInSource(MB);
  }

private:
  ASTMatchFinder *MV;
  bool MB;
};

/// Specialization of the conversion functions for QualType.
///
/// This specialization provides the Matcher<Type>->Matcher<QualType>
/// conversion that the static API does.
template <>
inline Matcher<QualType> DynTypedMatcher::convertTo<QualType>() const {
  assert(canConvertTo<QualType>());
  const ASTNodeKind SourceKind = getSupportedKind();
  if (SourceKind.isSame(ASTNodeKind::getFromNodeKind<Type>())) {
    // We support implicit conversion from Matcher<Type> to Matcher<QualType>
    return unconditionalConvertTo<Type>();
  }
  return unconditionalConvertTo<QualType>();
}

/// Finds the first node in a range that matches the given matcher.
template <typename MatcherT, typename IteratorT>
IteratorT matchesFirstInRange(const MatcherT &Matcher, IteratorT Start,
                              IteratorT End, ASTMatchFinder *Finder,
                              BoundNodesTreeBuilder *Builder) {
  for (IteratorT I = Start; I != End; ++I) {
    BoundNodesTreeBuilder Result(*Builder);
    if (Matcher.matches(*I, Finder, &Result)) {
      *Builder = std::move(Result);
      return I;
    }
  }
  return End;
}

/// Finds the first node in a pointer range that matches the given
/// matcher.
template <typename MatcherT, typename IteratorT>
IteratorT matchesFirstInPointerRange(const MatcherT &Matcher, IteratorT Start,
                                     IteratorT End, ASTMatchFinder *Finder,
                                     BoundNodesTreeBuilder *Builder) {
  for (IteratorT I = Start; I != End; ++I) {
    BoundNodesTreeBuilder Result(*Builder);
    if (Matcher.matches(**I, Finder, &Result)) {
      *Builder = std::move(Result);
      return I;
    }
  }
  return End;
}

template <typename T, std::enable_if_t<!std::is_base_of<FunctionDecl, T>::value>
                          * = nullptr>
inline bool isDefaultedHelper(const T *) {
  return false;
}
inline bool isDefaultedHelper(const FunctionDecl *FD) {
  return FD->isDefaulted();
}

// Metafunction to determine if type T has a member called getDecl.
template <typename Ty>
class has_getDecl {
  using yes = char[1];
  using no = char[2];

  template <typename Inner>
  static yes& test(Inner *I, decltype(I->getDecl()) * = nullptr);

  template <typename>
  static no& test(...);

public:
  static const bool value = sizeof(test<Ty>(nullptr)) == sizeof(yes);
};

/// Matches overloaded operators with a specific name.
///
/// The type argument ArgT is not used by this matcher but is used by
/// PolymorphicMatcher and should be StringRef.
template <typename T, typename ArgT>
class HasOverloadedOperatorNameMatcher : public SingleNodeMatcherInterface<T> {
  static_assert(std::is_same<T, CXXOperatorCallExpr>::value ||
                std::is_base_of<FunctionDecl, T>::value,
                "unsupported class for matcher");
  static_assert(std::is_same<ArgT, std::vector<std::string>>::value,
                "argument type must be std::vector<std::string>");

public:
  explicit HasOverloadedOperatorNameMatcher(std::vector<std::string> Names)
      : SingleNodeMatcherInterface<T>(), Names(std::move(Names)) {}

  bool matchesNode(const T &Node) const override {
    return matchesSpecialized(Node);
  }

private:

  /// CXXOperatorCallExpr exist only for calls to overloaded operators
  /// so this function returns true if the call is to an operator of the given
  /// name.
  bool matchesSpecialized(const CXXOperatorCallExpr &Node) const {
    return llvm::is_contained(Names, getOperatorSpelling(Node.getOperator()));
  }

  /// Returns true only if CXXMethodDecl represents an overloaded
  /// operator and has the given operator name.
  bool matchesSpecialized(const FunctionDecl &Node) const {
    return Node.isOverloadedOperator() &&
           llvm::is_contained(
               Names, getOperatorSpelling(Node.getOverloadedOperator()));
  }

  std::vector<std::string> Names;
};

/// Matches named declarations with a specific name.
///
/// See \c hasName() and \c hasAnyName() in ASTMatchers.h for details.
class HasNameMatcher : public SingleNodeMatcherInterface<NamedDecl> {
 public:
  explicit HasNameMatcher(std::vector<std::string> Names);

  bool matchesNode(const NamedDecl &Node) const override;

private:
  /// Unqualified match routine.
  ///
  /// It is much faster than the full match, but it only works for unqualified
  /// matches.
  bool matchesNodeUnqualified(const NamedDecl &Node) const;

  /// Full match routine
  ///
  /// Fast implementation for the simple case of a named declaration at
  /// namespace or RecordDecl scope.
  /// It is slower than matchesNodeUnqualified, but faster than
  /// matchesNodeFullSlow.
  bool matchesNodeFullFast(const NamedDecl &Node) const;

  /// Full match routine
  ///
  /// It generates the fully qualified name of the declaration (which is
  /// expensive) before trying to match.
  /// It is slower but simple and works on all cases.
  bool matchesNodeFullSlow(const NamedDecl &Node) const;

  bool UseUnqualifiedMatch;
  std::vector<std::string> Names;
};

/// Trampoline function to use VariadicFunction<> to construct a
///        HasNameMatcher.
Matcher<NamedDecl> hasAnyNameFunc(ArrayRef<const StringRef *> NameRefs);

/// Trampoline function to use VariadicFunction<> to construct a
///        hasAnySelector matcher.
Matcher<ObjCMessageExpr> hasAnySelectorFunc(
    ArrayRef<const StringRef *> NameRefs);

/// Matches declarations for QualType and CallExpr.
///
/// Type argument DeclMatcherT is required by PolymorphicMatcher but
/// not actually used.
template <typename T, typename DeclMatcherT>
class HasDeclarationMatcher : public MatcherInterface<T> {
  static_assert(std::is_same<DeclMatcherT, Matcher<Decl>>::value,
                "instantiated with wrong types");

  DynTypedMatcher InnerMatcher;

public:
  explicit HasDeclarationMatcher(const Matcher<Decl> &InnerMatcher)
      : InnerMatcher(InnerMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return matchesSpecialized(Node, Finder, Builder);
  }

private:
  /// Forwards to matching on the underlying type of the QualType.
  bool matchesSpecialized(const QualType &Node, ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    if (Node.isNull())
      return false;

    return matchesSpecialized(*Node, Finder, Builder);
  }

  /// Finds the best declaration for a type and returns whether the inner
  /// matcher matches on it.
  bool matchesSpecialized(const Type &Node, ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    // DeducedType does not have declarations of its own, so
    // match the deduced type instead.
    if (const auto *S = dyn_cast<DeducedType>(&Node)) {
      QualType DT = S->getDeducedType();
      return !DT.isNull() ? matchesSpecialized(*DT, Finder, Builder) : false;
    }

    // First, for any types that have a declaration, extract the declaration and
    // match on it.
    if (const auto *S = dyn_cast<TagType>(&Node)) {
      return matchesDecl(S->getDecl(), Finder, Builder);
    }
    if (const auto *S = dyn_cast<InjectedClassNameType>(&Node)) {
      return matchesDecl(S->getDecl(), Finder, Builder);
    }
    if (const auto *S = dyn_cast<TemplateTypeParmType>(&Node)) {
      return matchesDecl(S->getDecl(), Finder, Builder);
    }
    if (const auto *S = dyn_cast<TypedefType>(&Node)) {
      return matchesDecl(S->getDecl(), Finder, Builder);
    }
    if (const auto *S = dyn_cast<UnresolvedUsingType>(&Node)) {
      return matchesDecl(S->getDecl(), Finder, Builder);
    }
    if (const auto *S = dyn_cast<ObjCObjectType>(&Node)) {
      return matchesDecl(S->getInterface(), Finder, Builder);
    }

    // A SubstTemplateTypeParmType exists solely to mark a type substitution
    // on the instantiated template. As users usually want to match the
    // template parameter on the uninitialized template, we can always desugar
    // one level without loss of expressivness.
    // For example, given:
    //   template<typename T> struct X { T t; } class A {}; X<A> a;
    // The following matcher will match, which otherwise would not:
    //   fieldDecl(hasType(pointerType())).
    if (const auto *S = dyn_cast<SubstTemplateTypeParmType>(&Node)) {
      return matchesSpecialized(S->getReplacementType(), Finder, Builder);
    }

    // For template specialization types, we want to match the template
    // declaration, as long as the type is still dependent, and otherwise the
    // declaration of the instantiated tag type.
    if (const auto *S = dyn_cast<TemplateSpecializationType>(&Node)) {
      if (!S->isTypeAlias() && S->isSugared()) {
        // If the template is non-dependent, we want to match the instantiated
        // tag type.
        // For example, given:
        //   template<typename T> struct X {}; X<int> a;
        // The following matcher will match, which otherwise would not:
        //   templateSpecializationType(hasDeclaration(cxxRecordDecl())).
        return matchesSpecialized(*S->desugar(), Finder, Builder);
      }
      // If the template is dependent or an alias, match the template
      // declaration.
      return matchesDecl(S->getTemplateName().getAsTemplateDecl(), Finder,
                         Builder);
    }

    // FIXME: We desugar elaborated types. This makes the assumption that users
    // do never want to match on whether a type is elaborated - there are
    // arguments for both sides; for now, continue desugaring.
    if (const auto *S = dyn_cast<ElaboratedType>(&Node)) {
      return matchesSpecialized(S->desugar(), Finder, Builder);
    }
    // Similarly types found via using declarations.
    // These are *usually* meaningless sugar, and this matches the historical
    // behavior prior to the introduction of UsingType.
    if (const auto *S = dyn_cast<UsingType>(&Node)) {
      return matchesSpecialized(S->desugar(), Finder, Builder);
    }
    return false;
  }

  /// Extracts the Decl the DeclRefExpr references and returns whether
  /// the inner matcher matches on it.
  bool matchesSpecialized(const DeclRefExpr &Node, ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getDecl(), Finder, Builder);
  }

  /// Extracts the Decl of the callee of a CallExpr and returns whether
  /// the inner matcher matches on it.
  bool matchesSpecialized(const CallExpr &Node, ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getCalleeDecl(), Finder, Builder);
  }

  /// Extracts the Decl of the constructor call and returns whether the
  /// inner matcher matches on it.
  bool matchesSpecialized(const CXXConstructExpr &Node,
                          ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getConstructor(), Finder, Builder);
  }

  bool matchesSpecialized(const ObjCIvarRefExpr &Node,
                          ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getDecl(), Finder, Builder);
  }

  /// Extracts the operator new of the new call and returns whether the
  /// inner matcher matches on it.
  bool matchesSpecialized(const CXXNewExpr &Node,
                          ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getOperatorNew(), Finder, Builder);
  }

  /// Extracts the \c ValueDecl a \c MemberExpr refers to and returns
  /// whether the inner matcher matches on it.
  bool matchesSpecialized(const MemberExpr &Node,
                          ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getMemberDecl(), Finder, Builder);
  }

  /// Extracts the \c LabelDecl a \c AddrLabelExpr refers to and returns
  /// whether the inner matcher matches on it.
  bool matchesSpecialized(const AddrLabelExpr &Node,
                          ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getLabel(), Finder, Builder);
  }

  /// Extracts the declaration of a LabelStmt and returns whether the
  /// inner matcher matches on it.
  bool matchesSpecialized(const LabelStmt &Node, ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getDecl(), Finder, Builder);
  }

  /// Returns whether the inner matcher \c Node. Returns false if \c Node
  /// is \c NULL.
  bool matchesDecl(const Decl *Node, ASTMatchFinder *Finder,
                   BoundNodesTreeBuilder *Builder) const {
    return Node != nullptr &&
           !(Finder->isTraversalIgnoringImplicitNodes() &&
             Node->isImplicit()) &&
           this->InnerMatcher.matches(DynTypedNode::create(*Node), Finder,
                                      Builder);
  }
};

/// IsBaseType<T>::value is true if T is a "base" type in the AST
/// node class hierarchies.
template <typename T>
struct IsBaseType {
  static const bool value =
      std::is_same<T, Decl>::value || std::is_same<T, Stmt>::value ||
      std::is_same<T, QualType>::value || std::is_same<T, Type>::value ||
      std::is_same<T, TypeLoc>::value ||
      std::is_same<T, NestedNameSpecifier>::value ||
      std::is_same<T, NestedNameSpecifierLoc>::value ||
      std::is_same<T, CXXCtorInitializer>::value ||
      std::is_same<T, TemplateArgumentLoc>::value ||
      std::is_same<T, Attr>::value;
};
template <typename T>
const bool IsBaseType<T>::value;

/// A "type list" that contains all types.
///
/// Useful for matchers like \c anything and \c unless.
using AllNodeBaseTypes =
    TypeList<Decl, Stmt, NestedNameSpecifier, NestedNameSpecifierLoc, QualType,
             Type, TypeLoc, CXXCtorInitializer, Attr>;

/// Helper meta-function to extract the argument out of a function of
///   type void(Arg).
///
/// See AST_POLYMORPHIC_SUPPORTED_TYPES for details.
template <class T> struct ExtractFunctionArgMeta;
template <class T> struct ExtractFunctionArgMeta<void(T)> {
  using type = T;
};

template <class T, class Tuple, std::size_t... I>
constexpr T *new_from_tuple_impl(Tuple &&t, std::index_sequence<I...>) {
  return new T(std::get<I>(std::forward<Tuple>(t))...);
}

template <class T, class Tuple> constexpr T *new_from_tuple(Tuple &&t) {
  return new_from_tuple_impl<T>(
      std::forward<Tuple>(t),
      std::make_index_sequence<
          std::tuple_size<std::remove_reference_t<Tuple>>::value>{});
}

/// Default type lists for ArgumentAdaptingMatcher matchers.
using AdaptativeDefaultFromTypes = AllNodeBaseTypes;
using AdaptativeDefaultToTypes =
    TypeList<Decl, Stmt, NestedNameSpecifier, NestedNameSpecifierLoc, TypeLoc,
             QualType, Attr>;

/// All types that are supported by HasDeclarationMatcher above.
using HasDeclarationSupportedTypes =
    TypeList<CallExpr, CXXConstructExpr, CXXNewExpr, DeclRefExpr, EnumType,
             ElaboratedType, InjectedClassNameType, LabelStmt, AddrLabelExpr,
             MemberExpr, QualType, RecordType, TagType,
             TemplateSpecializationType, TemplateTypeParmType, TypedefType,
             UnresolvedUsingType, ObjCIvarRefExpr>;

/// A Matcher that allows binding the node it matches to an id.
///
/// BindableMatcher provides a \a bind() method that allows binding the
/// matched node to an id if the match was successful.
template <typename T> class BindableMatcher : public Matcher<T> {
public:
  explicit BindableMatcher(const Matcher<T> &M) : Matcher<T>(M) {}
  explicit BindableMatcher(MatcherInterface<T> *Implementation)
      : Matcher<T>(Implementation) {}

  /// Returns a matcher that will bind the matched node on a match.
  ///
  /// The returned matcher is equivalent to this matcher, but will
  /// bind the matched node on a match.
  Matcher<T> bind(StringRef ID) const {
    return DynTypedMatcher(*this)
        .tryBind(ID)
        ->template unconditionalConvertTo<T>();
  }

  /// Same as Matcher<T>'s conversion operator, but enables binding on
  /// the returned matcher.
  operator DynTypedMatcher() const {
    DynTypedMatcher Result = static_cast<const Matcher<T> &>(*this);
    Result.setAllowBind(true);
    return Result;
  }
};

/// Matches any instance of the given NodeType.
///
/// This is useful when a matcher syntactically requires a child matcher,
/// but the context doesn't care. See for example: anything().
class TrueMatcher {
public:
  using ReturnTypes = AllNodeBaseTypes;

  template <typename T> operator Matcher<T>() const {
    return DynTypedMatcher::trueMatcher(ASTNodeKind::getFromNodeKind<T>())
        .template unconditionalConvertTo<T>();
  }
};

/// Creates a Matcher<T> that matches if all inner matchers match.
template <typename T>
BindableMatcher<T>
makeAllOfComposite(ArrayRef<const Matcher<T> *> InnerMatchers) {
  // For the size() == 0 case, we return a "true" matcher.
  if (InnerMatchers.empty()) {
    return BindableMatcher<T>(TrueMatcher());
  }
  // For the size() == 1 case, we simply return that one matcher.
  // No need to wrap it in a variadic operation.
  if (InnerMatchers.size() == 1) {
    return BindableMatcher<T>(*InnerMatchers[0]);
  }

  using PI = llvm::pointee_iterator<const Matcher<T> *const *>;

  std::vector<DynTypedMatcher> DynMatchers(PI(InnerMatchers.begin()),
                                           PI(InnerMatchers.end()));
  return BindableMatcher<T>(
      DynTypedMatcher::constructVariadic(DynTypedMatcher::VO_AllOf,
                                         ASTNodeKind::getFromNodeKind<T>(),
                                         std::move(DynMatchers))
          .template unconditionalConvertTo<T>());
}

/// Creates a Matcher<T> that matches if
/// T is dyn_cast'able into InnerT and all inner matchers match.
///
/// Returns BindableMatcher, as matchers that use dyn_cast have
/// the same object both to match on and to run submatchers on,
/// so there is no ambiguity with what gets bound.
template <typename T, typename InnerT>
BindableMatcher<T>
makeDynCastAllOfComposite(ArrayRef<const Matcher<InnerT> *> InnerMatchers) {
  return BindableMatcher<T>(
      makeAllOfComposite(InnerMatchers).template dynCastTo<T>());
}

/// A VariadicDynCastAllOfMatcher<SourceT, TargetT> object is a
/// variadic functor that takes a number of Matcher<TargetT> and returns a
/// Matcher<SourceT> that matches TargetT nodes that are matched by all of the
/// given matchers, if SourceT can be dynamically casted into TargetT.
///
/// For example:
///   const VariadicDynCastAllOfMatcher<Decl, CXXRecordDecl> record;
/// Creates a functor record(...) that creates a Matcher<Decl> given
/// a variable number of arguments of type Matcher<CXXRecordDecl>.
/// The returned matcher matches if the given Decl can by dynamically
/// casted to CXXRecordDecl and all given matchers match.
template <typename SourceT, typename TargetT>
class VariadicDynCastAllOfMatcher
    : public VariadicFunction<BindableMatcher<SourceT>, Matcher<TargetT>,
                              makeDynCastAllOfComposite<SourceT, TargetT>> {
public:
  VariadicDynCastAllOfMatcher() {}
};

/// A \c VariadicAllOfMatcher<T> object is a variadic functor that takes
/// a number of \c Matcher<T> and returns a \c Matcher<T> that matches \c T
/// nodes that are matched by all of the given matchers.
///
/// For example:
///   const VariadicAllOfMatcher<NestedNameSpecifier> nestedNameSpecifier;
/// Creates a functor nestedNameSpecifier(...) that creates a
/// \c Matcher<NestedNameSpecifier> given a variable number of arguments of type
/// \c Matcher<NestedNameSpecifier>.
/// The returned matcher matches if all given matchers match.
template <typename T>
class VariadicAllOfMatcher
    : public VariadicFunction<BindableMatcher<T>, Matcher<T>,
                              makeAllOfComposite<T>> {
public:
  VariadicAllOfMatcher() {}
};

/// VariadicOperatorMatcher related types.
/// @{

/// Polymorphic matcher object that uses a \c
/// DynTypedMatcher::VariadicOperator operator.
///
/// Input matchers can have any type (including other polymorphic matcher
/// types), and the actual Matcher<T> is generated on demand with an implicit
/// conversion operator.
template <typename... Ps> class VariadicOperatorMatcher {
public:
  VariadicOperatorMatcher(DynTypedMatcher::VariadicOperator Op, Ps &&... Params)
      : Op(Op), Params(std::forward<Ps>(Params)...) {}

  template <typename T> operator Matcher<T>() const & {
    return DynTypedMatcher::constructVariadic(
               Op, ASTNodeKind::getFromNodeKind<T>(),
               getMatchers<T>(std::index_sequence_for<Ps...>()))
        .template unconditionalConvertTo<T>();
  }

  template <typename T> operator Matcher<T>() && {
    return DynTypedMatcher::constructVariadic(
               Op, ASTNodeKind::getFromNodeKind<T>(),
               getMatchers<T>(std::index_sequence_for<Ps...>()))
        .template unconditionalConvertTo<T>();
  }

private:
  // Helper method to unpack the tuple into a vector.
  template <typename T, std::size_t... Is>
  std::vector<DynTypedMatcher> getMatchers(std::index_sequence<Is...>) const & {
    return {Matcher<T>(std::get<Is>(Params))...};
  }

  template <typename T, std::size_t... Is>
  std::vector<DynTypedMatcher> getMatchers(std::index_sequence<Is...>) && {
    return {Matcher<T>(std::get<Is>(std::move(Params)))...};
  }

  const DynTypedMatcher::VariadicOperator Op;
  std::tuple<Ps...> Params;
};

/// Overloaded function object to generate VariadicOperatorMatcher
///   objects from arbitrary matchers.
template <unsigned MinCount, unsigned MaxCount>
struct VariadicOperatorMatcherFunc {
  DynTypedMatcher::VariadicOperator Op;

  template <typename... Ms>
  VariadicOperatorMatcher<Ms...> operator()(Ms &&... Ps) const {
    static_assert(MinCount <= sizeof...(Ms) && sizeof...(Ms) <= MaxCount,
                  "invalid number of parameters for variadic matcher");
    return VariadicOperatorMatcher<Ms...>(Op, std::forward<Ms>(Ps)...);
  }
};

template <typename T, bool IsBaseOf, typename Head, typename Tail>
struct GetCladeImpl {
  using Type = Head;
};
template <typename T, typename Head, typename Tail>
struct GetCladeImpl<T, false, Head, Tail>
    : GetCladeImpl<T, std::is_base_of<typename Tail::head, T>::value,
                   typename Tail::head, typename Tail::tail> {};

template <typename T, typename... U>
struct GetClade : GetCladeImpl<T, false, T, AllNodeBaseTypes> {};

template <typename CladeType, typename... MatcherTypes>
struct MapAnyOfMatcherImpl {

  template <typename... InnerMatchers>
  BindableMatcher<CladeType>
  operator()(InnerMatchers &&... InnerMatcher) const {
    return VariadicAllOfMatcher<CladeType>()(std::apply(
        internal::VariadicOperatorMatcherFunc<
            0, std::numeric_limits<unsigned>::max()>{
            internal::DynTypedMatcher::VO_AnyOf},
        std::apply(
            [&](auto... Matcher) {
              return std::make_tuple(Matcher(InnerMatcher...)...);
            },
            std::tuple<
                VariadicDynCastAllOfMatcher<CladeType, MatcherTypes>...>())));
  }
};

template <typename... MatcherTypes>
using MapAnyOfMatcher =
    MapAnyOfMatcherImpl<typename GetClade<MatcherTypes...>::Type,
                        MatcherTypes...>;

template <typename... MatcherTypes> struct MapAnyOfHelper {
  using CladeType = typename GetClade<MatcherTypes...>::Type;

  MapAnyOfMatcher<MatcherTypes...> with;

  operator BindableMatcher<CladeType>() const { return with(); }

  Matcher<CladeType> bind(StringRef ID) const { return with().bind(ID); }
};

template <template <typename ToArg, typename FromArg> class ArgumentAdapterT,
          typename T, typename ToTypes>
class ArgumentAdaptingMatcherFuncAdaptor {
public:
  explicit ArgumentAdaptingMatcherFuncAdaptor(const Matcher<T> &InnerMatcher)
      : InnerMatcher(InnerMatcher) {}

  using ReturnTypes = ToTypes;

  template <typename To> operator Matcher<To>() const & {
    return Matcher<To>(new ArgumentAdapterT<To, T>(InnerMatcher));
  }

  template <typename To> operator Matcher<To>() && {
    return Matcher<To>(new ArgumentAdapterT<To, T>(std::move(InnerMatcher)));
  }

private:
  Matcher<T> InnerMatcher;
};

/// Converts a \c Matcher<T> to a matcher of desired type \c To by
/// "adapting" a \c To into a \c T.
///
/// The \c ArgumentAdapterT argument specifies how the adaptation is done.
///
/// For example:
///   \c ArgumentAdaptingMatcher<HasMatcher, T>(InnerMatcher);
/// Given that \c InnerMatcher is of type \c Matcher<T>, this returns a matcher
/// that is convertible into any matcher of type \c To by constructing
/// \c HasMatcher<To, T>(InnerMatcher).
///
/// If a matcher does not need knowledge about the inner type, prefer to use
/// PolymorphicMatcher.
template <template <typename ToArg, typename FromArg> class ArgumentAdapterT,
          typename FromTypes = AdaptativeDefaultFromTypes,
          typename ToTypes = AdaptativeDefaultToTypes>
struct ArgumentAdaptingMatcherFunc {
  template <typename T>
  static ArgumentAdaptingMatcherFuncAdaptor<ArgumentAdapterT, T, ToTypes>
  create(const Matcher<T> &InnerMatcher) {
    return ArgumentAdaptingMatcherFuncAdaptor<ArgumentAdapterT, T, ToTypes>(
        InnerMatcher);
  }

  template <typename T>
  ArgumentAdaptingMatcherFuncAdaptor<ArgumentAdapterT, T, ToTypes>
  operator()(const Matcher<T> &InnerMatcher) const {
    return create(InnerMatcher);
  }

  template <typename... T>
  ArgumentAdaptingMatcherFuncAdaptor<ArgumentAdapterT,
                                     typename GetClade<T...>::Type, ToTypes>
  operator()(const MapAnyOfHelper<T...> &InnerMatcher) const {
    return create(InnerMatcher.with());
  }
};

template <typename T> class TraversalMatcher : public MatcherInterface<T> {
  DynTypedMatcher InnerMatcher;
  clang::TraversalKind Traversal;

public:
  explicit TraversalMatcher(clang::TraversalKind TK,
                            const Matcher<T> &InnerMatcher)
      : InnerMatcher(InnerMatcher), Traversal(TK) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return this->InnerMatcher.matches(DynTypedNode::create(Node), Finder,
                                      Builder);
  }

  std::optional<clang::TraversalKind> TraversalKind() const override {
    if (auto NestedKind = this->InnerMatcher.getTraversalKind())
      return NestedKind;
    return Traversal;
  }
};

template <typename MatcherType> class TraversalWrapper {
public:
  TraversalWrapper(TraversalKind TK, const MatcherType &InnerMatcher)
      : TK(TK), InnerMatcher(InnerMatcher) {}

  template <typename T> operator Matcher<T>() const & {
    return internal::DynTypedMatcher::constructRestrictedWrapper(
               new internal::TraversalMatcher<T>(TK, InnerMatcher),
               ASTNodeKind::getFromNodeKind<T>())
        .template unconditionalConvertTo<T>();
  }

  template <typename T> operator Matcher<T>() && {
    return internal::DynTypedMatcher::constructRestrictedWrapper(
               new internal::TraversalMatcher<T>(TK, std::move(InnerMatcher)),
               ASTNodeKind::getFromNodeKind<T>())
        .template unconditionalConvertTo<T>();
  }

private:
  TraversalKind TK;
  MatcherType InnerMatcher;
};

/// A PolymorphicMatcher<MatcherT, P1, ..., PN> object can be
/// created from N parameters p1, ..., pN (of type P1, ..., PN) and
/// used as a Matcher<T> where a MatcherT<T, P1, ..., PN>(p1, ..., pN)
/// can be constructed.
///
/// For example:
/// - PolymorphicMatcher<IsDefinitionMatcher>()
///   creates an object that can be used as a Matcher<T> for any type T
///   where an IsDefinitionMatcher<T>() can be constructed.
/// - PolymorphicMatcher<ValueEqualsMatcher, int>(42)
///   creates an object that can be used as a Matcher<T> for any type T
///   where a ValueEqualsMatcher<T, int>(42) can be constructed.
template <template <typename T, typename... Params> class MatcherT,
          typename ReturnTypesF, typename... ParamTypes>
class PolymorphicMatcher {
public:
  PolymorphicMatcher(const ParamTypes &... Params) : Params(Params...) {}

  using ReturnTypes = typename ExtractFunctionArgMeta<ReturnTypesF>::type;

  template <typename T> operator Matcher<T>() const & {
    static_assert(TypeListContainsSuperOf<ReturnTypes, T>::value,
                  "right polymorphic conversion");
    return Matcher<T>(new_from_tuple<MatcherT<T, ParamTypes...>>(Params));
  }

  template <typename T> operator Matcher<T>() && {
    static_assert(TypeListContainsSuperOf<ReturnTypes, T>::value,
                  "right polymorphic conversion");
    return Matcher<T>(
        new_from_tuple<MatcherT<T, ParamTypes...>>(std::move(Params)));
  }

private:
  std::tuple<ParamTypes...> Params;
};

/// Matches nodes of type T that have child nodes of type ChildT for
/// which a specified child matcher matches.
///
/// ChildT must be an AST base type.
template <typename T, typename ChildT>
class HasMatcher : public MatcherInterface<T> {
  DynTypedMatcher InnerMatcher;

public:
  explicit HasMatcher(const Matcher<ChildT> &InnerMatcher)
      : InnerMatcher(InnerMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return Finder->matchesChildOf(Node, this->InnerMatcher, Builder,
                                  ASTMatchFinder::BK_First);
  }
};

/// Matches nodes of type T that have child nodes of type ChildT for
/// which a specified child matcher matches. ChildT must be an AST base
/// type.
/// As opposed to the HasMatcher, the ForEachMatcher will produce a match
/// for each child that matches.
template <typename T, typename ChildT>
class ForEachMatcher : public MatcherInterface<T> {
  static_assert(IsBaseType<ChildT>::value,
                "for each only accepts base type matcher");

  DynTypedMatcher InnerMatcher;

public:
  explicit ForEachMatcher(const Matcher<ChildT> &InnerMatcher)
      : InnerMatcher(InnerMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return Finder->matchesChildOf(
        Node, this->InnerMatcher, Builder,
        ASTMatchFinder::BK_All);
  }
};

/// @}

template <typename T>
inline Matcher<T> DynTypedMatcher::unconditionalConvertTo() const {
  return Matcher<T>(*this);
}

/// Matches nodes of type T that have at least one descendant node of
/// type DescendantT for which the given inner matcher matches.
///
/// DescendantT must be an AST base type.
template <typename T, typename DescendantT>
class HasDescendantMatcher : public MatcherInterface<T> {
  static_assert(IsBaseType<DescendantT>::value,
                "has descendant only accepts base type matcher");

  DynTypedMatcher DescendantMatcher;

public:
  explicit HasDescendantMatcher(const Matcher<DescendantT> &DescendantMatcher)
      : DescendantMatcher(DescendantMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return Finder->matchesDescendantOf(Node, this->DescendantMatcher, Builder,
                                       ASTMatchFinder::BK_First);
  }
};

/// Matches nodes of type \c T that have a parent node of type \c ParentT
/// for which the given inner matcher matches.
///
/// \c ParentT must be an AST base type.
template <typename T, typename ParentT>
class HasParentMatcher : public MatcherInterface<T> {
  static_assert(IsBaseType<ParentT>::value,
                "has parent only accepts base type matcher");

  DynTypedMatcher ParentMatcher;

public:
  explicit HasParentMatcher(const Matcher<ParentT> &ParentMatcher)
      : ParentMatcher(ParentMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return Finder->matchesAncestorOf(Node, this->ParentMatcher, Builder,
                                     ASTMatchFinder::AMM_ParentOnly);
  }
};

/// Matches nodes of type \c T that have at least one ancestor node of
/// type \c AncestorT for which the given inner matcher matches.
///
/// \c AncestorT must be an AST base type.
template <typename T, typename AncestorT>
class HasAncestorMatcher : public MatcherInterface<T> {
  static_assert(IsBaseType<AncestorT>::value,
                "has ancestor only accepts base type matcher");

  DynTypedMatcher AncestorMatcher;

public:
  explicit HasAncestorMatcher(const Matcher<AncestorT> &AncestorMatcher)
      : AncestorMatcher(AncestorMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return Finder->matchesAncestorOf(Node, this->AncestorMatcher, Builder,
                                     ASTMatchFinder::AMM_All);
  }
};

/// Matches nodes of type T that have at least one descendant node of
/// type DescendantT for which the given inner matcher matches.
///
/// DescendantT must be an AST base type.
/// As opposed to HasDescendantMatcher, ForEachDescendantMatcher will match
/// for each descendant node that matches instead of only for the first.
template <typename T, typename DescendantT>
class ForEachDescendantMatcher : public MatcherInterface<T> {
  static_assert(IsBaseType<DescendantT>::value,
                "for each descendant only accepts base type matcher");

  DynTypedMatcher DescendantMatcher;

public:
  explicit ForEachDescendantMatcher(
      const Matcher<DescendantT> &DescendantMatcher)
      : DescendantMatcher(DescendantMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return Finder->matchesDescendantOf(Node, this->DescendantMatcher, Builder,
                                       ASTMatchFinder::BK_All);
  }
};

/// Matches on nodes that have a getValue() method if getValue() equals
/// the value the ValueEqualsMatcher was constructed with.
template <typename T, typename ValueT>
class ValueEqualsMatcher : public SingleNodeMatcherInterface<T> {
  static_assert(std::is_base_of<CharacterLiteral, T>::value ||
                std::is_base_of<CXXBoolLiteralExpr, T>::value ||
                std::is_base_of<FloatingLiteral, T>::value ||
                std::is_base_of<IntegerLiteral, T>::value,
                "the node must have a getValue method");

public:
  explicit ValueEqualsMatcher(const ValueT &ExpectedValue)
      : ExpectedValue(ExpectedValue) {}

  bool matchesNode(const T &Node) const override {
    return Node.getValue() == ExpectedValue;
  }

private:
  ValueT ExpectedValue;
};

/// Template specializations to easily write matchers for floating point
/// literals.
template <>
inline bool ValueEqualsMatcher<FloatingLiteral, double>::matchesNode(
    const FloatingLiteral &Node) const {
  if ((&Node.getSemantics()) == &llvm::APFloat::IEEEsingle())
    return Node.getValue().convertToFloat() == ExpectedValue;
  if ((&Node.getSemantics()) == &llvm::APFloat::IEEEdouble())
    return Node.getValue().convertToDouble() == ExpectedValue;
  return false;
}
template <>
inline bool ValueEqualsMatcher<FloatingLiteral, float>::matchesNode(
    const FloatingLiteral &Node) const {
  if ((&Node.getSemantics()) == &llvm::APFloat::IEEEsingle())
    return Node.getValue().convertToFloat() == ExpectedValue;
  if ((&Node.getSemantics()) == &llvm::APFloat::IEEEdouble())
    return Node.getValue().convertToDouble() == ExpectedValue;
  return false;
}
template <>
inline bool ValueEqualsMatcher<FloatingLiteral, llvm::APFloat>::matchesNode(
    const FloatingLiteral &Node) const {
  return ExpectedValue.compare(Node.getValue()) == llvm::APFloat::cmpEqual;
}

/// Matches nodes of type \c TLoc for which the inner
/// \c Matcher<T> matches.
template <typename TLoc, typename T>
class LocMatcher : public MatcherInterface<TLoc> {
  DynTypedMatcher InnerMatcher;

public:
  explicit LocMatcher(const Matcher<T> &InnerMatcher)
      : InnerMatcher(InnerMatcher) {}

  bool matches(const TLoc &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    if (!Node)
      return false;
    return this->InnerMatcher.matches(extract(Node), Finder, Builder);
  }

private:
  static DynTypedNode extract(const NestedNameSpecifierLoc &Loc) {
    return DynTypedNode::create(*Loc.getNestedNameSpecifier());
  }
};

/// Matches \c TypeLocs based on an inner matcher matching a certain
/// \c QualType.
///
/// Used to implement the \c loc() matcher.
class TypeLocTypeMatcher : public MatcherInterface<TypeLoc> {
  DynTypedMatcher InnerMatcher;

public:
  explicit TypeLocTypeMatcher(const Matcher<QualType> &InnerMatcher)
      : InnerMatcher(InnerMatcher) {}

  bool matches(const TypeLoc &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    if (!Node)
      return false;
    return this->InnerMatcher.matches(DynTypedNode::create(Node.getType()),
                                      Finder, Builder);
  }
};

/// Matches nodes of type \c T for which the inner matcher matches on a
/// another node of type \c T that can be reached using a given traverse
/// function.
template <typename T> class TypeTraverseMatcher : public MatcherInterface<T> {
  DynTypedMatcher InnerMatcher;

public:
  explicit TypeTraverseMatcher(const Matcher<QualType> &InnerMatcher,
                               QualType (T::*TraverseFunction)() const)
      : InnerMatcher(InnerMatcher), TraverseFunction(TraverseFunction) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    QualType NextNode = (Node.*TraverseFunction)();
    if (NextNode.isNull())
      return false;
    return this->InnerMatcher.matches(DynTypedNode::create(NextNode), Finder,
                                      Builder);
  }

private:
  QualType (T::*TraverseFunction)() const;
};

/// Matches nodes of type \c T in a ..Loc hierarchy, for which the inner
/// matcher matches on a another node of type \c T that can be reached using a
/// given traverse function.
template <typename T>
class TypeLocTraverseMatcher : public MatcherInterface<T> {
  DynTypedMatcher InnerMatcher;

public:
  explicit TypeLocTraverseMatcher(const Matcher<TypeLoc> &InnerMatcher,
                                  TypeLoc (T::*TraverseFunction)() const)
      : InnerMatcher(InnerMatcher), TraverseFunction(TraverseFunction) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    TypeLoc NextNode = (Node.*TraverseFunction)();
    if (!NextNode)
      return false;
    return this->InnerMatcher.matches(DynTypedNode::create(NextNode), Finder,
                                      Builder);
  }

private:
  TypeLoc (T::*TraverseFunction)() const;
};

/// Converts a \c Matcher<InnerT> to a \c Matcher<OuterT>, where
/// \c OuterT is any type that is supported by \c Getter.
///
/// \code Getter<OuterT>::value() \endcode returns a
/// \code InnerTBase (OuterT::*)() \endcode, which is used to adapt a \c OuterT
/// object into a \c InnerT
template <typename InnerTBase,
          template <typename OuterT> class Getter,
          template <typename OuterT> class MatcherImpl,
          typename ReturnTypesF>
class TypeTraversePolymorphicMatcher {
private:
  using Self = TypeTraversePolymorphicMatcher<InnerTBase, Getter, MatcherImpl,
                                              ReturnTypesF>;

  static Self create(ArrayRef<const Matcher<InnerTBase> *> InnerMatchers);

public:
  using ReturnTypes = typename ExtractFunctionArgMeta<ReturnTypesF>::type;

  explicit TypeTraversePolymorphicMatcher(
      ArrayRef<const Matcher<InnerTBase> *> InnerMatchers)
      : InnerMatcher(makeAllOfComposite(InnerMatchers)) {}

  template <typename OuterT> operator Matcher<OuterT>() const {
    return Matcher<OuterT>(
        new MatcherImpl<OuterT>(InnerMatcher, Getter<OuterT>::value()));
  }

  struct Func
      : public VariadicFunction<Self, Matcher<InnerTBase>, &Self::create> {
    Func() {}
  };

private:
  Matcher<InnerTBase> InnerMatcher;
};

/// A simple memoizer of T(*)() functions.
///
/// It will call the passed 'Func' template parameter at most once.
/// Used to support AST_MATCHER_FUNCTION() macro.
template <typename Matcher, Matcher (*Func)()> class MemoizedMatcher {
  struct Wrapper {
    Wrapper() : M(Func()) {}

    Matcher M;
  };

public:
  static const Matcher &getInstance() {
    static llvm::ManagedStatic<Wrapper> Instance;
    return Instance->M;
  }
};

// Define the create() method out of line to silence a GCC warning about
// the struct "Func" having greater visibility than its base, which comes from
// using the flag -fvisibility-inlines-hidden.
template <typename InnerTBase, template <typename OuterT> class Getter,
          template <typename OuterT> class MatcherImpl, typename ReturnTypesF>
TypeTraversePolymorphicMatcher<InnerTBase, Getter, MatcherImpl, ReturnTypesF>
TypeTraversePolymorphicMatcher<
    InnerTBase, Getter, MatcherImpl,
    ReturnTypesF>::create(ArrayRef<const Matcher<InnerTBase> *> InnerMatchers) {
  return Self(InnerMatchers);
}

// FIXME: unify ClassTemplateSpecializationDecl and TemplateSpecializationType's
// APIs for accessing the template argument list.
inline ArrayRef<TemplateArgument>
getTemplateSpecializationArgs(const ClassTemplateSpecializationDecl &D) {
  return D.getTemplateArgs().asArray();
}

inline ArrayRef<TemplateArgument>
getTemplateSpecializationArgs(const VarTemplateSpecializationDecl &D) {
  return D.getTemplateArgs().asArray();
}

inline ArrayRef<TemplateArgument>
getTemplateSpecializationArgs(const TemplateSpecializationType &T) {
  return T.template_arguments();
}

inline ArrayRef<TemplateArgument>
getTemplateSpecializationArgs(const FunctionDecl &FD) {
  if (const auto* TemplateArgs = FD.getTemplateSpecializationArgs())
    return TemplateArgs->asArray();
  return std::nullopt;
}

inline ArrayRef<TemplateArgumentLoc>
getTemplateArgsWritten(const ClassTemplateSpecializationDecl &D) {
  if (const ASTTemplateArgumentListInfo *Args = D.getTemplateArgsAsWritten())
    return Args->arguments();
  return std::nullopt;
}

inline ArrayRef<TemplateArgumentLoc>
getTemplateArgsWritten(const VarTemplateSpecializationDecl &D) {
  if (const ASTTemplateArgumentListInfo *Args = D.getTemplateArgsAsWritten())
    return Args->arguments();
  return std::nullopt;
}

inline ArrayRef<TemplateArgumentLoc>
getTemplateArgsWritten(const FunctionDecl &FD) {
  if (const auto *Args = FD.getTemplateSpecializationArgsAsWritten())
    return Args->arguments();
  return std::nullopt;
}

inline ArrayRef<TemplateArgumentLoc>
getTemplateArgsWritten(const DeclRefExpr &DRE) {
  if (const auto *Args = DRE.getTemplateArgs())
    return {Args, DRE.getNumTemplateArgs()};
  return std::nullopt;
}

inline SmallVector<TemplateArgumentLoc>
getTemplateArgsWritten(const TemplateSpecializationTypeLoc &T) {
  SmallVector<TemplateArgumentLoc> Args;
  if (!T.isNull()) {
    Args.reserve(T.getNumArgs());
    for (unsigned I = 0; I < T.getNumArgs(); ++I)
      Args.emplace_back(T.getArgLoc(I));
  }
  return Args;
}

struct NotEqualsBoundNodePredicate {
  bool operator()(const internal::BoundNodesMap &Nodes) const {
    return Nodes.getNode(ID) != Node;
  }

  std::string ID;
  DynTypedNode Node;
};

template <typename Ty, typename Enable = void> struct GetBodyMatcher {
  static const Stmt *get(const Ty &Node) { return Node.getBody(); }
};

template <typename Ty>
struct GetBodyMatcher<
    Ty, std::enable_if_t<std::is_base_of<FunctionDecl, Ty>::value>> {
  static const Stmt *get(const Ty &Node) {
    return Node.doesThisDeclarationHaveABody() ? Node.getBody() : nullptr;
  }
};

template <typename NodeType>
inline std::optional<BinaryOperatorKind>
equivalentBinaryOperator(const NodeType &Node) {
  return Node.getOpcode();
}

template <>
inline std::optional<BinaryOperatorKind>
equivalentBinaryOperator<CXXOperatorCallExpr>(const CXXOperatorCallExpr &Node) {
  if (Node.getNumArgs() != 2)
    return std::nullopt;
  switch (Node.getOperator()) {
  default:
    return std::nullopt;
  case OO_ArrowStar:
    return BO_PtrMemI;
  case OO_Star:
    return BO_Mul;
  case OO_Slash:
    return BO_Div;
  case OO_Percent:
    return BO_Rem;
  case OO_Plus:
    return BO_Add;
  case OO_Minus:
    return BO_Sub;
  case OO_LessLess:
    return BO_Shl;
  case OO_GreaterGreater:
    return BO_Shr;
  case OO_Spaceship:
    return BO_Cmp;
  case OO_Less:
    return BO_LT;
  case OO_Greater:
    return BO_GT;
  case OO_LessEqual:
    return BO_LE;
  case OO_GreaterEqual:
    return BO_GE;
  case OO_EqualEqual:
    return BO_EQ;
  case OO_ExclaimEqual:
    return BO_NE;
  case OO_Amp:
    return BO_And;
  case OO_Caret:
    return BO_Xor;
  case OO_Pipe:
    return BO_Or;
  case OO_AmpAmp:
    return BO_LAnd;
  case OO_PipePipe:
    return BO_LOr;
  case OO_Equal:
    return BO_Assign;
  case OO_StarEqual:
    return BO_MulAssign;
  case OO_SlashEqual:
    return BO_DivAssign;
  case OO_PercentEqual:
    return BO_RemAssign;
  case OO_PlusEqual:
    return BO_AddAssign;
  case OO_MinusEqual:
    return BO_SubAssign;
  case OO_LessLessEqual:
    return BO_ShlAssign;
  case OO_GreaterGreaterEqual:
    return BO_ShrAssign;
  case OO_AmpEqual:
    return BO_AndAssign;
  case OO_CaretEqual:
    return BO_XorAssign;
  case OO_PipeEqual:
    return BO_OrAssign;
  case OO_Comma:
    return BO_Comma;
  }
}

template <typename NodeType>
inline std::optional<UnaryOperatorKind>
equivalentUnaryOperator(const NodeType &Node) {
  return Node.getOpcode();
}

template <>
inline std::optional<UnaryOperatorKind>
equivalentUnaryOperator<CXXOperatorCallExpr>(const CXXOperatorCallExpr &Node) {
  if (Node.getNumArgs() != 1 && Node.getOperator() != OO_PlusPlus &&
      Node.getOperator() != OO_MinusMinus)
    return std::nullopt;
  switch (Node.getOperator()) {
  default:
    return std::nullopt;
  case OO_Plus:
    return UO_Plus;
  case OO_Minus:
    return UO_Minus;
  case OO_Amp:
    return UO_AddrOf;
  case OO_Star:
    return UO_Deref;
  case OO_Tilde:
    return UO_Not;
  case OO_Exclaim:
    return UO_LNot;
  case OO_PlusPlus: {
    const auto *FD = Node.getDirectCallee();
    if (!FD)
      return std::nullopt;
    return FD->getNumParams() > 0 ? UO_PostInc : UO_PreInc;
  }
  case OO_MinusMinus: {
    const auto *FD = Node.getDirectCallee();
    if (!FD)
      return std::nullopt;
    return FD->getNumParams() > 0 ? UO_PostDec : UO_PreDec;
  }
  case OO_Coawait:
    return UO_Coawait;
  }
}

template <typename NodeType> inline const Expr *getLHS(const NodeType &Node) {
  return Node.getLHS();
}
template <>
inline const Expr *
getLHS<CXXOperatorCallExpr>(const CXXOperatorCallExpr &Node) {
  if (!internal::equivalentBinaryOperator(Node))
    return nullptr;
  return Node.getArg(0);
}
template <typename NodeType> inline const Expr *getRHS(const NodeType &Node) {
  return Node.getRHS();
}
template <>
inline const Expr *
getRHS<CXXOperatorCallExpr>(const CXXOperatorCallExpr &Node) {
  if (!internal::equivalentBinaryOperator(Node))
    return nullptr;
  return Node.getArg(1);
}
template <typename NodeType>
inline const Expr *getSubExpr(const NodeType &Node) {
  return Node.getSubExpr();
}
template <>
inline const Expr *
getSubExpr<CXXOperatorCallExpr>(const CXXOperatorCallExpr &Node) {
  if (!internal::equivalentUnaryOperator(Node))
    return nullptr;
  return Node.getArg(0);
}

template <typename Ty>
struct HasSizeMatcher {
  static bool hasSize(const Ty &Node, unsigned int N) {
    return Node.getSize() == N;
  }
};

template <>
inline bool HasSizeMatcher<StringLiteral>::hasSize(
    const StringLiteral &Node, unsigned int N) {
  return Node.getLength() == N;
}

template <typename Ty>
struct GetSourceExpressionMatcher {
  static const Expr *get(const Ty &Node) {
    return Node.getSubExpr();
  }
};

template <>
inline const Expr *GetSourceExpressionMatcher<OpaqueValueExpr>::get(
    const OpaqueValueExpr &Node) {
  return Node.getSourceExpr();
}

template <typename Ty>
struct CompoundStmtMatcher {
  static const CompoundStmt *get(const Ty &Node) {
    return &Node;
  }
};

template <>
inline const CompoundStmt *
CompoundStmtMatcher<StmtExpr>::get(const StmtExpr &Node) {
  return Node.getSubStmt();
}

/// If \p Loc is (transitively) expanded from macro \p MacroName, returns the
/// location (in the chain of expansions) at which \p MacroName was
/// expanded. Since the macro may have been expanded inside a series of
/// expansions, that location may itself be a MacroID.
std::optional<SourceLocation> getExpansionLocOfMacro(StringRef MacroName,
                                                     SourceLocation Loc,
                                                     const ASTContext &Context);

inline std::optional<StringRef> getOpName(const UnaryOperator &Node) {
  return Node.getOpcodeStr(Node.getOpcode());
}
inline std::optional<StringRef> getOpName(const BinaryOperator &Node) {
  return Node.getOpcodeStr();
}
inline StringRef getOpName(const CXXRewrittenBinaryOperator &Node) {
  return Node.getOpcodeStr();
}
inline std::optional<StringRef> getOpName(const CXXOperatorCallExpr &Node) {
  auto optBinaryOpcode = equivalentBinaryOperator(Node);
  if (!optBinaryOpcode) {
    auto optUnaryOpcode = equivalentUnaryOperator(Node);
    if (!optUnaryOpcode)
      return std::nullopt;
    return UnaryOperator::getOpcodeStr(*optUnaryOpcode);
  }
  return BinaryOperator::getOpcodeStr(*optBinaryOpcode);
}
inline StringRef getOpName(const CXXFoldExpr &Node) {
  return BinaryOperator::getOpcodeStr(Node.getOperator());
}

/// Matches overloaded operators with a specific name.
///
/// The type argument ArgT is not used by this matcher but is used by
/// PolymorphicMatcher and should be std::vector<std::string>>.
template <typename T, typename ArgT = std::vector<std::string>>
class HasAnyOperatorNameMatcher : public SingleNodeMatcherInterface<T> {
  static_assert(std::is_same<T, BinaryOperator>::value ||
                    std::is_same<T, CXXOperatorCallExpr>::value ||
                    std::is_same<T, CXXRewrittenBinaryOperator>::value ||
                    std::is_same<T, UnaryOperator>::value,
                "Matcher only supports `BinaryOperator`, `UnaryOperator`, "
                "`CXXOperatorCallExpr` and `CXXRewrittenBinaryOperator`");
  static_assert(std::is_same<ArgT, std::vector<std::string>>::value,
                "Matcher ArgT must be std::vector<std::string>");

public:
  explicit HasAnyOperatorNameMatcher(std::vector<std::string> Names)
      : SingleNodeMatcherInterface<T>(), Names(std::move(Names)) {}

  bool matchesNode(const T &Node) const override {
    std::optional<StringRef> OptOpName = getOpName(Node);
    return OptOpName && llvm::is_contained(Names, *OptOpName);
  }

private:
  static std::optional<StringRef> getOpName(const UnaryOperator &Node) {
    return Node.getOpcodeStr(Node.getOpcode());
  }
  static std::optional<StringRef> getOpName(const BinaryOperator &Node) {
    return Node.getOpcodeStr();
  }
  static StringRef getOpName(const CXXRewrittenBinaryOperator &Node) {
    return Node.getOpcodeStr();
  }
  static std::optional<StringRef> getOpName(const CXXOperatorCallExpr &Node) {
    auto optBinaryOpcode = equivalentBinaryOperator(Node);
    if (!optBinaryOpcode) {
      auto optUnaryOpcode = equivalentUnaryOperator(Node);
      if (!optUnaryOpcode)
        return std::nullopt;
      return UnaryOperator::getOpcodeStr(*optUnaryOpcode);
    }
    return BinaryOperator::getOpcodeStr(*optBinaryOpcode);
  }

  std::vector<std::string> Names;
};

using HasOpNameMatcher =
    PolymorphicMatcher<HasAnyOperatorNameMatcher,
                       void(
                           TypeList<BinaryOperator, CXXOperatorCallExpr,
                                    CXXRewrittenBinaryOperator, UnaryOperator>),
                       std::vector<std::string>>;

HasOpNameMatcher hasAnyOperatorNameFunc(ArrayRef<const StringRef *> NameRefs);

using HasOverloadOpNameMatcher =
    PolymorphicMatcher<HasOverloadedOperatorNameMatcher,
                       void(TypeList<CXXOperatorCallExpr, FunctionDecl>),
                       std::vector<std::string>>;

HasOverloadOpNameMatcher
hasAnyOverloadedOperatorNameFunc(ArrayRef<const StringRef *> NameRefs);

/// Returns true if \p Node has a base specifier matching \p BaseSpec.
///
/// A class is not considered to be derived from itself.
bool matchesAnyBase(const CXXRecordDecl &Node,
                    const Matcher<CXXBaseSpecifier> &BaseSpecMatcher,
                    ASTMatchFinder *Finder, BoundNodesTreeBuilder *Builder);

std::shared_ptr<llvm::Regex> createAndVerifyRegex(StringRef Regex,
                                                  llvm::Regex::RegexFlags Flags,
                                                  StringRef MatcherID);

inline bool
MatchTemplateArgLocAt(const DeclRefExpr &Node, unsigned int Index,
                      internal::Matcher<TemplateArgumentLoc> InnerMatcher,
                      internal::ASTMatchFinder *Finder,
                      internal::BoundNodesTreeBuilder *Builder) {
  llvm::ArrayRef<TemplateArgumentLoc> ArgLocs = Node.template_arguments();
  return Index < ArgLocs.size() &&
         InnerMatcher.matches(ArgLocs[Index], Finder, Builder);
}

inline bool
MatchTemplateArgLocAt(const TemplateSpecializationTypeLoc &Node,
                      unsigned int Index,
                      internal::Matcher<TemplateArgumentLoc> InnerMatcher,
                      internal::ASTMatchFinder *Finder,
                      internal::BoundNodesTreeBuilder *Builder) {
  return !Node.isNull() && Index < Node.getNumArgs() &&
         InnerMatcher.matches(Node.getArgLoc(Index), Finder, Builder);
}

} // namespace internal

} // namespace ast_matchers

} // namespace clang

#endif // LLVM_CLANG_ASTMATCHERS_ASTMATCHERSINTERNAL_H

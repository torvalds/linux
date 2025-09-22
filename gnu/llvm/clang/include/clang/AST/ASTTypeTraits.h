//===--- ASTTypeTraits.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Provides a dynamic type identifier and a dynamically typed node container
//  that can be used to store an AST base node at runtime in the same storage in
//  a type safe way.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTTYPETRAITS_H
#define LLVM_CLANG_AST_ASTTYPETRAITS_H

#include "clang/AST/ASTFwd.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/LambdaCapture.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/AlignOf.h"

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace clang {

struct PrintingPolicy;

/// Defines how we descend a level in the AST when we pass
/// through expressions.
enum TraversalKind {
  /// Will traverse all child nodes.
  TK_AsIs,

  /// Ignore AST nodes not written in the source
  TK_IgnoreUnlessSpelledInSource
};

/// Kind identifier.
///
/// It can be constructed from any node kind and allows for runtime type
/// hierarchy checks.
/// Use getFromNodeKind<T>() to construct them.
class ASTNodeKind {
public:
  /// Empty identifier. It matches nothing.
  constexpr ASTNodeKind() : KindId(NKI_None) {}

  /// Construct an identifier for T.
  template <class T> static constexpr ASTNodeKind getFromNodeKind() {
    return ASTNodeKind(KindToKindId<T>::Id);
  }

  /// \{
  /// Construct an identifier for the dynamic type of the node
  static ASTNodeKind getFromNode(const Decl &D);
  static ASTNodeKind getFromNode(const Stmt &S);
  static ASTNodeKind getFromNode(const Type &T);
  static ASTNodeKind getFromNode(const TypeLoc &T);
  static ASTNodeKind getFromNode(const LambdaCapture &L);
  static ASTNodeKind getFromNode(const OMPClause &C);
  static ASTNodeKind getFromNode(const Attr &A);
  /// \}

  /// Returns \c true if \c this and \c Other represent the same kind.
  constexpr bool isSame(ASTNodeKind Other) const {
    return KindId != NKI_None && KindId == Other.KindId;
  }

  /// Returns \c true only for the default \c ASTNodeKind()
  constexpr bool isNone() const { return KindId == NKI_None; }

  /// Returns \c true if \c this is a base kind of (or same as) \c Other.
  bool isBaseOf(ASTNodeKind Other) const;

  /// Returns \c true if \c this is a base kind of (or same as) \c Other.
  /// \param Distance If non-null, used to return the distance between \c this
  /// and \c Other in the class hierarchy.
  bool isBaseOf(ASTNodeKind Other, unsigned *Distance) const;

  /// String representation of the kind.
  StringRef asStringRef() const;

  /// Strict weak ordering for ASTNodeKind.
  constexpr bool operator<(const ASTNodeKind &Other) const {
    return KindId < Other.KindId;
  }

  /// Return the most derived type between \p Kind1 and \p Kind2.
  ///
  /// Return ASTNodeKind() if they are not related.
  static ASTNodeKind getMostDerivedType(ASTNodeKind Kind1, ASTNodeKind Kind2);

  /// Return the most derived common ancestor between Kind1 and Kind2.
  ///
  /// Return ASTNodeKind() if they are not related.
  static ASTNodeKind getMostDerivedCommonAncestor(ASTNodeKind Kind1,
                                                  ASTNodeKind Kind2);

  ASTNodeKind getCladeKind() const;

  /// Hooks for using ASTNodeKind as a key in a DenseMap.
  struct DenseMapInfo {
    // ASTNodeKind() is a good empty key because it is represented as a 0.
    static inline ASTNodeKind getEmptyKey() { return ASTNodeKind(); }
    // NKI_NumberOfKinds is not a valid value, so it is good for a
    // tombstone key.
    static inline ASTNodeKind getTombstoneKey() {
      return ASTNodeKind(NKI_NumberOfKinds);
    }
    static unsigned getHashValue(const ASTNodeKind &Val) { return Val.KindId; }
    static bool isEqual(const ASTNodeKind &LHS, const ASTNodeKind &RHS) {
      return LHS.KindId == RHS.KindId;
    }
  };

  /// Check if the given ASTNodeKind identifies a type that offers pointer
  /// identity. This is useful for the fast path in DynTypedNode.
  constexpr bool hasPointerIdentity() const {
    return KindId > NKI_LastKindWithoutPointerIdentity;
  }

private:
  /// Kind ids.
  ///
  /// Includes all possible base and derived kinds.
  enum NodeKindId {
    NKI_None,
    NKI_TemplateArgument,
    NKI_TemplateArgumentLoc,
    NKI_LambdaCapture,
    NKI_TemplateName,
    NKI_NestedNameSpecifierLoc,
    NKI_QualType,
#define TYPELOC(CLASS, PARENT) NKI_##CLASS##TypeLoc,
#include "clang/AST/TypeLocNodes.def"
    NKI_TypeLoc,
    NKI_LastKindWithoutPointerIdentity = NKI_TypeLoc,
    NKI_CXXBaseSpecifier,
    NKI_CXXCtorInitializer,
    NKI_NestedNameSpecifier,
    NKI_Decl,
#define DECL(DERIVED, BASE) NKI_##DERIVED##Decl,
#include "clang/AST/DeclNodes.inc"
    NKI_Stmt,
#define STMT(DERIVED, BASE) NKI_##DERIVED,
#include "clang/AST/StmtNodes.inc"
    NKI_Type,
#define TYPE(DERIVED, BASE) NKI_##DERIVED##Type,
#include "clang/AST/TypeNodes.inc"
    NKI_OMPClause,
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class) NKI_##Class,
#include "llvm/Frontend/OpenMP/OMP.inc"
    NKI_Attr,
#define ATTR(A) NKI_##A##Attr,
#include "clang/Basic/AttrList.inc"
    NKI_ObjCProtocolLoc,
    NKI_ConceptReference,
    NKI_NumberOfKinds
  };

  /// Use getFromNodeKind<T>() to construct the kind.
  constexpr ASTNodeKind(NodeKindId KindId) : KindId(KindId) {}

  /// Returns \c true if \c Base is a base kind of (or same as) \c
  ///   Derived.
  static bool isBaseOf(NodeKindId Base, NodeKindId Derived);

  /// Returns \c true if \c Base is a base kind of (or same as) \c
  ///   Derived.
  /// \param Distance If non-null, used to return the distance between \c Base
  /// and \c Derived in the class hierarchy.
  static bool isBaseOf(NodeKindId Base, NodeKindId Derived, unsigned *Distance);

  /// Helper meta-function to convert a kind T to its enum value.
  ///
  /// This struct is specialized below for all known kinds.
  template <class T> struct KindToKindId {
    static const NodeKindId Id = NKI_None;
  };
  template <class T>
  struct KindToKindId<const T> : KindToKindId<T> {};

  /// Per kind info.
  struct KindInfo {
    /// The id of the parent kind, or None if it has no parent.
    NodeKindId ParentId;
    /// Name of the kind.
    const char *Name;
  };
  static const KindInfo AllKindInfo[NKI_NumberOfKinds];

  NodeKindId KindId;
};

#define KIND_TO_KIND_ID(Class)                                                 \
  template <> struct ASTNodeKind::KindToKindId<Class> {                        \
    static const NodeKindId Id = NKI_##Class;                                  \
  };
KIND_TO_KIND_ID(CXXCtorInitializer)
KIND_TO_KIND_ID(TemplateArgument)
KIND_TO_KIND_ID(TemplateArgumentLoc)
KIND_TO_KIND_ID(LambdaCapture)
KIND_TO_KIND_ID(TemplateName)
KIND_TO_KIND_ID(NestedNameSpecifier)
KIND_TO_KIND_ID(NestedNameSpecifierLoc)
KIND_TO_KIND_ID(QualType)
#define TYPELOC(CLASS, PARENT) KIND_TO_KIND_ID(CLASS##TypeLoc)
#include "clang/AST/TypeLocNodes.def"
KIND_TO_KIND_ID(TypeLoc)
KIND_TO_KIND_ID(Decl)
KIND_TO_KIND_ID(Stmt)
KIND_TO_KIND_ID(Type)
KIND_TO_KIND_ID(OMPClause)
KIND_TO_KIND_ID(Attr)
KIND_TO_KIND_ID(ObjCProtocolLoc)
KIND_TO_KIND_ID(CXXBaseSpecifier)
KIND_TO_KIND_ID(ConceptReference)
#define DECL(DERIVED, BASE) KIND_TO_KIND_ID(DERIVED##Decl)
#include "clang/AST/DeclNodes.inc"
#define STMT(DERIVED, BASE) KIND_TO_KIND_ID(DERIVED)
#include "clang/AST/StmtNodes.inc"
#define TYPE(DERIVED, BASE) KIND_TO_KIND_ID(DERIVED##Type)
#include "clang/AST/TypeNodes.inc"
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class) KIND_TO_KIND_ID(Class)
#include "llvm/Frontend/OpenMP/OMP.inc"
#define ATTR(A) KIND_TO_KIND_ID(A##Attr)
#include "clang/Basic/AttrList.inc"
#undef KIND_TO_KIND_ID

inline raw_ostream &operator<<(raw_ostream &OS, ASTNodeKind K) {
  OS << K.asStringRef();
  return OS;
}

/// A dynamically typed AST node container.
///
/// Stores an AST node in a type safe way. This allows writing code that
/// works with different kinds of AST nodes, despite the fact that they don't
/// have a common base class.
///
/// Use \c create(Node) to create a \c DynTypedNode from an AST node,
/// and \c get<T>() to retrieve the node as type T if the types match.
///
/// See \c ASTNodeKind for which node base types are currently supported;
/// You can create DynTypedNodes for all nodes in the inheritance hierarchy of
/// the supported base types.
class DynTypedNode {
public:
  /// Creates a \c DynTypedNode from \c Node.
  template <typename T>
  static DynTypedNode create(const T &Node) {
    return BaseConverter<T>::create(Node);
  }

  /// Retrieve the stored node as type \c T.
  ///
  /// Returns NULL if the stored node does not have a type that is
  /// convertible to \c T.
  ///
  /// For types that have identity via their pointer in the AST
  /// (like \c Stmt, \c Decl, \c Type and \c NestedNameSpecifier) the returned
  /// pointer points to the referenced AST node.
  /// For other types (like \c QualType) the value is stored directly
  /// in the \c DynTypedNode, and the returned pointer points at
  /// the storage inside DynTypedNode. For those nodes, do not
  /// use the pointer outside the scope of the DynTypedNode.
  template <typename T> const T *get() const {
    return BaseConverter<T>::get(NodeKind, &Storage);
  }

  /// Retrieve the stored node as type \c T.
  ///
  /// Similar to \c get(), but asserts that the type is what we are expecting.
  template <typename T>
  const T &getUnchecked() const {
    return BaseConverter<T>::getUnchecked(NodeKind, &Storage);
  }

  ASTNodeKind getNodeKind() const { return NodeKind; }

  /// Returns a pointer that identifies the stored AST node.
  ///
  /// Note that this is not supported by all AST nodes. For AST nodes
  /// that don't have a pointer-defined identity inside the AST, this
  /// method returns NULL.
  const void *getMemoizationData() const {
    return NodeKind.hasPointerIdentity()
               ? *reinterpret_cast<void *const *>(&Storage)
               : nullptr;
  }

  /// Prints the node to the given output stream.
  void print(llvm::raw_ostream &OS, const PrintingPolicy &PP) const;

  /// Dumps the node to the given output stream.
  void dump(llvm::raw_ostream &OS, const ASTContext &Context) const;

  /// For nodes which represent textual entities in the source code,
  /// return their SourceRange.  For all other nodes, return SourceRange().
  SourceRange getSourceRange() const;

  /// @{
  /// Imposes an order on \c DynTypedNode.
  ///
  /// Supports comparison of nodes that support memoization.
  /// FIXME: Implement comparison for other node types (currently
  /// only Stmt, Decl, Type and NestedNameSpecifier return memoization data).
  bool operator<(const DynTypedNode &Other) const {
    if (!NodeKind.isSame(Other.NodeKind))
      return NodeKind < Other.NodeKind;

    if (ASTNodeKind::getFromNodeKind<QualType>().isSame(NodeKind))
      return getUnchecked<QualType>().getAsOpaquePtr() <
             Other.getUnchecked<QualType>().getAsOpaquePtr();

    if (ASTNodeKind::getFromNodeKind<TypeLoc>().isBaseOf(NodeKind)) {
      auto TLA = getUnchecked<TypeLoc>();
      auto TLB = Other.getUnchecked<TypeLoc>();
      return std::make_pair(TLA.getType().getAsOpaquePtr(),
                            TLA.getOpaqueData()) <
             std::make_pair(TLB.getType().getAsOpaquePtr(),
                            TLB.getOpaqueData());
    }

    if (ASTNodeKind::getFromNodeKind<NestedNameSpecifierLoc>().isSame(
            NodeKind)) {
      auto NNSLA = getUnchecked<NestedNameSpecifierLoc>();
      auto NNSLB = Other.getUnchecked<NestedNameSpecifierLoc>();
      return std::make_pair(NNSLA.getNestedNameSpecifier(),
                            NNSLA.getOpaqueData()) <
             std::make_pair(NNSLB.getNestedNameSpecifier(),
                            NNSLB.getOpaqueData());
    }

    assert(getMemoizationData() && Other.getMemoizationData());
    return getMemoizationData() < Other.getMemoizationData();
  }
  bool operator==(const DynTypedNode &Other) const {
    // DynTypedNode::create() stores the exact kind of the node in NodeKind.
    // If they contain the same node, their NodeKind must be the same.
    if (!NodeKind.isSame(Other.NodeKind))
      return false;

    // FIXME: Implement for other types.
    if (ASTNodeKind::getFromNodeKind<QualType>().isSame(NodeKind))
      return getUnchecked<QualType>() == Other.getUnchecked<QualType>();

    if (ASTNodeKind::getFromNodeKind<TypeLoc>().isBaseOf(NodeKind))
      return getUnchecked<TypeLoc>() == Other.getUnchecked<TypeLoc>();

    if (ASTNodeKind::getFromNodeKind<NestedNameSpecifierLoc>().isSame(NodeKind))
      return getUnchecked<NestedNameSpecifierLoc>() ==
             Other.getUnchecked<NestedNameSpecifierLoc>();

    assert(getMemoizationData() && Other.getMemoizationData());
    return getMemoizationData() == Other.getMemoizationData();
  }
  bool operator!=(const DynTypedNode &Other) const {
    return !operator==(Other);
  }
  /// @}

  /// Hooks for using DynTypedNode as a key in a DenseMap.
  struct DenseMapInfo {
    static inline DynTypedNode getEmptyKey() {
      DynTypedNode Node;
      Node.NodeKind = ASTNodeKind::DenseMapInfo::getEmptyKey();
      return Node;
    }
    static inline DynTypedNode getTombstoneKey() {
      DynTypedNode Node;
      Node.NodeKind = ASTNodeKind::DenseMapInfo::getTombstoneKey();
      return Node;
    }
    static unsigned getHashValue(const DynTypedNode &Val) {
      // FIXME: Add hashing support for the remaining types.
      if (ASTNodeKind::getFromNodeKind<TypeLoc>().isBaseOf(Val.NodeKind)) {
        auto TL = Val.getUnchecked<TypeLoc>();
        return llvm::hash_combine(TL.getType().getAsOpaquePtr(),
                                  TL.getOpaqueData());
      }

      if (ASTNodeKind::getFromNodeKind<NestedNameSpecifierLoc>().isSame(
              Val.NodeKind)) {
        auto NNSL = Val.getUnchecked<NestedNameSpecifierLoc>();
        return llvm::hash_combine(NNSL.getNestedNameSpecifier(),
                                  NNSL.getOpaqueData());
      }

      assert(Val.getMemoizationData());
      return llvm::hash_value(Val.getMemoizationData());
    }
    static bool isEqual(const DynTypedNode &LHS, const DynTypedNode &RHS) {
      auto Empty = ASTNodeKind::DenseMapInfo::getEmptyKey();
      auto TombStone = ASTNodeKind::DenseMapInfo::getTombstoneKey();
      return (ASTNodeKind::DenseMapInfo::isEqual(LHS.NodeKind, Empty) &&
              ASTNodeKind::DenseMapInfo::isEqual(RHS.NodeKind, Empty)) ||
             (ASTNodeKind::DenseMapInfo::isEqual(LHS.NodeKind, TombStone) &&
              ASTNodeKind::DenseMapInfo::isEqual(RHS.NodeKind, TombStone)) ||
             LHS == RHS;
    }
  };

private:
  /// Takes care of converting from and to \c T.
  template <typename T, typename EnablerT = void> struct BaseConverter;

  /// Converter that uses dyn_cast<T> from a stored BaseT*.
  template <typename T, typename BaseT> struct DynCastPtrConverter {
    static const T *get(ASTNodeKind NodeKind, const void *Storage) {
      if (ASTNodeKind::getFromNodeKind<T>().isBaseOf(NodeKind))
        return &getUnchecked(NodeKind, Storage);
      return nullptr;
    }
    static const T &getUnchecked(ASTNodeKind NodeKind, const void *Storage) {
      assert(ASTNodeKind::getFromNodeKind<T>().isBaseOf(NodeKind));
      return *cast<T>(static_cast<const BaseT *>(
          *reinterpret_cast<const void *const *>(Storage)));
    }
    static DynTypedNode create(const BaseT &Node) {
      DynTypedNode Result;
      Result.NodeKind = ASTNodeKind::getFromNode(Node);
      new (&Result.Storage) const void *(&Node);
      return Result;
    }
  };

  /// Converter that stores T* (by pointer).
  template <typename T> struct PtrConverter {
    static const T *get(ASTNodeKind NodeKind, const void *Storage) {
      if (ASTNodeKind::getFromNodeKind<T>().isSame(NodeKind))
        return &getUnchecked(NodeKind, Storage);
      return nullptr;
    }
    static const T &getUnchecked(ASTNodeKind NodeKind, const void *Storage) {
      assert(ASTNodeKind::getFromNodeKind<T>().isSame(NodeKind));
      return *static_cast<const T *>(
          *reinterpret_cast<const void *const *>(Storage));
    }
    static DynTypedNode create(const T &Node) {
      DynTypedNode Result;
      Result.NodeKind = ASTNodeKind::getFromNodeKind<T>();
      new (&Result.Storage) const void *(&Node);
      return Result;
    }
  };

  /// Converter that stores T (by value).
  template <typename T> struct ValueConverter {
    static const T *get(ASTNodeKind NodeKind, const void *Storage) {
      if (ASTNodeKind::getFromNodeKind<T>().isSame(NodeKind))
        return reinterpret_cast<const T *>(Storage);
      return nullptr;
    }
    static const T &getUnchecked(ASTNodeKind NodeKind, const void *Storage) {
      assert(ASTNodeKind::getFromNodeKind<T>().isSame(NodeKind));
      return *reinterpret_cast<const T *>(Storage);
    }
    static DynTypedNode create(const T &Node) {
      DynTypedNode Result;
      Result.NodeKind = ASTNodeKind::getFromNodeKind<T>();
      new (&Result.Storage) T(Node);
      return Result;
    }
  };

  /// Converter that stores nodes by value. It must be possible to dynamically
  /// cast the stored node within a type hierarchy without breaking (especially
  /// through slicing).
  template <typename T, typename BaseT,
            typename = std::enable_if_t<(sizeof(T) == sizeof(BaseT))>>
  struct DynCastValueConverter {
    static const T *get(ASTNodeKind NodeKind, const void *Storage) {
      if (ASTNodeKind::getFromNodeKind<T>().isBaseOf(NodeKind))
        return &getUnchecked(NodeKind, Storage);
      return nullptr;
    }
    static const T &getUnchecked(ASTNodeKind NodeKind, const void *Storage) {
      assert(ASTNodeKind::getFromNodeKind<T>().isBaseOf(NodeKind));
      return *static_cast<const T *>(reinterpret_cast<const BaseT *>(Storage));
    }
    static DynTypedNode create(const T &Node) {
      DynTypedNode Result;
      Result.NodeKind = ASTNodeKind::getFromNode(Node);
      new (&Result.Storage) T(Node);
      return Result;
    }
  };

  ASTNodeKind NodeKind;

  /// Stores the data of the node.
  ///
  /// Note that we can store \c Decls, \c Stmts, \c Types,
  /// \c NestedNameSpecifiers and \c CXXCtorInitializer by pointer as they are
  /// guaranteed to be unique pointers pointing to dedicated storage in the AST.
  /// \c QualTypes, \c NestedNameSpecifierLocs, \c TypeLocs,
  /// \c TemplateArguments and \c TemplateArgumentLocs on the other hand do not
  /// have storage or unique pointers and thus need to be stored by value.
  llvm::AlignedCharArrayUnion<const void *, TemplateArgument,
                              TemplateArgumentLoc, NestedNameSpecifierLoc,
                              QualType, TypeLoc, ObjCProtocolLoc>
      Storage;
};

template <typename T>
struct DynTypedNode::BaseConverter<
    T, std::enable_if_t<std::is_base_of<Decl, T>::value>>
    : public DynCastPtrConverter<T, Decl> {};

template <typename T>
struct DynTypedNode::BaseConverter<
    T, std::enable_if_t<std::is_base_of<Stmt, T>::value>>
    : public DynCastPtrConverter<T, Stmt> {};

template <typename T>
struct DynTypedNode::BaseConverter<
    T, std::enable_if_t<std::is_base_of<Type, T>::value>>
    : public DynCastPtrConverter<T, Type> {};

template <typename T>
struct DynTypedNode::BaseConverter<
    T, std::enable_if_t<std::is_base_of<OMPClause, T>::value>>
    : public DynCastPtrConverter<T, OMPClause> {};

template <typename T>
struct DynTypedNode::BaseConverter<
    T, std::enable_if_t<std::is_base_of<Attr, T>::value>>
    : public DynCastPtrConverter<T, Attr> {};

template <>
struct DynTypedNode::BaseConverter<
    NestedNameSpecifier, void> : public PtrConverter<NestedNameSpecifier> {};

template <>
struct DynTypedNode::BaseConverter<
    CXXCtorInitializer, void> : public PtrConverter<CXXCtorInitializer> {};

template <>
struct DynTypedNode::BaseConverter<
    TemplateArgument, void> : public ValueConverter<TemplateArgument> {};

template <>
struct DynTypedNode::BaseConverter<TemplateArgumentLoc, void>
    : public ValueConverter<TemplateArgumentLoc> {};

template <>
struct DynTypedNode::BaseConverter<LambdaCapture, void>
    : public ValueConverter<LambdaCapture> {};

template <>
struct DynTypedNode::BaseConverter<
    TemplateName, void> : public ValueConverter<TemplateName> {};

template <>
struct DynTypedNode::BaseConverter<
    NestedNameSpecifierLoc,
    void> : public ValueConverter<NestedNameSpecifierLoc> {};

template <>
struct DynTypedNode::BaseConverter<QualType,
                                   void> : public ValueConverter<QualType> {};

template <typename T>
struct DynTypedNode::BaseConverter<
    T, std::enable_if_t<std::is_base_of<TypeLoc, T>::value>>
    : public DynCastValueConverter<T, TypeLoc> {};

template <>
struct DynTypedNode::BaseConverter<CXXBaseSpecifier, void>
    : public PtrConverter<CXXBaseSpecifier> {};

template <>
struct DynTypedNode::BaseConverter<ObjCProtocolLoc, void>
    : public ValueConverter<ObjCProtocolLoc> {};

template <>
struct DynTypedNode::BaseConverter<ConceptReference, void>
    : public PtrConverter<ConceptReference> {};

// The only operation we allow on unsupported types is \c get.
// This allows to conveniently use \c DynTypedNode when having an arbitrary
// AST node that is not supported, but prevents misuse - a user cannot create
// a DynTypedNode from arbitrary types.
template <typename T, typename EnablerT> struct DynTypedNode::BaseConverter {
  static const T *get(ASTNodeKind NodeKind, const char Storage[]) {
    return NULL;
  }
};

} // end namespace clang

namespace llvm {

template <>
struct DenseMapInfo<clang::ASTNodeKind> : clang::ASTNodeKind::DenseMapInfo {};

template <>
struct DenseMapInfo<clang::DynTypedNode> : clang::DynTypedNode::DenseMapInfo {};

}  // end namespace llvm

#endif

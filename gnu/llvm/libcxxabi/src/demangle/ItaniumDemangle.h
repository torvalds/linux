//===------------------------- ItaniumDemangle.h ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic itanium demangler library.
// There are two copies of this file in the source tree.  The one under
// libcxxabi is the original and the one under llvm is the copy.  Use
// cp-to-llvm.sh to update the copy.  See README.txt for more details.
//
//===----------------------------------------------------------------------===//

#ifndef DEMANGLE_ITANIUMDEMANGLE_H
#define DEMANGLE_ITANIUMDEMANGLE_H

#include "DemangleConfig.h"
#include "StringViewExtras.h"
#include "Utility.h"
#include <__cxxabi_config.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

#ifdef _LIBCXXABI_COMPILER_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-template"
#endif

DEMANGLE_NAMESPACE_BEGIN

template <class T, size_t N> class PODSmallVector {
  static_assert(std::is_trivial<T>::value,
                "T is required to be a trivial type");
  T *First = nullptr;
  T *Last = nullptr;
  T *Cap = nullptr;
  T Inline[N] = {};

  bool isInline() const { return First == Inline; }

  void clearInline() {
    First = Inline;
    Last = Inline;
    Cap = Inline + N;
  }

  void reserve(size_t NewCap) {
    size_t S = size();
    if (isInline()) {
      auto *Tmp = static_cast<T *>(std::malloc(NewCap * sizeof(T)));
      if (Tmp == nullptr)
        std::abort();
      std::copy(First, Last, Tmp);
      First = Tmp;
    } else {
      First = static_cast<T *>(std::realloc(First, NewCap * sizeof(T)));
      if (First == nullptr)
        std::abort();
    }
    Last = First + S;
    Cap = First + NewCap;
  }

public:
  PODSmallVector() : First(Inline), Last(First), Cap(Inline + N) {}

  PODSmallVector(const PODSmallVector &) = delete;
  PODSmallVector &operator=(const PODSmallVector &) = delete;

  PODSmallVector(PODSmallVector &&Other) : PODSmallVector() {
    if (Other.isInline()) {
      std::copy(Other.begin(), Other.end(), First);
      Last = First + Other.size();
      Other.clear();
      return;
    }

    First = Other.First;
    Last = Other.Last;
    Cap = Other.Cap;
    Other.clearInline();
  }

  PODSmallVector &operator=(PODSmallVector &&Other) {
    if (Other.isInline()) {
      if (!isInline()) {
        std::free(First);
        clearInline();
      }
      std::copy(Other.begin(), Other.end(), First);
      Last = First + Other.size();
      Other.clear();
      return *this;
    }

    if (isInline()) {
      First = Other.First;
      Last = Other.Last;
      Cap = Other.Cap;
      Other.clearInline();
      return *this;
    }

    std::swap(First, Other.First);
    std::swap(Last, Other.Last);
    std::swap(Cap, Other.Cap);
    Other.clear();
    return *this;
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  void push_back(const T &Elem) {
    if (Last == Cap)
      reserve(size() * 2);
    *Last++ = Elem;
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  void pop_back() {
    DEMANGLE_ASSERT(Last != First, "Popping empty vector!");
    --Last;
  }

  void shrinkToSize(size_t Index) {
    DEMANGLE_ASSERT(Index <= size(), "shrinkToSize() can't expand!");
    Last = First + Index;
  }

  T *begin() { return First; }
  T *end() { return Last; }

  bool empty() const { return First == Last; }
  size_t size() const { return static_cast<size_t>(Last - First); }
  T &back() {
    DEMANGLE_ASSERT(Last != First, "Calling back() on empty vector!");
    return *(Last - 1);
  }
  T &operator[](size_t Index) {
    DEMANGLE_ASSERT(Index < size(), "Invalid access!");
    return *(begin() + Index);
  }
  void clear() { Last = First; }

  ~PODSmallVector() {
    if (!isInline())
      std::free(First);
  }
};

// Base class of all AST nodes. The AST is built by the parser, then is
// traversed by the printLeft/Right functions to produce a demangled string.
class Node {
public:
  enum Kind : unsigned char {
#define NODE(NodeKind) K##NodeKind,
#include "ItaniumNodes.def"
  };

  /// Three-way bool to track a cached value. Unknown is possible if this node
  /// has an unexpanded parameter pack below it that may affect this cache.
  enum class Cache : unsigned char { Yes, No, Unknown, };

  /// Operator precedence for expression nodes. Used to determine required
  /// parens in expression emission.
  enum class Prec {
    Primary,
    Postfix,
    Unary,
    Cast,
    PtrMem,
    Multiplicative,
    Additive,
    Shift,
    Spaceship,
    Relational,
    Equality,
    And,
    Xor,
    Ior,
    AndIf,
    OrIf,
    Conditional,
    Assign,
    Comma,
    Default,
  };

private:
  Kind K;

  Prec Precedence : 6;

  // FIXME: Make these protected.
public:
  /// Tracks if this node has a component on its right side, in which case we
  /// need to call printRight.
  Cache RHSComponentCache : 2;

  /// Track if this node is a (possibly qualified) array type. This can affect
  /// how we format the output string.
  Cache ArrayCache : 2;

  /// Track if this node is a (possibly qualified) function type. This can
  /// affect how we format the output string.
  Cache FunctionCache : 2;

public:
  Node(Kind K_, Prec Precedence_ = Prec::Primary,
       Cache RHSComponentCache_ = Cache::No, Cache ArrayCache_ = Cache::No,
       Cache FunctionCache_ = Cache::No)
      : K(K_), Precedence(Precedence_), RHSComponentCache(RHSComponentCache_),
        ArrayCache(ArrayCache_), FunctionCache(FunctionCache_) {}
  Node(Kind K_, Cache RHSComponentCache_, Cache ArrayCache_ = Cache::No,
       Cache FunctionCache_ = Cache::No)
      : Node(K_, Prec::Primary, RHSComponentCache_, ArrayCache_,
             FunctionCache_) {}

  /// Visit the most-derived object corresponding to this object.
  template<typename Fn> void visit(Fn F) const;

  // The following function is provided by all derived classes:
  //
  // Call F with arguments that, when passed to the constructor of this node,
  // would construct an equivalent node.
  //template<typename Fn> void match(Fn F) const;

  bool hasRHSComponent(OutputBuffer &OB) const {
    if (RHSComponentCache != Cache::Unknown)
      return RHSComponentCache == Cache::Yes;
    return hasRHSComponentSlow(OB);
  }

  bool hasArray(OutputBuffer &OB) const {
    if (ArrayCache != Cache::Unknown)
      return ArrayCache == Cache::Yes;
    return hasArraySlow(OB);
  }

  bool hasFunction(OutputBuffer &OB) const {
    if (FunctionCache != Cache::Unknown)
      return FunctionCache == Cache::Yes;
    return hasFunctionSlow(OB);
  }

  Kind getKind() const { return K; }

  Prec getPrecedence() const { return Precedence; }

  virtual bool hasRHSComponentSlow(OutputBuffer &) const { return false; }
  virtual bool hasArraySlow(OutputBuffer &) const { return false; }
  virtual bool hasFunctionSlow(OutputBuffer &) const { return false; }

  // Dig through "glue" nodes like ParameterPack and ForwardTemplateReference to
  // get at a node that actually represents some concrete syntax.
  virtual const Node *getSyntaxNode(OutputBuffer &) const { return this; }

  // Print this node as an expression operand, surrounding it in parentheses if
  // its precedence is [Strictly] weaker than P.
  void printAsOperand(OutputBuffer &OB, Prec P = Prec::Default,
                      bool StrictlyWorse = false) const {
    bool Paren =
        unsigned(getPrecedence()) >= unsigned(P) + unsigned(StrictlyWorse);
    if (Paren)
      OB.printOpen();
    print(OB);
    if (Paren)
      OB.printClose();
  }

  void print(OutputBuffer &OB) const {
    printLeft(OB);
    if (RHSComponentCache != Cache::No)
      printRight(OB);
  }

  // Print the "left" side of this Node into OutputBuffer.
  virtual void printLeft(OutputBuffer &) const = 0;

  // Print the "right". This distinction is necessary to represent C++ types
  // that appear on the RHS of their subtype, such as arrays or functions.
  // Since most types don't have such a component, provide a default
  // implementation.
  virtual void printRight(OutputBuffer &) const {}

  virtual std::string_view getBaseName() const { return {}; }

  // Silence compiler warnings, this dtor will never be called.
  virtual ~Node() = default;

#ifndef NDEBUG
  DEMANGLE_DUMP_METHOD void dump() const;
#endif
};

class NodeArray {
  Node **Elements;
  size_t NumElements;

public:
  NodeArray() : Elements(nullptr), NumElements(0) {}
  NodeArray(Node **Elements_, size_t NumElements_)
      : Elements(Elements_), NumElements(NumElements_) {}

  bool empty() const { return NumElements == 0; }
  size_t size() const { return NumElements; }

  Node **begin() const { return Elements; }
  Node **end() const { return Elements + NumElements; }

  Node *operator[](size_t Idx) const { return Elements[Idx]; }

  void printWithComma(OutputBuffer &OB) const {
    bool FirstElement = true;
    for (size_t Idx = 0; Idx != NumElements; ++Idx) {
      size_t BeforeComma = OB.getCurrentPosition();
      if (!FirstElement)
        OB += ", ";
      size_t AfterComma = OB.getCurrentPosition();
      Elements[Idx]->printAsOperand(OB, Node::Prec::Comma);

      // Elements[Idx] is an empty parameter pack expansion, we should erase the
      // comma we just printed.
      if (AfterComma == OB.getCurrentPosition()) {
        OB.setCurrentPosition(BeforeComma);
        continue;
      }

      FirstElement = false;
    }
  }
};

struct NodeArrayNode : Node {
  NodeArray Array;
  NodeArrayNode(NodeArray Array_) : Node(KNodeArrayNode), Array(Array_) {}

  template<typename Fn> void match(Fn F) const { F(Array); }

  void printLeft(OutputBuffer &OB) const override { Array.printWithComma(OB); }
};

class DotSuffix final : public Node {
  const Node *Prefix;
  const std::string_view Suffix;

public:
  DotSuffix(const Node *Prefix_, std::string_view Suffix_)
      : Node(KDotSuffix), Prefix(Prefix_), Suffix(Suffix_) {}

  template<typename Fn> void match(Fn F) const { F(Prefix, Suffix); }

  void printLeft(OutputBuffer &OB) const override {
    Prefix->print(OB);
    OB += " (";
    OB += Suffix;
    OB += ")";
  }
};

class VendorExtQualType final : public Node {
  const Node *Ty;
  std::string_view Ext;
  const Node *TA;

public:
  VendorExtQualType(const Node *Ty_, std::string_view Ext_, const Node *TA_)
      : Node(KVendorExtQualType), Ty(Ty_), Ext(Ext_), TA(TA_) {}

  const Node *getTy() const { return Ty; }
  std::string_view getExt() const { return Ext; }
  const Node *getTA() const { return TA; }

  template <typename Fn> void match(Fn F) const { F(Ty, Ext, TA); }

  void printLeft(OutputBuffer &OB) const override {
    Ty->print(OB);
    OB += " ";
    OB += Ext;
    if (TA != nullptr)
      TA->print(OB);
  }
};

enum FunctionRefQual : unsigned char {
  FrefQualNone,
  FrefQualLValue,
  FrefQualRValue,
};

enum Qualifiers {
  QualNone = 0,
  QualConst = 0x1,
  QualVolatile = 0x2,
  QualRestrict = 0x4,
};

inline Qualifiers operator|=(Qualifiers &Q1, Qualifiers Q2) {
  return Q1 = static_cast<Qualifiers>(Q1 | Q2);
}

class QualType final : public Node {
protected:
  const Qualifiers Quals;
  const Node *Child;

  void printQuals(OutputBuffer &OB) const {
    if (Quals & QualConst)
      OB += " const";
    if (Quals & QualVolatile)
      OB += " volatile";
    if (Quals & QualRestrict)
      OB += " restrict";
  }

public:
  QualType(const Node *Child_, Qualifiers Quals_)
      : Node(KQualType, Child_->RHSComponentCache,
             Child_->ArrayCache, Child_->FunctionCache),
        Quals(Quals_), Child(Child_) {}

  Qualifiers getQuals() const { return Quals; }
  const Node *getChild() const { return Child; }

  template<typename Fn> void match(Fn F) const { F(Child, Quals); }

  bool hasRHSComponentSlow(OutputBuffer &OB) const override {
    return Child->hasRHSComponent(OB);
  }
  bool hasArraySlow(OutputBuffer &OB) const override {
    return Child->hasArray(OB);
  }
  bool hasFunctionSlow(OutputBuffer &OB) const override {
    return Child->hasFunction(OB);
  }

  void printLeft(OutputBuffer &OB) const override {
    Child->printLeft(OB);
    printQuals(OB);
  }

  void printRight(OutputBuffer &OB) const override { Child->printRight(OB); }
};

class ConversionOperatorType final : public Node {
  const Node *Ty;

public:
  ConversionOperatorType(const Node *Ty_)
      : Node(KConversionOperatorType), Ty(Ty_) {}

  template<typename Fn> void match(Fn F) const { F(Ty); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "operator ";
    Ty->print(OB);
  }
};

class PostfixQualifiedType final : public Node {
  const Node *Ty;
  const std::string_view Postfix;

public:
  PostfixQualifiedType(const Node *Ty_, std::string_view Postfix_)
      : Node(KPostfixQualifiedType), Ty(Ty_), Postfix(Postfix_) {}

  template<typename Fn> void match(Fn F) const { F(Ty, Postfix); }

  void printLeft(OutputBuffer &OB) const override {
    Ty->printLeft(OB);
    OB += Postfix;
  }
};

class NameType final : public Node {
  const std::string_view Name;

public:
  NameType(std::string_view Name_) : Node(KNameType), Name(Name_) {}

  template<typename Fn> void match(Fn F) const { F(Name); }

  std::string_view getName() const { return Name; }
  std::string_view getBaseName() const override { return Name; }

  void printLeft(OutputBuffer &OB) const override { OB += Name; }
};

class BitIntType final : public Node {
  const Node *Size;
  bool Signed;

public:
  BitIntType(const Node *Size_, bool Signed_)
      : Node(KBitIntType), Size(Size_), Signed(Signed_) {}

  template <typename Fn> void match(Fn F) const { F(Size, Signed); }

  void printLeft(OutputBuffer &OB) const override {
    if (!Signed)
      OB += "unsigned ";
    OB += "_BitInt";
    OB.printOpen();
    Size->printAsOperand(OB);
    OB.printClose();
  }
};

class ElaboratedTypeSpefType : public Node {
  std::string_view Kind;
  Node *Child;
public:
  ElaboratedTypeSpefType(std::string_view Kind_, Node *Child_)
      : Node(KElaboratedTypeSpefType), Kind(Kind_), Child(Child_) {}

  template<typename Fn> void match(Fn F) const { F(Kind, Child); }

  void printLeft(OutputBuffer &OB) const override {
    OB += Kind;
    OB += ' ';
    Child->print(OB);
  }
};

class TransformedType : public Node {
  std::string_view Transform;
  Node *BaseType;
public:
  TransformedType(std::string_view Transform_, Node *BaseType_)
      : Node(KTransformedType), Transform(Transform_), BaseType(BaseType_) {}

  template<typename Fn> void match(Fn F) const { F(Transform, BaseType); }

  void printLeft(OutputBuffer &OB) const override {
    OB += Transform;
    OB += '(';
    BaseType->print(OB);
    OB += ')';
  }
};

struct AbiTagAttr : Node {
  Node *Base;
  std::string_view Tag;

  AbiTagAttr(Node *Base_, std::string_view Tag_)
      : Node(KAbiTagAttr, Base_->RHSComponentCache, Base_->ArrayCache,
             Base_->FunctionCache),
        Base(Base_), Tag(Tag_) {}

  template<typename Fn> void match(Fn F) const { F(Base, Tag); }

  std::string_view getBaseName() const override { return Base->getBaseName(); }

  void printLeft(OutputBuffer &OB) const override {
    Base->printLeft(OB);
    OB += "[abi:";
    OB += Tag;
    OB += "]";
  }
};

class EnableIfAttr : public Node {
  NodeArray Conditions;
public:
  EnableIfAttr(NodeArray Conditions_)
      : Node(KEnableIfAttr), Conditions(Conditions_) {}

  template<typename Fn> void match(Fn F) const { F(Conditions); }

  void printLeft(OutputBuffer &OB) const override {
    OB += " [enable_if:";
    Conditions.printWithComma(OB);
    OB += ']';
  }
};

class ObjCProtoName : public Node {
  const Node *Ty;
  std::string_view Protocol;

  friend class PointerType;

public:
  ObjCProtoName(const Node *Ty_, std::string_view Protocol_)
      : Node(KObjCProtoName), Ty(Ty_), Protocol(Protocol_) {}

  template<typename Fn> void match(Fn F) const { F(Ty, Protocol); }

  bool isObjCObject() const {
    return Ty->getKind() == KNameType &&
           static_cast<const NameType *>(Ty)->getName() == "objc_object";
  }

  void printLeft(OutputBuffer &OB) const override {
    Ty->print(OB);
    OB += "<";
    OB += Protocol;
    OB += ">";
  }
};

class PointerType final : public Node {
  const Node *Pointee;

public:
  PointerType(const Node *Pointee_)
      : Node(KPointerType, Pointee_->RHSComponentCache),
        Pointee(Pointee_) {}

  const Node *getPointee() const { return Pointee; }

  template<typename Fn> void match(Fn F) const { F(Pointee); }

  bool hasRHSComponentSlow(OutputBuffer &OB) const override {
    return Pointee->hasRHSComponent(OB);
  }

  void printLeft(OutputBuffer &OB) const override {
    // We rewrite objc_object<SomeProtocol>* into id<SomeProtocol>.
    if (Pointee->getKind() != KObjCProtoName ||
        !static_cast<const ObjCProtoName *>(Pointee)->isObjCObject()) {
      Pointee->printLeft(OB);
      if (Pointee->hasArray(OB))
        OB += " ";
      if (Pointee->hasArray(OB) || Pointee->hasFunction(OB))
        OB += "(";
      OB += "*";
    } else {
      const auto *objcProto = static_cast<const ObjCProtoName *>(Pointee);
      OB += "id<";
      OB += objcProto->Protocol;
      OB += ">";
    }
  }

  void printRight(OutputBuffer &OB) const override {
    if (Pointee->getKind() != KObjCProtoName ||
        !static_cast<const ObjCProtoName *>(Pointee)->isObjCObject()) {
      if (Pointee->hasArray(OB) || Pointee->hasFunction(OB))
        OB += ")";
      Pointee->printRight(OB);
    }
  }
};

enum class ReferenceKind {
  LValue,
  RValue,
};

// Represents either a LValue or an RValue reference type.
class ReferenceType : public Node {
  const Node *Pointee;
  ReferenceKind RK;

  mutable bool Printing = false;

  // Dig through any refs to refs, collapsing the ReferenceTypes as we go. The
  // rule here is rvalue ref to rvalue ref collapses to a rvalue ref, and any
  // other combination collapses to a lvalue ref.
  //
  // A combination of a TemplateForwardReference and a back-ref Substitution
  // from an ill-formed string may have created a cycle; use cycle detection to
  // avoid looping forever.
  std::pair<ReferenceKind, const Node *> collapse(OutputBuffer &OB) const {
    auto SoFar = std::make_pair(RK, Pointee);
    // Track the chain of nodes for the Floyd's 'tortoise and hare'
    // cycle-detection algorithm, since getSyntaxNode(S) is impure
    PODSmallVector<const Node *, 8> Prev;
    for (;;) {
      const Node *SN = SoFar.second->getSyntaxNode(OB);
      if (SN->getKind() != KReferenceType)
        break;
      auto *RT = static_cast<const ReferenceType *>(SN);
      SoFar.second = RT->Pointee;
      SoFar.first = std::min(SoFar.first, RT->RK);

      // The middle of Prev is the 'slow' pointer moving at half speed
      Prev.push_back(SoFar.second);
      if (Prev.size() > 1 && SoFar.second == Prev[(Prev.size() - 1) / 2]) {
        // Cycle detected
        SoFar.second = nullptr;
        break;
      }
    }
    return SoFar;
  }

public:
  ReferenceType(const Node *Pointee_, ReferenceKind RK_)
      : Node(KReferenceType, Pointee_->RHSComponentCache),
        Pointee(Pointee_), RK(RK_) {}

  template<typename Fn> void match(Fn F) const { F(Pointee, RK); }

  bool hasRHSComponentSlow(OutputBuffer &OB) const override {
    return Pointee->hasRHSComponent(OB);
  }

  void printLeft(OutputBuffer &OB) const override {
    if (Printing)
      return;
    ScopedOverride<bool> SavePrinting(Printing, true);
    std::pair<ReferenceKind, const Node *> Collapsed = collapse(OB);
    if (!Collapsed.second)
      return;
    Collapsed.second->printLeft(OB);
    if (Collapsed.second->hasArray(OB))
      OB += " ";
    if (Collapsed.second->hasArray(OB) || Collapsed.second->hasFunction(OB))
      OB += "(";

    OB += (Collapsed.first == ReferenceKind::LValue ? "&" : "&&");
  }
  void printRight(OutputBuffer &OB) const override {
    if (Printing)
      return;
    ScopedOverride<bool> SavePrinting(Printing, true);
    std::pair<ReferenceKind, const Node *> Collapsed = collapse(OB);
    if (!Collapsed.second)
      return;
    if (Collapsed.second->hasArray(OB) || Collapsed.second->hasFunction(OB))
      OB += ")";
    Collapsed.second->printRight(OB);
  }
};

class PointerToMemberType final : public Node {
  const Node *ClassType;
  const Node *MemberType;

public:
  PointerToMemberType(const Node *ClassType_, const Node *MemberType_)
      : Node(KPointerToMemberType, MemberType_->RHSComponentCache),
        ClassType(ClassType_), MemberType(MemberType_) {}

  template<typename Fn> void match(Fn F) const { F(ClassType, MemberType); }

  bool hasRHSComponentSlow(OutputBuffer &OB) const override {
    return MemberType->hasRHSComponent(OB);
  }

  void printLeft(OutputBuffer &OB) const override {
    MemberType->printLeft(OB);
    if (MemberType->hasArray(OB) || MemberType->hasFunction(OB))
      OB += "(";
    else
      OB += " ";
    ClassType->print(OB);
    OB += "::*";
  }

  void printRight(OutputBuffer &OB) const override {
    if (MemberType->hasArray(OB) || MemberType->hasFunction(OB))
      OB += ")";
    MemberType->printRight(OB);
  }
};

class ArrayType final : public Node {
  const Node *Base;
  Node *Dimension;

public:
  ArrayType(const Node *Base_, Node *Dimension_)
      : Node(KArrayType,
             /*RHSComponentCache=*/Cache::Yes,
             /*ArrayCache=*/Cache::Yes),
        Base(Base_), Dimension(Dimension_) {}

  template<typename Fn> void match(Fn F) const { F(Base, Dimension); }

  bool hasRHSComponentSlow(OutputBuffer &) const override { return true; }
  bool hasArraySlow(OutputBuffer &) const override { return true; }

  void printLeft(OutputBuffer &OB) const override { Base->printLeft(OB); }

  void printRight(OutputBuffer &OB) const override {
    if (OB.back() != ']')
      OB += " ";
    OB += "[";
    if (Dimension)
      Dimension->print(OB);
    OB += "]";
    Base->printRight(OB);
  }
};

class FunctionType final : public Node {
  const Node *Ret;
  NodeArray Params;
  Qualifiers CVQuals;
  FunctionRefQual RefQual;
  const Node *ExceptionSpec;

public:
  FunctionType(const Node *Ret_, NodeArray Params_, Qualifiers CVQuals_,
               FunctionRefQual RefQual_, const Node *ExceptionSpec_)
      : Node(KFunctionType,
             /*RHSComponentCache=*/Cache::Yes, /*ArrayCache=*/Cache::No,
             /*FunctionCache=*/Cache::Yes),
        Ret(Ret_), Params(Params_), CVQuals(CVQuals_), RefQual(RefQual_),
        ExceptionSpec(ExceptionSpec_) {}

  template<typename Fn> void match(Fn F) const {
    F(Ret, Params, CVQuals, RefQual, ExceptionSpec);
  }

  bool hasRHSComponentSlow(OutputBuffer &) const override { return true; }
  bool hasFunctionSlow(OutputBuffer &) const override { return true; }

  // Handle C++'s ... quirky decl grammar by using the left & right
  // distinction. Consider:
  //   int (*f(float))(char) {}
  // f is a function that takes a float and returns a pointer to a function
  // that takes a char and returns an int. If we're trying to print f, start
  // by printing out the return types's left, then print our parameters, then
  // finally print right of the return type.
  void printLeft(OutputBuffer &OB) const override {
    Ret->printLeft(OB);
    OB += " ";
  }

  void printRight(OutputBuffer &OB) const override {
    OB.printOpen();
    Params.printWithComma(OB);
    OB.printClose();
    Ret->printRight(OB);

    if (CVQuals & QualConst)
      OB += " const";
    if (CVQuals & QualVolatile)
      OB += " volatile";
    if (CVQuals & QualRestrict)
      OB += " restrict";

    if (RefQual == FrefQualLValue)
      OB += " &";
    else if (RefQual == FrefQualRValue)
      OB += " &&";

    if (ExceptionSpec != nullptr) {
      OB += ' ';
      ExceptionSpec->print(OB);
    }
  }
};

class NoexceptSpec : public Node {
  const Node *E;
public:
  NoexceptSpec(const Node *E_) : Node(KNoexceptSpec), E(E_) {}

  template<typename Fn> void match(Fn F) const { F(E); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "noexcept";
    OB.printOpen();
    E->printAsOperand(OB);
    OB.printClose();
  }
};

class DynamicExceptionSpec : public Node {
  NodeArray Types;
public:
  DynamicExceptionSpec(NodeArray Types_)
      : Node(KDynamicExceptionSpec), Types(Types_) {}

  template<typename Fn> void match(Fn F) const { F(Types); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "throw";
    OB.printOpen();
    Types.printWithComma(OB);
    OB.printClose();
  }
};

/// Represents the explicitly named object parameter.
/// E.g.,
/// \code{.cpp}
///   struct Foo {
///     void bar(this Foo && self);
///   };
/// \endcode
class ExplicitObjectParameter final : public Node {
  Node *Base;

public:
  ExplicitObjectParameter(Node *Base_)
      : Node(KExplicitObjectParameter), Base(Base_) {
    DEMANGLE_ASSERT(
        Base != nullptr,
        "Creating an ExplicitObjectParameter without a valid Base Node.");
  }

  template <typename Fn> void match(Fn F) const { F(Base); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "this ";
    Base->print(OB);
  }
};

class FunctionEncoding final : public Node {
  const Node *Ret;
  const Node *Name;
  NodeArray Params;
  const Node *Attrs;
  const Node *Requires;
  Qualifiers CVQuals;
  FunctionRefQual RefQual;

public:
  FunctionEncoding(const Node *Ret_, const Node *Name_, NodeArray Params_,
                   const Node *Attrs_, const Node *Requires_,
                   Qualifiers CVQuals_, FunctionRefQual RefQual_)
      : Node(KFunctionEncoding,
             /*RHSComponentCache=*/Cache::Yes, /*ArrayCache=*/Cache::No,
             /*FunctionCache=*/Cache::Yes),
        Ret(Ret_), Name(Name_), Params(Params_), Attrs(Attrs_),
        Requires(Requires_), CVQuals(CVQuals_), RefQual(RefQual_) {}

  template<typename Fn> void match(Fn F) const {
    F(Ret, Name, Params, Attrs, Requires, CVQuals, RefQual);
  }

  Qualifiers getCVQuals() const { return CVQuals; }
  FunctionRefQual getRefQual() const { return RefQual; }
  NodeArray getParams() const { return Params; }
  const Node *getReturnType() const { return Ret; }

  bool hasRHSComponentSlow(OutputBuffer &) const override { return true; }
  bool hasFunctionSlow(OutputBuffer &) const override { return true; }

  const Node *getName() const { return Name; }

  void printLeft(OutputBuffer &OB) const override {
    if (Ret) {
      Ret->printLeft(OB);
      if (!Ret->hasRHSComponent(OB))
        OB += " ";
    }
    Name->print(OB);
  }

  void printRight(OutputBuffer &OB) const override {
    OB.printOpen();
    Params.printWithComma(OB);
    OB.printClose();
    if (Ret)
      Ret->printRight(OB);

    if (CVQuals & QualConst)
      OB += " const";
    if (CVQuals & QualVolatile)
      OB += " volatile";
    if (CVQuals & QualRestrict)
      OB += " restrict";

    if (RefQual == FrefQualLValue)
      OB += " &";
    else if (RefQual == FrefQualRValue)
      OB += " &&";

    if (Attrs != nullptr)
      Attrs->print(OB);

    if (Requires != nullptr) {
      OB += " requires ";
      Requires->print(OB);
    }
  }
};

class LiteralOperator : public Node {
  const Node *OpName;

public:
  LiteralOperator(const Node *OpName_)
      : Node(KLiteralOperator), OpName(OpName_) {}

  template<typename Fn> void match(Fn F) const { F(OpName); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "operator\"\" ";
    OpName->print(OB);
  }
};

class SpecialName final : public Node {
  const std::string_view Special;
  const Node *Child;

public:
  SpecialName(std::string_view Special_, const Node *Child_)
      : Node(KSpecialName), Special(Special_), Child(Child_) {}

  template<typename Fn> void match(Fn F) const { F(Special, Child); }

  void printLeft(OutputBuffer &OB) const override {
    OB += Special;
    Child->print(OB);
  }
};

class CtorVtableSpecialName final : public Node {
  const Node *FirstType;
  const Node *SecondType;

public:
  CtorVtableSpecialName(const Node *FirstType_, const Node *SecondType_)
      : Node(KCtorVtableSpecialName),
        FirstType(FirstType_), SecondType(SecondType_) {}

  template<typename Fn> void match(Fn F) const { F(FirstType, SecondType); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "construction vtable for ";
    FirstType->print(OB);
    OB += "-in-";
    SecondType->print(OB);
  }
};

struct NestedName : Node {
  Node *Qual;
  Node *Name;

  NestedName(Node *Qual_, Node *Name_)
      : Node(KNestedName), Qual(Qual_), Name(Name_) {}

  template<typename Fn> void match(Fn F) const { F(Qual, Name); }

  std::string_view getBaseName() const override { return Name->getBaseName(); }

  void printLeft(OutputBuffer &OB) const override {
    Qual->print(OB);
    OB += "::";
    Name->print(OB);
  }
};

struct MemberLikeFriendName : Node {
  Node *Qual;
  Node *Name;

  MemberLikeFriendName(Node *Qual_, Node *Name_)
      : Node(KMemberLikeFriendName), Qual(Qual_), Name(Name_) {}

  template<typename Fn> void match(Fn F) const { F(Qual, Name); }

  std::string_view getBaseName() const override { return Name->getBaseName(); }

  void printLeft(OutputBuffer &OB) const override {
    Qual->print(OB);
    OB += "::friend ";
    Name->print(OB);
  }
};

struct ModuleName : Node {
  ModuleName *Parent;
  Node *Name;
  bool IsPartition;

  ModuleName(ModuleName *Parent_, Node *Name_, bool IsPartition_ = false)
      : Node(KModuleName), Parent(Parent_), Name(Name_),
        IsPartition(IsPartition_) {}

  template <typename Fn> void match(Fn F) const {
    F(Parent, Name, IsPartition);
  }

  void printLeft(OutputBuffer &OB) const override {
    if (Parent)
      Parent->print(OB);
    if (Parent || IsPartition)
      OB += IsPartition ? ':' : '.';
    Name->print(OB);
  }
};

struct ModuleEntity : Node {
  ModuleName *Module;
  Node *Name;

  ModuleEntity(ModuleName *Module_, Node *Name_)
      : Node(KModuleEntity), Module(Module_), Name(Name_) {}

  template <typename Fn> void match(Fn F) const { F(Module, Name); }

  std::string_view getBaseName() const override { return Name->getBaseName(); }

  void printLeft(OutputBuffer &OB) const override {
    Name->print(OB);
    OB += '@';
    Module->print(OB);
  }
};

struct LocalName : Node {
  Node *Encoding;
  Node *Entity;

  LocalName(Node *Encoding_, Node *Entity_)
      : Node(KLocalName), Encoding(Encoding_), Entity(Entity_) {}

  template<typename Fn> void match(Fn F) const { F(Encoding, Entity); }

  void printLeft(OutputBuffer &OB) const override {
    Encoding->print(OB);
    OB += "::";
    Entity->print(OB);
  }
};

class QualifiedName final : public Node {
  // qualifier::name
  const Node *Qualifier;
  const Node *Name;

public:
  QualifiedName(const Node *Qualifier_, const Node *Name_)
      : Node(KQualifiedName), Qualifier(Qualifier_), Name(Name_) {}

  template<typename Fn> void match(Fn F) const { F(Qualifier, Name); }

  std::string_view getBaseName() const override { return Name->getBaseName(); }

  void printLeft(OutputBuffer &OB) const override {
    Qualifier->print(OB);
    OB += "::";
    Name->print(OB);
  }
};

class VectorType final : public Node {
  const Node *BaseType;
  const Node *Dimension;

public:
  VectorType(const Node *BaseType_, const Node *Dimension_)
      : Node(KVectorType), BaseType(BaseType_), Dimension(Dimension_) {}

  const Node *getBaseType() const { return BaseType; }
  const Node *getDimension() const { return Dimension; }

  template<typename Fn> void match(Fn F) const { F(BaseType, Dimension); }

  void printLeft(OutputBuffer &OB) const override {
    BaseType->print(OB);
    OB += " vector[";
    if (Dimension)
      Dimension->print(OB);
    OB += "]";
  }
};

class PixelVectorType final : public Node {
  const Node *Dimension;

public:
  PixelVectorType(const Node *Dimension_)
      : Node(KPixelVectorType), Dimension(Dimension_) {}

  template<typename Fn> void match(Fn F) const { F(Dimension); }

  void printLeft(OutputBuffer &OB) const override {
    // FIXME: This should demangle as "vector pixel".
    OB += "pixel vector[";
    Dimension->print(OB);
    OB += "]";
  }
};

class BinaryFPType final : public Node {
  const Node *Dimension;

public:
  BinaryFPType(const Node *Dimension_)
      : Node(KBinaryFPType), Dimension(Dimension_) {}

  template<typename Fn> void match(Fn F) const { F(Dimension); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "_Float";
    Dimension->print(OB);
  }
};

enum class TemplateParamKind { Type, NonType, Template };

/// An invented name for a template parameter for which we don't have a
/// corresponding template argument.
///
/// This node is created when parsing the <lambda-sig> for a lambda with
/// explicit template arguments, which might be referenced in the parameter
/// types appearing later in the <lambda-sig>.
class SyntheticTemplateParamName final : public Node {
  TemplateParamKind Kind;
  unsigned Index;

public:
  SyntheticTemplateParamName(TemplateParamKind Kind_, unsigned Index_)
      : Node(KSyntheticTemplateParamName), Kind(Kind_), Index(Index_) {}

  template<typename Fn> void match(Fn F) const { F(Kind, Index); }

  void printLeft(OutputBuffer &OB) const override {
    switch (Kind) {
    case TemplateParamKind::Type:
      OB += "$T";
      break;
    case TemplateParamKind::NonType:
      OB += "$N";
      break;
    case TemplateParamKind::Template:
      OB += "$TT";
      break;
    }
    if (Index > 0)
      OB << Index - 1;
  }
};

class TemplateParamQualifiedArg final : public Node {
  Node *Param;
  Node *Arg;

public:
  TemplateParamQualifiedArg(Node *Param_, Node *Arg_)
      : Node(KTemplateParamQualifiedArg), Param(Param_), Arg(Arg_) {}

  template <typename Fn> void match(Fn F) const { F(Param, Arg); }

  Node *getArg() { return Arg; }

  void printLeft(OutputBuffer &OB) const override {
    // Don't print Param to keep the output consistent.
    Arg->print(OB);
  }
};

/// A template type parameter declaration, 'typename T'.
class TypeTemplateParamDecl final : public Node {
  Node *Name;

public:
  TypeTemplateParamDecl(Node *Name_)
      : Node(KTypeTemplateParamDecl, Cache::Yes), Name(Name_) {}

  template<typename Fn> void match(Fn F) const { F(Name); }

  void printLeft(OutputBuffer &OB) const override { OB += "typename "; }

  void printRight(OutputBuffer &OB) const override { Name->print(OB); }
};

/// A constrained template type parameter declaration, 'C<U> T'.
class ConstrainedTypeTemplateParamDecl final : public Node {
  Node *Constraint;
  Node *Name;

public:
  ConstrainedTypeTemplateParamDecl(Node *Constraint_, Node *Name_)
      : Node(KConstrainedTypeTemplateParamDecl, Cache::Yes),
        Constraint(Constraint_), Name(Name_) {}

  template<typename Fn> void match(Fn F) const { F(Constraint, Name); }

  void printLeft(OutputBuffer &OB) const override {
    Constraint->print(OB);
    OB += " ";
  }

  void printRight(OutputBuffer &OB) const override { Name->print(OB); }
};

/// A non-type template parameter declaration, 'int N'.
class NonTypeTemplateParamDecl final : public Node {
  Node *Name;
  Node *Type;

public:
  NonTypeTemplateParamDecl(Node *Name_, Node *Type_)
      : Node(KNonTypeTemplateParamDecl, Cache::Yes), Name(Name_), Type(Type_) {}

  template<typename Fn> void match(Fn F) const { F(Name, Type); }

  void printLeft(OutputBuffer &OB) const override {
    Type->printLeft(OB);
    if (!Type->hasRHSComponent(OB))
      OB += " ";
  }

  void printRight(OutputBuffer &OB) const override {
    Name->print(OB);
    Type->printRight(OB);
  }
};

/// A template template parameter declaration,
/// 'template<typename T> typename N'.
class TemplateTemplateParamDecl final : public Node {
  Node *Name;
  NodeArray Params;
  Node *Requires;

public:
  TemplateTemplateParamDecl(Node *Name_, NodeArray Params_, Node *Requires_)
      : Node(KTemplateTemplateParamDecl, Cache::Yes), Name(Name_),
        Params(Params_), Requires(Requires_) {}

  template <typename Fn> void match(Fn F) const { F(Name, Params, Requires); }

  void printLeft(OutputBuffer &OB) const override {
    ScopedOverride<unsigned> LT(OB.GtIsGt, 0);
    OB += "template<";
    Params.printWithComma(OB);
    OB += "> typename ";
  }

  void printRight(OutputBuffer &OB) const override {
    Name->print(OB);
    if (Requires != nullptr) {
      OB += " requires ";
      Requires->print(OB);
    }
  }
};

/// A template parameter pack declaration, 'typename ...T'.
class TemplateParamPackDecl final : public Node {
  Node *Param;

public:
  TemplateParamPackDecl(Node *Param_)
      : Node(KTemplateParamPackDecl, Cache::Yes), Param(Param_) {}

  template<typename Fn> void match(Fn F) const { F(Param); }

  void printLeft(OutputBuffer &OB) const override {
    Param->printLeft(OB);
    OB += "...";
  }

  void printRight(OutputBuffer &OB) const override { Param->printRight(OB); }
};

/// An unexpanded parameter pack (either in the expression or type context). If
/// this AST is correct, this node will have a ParameterPackExpansion node above
/// it.
///
/// This node is created when some <template-args> are found that apply to an
/// <encoding>, and is stored in the TemplateParams table. In order for this to
/// appear in the final AST, it has to referenced via a <template-param> (ie,
/// T_).
class ParameterPack final : public Node {
  NodeArray Data;

  // Setup OutputBuffer for a pack expansion, unless we're already expanding
  // one.
  void initializePackExpansion(OutputBuffer &OB) const {
    if (OB.CurrentPackMax == std::numeric_limits<unsigned>::max()) {
      OB.CurrentPackMax = static_cast<unsigned>(Data.size());
      OB.CurrentPackIndex = 0;
    }
  }

public:
  ParameterPack(NodeArray Data_) : Node(KParameterPack), Data(Data_) {
    ArrayCache = FunctionCache = RHSComponentCache = Cache::Unknown;
    if (std::all_of(Data.begin(), Data.end(), [](Node* P) {
          return P->ArrayCache == Cache::No;
        }))
      ArrayCache = Cache::No;
    if (std::all_of(Data.begin(), Data.end(), [](Node* P) {
          return P->FunctionCache == Cache::No;
        }))
      FunctionCache = Cache::No;
    if (std::all_of(Data.begin(), Data.end(), [](Node* P) {
          return P->RHSComponentCache == Cache::No;
        }))
      RHSComponentCache = Cache::No;
  }

  template<typename Fn> void match(Fn F) const { F(Data); }

  bool hasRHSComponentSlow(OutputBuffer &OB) const override {
    initializePackExpansion(OB);
    size_t Idx = OB.CurrentPackIndex;
    return Idx < Data.size() && Data[Idx]->hasRHSComponent(OB);
  }
  bool hasArraySlow(OutputBuffer &OB) const override {
    initializePackExpansion(OB);
    size_t Idx = OB.CurrentPackIndex;
    return Idx < Data.size() && Data[Idx]->hasArray(OB);
  }
  bool hasFunctionSlow(OutputBuffer &OB) const override {
    initializePackExpansion(OB);
    size_t Idx = OB.CurrentPackIndex;
    return Idx < Data.size() && Data[Idx]->hasFunction(OB);
  }
  const Node *getSyntaxNode(OutputBuffer &OB) const override {
    initializePackExpansion(OB);
    size_t Idx = OB.CurrentPackIndex;
    return Idx < Data.size() ? Data[Idx]->getSyntaxNode(OB) : this;
  }

  void printLeft(OutputBuffer &OB) const override {
    initializePackExpansion(OB);
    size_t Idx = OB.CurrentPackIndex;
    if (Idx < Data.size())
      Data[Idx]->printLeft(OB);
  }
  void printRight(OutputBuffer &OB) const override {
    initializePackExpansion(OB);
    size_t Idx = OB.CurrentPackIndex;
    if (Idx < Data.size())
      Data[Idx]->printRight(OB);
  }
};

/// A variadic template argument. This node represents an occurrence of
/// J<something>E in some <template-args>. It isn't itself unexpanded, unless
/// one of its Elements is. The parser inserts a ParameterPack into the
/// TemplateParams table if the <template-args> this pack belongs to apply to an
/// <encoding>.
class TemplateArgumentPack final : public Node {
  NodeArray Elements;
public:
  TemplateArgumentPack(NodeArray Elements_)
      : Node(KTemplateArgumentPack), Elements(Elements_) {}

  template<typename Fn> void match(Fn F) const { F(Elements); }

  NodeArray getElements() const { return Elements; }

  void printLeft(OutputBuffer &OB) const override {
    Elements.printWithComma(OB);
  }
};

/// A pack expansion. Below this node, there are some unexpanded ParameterPacks
/// which each have Child->ParameterPackSize elements.
class ParameterPackExpansion final : public Node {
  const Node *Child;

public:
  ParameterPackExpansion(const Node *Child_)
      : Node(KParameterPackExpansion), Child(Child_) {}

  template<typename Fn> void match(Fn F) const { F(Child); }

  const Node *getChild() const { return Child; }

  void printLeft(OutputBuffer &OB) const override {
    constexpr unsigned Max = std::numeric_limits<unsigned>::max();
    ScopedOverride<unsigned> SavePackIdx(OB.CurrentPackIndex, Max);
    ScopedOverride<unsigned> SavePackMax(OB.CurrentPackMax, Max);
    size_t StreamPos = OB.getCurrentPosition();

    // Print the first element in the pack. If Child contains a ParameterPack,
    // it will set up S.CurrentPackMax and print the first element.
    Child->print(OB);

    // No ParameterPack was found in Child. This can occur if we've found a pack
    // expansion on a <function-param>.
    if (OB.CurrentPackMax == Max) {
      OB += "...";
      return;
    }

    // We found a ParameterPack, but it has no elements. Erase whatever we may
    // of printed.
    if (OB.CurrentPackMax == 0) {
      OB.setCurrentPosition(StreamPos);
      return;
    }

    // Else, iterate through the rest of the elements in the pack.
    for (unsigned I = 1, E = OB.CurrentPackMax; I < E; ++I) {
      OB += ", ";
      OB.CurrentPackIndex = I;
      Child->print(OB);
    }
  }
};

class TemplateArgs final : public Node {
  NodeArray Params;
  Node *Requires;

public:
  TemplateArgs(NodeArray Params_, Node *Requires_)
      : Node(KTemplateArgs), Params(Params_), Requires(Requires_) {}

  template<typename Fn> void match(Fn F) const { F(Params, Requires); }

  NodeArray getParams() { return Params; }

  void printLeft(OutputBuffer &OB) const override {
    ScopedOverride<unsigned> LT(OB.GtIsGt, 0);
    OB += "<";
    Params.printWithComma(OB);
    OB += ">";
    // Don't print the requires clause to keep the output simple.
  }
};

/// A forward-reference to a template argument that was not known at the point
/// where the template parameter name was parsed in a mangling.
///
/// This is created when demangling the name of a specialization of a
/// conversion function template:
///
/// \code
/// struct A {
///   template<typename T> operator T*();
/// };
/// \endcode
///
/// When demangling a specialization of the conversion function template, we
/// encounter the name of the template (including the \c T) before we reach
/// the template argument list, so we cannot substitute the parameter name
/// for the corresponding argument while parsing. Instead, we create a
/// \c ForwardTemplateReference node that is resolved after we parse the
/// template arguments.
struct ForwardTemplateReference : Node {
  size_t Index;
  Node *Ref = nullptr;

  // If we're currently printing this node. It is possible (though invalid) for
  // a forward template reference to refer to itself via a substitution. This
  // creates a cyclic AST, which will stack overflow printing. To fix this, bail
  // out if more than one print* function is active.
  mutable bool Printing = false;

  ForwardTemplateReference(size_t Index_)
      : Node(KForwardTemplateReference, Cache::Unknown, Cache::Unknown,
             Cache::Unknown),
        Index(Index_) {}

  // We don't provide a matcher for these, because the value of the node is
  // not determined by its construction parameters, and it generally needs
  // special handling.
  template<typename Fn> void match(Fn F) const = delete;

  bool hasRHSComponentSlow(OutputBuffer &OB) const override {
    if (Printing)
      return false;
    ScopedOverride<bool> SavePrinting(Printing, true);
    return Ref->hasRHSComponent(OB);
  }
  bool hasArraySlow(OutputBuffer &OB) const override {
    if (Printing)
      return false;
    ScopedOverride<bool> SavePrinting(Printing, true);
    return Ref->hasArray(OB);
  }
  bool hasFunctionSlow(OutputBuffer &OB) const override {
    if (Printing)
      return false;
    ScopedOverride<bool> SavePrinting(Printing, true);
    return Ref->hasFunction(OB);
  }
  const Node *getSyntaxNode(OutputBuffer &OB) const override {
    if (Printing)
      return this;
    ScopedOverride<bool> SavePrinting(Printing, true);
    return Ref->getSyntaxNode(OB);
  }

  void printLeft(OutputBuffer &OB) const override {
    if (Printing)
      return;
    ScopedOverride<bool> SavePrinting(Printing, true);
    Ref->printLeft(OB);
  }
  void printRight(OutputBuffer &OB) const override {
    if (Printing)
      return;
    ScopedOverride<bool> SavePrinting(Printing, true);
    Ref->printRight(OB);
  }
};

struct NameWithTemplateArgs : Node {
  // name<template_args>
  Node *Name;
  Node *TemplateArgs;

  NameWithTemplateArgs(Node *Name_, Node *TemplateArgs_)
      : Node(KNameWithTemplateArgs), Name(Name_), TemplateArgs(TemplateArgs_) {}

  template<typename Fn> void match(Fn F) const { F(Name, TemplateArgs); }

  std::string_view getBaseName() const override { return Name->getBaseName(); }

  void printLeft(OutputBuffer &OB) const override {
    Name->print(OB);
    TemplateArgs->print(OB);
  }
};

class GlobalQualifiedName final : public Node {
  Node *Child;

public:
  GlobalQualifiedName(Node* Child_)
      : Node(KGlobalQualifiedName), Child(Child_) {}

  template<typename Fn> void match(Fn F) const { F(Child); }

  std::string_view getBaseName() const override { return Child->getBaseName(); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "::";
    Child->print(OB);
  }
};

enum class SpecialSubKind {
  allocator,
  basic_string,
  string,
  istream,
  ostream,
  iostream,
};

class SpecialSubstitution;
class ExpandedSpecialSubstitution : public Node {
protected:
  SpecialSubKind SSK;

  ExpandedSpecialSubstitution(SpecialSubKind SSK_, Kind K_)
      : Node(K_), SSK(SSK_) {}
public:
  ExpandedSpecialSubstitution(SpecialSubKind SSK_)
      : ExpandedSpecialSubstitution(SSK_, KExpandedSpecialSubstitution) {}
  inline ExpandedSpecialSubstitution(SpecialSubstitution const *);

  template<typename Fn> void match(Fn F) const { F(SSK); }

protected:
  bool isInstantiation() const {
    return unsigned(SSK) >= unsigned(SpecialSubKind::string);
  }

  std::string_view getBaseName() const override {
    switch (SSK) {
    case SpecialSubKind::allocator:
      return {"allocator"};
    case SpecialSubKind::basic_string:
      return {"basic_string"};
    case SpecialSubKind::string:
      return {"basic_string"};
    case SpecialSubKind::istream:
      return {"basic_istream"};
    case SpecialSubKind::ostream:
      return {"basic_ostream"};
    case SpecialSubKind::iostream:
      return {"basic_iostream"};
    }
    DEMANGLE_UNREACHABLE;
  }

private:
  void printLeft(OutputBuffer &OB) const override {
    OB << "std::" << getBaseName();
    if (isInstantiation()) {
      OB << "<char, std::char_traits<char>";
      if (SSK == SpecialSubKind::string)
        OB << ", std::allocator<char>";
      OB << ">";
    }
  }
};

class SpecialSubstitution final : public ExpandedSpecialSubstitution {
public:
  SpecialSubstitution(SpecialSubKind SSK_)
      : ExpandedSpecialSubstitution(SSK_, KSpecialSubstitution) {}

  template<typename Fn> void match(Fn F) const { F(SSK); }

  std::string_view getBaseName() const override {
    std::string_view SV = ExpandedSpecialSubstitution::getBaseName();
    if (isInstantiation()) {
      // The instantiations are typedefs that drop the "basic_" prefix.
      DEMANGLE_ASSERT(starts_with(SV, "basic_"), "");
      SV.remove_prefix(sizeof("basic_") - 1);
    }
    return SV;
  }

  void printLeft(OutputBuffer &OB) const override {
    OB << "std::" << getBaseName();
  }
};

inline ExpandedSpecialSubstitution::ExpandedSpecialSubstitution(
    SpecialSubstitution const *SS)
    : ExpandedSpecialSubstitution(SS->SSK) {}

class CtorDtorName final : public Node {
  const Node *Basename;
  const bool IsDtor;
  const int Variant;

public:
  CtorDtorName(const Node *Basename_, bool IsDtor_, int Variant_)
      : Node(KCtorDtorName), Basename(Basename_), IsDtor(IsDtor_),
        Variant(Variant_) {}

  template<typename Fn> void match(Fn F) const { F(Basename, IsDtor, Variant); }

  void printLeft(OutputBuffer &OB) const override {
    if (IsDtor)
      OB += "~";
    OB += Basename->getBaseName();
  }
};

class DtorName : public Node {
  const Node *Base;

public:
  DtorName(const Node *Base_) : Node(KDtorName), Base(Base_) {}

  template<typename Fn> void match(Fn F) const { F(Base); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "~";
    Base->printLeft(OB);
  }
};

class UnnamedTypeName : public Node {
  const std::string_view Count;

public:
  UnnamedTypeName(std::string_view Count_)
      : Node(KUnnamedTypeName), Count(Count_) {}

  template<typename Fn> void match(Fn F) const { F(Count); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "'unnamed";
    OB += Count;
    OB += "\'";
  }
};

class ClosureTypeName : public Node {
  NodeArray TemplateParams;
  const Node *Requires1;
  NodeArray Params;
  const Node *Requires2;
  std::string_view Count;

public:
  ClosureTypeName(NodeArray TemplateParams_, const Node *Requires1_,
                  NodeArray Params_, const Node *Requires2_,
                  std::string_view Count_)
      : Node(KClosureTypeName), TemplateParams(TemplateParams_),
        Requires1(Requires1_), Params(Params_), Requires2(Requires2_),
        Count(Count_) {}

  template<typename Fn> void match(Fn F) const {
    F(TemplateParams, Requires1, Params, Requires2, Count);
  }

  void printDeclarator(OutputBuffer &OB) const {
    if (!TemplateParams.empty()) {
      ScopedOverride<unsigned> LT(OB.GtIsGt, 0);
      OB += "<";
      TemplateParams.printWithComma(OB);
      OB += ">";
    }
    if (Requires1 != nullptr) {
      OB += " requires ";
      Requires1->print(OB);
      OB += " ";
    }
    OB.printOpen();
    Params.printWithComma(OB);
    OB.printClose();
    if (Requires2 != nullptr) {
      OB += " requires ";
      Requires2->print(OB);
    }
  }

  void printLeft(OutputBuffer &OB) const override {
    // FIXME: This demangling is not particularly readable.
    OB += "\'lambda";
    OB += Count;
    OB += "\'";
    printDeclarator(OB);
  }
};

class StructuredBindingName : public Node {
  NodeArray Bindings;
public:
  StructuredBindingName(NodeArray Bindings_)
      : Node(KStructuredBindingName), Bindings(Bindings_) {}

  template<typename Fn> void match(Fn F) const { F(Bindings); }

  void printLeft(OutputBuffer &OB) const override {
    OB.printOpen('[');
    Bindings.printWithComma(OB);
    OB.printClose(']');
  }
};

// -- Expression Nodes --

class BinaryExpr : public Node {
  const Node *LHS;
  const std::string_view InfixOperator;
  const Node *RHS;

public:
  BinaryExpr(const Node *LHS_, std::string_view InfixOperator_,
             const Node *RHS_, Prec Prec_)
      : Node(KBinaryExpr, Prec_), LHS(LHS_), InfixOperator(InfixOperator_),
        RHS(RHS_) {}

  template <typename Fn> void match(Fn F) const {
    F(LHS, InfixOperator, RHS, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    bool ParenAll = OB.isGtInsideTemplateArgs() &&
                    (InfixOperator == ">" || InfixOperator == ">>");
    if (ParenAll)
      OB.printOpen();
    // Assignment is right associative, with special LHS precedence.
    bool IsAssign = getPrecedence() == Prec::Assign;
    LHS->printAsOperand(OB, IsAssign ? Prec::OrIf : getPrecedence(), !IsAssign);
    // No space before comma operator
    if (!(InfixOperator == ","))
      OB += " ";
    OB += InfixOperator;
    OB += " ";
    RHS->printAsOperand(OB, getPrecedence(), IsAssign);
    if (ParenAll)
      OB.printClose();
  }
};

class ArraySubscriptExpr : public Node {
  const Node *Op1;
  const Node *Op2;

public:
  ArraySubscriptExpr(const Node *Op1_, const Node *Op2_, Prec Prec_)
      : Node(KArraySubscriptExpr, Prec_), Op1(Op1_), Op2(Op2_) {}

  template <typename Fn> void match(Fn F) const {
    F(Op1, Op2, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    Op1->printAsOperand(OB, getPrecedence());
    OB.printOpen('[');
    Op2->printAsOperand(OB);
    OB.printClose(']');
  }
};

class PostfixExpr : public Node {
  const Node *Child;
  const std::string_view Operator;

public:
  PostfixExpr(const Node *Child_, std::string_view Operator_, Prec Prec_)
      : Node(KPostfixExpr, Prec_), Child(Child_), Operator(Operator_) {}

  template <typename Fn> void match(Fn F) const {
    F(Child, Operator, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    Child->printAsOperand(OB, getPrecedence(), true);
    OB += Operator;
  }
};

class ConditionalExpr : public Node {
  const Node *Cond;
  const Node *Then;
  const Node *Else;

public:
  ConditionalExpr(const Node *Cond_, const Node *Then_, const Node *Else_,
                  Prec Prec_)
      : Node(KConditionalExpr, Prec_), Cond(Cond_), Then(Then_), Else(Else_) {}

  template <typename Fn> void match(Fn F) const {
    F(Cond, Then, Else, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    Cond->printAsOperand(OB, getPrecedence());
    OB += " ? ";
    Then->printAsOperand(OB);
    OB += " : ";
    Else->printAsOperand(OB, Prec::Assign, true);
  }
};

class MemberExpr : public Node {
  const Node *LHS;
  const std::string_view Kind;
  const Node *RHS;

public:
  MemberExpr(const Node *LHS_, std::string_view Kind_, const Node *RHS_,
             Prec Prec_)
      : Node(KMemberExpr, Prec_), LHS(LHS_), Kind(Kind_), RHS(RHS_) {}

  template <typename Fn> void match(Fn F) const {
    F(LHS, Kind, RHS, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    LHS->printAsOperand(OB, getPrecedence(), true);
    OB += Kind;
    RHS->printAsOperand(OB, getPrecedence(), false);
  }
};

class SubobjectExpr : public Node {
  const Node *Type;
  const Node *SubExpr;
  std::string_view Offset;
  NodeArray UnionSelectors;
  bool OnePastTheEnd;

public:
  SubobjectExpr(const Node *Type_, const Node *SubExpr_,
                std::string_view Offset_, NodeArray UnionSelectors_,
                bool OnePastTheEnd_)
      : Node(KSubobjectExpr), Type(Type_), SubExpr(SubExpr_), Offset(Offset_),
        UnionSelectors(UnionSelectors_), OnePastTheEnd(OnePastTheEnd_) {}

  template<typename Fn> void match(Fn F) const {
    F(Type, SubExpr, Offset, UnionSelectors, OnePastTheEnd);
  }

  void printLeft(OutputBuffer &OB) const override {
    SubExpr->print(OB);
    OB += ".<";
    Type->print(OB);
    OB += " at offset ";
    if (Offset.empty()) {
      OB += "0";
    } else if (Offset[0] == 'n') {
      OB += "-";
      OB += std::string_view(Offset.data() + 1, Offset.size() - 1);
    } else {
      OB += Offset;
    }
    OB += ">";
  }
};

class EnclosingExpr : public Node {
  const std::string_view Prefix;
  const Node *Infix;
  const std::string_view Postfix;

public:
  EnclosingExpr(std::string_view Prefix_, const Node *Infix_,
                Prec Prec_ = Prec::Primary)
      : Node(KEnclosingExpr, Prec_), Prefix(Prefix_), Infix(Infix_) {}

  template <typename Fn> void match(Fn F) const {
    F(Prefix, Infix, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    OB += Prefix;
    OB.printOpen();
    Infix->print(OB);
    OB.printClose();
    OB += Postfix;
  }
};

class CastExpr : public Node {
  // cast_kind<to>(from)
  const std::string_view CastKind;
  const Node *To;
  const Node *From;

public:
  CastExpr(std::string_view CastKind_, const Node *To_, const Node *From_,
           Prec Prec_)
      : Node(KCastExpr, Prec_), CastKind(CastKind_), To(To_), From(From_) {}

  template <typename Fn> void match(Fn F) const {
    F(CastKind, To, From, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    OB += CastKind;
    {
      ScopedOverride<unsigned> LT(OB.GtIsGt, 0);
      OB += "<";
      To->printLeft(OB);
      OB += ">";
    }
    OB.printOpen();
    From->printAsOperand(OB);
    OB.printClose();
  }
};

class SizeofParamPackExpr : public Node {
  const Node *Pack;

public:
  SizeofParamPackExpr(const Node *Pack_)
      : Node(KSizeofParamPackExpr), Pack(Pack_) {}

  template<typename Fn> void match(Fn F) const { F(Pack); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "sizeof...";
    OB.printOpen();
    ParameterPackExpansion PPE(Pack);
    PPE.printLeft(OB);
    OB.printClose();
  }
};

class CallExpr : public Node {
  const Node *Callee;
  NodeArray Args;

public:
  CallExpr(const Node *Callee_, NodeArray Args_, Prec Prec_)
      : Node(KCallExpr, Prec_), Callee(Callee_), Args(Args_) {}

  template <typename Fn> void match(Fn F) const {
    F(Callee, Args, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    Callee->print(OB);
    OB.printOpen();
    Args.printWithComma(OB);
    OB.printClose();
  }
};

class NewExpr : public Node {
  // new (expr_list) type(init_list)
  NodeArray ExprList;
  Node *Type;
  NodeArray InitList;
  bool IsGlobal; // ::operator new ?
  bool IsArray;  // new[] ?
public:
  NewExpr(NodeArray ExprList_, Node *Type_, NodeArray InitList_, bool IsGlobal_,
          bool IsArray_, Prec Prec_)
      : Node(KNewExpr, Prec_), ExprList(ExprList_), Type(Type_),
        InitList(InitList_), IsGlobal(IsGlobal_), IsArray(IsArray_) {}

  template<typename Fn> void match(Fn F) const {
    F(ExprList, Type, InitList, IsGlobal, IsArray, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    if (IsGlobal)
      OB += "::";
    OB += "new";
    if (IsArray)
      OB += "[]";
    if (!ExprList.empty()) {
      OB.printOpen();
      ExprList.printWithComma(OB);
      OB.printClose();
    }
    OB += " ";
    Type->print(OB);
    if (!InitList.empty()) {
      OB.printOpen();
      InitList.printWithComma(OB);
      OB.printClose();
    }
  }
};

class DeleteExpr : public Node {
  Node *Op;
  bool IsGlobal;
  bool IsArray;

public:
  DeleteExpr(Node *Op_, bool IsGlobal_, bool IsArray_, Prec Prec_)
      : Node(KDeleteExpr, Prec_), Op(Op_), IsGlobal(IsGlobal_),
        IsArray(IsArray_) {}

  template <typename Fn> void match(Fn F) const {
    F(Op, IsGlobal, IsArray, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    if (IsGlobal)
      OB += "::";
    OB += "delete";
    if (IsArray)
      OB += "[]";
    OB += ' ';
    Op->print(OB);
  }
};

class PrefixExpr : public Node {
  std::string_view Prefix;
  Node *Child;

public:
  PrefixExpr(std::string_view Prefix_, Node *Child_, Prec Prec_)
      : Node(KPrefixExpr, Prec_), Prefix(Prefix_), Child(Child_) {}

  template <typename Fn> void match(Fn F) const {
    F(Prefix, Child, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    OB += Prefix;
    Child->printAsOperand(OB, getPrecedence());
  }
};

class FunctionParam : public Node {
  std::string_view Number;

public:
  FunctionParam(std::string_view Number_)
      : Node(KFunctionParam), Number(Number_) {}

  template<typename Fn> void match(Fn F) const { F(Number); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "fp";
    OB += Number;
  }
};

class ConversionExpr : public Node {
  const Node *Type;
  NodeArray Expressions;

public:
  ConversionExpr(const Node *Type_, NodeArray Expressions_, Prec Prec_)
      : Node(KConversionExpr, Prec_), Type(Type_), Expressions(Expressions_) {}

  template <typename Fn> void match(Fn F) const {
    F(Type, Expressions, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    OB.printOpen();
    Type->print(OB);
    OB.printClose();
    OB.printOpen();
    Expressions.printWithComma(OB);
    OB.printClose();
  }
};

class PointerToMemberConversionExpr : public Node {
  const Node *Type;
  const Node *SubExpr;
  std::string_view Offset;

public:
  PointerToMemberConversionExpr(const Node *Type_, const Node *SubExpr_,
                                std::string_view Offset_, Prec Prec_)
      : Node(KPointerToMemberConversionExpr, Prec_), Type(Type_),
        SubExpr(SubExpr_), Offset(Offset_) {}

  template <typename Fn> void match(Fn F) const {
    F(Type, SubExpr, Offset, getPrecedence());
  }

  void printLeft(OutputBuffer &OB) const override {
    OB.printOpen();
    Type->print(OB);
    OB.printClose();
    OB.printOpen();
    SubExpr->print(OB);
    OB.printClose();
  }
};

class InitListExpr : public Node {
  const Node *Ty;
  NodeArray Inits;
public:
  InitListExpr(const Node *Ty_, NodeArray Inits_)
      : Node(KInitListExpr), Ty(Ty_), Inits(Inits_) {}

  template<typename Fn> void match(Fn F) const { F(Ty, Inits); }

  void printLeft(OutputBuffer &OB) const override {
    if (Ty)
      Ty->print(OB);
    OB += '{';
    Inits.printWithComma(OB);
    OB += '}';
  }
};

class BracedExpr : public Node {
  const Node *Elem;
  const Node *Init;
  bool IsArray;
public:
  BracedExpr(const Node *Elem_, const Node *Init_, bool IsArray_)
      : Node(KBracedExpr), Elem(Elem_), Init(Init_), IsArray(IsArray_) {}

  template<typename Fn> void match(Fn F) const { F(Elem, Init, IsArray); }

  void printLeft(OutputBuffer &OB) const override {
    if (IsArray) {
      OB += '[';
      Elem->print(OB);
      OB += ']';
    } else {
      OB += '.';
      Elem->print(OB);
    }
    if (Init->getKind() != KBracedExpr && Init->getKind() != KBracedRangeExpr)
      OB += " = ";
    Init->print(OB);
  }
};

class BracedRangeExpr : public Node {
  const Node *First;
  const Node *Last;
  const Node *Init;
public:
  BracedRangeExpr(const Node *First_, const Node *Last_, const Node *Init_)
      : Node(KBracedRangeExpr), First(First_), Last(Last_), Init(Init_) {}

  template<typename Fn> void match(Fn F) const { F(First, Last, Init); }

  void printLeft(OutputBuffer &OB) const override {
    OB += '[';
    First->print(OB);
    OB += " ... ";
    Last->print(OB);
    OB += ']';
    if (Init->getKind() != KBracedExpr && Init->getKind() != KBracedRangeExpr)
      OB += " = ";
    Init->print(OB);
  }
};

class FoldExpr : public Node {
  const Node *Pack, *Init;
  std::string_view OperatorName;
  bool IsLeftFold;

public:
  FoldExpr(bool IsLeftFold_, std::string_view OperatorName_, const Node *Pack_,
           const Node *Init_)
      : Node(KFoldExpr), Pack(Pack_), Init(Init_), OperatorName(OperatorName_),
        IsLeftFold(IsLeftFold_) {}

  template<typename Fn> void match(Fn F) const {
    F(IsLeftFold, OperatorName, Pack, Init);
  }

  void printLeft(OutputBuffer &OB) const override {
    auto PrintPack = [&] {
      OB.printOpen();
      ParameterPackExpansion(Pack).print(OB);
      OB.printClose();
    };

    OB.printOpen();
    // Either '[init op ]... op pack' or 'pack op ...[ op init]'
    // Refactored to '[(init|pack) op ]...[ op (pack|init)]'
    // Fold expr operands are cast-expressions
    if (!IsLeftFold || Init != nullptr) {
      // '(init|pack) op '
      if (IsLeftFold)
        Init->printAsOperand(OB, Prec::Cast, true);
      else
        PrintPack();
      OB << " " << OperatorName << " ";
    }
    OB << "...";
    if (IsLeftFold || Init != nullptr) {
      // ' op (init|pack)'
      OB << " " << OperatorName << " ";
      if (IsLeftFold)
        PrintPack();
      else
        Init->printAsOperand(OB, Prec::Cast, true);
    }
    OB.printClose();
  }
};

class ThrowExpr : public Node {
  const Node *Op;

public:
  ThrowExpr(const Node *Op_) : Node(KThrowExpr), Op(Op_) {}

  template<typename Fn> void match(Fn F) const { F(Op); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "throw ";
    Op->print(OB);
  }
};

class BoolExpr : public Node {
  bool Value;

public:
  BoolExpr(bool Value_) : Node(KBoolExpr), Value(Value_) {}

  template<typename Fn> void match(Fn F) const { F(Value); }

  void printLeft(OutputBuffer &OB) const override {
    OB += Value ? std::string_view("true") : std::string_view("false");
  }
};

class StringLiteral : public Node {
  const Node *Type;

public:
  StringLiteral(const Node *Type_) : Node(KStringLiteral), Type(Type_) {}

  template<typename Fn> void match(Fn F) const { F(Type); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "\"<";
    Type->print(OB);
    OB += ">\"";
  }
};

class LambdaExpr : public Node {
  const Node *Type;

public:
  LambdaExpr(const Node *Type_) : Node(KLambdaExpr), Type(Type_) {}

  template<typename Fn> void match(Fn F) const { F(Type); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "[]";
    if (Type->getKind() == KClosureTypeName)
      static_cast<const ClosureTypeName *>(Type)->printDeclarator(OB);
    OB += "{...}";
  }
};

class EnumLiteral : public Node {
  // ty(integer)
  const Node *Ty;
  std::string_view Integer;

public:
  EnumLiteral(const Node *Ty_, std::string_view Integer_)
      : Node(KEnumLiteral), Ty(Ty_), Integer(Integer_) {}

  template<typename Fn> void match(Fn F) const { F(Ty, Integer); }

  void printLeft(OutputBuffer &OB) const override {
    OB.printOpen();
    Ty->print(OB);
    OB.printClose();

    if (Integer[0] == 'n')
      OB << '-' << std::string_view(Integer.data() + 1, Integer.size() - 1);
    else
      OB << Integer;
  }
};

class IntegerLiteral : public Node {
  std::string_view Type;
  std::string_view Value;

public:
  IntegerLiteral(std::string_view Type_, std::string_view Value_)
      : Node(KIntegerLiteral), Type(Type_), Value(Value_) {}

  template<typename Fn> void match(Fn F) const { F(Type, Value); }

  void printLeft(OutputBuffer &OB) const override {
    if (Type.size() > 3) {
      OB.printOpen();
      OB += Type;
      OB.printClose();
    }

    if (Value[0] == 'n')
      OB << '-' << std::string_view(Value.data() + 1, Value.size() - 1);
    else
      OB += Value;

    if (Type.size() <= 3)
      OB += Type;
  }
};

class RequiresExpr : public Node {
  NodeArray Parameters;
  NodeArray Requirements;
public:
  RequiresExpr(NodeArray Parameters_, NodeArray Requirements_)
      : Node(KRequiresExpr), Parameters(Parameters_),
        Requirements(Requirements_) {}

  template<typename Fn> void match(Fn F) const { F(Parameters, Requirements); }

  void printLeft(OutputBuffer &OB) const override {
    OB += "requires";
    if (!Parameters.empty()) {
      OB += ' ';
      OB.printOpen();
      Parameters.printWithComma(OB);
      OB.printClose();
    }
    OB += ' ';
    OB.printOpen('{');
    for (const Node *Req : Requirements) {
      Req->print(OB);
    }
    OB += ' ';
    OB.printClose('}');
  }
};

class ExprRequirement : public Node {
  const Node *Expr;
  bool IsNoexcept;
  const Node *TypeConstraint;
public:
  ExprRequirement(const Node *Expr_, bool IsNoexcept_,
                  const Node *TypeConstraint_)
      : Node(KExprRequirement), Expr(Expr_), IsNoexcept(IsNoexcept_),
        TypeConstraint(TypeConstraint_) {}

  template <typename Fn> void match(Fn F) const {
    F(Expr, IsNoexcept, TypeConstraint);
  }

  void printLeft(OutputBuffer &OB) const override {
    OB += " ";
    if (IsNoexcept || TypeConstraint)
      OB.printOpen('{');
    Expr->print(OB);
    if (IsNoexcept || TypeConstraint)
      OB.printClose('}');
    if (IsNoexcept)
      OB += " noexcept";
    if (TypeConstraint) {
      OB += " -> ";
      TypeConstraint->print(OB);
    }
    OB += ';';
  }
};

class TypeRequirement : public Node {
  const Node *Type;
public:
  TypeRequirement(const Node *Type_)
      : Node(KTypeRequirement), Type(Type_) {}

  template <typename Fn> void match(Fn F) const { F(Type); }

  void printLeft(OutputBuffer &OB) const override {
    OB += " typename ";
    Type->print(OB);
    OB += ';';
  }
};

class NestedRequirement : public Node {
  const Node *Constraint;
public:
  NestedRequirement(const Node *Constraint_)
      : Node(KNestedRequirement), Constraint(Constraint_) {}

  template <typename Fn> void match(Fn F) const { F(Constraint); }

  void printLeft(OutputBuffer &OB) const override {
    OB += " requires ";
    Constraint->print(OB);
    OB += ';';
  }
};

template <class Float> struct FloatData;

namespace float_literal_impl {
constexpr Node::Kind getFloatLiteralKind(float *) {
  return Node::KFloatLiteral;
}
constexpr Node::Kind getFloatLiteralKind(double *) {
  return Node::KDoubleLiteral;
}
constexpr Node::Kind getFloatLiteralKind(long double *) {
  return Node::KLongDoubleLiteral;
}
}

template <class Float> class FloatLiteralImpl : public Node {
  const std::string_view Contents;

  static constexpr Kind KindForClass =
      float_literal_impl::getFloatLiteralKind((Float *)nullptr);

public:
  FloatLiteralImpl(std::string_view Contents_)
      : Node(KindForClass), Contents(Contents_) {}

  template<typename Fn> void match(Fn F) const { F(Contents); }

  void printLeft(OutputBuffer &OB) const override {
    const size_t N = FloatData<Float>::mangled_size;
    if (Contents.size() >= N) {
      union {
        Float value;
        char buf[sizeof(Float)];
      };
      const char *t = Contents.data();
      const char *last = t + N;
      char *e = buf;
      for (; t != last; ++t, ++e) {
        unsigned d1 = isdigit(*t) ? static_cast<unsigned>(*t - '0')
                                  : static_cast<unsigned>(*t - 'a' + 10);
        ++t;
        unsigned d0 = isdigit(*t) ? static_cast<unsigned>(*t - '0')
                                  : static_cast<unsigned>(*t - 'a' + 10);
        *e = static_cast<char>((d1 << 4) + d0);
      }
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      std::reverse(buf, e);
#endif
      char num[FloatData<Float>::max_demangled_size] = {0};
      int n = snprintf(num, sizeof(num), FloatData<Float>::spec, value);
      OB += std::string_view(num, n);
    }
  }
};

using FloatLiteral = FloatLiteralImpl<float>;
using DoubleLiteral = FloatLiteralImpl<double>;
using LongDoubleLiteral = FloatLiteralImpl<long double>;

/// Visit the node. Calls \c F(P), where \c P is the node cast to the
/// appropriate derived class.
template<typename Fn>
void Node::visit(Fn F) const {
  switch (K) {
#define NODE(X)                                                                \
  case K##X:                                                                   \
    return F(static_cast<const X *>(this));
#include "ItaniumNodes.def"
  }
  DEMANGLE_ASSERT(0, "unknown mangling node kind");
}

/// Determine the kind of a node from its type.
template<typename NodeT> struct NodeKind;
#define NODE(X)                                                                \
  template <> struct NodeKind<X> {                                             \
    static constexpr Node::Kind Kind = Node::K##X;                             \
    static constexpr const char *name() { return #X; }                         \
  };
#include "ItaniumNodes.def"

template <typename Derived, typename Alloc> struct AbstractManglingParser {
  const char *First;
  const char *Last;

  // Name stack, this is used by the parser to hold temporary names that were
  // parsed. The parser collapses multiple names into new nodes to construct
  // the AST. Once the parser is finished, names.size() == 1.
  PODSmallVector<Node *, 32> Names;

  // Substitution table. Itanium supports name substitutions as a means of
  // compression. The string "S42_" refers to the 44nd entry (base-36) in this
  // table.
  PODSmallVector<Node *, 32> Subs;

  // A list of template argument values corresponding to a template parameter
  // list.
  using TemplateParamList = PODSmallVector<Node *, 8>;

  class ScopedTemplateParamList {
    AbstractManglingParser *Parser;
    size_t OldNumTemplateParamLists;
    TemplateParamList Params;

  public:
    ScopedTemplateParamList(AbstractManglingParser *TheParser)
        : Parser(TheParser),
          OldNumTemplateParamLists(TheParser->TemplateParams.size()) {
      Parser->TemplateParams.push_back(&Params);
    }
    ~ScopedTemplateParamList() {
      DEMANGLE_ASSERT(Parser->TemplateParams.size() >= OldNumTemplateParamLists,
                      "");
      Parser->TemplateParams.shrinkToSize(OldNumTemplateParamLists);
    }
    TemplateParamList *params() { return &Params; }
  };

  // Template parameter table. Like the above, but referenced like "T42_".
  // This has a smaller size compared to Subs and Names because it can be
  // stored on the stack.
  TemplateParamList OuterTemplateParams;

  // Lists of template parameters indexed by template parameter depth,
  // referenced like "TL2_4_". If nonempty, element 0 is always
  // OuterTemplateParams; inner elements are always template parameter lists of
  // lambda expressions. For a generic lambda with no explicit template
  // parameter list, the corresponding parameter list pointer will be null.
  PODSmallVector<TemplateParamList *, 4> TemplateParams;

  class SaveTemplateParams {
    AbstractManglingParser *Parser;
    decltype(TemplateParams) OldParams;
    decltype(OuterTemplateParams) OldOuterParams;

  public:
    SaveTemplateParams(AbstractManglingParser *TheParser) : Parser(TheParser) {
      OldParams = std::move(Parser->TemplateParams);
      OldOuterParams = std::move(Parser->OuterTemplateParams);
      Parser->TemplateParams.clear();
      Parser->OuterTemplateParams.clear();
    }
    ~SaveTemplateParams() {
      Parser->TemplateParams = std::move(OldParams);
      Parser->OuterTemplateParams = std::move(OldOuterParams);
    }
  };

  // Set of unresolved forward <template-param> references. These can occur in a
  // conversion operator's type, and are resolved in the enclosing <encoding>.
  PODSmallVector<ForwardTemplateReference *, 4> ForwardTemplateRefs;

  bool TryToParseTemplateArgs = true;
  bool PermitForwardTemplateReferences = false;
  bool InConstraintExpr = false;
  size_t ParsingLambdaParamsAtLevel = (size_t)-1;

  unsigned NumSyntheticTemplateParameters[3] = {};

  Alloc ASTAllocator;

  AbstractManglingParser(const char *First_, const char *Last_)
      : First(First_), Last(Last_) {}

  Derived &getDerived() { return static_cast<Derived &>(*this); }

  void reset(const char *First_, const char *Last_) {
    First = First_;
    Last = Last_;
    Names.clear();
    Subs.clear();
    TemplateParams.clear();
    ParsingLambdaParamsAtLevel = (size_t)-1;
    TryToParseTemplateArgs = true;
    PermitForwardTemplateReferences = false;
    for (int I = 0; I != 3; ++I)
      NumSyntheticTemplateParameters[I] = 0;
    ASTAllocator.reset();
  }

  template <class T, class... Args> Node *make(Args &&... args) {
    return ASTAllocator.template makeNode<T>(std::forward<Args>(args)...);
  }

  template <class It> NodeArray makeNodeArray(It begin, It end) {
    size_t sz = static_cast<size_t>(end - begin);
    void *mem = ASTAllocator.allocateNodeArray(sz);
    Node **data = new (mem) Node *[sz];
    std::copy(begin, end, data);
    return NodeArray(data, sz);
  }

  NodeArray popTrailingNodeArray(size_t FromPosition) {
    DEMANGLE_ASSERT(FromPosition <= Names.size(), "");
    NodeArray res =
        makeNodeArray(Names.begin() + (long)FromPosition, Names.end());
    Names.shrinkToSize(FromPosition);
    return res;
  }

  bool consumeIf(std::string_view S) {
    if (starts_with(std::string_view(First, Last - First), S)) {
      First += S.size();
      return true;
    }
    return false;
  }

  bool consumeIf(char C) {
    if (First != Last && *First == C) {
      ++First;
      return true;
    }
    return false;
  }

  char consume() { return First != Last ? *First++ : '\0'; }

  char look(unsigned Lookahead = 0) const {
    if (static_cast<size_t>(Last - First) <= Lookahead)
      return '\0';
    return First[Lookahead];
  }

  size_t numLeft() const { return static_cast<size_t>(Last - First); }

  std::string_view parseNumber(bool AllowNegative = false);
  Qualifiers parseCVQualifiers();
  bool parsePositiveInteger(size_t *Out);
  std::string_view parseBareSourceName();

  bool parseSeqId(size_t *Out);
  Node *parseSubstitution();
  Node *parseTemplateParam();
  Node *parseTemplateParamDecl(TemplateParamList *Params);
  Node *parseTemplateArgs(bool TagTemplates = false);
  Node *parseTemplateArg();

  bool isTemplateParamDecl() {
    return look() == 'T' &&
           std::string_view("yptnk").find(look(1)) != std::string_view::npos;
  }

  /// Parse the <expression> production.
  Node *parseExpr();
  Node *parsePrefixExpr(std::string_view Kind, Node::Prec Prec);
  Node *parseBinaryExpr(std::string_view Kind, Node::Prec Prec);
  Node *parseIntegerLiteral(std::string_view Lit);
  Node *parseExprPrimary();
  template <class Float> Node *parseFloatingLiteral();
  Node *parseFunctionParam();
  Node *parseConversionExpr();
  Node *parseBracedExpr();
  Node *parseFoldExpr();
  Node *parsePointerToMemberConversionExpr(Node::Prec Prec);
  Node *parseSubobjectExpr();
  Node *parseConstraintExpr();
  Node *parseRequiresExpr();

  /// Parse the <type> production.
  Node *parseType();
  Node *parseFunctionType();
  Node *parseVectorType();
  Node *parseDecltype();
  Node *parseArrayType();
  Node *parsePointerToMemberType();
  Node *parseClassEnumType();
  Node *parseQualifiedType();

  Node *parseEncoding(bool ParseParams = true);
  bool parseCallOffset();
  Node *parseSpecialName();

  /// Holds some extra information about a <name> that is being parsed. This
  /// information is only pertinent if the <name> refers to an <encoding>.
  struct NameState {
    bool CtorDtorConversion = false;
    bool EndsWithTemplateArgs = false;
    Qualifiers CVQualifiers = QualNone;
    FunctionRefQual ReferenceQualifier = FrefQualNone;
    size_t ForwardTemplateRefsBegin;
    bool HasExplicitObjectParameter = false;

    NameState(AbstractManglingParser *Enclosing)
        : ForwardTemplateRefsBegin(Enclosing->ForwardTemplateRefs.size()) {}
  };

  bool resolveForwardTemplateRefs(NameState &State) {
    size_t I = State.ForwardTemplateRefsBegin;
    size_t E = ForwardTemplateRefs.size();
    for (; I < E; ++I) {
      size_t Idx = ForwardTemplateRefs[I]->Index;
      if (TemplateParams.empty() || !TemplateParams[0] ||
          Idx >= TemplateParams[0]->size())
        return true;
      ForwardTemplateRefs[I]->Ref = (*TemplateParams[0])[Idx];
    }
    ForwardTemplateRefs.shrinkToSize(State.ForwardTemplateRefsBegin);
    return false;
  }

  /// Parse the <name> production>
  Node *parseName(NameState *State = nullptr);
  Node *parseLocalName(NameState *State);
  Node *parseOperatorName(NameState *State);
  bool parseModuleNameOpt(ModuleName *&Module);
  Node *parseUnqualifiedName(NameState *State, Node *Scope, ModuleName *Module);
  Node *parseUnnamedTypeName(NameState *State);
  Node *parseSourceName(NameState *State);
  Node *parseUnscopedName(NameState *State, bool *isSubstName);
  Node *parseNestedName(NameState *State);
  Node *parseCtorDtorName(Node *&SoFar, NameState *State);

  Node *parseAbiTags(Node *N);

  struct OperatorInfo {
    enum OIKind : unsigned char {
      Prefix,      // Prefix unary: @ expr
      Postfix,     // Postfix unary: expr @
      Binary,      // Binary: lhs @ rhs
      Array,       // Array index:  lhs [ rhs ]
      Member,      // Member access: lhs @ rhs
      New,         // New
      Del,         // Delete
      Call,        // Function call: expr (expr*)
      CCast,       // C cast: (type)expr
      Conditional, // Conditional: expr ? expr : expr
      NameOnly,    // Overload only, not allowed in expression.
      // Below do not have operator names
      NamedCast, // Named cast, @<type>(expr)
      OfIdOp,    // alignof, sizeof, typeid

      Unnameable = NamedCast,
    };
    char Enc[2];      // Encoding
    OIKind Kind;      // Kind of operator
    bool Flag : 1;    // Entry-specific flag
    Node::Prec Prec : 7; // Precedence
    const char *Name; // Spelling

  public:
    constexpr OperatorInfo(const char (&E)[3], OIKind K, bool F, Node::Prec P,
                           const char *N)
        : Enc{E[0], E[1]}, Kind{K}, Flag{F}, Prec{P}, Name{N} {}

  public:
    bool operator<(const OperatorInfo &Other) const {
      return *this < Other.Enc;
    }
    bool operator<(const char *Peek) const {
      return Enc[0] < Peek[0] || (Enc[0] == Peek[0] && Enc[1] < Peek[1]);
    }
    bool operator==(const char *Peek) const {
      return Enc[0] == Peek[0] && Enc[1] == Peek[1];
    }
    bool operator!=(const char *Peek) const { return !this->operator==(Peek); }

  public:
    std::string_view getSymbol() const {
      std::string_view Res = Name;
      if (Kind < Unnameable) {
        DEMANGLE_ASSERT(starts_with(Res, "operator"),
                        "operator name does not start with 'operator'");
        Res.remove_prefix(sizeof("operator") - 1);
        if (starts_with(Res, ' '))
          Res.remove_prefix(1);
      }
      return Res;
    }
    std::string_view getName() const { return Name; }
    OIKind getKind() const { return Kind; }
    bool getFlag() const { return Flag; }
    Node::Prec getPrecedence() const { return Prec; }
  };
  static const OperatorInfo Ops[];
  static const size_t NumOps;
  const OperatorInfo *parseOperatorEncoding();

  /// Parse the <unresolved-name> production.
  Node *parseUnresolvedName(bool Global);
  Node *parseSimpleId();
  Node *parseBaseUnresolvedName();
  Node *parseUnresolvedType();
  Node *parseDestructorName();

  /// Top-level entry point into the parser.
  Node *parse(bool ParseParams = true);
};

const char* parse_discriminator(const char* first, const char* last);

// <name> ::= <nested-name> // N
//        ::= <local-name> # See Scope Encoding below  // Z
//        ::= <unscoped-template-name> <template-args>
//        ::= <unscoped-name>
//
// <unscoped-template-name> ::= <unscoped-name>
//                          ::= <substitution>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseName(NameState *State) {
  if (look() == 'N')
    return getDerived().parseNestedName(State);
  if (look() == 'Z')
    return getDerived().parseLocalName(State);

  Node *Result = nullptr;
  bool IsSubst = false;

  Result = getDerived().parseUnscopedName(State, &IsSubst);
  if (!Result)
    return nullptr;

  if (look() == 'I') {
    //        ::= <unscoped-template-name> <template-args>
    if (!IsSubst)
      // An unscoped-template-name is substitutable.
      Subs.push_back(Result);
    Node *TA = getDerived().parseTemplateArgs(State != nullptr);
    if (TA == nullptr)
      return nullptr;
    if (State)
      State->EndsWithTemplateArgs = true;
    Result = make<NameWithTemplateArgs>(Result, TA);
  } else if (IsSubst) {
    // The substitution case must be followed by <template-args>.
    return nullptr;
  }

  return Result;
}

// <local-name> := Z <function encoding> E <entity name> [<discriminator>]
//              := Z <function encoding> E s [<discriminator>]
//              := Z <function encoding> Ed [ <parameter number> ] _ <entity name>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseLocalName(NameState *State) {
  if (!consumeIf('Z'))
    return nullptr;
  Node *Encoding = getDerived().parseEncoding();
  if (Encoding == nullptr || !consumeIf('E'))
    return nullptr;

  if (consumeIf('s')) {
    First = parse_discriminator(First, Last);
    auto *StringLitName = make<NameType>("string literal");
    if (!StringLitName)
      return nullptr;
    return make<LocalName>(Encoding, StringLitName);
  }

  // The template parameters of the inner name are unrelated to those of the
  // enclosing context.
  SaveTemplateParams SaveTemplateParamsScope(this);

  if (consumeIf('d')) {
    parseNumber(true);
    if (!consumeIf('_'))
      return nullptr;
    Node *N = getDerived().parseName(State);
    if (N == nullptr)
      return nullptr;
    return make<LocalName>(Encoding, N);
  }

  Node *Entity = getDerived().parseName(State);
  if (Entity == nullptr)
    return nullptr;
  First = parse_discriminator(First, Last);
  return make<LocalName>(Encoding, Entity);
}

// <unscoped-name> ::= <unqualified-name>
//                 ::= St <unqualified-name>   # ::std::
// [*] extension
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseUnscopedName(NameState *State,
                                                          bool *IsSubst) {

  Node *Std = nullptr;
  if (consumeIf("St")) {
    Std = make<NameType>("std");
    if (Std == nullptr)
      return nullptr;
  }

  Node *Res = nullptr;
  ModuleName *Module = nullptr;
  if (look() == 'S') {
    Node *S = getDerived().parseSubstitution();
    if (!S)
      return nullptr;
    if (S->getKind() == Node::KModuleName)
      Module = static_cast<ModuleName *>(S);
    else if (IsSubst && Std == nullptr) {
      Res = S;
      *IsSubst = true;
    } else {
      return nullptr;
    }
  }

  if (Res == nullptr || Std != nullptr) {
    Res = getDerived().parseUnqualifiedName(State, Std, Module);
  }

  return Res;
}

// <unqualified-name> ::= [<module-name>] F? L? <operator-name> [<abi-tags>]
//                    ::= [<module-name>] <ctor-dtor-name> [<abi-tags>]
//                    ::= [<module-name>] F? L? <source-name> [<abi-tags>]
//                    ::= [<module-name>] L? <unnamed-type-name> [<abi-tags>]
//			# structured binding declaration
//                    ::= [<module-name>] L? DC <source-name>+ E
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseUnqualifiedName(
    NameState *State, Node *Scope, ModuleName *Module) {
  if (getDerived().parseModuleNameOpt(Module))
    return nullptr;

  bool IsMemberLikeFriend = Scope && consumeIf('F');

  consumeIf('L');

  Node *Result;
  if (look() >= '1' && look() <= '9') {
    Result = getDerived().parseSourceName(State);
  } else if (look() == 'U') {
    Result = getDerived().parseUnnamedTypeName(State);
  } else if (consumeIf("DC")) {
    // Structured binding
    size_t BindingsBegin = Names.size();
    do {
      Node *Binding = getDerived().parseSourceName(State);
      if (Binding == nullptr)
        return nullptr;
      Names.push_back(Binding);
    } while (!consumeIf('E'));
    Result = make<StructuredBindingName>(popTrailingNodeArray(BindingsBegin));
  } else if (look() == 'C' || look() == 'D') {
    // A <ctor-dtor-name>.
    if (Scope == nullptr || Module != nullptr)
      return nullptr;
    Result = getDerived().parseCtorDtorName(Scope, State);
  } else {
    Result = getDerived().parseOperatorName(State);
  }

  if (Result != nullptr && Module != nullptr)
    Result = make<ModuleEntity>(Module, Result);
  if (Result != nullptr)
    Result = getDerived().parseAbiTags(Result);
  if (Result != nullptr && IsMemberLikeFriend)
    Result = make<MemberLikeFriendName>(Scope, Result);
  else if (Result != nullptr && Scope != nullptr)
    Result = make<NestedName>(Scope, Result);

  return Result;
}

// <module-name> ::= <module-subname>
// 	 	 ::= <module-name> <module-subname>
//		 ::= <substitution>  # passed in by caller
// <module-subname> ::= W <source-name>
//		    ::= W P <source-name>
template <typename Derived, typename Alloc>
bool AbstractManglingParser<Derived, Alloc>::parseModuleNameOpt(
    ModuleName *&Module) {
  while (consumeIf('W')) {
    bool IsPartition = consumeIf('P');
    Node *Sub = getDerived().parseSourceName(nullptr);
    if (!Sub)
      return true;
    Module =
        static_cast<ModuleName *>(make<ModuleName>(Module, Sub, IsPartition));
    Subs.push_back(Module);
  }

  return false;
}

// <unnamed-type-name> ::= Ut [<nonnegative number>] _
//                     ::= <closure-type-name>
//
// <closure-type-name> ::= Ul <lambda-sig> E [ <nonnegative number> ] _
//
// <lambda-sig> ::= <template-param-decl>* [Q <requires-clause expression>]
//                  <parameter type>+  # or "v" if the lambda has no parameters
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseUnnamedTypeName(NameState *State) {
  // <template-params> refer to the innermost <template-args>. Clear out any
  // outer args that we may have inserted into TemplateParams.
  if (State != nullptr)
    TemplateParams.clear();

  if (consumeIf("Ut")) {
    std::string_view Count = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<UnnamedTypeName>(Count);
  }
  if (consumeIf("Ul")) {
    ScopedOverride<size_t> SwapParams(ParsingLambdaParamsAtLevel,
                                      TemplateParams.size());
    ScopedTemplateParamList LambdaTemplateParams(this);

    size_t ParamsBegin = Names.size();
    while (getDerived().isTemplateParamDecl()) {
      Node *T =
          getDerived().parseTemplateParamDecl(LambdaTemplateParams.params());
      if (T == nullptr)
        return nullptr;
      Names.push_back(T);
    }
    NodeArray TempParams = popTrailingNodeArray(ParamsBegin);

    // FIXME: If TempParams is empty and none of the function parameters
    // includes 'auto', we should remove LambdaTemplateParams from the
    // TemplateParams list. Unfortunately, we don't find out whether there are
    // any 'auto' parameters until too late in an example such as:
    //
    //   template<typename T> void f(
    //       decltype([](decltype([]<typename T>(T v) {}),
    //                   auto) {})) {}
    //   template<typename T> void f(
    //       decltype([](decltype([]<typename T>(T w) {}),
    //                   int) {})) {}
    //
    // Here, the type of v is at level 2 but the type of w is at level 1. We
    // don't find this out until we encounter the type of the next parameter.
    //
    // However, compilers can't actually cope with the former example in
    // practice, and it's likely to be made ill-formed in future, so we don't
    // need to support it here.
    //
    // If we encounter an 'auto' in the function parameter types, we will
    // recreate a template parameter scope for it, but any intervening lambdas
    // will be parsed in the 'wrong' template parameter depth.
    if (TempParams.empty())
      TemplateParams.pop_back();

    Node *Requires1 = nullptr;
    if (consumeIf('Q')) {
      Requires1 = getDerived().parseConstraintExpr();
      if (Requires1 == nullptr)
        return nullptr;
    }

    if (!consumeIf("v")) {
      do {
        Node *P = getDerived().parseType();
        if (P == nullptr)
          return nullptr;
        Names.push_back(P);
      } while (look() != 'E' && look() != 'Q');
    }
    NodeArray Params = popTrailingNodeArray(ParamsBegin);

    Node *Requires2 = nullptr;
    if (consumeIf('Q')) {
      Requires2 = getDerived().parseConstraintExpr();
      if (Requires2 == nullptr)
        return nullptr;
    }

    if (!consumeIf('E'))
      return nullptr;

    std::string_view Count = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<ClosureTypeName>(TempParams, Requires1, Params, Requires2,
                                 Count);
  }
  if (consumeIf("Ub")) {
    (void)parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<NameType>("'block-literal'");
  }
  return nullptr;
}

// <source-name> ::= <positive length number> <identifier>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSourceName(NameState *) {
  size_t Length = 0;
  if (parsePositiveInteger(&Length))
    return nullptr;
  if (numLeft() < Length || Length == 0)
    return nullptr;
  std::string_view Name(First, Length);
  First += Length;
  if (starts_with(Name, "_GLOBAL__N"))
    return make<NameType>("(anonymous namespace)");
  return make<NameType>(Name);
}

// Operator encodings
template <typename Derived, typename Alloc>
const typename AbstractManglingParser<
    Derived, Alloc>::OperatorInfo AbstractManglingParser<Derived,
                                                         Alloc>::Ops[] = {
    // Keep ordered by encoding
    {"aN", OperatorInfo::Binary, false, Node::Prec::Assign, "operator&="},
    {"aS", OperatorInfo::Binary, false, Node::Prec::Assign, "operator="},
    {"aa", OperatorInfo::Binary, false, Node::Prec::AndIf, "operator&&"},
    {"ad", OperatorInfo::Prefix, false, Node::Prec::Unary, "operator&"},
    {"an", OperatorInfo::Binary, false, Node::Prec::And, "operator&"},
    {"at", OperatorInfo::OfIdOp, /*Type*/ true, Node::Prec::Unary, "alignof "},
    {"aw", OperatorInfo::NameOnly, false, Node::Prec::Primary,
     "operator co_await"},
    {"az", OperatorInfo::OfIdOp, /*Type*/ false, Node::Prec::Unary, "alignof "},
    {"cc", OperatorInfo::NamedCast, false, Node::Prec::Postfix, "const_cast"},
    {"cl", OperatorInfo::Call, false, Node::Prec::Postfix, "operator()"},
    {"cm", OperatorInfo::Binary, false, Node::Prec::Comma, "operator,"},
    {"co", OperatorInfo::Prefix, false, Node::Prec::Unary, "operator~"},
    {"cv", OperatorInfo::CCast, false, Node::Prec::Cast, "operator"}, // C Cast
    {"dV", OperatorInfo::Binary, false, Node::Prec::Assign, "operator/="},
    {"da", OperatorInfo::Del, /*Ary*/ true, Node::Prec::Unary,
     "operator delete[]"},
    {"dc", OperatorInfo::NamedCast, false, Node::Prec::Postfix, "dynamic_cast"},
    {"de", OperatorInfo::Prefix, false, Node::Prec::Unary, "operator*"},
    {"dl", OperatorInfo::Del, /*Ary*/ false, Node::Prec::Unary,
     "operator delete"},
    {"ds", OperatorInfo::Member, /*Named*/ false, Node::Prec::PtrMem,
     "operator.*"},
    {"dt", OperatorInfo::Member, /*Named*/ false, Node::Prec::Postfix,
     "operator."},
    {"dv", OperatorInfo::Binary, false, Node::Prec::Assign, "operator/"},
    {"eO", OperatorInfo::Binary, false, Node::Prec::Assign, "operator^="},
    {"eo", OperatorInfo::Binary, false, Node::Prec::Xor, "operator^"},
    {"eq", OperatorInfo::Binary, false, Node::Prec::Equality, "operator=="},
    {"ge", OperatorInfo::Binary, false, Node::Prec::Relational, "operator>="},
    {"gt", OperatorInfo::Binary, false, Node::Prec::Relational, "operator>"},
    {"ix", OperatorInfo::Array, false, Node::Prec::Postfix, "operator[]"},
    {"lS", OperatorInfo::Binary, false, Node::Prec::Assign, "operator<<="},
    {"le", OperatorInfo::Binary, false, Node::Prec::Relational, "operator<="},
    {"ls", OperatorInfo::Binary, false, Node::Prec::Shift, "operator<<"},
    {"lt", OperatorInfo::Binary, false, Node::Prec::Relational, "operator<"},
    {"mI", OperatorInfo::Binary, false, Node::Prec::Assign, "operator-="},
    {"mL", OperatorInfo::Binary, false, Node::Prec::Assign, "operator*="},
    {"mi", OperatorInfo::Binary, false, Node::Prec::Additive, "operator-"},
    {"ml", OperatorInfo::Binary, false, Node::Prec::Multiplicative,
     "operator*"},
    {"mm", OperatorInfo::Postfix, false, Node::Prec::Postfix, "operator--"},
    {"na", OperatorInfo::New, /*Ary*/ true, Node::Prec::Unary,
     "operator new[]"},
    {"ne", OperatorInfo::Binary, false, Node::Prec::Equality, "operator!="},
    {"ng", OperatorInfo::Prefix, false, Node::Prec::Unary, "operator-"},
    {"nt", OperatorInfo::Prefix, false, Node::Prec::Unary, "operator!"},
    {"nw", OperatorInfo::New, /*Ary*/ false, Node::Prec::Unary, "operator new"},
    {"oR", OperatorInfo::Binary, false, Node::Prec::Assign, "operator|="},
    {"oo", OperatorInfo::Binary, false, Node::Prec::OrIf, "operator||"},
    {"or", OperatorInfo::Binary, false, Node::Prec::Ior, "operator|"},
    {"pL", OperatorInfo::Binary, false, Node::Prec::Assign, "operator+="},
    {"pl", OperatorInfo::Binary, false, Node::Prec::Additive, "operator+"},
    {"pm", OperatorInfo::Member, /*Named*/ false, Node::Prec::PtrMem,
     "operator->*"},
    {"pp", OperatorInfo::Postfix, false, Node::Prec::Postfix, "operator++"},
    {"ps", OperatorInfo::Prefix, false, Node::Prec::Unary, "operator+"},
    {"pt", OperatorInfo::Member, /*Named*/ true, Node::Prec::Postfix,
     "operator->"},
    {"qu", OperatorInfo::Conditional, false, Node::Prec::Conditional,
     "operator?"},
    {"rM", OperatorInfo::Binary, false, Node::Prec::Assign, "operator%="},
    {"rS", OperatorInfo::Binary, false, Node::Prec::Assign, "operator>>="},
    {"rc", OperatorInfo::NamedCast, false, Node::Prec::Postfix,
     "reinterpret_cast"},
    {"rm", OperatorInfo::Binary, false, Node::Prec::Multiplicative,
     "operator%"},
    {"rs", OperatorInfo::Binary, false, Node::Prec::Shift, "operator>>"},
    {"sc", OperatorInfo::NamedCast, false, Node::Prec::Postfix, "static_cast"},
    {"ss", OperatorInfo::Binary, false, Node::Prec::Spaceship, "operator<=>"},
    {"st", OperatorInfo::OfIdOp, /*Type*/ true, Node::Prec::Unary, "sizeof "},
    {"sz", OperatorInfo::OfIdOp, /*Type*/ false, Node::Prec::Unary, "sizeof "},
    {"te", OperatorInfo::OfIdOp, /*Type*/ false, Node::Prec::Postfix,
     "typeid "},
    {"ti", OperatorInfo::OfIdOp, /*Type*/ true, Node::Prec::Postfix, "typeid "},
};
template <typename Derived, typename Alloc>
const size_t AbstractManglingParser<Derived, Alloc>::NumOps = sizeof(Ops) /
                                                              sizeof(Ops[0]);

// If the next 2 chars are an operator encoding, consume them and return their
// OperatorInfo.  Otherwise return nullptr.
template <typename Derived, typename Alloc>
const typename AbstractManglingParser<Derived, Alloc>::OperatorInfo *
AbstractManglingParser<Derived, Alloc>::parseOperatorEncoding() {
  if (numLeft() < 2)
    return nullptr;

  // We can't use lower_bound as that can link to symbols in the C++ library,
  // and this must remain independant of that.
  size_t lower = 0u, upper = NumOps - 1; // Inclusive bounds.
  while (upper != lower) {
    size_t middle = (upper + lower) / 2;
    if (Ops[middle] < First)
      lower = middle + 1;
    else
      upper = middle;
  }
  if (Ops[lower] != First)
    return nullptr;

  First += 2;
  return &Ops[lower];
}

//   <operator-name> ::= See parseOperatorEncoding()
//                   ::= li <source-name>  # operator ""
//                   ::= v <digit> <source-name>  # vendor extended operator
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseOperatorName(NameState *State) {
  if (const auto *Op = parseOperatorEncoding()) {
    if (Op->getKind() == OperatorInfo::CCast) {
      //              ::= cv <type>    # (cast)
      ScopedOverride<bool> SaveTemplate(TryToParseTemplateArgs, false);
      // If we're parsing an encoding, State != nullptr and the conversion
      // operators' <type> could have a <template-param> that refers to some
      // <template-arg>s further ahead in the mangled name.
      ScopedOverride<bool> SavePermit(PermitForwardTemplateReferences,
                                      PermitForwardTemplateReferences ||
                                          State != nullptr);
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      if (State) State->CtorDtorConversion = true;
      return make<ConversionOperatorType>(Ty);
    }

    if (Op->getKind() >= OperatorInfo::Unnameable)
      /* Not a nameable operator.  */
      return nullptr;
    if (Op->getKind() == OperatorInfo::Member && !Op->getFlag())
      /* Not a nameable MemberExpr */
      return nullptr;

    return make<NameType>(Op->getName());
  }

  if (consumeIf("li")) {
    //                   ::= li <source-name>  # operator ""
    Node *SN = getDerived().parseSourceName(State);
    if (SN == nullptr)
      return nullptr;
    return make<LiteralOperator>(SN);
  }

  if (consumeIf('v')) {
    // ::= v <digit> <source-name>        # vendor extended operator
    if (look() >= '0' && look() <= '9') {
      First++;
      Node *SN = getDerived().parseSourceName(State);
      if (SN == nullptr)
        return nullptr;
      return make<ConversionOperatorType>(SN);
    }
    return nullptr;
  }

  return nullptr;
}

// <ctor-dtor-name> ::= C1  # complete object constructor
//                  ::= C2  # base object constructor
//                  ::= C3  # complete object allocating constructor
//   extension      ::= C4  # gcc old-style "[unified]" constructor
//   extension      ::= C5  # the COMDAT used for ctors
//                  ::= D0  # deleting destructor
//                  ::= D1  # complete object destructor
//                  ::= D2  # base object destructor
//   extension      ::= D4  # gcc old-style "[unified]" destructor
//   extension      ::= D5  # the COMDAT used for dtors
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseCtorDtorName(Node *&SoFar,
                                                          NameState *State) {
  if (SoFar->getKind() == Node::KSpecialSubstitution) {
    // Expand the special substitution.
    SoFar = make<ExpandedSpecialSubstitution>(
        static_cast<SpecialSubstitution *>(SoFar));
    if (!SoFar)
      return nullptr;
  }

  if (consumeIf('C')) {
    bool IsInherited = consumeIf('I');
    if (look() != '1' && look() != '2' && look() != '3' && look() != '4' &&
        look() != '5')
      return nullptr;
    int Variant = look() - '0';
    ++First;
    if (State) State->CtorDtorConversion = true;
    if (IsInherited) {
      if (getDerived().parseName(State) == nullptr)
        return nullptr;
    }
    return make<CtorDtorName>(SoFar, /*IsDtor=*/false, Variant);
  }

  if (look() == 'D' && (look(1) == '0' || look(1) == '1' || look(1) == '2' ||
                        look(1) == '4' || look(1) == '5')) {
    int Variant = look(1) - '0';
    First += 2;
    if (State) State->CtorDtorConversion = true;
    return make<CtorDtorName>(SoFar, /*IsDtor=*/true, Variant);
  }

  return nullptr;
}

// <nested-name> ::= N [<CV-Qualifiers>] [<ref-qualifier>] <prefix>
// 			<unqualified-name> E
//               ::= N [<CV-Qualifiers>] [<ref-qualifier>] <template-prefix>
//               	<template-args> E
//
// <prefix> ::= <prefix> <unqualified-name>
//          ::= <template-prefix> <template-args>
//          ::= <template-param>
//          ::= <decltype>
//          ::= # empty
//          ::= <substitution>
//          ::= <prefix> <data-member-prefix>
// [*] extension
//
// <data-member-prefix> := <member source-name> [<template-args>] M
//
// <template-prefix> ::= <prefix> <template unqualified-name>
//                   ::= <template-param>
//                   ::= <substitution>
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseNestedName(NameState *State) {
  if (!consumeIf('N'))
    return nullptr;

  // 'H' specifies that the encoding that follows
  // has an explicit object parameter.
  if (!consumeIf('H')) {
    Qualifiers CVTmp = parseCVQualifiers();
    if (State)
      State->CVQualifiers = CVTmp;

    if (consumeIf('O')) {
      if (State)
        State->ReferenceQualifier = FrefQualRValue;
    } else if (consumeIf('R')) {
      if (State)
        State->ReferenceQualifier = FrefQualLValue;
    } else {
      if (State)
        State->ReferenceQualifier = FrefQualNone;
    }
  } else if (State) {
    State->HasExplicitObjectParameter = true;
  }

  Node *SoFar = nullptr;
  while (!consumeIf('E')) {
    if (State)
      // Only set end-with-template on the case that does that.
      State->EndsWithTemplateArgs = false;

    if (look() == 'T') {
      //          ::= <template-param>
      if (SoFar != nullptr)
        return nullptr; // Cannot have a prefix.
      SoFar = getDerived().parseTemplateParam();
    } else if (look() == 'I') {
      //          ::= <template-prefix> <template-args>
      if (SoFar == nullptr)
        return nullptr; // Must have a prefix.
      Node *TA = getDerived().parseTemplateArgs(State != nullptr);
      if (TA == nullptr)
        return nullptr;
      if (SoFar->getKind() == Node::KNameWithTemplateArgs)
        // Semantically <template-args> <template-args> cannot be generated by a
        // C++ entity.  There will always be [something like] a name between
        // them.
        return nullptr;
      if (State)
        State->EndsWithTemplateArgs = true;
      SoFar = make<NameWithTemplateArgs>(SoFar, TA);
    } else if (look() == 'D' && (look(1) == 't' || look(1) == 'T')) {
      //          ::= <decltype>
      if (SoFar != nullptr)
        return nullptr; // Cannot have a prefix.
      SoFar = getDerived().parseDecltype();
    } else {
      ModuleName *Module = nullptr;

      if (look() == 'S') {
        //          ::= <substitution>
        Node *S = nullptr;
        if (look(1) == 't') {
          First += 2;
          S = make<NameType>("std");
        } else {
          S = getDerived().parseSubstitution();
        }
        if (!S)
          return nullptr;
        if (S->getKind() == Node::KModuleName) {
          Module = static_cast<ModuleName *>(S);
        } else if (SoFar != nullptr) {
          return nullptr; // Cannot have a prefix.
        } else {
          SoFar = S;
          continue; // Do not push a new substitution.
        }
      }

      //          ::= [<prefix>] <unqualified-name>
      SoFar = getDerived().parseUnqualifiedName(State, SoFar, Module);
    }

    if (SoFar == nullptr)
      return nullptr;
    Subs.push_back(SoFar);

    // No longer used.
    // <data-member-prefix> := <member source-name> [<template-args>] M
    consumeIf('M');
  }

  if (SoFar == nullptr || Subs.empty())
    return nullptr;

  Subs.pop_back();
  return SoFar;
}

// <simple-id> ::= <source-name> [ <template-args> ]
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSimpleId() {
  Node *SN = getDerived().parseSourceName(/*NameState=*/nullptr);
  if (SN == nullptr)
    return nullptr;
  if (look() == 'I') {
    Node *TA = getDerived().parseTemplateArgs();
    if (TA == nullptr)
      return nullptr;
    return make<NameWithTemplateArgs>(SN, TA);
  }
  return SN;
}

// <destructor-name> ::= <unresolved-type>  # e.g., ~T or ~decltype(f())
//                   ::= <simple-id>        # e.g., ~A<2*N>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseDestructorName() {
  Node *Result;
  if (std::isdigit(look()))
    Result = getDerived().parseSimpleId();
  else
    Result = getDerived().parseUnresolvedType();
  if (Result == nullptr)
    return nullptr;
  return make<DtorName>(Result);
}

// <unresolved-type> ::= <template-param>
//                   ::= <decltype>
//                   ::= <substitution>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseUnresolvedType() {
  if (look() == 'T') {
    Node *TP = getDerived().parseTemplateParam();
    if (TP == nullptr)
      return nullptr;
    Subs.push_back(TP);
    return TP;
  }
  if (look() == 'D') {
    Node *DT = getDerived().parseDecltype();
    if (DT == nullptr)
      return nullptr;
    Subs.push_back(DT);
    return DT;
  }
  return getDerived().parseSubstitution();
}

// <base-unresolved-name> ::= <simple-id>                                # unresolved name
//          extension     ::= <operator-name>                            # unresolved operator-function-id
//          extension     ::= <operator-name> <template-args>            # unresolved operator template-id
//                        ::= on <operator-name>                         # unresolved operator-function-id
//                        ::= on <operator-name> <template-args>         # unresolved operator template-id
//                        ::= dn <destructor-name>                       # destructor or pseudo-destructor;
//                                                                         # e.g. ~X or ~X<N-1>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseBaseUnresolvedName() {
  if (std::isdigit(look()))
    return getDerived().parseSimpleId();

  if (consumeIf("dn"))
    return getDerived().parseDestructorName();

  consumeIf("on");

  Node *Oper = getDerived().parseOperatorName(/*NameState=*/nullptr);
  if (Oper == nullptr)
    return nullptr;
  if (look() == 'I') {
    Node *TA = getDerived().parseTemplateArgs();
    if (TA == nullptr)
      return nullptr;
    return make<NameWithTemplateArgs>(Oper, TA);
  }
  return Oper;
}

// <unresolved-name>
//  extension        ::= srN <unresolved-type> [<template-args>] <unresolved-qualifier-level>* E <base-unresolved-name>
//                   ::= [gs] <base-unresolved-name>                     # x or (with "gs") ::x
//                   ::= [gs] sr <unresolved-qualifier-level>+ E <base-unresolved-name>
//                                                                       # A::x, N::y, A<T>::z; "gs" means leading "::"
// [gs] has been parsed by caller.
//                   ::= sr <unresolved-type> <base-unresolved-name>     # T::x / decltype(p)::x
//  extension        ::= sr <unresolved-type> <template-args> <base-unresolved-name>
//                                                                       # T::N::x /decltype(p)::N::x
//  (ignored)        ::= srN <unresolved-type>  <unresolved-qualifier-level>+ E <base-unresolved-name>
//
// <unresolved-qualifier-level> ::= <simple-id>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseUnresolvedName(bool Global) {
  Node *SoFar = nullptr;

  // srN <unresolved-type> [<template-args>] <unresolved-qualifier-level>* E <base-unresolved-name>
  // srN <unresolved-type>                   <unresolved-qualifier-level>+ E <base-unresolved-name>
  if (consumeIf("srN")) {
    SoFar = getDerived().parseUnresolvedType();
    if (SoFar == nullptr)
      return nullptr;

    if (look() == 'I') {
      Node *TA = getDerived().parseTemplateArgs();
      if (TA == nullptr)
        return nullptr;
      SoFar = make<NameWithTemplateArgs>(SoFar, TA);
      if (!SoFar)
        return nullptr;
    }

    while (!consumeIf('E')) {
      Node *Qual = getDerived().parseSimpleId();
      if (Qual == nullptr)
        return nullptr;
      SoFar = make<QualifiedName>(SoFar, Qual);
      if (!SoFar)
        return nullptr;
    }

    Node *Base = getDerived().parseBaseUnresolvedName();
    if (Base == nullptr)
      return nullptr;
    return make<QualifiedName>(SoFar, Base);
  }

  // [gs] <base-unresolved-name>                     # x or (with "gs") ::x
  if (!consumeIf("sr")) {
    SoFar = getDerived().parseBaseUnresolvedName();
    if (SoFar == nullptr)
      return nullptr;
    if (Global)
      SoFar = make<GlobalQualifiedName>(SoFar);
    return SoFar;
  }

  // [gs] sr <unresolved-qualifier-level>+ E   <base-unresolved-name>
  if (std::isdigit(look())) {
    do {
      Node *Qual = getDerived().parseSimpleId();
      if (Qual == nullptr)
        return nullptr;
      if (SoFar)
        SoFar = make<QualifiedName>(SoFar, Qual);
      else if (Global)
        SoFar = make<GlobalQualifiedName>(Qual);
      else
        SoFar = Qual;
      if (!SoFar)
        return nullptr;
    } while (!consumeIf('E'));
  }
  //      sr <unresolved-type>                 <base-unresolved-name>
  //      sr <unresolved-type> <template-args> <base-unresolved-name>
  else {
    SoFar = getDerived().parseUnresolvedType();
    if (SoFar == nullptr)
      return nullptr;

    if (look() == 'I') {
      Node *TA = getDerived().parseTemplateArgs();
      if (TA == nullptr)
        return nullptr;
      SoFar = make<NameWithTemplateArgs>(SoFar, TA);
      if (!SoFar)
        return nullptr;
    }
  }

  DEMANGLE_ASSERT(SoFar != nullptr, "");

  Node *Base = getDerived().parseBaseUnresolvedName();
  if (Base == nullptr)
    return nullptr;
  return make<QualifiedName>(SoFar, Base);
}

// <abi-tags> ::= <abi-tag> [<abi-tags>]
// <abi-tag> ::= B <source-name>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseAbiTags(Node *N) {
  while (consumeIf('B')) {
    std::string_view SN = parseBareSourceName();
    if (SN.empty())
      return nullptr;
    N = make<AbiTagAttr>(N, SN);
    if (!N)
      return nullptr;
  }
  return N;
}

// <number> ::= [n] <non-negative decimal integer>
template <typename Alloc, typename Derived>
std::string_view
AbstractManglingParser<Alloc, Derived>::parseNumber(bool AllowNegative) {
  const char *Tmp = First;
  if (AllowNegative)
    consumeIf('n');
  if (numLeft() == 0 || !std::isdigit(*First))
    return std::string_view();
  while (numLeft() != 0 && std::isdigit(*First))
    ++First;
  return std::string_view(Tmp, First - Tmp);
}

// <positive length number> ::= [0-9]*
template <typename Alloc, typename Derived>
bool AbstractManglingParser<Alloc, Derived>::parsePositiveInteger(size_t *Out) {
  *Out = 0;
  if (look() < '0' || look() > '9')
    return true;
  while (look() >= '0' && look() <= '9') {
    *Out *= 10;
    *Out += static_cast<size_t>(consume() - '0');
  }
  return false;
}

template <typename Alloc, typename Derived>
std::string_view AbstractManglingParser<Alloc, Derived>::parseBareSourceName() {
  size_t Int = 0;
  if (parsePositiveInteger(&Int) || numLeft() < Int)
    return {};
  std::string_view R(First, Int);
  First += Int;
  return R;
}

// <function-type> ::= [<CV-qualifiers>] [<exception-spec>] [Dx] F [Y] <bare-function-type> [<ref-qualifier>] E
//
// <exception-spec> ::= Do                # non-throwing exception-specification (e.g., noexcept, throw())
//                  ::= DO <expression> E # computed (instantiation-dependent) noexcept
//                  ::= Dw <type>+ E      # dynamic exception specification with instantiation-dependent types
//
// <ref-qualifier> ::= R                   # & ref-qualifier
// <ref-qualifier> ::= O                   # && ref-qualifier
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseFunctionType() {
  Qualifiers CVQuals = parseCVQualifiers();

  Node *ExceptionSpec = nullptr;
  if (consumeIf("Do")) {
    ExceptionSpec = make<NameType>("noexcept");
    if (!ExceptionSpec)
      return nullptr;
  } else if (consumeIf("DO")) {
    Node *E = getDerived().parseExpr();
    if (E == nullptr || !consumeIf('E'))
      return nullptr;
    ExceptionSpec = make<NoexceptSpec>(E);
    if (!ExceptionSpec)
      return nullptr;
  } else if (consumeIf("Dw")) {
    size_t SpecsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *T = getDerived().parseType();
      if (T == nullptr)
        return nullptr;
      Names.push_back(T);
    }
    ExceptionSpec =
      make<DynamicExceptionSpec>(popTrailingNodeArray(SpecsBegin));
    if (!ExceptionSpec)
      return nullptr;
  }

  consumeIf("Dx"); // transaction safe

  if (!consumeIf('F'))
    return nullptr;
  consumeIf('Y'); // extern "C"
  Node *ReturnType = getDerived().parseType();
  if (ReturnType == nullptr)
    return nullptr;

  FunctionRefQual ReferenceQualifier = FrefQualNone;
  size_t ParamsBegin = Names.size();
  while (true) {
    if (consumeIf('E'))
      break;
    if (consumeIf('v'))
      continue;
    if (consumeIf("RE")) {
      ReferenceQualifier = FrefQualLValue;
      break;
    }
    if (consumeIf("OE")) {
      ReferenceQualifier = FrefQualRValue;
      break;
    }
    Node *T = getDerived().parseType();
    if (T == nullptr)
      return nullptr;
    Names.push_back(T);
  }

  NodeArray Params = popTrailingNodeArray(ParamsBegin);
  return make<FunctionType>(ReturnType, Params, CVQuals,
                            ReferenceQualifier, ExceptionSpec);
}

// extension:
// <vector-type>           ::= Dv <positive dimension number> _ <extended element type>
//                         ::= Dv [<dimension expression>] _ <element type>
// <extended element type> ::= <element type>
//                         ::= p # AltiVec vector pixel
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseVectorType() {
  if (!consumeIf("Dv"))
    return nullptr;
  if (look() >= '1' && look() <= '9') {
    Node *DimensionNumber = make<NameType>(parseNumber());
    if (!DimensionNumber)
      return nullptr;
    if (!consumeIf('_'))
      return nullptr;
    if (consumeIf('p'))
      return make<PixelVectorType>(DimensionNumber);
    Node *ElemType = getDerived().parseType();
    if (ElemType == nullptr)
      return nullptr;
    return make<VectorType>(ElemType, DimensionNumber);
  }

  if (!consumeIf('_')) {
    Node *DimExpr = getDerived().parseExpr();
    if (!DimExpr)
      return nullptr;
    if (!consumeIf('_'))
      return nullptr;
    Node *ElemType = getDerived().parseType();
    if (!ElemType)
      return nullptr;
    return make<VectorType>(ElemType, DimExpr);
  }
  Node *ElemType = getDerived().parseType();
  if (!ElemType)
    return nullptr;
  return make<VectorType>(ElemType, /*Dimension=*/nullptr);
}

// <decltype>  ::= Dt <expression> E  # decltype of an id-expression or class member access (C++0x)
//             ::= DT <expression> E  # decltype of an expression (C++0x)
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseDecltype() {
  if (!consumeIf('D'))
    return nullptr;
  if (!consumeIf('t') && !consumeIf('T'))
    return nullptr;
  Node *E = getDerived().parseExpr();
  if (E == nullptr)
    return nullptr;
  if (!consumeIf('E'))
    return nullptr;
  return make<EnclosingExpr>("decltype", E);
}

// <array-type> ::= A <positive dimension number> _ <element type>
//              ::= A [<dimension expression>] _ <element type>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseArrayType() {
  if (!consumeIf('A'))
    return nullptr;

  Node *Dimension = nullptr;

  if (std::isdigit(look())) {
    Dimension = make<NameType>(parseNumber());
    if (!Dimension)
      return nullptr;
    if (!consumeIf('_'))
      return nullptr;
  } else if (!consumeIf('_')) {
    Node *DimExpr = getDerived().parseExpr();
    if (DimExpr == nullptr)
      return nullptr;
    if (!consumeIf('_'))
      return nullptr;
    Dimension = DimExpr;
  }

  Node *Ty = getDerived().parseType();
  if (Ty == nullptr)
    return nullptr;
  return make<ArrayType>(Ty, Dimension);
}

// <pointer-to-member-type> ::= M <class type> <member type>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parsePointerToMemberType() {
  if (!consumeIf('M'))
    return nullptr;
  Node *ClassType = getDerived().parseType();
  if (ClassType == nullptr)
    return nullptr;
  Node *MemberType = getDerived().parseType();
  if (MemberType == nullptr)
    return nullptr;
  return make<PointerToMemberType>(ClassType, MemberType);
}

// <class-enum-type> ::= <name>     # non-dependent type name, dependent type name, or dependent typename-specifier
//                   ::= Ts <name>  # dependent elaborated type specifier using 'struct' or 'class'
//                   ::= Tu <name>  # dependent elaborated type specifier using 'union'
//                   ::= Te <name>  # dependent elaborated type specifier using 'enum'
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseClassEnumType() {
  std::string_view ElabSpef;
  if (consumeIf("Ts"))
    ElabSpef = "struct";
  else if (consumeIf("Tu"))
    ElabSpef = "union";
  else if (consumeIf("Te"))
    ElabSpef = "enum";

  Node *Name = getDerived().parseName();
  if (Name == nullptr)
    return nullptr;

  if (!ElabSpef.empty())
    return make<ElaboratedTypeSpefType>(ElabSpef, Name);

  return Name;
}

// <qualified-type>     ::= <qualifiers> <type>
// <qualifiers> ::= <extended-qualifier>* <CV-qualifiers>
// <extended-qualifier> ::= U <source-name> [<template-args>] # vendor extended type qualifier
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseQualifiedType() {
  if (consumeIf('U')) {
    std::string_view Qual = parseBareSourceName();
    if (Qual.empty())
      return nullptr;

    // extension            ::= U <objc-name> <objc-type>  # objc-type<identifier>
    if (starts_with(Qual, "objcproto")) {
      constexpr size_t Len = sizeof("objcproto") - 1;
      std::string_view ProtoSourceName(Qual.data() + Len, Qual.size() - Len);
      std::string_view Proto;
      {
        ScopedOverride<const char *> SaveFirst(First, ProtoSourceName.data()),
            SaveLast(Last, &*ProtoSourceName.rbegin() + 1);
        Proto = parseBareSourceName();
      }
      if (Proto.empty())
        return nullptr;
      Node *Child = getDerived().parseQualifiedType();
      if (Child == nullptr)
        return nullptr;
      return make<ObjCProtoName>(Child, Proto);
    }

    Node *TA = nullptr;
    if (look() == 'I') {
      TA = getDerived().parseTemplateArgs();
      if (TA == nullptr)
        return nullptr;
    }

    Node *Child = getDerived().parseQualifiedType();
    if (Child == nullptr)
      return nullptr;
    return make<VendorExtQualType>(Child, Qual, TA);
  }

  Qualifiers Quals = parseCVQualifiers();
  Node *Ty = getDerived().parseType();
  if (Ty == nullptr)
    return nullptr;
  if (Quals != QualNone)
    Ty = make<QualType>(Ty, Quals);
  return Ty;
}

// <type>      ::= <builtin-type>
//             ::= <qualified-type>
//             ::= <function-type>
//             ::= <class-enum-type>
//             ::= <array-type>
//             ::= <pointer-to-member-type>
//             ::= <template-param>
//             ::= <template-template-param> <template-args>
//             ::= <decltype>
//             ::= P <type>        # pointer
//             ::= R <type>        # l-value reference
//             ::= O <type>        # r-value reference (C++11)
//             ::= C <type>        # complex pair (C99)
//             ::= G <type>        # imaginary (C99)
//             ::= <substitution>  # See Compression below
// extension   ::= U <objc-name> <objc-type>  # objc-type<identifier>
// extension   ::= <vector-type> # <vector-type> starts with Dv
//
// <objc-name> ::= <k0 number> objcproto <k1 number> <identifier>  # k0 = 9 + <number of digits in k1> + k1
// <objc-type> ::= <source-name>  # PU<11+>objcproto 11objc_object<source-name> 11objc_object -> id<source-name>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseType() {
  Node *Result = nullptr;

  switch (look()) {
  //             ::= <qualified-type>
  case 'r':
  case 'V':
  case 'K': {
    unsigned AfterQuals = 0;
    if (look(AfterQuals) == 'r') ++AfterQuals;
    if (look(AfterQuals) == 'V') ++AfterQuals;
    if (look(AfterQuals) == 'K') ++AfterQuals;

    if (look(AfterQuals) == 'F' ||
        (look(AfterQuals) == 'D' &&
         (look(AfterQuals + 1) == 'o' || look(AfterQuals + 1) == 'O' ||
          look(AfterQuals + 1) == 'w' || look(AfterQuals + 1) == 'x'))) {
      Result = getDerived().parseFunctionType();
      break;
    }
    DEMANGLE_FALLTHROUGH;
  }
  case 'U': {
    Result = getDerived().parseQualifiedType();
    break;
  }
  // <builtin-type> ::= v    # void
  case 'v':
    ++First;
    return make<NameType>("void");
  //                ::= w    # wchar_t
  case 'w':
    ++First;
    return make<NameType>("wchar_t");
  //                ::= b    # bool
  case 'b':
    ++First;
    return make<NameType>("bool");
  //                ::= c    # char
  case 'c':
    ++First;
    return make<NameType>("char");
  //                ::= a    # signed char
  case 'a':
    ++First;
    return make<NameType>("signed char");
  //                ::= h    # unsigned char
  case 'h':
    ++First;
    return make<NameType>("unsigned char");
  //                ::= s    # short
  case 's':
    ++First;
    return make<NameType>("short");
  //                ::= t    # unsigned short
  case 't':
    ++First;
    return make<NameType>("unsigned short");
  //                ::= i    # int
  case 'i':
    ++First;
    return make<NameType>("int");
  //                ::= j    # unsigned int
  case 'j':
    ++First;
    return make<NameType>("unsigned int");
  //                ::= l    # long
  case 'l':
    ++First;
    return make<NameType>("long");
  //                ::= m    # unsigned long
  case 'm':
    ++First;
    return make<NameType>("unsigned long");
  //                ::= x    # long long, __int64
  case 'x':
    ++First;
    return make<NameType>("long long");
  //                ::= y    # unsigned long long, __int64
  case 'y':
    ++First;
    return make<NameType>("unsigned long long");
  //                ::= n    # __int128
  case 'n':
    ++First;
    return make<NameType>("__int128");
  //                ::= o    # unsigned __int128
  case 'o':
    ++First;
    return make<NameType>("unsigned __int128");
  //                ::= f    # float
  case 'f':
    ++First;
    return make<NameType>("float");
  //                ::= d    # double
  case 'd':
    ++First;
    return make<NameType>("double");
  //                ::= e    # long double, __float80
  case 'e':
    ++First;
    return make<NameType>("long double");
  //                ::= g    # __float128
  case 'g':
    ++First;
    return make<NameType>("__float128");
  //                ::= z    # ellipsis
  case 'z':
    ++First;
    return make<NameType>("...");

  // <builtin-type> ::= u <source-name>    # vendor extended type
  case 'u': {
    ++First;
    std::string_view Res = parseBareSourceName();
    if (Res.empty())
      return nullptr;
    // Typically, <builtin-type>s are not considered substitution candidates,
    // but the exception to that exception is vendor extended types (Itanium C++
    // ABI 5.9.1).
    if (consumeIf('I')) {
      Node *BaseType = parseType();
      if (BaseType == nullptr)
        return nullptr;
      if (!consumeIf('E'))
        return nullptr;
      Result = make<TransformedType>(Res, BaseType);
    } else
      Result = make<NameType>(Res);
    break;
  }
  case 'D':
    switch (look(1)) {
    //                ::= Dd   # IEEE 754r decimal floating point (64 bits)
    case 'd':
      First += 2;
      return make<NameType>("decimal64");
    //                ::= De   # IEEE 754r decimal floating point (128 bits)
    case 'e':
      First += 2;
      return make<NameType>("decimal128");
    //                ::= Df   # IEEE 754r decimal floating point (32 bits)
    case 'f':
      First += 2;
      return make<NameType>("decimal32");
    //                ::= Dh   # IEEE 754r half-precision floating point (16 bits)
    case 'h':
      First += 2;
      return make<NameType>("half");
    //                ::= DF <number> _ # ISO/IEC TS 18661 binary floating point (N bits)
    case 'F': {
      First += 2;
      Node *DimensionNumber = make<NameType>(parseNumber());
      if (!DimensionNumber)
        return nullptr;
      if (!consumeIf('_'))
        return nullptr;
      return make<BinaryFPType>(DimensionNumber);
    }
    //                ::= DB <number> _                             # C23 signed _BitInt(N)
    //                ::= DB <instantiation-dependent expression> _ # C23 signed _BitInt(N)
    //                ::= DU <number> _                             # C23 unsigned _BitInt(N)
    //                ::= DU <instantiation-dependent expression> _ # C23 unsigned _BitInt(N)
    case 'B':
    case 'U': {
      bool Signed = look(1) == 'B';
      First += 2;
      Node *Size = std::isdigit(look()) ? make<NameType>(parseNumber())
                                        : getDerived().parseExpr();
      if (!Size)
        return nullptr;
      if (!consumeIf('_'))
        return nullptr;
      return make<BitIntType>(Size, Signed);
    }
    //                ::= Di   # char32_t
    case 'i':
      First += 2;
      return make<NameType>("char32_t");
    //                ::= Ds   # char16_t
    case 's':
      First += 2;
      return make<NameType>("char16_t");
    //                ::= Du   # char8_t (C++2a, not yet in the Itanium spec)
    case 'u':
      First += 2;
      return make<NameType>("char8_t");
    //                ::= Da   # auto (in dependent new-expressions)
    case 'a':
      First += 2;
      return make<NameType>("auto");
    //                ::= Dc   # decltype(auto)
    case 'c':
      First += 2;
      return make<NameType>("decltype(auto)");
    //                ::= Dk <type-constraint> # constrained auto
    //                ::= DK <type-constraint> # constrained decltype(auto)
    case 'k':
    case 'K': {
      std::string_view Kind = look(1) == 'k' ? " auto" : " decltype(auto)";
      First += 2;
      Node *Constraint = getDerived().parseName();
      if (!Constraint)
        return nullptr;
      return make<PostfixQualifiedType>(Constraint, Kind);
    }
    //                ::= Dn   # std::nullptr_t (i.e., decltype(nullptr))
    case 'n':
      First += 2;
      return make<NameType>("std::nullptr_t");

    //             ::= <decltype>
    case 't':
    case 'T': {
      Result = getDerived().parseDecltype();
      break;
    }
    // extension   ::= <vector-type> # <vector-type> starts with Dv
    case 'v': {
      Result = getDerived().parseVectorType();
      break;
    }
    //           ::= Dp <type>       # pack expansion (C++0x)
    case 'p': {
      First += 2;
      Node *Child = getDerived().parseType();
      if (!Child)
        return nullptr;
      Result = make<ParameterPackExpansion>(Child);
      break;
    }
    // Exception specifier on a function type.
    case 'o':
    case 'O':
    case 'w':
    // Transaction safe function type.
    case 'x':
      Result = getDerived().parseFunctionType();
      break;
    }
    break;
  //             ::= <function-type>
  case 'F': {
    Result = getDerived().parseFunctionType();
    break;
  }
  //             ::= <array-type>
  case 'A': {
    Result = getDerived().parseArrayType();
    break;
  }
  //             ::= <pointer-to-member-type>
  case 'M': {
    Result = getDerived().parsePointerToMemberType();
    break;
  }
  //             ::= <template-param>
  case 'T': {
    // This could be an elaborate type specifier on a <class-enum-type>.
    if (look(1) == 's' || look(1) == 'u' || look(1) == 'e') {
      Result = getDerived().parseClassEnumType();
      break;
    }

    Result = getDerived().parseTemplateParam();
    if (Result == nullptr)
      return nullptr;

    // Result could be either of:
    //   <type>        ::= <template-param>
    //   <type>        ::= <template-template-param> <template-args>
    //
    //   <template-template-param> ::= <template-param>
    //                             ::= <substitution>
    //
    // If this is followed by some <template-args>, and we're permitted to
    // parse them, take the second production.

    if (TryToParseTemplateArgs && look() == 'I') {
      Node *TA = getDerived().parseTemplateArgs();
      if (TA == nullptr)
        return nullptr;
      Result = make<NameWithTemplateArgs>(Result, TA);
    }
    break;
  }
  //             ::= P <type>        # pointer
  case 'P': {
    ++First;
    Node *Ptr = getDerived().parseType();
    if (Ptr == nullptr)
      return nullptr;
    Result = make<PointerType>(Ptr);
    break;
  }
  //             ::= R <type>        # l-value reference
  case 'R': {
    ++First;
    Node *Ref = getDerived().parseType();
    if (Ref == nullptr)
      return nullptr;
    Result = make<ReferenceType>(Ref, ReferenceKind::LValue);
    break;
  }
  //             ::= O <type>        # r-value reference (C++11)
  case 'O': {
    ++First;
    Node *Ref = getDerived().parseType();
    if (Ref == nullptr)
      return nullptr;
    Result = make<ReferenceType>(Ref, ReferenceKind::RValue);
    break;
  }
  //             ::= C <type>        # complex pair (C99)
  case 'C': {
    ++First;
    Node *P = getDerived().parseType();
    if (P == nullptr)
      return nullptr;
    Result = make<PostfixQualifiedType>(P, " complex");
    break;
  }
  //             ::= G <type>        # imaginary (C99)
  case 'G': {
    ++First;
    Node *P = getDerived().parseType();
    if (P == nullptr)
      return P;
    Result = make<PostfixQualifiedType>(P, " imaginary");
    break;
  }
  //             ::= <substitution>  # See Compression below
  case 'S': {
    if (look(1) != 't') {
      bool IsSubst = false;
      Result = getDerived().parseUnscopedName(nullptr, &IsSubst);
      if (!Result)
        return nullptr;

      // Sub could be either of:
      //   <type>        ::= <substitution>
      //   <type>        ::= <template-template-param> <template-args>
      //
      //   <template-template-param> ::= <template-param>
      //                             ::= <substitution>
      //
      // If this is followed by some <template-args>, and we're permitted to
      // parse them, take the second production.

      if (look() == 'I' && (!IsSubst || TryToParseTemplateArgs)) {
        if (!IsSubst)
          Subs.push_back(Result);
        Node *TA = getDerived().parseTemplateArgs();
        if (TA == nullptr)
          return nullptr;
        Result = make<NameWithTemplateArgs>(Result, TA);
      } else if (IsSubst) {
        // If all we parsed was a substitution, don't re-insert into the
        // substitution table.
        return Result;
      }
      break;
    }
    DEMANGLE_FALLTHROUGH;
  }
  //        ::= <class-enum-type>
  default: {
    Result = getDerived().parseClassEnumType();
    break;
  }
  }

  // If we parsed a type, insert it into the substitution table. Note that all
  // <builtin-type>s and <substitution>s have already bailed out, because they
  // don't get substitutions.
  if (Result != nullptr)
    Subs.push_back(Result);
  return Result;
}

template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parsePrefixExpr(std::string_view Kind,
                                                        Node::Prec Prec) {
  Node *E = getDerived().parseExpr();
  if (E == nullptr)
    return nullptr;
  return make<PrefixExpr>(Kind, E, Prec);
}

template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseBinaryExpr(std::string_view Kind,
                                                        Node::Prec Prec) {
  Node *LHS = getDerived().parseExpr();
  if (LHS == nullptr)
    return nullptr;
  Node *RHS = getDerived().parseExpr();
  if (RHS == nullptr)
    return nullptr;
  return make<BinaryExpr>(LHS, Kind, RHS, Prec);
}

template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseIntegerLiteral(
    std::string_view Lit) {
  std::string_view Tmp = parseNumber(true);
  if (!Tmp.empty() && consumeIf('E'))
    return make<IntegerLiteral>(Lit, Tmp);
  return nullptr;
}

// <CV-Qualifiers> ::= [r] [V] [K]
template <typename Alloc, typename Derived>
Qualifiers AbstractManglingParser<Alloc, Derived>::parseCVQualifiers() {
  Qualifiers CVR = QualNone;
  if (consumeIf('r'))
    CVR |= QualRestrict;
  if (consumeIf('V'))
    CVR |= QualVolatile;
  if (consumeIf('K'))
    CVR |= QualConst;
  return CVR;
}

// <function-param> ::= fp <top-level CV-Qualifiers> _                                     # L == 0, first parameter
//                  ::= fp <top-level CV-Qualifiers> <parameter-2 non-negative number> _   # L == 0, second and later parameters
//                  ::= fL <L-1 non-negative number> p <top-level CV-Qualifiers> _         # L > 0, first parameter
//                  ::= fL <L-1 non-negative number> p <top-level CV-Qualifiers> <parameter-2 non-negative number> _   # L > 0, second and later parameters
//                  ::= fpT      # 'this' expression (not part of standard?)
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseFunctionParam() {
  if (consumeIf("fpT"))
    return make<NameType>("this");
  if (consumeIf("fp")) {
    parseCVQualifiers();
    std::string_view Num = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<FunctionParam>(Num);
  }
  if (consumeIf("fL")) {
    if (parseNumber().empty())
      return nullptr;
    if (!consumeIf('p'))
      return nullptr;
    parseCVQualifiers();
    std::string_view Num = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<FunctionParam>(Num);
  }
  return nullptr;
}

// cv <type> <expression>                               # conversion with one argument
// cv <type> _ <expression>* E                          # conversion with a different number of arguments
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseConversionExpr() {
  if (!consumeIf("cv"))
    return nullptr;
  Node *Ty;
  {
    ScopedOverride<bool> SaveTemp(TryToParseTemplateArgs, false);
    Ty = getDerived().parseType();
  }

  if (Ty == nullptr)
    return nullptr;

  if (consumeIf('_')) {
    size_t ExprsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *E = getDerived().parseExpr();
      if (E == nullptr)
        return E;
      Names.push_back(E);
    }
    NodeArray Exprs = popTrailingNodeArray(ExprsBegin);
    return make<ConversionExpr>(Ty, Exprs);
  }

  Node *E[1] = {getDerived().parseExpr()};
  if (E[0] == nullptr)
    return nullptr;
  return make<ConversionExpr>(Ty, makeNodeArray(E, E + 1));
}

// <expr-primary> ::= L <type> <value number> E                          # integer literal
//                ::= L <type> <value float> E                           # floating literal
//                ::= L <string type> E                                  # string literal
//                ::= L <nullptr type> E                                 # nullptr literal (i.e., "LDnE")
//                ::= L <lambda type> E                                  # lambda expression
// FIXME:         ::= L <type> <real-part float> _ <imag-part float> E   # complex floating point literal (C 2000)
//                ::= L <mangled-name> E                                 # external name
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseExprPrimary() {
  if (!consumeIf('L'))
    return nullptr;
  switch (look()) {
  case 'w':
    ++First;
    return getDerived().parseIntegerLiteral("wchar_t");
  case 'b':
    if (consumeIf("b0E"))
      return make<BoolExpr>(0);
    if (consumeIf("b1E"))
      return make<BoolExpr>(1);
    return nullptr;
  case 'c':
    ++First;
    return getDerived().parseIntegerLiteral("char");
  case 'a':
    ++First;
    return getDerived().parseIntegerLiteral("signed char");
  case 'h':
    ++First;
    return getDerived().parseIntegerLiteral("unsigned char");
  case 's':
    ++First;
    return getDerived().parseIntegerLiteral("short");
  case 't':
    ++First;
    return getDerived().parseIntegerLiteral("unsigned short");
  case 'i':
    ++First;
    return getDerived().parseIntegerLiteral("");
  case 'j':
    ++First;
    return getDerived().parseIntegerLiteral("u");
  case 'l':
    ++First;
    return getDerived().parseIntegerLiteral("l");
  case 'm':
    ++First;
    return getDerived().parseIntegerLiteral("ul");
  case 'x':
    ++First;
    return getDerived().parseIntegerLiteral("ll");
  case 'y':
    ++First;
    return getDerived().parseIntegerLiteral("ull");
  case 'n':
    ++First;
    return getDerived().parseIntegerLiteral("__int128");
  case 'o':
    ++First;
    return getDerived().parseIntegerLiteral("unsigned __int128");
  case 'f':
    ++First;
    return getDerived().template parseFloatingLiteral<float>();
  case 'd':
    ++First;
    return getDerived().template parseFloatingLiteral<double>();
  case 'e':
    ++First;
#if defined(__powerpc__) || defined(__s390__)
    // Handle cases where long doubles encoded with e have the same size
    // and representation as doubles.
    return getDerived().template parseFloatingLiteral<double>();
#else
    return getDerived().template parseFloatingLiteral<long double>();
#endif
  case '_':
    if (consumeIf("_Z")) {
      Node *R = getDerived().parseEncoding();
      if (R != nullptr && consumeIf('E'))
        return R;
    }
    return nullptr;
  case 'A': {
    Node *T = getDerived().parseType();
    if (T == nullptr)
      return nullptr;
    // FIXME: We need to include the string contents in the mangling.
    if (consumeIf('E'))
      return make<StringLiteral>(T);
    return nullptr;
  }
  case 'D':
    if (consumeIf("Dn") && (consumeIf('0'), consumeIf('E')))
      return make<NameType>("nullptr");
    return nullptr;
  case 'T':
    // Invalid mangled name per
    //   http://sourcerytools.com/pipermail/cxx-abi-dev/2011-August/002422.html
    return nullptr;
  case 'U': {
    // FIXME: Should we support LUb... for block literals?
    if (look(1) != 'l')
      return nullptr;
    Node *T = parseUnnamedTypeName(nullptr);
    if (!T || !consumeIf('E'))
      return nullptr;
    return make<LambdaExpr>(T);
  }
  default: {
    // might be named type
    Node *T = getDerived().parseType();
    if (T == nullptr)
      return nullptr;
    std::string_view N = parseNumber(/*AllowNegative=*/true);
    if (N.empty())
      return nullptr;
    if (!consumeIf('E'))
      return nullptr;
    return make<EnumLiteral>(T, N);
  }
  }
}

// <braced-expression> ::= <expression>
//                     ::= di <field source-name> <braced-expression>    # .name = expr
//                     ::= dx <index expression> <braced-expression>     # [expr] = expr
//                     ::= dX <range begin expression> <range end expression> <braced-expression>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseBracedExpr() {
  if (look() == 'd') {
    switch (look(1)) {
    case 'i': {
      First += 2;
      Node *Field = getDerived().parseSourceName(/*NameState=*/nullptr);
      if (Field == nullptr)
        return nullptr;
      Node *Init = getDerived().parseBracedExpr();
      if (Init == nullptr)
        return nullptr;
      return make<BracedExpr>(Field, Init, /*isArray=*/false);
    }
    case 'x': {
      First += 2;
      Node *Index = getDerived().parseExpr();
      if (Index == nullptr)
        return nullptr;
      Node *Init = getDerived().parseBracedExpr();
      if (Init == nullptr)
        return nullptr;
      return make<BracedExpr>(Index, Init, /*isArray=*/true);
    }
    case 'X': {
      First += 2;
      Node *RangeBegin = getDerived().parseExpr();
      if (RangeBegin == nullptr)
        return nullptr;
      Node *RangeEnd = getDerived().parseExpr();
      if (RangeEnd == nullptr)
        return nullptr;
      Node *Init = getDerived().parseBracedExpr();
      if (Init == nullptr)
        return nullptr;
      return make<BracedRangeExpr>(RangeBegin, RangeEnd, Init);
    }
    }
  }
  return getDerived().parseExpr();
}

// (not yet in the spec)
// <fold-expr> ::= fL <binary-operator-name> <expression> <expression>
//             ::= fR <binary-operator-name> <expression> <expression>
//             ::= fl <binary-operator-name> <expression>
//             ::= fr <binary-operator-name> <expression>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseFoldExpr() {
  if (!consumeIf('f'))
    return nullptr;

  bool IsLeftFold = false, HasInitializer = false;
  switch (look()) {
  default:
    return nullptr;
  case 'L':
    IsLeftFold = true;
    HasInitializer = true;
    break;
  case 'R':
    HasInitializer = true;
    break;
  case 'l':
    IsLeftFold = true;
    break;
  case 'r':
    break;
  }
  ++First;

  const auto *Op = parseOperatorEncoding();
  if (!Op)
    return nullptr;
  if (!(Op->getKind() == OperatorInfo::Binary
        || (Op->getKind() == OperatorInfo::Member
            && Op->getName().back() == '*')))
    return nullptr;

  Node *Pack = getDerived().parseExpr();
  if (Pack == nullptr)
    return nullptr;

  Node *Init = nullptr;
  if (HasInitializer) {
    Init = getDerived().parseExpr();
    if (Init == nullptr)
      return nullptr;
  }

  if (IsLeftFold && Init)
    std::swap(Pack, Init);

  return make<FoldExpr>(IsLeftFold, Op->getSymbol(), Pack, Init);
}

// <expression> ::= mc <parameter type> <expr> [<offset number>] E
//
// Not yet in the spec: https://github.com/itanium-cxx-abi/cxx-abi/issues/47
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parsePointerToMemberConversionExpr(
    Node::Prec Prec) {
  Node *Ty = getDerived().parseType();
  if (!Ty)
    return nullptr;
  Node *Expr = getDerived().parseExpr();
  if (!Expr)
    return nullptr;
  std::string_view Offset = getDerived().parseNumber(true);
  if (!consumeIf('E'))
    return nullptr;
  return make<PointerToMemberConversionExpr>(Ty, Expr, Offset, Prec);
}

// <expression> ::= so <referent type> <expr> [<offset number>] <union-selector>* [p] E
// <union-selector> ::= _ [<number>]
//
// Not yet in the spec: https://github.com/itanium-cxx-abi/cxx-abi/issues/47
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSubobjectExpr() {
  Node *Ty = getDerived().parseType();
  if (!Ty)
    return nullptr;
  Node *Expr = getDerived().parseExpr();
  if (!Expr)
    return nullptr;
  std::string_view Offset = getDerived().parseNumber(true);
  size_t SelectorsBegin = Names.size();
  while (consumeIf('_')) {
    Node *Selector = make<NameType>(parseNumber());
    if (!Selector)
      return nullptr;
    Names.push_back(Selector);
  }
  bool OnePastTheEnd = consumeIf('p');
  if (!consumeIf('E'))
    return nullptr;
  return make<SubobjectExpr>(
      Ty, Expr, Offset, popTrailingNodeArray(SelectorsBegin), OnePastTheEnd);
}

template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseConstraintExpr() {
  // Within this expression, all enclosing template parameter lists are in
  // scope.
  ScopedOverride<bool> SaveInConstraintExpr(InConstraintExpr, true);
  return getDerived().parseExpr();
}

template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseRequiresExpr() {
  NodeArray Params;
  if (consumeIf("rQ")) {
    // <expression> ::= rQ <bare-function-type> _ <requirement>+ E
    size_t ParamsBegin = Names.size();
    while (!consumeIf('_')) {
      Node *Type = getDerived().parseType();
      if (Type == nullptr)
        return nullptr;
      Names.push_back(Type);
    }
    Params = popTrailingNodeArray(ParamsBegin);
  } else if (!consumeIf("rq")) {
    // <expression> ::= rq <requirement>+ E
    return nullptr;
  }

  size_t ReqsBegin = Names.size();
  do {
    Node *Constraint = nullptr;
    if (consumeIf('X')) {
      // <requirement> ::= X <expression> [N] [R <type-constraint>]
      Node *Expr = getDerived().parseExpr();
      if (Expr == nullptr)
        return nullptr;
      bool Noexcept = consumeIf('N');
      Node *TypeReq = nullptr;
      if (consumeIf('R')) {
        TypeReq = getDerived().parseName();
        if (TypeReq == nullptr)
          return nullptr;
      }
      Constraint = make<ExprRequirement>(Expr, Noexcept, TypeReq);
    } else if (consumeIf('T')) {
      // <requirement> ::= T <type>
      Node *Type = getDerived().parseType();
      if (Type == nullptr)
        return nullptr;
      Constraint = make<TypeRequirement>(Type);
    } else if (consumeIf('Q')) {
      // <requirement> ::= Q <constraint-expression>
      //
      // FIXME: We use <expression> instead of <constraint-expression>. Either
      // the requires expression is already inside a constraint expression, in
      // which case it makes no difference, or we're in a requires-expression
      // that might be partially-substituted, where the language behavior is
      // not yet settled and clang mangles after substitution.
      Node *NestedReq = getDerived().parseExpr();
      if (NestedReq == nullptr)
        return nullptr;
      Constraint = make<NestedRequirement>(NestedReq);
    }
    if (Constraint == nullptr)
      return nullptr;
    Names.push_back(Constraint);
  } while (!consumeIf('E'));

  return make<RequiresExpr>(Params, popTrailingNodeArray(ReqsBegin));
}

// <expression> ::= <unary operator-name> <expression>
//              ::= <binary operator-name> <expression> <expression>
//              ::= <ternary operator-name> <expression> <expression> <expression>
//              ::= cl <expression>+ E                                   # call
//              ::= cv <type> <expression>                               # conversion with one argument
//              ::= cv <type> _ <expression>* E                          # conversion with a different number of arguments
//              ::= [gs] nw <expression>* _ <type> E                     # new (expr-list) type
//              ::= [gs] nw <expression>* _ <type> <initializer>         # new (expr-list) type (init)
//              ::= [gs] na <expression>* _ <type> E                     # new[] (expr-list) type
//              ::= [gs] na <expression>* _ <type> <initializer>         # new[] (expr-list) type (init)
//              ::= [gs] dl <expression>                                 # delete expression
//              ::= [gs] da <expression>                                 # delete[] expression
//              ::= pp_ <expression>                                     # prefix ++
//              ::= mm_ <expression>                                     # prefix --
//              ::= ti <type>                                            # typeid (type)
//              ::= te <expression>                                      # typeid (expression)
//              ::= dc <type> <expression>                               # dynamic_cast<type> (expression)
//              ::= sc <type> <expression>                               # static_cast<type> (expression)
//              ::= cc <type> <expression>                               # const_cast<type> (expression)
//              ::= rc <type> <expression>                               # reinterpret_cast<type> (expression)
//              ::= st <type>                                            # sizeof (a type)
//              ::= sz <expression>                                      # sizeof (an expression)
//              ::= at <type>                                            # alignof (a type)
//              ::= az <expression>                                      # alignof (an expression)
//              ::= nx <expression>                                      # noexcept (expression)
//              ::= <template-param>
//              ::= <function-param>
//              ::= dt <expression> <unresolved-name>                    # expr.name
//              ::= pt <expression> <unresolved-name>                    # expr->name
//              ::= ds <expression> <expression>                         # expr.*expr
//              ::= sZ <template-param>                                  # size of a parameter pack
//              ::= sZ <function-param>                                  # size of a function parameter pack
//              ::= sP <template-arg>* E                                 # sizeof...(T), size of a captured template parameter pack from an alias template
//              ::= sp <expression>                                      # pack expansion
//              ::= tw <expression>                                      # throw expression
//              ::= tr                                                   # throw with no operand (rethrow)
//              ::= <unresolved-name>                                    # f(p), N::f(p), ::f(p),
//                                                                       # freestanding dependent name (e.g., T::x),
//                                                                       # objectless nonstatic member reference
//              ::= fL <binary-operator-name> <expression> <expression>
//              ::= fR <binary-operator-name> <expression> <expression>
//              ::= fl <binary-operator-name> <expression>
//              ::= fr <binary-operator-name> <expression>
//              ::= <expr-primary>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseExpr() {
  bool Global = consumeIf("gs");

  const auto *Op = parseOperatorEncoding();
  if (Op) {
    auto Sym = Op->getSymbol();
    switch (Op->getKind()) {
    case OperatorInfo::Binary:
      // Binary operator: lhs @ rhs
      return getDerived().parseBinaryExpr(Sym, Op->getPrecedence());
    case OperatorInfo::Prefix:
      // Prefix unary operator: @ expr
      return getDerived().parsePrefixExpr(Sym, Op->getPrecedence());
    case OperatorInfo::Postfix: {
      // Postfix unary operator: expr @
      if (consumeIf('_'))
        return getDerived().parsePrefixExpr(Sym, Op->getPrecedence());
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return nullptr;
      return make<PostfixExpr>(Ex, Sym, Op->getPrecedence());
    }
    case OperatorInfo::Array: {
      // Array Index:  lhs [ rhs ]
      Node *Base = getDerived().parseExpr();
      if (Base == nullptr)
        return nullptr;
      Node *Index = getDerived().parseExpr();
      if (Index == nullptr)
        return nullptr;
      return make<ArraySubscriptExpr>(Base, Index, Op->getPrecedence());
    }
    case OperatorInfo::Member: {
      // Member access lhs @ rhs
      Node *LHS = getDerived().parseExpr();
      if (LHS == nullptr)
        return nullptr;
      Node *RHS = getDerived().parseExpr();
      if (RHS == nullptr)
        return nullptr;
      return make<MemberExpr>(LHS, Sym, RHS, Op->getPrecedence());
    }
    case OperatorInfo::New: {
      // New
      // # new (expr-list) type [(init)]
      // [gs] nw <expression>* _ <type> [pi <expression>*] E
      // # new[] (expr-list) type [(init)]
      // [gs] na <expression>* _ <type> [pi <expression>*] E
      size_t Exprs = Names.size();
      while (!consumeIf('_')) {
        Node *Ex = getDerived().parseExpr();
        if (Ex == nullptr)
          return nullptr;
        Names.push_back(Ex);
      }
      NodeArray ExprList = popTrailingNodeArray(Exprs);
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      bool HaveInits = consumeIf("pi");
      size_t InitsBegin = Names.size();
      while (!consumeIf('E')) {
        if (!HaveInits)
          return nullptr;
        Node *Init = getDerived().parseExpr();
        if (Init == nullptr)
          return Init;
        Names.push_back(Init);
      }
      NodeArray Inits = popTrailingNodeArray(InitsBegin);
      return make<NewExpr>(ExprList, Ty, Inits, Global,
                           /*IsArray=*/Op->getFlag(), Op->getPrecedence());
    }
    case OperatorInfo::Del: {
      // Delete
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return nullptr;
      return make<DeleteExpr>(Ex, Global, /*IsArray=*/Op->getFlag(),
                              Op->getPrecedence());
    }
    case OperatorInfo::Call: {
      // Function Call
      Node *Callee = getDerived().parseExpr();
      if (Callee == nullptr)
        return nullptr;
      size_t ExprsBegin = Names.size();
      while (!consumeIf('E')) {
        Node *E = getDerived().parseExpr();
        if (E == nullptr)
          return nullptr;
        Names.push_back(E);
      }
      return make<CallExpr>(Callee, popTrailingNodeArray(ExprsBegin),
                            Op->getPrecedence());
    }
    case OperatorInfo::CCast: {
      // C Cast: (type)expr
      Node *Ty;
      {
        ScopedOverride<bool> SaveTemp(TryToParseTemplateArgs, false);
        Ty = getDerived().parseType();
      }
      if (Ty == nullptr)
        return nullptr;

      size_t ExprsBegin = Names.size();
      bool IsMany = consumeIf('_');
      while (!consumeIf('E')) {
        Node *E = getDerived().parseExpr();
        if (E == nullptr)
          return E;
        Names.push_back(E);
        if (!IsMany)
          break;
      }
      NodeArray Exprs = popTrailingNodeArray(ExprsBegin);
      if (!IsMany && Exprs.size() != 1)
        return nullptr;
      return make<ConversionExpr>(Ty, Exprs, Op->getPrecedence());
    }
    case OperatorInfo::Conditional: {
      // Conditional operator: expr ? expr : expr
      Node *Cond = getDerived().parseExpr();
      if (Cond == nullptr)
        return nullptr;
      Node *LHS = getDerived().parseExpr();
      if (LHS == nullptr)
        return nullptr;
      Node *RHS = getDerived().parseExpr();
      if (RHS == nullptr)
        return nullptr;
      return make<ConditionalExpr>(Cond, LHS, RHS, Op->getPrecedence());
    }
    case OperatorInfo::NamedCast: {
      // Named cast operation, @<type>(expr)
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return nullptr;
      return make<CastExpr>(Sym, Ty, Ex, Op->getPrecedence());
    }
    case OperatorInfo::OfIdOp: {
      // [sizeof/alignof/typeid] ( <type>|<expr> )
      Node *Arg =
          Op->getFlag() ? getDerived().parseType() : getDerived().parseExpr();
      if (!Arg)
        return nullptr;
      return make<EnclosingExpr>(Sym, Arg, Op->getPrecedence());
    }
    case OperatorInfo::NameOnly: {
      // Not valid as an expression operand.
      return nullptr;
    }
    }
    DEMANGLE_UNREACHABLE;
  }

  if (numLeft() < 2)
    return nullptr;

  if (look() == 'L')
    return getDerived().parseExprPrimary();
  if (look() == 'T')
    return getDerived().parseTemplateParam();
  if (look() == 'f') {
    // Disambiguate a fold expression from a <function-param>.
    if (look(1) == 'p' || (look(1) == 'L' && std::isdigit(look(2))))
      return getDerived().parseFunctionParam();
    return getDerived().parseFoldExpr();
  }
  if (consumeIf("il")) {
    size_t InitsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *E = getDerived().parseBracedExpr();
      if (E == nullptr)
        return nullptr;
      Names.push_back(E);
    }
    return make<InitListExpr>(nullptr, popTrailingNodeArray(InitsBegin));
  }
  if (consumeIf("mc"))
    return parsePointerToMemberConversionExpr(Node::Prec::Unary);
  if (consumeIf("nx")) {
    Node *Ex = getDerived().parseExpr();
    if (Ex == nullptr)
      return Ex;
    return make<EnclosingExpr>("noexcept ", Ex, Node::Prec::Unary);
  }
  if (look() == 'r' && (look(1) == 'q' || look(1) == 'Q'))
    return parseRequiresExpr();
  if (consumeIf("so"))
    return parseSubobjectExpr();
  if (consumeIf("sp")) {
    Node *Child = getDerived().parseExpr();
    if (Child == nullptr)
      return nullptr;
    return make<ParameterPackExpansion>(Child);
  }
  if (consumeIf("sZ")) {
    if (look() == 'T') {
      Node *R = getDerived().parseTemplateParam();
      if (R == nullptr)
        return nullptr;
      return make<SizeofParamPackExpr>(R);
    }
    Node *FP = getDerived().parseFunctionParam();
    if (FP == nullptr)
      return nullptr;
    return make<EnclosingExpr>("sizeof... ", FP);
  }
  if (consumeIf("sP")) {
    size_t ArgsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
    }
    auto *Pack = make<NodeArrayNode>(popTrailingNodeArray(ArgsBegin));
    if (!Pack)
      return nullptr;
    return make<EnclosingExpr>("sizeof... ", Pack);
  }
  if (consumeIf("tl")) {
    Node *Ty = getDerived().parseType();
    if (Ty == nullptr)
      return nullptr;
    size_t InitsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *E = getDerived().parseBracedExpr();
      if (E == nullptr)
        return nullptr;
      Names.push_back(E);
    }
    return make<InitListExpr>(Ty, popTrailingNodeArray(InitsBegin));
  }
  if (consumeIf("tr"))
    return make<NameType>("throw");
  if (consumeIf("tw")) {
    Node *Ex = getDerived().parseExpr();
    if (Ex == nullptr)
      return nullptr;
    return make<ThrowExpr>(Ex);
  }
  if (consumeIf('u')) {
    Node *Name = getDerived().parseSourceName(/*NameState=*/nullptr);
    if (!Name)
      return nullptr;
    // Special case legacy __uuidof mangling. The 't' and 'z' appear where the
    // standard encoding expects a <template-arg>, and would be otherwise be
    // interpreted as <type> node 'short' or 'ellipsis'. However, neither
    // __uuidof(short) nor __uuidof(...) can actually appear, so there is no
    // actual conflict here.
    bool IsUUID = false;
    Node *UUID = nullptr;
    if (Name->getBaseName() == "__uuidof") {
      if (consumeIf('t')) {
        UUID = getDerived().parseType();
        IsUUID = true;
      } else if (consumeIf('z')) {
        UUID = getDerived().parseExpr();
        IsUUID = true;
      }
    }
    size_t ExprsBegin = Names.size();
    if (IsUUID) {
      if (UUID == nullptr)
        return nullptr;
      Names.push_back(UUID);
    } else {
      while (!consumeIf('E')) {
        Node *E = getDerived().parseTemplateArg();
        if (E == nullptr)
          return E;
        Names.push_back(E);
      }
    }
    return make<CallExpr>(Name, popTrailingNodeArray(ExprsBegin),
                          Node::Prec::Postfix);
  }

  // Only unresolved names remain.
  return getDerived().parseUnresolvedName(Global);
}

// <call-offset> ::= h <nv-offset> _
//               ::= v <v-offset> _
//
// <nv-offset> ::= <offset number>
//               # non-virtual base override
//
// <v-offset>  ::= <offset number> _ <virtual offset number>
//               # virtual base override, with vcall offset
template <typename Alloc, typename Derived>
bool AbstractManglingParser<Alloc, Derived>::parseCallOffset() {
  // Just scan through the call offset, we never add this information into the
  // output.
  if (consumeIf('h'))
    return parseNumber(true).empty() || !consumeIf('_');
  if (consumeIf('v'))
    return parseNumber(true).empty() || !consumeIf('_') ||
           parseNumber(true).empty() || !consumeIf('_');
  return true;
}

// <special-name> ::= TV <type>    # virtual table
//                ::= TT <type>    # VTT structure (construction vtable index)
//                ::= TI <type>    # typeinfo structure
//                ::= TS <type>    # typeinfo name (null-terminated byte string)
//                ::= Tc <call-offset> <call-offset> <base encoding>
//                    # base is the nominal target function of thunk
//                    # first call-offset is 'this' adjustment
//                    # second call-offset is result adjustment
//                ::= T <call-offset> <base encoding>
//                    # base is the nominal target function of thunk
//                # Guard variable for one-time initialization
//                ::= GV <object name>
//                                     # No <type>
//                ::= TW <object name> # Thread-local wrapper
//                ::= TH <object name> # Thread-local initialization
//                ::= GR <object name> _             # First temporary
//                ::= GR <object name> <seq-id> _    # Subsequent temporaries
//                # construction vtable for second-in-first
//      extension ::= TC <first type> <number> _ <second type>
//      extension ::= GR <object name> # reference temporary for object
//      extension ::= GI <module name> # module global initializer
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSpecialName() {
  switch (look()) {
  case 'T':
    switch (look(1)) {
    // TA <template-arg>    # template parameter object
    //
    // Not yet in the spec: https://github.com/itanium-cxx-abi/cxx-abi/issues/63
    case 'A': {
      First += 2;
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      return make<SpecialName>("template parameter object for ", Arg);
    }
    // TV <type>    # virtual table
    case 'V': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<SpecialName>("vtable for ", Ty);
    }
    // TT <type>    # VTT structure (construction vtable index)
    case 'T': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<SpecialName>("VTT for ", Ty);
    }
    // TI <type>    # typeinfo structure
    case 'I': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<SpecialName>("typeinfo for ", Ty);
    }
    // TS <type>    # typeinfo name (null-terminated byte string)
    case 'S': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<SpecialName>("typeinfo name for ", Ty);
    }
    // Tc <call-offset> <call-offset> <base encoding>
    case 'c': {
      First += 2;
      if (parseCallOffset() || parseCallOffset())
        return nullptr;
      Node *Encoding = getDerived().parseEncoding();
      if (Encoding == nullptr)
        return nullptr;
      return make<SpecialName>("covariant return thunk to ", Encoding);
    }
    // extension ::= TC <first type> <number> _ <second type>
    //               # construction vtable for second-in-first
    case 'C': {
      First += 2;
      Node *FirstType = getDerived().parseType();
      if (FirstType == nullptr)
        return nullptr;
      if (parseNumber(true).empty() || !consumeIf('_'))
        return nullptr;
      Node *SecondType = getDerived().parseType();
      if (SecondType == nullptr)
        return nullptr;
      return make<CtorVtableSpecialName>(SecondType, FirstType);
    }
    // TW <object name> # Thread-local wrapper
    case 'W': {
      First += 2;
      Node *Name = getDerived().parseName();
      if (Name == nullptr)
        return nullptr;
      return make<SpecialName>("thread-local wrapper routine for ", Name);
    }
    // TH <object name> # Thread-local initialization
    case 'H': {
      First += 2;
      Node *Name = getDerived().parseName();
      if (Name == nullptr)
        return nullptr;
      return make<SpecialName>("thread-local initialization routine for ", Name);
    }
    // T <call-offset> <base encoding>
    default: {
      ++First;
      bool IsVirt = look() == 'v';
      if (parseCallOffset())
        return nullptr;
      Node *BaseEncoding = getDerived().parseEncoding();
      if (BaseEncoding == nullptr)
        return nullptr;
      if (IsVirt)
        return make<SpecialName>("virtual thunk to ", BaseEncoding);
      else
        return make<SpecialName>("non-virtual thunk to ", BaseEncoding);
    }
    }
  case 'G':
    switch (look(1)) {
    // GV <object name> # Guard variable for one-time initialization
    case 'V': {
      First += 2;
      Node *Name = getDerived().parseName();
      if (Name == nullptr)
        return nullptr;
      return make<SpecialName>("guard variable for ", Name);
    }
    // GR <object name> # reference temporary for object
    // GR <object name> _             # First temporary
    // GR <object name> <seq-id> _    # Subsequent temporaries
    case 'R': {
      First += 2;
      Node *Name = getDerived().parseName();
      if (Name == nullptr)
        return nullptr;
      size_t Count;
      bool ParsedSeqId = !parseSeqId(&Count);
      if (!consumeIf('_') && ParsedSeqId)
        return nullptr;
      return make<SpecialName>("reference temporary for ", Name);
    }
    // GI <module-name> v
    case 'I': {
      First += 2;
      ModuleName *Module = nullptr;
      if (getDerived().parseModuleNameOpt(Module))
        return nullptr;
      if (Module == nullptr)
        return nullptr;
      return make<SpecialName>("initializer for module ", Module);
    }
    }
  }
  return nullptr;
}

// <encoding> ::= <function name> <bare-function-type>
//                    [`Q` <requires-clause expr>]
//            ::= <data name>
//            ::= <special-name>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseEncoding(bool ParseParams) {
  // The template parameters of an encoding are unrelated to those of the
  // enclosing context.
  SaveTemplateParams SaveTemplateParamsScope(this);

  if (look() == 'G' || look() == 'T')
    return getDerived().parseSpecialName();

  auto IsEndOfEncoding = [&] {
    // The set of chars that can potentially follow an <encoding> (none of which
    // can start a <type>). Enumerating these allows us to avoid speculative
    // parsing.
    return numLeft() == 0 || look() == 'E' || look() == '.' || look() == '_';
  };

  NameState NameInfo(this);
  Node *Name = getDerived().parseName(&NameInfo);
  if (Name == nullptr)
    return nullptr;

  if (resolveForwardTemplateRefs(NameInfo))
    return nullptr;

  if (IsEndOfEncoding())
    return Name;

  // ParseParams may be false at the top level only, when called from parse().
  // For example in the mangled name _Z3fooILZ3BarEET_f, ParseParams may be
  // false when demangling 3fooILZ3BarEET_f but is always true when demangling
  // 3Bar.
  if (!ParseParams) {
    while (consume())
      ;
    return Name;
  }

  Node *Attrs = nullptr;
  if (consumeIf("Ua9enable_ifI")) {
    size_t BeforeArgs = Names.size();
    while (!consumeIf('E')) {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
    }
    Attrs = make<EnableIfAttr>(popTrailingNodeArray(BeforeArgs));
    if (!Attrs)
      return nullptr;
  }

  Node *ReturnType = nullptr;
  if (!NameInfo.CtorDtorConversion && NameInfo.EndsWithTemplateArgs) {
    ReturnType = getDerived().parseType();
    if (ReturnType == nullptr)
      return nullptr;
  }

  NodeArray Params;
  if (!consumeIf('v')) {
    size_t ParamsBegin = Names.size();
    do {
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;

      const bool IsFirstParam = ParamsBegin == Names.size();
      if (NameInfo.HasExplicitObjectParameter && IsFirstParam)
        Ty = make<ExplicitObjectParameter>(Ty);

      if (Ty == nullptr)
        return nullptr;

      Names.push_back(Ty);
    } while (!IsEndOfEncoding() && look() != 'Q');
    Params = popTrailingNodeArray(ParamsBegin);
  }

  Node *Requires = nullptr;
  if (consumeIf('Q')) {
    Requires = getDerived().parseConstraintExpr();
    if (!Requires)
      return nullptr;
  }

  return make<FunctionEncoding>(ReturnType, Name, Params, Attrs, Requires,
                                NameInfo.CVQualifiers,
                                NameInfo.ReferenceQualifier);
}

template <class Float>
struct FloatData;

template <>
struct FloatData<float>
{
    static const size_t mangled_size = 8;
    static const size_t max_demangled_size = 24;
    static constexpr const char* spec = "%af";
};

template <>
struct FloatData<double>
{
    static const size_t mangled_size = 16;
    static const size_t max_demangled_size = 32;
    static constexpr const char* spec = "%a";
};

template <>
struct FloatData<long double>
{
#if defined(__mips__) && defined(__mips_n64) || defined(__aarch64__) || \
    defined(__wasm__) || defined(__riscv) || defined(__loongarch__) || \
    defined(__ve__)
    static const size_t mangled_size = 32;
#elif defined(__arm__) || defined(__mips__) || defined(__hexagon__)
    static const size_t mangled_size = 16;
#else
    static const size_t mangled_size = 20;  // May need to be adjusted to 16 or 24 on other platforms
#endif
    // `-0x1.ffffffffffffffffffffffffffffp+16383` + 'L' + '\0' == 42 bytes.
    // 28 'f's * 4 bits == 112 bits, which is the number of mantissa bits.
    // Negatives are one character longer than positives.
    // `0x1.` and `p` are constant, and exponents `+16383` and `-16382` are the
    // same length. 1 sign bit, 112 mantissa bits, and 15 exponent bits == 128.
    static const size_t max_demangled_size = 42;
    static constexpr const char *spec = "%LaL";
};

template <typename Alloc, typename Derived>
template <class Float>
Node *AbstractManglingParser<Alloc, Derived>::parseFloatingLiteral() {
  const size_t N = FloatData<Float>::mangled_size;
  if (numLeft() <= N)
    return nullptr;
  std::string_view Data(First, N);
  for (char C : Data)
    if (!(C >= '0' && C <= '9') && !(C >= 'a' && C <= 'f'))
      return nullptr;
  First += N;
  if (!consumeIf('E'))
    return nullptr;
  return make<FloatLiteralImpl<Float>>(Data);
}

// <seq-id> ::= <0-9A-Z>+
template <typename Alloc, typename Derived>
bool AbstractManglingParser<Alloc, Derived>::parseSeqId(size_t *Out) {
  if (!(look() >= '0' && look() <= '9') &&
      !(look() >= 'A' && look() <= 'Z'))
    return true;

  size_t Id = 0;
  while (true) {
    if (look() >= '0' && look() <= '9') {
      Id *= 36;
      Id += static_cast<size_t>(look() - '0');
    } else if (look() >= 'A' && look() <= 'Z') {
      Id *= 36;
      Id += static_cast<size_t>(look() - 'A') + 10;
    } else {
      *Out = Id;
      return false;
    }
    ++First;
  }
}

// <substitution> ::= S <seq-id> _
//                ::= S_
// <substitution> ::= Sa # ::std::allocator
// <substitution> ::= Sb # ::std::basic_string
// <substitution> ::= Ss # ::std::basic_string < char,
//                                               ::std::char_traits<char>,
//                                               ::std::allocator<char> >
// <substitution> ::= Si # ::std::basic_istream<char,  std::char_traits<char> >
// <substitution> ::= So # ::std::basic_ostream<char,  std::char_traits<char> >
// <substitution> ::= Sd # ::std::basic_iostream<char, std::char_traits<char> >
// The St case is handled specially in parseNestedName.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSubstitution() {
  if (!consumeIf('S'))
    return nullptr;

  if (look() >= 'a' && look() <= 'z') {
    SpecialSubKind Kind;
    switch (look()) {
    case 'a':
      Kind = SpecialSubKind::allocator;
      break;
    case 'b':
      Kind = SpecialSubKind::basic_string;
      break;
    case 'd':
      Kind = SpecialSubKind::iostream;
      break;
    case 'i':
      Kind = SpecialSubKind::istream;
      break;
    case 'o':
      Kind = SpecialSubKind::ostream;
      break;
    case 's':
      Kind = SpecialSubKind::string;
      break;
    default:
      return nullptr;
    }
    ++First;
    auto *SpecialSub = make<SpecialSubstitution>(Kind);
    if (!SpecialSub)
      return nullptr;

    // Itanium C++ ABI 5.1.2: If a name that would use a built-in <substitution>
    // has ABI tags, the tags are appended to the substitution; the result is a
    // substitutable component.
    Node *WithTags = getDerived().parseAbiTags(SpecialSub);
    if (WithTags != SpecialSub) {
      Subs.push_back(WithTags);
      SpecialSub = WithTags;
    }
    return SpecialSub;
  }

  //                ::= S_
  if (consumeIf('_')) {
    if (Subs.empty())
      return nullptr;
    return Subs[0];
  }

  //                ::= S <seq-id> _
  size_t Index = 0;
  if (parseSeqId(&Index))
    return nullptr;
  ++Index;
  if (!consumeIf('_') || Index >= Subs.size())
    return nullptr;
  return Subs[Index];
}

// <template-param> ::= T_    # first template parameter
//                  ::= T <parameter-2 non-negative number> _
//                  ::= TL <level-1> __
//                  ::= TL <level-1> _ <parameter-2 non-negative number> _
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseTemplateParam() {
  const char *Begin = First;
  if (!consumeIf('T'))
    return nullptr;

  size_t Level = 0;
  if (consumeIf('L')) {
    if (parsePositiveInteger(&Level))
      return nullptr;
    ++Level;
    if (!consumeIf('_'))
      return nullptr;
  }

  size_t Index = 0;
  if (!consumeIf('_')) {
    if (parsePositiveInteger(&Index))
      return nullptr;
    ++Index;
    if (!consumeIf('_'))
      return nullptr;
  }

  // We don't track enclosing template parameter levels well enough to reliably
  // substitute them all within a <constraint-expression>, so print the
  // parameter numbering instead for now.
  // TODO: Track all enclosing template parameters and substitute them here.
  if (InConstraintExpr) {
    return make<NameType>(std::string_view(Begin, First - 1 - Begin));
  }

  // If we're in a context where this <template-param> refers to a
  // <template-arg> further ahead in the mangled name (currently just conversion
  // operator types), then we should only look it up in the right context.
  // This can only happen at the outermost level.
  if (PermitForwardTemplateReferences && Level == 0) {
    Node *ForwardRef = make<ForwardTemplateReference>(Index);
    if (!ForwardRef)
      return nullptr;
    DEMANGLE_ASSERT(ForwardRef->getKind() == Node::KForwardTemplateReference,
                    "");
    ForwardTemplateRefs.push_back(
        static_cast<ForwardTemplateReference *>(ForwardRef));
    return ForwardRef;
  }

  if (Level >= TemplateParams.size() || !TemplateParams[Level] ||
      Index >= TemplateParams[Level]->size()) {
    // Itanium ABI 5.1.8: In a generic lambda, uses of auto in the parameter
    // list are mangled as the corresponding artificial template type parameter.
    if (ParsingLambdaParamsAtLevel == Level && Level <= TemplateParams.size()) {
      // This will be popped by the ScopedTemplateParamList in
      // parseUnnamedTypeName.
      if (Level == TemplateParams.size())
        TemplateParams.push_back(nullptr);
      return make<NameType>("auto");
    }

    return nullptr;
  }

  return (*TemplateParams[Level])[Index];
}

// <template-param-decl> ::= Ty                          # type parameter
//                       ::= Tk <concept name> [<template-args>] # constrained type parameter
//                       ::= Tn <type>                   # non-type parameter
//                       ::= Tt <template-param-decl>* E # template parameter
//                       ::= Tp <template-param-decl>    # parameter pack
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseTemplateParamDecl(
    TemplateParamList *Params) {
  auto InventTemplateParamName = [&](TemplateParamKind Kind) {
    unsigned Index = NumSyntheticTemplateParameters[(int)Kind]++;
    Node *N = make<SyntheticTemplateParamName>(Kind, Index);
    if (N && Params)
      Params->push_back(N);
    return N;
  };

  if (consumeIf("Ty")) {
    Node *Name = InventTemplateParamName(TemplateParamKind::Type);
    if (!Name)
      return nullptr;
    return make<TypeTemplateParamDecl>(Name);
  }

  if (consumeIf("Tk")) {
    Node *Constraint = getDerived().parseName();
    if (!Constraint)
      return nullptr;
    Node *Name = InventTemplateParamName(TemplateParamKind::Type);
    if (!Name)
      return nullptr;
    return make<ConstrainedTypeTemplateParamDecl>(Constraint, Name);
  }

  if (consumeIf("Tn")) {
    Node *Name = InventTemplateParamName(TemplateParamKind::NonType);
    if (!Name)
      return nullptr;
    Node *Type = parseType();
    if (!Type)
      return nullptr;
    return make<NonTypeTemplateParamDecl>(Name, Type);
  }

  if (consumeIf("Tt")) {
    Node *Name = InventTemplateParamName(TemplateParamKind::Template);
    if (!Name)
      return nullptr;
    size_t ParamsBegin = Names.size();
    ScopedTemplateParamList TemplateTemplateParamParams(this);
    Node *Requires = nullptr;
    while (!consumeIf('E')) {
      Node *P = parseTemplateParamDecl(TemplateTemplateParamParams.params());
      if (!P)
        return nullptr;
      Names.push_back(P);
      if (consumeIf('Q')) {
        Requires = getDerived().parseConstraintExpr();
        if (Requires == nullptr || !consumeIf('E'))
          return nullptr;
        break;
      }
    }
    NodeArray InnerParams = popTrailingNodeArray(ParamsBegin);
    return make<TemplateTemplateParamDecl>(Name, InnerParams, Requires);
  }

  if (consumeIf("Tp")) {
    Node *P = parseTemplateParamDecl(Params);
    if (!P)
      return nullptr;
    return make<TemplateParamPackDecl>(P);
  }

  return nullptr;
}

// <template-arg> ::= <type>                    # type or template
//                ::= X <expression> E          # expression
//                ::= <expr-primary>            # simple expressions
//                ::= J <template-arg>* E       # argument pack
//                ::= LZ <encoding> E           # extension
//                ::= <template-param-decl> <template-arg>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseTemplateArg() {
  switch (look()) {
  case 'X': {
    ++First;
    Node *Arg = getDerived().parseExpr();
    if (Arg == nullptr || !consumeIf('E'))
      return nullptr;
    return Arg;
  }
  case 'J': {
    ++First;
    size_t ArgsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
    }
    NodeArray Args = popTrailingNodeArray(ArgsBegin);
    return make<TemplateArgumentPack>(Args);
  }
  case 'L': {
    //                ::= LZ <encoding> E           # extension
    if (look(1) == 'Z') {
      First += 2;
      Node *Arg = getDerived().parseEncoding();
      if (Arg == nullptr || !consumeIf('E'))
        return nullptr;
      return Arg;
    }
    //                ::= <expr-primary>            # simple expressions
    return getDerived().parseExprPrimary();
  }
  case 'T': {
    // Either <template-param> or a <template-param-decl> <template-arg>.
    if (!getDerived().isTemplateParamDecl())
      return getDerived().parseType();
    Node *Param = getDerived().parseTemplateParamDecl(nullptr);
    if (!Param)
      return nullptr;
    Node *Arg = getDerived().parseTemplateArg();
    if (!Arg)
      return nullptr;
    return make<TemplateParamQualifiedArg>(Param, Arg);
  }
  default:
    return getDerived().parseType();
  }
}

// <template-args> ::= I <template-arg>* [Q <requires-clause expr>] E
//     extension, the abi says <template-arg>+
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseTemplateArgs(bool TagTemplates) {
  if (!consumeIf('I'))
    return nullptr;

  // <template-params> refer to the innermost <template-args>. Clear out any
  // outer args that we may have inserted into TemplateParams.
  if (TagTemplates) {
    TemplateParams.clear();
    TemplateParams.push_back(&OuterTemplateParams);
    OuterTemplateParams.clear();
  }

  size_t ArgsBegin = Names.size();
  Node *Requires = nullptr;
  while (!consumeIf('E')) {
    if (TagTemplates) {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
      Node *TableEntry = Arg;
      if (Arg->getKind() == Node::KTemplateParamQualifiedArg) {
        TableEntry =
            static_cast<TemplateParamQualifiedArg *>(TableEntry)->getArg();
      }
      if (Arg->getKind() == Node::KTemplateArgumentPack) {
        TableEntry = make<ParameterPack>(
            static_cast<TemplateArgumentPack*>(TableEntry)->getElements());
        if (!TableEntry)
          return nullptr;
      }
      OuterTemplateParams.push_back(TableEntry);
    } else {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
    }
    if (consumeIf('Q')) {
      Requires = getDerived().parseConstraintExpr();
      if (!Requires || !consumeIf('E'))
        return nullptr;
      break;
    }
  }
  return make<TemplateArgs>(popTrailingNodeArray(ArgsBegin), Requires);
}

// <mangled-name> ::= _Z <encoding>
//                ::= <type>
// extension      ::= ___Z <encoding> _block_invoke
// extension      ::= ___Z <encoding> _block_invoke<decimal-digit>+
// extension      ::= ___Z <encoding> _block_invoke_<decimal-digit>+
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parse(bool ParseParams) {
  if (consumeIf("_Z") || consumeIf("__Z")) {
    Node *Encoding = getDerived().parseEncoding(ParseParams);
    if (Encoding == nullptr)
      return nullptr;
    if (look() == '.') {
      Encoding =
          make<DotSuffix>(Encoding, std::string_view(First, Last - First));
      First = Last;
    }
    if (numLeft() != 0)
      return nullptr;
    return Encoding;
  }

  if (consumeIf("___Z") || consumeIf("____Z")) {
    Node *Encoding = getDerived().parseEncoding(ParseParams);
    if (Encoding == nullptr || !consumeIf("_block_invoke"))
      return nullptr;
    bool RequireNumber = consumeIf('_');
    if (parseNumber().empty() && RequireNumber)
      return nullptr;
    if (look() == '.')
      First = Last;
    if (numLeft() != 0)
      return nullptr;
    return make<SpecialName>("invocation function for block in ", Encoding);
  }

  Node *Ty = getDerived().parseType();
  if (numLeft() != 0)
    return nullptr;
  return Ty;
}

template <typename Alloc>
struct ManglingParser : AbstractManglingParser<ManglingParser<Alloc>, Alloc> {
  using AbstractManglingParser<ManglingParser<Alloc>,
                               Alloc>::AbstractManglingParser;
};

DEMANGLE_NAMESPACE_END

#ifdef _LIBCXXABI_COMPILER_CLANG
#pragma clang diagnostic pop
#endif

#endif // DEMANGLE_ITANIUMDEMANGLE_H

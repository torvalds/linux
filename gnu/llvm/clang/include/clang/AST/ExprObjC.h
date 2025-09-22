//===- ExprObjC.h - Classes for representing ObjC expressions ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ExprObjC interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_EXPROBJC_H
#define LLVM_CLANG_AST_EXPROBJC_H

#include "clang/AST/ComputeDependence.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DependenceFlags.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/SelectorLocationsKind.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TrailingObjects.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace clang {

class ASTContext;
class CXXBaseSpecifier;

/// ObjCStringLiteral, used for Objective-C string literals
/// i.e. @"foo".
class ObjCStringLiteral : public Expr {
  Stmt *String;
  SourceLocation AtLoc;

public:
  ObjCStringLiteral(StringLiteral *SL, QualType T, SourceLocation L)
      : Expr(ObjCStringLiteralClass, T, VK_PRValue, OK_Ordinary), String(SL),
        AtLoc(L) {
    setDependence(ExprDependence::None);
  }
  explicit ObjCStringLiteral(EmptyShell Empty)
      : Expr(ObjCStringLiteralClass, Empty) {}

  StringLiteral *getString() { return cast<StringLiteral>(String); }
  const StringLiteral *getString() const { return cast<StringLiteral>(String); }
  void setString(StringLiteral *S) { String = S; }

  SourceLocation getAtLoc() const { return AtLoc; }
  void setAtLoc(SourceLocation L) { AtLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return AtLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return String->getEndLoc(); }

  // Iterators
  child_range children() { return child_range(&String, &String+1); }

  const_child_range children() const {
    return const_child_range(&String, &String + 1);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCStringLiteralClass;
  }
};

/// ObjCBoolLiteralExpr - Objective-C Boolean Literal.
class ObjCBoolLiteralExpr : public Expr {
  bool Value;
  SourceLocation Loc;

public:
  ObjCBoolLiteralExpr(bool val, QualType Ty, SourceLocation l)
      : Expr(ObjCBoolLiteralExprClass, Ty, VK_PRValue, OK_Ordinary), Value(val),
        Loc(l) {
    setDependence(ExprDependence::None);
  }
  explicit ObjCBoolLiteralExpr(EmptyShell Empty)
      : Expr(ObjCBoolLiteralExprClass, Empty) {}

  bool getValue() const { return Value; }
  void setValue(bool V) { Value = V; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return Loc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return Loc; }

  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCBoolLiteralExprClass;
  }
};

/// ObjCBoxedExpr - used for generalized expression boxing.
/// as in: @(strdup("hello world")), @(random()) or @(view.frame)
/// Also used for boxing non-parenthesized numeric literals;
/// as in: @42 or \@true (c++/objc++) or \@__objc_yes (c/objc).
class ObjCBoxedExpr : public Expr {
  Stmt *SubExpr;
  ObjCMethodDecl *BoxingMethod;
  SourceRange Range;

public:
  friend class ASTStmtReader;

  ObjCBoxedExpr(Expr *E, QualType T, ObjCMethodDecl *method, SourceRange R)
      : Expr(ObjCBoxedExprClass, T, VK_PRValue, OK_Ordinary), SubExpr(E),
        BoxingMethod(method), Range(R) {
    setDependence(computeDependence(this));
  }
  explicit ObjCBoxedExpr(EmptyShell Empty)
      : Expr(ObjCBoxedExprClass, Empty) {}

  Expr *getSubExpr() { return cast<Expr>(SubExpr); }
  const Expr *getSubExpr() const { return cast<Expr>(SubExpr); }

  ObjCMethodDecl *getBoxingMethod() const {
    return BoxingMethod;
  }

  // Indicates whether this boxed expression can be emitted as a compile-time
  // constant.
  bool isExpressibleAsConstantInitializer() const {
    return !BoxingMethod && SubExpr;
  }

  SourceLocation getAtLoc() const { return Range.getBegin(); }

  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }

  SourceRange getSourceRange() const LLVM_READONLY {
    return Range;
  }

  // Iterators
  child_range children() { return child_range(&SubExpr, &SubExpr+1); }

  const_child_range children() const {
    return const_child_range(&SubExpr, &SubExpr + 1);
  }

  using const_arg_iterator = ConstExprIterator;

  const_arg_iterator arg_begin() const {
    return reinterpret_cast<Stmt const * const*>(&SubExpr);
  }

  const_arg_iterator arg_end() const {
    return reinterpret_cast<Stmt const * const*>(&SubExpr + 1);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCBoxedExprClass;
  }
};

/// ObjCArrayLiteral - used for objective-c array containers; as in:
/// @[@"Hello", NSApp, [NSNumber numberWithInt:42]];
class ObjCArrayLiteral final
    : public Expr,
      private llvm::TrailingObjects<ObjCArrayLiteral, Expr *> {
  unsigned NumElements;
  SourceRange Range;
  ObjCMethodDecl *ArrayWithObjectsMethod;

  ObjCArrayLiteral(ArrayRef<Expr *> Elements,
                   QualType T, ObjCMethodDecl * Method,
                   SourceRange SR);

  explicit ObjCArrayLiteral(EmptyShell Empty, unsigned NumElements)
      : Expr(ObjCArrayLiteralClass, Empty), NumElements(NumElements) {}

public:
  friend class ASTStmtReader;
  friend TrailingObjects;

  static ObjCArrayLiteral *Create(const ASTContext &C,
                                  ArrayRef<Expr *> Elements,
                                  QualType T, ObjCMethodDecl * Method,
                                  SourceRange SR);

  static ObjCArrayLiteral *CreateEmpty(const ASTContext &C,
                                       unsigned NumElements);

  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }
  SourceRange getSourceRange() const LLVM_READONLY { return Range; }

  /// Retrieve elements of array of literals.
  Expr **getElements() { return getTrailingObjects<Expr *>(); }

  /// Retrieve elements of array of literals.
  const Expr * const *getElements() const {
    return getTrailingObjects<Expr *>();
  }

  /// getNumElements - Return number of elements of objective-c array literal.
  unsigned getNumElements() const { return NumElements; }

  /// getElement - Return the Element at the specified index.
  Expr *getElement(unsigned Index) {
    assert((Index < NumElements) && "Arg access out of range!");
    return getElements()[Index];
  }
  const Expr *getElement(unsigned Index) const {
    assert((Index < NumElements) && "Arg access out of range!");
    return getElements()[Index];
  }

  ObjCMethodDecl *getArrayWithObjectsMethod() const {
    return ArrayWithObjectsMethod;
  }

  // Iterators
  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(getElements()),
                       reinterpret_cast<Stmt **>(getElements()) + NumElements);
  }

  const_child_range children() const {
    auto Children = const_cast<ObjCArrayLiteral *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  static bool classof(const Stmt *T) {
      return T->getStmtClass() == ObjCArrayLiteralClass;
  }
};

/// An element in an Objective-C dictionary literal.
///
struct ObjCDictionaryElement {
  /// The key for the dictionary element.
  Expr *Key;

  /// The value of the dictionary element.
  Expr *Value;

  /// The location of the ellipsis, if this is a pack expansion.
  SourceLocation EllipsisLoc;

  /// The number of elements this pack expansion will expand to, if
  /// this is a pack expansion and is known.
  std::optional<unsigned> NumExpansions;

  /// Determines whether this dictionary element is a pack expansion.
  bool isPackExpansion() const { return EllipsisLoc.isValid(); }
};

} // namespace clang

namespace clang {

/// Internal struct for storing Key/value pair.
struct ObjCDictionaryLiteral_KeyValuePair {
  Expr *Key;
  Expr *Value;
};

/// Internal struct to describes an element that is a pack
/// expansion, used if any of the elements in the dictionary literal
/// are pack expansions.
struct ObjCDictionaryLiteral_ExpansionData {
  /// The location of the ellipsis, if this element is a pack
  /// expansion.
  SourceLocation EllipsisLoc;

  /// If non-zero, the number of elements that this pack
  /// expansion will expand to (+1).
  unsigned NumExpansionsPlusOne;
};

/// ObjCDictionaryLiteral - AST node to represent objective-c dictionary
/// literals; as in:  @{@"name" : NSUserName(), @"date" : [NSDate date] };
class ObjCDictionaryLiteral final
    : public Expr,
      private llvm::TrailingObjects<ObjCDictionaryLiteral,
                                    ObjCDictionaryLiteral_KeyValuePair,
                                    ObjCDictionaryLiteral_ExpansionData> {
  /// The number of elements in this dictionary literal.
  unsigned NumElements : 31;

  /// Determine whether this dictionary literal has any pack expansions.
  ///
  /// If the dictionary literal has pack expansions, then there will
  /// be an array of pack expansion data following the array of
  /// key/value pairs, which provide the locations of the ellipses (if
  /// any) and number of elements in the expansion (if known). If
  /// there are no pack expansions, we optimize away this storage.
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasPackExpansions : 1;

  SourceRange Range;
  ObjCMethodDecl *DictWithObjectsMethod;

  using KeyValuePair = ObjCDictionaryLiteral_KeyValuePair;
  using ExpansionData = ObjCDictionaryLiteral_ExpansionData;

  ObjCDictionaryLiteral(ArrayRef<ObjCDictionaryElement> VK,
                        bool HasPackExpansions,
                        QualType T, ObjCMethodDecl *method,
                        SourceRange SR);

  explicit ObjCDictionaryLiteral(EmptyShell Empty, unsigned NumElements,
                                 bool HasPackExpansions)
      : Expr(ObjCDictionaryLiteralClass, Empty), NumElements(NumElements),
        HasPackExpansions(HasPackExpansions) {}

  size_t numTrailingObjects(OverloadToken<KeyValuePair>) const {
    return NumElements;
  }

public:
  friend class ASTStmtReader;
  friend class ASTStmtWriter;
  friend TrailingObjects;

  static ObjCDictionaryLiteral *Create(const ASTContext &C,
                                       ArrayRef<ObjCDictionaryElement> VK,
                                       bool HasPackExpansions,
                                       QualType T, ObjCMethodDecl *method,
                                       SourceRange SR);

  static ObjCDictionaryLiteral *CreateEmpty(const ASTContext &C,
                                            unsigned NumElements,
                                            bool HasPackExpansions);

  /// getNumElements - Return number of elements of objective-c dictionary
  /// literal.
  unsigned getNumElements() const { return NumElements; }

  ObjCDictionaryElement getKeyValueElement(unsigned Index) const {
    assert((Index < NumElements) && "Arg access out of range!");
    const KeyValuePair &KV = getTrailingObjects<KeyValuePair>()[Index];
    ObjCDictionaryElement Result = {KV.Key, KV.Value, SourceLocation(),
                                    std::nullopt};
    if (HasPackExpansions) {
      const ExpansionData &Expansion =
          getTrailingObjects<ExpansionData>()[Index];
      Result.EllipsisLoc = Expansion.EllipsisLoc;
      if (Expansion.NumExpansionsPlusOne > 0)
        Result.NumExpansions = Expansion.NumExpansionsPlusOne - 1;
    }
    return Result;
  }

  ObjCMethodDecl *getDictWithObjectsMethod() const {
    return DictWithObjectsMethod;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }
  SourceRange getSourceRange() const LLVM_READONLY { return Range; }

  // Iterators
  child_range children() {
    // Note: we're taking advantage of the layout of the KeyValuePair struct
    // here. If that struct changes, this code will need to change as well.
    static_assert(sizeof(KeyValuePair) == sizeof(Stmt *) * 2,
                  "KeyValuePair is expected size");
    return child_range(
        reinterpret_cast<Stmt **>(getTrailingObjects<KeyValuePair>()),
        reinterpret_cast<Stmt **>(getTrailingObjects<KeyValuePair>()) +
            NumElements * 2);
  }

  const_child_range children() const {
    auto Children = const_cast<ObjCDictionaryLiteral *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCDictionaryLiteralClass;
  }
};

/// ObjCEncodeExpr, used for \@encode in Objective-C.  \@encode has the same
/// type and behavior as StringLiteral except that the string initializer is
/// obtained from ASTContext with the encoding type as an argument.
class ObjCEncodeExpr : public Expr {
  TypeSourceInfo *EncodedType;
  SourceLocation AtLoc, RParenLoc;

public:
  ObjCEncodeExpr(QualType T, TypeSourceInfo *EncodedType, SourceLocation at,
                 SourceLocation rp)
      : Expr(ObjCEncodeExprClass, T, VK_LValue, OK_Ordinary),
        EncodedType(EncodedType), AtLoc(at), RParenLoc(rp) {
    setDependence(computeDependence(this));
  }

  explicit ObjCEncodeExpr(EmptyShell Empty) : Expr(ObjCEncodeExprClass, Empty){}

  SourceLocation getAtLoc() const { return AtLoc; }
  void setAtLoc(SourceLocation L) { AtLoc = L; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  QualType getEncodedType() const { return EncodedType->getType(); }

  TypeSourceInfo *getEncodedTypeSourceInfo() const { return EncodedType; }

  void setEncodedTypeSourceInfo(TypeSourceInfo *EncType) {
    EncodedType = EncType;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return AtLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCEncodeExprClass;
  }
};

/// ObjCSelectorExpr used for \@selector in Objective-C.
class ObjCSelectorExpr : public Expr {
  Selector SelName;
  SourceLocation AtLoc, RParenLoc;

public:
  ObjCSelectorExpr(QualType T, Selector selInfo, SourceLocation at,
                   SourceLocation rp)
      : Expr(ObjCSelectorExprClass, T, VK_PRValue, OK_Ordinary),
        SelName(selInfo), AtLoc(at), RParenLoc(rp) {
    setDependence(ExprDependence::None);
  }
  explicit ObjCSelectorExpr(EmptyShell Empty)
      : Expr(ObjCSelectorExprClass, Empty) {}

  Selector getSelector() const { return SelName; }
  void setSelector(Selector S) { SelName = S; }

  SourceLocation getAtLoc() const { return AtLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setAtLoc(SourceLocation L) { AtLoc = L; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return AtLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  /// getNumArgs - Return the number of actual arguments to this call.
  unsigned getNumArgs() const { return SelName.getNumArgs(); }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCSelectorExprClass;
  }
};

/// ObjCProtocolExpr used for protocol expression in Objective-C.
///
/// This is used as: \@protocol(foo), as in:
/// \code
///   [obj conformsToProtocol:@protocol(foo)]
/// \endcode
///
/// The return type is "Protocol*".
class ObjCProtocolExpr : public Expr {
  ObjCProtocolDecl *TheProtocol;
  SourceLocation AtLoc, ProtoLoc, RParenLoc;

public:
  friend class ASTStmtReader;
  friend class ASTStmtWriter;

  ObjCProtocolExpr(QualType T, ObjCProtocolDecl *protocol, SourceLocation at,
                   SourceLocation protoLoc, SourceLocation rp)
      : Expr(ObjCProtocolExprClass, T, VK_PRValue, OK_Ordinary),
        TheProtocol(protocol), AtLoc(at), ProtoLoc(protoLoc), RParenLoc(rp) {
    setDependence(ExprDependence::None);
  }
  explicit ObjCProtocolExpr(EmptyShell Empty)
      : Expr(ObjCProtocolExprClass, Empty) {}

  ObjCProtocolDecl *getProtocol() const { return TheProtocol; }
  void setProtocol(ObjCProtocolDecl *P) { TheProtocol = P; }

  SourceLocation getProtocolIdLoc() const { return ProtoLoc; }
  SourceLocation getAtLoc() const { return AtLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setAtLoc(SourceLocation L) { AtLoc = L; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return AtLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCProtocolExprClass;
  }
};

/// ObjCIvarRefExpr - A reference to an ObjC instance variable.
class ObjCIvarRefExpr : public Expr {
  ObjCIvarDecl *D;
  Stmt *Base;
  SourceLocation Loc;

  /// OpLoc - This is the location of '.' or '->'
  SourceLocation OpLoc;

  // True if this is "X->F", false if this is "X.F".
  LLVM_PREFERRED_TYPE(bool)
  bool IsArrow : 1;

  // True if ivar reference has no base (self assumed).
  LLVM_PREFERRED_TYPE(bool)
  bool IsFreeIvar : 1;

public:
  ObjCIvarRefExpr(ObjCIvarDecl *d, QualType t, SourceLocation l,
                  SourceLocation oploc, Expr *base, bool arrow = false,
                  bool freeIvar = false)
      : Expr(ObjCIvarRefExprClass, t, VK_LValue,
             d->isBitField() ? OK_BitField : OK_Ordinary),
        D(d), Base(base), Loc(l), OpLoc(oploc), IsArrow(arrow),
        IsFreeIvar(freeIvar) {
    setDependence(computeDependence(this));
  }

  explicit ObjCIvarRefExpr(EmptyShell Empty)
      : Expr(ObjCIvarRefExprClass, Empty) {}

  ObjCIvarDecl *getDecl() { return D; }
  const ObjCIvarDecl *getDecl() const { return D; }
  void setDecl(ObjCIvarDecl *d) { D = d; }

  const Expr *getBase() const { return cast<Expr>(Base); }
  Expr *getBase() { return cast<Expr>(Base); }
  void setBase(Expr * base) { Base = base; }

  bool isArrow() const { return IsArrow; }
  bool isFreeIvar() const { return IsFreeIvar; }
  void setIsArrow(bool A) { IsArrow = A; }
  void setIsFreeIvar(bool A) { IsFreeIvar = A; }

  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return isFreeIvar() ? Loc : getBase()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY { return Loc; }

  SourceLocation getOpLoc() const { return OpLoc; }
  void setOpLoc(SourceLocation L) { OpLoc = L; }

  // Iterators
  child_range children() { return child_range(&Base, &Base+1); }

  const_child_range children() const {
    return const_child_range(&Base, &Base + 1);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCIvarRefExprClass;
  }
};

/// ObjCPropertyRefExpr - A dot-syntax expression to access an ObjC
/// property.
class ObjCPropertyRefExpr : public Expr {
private:
  /// If the bool is true, this is an implicit property reference; the
  /// pointer is an (optional) ObjCMethodDecl and Setter may be set.
  /// if the bool is false, this is an explicit property reference;
  /// the pointer is an ObjCPropertyDecl and Setter is always null.
  llvm::PointerIntPair<NamedDecl *, 1, bool> PropertyOrGetter;

  /// Indicates whether the property reference will result in a message
  /// to the getter, the setter, or both.
  /// This applies to both implicit and explicit property references.
  enum MethodRefFlags {
    MethodRef_None = 0,
    MethodRef_Getter = 0x1,
    MethodRef_Setter = 0x2
  };

  /// Contains the Setter method pointer and MethodRefFlags bit flags.
  llvm::PointerIntPair<ObjCMethodDecl *, 2, unsigned> SetterAndMethodRefFlags;

  // FIXME: Maybe we should store the property identifier here,
  // because it's not rederivable from the other data when there's an
  // implicit property with no getter (because the 'foo' -> 'setFoo:'
  // transformation is lossy on the first character).

  SourceLocation IdLoc;

  /// When the receiver in property access is 'super', this is
  /// the location of the 'super' keyword.  When it's an interface,
  /// this is that interface.
  SourceLocation ReceiverLoc;
  llvm::PointerUnion<Stmt *, const Type *, ObjCInterfaceDecl *> Receiver;

public:
  ObjCPropertyRefExpr(ObjCPropertyDecl *PD, QualType t, ExprValueKind VK,
                      ExprObjectKind OK, SourceLocation l, Expr *base)
      : Expr(ObjCPropertyRefExprClass, t, VK, OK), PropertyOrGetter(PD, false),
        IdLoc(l), Receiver(base) {
    assert(t->isSpecificPlaceholderType(BuiltinType::PseudoObject));
    setDependence(computeDependence(this));
  }

  ObjCPropertyRefExpr(ObjCPropertyDecl *PD, QualType t, ExprValueKind VK,
                      ExprObjectKind OK, SourceLocation l, SourceLocation sl,
                      QualType st)
      : Expr(ObjCPropertyRefExprClass, t, VK, OK), PropertyOrGetter(PD, false),
        IdLoc(l), ReceiverLoc(sl), Receiver(st.getTypePtr()) {
    assert(t->isSpecificPlaceholderType(BuiltinType::PseudoObject));
    setDependence(computeDependence(this));
  }

  ObjCPropertyRefExpr(ObjCMethodDecl *Getter, ObjCMethodDecl *Setter,
                      QualType T, ExprValueKind VK, ExprObjectKind OK,
                      SourceLocation IdLoc, Expr *Base)
      : Expr(ObjCPropertyRefExprClass, T, VK, OK),
        PropertyOrGetter(Getter, true), SetterAndMethodRefFlags(Setter, 0),
        IdLoc(IdLoc), Receiver(Base) {
    assert(T->isSpecificPlaceholderType(BuiltinType::PseudoObject));
    setDependence(computeDependence(this));
  }

  ObjCPropertyRefExpr(ObjCMethodDecl *Getter, ObjCMethodDecl *Setter,
                      QualType T, ExprValueKind VK, ExprObjectKind OK,
                      SourceLocation IdLoc, SourceLocation SuperLoc,
                      QualType SuperTy)
      : Expr(ObjCPropertyRefExprClass, T, VK, OK),
        PropertyOrGetter(Getter, true), SetterAndMethodRefFlags(Setter, 0),
        IdLoc(IdLoc), ReceiverLoc(SuperLoc), Receiver(SuperTy.getTypePtr()) {
    assert(T->isSpecificPlaceholderType(BuiltinType::PseudoObject));
    setDependence(computeDependence(this));
  }

  ObjCPropertyRefExpr(ObjCMethodDecl *Getter, ObjCMethodDecl *Setter,
                      QualType T, ExprValueKind VK, ExprObjectKind OK,
                      SourceLocation IdLoc, SourceLocation ReceiverLoc,
                      ObjCInterfaceDecl *Receiver)
      : Expr(ObjCPropertyRefExprClass, T, VK, OK),
        PropertyOrGetter(Getter, true), SetterAndMethodRefFlags(Setter, 0),
        IdLoc(IdLoc), ReceiverLoc(ReceiverLoc), Receiver(Receiver) {
    assert(T->isSpecificPlaceholderType(BuiltinType::PseudoObject));
    setDependence(computeDependence(this));
  }

  explicit ObjCPropertyRefExpr(EmptyShell Empty)
      : Expr(ObjCPropertyRefExprClass, Empty) {}

  bool isImplicitProperty() const { return PropertyOrGetter.getInt(); }
  bool isExplicitProperty() const { return !PropertyOrGetter.getInt(); }

  ObjCPropertyDecl *getExplicitProperty() const {
    assert(!isImplicitProperty());
    return cast<ObjCPropertyDecl>(PropertyOrGetter.getPointer());
  }

  ObjCMethodDecl *getImplicitPropertyGetter() const {
    assert(isImplicitProperty());
    return cast_or_null<ObjCMethodDecl>(PropertyOrGetter.getPointer());
  }

  ObjCMethodDecl *getImplicitPropertySetter() const {
    assert(isImplicitProperty());
    return SetterAndMethodRefFlags.getPointer();
  }

  Selector getGetterSelector() const {
    if (isImplicitProperty())
      return getImplicitPropertyGetter()->getSelector();
    return getExplicitProperty()->getGetterName();
  }

  Selector getSetterSelector() const {
    if (isImplicitProperty())
      return getImplicitPropertySetter()->getSelector();
    return getExplicitProperty()->getSetterName();
  }

  /// True if the property reference will result in a message to the
  /// getter.
  /// This applies to both implicit and explicit property references.
  bool isMessagingGetter() const {
    return SetterAndMethodRefFlags.getInt() & MethodRef_Getter;
  }

  /// True if the property reference will result in a message to the
  /// setter.
  /// This applies to both implicit and explicit property references.
  bool isMessagingSetter() const {
    return SetterAndMethodRefFlags.getInt() & MethodRef_Setter;
  }

  void setIsMessagingGetter(bool val = true) {
    setMethodRefFlag(MethodRef_Getter, val);
  }

  void setIsMessagingSetter(bool val = true) {
    setMethodRefFlag(MethodRef_Setter, val);
  }

  const Expr *getBase() const {
    return cast<Expr>(Receiver.get<Stmt*>());
  }
  Expr *getBase() {
    return cast<Expr>(Receiver.get<Stmt*>());
  }

  SourceLocation getLocation() const { return IdLoc; }

  SourceLocation getReceiverLocation() const { return ReceiverLoc; }

  QualType getSuperReceiverType() const {
    return QualType(Receiver.get<const Type*>(), 0);
  }

  ObjCInterfaceDecl *getClassReceiver() const {
    return Receiver.get<ObjCInterfaceDecl*>();
  }

  bool isObjectReceiver() const { return Receiver.is<Stmt*>(); }
  bool isSuperReceiver() const { return Receiver.is<const Type*>(); }
  bool isClassReceiver() const { return Receiver.is<ObjCInterfaceDecl*>(); }

  /// Determine the type of the base, regardless of the kind of receiver.
  QualType getReceiverType(const ASTContext &ctx) const;

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return isObjectReceiver() ? getBase()->getBeginLoc()
                              : getReceiverLocation();
  }

  SourceLocation getEndLoc() const LLVM_READONLY { return IdLoc; }

  // Iterators
  child_range children() {
    if (Receiver.is<Stmt*>()) {
      Stmt **begin = reinterpret_cast<Stmt**>(&Receiver); // hack!
      return child_range(begin, begin+1);
    }
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    auto Children = const_cast<ObjCPropertyRefExpr *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCPropertyRefExprClass;
  }

private:
  friend class ASTStmtReader;
  friend class ASTStmtWriter;

  void setExplicitProperty(ObjCPropertyDecl *D, unsigned methRefFlags) {
    PropertyOrGetter.setPointer(D);
    PropertyOrGetter.setInt(false);
    SetterAndMethodRefFlags.setPointer(nullptr);
    SetterAndMethodRefFlags.setInt(methRefFlags);
  }

  void setImplicitProperty(ObjCMethodDecl *Getter, ObjCMethodDecl *Setter,
                           unsigned methRefFlags) {
    PropertyOrGetter.setPointer(Getter);
    PropertyOrGetter.setInt(true);
    SetterAndMethodRefFlags.setPointer(Setter);
    SetterAndMethodRefFlags.setInt(methRefFlags);
  }

  void setBase(Expr *Base) { Receiver = Base; }
  void setSuperReceiver(QualType T) { Receiver = T.getTypePtr(); }
  void setClassReceiver(ObjCInterfaceDecl *D) { Receiver = D; }

  void setLocation(SourceLocation L) { IdLoc = L; }
  void setReceiverLocation(SourceLocation Loc) { ReceiverLoc = Loc; }

  void setMethodRefFlag(MethodRefFlags flag, bool val) {
    unsigned f = SetterAndMethodRefFlags.getInt();
    if (val)
      f |= flag;
    else
      f &= ~flag;
    SetterAndMethodRefFlags.setInt(f);
  }
};

/// ObjCSubscriptRefExpr - used for array and dictionary subscripting.
/// array[4] = array[3]; dictionary[key] = dictionary[alt_key];
class ObjCSubscriptRefExpr : public Expr {
  // Location of ']' in an indexing expression.
  SourceLocation RBracket;

  // array/dictionary base expression.
  // for arrays, this is a numeric expression. For dictionaries, this is
  // an objective-c object pointer expression.
  enum { BASE, KEY, END_EXPR };
  Stmt* SubExprs[END_EXPR];

  ObjCMethodDecl *GetAtIndexMethodDecl;

  // For immutable objects this is null. When ObjCSubscriptRefExpr is to read
  // an indexed object this is null too.
  ObjCMethodDecl *SetAtIndexMethodDecl;

public:
  ObjCSubscriptRefExpr(Expr *base, Expr *key, QualType T, ExprValueKind VK,
                       ExprObjectKind OK, ObjCMethodDecl *getMethod,
                       ObjCMethodDecl *setMethod, SourceLocation RB)
      : Expr(ObjCSubscriptRefExprClass, T, VK, OK), RBracket(RB),
        GetAtIndexMethodDecl(getMethod), SetAtIndexMethodDecl(setMethod) {
    SubExprs[BASE] = base;
    SubExprs[KEY] = key;
    setDependence(computeDependence(this));
  }

  explicit ObjCSubscriptRefExpr(EmptyShell Empty)
      : Expr(ObjCSubscriptRefExprClass, Empty) {}

  SourceLocation getRBracket() const { return RBracket; }
  void setRBracket(SourceLocation RB) { RBracket = RB; }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return SubExprs[BASE]->getBeginLoc();
  }

  SourceLocation getEndLoc() const LLVM_READONLY { return RBracket; }

  Expr *getBaseExpr() const { return cast<Expr>(SubExprs[BASE]); }
  void setBaseExpr(Stmt *S) { SubExprs[BASE] = S; }

  Expr *getKeyExpr() const { return cast<Expr>(SubExprs[KEY]); }
  void setKeyExpr(Stmt *S) { SubExprs[KEY] = S; }

  ObjCMethodDecl *getAtIndexMethodDecl() const {
    return GetAtIndexMethodDecl;
  }

  ObjCMethodDecl *setAtIndexMethodDecl() const {
    return SetAtIndexMethodDecl;
  }

  bool isArraySubscriptRefExpr() const {
    return getKeyExpr()->getType()->isIntegralOrEnumerationType();
  }

  child_range children() {
    return child_range(SubExprs, SubExprs+END_EXPR);
  }

  const_child_range children() const {
    return const_child_range(SubExprs, SubExprs + END_EXPR);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCSubscriptRefExprClass;
  }

private:
  friend class ASTStmtReader;
};

/// An expression that sends a message to the given Objective-C
/// object or class.
///
/// The following contains two message send expressions:
///
/// \code
///   [[NSString alloc] initWithString:@"Hello"]
/// \endcode
///
/// The innermost message send invokes the "alloc" class method on the
/// NSString class, while the outermost message send invokes the
/// "initWithString" instance method on the object returned from
/// NSString's "alloc". In all, an Objective-C message send can take
/// on four different (although related) forms:
///
///   1. Send to an object instance.
///   2. Send to a class.
///   3. Send to the superclass instance of the current class.
///   4. Send to the superclass of the current class.
///
/// All four kinds of message sends are modeled by the ObjCMessageExpr
/// class, and can be distinguished via \c getReceiverKind(). Example:
///
/// The "void *" trailing objects are actually ONE void * (the
/// receiver pointer), and NumArgs Expr *. But due to the
/// implementation of children(), these must be together contiguously.
class ObjCMessageExpr final
    : public Expr,
      private llvm::TrailingObjects<ObjCMessageExpr, void *, SourceLocation> {
public:
  /// The kind of receiver this message is sending to.
  enum ReceiverKind {
    /// The receiver is a class.
    Class = 0,

    /// The receiver is an object instance.
    Instance,

    /// The receiver is a superclass.
    SuperClass,

    /// The receiver is the instance of the superclass object.
    SuperInstance
  };

private:
  /// Stores either the selector that this message is sending
  /// to (when \c HasMethod is zero) or an \c ObjCMethodDecl pointer
  /// referring to the method that we type-checked against.
  uintptr_t SelectorOrMethod = 0;

  enum { NumArgsBitWidth = 16 };

  /// The number of arguments in the message send, not
  /// including the receiver.
  unsigned NumArgs : NumArgsBitWidth;

  /// The kind of message send this is, which is one of the
  /// ReceiverKind values.
  ///
  /// We pad this out to a byte to avoid excessive masking and shifting.
  LLVM_PREFERRED_TYPE(ReceiverKind)
  unsigned Kind : 8;

  /// Whether we have an actual method prototype in \c
  /// SelectorOrMethod.
  ///
  /// When non-zero, we have a method declaration; otherwise, we just
  /// have a selector.
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasMethod : 1;

  /// Whether this message send is a "delegate init call",
  /// i.e. a call of an init method on self from within an init method.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsDelegateInitCall : 1;

  /// Whether this message send was implicitly generated by
  /// the implementation rather than explicitly written by the user.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsImplicit : 1;

  /// Whether the locations of the selector identifiers are in a
  /// "standard" position, a enum SelectorLocationsKind.
  LLVM_PREFERRED_TYPE(SelectorLocationsKind)
  unsigned SelLocsKind : 2;

  /// When the message expression is a send to 'super', this is
  /// the location of the 'super' keyword.
  SourceLocation SuperLoc;

  /// The source locations of the open and close square
  /// brackets ('[' and ']', respectively).
  SourceLocation LBracLoc, RBracLoc;

  ObjCMessageExpr(EmptyShell Empty, unsigned NumArgs)
      : Expr(ObjCMessageExprClass, Empty), Kind(0), HasMethod(false),
        IsDelegateInitCall(false), IsImplicit(false), SelLocsKind(0) {
    setNumArgs(NumArgs);
  }

  ObjCMessageExpr(QualType T, ExprValueKind VK,
                  SourceLocation LBracLoc,
                  SourceLocation SuperLoc,
                  bool IsInstanceSuper,
                  QualType SuperType,
                  Selector Sel,
                  ArrayRef<SourceLocation> SelLocs,
                  SelectorLocationsKind SelLocsK,
                  ObjCMethodDecl *Method,
                  ArrayRef<Expr *> Args,
                  SourceLocation RBracLoc,
                  bool isImplicit);
  ObjCMessageExpr(QualType T, ExprValueKind VK,
                  SourceLocation LBracLoc,
                  TypeSourceInfo *Receiver,
                  Selector Sel,
                  ArrayRef<SourceLocation> SelLocs,
                  SelectorLocationsKind SelLocsK,
                  ObjCMethodDecl *Method,
                  ArrayRef<Expr *> Args,
                  SourceLocation RBracLoc,
                  bool isImplicit);
  ObjCMessageExpr(QualType T, ExprValueKind VK,
                  SourceLocation LBracLoc,
                  Expr *Receiver,
                  Selector Sel,
                  ArrayRef<SourceLocation> SelLocs,
                  SelectorLocationsKind SelLocsK,
                  ObjCMethodDecl *Method,
                  ArrayRef<Expr *> Args,
                  SourceLocation RBracLoc,
                  bool isImplicit);

  size_t numTrailingObjects(OverloadToken<void *>) const { return NumArgs + 1; }

  void setNumArgs(unsigned Num) {
    assert((Num >> NumArgsBitWidth) == 0 && "Num of args is out of range!");
    NumArgs = Num;
  }

  void initArgsAndSelLocs(ArrayRef<Expr *> Args,
                          ArrayRef<SourceLocation> SelLocs,
                          SelectorLocationsKind SelLocsK);

  /// Retrieve the pointer value of the message receiver.
  void *getReceiverPointer() const { return *getTrailingObjects<void *>(); }

  /// Set the pointer value of the message receiver.
  void setReceiverPointer(void *Value) {
    *getTrailingObjects<void *>() = Value;
  }

  SelectorLocationsKind getSelLocsKind() const {
    return (SelectorLocationsKind)SelLocsKind;
  }

  bool hasStandardSelLocs() const {
    return getSelLocsKind() != SelLoc_NonStandard;
  }

  /// Get a pointer to the stored selector identifiers locations array.
  /// No locations will be stored if HasStandardSelLocs is true.
  SourceLocation *getStoredSelLocs() {
    return getTrailingObjects<SourceLocation>();
  }
  const SourceLocation *getStoredSelLocs() const {
    return getTrailingObjects<SourceLocation>();
  }

  /// Get the number of stored selector identifiers locations.
  /// No locations will be stored if HasStandardSelLocs is true.
  unsigned getNumStoredSelLocs() const {
    if (hasStandardSelLocs())
      return 0;
    return getNumSelectorLocs();
  }

  static ObjCMessageExpr *alloc(const ASTContext &C,
                                ArrayRef<Expr *> Args,
                                SourceLocation RBraceLoc,
                                ArrayRef<SourceLocation> SelLocs,
                                Selector Sel,
                                SelectorLocationsKind &SelLocsK);
  static ObjCMessageExpr *alloc(const ASTContext &C,
                                unsigned NumArgs,
                                unsigned NumStoredSelLocs);

public:
  friend class ASTStmtReader;
  friend class ASTStmtWriter;
  friend TrailingObjects;

  /// Create a message send to super.
  ///
  /// \param Context The ASTContext in which this expression will be created.
  ///
  /// \param T The result type of this message.
  ///
  /// \param VK The value kind of this message.  A message returning
  /// a l-value or r-value reference will be an l-value or x-value,
  /// respectively.
  ///
  /// \param LBracLoc The location of the open square bracket '['.
  ///
  /// \param SuperLoc The location of the "super" keyword.
  ///
  /// \param IsInstanceSuper Whether this is an instance "super"
  /// message (otherwise, it's a class "super" message).
  ///
  /// \param Sel The selector used to determine which method gets called.
  ///
  /// \param Method The Objective-C method against which this message
  /// send was type-checked. May be nullptr.
  ///
  /// \param Args The message send arguments.
  ///
  /// \param RBracLoc The location of the closing square bracket ']'.
  static ObjCMessageExpr *Create(const ASTContext &Context, QualType T,
                                 ExprValueKind VK,
                                 SourceLocation LBracLoc,
                                 SourceLocation SuperLoc,
                                 bool IsInstanceSuper,
                                 QualType SuperType,
                                 Selector Sel,
                                 ArrayRef<SourceLocation> SelLocs,
                                 ObjCMethodDecl *Method,
                                 ArrayRef<Expr *> Args,
                                 SourceLocation RBracLoc,
                                 bool isImplicit);

  /// Create a class message send.
  ///
  /// \param Context The ASTContext in which this expression will be created.
  ///
  /// \param T The result type of this message.
  ///
  /// \param VK The value kind of this message.  A message returning
  /// a l-value or r-value reference will be an l-value or x-value,
  /// respectively.
  ///
  /// \param LBracLoc The location of the open square bracket '['.
  ///
  /// \param Receiver The type of the receiver, including
  /// source-location information.
  ///
  /// \param Sel The selector used to determine which method gets called.
  ///
  /// \param Method The Objective-C method against which this message
  /// send was type-checked. May be nullptr.
  ///
  /// \param Args The message send arguments.
  ///
  /// \param RBracLoc The location of the closing square bracket ']'.
  static ObjCMessageExpr *Create(const ASTContext &Context, QualType T,
                                 ExprValueKind VK,
                                 SourceLocation LBracLoc,
                                 TypeSourceInfo *Receiver,
                                 Selector Sel,
                                 ArrayRef<SourceLocation> SelLocs,
                                 ObjCMethodDecl *Method,
                                 ArrayRef<Expr *> Args,
                                 SourceLocation RBracLoc,
                                 bool isImplicit);

  /// Create an instance message send.
  ///
  /// \param Context The ASTContext in which this expression will be created.
  ///
  /// \param T The result type of this message.
  ///
  /// \param VK The value kind of this message.  A message returning
  /// a l-value or r-value reference will be an l-value or x-value,
  /// respectively.
  ///
  /// \param LBracLoc The location of the open square bracket '['.
  ///
  /// \param Receiver The expression used to produce the object that
  /// will receive this message.
  ///
  /// \param Sel The selector used to determine which method gets called.
  ///
  /// \param Method The Objective-C method against which this message
  /// send was type-checked. May be nullptr.
  ///
  /// \param Args The message send arguments.
  ///
  /// \param RBracLoc The location of the closing square bracket ']'.
  static ObjCMessageExpr *Create(const ASTContext &Context, QualType T,
                                 ExprValueKind VK,
                                 SourceLocation LBracLoc,
                                 Expr *Receiver,
                                 Selector Sel,
                                 ArrayRef<SourceLocation> SeLocs,
                                 ObjCMethodDecl *Method,
                                 ArrayRef<Expr *> Args,
                                 SourceLocation RBracLoc,
                                 bool isImplicit);

  /// Create an empty Objective-C message expression, to be
  /// filled in by subsequent calls.
  ///
  /// \param Context The context in which the message send will be created.
  ///
  /// \param NumArgs The number of message arguments, not including
  /// the receiver.
  static ObjCMessageExpr *CreateEmpty(const ASTContext &Context,
                                      unsigned NumArgs,
                                      unsigned NumStoredSelLocs);

  /// Indicates whether the message send was implicitly
  /// generated by the implementation. If false, it was written explicitly
  /// in the source code.
  bool isImplicit() const { return IsImplicit; }

  /// Determine the kind of receiver that this message is being
  /// sent to.
  ReceiverKind getReceiverKind() const { return (ReceiverKind)Kind; }

  /// \return the return type of the message being sent.
  /// This is not always the type of the message expression itself because
  /// of references (the expression would not have a reference type).
  /// It is also not always the declared return type of the method because
  /// of `instancetype` (in that case it's an expression type).
  QualType getCallReturnType(ASTContext &Ctx) const;

  /// Source range of the receiver.
  SourceRange getReceiverRange() const;

  /// Determine whether this is an instance message to either a
  /// computed object or to super.
  bool isInstanceMessage() const {
    return getReceiverKind() == Instance || getReceiverKind() == SuperInstance;
  }

  /// Determine whether this is an class message to either a
  /// specified class or to super.
  bool isClassMessage() const {
    return getReceiverKind() == Class || getReceiverKind() == SuperClass;
  }

  /// Returns the object expression (receiver) for an instance message,
  /// or null for a message that is not an instance message.
  Expr *getInstanceReceiver() {
    if (getReceiverKind() == Instance)
      return static_cast<Expr *>(getReceiverPointer());

    return nullptr;
  }
  const Expr *getInstanceReceiver() const {
    return const_cast<ObjCMessageExpr*>(this)->getInstanceReceiver();
  }

  /// Turn this message send into an instance message that
  /// computes the receiver object with the given expression.
  void setInstanceReceiver(Expr *rec) {
    Kind = Instance;
    setReceiverPointer(rec);
  }

  /// Returns the type of a class message send, or NULL if the
  /// message is not a class message.
  QualType getClassReceiver() const {
    if (TypeSourceInfo *TSInfo = getClassReceiverTypeInfo())
      return TSInfo->getType();

    return {};
  }

  /// Returns a type-source information of a class message
  /// send, or nullptr if the message is not a class message.
  TypeSourceInfo *getClassReceiverTypeInfo() const {
    if (getReceiverKind() == Class)
      return reinterpret_cast<TypeSourceInfo *>(getReceiverPointer());
    return nullptr;
  }

  void setClassReceiver(TypeSourceInfo *TSInfo) {
    Kind = Class;
    setReceiverPointer(TSInfo);
  }

  /// Retrieve the location of the 'super' keyword for a class
  /// or instance message to 'super', otherwise an invalid source location.
  SourceLocation getSuperLoc() const {
    if (getReceiverKind() == SuperInstance || getReceiverKind() == SuperClass)
      return SuperLoc;

    return SourceLocation();
  }

  /// Retrieve the receiver type to which this message is being directed.
  ///
  /// This routine cross-cuts all of the different kinds of message
  /// sends to determine what the underlying (statically known) type
  /// of the receiver will be; use \c getReceiverKind() to determine
  /// whether the message is a class or an instance method, whether it
  /// is a send to super or not, etc.
  ///
  /// \returns The type of the receiver.
  QualType getReceiverType() const;

  /// Retrieve the Objective-C interface to which this message
  /// is being directed, if known.
  ///
  /// This routine cross-cuts all of the different kinds of message
  /// sends to determine what the underlying (statically known) type
  /// of the receiver will be; use \c getReceiverKind() to determine
  /// whether the message is a class or an instance method, whether it
  /// is a send to super or not, etc.
  ///
  /// \returns The Objective-C interface if known, otherwise nullptr.
  ObjCInterfaceDecl *getReceiverInterface() const;

  /// Retrieve the type referred to by 'super'.
  ///
  /// The returned type will either be an ObjCInterfaceType (for an
  /// class message to super) or an ObjCObjectPointerType that refers
  /// to a class (for an instance message to super);
  QualType getSuperType() const {
    if (getReceiverKind() == SuperInstance || getReceiverKind() == SuperClass)
      return QualType::getFromOpaquePtr(getReceiverPointer());

    return QualType();
  }

  void setSuper(SourceLocation Loc, QualType T, bool IsInstanceSuper) {
    Kind = IsInstanceSuper? SuperInstance : SuperClass;
    SuperLoc = Loc;
    setReceiverPointer(T.getAsOpaquePtr());
  }

  Selector getSelector() const;

  void setSelector(Selector S) {
    HasMethod = false;
    SelectorOrMethod = reinterpret_cast<uintptr_t>(S.getAsOpaquePtr());
  }

  const ObjCMethodDecl *getMethodDecl() const {
    if (HasMethod)
      return reinterpret_cast<const ObjCMethodDecl *>(SelectorOrMethod);

    return nullptr;
  }

  ObjCMethodDecl *getMethodDecl() {
    if (HasMethod)
      return reinterpret_cast<ObjCMethodDecl *>(SelectorOrMethod);

    return nullptr;
  }

  void setMethodDecl(ObjCMethodDecl *MD) {
    HasMethod = true;
    SelectorOrMethod = reinterpret_cast<uintptr_t>(MD);
  }

  ObjCMethodFamily getMethodFamily() const {
    if (HasMethod) return getMethodDecl()->getMethodFamily();
    return getSelector().getMethodFamily();
  }

  /// Return the number of actual arguments in this message,
  /// not counting the receiver.
  unsigned getNumArgs() const { return NumArgs; }

  /// Retrieve the arguments to this message, not including the
  /// receiver.
  Expr **getArgs() {
    return reinterpret_cast<Expr **>(getTrailingObjects<void *>() + 1);
  }
  const Expr * const *getArgs() const {
    return reinterpret_cast<const Expr *const *>(getTrailingObjects<void *>() +
                                                 1);
  }

  /// getArg - Return the specified argument.
  Expr *getArg(unsigned Arg) {
    assert(Arg < NumArgs && "Arg access out of range!");
    return getArgs()[Arg];
  }
  const Expr *getArg(unsigned Arg) const {
    assert(Arg < NumArgs && "Arg access out of range!");
    return getArgs()[Arg];
  }

  /// setArg - Set the specified argument.
  void setArg(unsigned Arg, Expr *ArgExpr) {
    assert(Arg < NumArgs && "Arg access out of range!");
    getArgs()[Arg] = ArgExpr;
  }

  /// isDelegateInitCall - Answers whether this message send has been
  /// tagged as a "delegate init call", i.e. a call to a method in the
  /// -init family on self from within an -init method implementation.
  bool isDelegateInitCall() const { return IsDelegateInitCall; }
  void setDelegateInitCall(bool isDelegate) { IsDelegateInitCall = isDelegate; }

  SourceLocation getLeftLoc() const { return LBracLoc; }
  SourceLocation getRightLoc() const { return RBracLoc; }

  SourceLocation getSelectorStartLoc() const {
    if (isImplicit())
      return getBeginLoc();
    return getSelectorLoc(0);
  }

  SourceLocation getSelectorLoc(unsigned Index) const {
    assert(Index < getNumSelectorLocs() && "Index out of range!");
    if (hasStandardSelLocs())
      return getStandardSelectorLoc(
          Index, getSelector(), getSelLocsKind() == SelLoc_StandardWithSpace,
          llvm::ArrayRef(const_cast<Expr **>(getArgs()), getNumArgs()),
          RBracLoc);
    return getStoredSelLocs()[Index];
  }

  void getSelectorLocs(SmallVectorImpl<SourceLocation> &SelLocs) const;

  unsigned getNumSelectorLocs() const {
    if (isImplicit())
      return 0;
    Selector Sel = getSelector();
    if (Sel.isUnarySelector())
      return 1;
    return Sel.getNumArgs();
  }

  void setSourceRange(SourceRange R) {
    LBracLoc = R.getBegin();
    RBracLoc = R.getEnd();
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return LBracLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RBracLoc; }

  // Iterators
  child_range children();

  const_child_range children() const;

  using arg_iterator = ExprIterator;
  using const_arg_iterator = ConstExprIterator;

  llvm::iterator_range<arg_iterator> arguments() {
    return llvm::make_range(arg_begin(), arg_end());
  }

  llvm::iterator_range<const_arg_iterator> arguments() const {
    return llvm::make_range(arg_begin(), arg_end());
  }

  arg_iterator arg_begin() { return reinterpret_cast<Stmt **>(getArgs()); }

  arg_iterator arg_end()   {
    return reinterpret_cast<Stmt **>(getArgs() + NumArgs);
  }

  const_arg_iterator arg_begin() const {
    return reinterpret_cast<Stmt const * const*>(getArgs());
  }

  const_arg_iterator arg_end() const {
    return reinterpret_cast<Stmt const * const*>(getArgs() + NumArgs);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCMessageExprClass;
  }
};

/// ObjCIsaExpr - Represent X->isa and X.isa when X is an ObjC 'id' type.
/// (similar in spirit to MemberExpr).
class ObjCIsaExpr : public Expr {
  /// Base - the expression for the base object pointer.
  Stmt *Base;

  /// IsaMemberLoc - This is the location of the 'isa'.
  SourceLocation IsaMemberLoc;

  /// OpLoc - This is the location of '.' or '->'
  SourceLocation OpLoc;

  /// IsArrow - True if this is "X->F", false if this is "X.F".
  bool IsArrow;

public:
  ObjCIsaExpr(Expr *base, bool isarrow, SourceLocation l, SourceLocation oploc,
              QualType ty)
      : Expr(ObjCIsaExprClass, ty, VK_LValue, OK_Ordinary), Base(base),
        IsaMemberLoc(l), OpLoc(oploc), IsArrow(isarrow) {
    setDependence(computeDependence(this));
  }

  /// Build an empty expression.
  explicit ObjCIsaExpr(EmptyShell Empty) : Expr(ObjCIsaExprClass, Empty) {}

  void setBase(Expr *E) { Base = E; }
  Expr *getBase() const { return cast<Expr>(Base); }

  bool isArrow() const { return IsArrow; }
  void setArrow(bool A) { IsArrow = A; }

  /// getMemberLoc - Return the location of the "member", in X->F, it is the
  /// location of 'F'.
  SourceLocation getIsaMemberLoc() const { return IsaMemberLoc; }
  void setIsaMemberLoc(SourceLocation L) { IsaMemberLoc = L; }

  SourceLocation getOpLoc() const { return OpLoc; }
  void setOpLoc(SourceLocation L) { OpLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getBase()->getBeginLoc();
  }

  SourceLocation getBaseLocEnd() const LLVM_READONLY {
    return getBase()->getEndLoc();
  }

  SourceLocation getEndLoc() const LLVM_READONLY { return IsaMemberLoc; }

  SourceLocation getExprLoc() const LLVM_READONLY { return IsaMemberLoc; }

  // Iterators
  child_range children() { return child_range(&Base, &Base+1); }

  const_child_range children() const {
    return const_child_range(&Base, &Base + 1);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCIsaExprClass;
  }
};

/// ObjCIndirectCopyRestoreExpr - Represents the passing of a function
/// argument by indirect copy-restore in ARC.  This is used to support
/// passing indirect arguments with the wrong lifetime, e.g. when
/// passing the address of a __strong local variable to an 'out'
/// parameter.  This expression kind is only valid in an "argument"
/// position to some sort of call expression.
///
/// The parameter must have type 'pointer to T', and the argument must
/// have type 'pointer to U', where T and U agree except possibly in
/// qualification.  If the argument value is null, then a null pointer
/// is passed;  otherwise it points to an object A, and:
/// 1. A temporary object B of type T is initialized, either by
///    zero-initialization (used when initializing an 'out' parameter)
///    or copy-initialization (used when initializing an 'inout'
///    parameter).
/// 2. The address of the temporary is passed to the function.
/// 3. If the call completes normally, A is move-assigned from B.
/// 4. Finally, A is destroyed immediately.
///
/// Currently 'T' must be a retainable object lifetime and must be
/// __autoreleasing;  this qualifier is ignored when initializing
/// the value.
class ObjCIndirectCopyRestoreExpr : public Expr {
  friend class ASTReader;
  friend class ASTStmtReader;

  Stmt *Operand;

  // unsigned ObjCIndirectCopyRestoreBits.ShouldCopy : 1;

  explicit ObjCIndirectCopyRestoreExpr(EmptyShell Empty)
      : Expr(ObjCIndirectCopyRestoreExprClass, Empty) {}

  void setShouldCopy(bool shouldCopy) {
    ObjCIndirectCopyRestoreExprBits.ShouldCopy = shouldCopy;
  }

public:
  ObjCIndirectCopyRestoreExpr(Expr *operand, QualType type, bool shouldCopy)
      : Expr(ObjCIndirectCopyRestoreExprClass, type, VK_LValue, OK_Ordinary),
        Operand(operand) {
    setShouldCopy(shouldCopy);
    setDependence(computeDependence(this));
  }

  Expr *getSubExpr() { return cast<Expr>(Operand); }
  const Expr *getSubExpr() const { return cast<Expr>(Operand); }

  /// shouldCopy - True if we should do the 'copy' part of the
  /// copy-restore.  If false, the temporary will be zero-initialized.
  bool shouldCopy() const { return ObjCIndirectCopyRestoreExprBits.ShouldCopy; }

  child_range children() { return child_range(&Operand, &Operand+1); }

  const_child_range children() const {
    return const_child_range(&Operand, &Operand + 1);
  }

  // Source locations are determined by the subexpression.
  SourceLocation getBeginLoc() const LLVM_READONLY {
    return Operand->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return Operand->getEndLoc();
  }

  SourceLocation getExprLoc() const LLVM_READONLY {
    return getSubExpr()->getExprLoc();
  }

  static bool classof(const Stmt *s) {
    return s->getStmtClass() == ObjCIndirectCopyRestoreExprClass;
  }
};

/// An Objective-C "bridged" cast expression, which casts between
/// Objective-C pointers and C pointers, transferring ownership in the process.
///
/// \code
/// NSString *str = (__bridge_transfer NSString *)CFCreateString();
/// \endcode
class ObjCBridgedCastExpr final
    : public ExplicitCastExpr,
      private llvm::TrailingObjects<ObjCBridgedCastExpr, CXXBaseSpecifier *> {
  friend class ASTStmtReader;
  friend class ASTStmtWriter;
  friend class CastExpr;
  friend TrailingObjects;

  SourceLocation LParenLoc;
  SourceLocation BridgeKeywordLoc;
  LLVM_PREFERRED_TYPE(ObjCBridgeCastKind)
  unsigned Kind : 2;

public:
  ObjCBridgedCastExpr(SourceLocation LParenLoc, ObjCBridgeCastKind Kind,
                      CastKind CK, SourceLocation BridgeKeywordLoc,
                      TypeSourceInfo *TSInfo, Expr *Operand)
      : ExplicitCastExpr(ObjCBridgedCastExprClass, TSInfo->getType(),
                         VK_PRValue, CK, Operand, 0, false, TSInfo),
        LParenLoc(LParenLoc), BridgeKeywordLoc(BridgeKeywordLoc), Kind(Kind) {}

  /// Construct an empty Objective-C bridged cast.
  explicit ObjCBridgedCastExpr(EmptyShell Shell)
      : ExplicitCastExpr(ObjCBridgedCastExprClass, Shell, 0, false) {}

  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Determine which kind of bridge is being performed via this cast.
  ObjCBridgeCastKind getBridgeKind() const {
    return static_cast<ObjCBridgeCastKind>(Kind);
  }

  /// Retrieve the kind of bridge being performed as a string.
  StringRef getBridgeKindName() const;

  /// The location of the bridge keyword.
  SourceLocation getBridgeKeywordLoc() const { return BridgeKeywordLoc; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return LParenLoc; }

  SourceLocation getEndLoc() const LLVM_READONLY {
    return getSubExpr()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCBridgedCastExprClass;
  }
};

/// A runtime availability query.
///
/// There are 2 ways to spell this node:
/// \code
///   @available(macos 10.10, ios 8, *); // Objective-C
///   __builtin_available(macos 10.10, ios 8, *); // C, C++, and Objective-C
/// \endcode
///
/// Note that we only need to keep track of one \c VersionTuple here, which is
/// the one that corresponds to the current deployment target. This is meant to
/// be used in the condition of an \c if, but it is also usable as top level
/// expressions.
///
class ObjCAvailabilityCheckExpr : public Expr {
  friend class ASTStmtReader;

  VersionTuple VersionToCheck;
  SourceLocation AtLoc, RParen;

public:
  ObjCAvailabilityCheckExpr(VersionTuple VersionToCheck, SourceLocation AtLoc,
                            SourceLocation RParen, QualType Ty)
      : Expr(ObjCAvailabilityCheckExprClass, Ty, VK_PRValue, OK_Ordinary),
        VersionToCheck(VersionToCheck), AtLoc(AtLoc), RParen(RParen) {
    setDependence(ExprDependence::None);
  }

  explicit ObjCAvailabilityCheckExpr(EmptyShell Shell)
      : Expr(ObjCAvailabilityCheckExprClass, Shell) {}

  SourceLocation getBeginLoc() const { return AtLoc; }
  SourceLocation getEndLoc() const { return RParen; }
  SourceRange getSourceRange() const { return {AtLoc, RParen}; }

  /// This may be '*', in which case this should fold to true.
  bool hasVersion() const { return !VersionToCheck.empty(); }
  VersionTuple getVersion() const { return VersionToCheck; }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCAvailabilityCheckExprClass;
  }
};

} // namespace clang

#endif // LLVM_CLANG_AST_EXPROBJC_H

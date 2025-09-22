//=== ASTTableGen.h - Common definitions for AST node tablegen --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_AST_TABLEGEN_H
#define CLANG_AST_TABLEGEN_H

#include "llvm/TableGen/Record.h"
#include "llvm/ADT/STLExtras.h"
#include <optional>

// These are spellings in the tblgen files.

#define HasPropertiesClassName "HasProperties"

// ASTNodes and their common fields.  `Base` is actually defined
// in subclasses, but it's still common across the hierarchies.
#define ASTNodeClassName "ASTNode"
#define BaseFieldName "Base"
#define AbstractFieldName "Abstract"

// Comment node hierarchy.
#define CommentNodeClassName "CommentNode"

// Decl node hierarchy.
#define DeclNodeClassName "DeclNode"
#define DeclContextNodeClassName "DeclContext"

// Stmt node hierarchy.
#define StmtNodeClassName "StmtNode"

// Type node hierarchy.
#define TypeNodeClassName "TypeNode"
#define AlwaysDependentClassName "AlwaysDependent"
#define NeverCanonicalClassName "NeverCanonical"
#define NeverCanonicalUnlessDependentClassName "NeverCanonicalUnlessDependent"
#define LeafTypeClassName "LeafType"

// Cases of various non-ASTNode structured types like DeclarationName.
#define TypeKindClassName "PropertyTypeKind"
#define KindTypeFieldName "KindType"
#define KindPropertyNameFieldName "KindPropertyName"
#define TypeCaseClassName "PropertyTypeCase"

// Properties of AST nodes.
#define PropertyClassName "Property"
#define ClassFieldName "Class"
#define NameFieldName "Name"
#define TypeFieldName "Type"
#define ReadFieldName "Read"

// Types of properties.
#define PropertyTypeClassName "PropertyType"
#define CXXTypeNameFieldName "CXXName"
#define PassByReferenceFieldName "PassByReference"
#define ConstWhenWritingFieldName "ConstWhenWriting"
#define ConditionalCodeFieldName "Conditional"
#define PackOptionalCodeFieldName "PackOptional"
#define UnpackOptionalCodeFieldName "UnpackOptional"
#define BufferElementTypesFieldName "BufferElementTypes"
#define ArrayTypeClassName "Array"
#define ArrayElementTypeFieldName "Element"
#define OptionalTypeClassName "Optional"
#define OptionalElementTypeFieldName "Element"
#define SubclassPropertyTypeClassName "SubclassPropertyType"
#define SubclassBaseTypeFieldName "Base"
#define SubclassClassNameFieldName "SubclassName"
#define EnumPropertyTypeClassName "EnumPropertyType"

// Write helper rules.
#define ReadHelperRuleClassName "ReadHelper"
#define HelperCodeFieldName "Code"

// Creation rules.
#define CreationRuleClassName "Creator"
#define CreateFieldName "Create"

// Override rules.
#define OverrideRuleClassName "Override"
#define IgnoredPropertiesFieldName "IgnoredProperties"

namespace clang {
namespace tblgen {

class WrappedRecord {
  llvm::Record *Record;

protected:
  WrappedRecord(llvm::Record *record = nullptr) : Record(record) {}

  llvm::Record *get() const {
    assert(Record && "accessing null record");
    return Record;
  }

public:
  llvm::Record *getRecord() const { return Record; }

  explicit operator bool() const { return Record != nullptr; }

  llvm::ArrayRef<llvm::SMLoc> getLoc() const {
    return get()->getLoc();
  }

  /// Does the node inherit from the given TableGen class?
  bool isSubClassOf(llvm::StringRef className) const {
    return get()->isSubClassOf(className);
  }

  template <class NodeClass>
  NodeClass getAs() const {
    return (isSubClassOf(NodeClass::getTableGenNodeClassName())
              ? NodeClass(get()) : NodeClass());
  }

  friend bool operator<(WrappedRecord lhs, WrappedRecord rhs) {
    assert(lhs && rhs && "sorting null nodes");
    return lhs.get()->getName() < rhs.get()->getName();
  }
  friend bool operator>(WrappedRecord lhs, WrappedRecord rhs) {
    return rhs < lhs;
  }
  friend bool operator<=(WrappedRecord lhs, WrappedRecord rhs) {
    return !(rhs < lhs);
  }
  friend bool operator>=(WrappedRecord lhs, WrappedRecord rhs) {
    return !(lhs < rhs);
  }
  friend bool operator==(WrappedRecord lhs, WrappedRecord rhs) {
    // This should handle null nodes.
    return lhs.getRecord() == rhs.getRecord();
  }
  friend bool operator!=(WrappedRecord lhs, WrappedRecord rhs) {
    return !(lhs == rhs);
  }
};

/// Anything in the AST that has properties.
class HasProperties : public WrappedRecord {
public:
  static constexpr llvm::StringRef ClassName = HasPropertiesClassName;

  HasProperties(llvm::Record *record = nullptr) : WrappedRecord(record) {}

  llvm::StringRef getName() const;

  static llvm::StringRef getTableGenNodeClassName() {
    return HasPropertiesClassName;
  }
};

/// An (optional) reference to a TableGen node representing a class
/// in one of Clang's AST hierarchies.
class ASTNode : public HasProperties {
public:
  ASTNode(llvm::Record *record = nullptr) : HasProperties(record) {}

  llvm::StringRef getName() const {
    return get()->getName();
  }

  /// Return the node for the base, if there is one.
  ASTNode getBase() const {
    return get()->getValueAsOptionalDef(BaseFieldName);
  }

  /// Is the corresponding class abstract?
  bool isAbstract() const {
    return get()->getValueAsBit(AbstractFieldName);
  }

  static llvm::StringRef getTableGenNodeClassName() {
    return ASTNodeClassName;
  }
};

class DeclNode : public ASTNode {
public:
  DeclNode(llvm::Record *record = nullptr) : ASTNode(record) {}

  llvm::StringRef getId() const;
  std::string getClassName() const;
  DeclNode getBase() const { return DeclNode(ASTNode::getBase().getRecord()); }

  static llvm::StringRef getASTHierarchyName() {
    return "Decl";
  }
  static llvm::StringRef getASTIdTypeName() {
    return "Decl::Kind";
  }
  static llvm::StringRef getASTIdAccessorName() {
    return "getKind";
  }
  static llvm::StringRef getTableGenNodeClassName() {
    return DeclNodeClassName;
  }
};

class TypeNode : public ASTNode {
public:
  TypeNode(llvm::Record *record = nullptr) : ASTNode(record) {}

  llvm::StringRef getId() const;
  llvm::StringRef getClassName() const;
  TypeNode getBase() const { return TypeNode(ASTNode::getBase().getRecord()); }

  static llvm::StringRef getASTHierarchyName() {
    return "Type";
  }
  static llvm::StringRef getASTIdTypeName() {
    return "Type::TypeClass";
  }
  static llvm::StringRef getASTIdAccessorName() {
    return "getTypeClass";
  }
  static llvm::StringRef getTableGenNodeClassName() {
    return TypeNodeClassName;
  }
};

class StmtNode : public ASTNode {
public:
  StmtNode(llvm::Record *record = nullptr) : ASTNode(record) {}

  std::string getId() const;
  llvm::StringRef getClassName() const;
  StmtNode getBase() const { return StmtNode(ASTNode::getBase().getRecord()); }

  static llvm::StringRef getASTHierarchyName() {
    return "Stmt";
  }
  static llvm::StringRef getASTIdTypeName() {
    return "Stmt::StmtClass";
  }
  static llvm::StringRef getASTIdAccessorName() {
    return "getStmtClass";
  }
  static llvm::StringRef getTableGenNodeClassName() {
    return StmtNodeClassName;
  }
};

/// The type of a property.
class PropertyType : public WrappedRecord {
public:
  PropertyType(llvm::Record *record = nullptr) : WrappedRecord(record) {}

  /// Is this a generic specialization (i.e. `Array<T>` or `Optional<T>`)?
  bool isGenericSpecialization() const {
    return get()->isAnonymous();
  }

  /// The abstract type name of the property.  Doesn't work for generic
  /// specializations.
  llvm::StringRef getAbstractTypeName() const {
    return get()->getName();
  }

  /// The C++ type name of the property.  Doesn't work for generic
  /// specializations.
  llvm::StringRef getCXXTypeName() const {
    return get()->getValueAsString(CXXTypeNameFieldName);
  }
  void emitCXXValueTypeName(bool forRead, llvm::raw_ostream &out) const;

  /// Whether the C++ type should be passed around by reference.
  bool shouldPassByReference() const {
    return get()->getValueAsBit(PassByReferenceFieldName);
  }

  /// Whether the C++ type should have 'const' prepended when working with
  /// a value of the type being written.
  bool isConstWhenWriting() const {
    return get()->getValueAsBit(ConstWhenWritingFieldName);
  }

  /// If this is `Array<T>`, return `T`; otherwise return null.
  PropertyType getArrayElementType() const {
    if (isSubClassOf(ArrayTypeClassName))
      return get()->getValueAsDef(ArrayElementTypeFieldName);
    return nullptr;
  }

  /// If this is `Optional<T>`, return `T`; otherwise return null.
  PropertyType getOptionalElementType() const {
    if (isSubClassOf(OptionalTypeClassName))
      return get()->getValueAsDef(OptionalElementTypeFieldName);
    return nullptr;
  }

  /// If this is a subclass type, return its superclass type.
  PropertyType getSuperclassType() const {
    if (isSubClassOf(SubclassPropertyTypeClassName))
      return get()->getValueAsDef(SubclassBaseTypeFieldName);
    return nullptr;
  }

  // Given that this is a subclass type, return the C++ name of its
  // subclass type.  This is just the bare class name, suitable for
  // use in `cast<>`.
  llvm::StringRef getSubclassClassName() const {
    return get()->getValueAsString(SubclassClassNameFieldName);
  }

  /// Does this represent an enum type?
  bool isEnum() const {
    return isSubClassOf(EnumPropertyTypeClassName);
  }

  llvm::StringRef getPackOptionalCode() const {
    return get()->getValueAsString(PackOptionalCodeFieldName);
  }

  llvm::StringRef getUnpackOptionalCode() const {
    return get()->getValueAsString(UnpackOptionalCodeFieldName);
  }

  std::vector<llvm::Record*> getBufferElementTypes() const {
    return get()->getValueAsListOfDefs(BufferElementTypesFieldName);
  }

  static llvm::StringRef getTableGenNodeClassName() {
    return PropertyTypeClassName;
  }
};

/// A rule for returning the kind of a type.
class TypeKindRule : public WrappedRecord {
public:
  TypeKindRule(llvm::Record *record = nullptr) : WrappedRecord(record) {}

  /// Return the type to which this applies.
  PropertyType getParentType() const {
    return get()->getValueAsDef(TypeFieldName);
  }

  /// Return the type of the kind.
  PropertyType getKindType() const {
    return get()->getValueAsDef(KindTypeFieldName);
  }

  /// Return the name to use for the kind property.
  llvm::StringRef getKindPropertyName() const {
    return get()->getValueAsString(KindPropertyNameFieldName);
  }

  /// Return the code for reading the kind value.
  llvm::StringRef getReadCode() const {
    return get()->getValueAsString(ReadFieldName);
  }

  static llvm::StringRef getTableGenNodeClassName() {
    return TypeKindClassName;
  }
};

/// An implementation case of a property type.
class TypeCase : public HasProperties {
public:
  TypeCase(llvm::Record *record = nullptr) : HasProperties(record) {}

  /// Return the name of this case.
  llvm::StringRef getCaseName() const {
    return get()->getValueAsString(NameFieldName);
  }

  /// Return the type of which this is a case.
  PropertyType getParentType() const {
    return get()->getValueAsDef(TypeFieldName);
  }

  static llvm::StringRef getTableGenNodeClassName() {
    return TypeCaseClassName;
  }
};

/// A property of an AST node.
class Property : public WrappedRecord {
public:
  Property(llvm::Record *record = nullptr) : WrappedRecord(record) {}

  /// Return the name of this property.
  llvm::StringRef getName() const {
    return get()->getValueAsString(NameFieldName);
  }

  /// Return the type of this property.
  PropertyType getType() const {
    return get()->getValueAsDef(TypeFieldName);
  }

  /// Return the class of which this is a property.
  HasProperties getClass() const {
    return get()->getValueAsDef(ClassFieldName);
  }

  /// Return the code for reading this property.
  llvm::StringRef getReadCode() const {
    return get()->getValueAsString(ReadFieldName);
  }

  /// Return the code for determining whether to add this property.
  llvm::StringRef getCondition() const {
    return get()->getValueAsString(ConditionalCodeFieldName);
  }

  static llvm::StringRef getTableGenNodeClassName() {
    return PropertyClassName;
  }
};

/// A rule for running some helper code for reading properties from
/// a value (which is actually done when writing the value out).
class ReadHelperRule : public WrappedRecord {
public:
  ReadHelperRule(llvm::Record *record = nullptr) : WrappedRecord(record) {}

  /// Return the class for which this is a creation rule.
  /// Should never be abstract.
  HasProperties getClass() const {
    return get()->getValueAsDef(ClassFieldName);
  }

  llvm::StringRef getHelperCode() const {
    return get()->getValueAsString(HelperCodeFieldName);
  }

  static llvm::StringRef getTableGenNodeClassName() {
    return ReadHelperRuleClassName;
  }
};

/// A rule for how to create an AST node from its properties.
class CreationRule : public WrappedRecord {
public:
  CreationRule(llvm::Record *record = nullptr) : WrappedRecord(record) {}

  /// Return the class for which this is a creation rule.
  /// Should never be abstract.
  HasProperties getClass() const {
    return get()->getValueAsDef(ClassFieldName);
  }

  llvm::StringRef getCreationCode() const {
    return get()->getValueAsString(CreateFieldName);
  }

  static llvm::StringRef getTableGenNodeClassName() {
    return CreationRuleClassName;
  }
};

/// A rule which overrides the standard rules for serializing an AST node.
class OverrideRule : public WrappedRecord {
public:
  OverrideRule(llvm::Record *record = nullptr) : WrappedRecord(record) {}

  /// Return the class for which this is an override rule.
  /// Should never be abstract.
  HasProperties getClass() const {
    return get()->getValueAsDef(ClassFieldName);
  }

  /// Return a set of properties that are unnecessary when serializing
  /// this AST node.  Generally this is used for inherited properties
  /// that are derived for this subclass.
  std::vector<llvm::StringRef> getIgnoredProperties() const {
    return get()->getValueAsListOfStrings(IgnoredPropertiesFieldName);
  }

  static llvm::StringRef getTableGenNodeClassName() {
    return OverrideRuleClassName;
  }
};

/// A visitor for an AST node hierarchy.  Note that `base` can be null for
/// the root class.
template <class NodeClass>
using ASTNodeHierarchyVisitor =
  llvm::function_ref<void(NodeClass node, NodeClass base)>;

void visitASTNodeHierarchyImpl(llvm::RecordKeeper &records,
                               llvm::StringRef nodeClassName,
                               ASTNodeHierarchyVisitor<ASTNode> visit);

template <class NodeClass>
void visitASTNodeHierarchy(llvm::RecordKeeper &records,
                           ASTNodeHierarchyVisitor<NodeClass> visit) {
  visitASTNodeHierarchyImpl(records, NodeClass::getTableGenNodeClassName(),
                            [visit](ASTNode node, ASTNode base) {
                              visit(NodeClass(node.getRecord()),
                                    NodeClass(base.getRecord()));
                            });
}

} // end namespace clang::tblgen
} // end namespace clang

#endif

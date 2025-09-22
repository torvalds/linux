//===--- ASTMutationListener.h - AST Mutation Interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTMutationListener interface.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_AST_ASTMUTATIONLISTENER_H
#define LLVM_CLANG_AST_ASTMUTATIONLISTENER_H

namespace clang {
  class Attr;
  class ClassTemplateDecl;
  class ClassTemplateSpecializationDecl;
  class ConstructorUsingShadowDecl;
  class CXXDestructorDecl;
  class CXXRecordDecl;
  class Decl;
  class DeclContext;
  class Expr;
  class FieldDecl;
  class FunctionDecl;
  class FunctionTemplateDecl;
  class Module;
  class NamedDecl;
  class NamespaceDecl;
  class ObjCCategoryDecl;
  class ObjCContainerDecl;
  class ObjCInterfaceDecl;
  class ObjCPropertyDecl;
  class ParmVarDecl;
  class QualType;
  class RecordDecl;
  class TagDecl;
  class TranslationUnitDecl;
  class ValueDecl;
  class VarDecl;
  class VarTemplateDecl;
  class VarTemplateSpecializationDecl;

/// An abstract interface that should be implemented by listeners
/// that want to be notified when an AST entity gets modified after its
/// initial creation.
class ASTMutationListener {
public:
  virtual ~ASTMutationListener();

  /// A new TagDecl definition was completed.
  virtual void CompletedTagDefinition(const TagDecl *D) { }

  /// A new declaration with name has been added to a DeclContext.
  virtual void AddedVisibleDecl(const DeclContext *DC, const Decl *D) {}

  /// An implicit member was added after the definition was completed.
  virtual void AddedCXXImplicitMember(const CXXRecordDecl *RD, const Decl *D) {}

  /// A template specialization (or partial one) was added to the
  /// template declaration.
  virtual void AddedCXXTemplateSpecialization(const ClassTemplateDecl *TD,
                                    const ClassTemplateSpecializationDecl *D) {}

  /// A template specialization (or partial one) was added to the
  /// template declaration.
  virtual void
  AddedCXXTemplateSpecialization(const VarTemplateDecl *TD,
                                 const VarTemplateSpecializationDecl *D) {}

  /// A template specialization (or partial one) was added to the
  /// template declaration.
  virtual void AddedCXXTemplateSpecialization(const FunctionTemplateDecl *TD,
                                              const FunctionDecl *D) {}

  /// A function's exception specification has been evaluated or
  /// instantiated.
  virtual void ResolvedExceptionSpec(const FunctionDecl *FD) {}

  /// A function's return type has been deduced.
  virtual void DeducedReturnType(const FunctionDecl *FD, QualType ReturnType);

  /// A virtual destructor's operator delete has been resolved.
  virtual void ResolvedOperatorDelete(const CXXDestructorDecl *DD,
                                      const FunctionDecl *Delete,
                                      Expr *ThisArg) {}

  /// An implicit member got a definition.
  virtual void CompletedImplicitDefinition(const FunctionDecl *D) {}

  /// The instantiation of a templated function or variable was
  /// requested. In particular, the point of instantiation and template
  /// specialization kind of \p D may have changed.
  virtual void InstantiationRequested(const ValueDecl *D) {}

  /// A templated variable's definition was implicitly instantiated.
  virtual void VariableDefinitionInstantiated(const VarDecl *D) {}

  /// A function template's definition was instantiated.
  virtual void FunctionDefinitionInstantiated(const FunctionDecl *D) {}

  /// A default argument was instantiated.
  virtual void DefaultArgumentInstantiated(const ParmVarDecl *D) {}

  /// A default member initializer was instantiated.
  virtual void DefaultMemberInitializerInstantiated(const FieldDecl *D) {}

  /// A new objc category class was added for an interface.
  virtual void AddedObjCCategoryToInterface(const ObjCCategoryDecl *CatD,
                                            const ObjCInterfaceDecl *IFD) {}

  /// A declaration is marked used which was not previously marked used.
  ///
  /// \param D the declaration marked used
  virtual void DeclarationMarkedUsed(const Decl *D) {}

  /// A declaration is marked as OpenMP threadprivate which was not
  /// previously marked as threadprivate.
  ///
  /// \param D the declaration marked OpenMP threadprivate.
  virtual void DeclarationMarkedOpenMPThreadPrivate(const Decl *D) {}

  /// A declaration is marked as OpenMP declaretarget which was not
  /// previously marked as declaretarget.
  ///
  /// \param D the declaration marked OpenMP declaretarget.
  /// \param Attr the added attribute.
  virtual void DeclarationMarkedOpenMPDeclareTarget(const Decl *D,
                                                    const Attr *Attr) {}

  /// A declaration is marked as a variable with OpenMP allocator.
  ///
  /// \param D the declaration marked as a variable with OpenMP allocator.
  virtual void DeclarationMarkedOpenMPAllocate(const Decl *D, const Attr *A) {}

  /// A definition has been made visible by being redefined locally.
  ///
  /// \param D The definition that was previously not visible.
  /// \param M The containing module in which the definition was made visible,
  ///        if any.
  virtual void RedefinedHiddenDefinition(const NamedDecl *D, Module *M) {}

  /// An attribute was added to a RecordDecl
  ///
  /// \param Attr The attribute that was added to the Record
  ///
  /// \param Record The RecordDecl that got a new attribute
  virtual void AddedAttributeToRecord(const Attr *Attr,
                                      const RecordDecl *Record) {}

  /// The parser find the named module declaration.
  virtual void EnteringModulePurview() {}

  /// An mangling number was added to a Decl
  ///
  /// \param D The decl that got a mangling number
  ///
  /// \param Number The mangling number that was added to the Decl
  virtual void AddedManglingNumber(const Decl *D, unsigned Number) {}

  /// An static local number was added to a Decl
  ///
  /// \param D The decl that got a static local number
  ///
  /// \param Number The static local number that was added to the Decl
  virtual void AddedStaticLocalNumbers(const Decl *D, unsigned Number) {}

  /// An anonymous namespace was added the translation unit decl
  ///
  /// \param TU The translation unit decl that got a new anonymous namespace
  ///
  /// \param AnonNamespace The anonymous namespace that was added
  virtual void AddedAnonymousNamespace(const TranslationUnitDecl *TU,
                                       NamespaceDecl *AnonNamespace) {}

  // NOTE: If new methods are added they should also be added to
  // MultiplexASTMutationListener.
};

} // end namespace clang

#endif

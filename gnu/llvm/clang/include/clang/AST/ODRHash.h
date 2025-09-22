//===-- ODRHash.h - Hashing to diagnose ODR failures ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the ODRHash class, which calculates
/// a hash based on AST nodes, which is stable across different runs.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ODRHASH_H
#define LLVM_CLANG_AST_ODRHASH_H

#include "clang/AST/DeclarationName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TemplateBase.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {

class APValue;
class Decl;
class IdentifierInfo;
class NestedNameSpecifier;
class Stmt;
class TemplateParameterList;

// ODRHash is used to calculate a hash based on AST node contents that
// does not rely on pointer addresses.  This allows the hash to not vary
// between runs and is usable to detect ODR problems in modules.  To use,
// construct an ODRHash object, then call Add* methods over the nodes that
// need to be hashed.  Then call CalculateHash to get the hash value.
// Typically, only one Add* call is needed.  clear can be called to reuse the
// object.
class ODRHash {
  // Use DenseMaps to convert from DeclarationName and Type pointers
  // to an index value.
  llvm::DenseMap<DeclarationName, unsigned> DeclNameMap;

  // Save space by processing bools at the end.
  llvm::SmallVector<bool, 128> Bools;

  llvm::FoldingSetNodeID ID;

public:
  ODRHash() {}

  // Use this for ODR checking classes between modules.  This method compares
  // more information than the AddDecl class.
  void AddCXXRecordDecl(const CXXRecordDecl *Record);

  // Use this for ODR checking records in C/Objective-C between modules. This
  // method compares more information than the AddDecl class.
  void AddRecordDecl(const RecordDecl *Record);

  // Use this for ODR checking ObjC interfaces. This
  // method compares more information than the AddDecl class.
  void AddObjCInterfaceDecl(const ObjCInterfaceDecl *Record);

  // Use this for ODR checking functions between modules.  This method compares
  // more information than the AddDecl class.  SkipBody will process the
  // hash as if the function has no body.
  void AddFunctionDecl(const FunctionDecl *Function, bool SkipBody = false);

  // Use this for ODR checking enums between modules.  This method compares
  // more information than the AddDecl class.
  void AddEnumDecl(const EnumDecl *Enum);

  // Use this for ODR checking ObjC protocols. This
  // method compares more information than the AddDecl class.
  void AddObjCProtocolDecl(const ObjCProtocolDecl *P);

  // Process SubDecls of the main Decl.  This method calls the DeclVisitor
  // while AddDecl does not.
  void AddSubDecl(const Decl *D);

  // Reset the object for reuse.
  void clear();

  // Add booleans to ID and uses it to calculate the hash.
  unsigned CalculateHash();

  // Add AST nodes that need to be processed.
  void AddDecl(const Decl *D);
  void AddType(const Type *T);
  void AddQualType(QualType T);
  void AddStmt(const Stmt *S);
  void AddIdentifierInfo(const IdentifierInfo *II);
  void AddNestedNameSpecifier(const NestedNameSpecifier *NNS);
  void AddTemplateName(TemplateName Name);
  void AddDeclarationName(DeclarationName Name, bool TreatAsDecl = false);
  void AddTemplateArgument(TemplateArgument TA);
  void AddTemplateParameterList(const TemplateParameterList *TPL);

  // Save booleans until the end to lower the size of data to process.
  void AddBoolean(bool value);

  void AddStructuralValue(const APValue &);

  static bool isSubDeclToBeProcessed(const Decl *D, const DeclContext *Parent);

private:
  void AddDeclarationNameImpl(DeclarationName Name);
};

}  // end namespace clang

#endif

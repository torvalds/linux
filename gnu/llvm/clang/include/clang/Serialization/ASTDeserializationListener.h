//===- ASTDeserializationListener.h - Decl/Type PCH Read Events -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTDeserializationListener class, which is notified
//  by the ASTReader whenever a type or declaration is deserialized.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_ASTDESERIALIZATIONLISTENER_H
#define LLVM_CLANG_SERIALIZATION_ASTDESERIALIZATIONLISTENER_H

#include "clang/Basic/IdentifierTable.h"
#include "clang/Serialization/ASTBitCodes.h"

namespace clang {

class Decl;
class ASTReader;
class QualType;
class MacroDefinitionRecord;
class MacroInfo;
class Module;
class SourceLocation;

class ASTDeserializationListener {
public:
  virtual ~ASTDeserializationListener();

  /// The ASTReader was initialized.
  virtual void ReaderInitialized(ASTReader *Reader) { }

  /// An identifier was deserialized from the AST file.
  virtual void IdentifierRead(serialization::IdentifierID ID,
                              IdentifierInfo *II) { }
  /// A macro was read from the AST file.
  virtual void MacroRead(serialization::MacroID ID, MacroInfo *MI) { }
  /// A type was deserialized from the AST file. The ID here has the
  ///        qualifier bits already removed, and T is guaranteed to be locally
  ///        unqualified.
  virtual void TypeRead(serialization::TypeIdx Idx, QualType T) { }
  /// A decl was deserialized from the AST file.
  virtual void DeclRead(GlobalDeclID ID, const Decl *D) {}
  /// A selector was read from the AST file.
  virtual void SelectorRead(serialization::SelectorID iD, Selector Sel) {}
  /// A macro definition was read from the AST file.
  virtual void MacroDefinitionRead(serialization::PreprocessedEntityID,
                                   MacroDefinitionRecord *MD) {}
  /// A module definition was read from the AST file.
  virtual void ModuleRead(serialization::SubmoduleID ID, Module *Mod) {}
  /// A module import was read from the AST file.
  virtual void ModuleImportRead(serialization::SubmoduleID ID,
                                SourceLocation ImportLoc) {}
};
}

#endif

//===- USRGeneration.h - Routines for USR generation ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INDEX_USRGENERATION_H
#define LLVM_CLANG_INDEX_USRGENERATION_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
class ASTContext;
class Decl;
class MacroDefinitionRecord;
class Module;
class SourceLocation;
class SourceManager;
class QualType;

namespace index {

static inline StringRef getUSRSpacePrefix() {
  return "c:";
}

/// Generate a USR for a Decl, including the USR prefix.
/// \returns true if the results should be ignored, false otherwise.
bool generateUSRForDecl(const Decl *D, SmallVectorImpl<char> &Buf);

/// Generate a USR fragment for an Objective-C class.
void generateUSRForObjCClass(StringRef Cls, raw_ostream &OS,
                             StringRef ExtSymbolDefinedIn = "",
                             StringRef CategoryContextExtSymbolDefinedIn = "");

/// Generate a USR fragment for an Objective-C class category.
void generateUSRForObjCCategory(StringRef Cls, StringRef Cat, raw_ostream &OS,
                                StringRef ClsExtSymbolDefinedIn = "",
                                StringRef CatExtSymbolDefinedIn = "");

/// Generate a USR fragment for an Objective-C instance variable.  The
/// complete USR can be created by concatenating the USR for the
/// encompassing class with this USR fragment.
void generateUSRForObjCIvar(StringRef Ivar, raw_ostream &OS);

/// Generate a USR fragment for an Objective-C method.
void generateUSRForObjCMethod(StringRef Sel, bool IsInstanceMethod,
                              raw_ostream &OS);

/// Generate a USR fragment for an Objective-C property.
void generateUSRForObjCProperty(StringRef Prop, bool isClassProp, raw_ostream &OS);

/// Generate a USR fragment for an Objective-C protocol.
void generateUSRForObjCProtocol(StringRef Prot, raw_ostream &OS,
                                StringRef ExtSymbolDefinedIn = "");

/// Generate USR fragment for a global (non-nested) enum.
void generateUSRForGlobalEnum(StringRef EnumName, raw_ostream &OS,
                              StringRef ExtSymbolDefinedIn = "");

/// Generate a USR fragment for an enum constant.
void generateUSRForEnumConstant(StringRef EnumConstantName, raw_ostream &OS);

/// Generate a USR for a macro, including the USR prefix.
///
/// \returns true on error, false on success.
bool generateUSRForMacro(const MacroDefinitionRecord *MD,
                         const SourceManager &SM, SmallVectorImpl<char> &Buf);
bool generateUSRForMacro(StringRef MacroName, SourceLocation Loc,
                         const SourceManager &SM, SmallVectorImpl<char> &Buf);

/// Generates a USR for a type.
///
/// \return true on error, false on success.
bool generateUSRForType(QualType T, ASTContext &Ctx, SmallVectorImpl<char> &Buf);

/// Generate a USR for a module, including the USR prefix.
/// \returns true on error, false on success.
bool generateFullUSRForModule(const Module *Mod, raw_ostream &OS);

/// Generate a USR for a top-level module name, including the USR prefix.
/// \returns true on error, false on success.
bool generateFullUSRForTopLevelModuleName(StringRef ModName, raw_ostream &OS);

/// Generate a USR fragment for a module.
/// \returns true on error, false on success.
bool generateUSRFragmentForModule(const Module *Mod, raw_ostream &OS);

/// Generate a USR fragment for a module name.
/// \returns true on error, false on success.
bool generateUSRFragmentForModuleName(StringRef ModName, raw_ostream &OS);


} // namespace index
} // namespace clang

#endif // LLVM_CLANG_INDEX_USRGENERATION_H


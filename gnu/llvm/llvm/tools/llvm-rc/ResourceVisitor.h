//===-- ResourceVisitor.h ---------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This defines a base class visiting resource script resources.
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMRC_RESOURCEVISITOR_H
#define LLVM_TOOLS_LLVMRC_RESOURCEVISITOR_H

#include "llvm/Support/Error.h"

namespace llvm {
namespace rc {

class RCResource;

class CaptionStmt;
class ClassStmt;
class CharacteristicsStmt;
class ExStyleStmt;
class FontStmt;
class LanguageResource;
class StyleStmt;
class VersionStmt;
class MenuStmt;

class Visitor {
public:
  virtual Error visitNullResource(const RCResource *) = 0;
  virtual Error visitAcceleratorsResource(const RCResource *) = 0;
  virtual Error visitBitmapResource(const RCResource *) = 0;
  virtual Error visitCursorResource(const RCResource *) = 0;
  virtual Error visitDialogResource(const RCResource *) = 0;
  virtual Error visitHTMLResource(const RCResource *) = 0;
  virtual Error visitIconResource(const RCResource *) = 0;
  virtual Error visitMenuResource(const RCResource *) = 0;
  virtual Error visitMenuExResource(const RCResource *) = 0;
  virtual Error visitStringTableResource(const RCResource *) = 0;
  virtual Error visitUserDefinedResource(const RCResource *) = 0;
  virtual Error visitVersionInfoResource(const RCResource *) = 0;

  virtual Error visitCaptionStmt(const CaptionStmt *) = 0;
  virtual Error visitClassStmt(const ClassStmt *) = 0;
  virtual Error visitCharacteristicsStmt(const CharacteristicsStmt *) = 0;
  virtual Error visitExStyleStmt(const ExStyleStmt *) = 0;
  virtual Error visitFontStmt(const FontStmt *) = 0;
  virtual Error visitLanguageStmt(const LanguageResource *) = 0;
  virtual Error visitStyleStmt(const StyleStmt *) = 0;
  virtual Error visitVersionStmt(const VersionStmt *) = 0;
  virtual Error visitMenuStmt(const MenuStmt *) = 0;

  virtual ~Visitor() {}
};

} // namespace rc
} // namespace llvm

#endif

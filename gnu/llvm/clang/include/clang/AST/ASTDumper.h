//===--- ASTDumper.h - Dumping implementation for ASTs --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTDUMPER_H
#define LLVM_CLANG_AST_ASTDUMPER_H

#include "clang/AST/ASTNodeTraverser.h"
#include "clang/AST/TextNodeDumper.h"
#include "clang/Basic/SourceManager.h"

namespace clang {

class ASTDumper : public ASTNodeTraverser<ASTDumper, TextNodeDumper> {

  TextNodeDumper NodeDumper;

  raw_ostream &OS;

  const bool ShowColors;

public:
  ASTDumper(raw_ostream &OS, const ASTContext &Context, bool ShowColors)
      : NodeDumper(OS, Context, ShowColors), OS(OS), ShowColors(ShowColors) {}

  ASTDumper(raw_ostream &OS, bool ShowColors)
      : NodeDumper(OS, ShowColors), OS(OS), ShowColors(ShowColors) {}

  TextNodeDumper &doGetNodeDelegate() { return NodeDumper; }

  void dumpInvalidDeclContext(const DeclContext *DC);
  void dumpLookups(const DeclContext *DC, bool DumpDecls);

  template <typename SpecializationDecl>
  void dumpTemplateDeclSpecialization(const SpecializationDecl *D,
                                      bool DumpExplicitInst, bool DumpRefOnly);
  template <typename TemplateDecl>
  void dumpTemplateDecl(const TemplateDecl *D, bool DumpExplicitInst);

  void VisitFunctionTemplateDecl(const FunctionTemplateDecl *D);
  void VisitClassTemplateDecl(const ClassTemplateDecl *D);
  void VisitVarTemplateDecl(const VarTemplateDecl *D);
};

} // namespace clang

#endif

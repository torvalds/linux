//===- Attribute.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Example clang plugin which adds an an annotation to file-scope declarations
// with the 'example' attribute.
//
// This plugin is used by clang/test/Frontend/plugin-attribute tests.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "llvm/IR/Attributes.h"
using namespace clang;

namespace {

struct ExampleAttrInfo : public ParsedAttrInfo {
  ExampleAttrInfo() {
    // Can take up to 15 optional arguments, to emulate accepting a variadic
    // number of arguments. This just illustrates how many arguments a
    // `ParsedAttrInfo` can hold, we will not use that much in this example.
    OptArgs = 15;
    // GNU-style __attribute__(("example")) and C++/C23-style [[example]] and
    // [[plugin::example]] supported.
    static constexpr Spelling S[] = {{ParsedAttr::AS_GNU, "example"},
                                     {ParsedAttr::AS_C23, "example"},
                                     {ParsedAttr::AS_CXX11, "example"},
                                     {ParsedAttr::AS_CXX11, "plugin::example"}};
    Spellings = S;
  }

  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    // This attribute appertains to functions only.
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    // Check if the decl is at file scope.
    if (!D->getDeclContext()->isFileContext()) {
      unsigned ID = S.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error,
          "'example' attribute only allowed at file scope");
      S.Diag(Attr.getLoc(), ID);
      return AttributeNotApplied;
    }
    // We make some rules here:
    // 1. Only accept at most 3 arguments here.
    // 2. The first argument must be a string literal if it exists.
    if (Attr.getNumArgs() > 3) {
      unsigned ID = S.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error,
          "'example' attribute only accepts at most three arguments");
      S.Diag(Attr.getLoc(), ID);
      return AttributeNotApplied;
    }
    // If there are arguments, the first argument should be a string literal.
    if (Attr.getNumArgs() > 0) {
      auto *Arg0 = Attr.getArgAsExpr(0);
      StringLiteral *Literal =
          dyn_cast<StringLiteral>(Arg0->IgnoreParenCasts());
      if (!Literal) {
        unsigned ID = S.getDiagnostics().getCustomDiagID(
            DiagnosticsEngine::Error, "first argument to the 'example' "
                                      "attribute must be a string literal");
        S.Diag(Attr.getLoc(), ID);
        return AttributeNotApplied;
      }
      SmallVector<Expr *, 16> ArgsBuf;
      for (unsigned i = 0; i < Attr.getNumArgs(); i++) {
        ArgsBuf.push_back(Attr.getArgAsExpr(i));
      }
      D->addAttr(AnnotateAttr::Create(S.Context, "example", ArgsBuf.data(),
                                      ArgsBuf.size(), Attr.getRange()));
    } else {
      // Attach an annotate attribute to the Decl.
      D->addAttr(AnnotateAttr::Create(S.Context, "example", nullptr, 0,
                                      Attr.getRange()));
    }
    return AttributeApplied;
  }
};

} // namespace

static ParsedAttrInfoRegistry::Add<ExampleAttrInfo> X("example", "");

//===--- ASTConsumers.h - ASTConsumer implementations -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AST Consumers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_REWRITE_FRONTEND_ASTCONSUMERS_H
#define LLVM_CLANG_REWRITE_FRONTEND_ASTCONSUMERS_H

#include "clang/Basic/LLVM.h"
#include <memory>
#include <string>

namespace clang {

class ASTConsumer;
class DiagnosticsEngine;
class LangOptions;
class Preprocessor;

// ObjC rewriter: attempts to rewrite ObjC constructs into pure C code.
// This is considered experimental, and only works with Apple's ObjC runtime.
std::unique_ptr<ASTConsumer>
CreateObjCRewriter(const std::string &InFile, std::unique_ptr<raw_ostream> OS,
                   DiagnosticsEngine &Diags, const LangOptions &LOpts,
                   bool SilenceRewriteMacroWarning);
std::unique_ptr<ASTConsumer>
CreateModernObjCRewriter(const std::string &InFile,
                         std::unique_ptr<raw_ostream> OS,
                         DiagnosticsEngine &Diags, const LangOptions &LOpts,
                         bool SilenceRewriteMacroWarning, bool LineInfo);

/// CreateHTMLPrinter - Create an AST consumer which rewrites source code to
/// HTML with syntax highlighting suitable for viewing in a web-browser.
std::unique_ptr<ASTConsumer> CreateHTMLPrinter(std::unique_ptr<raw_ostream> OS,
                                               Preprocessor &PP,
                                               bool SyntaxHighlight = true,
                                               bool HighlightMacros = true);

} // end clang namespace

#endif

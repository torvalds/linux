//===-- DataCollection.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DataCollection.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

namespace clang {
namespace data_collection {

/// Prints the macro name that contains the given SourceLocation into the given
/// raw_string_ostream.
static void printMacroName(llvm::raw_string_ostream &MacroStack,
                           ASTContext &Context, SourceLocation Loc) {
  MacroStack << Lexer::getImmediateMacroName(Loc, Context.getSourceManager(),
                                             Context.getLangOpts());

  // Add an empty space at the end as a padding to prevent
  // that macro names concatenate to the names of other macros.
  MacroStack << " ";
}

/// Returns a string that represents all macro expansions that expanded into the
/// given SourceLocation.
///
/// If 'getMacroStack(A) == getMacroStack(B)' is true, then the SourceLocations
/// A and B are expanded from the same macros in the same order.
std::string getMacroStack(SourceLocation Loc, ASTContext &Context) {
  std::string MacroStack;
  llvm::raw_string_ostream MacroStackStream(MacroStack);
  SourceManager &SM = Context.getSourceManager();

  // Iterate over all macros that expanded into the given SourceLocation.
  while (Loc.isMacroID()) {
    // Add the macro name to the stream.
    printMacroName(MacroStackStream, Context, Loc);
    Loc = SM.getImmediateMacroCallerLoc(Loc);
  }
  MacroStackStream.flush();
  return MacroStack;
}

} // end namespace data_collection
} // end namespace clang

//===--- SortJavaScriptImports.cpp - Sort ES6 Imports -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a sort operation for JavaScript ES6 imports.
///
//===----------------------------------------------------------------------===//

#include "SortJavaScriptImports.h"
#include "TokenAnalyzer.h"
#include "TokenAnnotator.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Format/Format.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include <algorithm>
#include <string>

#define DEBUG_TYPE "format-formatter"

namespace clang {
namespace format {

class FormatTokenLexer;

// An imported symbol in a JavaScript ES6 import/export, possibly aliased.
struct JsImportedSymbol {
  StringRef Symbol;
  StringRef Alias;
  SourceRange Range;

  bool operator==(const JsImportedSymbol &RHS) const {
    // Ignore Range for comparison, it is only used to stitch code together,
    // but imports at different code locations are still conceptually the same.
    return Symbol == RHS.Symbol && Alias == RHS.Alias;
  }
};

// An ES6 module reference.
//
// ES6 implements a module system, where individual modules (~= source files)
// can reference other modules, either importing symbols from them, or exporting
// symbols from them:
//   import {foo} from 'foo';
//   export {foo};
//   export {bar} from 'bar';
//
// `export`s with URLs are syntactic sugar for an import of the symbol from the
// URL, followed by an export of the symbol, allowing this code to treat both
// statements more or less identically, with the exception being that `export`s
// are sorted last.
//
// imports and exports support individual symbols, but also a wildcard syntax:
//   import * as prefix from 'foo';
//   export * from 'bar';
//
// This struct represents both exports and imports to build up the information
// required for sorting module references.
struct JsModuleReference {
  bool FormattingOff = false;
  bool IsExport = false;
  bool IsTypeOnly = false;
  // Module references are sorted into these categories, in order.
  enum ReferenceCategory {
    SIDE_EFFECT,     // "import 'something';"
    ABSOLUTE,        // from 'something'
    RELATIVE_PARENT, // from '../*'
    RELATIVE,        // from './*'
    ALIAS,           // import X = A.B;
  };
  ReferenceCategory Category = ReferenceCategory::SIDE_EFFECT;
  // The URL imported, e.g. `import .. from 'url';`. Empty for `export {a, b};`.
  StringRef URL;
  // Prefix from "import * as prefix". Empty for symbol imports and `export *`.
  // Implies an empty names list.
  StringRef Prefix;
  // Default import from "import DefaultName from '...';".
  StringRef DefaultImport;
  // Symbols from `import {SymbolA, SymbolB, ...} from ...;`.
  SmallVector<JsImportedSymbol, 1> Symbols;
  // Whether some symbols were merged into this one. Controls if the module
  // reference needs re-formatting.
  bool SymbolsMerged = false;
  // The source location just after { and just before } in the import.
  // Extracted eagerly to allow modification of Symbols later on.
  SourceLocation SymbolsStart, SymbolsEnd;
  // Textual position of the import/export, including preceding and trailing
  // comments.
  SourceRange Range;
};

bool operator<(const JsModuleReference &LHS, const JsModuleReference &RHS) {
  if (LHS.IsExport != RHS.IsExport)
    return LHS.IsExport < RHS.IsExport;
  if (LHS.Category != RHS.Category)
    return LHS.Category < RHS.Category;
  if (LHS.Category == JsModuleReference::ReferenceCategory::SIDE_EFFECT ||
      LHS.Category == JsModuleReference::ReferenceCategory::ALIAS) {
    // Side effect imports and aliases might be ordering sensitive. Consider
    // them equal so that they maintain their relative order in the stable sort
    // below. This retains transitivity because LHS.Category == RHS.Category
    // here.
    return false;
  }
  // Empty URLs sort *last* (for export {...};).
  if (LHS.URL.empty() != RHS.URL.empty())
    return LHS.URL.empty() < RHS.URL.empty();
  if (int Res = LHS.URL.compare_insensitive(RHS.URL))
    return Res < 0;
  // '*' imports (with prefix) sort before {a, b, ...} imports.
  if (LHS.Prefix.empty() != RHS.Prefix.empty())
    return LHS.Prefix.empty() < RHS.Prefix.empty();
  if (LHS.Prefix != RHS.Prefix)
    return LHS.Prefix > RHS.Prefix;
  return false;
}

// JavaScriptImportSorter sorts JavaScript ES6 imports and exports. It is
// implemented as a TokenAnalyzer because ES6 imports have substantial syntactic
// structure, making it messy to sort them using regular expressions.
class JavaScriptImportSorter : public TokenAnalyzer {
public:
  JavaScriptImportSorter(const Environment &Env, const FormatStyle &Style)
      : TokenAnalyzer(Env, Style),
        FileContents(Env.getSourceManager().getBufferData(Env.getFileID())) {
    // FormatToken.Tok starts out in an uninitialized state.
    invalidToken.Tok.startToken();
  }

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override {
    tooling::Replacements Result;
    AffectedRangeMgr.computeAffectedLines(AnnotatedLines);

    const AdditionalKeywords &Keywords = Tokens.getKeywords();
    SmallVector<JsModuleReference, 16> References;
    AnnotatedLine *FirstNonImportLine;
    std::tie(References, FirstNonImportLine) =
        parseModuleReferences(Keywords, AnnotatedLines);

    if (References.empty())
      return {Result, 0};

    // The text range of all parsed imports, to be replaced later.
    SourceRange InsertionPoint = References[0].Range;
    InsertionPoint.setEnd(References[References.size() - 1].Range.getEnd());

    References = sortModuleReferences(References);

    std::string ReferencesText;
    for (unsigned I = 0, E = References.size(); I != E; ++I) {
      JsModuleReference Reference = References[I];
      appendReference(ReferencesText, Reference);
      if (I + 1 < E) {
        // Insert breaks between imports and exports.
        ReferencesText += "\n";
        // Separate imports groups with two line breaks, but keep all exports
        // in a single group.
        if (!Reference.IsExport &&
            (Reference.IsExport != References[I + 1].IsExport ||
             Reference.Category != References[I + 1].Category)) {
          ReferencesText += "\n";
        }
      }
    }
    StringRef PreviousText = getSourceText(InsertionPoint);
    if (ReferencesText == PreviousText)
      return {Result, 0};

    // The loop above might collapse previously existing line breaks between
    // import blocks, and thus shrink the file. SortIncludes must not shrink
    // overall source length as there is currently no re-calculation of ranges
    // after applying source sorting.
    // This loop just backfills trailing spaces after the imports, which are
    // harmless and will be stripped by the subsequent formatting pass.
    // FIXME: A better long term fix is to re-calculate Ranges after sorting.
    unsigned PreviousSize = PreviousText.size();
    while (ReferencesText.size() < PreviousSize)
      ReferencesText += " ";

    // Separate references from the main code body of the file.
    if (FirstNonImportLine && FirstNonImportLine->First->NewlinesBefore < 2 &&
        !(FirstNonImportLine->First->is(tok::comment) &&
          isClangFormatOn(FirstNonImportLine->First->TokenText.trim()))) {
      ReferencesText += "\n";
    }

    LLVM_DEBUG(llvm::dbgs() << "Replacing imports:\n"
                            << PreviousText << "\nwith:\n"
                            << ReferencesText << "\n");
    auto Err = Result.add(tooling::Replacement(
        Env.getSourceManager(), CharSourceRange::getCharRange(InsertionPoint),
        ReferencesText));
    // FIXME: better error handling. For now, just print error message and skip
    // the replacement for the release version.
    if (Err) {
      llvm::errs() << toString(std::move(Err)) << "\n";
      assert(false);
    }

    return {Result, 0};
  }

private:
  FormatToken *Current = nullptr;
  FormatToken *LineEnd = nullptr;

  FormatToken invalidToken;

  StringRef FileContents;

  void skipComments() { Current = skipComments(Current); }

  FormatToken *skipComments(FormatToken *Tok) {
    while (Tok && Tok->is(tok::comment))
      Tok = Tok->Next;
    return Tok;
  }

  void nextToken() {
    Current = Current->Next;
    skipComments();
    if (!Current || Current == LineEnd->Next) {
      // Set the current token to an invalid token, so that further parsing on
      // this line fails.
      Current = &invalidToken;
    }
  }

  StringRef getSourceText(SourceRange Range) {
    return getSourceText(Range.getBegin(), Range.getEnd());
  }

  StringRef getSourceText(SourceLocation Begin, SourceLocation End) {
    const SourceManager &SM = Env.getSourceManager();
    return FileContents.substr(SM.getFileOffset(Begin),
                               SM.getFileOffset(End) - SM.getFileOffset(Begin));
  }

  // Sorts the given module references.
  // Imports can have formatting disabled (FormattingOff), so the code below
  // skips runs of "no-formatting" module references, and sorts/merges the
  // references that have formatting enabled in individual chunks.
  SmallVector<JsModuleReference, 16>
  sortModuleReferences(const SmallVector<JsModuleReference, 16> &References) {
    // Sort module references.
    // Imports can have formatting disabled (FormattingOff), so the code below
    // skips runs of "no-formatting" module references, and sorts other
    // references per group.
    const auto *Start = References.begin();
    SmallVector<JsModuleReference, 16> ReferencesSorted;
    while (Start != References.end()) {
      while (Start != References.end() && Start->FormattingOff) {
        // Skip over all imports w/ disabled formatting.
        ReferencesSorted.push_back(*Start);
        ++Start;
      }
      SmallVector<JsModuleReference, 16> SortChunk;
      while (Start != References.end() && !Start->FormattingOff) {
        // Skip over all imports w/ disabled formatting.
        SortChunk.push_back(*Start);
        ++Start;
      }
      stable_sort(SortChunk);
      mergeModuleReferences(SortChunk);
      ReferencesSorted.insert(ReferencesSorted.end(), SortChunk.begin(),
                              SortChunk.end());
    }
    return ReferencesSorted;
  }

  // Merge module references.
  // After sorting, find all references that import named symbols from the
  // same URL and merge their names. E.g.
  //   import {X} from 'a';
  //   import {Y} from 'a';
  // should be rewritten to:
  //   import {X, Y} from 'a';
  // Note: this modifies the passed in ``References`` vector (by removing no
  // longer needed references).
  void mergeModuleReferences(SmallVector<JsModuleReference, 16> &References) {
    if (References.empty())
      return;
    JsModuleReference *PreviousReference = References.begin();
    auto *Reference = std::next(References.begin());
    while (Reference != References.end()) {
      // Skip:
      //   import 'foo';
      //   import * as foo from 'foo'; on either previous or this.
      //   import Default from 'foo'; on either previous or this.
      //   mismatching
      if (Reference->Category == JsModuleReference::SIDE_EFFECT ||
          PreviousReference->Category == JsModuleReference::SIDE_EFFECT ||
          Reference->IsExport != PreviousReference->IsExport ||
          Reference->IsTypeOnly != PreviousReference->IsTypeOnly ||
          !PreviousReference->Prefix.empty() || !Reference->Prefix.empty() ||
          !PreviousReference->DefaultImport.empty() ||
          !Reference->DefaultImport.empty() || Reference->Symbols.empty() ||
          PreviousReference->URL != Reference->URL) {
        PreviousReference = Reference;
        ++Reference;
        continue;
      }
      // Merge symbols from identical imports.
      PreviousReference->Symbols.append(Reference->Symbols);
      PreviousReference->SymbolsMerged = true;
      // Remove the merged import.
      Reference = References.erase(Reference);
    }
  }

  // Appends ``Reference`` to ``Buffer``.
  void appendReference(std::string &Buffer, JsModuleReference &Reference) {
    if (Reference.FormattingOff) {
      Buffer +=
          getSourceText(Reference.Range.getBegin(), Reference.Range.getEnd());
      return;
    }
    // Sort the individual symbols within the import.
    // E.g. `import {b, a} from 'x';` -> `import {a, b} from 'x';`
    SmallVector<JsImportedSymbol, 1> Symbols = Reference.Symbols;
    stable_sort(Symbols,
                [&](const JsImportedSymbol &LHS, const JsImportedSymbol &RHS) {
                  return LHS.Symbol.compare_insensitive(RHS.Symbol) < 0;
                });
    if (!Reference.SymbolsMerged && Symbols == Reference.Symbols) {
      // Symbols didn't change, just emit the entire module reference.
      StringRef ReferenceStmt = getSourceText(Reference.Range);
      Buffer += ReferenceStmt;
      return;
    }
    // Stitch together the module reference start...
    Buffer += getSourceText(Reference.Range.getBegin(), Reference.SymbolsStart);
    // ... then the references in order ...
    if (!Symbols.empty()) {
      Buffer += getSourceText(Symbols.front().Range);
      for (const JsImportedSymbol &Symbol : drop_begin(Symbols)) {
        Buffer += ",";
        Buffer += getSourceText(Symbol.Range);
      }
    }
    // ... followed by the module reference end.
    Buffer += getSourceText(Reference.SymbolsEnd, Reference.Range.getEnd());
  }

  // Parses module references in the given lines. Returns the module references,
  // and a pointer to the first "main code" line if that is adjacent to the
  // affected lines of module references, nullptr otherwise.
  std::pair<SmallVector<JsModuleReference, 16>, AnnotatedLine *>
  parseModuleReferences(const AdditionalKeywords &Keywords,
                        SmallVectorImpl<AnnotatedLine *> &AnnotatedLines) {
    SmallVector<JsModuleReference, 16> References;
    SourceLocation Start;
    AnnotatedLine *FirstNonImportLine = nullptr;
    bool AnyImportAffected = false;
    bool FormattingOff = false;
    for (auto *Line : AnnotatedLines) {
      assert(Line->First);
      Current = Line->First;
      LineEnd = Line->Last;
      // clang-format comments toggle formatting on/off.
      // This is tracked in FormattingOff here and on JsModuleReference.
      while (Current && Current->is(tok::comment)) {
        StringRef CommentText = Current->TokenText.trim();
        if (isClangFormatOff(CommentText)) {
          FormattingOff = true;
        } else if (isClangFormatOn(CommentText)) {
          FormattingOff = false;
          // Special case: consider a trailing "clang-format on" line to be part
          // of the module reference, so that it gets moved around together with
          // it (as opposed to the next module reference, which might get sorted
          // around).
          if (!References.empty()) {
            References.back().Range.setEnd(Current->Tok.getEndLoc());
            Start = Current->Tok.getEndLoc().getLocWithOffset(1);
          }
        }
        // Handle all clang-format comments on a line, e.g. for an empty block.
        Current = Current->Next;
      }
      skipComments();
      if (Start.isInvalid() || References.empty()) {
        // After the first file level comment, consider line comments to be part
        // of the import that immediately follows them by using the previously
        // set Start.
        Start = Line->First->Tok.getLocation();
      }
      if (!Current) {
        // Only comments on this line. Could be the first non-import line.
        FirstNonImportLine = Line;
        continue;
      }
      JsModuleReference Reference;
      Reference.FormattingOff = FormattingOff;
      Reference.Range.setBegin(Start);
      // References w/o a URL, e.g. export {A}, groups with RELATIVE.
      Reference.Category = JsModuleReference::ReferenceCategory::RELATIVE;
      if (!parseModuleReference(Keywords, Reference)) {
        if (!FirstNonImportLine)
          FirstNonImportLine = Line; // if no comment before.
        break;
      }
      FirstNonImportLine = nullptr;
      AnyImportAffected = AnyImportAffected || Line->Affected;
      Reference.Range.setEnd(LineEnd->Tok.getEndLoc());
      LLVM_DEBUG({
        llvm::dbgs() << "JsModuleReference: {"
                     << "formatting_off: " << Reference.FormattingOff
                     << ", is_export: " << Reference.IsExport
                     << ", cat: " << Reference.Category
                     << ", url: " << Reference.URL
                     << ", prefix: " << Reference.Prefix;
        for (const JsImportedSymbol &Symbol : Reference.Symbols)
          llvm::dbgs() << ", " << Symbol.Symbol << " as " << Symbol.Alias;
        llvm::dbgs() << ", text: " << getSourceText(Reference.Range);
        llvm::dbgs() << "}\n";
      });
      References.push_back(Reference);
      Start = SourceLocation();
    }
    // Sort imports if any import line was affected.
    if (!AnyImportAffected)
      References.clear();
    return std::make_pair(References, FirstNonImportLine);
  }

  // Parses a JavaScript/ECMAScript 6 module reference.
  // See http://www.ecma-international.org/ecma-262/6.0/#sec-scripts-and-modules
  // for grammar EBNF (production ModuleItem).
  bool parseModuleReference(const AdditionalKeywords &Keywords,
                            JsModuleReference &Reference) {
    if (!Current || !Current->isOneOf(Keywords.kw_import, tok::kw_export))
      return false;
    Reference.IsExport = Current->is(tok::kw_export);

    nextToken();
    if (Current->isStringLiteral() && !Reference.IsExport) {
      // "import 'side-effect';"
      Reference.Category = JsModuleReference::ReferenceCategory::SIDE_EFFECT;
      Reference.URL =
          Current->TokenText.substr(1, Current->TokenText.size() - 2);
      return true;
    }

    if (!parseModuleBindings(Keywords, Reference))
      return false;

    if (Current->is(Keywords.kw_from)) {
      // imports have a 'from' clause, exports might not.
      nextToken();
      if (!Current->isStringLiteral())
        return false;
      // URL = TokenText without the quotes.
      Reference.URL =
          Current->TokenText.substr(1, Current->TokenText.size() - 2);
      if (Reference.URL.starts_with("..")) {
        Reference.Category =
            JsModuleReference::ReferenceCategory::RELATIVE_PARENT;
      } else if (Reference.URL.starts_with(".")) {
        Reference.Category = JsModuleReference::ReferenceCategory::RELATIVE;
      } else {
        Reference.Category = JsModuleReference::ReferenceCategory::ABSOLUTE;
      }
    }
    return true;
  }

  bool parseModuleBindings(const AdditionalKeywords &Keywords,
                           JsModuleReference &Reference) {
    if (parseStarBinding(Keywords, Reference))
      return true;
    return parseNamedBindings(Keywords, Reference);
  }

  bool parseStarBinding(const AdditionalKeywords &Keywords,
                        JsModuleReference &Reference) {
    // * as prefix from '...';
    if (Current->is(Keywords.kw_type) && Current->Next &&
        Current->Next->is(tok::star)) {
      Reference.IsTypeOnly = true;
      nextToken();
    }
    if (Current->isNot(tok::star))
      return false;
    nextToken();
    if (Current->isNot(Keywords.kw_as))
      return false;
    nextToken();
    if (Current->isNot(tok::identifier))
      return false;
    Reference.Prefix = Current->TokenText;
    nextToken();
    return true;
  }

  bool parseNamedBindings(const AdditionalKeywords &Keywords,
                          JsModuleReference &Reference) {
    if (Current->is(Keywords.kw_type) && Current->Next &&
        Current->Next->isOneOf(tok::identifier, tok::l_brace)) {
      Reference.IsTypeOnly = true;
      nextToken();
    }

    // eat a potential "import X, " prefix.
    if (!Reference.IsExport && Current->is(tok::identifier)) {
      Reference.DefaultImport = Current->TokenText;
      nextToken();
      if (Current->is(Keywords.kw_from))
        return true;
      // import X = A.B.C;
      if (Current->is(tok::equal)) {
        Reference.Category = JsModuleReference::ReferenceCategory::ALIAS;
        nextToken();
        while (Current->is(tok::identifier)) {
          nextToken();
          if (Current->is(tok::semi))
            return true;
          if (Current->isNot(tok::period))
            return false;
          nextToken();
        }
      }
      if (Current->isNot(tok::comma))
        return false;
      nextToken(); // eat comma.
    }
    if (Current->isNot(tok::l_brace))
      return false;

    // {sym as alias, sym2 as ...} from '...';
    Reference.SymbolsStart = Current->Tok.getEndLoc();
    while (Current->isNot(tok::r_brace)) {
      nextToken();
      if (Current->is(tok::r_brace))
        break;
      auto IsIdentifier = [](const auto *Tok) {
        return Tok->isOneOf(tok::identifier, tok::kw_default, tok::kw_template);
      };
      bool isTypeOnly = Current->is(Keywords.kw_type) && Current->Next &&
                        IsIdentifier(Current->Next);
      if (!isTypeOnly && !IsIdentifier(Current))
        return false;

      JsImportedSymbol Symbol;
      // Make sure to include any preceding comments.
      Symbol.Range.setBegin(
          Current->getPreviousNonComment()->Next->WhitespaceRange.getBegin());
      if (isTypeOnly)
        nextToken();
      Symbol.Symbol = Current->TokenText;
      nextToken();

      if (Current->is(Keywords.kw_as)) {
        nextToken();
        if (!IsIdentifier(Current))
          return false;
        Symbol.Alias = Current->TokenText;
        nextToken();
      }
      Symbol.Range.setEnd(Current->Tok.getLocation());
      Reference.Symbols.push_back(Symbol);

      if (!Current->isOneOf(tok::r_brace, tok::comma))
        return false;
    }
    Reference.SymbolsEnd = Current->Tok.getLocation();
    // For named imports with a trailing comma ("import {X,}"), consider the
    // comma to be the end of the import list, so that it doesn't get removed.
    if (Current->Previous->is(tok::comma))
      Reference.SymbolsEnd = Current->Previous->Tok.getLocation();
    nextToken(); // consume r_brace
    return true;
  }
};

tooling::Replacements sortJavaScriptImports(const FormatStyle &Style,
                                            StringRef Code,
                                            ArrayRef<tooling::Range> Ranges,
                                            StringRef FileName) {
  // FIXME: Cursor support.
  auto Env = Environment::make(Code, FileName, Ranges);
  if (!Env)
    return {};
  return JavaScriptImportSorter(*Env, Style).process().first;
}

} // end namespace format
} // end namespace clang

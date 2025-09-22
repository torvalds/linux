//===--- CommentToXML.cpp - Convert comments to XML representation --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Index/CommentToXML.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Comment.h"
#include "clang/AST/CommentVisitor.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Index/USRGeneration.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::comments;
using namespace clang::index;

namespace {

/// This comparison will sort parameters with valid index by index, then vararg
/// parameters, and invalid (unresolved) parameters last.
class ParamCommandCommentCompareIndex {
public:
  bool operator()(const ParamCommandComment *LHS,
                  const ParamCommandComment *RHS) const {
    unsigned LHSIndex = UINT_MAX;
    unsigned RHSIndex = UINT_MAX;

    if (LHS->isParamIndexValid()) {
      if (LHS->isVarArgParam())
        LHSIndex = UINT_MAX - 1;
      else
        LHSIndex = LHS->getParamIndex();
    }
    if (RHS->isParamIndexValid()) {
      if (RHS->isVarArgParam())
        RHSIndex = UINT_MAX - 1;
      else
        RHSIndex = RHS->getParamIndex();
    }
    return LHSIndex < RHSIndex;
  }
};

/// This comparison will sort template parameters in the following order:
/// \li real template parameters (depth = 1) in index order;
/// \li all other names (depth > 1);
/// \li unresolved names.
class TParamCommandCommentComparePosition {
public:
  bool operator()(const TParamCommandComment *LHS,
                  const TParamCommandComment *RHS) const {
    // Sort unresolved names last.
    if (!LHS->isPositionValid())
      return false;
    if (!RHS->isPositionValid())
      return true;

    if (LHS->getDepth() > 1)
      return false;
    if (RHS->getDepth() > 1)
      return true;

    // Sort template parameters in index order.
    if (LHS->getDepth() == 1 && RHS->getDepth() == 1)
      return LHS->getIndex(0) < RHS->getIndex(0);

    // Leave all other names in source order.
    return true;
  }
};

/// Separate parts of a FullComment.
struct FullCommentParts {
  /// Take a full comment apart and initialize members accordingly.
  FullCommentParts(const FullComment *C,
                   const CommandTraits &Traits);

  const BlockContentComment *Brief;
  const BlockContentComment *Headerfile;
  const ParagraphComment *FirstParagraph;
  SmallVector<const BlockCommandComment *, 4> Returns;
  SmallVector<const ParamCommandComment *, 8> Params;
  SmallVector<const TParamCommandComment *, 4> TParams;
  llvm::TinyPtrVector<const BlockCommandComment *> Exceptions;
  SmallVector<const BlockContentComment *, 8> MiscBlocks;
};

FullCommentParts::FullCommentParts(const FullComment *C,
                                   const CommandTraits &Traits) :
    Brief(nullptr), Headerfile(nullptr), FirstParagraph(nullptr) {
  for (Comment::child_iterator I = C->child_begin(), E = C->child_end();
       I != E; ++I) {
    const Comment *Child = *I;
    if (!Child)
      continue;
    switch (Child->getCommentKind()) {
    case CommentKind::None:
      continue;

    case CommentKind::ParagraphComment: {
      const ParagraphComment *PC = cast<ParagraphComment>(Child);
      if (PC->isWhitespace())
        break;
      if (!FirstParagraph)
        FirstParagraph = PC;

      MiscBlocks.push_back(PC);
      break;
    }

    case CommentKind::BlockCommandComment: {
      const BlockCommandComment *BCC = cast<BlockCommandComment>(Child);
      const CommandInfo *Info = Traits.getCommandInfo(BCC->getCommandID());
      if (!Brief && Info->IsBriefCommand) {
        Brief = BCC;
        break;
      }
      if (!Headerfile && Info->IsHeaderfileCommand) {
        Headerfile = BCC;
        break;
      }
      if (Info->IsReturnsCommand) {
        Returns.push_back(BCC);
        break;
      }
      if (Info->IsThrowsCommand) {
        Exceptions.push_back(BCC);
        break;
      }
      MiscBlocks.push_back(BCC);
      break;
    }

    case CommentKind::ParamCommandComment: {
      const ParamCommandComment *PCC = cast<ParamCommandComment>(Child);
      if (!PCC->hasParamName())
        break;

      if (!PCC->isDirectionExplicit() && !PCC->hasNonWhitespaceParagraph())
        break;

      Params.push_back(PCC);
      break;
    }

    case CommentKind::TParamCommandComment: {
      const TParamCommandComment *TPCC = cast<TParamCommandComment>(Child);
      if (!TPCC->hasParamName())
        break;

      if (!TPCC->hasNonWhitespaceParagraph())
        break;

      TParams.push_back(TPCC);
      break;
    }

    case CommentKind::VerbatimBlockComment:
      MiscBlocks.push_back(cast<BlockCommandComment>(Child));
      break;

    case CommentKind::VerbatimLineComment: {
      const VerbatimLineComment *VLC = cast<VerbatimLineComment>(Child);
      const CommandInfo *Info = Traits.getCommandInfo(VLC->getCommandID());
      if (!Info->IsDeclarationCommand)
        MiscBlocks.push_back(VLC);
      break;
    }

    case CommentKind::TextComment:
    case CommentKind::InlineCommandComment:
    case CommentKind::HTMLStartTagComment:
    case CommentKind::HTMLEndTagComment:
    case CommentKind::VerbatimBlockLineComment:
    case CommentKind::FullComment:
      llvm_unreachable("AST node of this kind can't be a child of "
                       "a FullComment");
    }
  }

  // Sort params in order they are declared in the function prototype.
  // Unresolved parameters are put at the end of the list in the same order
  // they were seen in the comment.
  llvm::stable_sort(Params, ParamCommandCommentCompareIndex());
  llvm::stable_sort(TParams, TParamCommandCommentComparePosition());
}

void printHTMLStartTagComment(const HTMLStartTagComment *C,
                              llvm::raw_svector_ostream &Result) {
  Result << "<" << C->getTagName();

  if (C->getNumAttrs() != 0) {
    for (unsigned i = 0, e = C->getNumAttrs(); i != e; i++) {
      Result << " ";
      const HTMLStartTagComment::Attribute &Attr = C->getAttr(i);
      Result << Attr.Name;
      if (!Attr.Value.empty())
        Result << "=\"" << Attr.Value << "\"";
    }
  }

  if (!C->isSelfClosing())
    Result << ">";
  else
    Result << "/>";
}

class CommentASTToHTMLConverter :
    public ConstCommentVisitor<CommentASTToHTMLConverter> {
public:
  /// \param Str accumulator for HTML.
  CommentASTToHTMLConverter(const FullComment *FC,
                            SmallVectorImpl<char> &Str,
                            const CommandTraits &Traits) :
      FC(FC), Result(Str), Traits(Traits)
  { }

  // Inline content.
  void visitTextComment(const TextComment *C);
  void visitInlineCommandComment(const InlineCommandComment *C);
  void visitHTMLStartTagComment(const HTMLStartTagComment *C);
  void visitHTMLEndTagComment(const HTMLEndTagComment *C);

  // Block content.
  void visitParagraphComment(const ParagraphComment *C);
  void visitBlockCommandComment(const BlockCommandComment *C);
  void visitParamCommandComment(const ParamCommandComment *C);
  void visitTParamCommandComment(const TParamCommandComment *C);
  void visitVerbatimBlockComment(const VerbatimBlockComment *C);
  void visitVerbatimBlockLineComment(const VerbatimBlockLineComment *C);
  void visitVerbatimLineComment(const VerbatimLineComment *C);

  void visitFullComment(const FullComment *C);

  // Helpers.

  /// Convert a paragraph that is not a block by itself (an argument to some
  /// command).
  void visitNonStandaloneParagraphComment(const ParagraphComment *C);

  void appendToResultWithHTMLEscaping(StringRef S);

private:
  const FullComment *FC;
  /// Output stream for HTML.
  llvm::raw_svector_ostream Result;

  const CommandTraits &Traits;
};
} // end unnamed namespace

void CommentASTToHTMLConverter::visitTextComment(const TextComment *C) {
  appendToResultWithHTMLEscaping(C->getText());
}

void CommentASTToHTMLConverter::visitInlineCommandComment(
                                  const InlineCommandComment *C) {
  // Nothing to render if no arguments supplied.
  if (C->getNumArgs() == 0)
    return;

  // Nothing to render if argument is empty.
  StringRef Arg0 = C->getArgText(0);
  if (Arg0.empty())
    return;

  switch (C->getRenderKind()) {
  case InlineCommandRenderKind::Normal:
    for (unsigned i = 0, e = C->getNumArgs(); i != e; ++i) {
      appendToResultWithHTMLEscaping(C->getArgText(i));
      Result << " ";
    }
    return;

  case InlineCommandRenderKind::Bold:
    assert(C->getNumArgs() == 1);
    Result << "<b>";
    appendToResultWithHTMLEscaping(Arg0);
    Result << "</b>";
    return;
  case InlineCommandRenderKind::Monospaced:
    assert(C->getNumArgs() == 1);
    Result << "<tt>";
    appendToResultWithHTMLEscaping(Arg0);
    Result<< "</tt>";
    return;
  case InlineCommandRenderKind::Emphasized:
    assert(C->getNumArgs() == 1);
    Result << "<em>";
    appendToResultWithHTMLEscaping(Arg0);
    Result << "</em>";
    return;
  case InlineCommandRenderKind::Anchor:
    assert(C->getNumArgs() == 1);
    Result << "<span id=\"" << Arg0 << "\"></span>";
    return;
  }
}

void CommentASTToHTMLConverter::visitHTMLStartTagComment(
                                  const HTMLStartTagComment *C) {
  printHTMLStartTagComment(C, Result);
}

void CommentASTToHTMLConverter::visitHTMLEndTagComment(
                                  const HTMLEndTagComment *C) {
  Result << "</" << C->getTagName() << ">";
}

void CommentASTToHTMLConverter::visitParagraphComment(
                                  const ParagraphComment *C) {
  if (C->isWhitespace())
    return;

  Result << "<p>";
  for (Comment::child_iterator I = C->child_begin(), E = C->child_end();
       I != E; ++I) {
    visit(*I);
  }
  Result << "</p>";
}

void CommentASTToHTMLConverter::visitBlockCommandComment(
                                  const BlockCommandComment *C) {
  const CommandInfo *Info = Traits.getCommandInfo(C->getCommandID());
  if (Info->IsBriefCommand) {
    Result << "<p class=\"para-brief\">";
    visitNonStandaloneParagraphComment(C->getParagraph());
    Result << "</p>";
    return;
  }
  if (Info->IsReturnsCommand) {
    Result << "<p class=\"para-returns\">"
              "<span class=\"word-returns\">Returns</span> ";
    visitNonStandaloneParagraphComment(C->getParagraph());
    Result << "</p>";
    return;
  }
  // We don't know anything about this command.  Just render the paragraph.
  visit(C->getParagraph());
}

void CommentASTToHTMLConverter::visitParamCommandComment(
                                  const ParamCommandComment *C) {
  if (C->isParamIndexValid()) {
    if (C->isVarArgParam()) {
      Result << "<dt class=\"param-name-index-vararg\">";
      appendToResultWithHTMLEscaping(C->getParamNameAsWritten());
    } else {
      Result << "<dt class=\"param-name-index-"
             << C->getParamIndex()
             << "\">";
      appendToResultWithHTMLEscaping(C->getParamName(FC));
    }
  } else {
    Result << "<dt class=\"param-name-index-invalid\">";
    appendToResultWithHTMLEscaping(C->getParamNameAsWritten());
  }
  Result << "</dt>";

  if (C->isParamIndexValid()) {
    if (C->isVarArgParam())
      Result << "<dd class=\"param-descr-index-vararg\">";
    else
      Result << "<dd class=\"param-descr-index-"
             << C->getParamIndex()
             << "\">";
  } else
    Result << "<dd class=\"param-descr-index-invalid\">";

  visitNonStandaloneParagraphComment(C->getParagraph());
  Result << "</dd>";
}

void CommentASTToHTMLConverter::visitTParamCommandComment(
                                  const TParamCommandComment *C) {
  if (C->isPositionValid()) {
    if (C->getDepth() == 1)
      Result << "<dt class=\"tparam-name-index-"
             << C->getIndex(0)
             << "\">";
    else
      Result << "<dt class=\"tparam-name-index-other\">";
    appendToResultWithHTMLEscaping(C->getParamName(FC));
  } else {
    Result << "<dt class=\"tparam-name-index-invalid\">";
    appendToResultWithHTMLEscaping(C->getParamNameAsWritten());
  }

  Result << "</dt>";

  if (C->isPositionValid()) {
    if (C->getDepth() == 1)
      Result << "<dd class=\"tparam-descr-index-"
             << C->getIndex(0)
             << "\">";
    else
      Result << "<dd class=\"tparam-descr-index-other\">";
  } else
    Result << "<dd class=\"tparam-descr-index-invalid\">";

  visitNonStandaloneParagraphComment(C->getParagraph());
  Result << "</dd>";
}

void CommentASTToHTMLConverter::visitVerbatimBlockComment(
                                  const VerbatimBlockComment *C) {
  unsigned NumLines = C->getNumLines();
  if (NumLines == 0)
    return;

  Result << "<pre>";
  for (unsigned i = 0; i != NumLines; ++i) {
    appendToResultWithHTMLEscaping(C->getText(i));
    if (i + 1 != NumLines)
      Result << '\n';
  }
  Result << "</pre>";
}

void CommentASTToHTMLConverter::visitVerbatimBlockLineComment(
                                  const VerbatimBlockLineComment *C) {
  llvm_unreachable("should not see this AST node");
}

void CommentASTToHTMLConverter::visitVerbatimLineComment(
                                  const VerbatimLineComment *C) {
  Result << "<pre>";
  appendToResultWithHTMLEscaping(C->getText());
  Result << "</pre>";
}

void CommentASTToHTMLConverter::visitFullComment(const FullComment *C) {
  FullCommentParts Parts(C, Traits);

  bool FirstParagraphIsBrief = false;
  if (Parts.Headerfile)
    visit(Parts.Headerfile);
  if (Parts.Brief)
    visit(Parts.Brief);
  else if (Parts.FirstParagraph) {
    Result << "<p class=\"para-brief\">";
    visitNonStandaloneParagraphComment(Parts.FirstParagraph);
    Result << "</p>";
    FirstParagraphIsBrief = true;
  }

  for (unsigned i = 0, e = Parts.MiscBlocks.size(); i != e; ++i) {
    const Comment *C = Parts.MiscBlocks[i];
    if (FirstParagraphIsBrief && C == Parts.FirstParagraph)
      continue;
    visit(C);
  }

  if (Parts.TParams.size() != 0) {
    Result << "<dl>";
    for (unsigned i = 0, e = Parts.TParams.size(); i != e; ++i)
      visit(Parts.TParams[i]);
    Result << "</dl>";
  }

  if (Parts.Params.size() != 0) {
    Result << "<dl>";
    for (unsigned i = 0, e = Parts.Params.size(); i != e; ++i)
      visit(Parts.Params[i]);
    Result << "</dl>";
  }

  if (Parts.Returns.size() != 0) {
    Result << "<div class=\"result-discussion\">";
    for (unsigned i = 0, e = Parts.Returns.size(); i != e; ++i)
      visit(Parts.Returns[i]);
    Result << "</div>";
  }

}

void CommentASTToHTMLConverter::visitNonStandaloneParagraphComment(
                                  const ParagraphComment *C) {
  if (!C)
    return;

  for (Comment::child_iterator I = C->child_begin(), E = C->child_end();
       I != E; ++I) {
    visit(*I);
  }
}

void CommentASTToHTMLConverter::appendToResultWithHTMLEscaping(StringRef S) {
  for (StringRef::iterator I = S.begin(), E = S.end(); I != E; ++I) {
    const char C = *I;
    switch (C) {
    case '&':
      Result << "&amp;";
      break;
    case '<':
      Result << "&lt;";
      break;
    case '>':
      Result << "&gt;";
      break;
    case '"':
      Result << "&quot;";
      break;
    case '\'':
      Result << "&#39;";
      break;
    case '/':
      Result << "&#47;";
      break;
    default:
      Result << C;
      break;
    }
  }
}

namespace {
class CommentASTToXMLConverter :
    public ConstCommentVisitor<CommentASTToXMLConverter> {
public:
  /// \param Str accumulator for XML.
  CommentASTToXMLConverter(const FullComment *FC,
                           SmallVectorImpl<char> &Str,
                           const CommandTraits &Traits,
                           const SourceManager &SM) :
      FC(FC), Result(Str), Traits(Traits), SM(SM) { }

  // Inline content.
  void visitTextComment(const TextComment *C);
  void visitInlineCommandComment(const InlineCommandComment *C);
  void visitHTMLStartTagComment(const HTMLStartTagComment *C);
  void visitHTMLEndTagComment(const HTMLEndTagComment *C);

  // Block content.
  void visitParagraphComment(const ParagraphComment *C);

  void appendParagraphCommentWithKind(const ParagraphComment *C,
                                      StringRef ParagraphKind,
                                      StringRef PrependBodyText);

  void visitBlockCommandComment(const BlockCommandComment *C);
  void visitParamCommandComment(const ParamCommandComment *C);
  void visitTParamCommandComment(const TParamCommandComment *C);
  void visitVerbatimBlockComment(const VerbatimBlockComment *C);
  void visitVerbatimBlockLineComment(const VerbatimBlockLineComment *C);
  void visitVerbatimLineComment(const VerbatimLineComment *C);

  void visitFullComment(const FullComment *C);

  // Helpers.
  void appendToResultWithXMLEscaping(StringRef S);
  void appendToResultWithCDATAEscaping(StringRef S);

  void formatTextOfDeclaration(const DeclInfo *DI,
                               SmallString<128> &Declaration);

private:
  const FullComment *FC;

  /// Output stream for XML.
  llvm::raw_svector_ostream Result;

  const CommandTraits &Traits;
  const SourceManager &SM;
};

void getSourceTextOfDeclaration(const DeclInfo *ThisDecl,
                                SmallVectorImpl<char> &Str) {
  ASTContext &Context = ThisDecl->CurrentDecl->getASTContext();
  const LangOptions &LangOpts = Context.getLangOpts();
  llvm::raw_svector_ostream OS(Str);
  PrintingPolicy PPolicy(LangOpts);
  PPolicy.PolishForDeclaration = true;
  PPolicy.TerseOutput = true;
  PPolicy.ConstantsAsWritten = true;
  ThisDecl->CurrentDecl->print(OS, PPolicy,
                               /*Indentation*/0, /*PrintInstantiation*/false);
}

void CommentASTToXMLConverter::formatTextOfDeclaration(
    const DeclInfo *DI, SmallString<128> &Declaration) {
  // Formatting API expects null terminated input string.
  StringRef StringDecl(Declaration.c_str(), Declaration.size());

  // Formatter specific code.
  unsigned Offset = 0;
  unsigned Length = Declaration.size();

  format::FormatStyle Style = format::getLLVMStyle();
  Style.FixNamespaceComments = false;
  tooling::Replacements Replaces =
      reformat(Style, StringDecl, tooling::Range(Offset, Length), "xmldecl.xd");
  auto FormattedStringDecl = applyAllReplacements(StringDecl, Replaces);
  if (static_cast<bool>(FormattedStringDecl)) {
    Declaration = *FormattedStringDecl;
  }
}

} // end unnamed namespace

void CommentASTToXMLConverter::visitTextComment(const TextComment *C) {
  appendToResultWithXMLEscaping(C->getText());
}

void CommentASTToXMLConverter::visitInlineCommandComment(
    const InlineCommandComment *C) {
  // Nothing to render if no arguments supplied.
  if (C->getNumArgs() == 0)
    return;

  // Nothing to render if argument is empty.
  StringRef Arg0 = C->getArgText(0);
  if (Arg0.empty())
    return;

  switch (C->getRenderKind()) {
  case InlineCommandRenderKind::Normal:
    for (unsigned i = 0, e = C->getNumArgs(); i != e; ++i) {
      appendToResultWithXMLEscaping(C->getArgText(i));
      Result << " ";
    }
    return;
  case InlineCommandRenderKind::Bold:
    assert(C->getNumArgs() == 1);
    Result << "<bold>";
    appendToResultWithXMLEscaping(Arg0);
    Result << "</bold>";
    return;
  case InlineCommandRenderKind::Monospaced:
    assert(C->getNumArgs() == 1);
    Result << "<monospaced>";
    appendToResultWithXMLEscaping(Arg0);
    Result << "</monospaced>";
    return;
  case InlineCommandRenderKind::Emphasized:
    assert(C->getNumArgs() == 1);
    Result << "<emphasized>";
    appendToResultWithXMLEscaping(Arg0);
    Result << "</emphasized>";
    return;
  case InlineCommandRenderKind::Anchor:
    assert(C->getNumArgs() == 1);
    Result << "<anchor id=\"" << Arg0 << "\"></anchor>";
    return;
  }
}

void CommentASTToXMLConverter::visitHTMLStartTagComment(
    const HTMLStartTagComment *C) {
  Result << "<rawHTML";
  if (C->isMalformed())
    Result << " isMalformed=\"1\"";
  Result << ">";
  {
    SmallString<32> Tag;
    {
      llvm::raw_svector_ostream TagOS(Tag);
      printHTMLStartTagComment(C, TagOS);
    }
    appendToResultWithCDATAEscaping(Tag);
  }
  Result << "</rawHTML>";
}

void
CommentASTToXMLConverter::visitHTMLEndTagComment(const HTMLEndTagComment *C) {
  Result << "<rawHTML";
  if (C->isMalformed())
    Result << " isMalformed=\"1\"";
  Result << ">&lt;/" << C->getTagName() << "&gt;</rawHTML>";
}

void CommentASTToXMLConverter::visitParagraphComment(
    const ParagraphComment *C) {
  appendParagraphCommentWithKind(C, StringRef(), StringRef());
}

void CommentASTToXMLConverter::appendParagraphCommentWithKind(
    const ParagraphComment *C, StringRef ParagraphKind,
    StringRef PrependBodyText) {
  if (C->isWhitespace() && PrependBodyText.empty())
    return;

  if (ParagraphKind.empty())
    Result << "<Para>";
  else
    Result << "<Para kind=\"" << ParagraphKind << "\">";

  if (!PrependBodyText.empty())
    Result << PrependBodyText << " ";

  for (Comment::child_iterator I = C->child_begin(), E = C->child_end(); I != E;
       ++I) {
    visit(*I);
  }
  Result << "</Para>";
}

void CommentASTToXMLConverter::visitBlockCommandComment(
    const BlockCommandComment *C) {
  StringRef ParagraphKind;
  StringRef ExceptionType;

  const unsigned CommandID = C->getCommandID();
  const CommandInfo *Info = Traits.getCommandInfo(CommandID);
  if (Info->IsThrowsCommand && C->getNumArgs() > 0) {
    ExceptionType = C->getArgText(0);
  }

  switch (CommandID) {
  case CommandTraits::KCI_attention:
  case CommandTraits::KCI_author:
  case CommandTraits::KCI_authors:
  case CommandTraits::KCI_bug:
  case CommandTraits::KCI_copyright:
  case CommandTraits::KCI_date:
  case CommandTraits::KCI_invariant:
  case CommandTraits::KCI_note:
  case CommandTraits::KCI_post:
  case CommandTraits::KCI_pre:
  case CommandTraits::KCI_remark:
  case CommandTraits::KCI_remarks:
  case CommandTraits::KCI_sa:
  case CommandTraits::KCI_see:
  case CommandTraits::KCI_since:
  case CommandTraits::KCI_todo:
  case CommandTraits::KCI_version:
  case CommandTraits::KCI_warning:
    ParagraphKind = C->getCommandName(Traits);
    break;
  default:
    break;
  }

  appendParagraphCommentWithKind(C->getParagraph(), ParagraphKind,
                                 ExceptionType);
}

void CommentASTToXMLConverter::visitParamCommandComment(
    const ParamCommandComment *C) {
  Result << "<Parameter><Name>";
  appendToResultWithXMLEscaping(C->isParamIndexValid()
                                    ? C->getParamName(FC)
                                    : C->getParamNameAsWritten());
  Result << "</Name>";

  if (C->isParamIndexValid()) {
    if (C->isVarArgParam())
      Result << "<IsVarArg />";
    else
      Result << "<Index>" << C->getParamIndex() << "</Index>";
  }

  Result << "<Direction isExplicit=\"" << C->isDirectionExplicit() << "\">";
  switch (C->getDirection()) {
  case ParamCommandPassDirection::In:
    Result << "in";
    break;
  case ParamCommandPassDirection::Out:
    Result << "out";
    break;
  case ParamCommandPassDirection::InOut:
    Result << "in,out";
    break;
  }
  Result << "</Direction><Discussion>";
  visit(C->getParagraph());
  Result << "</Discussion></Parameter>";
}

void CommentASTToXMLConverter::visitTParamCommandComment(
                                  const TParamCommandComment *C) {
  Result << "<Parameter><Name>";
  appendToResultWithXMLEscaping(C->isPositionValid() ? C->getParamName(FC)
                                : C->getParamNameAsWritten());
  Result << "</Name>";

  if (C->isPositionValid() && C->getDepth() == 1) {
    Result << "<Index>" << C->getIndex(0) << "</Index>";
  }

  Result << "<Discussion>";
  visit(C->getParagraph());
  Result << "</Discussion></Parameter>";
}

void CommentASTToXMLConverter::visitVerbatimBlockComment(
                                  const VerbatimBlockComment *C) {
  unsigned NumLines = C->getNumLines();
  if (NumLines == 0)
    return;

  switch (C->getCommandID()) {
  case CommandTraits::KCI_code:
    Result << "<Verbatim xml:space=\"preserve\" kind=\"code\">";
    break;
  default:
    Result << "<Verbatim xml:space=\"preserve\" kind=\"verbatim\">";
    break;
  }
  for (unsigned i = 0; i != NumLines; ++i) {
    appendToResultWithXMLEscaping(C->getText(i));
    if (i + 1 != NumLines)
      Result << '\n';
  }
  Result << "</Verbatim>";
}

void CommentASTToXMLConverter::visitVerbatimBlockLineComment(
                                  const VerbatimBlockLineComment *C) {
  llvm_unreachable("should not see this AST node");
}

void CommentASTToXMLConverter::visitVerbatimLineComment(
                                  const VerbatimLineComment *C) {
  Result << "<Verbatim xml:space=\"preserve\" kind=\"verbatim\">";
  appendToResultWithXMLEscaping(C->getText());
  Result << "</Verbatim>";
}

void CommentASTToXMLConverter::visitFullComment(const FullComment *C) {
  FullCommentParts Parts(C, Traits);

  const DeclInfo *DI = C->getDeclInfo();
  StringRef RootEndTag;
  if (DI) {
    switch (DI->getKind()) {
    case DeclInfo::OtherKind:
      RootEndTag = "</Other>";
      Result << "<Other";
      break;
    case DeclInfo::FunctionKind:
      RootEndTag = "</Function>";
      Result << "<Function";
      switch (DI->TemplateKind) {
      case DeclInfo::NotTemplate:
        break;
      case DeclInfo::Template:
        Result << " templateKind=\"template\"";
        break;
      case DeclInfo::TemplateSpecialization:
        Result << " templateKind=\"specialization\"";
        break;
      case DeclInfo::TemplatePartialSpecialization:
        llvm_unreachable("partial specializations of functions "
                         "are not allowed in C++");
      }
      if (DI->IsInstanceMethod)
        Result << " isInstanceMethod=\"1\"";
      if (DI->IsClassMethod)
        Result << " isClassMethod=\"1\"";
      break;
    case DeclInfo::ClassKind:
      RootEndTag = "</Class>";
      Result << "<Class";
      switch (DI->TemplateKind) {
      case DeclInfo::NotTemplate:
        break;
      case DeclInfo::Template:
        Result << " templateKind=\"template\"";
        break;
      case DeclInfo::TemplateSpecialization:
        Result << " templateKind=\"specialization\"";
        break;
      case DeclInfo::TemplatePartialSpecialization:
        Result << " templateKind=\"partialSpecialization\"";
        break;
      }
      break;
    case DeclInfo::VariableKind:
      RootEndTag = "</Variable>";
      Result << "<Variable";
      break;
    case DeclInfo::NamespaceKind:
      RootEndTag = "</Namespace>";
      Result << "<Namespace";
      break;
    case DeclInfo::TypedefKind:
      RootEndTag = "</Typedef>";
      Result << "<Typedef";
      break;
    case DeclInfo::EnumKind:
      RootEndTag = "</Enum>";
      Result << "<Enum";
      break;
    }

    {
      // Print line and column number.
      SourceLocation Loc = DI->CurrentDecl->getLocation();
      std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
      FileID FID = LocInfo.first;
      unsigned FileOffset = LocInfo.second;

      if (FID.isValid()) {
        if (OptionalFileEntryRef FE = SM.getFileEntryRefForID(FID)) {
          Result << " file=\"";
          appendToResultWithXMLEscaping(FE->getName());
          Result << "\"";
        }
        Result << " line=\"" << SM.getLineNumber(FID, FileOffset)
               << "\" column=\"" << SM.getColumnNumber(FID, FileOffset)
               << "\"";
      }
    }

    // Finish the root tag.
    Result << ">";

    bool FoundName = false;
    if (const NamedDecl *ND = dyn_cast<NamedDecl>(DI->CommentDecl)) {
      if (DeclarationName DeclName = ND->getDeclName()) {
        Result << "<Name>";
        std::string Name = DeclName.getAsString();
        appendToResultWithXMLEscaping(Name);
        FoundName = true;
        Result << "</Name>";
      }
    }
    if (!FoundName)
      Result << "<Name>&lt;anonymous&gt;</Name>";

    {
      // Print USR.
      SmallString<128> USR;
      generateUSRForDecl(DI->CommentDecl, USR);
      if (!USR.empty()) {
        Result << "<USR>";
        appendToResultWithXMLEscaping(USR);
        Result << "</USR>";
      }
    }
  } else {
    // No DeclInfo -- just emit some root tag and name tag.
    RootEndTag = "</Other>";
    Result << "<Other><Name>unknown</Name>";
  }

  if (Parts.Headerfile) {
    Result << "<Headerfile>";
    visit(Parts.Headerfile);
    Result << "</Headerfile>";
  }

  {
    // Pretty-print the declaration.
    Result << "<Declaration>";
    SmallString<128> Declaration;
    getSourceTextOfDeclaration(DI, Declaration);
    formatTextOfDeclaration(DI, Declaration);
    appendToResultWithXMLEscaping(Declaration);
    Result << "</Declaration>";
  }

  bool FirstParagraphIsBrief = false;
  if (Parts.Brief) {
    Result << "<Abstract>";
    visit(Parts.Brief);
    Result << "</Abstract>";
  } else if (Parts.FirstParagraph) {
    Result << "<Abstract>";
    visit(Parts.FirstParagraph);
    Result << "</Abstract>";
    FirstParagraphIsBrief = true;
  }

  if (Parts.TParams.size() != 0) {
    Result << "<TemplateParameters>";
    for (unsigned i = 0, e = Parts.TParams.size(); i != e; ++i)
      visit(Parts.TParams[i]);
    Result << "</TemplateParameters>";
  }

  if (Parts.Params.size() != 0) {
    Result << "<Parameters>";
    for (unsigned i = 0, e = Parts.Params.size(); i != e; ++i)
      visit(Parts.Params[i]);
    Result << "</Parameters>";
  }

  if (Parts.Exceptions.size() != 0) {
    Result << "<Exceptions>";
    for (unsigned i = 0, e = Parts.Exceptions.size(); i != e; ++i)
      visit(Parts.Exceptions[i]);
    Result << "</Exceptions>";
  }

  if (Parts.Returns.size() != 0) {
    Result << "<ResultDiscussion>";
    for (unsigned i = 0, e = Parts.Returns.size(); i != e; ++i)
      visit(Parts.Returns[i]);
    Result << "</ResultDiscussion>";
  }

  if (DI->CommentDecl->hasAttrs()) {
    const AttrVec &Attrs = DI->CommentDecl->getAttrs();
    for (unsigned i = 0, e = Attrs.size(); i != e; i++) {
      const AvailabilityAttr *AA = dyn_cast<AvailabilityAttr>(Attrs[i]);
      if (!AA) {
        if (const DeprecatedAttr *DA = dyn_cast<DeprecatedAttr>(Attrs[i])) {
          if (DA->getMessage().empty())
            Result << "<Deprecated/>";
          else {
            Result << "<Deprecated>";
            appendToResultWithXMLEscaping(DA->getMessage());
            Result << "</Deprecated>";
          }
        }
        else if (const UnavailableAttr *UA = dyn_cast<UnavailableAttr>(Attrs[i])) {
          if (UA->getMessage().empty())
            Result << "<Unavailable/>";
          else {
            Result << "<Unavailable>";
            appendToResultWithXMLEscaping(UA->getMessage());
            Result << "</Unavailable>";
          }
        }
        continue;
      }

      // 'availability' attribute.
      Result << "<Availability";
      StringRef Distribution;
      if (AA->getPlatform()) {
        Distribution = AvailabilityAttr::getPrettyPlatformName(
                                        AA->getPlatform()->getName());
        if (Distribution.empty())
          Distribution = AA->getPlatform()->getName();
      }
      Result << " distribution=\"" << Distribution << "\">";
      VersionTuple IntroducedInVersion = AA->getIntroduced();
      if (!IntroducedInVersion.empty()) {
        Result << "<IntroducedInVersion>"
               << IntroducedInVersion.getAsString()
               << "</IntroducedInVersion>";
      }
      VersionTuple DeprecatedInVersion = AA->getDeprecated();
      if (!DeprecatedInVersion.empty()) {
        Result << "<DeprecatedInVersion>"
               << DeprecatedInVersion.getAsString()
               << "</DeprecatedInVersion>";
      }
      VersionTuple RemovedAfterVersion = AA->getObsoleted();
      if (!RemovedAfterVersion.empty()) {
        Result << "<RemovedAfterVersion>"
               << RemovedAfterVersion.getAsString()
               << "</RemovedAfterVersion>";
      }
      StringRef DeprecationSummary = AA->getMessage();
      if (!DeprecationSummary.empty()) {
        Result << "<DeprecationSummary>";
        appendToResultWithXMLEscaping(DeprecationSummary);
        Result << "</DeprecationSummary>";
      }
      if (AA->getUnavailable())
        Result << "<Unavailable/>";

      IdentifierInfo *Environment = AA->getEnvironment();
      if (Environment) {
        Result << "<Environment>" << Environment->getName() << "</Environment>";
      }
      Result << "</Availability>";
    }
  }

  {
    bool StartTagEmitted = false;
    for (unsigned i = 0, e = Parts.MiscBlocks.size(); i != e; ++i) {
      const Comment *C = Parts.MiscBlocks[i];
      if (FirstParagraphIsBrief && C == Parts.FirstParagraph)
        continue;
      if (!StartTagEmitted) {
        Result << "<Discussion>";
        StartTagEmitted = true;
      }
      visit(C);
    }
    if (StartTagEmitted)
      Result << "</Discussion>";
  }

  Result << RootEndTag;
}

void CommentASTToXMLConverter::appendToResultWithXMLEscaping(StringRef S) {
  for (StringRef::iterator I = S.begin(), E = S.end(); I != E; ++I) {
    const char C = *I;
    switch (C) {
    case '&':
      Result << "&amp;";
      break;
    case '<':
      Result << "&lt;";
      break;
    case '>':
      Result << "&gt;";
      break;
    case '"':
      Result << "&quot;";
      break;
    case '\'':
      Result << "&apos;";
      break;
    default:
      Result << C;
      break;
    }
  }
}

void CommentASTToXMLConverter::appendToResultWithCDATAEscaping(StringRef S) {
  if (S.empty())
    return;

  Result << "<![CDATA[";
  while (!S.empty()) {
    size_t Pos = S.find("]]>");
    if (Pos == 0) {
      Result << "]]]]><![CDATA[>";
      S = S.drop_front(3);
      continue;
    }
    if (Pos == StringRef::npos)
      Pos = S.size();

    Result << S.substr(0, Pos);

    S = S.drop_front(Pos);
  }
  Result << "]]>";
}

CommentToXMLConverter::CommentToXMLConverter() {}
CommentToXMLConverter::~CommentToXMLConverter() {}

void CommentToXMLConverter::convertCommentToHTML(const FullComment *FC,
                                                 SmallVectorImpl<char> &HTML,
                                                 const ASTContext &Context) {
  CommentASTToHTMLConverter Converter(FC, HTML,
                                      Context.getCommentCommandTraits());
  Converter.visit(FC);
}

void CommentToXMLConverter::convertHTMLTagNodeToText(
    const comments::HTMLTagComment *HTC, SmallVectorImpl<char> &Text,
    const ASTContext &Context) {
  CommentASTToHTMLConverter Converter(nullptr, Text,
                                      Context.getCommentCommandTraits());
  Converter.visit(HTC);
}

void CommentToXMLConverter::convertCommentToXML(const FullComment *FC,
                                                SmallVectorImpl<char> &XML,
                                                const ASTContext &Context) {
  CommentASTToXMLConverter Converter(FC, XML, Context.getCommentCommandTraits(),
                                     Context.getSourceManager());
  Converter.visit(FC);
}

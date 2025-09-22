//===--- Comment.h - Comment AST nodes --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines comment AST nodes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_COMMENT_H
#define LLVM_CLANG_AST_COMMENT_H

#include "clang/AST/CommentCommandTraits.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
class Decl;
class ParmVarDecl;
class TemplateParameterList;

namespace comments {
class FullComment;
enum class InlineCommandRenderKind;
enum class ParamCommandPassDirection;

/// Describes the syntax that was used in a documentation command.
///
/// Exact values of this enumeration are important because they used to select
/// parts of diagnostic messages.  Audit diagnostics before changing or adding
/// a new value.
enum CommandMarkerKind {
  /// Command started with a backslash character:
  /// \code
  ///   \foo
  /// \endcode
  CMK_Backslash = 0,

  /// Command started with an 'at' character:
  /// \code
  ///   @foo
  /// \endcode
  CMK_At = 1
};

enum class CommentKind {
  None = 0,
#define COMMENT(CLASS, PARENT) CLASS,
#define COMMENT_RANGE(BASE, FIRST, LAST)                                       \
  First##BASE##Constant = FIRST, Last##BASE##Constant = LAST,
#define LAST_COMMENT_RANGE(BASE, FIRST, LAST)                                  \
  First##BASE##Constant = FIRST, Last##BASE##Constant = LAST
#define ABSTRACT_COMMENT(COMMENT)
#include "clang/AST/CommentNodes.inc"
};

/// Any part of the comment.
/// Abstract class.
class Comment {
protected:
  /// Preferred location to show caret.
  SourceLocation Loc;

  /// Source range of this AST node.
  SourceRange Range;

  class CommentBitfields {
    friend class Comment;

    /// Type of this AST node.
    LLVM_PREFERRED_TYPE(CommentKind)
    unsigned Kind : 8;
  };
  enum { NumCommentBits = 8 };

  class InlineContentCommentBitfields {
    friend class InlineContentComment;

    LLVM_PREFERRED_TYPE(CommentBitfields)
    unsigned : NumCommentBits;

    /// True if there is a newline after this inline content node.
    /// (There is no separate AST node for a newline.)
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasTrailingNewline : 1;
  };
  enum { NumInlineContentCommentBits = NumCommentBits + 1 };

  class TextCommentBitfields {
    friend class TextComment;

    LLVM_PREFERRED_TYPE(InlineContentCommentBitfields)
    unsigned : NumInlineContentCommentBits;

    /// True if \c IsWhitespace field contains a valid value.
    LLVM_PREFERRED_TYPE(bool)
    mutable unsigned IsWhitespaceValid : 1;

    /// True if this comment AST node contains only whitespace.
    LLVM_PREFERRED_TYPE(bool)
    mutable unsigned IsWhitespace : 1;
  };
  enum { NumTextCommentBits = NumInlineContentCommentBits + 2 };

  class InlineCommandCommentBitfields {
    friend class InlineCommandComment;

    LLVM_PREFERRED_TYPE(InlineContentCommentBitfields)
    unsigned : NumInlineContentCommentBits;

    LLVM_PREFERRED_TYPE(InlineCommandRenderKind)
    unsigned RenderKind : 3;

    LLVM_PREFERRED_TYPE(CommandTraits::KnownCommandIDs)
    unsigned CommandID : CommandInfo::NumCommandIDBits;
  };
  enum { NumInlineCommandCommentBits = NumInlineContentCommentBits + 3 +
                                       CommandInfo::NumCommandIDBits };

  class HTMLTagCommentBitfields {
    friend class HTMLTagComment;

    LLVM_PREFERRED_TYPE(InlineContentCommentBitfields)
    unsigned : NumInlineContentCommentBits;

    /// True if we found that this tag is malformed in some way.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsMalformed : 1;
  };
  enum { NumHTMLTagCommentBits = NumInlineContentCommentBits + 1 };

  class HTMLStartTagCommentBitfields {
    friend class HTMLStartTagComment;

    LLVM_PREFERRED_TYPE(HTMLTagCommentBitfields)
    unsigned : NumHTMLTagCommentBits;

    /// True if this tag is self-closing (e. g., <br />).  This is based on tag
    /// spelling in comment (plain <br> would not set this flag).
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsSelfClosing : 1;
  };
  enum { NumHTMLStartTagCommentBits = NumHTMLTagCommentBits + 1 };

  class ParagraphCommentBitfields {
    friend class ParagraphComment;

    LLVM_PREFERRED_TYPE(CommentBitfields)
    unsigned : NumCommentBits;

    /// True if \c IsWhitespace field contains a valid value.
    LLVM_PREFERRED_TYPE(bool)
    mutable unsigned IsWhitespaceValid : 1;

    /// True if this comment AST node contains only whitespace.
    LLVM_PREFERRED_TYPE(bool)
    mutable unsigned IsWhitespace : 1;
  };
  enum { NumParagraphCommentBits = NumCommentBits + 2 };

  class BlockCommandCommentBitfields {
    friend class BlockCommandComment;

    LLVM_PREFERRED_TYPE(CommentBitfields)
    unsigned : NumCommentBits;

    LLVM_PREFERRED_TYPE(CommandTraits::KnownCommandIDs)
    unsigned CommandID : CommandInfo::NumCommandIDBits;

    /// Describes the syntax that was used in a documentation command.
    /// Contains values from CommandMarkerKind enum.
    LLVM_PREFERRED_TYPE(CommandMarkerKind)
    unsigned CommandMarker : 1;
  };
  enum { NumBlockCommandCommentBits = NumCommentBits +
                                      CommandInfo::NumCommandIDBits + 1 };

  class ParamCommandCommentBitfields {
    friend class ParamCommandComment;

    LLVM_PREFERRED_TYPE(BlockCommandCommentBitfields)
    unsigned : NumBlockCommandCommentBits;

    /// Parameter passing direction.
    LLVM_PREFERRED_TYPE(ParamCommandPassDirection)
    unsigned Direction : 2;

    /// True if direction was specified explicitly in the comment.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsDirectionExplicit : 1;
  };
  enum { NumParamCommandCommentBits = NumBlockCommandCommentBits + 3 };

  union {
    CommentBitfields CommentBits;
    InlineContentCommentBitfields InlineContentCommentBits;
    TextCommentBitfields TextCommentBits;
    InlineCommandCommentBitfields InlineCommandCommentBits;
    HTMLTagCommentBitfields HTMLTagCommentBits;
    HTMLStartTagCommentBitfields HTMLStartTagCommentBits;
    ParagraphCommentBitfields ParagraphCommentBits;
    BlockCommandCommentBitfields BlockCommandCommentBits;
    ParamCommandCommentBitfields ParamCommandCommentBits;
  };

  void setSourceRange(SourceRange SR) {
    Range = SR;
  }

  void setLocation(SourceLocation L) {
    Loc = L;
  }

public:
  struct Argument {
    SourceRange Range;
    StringRef Text;
  };

  Comment(CommentKind K,
          SourceLocation LocBegin,
          SourceLocation LocEnd) :
      Loc(LocBegin), Range(SourceRange(LocBegin, LocEnd)) {
    CommentBits.Kind = llvm::to_underlying(K);
  }

  CommentKind getCommentKind() const {
    return static_cast<CommentKind>(CommentBits.Kind);
  }

  const char *getCommentKindName() const;

  void dump() const;
  void dumpColor() const;
  void dump(raw_ostream &OS, const ASTContext &Context) const;

  SourceRange getSourceRange() const LLVM_READONLY { return Range; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }

  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }

  SourceLocation getLocation() const LLVM_READONLY { return Loc; }

  typedef Comment * const *child_iterator;

  child_iterator child_begin() const;
  child_iterator child_end() const;

  // TODO: const child iterator

  unsigned child_count() const {
    return child_end() - child_begin();
  }
};

/// Inline content (contained within a block).
/// Abstract class.
class InlineContentComment : public Comment {
protected:
  InlineContentComment(CommentKind K,
                       SourceLocation LocBegin,
                       SourceLocation LocEnd) :
      Comment(K, LocBegin, LocEnd) {
    InlineContentCommentBits.HasTrailingNewline = 0;
  }

public:
  static bool classof(const Comment *C) {
    return C->getCommentKind() >=
               CommentKind::FirstInlineContentCommentConstant &&
           C->getCommentKind() <= CommentKind::LastInlineContentCommentConstant;
  }

  void addTrailingNewline() {
    InlineContentCommentBits.HasTrailingNewline = 1;
  }

  bool hasTrailingNewline() const {
    return InlineContentCommentBits.HasTrailingNewline;
  }
};

/// Plain text.
class TextComment : public InlineContentComment {
  StringRef Text;

public:
  TextComment(SourceLocation LocBegin, SourceLocation LocEnd, StringRef Text)
      : InlineContentComment(CommentKind::TextComment, LocBegin, LocEnd),
        Text(Text) {
    TextCommentBits.IsWhitespaceValid = false;
  }

  static bool classof(const Comment *C) {
    return C->getCommentKind() == CommentKind::TextComment;
  }

  child_iterator child_begin() const { return nullptr; }

  child_iterator child_end() const { return nullptr; }

  StringRef getText() const LLVM_READONLY { return Text; }

  bool isWhitespace() const {
    if (TextCommentBits.IsWhitespaceValid)
      return TextCommentBits.IsWhitespace;

    TextCommentBits.IsWhitespace = isWhitespaceNoCache();
    TextCommentBits.IsWhitespaceValid = true;
    return TextCommentBits.IsWhitespace;
  }

private:
  bool isWhitespaceNoCache() const;
};

/// The most appropriate rendering mode for this command, chosen on command
/// semantics in Doxygen.
enum class InlineCommandRenderKind {
  Normal,
  Bold,
  Monospaced,
  Emphasized,
  Anchor
};

/// A command with word-like arguments that is considered inline content.
class InlineCommandComment : public InlineContentComment {
protected:
  /// Command arguments.
  ArrayRef<Argument> Args;

public:
  InlineCommandComment(SourceLocation LocBegin, SourceLocation LocEnd,
                       unsigned CommandID, InlineCommandRenderKind RK,
                       ArrayRef<Argument> Args)
      : InlineContentComment(CommentKind::InlineCommandComment, LocBegin,
                             LocEnd),
        Args(Args) {
    InlineCommandCommentBits.RenderKind = llvm::to_underlying(RK);
    InlineCommandCommentBits.CommandID = CommandID;
  }

  static bool classof(const Comment *C) {
    return C->getCommentKind() == CommentKind::InlineCommandComment;
  }

  child_iterator child_begin() const { return nullptr; }

  child_iterator child_end() const { return nullptr; }

  unsigned getCommandID() const {
    return InlineCommandCommentBits.CommandID;
  }

  StringRef getCommandName(const CommandTraits &Traits) const {
    return Traits.getCommandInfo(getCommandID())->Name;
  }

  SourceRange getCommandNameRange() const {
    return SourceRange(getBeginLoc().getLocWithOffset(-1), getEndLoc());
  }

  InlineCommandRenderKind getRenderKind() const {
    return static_cast<InlineCommandRenderKind>(
        InlineCommandCommentBits.RenderKind);
  }

  unsigned getNumArgs() const {
    return Args.size();
  }

  StringRef getArgText(unsigned Idx) const {
    return Args[Idx].Text;
  }

  SourceRange getArgRange(unsigned Idx) const {
    return Args[Idx].Range;
  }
};

/// Abstract class for opening and closing HTML tags.  HTML tags are always
/// treated as inline content (regardless HTML semantics).
class HTMLTagComment : public InlineContentComment {
protected:
  StringRef TagName;
  SourceRange TagNameRange;

  HTMLTagComment(CommentKind K,
                 SourceLocation LocBegin,
                 SourceLocation LocEnd,
                 StringRef TagName,
                 SourceLocation TagNameBegin,
                 SourceLocation TagNameEnd) :
      InlineContentComment(K, LocBegin, LocEnd),
      TagName(TagName),
      TagNameRange(TagNameBegin, TagNameEnd) {
    setLocation(TagNameBegin);
    HTMLTagCommentBits.IsMalformed = 0;
  }

public:
  static bool classof(const Comment *C) {
    return C->getCommentKind() >= CommentKind::FirstHTMLTagCommentConstant &&
           C->getCommentKind() <= CommentKind::LastHTMLTagCommentConstant;
  }

  StringRef getTagName() const LLVM_READONLY { return TagName; }

  SourceRange getTagNameSourceRange() const LLVM_READONLY {
    SourceLocation L = getLocation();
    return SourceRange(L.getLocWithOffset(1),
                       L.getLocWithOffset(1 + TagName.size()));
  }

  bool isMalformed() const {
    return HTMLTagCommentBits.IsMalformed;
  }

  void setIsMalformed() {
    HTMLTagCommentBits.IsMalformed = 1;
  }
};

/// An opening HTML tag with attributes.
class HTMLStartTagComment : public HTMLTagComment {
public:
  class Attribute {
  public:
    SourceLocation NameLocBegin;
    StringRef Name;

    SourceLocation EqualsLoc;

    SourceRange ValueRange;
    StringRef Value;

    Attribute() { }

    Attribute(SourceLocation NameLocBegin, StringRef Name)
        : NameLocBegin(NameLocBegin), Name(Name), EqualsLoc(SourceLocation()) {}

    Attribute(SourceLocation NameLocBegin, StringRef Name,
              SourceLocation EqualsLoc, SourceRange ValueRange, StringRef Value)
        : NameLocBegin(NameLocBegin), Name(Name), EqualsLoc(EqualsLoc),
          ValueRange(ValueRange), Value(Value) {}

    SourceLocation getNameLocEnd() const {
      return NameLocBegin.getLocWithOffset(Name.size());
    }

    SourceRange getNameRange() const {
      return SourceRange(NameLocBegin, getNameLocEnd());
    }
  };

private:
  ArrayRef<Attribute> Attributes;

public:
  HTMLStartTagComment(SourceLocation LocBegin, StringRef TagName)
      : HTMLTagComment(CommentKind::HTMLStartTagComment, LocBegin,
                       LocBegin.getLocWithOffset(1 + TagName.size()), TagName,
                       LocBegin.getLocWithOffset(1),
                       LocBegin.getLocWithOffset(1 + TagName.size())) {
    HTMLStartTagCommentBits.IsSelfClosing = false;
  }

  static bool classof(const Comment *C) {
    return C->getCommentKind() == CommentKind::HTMLStartTagComment;
  }

  child_iterator child_begin() const { return nullptr; }

  child_iterator child_end() const { return nullptr; }

  unsigned getNumAttrs() const {
    return Attributes.size();
  }

  const Attribute &getAttr(unsigned Idx) const {
    return Attributes[Idx];
  }

  void setAttrs(ArrayRef<Attribute> Attrs) {
    Attributes = Attrs;
    if (!Attrs.empty()) {
      const Attribute &Attr = Attrs.back();
      SourceLocation L = Attr.ValueRange.getEnd();
      if (L.isValid())
        Range.setEnd(L);
      else {
        Range.setEnd(Attr.getNameLocEnd());
      }
    }
  }

  void setGreaterLoc(SourceLocation GreaterLoc) {
    Range.setEnd(GreaterLoc);
  }

  bool isSelfClosing() const {
    return HTMLStartTagCommentBits.IsSelfClosing;
  }

  void setSelfClosing() {
    HTMLStartTagCommentBits.IsSelfClosing = true;
  }
};

/// A closing HTML tag.
class HTMLEndTagComment : public HTMLTagComment {
public:
  HTMLEndTagComment(SourceLocation LocBegin, SourceLocation LocEnd,
                    StringRef TagName)
      : HTMLTagComment(CommentKind::HTMLEndTagComment, LocBegin, LocEnd,
                       TagName, LocBegin.getLocWithOffset(2),
                       LocBegin.getLocWithOffset(2 + TagName.size())) {}

  static bool classof(const Comment *C) {
    return C->getCommentKind() == CommentKind::HTMLEndTagComment;
  }

  child_iterator child_begin() const { return nullptr; }

  child_iterator child_end() const { return nullptr; }
};

/// Block content (contains inline content).
/// Abstract class.
class BlockContentComment : public Comment {
protected:
  BlockContentComment(CommentKind K,
                      SourceLocation LocBegin,
                      SourceLocation LocEnd) :
      Comment(K, LocBegin, LocEnd)
  { }

public:
  static bool classof(const Comment *C) {
    return C->getCommentKind() >=
               CommentKind::FirstBlockContentCommentConstant &&
           C->getCommentKind() <= CommentKind::LastBlockContentCommentConstant;
  }
};

/// A single paragraph that contains inline content.
class ParagraphComment : public BlockContentComment {
  ArrayRef<InlineContentComment *> Content;

public:
  ParagraphComment(ArrayRef<InlineContentComment *> Content)
      : BlockContentComment(CommentKind::ParagraphComment, SourceLocation(),
                            SourceLocation()),
        Content(Content) {
    if (Content.empty()) {
      ParagraphCommentBits.IsWhitespace = true;
      ParagraphCommentBits.IsWhitespaceValid = true;
      return;
    }

    ParagraphCommentBits.IsWhitespaceValid = false;

    setSourceRange(SourceRange(Content.front()->getBeginLoc(),
                               Content.back()->getEndLoc()));
    setLocation(Content.front()->getBeginLoc());
  }

  static bool classof(const Comment *C) {
    return C->getCommentKind() == CommentKind::ParagraphComment;
  }

  child_iterator child_begin() const {
    return reinterpret_cast<child_iterator>(Content.begin());
  }

  child_iterator child_end() const {
    return reinterpret_cast<child_iterator>(Content.end());
  }

  bool isWhitespace() const {
    if (ParagraphCommentBits.IsWhitespaceValid)
      return ParagraphCommentBits.IsWhitespace;

    ParagraphCommentBits.IsWhitespace = isWhitespaceNoCache();
    ParagraphCommentBits.IsWhitespaceValid = true;
    return ParagraphCommentBits.IsWhitespace;
  }

private:
  bool isWhitespaceNoCache() const;
};

/// A command that has zero or more word-like arguments (number of word-like
/// arguments depends on command name) and a paragraph as an argument
/// (e. g., \\brief).
class BlockCommandComment : public BlockContentComment {
protected:
  /// Word-like arguments.
  ArrayRef<Argument> Args;

  /// Paragraph argument.
  ParagraphComment *Paragraph;

  BlockCommandComment(CommentKind K,
                      SourceLocation LocBegin,
                      SourceLocation LocEnd,
                      unsigned CommandID,
                      CommandMarkerKind CommandMarker) :
      BlockContentComment(K, LocBegin, LocEnd),
      Paragraph(nullptr) {
    setLocation(getCommandNameBeginLoc());
    BlockCommandCommentBits.CommandID = CommandID;
    BlockCommandCommentBits.CommandMarker = CommandMarker;
  }

public:
  BlockCommandComment(SourceLocation LocBegin, SourceLocation LocEnd,
                      unsigned CommandID, CommandMarkerKind CommandMarker)
      : BlockContentComment(CommentKind::BlockCommandComment, LocBegin, LocEnd),
        Paragraph(nullptr) {
    setLocation(getCommandNameBeginLoc());
    BlockCommandCommentBits.CommandID = CommandID;
    BlockCommandCommentBits.CommandMarker = CommandMarker;
  }

  static bool classof(const Comment *C) {
    return C->getCommentKind() >=
               CommentKind::FirstBlockCommandCommentConstant &&
           C->getCommentKind() <= CommentKind::LastBlockCommandCommentConstant;
  }

  child_iterator child_begin() const {
    return reinterpret_cast<child_iterator>(&Paragraph);
  }

  child_iterator child_end() const {
    return reinterpret_cast<child_iterator>(&Paragraph + 1);
  }

  unsigned getCommandID() const {
    return BlockCommandCommentBits.CommandID;
  }

  StringRef getCommandName(const CommandTraits &Traits) const {
    return Traits.getCommandInfo(getCommandID())->Name;
  }

  SourceLocation getCommandNameBeginLoc() const {
    return getBeginLoc().getLocWithOffset(1);
  }

  SourceRange getCommandNameRange(const CommandTraits &Traits) const {
    StringRef Name = getCommandName(Traits);
    return SourceRange(getCommandNameBeginLoc(),
                       getBeginLoc().getLocWithOffset(1 + Name.size()));
  }

  unsigned getNumArgs() const {
    return Args.size();
  }

  StringRef getArgText(unsigned Idx) const {
    return Args[Idx].Text;
  }

  SourceRange getArgRange(unsigned Idx) const {
    return Args[Idx].Range;
  }

  void setArgs(ArrayRef<Argument> A) {
    Args = A;
    if (Args.size() > 0) {
      SourceLocation NewLocEnd = Args.back().Range.getEnd();
      if (NewLocEnd.isValid())
        setSourceRange(SourceRange(getBeginLoc(), NewLocEnd));
    }
  }

  ParagraphComment *getParagraph() const LLVM_READONLY {
    return Paragraph;
  }

  bool hasNonWhitespaceParagraph() const {
    return Paragraph && !Paragraph->isWhitespace();
  }

  void setParagraph(ParagraphComment *PC) {
    Paragraph = PC;
    SourceLocation NewLocEnd = PC->getEndLoc();
    if (NewLocEnd.isValid())
      setSourceRange(SourceRange(getBeginLoc(), NewLocEnd));
  }

  CommandMarkerKind getCommandMarker() const LLVM_READONLY {
    return static_cast<CommandMarkerKind>(
        BlockCommandCommentBits.CommandMarker);
  }
};

enum class ParamCommandPassDirection { In, Out, InOut };

/// Doxygen \\param command.
class ParamCommandComment : public BlockCommandComment {
private:
  /// Parameter index in the function declaration.
  unsigned ParamIndex;

public:
  enum : unsigned {
    InvalidParamIndex = ~0U,
    VarArgParamIndex = ~0U/*InvalidParamIndex*/ - 1U
  };

  ParamCommandComment(SourceLocation LocBegin, SourceLocation LocEnd,
                      unsigned CommandID, CommandMarkerKind CommandMarker)
      : BlockCommandComment(CommentKind::ParamCommandComment, LocBegin, LocEnd,
                            CommandID, CommandMarker),
        ParamIndex(InvalidParamIndex) {
    ParamCommandCommentBits.Direction =
        llvm::to_underlying(ParamCommandPassDirection::In);
    ParamCommandCommentBits.IsDirectionExplicit = false;
  }

  static bool classof(const Comment *C) {
    return C->getCommentKind() == CommentKind::ParamCommandComment;
  }

  static const char *getDirectionAsString(ParamCommandPassDirection D);

  ParamCommandPassDirection getDirection() const LLVM_READONLY {
    return static_cast<ParamCommandPassDirection>(
        ParamCommandCommentBits.Direction);
  }

  bool isDirectionExplicit() const LLVM_READONLY {
    return ParamCommandCommentBits.IsDirectionExplicit;
  }

  void setDirection(ParamCommandPassDirection Direction, bool Explicit) {
    ParamCommandCommentBits.Direction = llvm::to_underlying(Direction);
    ParamCommandCommentBits.IsDirectionExplicit = Explicit;
  }

  bool hasParamName() const {
    return getNumArgs() > 0;
  }

  StringRef getParamName(const FullComment *FC) const;

  StringRef getParamNameAsWritten() const {
    return Args[0].Text;
  }

  SourceRange getParamNameRange() const {
    return Args[0].Range;
  }

  bool isParamIndexValid() const LLVM_READONLY {
    return ParamIndex != InvalidParamIndex;
  }

  bool isVarArgParam() const LLVM_READONLY {
    return ParamIndex == VarArgParamIndex;
  }

  void setIsVarArgParam() {
    ParamIndex = VarArgParamIndex;
    assert(isParamIndexValid());
  }

  unsigned getParamIndex() const LLVM_READONLY {
    assert(isParamIndexValid());
    assert(!isVarArgParam());
    return ParamIndex;
  }

  void setParamIndex(unsigned Index) {
    ParamIndex = Index;
    assert(isParamIndexValid());
    assert(!isVarArgParam());
  }
};

/// Doxygen \\tparam command, describes a template parameter.
class TParamCommandComment : public BlockCommandComment {
private:
  /// If this template parameter name was resolved (found in template parameter
  /// list), then this stores a list of position indexes in all template
  /// parameter lists.
  ///
  /// For example:
  /// \verbatim
  ///     template<typename C, template<typename T> class TT>
  ///     void test(TT<int> aaa);
  /// \endverbatim
  /// For C:  Position = { 0 }
  /// For TT: Position = { 1 }
  /// For T:  Position = { 1, 0 }
  ArrayRef<unsigned> Position;

public:
  TParamCommandComment(SourceLocation LocBegin, SourceLocation LocEnd,
                       unsigned CommandID, CommandMarkerKind CommandMarker)
      : BlockCommandComment(CommentKind::TParamCommandComment, LocBegin, LocEnd,
                            CommandID, CommandMarker) {}

  static bool classof(const Comment *C) {
    return C->getCommentKind() == CommentKind::TParamCommandComment;
  }

  bool hasParamName() const {
    return getNumArgs() > 0;
  }

  StringRef getParamName(const FullComment *FC) const;

  StringRef getParamNameAsWritten() const {
    return Args[0].Text;
  }

  SourceRange getParamNameRange() const {
    return Args[0].Range;
  }

  bool isPositionValid() const LLVM_READONLY {
    return !Position.empty();
  }

  unsigned getDepth() const {
    assert(isPositionValid());
    return Position.size();
  }

  unsigned getIndex(unsigned Depth) const {
    assert(isPositionValid());
    return Position[Depth];
  }

  void setPosition(ArrayRef<unsigned> NewPosition) {
    Position = NewPosition;
    assert(isPositionValid());
  }
};

/// A line of text contained in a verbatim block.
class VerbatimBlockLineComment : public Comment {
  StringRef Text;

public:
  VerbatimBlockLineComment(SourceLocation LocBegin, StringRef Text)
      : Comment(CommentKind::VerbatimBlockLineComment, LocBegin,
                LocBegin.getLocWithOffset(Text.size())),
        Text(Text) {}

  static bool classof(const Comment *C) {
    return C->getCommentKind() == CommentKind::VerbatimBlockLineComment;
  }

  child_iterator child_begin() const { return nullptr; }

  child_iterator child_end() const { return nullptr; }

  StringRef getText() const LLVM_READONLY {
    return Text;
  }
};

/// A verbatim block command (e. g., preformatted code).  Verbatim block has an
/// opening and a closing command and contains multiple lines of text
/// (VerbatimBlockLineComment nodes).
class VerbatimBlockComment : public BlockCommandComment {
protected:
  StringRef CloseName;
  SourceLocation CloseNameLocBegin;
  ArrayRef<VerbatimBlockLineComment *> Lines;

public:
  VerbatimBlockComment(SourceLocation LocBegin, SourceLocation LocEnd,
                       unsigned CommandID)
      : BlockCommandComment(CommentKind::VerbatimBlockComment, LocBegin, LocEnd,
                            CommandID,
                            CMK_At) // FIXME: improve source fidelity.
  {}

  static bool classof(const Comment *C) {
    return C->getCommentKind() == CommentKind::VerbatimBlockComment;
  }

  child_iterator child_begin() const {
    return reinterpret_cast<child_iterator>(Lines.begin());
  }

  child_iterator child_end() const {
    return reinterpret_cast<child_iterator>(Lines.end());
  }

  void setCloseName(StringRef Name, SourceLocation LocBegin) {
    CloseName = Name;
    CloseNameLocBegin = LocBegin;
  }

  void setLines(ArrayRef<VerbatimBlockLineComment *> L) {
    Lines = L;
  }

  StringRef getCloseName() const {
    return CloseName;
  }

  unsigned getNumLines() const {
    return Lines.size();
  }

  StringRef getText(unsigned LineIdx) const {
    return Lines[LineIdx]->getText();
  }
};

/// A verbatim line command.  Verbatim line has an opening command, a single
/// line of text (up to the newline after the opening command) and has no
/// closing command.
class VerbatimLineComment : public BlockCommandComment {
protected:
  StringRef Text;
  SourceLocation TextBegin;

public:
  VerbatimLineComment(SourceLocation LocBegin, SourceLocation LocEnd,
                      unsigned CommandID, SourceLocation TextBegin,
                      StringRef Text)
      : BlockCommandComment(CommentKind::VerbatimLineComment, LocBegin, LocEnd,
                            CommandID,
                            CMK_At), // FIXME: improve source fidelity.
        Text(Text), TextBegin(TextBegin) {}

  static bool classof(const Comment *C) {
    return C->getCommentKind() == CommentKind::VerbatimLineComment;
  }

  child_iterator child_begin() const { return nullptr; }

  child_iterator child_end() const { return nullptr; }

  StringRef getText() const {
    return Text;
  }

  SourceRange getTextRange() const {
    return SourceRange(TextBegin, getEndLoc());
  }
};

/// Information about the declaration, useful to clients of FullComment.
struct DeclInfo {
  /// Declaration the comment is actually attached to (in the source).
  /// Should not be NULL.
  const Decl *CommentDecl;

  /// CurrentDecl is the declaration with which the FullComment is associated.
  ///
  /// It can be different from \c CommentDecl.  It happens when we decide
  /// that the comment originally attached to \c CommentDecl is fine for
  /// \c CurrentDecl too (for example, for a redeclaration or an overrider of
  /// \c CommentDecl).
  ///
  /// The information in the DeclInfo corresponds to CurrentDecl.
  const Decl *CurrentDecl;

  /// Parameters that can be referenced by \\param if \c CommentDecl is something
  /// that we consider a "function".
  ArrayRef<const ParmVarDecl *> ParamVars;

  /// Function return type if \c CommentDecl is something that we consider
  /// a "function".
  QualType ReturnType;

  /// Template parameters that can be referenced by \\tparam if \c CommentDecl is
  /// a template (\c IsTemplateDecl or \c IsTemplatePartialSpecialization is
  /// true).
  const TemplateParameterList *TemplateParameters;

  /// A simplified description of \c CommentDecl kind that should be good enough
  /// for documentation rendering purposes.
  enum DeclKind {
    /// Everything else not explicitly mentioned below.
    OtherKind,

    /// Something that we consider a "function":
    /// \li function,
    /// \li function template,
    /// \li function template specialization,
    /// \li member function,
    /// \li member function template,
    /// \li member function template specialization,
    /// \li ObjC method,
    FunctionKind,

    /// Something that we consider a "class":
    /// \li class/struct,
    /// \li class template,
    /// \li class template (partial) specialization.
    ClassKind,

    /// Something that we consider a "variable":
    /// \li namespace scope variables and variable templates;
    /// \li static and non-static class data members and member templates;
    /// \li enumerators.
    VariableKind,

    /// A C++ namespace.
    NamespaceKind,

    /// A C++ typedef-name (a 'typedef' decl specifier or alias-declaration),
    /// see \c TypedefNameDecl.
    TypedefKind,

    /// An enumeration or scoped enumeration.
    EnumKind
  };

  /// What kind of template specialization \c CommentDecl is.
  enum TemplateDeclKind {
    NotTemplate,
    Template,
    TemplateSpecialization,
    TemplatePartialSpecialization
  };

  /// If false, only \c CommentDecl is valid.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsFilled : 1;

  /// Simplified kind of \c CommentDecl, see \c DeclKind enum.
  LLVM_PREFERRED_TYPE(DeclKind)
  unsigned Kind : 3;

  /// Is \c CommentDecl a template declaration.
  LLVM_PREFERRED_TYPE(TemplateDeclKind)
  unsigned TemplateKind : 2;

  /// Is \c CommentDecl an ObjCMethodDecl.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsObjCMethod : 1;

  /// Is \c CommentDecl a non-static member function of C++ class or
  /// instance method of ObjC class.
  /// Can be true only if \c IsFunctionDecl is true.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsInstanceMethod : 1;

  /// Is \c CommentDecl a static member function of C++ class or
  /// class method of ObjC class.
  /// Can be true only if \c IsFunctionDecl is true.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsClassMethod : 1;

  /// Is \c CommentDecl something we consider a "function" that's variadic.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsVariadic : 1;

  void fill();

  DeclKind getKind() const LLVM_READONLY {
    return static_cast<DeclKind>(Kind);
  }

  TemplateDeclKind getTemplateKind() const LLVM_READONLY {
    return static_cast<TemplateDeclKind>(TemplateKind);
  }

  bool involvesFunctionType() const { return !ReturnType.isNull(); }
};

/// A full comment attached to a declaration, contains block content.
class FullComment : public Comment {
  ArrayRef<BlockContentComment *> Blocks;
  DeclInfo *ThisDeclInfo;

public:
  FullComment(ArrayRef<BlockContentComment *> Blocks, DeclInfo *D)
      : Comment(CommentKind::FullComment, SourceLocation(), SourceLocation()),
        Blocks(Blocks), ThisDeclInfo(D) {
    if (Blocks.empty())
      return;

    setSourceRange(
        SourceRange(Blocks.front()->getBeginLoc(), Blocks.back()->getEndLoc()));
    setLocation(Blocks.front()->getBeginLoc());
  }

  static bool classof(const Comment *C) {
    return C->getCommentKind() == CommentKind::FullComment;
  }

  child_iterator child_begin() const {
    return reinterpret_cast<child_iterator>(Blocks.begin());
  }

  child_iterator child_end() const {
    return reinterpret_cast<child_iterator>(Blocks.end());
  }

  const Decl *getDecl() const LLVM_READONLY {
    return ThisDeclInfo->CommentDecl;
  }

  const DeclInfo *getDeclInfo() const LLVM_READONLY {
    if (!ThisDeclInfo->IsFilled)
      ThisDeclInfo->fill();
    return ThisDeclInfo;
  }

  ArrayRef<BlockContentComment *> getBlocks() const { return Blocks; }

};
} // end namespace comments
} // end namespace clang

#endif


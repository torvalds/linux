//===--- FormatToken.h - Format C++ code ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the FormatToken, a wrapper
/// around Token with additional information related to formatting.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_FORMATTOKEN_H
#define LLVM_CLANG_LIB_FORMAT_FORMATTOKEN_H

#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/OperatorPrecedence.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include <unordered_set>

namespace clang {
namespace format {

#define LIST_TOKEN_TYPES                                                       \
  TYPE(ArrayInitializerLSquare)                                                \
  TYPE(ArraySubscriptLSquare)                                                  \
  TYPE(AttributeColon)                                                         \
  TYPE(AttributeLParen)                                                        \
  TYPE(AttributeMacro)                                                         \
  TYPE(AttributeRParen)                                                        \
  TYPE(AttributeSquare)                                                        \
  TYPE(BinaryOperator)                                                         \
  TYPE(BitFieldColon)                                                          \
  TYPE(BlockComment)                                                           \
  /* l_brace of a block that is not the body of a (e.g. loop) statement. */    \
  TYPE(BlockLBrace)                                                            \
  TYPE(BracedListLBrace)                                                       \
  TYPE(CaseLabelArrow)                                                         \
  /* The colon at the end of a case label. */                                  \
  TYPE(CaseLabelColon)                                                         \
  TYPE(CastRParen)                                                             \
  TYPE(ClassLBrace)                                                            \
  TYPE(ClassRBrace)                                                            \
  /* ternary ?: expression */                                                  \
  TYPE(ConditionalExpr)                                                        \
  /* the condition in an if statement */                                       \
  TYPE(ConditionLParen)                                                        \
  TYPE(ConflictAlternative)                                                    \
  TYPE(ConflictEnd)                                                            \
  TYPE(ConflictStart)                                                          \
  /* l_brace of if/for/while */                                                \
  TYPE(ControlStatementLBrace)                                                 \
  TYPE(ControlStatementRBrace)                                                 \
  TYPE(CppCastLParen)                                                          \
  TYPE(CSharpGenericTypeConstraint)                                            \
  TYPE(CSharpGenericTypeConstraintColon)                                       \
  TYPE(CSharpGenericTypeConstraintComma)                                       \
  TYPE(CSharpNamedArgumentColon)                                               \
  TYPE(CSharpNullable)                                                         \
  TYPE(CSharpNullConditionalLSquare)                                           \
  TYPE(CSharpStringLiteral)                                                    \
  TYPE(CtorInitializerColon)                                                   \
  TYPE(CtorInitializerComma)                                                   \
  TYPE(CtorDtorDeclName)                                                       \
  TYPE(DesignatedInitializerLSquare)                                           \
  TYPE(DesignatedInitializerPeriod)                                            \
  TYPE(DictLiteral)                                                            \
  TYPE(DoWhile)                                                                \
  TYPE(ElseLBrace)                                                             \
  TYPE(ElseRBrace)                                                             \
  TYPE(EnumLBrace)                                                             \
  TYPE(EnumRBrace)                                                             \
  TYPE(FatArrow)                                                               \
  TYPE(ForEachMacro)                                                           \
  TYPE(FunctionAnnotationRParen)                                               \
  TYPE(FunctionDeclarationName)                                                \
  TYPE(FunctionDeclarationLParen)                                              \
  TYPE(FunctionLBrace)                                                         \
  TYPE(FunctionLikeOrFreestandingMacro)                                        \
  TYPE(FunctionTypeLParen)                                                     \
  /* The colons as part of a C11 _Generic selection */                         \
  TYPE(GenericSelectionColon)                                                  \
  /* The colon at the end of a goto label. */                                  \
  TYPE(GotoLabelColon)                                                         \
  TYPE(IfMacro)                                                                \
  TYPE(ImplicitStringLiteral)                                                  \
  TYPE(InheritanceColon)                                                       \
  TYPE(InheritanceComma)                                                       \
  TYPE(InlineASMBrace)                                                         \
  TYPE(InlineASMColon)                                                         \
  TYPE(InlineASMSymbolicNameLSquare)                                           \
  TYPE(JavaAnnotation)                                                         \
  TYPE(JsAndAndEqual)                                                          \
  TYPE(JsComputedPropertyName)                                                 \
  TYPE(JsExponentiation)                                                       \
  TYPE(JsExponentiationEqual)                                                  \
  TYPE(JsPipePipeEqual)                                                        \
  TYPE(JsPrivateIdentifier)                                                    \
  TYPE(JsTypeColon)                                                            \
  TYPE(JsTypeOperator)                                                         \
  TYPE(JsTypeOptionalQuestion)                                                 \
  TYPE(LambdaArrow)                                                            \
  TYPE(LambdaLBrace)                                                           \
  TYPE(LambdaLSquare)                                                          \
  TYPE(LeadingJavaAnnotation)                                                  \
  TYPE(LineComment)                                                            \
  TYPE(MacroBlockBegin)                                                        \
  TYPE(MacroBlockEnd)                                                          \
  TYPE(ModulePartitionColon)                                                   \
  TYPE(NamespaceLBrace)                                                        \
  TYPE(NamespaceMacro)                                                         \
  TYPE(NamespaceRBrace)                                                        \
  TYPE(NonNullAssertion)                                                       \
  TYPE(NullCoalescingEqual)                                                    \
  TYPE(NullCoalescingOperator)                                                 \
  TYPE(NullPropagatingOperator)                                                \
  TYPE(ObjCBlockLBrace)                                                        \
  TYPE(ObjCBlockLParen)                                                        \
  TYPE(ObjCDecl)                                                               \
  TYPE(ObjCForIn)                                                              \
  TYPE(ObjCMethodExpr)                                                         \
  TYPE(ObjCMethodSpecifier)                                                    \
  TYPE(ObjCProperty)                                                           \
  TYPE(ObjCStringLiteral)                                                      \
  TYPE(OverloadedOperator)                                                     \
  TYPE(OverloadedOperatorLParen)                                               \
  TYPE(PointerOrReference)                                                     \
  TYPE(ProtoExtensionLSquare)                                                  \
  TYPE(PureVirtualSpecifier)                                                   \
  TYPE(RangeBasedForLoopColon)                                                 \
  TYPE(RecordLBrace)                                                           \
  TYPE(RecordRBrace)                                                           \
  TYPE(RegexLiteral)                                                           \
  TYPE(RequiresClause)                                                         \
  TYPE(RequiresClauseInARequiresExpression)                                    \
  TYPE(RequiresExpression)                                                     \
  TYPE(RequiresExpressionLBrace)                                               \
  TYPE(RequiresExpressionLParen)                                               \
  TYPE(SelectorName)                                                           \
  TYPE(StartOfName)                                                            \
  TYPE(StatementAttributeLikeMacro)                                            \
  TYPE(StatementMacro)                                                         \
  /* A string that is part of a string concatenation. For C#, JavaScript, and  \
   * Java, it is used for marking whether a string needs parentheses around it \
   * if it is to be split into parts joined by `+`. For Verilog, whether       \
   * braces need to be added to split it. Not used for other languages. */     \
  TYPE(StringInConcatenation)                                                  \
  TYPE(StructLBrace)                                                           \
  TYPE(StructRBrace)                                                           \
  TYPE(StructuredBindingLSquare)                                               \
  TYPE(SwitchExpressionLabel)                                                  \
  TYPE(SwitchExpressionLBrace)                                                 \
  TYPE(TableGenBangOperator)                                                   \
  TYPE(TableGenCondOperator)                                                   \
  TYPE(TableGenCondOperatorColon)                                              \
  TYPE(TableGenCondOperatorComma)                                              \
  TYPE(TableGenDAGArgCloser)                                                   \
  TYPE(TableGenDAGArgListColon)                                                \
  TYPE(TableGenDAGArgListColonToAlign)                                         \
  TYPE(TableGenDAGArgListComma)                                                \
  TYPE(TableGenDAGArgListCommaToBreak)                                         \
  TYPE(TableGenDAGArgOpener)                                                   \
  TYPE(TableGenDAGArgOpenerToBreak)                                            \
  TYPE(TableGenDAGArgOperatorID)                                               \
  TYPE(TableGenDAGArgOperatorToBreak)                                          \
  TYPE(TableGenListCloser)                                                     \
  TYPE(TableGenListOpener)                                                     \
  TYPE(TableGenMultiLineString)                                                \
  TYPE(TableGenTrailingPasteOperator)                                          \
  TYPE(TableGenValueSuffix)                                                    \
  TYPE(TemplateCloser)                                                         \
  TYPE(TemplateOpener)                                                         \
  TYPE(TemplateString)                                                         \
  TYPE(TrailingAnnotation)                                                     \
  TYPE(TrailingReturnArrow)                                                    \
  TYPE(TrailingUnaryOperator)                                                  \
  TYPE(TypeDeclarationParen)                                                   \
  TYPE(TypeName)                                                               \
  TYPE(TypenameMacro)                                                          \
  TYPE(UnaryOperator)                                                          \
  TYPE(UnionLBrace)                                                            \
  TYPE(UnionRBrace)                                                            \
  TYPE(UntouchableMacroFunc)                                                   \
  /* Like in 'assign x = 0, y = 1;' . */                                       \
  TYPE(VerilogAssignComma)                                                     \
  /* like in begin : block */                                                  \
  TYPE(VerilogBlockLabelColon)                                                 \
  /* The square bracket for the dimension part of the type name.               \
   * In 'logic [1:0] x[1:0]', only the first '['. This way we can have space   \
   * before the first bracket but not the second. */                           \
  TYPE(VerilogDimensionedTypeName)                                             \
  /* list of port connections or parameters in a module instantiation */       \
  TYPE(VerilogInstancePortComma)                                               \
  TYPE(VerilogInstancePortLParen)                                              \
  /* A parenthesized list within which line breaks are inserted by the         \
   * formatter, for example the list of ports in a module header. */           \
  TYPE(VerilogMultiLineListLParen)                                             \
  /* for the base in a number literal, not including the quote */              \
  TYPE(VerilogNumberBase)                                                      \
  /* like `(strong1, pull0)` */                                                \
  TYPE(VerilogStrength)                                                        \
  /* Things inside the table in user-defined primitives. */                    \
  TYPE(VerilogTableItem)                                                       \
  /* those that separate ports of different types */                           \
  TYPE(VerilogTypeComma)                                                       \
  TYPE(Unknown)

/// Determines the semantic type of a syntactic token, e.g. whether "<" is a
/// template opener or binary operator.
enum TokenType : uint8_t {
#define TYPE(X) TT_##X,
  LIST_TOKEN_TYPES
#undef TYPE
      NUM_TOKEN_TYPES
};

/// Determines the name of a token type.
const char *getTokenTypeName(TokenType Type);

// Represents what type of block a set of braces open.
enum BraceBlockKind { BK_Unknown, BK_Block, BK_BracedInit };

// The packing kind of a function's parameters.
enum ParameterPackingKind { PPK_BinPacked, PPK_OnePerLine, PPK_Inconclusive };

enum FormatDecision { FD_Unformatted, FD_Continue, FD_Break };

/// Roles a token can take in a configured macro expansion.
enum MacroRole {
  /// The token was expanded from a macro argument when formatting the expanded
  /// token sequence.
  MR_ExpandedArg,
  /// The token is part of a macro argument that was previously formatted as
  /// expansion when formatting the unexpanded macro call.
  MR_UnexpandedArg,
  /// The token was expanded from a macro definition, and is not visible as part
  /// of the macro call.
  MR_Hidden,
};

struct FormatToken;

/// Contains information on the token's role in a macro expansion.
///
/// Given the following definitions:
/// A(X) = [ X ]
/// B(X) = < X >
/// C(X) = X
///
/// Consider the macro call:
/// A({B(C(C(x)))}) -> [{<x>}]
///
/// In this case, the tokens of the unexpanded macro call will have the
/// following relevant entries in their macro context (note that formatting
/// the unexpanded macro call happens *after* formatting the expanded macro
/// call):
///                   A( { B( C( C(x) ) ) } )
/// Role:             NN U NN NN NNUN N N U N  (N=None, U=UnexpandedArg)
///
///                   [  { <       x    > } ]
/// Role:             H  E H       E    H E H  (H=Hidden, E=ExpandedArg)
/// ExpandedFrom[0]:  A  A A       A    A A A
/// ExpandedFrom[1]:       B       B    B
/// ExpandedFrom[2]:               C
/// ExpandedFrom[3]:               C
/// StartOfExpansion: 1  0 1       2    0 0 0
/// EndOfExpansion:   0  0 0       2    1 0 1
struct MacroExpansion {
  MacroExpansion(MacroRole Role) : Role(Role) {}

  /// The token's role in the macro expansion.
  /// When formatting an expanded macro, all tokens that are part of macro
  /// arguments will be MR_ExpandedArg, while all tokens that are not visible in
  /// the macro call will be MR_Hidden.
  /// When formatting an unexpanded macro call, all tokens that are part of
  /// macro arguments will be MR_UnexpandedArg.
  MacroRole Role;

  /// The stack of macro call identifier tokens this token was expanded from.
  llvm::SmallVector<FormatToken *, 1> ExpandedFrom;

  /// The number of expansions of which this macro is the first entry.
  unsigned StartOfExpansion = 0;

  /// The number of currently open expansions in \c ExpandedFrom this macro is
  /// the last token in.
  unsigned EndOfExpansion = 0;
};

class TokenRole;
class AnnotatedLine;

/// A wrapper around a \c Token storing information about the
/// whitespace characters preceding it.
struct FormatToken {
  FormatToken()
      : HasUnescapedNewline(false), IsMultiline(false), IsFirst(false),
        MustBreakBefore(false), MustBreakBeforeFinalized(false),
        IsUnterminatedLiteral(false), CanBreakBefore(false),
        ClosesTemplateDeclaration(false), StartsBinaryExpression(false),
        EndsBinaryExpression(false), PartOfMultiVariableDeclStmt(false),
        ContinuesLineCommentSection(false), Finalized(false),
        ClosesRequiresClause(false), EndsCppAttributeGroup(false),
        BlockKind(BK_Unknown), Decision(FD_Unformatted),
        PackingKind(PPK_Inconclusive), TypeIsFinalized(false),
        Type(TT_Unknown) {}

  /// The \c Token.
  Token Tok;

  /// The raw text of the token.
  ///
  /// Contains the raw token text without leading whitespace and without leading
  /// escaped newlines.
  StringRef TokenText;

  /// A token can have a special role that can carry extra information
  /// about the token's formatting.
  /// FIXME: Make FormatToken for parsing and AnnotatedToken two different
  /// classes and make this a unique_ptr in the AnnotatedToken class.
  std::shared_ptr<TokenRole> Role;

  /// The range of the whitespace immediately preceding the \c Token.
  SourceRange WhitespaceRange;

  /// Whether there is at least one unescaped newline before the \c
  /// Token.
  unsigned HasUnescapedNewline : 1;

  /// Whether the token text contains newlines (escaped or not).
  unsigned IsMultiline : 1;

  /// Indicates that this is the first token of the file.
  unsigned IsFirst : 1;

  /// Whether there must be a line break before this token.
  ///
  /// This happens for example when a preprocessor directive ended directly
  /// before the token.
  unsigned MustBreakBefore : 1;

  /// Whether MustBreakBefore is finalized during parsing and must not
  /// be reset between runs.
  unsigned MustBreakBeforeFinalized : 1;

  /// Set to \c true if this token is an unterminated literal.
  unsigned IsUnterminatedLiteral : 1;

  /// \c true if it is allowed to break before this token.
  unsigned CanBreakBefore : 1;

  /// \c true if this is the ">" of "template<..>".
  unsigned ClosesTemplateDeclaration : 1;

  /// \c true if this token starts a binary expression, i.e. has at least
  /// one fake l_paren with a precedence greater than prec::Unknown.
  unsigned StartsBinaryExpression : 1;
  /// \c true if this token ends a binary expression.
  unsigned EndsBinaryExpression : 1;

  /// Is this token part of a \c DeclStmt defining multiple variables?
  ///
  /// Only set if \c Type == \c TT_StartOfName.
  unsigned PartOfMultiVariableDeclStmt : 1;

  /// Does this line comment continue a line comment section?
  ///
  /// Only set to true if \c Type == \c TT_LineComment.
  unsigned ContinuesLineCommentSection : 1;

  /// If \c true, this token has been fully formatted (indented and
  /// potentially re-formatted inside), and we do not allow further formatting
  /// changes.
  unsigned Finalized : 1;

  /// \c true if this is the last token within requires clause.
  unsigned ClosesRequiresClause : 1;

  /// \c true if this token ends a group of C++ attributes.
  unsigned EndsCppAttributeGroup : 1;

private:
  /// Contains the kind of block if this token is a brace.
  unsigned BlockKind : 2;

public:
  BraceBlockKind getBlockKind() const {
    return static_cast<BraceBlockKind>(BlockKind);
  }
  void setBlockKind(BraceBlockKind BBK) {
    BlockKind = BBK;
    assert(getBlockKind() == BBK && "BraceBlockKind overflow!");
  }

private:
  /// Stores the formatting decision for the token once it was made.
  unsigned Decision : 2;

public:
  FormatDecision getDecision() const {
    return static_cast<FormatDecision>(Decision);
  }
  void setDecision(FormatDecision D) {
    Decision = D;
    assert(getDecision() == D && "FormatDecision overflow!");
  }

private:
  /// If this is an opening parenthesis, how are the parameters packed?
  unsigned PackingKind : 2;

public:
  ParameterPackingKind getPackingKind() const {
    return static_cast<ParameterPackingKind>(PackingKind);
  }
  void setPackingKind(ParameterPackingKind K) {
    PackingKind = K;
    assert(getPackingKind() == K && "ParameterPackingKind overflow!");
  }

private:
  unsigned TypeIsFinalized : 1;
  TokenType Type;

public:
  /// Returns the token's type, e.g. whether "<" is a template opener or
  /// binary operator.
  TokenType getType() const { return Type; }
  void setType(TokenType T) {
    // If this token is a macro argument while formatting an unexpanded macro
    // call, we do not change its type any more - the type was deduced from
    // formatting the expanded macro stream already.
    if (MacroCtx && MacroCtx->Role == MR_UnexpandedArg)
      return;
    assert((!TypeIsFinalized || T == Type) &&
           "Please use overwriteFixedType to change a fixed type.");
    Type = T;
  }
  /// Sets the type and also the finalized flag. This prevents the type to be
  /// reset in TokenAnnotator::resetTokenMetadata(). If the type needs to be set
  /// to another one please use overwriteFixedType, or even better remove the
  /// need to reassign the type.
  void setFinalizedType(TokenType T) {
    if (MacroCtx && MacroCtx->Role == MR_UnexpandedArg)
      return;
    Type = T;
    TypeIsFinalized = true;
  }
  void overwriteFixedType(TokenType T) {
    if (MacroCtx && MacroCtx->Role == MR_UnexpandedArg)
      return;
    TypeIsFinalized = false;
    setType(T);
  }
  bool isTypeFinalized() const { return TypeIsFinalized; }

  /// Used to set an operator precedence explicitly.
  prec::Level ForcedPrecedence = prec::Unknown;

  /// The number of newlines immediately before the \c Token.
  ///
  /// This can be used to determine what the user wrote in the original code
  /// and thereby e.g. leave an empty line between two function definitions.
  unsigned NewlinesBefore = 0;

  /// The number of newlines immediately before the \c Token after formatting.
  ///
  /// This is used to avoid overlapping whitespace replacements when \c Newlines
  /// is recomputed for a finalized preprocessor branching directive.
  int Newlines = -1;

  /// The offset just past the last '\n' in this token's leading
  /// whitespace (relative to \c WhiteSpaceStart). 0 if there is no '\n'.
  unsigned LastNewlineOffset = 0;

  /// The width of the non-whitespace parts of the token (or its first
  /// line for multi-line tokens) in columns.
  /// We need this to correctly measure number of columns a token spans.
  unsigned ColumnWidth = 0;

  /// Contains the width in columns of the last line of a multi-line
  /// token.
  unsigned LastLineColumnWidth = 0;

  /// The number of spaces that should be inserted before this token.
  unsigned SpacesRequiredBefore = 0;

  /// Number of parameters, if this is "(", "[" or "<".
  unsigned ParameterCount = 0;

  /// Number of parameters that are nested blocks,
  /// if this is "(", "[" or "<".
  unsigned BlockParameterCount = 0;

  /// If this is a bracket ("<", "(", "[" or "{"), contains the kind of
  /// the surrounding bracket.
  tok::TokenKind ParentBracket = tok::unknown;

  /// The total length of the unwrapped line up to and including this
  /// token.
  unsigned TotalLength = 0;

  /// The original 0-based column of this token, including expanded tabs.
  /// The configured TabWidth is used as tab width.
  unsigned OriginalColumn = 0;

  /// The length of following tokens until the next natural split point,
  /// or the next token that can be broken.
  unsigned UnbreakableTailLength = 0;

  // FIXME: Come up with a 'cleaner' concept.
  /// The binding strength of a token. This is a combined value of
  /// operator precedence, parenthesis nesting, etc.
  unsigned BindingStrength = 0;

  /// The nesting level of this token, i.e. the number of surrounding (),
  /// [], {} or <>.
  unsigned NestingLevel = 0;

  /// The indent level of this token. Copied from the surrounding line.
  unsigned IndentLevel = 0;

  /// Penalty for inserting a line break before this token.
  unsigned SplitPenalty = 0;

  /// If this is the first ObjC selector name in an ObjC method
  /// definition or call, this contains the length of the longest name.
  ///
  /// This being set to 0 means that the selectors should not be colon-aligned,
  /// e.g. because several of them are block-type.
  unsigned LongestObjCSelectorName = 0;

  /// If this is the first ObjC selector name in an ObjC method
  /// definition or call, this contains the number of parts that the whole
  /// selector consist of.
  unsigned ObjCSelectorNameParts = 0;

  /// The 0-based index of the parameter/argument. For ObjC it is set
  /// for the selector name token.
  /// For now calculated only for ObjC.
  unsigned ParameterIndex = 0;

  /// Stores the number of required fake parentheses and the
  /// corresponding operator precedence.
  ///
  /// If multiple fake parentheses start at a token, this vector stores them in
  /// reverse order, i.e. inner fake parenthesis first.
  SmallVector<prec::Level, 4> FakeLParens;
  /// Insert this many fake ) after this token for correct indentation.
  unsigned FakeRParens = 0;

  /// If this is an operator (or "."/"->") in a sequence of operators
  /// with the same precedence, contains the 0-based operator index.
  unsigned OperatorIndex = 0;

  /// If this is an operator (or "."/"->") in a sequence of operators
  /// with the same precedence, points to the next operator.
  FormatToken *NextOperator = nullptr;

  /// If this is a bracket, this points to the matching one.
  FormatToken *MatchingParen = nullptr;

  /// The previous token in the unwrapped line.
  FormatToken *Previous = nullptr;

  /// The next token in the unwrapped line.
  FormatToken *Next = nullptr;

  /// The first token in set of column elements.
  bool StartsColumn = false;

  /// This notes the start of the line of an array initializer.
  bool ArrayInitializerLineStart = false;

  /// This starts an array initializer.
  bool IsArrayInitializer = false;

  /// Is optional and can be removed.
  bool Optional = false;

  /// Might be function declaration open/closing paren.
  bool MightBeFunctionDeclParen = false;

  /// Number of optional braces to be inserted after this token:
  ///   -1: a single left brace
  ///    0: no braces
  ///   >0: number of right braces
  int8_t BraceCount = 0;

  /// If this token starts a block, this contains all the unwrapped lines
  /// in it.
  SmallVector<AnnotatedLine *, 1> Children;

  // Contains all attributes related to how this token takes part
  // in a configured macro expansion.
  std::optional<MacroExpansion> MacroCtx;

  /// When macro expansion introduces nodes with children, those are marked as
  /// \c MacroParent.
  /// FIXME: The formatting code currently hard-codes the assumption that
  /// child nodes are introduced by blocks following an opening brace.
  /// This is deeply baked into the code and disentangling this will require
  /// signficant refactorings. \c MacroParent allows us to special-case the
  /// cases in which we treat parents as block-openers for now.
  bool MacroParent = false;

  bool is(tok::TokenKind Kind) const { return Tok.is(Kind); }
  bool is(TokenType TT) const { return getType() == TT; }
  bool is(const IdentifierInfo *II) const {
    return II && II == Tok.getIdentifierInfo();
  }
  bool is(tok::PPKeywordKind Kind) const {
    return Tok.getIdentifierInfo() &&
           Tok.getIdentifierInfo()->getPPKeywordID() == Kind;
  }
  bool is(BraceBlockKind BBK) const { return getBlockKind() == BBK; }
  bool is(ParameterPackingKind PPK) const { return getPackingKind() == PPK; }

  template <typename A, typename B> bool isOneOf(A K1, B K2) const {
    return is(K1) || is(K2);
  }
  template <typename A, typename B, typename... Ts>
  bool isOneOf(A K1, B K2, Ts... Ks) const {
    return is(K1) || isOneOf(K2, Ks...);
  }
  template <typename T> bool isNot(T Kind) const { return !is(Kind); }

  bool isIf(bool AllowConstexprMacro = true) const {
    return is(tok::kw_if) || endsSequence(tok::kw_constexpr, tok::kw_if) ||
           (endsSequence(tok::identifier, tok::kw_if) && AllowConstexprMacro);
  }

  bool closesScopeAfterBlock() const {
    if (getBlockKind() == BK_Block)
      return true;
    if (closesScope())
      return Previous->closesScopeAfterBlock();
    return false;
  }

  /// \c true if this token starts a sequence with the given tokens in order,
  /// following the ``Next`` pointers, ignoring comments.
  template <typename A, typename... Ts>
  bool startsSequence(A K1, Ts... Tokens) const {
    return startsSequenceInternal(K1, Tokens...);
  }

  /// \c true if this token ends a sequence with the given tokens in order,
  /// following the ``Previous`` pointers, ignoring comments.
  /// For example, given tokens [T1, T2, T3], the function returns true if
  /// 3 tokens ending at this (ignoring comments) are [T3, T2, T1]. In other
  /// words, the tokens passed to this function need to the reverse of the
  /// order the tokens appear in code.
  template <typename A, typename... Ts>
  bool endsSequence(A K1, Ts... Tokens) const {
    return endsSequenceInternal(K1, Tokens...);
  }

  bool isStringLiteral() const { return tok::isStringLiteral(Tok.getKind()); }

  bool isAttribute() const {
    return isOneOf(tok::kw___attribute, tok::kw___declspec, TT_AttributeMacro);
  }

  bool isObjCAtKeyword(tok::ObjCKeywordKind Kind) const {
    return Tok.isObjCAtKeyword(Kind);
  }

  bool isAccessSpecifierKeyword() const {
    return isOneOf(tok::kw_public, tok::kw_protected, tok::kw_private);
  }

  bool isAccessSpecifier(bool ColonRequired = true) const {
    if (!isAccessSpecifierKeyword())
      return false;
    if (!ColonRequired)
      return true;
    const auto *NextNonComment = getNextNonComment();
    return NextNonComment && NextNonComment->is(tok::colon);
  }

  bool canBePointerOrReferenceQualifier() const {
    return isOneOf(tok::kw_const, tok::kw_restrict, tok::kw_volatile,
                   tok::kw__Nonnull, tok::kw__Nullable,
                   tok::kw__Null_unspecified, tok::kw___ptr32, tok::kw___ptr64,
                   tok::kw___funcref) ||
           isAttribute();
  }

  [[nodiscard]] bool isTypeName(const LangOptions &LangOpts) const;
  [[nodiscard]] bool isTypeOrIdentifier(const LangOptions &LangOpts) const;

  bool isObjCAccessSpecifier() const {
    return is(tok::at) && Next &&
           (Next->isObjCAtKeyword(tok::objc_public) ||
            Next->isObjCAtKeyword(tok::objc_protected) ||
            Next->isObjCAtKeyword(tok::objc_package) ||
            Next->isObjCAtKeyword(tok::objc_private));
  }

  /// Returns whether \p Tok is ([{ or an opening < of a template or in
  /// protos.
  bool opensScope() const {
    if (is(TT_TemplateString) && TokenText.ends_with("${"))
      return true;
    if (is(TT_DictLiteral) && is(tok::less))
      return true;
    return isOneOf(tok::l_paren, tok::l_brace, tok::l_square,
                   TT_TemplateOpener);
  }
  /// Returns whether \p Tok is )]} or a closing > of a template or in
  /// protos.
  bool closesScope() const {
    if (is(TT_TemplateString) && TokenText.starts_with("}"))
      return true;
    if (is(TT_DictLiteral) && is(tok::greater))
      return true;
    return isOneOf(tok::r_paren, tok::r_brace, tok::r_square,
                   TT_TemplateCloser);
  }

  /// Returns \c true if this is a "." or "->" accessing a member.
  bool isMemberAccess() const {
    return isOneOf(tok::arrow, tok::period, tok::arrowstar) &&
           !isOneOf(TT_DesignatedInitializerPeriod, TT_TrailingReturnArrow,
                    TT_LambdaArrow, TT_LeadingJavaAnnotation);
  }

  bool isPointerOrReference() const {
    return isOneOf(tok::star, tok::amp, tok::ampamp);
  }

  bool isCppAlternativeOperatorKeyword() const {
    assert(!TokenText.empty());
    if (!isalpha(TokenText[0]))
      return false;

    switch (Tok.getKind()) {
    case tok::ampamp:
    case tok::ampequal:
    case tok::amp:
    case tok::pipe:
    case tok::tilde:
    case tok::exclaim:
    case tok::exclaimequal:
    case tok::pipepipe:
    case tok::pipeequal:
    case tok::caret:
    case tok::caretequal:
      return true;
    default:
      return false;
    }
  }

  bool isUnaryOperator() const {
    switch (Tok.getKind()) {
    case tok::plus:
    case tok::plusplus:
    case tok::minus:
    case tok::minusminus:
    case tok::exclaim:
    case tok::tilde:
    case tok::kw_sizeof:
    case tok::kw_alignof:
      return true;
    default:
      return false;
    }
  }

  bool isBinaryOperator() const {
    // Comma is a binary operator, but does not behave as such wrt. formatting.
    return getPrecedence() > prec::Comma;
  }

  bool isTrailingComment() const {
    return is(tok::comment) &&
           (is(TT_LineComment) || !Next || Next->NewlinesBefore > 0);
  }

  /// Returns \c true if this is a keyword that can be used
  /// like a function call (e.g. sizeof, typeid, ...).
  bool isFunctionLikeKeyword() const {
    if (isAttribute())
      return true;

    return isOneOf(tok::kw_throw, tok::kw_typeid, tok::kw_return,
                   tok::kw_sizeof, tok::kw_alignof, tok::kw_alignas,
                   tok::kw_decltype, tok::kw_noexcept, tok::kw_static_assert,
                   tok::kw__Atomic,
#define TRANSFORM_TYPE_TRAIT_DEF(_, Trait) tok::kw___##Trait,
#include "clang/Basic/TransformTypeTraits.def"
                   tok::kw_requires);
  }

  /// Returns \c true if this is a string literal that's like a label,
  /// e.g. ends with "=" or ":".
  bool isLabelString() const {
    if (isNot(tok::string_literal))
      return false;
    StringRef Content = TokenText;
    if (Content.starts_with("\"") || Content.starts_with("'"))
      Content = Content.drop_front(1);
    if (Content.ends_with("\"") || Content.ends_with("'"))
      Content = Content.drop_back(1);
    Content = Content.trim();
    return Content.size() > 1 &&
           (Content.back() == ':' || Content.back() == '=');
  }

  /// Returns actual token start location without leading escaped
  /// newlines and whitespace.
  ///
  /// This can be different to Tok.getLocation(), which includes leading escaped
  /// newlines.
  SourceLocation getStartOfNonWhitespace() const {
    return WhitespaceRange.getEnd();
  }

  /// Returns \c true if the range of whitespace immediately preceding the \c
  /// Token is not empty.
  bool hasWhitespaceBefore() const {
    return WhitespaceRange.getBegin() != WhitespaceRange.getEnd();
  }

  prec::Level getPrecedence() const {
    if (ForcedPrecedence != prec::Unknown)
      return ForcedPrecedence;
    return getBinOpPrecedence(Tok.getKind(), /*GreaterThanIsOperator=*/true,
                              /*CPlusPlus11=*/true);
  }

  /// Returns the previous token ignoring comments.
  [[nodiscard]] FormatToken *getPreviousNonComment() const {
    FormatToken *Tok = Previous;
    while (Tok && Tok->is(tok::comment))
      Tok = Tok->Previous;
    return Tok;
  }

  /// Returns the next token ignoring comments.
  [[nodiscard]] FormatToken *getNextNonComment() const {
    FormatToken *Tok = Next;
    while (Tok && Tok->is(tok::comment))
      Tok = Tok->Next;
    return Tok;
  }

  /// Returns \c true if this token ends a block indented initializer list.
  [[nodiscard]] bool isBlockIndentedInitRBrace(const FormatStyle &Style) const;

  /// Returns \c true if this tokens starts a block-type list, i.e. a
  /// list that should be indented with a block indent.
  [[nodiscard]] bool opensBlockOrBlockTypeList(const FormatStyle &Style) const;

  /// Returns whether the token is the left square bracket of a C++
  /// structured binding declaration.
  bool isCppStructuredBinding(bool IsCpp) const {
    if (!IsCpp || isNot(tok::l_square))
      return false;
    const FormatToken *T = this;
    do {
      T = T->getPreviousNonComment();
    } while (T && T->isOneOf(tok::kw_const, tok::kw_volatile, tok::amp,
                             tok::ampamp));
    return T && T->is(tok::kw_auto);
  }

  /// Same as opensBlockOrBlockTypeList, but for the closing token.
  bool closesBlockOrBlockTypeList(const FormatStyle &Style) const {
    if (is(TT_TemplateString) && closesScope())
      return true;
    return MatchingParen && MatchingParen->opensBlockOrBlockTypeList(Style);
  }

  /// Return the actual namespace token, if this token starts a namespace
  /// block.
  const FormatToken *getNamespaceToken() const {
    const FormatToken *NamespaceTok = this;
    if (is(tok::comment))
      NamespaceTok = NamespaceTok->getNextNonComment();
    // Detect "(inline|export)? namespace" in the beginning of a line.
    if (NamespaceTok && NamespaceTok->isOneOf(tok::kw_inline, tok::kw_export))
      NamespaceTok = NamespaceTok->getNextNonComment();
    return NamespaceTok &&
                   NamespaceTok->isOneOf(tok::kw_namespace, TT_NamespaceMacro)
               ? NamespaceTok
               : nullptr;
  }

  void copyFrom(const FormatToken &Tok) { *this = Tok; }

private:
  // Only allow copying via the explicit copyFrom method.
  FormatToken(const FormatToken &) = delete;
  FormatToken &operator=(const FormatToken &) = default;

  template <typename A, typename... Ts>
  bool startsSequenceInternal(A K1, Ts... Tokens) const {
    if (is(tok::comment) && Next)
      return Next->startsSequenceInternal(K1, Tokens...);
    return is(K1) && Next && Next->startsSequenceInternal(Tokens...);
  }

  template <typename A> bool startsSequenceInternal(A K1) const {
    if (is(tok::comment) && Next)
      return Next->startsSequenceInternal(K1);
    return is(K1);
  }

  template <typename A, typename... Ts> bool endsSequenceInternal(A K1) const {
    if (is(tok::comment) && Previous)
      return Previous->endsSequenceInternal(K1);
    return is(K1);
  }

  template <typename A, typename... Ts>
  bool endsSequenceInternal(A K1, Ts... Tokens) const {
    if (is(tok::comment) && Previous)
      return Previous->endsSequenceInternal(K1, Tokens...);
    return is(K1) && Previous && Previous->endsSequenceInternal(Tokens...);
  }
};

class ContinuationIndenter;
struct LineState;

class TokenRole {
public:
  TokenRole(const FormatStyle &Style) : Style(Style) {}
  virtual ~TokenRole();

  /// After the \c TokenAnnotator has finished annotating all the tokens,
  /// this function precomputes required information for formatting.
  virtual void precomputeFormattingInfos(const FormatToken *Token);

  /// Apply the special formatting that the given role demands.
  ///
  /// Assumes that the token having this role is already formatted.
  ///
  /// Continues formatting from \p State leaving indentation to \p Indenter and
  /// returns the total penalty that this formatting incurs.
  virtual unsigned formatFromToken(LineState &State,
                                   ContinuationIndenter *Indenter,
                                   bool DryRun) {
    return 0;
  }

  /// Same as \c formatFromToken, but assumes that the first token has
  /// already been set thereby deciding on the first line break.
  virtual unsigned formatAfterToken(LineState &State,
                                    ContinuationIndenter *Indenter,
                                    bool DryRun) {
    return 0;
  }

  /// Notifies the \c Role that a comma was found.
  virtual void CommaFound(const FormatToken *Token) {}

  virtual const FormatToken *lastComma() { return nullptr; }

protected:
  const FormatStyle &Style;
};

class CommaSeparatedList : public TokenRole {
public:
  CommaSeparatedList(const FormatStyle &Style)
      : TokenRole(Style), HasNestedBracedList(false) {}

  void precomputeFormattingInfos(const FormatToken *Token) override;

  unsigned formatAfterToken(LineState &State, ContinuationIndenter *Indenter,
                            bool DryRun) override;

  unsigned formatFromToken(LineState &State, ContinuationIndenter *Indenter,
                           bool DryRun) override;

  /// Adds \p Token as the next comma to the \c CommaSeparated list.
  void CommaFound(const FormatToken *Token) override {
    Commas.push_back(Token);
  }

  const FormatToken *lastComma() override {
    if (Commas.empty())
      return nullptr;
    return Commas.back();
  }

private:
  /// A struct that holds information on how to format a given list with
  /// a specific number of columns.
  struct ColumnFormat {
    /// The number of columns to use.
    unsigned Columns;

    /// The total width in characters.
    unsigned TotalWidth;

    /// The number of lines required for this format.
    unsigned LineCount;

    /// The size of each column in characters.
    SmallVector<unsigned, 8> ColumnSizes;
  };

  /// Calculate which \c ColumnFormat fits best into
  /// \p RemainingCharacters.
  const ColumnFormat *getColumnFormat(unsigned RemainingCharacters) const;

  /// The ordered \c FormatTokens making up the commas of this list.
  SmallVector<const FormatToken *, 8> Commas;

  /// The length of each of the list's items in characters including the
  /// trailing comma.
  SmallVector<unsigned, 8> ItemLengths;

  /// Precomputed formats that can be used for this list.
  SmallVector<ColumnFormat, 4> Formats;

  bool HasNestedBracedList;
};

/// Encapsulates keywords that are context sensitive or for languages not
/// properly supported by Clang's lexer.
struct AdditionalKeywords {
  AdditionalKeywords(IdentifierTable &IdentTable) {
    kw_final = &IdentTable.get("final");
    kw_override = &IdentTable.get("override");
    kw_in = &IdentTable.get("in");
    kw_of = &IdentTable.get("of");
    kw_CF_CLOSED_ENUM = &IdentTable.get("CF_CLOSED_ENUM");
    kw_CF_ENUM = &IdentTable.get("CF_ENUM");
    kw_CF_OPTIONS = &IdentTable.get("CF_OPTIONS");
    kw_NS_CLOSED_ENUM = &IdentTable.get("NS_CLOSED_ENUM");
    kw_NS_ENUM = &IdentTable.get("NS_ENUM");
    kw_NS_ERROR_ENUM = &IdentTable.get("NS_ERROR_ENUM");
    kw_NS_OPTIONS = &IdentTable.get("NS_OPTIONS");

    kw_as = &IdentTable.get("as");
    kw_async = &IdentTable.get("async");
    kw_await = &IdentTable.get("await");
    kw_declare = &IdentTable.get("declare");
    kw_finally = &IdentTable.get("finally");
    kw_from = &IdentTable.get("from");
    kw_function = &IdentTable.get("function");
    kw_get = &IdentTable.get("get");
    kw_import = &IdentTable.get("import");
    kw_infer = &IdentTable.get("infer");
    kw_is = &IdentTable.get("is");
    kw_let = &IdentTable.get("let");
    kw_module = &IdentTable.get("module");
    kw_readonly = &IdentTable.get("readonly");
    kw_set = &IdentTable.get("set");
    kw_type = &IdentTable.get("type");
    kw_typeof = &IdentTable.get("typeof");
    kw_var = &IdentTable.get("var");
    kw_yield = &IdentTable.get("yield");

    kw_abstract = &IdentTable.get("abstract");
    kw_assert = &IdentTable.get("assert");
    kw_extends = &IdentTable.get("extends");
    kw_implements = &IdentTable.get("implements");
    kw_instanceof = &IdentTable.get("instanceof");
    kw_interface = &IdentTable.get("interface");
    kw_native = &IdentTable.get("native");
    kw_package = &IdentTable.get("package");
    kw_synchronized = &IdentTable.get("synchronized");
    kw_throws = &IdentTable.get("throws");
    kw___except = &IdentTable.get("__except");
    kw___has_include = &IdentTable.get("__has_include");
    kw___has_include_next = &IdentTable.get("__has_include_next");

    kw_mark = &IdentTable.get("mark");
    kw_region = &IdentTable.get("region");

    kw_extend = &IdentTable.get("extend");
    kw_option = &IdentTable.get("option");
    kw_optional = &IdentTable.get("optional");
    kw_repeated = &IdentTable.get("repeated");
    kw_required = &IdentTable.get("required");
    kw_returns = &IdentTable.get("returns");

    kw_signals = &IdentTable.get("signals");
    kw_qsignals = &IdentTable.get("Q_SIGNALS");
    kw_slots = &IdentTable.get("slots");
    kw_qslots = &IdentTable.get("Q_SLOTS");

    // For internal clang-format use.
    kw_internal_ident_after_define =
        &IdentTable.get("__CLANG_FORMAT_INTERNAL_IDENT_AFTER_DEFINE__");

    // C# keywords
    kw_dollar = &IdentTable.get("dollar");
    kw_base = &IdentTable.get("base");
    kw_byte = &IdentTable.get("byte");
    kw_checked = &IdentTable.get("checked");
    kw_decimal = &IdentTable.get("decimal");
    kw_delegate = &IdentTable.get("delegate");
    kw_event = &IdentTable.get("event");
    kw_fixed = &IdentTable.get("fixed");
    kw_foreach = &IdentTable.get("foreach");
    kw_init = &IdentTable.get("init");
    kw_implicit = &IdentTable.get("implicit");
    kw_internal = &IdentTable.get("internal");
    kw_lock = &IdentTable.get("lock");
    kw_null = &IdentTable.get("null");
    kw_object = &IdentTable.get("object");
    kw_out = &IdentTable.get("out");
    kw_params = &IdentTable.get("params");
    kw_ref = &IdentTable.get("ref");
    kw_string = &IdentTable.get("string");
    kw_stackalloc = &IdentTable.get("stackalloc");
    kw_sbyte = &IdentTable.get("sbyte");
    kw_sealed = &IdentTable.get("sealed");
    kw_uint = &IdentTable.get("uint");
    kw_ulong = &IdentTable.get("ulong");
    kw_unchecked = &IdentTable.get("unchecked");
    kw_unsafe = &IdentTable.get("unsafe");
    kw_ushort = &IdentTable.get("ushort");
    kw_when = &IdentTable.get("when");
    kw_where = &IdentTable.get("where");

    // Verilog keywords
    kw_always = &IdentTable.get("always");
    kw_always_comb = &IdentTable.get("always_comb");
    kw_always_ff = &IdentTable.get("always_ff");
    kw_always_latch = &IdentTable.get("always_latch");
    kw_assign = &IdentTable.get("assign");
    kw_assume = &IdentTable.get("assume");
    kw_automatic = &IdentTable.get("automatic");
    kw_before = &IdentTable.get("before");
    kw_begin = &IdentTable.get("begin");
    kw_begin_keywords = &IdentTable.get("begin_keywords");
    kw_bins = &IdentTable.get("bins");
    kw_binsof = &IdentTable.get("binsof");
    kw_casex = &IdentTable.get("casex");
    kw_casez = &IdentTable.get("casez");
    kw_celldefine = &IdentTable.get("celldefine");
    kw_checker = &IdentTable.get("checker");
    kw_clocking = &IdentTable.get("clocking");
    kw_constraint = &IdentTable.get("constraint");
    kw_cover = &IdentTable.get("cover");
    kw_covergroup = &IdentTable.get("covergroup");
    kw_coverpoint = &IdentTable.get("coverpoint");
    kw_default_decay_time = &IdentTable.get("default_decay_time");
    kw_default_nettype = &IdentTable.get("default_nettype");
    kw_default_trireg_strength = &IdentTable.get("default_trireg_strength");
    kw_delay_mode_distributed = &IdentTable.get("delay_mode_distributed");
    kw_delay_mode_path = &IdentTable.get("delay_mode_path");
    kw_delay_mode_unit = &IdentTable.get("delay_mode_unit");
    kw_delay_mode_zero = &IdentTable.get("delay_mode_zero");
    kw_disable = &IdentTable.get("disable");
    kw_dist = &IdentTable.get("dist");
    kw_edge = &IdentTable.get("edge");
    kw_elsif = &IdentTable.get("elsif");
    kw_end = &IdentTable.get("end");
    kw_end_keywords = &IdentTable.get("end_keywords");
    kw_endcase = &IdentTable.get("endcase");
    kw_endcelldefine = &IdentTable.get("endcelldefine");
    kw_endchecker = &IdentTable.get("endchecker");
    kw_endclass = &IdentTable.get("endclass");
    kw_endclocking = &IdentTable.get("endclocking");
    kw_endfunction = &IdentTable.get("endfunction");
    kw_endgenerate = &IdentTable.get("endgenerate");
    kw_endgroup = &IdentTable.get("endgroup");
    kw_endinterface = &IdentTable.get("endinterface");
    kw_endmodule = &IdentTable.get("endmodule");
    kw_endpackage = &IdentTable.get("endpackage");
    kw_endprimitive = &IdentTable.get("endprimitive");
    kw_endprogram = &IdentTable.get("endprogram");
    kw_endproperty = &IdentTable.get("endproperty");
    kw_endsequence = &IdentTable.get("endsequence");
    kw_endspecify = &IdentTable.get("endspecify");
    kw_endtable = &IdentTable.get("endtable");
    kw_endtask = &IdentTable.get("endtask");
    kw_forever = &IdentTable.get("forever");
    kw_fork = &IdentTable.get("fork");
    kw_generate = &IdentTable.get("generate");
    kw_highz0 = &IdentTable.get("highz0");
    kw_highz1 = &IdentTable.get("highz1");
    kw_iff = &IdentTable.get("iff");
    kw_ifnone = &IdentTable.get("ifnone");
    kw_ignore_bins = &IdentTable.get("ignore_bins");
    kw_illegal_bins = &IdentTable.get("illegal_bins");
    kw_initial = &IdentTable.get("initial");
    kw_inout = &IdentTable.get("inout");
    kw_input = &IdentTable.get("input");
    kw_inside = &IdentTable.get("inside");
    kw_interconnect = &IdentTable.get("interconnect");
    kw_intersect = &IdentTable.get("intersect");
    kw_join = &IdentTable.get("join");
    kw_join_any = &IdentTable.get("join_any");
    kw_join_none = &IdentTable.get("join_none");
    kw_large = &IdentTable.get("large");
    kw_local = &IdentTable.get("local");
    kw_localparam = &IdentTable.get("localparam");
    kw_macromodule = &IdentTable.get("macromodule");
    kw_matches = &IdentTable.get("matches");
    kw_medium = &IdentTable.get("medium");
    kw_negedge = &IdentTable.get("negedge");
    kw_nounconnected_drive = &IdentTable.get("nounconnected_drive");
    kw_output = &IdentTable.get("output");
    kw_packed = &IdentTable.get("packed");
    kw_parameter = &IdentTable.get("parameter");
    kw_posedge = &IdentTable.get("posedge");
    kw_primitive = &IdentTable.get("primitive");
    kw_priority = &IdentTable.get("priority");
    kw_program = &IdentTable.get("program");
    kw_property = &IdentTable.get("property");
    kw_pull0 = &IdentTable.get("pull0");
    kw_pull1 = &IdentTable.get("pull1");
    kw_pure = &IdentTable.get("pure");
    kw_rand = &IdentTable.get("rand");
    kw_randc = &IdentTable.get("randc");
    kw_randcase = &IdentTable.get("randcase");
    kw_randsequence = &IdentTable.get("randsequence");
    kw_repeat = &IdentTable.get("repeat");
    kw_resetall = &IdentTable.get("resetall");
    kw_sample = &IdentTable.get("sample");
    kw_scalared = &IdentTable.get("scalared");
    kw_sequence = &IdentTable.get("sequence");
    kw_small = &IdentTable.get("small");
    kw_soft = &IdentTable.get("soft");
    kw_solve = &IdentTable.get("solve");
    kw_specify = &IdentTable.get("specify");
    kw_specparam = &IdentTable.get("specparam");
    kw_strong0 = &IdentTable.get("strong0");
    kw_strong1 = &IdentTable.get("strong1");
    kw_supply0 = &IdentTable.get("supply0");
    kw_supply1 = &IdentTable.get("supply1");
    kw_table = &IdentTable.get("table");
    kw_tagged = &IdentTable.get("tagged");
    kw_task = &IdentTable.get("task");
    kw_timescale = &IdentTable.get("timescale");
    kw_tri = &IdentTable.get("tri");
    kw_tri0 = &IdentTable.get("tri0");
    kw_tri1 = &IdentTable.get("tri1");
    kw_triand = &IdentTable.get("triand");
    kw_trior = &IdentTable.get("trior");
    kw_trireg = &IdentTable.get("trireg");
    kw_unconnected_drive = &IdentTable.get("unconnected_drive");
    kw_undefineall = &IdentTable.get("undefineall");
    kw_unique = &IdentTable.get("unique");
    kw_unique0 = &IdentTable.get("unique0");
    kw_uwire = &IdentTable.get("uwire");
    kw_vectored = &IdentTable.get("vectored");
    kw_wand = &IdentTable.get("wand");
    kw_weak0 = &IdentTable.get("weak0");
    kw_weak1 = &IdentTable.get("weak1");
    kw_wildcard = &IdentTable.get("wildcard");
    kw_wire = &IdentTable.get("wire");
    kw_with = &IdentTable.get("with");
    kw_wor = &IdentTable.get("wor");

    // Symbols that are treated as keywords.
    kw_verilogHash = &IdentTable.get("#");
    kw_verilogHashHash = &IdentTable.get("##");
    kw_apostrophe = &IdentTable.get("\'");

    // TableGen keywords
    kw_bit = &IdentTable.get("bit");
    kw_bits = &IdentTable.get("bits");
    kw_code = &IdentTable.get("code");
    kw_dag = &IdentTable.get("dag");
    kw_def = &IdentTable.get("def");
    kw_defm = &IdentTable.get("defm");
    kw_defset = &IdentTable.get("defset");
    kw_defvar = &IdentTable.get("defvar");
    kw_dump = &IdentTable.get("dump");
    kw_include = &IdentTable.get("include");
    kw_list = &IdentTable.get("list");
    kw_multiclass = &IdentTable.get("multiclass");
    kw_then = &IdentTable.get("then");

    // Keep this at the end of the constructor to make sure everything here
    // is
    // already initialized.
    JsExtraKeywords = std::unordered_set<IdentifierInfo *>(
        {kw_as, kw_async, kw_await, kw_declare, kw_finally, kw_from,
         kw_function, kw_get, kw_import, kw_is, kw_let, kw_module, kw_override,
         kw_readonly, kw_set, kw_type, kw_typeof, kw_var, kw_yield,
         // Keywords from the Java section.
         kw_abstract, kw_extends, kw_implements, kw_instanceof, kw_interface});

    CSharpExtraKeywords = std::unordered_set<IdentifierInfo *>(
        {kw_base, kw_byte, kw_checked, kw_decimal, kw_delegate, kw_event,
         kw_fixed, kw_foreach, kw_implicit, kw_in, kw_init, kw_interface,
         kw_internal, kw_is, kw_lock, kw_null, kw_object, kw_out, kw_override,
         kw_params, kw_readonly, kw_ref, kw_string, kw_stackalloc, kw_sbyte,
         kw_sealed, kw_uint, kw_ulong, kw_unchecked, kw_unsafe, kw_ushort,
         kw_when, kw_where,
         // Keywords from the JavaScript section.
         kw_as, kw_async, kw_await, kw_declare, kw_finally, kw_from,
         kw_function, kw_get, kw_import, kw_is, kw_let, kw_module, kw_readonly,
         kw_set, kw_type, kw_typeof, kw_var, kw_yield,
         // Keywords from the Java section.
         kw_abstract, kw_extends, kw_implements, kw_instanceof, kw_interface});

    // Some keywords are not included here because they don't need special
    // treatment like `showcancelled` or they should be treated as identifiers
    // like `int` and `logic`.
    VerilogExtraKeywords = std::unordered_set<IdentifierInfo *>(
        {kw_always,       kw_always_comb,
         kw_always_ff,    kw_always_latch,
         kw_assert,       kw_assign,
         kw_assume,       kw_automatic,
         kw_before,       kw_begin,
         kw_bins,         kw_binsof,
         kw_casex,        kw_casez,
         kw_celldefine,   kw_checker,
         kw_clocking,     kw_constraint,
         kw_cover,        kw_covergroup,
         kw_coverpoint,   kw_disable,
         kw_dist,         kw_edge,
         kw_end,          kw_endcase,
         kw_endchecker,   kw_endclass,
         kw_endclocking,  kw_endfunction,
         kw_endgenerate,  kw_endgroup,
         kw_endinterface, kw_endmodule,
         kw_endpackage,   kw_endprimitive,
         kw_endprogram,   kw_endproperty,
         kw_endsequence,  kw_endspecify,
         kw_endtable,     kw_endtask,
         kw_extends,      kw_final,
         kw_foreach,      kw_forever,
         kw_fork,         kw_function,
         kw_generate,     kw_highz0,
         kw_highz1,       kw_iff,
         kw_ifnone,       kw_ignore_bins,
         kw_illegal_bins, kw_implements,
         kw_import,       kw_initial,
         kw_inout,        kw_input,
         kw_inside,       kw_interconnect,
         kw_interface,    kw_intersect,
         kw_join,         kw_join_any,
         kw_join_none,    kw_large,
         kw_let,          kw_local,
         kw_localparam,   kw_macromodule,
         kw_matches,      kw_medium,
         kw_negedge,      kw_output,
         kw_package,      kw_packed,
         kw_parameter,    kw_posedge,
         kw_primitive,    kw_priority,
         kw_program,      kw_property,
         kw_pull0,        kw_pull1,
         kw_pure,         kw_rand,
         kw_randc,        kw_randcase,
         kw_randsequence, kw_ref,
         kw_repeat,       kw_sample,
         kw_scalared,     kw_sequence,
         kw_small,        kw_soft,
         kw_solve,        kw_specify,
         kw_specparam,    kw_strong0,
         kw_strong1,      kw_supply0,
         kw_supply1,      kw_table,
         kw_tagged,       kw_task,
         kw_tri,          kw_tri0,
         kw_tri1,         kw_triand,
         kw_trior,        kw_trireg,
         kw_unique,       kw_unique0,
         kw_uwire,        kw_var,
         kw_vectored,     kw_wand,
         kw_weak0,        kw_weak1,
         kw_wildcard,     kw_wire,
         kw_with,         kw_wor,
         kw_verilogHash,  kw_verilogHashHash});

    TableGenExtraKeywords = std::unordered_set<IdentifierInfo *>({
        kw_assert,
        kw_bit,
        kw_bits,
        kw_code,
        kw_dag,
        kw_def,
        kw_defm,
        kw_defset,
        kw_defvar,
        kw_dump,
        kw_foreach,
        kw_in,
        kw_include,
        kw_let,
        kw_list,
        kw_multiclass,
        kw_string,
        kw_then,
    });
  }

  // Context sensitive keywords.
  IdentifierInfo *kw_final;
  IdentifierInfo *kw_override;
  IdentifierInfo *kw_in;
  IdentifierInfo *kw_of;
  IdentifierInfo *kw_CF_CLOSED_ENUM;
  IdentifierInfo *kw_CF_ENUM;
  IdentifierInfo *kw_CF_OPTIONS;
  IdentifierInfo *kw_NS_CLOSED_ENUM;
  IdentifierInfo *kw_NS_ENUM;
  IdentifierInfo *kw_NS_ERROR_ENUM;
  IdentifierInfo *kw_NS_OPTIONS;
  IdentifierInfo *kw___except;
  IdentifierInfo *kw___has_include;
  IdentifierInfo *kw___has_include_next;

  // JavaScript keywords.
  IdentifierInfo *kw_as;
  IdentifierInfo *kw_async;
  IdentifierInfo *kw_await;
  IdentifierInfo *kw_declare;
  IdentifierInfo *kw_finally;
  IdentifierInfo *kw_from;
  IdentifierInfo *kw_function;
  IdentifierInfo *kw_get;
  IdentifierInfo *kw_import;
  IdentifierInfo *kw_infer;
  IdentifierInfo *kw_is;
  IdentifierInfo *kw_let;
  IdentifierInfo *kw_module;
  IdentifierInfo *kw_readonly;
  IdentifierInfo *kw_set;
  IdentifierInfo *kw_type;
  IdentifierInfo *kw_typeof;
  IdentifierInfo *kw_var;
  IdentifierInfo *kw_yield;

  // Java keywords.
  IdentifierInfo *kw_abstract;
  IdentifierInfo *kw_assert;
  IdentifierInfo *kw_extends;
  IdentifierInfo *kw_implements;
  IdentifierInfo *kw_instanceof;
  IdentifierInfo *kw_interface;
  IdentifierInfo *kw_native;
  IdentifierInfo *kw_package;
  IdentifierInfo *kw_synchronized;
  IdentifierInfo *kw_throws;

  // Pragma keywords.
  IdentifierInfo *kw_mark;
  IdentifierInfo *kw_region;

  // Proto keywords.
  IdentifierInfo *kw_extend;
  IdentifierInfo *kw_option;
  IdentifierInfo *kw_optional;
  IdentifierInfo *kw_repeated;
  IdentifierInfo *kw_required;
  IdentifierInfo *kw_returns;

  // QT keywords.
  IdentifierInfo *kw_signals;
  IdentifierInfo *kw_qsignals;
  IdentifierInfo *kw_slots;
  IdentifierInfo *kw_qslots;

  // For internal use by clang-format.
  IdentifierInfo *kw_internal_ident_after_define;

  // C# keywords
  IdentifierInfo *kw_dollar;
  IdentifierInfo *kw_base;
  IdentifierInfo *kw_byte;
  IdentifierInfo *kw_checked;
  IdentifierInfo *kw_decimal;
  IdentifierInfo *kw_delegate;
  IdentifierInfo *kw_event;
  IdentifierInfo *kw_fixed;
  IdentifierInfo *kw_foreach;
  IdentifierInfo *kw_implicit;
  IdentifierInfo *kw_init;
  IdentifierInfo *kw_internal;

  IdentifierInfo *kw_lock;
  IdentifierInfo *kw_null;
  IdentifierInfo *kw_object;
  IdentifierInfo *kw_out;

  IdentifierInfo *kw_params;

  IdentifierInfo *kw_ref;
  IdentifierInfo *kw_string;
  IdentifierInfo *kw_stackalloc;
  IdentifierInfo *kw_sbyte;
  IdentifierInfo *kw_sealed;
  IdentifierInfo *kw_uint;
  IdentifierInfo *kw_ulong;
  IdentifierInfo *kw_unchecked;
  IdentifierInfo *kw_unsafe;
  IdentifierInfo *kw_ushort;
  IdentifierInfo *kw_when;
  IdentifierInfo *kw_where;

  // Verilog keywords
  IdentifierInfo *kw_always;
  IdentifierInfo *kw_always_comb;
  IdentifierInfo *kw_always_ff;
  IdentifierInfo *kw_always_latch;
  IdentifierInfo *kw_assign;
  IdentifierInfo *kw_assume;
  IdentifierInfo *kw_automatic;
  IdentifierInfo *kw_before;
  IdentifierInfo *kw_begin;
  IdentifierInfo *kw_begin_keywords;
  IdentifierInfo *kw_bins;
  IdentifierInfo *kw_binsof;
  IdentifierInfo *kw_casex;
  IdentifierInfo *kw_casez;
  IdentifierInfo *kw_celldefine;
  IdentifierInfo *kw_checker;
  IdentifierInfo *kw_clocking;
  IdentifierInfo *kw_constraint;
  IdentifierInfo *kw_cover;
  IdentifierInfo *kw_covergroup;
  IdentifierInfo *kw_coverpoint;
  IdentifierInfo *kw_default_decay_time;
  IdentifierInfo *kw_default_nettype;
  IdentifierInfo *kw_default_trireg_strength;
  IdentifierInfo *kw_delay_mode_distributed;
  IdentifierInfo *kw_delay_mode_path;
  IdentifierInfo *kw_delay_mode_unit;
  IdentifierInfo *kw_delay_mode_zero;
  IdentifierInfo *kw_disable;
  IdentifierInfo *kw_dist;
  IdentifierInfo *kw_elsif;
  IdentifierInfo *kw_edge;
  IdentifierInfo *kw_end;
  IdentifierInfo *kw_end_keywords;
  IdentifierInfo *kw_endcase;
  IdentifierInfo *kw_endcelldefine;
  IdentifierInfo *kw_endchecker;
  IdentifierInfo *kw_endclass;
  IdentifierInfo *kw_endclocking;
  IdentifierInfo *kw_endfunction;
  IdentifierInfo *kw_endgenerate;
  IdentifierInfo *kw_endgroup;
  IdentifierInfo *kw_endinterface;
  IdentifierInfo *kw_endmodule;
  IdentifierInfo *kw_endpackage;
  IdentifierInfo *kw_endprimitive;
  IdentifierInfo *kw_endprogram;
  IdentifierInfo *kw_endproperty;
  IdentifierInfo *kw_endsequence;
  IdentifierInfo *kw_endspecify;
  IdentifierInfo *kw_endtable;
  IdentifierInfo *kw_endtask;
  IdentifierInfo *kw_forever;
  IdentifierInfo *kw_fork;
  IdentifierInfo *kw_generate;
  IdentifierInfo *kw_highz0;
  IdentifierInfo *kw_highz1;
  IdentifierInfo *kw_iff;
  IdentifierInfo *kw_ifnone;
  IdentifierInfo *kw_ignore_bins;
  IdentifierInfo *kw_illegal_bins;
  IdentifierInfo *kw_initial;
  IdentifierInfo *kw_inout;
  IdentifierInfo *kw_input;
  IdentifierInfo *kw_inside;
  IdentifierInfo *kw_interconnect;
  IdentifierInfo *kw_intersect;
  IdentifierInfo *kw_join;
  IdentifierInfo *kw_join_any;
  IdentifierInfo *kw_join_none;
  IdentifierInfo *kw_large;
  IdentifierInfo *kw_local;
  IdentifierInfo *kw_localparam;
  IdentifierInfo *kw_macromodule;
  IdentifierInfo *kw_matches;
  IdentifierInfo *kw_medium;
  IdentifierInfo *kw_negedge;
  IdentifierInfo *kw_nounconnected_drive;
  IdentifierInfo *kw_output;
  IdentifierInfo *kw_packed;
  IdentifierInfo *kw_parameter;
  IdentifierInfo *kw_posedge;
  IdentifierInfo *kw_primitive;
  IdentifierInfo *kw_priority;
  IdentifierInfo *kw_program;
  IdentifierInfo *kw_property;
  IdentifierInfo *kw_pull0;
  IdentifierInfo *kw_pull1;
  IdentifierInfo *kw_pure;
  IdentifierInfo *kw_rand;
  IdentifierInfo *kw_randc;
  IdentifierInfo *kw_randcase;
  IdentifierInfo *kw_randsequence;
  IdentifierInfo *kw_repeat;
  IdentifierInfo *kw_resetall;
  IdentifierInfo *kw_sample;
  IdentifierInfo *kw_scalared;
  IdentifierInfo *kw_sequence;
  IdentifierInfo *kw_small;
  IdentifierInfo *kw_soft;
  IdentifierInfo *kw_solve;
  IdentifierInfo *kw_specify;
  IdentifierInfo *kw_specparam;
  IdentifierInfo *kw_strong0;
  IdentifierInfo *kw_strong1;
  IdentifierInfo *kw_supply0;
  IdentifierInfo *kw_supply1;
  IdentifierInfo *kw_table;
  IdentifierInfo *kw_tagged;
  IdentifierInfo *kw_task;
  IdentifierInfo *kw_timescale;
  IdentifierInfo *kw_tri0;
  IdentifierInfo *kw_tri1;
  IdentifierInfo *kw_tri;
  IdentifierInfo *kw_triand;
  IdentifierInfo *kw_trior;
  IdentifierInfo *kw_trireg;
  IdentifierInfo *kw_unconnected_drive;
  IdentifierInfo *kw_undefineall;
  IdentifierInfo *kw_unique;
  IdentifierInfo *kw_unique0;
  IdentifierInfo *kw_uwire;
  IdentifierInfo *kw_vectored;
  IdentifierInfo *kw_wand;
  IdentifierInfo *kw_weak0;
  IdentifierInfo *kw_weak1;
  IdentifierInfo *kw_wildcard;
  IdentifierInfo *kw_wire;
  IdentifierInfo *kw_with;
  IdentifierInfo *kw_wor;

  // Workaround for hashes and backticks in Verilog.
  IdentifierInfo *kw_verilogHash;
  IdentifierInfo *kw_verilogHashHash;

  // Symbols in Verilog that don't exist in C++.
  IdentifierInfo *kw_apostrophe;

  // TableGen keywords
  IdentifierInfo *kw_bit;
  IdentifierInfo *kw_bits;
  IdentifierInfo *kw_code;
  IdentifierInfo *kw_dag;
  IdentifierInfo *kw_def;
  IdentifierInfo *kw_defm;
  IdentifierInfo *kw_defset;
  IdentifierInfo *kw_defvar;
  IdentifierInfo *kw_dump;
  IdentifierInfo *kw_include;
  IdentifierInfo *kw_list;
  IdentifierInfo *kw_multiclass;
  IdentifierInfo *kw_then;

  /// Returns \c true if \p Tok is a keyword or an identifier.
  bool isWordLike(const FormatToken &Tok, bool IsVerilog = true) const {
    // getIdentifierinfo returns non-null for keywords as well as identifiers.
    return Tok.Tok.getIdentifierInfo() &&
           (!IsVerilog || !isVerilogKeywordSymbol(Tok));
  }

  /// Returns \c true if \p Tok is a true JavaScript identifier, returns
  /// \c false if it is a keyword or a pseudo keyword.
  /// If \c AcceptIdentifierName is true, returns true not only for keywords,
  // but also for IdentifierName tokens (aka pseudo-keywords), such as
  // ``yield``.
  bool isJavaScriptIdentifier(const FormatToken &Tok,
                              bool AcceptIdentifierName = true) const {
    // Based on the list of JavaScript & TypeScript keywords here:
    // https://github.com/microsoft/TypeScript/blob/main/src/compiler/scanner.ts#L74
    if (Tok.isAccessSpecifierKeyword())
      return false;
    switch (Tok.Tok.getKind()) {
    case tok::kw_break:
    case tok::kw_case:
    case tok::kw_catch:
    case tok::kw_class:
    case tok::kw_continue:
    case tok::kw_const:
    case tok::kw_default:
    case tok::kw_delete:
    case tok::kw_do:
    case tok::kw_else:
    case tok::kw_enum:
    case tok::kw_export:
    case tok::kw_false:
    case tok::kw_for:
    case tok::kw_if:
    case tok::kw_import:
    case tok::kw_module:
    case tok::kw_new:
    case tok::kw_return:
    case tok::kw_static:
    case tok::kw_switch:
    case tok::kw_this:
    case tok::kw_throw:
    case tok::kw_true:
    case tok::kw_try:
    case tok::kw_typeof:
    case tok::kw_void:
    case tok::kw_while:
      // These are JS keywords that are lexed by LLVM/clang as keywords.
      return false;
    case tok::identifier: {
      // For identifiers, make sure they are true identifiers, excluding the
      // JavaScript pseudo-keywords (not lexed by LLVM/clang as keywords).
      bool IsPseudoKeyword =
          JsExtraKeywords.find(Tok.Tok.getIdentifierInfo()) !=
          JsExtraKeywords.end();
      return AcceptIdentifierName || !IsPseudoKeyword;
    }
    default:
      // Other keywords are handled in the switch below, to avoid problems due
      // to duplicate case labels when using the #include trick.
      break;
    }

    switch (Tok.Tok.getKind()) {
      // Handle C++ keywords not included above: these are all JS identifiers.
#define KEYWORD(X, Y) case tok::kw_##X:
#include "clang/Basic/TokenKinds.def"
      // #undef KEYWORD is not needed -- it's #undef-ed at the end of
      // TokenKinds.def
      return true;
    default:
      // All other tokens (punctuation etc) are not JS identifiers.
      return false;
    }
  }

  /// Returns \c true if \p Tok is a C# keyword, returns
  /// \c false if it is a anything else.
  bool isCSharpKeyword(const FormatToken &Tok) const {
    if (Tok.isAccessSpecifierKeyword())
      return true;
    switch (Tok.Tok.getKind()) {
    case tok::kw_bool:
    case tok::kw_break:
    case tok::kw_case:
    case tok::kw_catch:
    case tok::kw_char:
    case tok::kw_class:
    case tok::kw_const:
    case tok::kw_continue:
    case tok::kw_default:
    case tok::kw_do:
    case tok::kw_double:
    case tok::kw_else:
    case tok::kw_enum:
    case tok::kw_explicit:
    case tok::kw_extern:
    case tok::kw_false:
    case tok::kw_float:
    case tok::kw_for:
    case tok::kw_goto:
    case tok::kw_if:
    case tok::kw_int:
    case tok::kw_long:
    case tok::kw_namespace:
    case tok::kw_new:
    case tok::kw_operator:
    case tok::kw_return:
    case tok::kw_short:
    case tok::kw_sizeof:
    case tok::kw_static:
    case tok::kw_struct:
    case tok::kw_switch:
    case tok::kw_this:
    case tok::kw_throw:
    case tok::kw_true:
    case tok::kw_try:
    case tok::kw_typeof:
    case tok::kw_using:
    case tok::kw_virtual:
    case tok::kw_void:
    case tok::kw_volatile:
    case tok::kw_while:
      return true;
    default:
      return Tok.is(tok::identifier) &&
             CSharpExtraKeywords.find(Tok.Tok.getIdentifierInfo()) ==
                 CSharpExtraKeywords.end();
    }
  }

  bool isVerilogKeywordSymbol(const FormatToken &Tok) const {
    return Tok.isOneOf(kw_verilogHash, kw_verilogHashHash, kw_apostrophe);
  }

  bool isVerilogWordOperator(const FormatToken &Tok) const {
    return Tok.isOneOf(kw_before, kw_intersect, kw_dist, kw_iff, kw_inside,
                       kw_with);
  }

  bool isVerilogIdentifier(const FormatToken &Tok) const {
    switch (Tok.Tok.getKind()) {
    case tok::kw_case:
    case tok::kw_class:
    case tok::kw_const:
    case tok::kw_continue:
    case tok::kw_default:
    case tok::kw_do:
    case tok::kw_extern:
    case tok::kw_else:
    case tok::kw_enum:
    case tok::kw_for:
    case tok::kw_if:
    case tok::kw_restrict:
    case tok::kw_signed:
    case tok::kw_static:
    case tok::kw_struct:
    case tok::kw_typedef:
    case tok::kw_union:
    case tok::kw_unsigned:
    case tok::kw_virtual:
    case tok::kw_while:
      return false;
    case tok::identifier:
      return isWordLike(Tok) &&
             VerilogExtraKeywords.find(Tok.Tok.getIdentifierInfo()) ==
                 VerilogExtraKeywords.end();
    default:
      // getIdentifierInfo returns non-null for both identifiers and keywords.
      return Tok.Tok.getIdentifierInfo();
    }
  }

  /// Returns whether \p Tok is a Verilog preprocessor directive.  This is
  /// needed because macro expansions start with a backtick as well and they
  /// need to be treated differently.
  bool isVerilogPPDirective(const FormatToken &Tok) const {
    auto Info = Tok.Tok.getIdentifierInfo();
    if (!Info)
      return false;
    switch (Info->getPPKeywordID()) {
    case tok::pp_define:
    case tok::pp_else:
    case tok::pp_endif:
    case tok::pp_ifdef:
    case tok::pp_ifndef:
    case tok::pp_include:
    case tok::pp_line:
    case tok::pp_pragma:
    case tok::pp_undef:
      return true;
    default:
      return Tok.isOneOf(kw_begin_keywords, kw_celldefine,
                         kw_default_decay_time, kw_default_nettype,
                         kw_default_trireg_strength, kw_delay_mode_distributed,
                         kw_delay_mode_path, kw_delay_mode_unit,
                         kw_delay_mode_zero, kw_elsif, kw_end_keywords,
                         kw_endcelldefine, kw_nounconnected_drive, kw_resetall,
                         kw_timescale, kw_unconnected_drive, kw_undefineall);
    }
  }

  /// Returns whether \p Tok is a Verilog keyword that opens a block.
  bool isVerilogBegin(const FormatToken &Tok) const {
    // `table` is not included since it needs to be treated specially.
    return !Tok.endsSequence(kw_fork, kw_disable) &&
           Tok.isOneOf(kw_begin, kw_fork, kw_generate, kw_specify);
  }

  /// Returns whether \p Tok is a Verilog keyword that closes a block.
  bool isVerilogEnd(const FormatToken &Tok) const {
    return !Tok.endsSequence(kw_join, kw_rand) &&
           Tok.isOneOf(TT_MacroBlockEnd, kw_end, kw_endcase, kw_endclass,
                       kw_endclocking, kw_endchecker, kw_endfunction,
                       kw_endgenerate, kw_endgroup, kw_endinterface,
                       kw_endmodule, kw_endpackage, kw_endprimitive,
                       kw_endprogram, kw_endproperty, kw_endsequence,
                       kw_endspecify, kw_endtable, kw_endtask, kw_join,
                       kw_join_any, kw_join_none);
  }

  /// Returns whether \p Tok is a Verilog keyword that opens a module, etc.
  bool isVerilogHierarchy(const FormatToken &Tok) const {
    if (Tok.endsSequence(kw_function, kw_with))
      return false;
    if (Tok.is(kw_property)) {
      const FormatToken *Prev = Tok.getPreviousNonComment();
      return !(Prev &&
               Prev->isOneOf(tok::kw_restrict, kw_assert, kw_assume, kw_cover));
    }
    return Tok.isOneOf(tok::kw_case, tok::kw_class, kw_function, kw_module,
                       kw_interface, kw_package, kw_casex, kw_casez, kw_checker,
                       kw_clocking, kw_covergroup, kw_macromodule, kw_primitive,
                       kw_program, kw_property, kw_randcase, kw_randsequence,
                       kw_task);
  }

  bool isVerilogEndOfLabel(const FormatToken &Tok) const {
    const FormatToken *Next = Tok.getNextNonComment();
    // In Verilog the colon in a default label is optional.
    return Tok.is(TT_CaseLabelColon) ||
           (Tok.is(tok::kw_default) &&
            !(Next && Next->isOneOf(tok::colon, tok::semi, kw_clocking, kw_iff,
                                    kw_input, kw_output, kw_sequence)));
  }

  /// Returns whether \p Tok is a Verilog keyword that starts a
  /// structured procedure like 'always'.
  bool isVerilogStructuredProcedure(const FormatToken &Tok) const {
    return Tok.isOneOf(kw_always, kw_always_comb, kw_always_ff, kw_always_latch,
                       kw_final, kw_forever, kw_initial);
  }

  bool isVerilogQualifier(const FormatToken &Tok) const {
    switch (Tok.Tok.getKind()) {
    case tok::kw_extern:
    case tok::kw_signed:
    case tok::kw_static:
    case tok::kw_unsigned:
    case tok::kw_virtual:
      return true;
    case tok::identifier:
      return Tok.isOneOf(
          kw_let, kw_var, kw_ref, kw_automatic, kw_bins, kw_coverpoint,
          kw_ignore_bins, kw_illegal_bins, kw_inout, kw_input, kw_interconnect,
          kw_local, kw_localparam, kw_output, kw_parameter, kw_pure, kw_rand,
          kw_randc, kw_scalared, kw_specparam, kw_tri, kw_tri0, kw_tri1,
          kw_triand, kw_trior, kw_trireg, kw_uwire, kw_vectored, kw_wand,
          kw_wildcard, kw_wire, kw_wor);
    default:
      return false;
    }
  }

  bool isTableGenDefinition(const FormatToken &Tok) const {
    return Tok.isOneOf(kw_def, kw_defm, kw_defset, kw_defvar, kw_multiclass,
                       kw_let, tok::kw_class);
  }

  bool isTableGenKeyword(const FormatToken &Tok) const {
    switch (Tok.Tok.getKind()) {
    case tok::kw_class:
    case tok::kw_else:
    case tok::kw_false:
    case tok::kw_if:
    case tok::kw_int:
    case tok::kw_true:
      return true;
    default:
      return Tok.is(tok::identifier) &&
             TableGenExtraKeywords.find(Tok.Tok.getIdentifierInfo()) !=
                 TableGenExtraKeywords.end();
    }
  }

private:
  /// The JavaScript keywords beyond the C++ keyword set.
  std::unordered_set<IdentifierInfo *> JsExtraKeywords;

  /// The C# keywords beyond the C++ keyword set
  std::unordered_set<IdentifierInfo *> CSharpExtraKeywords;

  /// The Verilog keywords beyond the C++ keyword set.
  std::unordered_set<IdentifierInfo *> VerilogExtraKeywords;

  /// The TableGen keywords beyond the C++ keyword set.
  std::unordered_set<IdentifierInfo *> TableGenExtraKeywords;
};

inline bool isLineComment(const FormatToken &FormatTok) {
  return FormatTok.is(tok::comment) && !FormatTok.TokenText.starts_with("/*");
}

// Checks if \p FormatTok is a line comment that continues the line comment
// \p Previous. The original column of \p MinColumnToken is used to determine
// whether \p FormatTok is indented enough to the right to continue \p Previous.
inline bool continuesLineComment(const FormatToken &FormatTok,
                                 const FormatToken *Previous,
                                 const FormatToken *MinColumnToken) {
  if (!Previous || !MinColumnToken)
    return false;
  unsigned MinContinueColumn =
      MinColumnToken->OriginalColumn + (isLineComment(*MinColumnToken) ? 0 : 1);
  return isLineComment(FormatTok) && FormatTok.NewlinesBefore == 1 &&
         isLineComment(*Previous) &&
         FormatTok.OriginalColumn >= MinContinueColumn;
}

} // namespace format
} // namespace clang

#endif

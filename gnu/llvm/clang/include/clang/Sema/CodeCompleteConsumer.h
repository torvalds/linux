//===- CodeCompleteConsumer.h - Code Completion Interface -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the CodeCompleteConsumer class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_CODECOMPLETECONSUMER_H
#define LLVM_CLANG_SEMA_CODECOMPLETECONSUMER_H

#include "clang-c/Index.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Sema/CodeCompleteOptions.h"
#include "clang/Sema/DeclSpec.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace clang {

class ASTContext;
class Decl;
class DeclContext;
class FunctionDecl;
class FunctionTemplateDecl;
class IdentifierInfo;
class LangOptions;
class NamedDecl;
class NestedNameSpecifier;
class Preprocessor;
class RawComment;
class Sema;
class UsingShadowDecl;

/// Default priority values for code-completion results based
/// on their kind.
enum {
  /// Priority for the next initialization in a constructor initializer
  /// list.
  CCP_NextInitializer = 7,

  /// Priority for an enumeration constant inside a switch whose
  /// condition is of the enumeration type.
  CCP_EnumInCase = 7,

  /// Priority for a send-to-super completion.
  CCP_SuperCompletion = 20,

  /// Priority for a declaration that is in the local scope.
  CCP_LocalDeclaration = 34,

  /// Priority for a member declaration found from the current
  /// method or member function.
  CCP_MemberDeclaration = 35,

  /// Priority for a language keyword (that isn't any of the other
  /// categories).
  CCP_Keyword = 40,

  /// Priority for a code pattern.
  CCP_CodePattern = 40,

  /// Priority for a non-type declaration.
  CCP_Declaration = 50,

  /// Priority for a type.
  CCP_Type = CCP_Declaration,

  /// Priority for a constant value (e.g., enumerator).
  CCP_Constant = 65,

  /// Priority for a preprocessor macro.
  CCP_Macro = 70,

  /// Priority for a nested-name-specifier.
  CCP_NestedNameSpecifier = 75,

  /// Priority for a result that isn't likely to be what the user wants,
  /// but is included for completeness.
  CCP_Unlikely = 80,

  /// Priority for the Objective-C "_cmd" implicit parameter.
  CCP_ObjC_cmd = CCP_Unlikely
};

/// Priority value deltas that are added to code-completion results
/// based on the context of the result.
enum {
  /// The result is in a base class.
  CCD_InBaseClass = 2,

  /// The result is a C++ non-static member function whose qualifiers
  /// exactly match the object type on which the member function can be called.
  CCD_ObjectQualifierMatch = -1,

  /// The selector of the given message exactly matches the selector
  /// of the current method, which might imply that some kind of delegation
  /// is occurring.
  CCD_SelectorMatch = -3,

  /// Adjustment to the "bool" type in Objective-C, where the typedef
  /// "BOOL" is preferred.
  CCD_bool_in_ObjC = 1,

  /// Adjustment for KVC code pattern priorities when it doesn't look
  /// like the
  CCD_ProbablyNotObjCCollection = 15,

  /// An Objective-C method being used as a property.
  CCD_MethodAsProperty = 2,

  /// An Objective-C block property completed as a setter with a
  /// block placeholder.
  CCD_BlockPropertySetter = 3
};

/// Priority value factors by which we will divide or multiply the
/// priority of a code-completion result.
enum {
  /// Divide by this factor when a code-completion result's type exactly
  /// matches the type we expect.
  CCF_ExactTypeMatch = 4,

  /// Divide by this factor when a code-completion result's type is
  /// similar to the type we expect (e.g., both arithmetic types, both
  /// Objective-C object pointer types).
  CCF_SimilarTypeMatch = 2
};

/// A simplified classification of types used when determining
/// "similar" types for code completion.
enum SimplifiedTypeClass {
  STC_Arithmetic,
  STC_Array,
  STC_Block,
  STC_Function,
  STC_ObjectiveC,
  STC_Other,
  STC_Pointer,
  STC_Record,
  STC_Void
};

/// Determine the simplified type class of the given canonical type.
SimplifiedTypeClass getSimplifiedTypeClass(CanQualType T);

/// Determine the type that this declaration will have if it is used
/// as a type or in an expression.
QualType getDeclUsageType(ASTContext &C, const NamedDecl *ND);

/// Determine the priority to be given to a macro code completion result
/// with the given name.
///
/// \param MacroName The name of the macro.
///
/// \param LangOpts Options describing the current language dialect.
///
/// \param PreferredTypeIsPointer Whether the preferred type for the context
/// of this macro is a pointer type.
unsigned getMacroUsagePriority(StringRef MacroName,
                               const LangOptions &LangOpts,
                               bool PreferredTypeIsPointer = false);

/// Determine the libclang cursor kind associated with the given
/// declaration.
CXCursorKind getCursorKindForDecl(const Decl *D);

/// The context in which code completion occurred, so that the
/// code-completion consumer can process the results accordingly.
class CodeCompletionContext {
public:
  enum Kind {
    /// An unspecified code-completion context.
    CCC_Other,

    /// An unspecified code-completion context where we should also add
    /// macro completions.
    CCC_OtherWithMacros,

    /// Code completion occurred within a "top-level" completion context,
    /// e.g., at namespace or global scope.
    CCC_TopLevel,

    /// Code completion occurred within an Objective-C interface,
    /// protocol, or category interface.
    CCC_ObjCInterface,

    /// Code completion occurred within an Objective-C implementation
    /// or category implementation.
    CCC_ObjCImplementation,

    /// Code completion occurred within the instance variable list of
    /// an Objective-C interface, implementation, or category implementation.
    CCC_ObjCIvarList,

    /// Code completion occurred within a class, struct, or union.
    CCC_ClassStructUnion,

    /// Code completion occurred where a statement (or declaration) is
    /// expected in a function, method, or block.
    CCC_Statement,

    /// Code completion occurred where an expression is expected.
    CCC_Expression,

    /// Code completion occurred where an Objective-C message receiver
    /// is expected.
    CCC_ObjCMessageReceiver,

    /// Code completion occurred on the right-hand side of a member
    /// access expression using the dot operator.
    ///
    /// The results of this completion are the members of the type being
    /// accessed. The type itself is available via
    /// \c CodeCompletionContext::getType().
    CCC_DotMemberAccess,

    /// Code completion occurred on the right-hand side of a member
    /// access expression using the arrow operator.
    ///
    /// The results of this completion are the members of the type being
    /// accessed. The type itself is available via
    /// \c CodeCompletionContext::getType().
    CCC_ArrowMemberAccess,

    /// Code completion occurred on the right-hand side of an Objective-C
    /// property access expression.
    ///
    /// The results of this completion are the members of the type being
    /// accessed. The type itself is available via
    /// \c CodeCompletionContext::getType().
    CCC_ObjCPropertyAccess,

    /// Code completion occurred after the "enum" keyword, to indicate
    /// an enumeration name.
    CCC_EnumTag,

    /// Code completion occurred after the "union" keyword, to indicate
    /// a union name.
    CCC_UnionTag,

    /// Code completion occurred after the "struct" or "class" keyword,
    /// to indicate a struct or class name.
    CCC_ClassOrStructTag,

    /// Code completion occurred where a protocol name is expected.
    CCC_ObjCProtocolName,

    /// Code completion occurred where a namespace or namespace alias
    /// is expected.
    CCC_Namespace,

    /// Code completion occurred where a type name is expected.
    CCC_Type,

    /// Code completion occurred where a new name is expected.
    CCC_NewName,

    /// Code completion occurred where both a new name and an existing symbol is
    /// permissible.
    CCC_SymbolOrNewName,

    /// Code completion occurred where an existing name(such as type, function
    /// or variable) is expected.
    CCC_Symbol,

    /// Code completion occurred where an macro is being defined.
    CCC_MacroName,

    /// Code completion occurred where a macro name is expected
    /// (without any arguments, in the case of a function-like macro).
    CCC_MacroNameUse,

    /// Code completion occurred within a preprocessor expression.
    CCC_PreprocessorExpression,

    /// Code completion occurred where a preprocessor directive is
    /// expected.
    CCC_PreprocessorDirective,

    /// Code completion occurred in a context where natural language is
    /// expected, e.g., a comment or string literal.
    ///
    /// This context usually implies that no completions should be added,
    /// unless they come from an appropriate natural-language dictionary.
    CCC_NaturalLanguage,

    /// Code completion for a selector, as in an \@selector expression.
    CCC_SelectorName,

    /// Code completion within a type-qualifier list.
    CCC_TypeQualifiers,

    /// Code completion in a parenthesized expression, which means that
    /// we may also have types here in C and Objective-C (as well as in C++).
    CCC_ParenthesizedExpression,

    /// Code completion where an Objective-C instance message is
    /// expected.
    CCC_ObjCInstanceMessage,

    /// Code completion where an Objective-C class message is expected.
    CCC_ObjCClassMessage,

    /// Code completion where the name of an Objective-C class is
    /// expected.
    CCC_ObjCInterfaceName,

    /// Code completion where an Objective-C category name is expected.
    CCC_ObjCCategoryName,

    /// Code completion inside the filename part of a #include directive.
    CCC_IncludedFile,

    /// Code completion of an attribute name.
    CCC_Attribute,

    /// An unknown context, in which we are recovering from a parsing
    /// error and don't know which completions we should give.
    CCC_Recovery,

    /// Code completion in a @class forward declaration.
    CCC_ObjCClassForwardDecl,

    /// Code completion at a top level, i.e. in a namespace or global scope,
    /// but also in expression statements. This is because REPL inputs can be
    /// declarations or expression statements.
    CCC_TopLevelOrExpression,
  };

  using VisitedContextSet = llvm::SmallPtrSet<DeclContext *, 8>;

private:
  Kind CCKind;

  /// Indicates whether we are completing a name of a using declaration, e.g.
  ///     using ^;
  ///     using a::^;
  bool IsUsingDeclaration;

  /// The type that would prefer to see at this point (e.g., the type
  /// of an initializer or function parameter).
  QualType PreferredType;

  /// The type of the base object in a member access expression.
  QualType BaseType;

  /// The identifiers for Objective-C selector parts.
  ArrayRef<const IdentifierInfo *> SelIdents;

  /// The scope specifier that comes before the completion token e.g.
  /// "a::b::"
  std::optional<CXXScopeSpec> ScopeSpecifier;

  /// A set of declaration contexts visited by Sema when doing lookup for
  /// code completion.
  VisitedContextSet VisitedContexts;

public:
  /// Construct a new code-completion context of the given kind.
  CodeCompletionContext(Kind CCKind)
      : CCKind(CCKind), IsUsingDeclaration(false), SelIdents(std::nullopt) {}

  /// Construct a new code-completion context of the given kind.
  CodeCompletionContext(
      Kind CCKind, QualType T,
      ArrayRef<const IdentifierInfo *> SelIdents = std::nullopt)
      : CCKind(CCKind), IsUsingDeclaration(false), SelIdents(SelIdents) {
    if (CCKind == CCC_DotMemberAccess || CCKind == CCC_ArrowMemberAccess ||
        CCKind == CCC_ObjCPropertyAccess || CCKind == CCC_ObjCClassMessage ||
        CCKind == CCC_ObjCInstanceMessage)
      BaseType = T;
    else
      PreferredType = T;
  }

  bool isUsingDeclaration() const { return IsUsingDeclaration; }
  void setIsUsingDeclaration(bool V) { IsUsingDeclaration = V; }

  /// Retrieve the kind of code-completion context.
  Kind getKind() const { return CCKind; }

  /// Retrieve the type that this expression would prefer to have, e.g.,
  /// if the expression is a variable initializer or a function argument, the
  /// type of the corresponding variable or function parameter.
  QualType getPreferredType() const { return PreferredType; }
  void setPreferredType(QualType T) { PreferredType = T; }

  /// Retrieve the type of the base object in a member-access
  /// expression.
  QualType getBaseType() const { return BaseType; }

  /// Retrieve the Objective-C selector identifiers.
  ArrayRef<const IdentifierInfo *> getSelIdents() const { return SelIdents; }

  /// Determines whether we want C++ constructors as results within this
  /// context.
  bool wantConstructorResults() const;

  /// Sets the scope specifier that comes before the completion token.
  /// This is expected to be set in code completions on qualfied specifiers
  /// (e.g. "a::b::").
  void setCXXScopeSpecifier(CXXScopeSpec SS) {
    this->ScopeSpecifier = std::move(SS);
  }

  /// Adds a visited context.
  void addVisitedContext(DeclContext *Ctx) {
    VisitedContexts.insert(Ctx);
  }

  /// Retrieves all visited contexts.
  const VisitedContextSet &getVisitedContexts() const {
    return VisitedContexts;
  }

  std::optional<const CXXScopeSpec *> getCXXScopeSpecifier() {
    if (ScopeSpecifier)
      return &*ScopeSpecifier;
    return std::nullopt;
  }
};

/// Get string representation of \p Kind, useful for debugging.
llvm::StringRef getCompletionKindString(CodeCompletionContext::Kind Kind);

/// A "string" used to describe how code completion can
/// be performed for an entity.
///
/// A code completion string typically shows how a particular entity can be
/// used. For example, the code completion string for a function would show
/// the syntax to call it, including the parentheses, placeholders for the
/// arguments, etc.
class CodeCompletionString {
public:
  /// The different kinds of "chunks" that can occur within a code
  /// completion string.
  enum ChunkKind {
    /// The piece of text that the user is expected to type to
    /// match the code-completion string, typically a keyword or the name of a
    /// declarator or macro.
    CK_TypedText,

    /// A piece of text that should be placed in the buffer, e.g.,
    /// parentheses or a comma in a function call.
    CK_Text,

    /// A code completion string that is entirely optional. For example,
    /// an optional code completion string that describes the default arguments
    /// in a function call.
    CK_Optional,

    /// A string that acts as a placeholder for, e.g., a function
    /// call argument.
    CK_Placeholder,

    /// A piece of text that describes something about the result but
    /// should not be inserted into the buffer.
    CK_Informative,
    /// A piece of text that describes the type of an entity or, for
    /// functions and methods, the return type.
    CK_ResultType,

    /// A piece of text that describes the parameter that corresponds
    /// to the code-completion location within a function call, message send,
    /// macro invocation, etc.
    CK_CurrentParameter,

    /// A left parenthesis ('(').
    CK_LeftParen,

    /// A right parenthesis (')').
    CK_RightParen,

    /// A left bracket ('[').
    CK_LeftBracket,

    /// A right bracket (']').
    CK_RightBracket,

    /// A left brace ('{').
    CK_LeftBrace,

    /// A right brace ('}').
    CK_RightBrace,

    /// A left angle bracket ('<').
    CK_LeftAngle,

    /// A right angle bracket ('>').
    CK_RightAngle,

    /// A comma separator (',').
    CK_Comma,

    /// A colon (':').
    CK_Colon,

    /// A semicolon (';').
    CK_SemiColon,

    /// An '=' sign.
    CK_Equal,

    /// Horizontal whitespace (' ').
    CK_HorizontalSpace,

    /// Vertical whitespace ('\\n' or '\\r\\n', depending on the
    /// platform).
    CK_VerticalSpace
  };

  /// One piece of the code completion string.
  struct Chunk {
    /// The kind of data stored in this piece of the code completion
    /// string.
    ChunkKind Kind = CK_Text;

    union {
      /// The text string associated with a CK_Text, CK_Placeholder,
      /// CK_Informative, or CK_Comma chunk.
      /// The string is owned by the chunk and will be deallocated
      /// (with delete[]) when the chunk is destroyed.
      const char *Text;

      /// The code completion string associated with a CK_Optional chunk.
      /// The optional code completion string is owned by the chunk, and will
      /// be deallocated (with delete) when the chunk is destroyed.
      CodeCompletionString *Optional;
    };

    Chunk() : Text(nullptr) {}

    explicit Chunk(ChunkKind Kind, const char *Text = "");

    /// Create a new text chunk.
    static Chunk CreateText(const char *Text);

    /// Create a new optional chunk.
    static Chunk CreateOptional(CodeCompletionString *Optional);

    /// Create a new placeholder chunk.
    static Chunk CreatePlaceholder(const char *Placeholder);

    /// Create a new informative chunk.
    static Chunk CreateInformative(const char *Informative);

    /// Create a new result type chunk.
    static Chunk CreateResultType(const char *ResultType);

    /// Create a new current-parameter chunk.
    static Chunk CreateCurrentParameter(const char *CurrentParameter);
  };

private:
  friend class CodeCompletionBuilder;
  friend class CodeCompletionResult;

  /// The number of chunks stored in this string.
  unsigned NumChunks : 16;

  /// The number of annotations for this code-completion result.
  unsigned NumAnnotations : 16;

  /// The priority of this code-completion string.
  unsigned Priority : 16;

  /// The availability of this code-completion result.
  LLVM_PREFERRED_TYPE(CXAvailabilityKind)
  unsigned Availability : 2;

  /// The name of the parent context.
  StringRef ParentName;

  /// A brief documentation comment attached to the declaration of
  /// entity being completed by this result.
  const char *BriefComment;

  CodeCompletionString(const Chunk *Chunks, unsigned NumChunks,
                       unsigned Priority, CXAvailabilityKind Availability,
                       const char **Annotations, unsigned NumAnnotations,
                       StringRef ParentName,
                       const char *BriefComment);
  ~CodeCompletionString() = default;

public:
  CodeCompletionString(const CodeCompletionString &) = delete;
  CodeCompletionString &operator=(const CodeCompletionString &) = delete;

  using iterator = const Chunk *;

  iterator begin() const { return reinterpret_cast<const Chunk *>(this + 1); }
  iterator end() const { return begin() + NumChunks; }
  bool empty() const { return NumChunks == 0; }
  unsigned size() const { return NumChunks; }

  const Chunk &operator[](unsigned I) const {
    assert(I < size() && "Chunk index out-of-range");
    return begin()[I];
  }

  /// Returns the text in the first TypedText chunk.
  const char *getTypedText() const;

  /// Returns the combined text from all TypedText chunks.
  std::string getAllTypedText() const;

  /// Retrieve the priority of this code completion result.
  unsigned getPriority() const { return Priority; }

  /// Retrieve the availability of this code completion result.
  unsigned getAvailability() const { return Availability; }

  /// Retrieve the number of annotations for this code completion result.
  unsigned getAnnotationCount() const;

  /// Retrieve the annotation string specified by \c AnnotationNr.
  const char *getAnnotation(unsigned AnnotationNr) const;

  /// Retrieve the name of the parent context.
  StringRef getParentContextName() const {
    return ParentName;
  }

  const char *getBriefComment() const {
    return BriefComment;
  }

  /// Retrieve a string representation of the code completion string,
  /// which is mainly useful for debugging.
  std::string getAsString() const;
};

/// An allocator used specifically for the purpose of code completion.
class CodeCompletionAllocator : public llvm::BumpPtrAllocator {
public:
  /// Copy the given string into this allocator.
  const char *CopyString(const Twine &String);
};

/// Allocator for a cached set of global code completions.
class GlobalCodeCompletionAllocator : public CodeCompletionAllocator {};

class CodeCompletionTUInfo {
  llvm::DenseMap<const DeclContext *, StringRef> ParentNames;
  std::shared_ptr<GlobalCodeCompletionAllocator> AllocatorRef;

public:
  explicit CodeCompletionTUInfo(
      std::shared_ptr<GlobalCodeCompletionAllocator> Allocator)
      : AllocatorRef(std::move(Allocator)) {}

  std::shared_ptr<GlobalCodeCompletionAllocator> getAllocatorRef() const {
    return AllocatorRef;
  }

  CodeCompletionAllocator &getAllocator() const {
    assert(AllocatorRef);
    return *AllocatorRef;
  }

  StringRef getParentName(const DeclContext *DC);
};

} // namespace clang

namespace clang {

/// A builder class used to construct new code-completion strings.
class CodeCompletionBuilder {
public:
  using Chunk = CodeCompletionString::Chunk;

private:
  CodeCompletionAllocator &Allocator;
  CodeCompletionTUInfo &CCTUInfo;
  unsigned Priority = 0;
  CXAvailabilityKind Availability = CXAvailability_Available;
  StringRef ParentName;
  const char *BriefComment = nullptr;

  /// The chunks stored in this string.
  SmallVector<Chunk, 4> Chunks;

  SmallVector<const char *, 2> Annotations;

public:
  CodeCompletionBuilder(CodeCompletionAllocator &Allocator,
                        CodeCompletionTUInfo &CCTUInfo)
      : Allocator(Allocator), CCTUInfo(CCTUInfo) {}

  CodeCompletionBuilder(CodeCompletionAllocator &Allocator,
                        CodeCompletionTUInfo &CCTUInfo,
                        unsigned Priority, CXAvailabilityKind Availability)
      : Allocator(Allocator), CCTUInfo(CCTUInfo), Priority(Priority),
        Availability(Availability) {}

  /// Retrieve the allocator into which the code completion
  /// strings should be allocated.
  CodeCompletionAllocator &getAllocator() const { return Allocator; }

  CodeCompletionTUInfo &getCodeCompletionTUInfo() const { return CCTUInfo; }

  /// Take the resulting completion string.
  ///
  /// This operation can only be performed once.
  CodeCompletionString *TakeString();

  /// Add a new typed-text chunk.
  void AddTypedTextChunk(const char *Text);

  /// Add a new text chunk.
  void AddTextChunk(const char *Text);

  /// Add a new optional chunk.
  void AddOptionalChunk(CodeCompletionString *Optional);

  /// Add a new placeholder chunk.
  void AddPlaceholderChunk(const char *Placeholder);

  /// Add a new informative chunk.
  void AddInformativeChunk(const char *Text);

  /// Add a new result-type chunk.
  void AddResultTypeChunk(const char *ResultType);

  /// Add a new current-parameter chunk.
  void AddCurrentParameterChunk(const char *CurrentParameter);

  /// Add a new chunk.
  void AddChunk(CodeCompletionString::ChunkKind CK, const char *Text = "");

  void AddAnnotation(const char *A) { Annotations.push_back(A); }

  /// Add the parent context information to this code completion.
  void addParentContext(const DeclContext *DC);

  const char *getBriefComment() const { return BriefComment; }
  void addBriefComment(StringRef Comment);

  StringRef getParentName() const { return ParentName; }
};

/// Captures a result of code completion.
class CodeCompletionResult {
public:
  /// Describes the kind of result generated.
  enum ResultKind {
    /// Refers to a declaration.
    RK_Declaration = 0,

    /// Refers to a keyword or symbol.
    RK_Keyword,

    /// Refers to a macro.
    RK_Macro,

    /// Refers to a precomputed pattern.
    RK_Pattern
  };

  /// When Kind == RK_Declaration or RK_Pattern, the declaration we are
  /// referring to. In the latter case, the declaration might be NULL.
  const NamedDecl *Declaration = nullptr;

  union {
    /// When Kind == RK_Keyword, the string representing the keyword
    /// or symbol's spelling.
    const char *Keyword;

    /// When Kind == RK_Pattern, the code-completion string that
    /// describes the completion text to insert.
    CodeCompletionString *Pattern;

    /// When Kind == RK_Macro, the identifier that refers to a macro.
    const IdentifierInfo *Macro;
  };

  /// The priority of this particular code-completion result.
  unsigned Priority;

  /// Specifies which parameter (of a function, Objective-C method,
  /// macro, etc.) we should start with when formatting the result.
  unsigned StartParameter = 0;

  /// The kind of result stored here.
  ResultKind Kind;

  /// The cursor kind that describes this result.
  CXCursorKind CursorKind;

  /// The availability of this result.
  CXAvailabilityKind Availability = CXAvailability_Available;

  /// Fix-its that *must* be applied before inserting the text for the
  /// corresponding completion.
  ///
  /// By default, CodeCompletionBuilder only returns completions with empty
  /// fix-its. Extra completions with non-empty fix-its should be explicitly
  /// requested by setting CompletionOptions::IncludeFixIts.
  ///
  /// For the clients to be able to compute position of the cursor after
  /// applying fix-its, the following conditions are guaranteed to hold for
  /// RemoveRange of the stored fix-its:
  ///  - Ranges in the fix-its are guaranteed to never contain the completion
  ///  point (or identifier under completion point, if any) inside them, except
  ///  at the start or at the end of the range.
  ///  - If a fix-it range starts or ends with completion point (or starts or
  ///  ends after the identifier under completion point), it will contain at
  ///  least one character. It allows to unambiguously recompute completion
  ///  point after applying the fix-it.
  ///
  /// The intuition is that provided fix-its change code around the identifier
  /// we complete, but are not allowed to touch the identifier itself or the
  /// completion point. One example of completions with corrections are the ones
  /// replacing '.' with '->' and vice versa:
  ///
  /// std::unique_ptr<std::vector<int>> vec_ptr;
  /// In 'vec_ptr.^', one of the completions is 'push_back', it requires
  /// replacing '.' with '->'.
  /// In 'vec_ptr->^', one of the completions is 'release', it requires
  /// replacing '->' with '.'.
  std::vector<FixItHint> FixIts;

  /// Whether this result is hidden by another name.
  bool Hidden : 1;

  /// Whether this is a class member from base class.
  bool InBaseClass : 1;

  /// Whether this result was found via lookup into a base class.
  bool QualifierIsInformative : 1;

  /// Whether this declaration is the beginning of a
  /// nested-name-specifier and, therefore, should be followed by '::'.
  bool StartsNestedNameSpecifier : 1;

  /// Whether all parameters (of a function, Objective-C
  /// method, etc.) should be considered "informative".
  bool AllParametersAreInformative : 1;

  /// Whether we're completing a declaration of the given entity,
  /// rather than a use of that entity.
  bool DeclaringEntity : 1;

  /// When completing a function, whether it can be a call. This will usually be
  /// true, but we have some heuristics, e.g. when a pointer to a non-static
  /// member function is completed outside of that class' scope, it can never
  /// be a call.
  bool FunctionCanBeCall : 1;

  /// If the result should have a nested-name-specifier, this is it.
  /// When \c QualifierIsInformative, the nested-name-specifier is
  /// informative rather than required.
  NestedNameSpecifier *Qualifier = nullptr;

  /// If this Decl was unshadowed by using declaration, this can store a
  /// pointer to the UsingShadowDecl which was used in the unshadowing process.
  /// This information can be used to uprank CodeCompletionResults / which have
  /// corresponding `using decl::qualified::name;` nearby.
  const UsingShadowDecl *ShadowDecl = nullptr;

  /// If the result is RK_Macro, this can store the information about the macro
  /// definition. This should be set in most cases but can be missing when
  /// the macro has been undefined.
  const MacroInfo *MacroDefInfo = nullptr;

  /// Build a result that refers to a declaration.
  CodeCompletionResult(const NamedDecl *Declaration, unsigned Priority,
                       NestedNameSpecifier *Qualifier = nullptr,
                       bool QualifierIsInformative = false,
                       bool Accessible = true,
                       std::vector<FixItHint> FixIts = std::vector<FixItHint>())
      : Declaration(Declaration), Priority(Priority), Kind(RK_Declaration),
        FixIts(std::move(FixIts)), Hidden(false), InBaseClass(false),
        QualifierIsInformative(QualifierIsInformative),
        StartsNestedNameSpecifier(false), AllParametersAreInformative(false),
        DeclaringEntity(false), FunctionCanBeCall(true), Qualifier(Qualifier) {
    // FIXME: Add assert to check FixIts range requirements.
    computeCursorKindAndAvailability(Accessible);
  }

  /// Build a result that refers to a keyword or symbol.
  CodeCompletionResult(const char *Keyword, unsigned Priority = CCP_Keyword)
      : Keyword(Keyword), Priority(Priority), Kind(RK_Keyword),
        CursorKind(CXCursor_NotImplemented), Hidden(false), InBaseClass(false),
        QualifierIsInformative(false), StartsNestedNameSpecifier(false),
        AllParametersAreInformative(false), DeclaringEntity(false),
        FunctionCanBeCall(true) {}

  /// Build a result that refers to a macro.
  CodeCompletionResult(const IdentifierInfo *Macro,
                       const MacroInfo *MI = nullptr,
                       unsigned Priority = CCP_Macro)
      : Macro(Macro), Priority(Priority), Kind(RK_Macro),
        CursorKind(CXCursor_MacroDefinition), Hidden(false), InBaseClass(false),
        QualifierIsInformative(false), StartsNestedNameSpecifier(false),
        AllParametersAreInformative(false), DeclaringEntity(false),
        FunctionCanBeCall(true), MacroDefInfo(MI) {}

  /// Build a result that refers to a pattern.
  CodeCompletionResult(
      CodeCompletionString *Pattern, unsigned Priority = CCP_CodePattern,
      CXCursorKind CursorKind = CXCursor_NotImplemented,
      CXAvailabilityKind Availability = CXAvailability_Available,
      const NamedDecl *D = nullptr)
      : Declaration(D), Pattern(Pattern), Priority(Priority), Kind(RK_Pattern),
        CursorKind(CursorKind), Availability(Availability), Hidden(false),
        InBaseClass(false), QualifierIsInformative(false),
        StartsNestedNameSpecifier(false), AllParametersAreInformative(false),
        DeclaringEntity(false), FunctionCanBeCall(true) {}

  /// Build a result that refers to a pattern with an associated
  /// declaration.
  CodeCompletionResult(CodeCompletionString *Pattern, const NamedDecl *D,
                       unsigned Priority)
      : Declaration(D), Pattern(Pattern), Priority(Priority), Kind(RK_Pattern),
        Hidden(false), InBaseClass(false), QualifierIsInformative(false),
        StartsNestedNameSpecifier(false), AllParametersAreInformative(false),
        DeclaringEntity(false), FunctionCanBeCall(true) {
    computeCursorKindAndAvailability();
  }

  /// Retrieve the declaration stored in this result. This might be nullptr if
  /// Kind is RK_Pattern.
  const NamedDecl *getDeclaration() const {
    assert(((Kind == RK_Declaration) || (Kind == RK_Pattern)) &&
           "Not a declaration or pattern result");
    return Declaration;
  }

  /// Retrieve the keyword stored in this result.
  const char *getKeyword() const {
    assert(Kind == RK_Keyword && "Not a keyword result");
    return Keyword;
  }

  /// Create a new code-completion string that describes how to insert
  /// this result into a program.
  ///
  /// \param S The semantic analysis that created the result.
  ///
  /// \param Allocator The allocator that will be used to allocate the
  /// string itself.
  CodeCompletionString *CreateCodeCompletionString(Sema &S,
                                         const CodeCompletionContext &CCContext,
                                           CodeCompletionAllocator &Allocator,
                                           CodeCompletionTUInfo &CCTUInfo,
                                           bool IncludeBriefComments);
  CodeCompletionString *CreateCodeCompletionString(ASTContext &Ctx,
                                                   Preprocessor &PP,
                                         const CodeCompletionContext &CCContext,
                                           CodeCompletionAllocator &Allocator,
                                           CodeCompletionTUInfo &CCTUInfo,
                                           bool IncludeBriefComments);
  /// Creates a new code-completion string for the macro result. Similar to the
  /// above overloads, except this only requires preprocessor information.
  /// The result kind must be `RK_Macro`.
  CodeCompletionString *
  CreateCodeCompletionStringForMacro(Preprocessor &PP,
                                     CodeCompletionAllocator &Allocator,
                                     CodeCompletionTUInfo &CCTUInfo);

  CodeCompletionString *createCodeCompletionStringForDecl(
      Preprocessor &PP, ASTContext &Ctx, CodeCompletionBuilder &Result,
      bool IncludeBriefComments, const CodeCompletionContext &CCContext,
      PrintingPolicy &Policy);

  CodeCompletionString *createCodeCompletionStringForOverride(
      Preprocessor &PP, ASTContext &Ctx, CodeCompletionBuilder &Result,
      bool IncludeBriefComments, const CodeCompletionContext &CCContext,
      PrintingPolicy &Policy);

  /// Retrieve the name that should be used to order a result.
  ///
  /// If the name needs to be constructed as a string, that string will be
  /// saved into Saved and the returned StringRef will refer to it.
  StringRef getOrderedName(std::string &Saved) const;

private:
  void computeCursorKindAndAvailability(bool Accessible = true);
};

bool operator<(const CodeCompletionResult &X, const CodeCompletionResult &Y);

inline bool operator>(const CodeCompletionResult &X,
                      const CodeCompletionResult &Y) {
  return Y < X;
}

inline bool operator<=(const CodeCompletionResult &X,
                      const CodeCompletionResult &Y) {
  return !(Y < X);
}

inline bool operator>=(const CodeCompletionResult &X,
                       const CodeCompletionResult &Y) {
  return !(X < Y);
}

/// Abstract interface for a consumer of code-completion
/// information.
class CodeCompleteConsumer {
protected:
  const CodeCompleteOptions CodeCompleteOpts;

public:
  class OverloadCandidate {
  public:
    /// Describes the type of overload candidate.
    enum CandidateKind {
      /// The candidate is a function declaration.
      CK_Function,

      /// The candidate is a function template, arguments are being completed.
      CK_FunctionTemplate,

      /// The "candidate" is actually a variable, expression, or block
      /// for which we only have a function prototype.
      CK_FunctionType,

      /// The candidate is a variable or expression of function type
      /// for which we have the location of the prototype declaration.
      CK_FunctionProtoTypeLoc,

      /// The candidate is a template, template arguments are being completed.
      CK_Template,

      /// The candidate is aggregate initialization of a record type.
      CK_Aggregate,
    };

  private:
    /// The kind of overload candidate.
    CandidateKind Kind;

    union {
      /// The function overload candidate, available when
      /// Kind == CK_Function.
      FunctionDecl *Function;

      /// The function template overload candidate, available when
      /// Kind == CK_FunctionTemplate.
      FunctionTemplateDecl *FunctionTemplate;

      /// The function type that describes the entity being called,
      /// when Kind == CK_FunctionType.
      const FunctionType *Type;

      /// The location of the function prototype that describes the entity being
      /// called, when Kind == CK_FunctionProtoTypeLoc.
      FunctionProtoTypeLoc ProtoTypeLoc;

      /// The template overload candidate, available when
      /// Kind == CK_Template.
      const TemplateDecl *Template;

      /// The class being aggregate-initialized,
      /// when Kind == CK_Aggregate
      const RecordDecl *AggregateType;
    };

  public:
    OverloadCandidate(FunctionDecl *Function)
        : Kind(CK_Function), Function(Function) {
      assert(Function != nullptr);
    }

    OverloadCandidate(FunctionTemplateDecl *FunctionTemplateDecl)
        : Kind(CK_FunctionTemplate), FunctionTemplate(FunctionTemplateDecl) {
      assert(FunctionTemplateDecl != nullptr);
    }

    OverloadCandidate(const FunctionType *Type)
        : Kind(CK_FunctionType), Type(Type) {
      assert(Type != nullptr);
    }

    OverloadCandidate(FunctionProtoTypeLoc Prototype)
        : Kind(CK_FunctionProtoTypeLoc), ProtoTypeLoc(Prototype) {
      assert(!Prototype.isNull());
    }

    OverloadCandidate(const RecordDecl *Aggregate)
        : Kind(CK_Aggregate), AggregateType(Aggregate) {
      assert(Aggregate != nullptr);
    }

    OverloadCandidate(const TemplateDecl *Template)
        : Kind(CK_Template), Template(Template) {}

    /// Determine the kind of overload candidate.
    CandidateKind getKind() const { return Kind; }

    /// Retrieve the function overload candidate or the templated
    /// function declaration for a function template.
    FunctionDecl *getFunction() const;

    /// Retrieve the function template overload candidate.
    FunctionTemplateDecl *getFunctionTemplate() const {
      assert(getKind() == CK_FunctionTemplate && "Not a function template");
      return FunctionTemplate;
    }

    /// Retrieve the function type of the entity, regardless of how the
    /// function is stored.
    const FunctionType *getFunctionType() const;

    /// Retrieve the function ProtoTypeLoc candidate.
    /// This can be called for any Kind, but returns null for kinds
    /// other than CK_FunctionProtoTypeLoc.
    const FunctionProtoTypeLoc getFunctionProtoTypeLoc() const;

    const TemplateDecl *getTemplate() const {
      assert(getKind() == CK_Template && "Not a template");
      return Template;
    }

    /// Retrieve the aggregate type being initialized.
    const RecordDecl *getAggregate() const {
      assert(getKind() == CK_Aggregate);
      return AggregateType;
    }

    /// Get the number of parameters in this signature.
    unsigned getNumParams() const;

    /// Get the type of the Nth parameter.
    /// Returns null if the type is unknown or N is out of range.
    QualType getParamType(unsigned N) const;

    /// Get the declaration of the Nth parameter.
    /// Returns null if the decl is unknown or N is out of range.
    const NamedDecl *getParamDecl(unsigned N) const;

    /// Create a new code-completion string that describes the function
    /// signature of this overload candidate.
    CodeCompletionString *
    CreateSignatureString(unsigned CurrentArg, Sema &S,
                          CodeCompletionAllocator &Allocator,
                          CodeCompletionTUInfo &CCTUInfo,
                          bool IncludeBriefComments, bool Braced) const;
  };

  CodeCompleteConsumer(const CodeCompleteOptions &CodeCompleteOpts)
      : CodeCompleteOpts(CodeCompleteOpts) {}

  /// Whether the code-completion consumer wants to see macros.
  bool includeMacros() const {
    return CodeCompleteOpts.IncludeMacros;
  }

  /// Whether the code-completion consumer wants to see code patterns.
  bool includeCodePatterns() const {
    return CodeCompleteOpts.IncludeCodePatterns;
  }

  /// Whether to include global (top-level) declaration results.
  bool includeGlobals() const { return CodeCompleteOpts.IncludeGlobals; }

  /// Whether to include declarations in namespace contexts (including
  /// the global namespace). If this is false, `includeGlobals()` will be
  /// ignored.
  bool includeNamespaceLevelDecls() const {
    return CodeCompleteOpts.IncludeNamespaceLevelDecls;
  }

  /// Whether to include brief documentation comments within the set of
  /// code completions returned.
  bool includeBriefComments() const {
    return CodeCompleteOpts.IncludeBriefComments;
  }

  /// Whether to include completion items with small fix-its, e.g. change
  /// '.' to '->' on member access, etc.
  bool includeFixIts() const { return CodeCompleteOpts.IncludeFixIts; }

  /// Hint whether to load data from the external AST in order to provide
  /// full results. If false, declarations from the preamble may be omitted.
  bool loadExternal() const {
    return CodeCompleteOpts.LoadExternal;
  }

  /// Deregisters and destroys this code-completion consumer.
  virtual ~CodeCompleteConsumer();

  /// \name Code-completion filtering
  /// Check if the result should be filtered out.
  virtual bool isResultFilteredOut(StringRef Filter,
                                   CodeCompletionResult Results) {
    return false;
  }

  /// \name Code-completion callbacks
  //@{
  /// Process the finalized code-completion results.
  virtual void ProcessCodeCompleteResults(Sema &S,
                                          CodeCompletionContext Context,
                                          CodeCompletionResult *Results,
                                          unsigned NumResults) {}

  /// \param S the semantic-analyzer object for which code-completion is being
  /// done.
  ///
  /// \param CurrentArg the index of the current argument.
  ///
  /// \param Candidates an array of overload candidates.
  ///
  /// \param NumCandidates the number of overload candidates
  ///
  /// \param OpenParLoc location of the opening parenthesis of the argument
  ///        list.
  virtual void ProcessOverloadCandidates(Sema &S, unsigned CurrentArg,
                                         OverloadCandidate *Candidates,
                                         unsigned NumCandidates,
                                         SourceLocation OpenParLoc,
                                         bool Braced) {}
  //@}

  /// Retrieve the allocator that will be used to allocate
  /// code completion strings.
  virtual CodeCompletionAllocator &getAllocator() = 0;

  virtual CodeCompletionTUInfo &getCodeCompletionTUInfo() = 0;
};

/// Get the documentation comment used to produce
/// CodeCompletionString::BriefComment for RK_Declaration.
const RawComment *getCompletionComment(const ASTContext &Ctx,
                                       const NamedDecl *Decl);

/// Get the documentation comment used to produce
/// CodeCompletionString::BriefComment for RK_Pattern.
const RawComment *getPatternCompletionComment(const ASTContext &Ctx,
                                              const NamedDecl *Decl);

/// Get the documentation comment used to produce
/// CodeCompletionString::BriefComment for OverloadCandidate.
const RawComment *
getParameterComment(const ASTContext &Ctx,
                    const CodeCompleteConsumer::OverloadCandidate &Result,
                    unsigned ArgIndex);

/// A simple code-completion consumer that prints the results it
/// receives in a simple format.
class PrintingCodeCompleteConsumer : public CodeCompleteConsumer {
  /// The raw output stream.
  raw_ostream &OS;

  CodeCompletionTUInfo CCTUInfo;

public:
  /// Create a new printing code-completion consumer that prints its
  /// results to the given raw output stream.
  PrintingCodeCompleteConsumer(const CodeCompleteOptions &CodeCompleteOpts,
                               raw_ostream &OS)
      : CodeCompleteConsumer(CodeCompleteOpts), OS(OS),
        CCTUInfo(std::make_shared<GlobalCodeCompletionAllocator>()) {}

  /// Prints the finalized code-completion results.
  void ProcessCodeCompleteResults(Sema &S, CodeCompletionContext Context,
                                  CodeCompletionResult *Results,
                                  unsigned NumResults) override;

  void ProcessOverloadCandidates(Sema &S, unsigned CurrentArg,
                                 OverloadCandidate *Candidates,
                                 unsigned NumCandidates,
                                 SourceLocation OpenParLoc,
                                 bool Braced) override;

  bool isResultFilteredOut(StringRef Filter, CodeCompletionResult Results) override;

  CodeCompletionAllocator &getAllocator() override {
    return CCTUInfo.getAllocator();
  }

  CodeCompletionTUInfo &getCodeCompletionTUInfo() override { return CCTUInfo; }
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_CODECOMPLETECONSUMER_H

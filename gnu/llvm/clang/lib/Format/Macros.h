//===--- Macros.h - Format C++ code -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the main building blocks of macro support in
/// clang-format.
///
/// In order to not violate the requirement that clang-format can format files
/// in isolation, clang-format's macro support uses expansions users provide
/// as part of clang-format's style configuration.
///
/// Macro definitions are of the form "MACRO(p1, p2)=p1 + p2", but only support
/// one level of expansion (\see MacroExpander for a full description of what
/// is supported).
///
/// As part of parsing, clang-format uses the MacroExpander to expand the
/// spelled token streams into expanded token streams when it encounters a
/// macro call. The UnwrappedLineParser continues to parse UnwrappedLines
/// from the expanded token stream.
/// After the expanded unwrapped lines are parsed, the MacroCallReconstructor
/// matches the spelled token stream into unwrapped lines that best resemble the
/// structure of the expanded unwrapped lines. These reconstructed unwrapped
/// lines are aliasing the tokens in the expanded token stream, so that token
/// annotations will be reused when formatting the spelled macro calls.
///
/// When formatting, clang-format annotates and formats the expanded unwrapped
/// lines first, determining the token types. Next, it formats the spelled
/// unwrapped lines, keeping the token types fixed, while allowing other
/// formatting decisions to change.
///
//===----------------------------------------------------------------------===//

#ifndef CLANG_LIB_FORMAT_MACROS_H
#define CLANG_LIB_FORMAT_MACROS_H

#include <list>

#include "FormatToken.h"
#include "llvm/ADT/DenseMap.h"

namespace clang {
namespace format {

struct UnwrappedLine;
struct UnwrappedLineNode;

/// Takes a set of macro definitions as strings and allows expanding calls to
/// those macros.
///
/// For example:
/// Definition: A(x, y)=x + y
/// Call      : A(int a = 1, 2)
/// Expansion : int a = 1 + 2
///
/// Expansion does not check arity of the definition.
/// If fewer arguments than expected are provided, the remaining parameters
/// are considered empty:
/// Call     : A(a)
/// Expansion: a +
/// If more arguments than expected are provided, they will be discarded.
///
/// The expander does not support:
/// - recursive expansion
/// - stringification
/// - concatenation
/// - variadic macros
///
/// Furthermore, only a single expansion of each macro argument is supported,
/// so that we cannot get conflicting formatting decisions from different
/// expansions.
/// Definition: A(x)=x+x
/// Call      : A(id)
/// Expansion : id+x
///
class MacroExpander {
public:
  using ArgsList = ArrayRef<SmallVector<FormatToken *, 8>>;

  /// Construct a macro expander from a set of macro definitions.
  /// Macro definitions must be encoded as UTF-8.
  ///
  /// Each entry in \p Macros must conform to the following simple
  /// macro-definition language:
  /// <definition> ::= <id> <expansion> | <id> "(" <params> ")" <expansion>
  /// <params>     ::= <id-list> | ""
  /// <id-list>    ::= <id> | <id> "," <params>
  /// <expansion>  ::= "=" <tail> | <eof>
  /// <tail>       ::= <tok> <tail> | <eof>
  ///
  /// Macros that cannot be parsed will be silently discarded.
  ///
  MacroExpander(const std::vector<std::string> &Macros,
                SourceManager &SourceMgr, const FormatStyle &Style,
                llvm::SpecificBumpPtrAllocator<FormatToken> &Allocator,
                IdentifierTable &IdentTable);
  ~MacroExpander();

  /// Returns whether any macro \p Name is defined, regardless of overloads.
  bool defined(StringRef Name) const;

  /// Returns whetherh there is an object-like overload, i.e. where the macro
  /// has no arguments and should not consume subsequent parentheses.
  bool objectLike(StringRef Name) const;

  /// Returns whether macro \p Name provides an overload with the given arity.
  bool hasArity(StringRef Name, unsigned Arity) const;

  /// Returns the expanded stream of format tokens for \p ID, where
  /// each element in \p Args is a positional argument to the macro call.
  /// If \p Args is not set, the object-like overload is used.
  /// If \p Args is set, the overload with the arity equal to \c Args.size() is
  /// used.
  SmallVector<FormatToken *, 8>
  expand(FormatToken *ID, std::optional<ArgsList> OptionalArgs) const;

private:
  struct Definition;
  class DefinitionParser;

  void parseDefinition(const std::string &Macro);

  SourceManager &SourceMgr;
  const FormatStyle &Style;
  llvm::SpecificBumpPtrAllocator<FormatToken> &Allocator;
  IdentifierTable &IdentTable;
  SmallVector<std::unique_ptr<llvm::MemoryBuffer>> Buffers;
  llvm::StringMap<llvm::DenseMap<int, Definition>> FunctionLike;
  llvm::StringMap<Definition> ObjectLike;
};

/// Converts a sequence of UnwrappedLines containing expanded macros into a
/// single UnwrappedLine containing the macro calls.  This UnwrappedLine may be
/// broken into child lines, in a way that best conveys the structure of the
/// expanded code.
///
/// In the simplest case, a spelled UnwrappedLine contains one macro, and after
/// expanding it we have one expanded UnwrappedLine.  In general, macro
/// expansions can span UnwrappedLines, and multiple macros can contribute
/// tokens to the same line.  We keep consuming expanded lines until:
/// *   all expansions that started have finished (we're not chopping any macros
///     in half)
/// *   *and* we've reached the end of a *spelled* unwrapped line.
///
/// A single UnwrappedLine represents this chunk of code.
///
/// After this point, the state of the spelled/expanded stream is "in sync"
/// (both at the start of an UnwrappedLine, with no macros open), so the
/// Reconstructor can be thrown away and parsing can continue.
///
/// Given a mapping from the macro name identifier token in the macro call
/// to the tokens of the macro call, for example:
/// CLASSA -> CLASSA({public: void x();})
///
/// When getting the formatted lines of the expansion via the \c addLine method
/// (each '->' specifies a call to \c addLine ):
/// -> class A {
/// -> public:
/// ->   void x();
/// -> };
///
/// Creates the tree of unwrapped lines containing the macro call tokens so that
/// the macro call tokens fit the semantic structure of the expanded formatted
/// lines:
/// -> CLASSA({
/// -> public:
/// ->   void x();
/// -> })
class MacroCallReconstructor {
public:
  /// Create an Reconstructor whose resulting \p UnwrappedLine will start at
  /// \p Level, using the map from name identifier token to the corresponding
  /// tokens of the spelled macro call.
  MacroCallReconstructor(
      unsigned Level,
      const llvm::DenseMap<FormatToken *, std::unique_ptr<UnwrappedLine>>
          &ActiveExpansions);

  /// For the given \p Line, match all occurences of tokens expanded from a
  /// macro to unwrapped lines in the spelled macro call so that the resulting
  /// tree of unwrapped lines best resembles the structure of unwrapped lines
  /// passed in via \c addLine.
  void addLine(const UnwrappedLine &Line);

  /// Check whether at the current state there is no open macro expansion
  /// that needs to be processed to finish an macro call.
  /// Only when \c finished() is true, \c takeResult() can be called to retrieve
  /// the resulting \c UnwrappedLine.
  /// If there are multiple subsequent macro calls within an unwrapped line in
  /// the spelled token stream, the calling code may also continue to call
  /// \c addLine() when \c finished() is true.
  bool finished() const { return ActiveExpansions.empty(); }

  /// Retrieve the formatted \c UnwrappedLine containing the orginal
  /// macro calls, formatted according to the expanded token stream received
  /// via \c addLine().
  /// Generally, this line tries to have the same structure as the expanded,
  /// formatted unwrapped lines handed in via \c addLine(), with the exception
  /// that for multiple top-level lines, each subsequent line will be the
  /// child of the last token in its predecessor. This representation is chosen
  /// because it is a precondition to the formatter that we get what looks like
  /// a single statement in a single \c UnwrappedLine (i.e. matching parens).
  ///
  /// If a token in a macro argument is a child of a token in the expansion,
  /// the parent will be the corresponding token in the macro call.
  /// For example:
  ///   #define C(a, b) class C { a b
  ///   C(int x;, int y;)
  /// would expand to
  ///   class C { int x; int y;
  /// where in a formatted line "int x;" and "int y;" would both be new separate
  /// lines.
  ///
  /// In the result, "int x;" will be a child of the opening parenthesis in "C("
  /// and "int y;" will be a child of the "," token:
  ///   C (
  ///     \- int x;
  ///     ,
  ///     \- int y;
  ///     )
  UnwrappedLine takeResult() &&;

private:
  void add(FormatToken *Token, FormatToken *ExpandedParent, bool First,
           unsigned Level);
  void prepareParent(FormatToken *ExpandedParent, bool First, unsigned Level);
  FormatToken *getParentInResult(FormatToken *Parent);
  void reconstruct(FormatToken *Token);
  void startReconstruction(FormatToken *Token);
  bool reconstructActiveCallUntil(FormatToken *Token);
  void endReconstruction(FormatToken *Token);
  bool processNextReconstructed();
  void finalize();

  struct ReconstructedLine;

  void appendToken(FormatToken *Token, ReconstructedLine *L = nullptr);
  UnwrappedLine createUnwrappedLine(const ReconstructedLine &Line, int Level);
  void debug(const ReconstructedLine &Line, int Level);
  ReconstructedLine &parentLine();
  ReconstructedLine *currentLine();
  void debugParentMap() const;

#ifndef NDEBUG
  enum ReconstructorState {
    Start,      // No macro expansion was found in the input yet.
    InProgress, // During a macro reconstruction.
    Finalized,  // Past macro reconstruction, the result is finalized.
  };
  ReconstructorState State = Start;
#endif

  // Node in which we build up the resulting unwrapped line; this type is
  // analogous to UnwrappedLineNode.
  struct LineNode {
    LineNode() = default;
    LineNode(FormatToken *Tok) : Tok(Tok) {}
    FormatToken *Tok = nullptr;
    SmallVector<std::unique_ptr<ReconstructedLine>> Children;
  };

  // Line in which we build up the resulting unwrapped line.
  // FIXME: Investigate changing UnwrappedLine to a pointer type and using it
  // instead of rolling our own type.
  struct ReconstructedLine {
    explicit ReconstructedLine(unsigned Level) : Level(Level) {}
    unsigned Level;
    SmallVector<std::unique_ptr<LineNode>> Tokens;
  };

  // The line in which we collect the resulting reconstructed output.
  // To reduce special cases in the algorithm, the first level of the line
  // contains a single null token that has the reconstructed incoming
  // lines as children.
  // In the end, we stich the lines together so that each subsequent line
  // is a child of the last token of the previous line. This is necessary
  // in order to format the overall expression as a single logical line -
  // if we created separate lines, we'd format them with their own top-level
  // indent depending on the semantic structure, which is not desired.
  ReconstructedLine Result;

  // Stack of currently "open" lines, where each line's predecessor's last
  // token is the parent token for that line.
  SmallVector<ReconstructedLine *> ActiveReconstructedLines;

  // Maps from the expanded token to the token that takes its place in the
  // reconstructed token stream in terms of parent-child relationships.
  // Note that it might take multiple steps to arrive at the correct
  // parent in the output.
  // Given: #define C(a, b) []() { a; b; }
  // And a call: C(f(), g())
  // The structure in the incoming formatted unwrapped line will be:
  // []() {
  //      |- f();
  //      \- g();
  // }
  // with f and g being children of the opening brace.
  // In the reconstructed call:
  // C(f(), g())
  //  \- f()
  //      \- g()
  // We want f to be a child of the opening parenthesis and g to be a child
  // of the comma token in the macro call.
  // Thus, we map
  // { -> (
  // and add
  // ( -> ,
  // once we're past the comma in the reconstruction.
  llvm::DenseMap<FormatToken *, FormatToken *>
      SpelledParentToReconstructedParent;

  // Keeps track of a single expansion while we're reconstructing tokens it
  // generated.
  struct Expansion {
    // The identifier token of the macro call.
    FormatToken *ID;
    // Our current position in the reconstruction.
    std::list<UnwrappedLineNode>::iterator SpelledI;
    // The end of the reconstructed token sequence.
    std::list<UnwrappedLineNode>::iterator SpelledE;
  };

  // Stack of macro calls for which we're in the middle of an expansion.
  SmallVector<Expansion> ActiveExpansions;

  struct MacroCallState {
    MacroCallState(ReconstructedLine *Line, FormatToken *ParentLastToken,
                   FormatToken *MacroCallLParen);

    ReconstructedLine *Line;

    // The last token in the parent line or expansion, or nullptr if the macro
    // expansion is on a top-level line.
    //
    // For example, in the macro call:
    //   auto f = []() { ID(1); };
    // The MacroCallState for ID will have '{' as ParentLastToken.
    //
    // In the macro call:
    //   ID(ID(void f()));
    // The MacroCallState of the outer ID will have nullptr as ParentLastToken,
    // while the MacroCallState for the inner ID will have the '(' of the outer
    // ID as ParentLastToken.
    //
    // In the macro call:
    //   ID2(a, ID(b));
    // The MacroCallState of ID will have ',' as ParentLastToken.
    FormatToken *ParentLastToken;

    // The l_paren of this MacroCallState's macro call.
    FormatToken *MacroCallLParen;
  };

  // Keeps track of the lines into which the opening brace/parenthesis &
  // argument separating commas for each level in the macro call go in order to
  // put the corresponding closing brace/parenthesis into the same line in the
  // output and keep track of which parents in the expanded token stream map to
  // which tokens in the reconstructed stream.
  // When an opening brace/parenthesis has children, we want the structure of
  // the output line to be:
  // |- MACRO
  // |- (
  // |  \- <argument>
  // |- ,
  // |  \- <argument>
  // \- )
  SmallVector<MacroCallState> MacroCallStructure;

  // Maps from identifier of the macro call to an unwrapped line containing
  // all tokens of the macro call.
  const llvm::DenseMap<FormatToken *, std::unique_ptr<UnwrappedLine>>
      &IdToReconstructed;
};

} // namespace format
} // namespace clang

#endif

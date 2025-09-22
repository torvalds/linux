//===--- Format.h - Format C++ code -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Various functions to configurably format source code.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FORMAT_FORMAT_H
#define LLVM_CLANG_FORMAT_FORMAT_H

#include "clang/Basic/LangOptions.h"
#include "clang/Tooling/Core/Replacement.h"
#include "clang/Tooling/Inclusions/IncludeStyle.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/SourceMgr.h"
#include <optional>
#include <system_error>

namespace llvm {
namespace vfs {
class FileSystem;
}
} // namespace llvm

namespace clang {
namespace format {

enum class ParseError {
  Success = 0,
  Error,
  Unsuitable,
  BinPackTrailingCommaConflict,
  InvalidQualifierSpecified,
  DuplicateQualifierSpecified,
  MissingQualifierType,
  MissingQualifierOrder
};
class ParseErrorCategory final : public std::error_category {
public:
  const char *name() const noexcept override;
  std::string message(int EV) const override;
};
const std::error_category &getParseCategory();
std::error_code make_error_code(ParseError e);

/// The ``FormatStyle`` is used to configure the formatting to follow
/// specific guidelines.
struct FormatStyle {
  // If the BasedOn: was InheritParentConfig and this style needs the file from
  // the parent directories. It is not part of the actual style for formatting.
  // Thus the // instead of ///.
  bool InheritsParentConfig;

  /// The extra indent or outdent of access modifiers, e.g. ``public:``.
  /// \version 3.3
  int AccessModifierOffset;

  /// Different styles for aligning after open brackets.
  enum BracketAlignmentStyle : int8_t {
    /// Align parameters on the open bracket, e.g.:
    /// \code
    ///   someLongFunction(argument1,
    ///                    argument2);
    /// \endcode
    BAS_Align,
    /// Don't align, instead use ``ContinuationIndentWidth``, e.g.:
    /// \code
    ///   someLongFunction(argument1,
    ///       argument2);
    /// \endcode
    BAS_DontAlign,
    /// Always break after an open bracket, if the parameters don't fit
    /// on a single line, e.g.:
    /// \code
    ///   someLongFunction(
    ///       argument1, argument2);
    /// \endcode
    BAS_AlwaysBreak,
    /// Always break after an open bracket, if the parameters don't fit
    /// on a single line. Closing brackets will be placed on a new line.
    /// E.g.:
    /// \code
    ///   someLongFunction(
    ///       argument1, argument2
    ///   )
    /// \endcode
    ///
    /// \note
    ///  This currently only applies to braced initializer lists (when
    ///  ``Cpp11BracedListStyle`` is ``true``) and parentheses.
    /// \endnote
    BAS_BlockIndent,
  };

  /// If ``true``, horizontally aligns arguments after an open bracket.
  ///
  /// This applies to round brackets (parentheses), angle brackets and square
  /// brackets.
  /// \version 3.8
  BracketAlignmentStyle AlignAfterOpenBracket;

  /// Different style for aligning array initializers.
  enum ArrayInitializerAlignmentStyle : int8_t {
    /// Align array column and left justify the columns e.g.:
    /// \code
    ///   struct test demo[] =
    ///   {
    ///       {56, 23,    "hello"},
    ///       {-1, 93463, "world"},
    ///       {7,  5,     "!!"   }
    ///   };
    /// \endcode
    AIAS_Left,
    /// Align array column and right justify the columns e.g.:
    /// \code
    ///   struct test demo[] =
    ///   {
    ///       {56,    23, "hello"},
    ///       {-1, 93463, "world"},
    ///       { 7,     5,    "!!"}
    ///   };
    /// \endcode
    AIAS_Right,
    /// Don't align array initializer columns.
    AIAS_None
  };
  /// if not ``None``, when using initialization for an array of structs
  /// aligns the fields into columns.
  ///
  /// \note
  ///  As of clang-format 15 this option only applied to arrays with equal
  ///  number of columns per row.
  /// \endnote
  ///
  /// \version 13
  ArrayInitializerAlignmentStyle AlignArrayOfStructures;

  /// Alignment options.
  ///
  /// They can also be read as a whole for compatibility. The choices are:
  /// - None
  /// - Consecutive
  /// - AcrossEmptyLines
  /// - AcrossComments
  /// - AcrossEmptyLinesAndComments
  ///
  /// For example, to align across empty lines and not across comments, either
  /// of these work.
  /// \code
  ///   <option-name>: AcrossEmptyLines
  ///
  ///   <option-name>:
  ///     Enabled: true
  ///     AcrossEmptyLines: true
  ///     AcrossComments: false
  /// \endcode
  struct AlignConsecutiveStyle {
    /// Whether aligning is enabled.
    /// \code
    ///   #define SHORT_NAME       42
    ///   #define LONGER_NAME      0x007f
    ///   #define EVEN_LONGER_NAME (2)
    ///   #define foo(x)           (x * x)
    ///   #define bar(y, z)        (y + z)
    ///
    ///   int a            = 1;
    ///   int somelongname = 2;
    ///   double c         = 3;
    ///
    ///   int aaaa : 1;
    ///   int b    : 12;
    ///   int ccc  : 8;
    ///
    ///   int         aaaa = 12;
    ///   float       b = 23;
    ///   std::string ccc;
    /// \endcode
    bool Enabled;
    /// Whether to align across empty lines.
    /// \code
    ///   true:
    ///   int a            = 1;
    ///   int somelongname = 2;
    ///   double c         = 3;
    ///
    ///   int d            = 3;
    ///
    ///   false:
    ///   int a            = 1;
    ///   int somelongname = 2;
    ///   double c         = 3;
    ///
    ///   int d = 3;
    /// \endcode
    bool AcrossEmptyLines;
    /// Whether to align across comments.
    /// \code
    ///   true:
    ///   int d    = 3;
    ///   /* A comment. */
    ///   double e = 4;
    ///
    ///   false:
    ///   int d = 3;
    ///   /* A comment. */
    ///   double e = 4;
    /// \endcode
    bool AcrossComments;
    /// Only for ``AlignConsecutiveAssignments``.  Whether compound assignments
    /// like ``+=`` are aligned along with ``=``.
    /// \code
    ///   true:
    ///   a   &= 2;
    ///   bbb  = 2;
    ///
    ///   false:
    ///   a &= 2;
    ///   bbb = 2;
    /// \endcode
    bool AlignCompound;
    /// Only for ``AlignConsecutiveDeclarations``. Whether function pointers are
    /// aligned.
    /// \code
    ///   true:
    ///   unsigned i;
    ///   int     &r;
    ///   int     *p;
    ///   int      (*f)();
    ///
    ///   false:
    ///   unsigned i;
    ///   int     &r;
    ///   int     *p;
    ///   int (*f)();
    /// \endcode
    bool AlignFunctionPointers;
    /// Only for ``AlignConsecutiveAssignments``.  Whether short assignment
    /// operators are left-padded to the same length as long ones in order to
    /// put all assignment operators to the right of the left hand side.
    /// \code
    ///   true:
    ///   a   >>= 2;
    ///   bbb   = 2;
    ///
    ///   a     = 2;
    ///   bbb >>= 2;
    ///
    ///   false:
    ///   a >>= 2;
    ///   bbb = 2;
    ///
    ///   a     = 2;
    ///   bbb >>= 2;
    /// \endcode
    bool PadOperators;
    bool operator==(const AlignConsecutiveStyle &R) const {
      return Enabled == R.Enabled && AcrossEmptyLines == R.AcrossEmptyLines &&
             AcrossComments == R.AcrossComments &&
             AlignCompound == R.AlignCompound &&
             AlignFunctionPointers == R.AlignFunctionPointers &&
             PadOperators == R.PadOperators;
    }
    bool operator!=(const AlignConsecutiveStyle &R) const {
      return !(*this == R);
    }
  };

  /// Style of aligning consecutive macro definitions.
  ///
  /// ``Consecutive`` will result in formattings like:
  /// \code
  ///   #define SHORT_NAME       42
  ///   #define LONGER_NAME      0x007f
  ///   #define EVEN_LONGER_NAME (2)
  ///   #define foo(x)           (x * x)
  ///   #define bar(y, z)        (y + z)
  /// \endcode
  /// \version 9
  AlignConsecutiveStyle AlignConsecutiveMacros;
  /// Style of aligning consecutive assignments.
  ///
  /// ``Consecutive`` will result in formattings like:
  /// \code
  ///   int a            = 1;
  ///   int somelongname = 2;
  ///   double c         = 3;
  /// \endcode
  /// \version 3.8
  AlignConsecutiveStyle AlignConsecutiveAssignments;
  /// Style of aligning consecutive bit fields.
  ///
  /// ``Consecutive`` will align the bitfield separators of consecutive lines.
  /// This will result in formattings like:
  /// \code
  ///   int aaaa : 1;
  ///   int b    : 12;
  ///   int ccc  : 8;
  /// \endcode
  /// \version 11
  AlignConsecutiveStyle AlignConsecutiveBitFields;
  /// Style of aligning consecutive declarations.
  ///
  /// ``Consecutive`` will align the declaration names of consecutive lines.
  /// This will result in formattings like:
  /// \code
  ///   int         aaaa = 12;
  ///   float       b = 23;
  ///   std::string ccc;
  /// \endcode
  /// \version 3.8
  AlignConsecutiveStyle AlignConsecutiveDeclarations;

  /// Alignment options.
  ///
  struct ShortCaseStatementsAlignmentStyle {
    /// Whether aligning is enabled.
    /// \code
    ///   true:
    ///   switch (level) {
    ///   case log::info:    return "info:";
    ///   case log::warning: return "warning:";
    ///   default:           return "";
    ///   }
    ///
    ///   false:
    ///   switch (level) {
    ///   case log::info: return "info:";
    ///   case log::warning: return "warning:";
    ///   default: return "";
    ///   }
    /// \endcode
    bool Enabled;
    /// Whether to align across empty lines.
    /// \code
    ///   true:
    ///   switch (level) {
    ///   case log::info:    return "info:";
    ///   case log::warning: return "warning:";
    ///
    ///   default:           return "";
    ///   }
    ///
    ///   false:
    ///   switch (level) {
    ///   case log::info:    return "info:";
    ///   case log::warning: return "warning:";
    ///
    ///   default: return "";
    ///   }
    /// \endcode
    bool AcrossEmptyLines;
    /// Whether to align across comments.
    /// \code
    ///   true:
    ///   switch (level) {
    ///   case log::info:    return "info:";
    ///   case log::warning: return "warning:";
    ///   /* A comment. */
    ///   default:           return "";
    ///   }
    ///
    ///   false:
    ///   switch (level) {
    ///   case log::info:    return "info:";
    ///   case log::warning: return "warning:";
    ///   /* A comment. */
    ///   default: return "";
    ///   }
    /// \endcode
    bool AcrossComments;
    /// Whether to align the case arrows when aligning short case expressions.
    /// \code{.java}
    ///   true:
    ///   i = switch (day) {
    ///     case THURSDAY, SATURDAY -> 8;
    ///     case WEDNESDAY          -> 9;
    ///     default                 -> 0;
    ///   };
    ///
    ///   false:
    ///   i = switch (day) {
    ///     case THURSDAY, SATURDAY -> 8;
    ///     case WEDNESDAY ->          9;
    ///     default ->                 0;
    ///   };
    /// \endcode
    bool AlignCaseArrows;
    /// Whether aligned case labels are aligned on the colon, or on the tokens
    /// after the colon.
    /// \code
    ///   true:
    ///   switch (level) {
    ///   case log::info   : return "info:";
    ///   case log::warning: return "warning:";
    ///   default          : return "";
    ///   }
    ///
    ///   false:
    ///   switch (level) {
    ///   case log::info:    return "info:";
    ///   case log::warning: return "warning:";
    ///   default:           return "";
    ///   }
    /// \endcode
    bool AlignCaseColons;
    bool operator==(const ShortCaseStatementsAlignmentStyle &R) const {
      return Enabled == R.Enabled && AcrossEmptyLines == R.AcrossEmptyLines &&
             AcrossComments == R.AcrossComments &&
             AlignCaseArrows == R.AlignCaseArrows &&
             AlignCaseColons == R.AlignCaseColons;
    }
  };

  /// Style of aligning consecutive short case labels.
  /// Only applies if ``AllowShortCaseExpressionOnASingleLine`` or
  /// ``AllowShortCaseLabelsOnASingleLine`` is ``true``.
  ///
  /// \code{.yaml}
  ///   # Example of usage:
  ///   AlignConsecutiveShortCaseStatements:
  ///     Enabled: true
  ///     AcrossEmptyLines: true
  ///     AcrossComments: true
  ///     AlignCaseColons: false
  /// \endcode
  /// \version 17
  ShortCaseStatementsAlignmentStyle AlignConsecutiveShortCaseStatements;

  /// Style of aligning consecutive TableGen DAGArg operator colons.
  /// If enabled, align the colon inside DAGArg which have line break inside.
  /// This works only when TableGenBreakInsideDAGArg is BreakElements or
  /// BreakAll and the DAGArg is not excepted by
  /// TableGenBreakingDAGArgOperators's effect.
  /// \code
  ///   let dagarg = (ins
  ///       a  :$src1,
  ///       aa :$src2,
  ///       aaa:$src3
  ///   )
  /// \endcode
  /// \version 19
  AlignConsecutiveStyle AlignConsecutiveTableGenBreakingDAGArgColons;

  /// Style of aligning consecutive TableGen cond operator colons.
  /// Align the colons of cases inside !cond operators.
  /// \code
  ///   !cond(!eq(size, 1) : 1,
  ///         !eq(size, 16): 1,
  ///         true         : 0)
  /// \endcode
  /// \version 19
  AlignConsecutiveStyle AlignConsecutiveTableGenCondOperatorColons;

  /// Style of aligning consecutive TableGen definition colons.
  /// This aligns the inheritance colons of consecutive definitions.
  /// \code
  ///   def Def       : Parent {}
  ///   def DefDef    : Parent {}
  ///   def DefDefDef : Parent {}
  /// \endcode
  /// \version 19
  AlignConsecutiveStyle AlignConsecutiveTableGenDefinitionColons;

  /// Different styles for aligning escaped newlines.
  enum EscapedNewlineAlignmentStyle : int8_t {
    /// Don't align escaped newlines.
    /// \code
    ///   #define A \
    ///     int aaaa; \
    ///     int b; \
    ///     int dddddddddd;
    /// \endcode
    ENAS_DontAlign,
    /// Align escaped newlines as far left as possible.
    /// \code
    ///   #define A   \
    ///     int aaaa; \
    ///     int b;    \
    ///     int dddddddddd;
    /// \endcode
    ENAS_Left,
    /// Align escaped newlines as far left as possible, using the last line of
    /// the preprocessor directive as the reference if it's the longest.
    /// \code
    ///   #define A         \
    ///     int aaaa;       \
    ///     int b;          \
    ///     int dddddddddd;
    /// \endcode
    ENAS_LeftWithLastLine,
    /// Align escaped newlines in the right-most column.
    /// \code
    ///   #define A                                                                      \
    ///     int aaaa;                                                                    \
    ///     int b;                                                                       \
    ///     int dddddddddd;
    /// \endcode
    ENAS_Right,
  };

  /// Options for aligning backslashes in escaped newlines.
  /// \version 5
  EscapedNewlineAlignmentStyle AlignEscapedNewlines;

  /// Different styles for aligning operands.
  enum OperandAlignmentStyle : int8_t {
    /// Do not align operands of binary and ternary expressions.
    /// The wrapped lines are indented ``ContinuationIndentWidth`` spaces from
    /// the start of the line.
    OAS_DontAlign,
    /// Horizontally align operands of binary and ternary expressions.
    ///
    /// Specifically, this aligns operands of a single expression that needs
    /// to be split over multiple lines, e.g.:
    /// \code
    ///   int aaa = bbbbbbbbbbbbbbb +
    ///             ccccccccccccccc;
    /// \endcode
    ///
    /// When ``BreakBeforeBinaryOperators`` is set, the wrapped operator is
    /// aligned with the operand on the first line.
    /// \code
    ///   int aaa = bbbbbbbbbbbbbbb
    ///             + ccccccccccccccc;
    /// \endcode
    OAS_Align,
    /// Horizontally align operands of binary and ternary expressions.
    ///
    /// This is similar to ``AO_Align``, except when
    /// ``BreakBeforeBinaryOperators`` is set, the operator is un-indented so
    /// that the wrapped operand is aligned with the operand on the first line.
    /// \code
    ///   int aaa = bbbbbbbbbbbbbbb
    ///           + ccccccccccccccc;
    /// \endcode
    OAS_AlignAfterOperator,
  };

  /// If ``true``, horizontally align operands of binary and ternary
  /// expressions.
  /// \version 3.5
  OperandAlignmentStyle AlignOperands;

  /// Enums for AlignTrailingComments
  enum TrailingCommentsAlignmentKinds : int8_t {
    /// Leave trailing comments as they are.
    /// \code
    ///   int a;    // comment
    ///   int ab;       // comment
    ///
    ///   int abc;  // comment
    ///   int abcd;     // comment
    /// \endcode
    TCAS_Leave,
    /// Align trailing comments.
    /// \code
    ///   int a;  // comment
    ///   int ab; // comment
    ///
    ///   int abc;  // comment
    ///   int abcd; // comment
    /// \endcode
    TCAS_Always,
    /// Don't align trailing comments but other formatter applies.
    /// \code
    ///   int a; // comment
    ///   int ab; // comment
    ///
    ///   int abc; // comment
    ///   int abcd; // comment
    /// \endcode
    TCAS_Never,
  };

  /// Alignment options
  struct TrailingCommentsAlignmentStyle {
    /// Specifies the way to align trailing comments.
    TrailingCommentsAlignmentKinds Kind;
    /// How many empty lines to apply alignment.
    /// When both ``MaxEmptyLinesToKeep`` and ``OverEmptyLines`` are set to 2,
    /// it formats like below.
    /// \code
    ///   int a;      // all these
    ///
    ///   int ab;     // comments are
    ///
    ///
    ///   int abcdef; // aligned
    /// \endcode
    ///
    /// When ``MaxEmptyLinesToKeep`` is set to 2 and ``OverEmptyLines`` is set
    /// to 1, it formats like below.
    /// \code
    ///   int a;  // these are
    ///
    ///   int ab; // aligned
    ///
    ///
    ///   int abcdef; // but this isn't
    /// \endcode
    unsigned OverEmptyLines;

    bool operator==(const TrailingCommentsAlignmentStyle &R) const {
      return Kind == R.Kind && OverEmptyLines == R.OverEmptyLines;
    }
    bool operator!=(const TrailingCommentsAlignmentStyle &R) const {
      return !(*this == R);
    }
  };

  /// Control of trailing comments.
  ///
  /// The alignment stops at closing braces after a line break, and only
  /// followed by other closing braces, a (``do-``) ``while``, a lambda call, or
  /// a semicolon.
  ///
  /// \note
  ///  As of clang-format 16 this option is not a bool but can be set
  ///  to the options. Conventional bool options still can be parsed as before.
  /// \endnote
  ///
  /// \code{.yaml}
  ///   # Example of usage:
  ///   AlignTrailingComments:
  ///     Kind: Always
  ///     OverEmptyLines: 2
  /// \endcode
  /// \version 3.7
  TrailingCommentsAlignmentStyle AlignTrailingComments;

  /// \brief If a function call or braced initializer list doesn't fit on a
  /// line, allow putting all arguments onto the next line, even if
  /// ``BinPackArguments`` is ``false``.
  /// \code
  ///   true:
  ///   callFunction(
  ///       a, b, c, d);
  ///
  ///   false:
  ///   callFunction(a,
  ///                b,
  ///                c,
  ///                d);
  /// \endcode
  /// \version 9
  bool AllowAllArgumentsOnNextLine;

  /// This option is **deprecated**. See ``NextLine`` of
  /// ``PackConstructorInitializers``.
  /// \version 9
  // bool AllowAllConstructorInitializersOnNextLine;

  /// If the function declaration doesn't fit on a line,
  /// allow putting all parameters of a function declaration onto
  /// the next line even if ``BinPackParameters`` is ``false``.
  /// \code
  ///   true:
  ///   void myFunction(
  ///       int a, int b, int c, int d, int e);
  ///
  ///   false:
  ///   void myFunction(int a,
  ///                   int b,
  ///                   int c,
  ///                   int d,
  ///                   int e);
  /// \endcode
  /// \version 3.3
  bool AllowAllParametersOfDeclarationOnNextLine;

  /// Different ways to break before a noexcept specifier.
  enum BreakBeforeNoexceptSpecifierStyle : int8_t {
    /// No line break allowed.
    /// \code
    ///   void foo(int arg1,
    ///            double arg2) noexcept;
    ///
    ///   void bar(int arg1, double arg2) noexcept(
    ///       noexcept(baz(arg1)) &&
    ///       noexcept(baz(arg2)));
    /// \endcode
    BBNSS_Never,
    /// For a simple ``noexcept`` there is no line break allowed, but when we
    /// have a condition it is.
    /// \code
    ///   void foo(int arg1,
    ///            double arg2) noexcept;
    ///
    ///   void bar(int arg1, double arg2)
    ///       noexcept(noexcept(baz(arg1)) &&
    ///                noexcept(baz(arg2)));
    /// \endcode
    BBNSS_OnlyWithParen,
    /// Line breaks are allowed. But note that because of the associated
    /// penalties ``clang-format`` often prefers not to break before the
    /// ``noexcept``.
    /// \code
    ///   void foo(int arg1,
    ///            double arg2) noexcept;
    ///
    ///   void bar(int arg1, double arg2)
    ///       noexcept(noexcept(baz(arg1)) &&
    ///                noexcept(baz(arg2)));
    /// \endcode
    BBNSS_Always,
  };

  /// Controls if there could be a line break before a ``noexcept`` specifier.
  /// \version 18
  BreakBeforeNoexceptSpecifierStyle AllowBreakBeforeNoexceptSpecifier;

  /// Different styles for merging short blocks containing at most one
  /// statement.
  enum ShortBlockStyle : int8_t {
    /// Never merge blocks into a single line.
    /// \code
    ///   while (true) {
    ///   }
    ///   while (true) {
    ///     continue;
    ///   }
    /// \endcode
    SBS_Never,
    /// Only merge empty blocks.
    /// \code
    ///   while (true) {}
    ///   while (true) {
    ///     continue;
    ///   }
    /// \endcode
    SBS_Empty,
    /// Always merge short blocks into a single line.
    /// \code
    ///   while (true) {}
    ///   while (true) { continue; }
    /// \endcode
    SBS_Always,
  };

  /// Dependent on the value, ``while (true) { continue; }`` can be put on a
  /// single line.
  /// \version 3.5
  ShortBlockStyle AllowShortBlocksOnASingleLine;

  /// Whether to merge a short switch labeled rule into a single line.
  /// \code{.java}
  ///   true:                               false:
  ///   switch (a) {           vs.          switch (a) {
  ///   case 1 -> 1;                        case 1 ->
  ///   default -> 0;                         1;
  ///   };                                  default ->
  ///                                         0;
  ///                                       };
  /// \endcode
  /// \version 19
  bool AllowShortCaseExpressionOnASingleLine;

  /// If ``true``, short case labels will be contracted to a single line.
  /// \code
  ///   true:                                   false:
  ///   switch (a) {                    vs.     switch (a) {
  ///   case 1: x = 1; break;                   case 1:
  ///   case 2: return;                           x = 1;
  ///   }                                         break;
  ///                                           case 2:
  ///                                             return;
  ///                                           }
  /// \endcode
  /// \version 3.6
  bool AllowShortCaseLabelsOnASingleLine;

  /// Allow short compound requirement on a single line.
  /// \code
  ///   true:
  ///   template <typename T>
  ///   concept c = requires(T x) {
  ///     { x + 1 } -> std::same_as<int>;
  ///   };
  ///
  ///   false:
  ///   template <typename T>
  ///   concept c = requires(T x) {
  ///     {
  ///       x + 1
  ///     } -> std::same_as<int>;
  ///   };
  /// \endcode
  /// \version 18
  bool AllowShortCompoundRequirementOnASingleLine;

  /// Allow short enums on a single line.
  /// \code
  ///   true:
  ///   enum { A, B } myEnum;
  ///
  ///   false:
  ///   enum {
  ///     A,
  ///     B
  ///   } myEnum;
  /// \endcode
  /// \version 11
  bool AllowShortEnumsOnASingleLine;

  /// Different styles for merging short functions containing at most one
  /// statement.
  enum ShortFunctionStyle : int8_t {
    /// Never merge functions into a single line.
    SFS_None,
    /// Only merge functions defined inside a class. Same as ``inline``,
    /// except it does not implies ``empty``: i.e. top level empty functions
    /// are not merged either.
    /// \code
    ///   class Foo {
    ///     void f() { foo(); }
    ///   };
    ///   void f() {
    ///     foo();
    ///   }
    ///   void f() {
    ///   }
    /// \endcode
    SFS_InlineOnly,
    /// Only merge empty functions.
    /// \code
    ///   void f() {}
    ///   void f2() {
    ///     bar2();
    ///   }
    /// \endcode
    SFS_Empty,
    /// Only merge functions defined inside a class. Implies ``empty``.
    /// \code
    ///   class Foo {
    ///     void f() { foo(); }
    ///   };
    ///   void f() {
    ///     foo();
    ///   }
    ///   void f() {}
    /// \endcode
    SFS_Inline,
    /// Merge all functions fitting on a single line.
    /// \code
    ///   class Foo {
    ///     void f() { foo(); }
    ///   };
    ///   void f() { bar(); }
    /// \endcode
    SFS_All,
  };

  /// Dependent on the value, ``int f() { return 0; }`` can be put on a
  /// single line.
  /// \version 3.5
  ShortFunctionStyle AllowShortFunctionsOnASingleLine;

  /// Different styles for handling short if statements.
  enum ShortIfStyle : int8_t {
    /// Never put short ifs on the same line.
    /// \code
    ///   if (a)
    ///     return;
    ///
    ///   if (b)
    ///     return;
    ///   else
    ///     return;
    ///
    ///   if (c)
    ///     return;
    ///   else {
    ///     return;
    ///   }
    /// \endcode
    SIS_Never,
    /// Put short ifs on the same line only if there is no else statement.
    /// \code
    ///   if (a) return;
    ///
    ///   if (b)
    ///     return;
    ///   else
    ///     return;
    ///
    ///   if (c)
    ///     return;
    ///   else {
    ///     return;
    ///   }
    /// \endcode
    SIS_WithoutElse,
    /// Put short ifs, but not else ifs nor else statements, on the same line.
    /// \code
    ///   if (a) return;
    ///
    ///   if (b) return;
    ///   else if (b)
    ///     return;
    ///   else
    ///     return;
    ///
    ///   if (c) return;
    ///   else {
    ///     return;
    ///   }
    /// \endcode
    SIS_OnlyFirstIf,
    /// Always put short ifs, else ifs and else statements on the same
    /// line.
    /// \code
    ///   if (a) return;
    ///
    ///   if (b) return;
    ///   else return;
    ///
    ///   if (c) return;
    ///   else {
    ///     return;
    ///   }
    /// \endcode
    SIS_AllIfsAndElse,
  };

  /// Dependent on the value, ``if (a) return;`` can be put on a single line.
  /// \version 3.3
  ShortIfStyle AllowShortIfStatementsOnASingleLine;

  /// Different styles for merging short lambdas containing at most one
  /// statement.
  enum ShortLambdaStyle : int8_t {
    /// Never merge lambdas into a single line.
    SLS_None,
    /// Only merge empty lambdas.
    /// \code
    ///   auto lambda = [](int a) {};
    ///   auto lambda2 = [](int a) {
    ///       return a;
    ///   };
    /// \endcode
    SLS_Empty,
    /// Merge lambda into a single line if the lambda is argument of a function.
    /// \code
    ///   auto lambda = [](int x, int y) {
    ///       return x < y;
    ///   };
    ///   sort(a.begin(), a.end(), [](int x, int y) { return x < y; });
    /// \endcode
    SLS_Inline,
    /// Merge all lambdas fitting on a single line.
    /// \code
    ///   auto lambda = [](int a) {};
    ///   auto lambda2 = [](int a) { return a; };
    /// \endcode
    SLS_All,
  };

  /// Dependent on the value, ``auto lambda []() { return 0; }`` can be put on a
  /// single line.
  /// \version 9
  ShortLambdaStyle AllowShortLambdasOnASingleLine;

  /// If ``true``, ``while (true) continue;`` can be put on a single
  /// line.
  /// \version 3.7
  bool AllowShortLoopsOnASingleLine;

  /// Different ways to break after the function definition return type.
  /// This option is **deprecated** and is retained for backwards compatibility.
  enum DefinitionReturnTypeBreakingStyle : int8_t {
    /// Break after return type automatically.
    /// ``PenaltyReturnTypeOnItsOwnLine`` is taken into account.
    DRTBS_None,
    /// Always break after the return type.
    DRTBS_All,
    /// Always break after the return types of top-level functions.
    DRTBS_TopLevel,
  };

  /// Different ways to break after the function definition or
  /// declaration return type.
  enum ReturnTypeBreakingStyle : int8_t {
    /// This is **deprecated**. See ``Automatic`` below.
    RTBS_None,
    /// Break after return type based on ``PenaltyReturnTypeOnItsOwnLine``.
    /// \code
    ///   class A {
    ///     int f() { return 0; };
    ///   };
    ///   int f();
    ///   int f() { return 1; }
    ///   int
    ///   LongName::AnotherLongName();
    /// \endcode
    RTBS_Automatic,
    /// Same as ``Automatic`` above, except that there is no break after short
    /// return types.
    /// \code
    ///   class A {
    ///     int f() { return 0; };
    ///   };
    ///   int f();
    ///   int f() { return 1; }
    ///   int LongName::
    ///       AnotherLongName();
    /// \endcode
    RTBS_ExceptShortType,
    /// Always break after the return type.
    /// \code
    ///   class A {
    ///     int
    ///     f() {
    ///       return 0;
    ///     };
    ///   };
    ///   int
    ///   f();
    ///   int
    ///   f() {
    ///     return 1;
    ///   }
    ///   int
    ///   LongName::AnotherLongName();
    /// \endcode
    RTBS_All,
    /// Always break after the return types of top-level functions.
    /// \code
    ///   class A {
    ///     int f() { return 0; };
    ///   };
    ///   int
    ///   f();
    ///   int
    ///   f() {
    ///     return 1;
    ///   }
    ///   int
    ///   LongName::AnotherLongName();
    /// \endcode
    RTBS_TopLevel,
    /// Always break after the return type of function definitions.
    /// \code
    ///   class A {
    ///     int
    ///     f() {
    ///       return 0;
    ///     };
    ///   };
    ///   int f();
    ///   int
    ///   f() {
    ///     return 1;
    ///   }
    ///   int
    ///   LongName::AnotherLongName();
    /// \endcode
    RTBS_AllDefinitions,
    /// Always break after the return type of top-level definitions.
    /// \code
    ///   class A {
    ///     int f() { return 0; };
    ///   };
    ///   int f();
    ///   int
    ///   f() {
    ///     return 1;
    ///   }
    ///   int
    ///   LongName::AnotherLongName();
    /// \endcode
    RTBS_TopLevelDefinitions,
  };

  /// The function definition return type breaking style to use.  This
  /// option is **deprecated** and is retained for backwards compatibility.
  /// \version 3.7
  DefinitionReturnTypeBreakingStyle AlwaysBreakAfterDefinitionReturnType;

  /// This option is renamed to ``BreakAfterReturnType``.
  /// \version 3.8
  /// @deprecated
  // ReturnTypeBreakingStyle AlwaysBreakAfterReturnType;

  /// If ``true``, always break before multiline string literals.
  ///
  /// This flag is mean to make cases where there are multiple multiline strings
  /// in a file look more consistent. Thus, it will only take effect if wrapping
  /// the string at that point leads to it being indented
  /// ``ContinuationIndentWidth`` spaces from the start of the line.
  /// \code
  ///    true:                                  false:
  ///    aaaa =                         vs.     aaaa = "bbbb"
  ///        "bbbb"                                    "cccc";
  ///        "cccc";
  /// \endcode
  /// \version 3.4
  bool AlwaysBreakBeforeMultilineStrings;

  /// Different ways to break after the template declaration.
  enum BreakTemplateDeclarationsStyle : int8_t {
    /// Do not change the line breaking before the declaration.
    /// \code
    ///    template <typename T>
    ///    T foo() {
    ///    }
    ///    template <typename T> T foo(int aaaaaaaaaaaaaaaaaaaaa,
    ///                                int bbbbbbbbbbbbbbbbbbbbb) {
    ///    }
    /// \endcode
    BTDS_Leave,
    /// Do not force break before declaration.
    /// ``PenaltyBreakTemplateDeclaration`` is taken into account.
    /// \code
    ///    template <typename T> T foo() {
    ///    }
    ///    template <typename T> T foo(int aaaaaaaaaaaaaaaaaaaaa,
    ///                                int bbbbbbbbbbbbbbbbbbbbb) {
    ///    }
    /// \endcode
    BTDS_No,
    /// Force break after template declaration only when the following
    /// declaration spans multiple lines.
    /// \code
    ///    template <typename T> T foo() {
    ///    }
    ///    template <typename T>
    ///    T foo(int aaaaaaaaaaaaaaaaaaaaa,
    ///          int bbbbbbbbbbbbbbbbbbbbb) {
    ///    }
    /// \endcode
    BTDS_MultiLine,
    /// Always break after template declaration.
    /// \code
    ///    template <typename T>
    ///    T foo() {
    ///    }
    ///    template <typename T>
    ///    T foo(int aaaaaaaaaaaaaaaaaaaaa,
    ///          int bbbbbbbbbbbbbbbbbbbbb) {
    ///    }
    /// \endcode
    BTDS_Yes
  };

  /// This option is renamed to ``BreakTemplateDeclarations``.
  /// \version 3.4
  /// @deprecated
  // BreakTemplateDeclarationsStyle AlwaysBreakTemplateDeclarations;

  /// A vector of strings that should be interpreted as attributes/qualifiers
  /// instead of identifiers. This can be useful for language extensions or
  /// static analyzer annotations.
  ///
  /// For example:
  /// \code
  ///   x = (char *__capability)&y;
  ///   int function(void) __unused;
  ///   void only_writes_to_buffer(char *__output buffer);
  /// \endcode
  ///
  /// In the .clang-format configuration file, this can be configured like:
  /// \code{.yaml}
  ///   AttributeMacros: [__capability, __output, __unused]
  /// \endcode
  ///
  /// \version 12
  std::vector<std::string> AttributeMacros;

  /// If ``false``, a function call's arguments will either be all on the
  /// same line or will have one line each.
  /// \code
  ///   true:
  ///   void f() {
  ///     f(aaaaaaaaaaaaaaaaaaaa, aaaaaaaaaaaaaaaaaaaa,
  ///       aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa);
  ///   }
  ///
  ///   false:
  ///   void f() {
  ///     f(aaaaaaaaaaaaaaaaaaaa,
  ///       aaaaaaaaaaaaaaaaaaaa,
  ///       aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa);
  ///   }
  /// \endcode
  /// \version 3.7
  bool BinPackArguments;

  /// If ``false``, a function declaration's or function definition's
  /// parameters will either all be on the same line or will have one line each.
  /// \code
  ///   true:
  ///   void f(int aaaaaaaaaaaaaaaaaaaa, int aaaaaaaaaaaaaaaaaaaa,
  ///          int aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa) {}
  ///
  ///   false:
  ///   void f(int aaaaaaaaaaaaaaaaaaaa,
  ///          int aaaaaaaaaaaaaaaaaaaa,
  ///          int aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa) {}
  /// \endcode
  /// \version 3.7
  bool BinPackParameters;

  /// Styles for adding spacing around ``:`` in bitfield definitions.
  enum BitFieldColonSpacingStyle : int8_t {
    /// Add one space on each side of the ``:``
    /// \code
    ///   unsigned bf : 2;
    /// \endcode
    BFCS_Both,
    /// Add no space around the ``:`` (except when needed for
    /// ``AlignConsecutiveBitFields``).
    /// \code
    ///   unsigned bf:2;
    /// \endcode
    BFCS_None,
    /// Add space before the ``:`` only
    /// \code
    ///   unsigned bf :2;
    /// \endcode
    BFCS_Before,
    /// Add space after the ``:`` only (space may be added before if
    /// needed for ``AlignConsecutiveBitFields``).
    /// \code
    ///   unsigned bf: 2;
    /// \endcode
    BFCS_After
  };
  /// The BitFieldColonSpacingStyle to use for bitfields.
  /// \version 12
  BitFieldColonSpacingStyle BitFieldColonSpacing;

  /// The number of columns to use to indent the contents of braced init lists.
  /// If unset, ``ContinuationIndentWidth`` is used.
  /// \code
  ///   AlignAfterOpenBracket: AlwaysBreak
  ///   BracedInitializerIndentWidth: 2
  ///
  ///   void f() {
  ///     SomeClass c{
  ///       "foo",
  ///       "bar",
  ///       "baz",
  ///     };
  ///     auto s = SomeStruct{
  ///       .foo = "foo",
  ///       .bar = "bar",
  ///       .baz = "baz",
  ///     };
  ///     SomeArrayT a[3] = {
  ///       {
  ///         foo,
  ///         bar,
  ///       },
  ///       {
  ///         foo,
  ///         bar,
  ///       },
  ///       SomeArrayT{},
  ///     };
  ///   }
  /// \endcode
  /// \version 17
  std::optional<unsigned> BracedInitializerIndentWidth;

  /// Different ways to wrap braces after control statements.
  enum BraceWrappingAfterControlStatementStyle : int8_t {
    /// Never wrap braces after a control statement.
    /// \code
    ///   if (foo()) {
    ///   } else {
    ///   }
    ///   for (int i = 0; i < 10; ++i) {
    ///   }
    /// \endcode
    BWACS_Never,
    /// Only wrap braces after a multi-line control statement.
    /// \code
    ///   if (foo && bar &&
    ///       baz)
    ///   {
    ///     quux();
    ///   }
    ///   while (foo || bar) {
    ///   }
    /// \endcode
    BWACS_MultiLine,
    /// Always wrap braces after a control statement.
    /// \code
    ///   if (foo())
    ///   {
    ///   } else
    ///   {}
    ///   for (int i = 0; i < 10; ++i)
    ///   {}
    /// \endcode
    BWACS_Always
  };

  /// Precise control over the wrapping of braces.
  /// \code
  ///   # Should be declared this way:
  ///   BreakBeforeBraces: Custom
  ///   BraceWrapping:
  ///       AfterClass: true
  /// \endcode
  struct BraceWrappingFlags {
    /// Wrap case labels.
    /// \code
    ///   false:                                true:
    ///   switch (foo) {                vs.     switch (foo) {
    ///     case 1: {                             case 1:
    ///       bar();                              {
    ///       break;                                bar();
    ///     }                                       break;
    ///     default: {                            }
    ///       plop();                             default:
    ///     }                                     {
    ///   }                                         plop();
    ///                                           }
    ///                                         }
    /// \endcode
    bool AfterCaseLabel;
    /// Wrap class definitions.
    /// \code
    ///   true:
    ///   class foo
    ///   {};
    ///
    ///   false:
    ///   class foo {};
    /// \endcode
    bool AfterClass;

    /// Wrap control statements (``if``/``for``/``while``/``switch``/..).
    BraceWrappingAfterControlStatementStyle AfterControlStatement;
    /// Wrap enum definitions.
    /// \code
    ///   true:
    ///   enum X : int
    ///   {
    ///     B
    ///   };
    ///
    ///   false:
    ///   enum X : int { B };
    /// \endcode
    bool AfterEnum;
    /// Wrap function definitions.
    /// \code
    ///   true:
    ///   void foo()
    ///   {
    ///     bar();
    ///     bar2();
    ///   }
    ///
    ///   false:
    ///   void foo() {
    ///     bar();
    ///     bar2();
    ///   }
    /// \endcode
    bool AfterFunction;
    /// Wrap namespace definitions.
    /// \code
    ///   true:
    ///   namespace
    ///   {
    ///   int foo();
    ///   int bar();
    ///   }
    ///
    ///   false:
    ///   namespace {
    ///   int foo();
    ///   int bar();
    ///   }
    /// \endcode
    bool AfterNamespace;
    /// Wrap ObjC definitions (interfaces, implementations...).
    /// \note
    ///  @autoreleasepool and @synchronized blocks are wrapped
    ///  according to ``AfterControlStatement`` flag.
    /// \endnote
    bool AfterObjCDeclaration;
    /// Wrap struct definitions.
    /// \code
    ///   true:
    ///   struct foo
    ///   {
    ///     int x;
    ///   };
    ///
    ///   false:
    ///   struct foo {
    ///     int x;
    ///   };
    /// \endcode
    bool AfterStruct;
    /// Wrap union definitions.
    /// \code
    ///   true:
    ///   union foo
    ///   {
    ///     int x;
    ///   }
    ///
    ///   false:
    ///   union foo {
    ///     int x;
    ///   }
    /// \endcode
    bool AfterUnion;
    /// Wrap extern blocks.
    /// \code
    ///   true:
    ///   extern "C"
    ///   {
    ///     int foo();
    ///   }
    ///
    ///   false:
    ///   extern "C" {
    ///   int foo();
    ///   }
    /// \endcode
    bool AfterExternBlock; // Partially superseded by IndentExternBlock
    /// Wrap before ``catch``.
    /// \code
    ///   true:
    ///   try {
    ///     foo();
    ///   }
    ///   catch () {
    ///   }
    ///
    ///   false:
    ///   try {
    ///     foo();
    ///   } catch () {
    ///   }
    /// \endcode
    bool BeforeCatch;
    /// Wrap before ``else``.
    /// \code
    ///   true:
    ///   if (foo()) {
    ///   }
    ///   else {
    ///   }
    ///
    ///   false:
    ///   if (foo()) {
    ///   } else {
    ///   }
    /// \endcode
    bool BeforeElse;
    /// Wrap lambda block.
    /// \code
    ///   true:
    ///   connect(
    ///     []()
    ///     {
    ///       foo();
    ///       bar();
    ///     });
    ///
    ///   false:
    ///   connect([]() {
    ///     foo();
    ///     bar();
    ///   });
    /// \endcode
    bool BeforeLambdaBody;
    /// Wrap before ``while``.
    /// \code
    ///   true:
    ///   do {
    ///     foo();
    ///   }
    ///   while (1);
    ///
    ///   false:
    ///   do {
    ///     foo();
    ///   } while (1);
    /// \endcode
    bool BeforeWhile;
    /// Indent the wrapped braces themselves.
    bool IndentBraces;
    /// If ``false``, empty function body can be put on a single line.
    /// This option is used only if the opening brace of the function has
    /// already been wrapped, i.e. the ``AfterFunction`` brace wrapping mode is
    /// set, and the function could/should not be put on a single line (as per
    /// ``AllowShortFunctionsOnASingleLine`` and constructor formatting
    /// options).
    /// \code
    ///   false:          true:
    ///   int f()   vs.   int f()
    ///   {}              {
    ///                   }
    /// \endcode
    ///
    bool SplitEmptyFunction;
    /// If ``false``, empty record (e.g. class, struct or union) body
    /// can be put on a single line. This option is used only if the opening
    /// brace of the record has already been wrapped, i.e. the ``AfterClass``
    /// (for classes) brace wrapping mode is set.
    /// \code
    ///   false:           true:
    ///   class Foo   vs.  class Foo
    ///   {}               {
    ///                    }
    /// \endcode
    ///
    bool SplitEmptyRecord;
    /// If ``false``, empty namespace body can be put on a single line.
    /// This option is used only if the opening brace of the namespace has
    /// already been wrapped, i.e. the ``AfterNamespace`` brace wrapping mode is
    /// set.
    /// \code
    ///   false:               true:
    ///   namespace Foo   vs.  namespace Foo
    ///   {}                   {
    ///                        }
    /// \endcode
    ///
    bool SplitEmptyNamespace;
  };

  /// Control of individual brace wrapping cases.
  ///
  /// If ``BreakBeforeBraces`` is set to ``BS_Custom``, use this to specify how
  /// each individual brace case should be handled. Otherwise, this is ignored.
  /// \code{.yaml}
  ///   # Example of usage:
  ///   BreakBeforeBraces: Custom
  ///   BraceWrapping:
  ///     AfterEnum: true
  ///     AfterStruct: false
  ///     SplitEmptyFunction: false
  /// \endcode
  /// \version 3.8
  BraceWrappingFlags BraceWrapping;

  /// Break between adjacent string literals.
  /// \code
  ///    true:
  ///    return "Code"
  ///           "\0\52\26\55\55\0"
  ///           "x013"
  ///           "\02\xBA";
  ///    false:
  ///    return "Code" "\0\52\26\55\55\0" "x013" "\02\xBA";
  /// \endcode
  /// \version 18
  bool BreakAdjacentStringLiterals;

  /// Different ways to break after attributes.
  enum AttributeBreakingStyle : int8_t {
    /// Always break after attributes.
    /// \code
    ///   [[maybe_unused]]
    ///   const int i;
    ///   [[gnu::const]] [[maybe_unused]]
    ///   int j;
    ///
    ///   [[nodiscard]]
    ///   inline int f();
    ///   [[gnu::const]] [[nodiscard]]
    ///   int g();
    ///
    ///   [[likely]]
    ///   if (a)
    ///     f();
    ///   else
    ///     g();
    ///
    ///   switch (b) {
    ///   [[unlikely]]
    ///   case 1:
    ///     ++b;
    ///     break;
    ///   [[likely]]
    ///   default:
    ///     return;
    ///   }
    /// \endcode
    ABS_Always,
    /// Leave the line breaking after attributes as is.
    /// \code
    ///   [[maybe_unused]] const int i;
    ///   [[gnu::const]] [[maybe_unused]]
    ///   int j;
    ///
    ///   [[nodiscard]] inline int f();
    ///   [[gnu::const]] [[nodiscard]]
    ///   int g();
    ///
    ///   [[likely]] if (a)
    ///     f();
    ///   else
    ///     g();
    ///
    ///   switch (b) {
    ///   [[unlikely]] case 1:
    ///     ++b;
    ///     break;
    ///   [[likely]]
    ///   default:
    ///     return;
    ///   }
    /// \endcode
    ABS_Leave,
    /// Never break after attributes.
    /// \code
    ///   [[maybe_unused]] const int i;
    ///   [[gnu::const]] [[maybe_unused]] int j;
    ///
    ///   [[nodiscard]] inline int f();
    ///   [[gnu::const]] [[nodiscard]] int g();
    ///
    ///   [[likely]] if (a)
    ///     f();
    ///   else
    ///     g();
    ///
    ///   switch (b) {
    ///   [[unlikely]] case 1:
    ///     ++b;
    ///     break;
    ///   [[likely]] default:
    ///     return;
    ///   }
    /// \endcode
    ABS_Never,
  };

  /// Break after a group of C++11 attributes before variable or function
  /// (including constructor/destructor) declaration/definition names or before
  /// control statements, i.e. ``if``, ``switch`` (including ``case`` and
  /// ``default`` labels), ``for``, and ``while`` statements.
  /// \version 16
  AttributeBreakingStyle BreakAfterAttributes;

  /// The function declaration return type breaking style to use.
  /// \version 19
  ReturnTypeBreakingStyle BreakAfterReturnType;

  /// If ``true``, clang-format will always break after a Json array ``[``
  /// otherwise it will scan until the closing ``]`` to determine if it should
  /// add newlines between elements (prettier compatible).
  ///
  /// \note
  ///  This is currently only for formatting JSON.
  /// \endnote
  /// \code
  ///    true:                                  false:
  ///    [                          vs.      [1, 2, 3, 4]
  ///      1,
  ///      2,
  ///      3,
  ///      4
  ///    ]
  /// \endcode
  /// \version 16
  bool BreakArrays;

  /// The style of wrapping parameters on the same line (bin-packed) or
  /// on one line each.
  enum BinPackStyle : int8_t {
    /// Automatically determine parameter bin-packing behavior.
    BPS_Auto,
    /// Always bin-pack parameters.
    BPS_Always,
    /// Never bin-pack parameters.
    BPS_Never,
  };

  /// The style of breaking before or after binary operators.
  enum BinaryOperatorStyle : int8_t {
    /// Break after operators.
    /// \code
    ///    LooooooooooongType loooooooooooooooooooooongVariable =
    ///        someLooooooooooooooooongFunction();
    ///
    ///    bool value = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa +
    ///                         aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa ==
    ///                     aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa &&
    ///                 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa >
    ///                     ccccccccccccccccccccccccccccccccccccccccc;
    /// \endcode
    BOS_None,
    /// Break before operators that aren't assignments.
    /// \code
    ///    LooooooooooongType loooooooooooooooooooooongVariable =
    ///        someLooooooooooooooooongFunction();
    ///
    ///    bool value = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    ///                         + aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    ///                     == aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    ///                 && aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    ///                        > ccccccccccccccccccccccccccccccccccccccccc;
    /// \endcode
    BOS_NonAssignment,
    /// Break before operators.
    /// \code
    ///    LooooooooooongType loooooooooooooooooooooongVariable
    ///        = someLooooooooooooooooongFunction();
    ///
    ///    bool value = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    ///                         + aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    ///                     == aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    ///                 && aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    ///                        > ccccccccccccccccccccccccccccccccccccccccc;
    /// \endcode
    BOS_All,
  };

  /// The way to wrap binary operators.
  /// \version 3.6
  BinaryOperatorStyle BreakBeforeBinaryOperators;

  /// Different ways to attach braces to their surrounding context.
  enum BraceBreakingStyle : int8_t {
    /// Always attach braces to surrounding context.
    /// \code
    ///   namespace N {
    ///   enum E {
    ///     E1,
    ///     E2,
    ///   };
    ///
    ///   class C {
    ///   public:
    ///     C();
    ///   };
    ///
    ///   bool baz(int i) {
    ///     try {
    ///       do {
    ///         switch (i) {
    ///         case 1: {
    ///           foobar();
    ///           break;
    ///         }
    ///         default: {
    ///           break;
    ///         }
    ///         }
    ///       } while (--i);
    ///       return true;
    ///     } catch (...) {
    ///       handleError();
    ///       return false;
    ///     }
    ///   }
    ///
    ///   void foo(bool b) {
    ///     if (b) {
    ///       baz(2);
    ///     } else {
    ///       baz(5);
    ///     }
    ///   }
    ///
    ///   void bar() { foo(true); }
    ///   } // namespace N
    /// \endcode
    BS_Attach,
    /// Like ``Attach``, but break before braces on function, namespace and
    /// class definitions.
    /// \code
    ///   namespace N
    ///   {
    ///   enum E {
    ///     E1,
    ///     E2,
    ///   };
    ///
    ///   class C
    ///   {
    ///   public:
    ///     C();
    ///   };
    ///
    ///   bool baz(int i)
    ///   {
    ///     try {
    ///       do {
    ///         switch (i) {
    ///         case 1: {
    ///           foobar();
    ///           break;
    ///         }
    ///         default: {
    ///           break;
    ///         }
    ///         }
    ///       } while (--i);
    ///       return true;
    ///     } catch (...) {
    ///       handleError();
    ///       return false;
    ///     }
    ///   }
    ///
    ///   void foo(bool b)
    ///   {
    ///     if (b) {
    ///       baz(2);
    ///     } else {
    ///       baz(5);
    ///     }
    ///   }
    ///
    ///   void bar() { foo(true); }
    ///   } // namespace N
    /// \endcode
    BS_Linux,
    /// Like ``Attach``, but break before braces on enum, function, and record
    /// definitions.
    /// \code
    ///   namespace N {
    ///   enum E
    ///   {
    ///     E1,
    ///     E2,
    ///   };
    ///
    ///   class C
    ///   {
    ///   public:
    ///     C();
    ///   };
    ///
    ///   bool baz(int i)
    ///   {
    ///     try {
    ///       do {
    ///         switch (i) {
    ///         case 1: {
    ///           foobar();
    ///           break;
    ///         }
    ///         default: {
    ///           break;
    ///         }
    ///         }
    ///       } while (--i);
    ///       return true;
    ///     } catch (...) {
    ///       handleError();
    ///       return false;
    ///     }
    ///   }
    ///
    ///   void foo(bool b)
    ///   {
    ///     if (b) {
    ///       baz(2);
    ///     } else {
    ///       baz(5);
    ///     }
    ///   }
    ///
    ///   void bar() { foo(true); }
    ///   } // namespace N
    /// \endcode
    BS_Mozilla,
    /// Like ``Attach``, but break before function definitions, ``catch``, and
    /// ``else``.
    /// \code
    ///   namespace N {
    ///   enum E {
    ///     E1,
    ///     E2,
    ///   };
    ///
    ///   class C {
    ///   public:
    ///     C();
    ///   };
    ///
    ///   bool baz(int i)
    ///   {
    ///     try {
    ///       do {
    ///         switch (i) {
    ///         case 1: {
    ///           foobar();
    ///           break;
    ///         }
    ///         default: {
    ///           break;
    ///         }
    ///         }
    ///       } while (--i);
    ///       return true;
    ///     }
    ///     catch (...) {
    ///       handleError();
    ///       return false;
    ///     }
    ///   }
    ///
    ///   void foo(bool b)
    ///   {
    ///     if (b) {
    ///       baz(2);
    ///     }
    ///     else {
    ///       baz(5);
    ///     }
    ///   }
    ///
    ///   void bar() { foo(true); }
    ///   } // namespace N
    /// \endcode
    BS_Stroustrup,
    /// Always break before braces.
    /// \code
    ///   namespace N
    ///   {
    ///   enum E
    ///   {
    ///     E1,
    ///     E2,
    ///   };
    ///
    ///   class C
    ///   {
    ///   public:
    ///     C();
    ///   };
    ///
    ///   bool baz(int i)
    ///   {
    ///     try
    ///     {
    ///       do
    ///       {
    ///         switch (i)
    ///         {
    ///         case 1:
    ///         {
    ///           foobar();
    ///           break;
    ///         }
    ///         default:
    ///         {
    ///           break;
    ///         }
    ///         }
    ///       } while (--i);
    ///       return true;
    ///     }
    ///     catch (...)
    ///     {
    ///       handleError();
    ///       return false;
    ///     }
    ///   }
    ///
    ///   void foo(bool b)
    ///   {
    ///     if (b)
    ///     {
    ///       baz(2);
    ///     }
    ///     else
    ///     {
    ///       baz(5);
    ///     }
    ///   }
    ///
    ///   void bar() { foo(true); }
    ///   } // namespace N
    /// \endcode
    BS_Allman,
    /// Like ``Allman`` but always indent braces and line up code with braces.
    /// \code
    ///   namespace N
    ///     {
    ///   enum E
    ///     {
    ///     E1,
    ///     E2,
    ///     };
    ///
    ///   class C
    ///     {
    ///   public:
    ///     C();
    ///     };
    ///
    ///   bool baz(int i)
    ///     {
    ///     try
    ///       {
    ///       do
    ///         {
    ///         switch (i)
    ///           {
    ///           case 1:
    ///           {
    ///           foobar();
    ///           break;
    ///           }
    ///           default:
    ///           {
    ///           break;
    ///           }
    ///           }
    ///         } while (--i);
    ///       return true;
    ///       }
    ///     catch (...)
    ///       {
    ///       handleError();
    ///       return false;
    ///       }
    ///     }
    ///
    ///   void foo(bool b)
    ///     {
    ///     if (b)
    ///       {
    ///       baz(2);
    ///       }
    ///     else
    ///       {
    ///       baz(5);
    ///       }
    ///     }
    ///
    ///   void bar() { foo(true); }
    ///     } // namespace N
    /// \endcode
    BS_Whitesmiths,
    /// Always break before braces and add an extra level of indentation to
    /// braces of control statements, not to those of class, function
    /// or other definitions.
    /// \code
    ///   namespace N
    ///   {
    ///   enum E
    ///   {
    ///     E1,
    ///     E2,
    ///   };
    ///
    ///   class C
    ///   {
    ///   public:
    ///     C();
    ///   };
    ///
    ///   bool baz(int i)
    ///   {
    ///     try
    ///       {
    ///         do
    ///           {
    ///             switch (i)
    ///               {
    ///               case 1:
    ///                 {
    ///                   foobar();
    ///                   break;
    ///                 }
    ///               default:
    ///                 {
    ///                   break;
    ///                 }
    ///               }
    ///           }
    ///         while (--i);
    ///         return true;
    ///       }
    ///     catch (...)
    ///       {
    ///         handleError();
    ///         return false;
    ///       }
    ///   }
    ///
    ///   void foo(bool b)
    ///   {
    ///     if (b)
    ///       {
    ///         baz(2);
    ///       }
    ///     else
    ///       {
    ///         baz(5);
    ///       }
    ///   }
    ///
    ///   void bar() { foo(true); }
    ///   } // namespace N
    /// \endcode
    BS_GNU,
    /// Like ``Attach``, but break before functions.
    /// \code
    ///   namespace N {
    ///   enum E {
    ///     E1,
    ///     E2,
    ///   };
    ///
    ///   class C {
    ///   public:
    ///     C();
    ///   };
    ///
    ///   bool baz(int i)
    ///   {
    ///     try {
    ///       do {
    ///         switch (i) {
    ///         case 1: {
    ///           foobar();
    ///           break;
    ///         }
    ///         default: {
    ///           break;
    ///         }
    ///         }
    ///       } while (--i);
    ///       return true;
    ///     } catch (...) {
    ///       handleError();
    ///       return false;
    ///     }
    ///   }
    ///
    ///   void foo(bool b)
    ///   {
    ///     if (b) {
    ///       baz(2);
    ///     } else {
    ///       baz(5);
    ///     }
    ///   }
    ///
    ///   void bar() { foo(true); }
    ///   } // namespace N
    /// \endcode
    BS_WebKit,
    /// Configure each individual brace in ``BraceWrapping``.
    BS_Custom
  };

  /// The brace breaking style to use.
  /// \version 3.7
  BraceBreakingStyle BreakBeforeBraces;

  /// Different ways to break before concept declarations.
  enum BreakBeforeConceptDeclarationsStyle : int8_t {
    /// Keep the template declaration line together with ``concept``.
    /// \code
    ///   template <typename T> concept C = ...;
    /// \endcode
    BBCDS_Never,
    /// Breaking between template declaration and ``concept`` is allowed. The
    /// actual behavior depends on the content and line breaking rules and
    /// penalties.
    BBCDS_Allowed,
    /// Always break before ``concept``, putting it in the line after the
    /// template declaration.
    /// \code
    ///   template <typename T>
    ///   concept C = ...;
    /// \endcode
    BBCDS_Always,
  };

  /// The concept declaration style to use.
  /// \version 12
  BreakBeforeConceptDeclarationsStyle BreakBeforeConceptDeclarations;

  /// Different ways to break ASM parameters.
  enum BreakBeforeInlineASMColonStyle : int8_t {
    /// No break before inline ASM colon.
    /// \code
    ///    asm volatile("string", : : val);
    /// \endcode
    BBIAS_Never,
    /// Break before inline ASM colon if the line length is longer than column
    /// limit.
    /// \code
    ///    asm volatile("string", : : val);
    ///    asm("cmoveq %1, %2, %[result]"
    ///        : [result] "=r"(result)
    ///        : "r"(test), "r"(new), "[result]"(old));
    /// \endcode
    BBIAS_OnlyMultiline,
    /// Always break before inline ASM colon.
    /// \code
    ///    asm volatile("string",
    ///                 :
    ///                 : val);
    /// \endcode
    BBIAS_Always,
  };

  /// The inline ASM colon style to use.
  /// \version 16
  BreakBeforeInlineASMColonStyle BreakBeforeInlineASMColon;

  /// If ``true``, ternary operators will be placed after line breaks.
  /// \code
  ///    true:
  ///    veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongDescription
  ///        ? firstValue
  ///        : SecondValueVeryVeryVeryVeryLong;
  ///
  ///    false:
  ///    veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongDescription ?
  ///        firstValue :
  ///        SecondValueVeryVeryVeryVeryLong;
  /// \endcode
  /// \version 3.7
  bool BreakBeforeTernaryOperators;

  /// Different ways to break initializers.
  enum BreakConstructorInitializersStyle : int8_t {
    /// Break constructor initializers before the colon and after the commas.
    /// \code
    ///    Constructor()
    ///        : initializer1(),
    ///          initializer2()
    /// \endcode
    BCIS_BeforeColon,
    /// Break constructor initializers before the colon and commas, and align
    /// the commas with the colon.
    /// \code
    ///    Constructor()
    ///        : initializer1()
    ///        , initializer2()
    /// \endcode
    BCIS_BeforeComma,
    /// Break constructor initializers after the colon and commas.
    /// \code
    ///    Constructor() :
    ///        initializer1(),
    ///        initializer2()
    /// \endcode
    BCIS_AfterColon
  };

  /// The break constructor initializers style to use.
  /// \version 5
  BreakConstructorInitializersStyle BreakConstructorInitializers;

  /// If ``true``, clang-format will always break before function definition
  /// parameters.
  /// \code
  ///    true:
  ///    void functionDefinition(
  ///             int A, int B) {}
  ///
  ///    false:
  ///    void functionDefinition(int A, int B) {}
  ///
  /// \endcode
  /// \version 19
  bool BreakFunctionDefinitionParameters;

  /// Break after each annotation on a field in Java files.
  /// \code{.java}
  ///    true:                                  false:
  ///    @Partial                       vs.     @Partial @Mock DataLoad loader;
  ///    @Mock
  ///    DataLoad loader;
  /// \endcode
  /// \version 3.8
  bool BreakAfterJavaFieldAnnotations;

  /// Allow breaking string literals when formatting.
  ///
  /// In C, C++, and Objective-C:
  /// \code
  ///    true:
  ///    const char* x = "veryVeryVeryVeryVeryVe"
  ///                    "ryVeryVeryVeryVeryVery"
  ///                    "VeryLongString";
  ///
  ///    false:
  ///    const char* x =
  ///        "veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongString";
  /// \endcode
  ///
  /// In C# and Java:
  /// \code
  ///    true:
  ///    string x = "veryVeryVeryVeryVeryVe" +
  ///               "ryVeryVeryVeryVeryVery" +
  ///               "VeryLongString";
  ///
  ///    false:
  ///    string x =
  ///        "veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongString";
  /// \endcode
  ///
  /// C# interpolated strings are not broken.
  ///
  /// In Verilog:
  /// \code
  ///    true:
  ///    string x = {"veryVeryVeryVeryVeryVe",
  ///                "ryVeryVeryVeryVeryVery",
  ///                "VeryLongString"};
  ///
  ///    false:
  ///    string x =
  ///        "veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongString";
  /// \endcode
  ///
  /// \version 3.9
  bool BreakStringLiterals;

  /// The column limit.
  ///
  /// A column limit of ``0`` means that there is no column limit. In this case,
  /// clang-format will respect the input's line breaking decisions within
  /// statements unless they contradict other rules.
  /// \version 3.7
  unsigned ColumnLimit;

  /// A regular expression that describes comments with special meaning,
  /// which should not be split into lines or otherwise changed.
  /// \code
  ///    // CommentPragmas: '^ FOOBAR pragma:'
  ///    // Will leave the following line unaffected
  ///    #include <vector> // FOOBAR pragma: keep
  /// \endcode
  /// \version 3.7
  std::string CommentPragmas;

  /// Different ways to break inheritance list.
  enum BreakInheritanceListStyle : int8_t {
    /// Break inheritance list before the colon and after the commas.
    /// \code
    ///    class Foo
    ///        : Base1,
    ///          Base2
    ///    {};
    /// \endcode
    BILS_BeforeColon,
    /// Break inheritance list before the colon and commas, and align
    /// the commas with the colon.
    /// \code
    ///    class Foo
    ///        : Base1
    ///        , Base2
    ///    {};
    /// \endcode
    BILS_BeforeComma,
    /// Break inheritance list after the colon and commas.
    /// \code
    ///    class Foo :
    ///        Base1,
    ///        Base2
    ///    {};
    /// \endcode
    BILS_AfterColon,
    /// Break inheritance list only after the commas.
    /// \code
    ///    class Foo : Base1,
    ///                Base2
    ///    {};
    /// \endcode
    BILS_AfterComma,
  };

  /// The inheritance list style to use.
  /// \version 7
  BreakInheritanceListStyle BreakInheritanceList;

  /// The template declaration breaking style to use.
  /// \version 19
  BreakTemplateDeclarationsStyle BreakTemplateDeclarations;

  /// If ``true``, consecutive namespace declarations will be on the same
  /// line. If ``false``, each namespace is declared on a new line.
  /// \code
  ///   true:
  ///   namespace Foo { namespace Bar {
  ///   }}
  ///
  ///   false:
  ///   namespace Foo {
  ///   namespace Bar {
  ///   }
  ///   }
  /// \endcode
  ///
  /// If it does not fit on a single line, the overflowing namespaces get
  /// wrapped:
  /// \code
  ///   namespace Foo { namespace Bar {
  ///   namespace Extra {
  ///   }}}
  /// \endcode
  /// \version 5
  bool CompactNamespaces;

  /// This option is **deprecated**. See ``CurrentLine`` of
  /// ``PackConstructorInitializers``.
  /// \version 3.7
  // bool ConstructorInitializerAllOnOneLineOrOnePerLine;

  /// The number of characters to use for indentation of constructor
  /// initializer lists as well as inheritance lists.
  /// \version 3.7
  unsigned ConstructorInitializerIndentWidth;

  /// Indent width for line continuations.
  /// \code
  ///    ContinuationIndentWidth: 2
  ///
  ///    int i =         //  VeryVeryVeryVeryVeryLongComment
  ///      longFunction( // Again a long comment
  ///        arg);
  /// \endcode
  /// \version 3.7
  unsigned ContinuationIndentWidth;

  /// If ``true``, format braced lists as best suited for C++11 braced
  /// lists.
  ///
  /// Important differences:
  /// - No spaces inside the braced list.
  /// - No line break before the closing brace.
  /// - Indentation with the continuation indent, not with the block indent.
  ///
  /// Fundamentally, C++11 braced lists are formatted exactly like function
  /// calls would be formatted in their place. If the braced list follows a name
  /// (e.g. a type or variable name), clang-format formats as if the ``{}`` were
  /// the parentheses of a function call with that name. If there is no name,
  /// a zero-length name is assumed.
  /// \code
  ///    true:                                  false:
  ///    vector<int> x{1, 2, 3, 4};     vs.     vector<int> x{ 1, 2, 3, 4 };
  ///    vector<T> x{{}, {}, {}, {}};           vector<T> x{ {}, {}, {}, {} };
  ///    f(MyMap[{composite, key}]);            f(MyMap[{ composite, key }]);
  ///    new int[3]{1, 2, 3};                   new int[3]{ 1, 2, 3 };
  /// \endcode
  /// \version 3.4
  bool Cpp11BracedListStyle;

  /// This option is **deprecated**. See ``DeriveLF`` and ``DeriveCRLF`` of
  /// ``LineEnding``.
  /// \version 10
  // bool DeriveLineEnding;

  /// If ``true``, analyze the formatted file for the most common
  /// alignment of ``&`` and ``*``.
  /// Pointer and reference alignment styles are going to be updated according
  /// to the preferences found in the file.
  /// ``PointerAlignment`` is then used only as fallback.
  /// \version 3.7
  bool DerivePointerAlignment;

  /// Disables formatting completely.
  /// \version 3.7
  bool DisableFormat;

  /// Different styles for empty line after access modifiers.
  /// ``EmptyLineBeforeAccessModifier`` configuration handles the number of
  /// empty lines between two access modifiers.
  enum EmptyLineAfterAccessModifierStyle : int8_t {
    /// Remove all empty lines after access modifiers.
    /// \code
    ///   struct foo {
    ///   private:
    ///     int i;
    ///   protected:
    ///     int j;
    ///     /* comment */
    ///   public:
    ///     foo() {}
    ///   private:
    ///   protected:
    ///   };
    /// \endcode
    ELAAMS_Never,
    /// Keep existing empty lines after access modifiers.
    /// MaxEmptyLinesToKeep is applied instead.
    ELAAMS_Leave,
    /// Always add empty line after access modifiers if there are none.
    /// MaxEmptyLinesToKeep is applied also.
    /// \code
    ///   struct foo {
    ///   private:
    ///
    ///     int i;
    ///   protected:
    ///
    ///     int j;
    ///     /* comment */
    ///   public:
    ///
    ///     foo() {}
    ///   private:
    ///
    ///   protected:
    ///
    ///   };
    /// \endcode
    ELAAMS_Always,
  };

  /// Defines when to put an empty line after access modifiers.
  /// ``EmptyLineBeforeAccessModifier`` configuration handles the number of
  /// empty lines between two access modifiers.
  /// \version 13
  EmptyLineAfterAccessModifierStyle EmptyLineAfterAccessModifier;

  /// Different styles for empty line before access modifiers.
  enum EmptyLineBeforeAccessModifierStyle : int8_t {
    /// Remove all empty lines before access modifiers.
    /// \code
    ///   struct foo {
    ///   private:
    ///     int i;
    ///   protected:
    ///     int j;
    ///     /* comment */
    ///   public:
    ///     foo() {}
    ///   private:
    ///   protected:
    ///   };
    /// \endcode
    ELBAMS_Never,
    /// Keep existing empty lines before access modifiers.
    ELBAMS_Leave,
    /// Add empty line only when access modifier starts a new logical block.
    /// Logical block is a group of one or more member fields or functions.
    /// \code
    ///   struct foo {
    ///   private:
    ///     int i;
    ///
    ///   protected:
    ///     int j;
    ///     /* comment */
    ///   public:
    ///     foo() {}
    ///
    ///   private:
    ///   protected:
    ///   };
    /// \endcode
    ELBAMS_LogicalBlock,
    /// Always add empty line before access modifiers unless access modifier
    /// is at the start of struct or class definition.
    /// \code
    ///   struct foo {
    ///   private:
    ///     int i;
    ///
    ///   protected:
    ///     int j;
    ///     /* comment */
    ///
    ///   public:
    ///     foo() {}
    ///
    ///   private:
    ///
    ///   protected:
    ///   };
    /// \endcode
    ELBAMS_Always,
  };

  /// Defines in which cases to put empty line before access modifiers.
  /// \version 12
  EmptyLineBeforeAccessModifierStyle EmptyLineBeforeAccessModifier;

  /// If ``true``, clang-format detects whether function calls and
  /// definitions are formatted with one parameter per line.
  ///
  /// Each call can be bin-packed, one-per-line or inconclusive. If it is
  /// inconclusive, e.g. completely on one line, but a decision needs to be
  /// made, clang-format analyzes whether there are other bin-packed cases in
  /// the input file and act accordingly.
  ///
  /// \note
  ///  This is an experimental flag, that might go away or be renamed. Do
  ///  not use this in config files, etc. Use at your own risk.
  /// \endnote
  /// \version 3.7
  bool ExperimentalAutoDetectBinPacking;

  /// If ``true``, clang-format adds missing namespace end comments for
  /// namespaces and fixes invalid existing ones. This doesn't affect short
  /// namespaces, which are controlled by ``ShortNamespaceLines``.
  /// \code
  ///    true:                                  false:
  ///    namespace longNamespace {      vs.     namespace longNamespace {
  ///    void foo();                            void foo();
  ///    void bar();                            void bar();
  ///    } // namespace a                       }
  ///    namespace shortNamespace {             namespace shortNamespace {
  ///    void baz();                            void baz();
  ///    }                                      }
  /// \endcode
  /// \version 5
  bool FixNamespaceComments;

  /// A vector of macros that should be interpreted as foreach loops
  /// instead of as function calls.
  ///
  /// These are expected to be macros of the form:
  /// \code
  ///   FOREACH(<variable-declaration>, ...)
  ///     <loop-body>
  /// \endcode
  ///
  /// In the .clang-format configuration file, this can be configured like:
  /// \code{.yaml}
  ///   ForEachMacros: [RANGES_FOR, FOREACH]
  /// \endcode
  ///
  /// For example: BOOST_FOREACH.
  /// \version 3.7
  std::vector<std::string> ForEachMacros;

  tooling::IncludeStyle IncludeStyle;

  /// A vector of macros that should be interpreted as conditionals
  /// instead of as function calls.
  ///
  /// These are expected to be macros of the form:
  /// \code
  ///   IF(...)
  ///     <conditional-body>
  ///   else IF(...)
  ///     <conditional-body>
  /// \endcode
  ///
  /// In the .clang-format configuration file, this can be configured like:
  /// \code{.yaml}
  ///   IfMacros: [IF]
  /// \endcode
  ///
  /// For example: `KJ_IF_MAYBE
  /// <https://github.com/capnproto/capnproto/blob/master/kjdoc/tour.md#maybes>`_
  /// \version 13
  std::vector<std::string> IfMacros;

  /// Specify whether access modifiers should have their own indentation level.
  ///
  /// When ``false``, access modifiers are indented (or outdented) relative to
  /// the record members, respecting the ``AccessModifierOffset``. Record
  /// members are indented one level below the record.
  /// When ``true``, access modifiers get their own indentation level. As a
  /// consequence, record members are always indented 2 levels below the record,
  /// regardless of the access modifier presence. Value of the
  /// ``AccessModifierOffset`` is ignored.
  /// \code
  ///    false:                                 true:
  ///    class C {                      vs.     class C {
  ///      class D {                                class D {
  ///        void bar();                                void bar();
  ///      protected:                                 protected:
  ///        D();                                       D();
  ///      };                                       };
  ///    public:                                  public:
  ///      C();                                     C();
  ///    };                                     };
  ///    void foo() {                           void foo() {
  ///      return 1;                              return 1;
  ///    }                                      }
  /// \endcode
  /// \version 13
  bool IndentAccessModifiers;

  /// Indent case label blocks one level from the case label.
  ///
  /// When ``false``, the block following the case label uses the same
  /// indentation level as for the case label, treating the case label the same
  /// as an if-statement.
  /// When ``true``, the block gets indented as a scope block.
  /// \code
  ///    false:                                 true:
  ///    switch (fool) {                vs.     switch (fool) {
  ///    case 1: {                              case 1:
  ///      bar();                                 {
  ///    } break;                                   bar();
  ///    default: {                               }
  ///      plop();                                break;
  ///    }                                      default:
  ///    }                                        {
  ///                                               plop();
  ///                                             }
  ///                                           }
  /// \endcode
  /// \version 11
  bool IndentCaseBlocks;

  /// Indent case labels one level from the switch statement.
  ///
  /// When ``false``, use the same indentation level as for the switch
  /// statement. Switch statement body is always indented one level more than
  /// case labels (except the first block following the case label, which
  /// itself indents the code - unless IndentCaseBlocks is enabled).
  /// \code
  ///    false:                                 true:
  ///    switch (fool) {                vs.     switch (fool) {
  ///    case 1:                                  case 1:
  ///      bar();                                   bar();
  ///      break;                                   break;
  ///    default:                                 default:
  ///      plop();                                  plop();
  ///    }                                      }
  /// \endcode
  /// \version 3.3
  bool IndentCaseLabels;

  /// Indent goto labels.
  ///
  /// When ``false``, goto labels are flushed left.
  /// \code
  ///    true:                                  false:
  ///    int f() {                      vs.     int f() {
  ///      if (foo()) {                           if (foo()) {
  ///      label1:                              label1:
  ///        bar();                                 bar();
  ///      }                                      }
  ///    label2:                                label2:
  ///      return 1;                              return 1;
  ///    }                                      }
  /// \endcode
  /// \version 10
  bool IndentGotoLabels;

  /// Indents extern blocks
  enum IndentExternBlockStyle : int8_t {
    /// Backwards compatible with AfterExternBlock's indenting.
    /// \code
    ///    IndentExternBlock: AfterExternBlock
    ///    BraceWrapping.AfterExternBlock: true
    ///    extern "C"
    ///    {
    ///        void foo();
    ///    }
    /// \endcode
    ///
    /// \code
    ///    IndentExternBlock: AfterExternBlock
    ///    BraceWrapping.AfterExternBlock: false
    ///    extern "C" {
    ///    void foo();
    ///    }
    /// \endcode
    IEBS_AfterExternBlock,
    /// Does not indent extern blocks.
    /// \code
    ///     extern "C" {
    ///     void foo();
    ///     }
    /// \endcode
    IEBS_NoIndent,
    /// Indents extern blocks.
    /// \code
    ///     extern "C" {
    ///       void foo();
    ///     }
    /// \endcode
    IEBS_Indent,
  };

  /// IndentExternBlockStyle is the type of indenting of extern blocks.
  /// \version 11
  IndentExternBlockStyle IndentExternBlock;

  /// Options for indenting preprocessor directives.
  enum PPDirectiveIndentStyle : int8_t {
    /// Does not indent any directives.
    /// \code
    ///    #if FOO
    ///    #if BAR
    ///    #include <foo>
    ///    #endif
    ///    #endif
    /// \endcode
    PPDIS_None,
    /// Indents directives after the hash.
    /// \code
    ///    #if FOO
    ///    #  if BAR
    ///    #    include <foo>
    ///    #  endif
    ///    #endif
    /// \endcode
    PPDIS_AfterHash,
    /// Indents directives before the hash.
    /// \code
    ///    #if FOO
    ///      #if BAR
    ///        #include <foo>
    ///      #endif
    ///    #endif
    /// \endcode
    PPDIS_BeforeHash
  };

  /// The preprocessor directive indenting style to use.
  /// \version 6
  PPDirectiveIndentStyle IndentPPDirectives;

  /// Indent the requires clause in a template. This only applies when
  /// ``RequiresClausePosition`` is ``OwnLine``, or ``WithFollowing``.
  ///
  /// In clang-format 12, 13 and 14 it was named ``IndentRequires``.
  /// \code
  ///    true:
  ///    template <typename It>
  ///      requires Iterator<It>
  ///    void sort(It begin, It end) {
  ///      //....
  ///    }
  ///
  ///    false:
  ///    template <typename It>
  ///    requires Iterator<It>
  ///    void sort(It begin, It end) {
  ///      //....
  ///    }
  /// \endcode
  /// \version 15
  bool IndentRequiresClause;

  /// The number of columns to use for indentation.
  /// \code
  ///    IndentWidth: 3
  ///
  ///    void f() {
  ///       someFunction();
  ///       if (true, false) {
  ///          f();
  ///       }
  ///    }
  /// \endcode
  /// \version 3.7
  unsigned IndentWidth;

  /// Indent if a function definition or declaration is wrapped after the
  /// type.
  /// \code
  ///    true:
  ///    LoooooooooooooooooooooooooooooooooooooooongReturnType
  ///        LoooooooooooooooooooooooooooooooongFunctionDeclaration();
  ///
  ///    false:
  ///    LoooooooooooooooooooooooooooooooooooooooongReturnType
  ///    LoooooooooooooooooooooooooooooooongFunctionDeclaration();
  /// \endcode
  /// \version 3.7
  bool IndentWrappedFunctionNames;

  /// Insert braces after control statements (``if``, ``else``, ``for``, ``do``,
  /// and ``while``) in C++ unless the control statements are inside macro
  /// definitions or the braces would enclose preprocessor directives.
  /// \warning
  ///  Setting this option to ``true`` could lead to incorrect code formatting
  ///  due to clang-format's lack of complete semantic information. As such,
  ///  extra care should be taken to review code changes made by this option.
  /// \endwarning
  /// \code
  ///   false:                                    true:
  ///
  ///   if (isa<FunctionDecl>(D))        vs.      if (isa<FunctionDecl>(D)) {
  ///     handleFunctionDecl(D);                    handleFunctionDecl(D);
  ///   else if (isa<VarDecl>(D))                 } else if (isa<VarDecl>(D)) {
  ///     handleVarDecl(D);                         handleVarDecl(D);
  ///   else                                      } else {
  ///     return;                                   return;
  ///                                             }
  ///
  ///   while (i--)                      vs.      while (i--) {
  ///     for (auto *A : D.attrs())                 for (auto *A : D.attrs()) {
  ///       handleAttr(A);                            handleAttr(A);
  ///                                               }
  ///                                             }
  ///
  ///   do                               vs.      do {
  ///     --i;                                      --i;
  ///   while (i);                                } while (i);
  /// \endcode
  /// \version 15
  bool InsertBraces;

  /// Insert a newline at end of file if missing.
  /// \version 16
  bool InsertNewlineAtEOF;

  /// The style of inserting trailing commas into container literals.
  enum TrailingCommaStyle : int8_t {
    /// Do not insert trailing commas.
    TCS_None,
    /// Insert trailing commas in container literals that were wrapped over
    /// multiple lines. Note that this is conceptually incompatible with
    /// bin-packing, because the trailing comma is used as an indicator
    /// that a container should be formatted one-per-line (i.e. not bin-packed).
    /// So inserting a trailing comma counteracts bin-packing.
    TCS_Wrapped,
  };

  /// If set to ``TCS_Wrapped`` will insert trailing commas in container
  /// literals (arrays and objects) that wrap across multiple lines.
  /// It is currently only available for JavaScript
  /// and disabled by default ``TCS_None``.
  /// ``InsertTrailingCommas`` cannot be used together with ``BinPackArguments``
  /// as inserting the comma disables bin-packing.
  /// \code
  ///   TSC_Wrapped:
  ///   const someArray = [
  ///   aaaaaaaaaaaaaaaaaaaaaaaaaa,
  ///   aaaaaaaaaaaaaaaaaaaaaaaaaa,
  ///   aaaaaaaaaaaaaaaaaaaaaaaaaa,
  ///   //                        ^ inserted
  ///   ]
  /// \endcode
  /// \version 11
  TrailingCommaStyle InsertTrailingCommas;

  /// Separator format of integer literals of different bases.
  ///
  /// If negative, remove separators. If  ``0``, leave the literal as is. If
  /// positive, insert separators between digits starting from the rightmost
  /// digit.
  ///
  /// For example, the config below will leave separators in binary literals
  /// alone, insert separators in decimal literals to separate the digits into
  /// groups of 3, and remove separators in hexadecimal literals.
  /// \code
  ///   IntegerLiteralSeparator:
  ///     Binary: 0
  ///     Decimal: 3
  ///     Hex: -1
  /// \endcode
  ///
  /// You can also specify a minimum number of digits (``BinaryMinDigits``,
  /// ``DecimalMinDigits``, and ``HexMinDigits``) the integer literal must
  /// have in order for the separators to be inserted.
  struct IntegerLiteralSeparatorStyle {
    /// Format separators in binary literals.
    /// \code{.text}
    ///   /* -1: */ b = 0b100111101101;
    ///   /*  0: */ b = 0b10011'11'0110'1;
    ///   /*  3: */ b = 0b100'111'101'101;
    ///   /*  4: */ b = 0b1001'1110'1101;
    /// \endcode
    int8_t Binary;
    /// Format separators in binary literals with a minimum number of digits.
    /// \code{.text}
    ///   // Binary: 3
    ///   // BinaryMinDigits: 7
    ///   b1 = 0b101101;
    ///   b2 = 0b1'101'101;
    /// \endcode
    int8_t BinaryMinDigits;
    /// Format separators in decimal literals.
    /// \code{.text}
    ///   /* -1: */ d = 18446744073709550592ull;
    ///   /*  0: */ d = 184467'440737'0'95505'92ull;
    ///   /*  3: */ d = 18'446'744'073'709'550'592ull;
    /// \endcode
    int8_t Decimal;
    /// Format separators in decimal literals with a minimum number of digits.
    /// \code{.text}
    ///   // Decimal: 3
    ///   // DecimalMinDigits: 5
    ///   d1 = 2023;
    ///   d2 = 10'000;
    /// \endcode
    int8_t DecimalMinDigits;
    /// Format separators in hexadecimal literals.
    /// \code{.text}
    ///   /* -1: */ h = 0xDEADBEEFDEADBEEFuz;
    ///   /*  0: */ h = 0xDEAD'BEEF'DE'AD'BEE'Fuz;
    ///   /*  2: */ h = 0xDE'AD'BE'EF'DE'AD'BE'EFuz;
    /// \endcode
    int8_t Hex;
    /// Format separators in hexadecimal literals with a minimum number of
    /// digits.
    /// \code{.text}
    ///   // Hex: 2
    ///   // HexMinDigits: 6
    ///   h1 = 0xABCDE;
    ///   h2 = 0xAB'CD'EF;
    /// \endcode
    int8_t HexMinDigits;
    bool operator==(const IntegerLiteralSeparatorStyle &R) const {
      return Binary == R.Binary && BinaryMinDigits == R.BinaryMinDigits &&
             Decimal == R.Decimal && DecimalMinDigits == R.DecimalMinDigits &&
             Hex == R.Hex && HexMinDigits == R.HexMinDigits;
    }
  };

  /// Format integer literal separators (``'`` for C++ and ``_`` for C#, Java,
  /// and JavaScript).
  /// \version 16
  IntegerLiteralSeparatorStyle IntegerLiteralSeparator;

  /// A vector of prefixes ordered by the desired groups for Java imports.
  ///
  /// One group's prefix can be a subset of another - the longest prefix is
  /// always matched. Within a group, the imports are ordered lexicographically.
  /// Static imports are grouped separately and follow the same group rules.
  /// By default, static imports are placed before non-static imports,
  /// but this behavior is changed by another option,
  /// ``SortJavaStaticImport``.
  ///
  /// In the .clang-format configuration file, this can be configured like
  /// in the following yaml example. This will result in imports being
  /// formatted as in the Java example below.
  /// \code{.yaml}
  ///   JavaImportGroups: [com.example, com, org]
  /// \endcode
  ///
  /// \code{.java}
  ///    import static com.example.function1;
  ///
  ///    import static com.test.function2;
  ///
  ///    import static org.example.function3;
  ///
  ///    import com.example.ClassA;
  ///    import com.example.Test;
  ///    import com.example.a.ClassB;
  ///
  ///    import com.test.ClassC;
  ///
  ///    import org.example.ClassD;
  /// \endcode
  /// \version 8
  std::vector<std::string> JavaImportGroups;

  /// Quotation styles for JavaScript strings. Does not affect template
  /// strings.
  enum JavaScriptQuoteStyle : int8_t {
    /// Leave string quotes as they are.
    /// \code{.js}
    ///    string1 = "foo";
    ///    string2 = 'bar';
    /// \endcode
    JSQS_Leave,
    /// Always use single quotes.
    /// \code{.js}
    ///    string1 = 'foo';
    ///    string2 = 'bar';
    /// \endcode
    JSQS_Single,
    /// Always use double quotes.
    /// \code{.js}
    ///    string1 = "foo";
    ///    string2 = "bar";
    /// \endcode
    JSQS_Double
  };

  /// The JavaScriptQuoteStyle to use for JavaScript strings.
  /// \version 3.9
  JavaScriptQuoteStyle JavaScriptQuotes;

  // clang-format off
  /// Whether to wrap JavaScript import/export statements.
  /// \code{.js}
  ///    true:
  ///    import {
  ///        VeryLongImportsAreAnnoying,
  ///        VeryLongImportsAreAnnoying,
  ///        VeryLongImportsAreAnnoying,
  ///    } from "some/module.js"
  ///
  ///    false:
  ///    import {VeryLongImportsAreAnnoying, VeryLongImportsAreAnnoying, VeryLongImportsAreAnnoying,} from "some/module.js"
  /// \endcode
  /// \version 3.9
  bool JavaScriptWrapImports;
  // clang-format on

  /// Options regarding which empty lines are kept.
  ///
  /// For example, the config below will remove empty lines at start of the
  /// file, end of the file, and start of blocks.
  ///
  /// \code
  ///   KeepEmptyLines:
  ///     AtEndOfFile: false
  ///     AtStartOfBlock: false
  ///     AtStartOfFile: false
  /// \endcode
  struct KeepEmptyLinesStyle {
    /// Keep empty lines at end of file.
    bool AtEndOfFile;
    /// Keep empty lines at start of a block.
    /// \code
    ///    true:                                  false:
    ///    if (foo) {                     vs.     if (foo) {
    ///                                             bar();
    ///      bar();                               }
    ///    }
    /// \endcode
    bool AtStartOfBlock;
    /// Keep empty lines at start of file.
    bool AtStartOfFile;
    bool operator==(const KeepEmptyLinesStyle &R) const {
      return AtEndOfFile == R.AtEndOfFile &&
             AtStartOfBlock == R.AtStartOfBlock &&
             AtStartOfFile == R.AtStartOfFile;
    }
  };
  /// Which empty lines are kept.  See ``MaxEmptyLinesToKeep`` for how many
  /// consecutive empty lines are kept.
  /// \version 19
  KeepEmptyLinesStyle KeepEmptyLines;

  /// This option is deprecated. See ``AtEndOfFile`` of ``KeepEmptyLines``.
  /// \version 17
  // bool KeepEmptyLinesAtEOF;

  /// This option is deprecated. See ``AtStartOfBlock`` of ``KeepEmptyLines``.
  /// \version 3.7
  // bool KeepEmptyLinesAtTheStartOfBlocks;

  /// Indentation logic for lambda bodies.
  enum LambdaBodyIndentationKind : int8_t {
    /// Align lambda body relative to the lambda signature. This is the default.
    /// \code
    ///    someMethod(
    ///        [](SomeReallyLongLambdaSignatureArgument foo) {
    ///          return;
    ///        });
    /// \endcode
    LBI_Signature,
    /// For statements within block scope, align lambda body relative to the
    /// indentation level of the outer scope the lambda signature resides in.
    /// \code
    ///    someMethod(
    ///        [](SomeReallyLongLambdaSignatureArgument foo) {
    ///      return;
    ///    });
    ///
    ///    someMethod(someOtherMethod(
    ///        [](SomeReallyLongLambdaSignatureArgument foo) {
    ///      return;
    ///    }));
    /// \endcode
    LBI_OuterScope,
  };

  /// The indentation style of lambda bodies. ``Signature`` (the default)
  /// causes the lambda body to be indented one additional level relative to
  /// the indentation level of the signature. ``OuterScope`` forces the lambda
  /// body to be indented one additional level relative to the parent scope
  /// containing the lambda signature.
  /// \version 13
  LambdaBodyIndentationKind LambdaBodyIndentation;

  /// Supported languages.
  ///
  /// When stored in a configuration file, specifies the language, that the
  /// configuration targets. When passed to the ``reformat()`` function, enables
  /// syntax features specific to the language.
  enum LanguageKind : int8_t {
    /// Do not use.
    LK_None,
    /// Should be used for C, C++.
    LK_Cpp,
    /// Should be used for C#.
    LK_CSharp,
    /// Should be used for Java.
    LK_Java,
    /// Should be used for JavaScript.
    LK_JavaScript,
    /// Should be used for JSON.
    LK_Json,
    /// Should be used for Objective-C, Objective-C++.
    LK_ObjC,
    /// Should be used for Protocol Buffers
    /// (https://developers.google.com/protocol-buffers/).
    LK_Proto,
    /// Should be used for TableGen code.
    LK_TableGen,
    /// Should be used for Protocol Buffer messages in text format
    /// (https://developers.google.com/protocol-buffers/).
    LK_TextProto,
    /// Should be used for Verilog and SystemVerilog.
    /// https://standards.ieee.org/ieee/1800/6700/
    /// https://sci-hub.st/10.1109/IEEESTD.2018.8299595
    LK_Verilog
  };
  bool isCpp() const { return Language == LK_Cpp || Language == LK_ObjC; }
  bool isCSharp() const { return Language == LK_CSharp; }
  bool isJson() const { return Language == LK_Json; }
  bool isJavaScript() const { return Language == LK_JavaScript; }
  bool isVerilog() const { return Language == LK_Verilog; }
  bool isProto() const {
    return Language == LK_Proto || Language == LK_TextProto;
  }
  bool isTableGen() const { return Language == LK_TableGen; }

  /// Language, this format style is targeted at.
  /// \version 3.5
  LanguageKind Language;

  /// Line ending style.
  enum LineEndingStyle : int8_t {
    /// Use ``\n``.
    LE_LF,
    /// Use ``\r\n``.
    LE_CRLF,
    /// Use ``\n`` unless the input has more lines ending in ``\r\n``.
    LE_DeriveLF,
    /// Use ``\r\n`` unless the input has more lines ending in ``\n``.
    LE_DeriveCRLF,
  };

  /// Line ending style (``\n`` or ``\r\n``) to use.
  /// \version 16
  LineEndingStyle LineEnding;

  /// A regular expression matching macros that start a block.
  /// \code
  ///    # With:
  ///    MacroBlockBegin: "^NS_MAP_BEGIN|\
  ///    NS_TABLE_HEAD$"
  ///    MacroBlockEnd: "^\
  ///    NS_MAP_END|\
  ///    NS_TABLE_.*_END$"
  ///
  ///    NS_MAP_BEGIN
  ///      foo();
  ///    NS_MAP_END
  ///
  ///    NS_TABLE_HEAD
  ///      bar();
  ///    NS_TABLE_FOO_END
  ///
  ///    # Without:
  ///    NS_MAP_BEGIN
  ///    foo();
  ///    NS_MAP_END
  ///
  ///    NS_TABLE_HEAD
  ///    bar();
  ///    NS_TABLE_FOO_END
  /// \endcode
  /// \version 3.7
  std::string MacroBlockBegin;

  /// A regular expression matching macros that end a block.
  /// \version 3.7
  std::string MacroBlockEnd;

  /// A list of macros of the form \c <definition>=<expansion> .
  ///
  /// Code will be parsed with macros expanded, in order to determine how to
  /// interpret and format the macro arguments.
  ///
  /// For example, the code:
  /// \code
  ///   A(a*b);
  /// \endcode
  ///
  /// will usually be interpreted as a call to a function A, and the
  /// multiplication expression will be formatted as ``a * b``.
  ///
  /// If we specify the macro definition:
  /// \code{.yaml}
  ///   Macros:
  ///   - A(x)=x
  /// \endcode
  ///
  /// the code will now be parsed as a declaration of the variable b of type a*,
  /// and formatted as ``a* b`` (depending on pointer-binding rules).
  ///
  /// Features and restrictions:
  ///  * Both function-like macros and object-like macros are supported.
  ///  * Macro arguments must be used exactly once in the expansion.
  ///  * No recursive expansion; macros referencing other macros will be
  ///    ignored.
  ///  * Overloading by arity is supported: for example, given the macro
  ///    definitions A=x, A()=y, A(a)=a
  ///
  /// \code
  ///    A; -> x;
  ///    A(); -> y;
  ///    A(z); -> z;
  ///    A(a, b); // will not be expanded.
  /// \endcode
  ///
  /// \version 17
  std::vector<std::string> Macros;

  /// The maximum number of consecutive empty lines to keep.
  /// \code
  ///    MaxEmptyLinesToKeep: 1         vs.     MaxEmptyLinesToKeep: 0
  ///    int f() {                              int f() {
  ///      int = 1;                                 int i = 1;
  ///                                               i = foo();
  ///      i = foo();                               return i;
  ///                                           }
  ///      return i;
  ///    }
  /// \endcode
  /// \version 3.7
  unsigned MaxEmptyLinesToKeep;

  /// Different ways to indent namespace contents.
  enum NamespaceIndentationKind : int8_t {
    /// Don't indent in namespaces.
    /// \code
    ///    namespace out {
    ///    int i;
    ///    namespace in {
    ///    int i;
    ///    }
    ///    }
    /// \endcode
    NI_None,
    /// Indent only in inner namespaces (nested in other namespaces).
    /// \code
    ///    namespace out {
    ///    int i;
    ///    namespace in {
    ///      int i;
    ///    }
    ///    }
    /// \endcode
    NI_Inner,
    /// Indent in all namespaces.
    /// \code
    ///    namespace out {
    ///      int i;
    ///      namespace in {
    ///        int i;
    ///      }
    ///    }
    /// \endcode
    NI_All
  };

  /// The indentation used for namespaces.
  /// \version 3.7
  NamespaceIndentationKind NamespaceIndentation;

  /// A vector of macros which are used to open namespace blocks.
  ///
  /// These are expected to be macros of the form:
  /// \code
  ///   NAMESPACE(<namespace-name>, ...) {
  ///     <namespace-content>
  ///   }
  /// \endcode
  ///
  /// For example: TESTSUITE
  /// \version 9
  std::vector<std::string> NamespaceMacros;

  /// Controls bin-packing Objective-C protocol conformance list
  /// items into as few lines as possible when they go over ``ColumnLimit``.
  ///
  /// If ``Auto`` (the default), delegates to the value in
  /// ``BinPackParameters``. If that is ``true``, bin-packs Objective-C
  /// protocol conformance list items into as few lines as possible
  /// whenever they go over ``ColumnLimit``.
  ///
  /// If ``Always``, always bin-packs Objective-C protocol conformance
  /// list items into as few lines as possible whenever they go over
  /// ``ColumnLimit``.
  ///
  /// If ``Never``, lays out Objective-C protocol conformance list items
  /// onto individual lines whenever they go over ``ColumnLimit``.
  ///
  /// \code{.objc}
  ///    Always (or Auto, if BinPackParameters=true):
  ///    @interface ccccccccccccc () <
  ///        ccccccccccccc, ccccccccccccc,
  ///        ccccccccccccc, ccccccccccccc> {
  ///    }
  ///
  ///    Never (or Auto, if BinPackParameters=false):
  ///    @interface ddddddddddddd () <
  ///        ddddddddddddd,
  ///        ddddddddddddd,
  ///        ddddddddddddd,
  ///        ddddddddddddd> {
  ///    }
  /// \endcode
  /// \version 7
  BinPackStyle ObjCBinPackProtocolList;

  /// The number of characters to use for indentation of ObjC blocks.
  /// \code{.objc}
  ///    ObjCBlockIndentWidth: 4
  ///
  ///    [operation setCompletionBlock:^{
  ///        [self onOperationDone];
  ///    }];
  /// \endcode
  /// \version 3.7
  unsigned ObjCBlockIndentWidth;

  /// Break parameters list into lines when there is nested block
  /// parameters in a function call.
  /// \code
  ///   false:
  ///    - (void)_aMethod
  ///    {
  ///        [self.test1 t:self w:self callback:^(typeof(self) self, NSNumber
  ///        *u, NSNumber *v) {
  ///            u = c;
  ///        }]
  ///    }
  ///    true:
  ///    - (void)_aMethod
  ///    {
  ///       [self.test1 t:self
  ///                    w:self
  ///           callback:^(typeof(self) self, NSNumber *u, NSNumber *v) {
  ///                u = c;
  ///            }]
  ///    }
  /// \endcode
  /// \version 11
  bool ObjCBreakBeforeNestedBlockParam;

  /// The order in which ObjC property attributes should appear.
  ///
  /// Attributes in code will be sorted in the order specified. Any attributes
  /// encountered that are not mentioned in this array will be sorted last, in
  /// stable order. Comments between attributes will leave the attributes
  /// untouched.
  /// \warning
  ///  Using this option could lead to incorrect code formatting due to
  ///  clang-format's lack of complete semantic information. As such, extra
  ///  care should be taken to review code changes made by this option.
  /// \endwarning
  /// \code{.yaml}
  ///   ObjCPropertyAttributeOrder: [
  ///       class, direct,
  ///       atomic, nonatomic,
  ///       assign, retain, strong, copy, weak, unsafe_unretained,
  ///       readonly, readwrite, getter, setter,
  ///       nullable, nonnull, null_resettable, null_unspecified
  ///   ]
  /// \endcode
  /// \version 18
  std::vector<std::string> ObjCPropertyAttributeOrder;

  /// Add a space after ``@property`` in Objective-C, i.e. use
  /// ``@property (readonly)`` instead of ``@property(readonly)``.
  /// \version 3.7
  bool ObjCSpaceAfterProperty;

  /// Add a space in front of an Objective-C protocol list, i.e. use
  /// ``Foo <Protocol>`` instead of ``Foo<Protocol>``.
  /// \version 3.7
  bool ObjCSpaceBeforeProtocolList;

  /// Different ways to try to fit all constructor initializers on a line.
  enum PackConstructorInitializersStyle : int8_t {
    /// Always put each constructor initializer on its own line.
    /// \code
    ///    Constructor()
    ///        : a(),
    ///          b()
    /// \endcode
    PCIS_Never,
    /// Bin-pack constructor initializers.
    /// \code
    ///    Constructor()
    ///        : aaaaaaaaaaaaaaaaaaaa(), bbbbbbbbbbbbbbbbbbbb(),
    ///          cccccccccccccccccccc()
    /// \endcode
    PCIS_BinPack,
    /// Put all constructor initializers on the current line if they fit.
    /// Otherwise, put each one on its own line.
    /// \code
    ///    Constructor() : a(), b()
    ///
    ///    Constructor()
    ///        : aaaaaaaaaaaaaaaaaaaa(),
    ///          bbbbbbbbbbbbbbbbbbbb(),
    ///          ddddddddddddd()
    /// \endcode
    PCIS_CurrentLine,
    /// Same as ``PCIS_CurrentLine`` except that if all constructor initializers
    /// do not fit on the current line, try to fit them on the next line.
    /// \code
    ///    Constructor() : a(), b()
    ///
    ///    Constructor()
    ///        : aaaaaaaaaaaaaaaaaaaa(), bbbbbbbbbbbbbbbbbbbb(), ddddddddddddd()
    ///
    ///    Constructor()
    ///        : aaaaaaaaaaaaaaaaaaaa(),
    ///          bbbbbbbbbbbbbbbbbbbb(),
    ///          cccccccccccccccccccc()
    /// \endcode
    PCIS_NextLine,
    /// Put all constructor initializers on the next line if they fit.
    /// Otherwise, put each one on its own line.
    /// \code
    ///    Constructor()
    ///        : a(), b()
    ///
    ///    Constructor()
    ///        : aaaaaaaaaaaaaaaaaaaa(), bbbbbbbbbbbbbbbbbbbb(), ddddddddddddd()
    ///
    ///    Constructor()
    ///        : aaaaaaaaaaaaaaaaaaaa(),
    ///          bbbbbbbbbbbbbbbbbbbb(),
    ///          cccccccccccccccccccc()
    /// \endcode
    PCIS_NextLineOnly,
  };

  /// The pack constructor initializers style to use.
  /// \version 14
  PackConstructorInitializersStyle PackConstructorInitializers;

  /// The penalty for breaking around an assignment operator.
  /// \version 5
  unsigned PenaltyBreakAssignment;

  /// The penalty for breaking a function call after ``call(``.
  /// \version 3.7
  unsigned PenaltyBreakBeforeFirstCallParameter;

  /// The penalty for each line break introduced inside a comment.
  /// \version 3.7
  unsigned PenaltyBreakComment;

  /// The penalty for breaking before the first ``<<``.
  /// \version 3.7
  unsigned PenaltyBreakFirstLessLess;

  /// The penalty for breaking after ``(``.
  /// \version 14
  unsigned PenaltyBreakOpenParenthesis;

  /// The penalty for breaking after ``::``.
  /// \version 18
  unsigned PenaltyBreakScopeResolution;

  /// The penalty for each line break introduced inside a string literal.
  /// \version 3.7
  unsigned PenaltyBreakString;

  /// The penalty for breaking after template declaration.
  /// \version 7
  unsigned PenaltyBreakTemplateDeclaration;

  /// The penalty for each character outside of the column limit.
  /// \version 3.7
  unsigned PenaltyExcessCharacter;

  /// Penalty for each character of whitespace indentation
  /// (counted relative to leading non-whitespace column).
  /// \version 12
  unsigned PenaltyIndentedWhitespace;

  /// Penalty for putting the return type of a function onto its own line.
  /// \version 3.7
  unsigned PenaltyReturnTypeOnItsOwnLine;

  /// The ``&``, ``&&`` and ``*`` alignment style.
  enum PointerAlignmentStyle : int8_t {
    /// Align pointer to the left.
    /// \code
    ///   int* a;
    /// \endcode
    PAS_Left,
    /// Align pointer to the right.
    /// \code
    ///   int *a;
    /// \endcode
    PAS_Right,
    /// Align pointer in the middle.
    /// \code
    ///   int * a;
    /// \endcode
    PAS_Middle
  };

  /// Pointer and reference alignment style.
  /// \version 3.7
  PointerAlignmentStyle PointerAlignment;

  /// The number of columns to use for indentation of preprocessor statements.
  /// When set to -1 (default) ``IndentWidth`` is used also for preprocessor
  /// statements.
  /// \code
  ///    PPIndentWidth: 1
  ///
  ///    #ifdef __linux__
  ///    # define FOO
  ///    #else
  ///    # define BAR
  ///    #endif
  /// \endcode
  /// \version 13
  int PPIndentWidth;

  /// Different specifiers and qualifiers alignment styles.
  enum QualifierAlignmentStyle : int8_t {
    /// Don't change specifiers/qualifiers to either Left or Right alignment
    /// (default).
    /// \code
    ///    int const a;
    ///    const int *a;
    /// \endcode
    QAS_Leave,
    /// Change specifiers/qualifiers to be left-aligned.
    /// \code
    ///    const int a;
    ///    const int *a;
    /// \endcode
    QAS_Left,
    /// Change specifiers/qualifiers to be right-aligned.
    /// \code
    ///    int const a;
    ///    int const *a;
    /// \endcode
    QAS_Right,
    /// Change specifiers/qualifiers to be aligned based on ``QualifierOrder``.
    /// With:
    /// \code{.yaml}
    ///   QualifierOrder: [inline, static, type, const]
    /// \endcode
    ///
    /// \code
    ///
    ///    int const a;
    ///    int const *a;
    /// \endcode
    QAS_Custom
  };

  /// Different ways to arrange specifiers and qualifiers (e.g. const/volatile).
  /// \warning
  ///  Setting ``QualifierAlignment``  to something other than ``Leave``, COULD
  ///  lead to incorrect code formatting due to incorrect decisions made due to
  ///  clang-formats lack of complete semantic information.
  ///  As such extra care should be taken to review code changes made by the use
  ///  of this option.
  /// \endwarning
  /// \version 14
  QualifierAlignmentStyle QualifierAlignment;

  /// The order in which the qualifiers appear.
  /// Order is an array that can contain any of the following:
  ///
  ///   * const
  ///   * inline
  ///   * static
  ///   * friend
  ///   * constexpr
  ///   * volatile
  ///   * restrict
  ///   * type
  ///
  /// \note
  ///  It **must** contain ``type``.
  /// \endnote
  ///
  /// Items to the left of ``type`` will be placed to the left of the type and
  /// aligned in the order supplied. Items to the right of ``type`` will be
  /// placed to the right of the type and aligned in the order supplied.
  ///
  /// \code{.yaml}
  ///   QualifierOrder: [inline, static, type, const, volatile]
  /// \endcode
  /// \version 14
  std::vector<std::string> QualifierOrder;

  /// See documentation of ``RawStringFormats``.
  struct RawStringFormat {
    /// The language of this raw string.
    LanguageKind Language;
    /// A list of raw string delimiters that match this language.
    std::vector<std::string> Delimiters;
    /// A list of enclosing function names that match this language.
    std::vector<std::string> EnclosingFunctions;
    /// The canonical delimiter for this language.
    std::string CanonicalDelimiter;
    /// The style name on which this raw string format is based on.
    /// If not specified, the raw string format is based on the style that this
    /// format is based on.
    std::string BasedOnStyle;
    bool operator==(const RawStringFormat &Other) const {
      return Language == Other.Language && Delimiters == Other.Delimiters &&
             EnclosingFunctions == Other.EnclosingFunctions &&
             CanonicalDelimiter == Other.CanonicalDelimiter &&
             BasedOnStyle == Other.BasedOnStyle;
    }
  };

  /// Defines hints for detecting supported languages code blocks in raw
  /// strings.
  ///
  /// A raw string with a matching delimiter or a matching enclosing function
  /// name will be reformatted assuming the specified language based on the
  /// style for that language defined in the .clang-format file. If no style has
  /// been defined in the .clang-format file for the specific language, a
  /// predefined style given by ``BasedOnStyle`` is used. If ``BasedOnStyle`` is
  /// not found, the formatting is based on ``LLVM`` style. A matching delimiter
  /// takes precedence over a matching enclosing function name for determining
  /// the language of the raw string contents.
  ///
  /// If a canonical delimiter is specified, occurrences of other delimiters for
  /// the same language will be updated to the canonical if possible.
  ///
  /// There should be at most one specification per language and each delimiter
  /// and enclosing function should not occur in multiple specifications.
  ///
  /// To configure this in the .clang-format file, use:
  /// \code{.yaml}
  ///   RawStringFormats:
  ///     - Language: TextProto
  ///         Delimiters:
  ///           - pb
  ///           - proto
  ///         EnclosingFunctions:
  ///           - PARSE_TEXT_PROTO
  ///         BasedOnStyle: google
  ///     - Language: Cpp
  ///         Delimiters:
  ///           - cc
  ///           - cpp
  ///         BasedOnStyle: LLVM
  ///         CanonicalDelimiter: cc
  /// \endcode
  /// \version 6
  std::vector<RawStringFormat> RawStringFormats;

  /// \brief The ``&`` and ``&&`` alignment style.
  enum ReferenceAlignmentStyle : int8_t {
    /// Align reference like ``PointerAlignment``.
    RAS_Pointer,
    /// Align reference to the left.
    /// \code
    ///   int& a;
    /// \endcode
    RAS_Left,
    /// Align reference to the right.
    /// \code
    ///   int &a;
    /// \endcode
    RAS_Right,
    /// Align reference in the middle.
    /// \code
    ///   int & a;
    /// \endcode
    RAS_Middle
  };

  /// \brief Reference alignment style (overrides ``PointerAlignment`` for
  /// references).
  /// \version 13
  ReferenceAlignmentStyle ReferenceAlignment;

  // clang-format off
  /// If ``true``, clang-format will attempt to re-flow comments. That is it
  /// will touch a comment and *reflow* long comments into new lines, trying to
  /// obey the ``ColumnLimit``.
  /// \code
  ///    false:
  ///    // veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment with plenty of information
  ///    /* second veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment with plenty of information */
  ///
  ///    true:
  ///    // veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment with plenty of
  ///    // information
  ///    /* second veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment with plenty of
  ///     * information */
  /// \endcode
  /// \version 3.8
  bool ReflowComments;
  // clang-format on

  /// Remove optional braces of control statements (``if``, ``else``, ``for``,
  /// and ``while``) in C++ according to the LLVM coding style.
  /// \warning
  ///  This option will be renamed and expanded to support other styles.
  /// \endwarning
  /// \warning
  ///  Setting this option to ``true`` could lead to incorrect code formatting
  ///  due to clang-format's lack of complete semantic information. As such,
  ///  extra care should be taken to review code changes made by this option.
  /// \endwarning
  /// \code
  ///   false:                                     true:
  ///
  ///   if (isa<FunctionDecl>(D)) {        vs.     if (isa<FunctionDecl>(D))
  ///     handleFunctionDecl(D);                     handleFunctionDecl(D);
  ///   } else if (isa<VarDecl>(D)) {              else if (isa<VarDecl>(D))
  ///     handleVarDecl(D);                          handleVarDecl(D);
  ///   }
  ///
  ///   if (isa<VarDecl>(D)) {             vs.     if (isa<VarDecl>(D)) {
  ///     for (auto *A : D.attrs()) {                for (auto *A : D.attrs())
  ///       if (shouldProcessAttr(A)) {                if (shouldProcessAttr(A))
  ///         handleAttr(A);                             handleAttr(A);
  ///       }                                      }
  ///     }
  ///   }
  ///
  ///   if (isa<FunctionDecl>(D)) {        vs.     if (isa<FunctionDecl>(D))
  ///     for (auto *A : D.attrs()) {                for (auto *A : D.attrs())
  ///       handleAttr(A);                             handleAttr(A);
  ///     }
  ///   }
  ///
  ///   if (auto *D = (T)(D)) {            vs.     if (auto *D = (T)(D)) {
  ///     if (shouldProcess(D)) {                    if (shouldProcess(D))
  ///       handleVarDecl(D);                          handleVarDecl(D);
  ///     } else {                                   else
  ///       markAsIgnored(D);                          markAsIgnored(D);
  ///     }                                        }
  ///   }
  ///
  ///   if (a) {                           vs.     if (a)
  ///     b();                                       b();
  ///   } else {                                   else if (c)
  ///     if (c) {                                   d();
  ///       d();                                   else
  ///     } else {                                   e();
  ///       e();
  ///     }
  ///   }
  /// \endcode
  /// \version 14
  bool RemoveBracesLLVM;

  /// Types of redundant parentheses to remove.
  enum RemoveParenthesesStyle : int8_t {
    /// Do not remove parentheses.
    /// \code
    ///   class __declspec((dllimport)) X {};
    ///   co_return (((0)));
    ///   return ((a + b) - ((c + d)));
    /// \endcode
    RPS_Leave,
    /// Replace multiple parentheses with single parentheses.
    /// \code
    ///   class __declspec(dllimport) X {};
    ///   co_return (0);
    ///   return ((a + b) - (c + d));
    /// \endcode
    RPS_MultipleParentheses,
    /// Also remove parentheses enclosing the expression in a
    /// ``return``/``co_return`` statement.
    /// \code
    ///   class __declspec(dllimport) X {};
    ///   co_return 0;
    ///   return (a + b) - (c + d);
    /// \endcode
    RPS_ReturnStatement,
  };

  /// Remove redundant parentheses.
  /// \warning
  ///  Setting this option to any value other than ``Leave`` could lead to
  ///  incorrect code formatting due to clang-format's lack of complete semantic
  ///  information. As such, extra care should be taken to review code changes
  ///  made by this option.
  /// \endwarning
  /// \version 17
  RemoveParenthesesStyle RemoveParentheses;

  /// Remove semicolons after the closing braces of functions and
  /// constructors/destructors.
  /// \warning
  ///  Setting this option to ``true`` could lead to incorrect code formatting
  ///  due to clang-format's lack of complete semantic information. As such,
  ///  extra care should be taken to review code changes made by this option.
  /// \endwarning
  /// \code
  ///   false:                                     true:
  ///
  ///   int max(int a, int b) {                    int max(int a, int b) {
  ///     return a > b ? a : b;                      return a > b ? a : b;
  ///   };                                         }
  ///
  /// \endcode
  /// \version 16
  bool RemoveSemicolon;

  /// \brief The possible positions for the requires clause. The
  /// ``IndentRequires`` option is only used if the ``requires`` is put on the
  /// start of a line.
  enum RequiresClausePositionStyle : int8_t {
    /// Always put the ``requires`` clause on its own line.
    /// \code
    ///   template <typename T>
    ///   requires C<T>
    ///   struct Foo {...
    ///
    ///   template <typename T>
    ///   requires C<T>
    ///   void bar(T t) {...
    ///
    ///   template <typename T>
    ///   void baz(T t)
    ///   requires C<T>
    ///   {...
    /// \endcode
    RCPS_OwnLine,
    /// Try to put the clause together with the preceding part of a declaration.
    /// For class templates: stick to the template declaration.
    /// For function templates: stick to the template declaration.
    /// For function declaration followed by a requires clause: stick to the
    /// parameter list.
    /// \code
    ///   template <typename T> requires C<T>
    ///   struct Foo {...
    ///
    ///   template <typename T> requires C<T>
    ///   void bar(T t) {...
    ///
    ///   template <typename T>
    ///   void baz(T t) requires C<T>
    ///   {...
    /// \endcode
    RCPS_WithPreceding,
    /// Try to put the ``requires`` clause together with the class or function
    /// declaration.
    /// \code
    ///   template <typename T>
    ///   requires C<T> struct Foo {...
    ///
    ///   template <typename T>
    ///   requires C<T> void bar(T t) {...
    ///
    ///   template <typename T>
    ///   void baz(T t)
    ///   requires C<T> {...
    /// \endcode
    RCPS_WithFollowing,
    /// Try to put everything in the same line if possible. Otherwise normal
    /// line breaking rules take over.
    /// \code
    ///   // Fitting:
    ///   template <typename T> requires C<T> struct Foo {...
    ///
    ///   template <typename T> requires C<T> void bar(T t) {...
    ///
    ///   template <typename T> void bar(T t) requires C<T> {...
    ///
    ///   // Not fitting, one possible example:
    ///   template <typename LongName>
    ///   requires C<LongName>
    ///   struct Foo {...
    ///
    ///   template <typename LongName>
    ///   requires C<LongName>
    ///   void bar(LongName ln) {
    ///
    ///   template <typename LongName>
    ///   void bar(LongName ln)
    ///       requires C<LongName> {
    /// \endcode
    RCPS_SingleLine,
  };

  /// \brief The position of the ``requires`` clause.
  /// \version 15
  RequiresClausePositionStyle RequiresClausePosition;

  /// Indentation logic for requires expression bodies.
  enum RequiresExpressionIndentationKind : int8_t {
    /// Align requires expression body relative to the indentation level of the
    /// outer scope the requires expression resides in.
    /// This is the default.
    /// \code
    ///    template <typename T>
    ///    concept C = requires(T t) {
    ///      ...
    ///    }
    /// \endcode
    REI_OuterScope,
    /// Align requires expression body relative to the ``requires`` keyword.
    /// \code
    ///    template <typename T>
    ///    concept C = requires(T t) {
    ///                  ...
    ///                }
    /// \endcode
    REI_Keyword,
  };

  /// The indentation used for requires expression bodies.
  /// \version 16
  RequiresExpressionIndentationKind RequiresExpressionIndentation;

  /// \brief The style if definition blocks should be separated.
  enum SeparateDefinitionStyle : int8_t {
    /// Leave definition blocks as they are.
    SDS_Leave,
    /// Insert an empty line between definition blocks.
    SDS_Always,
    /// Remove any empty line between definition blocks.
    SDS_Never
  };

  /// Specifies the use of empty lines to separate definition blocks, including
  /// classes, structs, enums, and functions.
  /// \code
  ///    Never                  v.s.     Always
  ///    #include <cstring>              #include <cstring>
  ///    struct Foo {
  ///      int a, b, c;                  struct Foo {
  ///    };                                int a, b, c;
  ///    namespace Ns {                  };
  ///    class Bar {
  ///    public:                         namespace Ns {
  ///      struct Foobar {               class Bar {
  ///        int a;                      public:
  ///        int b;                        struct Foobar {
  ///      };                                int a;
  ///    private:                            int b;
  ///      int t;                          };
  ///      int method1() {
  ///        // ...                      private:
  ///      }                               int t;
  ///      enum List {
  ///        ITEM1,                        int method1() {
  ///        ITEM2                           // ...
  ///      };                              }
  ///      template<typename T>
  ///      int method2(T x) {              enum List {
  ///        // ...                          ITEM1,
  ///      }                                 ITEM2
  ///      int i, j, k;                    };
  ///      int method3(int par) {
  ///        // ...                        template<typename T>
  ///      }                               int method2(T x) {
  ///    };                                  // ...
  ///    class C {};                       }
  ///    }
  ///                                      int i, j, k;
  ///
  ///                                      int method3(int par) {
  ///                                        // ...
  ///                                      }
  ///                                    };
  ///
  ///                                    class C {};
  ///                                    }
  /// \endcode
  /// \version 14
  SeparateDefinitionStyle SeparateDefinitionBlocks;

  /// The maximal number of unwrapped lines that a short namespace spans.
  /// Defaults to 1.
  ///
  /// This determines the maximum length of short namespaces by counting
  /// unwrapped lines (i.e. containing neither opening nor closing
  /// namespace brace) and makes ``FixNamespaceComments`` omit adding
  /// end comments for those.
  /// \code
  ///    ShortNamespaceLines: 1     vs.     ShortNamespaceLines: 0
  ///    namespace a {                      namespace a {
  ///      int foo;                           int foo;
  ///    }                                  } // namespace a
  ///
  ///    ShortNamespaceLines: 1     vs.     ShortNamespaceLines: 0
  ///    namespace b {                      namespace b {
  ///      int foo;                           int foo;
  ///      int bar;                           int bar;
  ///    } // namespace b                   } // namespace b
  /// \endcode
  /// \version 13
  unsigned ShortNamespaceLines;

  /// Do not format macro definition body.
  /// \version 18
  bool SkipMacroDefinitionBody;

  /// Include sorting options.
  enum SortIncludesOptions : int8_t {
    /// Includes are never sorted.
    /// \code
    ///    #include "B/A.h"
    ///    #include "A/B.h"
    ///    #include "a/b.h"
    ///    #include "A/b.h"
    ///    #include "B/a.h"
    /// \endcode
    SI_Never,
    /// Includes are sorted in an ASCIIbetical or case sensitive fashion.
    /// \code
    ///    #include "A/B.h"
    ///    #include "A/b.h"
    ///    #include "B/A.h"
    ///    #include "B/a.h"
    ///    #include "a/b.h"
    /// \endcode
    SI_CaseSensitive,
    /// Includes are sorted in an alphabetical or case insensitive fashion.
    /// \code
    ///    #include "A/B.h"
    ///    #include "A/b.h"
    ///    #include "a/b.h"
    ///    #include "B/A.h"
    ///    #include "B/a.h"
    /// \endcode
    SI_CaseInsensitive,
  };

  /// Controls if and how clang-format will sort ``#includes``.
  /// \version 3.8
  SortIncludesOptions SortIncludes;

  /// Position for Java Static imports.
  enum SortJavaStaticImportOptions : int8_t {
    /// Static imports are placed before non-static imports.
    /// \code{.java}
    ///   import static org.example.function1;
    ///
    ///   import org.example.ClassA;
    /// \endcode
    SJSIO_Before,
    /// Static imports are placed after non-static imports.
    /// \code{.java}
    ///   import org.example.ClassA;
    ///
    ///   import static org.example.function1;
    /// \endcode
    SJSIO_After,
  };

  /// When sorting Java imports, by default static imports are placed before
  /// non-static imports. If ``JavaStaticImportAfterImport`` is ``After``,
  /// static imports are placed after non-static imports.
  /// \version 12
  SortJavaStaticImportOptions SortJavaStaticImport;

  /// Using declaration sorting options.
  enum SortUsingDeclarationsOptions : int8_t {
    /// Using declarations are never sorted.
    /// \code
    ///    using std::chrono::duration_cast;
    ///    using std::move;
    ///    using boost::regex;
    ///    using boost::regex_constants::icase;
    ///    using std::string;
    /// \endcode
    SUD_Never,
    /// Using declarations are sorted in the order defined as follows:
    /// Split the strings by ``::`` and discard any initial empty strings. Sort
    /// the lists of names lexicographically, and within those groups, names are
    /// in case-insensitive lexicographic order.
    /// \code
    ///    using boost::regex;
    ///    using boost::regex_constants::icase;
    ///    using std::chrono::duration_cast;
    ///    using std::move;
    ///    using std::string;
    /// \endcode
    SUD_Lexicographic,
    /// Using declarations are sorted in the order defined as follows:
    /// Split the strings by ``::`` and discard any initial empty strings. The
    /// last element of each list is a non-namespace name; all others are
    /// namespace names. Sort the lists of names lexicographically, where the
    /// sort order of individual names is that all non-namespace names come
    /// before all namespace names, and within those groups, names are in
    /// case-insensitive lexicographic order.
    /// \code
    ///    using boost::regex;
    ///    using boost::regex_constants::icase;
    ///    using std::move;
    ///    using std::string;
    ///    using std::chrono::duration_cast;
    /// \endcode
    SUD_LexicographicNumeric,
  };

  /// Controls if and how clang-format will sort using declarations.
  /// \version 5
  SortUsingDeclarationsOptions SortUsingDeclarations;

  /// If ``true``, a space is inserted after C style casts.
  /// \code
  ///    true:                                  false:
  ///    (int) i;                       vs.     (int)i;
  /// \endcode
  /// \version 3.5
  bool SpaceAfterCStyleCast;

  /// If ``true``, a space is inserted after the logical not operator (``!``).
  /// \code
  ///    true:                                  false:
  ///    ! someExpression();            vs.     !someExpression();
  /// \endcode
  /// \version 9
  bool SpaceAfterLogicalNot;

  /// If \c true, a space will be inserted after the ``template`` keyword.
  /// \code
  ///    true:                                  false:
  ///    template <int> void foo();     vs.     template<int> void foo();
  /// \endcode
  /// \version 4
  bool SpaceAfterTemplateKeyword;

  /// Different ways to put a space before opening parentheses.
  enum SpaceAroundPointerQualifiersStyle : int8_t {
    /// Don't ensure spaces around pointer qualifiers and use PointerAlignment
    /// instead.
    /// \code
    ///    PointerAlignment: Left                 PointerAlignment: Right
    ///    void* const* x = NULL;         vs.     void *const *x = NULL;
    /// \endcode
    SAPQ_Default,
    /// Ensure that there is a space before pointer qualifiers.
    /// \code
    ///    PointerAlignment: Left                 PointerAlignment: Right
    ///    void* const* x = NULL;         vs.     void * const *x = NULL;
    /// \endcode
    SAPQ_Before,
    /// Ensure that there is a space after pointer qualifiers.
    /// \code
    ///    PointerAlignment: Left                 PointerAlignment: Right
    ///    void* const * x = NULL;         vs.     void *const *x = NULL;
    /// \endcode
    SAPQ_After,
    /// Ensure that there is a space both before and after pointer qualifiers.
    /// \code
    ///    PointerAlignment: Left                 PointerAlignment: Right
    ///    void* const * x = NULL;         vs.     void * const *x = NULL;
    /// \endcode
    SAPQ_Both,
  };

  ///  Defines in which cases to put a space before or after pointer qualifiers
  /// \version 12
  SpaceAroundPointerQualifiersStyle SpaceAroundPointerQualifiers;

  /// If ``false``, spaces will be removed before assignment operators.
  /// \code
  ///    true:                                  false:
  ///    int a = 5;                     vs.     int a= 5;
  ///    a += 42;                               a+= 42;
  /// \endcode
  /// \version 3.7
  bool SpaceBeforeAssignmentOperators;

  /// If ``false``, spaces will be removed before case colon.
  /// \code
  ///   true:                                   false
  ///   switch (x) {                    vs.     switch (x) {
  ///     case 1 : break;                         case 1: break;
  ///   }                                       }
  /// \endcode
  /// \version 12
  bool SpaceBeforeCaseColon;

  /// If ``true``, a space will be inserted before a C++11 braced list
  /// used to initialize an object (after the preceding identifier or type).
  /// \code
  ///    true:                                  false:
  ///    Foo foo { bar };               vs.     Foo foo{ bar };
  ///    Foo {};                                Foo{};
  ///    vector<int> { 1, 2, 3 };               vector<int>{ 1, 2, 3 };
  ///    new int[3] { 1, 2, 3 };                new int[3]{ 1, 2, 3 };
  /// \endcode
  /// \version 7
  bool SpaceBeforeCpp11BracedList;

  /// If ``false``, spaces will be removed before constructor initializer
  /// colon.
  /// \code
  ///    true:                                  false:
  ///    Foo::Foo() : a(a) {}                   Foo::Foo(): a(a) {}
  /// \endcode
  /// \version 7
  bool SpaceBeforeCtorInitializerColon;

  /// If ``false``, spaces will be removed before inheritance colon.
  /// \code
  ///    true:                                  false:
  ///    class Foo : Bar {}             vs.     class Foo: Bar {}
  /// \endcode
  /// \version 7
  bool SpaceBeforeInheritanceColon;

  /// If ``true``, a space will be added before a JSON colon. For other
  /// languages, e.g. JavaScript, use ``SpacesInContainerLiterals`` instead.
  /// \code
  ///    true:                                  false:
  ///    {                                      {
  ///      "key" : "value"              vs.       "key": "value"
  ///    }                                      }
  /// \endcode
  /// \version 17
  bool SpaceBeforeJsonColon;

  /// Different ways to put a space before opening parentheses.
  enum SpaceBeforeParensStyle : int8_t {
    /// This is **deprecated** and replaced by ``Custom`` below, with all
    /// ``SpaceBeforeParensOptions`` but ``AfterPlacementOperator`` set to
    /// ``false``.
    SBPO_Never,
    /// Put a space before opening parentheses only after control statement
    /// keywords (``for/if/while...``).
    /// \code
    ///    void f() {
    ///      if (true) {
    ///        f();
    ///      }
    ///    }
    /// \endcode
    SBPO_ControlStatements,
    /// Same as ``SBPO_ControlStatements`` except this option doesn't apply to
    /// ForEach and If macros. This is useful in projects where ForEach/If
    /// macros are treated as function calls instead of control statements.
    /// ``SBPO_ControlStatementsExceptForEachMacros`` remains an alias for
    /// backward compatibility.
    /// \code
    ///    void f() {
    ///      Q_FOREACH(...) {
    ///        f();
    ///      }
    ///    }
    /// \endcode
    SBPO_ControlStatementsExceptControlMacros,
    /// Put a space before opening parentheses only if the parentheses are not
    /// empty.
    /// \code
    ///   void() {
    ///     if (true) {
    ///       f();
    ///       g (x, y, z);
    ///     }
    ///   }
    /// \endcode
    SBPO_NonEmptyParentheses,
    /// Always put a space before opening parentheses, except when it's
    /// prohibited by the syntax rules (in function-like macro definitions) or
    /// when determined by other style rules (after unary operators, opening
    /// parentheses, etc.)
    /// \code
    ///    void f () {
    ///      if (true) {
    ///        f ();
    ///      }
    ///    }
    /// \endcode
    SBPO_Always,
    /// Configure each individual space before parentheses in
    /// ``SpaceBeforeParensOptions``.
    SBPO_Custom,
  };

  /// Defines in which cases to put a space before opening parentheses.
  /// \version 3.5
  SpaceBeforeParensStyle SpaceBeforeParens;

  /// Precise control over the spacing before parentheses.
  /// \code
  ///   # Should be declared this way:
  ///   SpaceBeforeParens: Custom
  ///   SpaceBeforeParensOptions:
  ///     AfterControlStatements: true
  ///     AfterFunctionDefinitionName: true
  /// \endcode
  struct SpaceBeforeParensCustom {
    /// If ``true``, put space between control statement keywords
    /// (for/if/while...) and opening parentheses.
    /// \code
    ///    true:                                  false:
    ///    if (...) {}                     vs.    if(...) {}
    /// \endcode
    bool AfterControlStatements;
    /// If ``true``, put space between foreach macros and opening parentheses.
    /// \code
    ///    true:                                  false:
    ///    FOREACH (...)                   vs.    FOREACH(...)
    ///      <loop-body>                            <loop-body>
    /// \endcode
    bool AfterForeachMacros;
    /// If ``true``, put a space between function declaration name and opening
    /// parentheses.
    /// \code
    ///    true:                                  false:
    ///    void f ();                      vs.    void f();
    /// \endcode
    bool AfterFunctionDeclarationName;
    /// If ``true``, put a space between function definition name and opening
    /// parentheses.
    /// \code
    ///    true:                                  false:
    ///    void f () {}                    vs.    void f() {}
    /// \endcode
    bool AfterFunctionDefinitionName;
    /// If ``true``, put space between if macros and opening parentheses.
    /// \code
    ///    true:                                  false:
    ///    IF (...)                        vs.    IF(...)
    ///      <conditional-body>                     <conditional-body>
    /// \endcode
    bool AfterIfMacros;
    /// If ``true``, put a space between operator overloading and opening
    /// parentheses.
    /// \code
    ///    true:                                  false:
    ///    void operator++ (int a);        vs.    void operator++(int a);
    ///    object.operator++ (10);                object.operator++(10);
    /// \endcode
    bool AfterOverloadedOperator;
    /// If ``true``, put a space between operator ``new``/``delete`` and opening
    /// parenthesis.
    /// \code
    ///    true:                                  false:
    ///    new (buf) T;                    vs.    new(buf) T;
    ///    delete (buf) T;                        delete(buf) T;
    /// \endcode
    bool AfterPlacementOperator;
    /// If ``true``, put space between requires keyword in a requires clause and
    /// opening parentheses, if there is one.
    /// \code
    ///    true:                                  false:
    ///    template<typename T>            vs.    template<typename T>
    ///    requires (A<T> && B<T>)                requires(A<T> && B<T>)
    ///    ...                                    ...
    /// \endcode
    bool AfterRequiresInClause;
    /// If ``true``, put space between requires keyword in a requires expression
    /// and opening parentheses.
    /// \code
    ///    true:                                  false:
    ///    template<typename T>            vs.    template<typename T>
    ///    concept C = requires (T t) {           concept C = requires(T t) {
    ///                  ...                                    ...
    ///                }                                      }
    /// \endcode
    bool AfterRequiresInExpression;
    /// If ``true``, put a space before opening parentheses only if the
    /// parentheses are not empty.
    /// \code
    ///    true:                                  false:
    ///    void f (int a);                 vs.    void f();
    ///    f (a);                                 f();
    /// \endcode
    bool BeforeNonEmptyParentheses;

    SpaceBeforeParensCustom()
        : AfterControlStatements(false), AfterForeachMacros(false),
          AfterFunctionDeclarationName(false),
          AfterFunctionDefinitionName(false), AfterIfMacros(false),
          AfterOverloadedOperator(false), AfterPlacementOperator(true),
          AfterRequiresInClause(false), AfterRequiresInExpression(false),
          BeforeNonEmptyParentheses(false) {}

    bool operator==(const SpaceBeforeParensCustom &Other) const {
      return AfterControlStatements == Other.AfterControlStatements &&
             AfterForeachMacros == Other.AfterForeachMacros &&
             AfterFunctionDeclarationName ==
                 Other.AfterFunctionDeclarationName &&
             AfterFunctionDefinitionName == Other.AfterFunctionDefinitionName &&
             AfterIfMacros == Other.AfterIfMacros &&
             AfterOverloadedOperator == Other.AfterOverloadedOperator &&
             AfterPlacementOperator == Other.AfterPlacementOperator &&
             AfterRequiresInClause == Other.AfterRequiresInClause &&
             AfterRequiresInExpression == Other.AfterRequiresInExpression &&
             BeforeNonEmptyParentheses == Other.BeforeNonEmptyParentheses;
    }
  };

  /// Control of individual space before parentheses.
  ///
  /// If ``SpaceBeforeParens`` is set to ``Custom``, use this to specify
  /// how each individual space before parentheses case should be handled.
  /// Otherwise, this is ignored.
  /// \code{.yaml}
  ///   # Example of usage:
  ///   SpaceBeforeParens: Custom
  ///   SpaceBeforeParensOptions:
  ///     AfterControlStatements: true
  ///     AfterFunctionDefinitionName: true
  /// \endcode
  /// \version 14
  SpaceBeforeParensCustom SpaceBeforeParensOptions;

  /// If ``true``, spaces will be before  ``[``.
  /// Lambdas will not be affected. Only the first ``[`` will get a space added.
  /// \code
  ///    true:                                  false:
  ///    int a [5];                    vs.      int a[5];
  ///    int a [5][5];                 vs.      int a[5][5];
  /// \endcode
  /// \version 10
  bool SpaceBeforeSquareBrackets;

  /// If ``false``, spaces will be removed before range-based for loop
  /// colon.
  /// \code
  ///    true:                                  false:
  ///    for (auto v : values) {}       vs.     for(auto v: values) {}
  /// \endcode
  /// \version 7
  bool SpaceBeforeRangeBasedForLoopColon;

  /// If ``true``, spaces will be inserted into ``{}``.
  /// \code
  ///    true:                                false:
  ///    void f() { }                   vs.   void f() {}
  ///    while (true) { }                     while (true) {}
  /// \endcode
  /// \version 10
  bool SpaceInEmptyBlock;

  /// If ``true``, spaces may be inserted into ``()``.
  /// This option is **deprecated**. See ``InEmptyParentheses`` of
  /// ``SpacesInParensOptions``.
  /// \version 3.7
  // bool SpaceInEmptyParentheses;

  /// The number of spaces before trailing line comments
  /// (``//`` - comments).
  ///
  /// This does not affect trailing block comments (``/*`` - comments) as those
  /// commonly have different usage patterns and a number of special cases.  In
  /// the case of Verilog, it doesn't affect a comment right after the opening
  /// parenthesis in the port or parameter list in a module header, because it
  /// is probably for the port on the following line instead of the parenthesis
  /// it follows.
  /// \code
  ///    SpacesBeforeTrailingComments: 3
  ///    void f() {
  ///      if (true) {   // foo1
  ///        f();        // bar
  ///      }             // foo
  ///    }
  /// \endcode
  /// \version 3.7
  unsigned SpacesBeforeTrailingComments;

  /// Styles for adding spacing after ``<`` and before ``>``
  ///  in template argument lists.
  enum SpacesInAnglesStyle : int8_t {
    /// Remove spaces after ``<`` and before ``>``.
    /// \code
    ///    static_cast<int>(arg);
    ///    std::function<void(int)> fct;
    /// \endcode
    SIAS_Never,
    /// Add spaces after ``<`` and before ``>``.
    /// \code
    ///    static_cast< int >(arg);
    ///    std::function< void(int) > fct;
    /// \endcode
    SIAS_Always,
    /// Keep a single space after ``<`` and before ``>`` if any spaces were
    /// present. Option ``Standard: Cpp03`` takes precedence.
    SIAS_Leave
  };
  /// The SpacesInAnglesStyle to use for template argument lists.
  /// \version 3.4
  SpacesInAnglesStyle SpacesInAngles;

  /// If ``true``, spaces will be inserted around if/for/switch/while
  /// conditions.
  /// This option is **deprecated**. See ``InConditionalStatements`` of
  /// ``SpacesInParensOptions``.
  /// \version 10
  // bool SpacesInConditionalStatement;

  /// If ``true``, spaces are inserted inside container literals (e.g.  ObjC and
  /// Javascript array and dict literals). For JSON, use
  /// ``SpaceBeforeJsonColon`` instead.
  /// \code{.js}
  ///    true:                                  false:
  ///    var arr = [ 1, 2, 3 ];         vs.     var arr = [1, 2, 3];
  ///    f({a : 1, b : 2, c : 3});              f({a: 1, b: 2, c: 3});
  /// \endcode
  /// \version 3.7
  bool SpacesInContainerLiterals;

  /// If ``true``, spaces may be inserted into C style casts.
  /// This option is **deprecated**. See ``InCStyleCasts`` of
  /// ``SpacesInParensOptions``.
  /// \version 3.7
  // bool SpacesInCStyleCastParentheses;

  /// Control of spaces within a single line comment.
  struct SpacesInLineComment {
    /// The minimum number of spaces at the start of the comment.
    unsigned Minimum;
    /// The maximum number of spaces at the start of the comment.
    unsigned Maximum;
  };

  /// How many spaces are allowed at the start of a line comment. To disable the
  /// maximum set it to ``-1``, apart from that the maximum takes precedence
  /// over the minimum.
  /// \code
  ///   Minimum = 1
  ///   Maximum = -1
  ///   // One space is forced
  ///
  ///   //  but more spaces are possible
  ///
  ///   Minimum = 0
  ///   Maximum = 0
  ///   //Forces to start every comment directly after the slashes
  /// \endcode
  ///
  /// Note that in line comment sections the relative indent of the subsequent
  /// lines is kept, that means the following:
  /// \code
  ///   before:                                   after:
  ///   Minimum: 1
  ///   //if (b) {                                // if (b) {
  ///   //  return true;                          //   return true;
  ///   //}                                       // }
  ///
  ///   Maximum: 0
  ///   /// List:                                 ///List:
  ///   ///  - Foo                                /// - Foo
  ///   ///    - Bar                              ///   - Bar
  /// \endcode
  ///
  /// This option has only effect if ``ReflowComments`` is set to ``true``.
  /// \version 13
  SpacesInLineComment SpacesInLineCommentPrefix;

  /// Different ways to put a space before opening and closing parentheses.
  enum SpacesInParensStyle : int8_t {
    /// Never put a space in parentheses.
    /// \code
    ///    void f() {
    ///      if(true) {
    ///        f();
    ///      }
    ///    }
    /// \endcode
    SIPO_Never,
    /// Configure each individual space in parentheses in
    /// `SpacesInParensOptions`.
    SIPO_Custom,
  };

  /// If ``true``, spaces will be inserted after ``(`` and before ``)``.
  /// This option is **deprecated**. The previous behavior is preserved by using
  /// ``SpacesInParens`` with ``Custom`` and by setting all
  /// ``SpacesInParensOptions`` to ``true`` except for ``InCStyleCasts`` and
  /// ``InEmptyParentheses``.
  /// \version 3.7
  // bool SpacesInParentheses;

  /// Defines in which cases spaces will be inserted after ``(`` and before
  /// ``)``.
  /// \version 17
  SpacesInParensStyle SpacesInParens;

  /// Precise control over the spacing in parentheses.
  /// \code
  ///   # Should be declared this way:
  ///   SpacesInParens: Custom
  ///   SpacesInParensOptions:
  ///     ExceptDoubleParentheses: false
  ///     InConditionalStatements: true
  ///     Other: true
  /// \endcode
  struct SpacesInParensCustom {
    /// Override any of the following options to prevent addition of space
    /// when both opening and closing parentheses use multiple parentheses.
    /// \code
    ///   true:
    ///   __attribute__(( noreturn ))
    ///   __decltype__(( x ))
    ///   if (( a = b ))
    /// \endcode
    ///  false:
    ///    Uses the applicable option.
    bool ExceptDoubleParentheses;
    /// Put a space in parentheses only inside conditional statements
    /// (``for/if/while/switch...``).
    /// \code
    ///    true:                                  false:
    ///    if ( a )  { ... }              vs.     if (a) { ... }
    ///    while ( i < 5 )  { ... }               while (i < 5) { ... }
    /// \endcode
    bool InConditionalStatements;
    /// Put a space in C style casts.
    /// \code
    ///   true:                                  false:
    ///   x = ( int32 )y                  vs.    x = (int32)y
    ///   y = (( int (*)(int) )foo)(x);          y = ((int (*)(int))foo)(x);
    /// \endcode
    bool InCStyleCasts;
    /// Insert a space in empty parentheses, i.e. ``()``.
    /// \code
    ///    true:                                false:
    ///    void f( ) {                    vs.   void f() {
    ///      int x[] = {foo( ), bar( )};          int x[] = {foo(), bar()};
    ///      if (true) {                          if (true) {
    ///        f( );                                f();
    ///      }                                    }
    ///    }                                    }
    /// \endcode
    bool InEmptyParentheses;
    /// Put a space in parentheses not covered by preceding options.
    /// \code
    ///   true:                                 false:
    ///   t f( Deleted & ) & = delete;    vs.   t f(Deleted &) & = delete;
    /// \endcode
    bool Other;

    SpacesInParensCustom()
        : ExceptDoubleParentheses(false), InConditionalStatements(false),
          InCStyleCasts(false), InEmptyParentheses(false), Other(false) {}

    SpacesInParensCustom(bool ExceptDoubleParentheses,
                         bool InConditionalStatements, bool InCStyleCasts,
                         bool InEmptyParentheses, bool Other)
        : ExceptDoubleParentheses(ExceptDoubleParentheses),
          InConditionalStatements(InConditionalStatements),
          InCStyleCasts(InCStyleCasts), InEmptyParentheses(InEmptyParentheses),
          Other(Other) {}

    bool operator==(const SpacesInParensCustom &R) const {
      return ExceptDoubleParentheses == R.ExceptDoubleParentheses &&
             InConditionalStatements == R.InConditionalStatements &&
             InCStyleCasts == R.InCStyleCasts &&
             InEmptyParentheses == R.InEmptyParentheses && Other == R.Other;
    }
    bool operator!=(const SpacesInParensCustom &R) const {
      return !(*this == R);
    }
  };

  /// Control of individual spaces in parentheses.
  ///
  /// If ``SpacesInParens`` is set to ``Custom``, use this to specify
  /// how each individual space in parentheses case should be handled.
  /// Otherwise, this is ignored.
  /// \code{.yaml}
  ///   # Example of usage:
  ///   SpacesInParens: Custom
  ///   SpacesInParensOptions:
  ///     ExceptDoubleParentheses: false
  ///     InConditionalStatements: true
  ///     InEmptyParentheses: true
  /// \endcode
  /// \version 17
  SpacesInParensCustom SpacesInParensOptions;

  /// If ``true``, spaces will be inserted after ``[`` and before ``]``.
  /// Lambdas without arguments or unspecified size array declarations will not
  /// be affected.
  /// \code
  ///    true:                                  false:
  ///    int a[ 5 ];                    vs.     int a[5];
  ///    std::unique_ptr<int[]> foo() {} // Won't be affected
  /// \endcode
  /// \version 3.7
  bool SpacesInSquareBrackets;

  /// Supported language standards for parsing and formatting C++ constructs.
  /// \code
  ///    Latest:                                vector<set<int>>
  ///    c++03                          vs.     vector<set<int> >
  /// \endcode
  ///
  /// The correct way to spell a specific language version is e.g. ``c++11``.
  /// The historical aliases ``Cpp03`` and ``Cpp11`` are deprecated.
  enum LanguageStandard : int8_t {
    /// Parse and format as C++03.
    /// ``Cpp03`` is a deprecated alias for ``c++03``
    LS_Cpp03, // c++03
    /// Parse and format as C++11.
    LS_Cpp11, // c++11
    /// Parse and format as C++14.
    LS_Cpp14, // c++14
    /// Parse and format as C++17.
    LS_Cpp17, // c++17
    /// Parse and format as C++20.
    LS_Cpp20, // c++20
    /// Parse and format using the latest supported language version.
    /// ``Cpp11`` is a deprecated alias for ``Latest``
    LS_Latest,
    /// Automatic detection based on the input.
    LS_Auto,
  };

  /// Parse and format C++ constructs compatible with this standard.
  /// \code
  ///    c++03:                                 latest:
  ///    vector<set<int> > x;           vs.     vector<set<int>> x;
  /// \endcode
  /// \version 3.7
  LanguageStandard Standard;

  /// Macros which are ignored in front of a statement, as if they were an
  /// attribute. So that they are not parsed as identifier, for example for Qts
  /// emit.
  /// \code
  ///   AlignConsecutiveDeclarations: true
  ///   StatementAttributeLikeMacros: []
  ///   unsigned char data = 'x';
  ///   emit          signal(data); // This is parsed as variable declaration.
  ///
  ///   AlignConsecutiveDeclarations: true
  ///   StatementAttributeLikeMacros: [emit]
  ///   unsigned char data = 'x';
  ///   emit signal(data); // Now it's fine again.
  /// \endcode
  /// \version 12
  std::vector<std::string> StatementAttributeLikeMacros;

  /// A vector of macros that should be interpreted as complete
  /// statements.
  ///
  /// Typical macros are expressions, and require a semi-colon to be
  /// added; sometimes this is not the case, and this allows to make
  /// clang-format aware of such cases.
  ///
  /// For example: Q_UNUSED
  /// \version 8
  std::vector<std::string> StatementMacros;

  /// Works only when TableGenBreakInsideDAGArg is not DontBreak.
  /// The string list needs to consist of identifiers in TableGen.
  /// If any identifier is specified, this limits the line breaks by
  /// TableGenBreakInsideDAGArg option only on DAGArg values beginning with
  /// the specified identifiers.
  ///
  /// For example the configuration,
  /// \code{.yaml}
  ///   TableGenBreakInsideDAGArg: BreakAll
  ///   TableGenBreakingDAGArgOperators: [ins, outs]
  /// \endcode
  ///
  /// makes the line break only occurs inside DAGArgs beginning with the
  /// specified identifiers ``ins`` and ``outs``.
  ///
  /// \code
  ///   let DAGArgIns = (ins
  ///       i32:$src1,
  ///       i32:$src2
  ///   );
  ///   let DAGArgOtherID = (other i32:$other1, i32:$other2);
  ///   let DAGArgBang = (!cast<SomeType>("Some") i32:$src1, i32:$src2)
  /// \endcode
  /// \version 19
  std::vector<std::string> TableGenBreakingDAGArgOperators;

  /// Different ways to control the format inside TableGen DAGArg.
  enum DAGArgStyle : int8_t {
    /// Never break inside DAGArg.
    /// \code
    ///   let DAGArgIns = (ins i32:$src1, i32:$src2);
    /// \endcode
    DAS_DontBreak,
    /// Break inside DAGArg after each list element but for the last.
    /// This aligns to the first element.
    /// \code
    ///   let DAGArgIns = (ins i32:$src1,
    ///                        i32:$src2);
    /// \endcode
    DAS_BreakElements,
    /// Break inside DAGArg after the operator and the all elements.
    /// \code
    ///   let DAGArgIns = (ins
    ///       i32:$src1,
    ///       i32:$src2
    ///   );
    /// \endcode
    DAS_BreakAll,
  };

  /// The styles of the line break inside the DAGArg in TableGen.
  /// \version 19
  DAGArgStyle TableGenBreakInsideDAGArg;

  /// The number of columns used for tab stops.
  /// \version 3.7
  unsigned TabWidth;

  /// A vector of non-keyword identifiers that should be interpreted as type
  /// names.
  ///
  /// A ``*``, ``&``, or ``&&`` between a type name and another non-keyword
  /// identifier is annotated as a pointer or reference token instead of a
  /// binary operator.
  ///
  /// \version 17
  std::vector<std::string> TypeNames;

  /// \brief A vector of macros that should be interpreted as type declarations
  /// instead of as function calls.
  ///
  /// These are expected to be macros of the form:
  /// \code
  ///   STACK_OF(...)
  /// \endcode
  ///
  /// In the .clang-format configuration file, this can be configured like:
  /// \code{.yaml}
  ///   TypenameMacros: [STACK_OF, LIST]
  /// \endcode
  ///
  /// For example: OpenSSL STACK_OF, BSD LIST_ENTRY.
  /// \version 9
  std::vector<std::string> TypenameMacros;

  /// This option is **deprecated**. See ``LF`` and ``CRLF`` of ``LineEnding``.
  /// \version 10
  // bool UseCRLF;

  /// Different ways to use tab in formatting.
  enum UseTabStyle : int8_t {
    /// Never use tab.
    UT_Never,
    /// Use tabs only for indentation.
    UT_ForIndentation,
    /// Fill all leading whitespace with tabs, and use spaces for alignment that
    /// appears within a line (e.g. consecutive assignments and declarations).
    UT_ForContinuationAndIndentation,
    /// Use tabs for line continuation and indentation, and spaces for
    /// alignment.
    UT_AlignWithSpaces,
    /// Use tabs whenever we need to fill whitespace that spans at least from
    /// one tab stop to the next one.
    UT_Always
  };

  /// The way to use tab characters in the resulting file.
  /// \version 3.7
  UseTabStyle UseTab;

  /// For Verilog, put each port on its own line in module instantiations.
  /// \code
  ///    true:
  ///    ffnand ff1(.q(),
  ///               .qbar(out1),
  ///               .clear(in1),
  ///               .preset(in2));
  ///
  ///    false:
  ///    ffnand ff1(.q(), .qbar(out1), .clear(in1), .preset(in2));
  /// \endcode
  /// \version 17
  bool VerilogBreakBetweenInstancePorts;

  /// A vector of macros which are whitespace-sensitive and should not
  /// be touched.
  ///
  /// These are expected to be macros of the form:
  /// \code
  ///   STRINGIZE(...)
  /// \endcode
  ///
  /// In the .clang-format configuration file, this can be configured like:
  /// \code{.yaml}
  ///   WhitespaceSensitiveMacros: [STRINGIZE, PP_STRINGIZE]
  /// \endcode
  ///
  /// For example: BOOST_PP_STRINGIZE
  /// \version 11
  std::vector<std::string> WhitespaceSensitiveMacros;

  bool operator==(const FormatStyle &R) const {
    return AccessModifierOffset == R.AccessModifierOffset &&
           AlignAfterOpenBracket == R.AlignAfterOpenBracket &&
           AlignArrayOfStructures == R.AlignArrayOfStructures &&
           AlignConsecutiveAssignments == R.AlignConsecutiveAssignments &&
           AlignConsecutiveBitFields == R.AlignConsecutiveBitFields &&
           AlignConsecutiveDeclarations == R.AlignConsecutiveDeclarations &&
           AlignConsecutiveMacros == R.AlignConsecutiveMacros &&
           AlignConsecutiveShortCaseStatements ==
               R.AlignConsecutiveShortCaseStatements &&
           AlignConsecutiveTableGenBreakingDAGArgColons ==
               R.AlignConsecutiveTableGenBreakingDAGArgColons &&
           AlignConsecutiveTableGenCondOperatorColons ==
               R.AlignConsecutiveTableGenCondOperatorColons &&
           AlignConsecutiveTableGenDefinitionColons ==
               R.AlignConsecutiveTableGenDefinitionColons &&
           AlignEscapedNewlines == R.AlignEscapedNewlines &&
           AlignOperands == R.AlignOperands &&
           AlignTrailingComments == R.AlignTrailingComments &&
           AllowAllArgumentsOnNextLine == R.AllowAllArgumentsOnNextLine &&
           AllowAllParametersOfDeclarationOnNextLine ==
               R.AllowAllParametersOfDeclarationOnNextLine &&
           AllowBreakBeforeNoexceptSpecifier ==
               R.AllowBreakBeforeNoexceptSpecifier &&
           AllowShortBlocksOnASingleLine == R.AllowShortBlocksOnASingleLine &&
           AllowShortCaseExpressionOnASingleLine ==
               R.AllowShortCaseExpressionOnASingleLine &&
           AllowShortCaseLabelsOnASingleLine ==
               R.AllowShortCaseLabelsOnASingleLine &&
           AllowShortCompoundRequirementOnASingleLine ==
               R.AllowShortCompoundRequirementOnASingleLine &&
           AllowShortEnumsOnASingleLine == R.AllowShortEnumsOnASingleLine &&
           AllowShortFunctionsOnASingleLine ==
               R.AllowShortFunctionsOnASingleLine &&
           AllowShortIfStatementsOnASingleLine ==
               R.AllowShortIfStatementsOnASingleLine &&
           AllowShortLambdasOnASingleLine == R.AllowShortLambdasOnASingleLine &&
           AllowShortLoopsOnASingleLine == R.AllowShortLoopsOnASingleLine &&
           AlwaysBreakBeforeMultilineStrings ==
               R.AlwaysBreakBeforeMultilineStrings &&
           AttributeMacros == R.AttributeMacros &&
           BinPackArguments == R.BinPackArguments &&
           BinPackParameters == R.BinPackParameters &&
           BitFieldColonSpacing == R.BitFieldColonSpacing &&
           BracedInitializerIndentWidth == R.BracedInitializerIndentWidth &&
           BreakAdjacentStringLiterals == R.BreakAdjacentStringLiterals &&
           BreakAfterAttributes == R.BreakAfterAttributes &&
           BreakAfterJavaFieldAnnotations == R.BreakAfterJavaFieldAnnotations &&
           BreakAfterReturnType == R.BreakAfterReturnType &&
           BreakArrays == R.BreakArrays &&
           BreakBeforeBinaryOperators == R.BreakBeforeBinaryOperators &&
           BreakBeforeBraces == R.BreakBeforeBraces &&
           BreakBeforeConceptDeclarations == R.BreakBeforeConceptDeclarations &&
           BreakBeforeInlineASMColon == R.BreakBeforeInlineASMColon &&
           BreakBeforeTernaryOperators == R.BreakBeforeTernaryOperators &&
           BreakConstructorInitializers == R.BreakConstructorInitializers &&
           BreakFunctionDefinitionParameters ==
               R.BreakFunctionDefinitionParameters &&
           BreakInheritanceList == R.BreakInheritanceList &&
           BreakStringLiterals == R.BreakStringLiterals &&
           BreakTemplateDeclarations == R.BreakTemplateDeclarations &&
           ColumnLimit == R.ColumnLimit && CommentPragmas == R.CommentPragmas &&
           CompactNamespaces == R.CompactNamespaces &&
           ConstructorInitializerIndentWidth ==
               R.ConstructorInitializerIndentWidth &&
           ContinuationIndentWidth == R.ContinuationIndentWidth &&
           Cpp11BracedListStyle == R.Cpp11BracedListStyle &&
           DerivePointerAlignment == R.DerivePointerAlignment &&
           DisableFormat == R.DisableFormat &&
           EmptyLineAfterAccessModifier == R.EmptyLineAfterAccessModifier &&
           EmptyLineBeforeAccessModifier == R.EmptyLineBeforeAccessModifier &&
           ExperimentalAutoDetectBinPacking ==
               R.ExperimentalAutoDetectBinPacking &&
           FixNamespaceComments == R.FixNamespaceComments &&
           ForEachMacros == R.ForEachMacros &&
           IncludeStyle.IncludeBlocks == R.IncludeStyle.IncludeBlocks &&
           IncludeStyle.IncludeCategories == R.IncludeStyle.IncludeCategories &&
           IncludeStyle.IncludeIsMainRegex ==
               R.IncludeStyle.IncludeIsMainRegex &&
           IncludeStyle.IncludeIsMainSourceRegex ==
               R.IncludeStyle.IncludeIsMainSourceRegex &&
           IncludeStyle.MainIncludeChar == R.IncludeStyle.MainIncludeChar &&
           IndentAccessModifiers == R.IndentAccessModifiers &&
           IndentCaseBlocks == R.IndentCaseBlocks &&
           IndentCaseLabels == R.IndentCaseLabels &&
           IndentExternBlock == R.IndentExternBlock &&
           IndentGotoLabels == R.IndentGotoLabels &&
           IndentPPDirectives == R.IndentPPDirectives &&
           IndentRequiresClause == R.IndentRequiresClause &&
           IndentWidth == R.IndentWidth &&
           IndentWrappedFunctionNames == R.IndentWrappedFunctionNames &&
           InsertBraces == R.InsertBraces &&
           InsertNewlineAtEOF == R.InsertNewlineAtEOF &&
           IntegerLiteralSeparator == R.IntegerLiteralSeparator &&
           JavaImportGroups == R.JavaImportGroups &&
           JavaScriptQuotes == R.JavaScriptQuotes &&
           JavaScriptWrapImports == R.JavaScriptWrapImports &&
           KeepEmptyLines == R.KeepEmptyLines && Language == R.Language &&
           LambdaBodyIndentation == R.LambdaBodyIndentation &&
           LineEnding == R.LineEnding && MacroBlockBegin == R.MacroBlockBegin &&
           MacroBlockEnd == R.MacroBlockEnd && Macros == R.Macros &&
           MaxEmptyLinesToKeep == R.MaxEmptyLinesToKeep &&
           NamespaceIndentation == R.NamespaceIndentation &&
           NamespaceMacros == R.NamespaceMacros &&
           ObjCBinPackProtocolList == R.ObjCBinPackProtocolList &&
           ObjCBlockIndentWidth == R.ObjCBlockIndentWidth &&
           ObjCBreakBeforeNestedBlockParam ==
               R.ObjCBreakBeforeNestedBlockParam &&
           ObjCPropertyAttributeOrder == R.ObjCPropertyAttributeOrder &&
           ObjCSpaceAfterProperty == R.ObjCSpaceAfterProperty &&
           ObjCSpaceBeforeProtocolList == R.ObjCSpaceBeforeProtocolList &&
           PackConstructorInitializers == R.PackConstructorInitializers &&
           PenaltyBreakAssignment == R.PenaltyBreakAssignment &&
           PenaltyBreakBeforeFirstCallParameter ==
               R.PenaltyBreakBeforeFirstCallParameter &&
           PenaltyBreakComment == R.PenaltyBreakComment &&
           PenaltyBreakFirstLessLess == R.PenaltyBreakFirstLessLess &&
           PenaltyBreakOpenParenthesis == R.PenaltyBreakOpenParenthesis &&
           PenaltyBreakScopeResolution == R.PenaltyBreakScopeResolution &&
           PenaltyBreakString == R.PenaltyBreakString &&
           PenaltyBreakTemplateDeclaration ==
               R.PenaltyBreakTemplateDeclaration &&
           PenaltyExcessCharacter == R.PenaltyExcessCharacter &&
           PenaltyReturnTypeOnItsOwnLine == R.PenaltyReturnTypeOnItsOwnLine &&
           PointerAlignment == R.PointerAlignment &&
           QualifierAlignment == R.QualifierAlignment &&
           QualifierOrder == R.QualifierOrder &&
           RawStringFormats == R.RawStringFormats &&
           ReferenceAlignment == R.ReferenceAlignment &&
           RemoveBracesLLVM == R.RemoveBracesLLVM &&
           RemoveParentheses == R.RemoveParentheses &&
           RemoveSemicolon == R.RemoveSemicolon &&
           RequiresClausePosition == R.RequiresClausePosition &&
           RequiresExpressionIndentation == R.RequiresExpressionIndentation &&
           SeparateDefinitionBlocks == R.SeparateDefinitionBlocks &&
           ShortNamespaceLines == R.ShortNamespaceLines &&
           SkipMacroDefinitionBody == R.SkipMacroDefinitionBody &&
           SortIncludes == R.SortIncludes &&
           SortJavaStaticImport == R.SortJavaStaticImport &&
           SpaceAfterCStyleCast == R.SpaceAfterCStyleCast &&
           SpaceAfterLogicalNot == R.SpaceAfterLogicalNot &&
           SpaceAfterTemplateKeyword == R.SpaceAfterTemplateKeyword &&
           SpaceBeforeAssignmentOperators == R.SpaceBeforeAssignmentOperators &&
           SpaceBeforeCaseColon == R.SpaceBeforeCaseColon &&
           SpaceBeforeCpp11BracedList == R.SpaceBeforeCpp11BracedList &&
           SpaceBeforeCtorInitializerColon ==
               R.SpaceBeforeCtorInitializerColon &&
           SpaceBeforeInheritanceColon == R.SpaceBeforeInheritanceColon &&
           SpaceBeforeJsonColon == R.SpaceBeforeJsonColon &&
           SpaceBeforeParens == R.SpaceBeforeParens &&
           SpaceBeforeParensOptions == R.SpaceBeforeParensOptions &&
           SpaceAroundPointerQualifiers == R.SpaceAroundPointerQualifiers &&
           SpaceBeforeRangeBasedForLoopColon ==
               R.SpaceBeforeRangeBasedForLoopColon &&
           SpaceBeforeSquareBrackets == R.SpaceBeforeSquareBrackets &&
           SpaceInEmptyBlock == R.SpaceInEmptyBlock &&
           SpacesBeforeTrailingComments == R.SpacesBeforeTrailingComments &&
           SpacesInAngles == R.SpacesInAngles &&
           SpacesInContainerLiterals == R.SpacesInContainerLiterals &&
           SpacesInLineCommentPrefix.Minimum ==
               R.SpacesInLineCommentPrefix.Minimum &&
           SpacesInLineCommentPrefix.Maximum ==
               R.SpacesInLineCommentPrefix.Maximum &&
           SpacesInParens == R.SpacesInParens &&
           SpacesInParensOptions == R.SpacesInParensOptions &&
           SpacesInSquareBrackets == R.SpacesInSquareBrackets &&
           Standard == R.Standard &&
           StatementAttributeLikeMacros == R.StatementAttributeLikeMacros &&
           StatementMacros == R.StatementMacros &&
           TableGenBreakingDAGArgOperators ==
               R.TableGenBreakingDAGArgOperators &&
           TableGenBreakInsideDAGArg == R.TableGenBreakInsideDAGArg &&
           TabWidth == R.TabWidth && TypeNames == R.TypeNames &&
           TypenameMacros == R.TypenameMacros && UseTab == R.UseTab &&
           VerilogBreakBetweenInstancePorts ==
               R.VerilogBreakBetweenInstancePorts &&
           WhitespaceSensitiveMacros == R.WhitespaceSensitiveMacros;
  }

  std::optional<FormatStyle> GetLanguageStyle(LanguageKind Language) const;

  // Stores per-language styles. A FormatStyle instance inside has an empty
  // StyleSet. A FormatStyle instance returned by the Get method has its
  // StyleSet set to a copy of the originating StyleSet, effectively keeping the
  // internal representation of that StyleSet alive.
  //
  // The memory management and ownership reminds of a birds nest: chicks
  // leaving the nest take photos of the nest with them.
  struct FormatStyleSet {
    typedef std::map<FormatStyle::LanguageKind, FormatStyle> MapType;

    std::optional<FormatStyle> Get(FormatStyle::LanguageKind Language) const;

    // Adds \p Style to this FormatStyleSet. Style must not have an associated
    // FormatStyleSet.
    // Style.Language should be different than LK_None. If this FormatStyleSet
    // already contains an entry for Style.Language, that gets replaced with the
    // passed Style.
    void Add(FormatStyle Style);

    // Clears this FormatStyleSet.
    void Clear();

  private:
    std::shared_ptr<MapType> Styles;
  };

  static FormatStyleSet BuildStyleSetFromConfiguration(
      const FormatStyle &MainStyle,
      const std::vector<FormatStyle> &ConfigurationStyles);

private:
  FormatStyleSet StyleSet;

  friend std::error_code
  parseConfiguration(llvm::MemoryBufferRef Config, FormatStyle *Style,
                     bool AllowUnknownOptions,
                     llvm::SourceMgr::DiagHandlerTy DiagHandler,
                     void *DiagHandlerCtxt);
};

/// Returns a format style complying with the LLVM coding standards:
/// http://llvm.org/docs/CodingStandards.html.
FormatStyle getLLVMStyle(
    FormatStyle::LanguageKind Language = FormatStyle::LanguageKind::LK_Cpp);

/// Returns a format style complying with one of Google's style guides:
/// http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml.
/// http://google-styleguide.googlecode.com/svn/trunk/javascriptguide.xml.
/// https://developers.google.com/protocol-buffers/docs/style.
FormatStyle getGoogleStyle(FormatStyle::LanguageKind Language);

/// Returns a format style complying with Chromium's style guide:
/// http://www.chromium.org/developers/coding-style.
FormatStyle getChromiumStyle(FormatStyle::LanguageKind Language);

/// Returns a format style complying with Mozilla's style guide:
/// https://firefox-source-docs.mozilla.org/code-quality/coding-style/index.html.
FormatStyle getMozillaStyle();

/// Returns a format style complying with Webkit's style guide:
/// http://www.webkit.org/coding/coding-style.html
FormatStyle getWebKitStyle();

/// Returns a format style complying with GNU Coding Standards:
/// http://www.gnu.org/prep/standards/standards.html
FormatStyle getGNUStyle();

/// Returns a format style complying with Microsoft style guide:
/// https://docs.microsoft.com/en-us/visualstudio/ide/editorconfig-code-style-settings-reference?view=vs-2017
FormatStyle getMicrosoftStyle(FormatStyle::LanguageKind Language);

FormatStyle getClangFormatStyle();

/// Returns style indicating formatting should be not applied at all.
FormatStyle getNoStyle();

/// Gets a predefined style for the specified language by name.
///
/// Currently supported names: LLVM, Google, Chromium, Mozilla. Names are
/// compared case-insensitively.
///
/// Returns ``true`` if the Style has been set.
bool getPredefinedStyle(StringRef Name, FormatStyle::LanguageKind Language,
                        FormatStyle *Style);

/// Parse configuration from YAML-formatted text.
///
/// Style->Language is used to get the base style, if the ``BasedOnStyle``
/// option is present.
///
/// The FormatStyleSet of Style is reset.
///
/// When ``BasedOnStyle`` is not present, options not present in the YAML
/// document, are retained in \p Style.
///
/// If AllowUnknownOptions is true, no errors are emitted if unknown
/// format options are occurred.
///
/// If set all diagnostics are emitted through the DiagHandler.
std::error_code
parseConfiguration(llvm::MemoryBufferRef Config, FormatStyle *Style,
                   bool AllowUnknownOptions = false,
                   llvm::SourceMgr::DiagHandlerTy DiagHandler = nullptr,
                   void *DiagHandlerCtx = nullptr);

/// Like above but accepts an unnamed buffer.
inline std::error_code parseConfiguration(StringRef Config, FormatStyle *Style,
                                          bool AllowUnknownOptions = false) {
  return parseConfiguration(llvm::MemoryBufferRef(Config, "YAML"), Style,
                            AllowUnknownOptions);
}

/// Gets configuration in a YAML string.
std::string configurationAsText(const FormatStyle &Style);

/// Returns the replacements necessary to sort all ``#include`` blocks
/// that are affected by ``Ranges``.
tooling::Replacements sortIncludes(const FormatStyle &Style, StringRef Code,
                                   ArrayRef<tooling::Range> Ranges,
                                   StringRef FileName,
                                   unsigned *Cursor = nullptr);

/// Returns the replacements corresponding to applying and formatting
/// \p Replaces on success; otheriwse, return an llvm::Error carrying
/// llvm::StringError.
Expected<tooling::Replacements>
formatReplacements(StringRef Code, const tooling::Replacements &Replaces,
                   const FormatStyle &Style);

/// Returns the replacements corresponding to applying \p Replaces and
/// cleaning up the code after that on success; otherwise, return an llvm::Error
/// carrying llvm::StringError.
/// This also supports inserting/deleting C++ #include directives:
/// - If a replacement has offset UINT_MAX, length 0, and a replacement text
///   that is an #include directive, this will insert the #include into the
///   correct block in the \p Code.
/// - If a replacement has offset UINT_MAX, length 1, and a replacement text
///   that is the name of the header to be removed, the header will be removed
///   from \p Code if it exists.
/// The include manipulation is done via ``tooling::HeaderInclude``, see its
/// documentation for more details on how include insertion points are found and
/// what edits are produced.
Expected<tooling::Replacements>
cleanupAroundReplacements(StringRef Code, const tooling::Replacements &Replaces,
                          const FormatStyle &Style);

/// Represents the status of a formatting attempt.
struct FormattingAttemptStatus {
  /// A value of ``false`` means that any of the affected ranges were not
  /// formatted due to a non-recoverable syntax error.
  bool FormatComplete = true;

  /// If ``FormatComplete`` is false, ``Line`` records a one-based
  /// original line number at which a syntax error might have occurred. This is
  /// based on a best-effort analysis and could be imprecise.
  unsigned Line = 0;
};

/// Reformats the given \p Ranges in \p Code.
///
/// Each range is extended on either end to its next bigger logic unit, i.e.
/// everything that might influence its formatting or might be influenced by its
/// formatting.
///
/// Returns the ``Replacements`` necessary to make all \p Ranges comply with
/// \p Style.
///
/// If ``Status`` is non-null, its value will be populated with the status of
/// this formatting attempt. See \c FormattingAttemptStatus.
tooling::Replacements reformat(const FormatStyle &Style, StringRef Code,
                               ArrayRef<tooling::Range> Ranges,
                               StringRef FileName = "<stdin>",
                               FormattingAttemptStatus *Status = nullptr);

/// Same as above, except if ``IncompleteFormat`` is non-null, its value
/// will be set to true if any of the affected ranges were not formatted due to
/// a non-recoverable syntax error.
tooling::Replacements reformat(const FormatStyle &Style, StringRef Code,
                               ArrayRef<tooling::Range> Ranges,
                               StringRef FileName, bool *IncompleteFormat);

/// Clean up any erroneous/redundant code in the given \p Ranges in \p
/// Code.
///
/// Returns the ``Replacements`` that clean up all \p Ranges in \p Code.
tooling::Replacements cleanup(const FormatStyle &Style, StringRef Code,
                              ArrayRef<tooling::Range> Ranges,
                              StringRef FileName = "<stdin>");

/// Fix namespace end comments in the given \p Ranges in \p Code.
///
/// Returns the ``Replacements`` that fix the namespace comments in all
/// \p Ranges in \p Code.
tooling::Replacements fixNamespaceEndComments(const FormatStyle &Style,
                                              StringRef Code,
                                              ArrayRef<tooling::Range> Ranges,
                                              StringRef FileName = "<stdin>");

/// Inserts or removes empty lines separating definition blocks including
/// classes, structs, functions, namespaces, and enums in the given \p Ranges in
/// \p Code.
///
/// Returns the ``Replacements`` that inserts or removes empty lines separating
/// definition blocks in all \p Ranges in \p Code.
tooling::Replacements separateDefinitionBlocks(const FormatStyle &Style,
                                               StringRef Code,
                                               ArrayRef<tooling::Range> Ranges,
                                               StringRef FileName = "<stdin>");

/// Sort consecutive using declarations in the given \p Ranges in
/// \p Code.
///
/// Returns the ``Replacements`` that sort the using declarations in all
/// \p Ranges in \p Code.
tooling::Replacements sortUsingDeclarations(const FormatStyle &Style,
                                            StringRef Code,
                                            ArrayRef<tooling::Range> Ranges,
                                            StringRef FileName = "<stdin>");

/// Returns the ``LangOpts`` that the formatter expects you to set.
///
/// \param Style determines specific settings for lexing mode.
LangOptions getFormattingLangOpts(const FormatStyle &Style = getLLVMStyle());

/// Description to be used for help text for a ``llvm::cl`` option for
/// specifying format style. The description is closely related to the operation
/// of ``getStyle()``.
extern const char *StyleOptionHelpDescription;

/// The suggested format style to use by default. This allows tools using
/// ``getStyle`` to have a consistent default style.
/// Different builds can modify the value to the preferred styles.
extern const char *DefaultFormatStyle;

/// The suggested predefined style to use as the fallback style in ``getStyle``.
/// Different builds can modify the value to the preferred styles.
extern const char *DefaultFallbackStyle;

/// Construct a FormatStyle based on ``StyleName``.
///
/// ``StyleName`` can take several forms:
/// * "{<key>: <value>, ...}" - Set specic style parameters.
/// * "<style name>" - One of the style names supported by
/// getPredefinedStyle().
/// * "file" - Load style configuration from a file called ``.clang-format``
/// located in one of the parent directories of ``FileName`` or the current
/// directory if ``FileName`` is empty.
/// * "file:<format_file_path>" to explicitly specify the configuration file to
/// use.
///
/// \param[in] StyleName Style name to interpret according to the description
/// above.
/// \param[in] FileName Path to start search for .clang-format if ``StyleName``
/// == "file".
/// \param[in] FallbackStyle The name of a predefined style used to fallback to
/// in case \p StyleName is "file" and no file can be found.
/// \param[in] Code The actual code to be formatted. Used to determine the
/// language if the filename isn't sufficient.
/// \param[in] FS The underlying file system, in which the file resides. By
/// default, the file system is the real file system.
/// \param[in] AllowUnknownOptions If true, unknown format options only
///             emit a warning. If false, errors are emitted on unknown format
///             options.
///
/// \returns FormatStyle as specified by ``StyleName``. If ``StyleName`` is
/// "file" and no file is found, returns ``FallbackStyle``. If no style could be
/// determined, returns an Error.
Expected<FormatStyle>
getStyle(StringRef StyleName, StringRef FileName, StringRef FallbackStyle,
         StringRef Code = "", llvm::vfs::FileSystem *FS = nullptr,
         bool AllowUnknownOptions = false,
         llvm::SourceMgr::DiagHandlerTy DiagHandler = nullptr);

// Guesses the language from the ``FileName`` and ``Code`` to be formatted.
// Defaults to FormatStyle::LK_Cpp.
FormatStyle::LanguageKind guessLanguage(StringRef FileName, StringRef Code);

// Returns a string representation of ``Language``.
inline StringRef getLanguageName(FormatStyle::LanguageKind Language) {
  switch (Language) {
  case FormatStyle::LK_Cpp:
    return "C++";
  case FormatStyle::LK_CSharp:
    return "CSharp";
  case FormatStyle::LK_ObjC:
    return "Objective-C";
  case FormatStyle::LK_Java:
    return "Java";
  case FormatStyle::LK_JavaScript:
    return "JavaScript";
  case FormatStyle::LK_Json:
    return "Json";
  case FormatStyle::LK_Proto:
    return "Proto";
  case FormatStyle::LK_TableGen:
    return "TableGen";
  case FormatStyle::LK_TextProto:
    return "TextProto";
  case FormatStyle::LK_Verilog:
    return "Verilog";
  default:
    return "Unknown";
  }
}

bool isClangFormatOn(StringRef Comment);
bool isClangFormatOff(StringRef Comment);

} // end namespace format
} // end namespace clang

template <>
struct std::is_error_code_enum<clang::format::ParseError> : std::true_type {};

#endif // LLVM_CLANG_FORMAT_FORMAT_H

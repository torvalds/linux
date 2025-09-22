//===-- FileCheckImpl.h - Private FileCheck Interface ------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the private interfaces of FileCheck. Its purpose is to
// allow unit testing of FileCheck and to separate the interface from the
// implementation. It is only meant to be used by FileCheck.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_FILECHECK_FILECHECKIMPL_H
#define LLVM_LIB_FILECHECK_FILECHECKIMPL_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/FileCheck/FileCheck.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SourceMgr.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

//===----------------------------------------------------------------------===//
// Numeric substitution handling code.
//===----------------------------------------------------------------------===//

/// Type representing the format an expression value should be textualized into
/// for matching. Used to represent both explicit format specifiers as well as
/// implicit format from using numeric variables.
struct ExpressionFormat {
  enum class Kind {
    /// Denote absence of format. Used for implicit format of literals and
    /// empty expressions.
    NoFormat,
    /// Value is an unsigned integer and should be printed as a decimal number.
    Unsigned,
    /// Value is a signed integer and should be printed as a decimal number.
    Signed,
    /// Value should be printed as an uppercase hex number.
    HexUpper,
    /// Value should be printed as a lowercase hex number.
    HexLower
  };

private:
  Kind Value;
  unsigned Precision = 0;
  /// printf-like "alternate form" selected.
  bool AlternateForm = false;

public:
  /// Evaluates a format to true if it can be used in a match.
  explicit operator bool() const { return Value != Kind::NoFormat; }

  /// Define format equality: formats are equal if neither is NoFormat and
  /// their kinds and precision are the same.
  bool operator==(const ExpressionFormat &Other) const {
    return Value != Kind::NoFormat && Value == Other.Value &&
           Precision == Other.Precision && AlternateForm == Other.AlternateForm;
  }

  bool operator!=(const ExpressionFormat &Other) const {
    return !(*this == Other);
  }

  bool operator==(Kind OtherValue) const { return Value == OtherValue; }

  bool operator!=(Kind OtherValue) const { return !(*this == OtherValue); }

  /// \returns the format specifier corresponding to this format as a string.
  StringRef toString() const;

  ExpressionFormat() : Value(Kind::NoFormat){};
  explicit ExpressionFormat(Kind Value) : Value(Value), Precision(0){};
  explicit ExpressionFormat(Kind Value, unsigned Precision)
      : Value(Value), Precision(Precision){};
  explicit ExpressionFormat(Kind Value, unsigned Precision, bool AlternateForm)
      : Value(Value), Precision(Precision), AlternateForm(AlternateForm){};

  /// \returns a wildcard regular expression string that matches any value in
  /// the format represented by this instance and no other value, or an error
  /// if the format is NoFormat.
  Expected<std::string> getWildcardRegex() const;

  /// \returns the string representation of \p Value in the format represented
  /// by this instance, or an error if conversion to this format failed or the
  /// format is NoFormat.
  Expected<std::string> getMatchingString(APInt Value) const;

  /// \returns the value corresponding to string representation \p StrVal
  /// according to the matching format represented by this instance.
  APInt valueFromStringRepr(StringRef StrVal, const SourceMgr &SM) const;
};

/// Class to represent an overflow error that might result when manipulating a
/// value.
class OverflowError : public ErrorInfo<OverflowError> {
public:
  static char ID;

  std::error_code convertToErrorCode() const override {
    return std::make_error_code(std::errc::value_too_large);
  }

  void log(raw_ostream &OS) const override { OS << "overflow error"; }
};

/// Performs operation and \returns its result or an error in case of failure,
/// such as if an overflow occurs.
Expected<APInt> exprAdd(const APInt &Lhs, const APInt &Rhs, bool &Overflow);
Expected<APInt> exprSub(const APInt &Lhs, const APInt &Rhs, bool &Overflow);
Expected<APInt> exprMul(const APInt &Lhs, const APInt &Rhs, bool &Overflow);
Expected<APInt> exprDiv(const APInt &Lhs, const APInt &Rhs, bool &Overflow);
Expected<APInt> exprMax(const APInt &Lhs, const APInt &Rhs, bool &Overflow);
Expected<APInt> exprMin(const APInt &Lhs, const APInt &Rhs, bool &Overflow);

/// Base class representing the AST of a given expression.
class ExpressionAST {
private:
  StringRef ExpressionStr;

public:
  ExpressionAST(StringRef ExpressionStr) : ExpressionStr(ExpressionStr) {}

  virtual ~ExpressionAST() = default;

  StringRef getExpressionStr() const { return ExpressionStr; }

  /// Evaluates and \returns the value of the expression represented by this
  /// AST or an error if evaluation fails.
  virtual Expected<APInt> eval() const = 0;

  /// \returns either the implicit format of this AST, a diagnostic against
  /// \p SM if implicit formats of the AST's components conflict, or NoFormat
  /// if the AST has no implicit format (e.g. AST is made up of a single
  /// literal).
  virtual Expected<ExpressionFormat>
  getImplicitFormat(const SourceMgr &SM) const {
    return ExpressionFormat();
  }
};

/// Class representing an unsigned literal in the AST of an expression.
class ExpressionLiteral : public ExpressionAST {
private:
  /// Actual value of the literal.
  APInt Value;

public:
  explicit ExpressionLiteral(StringRef ExpressionStr, APInt Val)
      : ExpressionAST(ExpressionStr), Value(Val) {}

  /// \returns the literal's value.
  Expected<APInt> eval() const override { return Value; }
};

/// Class to represent an undefined variable error, which quotes that
/// variable's name when printed.
class UndefVarError : public ErrorInfo<UndefVarError> {
private:
  StringRef VarName;

public:
  static char ID;

  UndefVarError(StringRef VarName) : VarName(VarName) {}

  StringRef getVarName() const { return VarName; }

  std::error_code convertToErrorCode() const override {
    return inconvertibleErrorCode();
  }

  /// Print name of variable associated with this error.
  void log(raw_ostream &OS) const override {
    OS << "undefined variable: " << VarName;
  }
};

/// Class representing an expression and its matching format.
class Expression {
private:
  /// Pointer to AST of the expression.
  std::unique_ptr<ExpressionAST> AST;

  /// Format to use (e.g. hex upper case letters) when matching the value.
  ExpressionFormat Format;

public:
  /// Generic constructor for an expression represented by the given \p AST and
  /// whose matching format is \p Format.
  Expression(std::unique_ptr<ExpressionAST> AST, ExpressionFormat Format)
      : AST(std::move(AST)), Format(Format) {}

  /// \returns pointer to AST of the expression. Pointer is guaranteed to be
  /// valid as long as this object is.
  ExpressionAST *getAST() const { return AST.get(); }

  ExpressionFormat getFormat() const { return Format; }
};

/// Class representing a numeric variable and its associated current value.
class NumericVariable {
private:
  /// Name of the numeric variable.
  StringRef Name;

  /// Format to use for expressions using this variable without an explicit
  /// format.
  ExpressionFormat ImplicitFormat;

  /// Value of numeric variable, if defined, or std::nullopt otherwise.
  std::optional<APInt> Value;

  /// The input buffer's string from which Value was parsed, or std::nullopt.
  /// See comments on getStringValue for a discussion of the std::nullopt case.
  std::optional<StringRef> StrValue;

  /// Line number where this variable is defined, or std::nullopt if defined
  /// before input is parsed. Used to determine whether a variable is defined on
  /// the same line as a given use.
  std::optional<size_t> DefLineNumber;

public:
  /// Constructor for a variable \p Name with implicit format \p ImplicitFormat
  /// defined at line \p DefLineNumber or defined before input is parsed if
  /// \p DefLineNumber is std::nullopt.
  explicit NumericVariable(StringRef Name, ExpressionFormat ImplicitFormat,
                           std::optional<size_t> DefLineNumber = std::nullopt)
      : Name(Name), ImplicitFormat(ImplicitFormat),
        DefLineNumber(DefLineNumber) {}

  /// \returns name of this numeric variable.
  StringRef getName() const { return Name; }

  /// \returns implicit format of this numeric variable.
  ExpressionFormat getImplicitFormat() const { return ImplicitFormat; }

  /// \returns this variable's value.
  std::optional<APInt> getValue() const { return Value; }

  /// \returns the input buffer's string from which this variable's value was
  /// parsed, or std::nullopt if the value is not yet defined or was not parsed
  /// from the input buffer.  For example, the value of @LINE is not parsed from
  /// the input buffer, and some numeric variables are parsed from the command
  /// line instead.
  std::optional<StringRef> getStringValue() const { return StrValue; }

  /// Sets value of this numeric variable to \p NewValue, and sets the input
  /// buffer string from which it was parsed to \p NewStrValue.  See comments on
  /// getStringValue for a discussion of when the latter can be std::nullopt.
  void setValue(APInt NewValue,
                std::optional<StringRef> NewStrValue = std::nullopt) {
    Value = NewValue;
    StrValue = NewStrValue;
  }

  /// Clears value of this numeric variable, regardless of whether it is
  /// currently defined or not.
  void clearValue() {
    Value = std::nullopt;
    StrValue = std::nullopt;
  }

  /// \returns the line number where this variable is defined, if any, or
  /// std::nullopt if defined before input is parsed.
  std::optional<size_t> getDefLineNumber() const { return DefLineNumber; }
};

/// Class representing the use of a numeric variable in the AST of an
/// expression.
class NumericVariableUse : public ExpressionAST {
private:
  /// Pointer to the class instance for the variable this use is about.
  NumericVariable *Variable;

public:
  NumericVariableUse(StringRef Name, NumericVariable *Variable)
      : ExpressionAST(Name), Variable(Variable) {}
  /// \returns the value of the variable referenced by this instance.
  Expected<APInt> eval() const override;

  /// \returns implicit format of this numeric variable.
  Expected<ExpressionFormat>
  getImplicitFormat(const SourceMgr &SM) const override {
    return Variable->getImplicitFormat();
  }
};

/// Type of functions evaluating a given binary operation.
using binop_eval_t = Expected<APInt> (*)(const APInt &, const APInt &, bool &);

/// Class representing a single binary operation in the AST of an expression.
class BinaryOperation : public ExpressionAST {
private:
  /// Left operand.
  std::unique_ptr<ExpressionAST> LeftOperand;

  /// Right operand.
  std::unique_ptr<ExpressionAST> RightOperand;

  /// Pointer to function that can evaluate this binary operation.
  binop_eval_t EvalBinop;

public:
  BinaryOperation(StringRef ExpressionStr, binop_eval_t EvalBinop,
                  std::unique_ptr<ExpressionAST> LeftOp,
                  std::unique_ptr<ExpressionAST> RightOp)
      : ExpressionAST(ExpressionStr), EvalBinop(EvalBinop) {
    LeftOperand = std::move(LeftOp);
    RightOperand = std::move(RightOp);
  }

  /// Evaluates the value of the binary operation represented by this AST,
  /// using EvalBinop on the result of recursively evaluating the operands.
  /// \returns the expression value or an error if an undefined numeric
  /// variable is used in one of the operands.
  Expected<APInt> eval() const override;

  /// \returns the implicit format of this AST, if any, a diagnostic against
  /// \p SM if the implicit formats of the AST's components conflict, or no
  /// format if the AST has no implicit format (e.g. AST is made of a single
  /// literal).
  Expected<ExpressionFormat>
  getImplicitFormat(const SourceMgr &SM) const override;
};

class FileCheckPatternContext;

/// Class representing a substitution to perform in the RegExStr string.
class Substitution {
protected:
  /// Pointer to a class instance holding, among other things, the table with
  /// the values of live string variables at the start of any given CHECK line.
  /// Used for substituting string variables with the text they were defined
  /// as. Expressions are linked to the numeric variables they use at
  /// parse time and directly access the value of the numeric variable to
  /// evaluate their value.
  FileCheckPatternContext *Context;

  /// The string that needs to be substituted for something else. For a
  /// string variable this is its name, otherwise this is the whole expression.
  StringRef FromStr;

  // Index in RegExStr of where to do the substitution.
  size_t InsertIdx;

public:
  Substitution(FileCheckPatternContext *Context, StringRef VarName,
               size_t InsertIdx)
      : Context(Context), FromStr(VarName), InsertIdx(InsertIdx) {}

  virtual ~Substitution() = default;

  /// \returns the string to be substituted for something else.
  StringRef getFromString() const { return FromStr; }

  /// \returns the index where the substitution is to be performed in RegExStr.
  size_t getIndex() const { return InsertIdx; }

  /// \returns a string containing the result of the substitution represented
  /// by this class instance or an error if substitution failed.
  virtual Expected<std::string> getResult() const = 0;
};

class StringSubstitution : public Substitution {
public:
  StringSubstitution(FileCheckPatternContext *Context, StringRef VarName,
                     size_t InsertIdx)
      : Substitution(Context, VarName, InsertIdx) {}

  /// \returns the text that the string variable in this substitution matched
  /// when defined, or an error if the variable is undefined.
  Expected<std::string> getResult() const override;
};

class NumericSubstitution : public Substitution {
private:
  /// Pointer to the class representing the expression whose value is to be
  /// substituted.
  std::unique_ptr<Expression> ExpressionPointer;

public:
  NumericSubstitution(FileCheckPatternContext *Context, StringRef ExpressionStr,
                      std::unique_ptr<Expression> ExpressionPointer,
                      size_t InsertIdx)
      : Substitution(Context, ExpressionStr, InsertIdx),
        ExpressionPointer(std::move(ExpressionPointer)) {}

  /// \returns a string containing the result of evaluating the expression in
  /// this substitution, or an error if evaluation failed.
  Expected<std::string> getResult() const override;
};

//===----------------------------------------------------------------------===//
// Pattern handling code.
//===----------------------------------------------------------------------===//

/// Class holding the Pattern global state, shared by all patterns: tables
/// holding values of variables and whether they are defined or not at any
/// given time in the matching process.
class FileCheckPatternContext {
  friend class Pattern;

private:
  /// When matching a given pattern, this holds the value of all the string
  /// variables defined in previous patterns. In a pattern, only the last
  /// definition for a given variable is recorded in this table.
  /// Back-references are used for uses after any the other definition.
  StringMap<StringRef> GlobalVariableTable;

  /// Map of all string variables defined so far. Used at parse time to detect
  /// a name conflict between a numeric variable and a string variable when
  /// the former is defined on a later line than the latter.
  StringMap<bool> DefinedVariableTable;

  /// When matching a given pattern, this holds the pointers to the classes
  /// representing the numeric variables defined in previous patterns. When
  /// matching a pattern all definitions for that pattern are recorded in the
  /// NumericVariableDefs table in the Pattern instance of that pattern.
  StringMap<NumericVariable *> GlobalNumericVariableTable;

  /// Pointer to the class instance representing the @LINE pseudo variable for
  /// easily updating its value.
  NumericVariable *LineVariable = nullptr;

  /// Vector holding pointers to all parsed numeric variables. Used to
  /// automatically free them once they are guaranteed to no longer be used.
  std::vector<std::unique_ptr<NumericVariable>> NumericVariables;

  /// Vector holding pointers to all parsed expressions. Used to automatically
  /// free the expressions once they are guaranteed to no longer be used.
  std::vector<std::unique_ptr<Expression>> Expressions;

  /// Vector holding pointers to all substitutions. Used to automatically free
  /// them once they are guaranteed to no longer be used.
  std::vector<std::unique_ptr<Substitution>> Substitutions;

public:
  /// \returns the value of string variable \p VarName or an error if no such
  /// variable has been defined.
  Expected<StringRef> getPatternVarValue(StringRef VarName);

  /// Defines string and numeric variables from definitions given on the
  /// command line, passed as a vector of [#]VAR=VAL strings in
  /// \p CmdlineDefines. \returns an error list containing diagnostics against
  /// \p SM for all definition parsing failures, if any, or Success otherwise.
  Error defineCmdlineVariables(ArrayRef<StringRef> CmdlineDefines,
                               SourceMgr &SM);

  /// Create @LINE pseudo variable. Value is set when pattern are being
  /// matched.
  void createLineVariable();

  /// Undefines local variables (variables whose name does not start with a '$'
  /// sign), i.e. removes them from GlobalVariableTable and from
  /// GlobalNumericVariableTable and also clears the value of numeric
  /// variables.
  void clearLocalVars();

private:
  /// Makes a new numeric variable and registers it for destruction when the
  /// context is destroyed.
  template <class... Types> NumericVariable *makeNumericVariable(Types... args);

  /// Makes a new string substitution and registers it for destruction when the
  /// context is destroyed.
  Substitution *makeStringSubstitution(StringRef VarName, size_t InsertIdx);

  /// Makes a new numeric substitution and registers it for destruction when
  /// the context is destroyed.
  Substitution *makeNumericSubstitution(StringRef ExpressionStr,
                                        std::unique_ptr<Expression> Expression,
                                        size_t InsertIdx);
};

/// Class to represent an error holding a diagnostic with location information
/// used when printing it.
class ErrorDiagnostic : public ErrorInfo<ErrorDiagnostic> {
private:
  SMDiagnostic Diagnostic;
  SMRange Range;

public:
  static char ID;

  ErrorDiagnostic(SMDiagnostic &&Diag, SMRange Range)
      : Diagnostic(Diag), Range(Range) {}

  std::error_code convertToErrorCode() const override {
    return inconvertibleErrorCode();
  }

  /// Print diagnostic associated with this error when printing the error.
  void log(raw_ostream &OS) const override { Diagnostic.print(nullptr, OS); }

  StringRef getMessage() const { return Diagnostic.getMessage(); }
  SMRange getRange() const { return Range; }

  static Error get(const SourceMgr &SM, SMLoc Loc, const Twine &ErrMsg,
                   SMRange Range = std::nullopt) {
    return make_error<ErrorDiagnostic>(
        SM.GetMessage(Loc, SourceMgr::DK_Error, ErrMsg), Range);
  }

  static Error get(const SourceMgr &SM, StringRef Buffer, const Twine &ErrMsg) {
    SMLoc Start = SMLoc::getFromPointer(Buffer.data());
    SMLoc End = SMLoc::getFromPointer(Buffer.data() + Buffer.size());
    return get(SM, Start, ErrMsg, SMRange(Start, End));
  }
};

class NotFoundError : public ErrorInfo<NotFoundError> {
public:
  static char ID;

  std::error_code convertToErrorCode() const override {
    return inconvertibleErrorCode();
  }

  /// Print diagnostic associated with this error when printing the error.
  void log(raw_ostream &OS) const override {
    OS << "String not found in input";
  }
};

/// An error that has already been reported.
///
/// This class is designed to support a function whose callers may need to know
/// whether the function encountered and reported an error but never need to
/// know the nature of that error.  For example, the function has a return type
/// of \c Error and always returns either \c ErrorReported or \c ErrorSuccess.
/// That interface is similar to that of a function returning bool to indicate
/// an error except, in the former case, (1) there is no confusion over polarity
/// and (2) the caller must either check the result or explicitly ignore it with
/// a call like \c consumeError.
class ErrorReported final : public ErrorInfo<ErrorReported> {
public:
  static char ID;

  std::error_code convertToErrorCode() const override {
    return inconvertibleErrorCode();
  }

  /// Print diagnostic associated with this error when printing the error.
  void log(raw_ostream &OS) const override {
    OS << "error previously reported";
  }

  static inline Error reportedOrSuccess(bool HasErrorReported) {
    if (HasErrorReported)
      return make_error<ErrorReported>();
    return Error::success();
  }
};

class Pattern {
  SMLoc PatternLoc;

  /// A fixed string to match as the pattern or empty if this pattern requires
  /// a regex match.
  StringRef FixedStr;

  /// A regex string to match as the pattern or empty if this pattern requires
  /// a fixed string to match.
  std::string RegExStr;

  /// Entries in this vector represent a substitution of a string variable or
  /// an expression in the RegExStr regex at match time. For example, in the
  /// case of a CHECK directive with the pattern "foo[[bar]]baz[[#N+1]]",
  /// RegExStr will contain "foobaz" and we'll get two entries in this vector
  /// that tells us to insert the value of string variable "bar" at offset 3
  /// and the value of expression "N+1" at offset 6.
  std::vector<Substitution *> Substitutions;

  /// Maps names of string variables defined in a pattern to the number of
  /// their parenthesis group in RegExStr capturing their last definition.
  ///
  /// E.g. for the pattern "foo[[bar:.*]]baz([[bar]][[QUUX]][[bar:.*]])",
  /// RegExStr will be "foo(.*)baz(\1<quux value>(.*))" where <quux value> is
  /// the value captured for QUUX on the earlier line where it was defined, and
  /// VariableDefs will map "bar" to the third parenthesis group which captures
  /// the second definition of "bar".
  ///
  /// Note: uses std::map rather than StringMap to be able to get the key when
  /// iterating over values.
  std::map<StringRef, unsigned> VariableDefs;

  /// Structure representing the definition of a numeric variable in a pattern.
  /// It holds the pointer to the class instance holding the value and matching
  /// format of the numeric variable whose value is being defined and the
  /// number of the parenthesis group in RegExStr to capture that value.
  struct NumericVariableMatch {
    /// Pointer to class instance holding the value and matching format of the
    /// numeric variable being defined.
    NumericVariable *DefinedNumericVariable;

    /// Number of the parenthesis group in RegExStr that captures the value of
    /// this numeric variable definition.
    unsigned CaptureParenGroup;
  };

  /// Holds the number of the parenthesis group in RegExStr and pointer to the
  /// corresponding NumericVariable class instance of all numeric variable
  /// definitions. Used to set the matched value of all those variables.
  StringMap<NumericVariableMatch> NumericVariableDefs;

  /// Pointer to a class instance holding the global state shared by all
  /// patterns:
  /// - separate tables with the values of live string and numeric variables
  ///   respectively at the start of any given CHECK line;
  /// - table holding whether a string variable has been defined at any given
  ///   point during the parsing phase.
  FileCheckPatternContext *Context;

  Check::FileCheckType CheckTy;

  /// Line number for this CHECK pattern or std::nullopt if it is an implicit
  /// pattern. Used to determine whether a variable definition is made on an
  /// earlier line to the one with this CHECK.
  std::optional<size_t> LineNumber;

  /// Ignore case while matching if set to true.
  bool IgnoreCase = false;

public:
  Pattern(Check::FileCheckType Ty, FileCheckPatternContext *Context,
          std::optional<size_t> Line = std::nullopt)
      : Context(Context), CheckTy(Ty), LineNumber(Line) {}

  /// \returns the location in source code.
  SMLoc getLoc() const { return PatternLoc; }

  /// \returns the pointer to the global state for all patterns in this
  /// FileCheck instance.
  FileCheckPatternContext *getContext() const { return Context; }

  /// \returns whether \p C is a valid first character for a variable name.
  static bool isValidVarNameStart(char C);

  /// Parsing information about a variable.
  struct VariableProperties {
    StringRef Name;
    bool IsPseudo;
  };

  /// Parses the string at the start of \p Str for a variable name. \returns
  /// a VariableProperties structure holding the variable name and whether it
  /// is the name of a pseudo variable, or an error holding a diagnostic
  /// against \p SM if parsing fail. If parsing was successful, also strips
  /// \p Str from the variable name.
  static Expected<VariableProperties> parseVariable(StringRef &Str,
                                                    const SourceMgr &SM);
  /// Parses \p Expr for a numeric substitution block at line \p LineNumber,
  /// or before input is parsed if \p LineNumber is None. Parameter
  /// \p IsLegacyLineExpr indicates whether \p Expr should be a legacy @LINE
  /// expression and \p Context points to the class instance holding the live
  /// string and numeric variables. \returns a pointer to the class instance
  /// representing the expression whose value must be substitued, or an error
  /// holding a diagnostic against \p SM if parsing fails. If substitution was
  /// successful, sets \p DefinedNumericVariable to point to the class
  /// representing the numeric variable defined in this numeric substitution
  /// block, or std::nullopt if this block does not define any variable.
  static Expected<std::unique_ptr<Expression>> parseNumericSubstitutionBlock(
      StringRef Expr, std::optional<NumericVariable *> &DefinedNumericVariable,
      bool IsLegacyLineExpr, std::optional<size_t> LineNumber,
      FileCheckPatternContext *Context, const SourceMgr &SM);
  /// Parses the pattern in \p PatternStr and initializes this Pattern instance
  /// accordingly.
  ///
  /// \p Prefix provides which prefix is being matched, \p Req describes the
  /// global options that influence the parsing such as whitespace
  /// canonicalization, \p SM provides the SourceMgr used for error reports.
  /// \returns true in case of an error, false otherwise.
  bool parsePattern(StringRef PatternStr, StringRef Prefix, SourceMgr &SM,
                    const FileCheckRequest &Req);
  struct Match {
    size_t Pos;
    size_t Len;
  };
  struct MatchResult {
    std::optional<Match> TheMatch;
    Error TheError;
    MatchResult(size_t MatchPos, size_t MatchLen, Error E)
        : TheMatch(Match{MatchPos, MatchLen}), TheError(std::move(E)) {}
    MatchResult(Match M, Error E) : TheMatch(M), TheError(std::move(E)) {}
    MatchResult(Error E) : TheError(std::move(E)) {}
  };
  /// Matches the pattern string against the input buffer \p Buffer.
  ///
  /// \returns either (1) an error resulting in no match or (2) a match possibly
  /// with an error encountered while processing the match.
  ///
  /// The GlobalVariableTable StringMap in the FileCheckPatternContext class
  /// instance provides the current values of FileCheck string variables and is
  /// updated if this match defines new values. Likewise, the
  /// GlobalNumericVariableTable StringMap in the same class provides the
  /// current values of FileCheck numeric variables and is updated if this
  /// match defines new numeric values.
  MatchResult match(StringRef Buffer, const SourceMgr &SM) const;
  /// Prints the value of successful substitutions.
  void printSubstitutions(const SourceMgr &SM, StringRef Buffer,
                          SMRange MatchRange, FileCheckDiag::MatchType MatchTy,
                          std::vector<FileCheckDiag> *Diags) const;
  void printFuzzyMatch(const SourceMgr &SM, StringRef Buffer,
                       std::vector<FileCheckDiag> *Diags) const;

  bool hasVariable() const {
    return !(Substitutions.empty() && VariableDefs.empty());
  }
  void printVariableDefs(const SourceMgr &SM, FileCheckDiag::MatchType MatchTy,
                         std::vector<FileCheckDiag> *Diags) const;

  Check::FileCheckType getCheckTy() const { return CheckTy; }

  int getCount() const { return CheckTy.getCount(); }

private:
  bool AddRegExToRegEx(StringRef RS, unsigned &CurParen, SourceMgr &SM);
  void AddBackrefToRegEx(unsigned BackrefNum);
  /// Computes an arbitrary estimate for the quality of matching this pattern
  /// at the start of \p Buffer; a distance of zero should correspond to a
  /// perfect match.
  unsigned computeMatchDistance(StringRef Buffer) const;
  /// Finds the closing sequence of a regex variable usage or definition.
  ///
  /// \p Str has to point in the beginning of the definition (right after the
  /// opening sequence). \p SM holds the SourceMgr used for error reporting.
  ///  \returns the offset of the closing sequence within Str, or npos if it
  /// was not found.
  static size_t FindRegexVarEnd(StringRef Str, SourceMgr &SM);

  /// Parses \p Expr for the name of a numeric variable to be defined at line
  /// \p LineNumber, or before input is parsed if \p LineNumber is None.
  /// \returns a pointer to the class instance representing that variable,
  /// creating it if needed, or an error holding a diagnostic against \p SM
  /// should defining such a variable be invalid.
  static Expected<NumericVariable *> parseNumericVariableDefinition(
      StringRef &Expr, FileCheckPatternContext *Context,
      std::optional<size_t> LineNumber, ExpressionFormat ImplicitFormat,
      const SourceMgr &SM);
  /// Parses \p Name as a (pseudo if \p IsPseudo is true) numeric variable use
  /// at line \p LineNumber, or before input is parsed if \p LineNumber is
  /// None. Parameter \p Context points to the class instance holding the live
  /// string and numeric variables. \returns the pointer to the class instance
  /// representing that variable if successful, or an error holding a
  /// diagnostic against \p SM otherwise.
  static Expected<std::unique_ptr<NumericVariableUse>> parseNumericVariableUse(
      StringRef Name, bool IsPseudo, std::optional<size_t> LineNumber,
      FileCheckPatternContext *Context, const SourceMgr &SM);
  enum class AllowedOperand { LineVar, LegacyLiteral, Any };
  /// Parses \p Expr for use of a numeric operand at line \p LineNumber, or
  /// before input is parsed if \p LineNumber is None. Accepts literal values,
  /// numeric variables and function calls, depending on the value of \p AO.
  /// \p MaybeInvalidConstraint indicates whether the text being parsed could
  /// be an invalid constraint. \p Context points to the class instance holding
  /// the live string and numeric variables. \returns the class representing
  /// that operand in the AST of the expression or an error holding a
  /// diagnostic against \p SM otherwise. If \p Expr starts with a "(" this
  /// function will attempt to parse a parenthesized expression.
  static Expected<std::unique_ptr<ExpressionAST>>
  parseNumericOperand(StringRef &Expr, AllowedOperand AO, bool ConstraintParsed,
                      std::optional<size_t> LineNumber,
                      FileCheckPatternContext *Context, const SourceMgr &SM);
  /// Parses and updates \p RemainingExpr for a binary operation at line
  /// \p LineNumber, or before input is parsed if \p LineNumber is None. The
  /// left operand of this binary operation is given in \p LeftOp and \p Expr
  /// holds the string for the full expression, including the left operand.
  /// Parameter \p IsLegacyLineExpr indicates whether we are parsing a legacy
  /// @LINE expression. Parameter \p Context points to the class instance
  /// holding the live string and numeric variables. \returns the class
  /// representing the binary operation in the AST of the expression, or an
  /// error holding a diagnostic against \p SM otherwise.
  static Expected<std::unique_ptr<ExpressionAST>>
  parseBinop(StringRef Expr, StringRef &RemainingExpr,
             std::unique_ptr<ExpressionAST> LeftOp, bool IsLegacyLineExpr,
             std::optional<size_t> LineNumber, FileCheckPatternContext *Context,
             const SourceMgr &SM);

  /// Parses a parenthesized expression inside \p Expr at line \p LineNumber, or
  /// before input is parsed if \p LineNumber is None. \p Expr must start with
  /// a '('. Accepts both literal values and numeric variables. Parameter \p
  /// Context points to the class instance holding the live string and numeric
  /// variables. \returns the class representing that operand in the AST of the
  /// expression or an error holding a diagnostic against \p SM otherwise.
  static Expected<std::unique_ptr<ExpressionAST>>
  parseParenExpr(StringRef &Expr, std::optional<size_t> LineNumber,
                 FileCheckPatternContext *Context, const SourceMgr &SM);

  /// Parses \p Expr for an argument list belonging to a call to function \p
  /// FuncName at line \p LineNumber, or before input is parsed if \p LineNumber
  /// is None. Parameter \p FuncLoc is the source location used for diagnostics.
  /// Parameter \p Context points to the class instance holding the live string
  /// and numeric variables. \returns the class representing that call in the
  /// AST of the expression or an error holding a diagnostic against \p SM
  /// otherwise.
  static Expected<std::unique_ptr<ExpressionAST>>
  parseCallExpr(StringRef &Expr, StringRef FuncName,
                std::optional<size_t> LineNumber,
                FileCheckPatternContext *Context, const SourceMgr &SM);
};

//===----------------------------------------------------------------------===//
// Check Strings.
//===----------------------------------------------------------------------===//

/// A check that we found in the input file.
struct FileCheckString {
  /// The pattern to match.
  Pattern Pat;

  /// Which prefix name this check matched.
  StringRef Prefix;

  /// The location in the match file that the check string was specified.
  SMLoc Loc;

  /// Hold the information about the DAG/NOT strings in the program, which are
  /// not explicitly stored otherwise. This allows for better and more accurate
  /// diagnostic messages.
  struct DagNotPrefixInfo {
    Pattern DagNotPat;
    StringRef DagNotPrefix;

    DagNotPrefixInfo(const Pattern &P, StringRef S)
        : DagNotPat(P), DagNotPrefix(S) {}
  };

  /// Hold the DAG/NOT strings occurring in the input file.
  std::vector<DagNotPrefixInfo> DagNotStrings;

  FileCheckString(const Pattern &P, StringRef S, SMLoc L)
      : Pat(P), Prefix(S), Loc(L) {}

  /// Matches check string and its "not strings" and/or "dag strings".
  size_t Check(const SourceMgr &SM, StringRef Buffer, bool IsLabelScanMode,
               size_t &MatchLen, FileCheckRequest &Req,
               std::vector<FileCheckDiag> *Diags) const;

  /// Verifies that there is a single line in the given \p Buffer. Errors are
  /// reported against \p SM.
  bool CheckNext(const SourceMgr &SM, StringRef Buffer) const;
  /// Verifies that there is no newline in the given \p Buffer. Errors are
  /// reported against \p SM.
  bool CheckSame(const SourceMgr &SM, StringRef Buffer) const;
  /// Verifies that none of the strings in \p NotStrings are found in the given
  /// \p Buffer. Errors are reported against \p SM and diagnostics recorded in
  /// \p Diags according to the verbosity level set in \p Req.
  bool CheckNot(const SourceMgr &SM, StringRef Buffer,
                const std::vector<const DagNotPrefixInfo *> &NotStrings,
                const FileCheckRequest &Req,
                std::vector<FileCheckDiag> *Diags) const;
  /// Matches "dag strings" and their mixed "not strings".
  size_t CheckDag(const SourceMgr &SM, StringRef Buffer,
                  std::vector<const DagNotPrefixInfo *> &NotStrings,
                  const FileCheckRequest &Req,
                  std::vector<FileCheckDiag> *Diags) const;
};

} // namespace llvm

#endif

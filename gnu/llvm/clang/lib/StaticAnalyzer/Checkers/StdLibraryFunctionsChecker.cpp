//=== StdLibraryFunctionsChecker.cpp - Model standard functions -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This checker improves modeling of a few simple library functions.
//
// This checker provides a specification format - `Summary' - and
// contains descriptions of some library functions in this format. Each
// specification contains a list of branches for splitting the program state
// upon call, and range constraints on argument and return-value symbols that
// are satisfied on each branch. This spec can be expanded to include more
// items, like external effects of the function.
//
// The main difference between this approach and the body farms technique is
// in more explicit control over how many branches are produced. For example,
// consider standard C function `ispunct(int x)', which returns a non-zero value
// iff `x' is a punctuation character, that is, when `x' is in range
//   ['!', '/']   [':', '@']  U  ['[', '\`']  U  ['{', '~'].
// `Summary' provides only two branches for this function. However,
// any attempt to describe this range with if-statements in the body farm
// would result in many more branches. Because each branch needs to be analyzed
// independently, this significantly reduces performance. Additionally,
// once we consider a branch on which `x' is in range, say, ['!', '/'],
// we assume that such branch is an important separate path through the program,
// which may lead to false positives because considering this particular path
// was not consciously intended, and therefore it might have been unreachable.
//
// This checker uses eval::Call for modeling pure functions (functions without
// side effects), for which their `Summary' is a precise model. This avoids
// unnecessary invalidation passes. Conflicts with other checkers are unlikely
// because if the function has no other effects, other checkers would probably
// never want to improve upon the modeling done by this checker.
//
// Non-pure functions, for which only partial improvement over the default
// behavior is expected, are modeled via check::PostCall, non-intrusively.
//
//===----------------------------------------------------------------------===//

#include "ErrnoModeling.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerHelpers.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FormatVariadic.h"

#include <optional>
#include <string>

using namespace clang;
using namespace clang::ento;

namespace {
class StdLibraryFunctionsChecker
    : public Checker<check::PreCall, check::PostCall, eval::Call> {

  class Summary;

  /// Specify how much the analyzer engine should entrust modeling this function
  /// to us.
  enum InvalidationKind {
    /// No \c eval::Call for the function, it can be modeled elsewhere.
    /// This checker checks only pre and post conditions.
    NoEvalCall,
    /// The function is modeled completely in this checker.
    EvalCallAsPure
  };

  /// Given a range, should the argument stay inside or outside this range?
  enum RangeKind { OutOfRange, WithinRange };

  static RangeKind negateKind(RangeKind K) {
    switch (K) {
    case OutOfRange:
      return WithinRange;
    case WithinRange:
      return OutOfRange;
    }
    llvm_unreachable("Unknown range kind");
  }

  /// The universal integral type to use in value range descriptions.
  /// Unsigned to make sure overflows are well-defined.
  typedef uint64_t RangeInt;

  /// Describes a single range constraint. Eg. {{0, 1}, {3, 4}} is
  /// a non-negative integer, which less than 5 and not equal to 2.
  typedef std::vector<std::pair<RangeInt, RangeInt>> IntRangeVector;

  /// A reference to an argument or return value by its number.
  /// ArgNo in CallExpr and CallEvent is defined as Unsigned, but
  /// obviously uint32_t should be enough for all practical purposes.
  typedef uint32_t ArgNo;
  /// Special argument number for specifying the return value.
  static const ArgNo Ret;

  /// Get a string representation of an argument index.
  /// E.g.: (1) -> '1st arg', (2) - > '2nd arg'
  static void printArgDesc(ArgNo, llvm::raw_ostream &Out);
  /// Print value X of the argument in form " (which is X)",
  /// if the value is a fixed known value, otherwise print nothing.
  /// This is used as simple explanation of values if possible.
  static void printArgValueInfo(ArgNo ArgN, ProgramStateRef State,
                                const CallEvent &Call, llvm::raw_ostream &Out);
  /// Append textual description of a numeric range [RMin,RMax] to
  /// \p Out.
  static void appendInsideRangeDesc(llvm::APSInt RMin, llvm::APSInt RMax,
                                    QualType ArgT, BasicValueFactory &BVF,
                                    llvm::raw_ostream &Out);
  /// Append textual description of a numeric range out of [RMin,RMax] to
  /// \p Out.
  static void appendOutOfRangeDesc(llvm::APSInt RMin, llvm::APSInt RMax,
                                   QualType ArgT, BasicValueFactory &BVF,
                                   llvm::raw_ostream &Out);

  class ValueConstraint;

  /// Pointer to the ValueConstraint. We need a copyable, polymorphic and
  /// default initializable type (vector needs that). A raw pointer was good,
  /// however, we cannot default initialize that. unique_ptr makes the Summary
  /// class non-copyable, therefore not an option. Releasing the copyability
  /// requirement would render the initialization of the Summary map infeasible.
  /// Mind that a pointer to a new value constraint is created when the negate
  /// function is used.
  using ValueConstraintPtr = std::shared_ptr<ValueConstraint>;

  /// Polymorphic base class that represents a constraint on a given argument
  /// (or return value) of a function. Derived classes implement different kind
  /// of constraints, e.g range constraints or correlation between two
  /// arguments.
  /// These are used as argument constraints (preconditions) of functions, in
  /// which case a bug report may be emitted if the constraint is not satisfied.
  /// Another use is as conditions for summary cases, to create different
  /// classes of behavior for a function. In this case no description of the
  /// constraint is needed because the summary cases have an own (not generated)
  /// description string.
  class ValueConstraint {
  public:
    ValueConstraint(ArgNo ArgN) : ArgN(ArgN) {}
    virtual ~ValueConstraint() {}

    /// Apply the effects of the constraint on the given program state. If null
    /// is returned then the constraint is not feasible.
    virtual ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                                  const Summary &Summary,
                                  CheckerContext &C) const = 0;

    /// Represents that in which context do we require a description of the
    /// constraint.
    enum DescriptionKind {
      /// Describe a constraint that was violated.
      /// Description should start with something like "should be".
      Violation,
      /// Describe a constraint that was assumed to be true.
      /// This can be used when a precondition is satisfied, or when a summary
      /// case is applied.
      /// Description should start with something like "is".
      Assumption
    };

    /// Give a description that explains the constraint to the user. Used when
    /// a bug is reported or when the constraint is applied and displayed as a
    /// note. The description should not mention the argument (getArgNo).
    /// See StdLibraryFunctionsChecker::reportBug about how this function is
    /// used (this function is used not only there).
    virtual void describe(DescriptionKind DK, const CallEvent &Call,
                          ProgramStateRef State, const Summary &Summary,
                          llvm::raw_ostream &Out) const {
      // There are some descendant classes that are not used as argument
      // constraints, e.g. ComparisonConstraint. In that case we can safely
      // ignore the implementation of this function.
      llvm_unreachable(
          "Description not implemented for summary case constraints");
    }

    /// Give a description that explains the actual argument value (where the
    /// current ValueConstraint applies to) to the user. This function should be
    /// called only when the current constraint is satisfied by the argument.
    /// It should produce a more precise description than the constraint itself.
    /// The actual value of the argument and the program state can be used to
    /// make the description more precise. In the most simple case, if the
    /// argument has a fixed known value this value can be printed into \p Out,
    /// this is done by default.
    /// The function should return true if a description was printed to \p Out,
    /// otherwise false.
    /// See StdLibraryFunctionsChecker::reportBug about how this function is
    /// used.
    virtual bool describeArgumentValue(const CallEvent &Call,
                                       ProgramStateRef State,
                                       const Summary &Summary,
                                       llvm::raw_ostream &Out) const {
      if (auto N = getArgSVal(Call, getArgNo()).getAs<NonLoc>()) {
        if (const llvm::APSInt *Int = N->getAsInteger()) {
          Out << *Int;
          return true;
        }
      }
      return false;
    }

    /// Return those arguments that should be tracked when we report a bug about
    /// argument constraint violation. By default it is the argument that is
    /// constrained, however, in some special cases we need to track other
    /// arguments as well. E.g. a buffer size might be encoded in another
    /// argument.
    /// The "return value" argument number can not occur as returned value.
    virtual std::vector<ArgNo> getArgsToTrack() const { return {ArgN}; }

    /// Get a constraint that represents exactly the opposite of the current.
    virtual ValueConstraintPtr negate() const {
      llvm_unreachable("Not implemented");
    };

    /// Check whether the constraint is malformed or not. It is malformed if the
    /// specified argument has a mismatch with the given FunctionDecl (e.g. the
    /// arg number is out-of-range of the function's argument list).
    /// This condition can indicate if a probably wrong or unexpected function
    /// was found where the constraint is to be applied.
    bool checkValidity(const FunctionDecl *FD) const {
      const bool ValidArg = ArgN == Ret || ArgN < FD->getNumParams();
      assert(ValidArg && "Arg out of range!");
      if (!ValidArg)
        return false;
      // Subclasses may further refine the validation.
      return checkSpecificValidity(FD);
    }

    /// Return the argument number (may be placeholder for "return value").
    ArgNo getArgNo() const { return ArgN; }

  protected:
    /// Argument to which to apply the constraint. It can be a real argument of
    /// the function to check, or a special value to indicate the return value
    /// of the function.
    /// Every constraint is assigned to one main argument, even if other
    /// arguments are involved.
    ArgNo ArgN;

    /// Do constraint-specific validation check.
    virtual bool checkSpecificValidity(const FunctionDecl *FD) const {
      return true;
    }
  };

  /// Check if a single argument falls into a specific "range".
  /// A range is formed as a set of intervals.
  /// E.g. \code {['A', 'Z'], ['a', 'z'], ['_', '_']} \endcode
  /// The intervals are closed intervals that contain one or more values.
  ///
  /// The default constructed RangeConstraint has an empty range, applying
  /// such constraint does not involve any assumptions, thus the State remains
  /// unchanged. This is meaningful, if the range is dependent on a looked up
  /// type (e.g. [0, Socklen_tMax]). If the type is not found, then the range
  /// is default initialized to be empty.
  class RangeConstraint : public ValueConstraint {
    /// The constraint can be specified by allowing or disallowing the range.
    /// WithinRange indicates allowing the range, OutOfRange indicates
    /// disallowing it (allowing the complementary range).
    RangeKind Kind;

    /// A set of intervals.
    IntRangeVector Ranges;

    /// A textual description of this constraint for the specific case where the
    /// constraint is used. If empty a generated description will be used that
    /// is built from the range of the constraint.
    StringRef Description;

  public:
    RangeConstraint(ArgNo ArgN, RangeKind Kind, const IntRangeVector &Ranges,
                    StringRef Desc = "")
        : ValueConstraint(ArgN), Kind(Kind), Ranges(Ranges), Description(Desc) {
    }

    const IntRangeVector &getRanges() const { return Ranges; }

    ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                          const Summary &Summary,
                          CheckerContext &C) const override;

    void describe(DescriptionKind DK, const CallEvent &Call,
                  ProgramStateRef State, const Summary &Summary,
                  llvm::raw_ostream &Out) const override;

    bool describeArgumentValue(const CallEvent &Call, ProgramStateRef State,
                               const Summary &Summary,
                               llvm::raw_ostream &Out) const override;

    ValueConstraintPtr negate() const override {
      RangeConstraint Tmp(*this);
      Tmp.Kind = negateKind(Kind);
      return std::make_shared<RangeConstraint>(Tmp);
    }

  protected:
    bool checkSpecificValidity(const FunctionDecl *FD) const override {
      const bool ValidArg =
          getArgType(FD, ArgN)->isIntegralType(FD->getASTContext());
      assert(ValidArg &&
             "This constraint should be applied on an integral type");
      return ValidArg;
    }

  private:
    /// A callback function that is used when iterating over the range
    /// intervals. It gets the begin and end (inclusive) of one interval.
    /// This is used to make any kind of task possible that needs an iteration
    /// over the intervals.
    using RangeApplyFunction =
        std::function<bool(const llvm::APSInt &Min, const llvm::APSInt &Max)>;

    /// Call a function on the intervals of the range.
    /// The function is called with all intervals in the range.
    void applyOnWithinRange(BasicValueFactory &BVF, QualType ArgT,
                            const RangeApplyFunction &F) const;
    /// Call a function on all intervals in the complementary range.
    /// The function is called with all intervals that fall out of the range.
    /// E.g. consider an interval list [A, B] and [C, D]
    /// \code
    /// -------+--------+------------------+------------+----------->
    ///        A        B                  C            D
    /// \endcode
    /// We get the ranges [-inf, A - 1], [D + 1, +inf], [B + 1, C - 1].
    /// The \p ArgT is used to determine the min and max of the type that is
    /// used as "-inf" and "+inf".
    void applyOnOutOfRange(BasicValueFactory &BVF, QualType ArgT,
                           const RangeApplyFunction &F) const;
    /// Call a function on the intervals of the range or the complementary
    /// range.
    void applyOnRange(RangeKind Kind, BasicValueFactory &BVF, QualType ArgT,
                      const RangeApplyFunction &F) const {
      switch (Kind) {
      case OutOfRange:
        applyOnOutOfRange(BVF, ArgT, F);
        break;
      case WithinRange:
        applyOnWithinRange(BVF, ArgT, F);
        break;
      };
    }
  };

  /// Check relation of an argument to another.
  class ComparisonConstraint : public ValueConstraint {
    BinaryOperator::Opcode Opcode;
    ArgNo OtherArgN;

  public:
    ComparisonConstraint(ArgNo ArgN, BinaryOperator::Opcode Opcode,
                         ArgNo OtherArgN)
        : ValueConstraint(ArgN), Opcode(Opcode), OtherArgN(OtherArgN) {}
    ArgNo getOtherArgNo() const { return OtherArgN; }
    BinaryOperator::Opcode getOpcode() const { return Opcode; }
    ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                          const Summary &Summary,
                          CheckerContext &C) const override;
  };

  /// Check null or non-null-ness of an argument that is of pointer type.
  class NotNullConstraint : public ValueConstraint {
    using ValueConstraint::ValueConstraint;
    // This variable has a role when we negate the constraint.
    bool CannotBeNull = true;

  public:
    NotNullConstraint(ArgNo ArgN, bool CannotBeNull = true)
        : ValueConstraint(ArgN), CannotBeNull(CannotBeNull) {}

    ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                          const Summary &Summary,
                          CheckerContext &C) const override;

    void describe(DescriptionKind DK, const CallEvent &Call,
                  ProgramStateRef State, const Summary &Summary,
                  llvm::raw_ostream &Out) const override;

    bool describeArgumentValue(const CallEvent &Call, ProgramStateRef State,
                               const Summary &Summary,
                               llvm::raw_ostream &Out) const override;

    ValueConstraintPtr negate() const override {
      NotNullConstraint Tmp(*this);
      Tmp.CannotBeNull = !this->CannotBeNull;
      return std::make_shared<NotNullConstraint>(Tmp);
    }

  protected:
    bool checkSpecificValidity(const FunctionDecl *FD) const override {
      const bool ValidArg = getArgType(FD, ArgN)->isPointerType();
      assert(ValidArg &&
             "This constraint should be applied only on a pointer type");
      return ValidArg;
    }
  };

  /// Check null or non-null-ness of an argument that is of pointer type.
  /// The argument is meant to be a buffer that has a size constraint, and it
  /// is allowed to have a NULL value if the size is 0. The size can depend on
  /// 1 or 2 additional arguments, if one of these is 0 the buffer is allowed to
  /// be NULL. This is useful for functions like `fread` which have this special
  /// property.
  class NotNullBufferConstraint : public ValueConstraint {
    using ValueConstraint::ValueConstraint;
    ArgNo SizeArg1N;
    std::optional<ArgNo> SizeArg2N;
    // This variable has a role when we negate the constraint.
    bool CannotBeNull = true;

  public:
    NotNullBufferConstraint(ArgNo ArgN, ArgNo SizeArg1N,
                            std::optional<ArgNo> SizeArg2N,
                            bool CannotBeNull = true)
        : ValueConstraint(ArgN), SizeArg1N(SizeArg1N), SizeArg2N(SizeArg2N),
          CannotBeNull(CannotBeNull) {}

    ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                          const Summary &Summary,
                          CheckerContext &C) const override;

    void describe(DescriptionKind DK, const CallEvent &Call,
                  ProgramStateRef State, const Summary &Summary,
                  llvm::raw_ostream &Out) const override;

    bool describeArgumentValue(const CallEvent &Call, ProgramStateRef State,
                               const Summary &Summary,
                               llvm::raw_ostream &Out) const override;

    ValueConstraintPtr negate() const override {
      NotNullBufferConstraint Tmp(*this);
      Tmp.CannotBeNull = !this->CannotBeNull;
      return std::make_shared<NotNullBufferConstraint>(Tmp);
    }

  protected:
    bool checkSpecificValidity(const FunctionDecl *FD) const override {
      const bool ValidArg = getArgType(FD, ArgN)->isPointerType();
      assert(ValidArg &&
             "This constraint should be applied only on a pointer type");
      return ValidArg;
    }
  };

  // Represents a buffer argument with an additional size constraint. The
  // constraint may be a concrete value, or a symbolic value in an argument.
  // Example 1. Concrete value as the minimum buffer size.
  //   char *asctime_r(const struct tm *restrict tm, char *restrict buf);
  //   // `buf` size must be at least 26 bytes according the POSIX standard.
  // Example 2. Argument as a buffer size.
  //   ctime_s(char *buffer, rsize_t bufsz, const time_t *time);
  // Example 3. The size is computed as a multiplication of other args.
  //   size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
  //   // Here, ptr is the buffer, and its minimum size is `size * nmemb`.
  class BufferSizeConstraint : public ValueConstraint {
    // The concrete value which is the minimum size for the buffer.
    std::optional<llvm::APSInt> ConcreteSize;
    // The argument which holds the size of the buffer.
    std::optional<ArgNo> SizeArgN;
    // The argument which is a multiplier to size. This is set in case of
    // `fread` like functions where the size is computed as a multiplication of
    // two arguments.
    std::optional<ArgNo> SizeMultiplierArgN;
    // The operator we use in apply. This is negated in negate().
    BinaryOperator::Opcode Op = BO_LE;

  public:
    BufferSizeConstraint(ArgNo Buffer, llvm::APSInt BufMinSize)
        : ValueConstraint(Buffer), ConcreteSize(BufMinSize) {}
    BufferSizeConstraint(ArgNo Buffer, ArgNo BufSize)
        : ValueConstraint(Buffer), SizeArgN(BufSize) {}
    BufferSizeConstraint(ArgNo Buffer, ArgNo BufSize, ArgNo BufSizeMultiplier)
        : ValueConstraint(Buffer), SizeArgN(BufSize),
          SizeMultiplierArgN(BufSizeMultiplier) {}

    ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                          const Summary &Summary,
                          CheckerContext &C) const override;

    void describe(DescriptionKind DK, const CallEvent &Call,
                  ProgramStateRef State, const Summary &Summary,
                  llvm::raw_ostream &Out) const override;

    bool describeArgumentValue(const CallEvent &Call, ProgramStateRef State,
                               const Summary &Summary,
                               llvm::raw_ostream &Out) const override;

    std::vector<ArgNo> getArgsToTrack() const override {
      std::vector<ArgNo> Result{ArgN};
      if (SizeArgN)
        Result.push_back(*SizeArgN);
      if (SizeMultiplierArgN)
        Result.push_back(*SizeMultiplierArgN);
      return Result;
    }

    ValueConstraintPtr negate() const override {
      BufferSizeConstraint Tmp(*this);
      Tmp.Op = BinaryOperator::negateComparisonOp(Op);
      return std::make_shared<BufferSizeConstraint>(Tmp);
    }

  protected:
    bool checkSpecificValidity(const FunctionDecl *FD) const override {
      const bool ValidArg = getArgType(FD, ArgN)->isPointerType();
      assert(ValidArg &&
             "This constraint should be applied only on a pointer type");
      return ValidArg;
    }
  };

  /// The complete list of constraints that defines a single branch.
  using ConstraintSet = std::vector<ValueConstraintPtr>;

  /// Define how a function affects the system variable 'errno'.
  /// This works together with the \c ErrnoModeling and \c ErrnoChecker classes.
  /// Currently 3 use cases exist: success, failure, irrelevant.
  /// In the future the failure case can be customized to set \c errno to a
  /// more specific constraint (for example > 0), or new case can be added
  /// for functions which require check of \c errno in both success and failure
  /// case.
  class ErrnoConstraintBase {
  public:
    /// Apply specific state changes related to the errno variable.
    virtual ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                                  const Summary &Summary,
                                  CheckerContext &C) const = 0;
    /// Get a description about what happens with 'errno' here and how it causes
    /// a later bug report created by ErrnoChecker.
    /// Empty return value means that 'errno' related bug may not happen from
    /// the current analyzed function.
    virtual const std::string describe(CheckerContext &C) const { return ""; }

    virtual ~ErrnoConstraintBase() {}

  protected:
    ErrnoConstraintBase() = default;

    /// This is used for conjure symbol for errno to differentiate from the
    /// original call expression (same expression is used for the errno symbol).
    static int Tag;
  };

  /// Reset errno constraints to irrelevant.
  /// This is applicable to functions that may change 'errno' and are not
  /// modeled elsewhere.
  class ResetErrnoConstraint : public ErrnoConstraintBase {
  public:
    ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                          const Summary &Summary,
                          CheckerContext &C) const override {
      return errno_modeling::setErrnoState(State, errno_modeling::Irrelevant);
    }
  };

  /// Do not change errno constraints.
  /// This is applicable to functions that are modeled in another checker
  /// and the already set errno constraints should not be changed in the
  /// post-call event.
  class NoErrnoConstraint : public ErrnoConstraintBase {
  public:
    ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                          const Summary &Summary,
                          CheckerContext &C) const override {
      return State;
    }
  };

  /// Set errno constraint at failure cases of standard functions.
  /// Failure case: 'errno' becomes not equal to 0 and may or may not be checked
  /// by the program. \c ErrnoChecker does not emit a bug report after such a
  /// function call.
  class FailureErrnoConstraint : public ErrnoConstraintBase {
  public:
    ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                          const Summary &Summary,
                          CheckerContext &C) const override {
      SValBuilder &SVB = C.getSValBuilder();
      NonLoc ErrnoSVal =
          SVB.conjureSymbolVal(&Tag, Call.getOriginExpr(),
                               C.getLocationContext(), C.getASTContext().IntTy,
                               C.blockCount())
              .castAs<NonLoc>();
      return errno_modeling::setErrnoForStdFailure(State, C, ErrnoSVal);
    }
  };

  /// Set errno constraint at success cases of standard functions.
  /// Success case: 'errno' is not allowed to be used because the value is
  /// undefined after successful call.
  /// \c ErrnoChecker can emit bug report after such a function call if errno
  /// is used.
  class SuccessErrnoConstraint : public ErrnoConstraintBase {
  public:
    ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                          const Summary &Summary,
                          CheckerContext &C) const override {
      return errno_modeling::setErrnoForStdSuccess(State, C);
    }

    const std::string describe(CheckerContext &C) const override {
      return "'errno' becomes undefined after the call";
    }
  };

  /// Set errno constraint at functions that indicate failure only with 'errno'.
  /// In this case 'errno' is required to be observed.
  /// \c ErrnoChecker can emit bug report after such a function call if errno
  /// is overwritten without a read before.
  class ErrnoMustBeCheckedConstraint : public ErrnoConstraintBase {
  public:
    ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                          const Summary &Summary,
                          CheckerContext &C) const override {
      return errno_modeling::setErrnoStdMustBeChecked(State, C,
                                                      Call.getOriginExpr());
    }

    const std::string describe(CheckerContext &C) const override {
      return "reading 'errno' is required to find out if the call has failed";
    }
  };

  /// A single branch of a function summary.
  ///
  /// A branch is defined by a series of constraints - "assumptions" -
  /// that together form a single possible outcome of invoking the function.
  /// When static analyzer considers a branch, it tries to introduce
  /// a child node in the Exploded Graph. The child node has to include
  /// constraints that define the branch. If the constraints contradict
  /// existing constraints in the state, the node is not created and the branch
  /// is dropped; otherwise it's queued for future exploration.
  /// The branch is accompanied by a note text that may be displayed
  /// to the user when a bug is found on a path that takes this branch.
  ///
  /// For example, consider the branches in `isalpha(x)`:
  ///   Branch 1)
  ///     x is in range ['A', 'Z'] or in ['a', 'z']
  ///     then the return value is not 0. (I.e. out-of-range [0, 0])
  ///     and the note may say "Assuming the character is alphabetical"
  ///   Branch 2)
  ///     x is out-of-range ['A', 'Z'] and out-of-range ['a', 'z']
  ///     then the return value is 0
  ///     and the note may say "Assuming the character is non-alphabetical".
  class SummaryCase {
    ConstraintSet Constraints;
    const ErrnoConstraintBase &ErrnoConstraint;
    StringRef Note;

  public:
    SummaryCase(ConstraintSet &&Constraints, const ErrnoConstraintBase &ErrnoC,
                StringRef Note)
        : Constraints(std::move(Constraints)), ErrnoConstraint(ErrnoC),
          Note(Note) {}

    SummaryCase(const ConstraintSet &Constraints,
                const ErrnoConstraintBase &ErrnoC, StringRef Note)
        : Constraints(Constraints), ErrnoConstraint(ErrnoC), Note(Note) {}

    const ConstraintSet &getConstraints() const { return Constraints; }
    const ErrnoConstraintBase &getErrnoConstraint() const {
      return ErrnoConstraint;
    }
    StringRef getNote() const { return Note; }
  };

  using ArgTypes = ArrayRef<std::optional<QualType>>;
  using RetType = std::optional<QualType>;

  // A placeholder type, we use it whenever we do not care about the concrete
  // type in a Signature.
  const QualType Irrelevant{};
  bool static isIrrelevant(QualType T) { return T.isNull(); }

  // The signature of a function we want to describe with a summary. This is a
  // concessive signature, meaning there may be irrelevant types in the
  // signature which we do not check against a function with concrete types.
  // All types in the spec need to be canonical.
  class Signature {
    using ArgQualTypes = std::vector<QualType>;
    ArgQualTypes ArgTys;
    QualType RetTy;
    // True if any component type is not found by lookup.
    bool Invalid = false;

  public:
    // Construct a signature from optional types. If any of the optional types
    // are not set then the signature will be invalid.
    Signature(ArgTypes ArgTys, RetType RetTy) {
      for (std::optional<QualType> Arg : ArgTys) {
        if (!Arg) {
          Invalid = true;
          return;
        } else {
          assertArgTypeSuitableForSignature(*Arg);
          this->ArgTys.push_back(*Arg);
        }
      }
      if (!RetTy) {
        Invalid = true;
        return;
      } else {
        assertRetTypeSuitableForSignature(*RetTy);
        this->RetTy = *RetTy;
      }
    }

    bool isInvalid() const { return Invalid; }
    bool matches(const FunctionDecl *FD) const;

  private:
    static void assertArgTypeSuitableForSignature(QualType T) {
      assert((T.isNull() || !T->isVoidType()) &&
             "We should have no void types in the spec");
      assert((T.isNull() || T.isCanonical()) &&
             "We should only have canonical types in the spec");
    }
    static void assertRetTypeSuitableForSignature(QualType T) {
      assert((T.isNull() || T.isCanonical()) &&
             "We should only have canonical types in the spec");
    }
  };

  static QualType getArgType(const FunctionDecl *FD, ArgNo ArgN) {
    assert(FD && "Function must be set");
    QualType T = (ArgN == Ret)
                     ? FD->getReturnType().getCanonicalType()
                     : FD->getParamDecl(ArgN)->getType().getCanonicalType();
    return T;
  }

  using SummaryCases = std::vector<SummaryCase>;

  /// A summary includes information about
  ///   * function prototype (signature)
  ///   * approach to invalidation,
  ///   * a list of branches - so, a list of list of ranges,
  ///   * a list of argument constraints, that must be true on every branch.
  ///     If these constraints are not satisfied that means a fatal error
  ///     usually resulting in undefined behaviour.
  ///
  /// Application of a summary:
  ///   The signature and argument constraints together contain information
  ///   about which functions are handled by the summary. The signature can use
  ///   "wildcards", i.e. Irrelevant types. Irrelevant type of a parameter in
  ///   a signature means that type is not compared to the type of the parameter
  ///   in the found FunctionDecl. Argument constraints may specify additional
  ///   rules for the given parameter's type, those rules are checked once the
  ///   signature is matched.
  class Summary {
    const InvalidationKind InvalidationKd;
    SummaryCases Cases;
    ConstraintSet ArgConstraints;

    // The function to which the summary applies. This is set after lookup and
    // match to the signature.
    const FunctionDecl *FD = nullptr;

  public:
    Summary(InvalidationKind InvalidationKd) : InvalidationKd(InvalidationKd) {}

    Summary &Case(ConstraintSet &&CS, const ErrnoConstraintBase &ErrnoC,
                  StringRef Note = "") {
      Cases.push_back(SummaryCase(std::move(CS), ErrnoC, Note));
      return *this;
    }
    Summary &Case(const ConstraintSet &CS, const ErrnoConstraintBase &ErrnoC,
                  StringRef Note = "") {
      Cases.push_back(SummaryCase(CS, ErrnoC, Note));
      return *this;
    }
    Summary &ArgConstraint(ValueConstraintPtr VC) {
      assert(VC->getArgNo() != Ret &&
             "Arg constraint should not refer to the return value");
      ArgConstraints.push_back(VC);
      return *this;
    }

    InvalidationKind getInvalidationKd() const { return InvalidationKd; }
    const SummaryCases &getCases() const { return Cases; }
    const ConstraintSet &getArgConstraints() const { return ArgConstraints; }

    QualType getArgType(ArgNo ArgN) const {
      return StdLibraryFunctionsChecker::getArgType(FD, ArgN);
    }

    // Returns true if the summary should be applied to the given function.
    // And if yes then store the function declaration.
    bool matchesAndSet(const Signature &Sign, const FunctionDecl *FD) {
      bool Result = Sign.matches(FD) && validateByConstraints(FD);
      if (Result) {
        assert(!this->FD && "FD must not be set more than once");
        this->FD = FD;
      }
      return Result;
    }

  private:
    // Once we know the exact type of the function then do validation check on
    // all the given constraints.
    bool validateByConstraints(const FunctionDecl *FD) const {
      for (const SummaryCase &Case : Cases)
        for (const ValueConstraintPtr &Constraint : Case.getConstraints())
          if (!Constraint->checkValidity(FD))
            return false;
      for (const ValueConstraintPtr &Constraint : ArgConstraints)
        if (!Constraint->checkValidity(FD))
          return false;
      return true;
    }
  };

  // The map of all functions supported by the checker. It is initialized
  // lazily, and it doesn't change after initialization.
  using FunctionSummaryMapType = llvm::DenseMap<const FunctionDecl *, Summary>;
  mutable FunctionSummaryMapType FunctionSummaryMap;

  const BugType BT_InvalidArg{this, "Function call with invalid argument"};
  mutable bool SummariesInitialized = false;

  static SVal getArgSVal(const CallEvent &Call, ArgNo ArgN) {
    return ArgN == Ret ? Call.getReturnValue() : Call.getArgSVal(ArgN);
  }
  static std::string getFunctionName(const CallEvent &Call) {
    assert(Call.getDecl() &&
           "Call was found by a summary, should have declaration");
    return cast<NamedDecl>(Call.getDecl())->getNameAsString();
  }

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  bool evalCall(const CallEvent &Call, CheckerContext &C) const;

  CheckerNameRef CheckName;
  bool AddTestFunctions = false;

  bool DisplayLoadedSummaries = false;
  bool ModelPOSIX = false;
  bool ShouldAssumeControlledEnvironment = false;

private:
  std::optional<Summary> findFunctionSummary(const FunctionDecl *FD,
                                             CheckerContext &C) const;
  std::optional<Summary> findFunctionSummary(const CallEvent &Call,
                                             CheckerContext &C) const;

  void initFunctionSummaries(CheckerContext &C) const;

  void reportBug(const CallEvent &Call, ExplodedNode *N,
                 const ValueConstraint *VC, const ValueConstraint *NegatedVC,
                 const Summary &Summary, CheckerContext &C) const {
    assert(Call.getDecl() &&
           "Function found in summary must have a declaration available");
    SmallString<256> Msg;
    llvm::raw_svector_ostream MsgOs(Msg);

    MsgOs << "The ";
    printArgDesc(VC->getArgNo(), MsgOs);
    MsgOs << " to '" << getFunctionName(Call) << "' ";
    bool ValuesPrinted =
        NegatedVC->describeArgumentValue(Call, N->getState(), Summary, MsgOs);
    if (ValuesPrinted)
      MsgOs << " but ";
    else
      MsgOs << "is out of the accepted range; It ";
    VC->describe(ValueConstraint::Violation, Call, C.getState(), Summary,
                 MsgOs);
    Msg[0] = toupper(Msg[0]);
    auto R = std::make_unique<PathSensitiveBugReport>(BT_InvalidArg, Msg, N);

    for (ArgNo ArgN : VC->getArgsToTrack()) {
      bugreporter::trackExpressionValue(N, Call.getArgExpr(ArgN), *R);
      R->markInteresting(Call.getArgSVal(ArgN));
      // All tracked arguments are important, highlight them.
      R->addRange(Call.getArgSourceRange(ArgN));
    }

    C.emitReport(std::move(R));
  }

  /// These are the errno constraints that can be passed to summary cases.
  /// One of these should fit for a single summary case.
  /// Usually if a failure return value exists for function, that function
  /// needs different cases for success and failure with different errno
  /// constraints (and different return value constraints).
  const NoErrnoConstraint ErrnoUnchanged{};
  const ResetErrnoConstraint ErrnoIrrelevant{};
  const ErrnoMustBeCheckedConstraint ErrnoMustBeChecked{};
  const SuccessErrnoConstraint ErrnoMustNotBeChecked{};
  const FailureErrnoConstraint ErrnoNEZeroIrrelevant{};
};

int StdLibraryFunctionsChecker::ErrnoConstraintBase::Tag = 0;

const StdLibraryFunctionsChecker::ArgNo StdLibraryFunctionsChecker::Ret =
    std::numeric_limits<ArgNo>::max();

static BasicValueFactory &getBVF(ProgramStateRef State) {
  ProgramStateManager &Mgr = State->getStateManager();
  SValBuilder &SVB = Mgr.getSValBuilder();
  return SVB.getBasicValueFactory();
}

} // end of anonymous namespace

void StdLibraryFunctionsChecker::printArgDesc(
    StdLibraryFunctionsChecker::ArgNo ArgN, llvm::raw_ostream &Out) {
  Out << std::to_string(ArgN + 1);
  Out << llvm::getOrdinalSuffix(ArgN + 1);
  Out << " argument";
}

void StdLibraryFunctionsChecker::printArgValueInfo(ArgNo ArgN,
                                                   ProgramStateRef State,
                                                   const CallEvent &Call,
                                                   llvm::raw_ostream &Out) {
  if (const llvm::APSInt *Val =
          State->getStateManager().getSValBuilder().getKnownValue(
              State, getArgSVal(Call, ArgN)))
    Out << " (which is " << *Val << ")";
}

void StdLibraryFunctionsChecker::appendInsideRangeDesc(llvm::APSInt RMin,
                                                       llvm::APSInt RMax,
                                                       QualType ArgT,
                                                       BasicValueFactory &BVF,
                                                       llvm::raw_ostream &Out) {
  if (RMin.isZero() && RMax.isZero())
    Out << "zero";
  else if (RMin == RMax)
    Out << RMin;
  else if (RMin == BVF.getMinValue(ArgT)) {
    if (RMax == -1)
      Out << "< 0";
    else
      Out << "<= " << RMax;
  } else if (RMax == BVF.getMaxValue(ArgT)) {
    if (RMin.isOne())
      Out << "> 0";
    else
      Out << ">= " << RMin;
  } else if (RMin.isNegative() == RMax.isNegative() &&
             RMin.getLimitedValue() == RMax.getLimitedValue() - 1) {
    Out << RMin << " or " << RMax;
  } else {
    Out << "between " << RMin << " and " << RMax;
  }
}

void StdLibraryFunctionsChecker::appendOutOfRangeDesc(llvm::APSInt RMin,
                                                      llvm::APSInt RMax,
                                                      QualType ArgT,
                                                      BasicValueFactory &BVF,
                                                      llvm::raw_ostream &Out) {
  if (RMin.isZero() && RMax.isZero())
    Out << "nonzero";
  else if (RMin == RMax) {
    Out << "not equal to " << RMin;
  } else if (RMin == BVF.getMinValue(ArgT)) {
    if (RMax == -1)
      Out << ">= 0";
    else
      Out << "> " << RMax;
  } else if (RMax == BVF.getMaxValue(ArgT)) {
    if (RMin.isOne())
      Out << "<= 0";
    else
      Out << "< " << RMin;
  } else if (RMin.isNegative() == RMax.isNegative() &&
             RMin.getLimitedValue() == RMax.getLimitedValue() - 1) {
    Out << "not " << RMin << " and not " << RMax;
  } else {
    Out << "not between " << RMin << " and " << RMax;
  }
}

void StdLibraryFunctionsChecker::RangeConstraint::applyOnWithinRange(
    BasicValueFactory &BVF, QualType ArgT, const RangeApplyFunction &F) const {
  if (Ranges.empty())
    return;

  for (auto [Start, End] : getRanges()) {
    const llvm::APSInt &Min = BVF.getValue(Start, ArgT);
    const llvm::APSInt &Max = BVF.getValue(End, ArgT);
    assert(Min <= Max);
    if (!F(Min, Max))
      return;
  }
}

void StdLibraryFunctionsChecker::RangeConstraint::applyOnOutOfRange(
    BasicValueFactory &BVF, QualType ArgT, const RangeApplyFunction &F) const {
  if (Ranges.empty())
    return;

  const IntRangeVector &R = getRanges();
  size_t E = R.size();

  const llvm::APSInt &MinusInf = BVF.getMinValue(ArgT);
  const llvm::APSInt &PlusInf = BVF.getMaxValue(ArgT);

  const llvm::APSInt &RangeLeft = BVF.getValue(R[0].first - 1ULL, ArgT);
  const llvm::APSInt &RangeRight = BVF.getValue(R[E - 1].second + 1ULL, ArgT);

  // Iterate over the "holes" between intervals.
  for (size_t I = 1; I != E; ++I) {
    const llvm::APSInt &Min = BVF.getValue(R[I - 1].second + 1ULL, ArgT);
    const llvm::APSInt &Max = BVF.getValue(R[I].first - 1ULL, ArgT);
    if (Min <= Max) {
      if (!F(Min, Max))
        return;
    }
  }
  // Check the interval [T_MIN, min(R) - 1].
  if (RangeLeft != PlusInf) {
    assert(MinusInf <= RangeLeft);
    if (!F(MinusInf, RangeLeft))
      return;
  }
  // Check the interval [max(R) + 1, T_MAX],
  if (RangeRight != MinusInf) {
    assert(RangeRight <= PlusInf);
    if (!F(RangeRight, PlusInf))
      return;
  }
}

ProgramStateRef StdLibraryFunctionsChecker::RangeConstraint::apply(
    ProgramStateRef State, const CallEvent &Call, const Summary &Summary,
    CheckerContext &C) const {
  ConstraintManager &CM = C.getConstraintManager();
  SVal V = getArgSVal(Call, getArgNo());
  QualType T = Summary.getArgType(getArgNo());

  if (auto N = V.getAs<NonLoc>()) {
    auto ExcludeRangeFromArg = [&](const llvm::APSInt &Min,
                                   const llvm::APSInt &Max) {
      State = CM.assumeInclusiveRange(State, *N, Min, Max, false);
      return static_cast<bool>(State);
    };
    // "OutOfRange R" is handled by excluding all ranges in R.
    // "WithinRange R" is treated as "OutOfRange [T_MIN, T_MAX] \ R".
    applyOnRange(negateKind(Kind), C.getSValBuilder().getBasicValueFactory(), T,
                 ExcludeRangeFromArg);
  }

  return State;
}

void StdLibraryFunctionsChecker::RangeConstraint::describe(
    DescriptionKind DK, const CallEvent &Call, ProgramStateRef State,
    const Summary &Summary, llvm::raw_ostream &Out) const {

  BasicValueFactory &BVF = getBVF(State);
  QualType T = Summary.getArgType(getArgNo());

  Out << ((DK == Violation) ? "should be " : "is ");
  if (!Description.empty()) {
    Out << Description;
  } else {
    unsigned I = Ranges.size();
    if (Kind == WithinRange) {
      for (const std::pair<RangeInt, RangeInt> &R : Ranges) {
        appendInsideRangeDesc(BVF.getValue(R.first, T),
                              BVF.getValue(R.second, T), T, BVF, Out);
        if (--I > 0)
          Out << " or ";
      }
    } else {
      for (const std::pair<RangeInt, RangeInt> &R : Ranges) {
        appendOutOfRangeDesc(BVF.getValue(R.first, T),
                             BVF.getValue(R.second, T), T, BVF, Out);
        if (--I > 0)
          Out << " and ";
      }
    }
  }
}

bool StdLibraryFunctionsChecker::RangeConstraint::describeArgumentValue(
    const CallEvent &Call, ProgramStateRef State, const Summary &Summary,
    llvm::raw_ostream &Out) const {
  unsigned int NRanges = 0;
  bool HaveAllRanges = true;

  ProgramStateManager &Mgr = State->getStateManager();
  BasicValueFactory &BVF = Mgr.getSValBuilder().getBasicValueFactory();
  ConstraintManager &CM = Mgr.getConstraintManager();
  SVal V = getArgSVal(Call, getArgNo());

  if (auto N = V.getAs<NonLoc>()) {
    if (const llvm::APSInt *Int = N->getAsInteger()) {
      Out << "is ";
      Out << *Int;
      return true;
    }
    QualType T = Summary.getArgType(getArgNo());
    SmallString<128> MoreInfo;
    llvm::raw_svector_ostream MoreInfoOs(MoreInfo);
    auto ApplyF = [&](const llvm::APSInt &Min, const llvm::APSInt &Max) {
      if (CM.assumeInclusiveRange(State, *N, Min, Max, true)) {
        if (NRanges > 0)
          MoreInfoOs << " or ";
        appendInsideRangeDesc(Min, Max, T, BVF, MoreInfoOs);
        ++NRanges;
      } else {
        HaveAllRanges = false;
      }
      return true;
    };

    applyOnRange(Kind, BVF, T, ApplyF);
    assert(NRanges > 0);
    if (!HaveAllRanges || NRanges == 1) {
      Out << "is ";
      Out << MoreInfo;
      return true;
    }
  }
  return false;
}

ProgramStateRef StdLibraryFunctionsChecker::ComparisonConstraint::apply(
    ProgramStateRef State, const CallEvent &Call, const Summary &Summary,
    CheckerContext &C) const {

  ProgramStateManager &Mgr = State->getStateManager();
  SValBuilder &SVB = Mgr.getSValBuilder();
  QualType CondT = SVB.getConditionType();
  QualType T = Summary.getArgType(getArgNo());
  SVal V = getArgSVal(Call, getArgNo());

  BinaryOperator::Opcode Op = getOpcode();
  ArgNo OtherArg = getOtherArgNo();
  SVal OtherV = getArgSVal(Call, OtherArg);
  QualType OtherT = Summary.getArgType(OtherArg);
  // Note: we avoid integral promotion for comparison.
  OtherV = SVB.evalCast(OtherV, T, OtherT);
  if (auto CompV = SVB.evalBinOp(State, Op, V, OtherV, CondT)
                       .getAs<DefinedOrUnknownSVal>())
    State = State->assume(*CompV, true);
  return State;
}

ProgramStateRef StdLibraryFunctionsChecker::NotNullConstraint::apply(
    ProgramStateRef State, const CallEvent &Call, const Summary &Summary,
    CheckerContext &C) const {
  SVal V = getArgSVal(Call, getArgNo());
  if (V.isUndef())
    return State;

  DefinedOrUnknownSVal L = V.castAs<DefinedOrUnknownSVal>();
  if (!isa<Loc>(L))
    return State;

  return State->assume(L, CannotBeNull);
}

void StdLibraryFunctionsChecker::NotNullConstraint::describe(
    DescriptionKind DK, const CallEvent &Call, ProgramStateRef State,
    const Summary &Summary, llvm::raw_ostream &Out) const {
  assert(CannotBeNull &&
         "Describe should not be used when the value must be NULL");
  if (DK == Violation)
    Out << "should not be NULL";
  else
    Out << "is not NULL";
}

bool StdLibraryFunctionsChecker::NotNullConstraint::describeArgumentValue(
    const CallEvent &Call, ProgramStateRef State, const Summary &Summary,
    llvm::raw_ostream &Out) const {
  assert(!CannotBeNull && "This function is used when the value is NULL");
  Out << "is NULL";
  return true;
}

ProgramStateRef StdLibraryFunctionsChecker::NotNullBufferConstraint::apply(
    ProgramStateRef State, const CallEvent &Call, const Summary &Summary,
    CheckerContext &C) const {
  SVal V = getArgSVal(Call, getArgNo());
  if (V.isUndef())
    return State;
  DefinedOrUnknownSVal L = V.castAs<DefinedOrUnknownSVal>();
  if (!isa<Loc>(L))
    return State;

  std::optional<DefinedOrUnknownSVal> SizeArg1 =
      getArgSVal(Call, SizeArg1N).getAs<DefinedOrUnknownSVal>();
  std::optional<DefinedOrUnknownSVal> SizeArg2;
  if (SizeArg2N)
    SizeArg2 = getArgSVal(Call, *SizeArg2N).getAs<DefinedOrUnknownSVal>();

  auto IsArgZero = [State](std::optional<DefinedOrUnknownSVal> Val) {
    if (!Val)
      return false;
    auto [IsNonNull, IsNull] = State->assume(*Val);
    return IsNull && !IsNonNull;
  };

  if (IsArgZero(SizeArg1) || IsArgZero(SizeArg2))
    return State;

  return State->assume(L, CannotBeNull);
}

void StdLibraryFunctionsChecker::NotNullBufferConstraint::describe(
    DescriptionKind DK, const CallEvent &Call, ProgramStateRef State,
    const Summary &Summary, llvm::raw_ostream &Out) const {
  assert(CannotBeNull &&
         "Describe should not be used when the value must be NULL");
  if (DK == Violation)
    Out << "should not be NULL";
  else
    Out << "is not NULL";
}

bool StdLibraryFunctionsChecker::NotNullBufferConstraint::describeArgumentValue(
    const CallEvent &Call, ProgramStateRef State, const Summary &Summary,
    llvm::raw_ostream &Out) const {
  assert(!CannotBeNull && "This function is used when the value is NULL");
  Out << "is NULL";
  return true;
}

ProgramStateRef StdLibraryFunctionsChecker::BufferSizeConstraint::apply(
    ProgramStateRef State, const CallEvent &Call, const Summary &Summary,
    CheckerContext &C) const {
  SValBuilder &SvalBuilder = C.getSValBuilder();
  // The buffer argument.
  SVal BufV = getArgSVal(Call, getArgNo());

  // Get the size constraint.
  const SVal SizeV = [this, &State, &Call, &Summary, &SvalBuilder]() {
    if (ConcreteSize) {
      return SVal(SvalBuilder.makeIntVal(*ConcreteSize));
    }
    assert(SizeArgN && "The constraint must be either a concrete value or "
                       "encoded in an argument.");
    // The size argument.
    SVal SizeV = getArgSVal(Call, *SizeArgN);
    // Multiply with another argument if given.
    if (SizeMultiplierArgN) {
      SVal SizeMulV = getArgSVal(Call, *SizeMultiplierArgN);
      SizeV = SvalBuilder.evalBinOp(State, BO_Mul, SizeV, SizeMulV,
                                    Summary.getArgType(*SizeArgN));
    }
    return SizeV;
  }();

  // The dynamic size of the buffer argument, got from the analyzer engine.
  SVal BufDynSize = getDynamicExtentWithOffset(State, BufV);

  SVal Feasible = SvalBuilder.evalBinOp(State, Op, SizeV, BufDynSize,
                                        SvalBuilder.getContext().BoolTy);
  if (auto F = Feasible.getAs<DefinedOrUnknownSVal>())
    return State->assume(*F, true);

  // We can get here only if the size argument or the dynamic size is
  // undefined. But the dynamic size should never be undefined, only
  // unknown. So, here, the size of the argument is undefined, i.e. we
  // cannot apply the constraint. Actually, other checkers like
  // CallAndMessage should catch this situation earlier, because we call a
  // function with an uninitialized argument.
  llvm_unreachable("Size argument or the dynamic size is Undefined");
}

void StdLibraryFunctionsChecker::BufferSizeConstraint::describe(
    DescriptionKind DK, const CallEvent &Call, ProgramStateRef State,
    const Summary &Summary, llvm::raw_ostream &Out) const {
  Out << ((DK == Violation) ? "should be " : "is ");
  Out << "a buffer with size equal to or greater than ";
  if (ConcreteSize) {
    Out << *ConcreteSize;
  } else if (SizeArgN) {
    Out << "the value of the ";
    printArgDesc(*SizeArgN, Out);
    printArgValueInfo(*SizeArgN, State, Call, Out);
    if (SizeMultiplierArgN) {
      Out << " times the ";
      printArgDesc(*SizeMultiplierArgN, Out);
      printArgValueInfo(*SizeMultiplierArgN, State, Call, Out);
    }
  }
}

bool StdLibraryFunctionsChecker::BufferSizeConstraint::describeArgumentValue(
    const CallEvent &Call, ProgramStateRef State, const Summary &Summary,
    llvm::raw_ostream &Out) const {
  SVal BufV = getArgSVal(Call, getArgNo());
  SVal BufDynSize = getDynamicExtentWithOffset(State, BufV);
  if (const llvm::APSInt *Val =
          State->getStateManager().getSValBuilder().getKnownValue(State,
                                                                  BufDynSize)) {
    Out << "is a buffer with size " << *Val;
    return true;
  }
  return false;
}

void StdLibraryFunctionsChecker::checkPreCall(const CallEvent &Call,
                                              CheckerContext &C) const {
  std::optional<Summary> FoundSummary = findFunctionSummary(Call, C);
  if (!FoundSummary)
    return;

  const Summary &Summary = *FoundSummary;
  ProgramStateRef State = C.getState();

  ProgramStateRef NewState = State;
  ExplodedNode *NewNode = C.getPredecessor();
  for (const ValueConstraintPtr &Constraint : Summary.getArgConstraints()) {
    ValueConstraintPtr NegatedConstraint = Constraint->negate();
    ProgramStateRef SuccessSt = Constraint->apply(NewState, Call, Summary, C);
    ProgramStateRef FailureSt =
        NegatedConstraint->apply(NewState, Call, Summary, C);
    // The argument constraint is not satisfied.
    if (FailureSt && !SuccessSt) {
      if (ExplodedNode *N = C.generateErrorNode(State, NewNode))
        reportBug(Call, N, Constraint.get(), NegatedConstraint.get(), Summary,
                  C);
      break;
    }
    // We will apply the constraint even if we cannot reason about the
    // argument. This means both SuccessSt and FailureSt can be true. If we
    // weren't applying the constraint that would mean that symbolic
    // execution continues on a code whose behaviour is undefined.
    assert(SuccessSt);
    NewState = SuccessSt;
    if (NewState != State) {
      SmallString<128> Msg;
      llvm::raw_svector_ostream Os(Msg);
      Os << "Assuming that the ";
      printArgDesc(Constraint->getArgNo(), Os);
      Os << " to '";
      Os << getFunctionName(Call);
      Os << "' ";
      Constraint->describe(ValueConstraint::Assumption, Call, NewState, Summary,
                           Os);
      const auto ArgSVal = Call.getArgSVal(Constraint->getArgNo());
      NewNode = C.addTransition(
          NewState, NewNode,
          C.getNoteTag([Msg = std::move(Msg), ArgSVal](
                           PathSensitiveBugReport &BR, llvm::raw_ostream &OS) {
            if (BR.isInteresting(ArgSVal))
              OS << Msg;
          }));
    }
  }
}

void StdLibraryFunctionsChecker::checkPostCall(const CallEvent &Call,
                                               CheckerContext &C) const {
  std::optional<Summary> FoundSummary = findFunctionSummary(Call, C);
  if (!FoundSummary)
    return;

  // Now apply the constraints.
  const Summary &Summary = *FoundSummary;
  ProgramStateRef State = C.getState();
  ExplodedNode *Node = C.getPredecessor();

  // Apply case/branch specifications.
  for (const SummaryCase &Case : Summary.getCases()) {
    ProgramStateRef NewState = State;
    for (const ValueConstraintPtr &Constraint : Case.getConstraints()) {
      NewState = Constraint->apply(NewState, Call, Summary, C);
      if (!NewState)
        break;
    }

    if (NewState)
      NewState = Case.getErrnoConstraint().apply(NewState, Call, Summary, C);

    if (!NewState)
      continue;

    // Here it's possible that NewState == State, e.g. when other checkers
    // already applied the same constraints (or stricter ones).
    // Still add these note tags, the other checker should add only its
    // specialized note tags. These general note tags are handled always by
    // StdLibraryFunctionsChecker.

    ExplodedNode *Pred = Node;
    DeclarationName FunctionName =
        cast<NamedDecl>(Call.getDecl())->getDeclName();

    std::string ErrnoNote = Case.getErrnoConstraint().describe(C);
    std::string CaseNote;
    if (Case.getNote().empty()) {
      if (!ErrnoNote.empty())
        ErrnoNote =
            llvm::formatv("After calling '{0}' {1}", FunctionName, ErrnoNote);
    } else {
      CaseNote = llvm::formatv(Case.getNote().str().c_str(), FunctionName);
    }
    const SVal RV = Call.getReturnValue();

    if (Summary.getInvalidationKd() == EvalCallAsPure) {
      // Do not expect that errno is interesting (the "pure" functions do not
      // affect it).
      if (!CaseNote.empty()) {
        const NoteTag *Tag = C.getNoteTag(
            [Node, CaseNote, RV](PathSensitiveBugReport &BR) -> std::string {
              // Try to omit the note if we know in advance which branch is
              // taken (this means, only one branch exists).
              // This check is performed inside the lambda, after other
              // (or this) checkers had a chance to add other successors.
              // Dereferencing the saved node object is valid because it's part
              // of a bug report call sequence.
              // FIXME: This check is not exact. We may be here after a state
              // split that was performed by another checker (and can not find
              // the successors). This is why this check is only used in the
              // EvalCallAsPure case.
              if (BR.isInteresting(RV) && Node->succ_size() > 1)
                return CaseNote;
              return "";
            });
        Pred = C.addTransition(NewState, Pred, Tag);
      }
    } else {
      if (!CaseNote.empty() || !ErrnoNote.empty()) {
        const NoteTag *Tag =
            C.getNoteTag([CaseNote, ErrnoNote,
                          RV](PathSensitiveBugReport &BR) -> std::string {
              // If 'errno' is interesting, show the user a note about the case
              // (what happened at the function call) and about how 'errno'
              // causes the problem. ErrnoChecker sets the errno (but not RV) to
              // interesting.
              // If only the return value is interesting, show only the case
              // note.
              std::optional<Loc> ErrnoLoc =
                  errno_modeling::getErrnoLoc(BR.getErrorNode()->getState());
              bool ErrnoImportant = !ErrnoNote.empty() && ErrnoLoc &&
                                    BR.isInteresting(ErrnoLoc->getAsRegion());
              if (ErrnoImportant) {
                BR.markNotInteresting(ErrnoLoc->getAsRegion());
                if (CaseNote.empty())
                  return ErrnoNote;
                return llvm::formatv("{0}; {1}", CaseNote, ErrnoNote);
              } else {
                if (BR.isInteresting(RV))
                  return CaseNote;
              }
              return "";
            });
        Pred = C.addTransition(NewState, Pred, Tag);
      }
    }

    // Add the transition if no note tag was added.
    if (Pred == Node && NewState != State)
      C.addTransition(NewState);
  }
}

bool StdLibraryFunctionsChecker::evalCall(const CallEvent &Call,
                                          CheckerContext &C) const {
  std::optional<Summary> FoundSummary = findFunctionSummary(Call, C);
  if (!FoundSummary)
    return false;

  const Summary &Summary = *FoundSummary;
  switch (Summary.getInvalidationKd()) {
  case EvalCallAsPure: {
    ProgramStateRef State = C.getState();
    const LocationContext *LC = C.getLocationContext();
    const auto *CE = cast<CallExpr>(Call.getOriginExpr());
    SVal V = C.getSValBuilder().conjureSymbolVal(
        CE, LC, CE->getType().getCanonicalType(), C.blockCount());
    State = State->BindExpr(CE, LC, V);

    C.addTransition(State);

    return true;
  }
  case NoEvalCall:
    // Summary tells us to avoid performing eval::Call. The function is possibly
    // evaluated by another checker, or evaluated conservatively.
    return false;
  }
  llvm_unreachable("Unknown invalidation kind!");
}

bool StdLibraryFunctionsChecker::Signature::matches(
    const FunctionDecl *FD) const {
  assert(!isInvalid());
  // Check the number of arguments.
  if (FD->param_size() != ArgTys.size())
    return false;

  // The "restrict" keyword is illegal in C++, however, many libc
  // implementations use the "__restrict" compiler intrinsic in functions
  // prototypes. The "__restrict" keyword qualifies a type as a restricted type
  // even in C++.
  // In case of any non-C99 languages, we don't want to match based on the
  // restrict qualifier because we cannot know if the given libc implementation
  // qualifies the paramter type or not.
  auto RemoveRestrict = [&FD](QualType T) {
    if (!FD->getASTContext().getLangOpts().C99)
      T.removeLocalRestrict();
    return T;
  };

  // Check the return type.
  if (!isIrrelevant(RetTy)) {
    QualType FDRetTy = RemoveRestrict(FD->getReturnType().getCanonicalType());
    if (RetTy != FDRetTy)
      return false;
  }

  // Check the argument types.
  for (auto [Idx, ArgTy] : llvm::enumerate(ArgTys)) {
    if (isIrrelevant(ArgTy))
      continue;
    QualType FDArgTy =
        RemoveRestrict(FD->getParamDecl(Idx)->getType().getCanonicalType());
    if (ArgTy != FDArgTy)
      return false;
  }

  return true;
}

std::optional<StdLibraryFunctionsChecker::Summary>
StdLibraryFunctionsChecker::findFunctionSummary(const FunctionDecl *FD,
                                                CheckerContext &C) const {
  if (!FD)
    return std::nullopt;

  initFunctionSummaries(C);

  auto FSMI = FunctionSummaryMap.find(FD->getCanonicalDecl());
  if (FSMI == FunctionSummaryMap.end())
    return std::nullopt;
  return FSMI->second;
}

std::optional<StdLibraryFunctionsChecker::Summary>
StdLibraryFunctionsChecker::findFunctionSummary(const CallEvent &Call,
                                                CheckerContext &C) const {
  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!FD)
    return std::nullopt;
  return findFunctionSummary(FD, C);
}

void StdLibraryFunctionsChecker::initFunctionSummaries(
    CheckerContext &C) const {
  if (SummariesInitialized)
    return;
  SummariesInitialized = true;

  SValBuilder &SVB = C.getSValBuilder();
  BasicValueFactory &BVF = SVB.getBasicValueFactory();
  const ASTContext &ACtx = BVF.getContext();
  Preprocessor &PP = C.getPreprocessor();

  // Helper class to lookup a type by its name.
  class LookupType {
    const ASTContext &ACtx;

  public:
    LookupType(const ASTContext &ACtx) : ACtx(ACtx) {}

    // Find the type. If not found then the optional is not set.
    std::optional<QualType> operator()(StringRef Name) {
      IdentifierInfo &II = ACtx.Idents.get(Name);
      auto LookupRes = ACtx.getTranslationUnitDecl()->lookup(&II);
      if (LookupRes.empty())
        return std::nullopt;

      // Prioritze typedef declarations.
      // This is needed in case of C struct typedefs. E.g.:
      //   typedef struct FILE FILE;
      // In this case, we have a RecordDecl 'struct FILE' with the name 'FILE'
      // and we have a TypedefDecl with the name 'FILE'.
      for (Decl *D : LookupRes)
        if (auto *TD = dyn_cast<TypedefNameDecl>(D))
          return ACtx.getTypeDeclType(TD).getCanonicalType();

      // Find the first TypeDecl.
      // There maybe cases when a function has the same name as a struct.
      // E.g. in POSIX: `struct stat` and the function `stat()`:
      //   int stat(const char *restrict path, struct stat *restrict buf);
      for (Decl *D : LookupRes)
        if (auto *TD = dyn_cast<TypeDecl>(D))
          return ACtx.getTypeDeclType(TD).getCanonicalType();
      return std::nullopt;
    }
  } lookupTy(ACtx);

  // Below are auxiliary classes to handle optional types that we get as a
  // result of the lookup.
  class GetRestrictTy {
    const ASTContext &ACtx;

  public:
    GetRestrictTy(const ASTContext &ACtx) : ACtx(ACtx) {}
    QualType operator()(QualType Ty) {
      return ACtx.getLangOpts().C99 ? ACtx.getRestrictType(Ty) : Ty;
    }
    std::optional<QualType> operator()(std::optional<QualType> Ty) {
      if (Ty)
        return operator()(*Ty);
      return std::nullopt;
    }
  } getRestrictTy(ACtx);
  class GetPointerTy {
    const ASTContext &ACtx;

  public:
    GetPointerTy(const ASTContext &ACtx) : ACtx(ACtx) {}
    QualType operator()(QualType Ty) { return ACtx.getPointerType(Ty); }
    std::optional<QualType> operator()(std::optional<QualType> Ty) {
      if (Ty)
        return operator()(*Ty);
      return std::nullopt;
    }
  } getPointerTy(ACtx);
  class {
  public:
    std::optional<QualType> operator()(std::optional<QualType> Ty) {
      return Ty ? std::optional<QualType>(Ty->withConst()) : std::nullopt;
    }
    QualType operator()(QualType Ty) { return Ty.withConst(); }
  } getConstTy;
  class GetMaxValue {
    BasicValueFactory &BVF;

  public:
    GetMaxValue(BasicValueFactory &BVF) : BVF(BVF) {}
    std::optional<RangeInt> operator()(QualType Ty) {
      return BVF.getMaxValue(Ty).getLimitedValue();
    }
    std::optional<RangeInt> operator()(std::optional<QualType> Ty) {
      if (Ty) {
        return operator()(*Ty);
      }
      return std::nullopt;
    }
  } getMaxValue(BVF);

  // These types are useful for writing specifications quickly,
  // New specifications should probably introduce more types.
  // Some types are hard to obtain from the AST, eg. "ssize_t".
  // In such cases it should be possible to provide multiple variants
  // of function summary for common cases (eg. ssize_t could be int or long
  // or long long, so three summary variants would be enough).
  // Of course, function variants are also useful for C++ overloads.
  const QualType VoidTy = ACtx.VoidTy;
  const QualType CharTy = ACtx.CharTy;
  const QualType WCharTy = ACtx.WCharTy;
  const QualType IntTy = ACtx.IntTy;
  const QualType UnsignedIntTy = ACtx.UnsignedIntTy;
  const QualType LongTy = ACtx.LongTy;
  const QualType SizeTy = ACtx.getSizeType();

  const QualType VoidPtrTy = getPointerTy(VoidTy); // void *
  const QualType IntPtrTy = getPointerTy(IntTy);   // int *
  const QualType UnsignedIntPtrTy =
      getPointerTy(UnsignedIntTy); // unsigned int *
  const QualType VoidPtrRestrictTy = getRestrictTy(VoidPtrTy);
  const QualType ConstVoidPtrTy =
      getPointerTy(getConstTy(VoidTy));            // const void *
  const QualType CharPtrTy = getPointerTy(CharTy); // char *
  const QualType CharPtrRestrictTy = getRestrictTy(CharPtrTy);
  const QualType ConstCharPtrTy =
      getPointerTy(getConstTy(CharTy)); // const char *
  const QualType ConstCharPtrRestrictTy = getRestrictTy(ConstCharPtrTy);
  const QualType Wchar_tPtrTy = getPointerTy(WCharTy); // wchar_t *
  const QualType ConstWchar_tPtrTy =
      getPointerTy(getConstTy(WCharTy)); // const wchar_t *
  const QualType ConstVoidPtrRestrictTy = getRestrictTy(ConstVoidPtrTy);
  const QualType SizePtrTy = getPointerTy(SizeTy);
  const QualType SizePtrRestrictTy = getRestrictTy(SizePtrTy);

  const RangeInt IntMax = BVF.getMaxValue(IntTy).getLimitedValue();
  const RangeInt UnsignedIntMax =
      BVF.getMaxValue(UnsignedIntTy).getLimitedValue();
  const RangeInt LongMax = BVF.getMaxValue(LongTy).getLimitedValue();
  const RangeInt SizeMax = BVF.getMaxValue(SizeTy).getLimitedValue();

  // Set UCharRangeMax to min of int or uchar maximum value.
  // The C standard states that the arguments of functions like isalpha must
  // be representable as an unsigned char. Their type is 'int', so the max
  // value of the argument should be min(UCharMax, IntMax). This just happen
  // to be true for commonly used and well tested instruction set
  // architectures, but not for others.
  const RangeInt UCharRangeMax =
      std::min(BVF.getMaxValue(ACtx.UnsignedCharTy).getLimitedValue(), IntMax);

  // Get platform dependent values of some macros.
  // Try our best to parse this from the Preprocessor, otherwise fallback to a
  // default value (what is found in a library header).
  const auto EOFv = tryExpandAsInteger("EOF", PP).value_or(-1);
  const auto AT_FDCWDv = tryExpandAsInteger("AT_FDCWD", PP).value_or(-100);

  // Auxiliary class to aid adding summaries to the summary map.
  struct AddToFunctionSummaryMap {
    const ASTContext &ACtx;
    FunctionSummaryMapType &Map;
    bool DisplayLoadedSummaries;
    AddToFunctionSummaryMap(const ASTContext &ACtx, FunctionSummaryMapType &FSM,
                            bool DisplayLoadedSummaries)
        : ACtx(ACtx), Map(FSM), DisplayLoadedSummaries(DisplayLoadedSummaries) {
    }

    // Add a summary to a FunctionDecl found by lookup. The lookup is performed
    // by the given Name, and in the global scope. The summary will be attached
    // to the found FunctionDecl only if the signatures match.
    //
    // Returns true if the summary has been added, false otherwise.
    bool operator()(StringRef Name, Signature Sign, Summary Sum) {
      if (Sign.isInvalid())
        return false;
      IdentifierInfo &II = ACtx.Idents.get(Name);
      auto LookupRes = ACtx.getTranslationUnitDecl()->lookup(&II);
      if (LookupRes.empty())
        return false;
      for (Decl *D : LookupRes) {
        if (auto *FD = dyn_cast<FunctionDecl>(D)) {
          if (Sum.matchesAndSet(Sign, FD)) {
            auto Res = Map.insert({FD->getCanonicalDecl(), Sum});
            assert(Res.second && "Function already has a summary set!");
            (void)Res;
            if (DisplayLoadedSummaries) {
              llvm::errs() << "Loaded summary for: ";
              FD->print(llvm::errs());
              llvm::errs() << "\n";
            }
            return true;
          }
        }
      }
      return false;
    }
    // Add the same summary for different names with the Signature explicitly
    // given.
    void operator()(ArrayRef<StringRef> Names, Signature Sign, Summary Sum) {
      for (StringRef Name : Names)
        operator()(Name, Sign, Sum);
    }
  } addToFunctionSummaryMap(ACtx, FunctionSummaryMap, DisplayLoadedSummaries);

  // Below are helpers functions to create the summaries.
  auto ArgumentCondition = [](ArgNo ArgN, RangeKind Kind, IntRangeVector Ranges,
                              StringRef Desc = "") {
    return std::make_shared<RangeConstraint>(ArgN, Kind, Ranges, Desc);
  };
  auto BufferSize = [](auto... Args) {
    return std::make_shared<BufferSizeConstraint>(Args...);
  };
  struct {
    auto operator()(RangeKind Kind, IntRangeVector Ranges) {
      return std::make_shared<RangeConstraint>(Ret, Kind, Ranges);
    }
    auto operator()(BinaryOperator::Opcode Op, ArgNo OtherArgN) {
      return std::make_shared<ComparisonConstraint>(Ret, Op, OtherArgN);
    }
  } ReturnValueCondition;
  struct {
    auto operator()(RangeInt b, RangeInt e) {
      return IntRangeVector{std::pair<RangeInt, RangeInt>{b, e}};
    }
    auto operator()(RangeInt b, std::optional<RangeInt> e) {
      if (e)
        return IntRangeVector{std::pair<RangeInt, RangeInt>{b, *e}};
      return IntRangeVector{};
    }
    auto operator()(std::pair<RangeInt, RangeInt> i0,
                    std::pair<RangeInt, std::optional<RangeInt>> i1) {
      if (i1.second)
        return IntRangeVector{i0, {i1.first, *(i1.second)}};
      return IntRangeVector{i0};
    }
  } Range;
  auto SingleValue = [](RangeInt v) {
    return IntRangeVector{std::pair<RangeInt, RangeInt>{v, v}};
  };
  auto LessThanOrEq = BO_LE;
  auto NotNull = [&](ArgNo ArgN) {
    return std::make_shared<NotNullConstraint>(ArgN);
  };
  auto IsNull = [&](ArgNo ArgN) {
    return std::make_shared<NotNullConstraint>(ArgN, false);
  };
  auto NotNullBuffer = [&](ArgNo ArgN, ArgNo SizeArg1N, ArgNo SizeArg2N) {
    return std::make_shared<NotNullBufferConstraint>(ArgN, SizeArg1N,
                                                     SizeArg2N);
  };

  std::optional<QualType> FileTy = lookupTy("FILE");
  std::optional<QualType> FilePtrTy = getPointerTy(FileTy);
  std::optional<QualType> FilePtrRestrictTy = getRestrictTy(FilePtrTy);

  std::optional<QualType> FPosTTy = lookupTy("fpos_t");
  std::optional<QualType> FPosTPtrTy = getPointerTy(FPosTTy);
  std::optional<QualType> ConstFPosTPtrTy = getPointerTy(getConstTy(FPosTTy));
  std::optional<QualType> FPosTPtrRestrictTy = getRestrictTy(FPosTPtrTy);

  constexpr llvm::StringLiteral GenericSuccessMsg(
      "Assuming that '{0}' is successful");
  constexpr llvm::StringLiteral GenericFailureMsg("Assuming that '{0}' fails");

  // We are finally ready to define specifications for all supported functions.
  //
  // Argument ranges should always cover all variants. If return value
  // is completely unknown, omit it from the respective range set.
  //
  // Every item in the list of range sets represents a particular
  // execution path the analyzer would need to explore once
  // the call is modeled - a new program state is constructed
  // for every range set, and each range line in the range set
  // corresponds to a specific constraint within this state.

  // The isascii() family of functions.
  // The behavior is undefined if the value of the argument is not
  // representable as unsigned char or is not equal to EOF. See e.g. C99
  // 7.4.1.2 The isalpha function (p: 181-182).
  addToFunctionSummaryMap(
      "isalnum", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          // Boils down to isupper() or islower() or isdigit().
          .Case({ArgumentCondition(0U, WithinRange,
                                   {{'0', '9'}, {'A', 'Z'}, {'a', 'z'}}),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant, "Assuming the character is alphanumeric")
          // The locale-specific range.
          // No post-condition. We are completely unaware of
          // locale-specific return values.
          .Case({ArgumentCondition(0U, WithinRange, {{128, UCharRangeMax}})},
                ErrnoIrrelevant)
          .Case(
              {ArgumentCondition(
                   0U, OutOfRange,
                   {{'0', '9'}, {'A', 'Z'}, {'a', 'z'}, {128, UCharRangeMax}}),
               ReturnValueCondition(WithinRange, SingleValue(0))},
              ErrnoIrrelevant, "Assuming the character is non-alphanumeric")
          .ArgConstraint(ArgumentCondition(0U, WithinRange,
                                           {{EOFv, EOFv}, {0, UCharRangeMax}},
                                           "an unsigned char value or EOF")));
  addToFunctionSummaryMap(
      "isalpha", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          .Case({ArgumentCondition(0U, WithinRange, {{'A', 'Z'}, {'a', 'z'}}),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant, "Assuming the character is alphabetical")
          // The locale-specific range.
          .Case({ArgumentCondition(0U, WithinRange, {{128, UCharRangeMax}})},
                ErrnoIrrelevant)
          .Case({ArgumentCondition(
                     0U, OutOfRange,
                     {{'A', 'Z'}, {'a', 'z'}, {128, UCharRangeMax}}),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoIrrelevant, "Assuming the character is non-alphabetical"));
  addToFunctionSummaryMap(
      "isascii", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          .Case({ArgumentCondition(0U, WithinRange, Range(0, 127)),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant, "Assuming the character is an ASCII character")
          .Case({ArgumentCondition(0U, OutOfRange, Range(0, 127)),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character is not an ASCII character"));
  addToFunctionSummaryMap(
      "isblank", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          .Case({ArgumentCondition(0U, WithinRange, {{'\t', '\t'}, {' ', ' '}}),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant, "Assuming the character is a blank character")
          .Case({ArgumentCondition(0U, OutOfRange, {{'\t', '\t'}, {' ', ' '}}),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character is not a blank character"));
  addToFunctionSummaryMap(
      "iscntrl", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          .Case({ArgumentCondition(0U, WithinRange, {{0, 32}, {127, 127}}),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character is a control character")
          .Case({ArgumentCondition(0U, OutOfRange, {{0, 32}, {127, 127}}),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character is not a control character"));
  addToFunctionSummaryMap(
      "isdigit", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          .Case({ArgumentCondition(0U, WithinRange, Range('0', '9')),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant, "Assuming the character is a digit")
          .Case({ArgumentCondition(0U, OutOfRange, Range('0', '9')),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoIrrelevant, "Assuming the character is not a digit"));
  addToFunctionSummaryMap(
      "isgraph", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          .Case({ArgumentCondition(0U, WithinRange, Range(33, 126)),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character has graphical representation")
          .Case(
              {ArgumentCondition(0U, OutOfRange, Range(33, 126)),
               ReturnValueCondition(WithinRange, SingleValue(0))},
              ErrnoIrrelevant,
              "Assuming the character does not have graphical representation"));
  addToFunctionSummaryMap(
      "islower", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          // Is certainly lowercase.
          .Case({ArgumentCondition(0U, WithinRange, Range('a', 'z')),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant, "Assuming the character is a lowercase letter")
          // Is ascii but not lowercase.
          .Case({ArgumentCondition(0U, WithinRange, Range(0, 127)),
                 ArgumentCondition(0U, OutOfRange, Range('a', 'z')),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character is not a lowercase letter")
          // The locale-specific range.
          .Case({ArgumentCondition(0U, WithinRange, {{128, UCharRangeMax}})},
                ErrnoIrrelevant)
          // Is not an unsigned char.
          .Case({ArgumentCondition(0U, OutOfRange, Range(0, UCharRangeMax)),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoIrrelevant));
  addToFunctionSummaryMap(
      "isprint", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          .Case({ArgumentCondition(0U, WithinRange, Range(32, 126)),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant, "Assuming the character is printable")
          .Case({ArgumentCondition(0U, OutOfRange, Range(32, 126)),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoIrrelevant, "Assuming the character is non-printable"));
  addToFunctionSummaryMap(
      "ispunct", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          .Case({ArgumentCondition(
                     0U, WithinRange,
                     {{'!', '/'}, {':', '@'}, {'[', '`'}, {'{', '~'}}),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant, "Assuming the character is a punctuation mark")
          .Case({ArgumentCondition(
                     0U, OutOfRange,
                     {{'!', '/'}, {':', '@'}, {'[', '`'}, {'{', '~'}}),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character is not a punctuation mark"));
  addToFunctionSummaryMap(
      "isspace", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          // Space, '\f', '\n', '\r', '\t', '\v'.
          .Case({ArgumentCondition(0U, WithinRange, {{9, 13}, {' ', ' '}}),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character is a whitespace character")
          // The locale-specific range.
          .Case({ArgumentCondition(0U, WithinRange, {{128, UCharRangeMax}})},
                ErrnoIrrelevant)
          .Case({ArgumentCondition(0U, OutOfRange,
                                   {{9, 13}, {' ', ' '}, {128, UCharRangeMax}}),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character is not a whitespace character"));
  addToFunctionSummaryMap(
      "isupper", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          // Is certainly uppercase.
          .Case({ArgumentCondition(0U, WithinRange, Range('A', 'Z')),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character is an uppercase letter")
          // The locale-specific range.
          .Case({ArgumentCondition(0U, WithinRange, {{128, UCharRangeMax}})},
                ErrnoIrrelevant)
          // Other.
          .Case({ArgumentCondition(0U, OutOfRange,
                                   {{'A', 'Z'}, {128, UCharRangeMax}}),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character is not an uppercase letter"));
  addToFunctionSummaryMap(
      "isxdigit", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          .Case({ArgumentCondition(0U, WithinRange,
                                   {{'0', '9'}, {'A', 'F'}, {'a', 'f'}}),
                 ReturnValueCondition(OutOfRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character is a hexadecimal digit")
          .Case({ArgumentCondition(0U, OutOfRange,
                                   {{'0', '9'}, {'A', 'F'}, {'a', 'f'}}),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoIrrelevant,
                "Assuming the character is not a hexadecimal digit"));
  addToFunctionSummaryMap(
      "toupper", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          .ArgConstraint(ArgumentCondition(0U, WithinRange,
                                           {{EOFv, EOFv}, {0, UCharRangeMax}},
                                           "an unsigned char value or EOF")));
  addToFunctionSummaryMap(
      "tolower", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          .ArgConstraint(ArgumentCondition(0U, WithinRange,
                                           {{EOFv, EOFv}, {0, UCharRangeMax}},
                                           "an unsigned char value or EOF")));
  addToFunctionSummaryMap(
      "toascii", Signature(ArgTypes{IntTy}, RetType{IntTy}),
      Summary(EvalCallAsPure)
          .ArgConstraint(ArgumentCondition(0U, WithinRange,
                                           {{EOFv, EOFv}, {0, UCharRangeMax}},
                                           "an unsigned char value or EOF")));

  addToFunctionSummaryMap(
      "getchar", Signature(ArgTypes{}, RetType{IntTy}),
      Summary(NoEvalCall)
          .Case({ReturnValueCondition(WithinRange,
                                      {{EOFv, EOFv}, {0, UCharRangeMax}})},
                ErrnoIrrelevant));

  // read()-like functions that never return more than buffer size.
  auto FreadSummary =
      Summary(NoEvalCall)
          .Case({ArgumentCondition(1U, WithinRange, Range(1, SizeMax)),
                 ArgumentCondition(2U, WithinRange, Range(1, SizeMax)),
                 ReturnValueCondition(BO_LT, ArgNo(2)),
                 ReturnValueCondition(WithinRange, Range(0, SizeMax))},
                ErrnoNEZeroIrrelevant, GenericFailureMsg)
          .Case({ArgumentCondition(1U, WithinRange, Range(1, SizeMax)),
                 ReturnValueCondition(BO_EQ, ArgNo(2)),
                 ReturnValueCondition(WithinRange, Range(0, SizeMax))},
                ErrnoMustNotBeChecked, GenericSuccessMsg)
          .Case({ArgumentCondition(1U, WithinRange, SingleValue(0)),
                 ReturnValueCondition(WithinRange, SingleValue(0))},
                ErrnoMustNotBeChecked,
                "Assuming that argument 'size' to '{0}' is 0")
          .ArgConstraint(NotNullBuffer(ArgNo(0), ArgNo(1), ArgNo(2)))
          .ArgConstraint(NotNull(ArgNo(3)))
          .ArgConstraint(BufferSize(/*Buffer=*/ArgNo(0), /*BufSize=*/ArgNo(1),
                                    /*BufSizeMultiplier=*/ArgNo(2)));

  // size_t fread(void *restrict ptr, size_t size, size_t nitems,
  //              FILE *restrict stream);
  addToFunctionSummaryMap(
      "fread",
      Signature(ArgTypes{VoidPtrRestrictTy, SizeTy, SizeTy, FilePtrRestrictTy},
                RetType{SizeTy}),
      FreadSummary);
  // size_t fwrite(const void *restrict ptr, size_t size, size_t nitems,
  //               FILE *restrict stream);
  addToFunctionSummaryMap("fwrite",
                          Signature(ArgTypes{ConstVoidPtrRestrictTy, SizeTy,
                                             SizeTy, FilePtrRestrictTy},
                                    RetType{SizeTy}),
                          FreadSummary);

  std::optional<QualType> Ssize_tTy = lookupTy("ssize_t");
  std::optional<RangeInt> Ssize_tMax = getMaxValue(Ssize_tTy);

  auto ReadSummary =
      Summary(NoEvalCall)
          .Case({ReturnValueCondition(LessThanOrEq, ArgNo(2)),
                 ReturnValueCondition(WithinRange, Range(-1, Ssize_tMax))},
                ErrnoIrrelevant);

  // FIXME these are actually defined by POSIX and not by the C standard, we
  // should handle them together with the rest of the POSIX functions.
  // ssize_t read(int fildes, void *buf, size_t nbyte);
  addToFunctionSummaryMap(
      "read", Signature(ArgTypes{IntTy, VoidPtrTy, SizeTy}, RetType{Ssize_tTy}),
      ReadSummary);
  // ssize_t write(int fildes, const void *buf, size_t nbyte);
  addToFunctionSummaryMap(
      "write",
      Signature(ArgTypes{IntTy, ConstVoidPtrTy, SizeTy}, RetType{Ssize_tTy}),
      ReadSummary);

  auto GetLineSummary =
      Summary(NoEvalCall)
          .Case({ReturnValueCondition(WithinRange,
                                      Range({-1, -1}, {1, Ssize_tMax}))},
                ErrnoIrrelevant);

  QualType CharPtrPtrRestrictTy = getRestrictTy(getPointerTy(CharPtrTy));

  // getline()-like functions either fail or read at least the delimiter.
  // FIXME these are actually defined by POSIX and not by the C standard, we
  // should handle them together with the rest of the POSIX functions.
  // ssize_t getline(char **restrict lineptr, size_t *restrict n,
  //                 FILE *restrict stream);
  addToFunctionSummaryMap(
      "getline",
      Signature(
          ArgTypes{CharPtrPtrRestrictTy, SizePtrRestrictTy, FilePtrRestrictTy},
          RetType{Ssize_tTy}),
      GetLineSummary);
  // ssize_t getdelim(char **restrict lineptr, size_t *restrict n,
  //                  int delimiter, FILE *restrict stream);
  addToFunctionSummaryMap(
      "getdelim",
      Signature(ArgTypes{CharPtrPtrRestrictTy, SizePtrRestrictTy, IntTy,
                         FilePtrRestrictTy},
                RetType{Ssize_tTy}),
      GetLineSummary);

  {
    Summary GetenvSummary =
        Summary(NoEvalCall)
            .ArgConstraint(NotNull(ArgNo(0)))
            .Case({NotNull(Ret)}, ErrnoIrrelevant,
                  "Assuming the environment variable exists");
    // In untrusted environments the envvar might not exist.
    if (!ShouldAssumeControlledEnvironment)
      GetenvSummary.Case({NotNull(Ret)->negate()}, ErrnoIrrelevant,
                         "Assuming the environment variable does not exist");

    // char *getenv(const char *name);
    addToFunctionSummaryMap(
        "getenv", Signature(ArgTypes{ConstCharPtrTy}, RetType{CharPtrTy}),
        std::move(GetenvSummary));
  }

  if (!ModelPOSIX) {
    // Without POSIX use of 'errno' is not specified (in these cases).
    // Add these functions without 'errno' checks.
    addToFunctionSummaryMap(
        {"getc", "fgetc"}, Signature(ArgTypes{FilePtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(WithinRange,
                                        {{EOFv, EOFv}, {0, UCharRangeMax}})},
                  ErrnoIrrelevant)
            .ArgConstraint(NotNull(ArgNo(0))));
  } else {
    const auto ReturnsZeroOrMinusOne =
        ConstraintSet{ReturnValueCondition(WithinRange, Range(-1, 0))};
    const auto ReturnsZero =
        ConstraintSet{ReturnValueCondition(WithinRange, SingleValue(0))};
    const auto ReturnsMinusOne =
        ConstraintSet{ReturnValueCondition(WithinRange, SingleValue(-1))};
    const auto ReturnsEOF =
        ConstraintSet{ReturnValueCondition(WithinRange, SingleValue(EOFv))};
    const auto ReturnsNonnegative =
        ConstraintSet{ReturnValueCondition(WithinRange, Range(0, IntMax))};
    const auto ReturnsNonZero =
        ConstraintSet{ReturnValueCondition(OutOfRange, SingleValue(0))};
    const auto ReturnsFileDescriptor =
        ConstraintSet{ReturnValueCondition(WithinRange, Range(-1, IntMax))};
    const auto &ReturnsValidFileDescriptor = ReturnsNonnegative;

    auto ValidFileDescriptorOrAtFdcwd = [&](ArgNo ArgN) {
      return std::make_shared<RangeConstraint>(
          ArgN, WithinRange, Range({AT_FDCWDv, AT_FDCWDv}, {0, IntMax}),
          "a valid file descriptor or AT_FDCWD");
    };

    // FILE *fopen(const char *restrict pathname, const char *restrict mode);
    addToFunctionSummaryMap(
        "fopen",
        Signature(ArgTypes{ConstCharPtrRestrictTy, ConstCharPtrRestrictTy},
                  RetType{FilePtrTy}),
        Summary(NoEvalCall)
            .Case({NotNull(Ret)}, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({IsNull(Ret)}, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // FILE *fdopen(int fd, const char *mode);
    addToFunctionSummaryMap(
        "fdopen",
        Signature(ArgTypes{IntTy, ConstCharPtrTy}, RetType{FilePtrTy}),
        Summary(NoEvalCall)
            .Case({NotNull(Ret)}, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({IsNull(Ret)}, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ArgumentCondition(0, WithinRange, Range(0, IntMax)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // FILE *tmpfile(void);
    addToFunctionSummaryMap(
        "tmpfile", Signature(ArgTypes{}, RetType{FilePtrTy}),
        Summary(NoEvalCall)
            .Case({NotNull(Ret)}, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({IsNull(Ret)}, ErrnoNEZeroIrrelevant, GenericFailureMsg));

    // FILE *freopen(const char *restrict pathname, const char *restrict mode,
    //               FILE *restrict stream);
    addToFunctionSummaryMap(
        "freopen",
        Signature(ArgTypes{ConstCharPtrRestrictTy, ConstCharPtrRestrictTy,
                           FilePtrRestrictTy},
                  RetType{FilePtrTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(BO_EQ, ArgNo(2))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({IsNull(Ret)}, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(1)))
            .ArgConstraint(NotNull(ArgNo(2))));

    // FILE *popen(const char *command, const char *type);
    addToFunctionSummaryMap(
        "popen",
        Signature(ArgTypes{ConstCharPtrTy, ConstCharPtrTy}, RetType{FilePtrTy}),
        Summary(NoEvalCall)
            .Case({NotNull(Ret)}, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({IsNull(Ret)}, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int fclose(FILE *stream);
    addToFunctionSummaryMap(
        "fclose", Signature(ArgTypes{FilePtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsEOF, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int pclose(FILE *stream);
    addToFunctionSummaryMap(
        "pclose", Signature(ArgTypes{FilePtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(WithinRange, {{0, IntMax}})},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    std::optional<QualType> Off_tTy = lookupTy("off_t");
    std::optional<RangeInt> Off_tMax = getMaxValue(Off_tTy);

    // int fgetc(FILE *stream);
    // 'getc' is the same as 'fgetc' but may be a macro
    addToFunctionSummaryMap(
        {"getc", "fgetc"}, Signature(ArgTypes{FilePtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(WithinRange, {{0, UCharRangeMax}})},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({ReturnValueCondition(WithinRange, SingleValue(EOFv))},
                  ErrnoIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int fputc(int c, FILE *stream);
    // 'putc' is the same as 'fputc' but may be a macro
    addToFunctionSummaryMap(
        {"putc", "fputc"},
        Signature(ArgTypes{IntTy, FilePtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case({ArgumentCondition(0, WithinRange, Range(0, UCharRangeMax)),
                   ReturnValueCondition(BO_EQ, ArgNo(0))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({ArgumentCondition(0, OutOfRange, Range(0, UCharRangeMax)),
                   ReturnValueCondition(WithinRange, Range(0, UCharRangeMax))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({ReturnValueCondition(WithinRange, SingleValue(EOFv))},
                  ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(1))));

    // char *fgets(char *restrict s, int n, FILE *restrict stream);
    addToFunctionSummaryMap(
        "fgets",
        Signature(ArgTypes{CharPtrRestrictTy, IntTy, FilePtrRestrictTy},
                  RetType{CharPtrTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(BO_EQ, ArgNo(0))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({IsNull(Ret)}, ErrnoIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(ArgumentCondition(1, WithinRange, Range(0, IntMax)))
            .ArgConstraint(
                BufferSize(/*Buffer=*/ArgNo(0), /*BufSize=*/ArgNo(1)))
            .ArgConstraint(NotNull(ArgNo(2))));

    // int fputs(const char *restrict s, FILE *restrict stream);
    addToFunctionSummaryMap(
        "fputs",
        Signature(ArgTypes{ConstCharPtrRestrictTy, FilePtrRestrictTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsNonnegative, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({ReturnValueCondition(WithinRange, SingleValue(EOFv))},
                  ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int ungetc(int c, FILE *stream);
    addToFunctionSummaryMap(
        "ungetc", Signature(ArgTypes{IntTy, FilePtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(BO_EQ, ArgNo(0)),
                   ArgumentCondition(0, WithinRange, {{0, UCharRangeMax}})},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({ReturnValueCondition(WithinRange, SingleValue(EOFv)),
                   ArgumentCondition(0, WithinRange, SingleValue(EOFv))},
                  ErrnoNEZeroIrrelevant,
                  "Assuming that 'ungetc' fails because EOF was passed as "
                  "character")
            .Case({ReturnValueCondition(WithinRange, SingleValue(EOFv)),
                   ArgumentCondition(0, WithinRange, {{0, UCharRangeMax}})},
                  ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ArgumentCondition(
                0, WithinRange, {{EOFv, EOFv}, {0, UCharRangeMax}}))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int fseek(FILE *stream, long offset, int whence);
    // FIXME: It can be possible to get the 'SEEK_' values (like EOFv) and use
    // these for condition of arg 2.
    // Now the range [0,2] is used (the `SEEK_*` constants are usually 0,1,2).
    addToFunctionSummaryMap(
        "fseek", Signature(ArgTypes{FilePtrTy, LongTy, IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(ArgumentCondition(2, WithinRange, {{0, 2}})));

    // int fseeko(FILE *stream, off_t offset, int whence);
    addToFunctionSummaryMap(
        "fseeko",
        Signature(ArgTypes{FilePtrTy, Off_tTy, IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(ArgumentCondition(2, WithinRange, {{0, 2}})));

    // int fgetpos(FILE *restrict stream, fpos_t *restrict pos);
    // From 'The Open Group Base Specifications Issue 7, 2018 edition':
    // "The fgetpos() function shall not change the setting of errno if
    // successful."
    addToFunctionSummaryMap(
        "fgetpos",
        Signature(ArgTypes{FilePtrRestrictTy, FPosTPtrRestrictTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoUnchanged, GenericSuccessMsg)
            .Case(ReturnsNonZero, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int fsetpos(FILE *stream, const fpos_t *pos);
    // From 'The Open Group Base Specifications Issue 7, 2018 edition':
    // "The fsetpos() function shall not change the setting of errno if
    // successful."
    addToFunctionSummaryMap(
        "fsetpos",
        Signature(ArgTypes{FilePtrTy, ConstFPosTPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoUnchanged, GenericSuccessMsg)
            .Case(ReturnsNonZero, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int fflush(FILE *stream);
    addToFunctionSummaryMap(
        "fflush", Signature(ArgTypes{FilePtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsEOF, ErrnoNEZeroIrrelevant, GenericFailureMsg));

    // long ftell(FILE *stream);
    // From 'The Open Group Base Specifications Issue 7, 2018 edition':
    // "The ftell() function shall not change the setting of errno if
    // successful."
    addToFunctionSummaryMap(
        "ftell", Signature(ArgTypes{FilePtrTy}, RetType{LongTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(WithinRange, Range(0, LongMax))},
                  ErrnoUnchanged, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // off_t ftello(FILE *stream);
    addToFunctionSummaryMap(
        "ftello", Signature(ArgTypes{FilePtrTy}, RetType{Off_tTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(WithinRange, Range(0, Off_tMax))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int fileno(FILE *stream);
    // According to POSIX 'fileno' may fail and set 'errno'.
    // But in Linux it may fail only if the specified file pointer is invalid.
    // At many places 'fileno' is used without check for failure and a failure
    // case here would produce a large amount of likely false positive warnings.
    // To avoid this, we assume here that it does not fail.
    addToFunctionSummaryMap(
        "fileno", Signature(ArgTypes{FilePtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsValidFileDescriptor, ErrnoUnchanged, GenericSuccessMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // void rewind(FILE *stream);
    // This function indicates error only by setting of 'errno'.
    addToFunctionSummaryMap("rewind",
                            Signature(ArgTypes{FilePtrTy}, RetType{VoidTy}),
                            Summary(NoEvalCall)
                                .Case({}, ErrnoMustBeChecked)
                                .ArgConstraint(NotNull(ArgNo(0))));

    // void clearerr(FILE *stream);
    addToFunctionSummaryMap(
        "clearerr", Signature(ArgTypes{FilePtrTy}, RetType{VoidTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // int feof(FILE *stream);
    addToFunctionSummaryMap(
        "feof", Signature(ArgTypes{FilePtrTy}, RetType{IntTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // int ferror(FILE *stream);
    addToFunctionSummaryMap(
        "ferror", Signature(ArgTypes{FilePtrTy}, RetType{IntTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // long a64l(const char *str64);
    addToFunctionSummaryMap(
        "a64l", Signature(ArgTypes{ConstCharPtrTy}, RetType{LongTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // char *l64a(long value);
    addToFunctionSummaryMap("l64a",
                            Signature(ArgTypes{LongTy}, RetType{CharPtrTy}),
                            Summary(NoEvalCall)
                                .ArgConstraint(ArgumentCondition(
                                    0, WithinRange, Range(0, LongMax))));

    // int open(const char *path, int oflag, ...);
    addToFunctionSummaryMap(
        "open", Signature(ArgTypes{ConstCharPtrTy, IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsValidFileDescriptor, ErrnoMustNotBeChecked,
                  GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int openat(int fd, const char *path, int oflag, ...);
    addToFunctionSummaryMap(
        "openat",
        Signature(ArgTypes{IntTy, ConstCharPtrTy, IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsValidFileDescriptor, ErrnoMustNotBeChecked,
                  GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int access(const char *pathname, int amode);
    addToFunctionSummaryMap(
        "access", Signature(ArgTypes{ConstCharPtrTy, IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int faccessat(int dirfd, const char *pathname, int mode, int flags);
    addToFunctionSummaryMap(
        "faccessat",
        Signature(ArgTypes{IntTy, ConstCharPtrTy, IntTy, IntTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int dup(int fildes);
    addToFunctionSummaryMap(
        "dup", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsValidFileDescriptor, ErrnoMustNotBeChecked,
                  GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // int dup2(int fildes1, int filedes2);
    addToFunctionSummaryMap(
        "dup2", Signature(ArgTypes{IntTy, IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsValidFileDescriptor, ErrnoMustNotBeChecked,
                  GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ArgumentCondition(0, WithinRange, Range(0, IntMax)))
            .ArgConstraint(
                ArgumentCondition(1, WithinRange, Range(0, IntMax))));

    // int fdatasync(int fildes);
    addToFunctionSummaryMap(
        "fdatasync", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // int fnmatch(const char *pattern, const char *string, int flags);
    addToFunctionSummaryMap(
        "fnmatch",
        Signature(ArgTypes{ConstCharPtrTy, ConstCharPtrTy, IntTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int fsync(int fildes);
    addToFunctionSummaryMap(
        "fsync", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // int truncate(const char *path, off_t length);
    addToFunctionSummaryMap(
        "truncate",
        Signature(ArgTypes{ConstCharPtrTy, Off_tTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int symlink(const char *oldpath, const char *newpath);
    addToFunctionSummaryMap(
        "symlink",
        Signature(ArgTypes{ConstCharPtrTy, ConstCharPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int symlinkat(const char *oldpath, int newdirfd, const char *newpath);
    addToFunctionSummaryMap(
        "symlinkat",
        Signature(ArgTypes{ConstCharPtrTy, IntTy, ConstCharPtrTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(1)))
            .ArgConstraint(NotNull(ArgNo(2))));

    // int lockf(int fd, int cmd, off_t len);
    addToFunctionSummaryMap(
        "lockf", Signature(ArgTypes{IntTy, IntTy, Off_tTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    std::optional<QualType> Mode_tTy = lookupTy("mode_t");

    // int creat(const char *pathname, mode_t mode);
    addToFunctionSummaryMap(
        "creat", Signature(ArgTypes{ConstCharPtrTy, Mode_tTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsValidFileDescriptor, ErrnoMustNotBeChecked,
                  GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // unsigned int sleep(unsigned int seconds);
    addToFunctionSummaryMap(
        "sleep", Signature(ArgTypes{UnsignedIntTy}, RetType{UnsignedIntTy}),
        Summary(NoEvalCall)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, UnsignedIntMax))));

    std::optional<QualType> DirTy = lookupTy("DIR");
    std::optional<QualType> DirPtrTy = getPointerTy(DirTy);

    // int dirfd(DIR *dirp);
    addToFunctionSummaryMap(
        "dirfd", Signature(ArgTypes{DirPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsValidFileDescriptor, ErrnoMustNotBeChecked,
                  GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // unsigned int alarm(unsigned int seconds);
    addToFunctionSummaryMap(
        "alarm", Signature(ArgTypes{UnsignedIntTy}, RetType{UnsignedIntTy}),
        Summary(NoEvalCall)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, UnsignedIntMax))));

    // int closedir(DIR *dir);
    addToFunctionSummaryMap(
        "closedir", Signature(ArgTypes{DirPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // char *strdup(const char *s);
    addToFunctionSummaryMap(
        "strdup", Signature(ArgTypes{ConstCharPtrTy}, RetType{CharPtrTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // char *strndup(const char *s, size_t n);
    addToFunctionSummaryMap(
        "strndup",
        Signature(ArgTypes{ConstCharPtrTy, SizeTy}, RetType{CharPtrTy}),
        Summary(NoEvalCall)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(
                ArgumentCondition(1, WithinRange, Range(0, SizeMax))));

    // wchar_t *wcsdup(const wchar_t *s);
    addToFunctionSummaryMap(
        "wcsdup", Signature(ArgTypes{ConstWchar_tPtrTy}, RetType{Wchar_tPtrTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // int mkstemp(char *template);
    addToFunctionSummaryMap(
        "mkstemp", Signature(ArgTypes{CharPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsValidFileDescriptor, ErrnoMustNotBeChecked,
                  GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // char *mkdtemp(char *template);
    addToFunctionSummaryMap(
        "mkdtemp", Signature(ArgTypes{CharPtrTy}, RetType{CharPtrTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(BO_EQ, ArgNo(0))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({IsNull(Ret)}, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // char *getcwd(char *buf, size_t size);
    addToFunctionSummaryMap(
        "getcwd", Signature(ArgTypes{CharPtrTy, SizeTy}, RetType{CharPtrTy}),
        Summary(NoEvalCall)
            .Case({ArgumentCondition(1, WithinRange, Range(1, SizeMax)),
                   ReturnValueCondition(BO_EQ, ArgNo(0))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({ArgumentCondition(1, WithinRange, SingleValue(0)),
                   IsNull(Ret)},
                  ErrnoNEZeroIrrelevant, "Assuming that argument 'size' is 0")
            .Case({ArgumentCondition(1, WithinRange, Range(1, SizeMax)),
                   IsNull(Ret)},
                  ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(
                BufferSize(/*Buffer*/ ArgNo(0), /*BufSize*/ ArgNo(1)))
            .ArgConstraint(
                ArgumentCondition(1, WithinRange, Range(0, SizeMax))));

    // int mkdir(const char *pathname, mode_t mode);
    addToFunctionSummaryMap(
        "mkdir", Signature(ArgTypes{ConstCharPtrTy, Mode_tTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int mkdirat(int dirfd, const char *pathname, mode_t mode);
    addToFunctionSummaryMap(
        "mkdirat",
        Signature(ArgTypes{IntTy, ConstCharPtrTy, Mode_tTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    std::optional<QualType> Dev_tTy = lookupTy("dev_t");

    // int mknod(const char *pathname, mode_t mode, dev_t dev);
    addToFunctionSummaryMap(
        "mknod",
        Signature(ArgTypes{ConstCharPtrTy, Mode_tTy, Dev_tTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int mknodat(int dirfd, const char *pathname, mode_t mode, dev_t dev);
    addToFunctionSummaryMap(
        "mknodat",
        Signature(ArgTypes{IntTy, ConstCharPtrTy, Mode_tTy, Dev_tTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int chmod(const char *path, mode_t mode);
    addToFunctionSummaryMap(
        "chmod", Signature(ArgTypes{ConstCharPtrTy, Mode_tTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int fchmodat(int dirfd, const char *pathname, mode_t mode, int flags);
    addToFunctionSummaryMap(
        "fchmodat",
        Signature(ArgTypes{IntTy, ConstCharPtrTy, Mode_tTy, IntTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int fchmod(int fildes, mode_t mode);
    addToFunctionSummaryMap(
        "fchmod", Signature(ArgTypes{IntTy, Mode_tTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    std::optional<QualType> Uid_tTy = lookupTy("uid_t");
    std::optional<QualType> Gid_tTy = lookupTy("gid_t");

    // int fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group,
    //              int flags);
    addToFunctionSummaryMap(
        "fchownat",
        Signature(ArgTypes{IntTy, ConstCharPtrTy, Uid_tTy, Gid_tTy, IntTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int chown(const char *path, uid_t owner, gid_t group);
    addToFunctionSummaryMap(
        "chown",
        Signature(ArgTypes{ConstCharPtrTy, Uid_tTy, Gid_tTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int lchown(const char *path, uid_t owner, gid_t group);
    addToFunctionSummaryMap(
        "lchown",
        Signature(ArgTypes{ConstCharPtrTy, Uid_tTy, Gid_tTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int fchown(int fildes, uid_t owner, gid_t group);
    addToFunctionSummaryMap(
        "fchown", Signature(ArgTypes{IntTy, Uid_tTy, Gid_tTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // int rmdir(const char *pathname);
    addToFunctionSummaryMap(
        "rmdir", Signature(ArgTypes{ConstCharPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int chdir(const char *path);
    addToFunctionSummaryMap(
        "chdir", Signature(ArgTypes{ConstCharPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int link(const char *oldpath, const char *newpath);
    addToFunctionSummaryMap(
        "link",
        Signature(ArgTypes{ConstCharPtrTy, ConstCharPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int linkat(int fd1, const char *path1, int fd2, const char *path2,
    //            int flag);
    addToFunctionSummaryMap(
        "linkat",
        Signature(ArgTypes{IntTy, ConstCharPtrTy, IntTy, ConstCharPtrTy, IntTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1)))
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(2)))
            .ArgConstraint(NotNull(ArgNo(3))));

    // int unlink(const char *pathname);
    addToFunctionSummaryMap(
        "unlink", Signature(ArgTypes{ConstCharPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int unlinkat(int fd, const char *path, int flag);
    addToFunctionSummaryMap(
        "unlinkat",
        Signature(ArgTypes{IntTy, ConstCharPtrTy, IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    std::optional<QualType> StructStatTy = lookupTy("stat");
    std::optional<QualType> StructStatPtrTy = getPointerTy(StructStatTy);
    std::optional<QualType> StructStatPtrRestrictTy =
        getRestrictTy(StructStatPtrTy);

    // int fstat(int fd, struct stat *statbuf);
    addToFunctionSummaryMap(
        "fstat", Signature(ArgTypes{IntTy, StructStatPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ArgumentCondition(0, WithinRange, Range(0, IntMax)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int stat(const char *restrict path, struct stat *restrict buf);
    addToFunctionSummaryMap(
        "stat",
        Signature(ArgTypes{ConstCharPtrRestrictTy, StructStatPtrRestrictTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int lstat(const char *restrict path, struct stat *restrict buf);
    addToFunctionSummaryMap(
        "lstat",
        Signature(ArgTypes{ConstCharPtrRestrictTy, StructStatPtrRestrictTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int fstatat(int fd, const char *restrict path,
    //             struct stat *restrict buf, int flag);
    addToFunctionSummaryMap(
        "fstatat",
        Signature(ArgTypes{IntTy, ConstCharPtrRestrictTy,
                           StructStatPtrRestrictTy, IntTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1)))
            .ArgConstraint(NotNull(ArgNo(2))));

    // DIR *opendir(const char *name);
    addToFunctionSummaryMap(
        "opendir", Signature(ArgTypes{ConstCharPtrTy}, RetType{DirPtrTy}),
        Summary(NoEvalCall)
            .Case({NotNull(Ret)}, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({IsNull(Ret)}, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // DIR *fdopendir(int fd);
    addToFunctionSummaryMap(
        "fdopendir", Signature(ArgTypes{IntTy}, RetType{DirPtrTy}),
        Summary(NoEvalCall)
            .Case({NotNull(Ret)}, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({IsNull(Ret)}, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // int isatty(int fildes);
    addToFunctionSummaryMap(
        "isatty", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(WithinRange, Range(0, 1))},
                  ErrnoIrrelevant)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // int close(int fildes);
    addToFunctionSummaryMap(
        "close", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(-1, IntMax))));

    // long fpathconf(int fildes, int name);
    addToFunctionSummaryMap("fpathconf",
                            Signature(ArgTypes{IntTy, IntTy}, RetType{LongTy}),
                            Summary(NoEvalCall)
                                .ArgConstraint(ArgumentCondition(
                                    0, WithinRange, Range(0, IntMax))));

    // long pathconf(const char *path, int name);
    addToFunctionSummaryMap(
        "pathconf", Signature(ArgTypes{ConstCharPtrTy, IntTy}, RetType{LongTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // void rewinddir(DIR *dir);
    addToFunctionSummaryMap(
        "rewinddir", Signature(ArgTypes{DirPtrTy}, RetType{VoidTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // void seekdir(DIR *dirp, long loc);
    addToFunctionSummaryMap(
        "seekdir", Signature(ArgTypes{DirPtrTy, LongTy}, RetType{VoidTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // int rand_r(unsigned int *seedp);
    addToFunctionSummaryMap(
        "rand_r", Signature(ArgTypes{UnsignedIntPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // void *mmap(void *addr, size_t length, int prot, int flags, int fd,
    // off_t offset);
    // FIXME: Improve for errno modeling.
    addToFunctionSummaryMap(
        "mmap",
        Signature(ArgTypes{VoidPtrTy, SizeTy, IntTy, IntTy, IntTy, Off_tTy},
                  RetType{VoidPtrTy}),
        Summary(NoEvalCall)
            .ArgConstraint(ArgumentCondition(1, WithinRange, Range(1, SizeMax)))
            .ArgConstraint(
                ArgumentCondition(4, WithinRange, Range(-1, IntMax))));

    std::optional<QualType> Off64_tTy = lookupTy("off64_t");
    // void *mmap64(void *addr, size_t length, int prot, int flags, int fd,
    // off64_t offset);
    // FIXME: Improve for errno modeling.
    addToFunctionSummaryMap(
        "mmap64",
        Signature(ArgTypes{VoidPtrTy, SizeTy, IntTy, IntTy, IntTy, Off64_tTy},
                  RetType{VoidPtrTy}),
        Summary(NoEvalCall)
            .ArgConstraint(ArgumentCondition(1, WithinRange, Range(1, SizeMax)))
            .ArgConstraint(
                ArgumentCondition(4, WithinRange, Range(-1, IntMax))));

    // int pipe(int fildes[2]);
    addToFunctionSummaryMap(
        "pipe", Signature(ArgTypes{IntPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // off_t lseek(int fildes, off_t offset, int whence);
    // In the first case we can not tell for sure if it failed or not.
    // A return value different from of the expected offset (that is unknown
    // here) may indicate failure. For this reason we do not enforce the errno
    // check (can cause false positive).
    addToFunctionSummaryMap(
        "lseek", Signature(ArgTypes{IntTy, Off_tTy, IntTy}, RetType{Off_tTy}),
        Summary(NoEvalCall)
            .Case(ReturnsNonnegative, ErrnoIrrelevant)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // ssize_t readlink(const char *restrict path, char *restrict buf,
    //                  size_t bufsize);
    addToFunctionSummaryMap(
        "readlink",
        Signature(ArgTypes{ConstCharPtrRestrictTy, CharPtrRestrictTy, SizeTy},
                  RetType{Ssize_tTy}),
        Summary(NoEvalCall)
            .Case({ArgumentCondition(2, WithinRange, Range(1, IntMax)),
                   ReturnValueCondition(LessThanOrEq, ArgNo(2)),
                   ReturnValueCondition(WithinRange, Range(1, Ssize_tMax))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({ArgumentCondition(2, WithinRange, SingleValue(0)),
                   ReturnValueCondition(WithinRange, SingleValue(0))},
                  ErrnoMustNotBeChecked,
                  "Assuming that argument 'bufsize' is 0")
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1)))
            .ArgConstraint(BufferSize(/*Buffer=*/ArgNo(1),
                                      /*BufSize=*/ArgNo(2)))
            .ArgConstraint(
                ArgumentCondition(2, WithinRange, Range(0, SizeMax))));

    // ssize_t readlinkat(int fd, const char *restrict path,
    //                    char *restrict buf, size_t bufsize);
    addToFunctionSummaryMap(
        "readlinkat",
        Signature(
            ArgTypes{IntTy, ConstCharPtrRestrictTy, CharPtrRestrictTy, SizeTy},
            RetType{Ssize_tTy}),
        Summary(NoEvalCall)
            .Case({ArgumentCondition(3, WithinRange, Range(1, IntMax)),
                   ReturnValueCondition(LessThanOrEq, ArgNo(3)),
                   ReturnValueCondition(WithinRange, Range(1, Ssize_tMax))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({ArgumentCondition(3, WithinRange, SingleValue(0)),
                   ReturnValueCondition(WithinRange, SingleValue(0))},
                  ErrnoMustNotBeChecked,
                  "Assuming that argument 'bufsize' is 0")
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1)))
            .ArgConstraint(NotNull(ArgNo(2)))
            .ArgConstraint(BufferSize(/*Buffer=*/ArgNo(2),
                                      /*BufSize=*/ArgNo(3)))
            .ArgConstraint(
                ArgumentCondition(3, WithinRange, Range(0, SizeMax))));

    // int renameat(int olddirfd, const char *oldpath, int newdirfd, const char
    // *newpath);
    addToFunctionSummaryMap(
        "renameat",
        Signature(ArgTypes{IntTy, ConstCharPtrTy, IntTy, ConstCharPtrTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1)))
            .ArgConstraint(ValidFileDescriptorOrAtFdcwd(ArgNo(2)))
            .ArgConstraint(NotNull(ArgNo(3))));

    // char *realpath(const char *restrict file_name,
    //                char *restrict resolved_name);
    // FIXME: If the argument 'resolved_name' is not NULL, macro 'PATH_MAX'
    //        should be defined in "limits.h" to guarrantee a success.
    addToFunctionSummaryMap(
        "realpath",
        Signature(ArgTypes{ConstCharPtrRestrictTy, CharPtrRestrictTy},
                  RetType{CharPtrTy}),
        Summary(NoEvalCall)
            .Case({NotNull(Ret)}, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({IsNull(Ret)}, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    QualType CharPtrConstPtr = getPointerTy(getConstTy(CharPtrTy));

    // int execv(const char *path, char *const argv[]);
    addToFunctionSummaryMap(
        "execv",
        Signature(ArgTypes{ConstCharPtrTy, CharPtrConstPtr}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int execvp(const char *file, char *const argv[]);
    addToFunctionSummaryMap(
        "execvp",
        Signature(ArgTypes{ConstCharPtrTy, CharPtrConstPtr}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int getopt(int argc, char * const argv[], const char *optstring);
    addToFunctionSummaryMap(
        "getopt",
        Signature(ArgTypes{IntTy, CharPtrConstPtr, ConstCharPtrTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(WithinRange, Range(-1, UCharRangeMax))},
                  ErrnoIrrelevant)
            .ArgConstraint(ArgumentCondition(0, WithinRange, Range(0, IntMax)))
            .ArgConstraint(NotNull(ArgNo(1)))
            .ArgConstraint(NotNull(ArgNo(2))));

    std::optional<QualType> StructSockaddrTy = lookupTy("sockaddr");
    std::optional<QualType> StructSockaddrPtrTy =
        getPointerTy(StructSockaddrTy);
    std::optional<QualType> ConstStructSockaddrPtrTy =
        getPointerTy(getConstTy(StructSockaddrTy));
    std::optional<QualType> StructSockaddrPtrRestrictTy =
        getRestrictTy(StructSockaddrPtrTy);
    std::optional<QualType> ConstStructSockaddrPtrRestrictTy =
        getRestrictTy(ConstStructSockaddrPtrTy);
    std::optional<QualType> Socklen_tTy = lookupTy("socklen_t");
    std::optional<QualType> Socklen_tPtrTy = getPointerTy(Socklen_tTy);
    std::optional<QualType> Socklen_tPtrRestrictTy =
        getRestrictTy(Socklen_tPtrTy);
    std::optional<RangeInt> Socklen_tMax = getMaxValue(Socklen_tTy);

    // In 'socket.h' of some libc implementations with C99, sockaddr parameter
    // is a transparent union of the underlying sockaddr_ family of pointers
    // instead of being a pointer to struct sockaddr. In these cases, the
    // standardized signature will not match, thus we try to match with another
    // signature that has the joker Irrelevant type. We also remove those
    // constraints which require pointer types for the sockaddr param.

    // int socket(int domain, int type, int protocol);
    addToFunctionSummaryMap(
        "socket", Signature(ArgTypes{IntTy, IntTy, IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsValidFileDescriptor, ErrnoMustNotBeChecked,
                  GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg));

    auto Accept =
        Summary(NoEvalCall)
            .Case(ReturnsValidFileDescriptor, ErrnoMustNotBeChecked,
                  GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ArgumentCondition(0, WithinRange, Range(0, IntMax)));
    if (!addToFunctionSummaryMap(
            "accept",
            // int accept(int socket, struct sockaddr *restrict address,
            //            socklen_t *restrict address_len);
            Signature(ArgTypes{IntTy, StructSockaddrPtrRestrictTy,
                               Socklen_tPtrRestrictTy},
                      RetType{IntTy}),
            Accept))
      addToFunctionSummaryMap(
          "accept",
          Signature(ArgTypes{IntTy, Irrelevant, Socklen_tPtrRestrictTy},
                    RetType{IntTy}),
          Accept);

    // int bind(int socket, const struct sockaddr *address, socklen_t
    //          address_len);
    if (!addToFunctionSummaryMap(
            "bind",
            Signature(ArgTypes{IntTy, ConstStructSockaddrPtrTy, Socklen_tTy},
                      RetType{IntTy}),
            Summary(NoEvalCall)
                .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
                .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
                .ArgConstraint(
                    ArgumentCondition(0, WithinRange, Range(0, IntMax)))
                .ArgConstraint(NotNull(ArgNo(1)))
                .ArgConstraint(
                    BufferSize(/*Buffer=*/ArgNo(1), /*BufSize=*/ArgNo(2)))
                .ArgConstraint(
                    ArgumentCondition(2, WithinRange, Range(0, Socklen_tMax)))))
      // Do not add constraints on sockaddr.
      addToFunctionSummaryMap(
          "bind",
          Signature(ArgTypes{IntTy, Irrelevant, Socklen_tTy}, RetType{IntTy}),
          Summary(NoEvalCall)
              .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
              .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
              .ArgConstraint(
                  ArgumentCondition(0, WithinRange, Range(0, IntMax)))
              .ArgConstraint(
                  ArgumentCondition(2, WithinRange, Range(0, Socklen_tMax))));

    // int getpeername(int socket, struct sockaddr *restrict address,
    //                 socklen_t *restrict address_len);
    if (!addToFunctionSummaryMap(
            "getpeername",
            Signature(ArgTypes{IntTy, StructSockaddrPtrRestrictTy,
                               Socklen_tPtrRestrictTy},
                      RetType{IntTy}),
            Summary(NoEvalCall)
                .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
                .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
                .ArgConstraint(
                    ArgumentCondition(0, WithinRange, Range(0, IntMax)))
                .ArgConstraint(NotNull(ArgNo(1)))
                .ArgConstraint(NotNull(ArgNo(2)))))
      addToFunctionSummaryMap(
          "getpeername",
          Signature(ArgTypes{IntTy, Irrelevant, Socklen_tPtrRestrictTy},
                    RetType{IntTy}),
          Summary(NoEvalCall)
              .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
              .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
              .ArgConstraint(
                  ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // int getsockname(int socket, struct sockaddr *restrict address,
    //                 socklen_t *restrict address_len);
    if (!addToFunctionSummaryMap(
            "getsockname",
            Signature(ArgTypes{IntTy, StructSockaddrPtrRestrictTy,
                               Socklen_tPtrRestrictTy},
                      RetType{IntTy}),
            Summary(NoEvalCall)
                .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
                .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
                .ArgConstraint(
                    ArgumentCondition(0, WithinRange, Range(0, IntMax)))
                .ArgConstraint(NotNull(ArgNo(1)))
                .ArgConstraint(NotNull(ArgNo(2)))))
      addToFunctionSummaryMap(
          "getsockname",
          Signature(ArgTypes{IntTy, Irrelevant, Socklen_tPtrRestrictTy},
                    RetType{IntTy}),
          Summary(NoEvalCall)
              .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
              .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
              .ArgConstraint(
                  ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // int connect(int socket, const struct sockaddr *address, socklen_t
    //             address_len);
    if (!addToFunctionSummaryMap(
            "connect",
            Signature(ArgTypes{IntTy, ConstStructSockaddrPtrTy, Socklen_tTy},
                      RetType{IntTy}),
            Summary(NoEvalCall)
                .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
                .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
                .ArgConstraint(
                    ArgumentCondition(0, WithinRange, Range(0, IntMax)))
                .ArgConstraint(NotNull(ArgNo(1)))))
      addToFunctionSummaryMap(
          "connect",
          Signature(ArgTypes{IntTy, Irrelevant, Socklen_tTy}, RetType{IntTy}),
          Summary(NoEvalCall)
              .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
              .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
              .ArgConstraint(
                  ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    auto Recvfrom =
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(LessThanOrEq, ArgNo(2)),
                   ReturnValueCondition(WithinRange, Range(1, Ssize_tMax))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({ReturnValueCondition(WithinRange, SingleValue(0)),
                   ArgumentCondition(2, WithinRange, SingleValue(0))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ArgumentCondition(0, WithinRange, Range(0, IntMax)))
            .ArgConstraint(BufferSize(/*Buffer=*/ArgNo(1),
                                      /*BufSize=*/ArgNo(2)));
    if (!addToFunctionSummaryMap(
            "recvfrom",
            // ssize_t recvfrom(int socket, void *restrict buffer,
            //                  size_t length,
            //                  int flags, struct sockaddr *restrict address,
            //                  socklen_t *restrict address_len);
            Signature(ArgTypes{IntTy, VoidPtrRestrictTy, SizeTy, IntTy,
                               StructSockaddrPtrRestrictTy,
                               Socklen_tPtrRestrictTy},
                      RetType{Ssize_tTy}),
            Recvfrom))
      addToFunctionSummaryMap(
          "recvfrom",
          Signature(ArgTypes{IntTy, VoidPtrRestrictTy, SizeTy, IntTy,
                             Irrelevant, Socklen_tPtrRestrictTy},
                    RetType{Ssize_tTy}),
          Recvfrom);

    auto Sendto =
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(LessThanOrEq, ArgNo(2)),
                   ReturnValueCondition(WithinRange, Range(1, Ssize_tMax))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({ReturnValueCondition(WithinRange, SingleValue(0)),
                   ArgumentCondition(2, WithinRange, SingleValue(0))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ArgumentCondition(0, WithinRange, Range(0, IntMax)))
            .ArgConstraint(BufferSize(/*Buffer=*/ArgNo(1),
                                      /*BufSize=*/ArgNo(2)));
    if (!addToFunctionSummaryMap(
            "sendto",
            // ssize_t sendto(int socket, const void *message, size_t length,
            //                int flags, const struct sockaddr *dest_addr,
            //                socklen_t dest_len);
            Signature(ArgTypes{IntTy, ConstVoidPtrTy, SizeTy, IntTy,
                               ConstStructSockaddrPtrTy, Socklen_tTy},
                      RetType{Ssize_tTy}),
            Sendto))
      addToFunctionSummaryMap(
          "sendto",
          Signature(ArgTypes{IntTy, ConstVoidPtrTy, SizeTy, IntTy, Irrelevant,
                             Socklen_tTy},
                    RetType{Ssize_tTy}),
          Sendto);

    // int listen(int sockfd, int backlog);
    addToFunctionSummaryMap(
        "listen", Signature(ArgTypes{IntTy, IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // ssize_t recv(int sockfd, void *buf, size_t len, int flags);
    addToFunctionSummaryMap(
        "recv",
        Signature(ArgTypes{IntTy, VoidPtrTy, SizeTy, IntTy},
                  RetType{Ssize_tTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(LessThanOrEq, ArgNo(2)),
                   ReturnValueCondition(WithinRange, Range(1, Ssize_tMax))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({ReturnValueCondition(WithinRange, SingleValue(0)),
                   ArgumentCondition(2, WithinRange, SingleValue(0))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ArgumentCondition(0, WithinRange, Range(0, IntMax)))
            .ArgConstraint(BufferSize(/*Buffer=*/ArgNo(1),
                                      /*BufSize=*/ArgNo(2))));

    std::optional<QualType> StructMsghdrTy = lookupTy("msghdr");
    std::optional<QualType> StructMsghdrPtrTy = getPointerTy(StructMsghdrTy);
    std::optional<QualType> ConstStructMsghdrPtrTy =
        getPointerTy(getConstTy(StructMsghdrTy));

    // ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
    addToFunctionSummaryMap(
        "recvmsg",
        Signature(ArgTypes{IntTy, StructMsghdrPtrTy, IntTy},
                  RetType{Ssize_tTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(WithinRange, Range(1, Ssize_tMax))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
    addToFunctionSummaryMap(
        "sendmsg",
        Signature(ArgTypes{IntTy, ConstStructMsghdrPtrTy, IntTy},
                  RetType{Ssize_tTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(WithinRange, Range(1, Ssize_tMax))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // int setsockopt(int socket, int level, int option_name,
    //                const void *option_value, socklen_t option_len);
    addToFunctionSummaryMap(
        "setsockopt",
        Signature(ArgTypes{IntTy, IntTy, IntTy, ConstVoidPtrTy, Socklen_tTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(3)))
            .ArgConstraint(
                BufferSize(/*Buffer=*/ArgNo(3), /*BufSize=*/ArgNo(4)))
            .ArgConstraint(
                ArgumentCondition(4, WithinRange, Range(0, Socklen_tMax))));

    // int getsockopt(int socket, int level, int option_name,
    //                void *restrict option_value,
    //                socklen_t *restrict option_len);
    addToFunctionSummaryMap(
        "getsockopt",
        Signature(ArgTypes{IntTy, IntTy, IntTy, VoidPtrRestrictTy,
                           Socklen_tPtrRestrictTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(3)))
            .ArgConstraint(NotNull(ArgNo(4))));

    // ssize_t send(int sockfd, const void *buf, size_t len, int flags);
    addToFunctionSummaryMap(
        "send",
        Signature(ArgTypes{IntTy, ConstVoidPtrTy, SizeTy, IntTy},
                  RetType{Ssize_tTy}),
        Summary(NoEvalCall)
            .Case({ReturnValueCondition(LessThanOrEq, ArgNo(2)),
                   ReturnValueCondition(WithinRange, Range(1, Ssize_tMax))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case({ReturnValueCondition(WithinRange, SingleValue(0)),
                   ArgumentCondition(2, WithinRange, SingleValue(0))},
                  ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(ArgumentCondition(0, WithinRange, Range(0, IntMax)))
            .ArgConstraint(BufferSize(/*Buffer=*/ArgNo(1),
                                      /*BufSize=*/ArgNo(2))));

    // int socketpair(int domain, int type, int protocol, int sv[2]);
    addToFunctionSummaryMap(
        "socketpair",
        Signature(ArgTypes{IntTy, IntTy, IntTy, IntPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(3))));

    // int shutdown(int socket, int how);
    addToFunctionSummaryMap(
        "shutdown", Signature(ArgTypes{IntTy, IntTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // int getnameinfo(const struct sockaddr *restrict sa, socklen_t salen,
    //                 char *restrict node, socklen_t nodelen,
    //                 char *restrict service,
    //                 socklen_t servicelen, int flags);
    //
    // This is defined in netdb.h. And contrary to 'socket.h', the sockaddr
    // parameter is never handled as a transparent union in netdb.h
    addToFunctionSummaryMap(
        "getnameinfo",
        Signature(ArgTypes{ConstStructSockaddrPtrRestrictTy, Socklen_tTy,
                           CharPtrRestrictTy, Socklen_tTy, CharPtrRestrictTy,
                           Socklen_tTy, IntTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .ArgConstraint(
                BufferSize(/*Buffer=*/ArgNo(0), /*BufSize=*/ArgNo(1)))
            .ArgConstraint(
                ArgumentCondition(1, WithinRange, Range(0, Socklen_tMax)))
            .ArgConstraint(
                BufferSize(/*Buffer=*/ArgNo(2), /*BufSize=*/ArgNo(3)))
            .ArgConstraint(
                ArgumentCondition(3, WithinRange, Range(0, Socklen_tMax)))
            .ArgConstraint(
                BufferSize(/*Buffer=*/ArgNo(4), /*BufSize=*/ArgNo(5)))
            .ArgConstraint(
                ArgumentCondition(5, WithinRange, Range(0, Socklen_tMax))));

    std::optional<QualType> StructUtimbufTy = lookupTy("utimbuf");
    std::optional<QualType> StructUtimbufPtrTy = getPointerTy(StructUtimbufTy);

    // int utime(const char *filename, struct utimbuf *buf);
    addToFunctionSummaryMap(
        "utime",
        Signature(ArgTypes{ConstCharPtrTy, StructUtimbufPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    std::optional<QualType> StructTimespecTy = lookupTy("timespec");
    std::optional<QualType> StructTimespecPtrTy =
        getPointerTy(StructTimespecTy);
    std::optional<QualType> ConstStructTimespecPtrTy =
        getPointerTy(getConstTy(StructTimespecTy));

    // int futimens(int fd, const struct timespec times[2]);
    addToFunctionSummaryMap(
        "futimens",
        Signature(ArgTypes{IntTy, ConstStructTimespecPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(
                ArgumentCondition(0, WithinRange, Range(0, IntMax))));

    // int utimensat(int dirfd, const char *pathname,
    //               const struct timespec times[2], int flags);
    addToFunctionSummaryMap(
        "utimensat",
        Signature(
            ArgTypes{IntTy, ConstCharPtrTy, ConstStructTimespecPtrTy, IntTy},
            RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(1))));

    std::optional<QualType> StructTimevalTy = lookupTy("timeval");
    std::optional<QualType> ConstStructTimevalPtrTy =
        getPointerTy(getConstTy(StructTimevalTy));

    // int utimes(const char *filename, const struct timeval times[2]);
    addToFunctionSummaryMap(
        "utimes",
        Signature(ArgTypes{ConstCharPtrTy, ConstStructTimevalPtrTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    // int nanosleep(const struct timespec *rqtp, struct timespec *rmtp);
    addToFunctionSummaryMap(
        "nanosleep",
        Signature(ArgTypes{ConstStructTimespecPtrTy, StructTimespecPtrTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(0))));

    std::optional<QualType> Time_tTy = lookupTy("time_t");
    std::optional<QualType> ConstTime_tPtrTy =
        getPointerTy(getConstTy(Time_tTy));
    std::optional<QualType> ConstTime_tPtrRestrictTy =
        getRestrictTy(ConstTime_tPtrTy);

    std::optional<QualType> StructTmTy = lookupTy("tm");
    std::optional<QualType> StructTmPtrTy = getPointerTy(StructTmTy);
    std::optional<QualType> StructTmPtrRestrictTy =
        getRestrictTy(StructTmPtrTy);
    std::optional<QualType> ConstStructTmPtrTy =
        getPointerTy(getConstTy(StructTmTy));
    std::optional<QualType> ConstStructTmPtrRestrictTy =
        getRestrictTy(ConstStructTmPtrTy);

    // struct tm * localtime(const time_t *tp);
    addToFunctionSummaryMap(
        "localtime",
        Signature(ArgTypes{ConstTime_tPtrTy}, RetType{StructTmPtrTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // struct tm *localtime_r(const time_t *restrict timer,
    //                        struct tm *restrict result);
    addToFunctionSummaryMap(
        "localtime_r",
        Signature(ArgTypes{ConstTime_tPtrRestrictTy, StructTmPtrRestrictTy},
                  RetType{StructTmPtrTy}),
        Summary(NoEvalCall)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // char *asctime_r(const struct tm *restrict tm, char *restrict buf);
    addToFunctionSummaryMap(
        "asctime_r",
        Signature(ArgTypes{ConstStructTmPtrRestrictTy, CharPtrRestrictTy},
                  RetType{CharPtrTy}),
        Summary(NoEvalCall)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1)))
            .ArgConstraint(BufferSize(/*Buffer=*/ArgNo(1),
                                      /*MinBufSize=*/BVF.getValue(26, IntTy))));

    // char *ctime_r(const time_t *timep, char *buf);
    addToFunctionSummaryMap(
        "ctime_r",
        Signature(ArgTypes{ConstTime_tPtrTy, CharPtrTy}, RetType{CharPtrTy}),
        Summary(NoEvalCall)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1)))
            .ArgConstraint(BufferSize(
                /*Buffer=*/ArgNo(1),
                /*MinBufSize=*/BVF.getValue(26, IntTy))));

    // struct tm *gmtime_r(const time_t *restrict timer,
    //                     struct tm *restrict result);
    addToFunctionSummaryMap(
        "gmtime_r",
        Signature(ArgTypes{ConstTime_tPtrRestrictTy, StructTmPtrRestrictTy},
                  RetType{StructTmPtrTy}),
        Summary(NoEvalCall)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // struct tm * gmtime(const time_t *tp);
    addToFunctionSummaryMap(
        "gmtime", Signature(ArgTypes{ConstTime_tPtrTy}, RetType{StructTmPtrTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    std::optional<QualType> Clockid_tTy = lookupTy("clockid_t");

    // int clock_gettime(clockid_t clock_id, struct timespec *tp);
    addToFunctionSummaryMap(
        "clock_gettime",
        Signature(ArgTypes{Clockid_tTy, StructTimespecPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(1))));

    std::optional<QualType> StructItimervalTy = lookupTy("itimerval");
    std::optional<QualType> StructItimervalPtrTy =
        getPointerTy(StructItimervalTy);

    // int getitimer(int which, struct itimerval *curr_value);
    addToFunctionSummaryMap(
        "getitimer",
        Signature(ArgTypes{IntTy, StructItimervalPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .Case(ReturnsZero, ErrnoMustNotBeChecked, GenericSuccessMsg)
            .Case(ReturnsMinusOne, ErrnoNEZeroIrrelevant, GenericFailureMsg)
            .ArgConstraint(NotNull(ArgNo(1))));

    std::optional<QualType> Pthread_cond_tTy = lookupTy("pthread_cond_t");
    std::optional<QualType> Pthread_cond_tPtrTy =
        getPointerTy(Pthread_cond_tTy);
    std::optional<QualType> Pthread_tTy = lookupTy("pthread_t");
    std::optional<QualType> Pthread_tPtrTy = getPointerTy(Pthread_tTy);
    std::optional<QualType> Pthread_tPtrRestrictTy =
        getRestrictTy(Pthread_tPtrTy);
    std::optional<QualType> Pthread_mutex_tTy = lookupTy("pthread_mutex_t");
    std::optional<QualType> Pthread_mutex_tPtrTy =
        getPointerTy(Pthread_mutex_tTy);
    std::optional<QualType> Pthread_mutex_tPtrRestrictTy =
        getRestrictTy(Pthread_mutex_tPtrTy);
    std::optional<QualType> Pthread_attr_tTy = lookupTy("pthread_attr_t");
    std::optional<QualType> Pthread_attr_tPtrTy =
        getPointerTy(Pthread_attr_tTy);
    std::optional<QualType> ConstPthread_attr_tPtrTy =
        getPointerTy(getConstTy(Pthread_attr_tTy));
    std::optional<QualType> ConstPthread_attr_tPtrRestrictTy =
        getRestrictTy(ConstPthread_attr_tPtrTy);
    std::optional<QualType> Pthread_mutexattr_tTy =
        lookupTy("pthread_mutexattr_t");
    std::optional<QualType> ConstPthread_mutexattr_tPtrTy =
        getPointerTy(getConstTy(Pthread_mutexattr_tTy));
    std::optional<QualType> ConstPthread_mutexattr_tPtrRestrictTy =
        getRestrictTy(ConstPthread_mutexattr_tPtrTy);

    QualType PthreadStartRoutineTy = getPointerTy(
        ACtx.getFunctionType(/*ResultTy=*/VoidPtrTy, /*Args=*/VoidPtrTy,
                             FunctionProtoType::ExtProtoInfo()));

    // int pthread_cond_signal(pthread_cond_t *cond);
    // int pthread_cond_broadcast(pthread_cond_t *cond);
    addToFunctionSummaryMap(
        {"pthread_cond_signal", "pthread_cond_broadcast"},
        Signature(ArgTypes{Pthread_cond_tPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // int pthread_create(pthread_t *restrict thread,
    //                    const pthread_attr_t *restrict attr,
    //                    void *(*start_routine)(void*), void *restrict arg);
    addToFunctionSummaryMap(
        "pthread_create",
        Signature(ArgTypes{Pthread_tPtrRestrictTy,
                           ConstPthread_attr_tPtrRestrictTy,
                           PthreadStartRoutineTy, VoidPtrRestrictTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(2))));

    // int pthread_attr_destroy(pthread_attr_t *attr);
    // int pthread_attr_init(pthread_attr_t *attr);
    addToFunctionSummaryMap(
        {"pthread_attr_destroy", "pthread_attr_init"},
        Signature(ArgTypes{Pthread_attr_tPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // int pthread_attr_getstacksize(const pthread_attr_t *restrict attr,
    //                               size_t *restrict stacksize);
    // int pthread_attr_getguardsize(const pthread_attr_t *restrict attr,
    //                               size_t *restrict guardsize);
    addToFunctionSummaryMap(
        {"pthread_attr_getstacksize", "pthread_attr_getguardsize"},
        Signature(ArgTypes{ConstPthread_attr_tPtrRestrictTy, SizePtrRestrictTy},
                  RetType{IntTy}),
        Summary(NoEvalCall)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));

    // int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
    // int pthread_attr_setguardsize(pthread_attr_t *attr, size_t guardsize);
    addToFunctionSummaryMap(
        {"pthread_attr_setstacksize", "pthread_attr_setguardsize"},
        Signature(ArgTypes{Pthread_attr_tPtrTy, SizeTy}, RetType{IntTy}),
        Summary(NoEvalCall)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(
                ArgumentCondition(1, WithinRange, Range(0, SizeMax))));

    // int pthread_mutex_init(pthread_mutex_t *restrict mutex, const
    //                        pthread_mutexattr_t *restrict attr);
    addToFunctionSummaryMap(
        "pthread_mutex_init",
        Signature(ArgTypes{Pthread_mutex_tPtrRestrictTy,
                           ConstPthread_mutexattr_tPtrRestrictTy},
                  RetType{IntTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));

    // int pthread_mutex_destroy(pthread_mutex_t *mutex);
    // int pthread_mutex_lock(pthread_mutex_t *mutex);
    // int pthread_mutex_trylock(pthread_mutex_t *mutex);
    // int pthread_mutex_unlock(pthread_mutex_t *mutex);
    addToFunctionSummaryMap(
        {"pthread_mutex_destroy", "pthread_mutex_lock", "pthread_mutex_trylock",
         "pthread_mutex_unlock"},
        Signature(ArgTypes{Pthread_mutex_tPtrTy}, RetType{IntTy}),
        Summary(NoEvalCall).ArgConstraint(NotNull(ArgNo(0))));
  }

  // Functions for testing.
  if (AddTestFunctions) {
    const RangeInt IntMin = BVF.getMinValue(IntTy).getLimitedValue();

    addToFunctionSummaryMap(
        "__not_null", Signature(ArgTypes{IntPtrTy}, RetType{IntTy}),
        Summary(EvalCallAsPure).ArgConstraint(NotNull(ArgNo(0))));

    addToFunctionSummaryMap(
        "__not_null_buffer",
        Signature(ArgTypes{VoidPtrTy, IntTy, IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(NotNullBuffer(ArgNo(0), ArgNo(1), ArgNo(2))));

    // Test inside range constraints.
    addToFunctionSummaryMap(
        "__single_val_0", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, WithinRange, SingleValue(0))));
    addToFunctionSummaryMap(
        "__single_val_1", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, WithinRange, SingleValue(1))));
    addToFunctionSummaryMap(
        "__range_1_2", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, WithinRange, Range(1, 2))));
    addToFunctionSummaryMap(
        "__range_m1_1", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, WithinRange, Range(-1, 1))));
    addToFunctionSummaryMap(
        "__range_m2_m1", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, WithinRange, Range(-2, -1))));
    addToFunctionSummaryMap(
        "__range_m10_10", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, WithinRange, Range(-10, 10))));
    addToFunctionSummaryMap("__range_m1_inf",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, WithinRange, Range(-1, IntMax))));
    addToFunctionSummaryMap("__range_0_inf",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, WithinRange, Range(0, IntMax))));
    addToFunctionSummaryMap("__range_1_inf",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, WithinRange, Range(1, IntMax))));
    addToFunctionSummaryMap("__range_minf_m1",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, WithinRange, Range(IntMin, -1))));
    addToFunctionSummaryMap("__range_minf_0",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, WithinRange, Range(IntMin, 0))));
    addToFunctionSummaryMap("__range_minf_1",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, WithinRange, Range(IntMin, 1))));
    addToFunctionSummaryMap("__range_1_2__4_6",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, WithinRange, Range({1, 2}, {4, 6}))));
    addToFunctionSummaryMap(
        "__range_1_2__4_inf", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, WithinRange,
                                             Range({1, 2}, {4, IntMax}))));

    // Test out of range constraints.
    addToFunctionSummaryMap(
        "__single_val_out_0", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, OutOfRange, SingleValue(0))));
    addToFunctionSummaryMap(
        "__single_val_out_1", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, OutOfRange, SingleValue(1))));
    addToFunctionSummaryMap(
        "__range_out_1_2", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, OutOfRange, Range(1, 2))));
    addToFunctionSummaryMap(
        "__range_out_m1_1", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, OutOfRange, Range(-1, 1))));
    addToFunctionSummaryMap(
        "__range_out_m2_m1", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, OutOfRange, Range(-2, -1))));
    addToFunctionSummaryMap(
        "__range_out_m10_10", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, OutOfRange, Range(-10, 10))));
    addToFunctionSummaryMap("__range_out_m1_inf",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, OutOfRange, Range(-1, IntMax))));
    addToFunctionSummaryMap("__range_out_0_inf",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, OutOfRange, Range(0, IntMax))));
    addToFunctionSummaryMap("__range_out_1_inf",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, OutOfRange, Range(1, IntMax))));
    addToFunctionSummaryMap("__range_out_minf_m1",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, OutOfRange, Range(IntMin, -1))));
    addToFunctionSummaryMap("__range_out_minf_0",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, OutOfRange, Range(IntMin, 0))));
    addToFunctionSummaryMap("__range_out_minf_1",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, OutOfRange, Range(IntMin, 1))));
    addToFunctionSummaryMap("__range_out_1_2__4_6",
                            Signature(ArgTypes{IntTy}, RetType{IntTy}),
                            Summary(EvalCallAsPure)
                                .ArgConstraint(ArgumentCondition(
                                    0U, OutOfRange, Range({1, 2}, {4, 6}))));
    addToFunctionSummaryMap(
        "__range_out_1_2__4_inf", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(
                ArgumentCondition(0U, OutOfRange, Range({1, 2}, {4, IntMax}))));

    // Test range kind.
    addToFunctionSummaryMap(
        "__within", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, WithinRange, SingleValue(1))));
    addToFunctionSummaryMap(
        "__out_of", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, OutOfRange, SingleValue(1))));

    addToFunctionSummaryMap(
        "__two_constrained_args",
        Signature(ArgTypes{IntTy, IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, WithinRange, SingleValue(1)))
            .ArgConstraint(ArgumentCondition(1U, WithinRange, SingleValue(1))));
    addToFunctionSummaryMap(
        "__arg_constrained_twice", Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(ArgumentCondition(0U, OutOfRange, SingleValue(1)))
            .ArgConstraint(ArgumentCondition(0U, OutOfRange, SingleValue(2))));
    addToFunctionSummaryMap(
        "__defaultparam",
        Signature(ArgTypes{Irrelevant, IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure).ArgConstraint(NotNull(ArgNo(0))));
    addToFunctionSummaryMap(
        "__variadic",
        Signature(ArgTypes{VoidPtrTy, ConstCharPtrTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(NotNull(ArgNo(0)))
            .ArgConstraint(NotNull(ArgNo(1))));
    addToFunctionSummaryMap(
        "__buf_size_arg_constraint",
        Signature(ArgTypes{ConstVoidPtrTy, SizeTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(
                BufferSize(/*Buffer=*/ArgNo(0), /*BufSize=*/ArgNo(1))));
    addToFunctionSummaryMap(
        "__buf_size_arg_constraint_mul",
        Signature(ArgTypes{ConstVoidPtrTy, SizeTy, SizeTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(BufferSize(/*Buffer=*/ArgNo(0), /*BufSize=*/ArgNo(1),
                                      /*BufSizeMultiplier=*/ArgNo(2))));
    addToFunctionSummaryMap(
        "__buf_size_arg_constraint_concrete",
        Signature(ArgTypes{ConstVoidPtrTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .ArgConstraint(BufferSize(/*Buffer=*/ArgNo(0),
                                      /*BufSize=*/BVF.getValue(10, IntTy))));
    addToFunctionSummaryMap(
        {"__test_restrict_param_0", "__test_restrict_param_1",
         "__test_restrict_param_2"},
        Signature(ArgTypes{VoidPtrRestrictTy}, RetType{VoidTy}),
        Summary(EvalCallAsPure));

    // Test the application of cases.
    addToFunctionSummaryMap(
        "__test_case_note", Signature(ArgTypes{}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .Case({ReturnValueCondition(WithinRange, SingleValue(0))},
                  ErrnoIrrelevant, "Function returns 0")
            .Case({ReturnValueCondition(WithinRange, SingleValue(1))},
                  ErrnoIrrelevant, "Function returns 1"));
    addToFunctionSummaryMap(
        "__test_case_range_1_2__4_6",
        Signature(ArgTypes{IntTy}, RetType{IntTy}),
        Summary(EvalCallAsPure)
            .Case({ArgumentCondition(0U, WithinRange,
                                     IntRangeVector{{IntMin, 0}, {3, 3}}),
                   ReturnValueCondition(WithinRange, SingleValue(1))},
                  ErrnoIrrelevant)
            .Case({ArgumentCondition(0U, WithinRange,
                                     IntRangeVector{{3, 3}, {7, IntMax}}),
                   ReturnValueCondition(WithinRange, SingleValue(2))},
                  ErrnoIrrelevant)
            .Case({ArgumentCondition(0U, WithinRange,
                                     IntRangeVector{{IntMin, 0}, {7, IntMax}}),
                   ReturnValueCondition(WithinRange, SingleValue(3))},
                  ErrnoIrrelevant)
            .Case({ArgumentCondition(
                       0U, WithinRange,
                       IntRangeVector{{IntMin, 0}, {3, 3}, {7, IntMax}}),
                   ReturnValueCondition(WithinRange, SingleValue(4))},
                  ErrnoIrrelevant));
  }
}

void ento::registerStdCLibraryFunctionsChecker(CheckerManager &mgr) {
  auto *Checker = mgr.registerChecker<StdLibraryFunctionsChecker>();
  Checker->CheckName = mgr.getCurrentCheckerName();
  const AnalyzerOptions &Opts = mgr.getAnalyzerOptions();
  Checker->DisplayLoadedSummaries =
      Opts.getCheckerBooleanOption(Checker, "DisplayLoadedSummaries");
  Checker->ModelPOSIX = Opts.getCheckerBooleanOption(Checker, "ModelPOSIX");
  Checker->ShouldAssumeControlledEnvironment =
      Opts.ShouldAssumeControlledEnvironment;
}

bool ento::shouldRegisterStdCLibraryFunctionsChecker(
    const CheckerManager &mgr) {
  return true;
}

void ento::registerStdCLibraryFunctionsTesterChecker(CheckerManager &mgr) {
  auto *Checker = mgr.getChecker<StdLibraryFunctionsChecker>();
  Checker->AddTestFunctions = true;
}

bool ento::shouldRegisterStdCLibraryFunctionsTesterChecker(
    const CheckerManager &mgr) {
  return true;
}

//== ArrayBoundCheckerV2.cpp ------------------------------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines ArrayBoundCheckerV2, which is a path-sensitive check
// which looks for an out-of-bound array element access.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/CharUnits.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Checkers/Taint.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace clang;
using namespace ento;
using namespace taint;
using llvm::formatv;

namespace {
/// If `E` is a "clean" array subscript expression, return the type of the
/// accessed element. If the base of the subscript expression is modified by
/// pointer arithmetic (and not the beginning of a "full" memory region), this
/// always returns nullopt because that's the right (or the least bad) thing to
/// do for the diagnostic output that's relying on this.
static std::optional<QualType> determineElementType(const Expr *E,
                                                    const CheckerContext &C) {
  const auto *ASE = dyn_cast<ArraySubscriptExpr>(E);
  if (!ASE)
    return std::nullopt;

  const MemRegion *SubscriptBaseReg = C.getSVal(ASE->getBase()).getAsRegion();
  if (!SubscriptBaseReg)
    return std::nullopt;

  // The base of the subscript expression is affected by pointer arithmetics,
  // so we want to report byte offsets instead of indices.
  if (isa<ElementRegion>(SubscriptBaseReg->StripCasts()))
    return std::nullopt;

  return ASE->getType();
}

static std::optional<int64_t>
determineElementSize(const std::optional<QualType> T, const CheckerContext &C) {
  if (!T)
    return std::nullopt;
  return C.getASTContext().getTypeSizeInChars(*T).getQuantity();
}

class StateUpdateReporter {
  const SubRegion *Reg;
  const NonLoc ByteOffsetVal;
  const std::optional<QualType> ElementType;
  const std::optional<int64_t> ElementSize;
  bool AssumedNonNegative = false;
  std::optional<NonLoc> AssumedUpperBound = std::nullopt;

public:
  StateUpdateReporter(const SubRegion *R, NonLoc ByteOffsVal, const Expr *E,
                      CheckerContext &C)
      : Reg(R), ByteOffsetVal(ByteOffsVal),
        ElementType(determineElementType(E, C)),
        ElementSize(determineElementSize(ElementType, C)) {}

  void recordNonNegativeAssumption() { AssumedNonNegative = true; }
  void recordUpperBoundAssumption(NonLoc UpperBoundVal) {
    AssumedUpperBound = UpperBoundVal;
  }

  bool assumedNonNegative() { return AssumedNonNegative; }

  const NoteTag *createNoteTag(CheckerContext &C) const;

private:
  std::string getMessage(PathSensitiveBugReport &BR) const;

  /// Return true if information about the value of `Sym` can put constraints
  /// on some symbol which is interesting within the bug report `BR`.
  /// In particular, this returns true when `Sym` is interesting within `BR`;
  /// but it also returns true if `Sym` is an expression that contains integer
  /// constants and a single symbolic operand which is interesting (in `BR`).
  /// We need to use this instead of plain `BR.isInteresting()` because if we
  /// are analyzing code like
  ///   int array[10];
  ///   int f(int arg) {
  ///     return array[arg] && array[arg + 10];
  ///   }
  /// then the byte offsets are `arg * 4` and `(arg + 10) * 4`, which are not
  /// sub-expressions of each other (but `getSimplifiedOffsets` is smart enough
  /// to detect this out of bounds access).
  static bool providesInformationAboutInteresting(SymbolRef Sym,
                                                  PathSensitiveBugReport &BR);
  static bool providesInformationAboutInteresting(SVal SV,
                                                  PathSensitiveBugReport &BR) {
    return providesInformationAboutInteresting(SV.getAsSymbol(), BR);
  }
};

struct Messages {
  std::string Short, Full;
};

// NOTE: The `ArraySubscriptExpr` and `UnaryOperator` callbacks are `PostStmt`
// instead of `PreStmt` because the current implementation passes the whole
// expression to `CheckerContext::getSVal()` which only works after the
// symbolic evaluation of the expression. (To turn them into `PreStmt`
// callbacks, we'd need to duplicate the logic that evaluates these
// expressions.) The `MemberExpr` callback would work as `PreStmt` but it's
// defined as `PostStmt` for the sake of consistency with the other callbacks.
class ArrayBoundCheckerV2 : public Checker<check::PostStmt<ArraySubscriptExpr>,
                                           check::PostStmt<UnaryOperator>,
                                           check::PostStmt<MemberExpr>> {
  BugType BT{this, "Out-of-bound access"};
  BugType TaintBT{this, "Out-of-bound access", categories::TaintedData};

  void performCheck(const Expr *E, CheckerContext &C) const;

  void reportOOB(CheckerContext &C, ProgramStateRef ErrorState, Messages Msgs,
                 NonLoc Offset, std::optional<NonLoc> Extent,
                 bool IsTaintBug = false) const;

  static void markPartsInteresting(PathSensitiveBugReport &BR,
                                   ProgramStateRef ErrorState, NonLoc Val,
                                   bool MarkTaint);

  static bool isFromCtypeMacro(const Stmt *S, ASTContext &AC);

  static bool isIdiomaticPastTheEndPtr(const Expr *E, ProgramStateRef State,
                                       NonLoc Offset, NonLoc Limit,
                                       CheckerContext &C);
  static bool isInAddressOf(const Stmt *S, ASTContext &AC);

public:
  void checkPostStmt(const ArraySubscriptExpr *E, CheckerContext &C) const {
    performCheck(E, C);
  }
  void checkPostStmt(const UnaryOperator *E, CheckerContext &C) const {
    if (E->getOpcode() == UO_Deref)
      performCheck(E, C);
  }
  void checkPostStmt(const MemberExpr *E, CheckerContext &C) const {
    if (E->isArrow())
      performCheck(E->getBase(), C);
  }
};

} // anonymous namespace

/// For a given Location that can be represented as a symbolic expression
/// Arr[Idx] (or perhaps Arr[Idx1][Idx2] etc.), return the parent memory block
/// Arr and the distance of Location from the beginning of Arr (expressed in a
/// NonLoc that specifies the number of CharUnits). Returns nullopt when these
/// cannot be determined.
static std::optional<std::pair<const SubRegion *, NonLoc>>
computeOffset(ProgramStateRef State, SValBuilder &SVB, SVal Location) {
  QualType T = SVB.getArrayIndexType();
  auto EvalBinOp = [&SVB, State, T](BinaryOperatorKind Op, NonLoc L, NonLoc R) {
    // We will use this utility to add and multiply values.
    return SVB.evalBinOpNN(State, Op, L, R, T).getAs<NonLoc>();
  };

  const SubRegion *OwnerRegion = nullptr;
  std::optional<NonLoc> Offset = SVB.makeZeroArrayIndex();

  const ElementRegion *CurRegion =
      dyn_cast_or_null<ElementRegion>(Location.getAsRegion());

  while (CurRegion) {
    const auto Index = CurRegion->getIndex().getAs<NonLoc>();
    if (!Index)
      return std::nullopt;

    QualType ElemType = CurRegion->getElementType();

    // FIXME: The following early return was presumably added to safeguard the
    // getTypeSizeInChars() call (which doesn't accept an incomplete type), but
    // it seems that `ElemType` cannot be incomplete at this point.
    if (ElemType->isIncompleteType())
      return std::nullopt;

    // Calculate Delta = Index * sizeof(ElemType).
    NonLoc Size = SVB.makeArrayIndex(
        SVB.getContext().getTypeSizeInChars(ElemType).getQuantity());
    auto Delta = EvalBinOp(BO_Mul, *Index, Size);
    if (!Delta)
      return std::nullopt;

    // Perform Offset += Delta.
    Offset = EvalBinOp(BO_Add, *Offset, *Delta);
    if (!Offset)
      return std::nullopt;

    OwnerRegion = CurRegion->getSuperRegion()->getAs<SubRegion>();
    // When this is just another ElementRegion layer, we need to continue the
    // offset calculations:
    CurRegion = dyn_cast_or_null<ElementRegion>(OwnerRegion);
  }

  if (OwnerRegion)
    return std::make_pair(OwnerRegion, *Offset);

  return std::nullopt;
}

// NOTE: This function is the "heart" of this checker. It simplifies
// inequalities with transformations that are valid (and very elementary) in
// pure mathematics, but become invalid if we use them in C++ number model
// where the calculations may overflow.
// Due to the overflow issues I think it's impossible (or at least not
// practical) to integrate this kind of simplification into the resolution of
// arbitrary inequalities (i.e. the code of `evalBinOp`); but this function
// produces valid results when the calculations are handling memory offsets
// and every value is well below SIZE_MAX.
// TODO: This algorithm should be moved to a central location where it's
// available for other checkers that need to compare memory offsets.
// NOTE: the simplification preserves the order of the two operands in a
// mathematical sense, but it may change the result produced by a C++
// comparison operator (and the automatic type conversions).
// For example, consider a comparison "X+1 < 0", where the LHS is stored as a
// size_t and the RHS is stored in an int. (As size_t is unsigned, this
// comparison is false for all values of "X".) However, the simplification may
// turn it into "X < -1", which is still always false in a mathematical sense,
// but can produce a true result when evaluated by `evalBinOp` (which follows
// the rules of C++ and casts -1 to SIZE_MAX).
static std::pair<NonLoc, nonloc::ConcreteInt>
getSimplifiedOffsets(NonLoc offset, nonloc::ConcreteInt extent,
                     SValBuilder &svalBuilder) {
  std::optional<nonloc::SymbolVal> SymVal = offset.getAs<nonloc::SymbolVal>();
  if (SymVal && SymVal->isExpression()) {
    if (const SymIntExpr *SIE = dyn_cast<SymIntExpr>(SymVal->getSymbol())) {
      llvm::APSInt constant =
          APSIntType(extent.getValue()).convert(SIE->getRHS());
      switch (SIE->getOpcode()) {
      case BO_Mul:
        // The constant should never be 0 here, becasue multiplication by zero
        // is simplified by the engine.
        if ((extent.getValue() % constant) != 0)
          return std::pair<NonLoc, nonloc::ConcreteInt>(offset, extent);
        else
          return getSimplifiedOffsets(
              nonloc::SymbolVal(SIE->getLHS()),
              svalBuilder.makeIntVal(extent.getValue() / constant),
              svalBuilder);
      case BO_Add:
        return getSimplifiedOffsets(
            nonloc::SymbolVal(SIE->getLHS()),
            svalBuilder.makeIntVal(extent.getValue() - constant), svalBuilder);
      default:
        break;
      }
    }
  }

  return std::pair<NonLoc, nonloc::ConcreteInt>(offset, extent);
}

static bool isNegative(SValBuilder &SVB, ProgramStateRef State, NonLoc Value) {
  const llvm::APSInt *MaxV = SVB.getMaxValue(State, Value);
  return MaxV && MaxV->isNegative();
}

static bool isUnsigned(SValBuilder &SVB, NonLoc Value) {
  QualType T = Value.getType(SVB.getContext());
  return T->isUnsignedIntegerType();
}

// Evaluate the comparison Value < Threshold with the help of the custom
// simplification algorithm defined for this checker. Return a pair of states,
// where the first one corresponds to "value below threshold" and the second
// corresponds to "value at or above threshold". Returns {nullptr, nullptr} in
// the case when the evaluation fails.
// If the optional argument CheckEquality is true, then use BO_EQ instead of
// the default BO_LT after consistently applying the same simplification steps.
static std::pair<ProgramStateRef, ProgramStateRef>
compareValueToThreshold(ProgramStateRef State, NonLoc Value, NonLoc Threshold,
                        SValBuilder &SVB, bool CheckEquality = false) {
  if (auto ConcreteThreshold = Threshold.getAs<nonloc::ConcreteInt>()) {
    std::tie(Value, Threshold) = getSimplifiedOffsets(Value, *ConcreteThreshold, SVB);
  }

  // We want to perform a _mathematical_ comparison between the numbers `Value`
  // and `Threshold`; but `evalBinOpNN` evaluates a C/C++ operator that may
  // perform automatic conversions. For example the number -1 is less than the
  // number 1000, but -1 < `1000ull` will evaluate to `false` because the `int`
  // -1 is converted to ULONGLONG_MAX.
  // To avoid automatic conversions, we evaluate the "obvious" cases without
  // calling `evalBinOpNN`:
  if (isNegative(SVB, State, Value) && isUnsigned(SVB, Threshold)) {
    if (CheckEquality) {
      // negative_value == unsigned_threshold is always false
      return {nullptr, State};
    }
    // negative_value < unsigned_threshold is always true
    return {State, nullptr};
  }
  if (isUnsigned(SVB, Value) && isNegative(SVB, State, Threshold)) {
    // unsigned_value == negative_threshold and
    // unsigned_value < negative_threshold are both always false
    return {nullptr, State};
  }
  // FIXME: These special cases are sufficient for handling real-world
  // comparisons, but in theory there could be contrived situations where
  // automatic conversion of a symbolic value (which can be negative and can be
  // positive) leads to incorrect results.
  // NOTE: We NEED to use the `evalBinOpNN` call in the "common" case, because
  // we want to ensure that assumptions coming from this precondition and
  // assumptions coming from regular C/C++ operator calls are represented by
  // constraints on the same symbolic expression. A solution that would
  // evaluate these "mathematical" compariosns through a separate pathway would
  // be a step backwards in this sense.

  const BinaryOperatorKind OpKind = CheckEquality ? BO_EQ : BO_LT;
  auto BelowThreshold =
      SVB.evalBinOpNN(State, OpKind, Value, Threshold, SVB.getConditionType())
          .getAs<NonLoc>();

  if (BelowThreshold)
    return State->assume(*BelowThreshold);

  return {nullptr, nullptr};
}

static std::string getRegionName(const SubRegion *Region) {
  if (std::string RegName = Region->getDescriptiveName(); !RegName.empty())
    return RegName;

  // Field regions only have descriptive names when their parent has a
  // descriptive name; so we provide a fallback representation for them:
  if (const auto *FR = Region->getAs<FieldRegion>()) {
    if (StringRef Name = FR->getDecl()->getName(); !Name.empty())
      return formatv("the field '{0}'", Name);
    return "the unnamed field";
  }

  if (isa<AllocaRegion>(Region))
    return "the memory returned by 'alloca'";

  if (isa<SymbolicRegion>(Region) &&
      isa<HeapSpaceRegion>(Region->getMemorySpace()))
    return "the heap area";

  if (isa<StringRegion>(Region))
    return "the string literal";

  return "the region";
}

static std::optional<int64_t> getConcreteValue(NonLoc SV) {
  if (auto ConcreteVal = SV.getAs<nonloc::ConcreteInt>()) {
    return ConcreteVal->getValue().tryExtValue();
  }
  return std::nullopt;
}

static std::optional<int64_t> getConcreteValue(std::optional<NonLoc> SV) {
  return SV ? getConcreteValue(*SV) : std::nullopt;
}

static Messages getPrecedesMsgs(const SubRegion *Region, NonLoc Offset) {
  std::string RegName = getRegionName(Region), OffsetStr = "";

  if (auto ConcreteOffset = getConcreteValue(Offset))
    OffsetStr = formatv(" {0}", ConcreteOffset);

  return {
      formatv("Out of bound access to memory preceding {0}", RegName),
      formatv("Access of {0} at negative byte offset{1}", RegName, OffsetStr)};
}

/// Try to divide `Val1` and `Val2` (in place) by `Divisor` and return true if
/// it can be performed (`Divisor` is nonzero and there is no remainder). The
/// values `Val1` and `Val2` may be nullopt and in that case the corresponding
/// division is considered to be successful.
static bool tryDividePair(std::optional<int64_t> &Val1,
                          std::optional<int64_t> &Val2, int64_t Divisor) {
  if (!Divisor)
    return false;
  const bool Val1HasRemainder = Val1 && *Val1 % Divisor;
  const bool Val2HasRemainder = Val2 && *Val2 % Divisor;
  if (!Val1HasRemainder && !Val2HasRemainder) {
    if (Val1)
      *Val1 /= Divisor;
    if (Val2)
      *Val2 /= Divisor;
    return true;
  }
  return false;
}

static Messages getExceedsMsgs(ASTContext &ACtx, const SubRegion *Region,
                               NonLoc Offset, NonLoc Extent, SVal Location,
                               bool AlsoMentionUnderflow) {
  std::string RegName = getRegionName(Region);
  const auto *EReg = Location.getAsRegion()->getAs<ElementRegion>();
  assert(EReg && "this checker only handles element access");
  QualType ElemType = EReg->getElementType();

  std::optional<int64_t> OffsetN = getConcreteValue(Offset);
  std::optional<int64_t> ExtentN = getConcreteValue(Extent);

  int64_t ElemSize = ACtx.getTypeSizeInChars(ElemType).getQuantity();

  bool UseByteOffsets = !tryDividePair(OffsetN, ExtentN, ElemSize);
  const char *OffsetOrIndex = UseByteOffsets ? "byte offset" : "index";

  SmallString<256> Buf;
  llvm::raw_svector_ostream Out(Buf);
  Out << "Access of ";
  if (!ExtentN && !UseByteOffsets)
    Out << "'" << ElemType.getAsString() << "' element in ";
  Out << RegName << " at ";
  if (AlsoMentionUnderflow) {
    Out << "a negative or overflowing " << OffsetOrIndex;
  } else if (OffsetN) {
    Out << OffsetOrIndex << " " << *OffsetN;
  } else {
    Out << "an overflowing " << OffsetOrIndex;
  }
  if (ExtentN) {
    Out << ", while it holds only ";
    if (*ExtentN != 1)
      Out << *ExtentN;
    else
      Out << "a single";
    if (UseByteOffsets)
      Out << " byte";
    else
      Out << " '" << ElemType.getAsString() << "' element";

    if (*ExtentN > 1)
      Out << "s";
  }

  return {formatv("Out of bound access to memory {0} {1}",
                  AlsoMentionUnderflow ? "around" : "after the end of",
                  RegName),
          std::string(Buf)};
}

static Messages getTaintMsgs(const SubRegion *Region, const char *OffsetName,
                             bool AlsoMentionUnderflow) {
  std::string RegName = getRegionName(Region);
  return {formatv("Potential out of bound access to {0} with tainted {1}",
                  RegName, OffsetName),
          formatv("Access of {0} with a tainted {1} that may be {2}too large",
                  RegName, OffsetName,
                  AlsoMentionUnderflow ? "negative or " : "")};
}

const NoteTag *StateUpdateReporter::createNoteTag(CheckerContext &C) const {
  // Don't create a note tag if we didn't assume anything:
  if (!AssumedNonNegative && !AssumedUpperBound)
    return nullptr;

  return C.getNoteTag([*this](PathSensitiveBugReport &BR) -> std::string {
    return getMessage(BR);
  });
}

std::string StateUpdateReporter::getMessage(PathSensitiveBugReport &BR) const {
  bool ShouldReportNonNegative = AssumedNonNegative;
  if (!providesInformationAboutInteresting(ByteOffsetVal, BR)) {
    if (AssumedUpperBound &&
        providesInformationAboutInteresting(*AssumedUpperBound, BR)) {
      // Even if the byte offset isn't interesting (e.g. it's a constant value),
      // the assumption can still be interesting if it provides information
      // about an interesting symbolic upper bound.
      ShouldReportNonNegative = false;
    } else {
      // We don't have anything interesting, don't report the assumption.
      return "";
    }
  }

  std::optional<int64_t> OffsetN = getConcreteValue(ByteOffsetVal);
  std::optional<int64_t> ExtentN = getConcreteValue(AssumedUpperBound);

  const bool UseIndex =
      ElementSize && tryDividePair(OffsetN, ExtentN, *ElementSize);

  SmallString<256> Buf;
  llvm::raw_svector_ostream Out(Buf);
  Out << "Assuming ";
  if (UseIndex) {
    Out << "index ";
    if (OffsetN)
      Out << "'" << OffsetN << "' ";
  } else if (AssumedUpperBound) {
    Out << "byte offset ";
    if (OffsetN)
      Out << "'" << OffsetN << "' ";
  } else {
    Out << "offset ";
  }

  Out << "is";
  if (ShouldReportNonNegative) {
    Out << " non-negative";
  }
  if (AssumedUpperBound) {
    if (ShouldReportNonNegative)
      Out << " and";
    Out << " less than ";
    if (ExtentN)
      Out << *ExtentN << ", ";
    if (UseIndex && ElementType)
      Out << "the number of '" << ElementType->getAsString()
          << "' elements in ";
    else
      Out << "the extent of ";
    Out << getRegionName(Reg);
  }
  return std::string(Out.str());
}

bool StateUpdateReporter::providesInformationAboutInteresting(
    SymbolRef Sym, PathSensitiveBugReport &BR) {
  if (!Sym)
    return false;
  for (SymbolRef PartSym : Sym->symbols()) {
    // The interestingess mark may appear on any layer as we're stripping off
    // the SymIntExpr, UnarySymExpr etc. layers...
    if (BR.isInteresting(PartSym))
      return true;
    // ...but if both sides of the expression are symbolic, then there is no
    // practical algorithm to produce separate constraints for the two
    // operands (from the single combined result).
    if (isa<SymSymExpr>(PartSym))
      return false;
  }
  return false;
}

void ArrayBoundCheckerV2::performCheck(const Expr *E, CheckerContext &C) const {
  const SVal Location = C.getSVal(E);

  // The header ctype.h (from e.g. glibc) implements the isXXXXX() macros as
  //   #define isXXXXX(arg) (LOOKUP_TABLE[arg] & BITMASK_FOR_XXXXX)
  // and incomplete analysis of these leads to false positives. As even
  // accurate reports would be confusing for the users, just disable reports
  // from these macros:
  if (isFromCtypeMacro(E, C.getASTContext()))
    return;

  ProgramStateRef State = C.getState();
  SValBuilder &SVB = C.getSValBuilder();

  const std::optional<std::pair<const SubRegion *, NonLoc>> &RawOffset =
      computeOffset(State, SVB, Location);

  if (!RawOffset)
    return;

  auto [Reg, ByteOffset] = *RawOffset;

  // The state updates will be reported as a single note tag, which will be
  // composed by this helper class.
  StateUpdateReporter SUR(Reg, ByteOffset, E, C);

  // CHECK LOWER BOUND
  const MemSpaceRegion *Space = Reg->getMemorySpace();
  if (!(isa<SymbolicRegion>(Reg) && isa<UnknownSpaceRegion>(Space))) {
    // A symbolic region in unknown space represents an unknown pointer that
    // may point into the middle of an array, so we don't look for underflows.
    // Both conditions are significant because we want to check underflows in
    // symbolic regions on the heap (which may be introduced by checkers like
    // MallocChecker that call SValBuilder::getConjuredHeapSymbolVal()) and
    // non-symbolic regions (e.g. a field subregion of a symbolic region) in
    // unknown space.
    auto [PrecedesLowerBound, WithinLowerBound] = compareValueToThreshold(
        State, ByteOffset, SVB.makeZeroArrayIndex(), SVB);

    if (PrecedesLowerBound) {
      // The offset may be invalid (negative)...
      if (!WithinLowerBound) {
        // ...and it cannot be valid (>= 0), so report an error.
        Messages Msgs = getPrecedesMsgs(Reg, ByteOffset);
        reportOOB(C, PrecedesLowerBound, Msgs, ByteOffset, std::nullopt);
        return;
      }
      // ...but it can be valid as well, so the checker will (optimistically)
      // assume that it's valid and mention this in the note tag.
      SUR.recordNonNegativeAssumption();
    }

    // Actually update the state. The "if" only fails in the extremely unlikely
    // case when compareValueToThreshold returns {nullptr, nullptr} becasue
    // evalBinOpNN fails to evaluate the less-than operator.
    if (WithinLowerBound)
      State = WithinLowerBound;
  }

  // CHECK UPPER BOUND
  DefinedOrUnknownSVal Size = getDynamicExtent(State, Reg, SVB);
  if (auto KnownSize = Size.getAs<NonLoc>()) {
    // In a situation where both underflow and overflow are possible (but the
    // index is either tainted or known to be invalid), the logic of this
    // checker will first assume that the offset is non-negative, and then
    // (with this additional assumption) it will detect an overflow error.
    // In this situation the warning message should mention both possibilities.
    bool AlsoMentionUnderflow = SUR.assumedNonNegative();

    auto [WithinUpperBound, ExceedsUpperBound] =
        compareValueToThreshold(State, ByteOffset, *KnownSize, SVB);

    if (ExceedsUpperBound) {
      // The offset may be invalid (>= Size)...
      if (!WithinUpperBound) {
        // ...and it cannot be within bounds, so report an error, unless we can
        // definitely determine that this is an idiomatic `&array[size]`
        // expression that calculates the past-the-end pointer.
        if (isIdiomaticPastTheEndPtr(E, ExceedsUpperBound, ByteOffset,
                                     *KnownSize, C)) {
          C.addTransition(ExceedsUpperBound, SUR.createNoteTag(C));
          return;
        }

        Messages Msgs =
            getExceedsMsgs(C.getASTContext(), Reg, ByteOffset, *KnownSize,
                           Location, AlsoMentionUnderflow);
        reportOOB(C, ExceedsUpperBound, Msgs, ByteOffset, KnownSize);
        return;
      }
      // ...and it can be valid as well...
      if (isTainted(State, ByteOffset)) {
        // ...but it's tainted, so report an error.

        // Diagnostic detail: saying "tainted offset" is always correct, but
        // the common case is that 'idx' is tainted in 'arr[idx]' and then it's
        // nicer to say "tainted index".
        const char *OffsetName = "offset";
        if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E))
          if (isTainted(State, ASE->getIdx(), C.getLocationContext()))
            OffsetName = "index";

        Messages Msgs = getTaintMsgs(Reg, OffsetName, AlsoMentionUnderflow);
        reportOOB(C, ExceedsUpperBound, Msgs, ByteOffset, KnownSize,
                  /*IsTaintBug=*/true);
        return;
      }
      // ...and it isn't tainted, so the checker will (optimistically) assume
      // that the offset is in bounds and mention this in the note tag.
      SUR.recordUpperBoundAssumption(*KnownSize);
    }

    // Actually update the state. The "if" only fails in the extremely unlikely
    // case when compareValueToThreshold returns {nullptr, nullptr} becasue
    // evalBinOpNN fails to evaluate the less-than operator.
    if (WithinUpperBound)
      State = WithinUpperBound;
  }

  // Add a transition, reporting the state updates that we accumulated.
  C.addTransition(State, SUR.createNoteTag(C));
}

void ArrayBoundCheckerV2::markPartsInteresting(PathSensitiveBugReport &BR,
                                               ProgramStateRef ErrorState,
                                               NonLoc Val, bool MarkTaint) {
  if (SymbolRef Sym = Val.getAsSymbol()) {
    // If the offset is a symbolic value, iterate over its "parts" with
    // `SymExpr::symbols()` and mark each of them as interesting.
    // For example, if the offset is `x*4 + y` then we put interestingness onto
    // the SymSymExpr `x*4 + y`, the SymIntExpr `x*4` and the two data symbols
    // `x` and `y`.
    for (SymbolRef PartSym : Sym->symbols())
      BR.markInteresting(PartSym);
  }

  if (MarkTaint) {
    // If the issue that we're reporting depends on the taintedness of the
    // offset, then put interestingness onto symbols that could be the origin
    // of the taint. Note that this may find symbols that did not appear in
    // `Sym->symbols()` (because they're only loosely connected to `Val`).
    for (SymbolRef Sym : getTaintedSymbols(ErrorState, Val))
      BR.markInteresting(Sym);
  }
}

void ArrayBoundCheckerV2::reportOOB(CheckerContext &C,
                                    ProgramStateRef ErrorState, Messages Msgs,
                                    NonLoc Offset, std::optional<NonLoc> Extent,
                                    bool IsTaintBug /*=false*/) const {

  ExplodedNode *ErrorNode = C.generateErrorNode(ErrorState);
  if (!ErrorNode)
    return;

  auto BR = std::make_unique<PathSensitiveBugReport>(
      IsTaintBug ? TaintBT : BT, Msgs.Short, Msgs.Full, ErrorNode);

  // FIXME: ideally we would just call trackExpressionValue() and that would
  // "do the right thing": mark the relevant symbols as interesting, track the
  // control dependencies and statements storing the relevant values and add
  // helpful diagnostic pieces. However, right now trackExpressionValue() is
  // a heap of unreliable heuristics, so it would cause several issues:
  // - Interestingness is not applied consistently, e.g. if `array[x+10]`
  //   causes an overflow, then `x` is not marked as interesting.
  // - We get irrelevant diagnostic pieces, e.g. in the code
  //   `int *p = (int*)malloc(2*sizeof(int)); p[3] = 0;`
  //   it places a "Storing uninitialized value" note on the `malloc` call
  //   (which is technically true, but irrelevant).
  // If trackExpressionValue() becomes reliable, it should be applied instead
  // of this custom markPartsInteresting().
  markPartsInteresting(*BR, ErrorState, Offset, IsTaintBug);
  if (Extent)
    markPartsInteresting(*BR, ErrorState, *Extent, IsTaintBug);

  C.emitReport(std::move(BR));
}

bool ArrayBoundCheckerV2::isFromCtypeMacro(const Stmt *S, ASTContext &ACtx) {
  SourceLocation Loc = S->getBeginLoc();
  if (!Loc.isMacroID())
    return false;

  StringRef MacroName = Lexer::getImmediateMacroName(
      Loc, ACtx.getSourceManager(), ACtx.getLangOpts());

  if (MacroName.size() < 7 || MacroName[0] != 'i' || MacroName[1] != 's')
    return false;

  return ((MacroName == "isalnum") || (MacroName == "isalpha") ||
          (MacroName == "isblank") || (MacroName == "isdigit") ||
          (MacroName == "isgraph") || (MacroName == "islower") ||
          (MacroName == "isnctrl") || (MacroName == "isprint") ||
          (MacroName == "ispunct") || (MacroName == "isspace") ||
          (MacroName == "isupper") || (MacroName == "isxdigit"));
}

bool ArrayBoundCheckerV2::isInAddressOf(const Stmt *S, ASTContext &ACtx) {
  ParentMapContext &ParentCtx = ACtx.getParentMapContext();
  do {
    const DynTypedNodeList Parents = ParentCtx.getParents(*S);
    if (Parents.empty())
      return false;
    S = Parents[0].get<Stmt>();
  } while (isa_and_nonnull<ParenExpr, ImplicitCastExpr>(S));
  const auto *UnaryOp = dyn_cast_or_null<UnaryOperator>(S);
  return UnaryOp && UnaryOp->getOpcode() == UO_AddrOf;
}

bool ArrayBoundCheckerV2::isIdiomaticPastTheEndPtr(const Expr *E,
                                                   ProgramStateRef State,
                                                   NonLoc Offset, NonLoc Limit,
                                                   CheckerContext &C) {
  if (isa<ArraySubscriptExpr>(E) && isInAddressOf(E, C.getASTContext())) {
    auto [EqualsToThreshold, NotEqualToThreshold] = compareValueToThreshold(
        State, Offset, Limit, C.getSValBuilder(), /*CheckEquality=*/true);
    return EqualsToThreshold && !NotEqualToThreshold;
  }
  return false;
}

void ento::registerArrayBoundCheckerV2(CheckerManager &mgr) {
  mgr.registerChecker<ArrayBoundCheckerV2>();
}

bool ento::shouldRegisterArrayBoundCheckerV2(const CheckerManager &mgr) {
  return true;
}
